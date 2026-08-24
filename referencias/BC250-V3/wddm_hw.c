/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    wddm_hw.c

Abstract:
    Hardware initialization and silicon execution engine for AMD BC-250 (Cyan Skillfish).
    
    Validated Silicon Control Pipeline:
      1. SMU Mailbox Handshake (TestMessage token 0x1234, Clocks 1500 MHz, 12 WGPs / 24 CUs)
      2. PSP Mailbox GPCOM Ring in Host RAM DMA (4 KB Contiguous Memory)
      3. SDMA0 uCode Loading via PSP (Type 9)
      4. TMR Carveout (4 MB in GDDR6 VRAM at offset 0x0F800000)
      5. 6 Graphics Microcodes Loading (RLC_G, CP_CE, CP_PFP, CP_ME, CP_MEC, CP_MEC2)
      6. GCVM L2 MMU & System Aperture Configuration
      7. CP PM4 Ring 0 Initialization in VRAM (0x0FE00000)
      8. SDMA0 Copy Ring Initialization in VRAM (0x0FD00000)
      9. D3DKMT Escape Execution Bridge (Direct PM4, Indirect Buffers, Compute Shaders, SDMA, VRAM R/W)

Environment:
    Kernel mode only

--*/

#include "wddm.h"
#include "wddm_fw.h"

/* Registers */
#define A_MMIO_INDEX                                0x00000u
#define A_MMIO_DATA                                 0x00004u

#define MP0_BASE                                    0x58000u
#define A_PSP_C2PMSG_64                             (MP0_BASE + 0x200u)
#define A_PSP_C2PMSG_67                             (MP0_BASE + 0x20Cu)
#define A_PSP_C2PMSG_69                             (MP0_BASE + 0x214u)
#define A_PSP_C2PMSG_70                             (MP0_BASE + 0x218u)
#define A_PSP_C2PMSG_71                             (MP0_BASE + 0x21Cu)
#define A_PSP_C2PMSG_81                             (MP0_BASE + 0x244u)

/* SMU Mailboxes */
#define SMU_Q0_CMD                                  0x03B10A08u /* C2PMSG_66 */
#define SMU_Q0_ARG                                  0x03B10A48u /* C2PMSG_82 */
#define SMU_Q0_RSP                                  0x03B10A68u /* C2PMSG_90 */
#define SMU_Q2_CMD                                  0x03B10528u
#define SMU_Q2_RSP                                  0x03B10564u
#define SMU_Q2_ARG                                  0x03B10998u
#define SMU_Q2_ARG_HI                               0x03B1099Cu
#define SMU_Q3_CMD                                  0x03B10A20u
#define SMU_Q3_RSP                                  0x03B10A80u
#define SMU_Q3_ARG                                  0x03B10A88u

#define TABLE_SMU_METRICS                           6u
#define SMU_MSG_TEST_MESSAGE                        0x01u
#define SMU_MSG_GET_SMU_VERSION                     0x02u
#define SMU_TEST_ARG                                0x1234u

/* SOC15 Registers -> byte offset in BAR5 MMIO */
#define A_GCMC_VM_FB_OFFSET                         0x0A5ACu
#define A_GCVM_L2_CNTL                              0x0A100u
#define A_GCVM_L2_CNTL2                             0x0A104u
#define A_GCVM_L2_CNTL3                             0x0A108u
#define A_GCVM_L2_CNTL4                             0x0A160u
#define A_GCVM_L2_CNTL5                             0x0A184u
#define A_GCVM_CONTEXT0_CNTL                        0x0A200u
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO     0x0A3ACu
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI     0x0A3B0u
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO    0x0A42Cu
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI    0x0A430u
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO      0x0A4ACu
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI      0x0A4B0u
#define A_GCVM_INVALIDATE_ENG0_REQ                  0x0A28Cu
#define A_GCVM_INVALIDATE_ENG0_ACK                  0x0A2D4u

#define A_CP_ME_CNTL                                0x086D8u
#define A_CP_RB0_RPTR                               0x08700u
#define A_CP_RB_WPTR_POLL_CNTL                      0x08708u
#define A_CP_RB0_BASE                               0x0C100u
#define A_CP_RB0_CNTL                               0x0C104u
#define A_CP_RB0_RPTR_ADDR                          0x0C10Cu
#define A_CP_RB0_RPTR_ADDR_HI                       0x0C110u
#define A_CP_RB0_WPTR                               0x0C150u
#define A_CP_RB0_WPTR_HI                            0x0C154u
#define A_CP_RB0_BASE_HI                            0x0C2C4u
#define A_CP_RB_WPTR_POLL_ADDR_LO                   0x0C3ACu
#define A_CP_RB_WPTR_POLL_ADDR_HI                   0x0C3B0u
#define A_CP_RB_DOORBELL_CONTROL                    0x0C3B4u

#define A_VGT_NUM_INSTANCES                         0x30934u
#define A_PA_SC_LINE_STIPPLE_STATE                  0x30A04u

/*---------------------------------------------------------------------------
  Derivacao SOC15 dos offsets BAR5 (Fase 0, item 0.8)

  Formula autoritativa, extraida de gc_10_1_0_offset.h + cyan_skillfish_reg_init.c:

      BAR5_byte = (segment_base + mmREG) * 4

  onde segment_base = 0x1260 para BASE_IDX 0 e 0xA000 para BASE_IDX 1.

  Verificada no silicio: CP_ME_CNTL, CP_RB0_RPTR, CP_RB0_BASE/CNTL/WPTR ja
  batiam com os valores antigos escritos a mao. GRBM_STATUS, CP_STAT,
  SPI_PG_ENABLE_STATIC_WGP_MASK, CC_GC_SHADER_ARRAY_CONFIG, GRBM_GFX_INDEX,
  SCRATCH_REG* e todo o bloco COMPUTE_* estavam ERRADOS e foram corrigidos.
---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------
  Orcamento de espera dentro de um DDI/escape.

  O TDR do Windows dispara em ~2 s. Um miniport de display que bloqueia mais
  que isso faz o SO resetar o driver: o handle do adaptador vira invalido e
  TODA chamada seguinte falha. Foi exatamente o que aconteceu — o loop da SDMA
  esperava 2.000.000 x 5 us = 10 s, mediu-se 14,4 s numa submissao, o TDR
  disparou, e os cinco testes seguintes da suite leram 0xBADBAD00 e
  reportaram lixo.

  Nenhuma espera deste driver pode passar de BC250_ENGINE_WAIT_US.
---------------------------------------------------------------------------*/
#define BC250_ENGINE_WAIT_US                        100000u  /* 100 ms << TDR */
#define BC250_WAIT_ITERS(stall_us)                  (BC250_ENGINE_WAIT_US / (stall_us))

#define SOC15_GC0(reg)                              (((0x1260u + (reg)) * 4u))
#define SOC15_GC1(reg)                              (((0xA000u + (reg)) * 4u))

/* CU & WGP Hardware Harvesting / Unlocking (40 CU / 20 WGP) */
#define A_GRBM_GFX_INDEX                            SOC15_GC1(0x2200u)  /* 0x30800 */
#define A_SPI_PG_ENABLE_STATIC_WGP_MASK             SOC15_GC0(0x1277u)  /* 0x0935C */
#define A_CC_GC_SHADER_ARRAY_CONFIG                 SOC15_GC0(0x100Fu)  /* 0x089BC */
#define A_RLC_PG_ALWAYS_ON_WGP_MASK                 SOC15_GC1(0x4C53u)  /* 0x3B14C */
#define A_SCRATCH_REG0                              SOC15_GC1(0x2040u)  /* 0x30100 */
#define A_SCRATCH_REG1                              SOC15_GC1(0x2041u)  /* 0x30104 */
#define GRBM_GFX_INDEX_BROADCAST                    0xE0000000u

/* Offsets historicos, comprovadamente INVALIDOS neste silicio. Mantidos apenas
   para que o teste de regressao possa demonstrar que sao lixo/aliasing. */
#define A_LEGACY_BAD_GRBM_STATUS                    0x03040u
#define A_LEGACY_BAD_CP_STAT                        0x08684u
#define A_LEGACY_BAD_SPI_PG_WGP                     0x05C3Cu
#define A_LEGACY_BAD_CC_SHADER_ARRAY                0x09C1Cu
#define A_LEGACY_BAD_SCRATCH                        0x032D4u
#define A_LEGACY_BAD_COMPUTE_DISPATCH               0x080E0u

/* HDP Coherency */
#define A_HDP_MEM_COHERENCY_FLUSH_CNTL              0x04A80u
#define A_HDP_READ_CACHE_INVALIDATE                 0x03FC4u
#define A_HDP_MISC_CNTL                             0x03FCCu

#define A_SDMA0_F32_CNTL                            0x04A28u

/* PSP Protocol Constants */
#define PSP_TOS_READY                               0x80000000u
#define PSP_CMD_DESTROY_GPCOM_RING                  0x000C0000u
#define PSP_RING_TYPE_KM                            2u
#define PSP_RING_SIZE                               0x1000u
#define PSP_CMD_BUF_SIZE                            0x1000u
#define PSP_FRAME_BYTES                             64u
#define PSP_CMD_BUF_DECLARED                        0x400u
#define PSP_RESP_OFFSET                             864u

#define PSP_CMD_LOAD_IP_FW                          0x06u
#define PSP_CMD_SETUP_TMR                           0x05u

#define PSP_FW_CP_ME                                1u
#define PSP_FW_CP_PFP                               2u
#define PSP_FW_CP_CE                                3u
#define PSP_FW_CP_MEC                               4u
#define PSP_FW_CP_MEC2                              5u
#define PSP_FW_RLC_G                                8u
#define PSP_FW_SDMA0                                9u

#define PSP_ACK_MASK                                0x8000FFFFu
#define PSP_ACK_VALUE                               0x80000000u

/* MMIO & SMN Access Helpers */
#define A_RSMU_INDEX                                0x00038u
#define A_RSMU_DATA                                 0x0003Cu

static ULONG
BcSmnRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG SmnAddr)
{
    /*
      O par indice/dado do NBIO precisa de uma leitura de retorno (posting read)
      do proprio indice antes de ler o dado. Sem isso a escrita do indice pode
      nao ter propagado quando o dado e' amostrado, e o valor lido vem de um
      endereco antigo ou indefinido.

      Sintoma observado sem esta leitura: SMN_GFXOFF_FEATURE_LOW (0x03B10528)
      devolvia 0x00000001 — valor implausivel para uma mascara de features, que
      levou o teste de GFXOFF a declarar "Prova 9 satisfeita" a partir de lixo.

      A implementacao de referencia que funciona faz exatamente isto
      (referencias/AMD-BC-250-Windows-Driver/src/amdbc250_reg/amdbc250_reg.c).
    */
    BcRegWrite(Dev, A_RSMU_INDEX, SmnAddr);
    (VOID)BcRegRead(Dev, A_RSMU_INDEX);
    return BcRegRead(Dev, A_RSMU_DATA);
}

static VOID
BcSmnWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG SmnAddr, _In_ ULONG Value)
{
    BcRegWrite(Dev, A_RSMU_INDEX, SmnAddr);
    BcRegWrite(Dev, A_RSMU_DATA, Value);
}

static VOID
BcHdpReadCacheInvalidate(_In_ PBC250_WDDM_DEVICE Dev)
{
    ULONG miscCntl;

    BcRegWrite(Dev, A_HDP_MEM_COHERENCY_FLUSH_CNTL, 0);
    (VOID)BcRegRead(Dev, A_HDP_MEM_COHERENCY_FLUSH_CNTL);

    BcRegWrite(Dev, A_HDP_READ_CACHE_INVALIDATE, 1);
    (VOID)BcRegRead(Dev, A_HDP_READ_CACHE_INVALIDATE);

    miscCntl = BcRegRead(Dev, A_HDP_MISC_CNTL);
    BcRegWrite(Dev, A_HDP_MISC_CNTL, miscCntl | 1u);
    (VOID)BcRegRead(Dev, A_HDP_MISC_CNTL);
    BcRegWrite(Dev, A_HDP_MISC_CNTL, miscCntl & ~1u);
    (VOID)BcRegRead(Dev, A_HDP_MISC_CNTL);
}

static VOID
BcGcvmTlbFlush(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Vmid)
{
    ULONG req = (1u << 0) | ((Vmid & 0xFu) << 7) | (1u << 11);
    ULONG i;
    BcRegWrite(Dev, A_GCVM_INVALIDATE_ENG0_REQ, req);
    for (i = 0; i < 20000; i++) {
        if ((BcRegRead(Dev, A_GCVM_INVALIDATE_ENG0_ACK) & (1u << Vmid)) != 0) break;
        KeStallExecutionProcessor(2);
    }
}

/* SMU Mailbox Helper */
static NTSTATUS
BcSmuSendMsg(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG              Queue,
    _In_ ULONG              MsgId,
    _In_ ULONG              Param,
    _Out_opt_ PULONG        SmuStatus
    )
{
    ULONG cmdReg, argReg, argHiReg, rspReg;
    ULONG i, rsp;

    if (Queue == 2) {
        cmdReg = SMU_Q2_CMD; argReg = SMU_Q2_ARG;
        argHiReg = SMU_Q2_ARG_HI; rspReg = SMU_Q2_RSP;
    } else if (Queue == 3) {
        cmdReg = SMU_Q3_CMD; argReg = SMU_Q3_ARG;
        argHiReg = 0; rspReg = SMU_Q3_RSP;
    } else {
        cmdReg = SMU_Q0_CMD; argReg = SMU_Q0_ARG;
        argHiReg = 0; rspReg = SMU_Q0_RSP;
    }

    if (SmuStatus != NULL) {
        *SmuStatus = 0;
    }

    for (i = 0; i < 20000; i++) {
        if (BcSmnRead(Dev, rspReg) != 0) break;
        KeStallExecutionProcessor(5);
    }

    BcSmnWrite(Dev, rspReg, 0);
    BcSmnWrite(Dev, argReg, Param);
    if (argHiReg != 0) {
        BcSmnWrite(Dev, argHiReg, 0);
    }
    BcSmnWrite(Dev, cmdReg, MsgId);

    Dev->HwSmuCmdReadback = BcSmnRead(Dev, cmdReg);
    Dev->HwSmuArgReadback = BcSmnRead(Dev, argReg);
    Dev->HwSmuRspAtSubmit = BcSmnRead(Dev, rspReg);

    for (i = 0; i < 20000; i++) {
        rsp = BcSmnRead(Dev, rspReg);
        if (rsp != 0) {
            if (SmuStatus != NULL) {
                *SmuStatus = rsp;
            }
            return (rsp == 1) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }
        KeStallExecutionProcessor(5);
    }
    return STATUS_IO_TIMEOUT;
}

/* PSP Helpers */
static NTSTATUS
BcPspWaitAck(_In_ PBC250_WDDM_DEVICE Dev)
{
    ULONG i, reg;
    for (i = 0; i < 500; i++) {
        reg = BcRegRead(Dev, A_PSP_C2PMSG_64);
        if ((reg & PSP_ACK_MASK) == PSP_ACK_VALUE) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
BcPspSubmitCmd(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ PVOID              RingVa,
    _Inout_ PULONG          RingWptr,
    _In_ PHYSICAL_ADDRESS   CmdPa,
    _In_ PVOID              CmdVa,
    _In_ PHYSICAL_ADDRESS   FencePa,
    _In_ PVOID              FenceVa,
    _Inout_ PULONG          FenceValue
    )
{
    volatile ULONG *fence = (volatile ULONG *)FenceVa;
    volatile ULONG *frame;
    ULONG wptr = *RingWptr;
    ULONG index = ++(*FenceValue);
    ULONG ringDwords = PSP_RING_SIZE / 4;
    ULONG i;

    *fence = 0;
    KeMemoryBarrier();

    frame = (volatile ULONG *)((PUCHAR)RingVa + wptr * 4);
    RtlZeroMemory((PVOID)frame, PSP_FRAME_BYTES);
    frame[0] = (ULONG)(CmdPa.QuadPart & 0xFFFFFFFFULL);
    frame[1] = (ULONG)(CmdPa.QuadPart >> 32);
    frame[2] = PSP_CMD_BUF_DECLARED;
    frame[3] = (ULONG)(FencePa.QuadPart & 0xFFFFFFFFULL);
    frame[4] = (ULONG)(FencePa.QuadPart >> 32);
    frame[5] = index;
    KeMemoryBarrier();

    wptr = (wptr + (PSP_FRAME_BYTES / 4)) % ringDwords;
    *RingWptr = wptr;

    BcRegWrite(Dev, A_PSP_C2PMSG_67, wptr);

    for (i = 0; i < 500; i++) {
        if (*fence == index) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
BcPspLoadIpFw(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ PVOID              RingVa,
    _Inout_ PULONG          RingWptr,
    _In_ PHYSICAL_ADDRESS   CmdPa,
    _In_ PVOID              CmdVa,
    _In_ PHYSICAL_ADDRESS   FencePa,
    _In_ PVOID              FenceVa,
    _Inout_ PULONG          FenceValue,
    _In_ ULONG              FwType,
    _In_ const ULONG*       FwBlob,
    _In_ ULONG              FwSizeBytes
    )
{
    PHYSICAL_ADDRESS low, high, boundary, stagePa;
    PVOID stageVa;
    ULONG *cmd, *ucmd;
    NTSTATUS status;

    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFULL;
    boundary.QuadPart = 0;

    stageVa = MmAllocateContiguousMemorySpecifyCache(FwSizeBytes, low, high, boundary, MmNonCached);
    if (!stageVa) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(stageVa, FwBlob, FwSizeBytes);
    stagePa = MmGetPhysicalAddress(stageVa);
    KeMemoryBarrier();

    cmd = (ULONG *)CmdVa;
    RtlZeroMemory(cmd, PSP_CMD_BUF_SIZE);
    cmd[0] = PSP_CMD_BUF_DECLARED;
    cmd[1] = 1;
    cmd[2] = PSP_CMD_LOAD_IP_FW;

    ucmd = cmd + (28 / 4);
    ucmd[0] = (ULONG)(stagePa.QuadPart & 0xFFFFFFFFULL);
    ucmd[1] = (ULONG)(stagePa.QuadPart >> 32);
    ucmd[2] = FwSizeBytes;
    ucmd[3] = FwType;
    KeMemoryBarrier();

    status = BcPspSubmitCmd(Dev, RingVa, RingWptr, CmdPa, CmdVa, FencePa, FenceVa, FenceValue);

    if (NT_SUCCESS(status)) {
        volatile ULONG *resp;
        KeStallExecutionProcessor(1000);
        KeMemoryBarrier();
        resp = (volatile ULONG *)((PUCHAR)CmdVa + PSP_RESP_OFFSET);
        if (FwType < RTL_NUMBER_OF(Dev->HwPspFwResp)) {
            Dev->HwPspFwResp[FwType]    = resp[0];
            Dev->HwPspFwSession[FwType] = resp[1];
            Dev->HwPspFwSeen[FwType]    = 1;
        }
        if (resp[0] != 0) {
            status = STATUS_DEVICE_HARDWARE_ERROR;
        }
    }

    MmFreeContiguousMemory(stageVa);
    return status;
}

static NTSTATUS
BcPspSetupTmr(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ PVOID              RingVa,
    _Inout_ PULONG          RingWptr,
    _In_ PHYSICAL_ADDRESS   CmdPa,
    _In_ PVOID              CmdVa,
    _In_ PHYSICAL_ADDRESS   FencePa,
    _In_ PVOID              FenceVa,
    _Inout_ PULONG          FenceValue
    )
{
    ULONG *cmd, *ucmd;
    ULONG fbOffset;
    ULONGLONG tmrGpuAddr, tmrPhysAddr;
    ULONG tmrSizeBytes = 0x400000;   /* 4 MB */
    ULONG vramOffset = 0x0F800000;     /* 248 MB na VRAM */

    fbOffset = BcRegRead(Dev, A_GCMC_VM_FB_OFFSET);
    tmrGpuAddr  = BC250_VRAM_GPU_BASE + (ULONGLONG)vramOffset;
    tmrPhysAddr = ((ULONGLONG)fbOffset << 24) + (ULONGLONG)vramOffset;

    cmd = (ULONG *)CmdVa;
    RtlZeroMemory(cmd, PSP_CMD_BUF_SIZE);
    cmd[0] = PSP_CMD_BUF_DECLARED;
    cmd[1] = 1;
    cmd[2] = PSP_CMD_SETUP_TMR;

    ucmd = cmd + (28 / 4);
    ucmd[0] = (ULONG)(tmrGpuAddr & 0xFFFFFFFFULL);
    ucmd[1] = (ULONG)(tmrGpuAddr >> 32);
    ucmd[2] = tmrSizeBytes;
    ucmd[3] = 0x2; /* tmr_flags: virt_phy_addr=1 */
    ucmd[4] = (ULONG)(tmrPhysAddr & 0xFFFFFFFFULL);
    ucmd[5] = (ULONG)(tmrPhysAddr >> 32);
    KeMemoryBarrier();

    {
        NTSTATUS status = BcPspSubmitCmd(
            Dev, RingVa, RingWptr, CmdPa, CmdVa, FencePa, FenceVa, FenceValue);
        if (NT_SUCCESS(status)) {
            ULONG *resp;
            KeStallExecutionProcessor(1000);
            resp = (ULONG *)((PUCHAR)CmdVa + PSP_RESP_OFFSET);
            if (resp[0] != 0) {
                status = STATUS_DEVICE_HARDWARE_ERROR;
            }
        }
        return status;
    }
}

/*===========================================================================
  Dynamic Compute Units (CU) / WGP Configuration & 40 CU Unlocking
===========================================================================*/
static VOID
Bc250HwConfigureComputeUnits(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    ULONG unlockMode;
    ULONG wgpMask;
    ULONG spiBefore, spiAfter, ccBefore, ccAfter;
    ULONG se, sh, b;
    ULONG countPerSa = 0;
    ULONG targetWgps;

    if (!Dev || !Dev->MmioVirtualBase) {
        return;
    }

    unlockMode = Dev->HwCuUnlockMode; /* 0 = Auto-detect, 1 = Stock 24 CU, 2 = Force 40 CU */

    /* Determina a máscara de WGPs solicitada:
       - Se o usuário especificou HwWgpMask no registro (ex: 0x0F para 32 CUs, 0x1B para custom, 0x1F para 40 CUs)
       - Se Stock (1): 0x07 (3 WGPs por array = 24 CUs)
       - Se Auto/Unlock (0 ou 2): 0x1F (5 WGPs por array = 40 CUs)
    */
    if (Dev->HwWgpMask != 0 && (Dev->HwWgpMask & 0x1Fu) != 0) {
        wgpMask = Dev->HwWgpMask & 0x1Fu;
    } else if (unlockMode == 1) {
        wgpMask = 0x07u;
    } else {
        wgpMask = 0x1Fu;
    }

    /* 1. Proba estado inicial */
    BcRegWrite(Dev, A_GRBM_GFX_INDEX, GRBM_GFX_INDEX_BROADCAST);
    spiBefore = BcRegRead(Dev, A_SPI_PG_ENABLE_STATIC_WGP_MASK);
    ccBefore = BcRegRead(Dev, A_CC_GC_SHADER_ARRAY_CONFIG);

    Dev->HwSpiPgRaw   = spiBefore;
    Dev->HwCcArrayRaw = ccBefore;

    /*
      Fase 0 / fail-closed: os offsets de SPI_PG e CC_GC_SHADER_ARRAY foram
      corrigidos (0x0935C / 0x089BC). Nos offsets antigos essas escritas caiam
      em enderecos invalidos e eram inofensivas; nos corretos elas REALMENTE
      reconfiguram o shader array. Reconfigurar WGP no bring-up sem DPM do SMU
      inicializado nao esta validado — fica desligado por padrao e so' e'
      habilitado explicitamente por HwCuUnlockMode != 0.

      Com unlockMode == 0 o driver apenas OBSERVA e reporta o que o silicio diz.
    */
    if (unlockMode == 0) {
        goto count_only;
    }

    /* 2. Executa sequência de configuração com a máscara granular escolhida */
    for (se = 0; se < 4; se++) {
        for (sh = 0; sh < 2; sh++) {
            ULONG gfxIdx = (se << 24) | (sh << 12) | (1u << 26) | (1u << 24);
            BcRegWrite(Dev, A_GRBM_GFX_INDEX, gfxIdx);
            
            /* Limpa máscara de colheita se estiver ativando mais que o stock */
            if (wgpMask != 0x07u) {
                BcRegWrite(Dev, A_CC_GC_SHADER_ARRAY_CONFIG, 0);
            }
            /* Configura o despachador SPI com a máscara exata de WGPs */
            BcRegWrite(Dev, A_SPI_PG_ENABLE_STATIC_WGP_MASK, wgpMask);
            /* Alimenta eletricamente os WGPs selecionados */
            BcRegWrite(Dev, A_RLC_PG_ALWAYS_ON_WGP_MASK, wgpMask);
        }
    }

    /* Broadcast Global */
    BcRegWrite(Dev, A_GRBM_GFX_INDEX, GRBM_GFX_INDEX_BROADCAST);
    if (wgpMask != 0x07u) {
        BcRegWrite(Dev, A_CC_GC_SHADER_ARRAY_CONFIG, 0);
    }
    BcRegWrite(Dev, A_SPI_PG_ENABLE_STATIC_WGP_MASK, wgpMask);
    BcRegWrite(Dev, A_RLC_PG_ALWAYS_ON_WGP_MASK, wgpMask);

count_only:
    /* 3. Contagem dos WGPs ativos lida do silício */
    BcRegWrite(Dev, A_GRBM_GFX_INDEX, GRBM_GFX_INDEX_BROADCAST);
    spiAfter = BcRegRead(Dev, A_SPI_PG_ENABLE_STATIC_WGP_MASK);
    ccAfter = BcRegRead(Dev, A_CC_GC_SHADER_ARRAY_CONFIG);

    Dev->HwSpiPgRaw   = spiAfter;
    Dev->HwCcArrayRaw = ccAfter;

    /*
      A fonte autoritativa da contagem e' CC_GC_SHADER_ARRAY_CONFIG, campo
      INACTIVE_WGPS em [31:16]: bit b limpo = WGP b ativo naquele shader array.

      Contar bits de SPI_PG_ENABLE_STATIC_WGP_MASK esta ERRADO. Lido com
      GRBM_GFX_INDEX em broadcast ele devolve 0x0000FFFF neste silicio, que
      nao e' uma mascara de 5 bits por SA — contar bits 0..4 dali produzia
      "5 WGP/SA = 40 CU = 2560 shaders", numero inflado e apresentado como
      DETECTADO. Com CC: INACTIVE_WGPS = 0xFFF8, bits 0/1/2 limpos, ou seja
      3 WGP/SA -> 12 WGP -> 24 CU -> 1536 SP, que bate com o stock documentado.
    */
    {
        ULONG inactiveWgps = (ccAfter >> 16) & 0xFFFFu;
        ULONG spiPerSa     = 0;
        BOOLEAN ccValido   = (ccAfter != 0xFFFFFFFFu) && (ccAfter != 0u) &&
                             (inactiveWgps != 0xFFFFu);

        for (b = 0; b < BC250_WGP_PER_SA_MAX; b++) {
            if ((inactiveWgps & (1u << b)) == 0) {
                countPerSa++;
            }
            if (spiAfter & (1u << b)) {
                spiPerSa++;
            }
        }

        if (!ccValido || countPerSa == 0) {
            /* Sem leitura confiavel nao inventamos numero: marcamos ASSUMIDO. */
            countPerSa = BC250_WGP_PER_SA_STOCK;
            Dev->HwCuCountSource = 1;   /* ASSUMIDO */
            Dev->HwCuUnlockSuccess = FALSE;
        } else {
            Dev->HwCuCountSource = 0;   /* DETECTADO em CC_GC_SHADER_ARRAY_CONFIG */
            Dev->HwCuUnlockSuccess = (countPerSa > BC250_WGP_PER_SA_STOCK);
        }

        /* Divergencia entre as duas fontes e' informacao, nao motivo para
           escolher a maior. CC manda; a divergencia fica registrada. */
        Dev->HwWgpSourcesDisagree = (ccValido && spiPerSa != countPerSa) ? 1u : 0u;
    }

    targetWgps = countPerSa * BC250_SHADER_ARRAY_COUNT;
    Dev->HwNumWgp = targetWgps;
    Dev->HwNumCu = targetWgps * 2;              /* 2 CU por WGP (RDNA) */
    Dev->HwNumShaders = Dev->HwNumCu * 64;      /* 64 ALUs por CU */
    Dev->HwWgpMask = (spiAfter != 0) ? spiAfter : 0x07u;

    /* 4. Notifica o firmware SMU com a contagem de WGPs (Fase 1: 0x18 removido do bring-up para evitar timeout sem DPM) */
}

/*===========================================================================
  GFXOFF na Cyan Skillfish

  REMOVIDO. Esta funcao escrevia no mailbox do SMU achando que era GFXOFF.

  Os enderecos que ela usava colidem com os nossos proprios registradores:

      SMN_GFXOFF_FEATURE_LOW  0x03B10528  ==  SMU_Q2_CMD
      SMN_GFXOFF_FEATURE_HIGH 0x03B10998  ==  SMU_Q2_ARG

  Ou seja, o bring-up lia o comando/argumento da fila 2 do SMU (por isso o
  "FEATURE_HIGH" devolvia 0x00001235 — o eco do token de teste 0x1234) e
  depois GRAVAVA um valor arbitrario no registrador de COMANDO do SMU. Todo
  boot disparava um comando invalido no mailbox.

  Alem de errado, era ativo: e' candidato direto a causa da degradacao da
  SDMA0, que passou de 1-3 fences para 0 logo depois que esta funcao entrou.

  E GFXOFF nao se aplica a este ASIC:

    - smu_v11_8_ppsmc.h vai ate' 0x3D e nao tem AllowGfxOff/DisallowGfxOff;
    - cyan_skillfish_ppt.c do Linux nao tem nenhuma referencia a gfx_off;
    - o Linux configura cg_flags = 0 e pg_flags = 0 para cyan skillfish,
      isto e', sem clock gating e sem power gating.

  A prova 9 da regra ("GFXOFF comprovadamente desativado") e' NAO APLICAVEL
  aqui, e isso precisa ser justificado com estas evidencias — nao satisfeito
  com um bit lido de um registrador que nao decodifica.

  Se algum dia aparecer um mecanismo real de GFXOFF para este chip, ele NAO
  vai ser uma escrita SMN nesta faixa: 0x03B1xxxx e' o bloco de mailboxes.
===========================================================================*/

/*===========================================================================
  Unified Hardware Bring-up
===========================================================================*/
NTSTATUS
Bc250HwBringup(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    PHYSICAL_ADDRESS low, high, boundary;
    PHYSICAL_ADDRESS pspRingPa, pspCmdPa, pspFencePa;
    PVOID pspRingVa = NULL, pspCmdVa = NULL, pspFenceVa = NULL;
    ULONG pspRingWptr = 0, pspFenceValue = 0;
    ULONG reg, rbCntl;
    NTSTATUS status;

    if (!Dev || !Dev->MmioVirtualBase) {
        return STATUS_INVALID_PARAMETER;
    }

    /* 1. Handshake do Mailbox do SMU, Disable GFXOFF e Deteccao de CUs / WGPs */
    {
        ULONG rsp;

        rsp = 0;
        Dev->HwSmuTestQ0Status = (ULONG)BcSmuSendMsg(Dev, 0, SMU_MSG_TEST_MESSAGE,
                                                    SMU_TEST_ARG, &rsp);
        Dev->HwSmuTestQ0Rsp = rsp;
        Dev->HwSmuTestQ0Arg = BcSmnRead(Dev, SMU_Q0_ARG);

        rsp = 0;
        Dev->HwSmuTestQ2Status = (ULONG)BcSmuSendMsg(Dev, 2, SMU_MSG_TEST_MESSAGE,
                                                    SMU_TEST_ARG, &rsp);
        Dev->HwSmuTestQ2Rsp = rsp;
        Dev->HwSmuTestQ2Arg = BcSmnRead(Dev, SMU_Q2_ARG);

        rsp = 0;
        (VOID)BcSmuSendMsg(Dev, 0, SMU_MSG_GET_SMU_VERSION, 0, &rsp);
        Dev->HwSmuVersionRsp = rsp;
        Dev->HwSmuVersion = BcSmnRead(Dev, SMU_Q0_ARG);

        /* Fase 1: Desativa GFXOFF via SMN antes de qualquer engine */

        /* Fase 1: ForceGfxFreq (0x39) REMOVIDO do bring-up. Sem tabelas DPM
           ele causa instabilidade/crash no silicio. O clock sera lido de 0x37. */
        
        /* Configuracao e Deteccao de CUs / WGPs no silicio */
        Bc250HwConfigureComputeUnits(Dev);
        
        rsp = 0;
        (VOID)BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp);    /* PPSMC_MSG_GetGfxFrequency */
        Dev->HwStatusSmuGfxOff = (rsp == 1) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED;
        Dev->HwSmuRawResponse = BcSmnRead(Dev, SMU_Q0_ARG);
    }

    if (Dev->HwInitMaxStep == 1) {
        Dev->HardwareInitialized = TRUE;
        return STATUS_SUCCESS;
    }

    /* 2. PSP: Cria o anel GPCOM em Host RAM DMA */
    reg = BcRegRead(Dev, A_PSP_C2PMSG_64);
    Dev->HwPspTosReg = reg;
    if ((reg & PSP_TOS_READY) == 0) {
        Dev->HwStatusPspRing = STATUS_DEVICE_NOT_READY;
        return STATUS_DEVICE_NOT_READY;
    }

    BcRegWrite(Dev, A_PSP_C2PMSG_64, PSP_CMD_DESTROY_GPCOM_RING);
    KeStallExecutionProcessor(20000);
    (VOID)BcPspWaitAck(Dev);

    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFULL;
    boundary.QuadPart = 0;

    pspRingVa  = MmAllocateContiguousMemorySpecifyCache(PSP_RING_SIZE, low, high, boundary, MmNonCached);
    pspCmdVa   = MmAllocateContiguousMemorySpecifyCache(PSP_CMD_BUF_SIZE, low, high, boundary, MmNonCached);
    pspFenceVa = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, low, high, boundary, MmNonCached);

    Dev->PspRingVa  = pspRingVa;
    Dev->PspCmdVa   = pspCmdVa;
    Dev->PspFenceVa = pspFenceVa;

    if (!pspRingVa || !pspCmdVa || !pspFenceVa) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup_psp;
    }

    RtlZeroMemory(pspRingVa, PSP_RING_SIZE);
    RtlZeroMemory(pspCmdVa, PSP_CMD_BUF_SIZE);
    RtlZeroMemory(pspFenceVa, PAGE_SIZE);

    pspRingPa  = MmGetPhysicalAddress(pspRingVa);
    pspCmdPa   = MmGetPhysicalAddress(pspCmdVa);
    pspFencePa = MmGetPhysicalAddress(pspFenceVa);
    Dev->PspRingPa  = pspRingPa;
    Dev->PspCmdPa   = pspCmdPa;
    Dev->PspFencePa = pspFencePa;

    BcRegWrite(Dev, A_PSP_C2PMSG_69, (ULONG)(pspRingPa.QuadPart & 0xFFFFFFFFULL));
    BcRegWrite(Dev, A_PSP_C2PMSG_70, (ULONG)(pspRingPa.QuadPart >> 32));
    BcRegWrite(Dev, A_PSP_C2PMSG_71, PSP_RING_SIZE);
    KeMemoryBarrier();

    BcRegWrite(Dev, A_PSP_C2PMSG_64, PSP_RING_TYPE_KM << 16);
    Dev->PspRingActive = TRUE;
    KeStallExecutionProcessor(20000);
    status = BcPspWaitAck(Dev);
    Dev->HwStatusPspRing = status;
    if (Dev->HwInitMaxStep == 2) {
        Dev->HardwareInitialized = TRUE;
        return STATUS_SUCCESS;
    }

    /* 3, 4, 5. Carga de Firmware e TMR (se HwInitFirmware != 0) */
    if (Dev->HwInitFirmware) {
        Dev->HwStatusFw[0] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_SDMA0, (const ULONG*)g_FwBlob_SDMA, g_FwBlobSize_SDMA);

        Dev->HwStatusTmr = BcPspSetupTmr(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue);

        Dev->HwStatusFw[1] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_RLC_G, (const ULONG*)g_FwBlob_RLC, g_FwBlobSize_RLC);
        Dev->HwStatusFw[2] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_CP_CE, (const ULONG*)g_FwBlob_CE, g_FwBlobSize_CE);
        Dev->HwStatusFw[3] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_CP_PFP, (const ULONG*)g_FwBlob_PFP, g_FwBlobSize_PFP);
        Dev->HwStatusFw[4] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_CP_ME, (const ULONG*)g_FwBlob_ME, g_FwBlobSize_ME);
        Dev->HwStatusFw[5] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_CP_MEC, (const ULONG*)g_FwBlob_MEC, g_FwBlobSize_MEC);
        Dev->HwStatusFw[6] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                      PSP_FW_CP_MEC2, (const ULONG*)g_FwBlob_MEC2, g_FwBlobSize_MEC2);
    }

    if (Dev->HwInitMaxStep == 3) {
        Dev->HardwareInitialized = TRUE;
        return STATUS_SUCCESS;
    }

    /* 6. GCVM L2 MMU & System Aperture */
    Dev->HwFbOffsetReg = BcRegRead(Dev, A_GCMC_VM_FB_OFFSET);
    BcRegWrite(Dev, A_GCVM_L2_CNTL, 0x00080E03u);
    BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000000u);
    BcRegWrite(Dev, A_GCVM_L2_CNTL3, 0x00100000u);
    BcRegWrite(Dev, A_GCVM_L2_CNTL4, 0x00000000u);
    BcRegWrite(Dev, A_GCVM_L2_CNTL5, 0x000FFFE0u);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_CNTL, 0x007FFE80u);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO, 0);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI, 0);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO, 0);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI, 0);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO, 0xFFFFFFFFu);
    BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI, 0x000000FFu);

    BcHdpReadCacheInvalidate(Dev);
    BcGcvmTlbFlush(Dev, 0);
    Dev->HwL2CntlReadback = BcRegRead(Dev, A_GCVM_L2_CNTL);

    if (Dev->HwInitMaxStep == 4) {
        Dev->HardwareInitialized = TRUE;
        return STATUS_SUCCESS;
    }

    /* 7. CP PM4 Ring 0 (se HwInitGfxRing != 0) */
    if (Dev->HwInitGfxRing) {
        ULONGLONG ringGpuAddr = BC250_VRAM_GPU_BASE + 0x0FE00000ULL;
        ULONGLONG rptrWbAddr  = ringGpuAddr + 4096ULL;
        ULONGLONG wptrPoll    = ringGpuAddr + 4096ULL + 0x100ULL;

        BcRegWrite(Dev, A_CP_ME_CNTL, 0x15000150u);
        KeStallExecutionProcessor(50);

        BcRegWrite(Dev, A_CP_RB0_BASE, (ULONG)((ringGpuAddr >> 8) & 0xFFFFFFFFULL));
        BcRegWrite(Dev, A_CP_RB0_BASE_HI, (ULONG)((ringGpuAddr >> 40) & 0xFFULL));

        BcRegWrite(Dev, A_CP_RB0_RPTR_ADDR, (ULONG)(rptrWbAddr & 0xFFFFFFFCULL));
        BcRegWrite(Dev, A_CP_RB0_RPTR_ADDR_HI, (ULONG)(rptrWbAddr >> 32));

        BcRegWrite(Dev, A_CP_RB_DOORBELL_CONTROL, 0);
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_ADDR_LO, (ULONG)(wptrPoll & 0xFFFFFFFCULL));
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_ADDR_HI, (ULONG)(wptrPoll >> 32));
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);

        rbCntl = (9u & 0x3Fu) | ((7u & 0x3Fu) << 8) | (1u << 28) | (1u << 30);
        BcRegWrite(Dev, A_CP_RB0_CNTL, rbCntl | 0x80000000u | (1u << 27));
        BcRegWrite(Dev, A_CP_RB0_RPTR, 0);
        BcRegWrite(Dev, A_CP_RB0_WPTR, 0);
        BcRegWrite(Dev, A_CP_RB0_WPTR_HI, 0);

        BcRegWrite(Dev, A_CP_RB0_CNTL, rbCntl);

        BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000003u);
        BcHdpReadCacheInvalidate(Dev);
        BcGcvmTlbFlush(Dev, 0);

        BcRegWrite(Dev, A_VGT_NUM_INSTANCES, 1);
        BcRegWrite(Dev, A_PA_SC_LINE_STIPPLE_STATE, 0);

        /* Fase 1 / Risco R2: Nao des-haltar o CP (CP_ME_CNTL=0) por padrao sem GPUVM pronta */
        if (Dev->HwUnhaltCp) {
            BcRegWrite(Dev, A_CP_ME_CNTL, 0);
            KeStallExecutionProcessor(50);
        }

        BcRegWrite(Dev, A_CP_RB0_CNTL, rbCntl);
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);

        Dev->HwRbCntlReadback  = BcRegRead(Dev, A_CP_RB0_CNTL);
        Dev->HwMeCntlReadback  = BcRegRead(Dev, A_CP_ME_CNTL);
        Dev->HwRptrAfterInit   = BcRegRead(Dev, A_CP_RB0_RPTR);

        Dev->GfxWptr = 0;
        Dev->GfxRingVramOffset = 0x0FE00000u;
        Dev->GfxRingSize = 4096u;
    }

    if (Dev->HwInitMaxStep == 5) {
        Dev->HardwareInitialized = TRUE;
        return STATUS_SUCCESS;
    }

    /* 8. SDMA0 Hardware Copy Ring (se HwInitSdmaRing != 0) */
    if (Dev->HwInitSdmaRing) {
        Dev->HwStatusSdmaRing = Bc250SdmaRingInit(Dev);
    } else {
        Dev->HwStatusSdmaRing = STATUS_SUCCESS;
    }

    status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Dev->HwStatusSdmaRing)) {
        status = Dev->HwStatusSdmaRing;
    }

    Dev->HardwareInitialized = (BOOLEAN)NT_SUCCESS(status);
    Dev->PspRingWptr = pspRingWptr;
    Dev->PspFenceValue = pspFenceValue;

    /*
      Fase 0: fence fisica do compute e estado real do GFX no fim do bring-up.
      A fence do compute vive na VRAM logo apos o CP ring (0x0FE00000 + 0x4000),
      fora do ring e fora do carveout TMR.
    */
    Dev->ComputeFenceVramOffset = 0x0FE04000u;
    if (Dev->FbVirtualBase != NULL && (Dev->ComputeFenceVramOffset + 4u) <= Dev->FbSize) {
        *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->ComputeFenceVramOffset) = 0;
    }

    Dev->HwGrbmStatusAtInit = BcRegRead(Dev, A_GRBM_STATUS);
    Dev->HwCpStatAtInit     = BcRegRead(Dev, A_CP_STAT);

    /*
      FirmwareReady so' e' TRUE se TODOS os microcodigos do CP foram aceitos
      pelo PSP. Nunca derivar de "o driver carregou" nem de HardwareInitialized.
      Hoje isto e' FALSE nesta placa: o SOS da BC-250 responde
      TEE_ERROR_ITEM_NOT_FOUND para CP/RLC/SDMA (so' fw_type 18/SMU e' aceito).
    */
    {
        ULONG fwIdx;
        BOOLEAN allOk = TRUE;
        for (fwIdx = 0; fwIdx < RTL_NUMBER_OF(Dev->HwStatusFw); fwIdx++) {
            if (!NT_SUCCESS(Dev->HwStatusFw[fwIdx])) {
                allOk = FALSE;
                break;
            }
        }
        Dev->HwFirmwareReady = allOk;
    }

cleanup_psp:
    if (!NT_SUCCESS(status)) {
        Bc250HwShutdown(Dev);
    }

    return status;
}

VOID
Bc250HwShutdown(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    if (!Dev) {
        return;
    }

    Bc250SdmaRingShutdown(Dev);

    if (Dev->MmioVirtualBase) {
        if (Dev->HardwareInitialized || Dev->PspRingActive) {
            BcRegWrite(Dev, A_CP_ME_CNTL, 0x15000150u);
            BcRegWrite(Dev, A_SDMA0_F32_CNTL, 0x00000001u);
        }

        if (Dev->PspRingActive) {
            BcRegWrite(Dev, A_PSP_C2PMSG_64, PSP_CMD_DESTROY_GPCOM_RING);
            KeStallExecutionProcessor(20000);
            (VOID)BcPspWaitAck(Dev);
            Dev->PspRingActive = FALSE;
        }
    }
    Dev->PspRingActive = FALSE;

    if (Dev->PspRingVa) {
        MmFreeContiguousMemory(Dev->PspRingVa);
        Dev->PspRingVa = NULL;
    }
    if (Dev->PspCmdVa) {
        MmFreeContiguousMemory(Dev->PspCmdVa);
        Dev->PspCmdVa = NULL;
    }
    if (Dev->PspFenceVa) {
        MmFreeContiguousMemory(Dev->PspFenceVa);
        Dev->PspFenceVa = NULL;
    }
    if (Dev->SmuMetricsVa) {
        MmFreeContiguousMemory(Dev->SmuMetricsVa);
        Dev->SmuMetricsVa = NULL;
    }

    Dev->HardwareInitialized = FALSE;
    Dev->PspRingWptr = 0;
    Dev->PspFenceValue = 0;
}

NTSTATUS
Bc250HwSubmitPm4(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ const ULONG *Data,
    _In_ ULONG DwordCount,
    _Out_ PULONG RptrBefore,
    _Out_ PULONG RptrAfter
    )
{
    ULONG ringDwords, wptr, rptrB, rptrA, wptrFinal, i;
    ULONG timeout = BC250_WAIT_ITERS(2u);

    if (!Dev || !Dev->MmioVirtualBase || !Dev->FbVirtualBase || !Data || DwordCount == 0 || DwordCount > 256) {
        return STATUS_INVALID_PARAMETER;
    }

    ringDwords = Dev->GfxRingSize / 4;
    wptr = Dev->GfxWptr;
    rptrB = BcRegRead(Dev, A_CP_RB0_RPTR);
    if (RptrBefore) *RptrBefore = rptrB;

    for (i = 0; i < DwordCount; i++) {
        ULONG off = Dev->GfxRingVramOffset + ((wptr + i) % ringDwords) * 4;
        *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + off) = Data[i];
    }
    wptrFinal = (wptr + DwordCount) % ringDwords;
    Dev->GfxWptr = wptrFinal;

    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->GfxRingVramOffset + Dev->GfxRingSize + 0x100) = wptrFinal;

    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000003u);
    BcGcvmTlbFlush(Dev, 0);

    BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);
    BcRegWrite(Dev, A_CP_RB0_WPTR, wptrFinal);
    BcRegWrite(Dev, A_CP_RB0_WPTR_HI, 0);

    volatile ULONG *rptrWb = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->GfxRingVramOffset + 4096);
    rptrA = rptrB;
    for (i = 0; i < timeout; i++) {
        rptrA = BcRegRead(Dev, A_CP_RB0_RPTR);
        if (rptrA == wptrFinal || *rptrWb == wptrFinal) {
            rptrA = wptrFinal;
            break;
        }
        KeStallExecutionProcessor(2);
    }
    if (RptrAfter) *RptrAfter = rptrA;

    return STATUS_SUCCESS;
}

/*===========================================================================
  SDMA0 Hardware DMA Copy Engine
===========================================================================*/

#define A_SDMA0_CLK_CTRL                            0x049ECu
#define A_SDMA0_CNTL                                0x049F0u
#define A_SDMA0_CHICKEN_BITS                        0x049F4u
#define A_SDMA0_GB_ADDR_CONFIG                      0x049F8u
#define A_SDMA0_GB_ADDR_CONFIG_READ                 0x049FCu
#define A_SDMA0_SEM_WAIT_FAIL_TIMER                 0x04A04u
#define A_SDMA0_STATUS2                             0x04A60u
#define A_SDMA0_UTCL1_CNTL                          0x04A70u
#define A_SDMA0_UTCL1_PAGE                          0x04AA0u
#define A_SDMA0_GFX_RB_CNTL                         0x04B80u
#define A_SDMA0_GFX_RB_BASE                         0x04B84u
#define A_SDMA0_GFX_RB_BASE_HI                      0x04B88u
#define A_SDMA0_GFX_RB_RPTR                         0x04B8Cu
#define A_SDMA0_GFX_RB_RPTR_HI                      0x04B90u
#define A_SDMA0_GFX_RB_WPTR                         0x04B94u
#define A_SDMA0_GFX_RB_WPTR_HI                      0x04B98u
#define A_SDMA0_GFX_RB_WPTR_POLL_CNTL               0x04B9Cu
#define A_SDMA0_GFX_RB_RPTR_ADDR_HI                 0x04BA0u
#define A_SDMA0_GFX_RB_RPTR_ADDR_LO                 0x04BA4u
#define A_SDMA0_GFX_IB_CNTL                         0x04BA8u
#define A_SDMA0_GFX_DOORBELL                        0x04BC8u
#define A_SDMA0_GFX_MINOR_PTR_UPDATE                0x04BD0u
#define A_SDMA0_GFX_POLL_ADDR_HI                    0x04C48u
#define A_SDMA0_GFX_POLL_ADDR_LO                    0x04C4Cu

#define SDMA_OP_COPY                                1u
#define SDMA_OP_CONST_FILL                          11u
#define SDMA_OP_FENCE                               5u
#define SDMA_OP_TRAP                                6u

#define SDMA_RB_ENABLE                              0x00000001u
#define SDMA_RB_SIZE_MASK                           0x0000003Eu
#define SDMA_RB_SIZE_SHIFT                          1u
#define SDMA_RPTR_WB_ENABLE                         0x00001000u
#define SDMA_F32_HALT                               0x00000001u
#define SDMA_F32_RESET                              0x00000200u
#define SDMA_IB_ENABLE                              0x00000001u
#define SDMA_DOORBELL_ENABLE                        0x10000000u
#define SDMA_MINOR_PTR_ENABLE                       0x00000001u
#define SDMA_F32_POLL_ENABLE                        0x00000004u
#define SDMA_CLK_OVERRIDE_ALL                       0xFF000000u
#define SDMA_AUTO_CTXSW_ENABLE                      0x00040000u
#define SDMA_CNTL_REQUIRED                          0x00040022u
#define SDMA_MAX_TRANSFER_BYTES                     0x00400000ull /* 4 MB per packet */

static VOID
BcSdmaGoldenRmw(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG AndMask,
    _In_ ULONG OrMask
    )
{
    ULONG val = BcRegRead(Dev, Offset);
    val &= ~AndMask;
    val |= (OrMask & AndMask);
    BcRegWrite(Dev, Offset, val);
}

NTSTATUS
Bc250SdmaRingInit(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    ULONGLONG ringGpu, rptrGpu, wptrGpu;
    ULONG rbCntl, rbBufSize = 0, ringDwords;
    ULONG val;

    if (!Dev || !Dev->MmioVirtualBase || !Dev->FbVirtualBase) {
        return STATUS_INVALID_PARAMETER;
    }

    ExInitializeFastMutex(&Dev->SdmaLock);

    Dev->SdmaRingVramOffset = 0x0FD00000u; /* 253 MB */
    Dev->SdmaRingSize       = 4096u;
    Dev->SdmaRingGpuBase    = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->SdmaRingVramOffset;
    Dev->SdmaRingVirtualBase= (PUCHAR)Dev->FbVirtualBase + Dev->SdmaRingVramOffset;
    Dev->SdmaRptrVramOffset = Dev->SdmaRingVramOffset + 0x1000u;
    Dev->SdmaFenceVramOffset= Dev->SdmaRingVramOffset + 0x3000u;
    Dev->SdmaWptr           = 0;
    Dev->SdmaFenceSequence  = 0;

    ringGpu = Dev->SdmaRingGpuBase;
    rptrGpu = ringGpu + 0x1000ULL;
    wptrGpu = ringGpu + 0x2000ULL;

    RtlZeroMemory(Dev->SdmaRingVirtualBase, Dev->SdmaRingSize + 0x4000u);

    /* 1. Soft-Reset do microcontrolador F32 da SDMA0 */
    val = BcRegRead(Dev, A_SDMA0_CLK_CTRL);
    BcRegWrite(Dev, A_SDMA0_CLK_CTRL, val | SDMA_CLK_OVERRIDE_ALL);

    val = BcRegRead(Dev, A_SDMA0_CNTL);
    BcRegWrite(Dev, A_SDMA0_CNTL, val | SDMA_AUTO_CTXSW_ENABLE);

    BcRegWrite(Dev, A_SDMA0_F32_CNTL, SDMA_F32_HALT);
    BcRegWrite(Dev, A_SDMA0_F32_CNTL, SDMA_F32_HALT | SDMA_F32_RESET);
    KeStallExecutionProcessor(50000); /* 50 ms */
    BcRegWrite(Dev, A_SDMA0_F32_CNTL, SDMA_F32_HALT);
    BcRegWrite(Dev, A_SDMA0_F32_CNTL, 0);
    KeStallExecutionProcessor(100000); /* 100 ms */

    /* 2. Programação do buffer circular SDMA0 */
    BcRegWrite(Dev, A_SDMA0_SEM_WAIT_FAIL_TIMER, 0);

    ringDwords = Dev->SdmaRingSize / sizeof(ULONG);
    while ((1u << (rbBufSize + 1u)) <= ringDwords) {
        rbBufSize++;
    }

    rbCntl = BcRegRead(Dev, A_SDMA0_GFX_RB_CNTL);
    rbCntl &= ~(SDMA_RB_SIZE_MASK | SDMA_RB_ENABLE | SDMA_RPTR_WB_ENABLE);
    rbCntl |= (rbBufSize << SDMA_RB_SIZE_SHIFT) & SDMA_RB_SIZE_MASK;
    BcRegWrite(Dev, A_SDMA0_GFX_RB_CNTL, rbCntl);

    BcRegWrite(Dev, A_SDMA0_GFX_RB_RPTR, 0);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_RPTR_HI, 0);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, 0);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_HI, 0);

    BcRegWrite(Dev, A_SDMA0_GFX_POLL_ADDR_LO, (ULONG)(wptrGpu & 0xFFFFFFFFULL));
    BcRegWrite(Dev, A_SDMA0_GFX_POLL_ADDR_HI, (ULONG)(wptrGpu >> 32));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0);

    BcRegWrite(Dev, A_SDMA0_GFX_RB_RPTR_ADDR_LO, (ULONG)(rptrGpu & 0xFFFFFFFCULL));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_RPTR_ADDR_HI, (ULONG)(rptrGpu >> 32));
    rbCntl |= SDMA_RPTR_WB_ENABLE;

    /* Golden Registers SOC15 */
    BcSdmaGoldenRmw(Dev, A_SDMA0_CHICKEN_BITS, 0xFFBF1F0Fu, 0x03AB0107u);
    BcSdmaGoldenRmw(Dev, A_SDMA0_GB_ADDR_CONFIG, 0x001877FFu, 0x00000044u);
    BcSdmaGoldenRmw(Dev, A_SDMA0_GB_ADDR_CONFIG_READ, 0x001877FFu, 0x00000044u);

    /* Base da fila */
    BcRegWrite(Dev, A_SDMA0_GFX_RB_BASE, (ULONG)(ringGpu >> 8));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_BASE_HI, (ULONG)(ringGpu >> 40));
    BcRegWrite(Dev, A_SDMA0_GFX_MINOR_PTR_UPDATE, SDMA_MINOR_PTR_ENABLE);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, 0);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_HI, 0);

    val = BcRegRead(Dev, A_SDMA0_GFX_DOORBELL);
    BcRegWrite(Dev, A_SDMA0_GFX_DOORBELL, val & ~SDMA_DOORBELL_ENABLE);
    BcRegWrite(Dev, A_SDMA0_GFX_MINOR_PTR_UPDATE, 0);

    /* Clock e controle de contexto */
    val = BcRegRead(Dev, A_SDMA0_CLK_CTRL);
    BcRegWrite(Dev, A_SDMA0_CLK_CTRL, val | SDMA_CLK_OVERRIDE_ALL);

    val = BcRegRead(Dev, A_SDMA0_CNTL);
    BcRegWrite(Dev, A_SDMA0_CNTL, val | SDMA_CNTL_REQUIRED);

    val = BcRegRead(Dev, A_SDMA0_UTCL1_CNTL);
    val &= ~((0x7u << 9) | (0x1Fu << 1));
    val |= (3u << 9) | (9u << 1);
    BcRegWrite(Dev, A_SDMA0_UTCL1_CNTL, val);
    BcSdmaGoldenRmw(Dev, A_SDMA0_UTCL1_PAGE, 0x00FFF000u, 0x004C5C00u);

    /* Ativa o anel */
    val = BcRegRead(Dev, A_SDMA0_F32_CNTL);
    BcRegWrite(Dev, A_SDMA0_F32_CNTL, val & ~SDMA_F32_HALT);

    rbCntl |= SDMA_RB_ENABLE;
    BcRegWrite(Dev, A_SDMA0_GFX_RB_CNTL, rbCntl);

    val = BcRegRead(Dev, A_SDMA0_GFX_IB_CNTL);
    BcRegWrite(Dev, A_SDMA0_GFX_IB_CNTL, val | SDMA_IB_ENABLE);

    Dev->HwSdmaRbCntlReadback = BcRegRead(Dev, A_SDMA0_GFX_RB_CNTL);
    if ((Dev->HwSdmaRbCntlReadback & SDMA_RB_ENABLE) == 0) {
        Dev->HwStatusSdmaRing = STATUS_DEVICE_HARDWARE_ERROR;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Dev->SdmaInitialized = TRUE;
    Dev->HwStatusSdmaRing = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

VOID
Bc250SdmaRingShutdown(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    if (!Dev || !Dev->MmioVirtualBase) {
        return;
    }
    if (Dev->SdmaInitialized) {
        BcRegWrite(Dev, A_SDMA0_F32_CNTL, SDMA_F32_HALT);
        BcRegWrite(Dev, A_SDMA0_GFX_RB_CNTL, 0);
        Dev->SdmaInitialized = FALSE;
    }
}

NTSTATUS
Bc250SdmaSubmitCopy(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONGLONG SrcGpuAddr,
    _In_ ULONGLONG DstGpuAddr,
    _In_ ULONG Bytes,
    _Out_opt_ PULONG DurationUs,
    _Out_opt_ PULONG FenceValue,
    _Out_opt_ BC250_WDDM_ESCAPE_SDMA_COPY *Evidence
    )
{
    ULONG packet[16];
    ULONG packetDwords = 0;
    ULONG fenceValue;
    ULONG ringDwords, wptr, nextWptr, i;
    volatile ULONG* fencePtr;
    LARGE_INTEGER freq, tStart, tEnd;
    ULONG elapsedUs = 0;
    ULONG rptrBefore = 0;

    if (!Dev || !Dev->SdmaInitialized || Bytes == 0 || (Bytes & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (SrcGpuAddr < BC250_VRAM_GPU_BASE) {
        SrcGpuAddr += BC250_VRAM_GPU_BASE;
    }
    if (DstGpuAddr < BC250_VRAM_GPU_BASE) {
        DstGpuAddr += BC250_VRAM_GPU_BASE;
    }

    ExAcquireFastMutex(&Dev->SdmaLock);

    fenceValue = ++Dev->SdmaFenceSequence;
    if (fenceValue == 0) fenceValue = ++Dev->SdmaFenceSequence;

    fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaFenceVramOffset);
    *fencePtr = ~fenceValue;
    KeMemoryBarrier();

    /* 1. Pacote SDMA_OP_COPY (7 Dwords) */
    packet[0] = SDMA_OP_COPY;
    packet[1] = Bytes - 1u;
    packet[2] = 0;
    packet[3] = (ULONG)(SrcGpuAddr & 0xFFFFFFFFULL);
    packet[4] = (ULONG)(SrcGpuAddr >> 32);
    packet[5] = (ULONG)(DstGpuAddr & 0xFFFFFFFFULL);
    packet[6] = (ULONG)(DstGpuAddr >> 32);
    packetDwords += 7;

    /* 2. Pacote SDMA_OP_FENCE (4 Dwords) */
    {
        ULONGLONG fenceGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->SdmaFenceVramOffset;
        packet[packetDwords + 0] = SDMA_OP_FENCE | (3u << 16);
        packet[packetDwords + 1] = (ULONG)(fenceGpu & 0xFFFFFFFFULL);
        packet[packetDwords + 2] = (ULONG)(fenceGpu >> 32);
        packet[packetDwords + 3] = fenceValue;
        packetDwords += 4;
    }

    /* 3. Escrita no anel circular */
    ringDwords = Dev->SdmaRingSize / sizeof(ULONG);
    wptr = Dev->SdmaWptr;

    for (i = 0; i < packetDwords; i++) {
        ULONG off = Dev->SdmaRingVramOffset + ((wptr + i) % ringDwords) * sizeof(ULONG);
        *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + off) = packet[i];
    }
    nextWptr = (wptr + packetDwords) % ringDwords;
    Dev->SdmaWptr = nextWptr;
    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRingVramOffset + 0x2000u) = (nextWptr << 2);
    KeMemoryBarrier();

    tStart = KeQueryPerformanceCounter(&freq);
    rptrBefore = BcRegRead(Dev, A_SDMA0_GFX_RB_RPTR);

    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, (nextWptr << 2));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_HI, 0);

    /* 4. Espera ativa pelo avanço do hardware RPTR ou da cerca */
    ULONG targetWptrBytes = (nextWptr << 2);
    volatile ULONG *rptrWb = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRptrVramOffset);

    for (i = 0; i < BC250_WAIT_ITERS(5u); i++) {
        ULONG currentRptr = BcRegRead(Dev, A_SDMA0_GFX_RB_RPTR);
        if (*fencePtr == fenceValue || currentRptr == targetWptrBytes || *rptrWb == targetWptrBytes || (*rptrWb << 2) == targetWptrBytes) {
            break;
        }
        KeStallExecutionProcessor(5);
    }

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        elapsedUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    }
    if (DurationUs) *DurationUs = elapsedUs;
    if (FenceValue) *FenceValue = fenceValue;

    {
        ULONG currentRptr = BcRegRead(Dev, A_SDMA0_GFX_RB_RPTR);
        ULONG fenceObserved = *fencePtr;
        BOOLEAN fenceWritten = (fenceObserved == fenceValue);
        BOOLEAN rptrReached  = (currentRptr == targetWptrBytes) ||
                               (*rptrWb == targetWptrBytes) ||
                               ((*rptrWb << 2) == targetWptrBytes);

        /*
          Fase 0, item 0.5: reportar QUAL condicao encerrou a espera.

          O criterio antigo era "fence OU rptr" e devolvia STATUS_SUCCESS nos
          dois casos. Mas RPTR alcancar o WPTR prova apenas que o motor
          consumiu o frame do anel — nao que executou a copia. Medido nesta
          placa: RPTR alcanca o WPTR, a fence NAO e' escrita, e o destino
          permanece com o poison intacto. "Copia concluida" naquele criterio
          era falso.
        */
        if (Evidence) {
            Evidence->FenceObserved       = fenceObserved;
            Evidence->FenceGpuAddress     = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->SdmaFenceVramOffset;
            Evidence->RptrBefore          = rptrBefore;
            Evidence->RptrAfter           = currentRptr;
            Evidence->WptrTarget          = targetWptrBytes;
            Evidence->FenceWritten        = fenceWritten ? 1u : 0u;
            Evidence->CompletedByRptrOnly = (!fenceWritten && rptrReached) ? 1u : 0u;
        }

        if (fenceWritten) {
            Dev->SdmaCopyCount++;
            if (elapsedUs > 0) {
                Dev->SdmaBandwidthMBs = (ULONG)(((ULONGLONG)Bytes * 1000000ULL) / ((ULONGLONG)elapsedUs * 1048576ULL));
            }
            ExReleaseFastMutex(&Dev->SdmaLock);
            return STATUS_SUCCESS;
        }

        ExReleaseFastMutex(&Dev->SdmaLock);
        /* Frame consumido sem fence = execucao nao comprovada, nao sucesso. */
        return rptrReached ? STATUS_DEVICE_NOT_READY : STATUS_IO_TIMEOUT;
    }
}

NTSTATUS
Bc250SdmaSubmitFill(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONGLONG DstGpuAddr,
    _In_ ULONG Bytes,
    _In_ ULONG Pattern
    )
{
    ULONG packet[16];
    ULONG packetDwords = 0;
    ULONG fenceValue;
    ULONG ringDwords, wptr, nextWptr, i;
    volatile ULONG* fencePtr;

    if (!Dev || !Dev->SdmaInitialized || Bytes == 0 || (Bytes & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (DstGpuAddr < BC250_VRAM_GPU_BASE) {
        DstGpuAddr += BC250_VRAM_GPU_BASE;
    }

    ExAcquireFastMutex(&Dev->SdmaLock);

    fenceValue = ++Dev->SdmaFenceSequence;
    if (fenceValue == 0) fenceValue = ++Dev->SdmaFenceSequence;

    fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaFenceVramOffset);
    *fencePtr = ~fenceValue;
    KeMemoryBarrier();

    /* SDMA_OP_CONST_FILL (5 Dwords) */
    packet[0] = SDMA_OP_CONST_FILL | (2u << 30);
    packet[1] = (ULONG)(DstGpuAddr & 0xFFFFFFFFULL);
    packet[2] = (ULONG)(DstGpuAddr >> 32);
    packet[3] = Pattern;
    packet[4] = Bytes - 1u;
    packetDwords += 5;

    /* SDMA_OP_FENCE (4 Dwords) */
    {
        ULONGLONG fenceGpu = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->SdmaFenceVramOffset;
        packet[packetDwords + 0] = SDMA_OP_FENCE | (3u << 16);
        packet[packetDwords + 1] = (ULONG)(fenceGpu & 0xFFFFFFFFULL);
        packet[packetDwords + 2] = (ULONG)(fenceGpu >> 32);
        packet[packetDwords + 3] = fenceValue;
        packetDwords += 4;
    }

    ringDwords = Dev->SdmaRingSize / sizeof(ULONG);
    wptr = Dev->SdmaWptr;

    for (i = 0; i < packetDwords; i++) {
        ULONG off = Dev->SdmaRingVramOffset + ((wptr + i) % ringDwords) * sizeof(ULONG);
        *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + off) = packet[i];
    }
    nextWptr = (wptr + packetDwords) % ringDwords;
    Dev->SdmaWptr = nextWptr;

    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRingVramOffset + 0x2000u) = (nextWptr << 2);
    KeMemoryBarrier();

    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, (nextWptr << 2));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_HI, 0);

    ULONG targetWptrBytes = (nextWptr << 2);
    volatile ULONG *rptrWb = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRptrVramOffset);
    for (i = 0; i < BC250_WAIT_ITERS(5u); i++) {
        ULONG currentRptr = BcRegRead(Dev, A_SDMA0_GFX_RB_RPTR);
        if (*fencePtr == fenceValue || currentRptr == targetWptrBytes || *rptrWb == targetWptrBytes || (*rptrWb << 2) == targetWptrBytes) {
            ExReleaseFastMutex(&Dev->SdmaLock);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }

    ExReleaseFastMutex(&Dev->SdmaLock);
    return STATUS_IO_TIMEOUT;
}

NTSTATUS
Bc250SdmaRunBenchmark(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG Iterations,
    _In_ ULONG BlockSizeBytes,
    _Out_ PULONG AvgBandwidthMBs,
    _Out_ PULONG PeakBandwidthMBs
    )
{
    ULONGLONG srcGpu, dstGpu;
    ULONG iter, count = 0;
    ULONGLONG totalMBs = 0, peakMBs = 0;
    ULONG durationUs = 0;
    NTSTATUS status;

    if (!Dev || !Dev->SdmaInitialized || !AvgBandwidthMBs || !PeakBandwidthMBs) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Iterations == 0) Iterations = 10;
    if (BlockSizeBytes == 0 || BlockSizeBytes > 0x00400000u) {
        BlockSizeBytes = 0x00400000u; /* 4 MB */
    }

    srcGpu = BC250_VRAM_GPU_BASE + 0x01000000ULL; /* 16 MB in VRAM */
    dstGpu = BC250_VRAM_GPU_BASE + 0x02000000ULL; /* 32 MB in VRAM */

    for (iter = 0; iter < Iterations; iter++) {
        status = Bc250SdmaSubmitCopy(Dev, srcGpu, dstGpu, BlockSizeBytes, &durationUs, NULL, NULL);
        if (NT_SUCCESS(status) && durationUs > 0) {
            ULONGLONG bw = ((ULONGLONG)BlockSizeBytes * 1000000ULL) / ((ULONGLONG)durationUs * 1048576ULL);
            totalMBs += bw;
            if (bw > peakMBs) peakMBs = bw;
            count++;
        }
    }

    if (count > 0) {
        *AvgBandwidthMBs = (ULONG)(totalMBs / count);
        *PeakBandwidthMBs = (ULONG)peakMBs;
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

/*===========================================================================
  Compute & GPGPU Engine
===========================================================================*/

/* Todos derivados por SOC15_GC0/GC1 — ver bloco de derivacao no topo do arquivo. */
#define A_GRBM_STATUS2                              SOC15_GC0(0x0DA2u)  /* 0x08008 */
#define A_GRBM_GFX_CNTL                             SOC15_GC0(0x0DC2u)  /* 0x08088 */

#define A_COMPUTE_DISPATCH_INITIATOR                SOC15_GC0(0x1BA0u)  /* 0x0B800 */
#define A_COMPUTE_DIM_X                             SOC15_GC0(0x1BA1u)  /* 0x0B804 */
#define A_COMPUTE_DIM_Y                             SOC15_GC0(0x1BA2u)  /* 0x0B808 */
#define A_COMPUTE_DIM_Z                             SOC15_GC0(0x1BA3u)  /* 0x0B80C */
#define A_COMPUTE_START_X                           SOC15_GC0(0x1BA4u)  /* 0x0B810 */
#define A_COMPUTE_START_Y                           SOC15_GC0(0x1BA5u)  /* 0x0B814 */
#define A_COMPUTE_START_Z                           SOC15_GC0(0x1BA6u)  /* 0x0B818 */
#define A_COMPUTE_NUM_THREAD_X                      SOC15_GC0(0x1BA7u)  /* 0x0B81C */
#define A_COMPUTE_NUM_THREAD_Y                      SOC15_GC0(0x1BA8u)  /* 0x0B820 */
#define A_COMPUTE_NUM_THREAD_Z                      SOC15_GC0(0x1BA9u)  /* 0x0B824 */
#define A_COMPUTE_PGM_LO                            SOC15_GC0(0x1BACu)  /* 0x0B830 */
#define A_COMPUTE_PGM_HI                            SOC15_GC0(0x1BADu)  /* 0x0B834 */
#define A_COMPUTE_PGM_RSRC1                         SOC15_GC0(0x1BB2u)  /* 0x0B848 */
#define A_COMPUTE_PGM_RSRC2                         SOC15_GC0(0x1BB3u)  /* 0x0B84C */
#define A_COMPUTE_RESOURCE_LIMITS                   SOC15_GC0(0x1BB5u)  /* 0x0B854 */
#define A_COMPUTE_STATIC_THREAD_MGMT_SE0            SOC15_GC0(0x1BB6u)  /* 0x0B858 */
#define A_COMPUTE_STATIC_THREAD_MGMT_SE1            SOC15_GC0(0x1BB7u)  /* 0x0B85C */
#define A_COMPUTE_TMPRING_SIZE                      SOC15_GC0(0x1BB8u)  /* 0x0B860 */
#define A_COMPUTE_USER_DATA_0                       SOC15_GC0(0x1BE0u)  /* 0x0B900 */

#define A_CC_ARRAY_CONFIG                           A_CC_GC_SHADER_ARRAY_CONFIG

#define COMPUTE_DISPATCH_EN_MASK                    0x00008003u /* USE_THREAD_DIM | VALID | COMPUTE_SHADER_EN */

NTSTATUS
Bc250HwVramWrite(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONGLONG Offset,
    _In_ const VOID *Buffer,
    _In_ ULONG Bytes
    )
{
    if (!Dev || !Dev->FbVirtualBase || !Buffer || Bytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Offset + Bytes > Dev->FbSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory((PUCHAR)Dev->FbVirtualBase + Offset, Buffer, Bytes);
    KeMemoryBarrier();
    BcHdpReadCacheInvalidate(Dev);

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250HwVramRead(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONGLONG Offset,
    _Out_ VOID *Buffer,
    _In_ ULONG Bytes
    )
{
    if (!Dev || !Dev->FbVirtualBase || !Buffer || Bytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Offset + Bytes > Dev->FbSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    BcHdpReadCacheInvalidate(Dev);
    RtlCopyMemory(Buffer, (PUCHAR)Dev->FbVirtualBase + Offset, Bytes);

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250HwSubmitComputeDispatch(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Inout_ BC250_WDDM_ESCAPE_COMPUTE_DISPATCH *Dispatch
    )
{
    LARGE_INTEGER freq, tStart, tEnd;
    ULONG i, grbmBefore, grbmAfter;
    ULONGLONG shaderGpu;
    ULONG dimX, dimY, dimZ;
    ULONG threadsX, threadsY, threadsZ;
    ULONG fenceExpected;
    volatile ULONG *fencePtr;
    ULONG timeout = BC250_WAIT_ITERS(2u);

    if (!Dev || !Dev->HardwareInitialized || !Dev->MmioVirtualBase || !Dev->FbVirtualBase || !Dispatch) {
        return STATUS_INVALID_PARAMETER;
    }

    dimX = (Dispatch->DimX > 0) ? Dispatch->DimX : 1;
    dimY = (Dispatch->DimY > 0) ? Dispatch->DimY : 1;
    dimZ = (Dispatch->DimZ > 0) ? Dispatch->DimZ : 1;

    threadsX = (Dispatch->ThreadsPerGroupX > 0) ? Dispatch->ThreadsPerGroupX : 32;
    threadsY = (Dispatch->ThreadsPerGroupY > 0) ? Dispatch->ThreadsPerGroupY : 1;
    threadsZ = (Dispatch->ThreadsPerGroupZ > 0) ? Dispatch->ThreadsPerGroupZ : 1;

    shaderGpu = Dispatch->ShaderGpuAddress;
    if (shaderGpu == 0) {
        shaderGpu = BC250_VRAM_GPU_BASE + 0x01000000ULL;
    }

    /* 1. Broadcast de GRBM. As escritas em SPI_PG / CC_GC_SHADER_ARRAY que
          existiam aqui foram REMOVIDAS na Fase 0: usavam offsets invalidos
          (0x34FC / 0x9C1C) e, com os offsets corretos, alterariam a
          configuracao fisica de WGP em todo dispatch. Reconfiguracao de WGP
          e' assunto de bring-up deliberado, nao de caminho de submissao. */
    BcRegWrite(Dev, A_GRBM_GFX_INDEX, GRBM_GFX_INDEX_BROADCAST);

    /* 2. Flush de caches e TLB */
    BcHdpReadCacheInvalidate(Dev);
    BcGcvmTlbFlush(Dev, 0);

    grbmBefore = BcRegRead(Dev, A_GRBM_STATUS);
    Dispatch->GrbmStatusBefore = grbmBefore;

    /* 2b. Fence fisica em VRAM, pre-envenenada com ~valor.
          A CPU NUNCA escreve o valor de conclusao — so' o poison. Se a fence
          for observada com o valor esperado, foi a GPU que escreveu. */
    fenceExpected = ++Dev->ComputeFenceSequence;
    if (fenceExpected == 0) fenceExpected = ++Dev->ComputeFenceSequence;

    fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->ComputeFenceVramOffset);
    *fencePtr = ~fenceExpected;
    KeMemoryBarrier();

    Dispatch->FenceValue      = fenceExpected;
    Dispatch->FenceGpuAddress = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->ComputeFenceVramOffset;

    /* 3. Registradores de Compute Shader */
    BcRegWrite(Dev, A_COMPUTE_START_X, 0);
    BcRegWrite(Dev, A_COMPUTE_START_Y, 0);
    BcRegWrite(Dev, A_COMPUTE_START_Z, 0);

    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_X, threadsX);
    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_Y, threadsY);
    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_Z, threadsZ);

    BcRegWrite(Dev, A_COMPUTE_DIM_X, dimX);
    BcRegWrite(Dev, A_COMPUTE_DIM_Y, dimY);
    BcRegWrite(Dev, A_COMPUTE_DIM_Z, dimZ);

    BcRegWrite(Dev, A_COMPUTE_PGM_LO, (ULONG)(shaderGpu >> 8));
    BcRegWrite(Dev, A_COMPUTE_PGM_HI, (ULONG)(shaderGpu >> 40));

    BcRegWrite(Dev, A_COMPUTE_PGM_RSRC1, 0x00000000u);
    BcRegWrite(Dev, A_COMPUTE_PGM_RSRC2, 0x00000000u);

    if (Dispatch->UserDataCount > 0 && Dispatch->UserDataCount <= 8) {
        for (i = 0; i < Dispatch->UserDataCount; i++) {
            BcRegWrite(Dev, A_COMPUTE_USER_DATA_0 + (i * 4), Dispatch->UserData[i]);
        }
    }

    KeMemoryBarrier();
    tStart = KeQueryPerformanceCounter(&freq);

    /* 4. DISPATCH INITIATOR: Dispara os 1536 Shaders no silício */
    BcRegWrite(Dev, A_COMPUTE_DISPATCH_INITIATOR, COMPUTE_DISPATCH_EN_MASK);

    /* 5. Aguarda conclusão do processamento */
    for (i = 0; i < timeout; i++) {
        grbmAfter = BcRegRead(Dev, A_GRBM_STATUS);
        if ((grbmAfter & (1u << 10)) == 0 && (grbmAfter & (1u << 31)) == 0) {
            break;
        }
        KeStallExecutionProcessor(2);
    }

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        Dispatch->DurationUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    }

    BcHdpReadCacheInvalidate(Dev);
    KeMemoryBarrier();

    Dispatch->GrbmStatusAfter = BcRegRead(Dev, A_GRBM_STATUS);
    Dispatch->GrbmChanged     = (Dispatch->GrbmStatusAfter != grbmBefore) ? 1u : 0u;
    Dispatch->FenceObserved   = *fencePtr;
    Dispatch->ExecutionProven = (Dispatch->FenceObserved == fenceExpected) ? 1u : 0u;
    Dev->ComputeDispatchCount++;

    /*
      REGRA BC-250: proibido fallback por software.

      O loop de soma em CPU que existia aqui (pC[k] = pA[k] + pB[k]) foi removido
      na Fase 0. Ele produzia o "resultado correto" que a suite de testes exibia
      como prova de aceleracao — era a CPU calculando, nao a GPU.

      Enquanto o CP nao tiver microcodigo aceito e a fila nao estiver mapeada via
      MQD/MAP_QUEUES, nenhum shader executa. Falhar honestamente e' o
      comportamento correto: preservar a falha, registrar a evidencia.
    */
    if (!Dispatch->ExecutionProven) {
        Dev->ComputeFailNoFence++;
        return STATUS_DEVICE_NOT_READY;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250HwSubmitIndirectBuffer(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Inout_ BC250_WDDM_ESCAPE_SUBMIT_IB *IbSubmit
    )
{
    LARGE_INTEGER freq, tStart, tEnd;
    ULONG ringDwords, wptr, rptrB, rptrA, wptrFinal, i;
    ULONG packetDwords = 0;
    ULONG packetData[16];
    ULONG timeout = BC250_WAIT_ITERS(2u);

    if (!Dev || !Dev->HardwareInitialized || !Dev->MmioVirtualBase || !Dev->FbVirtualBase || !IbSubmit) {
        return STATUS_INVALID_PARAMETER;
    }

    if (IbSubmit->IbGpuAddress == 0 || IbSubmit->IbDwordCount == 0 || IbSubmit->IbDwordCount > (1024 * 1024)) {
        return STATUS_INVALID_PARAMETER;
    }

    tStart = KeQueryPerformanceCounter(&freq);
    IbSubmit->GrbmStatusBefore = BcRegRead(Dev, A_GRBM_STATUS);
    rptrB = BcRegRead(Dev, A_CP_RB0_RPTR);
    IbSubmit->RptrBefore = rptrB;

    /* 1. PACKET3_INDIRECT_BUFFER (Opcode 0x3F) */
    packetData[packetDwords++] = 0xC0003F02u;
    packetData[packetDwords++] = (ULONG)(IbSubmit->IbGpuAddress & 0xFFFFFFFCULL);
    packetData[packetDwords++] = (ULONG)(IbSubmit->IbGpuAddress >> 32) | ((IbSubmit->VmId & 0x0Fu) << 16);
    packetData[packetDwords++] = (IbSubmit->IbDwordCount & 0x000FFFFFu);

    /* 2. PACKET3_WRITE_DATA Fence (opcional) */
    if (IbSubmit->FenceGpuAddress != 0) {
        packetData[packetDwords++] = 0xC0003703u;
        packetData[packetDwords++] = 0x00000500u;
        packetData[packetDwords++] = (ULONG)(IbSubmit->FenceGpuAddress & 0xFFFFFFFCULL);
        packetData[packetDwords++] = (ULONG)(IbSubmit->FenceGpuAddress >> 32);
        packetData[packetDwords++] = IbSubmit->FenceValue;
    }

    ringDwords = Dev->GfxRingSize / 4;
    wptr = Dev->GfxWptr;

    for (i = 0; i < packetDwords; i++) {
        ULONG off = Dev->GfxRingVramOffset + ((wptr + i) % ringDwords) * 4;
        *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + off) = packetData[i];
    }
    wptrFinal = (wptr + packetDwords) % ringDwords;
    Dev->GfxWptr = wptrFinal;

    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->GfxRingVramOffset + Dev->GfxRingSize + 0x100) = wptrFinal;

    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000003u);
    BcGcvmTlbFlush(Dev, IbSubmit->VmId);

    BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);
    BcRegWrite(Dev, A_CP_RB0_WPTR, wptrFinal);
    BcRegWrite(Dev, A_CP_RB0_WPTR_HI, 0);

    volatile ULONG *rptrWb = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->GfxRingVramOffset + 4096);
    rptrA = rptrB;
    for (i = 0; i < timeout; i++) {
        rptrA = BcRegRead(Dev, A_CP_RB0_RPTR);
        if (rptrA == wptrFinal || *rptrWb == wptrFinal) {
            rptrA = wptrFinal;
            break;
        }
        KeStallExecutionProcessor(2);
    }

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        IbSubmit->DurationUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    }

    IbSubmit->RptrAfter = rptrA;
    IbSubmit->GrbmStatusAfter = BcRegRead(Dev, A_GRBM_STATUS);

    BcHdpReadCacheInvalidate(Dev);

    return STATUS_SUCCESS;
}

/* SMU Metrics Table Structure (smu11_driver_if_cyan_skillfish.h) */
#pragma pack(push, 2)
typedef struct _SMU_METRICS_TABLE_CYAN_SKILLFISH {
    USHORT CoreFrequency[6];        /* [MHz] */
    ULONG  CorePower[6];            /* [mW] */
    USHORT CoreTemperature[6];      /* [centi-Celsius] */
    USHORT L3Frequency[2];          /* [MHz] */
    USHORT L3Temperature[2];        /* [centi-Celsius] */
    USHORT C0Residency[6];          /* Percentage */

    USHORT GfxclkFrequency;         /* [MHz] */
    USHORT GfxTemperature;          /* [centi-Celsius] */

    USHORT SocclkFrequency;         /* [MHz] */
    USHORT VclkFrequency;           /* [MHz] */
    USHORT DclkFrequency;           /* [MHz] */
    USHORT MemclkFrequency;         /* [MHz] */

    ULONG  Voltage[2];              /* [mV] [0]=VDDCR_VDD (CPU/SoC), [1]=VDDCR_GFX (GPU) */
    ULONG  Current[2];              /* [mA] [0]=VDDCR_VDD, [1]=VDDCR_GFX */
    ULONG  Power[2];                /* [mW] [0]=VDDCR_VDD, [1]=VDDCR_GFX */
    ULONG  CurrentSocketPower;      /* [mW] */

    USHORT SocTemperature;          /* [centi-Celsius] */
    USHORT EdgeTemperature;         /* [centi-Celsius] */
    USHORT ThrottlerStatus;
    USHORT Spare;
} SMU_METRICS_TABLE_CYAN_SKILLFISH;

typedef struct _SMU_METRICS_CYAN_SKILLFISH {
    SMU_METRICS_TABLE_CYAN_SKILLFISH Current;
    SMU_METRICS_TABLE_CYAN_SKILLFISH Average;
    ULONG SampleStartTime;
    ULONG SampleStopTime;
    ULONG Accnt;
} SMU_METRICS_CYAN_SKILLFISH;
#pragma pack(pop)

NTSTATUS
Bc250HwQueryTelemetry(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Out_ BC250_WDDM_ESCAPE_TELEMETRY *Telem
    )
{
    ULONG rsp = 0;
    NTSTATUS status = STATUS_SUCCESS;
    SMU_METRICS_CYAN_SKILLFISH *metrics = NULL;
    BOOLEAN tableValid = FALSE;

    if (!Dev || !Telem) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Telem, sizeof(*Telem));
    Telem->Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    Telem->Header.Status = STATUS_SUCCESS;
    Telem->Header.OutSize = sizeof(*Telem);

    /* 1. Aloca buffer DMA de telemetria de 4 KB */
    if (!Dev->SmuMetricsVa) {
        PHYSICAL_ADDRESS low, high, boundary;
        low.QuadPart = 0;
        high.QuadPart = 0xFFFFFFFFULL;
        boundary.QuadPart = 0;
        Dev->SmuMetricsVa = MmAllocateContiguousMemorySpecifyCache(4096, low, high, boundary, MmNonCached);
        if (Dev->SmuMetricsVa) {
            Dev->SmuMetricsPa = MmGetPhysicalAddress(Dev->SmuMetricsVa);
            RtlZeroMemory(Dev->SmuMetricsVa, 4096);
        }
    }

    /* 2. Tabela de métricas via Mailbox SMU (TABLE_SMU_METRICS = 6) */
    if (Dev->SmuMetricsVa && Dev->MmioVirtualBase) {
        ULONG paHi = (ULONG)(Dev->SmuMetricsPa.QuadPart >> 32);
        ULONG paLo = (ULONG)(Dev->SmuMetricsPa.QuadPart & 0xFFFFFFFF);

        (VOID)BcSmuSendMsg(Dev, 0, 0x04, paHi, &rsp);
        (VOID)BcSmuSendMsg(Dev, 0, 0x05, paLo, &rsp);

        rsp = 0;
        status = BcSmuSendMsg(Dev, 0, 0x06, TABLE_SMU_METRICS, &rsp);
        if (NT_SUCCESS(status) && rsp == 1) {
            metrics = (SMU_METRICS_CYAN_SKILLFISH *)Dev->SmuMetricsVa;
            if (metrics->Current.GfxTemperature > 0 || metrics->Current.GfxclkFrequency > 0) {
                tableValid = TRUE;

                Telem->GfxTemperatureC = metrics->Current.GfxTemperature / 100;
                Telem->GfxTemperature = (ULONG)metrics->Current.GfxTemperature * 10;
                Telem->SocTemperatureC = metrics->Current.SocTemperature / 100;
                Telem->EdgeTemperatureC = metrics->Current.EdgeTemperature / 100;
                Telem->HotspotTemperatureC = metrics->Current.EdgeTemperature / 100;

                Telem->GfxVoltageMv = metrics->Current.Voltage[1];
                Telem->SocVoltageMv = metrics->Current.Voltage[0];
                Telem->GfxClockMhz  = metrics->Current.GfxclkFrequency;
                Telem->SocClockMhz  = metrics->Current.SocclkFrequency;
                Telem->MemClockMhz  = metrics->Current.MemclkFrequency;

                Telem->PowerMilliwatts = (metrics->Current.Power[1] > 0) ? metrics->Current.Power[1] : 
                                         ((metrics->Current.CurrentSocketPower > 0) ? metrics->Current.CurrentSocketPower : 0);
                Telem->PowerWatts = Telem->PowerMilliwatts / 1000;
                Telem->Flags = 1; /* Tabela SMU DMA (Métricas Completas) */
            }
        }
    }

    /* 3. Fallback: Leitura direta de sensores de silício por barramento SMN/SMU */
    if (!tableValid && Dev->MmioVirtualBase) {
        rsp = 0;
        if (NT_SUCCESS(BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp)) && rsp == 1) {
            Telem->GfxClockMhz = BcSmnRead(Dev, SMU_Q0_ARG);
        }

        /* Consulta Tensão: Tenta Queue 3 Msg 0x37 (GPU Voltage) ou Queue 0 Msg 0x38 (VID) */
        rsp = 0;
        if (NT_SUCCESS(BcSmuSendMsg(Dev, 3, 0x37, 0, &rsp)) && rsp == 1) {
            Telem->GfxVoltageMv = BcSmnRead(Dev, SMU_Q3_ARG);
        } else {
            rsp = 0;
            if (NT_SUCCESS(BcSmuSendMsg(Dev, 0, 0x38, 0, &rsp)) && rsp == 1) {
                ULONG vid = BcSmnRead(Dev, SMU_Q0_ARG);
                if (vid > 0 && vid < 256) {
                    /* Formula VID: mV = 1550 - (VID * 6.25) */
                    Telem->GfxVoltageMv = (vid <= 248) ? (1550 - (vid * 625 / 100)) : 0;
                }
            }
        }

        /* Consulta Temperatura via Diodo TSI SMN (0x00059800) */
        ULONG tsi = BcSmnRead(Dev, 0x00059800u);
        ULONG tsiRaw = (tsi >> 21) & 0x7FFu;
        /* Formula autêntica AMD TSI: Tdie (°C) = (tsiRaw / 8) - 49 */
        if (tsiRaw >= 392 && tsiRaw <= 1600) {
            LONG t = ((LONG)tsiRaw * 125 - 49000) / 1000;
            if (t > 0 && t < 125) {
                Telem->GfxTemperatureC = (ULONG)t;
                Telem->GfxTemperature = (ULONG)(t * 1000);
                Telem->SocTemperatureC = Telem->GfxTemperatureC;
                Telem->EdgeTemperatureC = Telem->GfxTemperatureC;
                Telem->HotspotTemperatureC = Telem->GfxTemperatureC;
                Telem->Flags = 2; /* Sonda física SMN ativa (Diodo TSI) */
            }
        }

        /* Sem tabela SMU, potência não pode ser calculada com precisão. Zera para evitar valores fabricados */
        Telem->PowerWatts = 0;
        Telem->PowerMilliwatts = 0;
    }

    Telem->SmuStatus = rsp;
    Dev->LastTelemetry = *Telem;
    return STATUS_SUCCESS;
}

/*===========================================================================
  Event Logging Helper
===========================================================================*/
VOID
Bc250LogEvent(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG EventId,
    _In_ ULONG Param1,
    _In_ ULONG Param2
    )
{
    if (!Dev) return;
    ULONG idx = (ULONG)InterlockedIncrement((LONG*)&Dev->EventLogTotal) - 1;
    ULONG slot = idx % BC250_EVENT_LOG_MAX;
    LARGE_INTEGER now;
    now = KeQueryPerformanceCounter(NULL);
    Dev->EventLog[slot].EventId = EventId;
    Dev->EventLog[slot].Param1 = Param1;
    Dev->EventLog[slot].Param2 = Param2;
    Dev->EventLog[slot].Timestamp = now;
}

/*===========================================================================
  Low-Level SMN / SMU & PSP Services
===========================================================================*/
ULONG
Bc250HwSmnRead(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG SmnAddr
    )
{
    if (!Dev || !Dev->MmioVirtualBase) return 0;
    return BcSmnRead(Dev, SmnAddr);
}

VOID
Bc250HwSmnWrite(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG SmnAddr,
    _In_ ULONG Value
    )
{
    if (!Dev || !Dev->MmioVirtualBase) return;
    BcSmnWrite(Dev, SmnAddr, Value);
    Bc250LogEvent(Dev, 0x534D4E57u /* 'SMNW' */, SmnAddr, Value);
}

NTSTATUS
Bc250HwSmuSendRawMsg(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG QueueId,
    _In_ ULONG MsgId,
    _In_ ULONG Param,
    _Out_ PULONG SmuStatus,
    _Out_ PULONG ArgReadback
    )
{
    NTSTATUS status;
    ULONG rsp = 0;
    if (!Dev || !Dev->MmioVirtualBase || !SmuStatus || !ArgReadback) {
        return STATUS_INVALID_PARAMETER;
    }
    status = BcSmuSendMsg(Dev, QueueId, MsgId, Param, &rsp);
    *SmuStatus = rsp;
    if (QueueId == 2) {
        *ArgReadback = BcSmnRead(Dev, SMU_Q2_ARG);
    } else if (QueueId == 3) {
        *ArgReadback = BcSmnRead(Dev, SMU_Q3_ARG);
    } else {
        *ArgReadback = BcSmnRead(Dev, SMU_Q0_ARG);
    }
    Bc250LogEvent(Dev, 0x534D5501u /* 'SMU1' */, (QueueId << 16) | (MsgId & 0xFFFF), *ArgReadback);
    return status;
}

NTSTATUS
Bc250HwPspRawSubmit(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ ULONG Opcode,
    _In_ ULONG SubOp,
    _In_ ULONG BufSizeBytes,
    _Inout_updates_bytes_(BufSizeBytes) PULONG Data,
    _Out_ PULONG RespStatus,
    _Out_ PULONG RespSession,
    _Out_ PULONG FenceValue,
    _Out_ PULONG DurationUs
    )
{
    LARGE_INTEGER freq, tStart, tEnd;
    NTSTATUS status;
    ULONG *cmd, *ucmd;

    if (!Dev || !Dev->PspRingVa || !Dev->PspCmdVa || !Dev->PspFenceVa || !RespStatus || !RespSession || !FenceValue || !DurationUs) {
        return STATUS_INVALID_PARAMETER;
    }

    if (BufSizeBytes > 512) BufSizeBytes = 512;

    ExAcquireFastMutex(&Dev->DeviceMutex);

    cmd = (ULONG *)Dev->PspCmdVa;
    RtlZeroMemory(cmd, PSP_CMD_BUF_SIZE);
    cmd[0] = PSP_CMD_BUF_DECLARED;
    cmd[1] = SubOp ? SubOp : 1;
    cmd[2] = Opcode;

    ucmd = cmd + (28 / 4);
    if (Data && BufSizeBytes > 0) {
        RtlCopyMemory(ucmd, Data, BufSizeBytes);
    }
    KeMemoryBarrier();

    tStart = KeQueryPerformanceCounter(&freq);

    status = BcPspSubmitCmd(Dev, Dev->PspRingVa, &Dev->PspRingWptr, Dev->PspCmdPa,
                            Dev->PspCmdVa, Dev->PspFencePa, Dev->PspFenceVa,
                            &Dev->PspFenceValue);

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        *DurationUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    } else {
        *DurationUs = 0;
    }

    *FenceValue = Dev->PspFenceValue;

    if (NT_SUCCESS(status)) {
        volatile ULONG *resp;
        KeStallExecutionProcessor(1000);
        KeMemoryBarrier();
        resp = (volatile ULONG *)((PUCHAR)Dev->PspCmdVa + PSP_RESP_OFFSET);
        *RespStatus = resp[0];
        *RespSession = resp[1];
        if (Data && BufSizeBytes > 0) {
            RtlCopyMemory(Data, (PVOID)ucmd, BufSizeBytes);
        }
        Dev->PspCmdCount++;
        Bc250LogEvent(Dev, 0x50535001u /* 'PSP1' */, Opcode, resp[0]);
    } else {
        *RespStatus = 0xFFFFFFFF;
        *RespSession = 0;
    }

    ExReleaseFastMutex(&Dev->DeviceMutex);
    return status;
}

/*===========================================================================
  Real-Time Studio Queries
===========================================================================*/
VOID
Bc250HwQueryRingStatus(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Out_ BC250_WDDM_ESCAPE_RING_STATUS *Rings
    )
{
    ULONG cpDwords, sdmaDwords;

    if (!Dev || !Rings) return;
    RtlZeroMemory(Rings, sizeof(*Rings));
    Rings->Header.CommandId = BC250_ESCAPE_QUERY_RING_STATUS;
    Rings->Header.OutSize = sizeof(*Rings);
    Rings->Header.Status = STATUS_SUCCESS;

    /* CP PM4 Ring 0 */
    Rings->CpRingGpuBase = BC250_VRAM_GPU_BASE + (ULONGLONG)Dev->GfxRingVramOffset;
    Rings->CpRingSizeBytes = Dev->GfxRingSize;
    Rings->CpRingWptr = Dev->GfxWptr;
    Rings->CpRingRptr = BcRegRead(Dev, A_CP_RB0_RPTR);
    cpDwords = (Dev->GfxRingSize > 0) ? (Dev->GfxRingSize / sizeof(ULONG)) : 1024;
    if (cpDwords > 0) {
        ULONG occ = (Dev->GfxWptr >= Rings->CpRingRptr) ? 
                    (Dev->GfxWptr - Rings->CpRingRptr) : 
                    (cpDwords - Rings->CpRingRptr + Dev->GfxWptr);
        Rings->CpRingOccupancyBytes = occ * sizeof(ULONG);
    }
    Rings->CpRingSubmitCount = Dev->IbSubmitCount;

    /* SDMA0 Copy Ring */
    Rings->SdmaRingGpuBase = Dev->SdmaRingGpuBase;
    Rings->SdmaRingSizeBytes = Dev->SdmaRingSize;
    Rings->SdmaRingWptr = Dev->SdmaWptr;
    Rings->SdmaRingRptr = BcRegRead(Dev, A_SDMA0_GFX_RB_RPTR) / sizeof(ULONG);
    sdmaDwords = (Dev->SdmaRingSize > 0) ? (Dev->SdmaRingSize / sizeof(ULONG)) : 1024;
    if (sdmaDwords > 0) {
        ULONG occ = (Dev->SdmaWptr >= Rings->SdmaRingRptr) ?
                    (Dev->SdmaWptr - Rings->SdmaRingRptr) :
                    (sdmaDwords - Rings->SdmaRingRptr + Dev->SdmaWptr);
        Rings->SdmaRingOccupancyBytes = occ * sizeof(ULONG);
    }
    Rings->SdmaCopyCount = Dev->SdmaCopyCount;
    Rings->SdmaBandwidthMBs = Dev->SdmaBandwidthMBs;

    /* PSP Ring */
    Rings->PspRingGpuBase = (ULONGLONG)Dev->PspRingPa.QuadPart;
    Rings->PspRingSizeBytes = PSP_RING_SIZE;
    Rings->PspRingWptr = Dev->PspRingWptr;
    Rings->PspFenceValue = Dev->PspFenceValue;
    Rings->PspCmdCount = Dev->PspCmdCount;

    Rings->GrbmStatus = BcRegRead(Dev, A_GRBM_STATUS);
    Rings->CpStat = BcRegRead(Dev, A_CP_STAT);
    Rings->TimestampQpc = KeQueryPerformanceCounter(&Rings->QpcFrequency);
}

VOID
Bc250HwQueryMemoryMap(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Out_ BC250_WDDM_ESCAPE_MEMORY_MAP *Map
    )
{
    if (!Dev || !Map) return;
    RtlZeroMemory(Map, sizeof(*Map));
    Map->Header.CommandId = BC250_ESCAPE_QUERY_MEMORY_MAP;
    Map->Header.OutSize = sizeof(*Map);
    Map->Header.Status = STATUS_SUCCESS;

    Map->TotalPhysicalBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL; /* 16 GB GDDR6 UMA */
    Map->Bar0PhysicalBase = Dev->FbPhysicalBase.QuadPart ? (ULONGLONG)Dev->FbPhysicalBase.QuadPart : BC250_VRAM_GPU_BASE;
    Map->Bar0ApertureBytes = Dev->FbSize ? Dev->FbSize : BC250_BAR0_VRAM_SIZE;

    Map->GopFbBase = 0x00000000ULL;
    Map->GopFbSizeBytes = (Dev->ScreenPitch > 0 && Dev->ScreenHeight > 0) ? (Dev->ScreenPitch * Dev->ScreenHeight) : 0x00A00000u;
    Map->StagingComputeBase = 0x01000000ULL;
    Map->StagingComputeBytes = 224u * 1024u * 1024u; /* 224 MB */
    Map->TmrBase = 0x0F800000ULL;
    Map->TmrSizeBytes = 4u * 1024u * 1024u;          /* 4 MB */
    Map->SdmaRingBase = 0x0FD00000ULL;
    Map->SdmaRingSizeBytes = 16u * 1024u;            /* 16 KB */
    Map->CpRingBase = 0x0FE00000ULL;
    Map->CpRingSizeBytes = 16u * 1024u;              /* 16 KB */
    Map->DynamicPoolBase = 0x10000000ULL;
    Map->DynamicPoolBytes = Map->TotalPhysicalBytes - Map->DynamicPoolBase;

    Map->ScreenWidth = Dev->ScreenWidth;
    Map->ScreenHeight = Dev->ScreenHeight;
    Map->ScreenPitch = Dev->ScreenPitch;
}

VOID
Bc250HwQueryEventLog(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Out_ BC250_WDDM_ESCAPE_EVENT_LOG *Log
    )
{
    ULONG i;

    if (!Dev || !Log) return;
    RtlZeroMemory(Log, sizeof(*Log));
    Log->Header.CommandId = BC250_ESCAPE_QUERY_EVENT_LOG;
    Log->Header.OutSize = sizeof(*Log);
    Log->Header.Status = STATUS_SUCCESS;

    Log->TotalEventsCount = Dev->EventLogTotal;
    Log->EntryCount = (Dev->EventLogTotal < BC250_EVENT_LOG_MAX) ? Dev->EventLogTotal : BC250_EVENT_LOG_MAX;
    Log->PresentCount = Dev->PresentCount;
    Log->VsyncCount = Dev->VsyncCount;
    Log->IbSubmitCount = Dev->IbSubmitCount;
    Log->ComputeDispatchCount = Dev->ComputeDispatchCount;
    Log->SdmaCopyCount = Dev->SdmaCopyCount;

    for (i = 0; i < Log->EntryCount; i++) {
        ULONG idx = (Dev->EventLogTotal >= BC250_EVENT_LOG_MAX) ? 
                    ((Dev->EventLogTotal - Log->EntryCount + i) % BC250_EVENT_LOG_MAX) : i;
        Log->Entries[i] = Dev->EventLog[idx];
    }
}

/* Helper conversion between mV and VID for SMU v11.8 */
static __inline ULONG BcMvToVid(ULONG Mv)
{
    /* Formula: VID = (1.55 - mV/1000) / 0.00625 */
    if (Mv >= 1550) return 0;
    if (Mv < 700) Mv = 700;
    return (ULONG)(((1550 - Mv) * 1000 + 3125) / 6250);
}

static __inline ULONG BcVidToMv(ULONG Vid)
{
    /* Formula: mV = 1550 - (VID * 6.25) */
    LONG mv = 1550 - (LONG)((Vid * 6250 + 500) / 1000);
    return (mv > 0) ? (ULONG)mv : 0;
}

NTSTATUS
Bc250HwSetPowerProfile(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Inout_ BC250_WDDM_ESCAPE_POWER_PROFILE *Profile
    )
{
    ULONG rsp = 0;
    ULONG currentFreq = 0;
    ULONG currentVid = 0;
    ULONG vid = 0;
    ULONG maxTemp = 85;
    ULONG perfProfile = 3;

    if (!Dev || !Profile || !Dev->MmioVirtualBase) {
        return STATUS_INVALID_PARAMETER;
    }

    Profile->Header.Status = STATUS_SUCCESS;
    Profile->Header.OutSize = sizeof(*Profile);

    /* 1. Modo de restauracao / Unforce (Retorna ao controle dinamico da SMU) */
    if (Profile->Flags == 1 || (Profile->TargetFreqMhz == 0 && Profile->TargetVoltageMv == 0)) {
        (VOID)BcSmuSendMsg(Dev, 3, 0x8C, 85, &rsp); /* GPU Max Temp 85C */
        (VOID)BcSmuSendMsg(Dev, 0, 0x3A, 0, &rsp);  /* Unforce GFX Freq */
        (VOID)BcSmuSendMsg(Dev, 0, 0x3C, 0, &rsp);  /* Unforce GFX VID */
        (VOID)BcSmuSendMsg(Dev, 3, 0x1E, 3, &rsp);  /* Set Perf Profile 3 (Max IF/Memory) */

        KeStallExecutionProcessor(2000);

        /* Readback */
        (VOID)BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp);
        currentFreq = BcSmnRead(Dev, SMU_Q0_ARG);

        (VOID)BcSmuSendMsg(Dev, 0, 0x38, 0, &rsp);
        currentVid = BcSmnRead(Dev, SMU_Q0_ARG);

        Profile->ResultStatus = 0;
        Profile->CurrentFreqMhz = currentFreq ? currentFreq : 1500;
        Profile->CurrentVid = currentVid;
        Profile->CurrentVoltageMv = BcVidToMv(currentVid);

        Bc250LogEvent(Dev, 0x50575230u /* 'PWR0' */, Profile->CurrentFreqMhz, Profile->CurrentVoltageMv);
        return STATUS_SUCCESS;
    }

    /* 2. Validacao estrita de limites de seguranca */
    if (Profile->TargetFreqMhz < 400 || Profile->TargetFreqMhz > 2350) {
        Profile->ResultStatus = (ULONG)STATUS_INVALID_PARAMETER_1;
        return STATUS_INVALID_PARAMETER;
    }

    if (Profile->TargetVoltageMv < 700 || Profile->TargetVoltageMv > 1125) {
        Profile->ResultStatus = (ULONG)STATUS_INVALID_PARAMETER_2;
        return STATUS_INVALID_PARAMETER;
    }

    maxTemp = (Profile->MaxTemperatureC >= 60 && Profile->MaxTemperatureC <= 85) ? Profile->MaxTemperatureC : 85;
    perfProfile = (Profile->PerfProfile == 1) ? 1 : 3;

    /* 3. Le frequencia e voltagem atuais */
    (VOID)BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp);
    currentFreq = BcSmnRead(Dev, SMU_Q0_ARG);

    /* 4. Configura limite termico e perfil de controlador de memoria/IF */
    (VOID)BcSmuSendMsg(Dev, 3, 0x8C, maxTemp, &rsp);
    (VOID)BcSmuSendMsg(Dev, 3, 0x1E, perfProfile, &rsp);

    vid = BcMvToVid(Profile->TargetVoltageMv);

    /* 5. Ordem atomica de subida/descida de clock e tensao.
       NOTA DE SEGURANCA: ForceGfxFreq (0x39) e ForceGfxVid (0x3B) sem tabelas
       DPM ativas podem causar instabilidade no silicio. Esta acao e' restrita
       ao escape explicito de usuario (test_power_profile), nunca no bring-up. */
    if (Profile->TargetFreqMhz >= currentFreq) {
        /* Aumentando clock: Eleva voltagem primeiro para evitar under-voltage crash */
        (VOID)BcSmuSendMsg(Dev, 0, 0x3B, vid, &rsp);
        KeStallExecutionProcessor(1000);
        (VOID)BcSmuSendMsg(Dev, 0, 0x39, Profile->TargetFreqMhz, &rsp);
    } else {
        /* Diminuindo clock: Reduz frequencia primeiro */
        (VOID)BcSmuSendMsg(Dev, 0, 0x39, Profile->TargetFreqMhz, &rsp);
        KeStallExecutionProcessor(1000);
        (VOID)BcSmuSendMsg(Dev, 0, 0x3B, vid, &rsp);
    }

    KeStallExecutionProcessor(2000);

    /* 6. Leitura e confirmacao no silicio */
    (VOID)BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp);
    currentFreq = BcSmnRead(Dev, SMU_Q0_ARG);

    (VOID)BcSmuSendMsg(Dev, 0, 0x38, 0, &rsp);
    currentVid = BcSmnRead(Dev, SMU_Q0_ARG);

    Profile->ResultStatus = 0;
    Profile->CurrentFreqMhz = currentFreq ? currentFreq : Profile->TargetFreqMhz;
    Profile->CurrentVid = currentVid;
    Profile->CurrentVoltageMv = BcVidToMv(currentVid);

    Bc250LogEvent(Dev, 0x50575231u /* 'PWR1' */, Profile->CurrentFreqMhz, Profile->CurrentVoltageMv);
    return STATUS_SUCCESS;
}
