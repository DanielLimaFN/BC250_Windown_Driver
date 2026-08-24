/*
 * psp.c — mailbox GPCOM do PSP (MP0) na BC-250.
 *
 * Por que isso existe: a porta direta de microcodigo da SDMA
 * (SDMA0_UCODE_ADDR/DATA) nao existe em silicio real — le 0xFFFFFFFF e recusa
 * escrita. O proprio kernel avisa em sdma_v5_0.c:
 *
 *     // emulation only, won't work on real chip
 *     // navi10 real chip need to use PSP to load firmware
 *
 * Medido nesta placa em 2026-08-19 (ver docs/RESUMO_E_PROXIMOS_PASSOS.md):
 * o F32 da SDMA0 tem o ponteiro de instrucao congelado e caminha uma
 * instrucao por pulso de HALT, sem nenhum desvio em 24 pulsos — RAM de
 * instrucao vazia. Sem microcodigo o anel nunca e consumido.
 *
 * O protocolo aqui e espelhado de psp_v11_0_8_ring_create e
 * psp_ring_cmd_submit do amdgpu, com as estruturas de psp_gfx_if.h.
 */

#include "bc250dbg.h"

/* Frame do anel (psp_gfx_rb_frame), 64 bytes:
 *   +0  cmd_buf_addr_lo
 *   +4  cmd_buf_addr_hi
 *   +8  cmd_buf_size
 *   +12 fence_addr_lo
 *   +16 fence_addr_hi
 *   +20 fence_value
 * O resto e zero. */
#define PSP_FRAME_CMD_LO    0
#define PSP_FRAME_CMD_HI    1
#define PSP_FRAME_CMD_SIZE  2
#define PSP_FRAME_FENCE_LO  3
#define PSP_FRAME_FENCE_HI  4
#define PSP_FRAME_FENCE_VAL 5

/* Buffer de comando (psp_gfx_cmd_resp):
 *   +0  buf_size
 *   +4  buf_version
 *   +8  cmd_id
 *   +28 inicio da union de comando
 *   +864 resposta (psp_gfx_resp) */
#define PSP_CMD_BUF_SIZE_DW    0
#define PSP_CMD_BUF_VERSION_DW 1
#define PSP_CMD_ID_DW          2
#define PSP_CMD_UNION_DW       (28 / 4)

/* psp_gfx_cmd_load_ip_fw dentro da union:
 *   +0 fw_phy_addr_lo  +4 fw_phy_addr_hi  +8 fw_size  +12 fw_type */
#define PSP_LOADFW_PA_LO_DW  0
#define PSP_LOADFW_PA_HI_DW  1
#define PSP_LOADFW_SIZE_DW   2
#define PSP_LOADFW_TYPE_DW   3

/* psp_gfx_resp:
 *   +0 status  +4 session_id  +8 fw_addr_lo  +12 fw_addr_hi  +16 tmr_size */
#define PSP_RESP_STATUS_DW   0
#define PSP_RESP_FWLO_DW     2
#define PSP_RESP_FWHI_DW     3
#define PSP_RESP_TMRSZ_DW    4

#define PSP_POLL_MS          500

/* ------------------------------------------------------------------------ */

static VOID BcPspStall(ULONG Microseconds)
{
    /* KeStallExecutionProcessor tem limite pratico por chamada; fatiamos. */
    while (Microseconds > 1000) {
        KeStallExecutionProcessor(1000);
        Microseconds -= 1000;
    }
    if (Microseconds > 0) {
        KeStallExecutionProcessor(Microseconds);
    }
}

/* O amdgpu espera por (valor & 0x8000FFFF) == 0x80000000: bit31 setado E os
 * 16 bits baixos limpos. Checar so o bit31 nao serve — ele fica setado do
 * comando anterior, e o poll passa na hora sem o PSP ter respondido nada.
 * Foi assim que um anel velho continuou registrado e o fence estourou. */
#define PSP_ACK_MASK   0x8000FFFFu
#define PSP_ACK_VALUE  0x80000000u

static NTSTATUS BcPspWaitAck(ULONG *OutReg)
{
    ULONG reg = 0, i;

    for (i = 0; i < PSP_POLL_MS; i++) {
        reg = BcRegReadRaw(BC250_PSP_C2PMSG_64);
        if ((reg & PSP_ACK_MASK) == PSP_ACK_VALUE) {
            if (OutReg) *OutReg = reg;
            return STATUS_SUCCESS;
        }
        BcPspStall(1000);
    }

    if (OutReg) *OutReg = reg;
    return STATUS_IO_TIMEOUT;
}

VOID BcPspReadStatus(BC250DBG_PSP_STATUS_IO *Out)
{
    if (Out == NULL) {
        return;
    }

    Out->C2pmsg64    = BcRegReadRaw(BC250_PSP_C2PMSG_64);
    Out->C2pmsg81    = BcRegReadRaw(BC250_PSP_C2PMSG_81);
    Out->C2pmsg67    = BcRegReadRaw(BC250_PSP_C2PMSG_67);
    Out->RingLo      = BcRegReadRaw(BC250_PSP_C2PMSG_69);
    Out->RingHi      = BcRegReadRaw(BC250_PSP_C2PMSG_70);
    Out->RingSizeReg = BcRegReadRaw(BC250_PSP_C2PMSG_71);
    Out->RingCreated = g_Dev.PspRingCreated ? 1u : 0u;
    Out->RingPhysical = (ULONGLONG)g_Dev.PspRingPa.QuadPart;
}

/* ------------------------------------------------------------------------ */
/* Criacao do anel — psp_v11_0_8_ring_create                                 */
/* ------------------------------------------------------------------------ */

NTSTATUS BcPspRingCreate(ULONGLONG *OutRingPa, ULONG *OutBefore, ULONG *OutAfter)
{
    PHYSICAL_ADDRESS low, high, boundary;
    ULONG reg, ringLo, ringHi, ringSize, wptr;
    NTSTATUS status;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }

    if (g_Dev.PspRingCreated) {
        if (OutRingPa) *OutRingPa = (ULONGLONG)g_Dev.PspRingPa.QuadPart;
        if (OutBefore) *OutBefore = BcRegReadRaw(BC250_PSP_C2PMSG_64);
        if (OutAfter)  *OutAfter  = BcRegReadRaw(BC250_PSP_C2PMSG_64);
        return STATUS_SUCCESS;
    }

    /* 1. O TOS precisa estar pronto (bit31 de C2PMSG_64). Se nao estiver, o
     *    PSP nao vai atender nada e nao adianta alocar coisa nenhuma. */
    reg = BcRegReadRaw(BC250_PSP_C2PMSG_64);
    if (OutBefore) *OutBefore = reg;
    if ((reg & BC250_PSP_TOS_READY) == 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "PSP: TOS nao esta pronto (C2PMSG_64=0x%08X, bit31 zerado)", reg);
        return STATUS_DEVICE_NOT_READY;
    }

    /* 2. Fail-closed: nunca destruir um ring que não foi criado por este
     * driver. No boot limpo endereço, tamanho e WPTR são zero. */
    wptr     = BcRegReadRaw(BC250_PSP_C2PMSG_67);
    ringLo   = BcRegReadRaw(BC250_PSP_C2PMSG_69);
    ringHi   = BcRegReadRaw(BC250_PSP_C2PMSG_70);
    ringSize = BcRegReadRaw(BC250_PSP_C2PMSG_71);
    if (wptr != 0 || ringLo != 0 || ringHi != 0 || ringSize != 0) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_PSP,
                  "PSP: preflight recusado; ring externo presente "
                  "(wptr=0x%X lo=0x%X hi=0x%X size=0x%X)",
                  wptr, ringLo, ringHi, ringSize);
        return STATUS_DEVICE_BUSY;
    }

    g_Dev.PspSnapshotC2pmsg64 = reg;
    g_Dev.PspSnapshotC2pmsg67 = wptr;
    g_Dev.PspSnapshotRingLo   = ringLo;
    g_Dev.PspSnapshotRingHi   = ringHi;
    g_Dev.PspSnapshotRingSize = ringSize;
    g_Dev.PspSnapshotValid    = TRUE;

    /* 3. Anel contiguo, nao-cacheado. Nao-cacheado importa: o PSP escreve o
     *    fence por DMA e a CPU precisa enxergar sem depender de invalidacao. */
    low.QuadPart      = 0;
    high.QuadPart     = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;

    g_Dev.PspRingVa = MmAllocateContiguousMemorySpecifyCache(
        BC250_PSP_RING_SIZE, low, high, boundary, MmNonCached);
    if (g_Dev.PspRingVa == NULL) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "PSP: falha ao alocar o anel (%u bytes)", BC250_PSP_RING_SIZE);
        g_Dev.PspSnapshotValid = FALSE;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_Dev.PspRingVa, BC250_PSP_RING_SIZE);
    g_Dev.PspRingPa = MmGetPhysicalAddress(g_Dev.PspRingVa);

    /* A partir daqui um rollback deve sempre destruir/desregistrar o ring
     * antes de liberar a memória, mesmo se o ACK de criação falhar. */
    g_Dev.PspRingCreated = TRUE;

    /* 4. Endereco e tamanho do anel. */
    BcRegWriteRaw(BC250_PSP_C2PMSG_69, (ULONG)(g_Dev.PspRingPa.QuadPart & 0xFFFFFFFFull));
    BcRegWriteRaw(BC250_PSP_C2PMSG_70, (ULONG)(g_Dev.PspRingPa.QuadPart >> 32));
    BcRegWriteRaw(BC250_PSP_C2PMSG_71, BC250_PSP_RING_SIZE);
    KeMemoryBarrier();

    /* 5. Comando de criacao: tipo do anel no campo alto. */
    BcRegWriteRaw(BC250_PSP_C2PMSG_64, BC250_PSP_RING_TYPE_KM << 16);

    /* Espera de 20 ms, como o amdgpu. */
    BcPspStall(20000);

    /* 6. ACK do PSP. */
    status = BcPspWaitAck(&reg);

    if (OutAfter) *OutAfter = reg;

    if (!NT_SUCCESS(status)) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "PSP: anel nao foi confirmado (C2PMSG_64=0x%08X apos %u ms)",
                  reg, PSP_POLL_MS);
        (VOID)BcPspFree(NULL);
        return STATUS_DEVICE_NOT_READY;
    }

    g_Dev.PspRingWptr    = 0;
    g_Dev.PspFenceValue  = 0;

    if (OutRingPa) *OutRingPa = (ULONGLONG)g_Dev.PspRingPa.QuadPart;

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "PSP: anel criado em PA 0x%llx, %u bytes, C2PMSG_64=0x%08X",
              (ULONGLONG)g_Dev.PspRingPa.QuadPart, BC250_PSP_RING_SIZE, reg);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------ */

static NTSTATUS BcPspEnsureBuffers(VOID)
{
    PHYSICAL_ADDRESS low, high, boundary;

    low.QuadPart      = 0;
    high.QuadPart     = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;

    if (g_Dev.PspCmdVa == NULL) {
        g_Dev.PspCmdVa = MmAllocateContiguousMemorySpecifyCache(
            BC250_PSP_CMD_BUF_SIZE, low, high, boundary, MmNonCached);
        if (g_Dev.PspCmdVa == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(g_Dev.PspCmdVa, BC250_PSP_CMD_BUF_SIZE);
        g_Dev.PspCmdPa = MmGetPhysicalAddress(g_Dev.PspCmdVa);
    }

    if (g_Dev.PspFenceVa == NULL) {
        g_Dev.PspFenceVa = MmAllocateContiguousMemorySpecifyCache(
            PAGE_SIZE, low, high, boundary, MmNonCached);
        if (g_Dev.PspFenceVa == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(g_Dev.PspFenceVa, PAGE_SIZE);
        g_Dev.PspFencePa = MmGetPhysicalAddress(g_Dev.PspFenceVa);
    }

    return STATUS_SUCCESS;
}

/* Monta o frame, chuta o WPTR e espera o fence. */
static NTSTATUS BcPspSubmit(ULONG *OutFenceReached)
{
    volatile ULONG *fence = (volatile ULONG *)g_Dev.PspFenceVa;
    volatile ULONG *frame;
    ULONG index, ringDwords, wptr, i;

    ringDwords = BC250_PSP_RING_SIZE / sizeof(ULONG);
    wptr       = g_Dev.PspRingWptr;
    index      = ++g_Dev.PspFenceValue;

    *fence = 0;
    KeMemoryBarrier();

    frame = (volatile ULONG *)((PUCHAR)g_Dev.PspRingVa + wptr * sizeof(ULONG));
    RtlZeroMemory((PVOID)frame, BC250_PSP_FRAME_BYTES);

    frame[PSP_FRAME_CMD_LO]    = (ULONG)(g_Dev.PspCmdPa.QuadPart & 0xFFFFFFFFull);
    frame[PSP_FRAME_CMD_HI]    = (ULONG)(g_Dev.PspCmdPa.QuadPart >> 32);
    frame[PSP_FRAME_CMD_SIZE]  = BC250_PSP_CMD_BUF_DECLARED;
    frame[PSP_FRAME_FENCE_LO]  = (ULONG)(g_Dev.PspFencePa.QuadPart & 0xFFFFFFFFull);
    frame[PSP_FRAME_FENCE_HI]  = (ULONG)(g_Dev.PspFencePa.QuadPart >> 32);
    frame[PSP_FRAME_FENCE_VAL] = index;
    KeMemoryBarrier();

    /* Um frame = 64 bytes = 16 dwords. */
    wptr = (wptr + BC250_PSP_FRAME_BYTES / sizeof(ULONG)) % ringDwords;
    g_Dev.PspRingWptr = wptr;

    BcRegWriteRaw(BC250_PSP_C2PMSG_67, wptr);

    for (i = 0; i < PSP_POLL_MS; i++) {
        if (*fence == index) {
            if (OutFenceReached) *OutFenceReached = 1;
            return STATUS_SUCCESS;
        }
        BcPspStall(1000);
    }

    if (OutFenceReached) *OutFenceReached = 0;
    return STATUS_IO_TIMEOUT;
}

/* ------------------------------------------------------------------------ */
/* GFX_CMD_ID_LOAD_IP_FW                                                     */
/* ------------------------------------------------------------------------ */

NTSTATUS BcPspLoadIpFw(ULONG FwType, const ULONG *Dwords, ULONG Count,
                       BC250DBG_PSP_LOAD_FW_IO *Out)
{
    PHYSICAL_ADDRESS low, high, boundary, stagePa;
    PVOID stageVa;
    ULONG *cmd, *ucmd, *resp;
    ULONG fwBytes, fenceReached = 0;
    NTSTATUS status;

    if (g_Dev.Mmio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) {
        return STATUS_ACCESS_DENIED;
    }
    if (Dwords == NULL || Count == 0 || Count > BC250DBG_PSP_MAX_FW_DWORDS) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!g_Dev.PspRingCreated) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "PSP: anel nao criado; rode 'psp init' antes");
        return STATUS_DEVICE_NOT_READY;
    }

    status = BcPspEnsureBuffers();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    fwBytes = Count * sizeof(ULONG);

    /* Buffer de staging: o PSP le por DMA, entao precisa ser fisicamente
     * contiguo e visivel pelo dispositivo. */
    low.QuadPart      = 0;
    high.QuadPart     = 0xFFFFFFFFFFull;
    boundary.QuadPart = 0;

    stageVa = MmAllocateContiguousMemorySpecifyCache(
        fwBytes, low, high, boundary, MmNonCached);
    if (stageVa == NULL) {
        BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_CORE,
                  "PSP: falha ao alocar staging de firmware (%u bytes)", fwBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(stageVa, Dwords, fwBytes);
    stagePa = MmGetPhysicalAddress(stageVa);
    KeMemoryBarrier();

    /* psp_gfx_cmd_resp */
    cmd = (ULONG *)g_Dev.PspCmdVa;
    RtlZeroMemory(cmd, BC250_PSP_CMD_BUF_SIZE);
    cmd[PSP_CMD_BUF_SIZE_DW]    = BC250_PSP_CMD_BUF_DECLARED;
    cmd[PSP_CMD_BUF_VERSION_DW] = 1;
    cmd[PSP_CMD_ID_DW]          = BC250_PSP_CMD_LOAD_IP_FW;

    ucmd = cmd + PSP_CMD_UNION_DW;
    ucmd[PSP_LOADFW_PA_LO_DW] = (ULONG)(stagePa.QuadPart & 0xFFFFFFFFull);
    ucmd[PSP_LOADFW_PA_HI_DW] = (ULONG)(stagePa.QuadPart >> 32);
    ucmd[PSP_LOADFW_SIZE_DW]  = fwBytes;
    ucmd[PSP_LOADFW_TYPE_DW]  = FwType;
    KeMemoryBarrier();

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
              "PSP: LOAD_IP_FW tipo %u, %u bytes, staging PA 0x%llx",
              FwType, fwBytes, (ULONGLONG)stagePa.QuadPart);

    status = BcPspSubmit(&fenceReached);

    if (Out) {
        Out->FenceReached = fenceReached;
        if (fenceReached) {
            /* O PSP pode levar um instante para publicar a resposta depois
             * de escrever o fence. */
            BcPspStall(1000);
            resp = (ULONG *)((PUCHAR)g_Dev.PspCmdVa + BC250_PSP_RESP_OFFSET);
            Out->RespStatus   = resp[PSP_RESP_STATUS_DW];
            Out->RespFwAddrLo = resp[PSP_RESP_FWLO_DW];
            Out->RespFwAddrHi = resp[PSP_RESP_FWHI_DW];
            Out->RespTmrSize  = resp[PSP_RESP_TMRSZ_DW];
        }
    }

    if (fenceReached) {
        MmFreeContiguousMemory(stageVa);
    } else {
        /* Timeout: o PSP pode ainda estar lendo o buffer por DMA. Liberar
         * aqui seria use-after-free visto pelo dispositivo. Vazamos de
         * proposito — o driver e de instrumentacao e o descarregamento
         * limpa tudo. */
        BcLogText(BC250DBG_LVL_WARN, BC250DBG_SUB_CORE,
                  "PSP: fence nao chegou; staging PA 0x%llx mantido alocado "
                  "(o PSP pode estar lendo por DMA)",
                  (ULONGLONG)stagePa.QuadPart);
    }

    if (NT_SUCCESS(status) && Out) {
        BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_CORE,
                  "PSP: LOAD_IP_FW tipo %u -> RespStatus=0x%08X%s",
                  FwType, Out->RespStatus,
                  (Out->RespStatus == BC250_PSP_ERR_UNKNOWN_CMD)
                      ? " (comando desconhecido para este SOS)" : "");
    }

    return status;
}

/* ------------------------------------------------------------------------ */
/* GFX_CMD_ID_SETUP_TMR (0x05)                                              */
/* ------------------------------------------------------------------------ */

#define A_GCMC_VM_FB_OFFSET 0x0A5AC

NTSTATUS BcPspSetupTmr(ULONG TmrSizeBytes, ULONG VramOffset,
                       BC250DBG_PSP_SETUP_TMR_IO *Out)
{
    ULONG *cmd, *ucmd, *resp;
    ULONG fenceReached = 0;
    ULONGLONG tmrGpuAddr, tmrPhysAddr;
    ULONG fbOffset;
    NTSTATUS status;

    if (g_Dev.Mmio == NULL) return STATUS_DEVICE_NOT_READY;
    if (g_Dev.ArmLevel < BC250DBG_ARM_WRITE) return STATUS_ACCESS_DENIED;
    if (!g_Dev.PspRingCreated) return STATUS_DEVICE_NOT_READY;

    status = BcPspEnsureBuffers();
    if (!NT_SUCCESS(status)) return status;

    if (TmrSizeBytes == 0) TmrSizeBytes = 0x400000; /* 4 MB padrao */
    if (VramOffset == 0)   VramOffset   = 0x0F800000; /* 248 MB na VRAM */

    fbOffset = BcRegReadRaw(A_GCMC_VM_FB_OFFSET); /* 0x450 */
    tmrGpuAddr  = BC250_VRAM_GPU_BASE + (ULONGLONG)VramOffset;
    tmrPhysAddr = ((ULONGLONG)fbOffset << 24) + (ULONGLONG)VramOffset;

    cmd = (ULONG *)g_Dev.PspCmdVa;
    RtlZeroMemory(cmd, BC250_PSP_CMD_BUF_SIZE);

    cmd[PSP_CMD_BUF_SIZE_DW]    = BC250_PSP_CMD_BUF_DECLARED; /* 0x400 */
    cmd[PSP_CMD_BUF_VERSION_DW] = 1;
    cmd[PSP_CMD_ID_DW]          = 0x05; /* GFX_CMD_ID_SETUP_TMR */

    ucmd = cmd + PSP_CMD_UNION_DW;
    ucmd[0] = (ULONG)(tmrGpuAddr & 0xFFFFFFFFull); /* buf_phy_addr_lo (GPU MC/VA) */
    ucmd[1] = (ULONG)(tmrGpuAddr >> 32);          /* buf_phy_addr_hi */
    ucmd[2] = TmrSizeBytes;                       /* buf_size */
    ucmd[3] = 0x2;                                /* tmr_flags (virt_phy_addr=1) */
    ucmd[4] = (ULONG)(tmrPhysAddr & 0xFFFFFFFFull);/* system_phy_addr_lo (CPU Physical) */
    ucmd[5] = (ULONG)(tmrPhysAddr >> 32);         /* system_phy_addr_hi */

    KeMemoryBarrier();

    status = BcPspSubmit(&fenceReached);

    resp = cmd + (864 / sizeof(ULONG));
    if (Out) {
        Out->Size         = sizeof(BC250DBG_PSP_SETUP_TMR_IO);
        Out->FenceReached = fenceReached;
        Out->RespStatus   = resp[PSP_RESP_STATUS_DW];
        Out->TmrGpuAddr   = tmrGpuAddr;
        Out->TmrPhysAddr  = tmrPhysAddr;
        Out->TmrSizeBytes = TmrSizeBytes;
        Out->VramOffset   = VramOffset;
    }

    BcLogText(BC250DBG_LVL_INFO, BC250DBG_SUB_PSP,
              "PSP: SETUP_TMR size 0x%X, GPU 0x%llX, PA 0x%llX -> RespStatus=0x%08X",
              TmrSizeBytes, tmrGpuAddr, tmrPhysAddr, (Out ? Out->RespStatus : 0));

    return status;
}

/* ------------------------------------------------------------------------ */

NTSTATUS BcPspFree(BC250DBG_PSP_ROLLBACK_IO *Out)
{
    ULONG ack = 0, mismatch = 0, restored = 0;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN hadSnapshot = g_Dev.PspSnapshotValid;

    if (Out != NULL) {
        RtlZeroMemory(Out, sizeof(*Out));
        Out->Size = sizeof(*Out);
        Out->SnapshotPresent = hadSnapshot ? 1u : 0u;
    }

    if (g_Dev.PspRingCreated || g_Dev.PspRingVa != NULL) {
        if (g_Dev.Mmio == NULL) {
            if (Out != NULL) Out->Status = BC250DBG_OPS_BADOP;
            return STATUS_DEVICE_NOT_READY;
        }

        BcRegWriteRaw(BC250_PSP_C2PMSG_64, BC250_PSP_CMD_DESTROY_GPCOM_RING);
        BcPspStall(20000);
        status = BcPspWaitAck(&ack);
        if (!NT_SUCCESS(status)) {
            /* Não liberar: o PSP pode continuar lendo esses buffers por DMA. */
            BcLogText(BC250DBG_LVL_ERROR, BC250DBG_SUB_PSP,
                      "PSP: destroy do ring sem ACK (C2PMSG_64=0x%08X); "
                      "buffers preservados para evitar DMA use-after-free", ack);
            if (Out != NULL) {
                Out->Status = BC250DBG_OPS_TIMEOUT;
                Out->DestroyAck = 0;
            }
            return status;
        }
        if (Out != NULL) Out->DestroyAck = 1;
    }

    if (hadSnapshot) {
        BcRegWriteRaw(BC250_PSP_C2PMSG_67, g_Dev.PspSnapshotC2pmsg67);
        BcRegWriteRaw(BC250_PSP_C2PMSG_69, g_Dev.PspSnapshotRingLo);
        BcRegWriteRaw(BC250_PSP_C2PMSG_70, g_Dev.PspSnapshotRingHi);
        BcRegWriteRaw(BC250_PSP_C2PMSG_71, g_Dev.PspSnapshotRingSize);
        restored = 4;
        if (BcRegReadRaw(BC250_PSP_C2PMSG_67) != g_Dev.PspSnapshotC2pmsg67) mismatch++;
        if (BcRegReadRaw(BC250_PSP_C2PMSG_69) != g_Dev.PspSnapshotRingLo) mismatch++;
        if (BcRegReadRaw(BC250_PSP_C2PMSG_70) != g_Dev.PspSnapshotRingHi) mismatch++;
        if (BcRegReadRaw(BC250_PSP_C2PMSG_71) != g_Dev.PspSnapshotRingSize) mismatch++;
        if ((ack & PSP_ACK_MASK) != PSP_ACK_VALUE) mismatch++;
    }

    if (g_Dev.PspRingVa != NULL) {
        MmFreeContiguousMemory(g_Dev.PspRingVa);
        g_Dev.PspRingVa = NULL;
    }
    if (g_Dev.PspCmdVa != NULL) {
        MmFreeContiguousMemory(g_Dev.PspCmdVa);
        g_Dev.PspCmdVa = NULL;
    }
    if (g_Dev.PspFenceVa != NULL) {
        MmFreeContiguousMemory(g_Dev.PspFenceVa);
        g_Dev.PspFenceVa = NULL;
    }
    g_Dev.PspRingPa.QuadPart  = 0;
    g_Dev.PspCmdPa.QuadPart   = 0;
    g_Dev.PspFencePa.QuadPart = 0;
    g_Dev.PspRingCreated      = FALSE;
    g_Dev.PspRingWptr         = 0;
    g_Dev.PspFenceValue       = 0;
    g_Dev.PspSnapshotValid    = FALSE;
    g_Dev.PspSnapshotC2pmsg64 = 0;
    g_Dev.PspSnapshotC2pmsg67 = 0;
    g_Dev.PspSnapshotRingLo   = 0;
    g_Dev.PspSnapshotRingHi   = 0;
    g_Dev.PspSnapshotRingSize = 0;

    if (Out != NULL) {
        Out->RegistersRestored = restored;
        Out->RegisterMismatches = mismatch;
        Out->Status = mismatch ? BC250DBG_OPS_MISMATCH : BC250DBG_OPS_OK;
    }
    BcLogText(mismatch ? BC250DBG_LVL_ERROR : BC250DBG_LVL_INFO,
              BC250DBG_SUB_PSP,
              "PSP rollback: snapshot=%u destroy_ack=%u regs=%u mismatch=%u",
              hadSnapshot ? 1u : 0u,
              (ack & PSP_ACK_MASK) == PSP_ACK_VALUE ? 1u : 0u,
              restored, mismatch);
    return mismatch ? STATUS_DEVICE_DATA_ERROR : STATUS_SUCCESS;
}
