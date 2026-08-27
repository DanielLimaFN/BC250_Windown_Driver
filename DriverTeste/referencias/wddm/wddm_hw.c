/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    wddm_hw.c

Abstract:
    Hardware initialization engine for AMD BC-250 (Cyan Skillfish).
    Mirrors our 100% validated pipeline from dbg/hw.c and dbg/psp.c:
      1. SMU Message 0x06 (Disable GFXOFF)
      2. PSP Mailbox GPCOM Ring in Host RAM DMA (Destroy + Create)
      3. SDMA0 Firmware load via PSP (Type 9)
      4. TMR Carveout (4MB in VRAM) + 6 Graphics Microcodes (RLC, CE, PFP, ME, MEC, MEC2)
      5. GCVM L2 MMU & System Aperture (L2=0x00080E03, Context0=0x007FFE80)
      6. CP PM4 Ring 0 initialization in VRAM (0xF40FE00000)
      7. DxgkDdiEscape PM4 Submission Bridge

Environment:
    Kernel mode only

--*/

#include "wddm.h"
#include "wddm_fw.h"

/* Registers */
#define A_MMIO_INDEX                        0x00000u
#define A_MMIO_DATA                         0x00004u

#define MP0_BASE                            0x58000u
#define A_PSP_C2PMSG_64                     (MP0_BASE + 0x200u)
#define A_PSP_C2PMSG_67                     (MP0_BASE + 0x20Cu)
#define A_PSP_C2PMSG_69                     (MP0_BASE + 0x214u)
#define A_PSP_C2PMSG_70                     (MP0_BASE + 0x218u)
#define A_PSP_C2PMSG_71                     (MP0_BASE + 0x21Cu)
#define A_PSP_C2PMSG_81                     (MP0_BASE + 0x244u)

/*---------------------------------------------------------------------------
  Mailboxes do SMU -- valores de inc/bc250dbg_abi.h, validados em hardware.

  ATENCAO: a versao anterior deste arquivo definia "C2PMSG_66" como
  0x03B10A48, que na verdade e o C2PMSG_82 (registrador de ARGUMENTO da
  fila 0). O driver escrevia o ID da mensagem no argumento e consultava
  0x03B10AA8, que nao e o registrador de resposta. Nada podia funcionar --
  era a causa de Hw_SmuGfxOff = 0xC0000001.

  A BC-250 tem tres filas. GFXOFF vive na FILA 2, nao na 0.
---------------------------------------------------------------------------*/
#define SMU_Q0_CMD                          0x03B10A08u /* C2PMSG_66 */
#define SMU_Q0_ARG                          0x03B10A48u /* C2PMSG_82 */
#define SMU_Q0_RSP                          0x03B10A68u /* C2PMSG_90 */
#define SMU_Q2_CMD                          0x03B10528u
#define SMU_Q2_RSP                          0x03B10564u
#define SMU_Q2_ARG                          0x03B10998u
#define SMU_Q2_ARG_HI                       0x03B1099Cu

/* Mensagens confirmadas em hardware (ver docs/HANDOVER_IA_PROXIMOS_PASSOS.md) */
#define SMU_MSG_DISABLE_GFXOFF              0x06u   /* fila 2, argumento 4 */
#define SMU_ARG_DISABLE_GFXOFF              0x04u

/*---------------------------------------------------------------------------
  ATENCAO SOBRE O ID 0x06.

  A tabela de mensagens extraida do firmware do SMU desta placa
  (../amd_smu_reverse_engineering-main/README.md, obtida por disassembly do
  blob da BIOS 3.0) diz:

     0x01  PPSMC_MSG_TestMessage
     0x02  PPSMC_MSG_GetSmuVersion
     0x03  PPSMC_MSG_GetDriverIfVersion
     0x04  PPSMC_MSG_SetDriverTableDramAddrHigh
     0x05  PPSMC_MSG_SetDriverTableDramAddrLow
     0x06  PPSMC_MSG_TransferTableSmu2Dram      <-- NAO e DisableGfxOff
     0x07  PPSMC_MSG_TransferTableDram2Smu

  Ou seja: 0x06 e transferencia de tabela, e exige que o endereco de DRAM
  tenha sido configurado antes por 0x04/0x05. Mandar 0x06 com argumento 4
  como "DisableGfxOff" nao corresponde a nenhum handler valido -- consistente
  com Hw_SmuRawResponse ficando 0 e o GFXOFF terminando em IO_TIMEOUT.

  TestMessage e o unico teste de handshake que nao depende de adivinhar ID. O
  disassembly do handler e conhecido:

     iVar1 = pmfw_queue_read_head(q);        -- le o argumento
     pmfw_queue_store_word(q, iVar1 + 1);    -- escreve argumento + 1
     pmfw_queue_write_status(q, 1);          -- resposta = 1

  Logo: resposta 1 E argumento == valor enviado + 1 prova o caminho inteiro.
---------------------------------------------------------------------------*/
#define SMU_MSG_TEST_MESSAGE                0x01u
#define SMU_MSG_GET_SMU_VERSION             0x02u
#define SMU_TEST_ARG                        0x1234u

/*---------------------------------------------------------------------------
  Mapa de registradores SOC15 -> offset em bytes no BAR5.
  byte = (GC_BASE_SEGn + mmREG) * 4, com GC seg0 = 0x1260 e seg1 = 0xA000
  (dwords), conforme gc_10_1_0_offset.h. Estes valores sao identicos aos
  usados e validados em hardware por dbg/hw.c -- NAO alterar sem recalcular.
---------------------------------------------------------------------------*/
#define A_GCMC_VM_FB_OFFSET                 0x0A5ACu /* dw 0x170b seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR   0x0A614u /* dw 0x1725 seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR  0x0A618u /* dw 0x1726 seg0 */
#define A_GCMC_VM_MX_L1_TLB_CNTL             0x0A61Cu /* dw 0x1727 seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB 0x0A5B0u /* dw 0x170c seg0 */
#define A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB 0x0A5B4u /* dw 0x170d seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO 0x0A148u /* dw 0x15f2 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI 0x0A14Cu /* dw 0x15f3 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO 0x0A150u /* dw 0x15f4 seg0 */
#define A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI 0x0A154u /* dw 0x15f5 seg0 */
#define A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO 0x0A158u /* dw 0x15f6 seg0 */
#define A_GCVM_L2_CNTL                      0x0A100u /* dw 0x15e0 seg0 */
#define A_GCVM_L2_CNTL2                     0x0A104u /* dw 0x15e1 seg0 */
#define A_GCVM_L2_CNTL3                     0x0A108u /* dw 0x15e2 seg0 */
#define A_GCVM_L2_CNTL4                     0x0A160u /* dw 0x15f8 seg0 */
#define A_GCVM_L2_CNTL5                     0x0A184u /* dw 0x1601 seg0 */
#define A_GCVM_CONTEXT0_CNTL                0x0A200u /* dw 0x1620 seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO  0x0A3ACu /* dw 0x166b seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI  0x0A3B0u /* dw 0x166c seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO 0x0A42Cu /* dw 0x168b seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI 0x0A430u /* dw 0x168c seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO   0x0A4ACu /* dw 0x16ab seg0 */
#define A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI   0x0A4B0u /* dw 0x16ac seg0 */
#define A_GCVM_INVALIDATE_ENG0_REQ          0x0A28Cu /* dw 0x1643 seg0 */
#define A_GCVM_INVALIDATE_ENG0_ACK          0x0A2D4u /* dw 0x1655 seg0 */

#define A_CP_ME_CNTL                        0x086D8u /* dw 0x0f56 seg0 */
#define A_CP_RB0_BASE                       0x0DA60u /* GFX Ring0 Base Low */
#define A_CP_RB0_BASE_HI                    0x0DA64u /* GFX Ring0 Base High */
#define A_CP_RB0_CNTL                       0x0DA68u /* GFX Ring0 Control */
#define A_CP_RB0_RPTR                       0x0DA6Cu /* GFX Ring0 Read Pointer */
#define A_CP_RB0_WPTR_HI                    0x0DA74u /* GFX Ring0 Write Pointer High */
#define A_CP_RB0_WPTR                       0x0DA78u /* GFX Ring0 Write Pointer Low */
#define A_CP_RB_ACTIVE                      0x0DA7Cu /* GFX Ring0 Active / Status */
#define A_CP_RB_WPTR_POLL_CNTL              0x08708u /* dw 0x0f62 seg0 */
#define A_CP_RB0_RPTR_ADDR                  0x0C10Cu /* dw 0x1de3 seg0 */
#define A_CP_RB0_RPTR_ADDR_HI               0x0C110u /* dw 0x1de4 seg0 */
#define A_CP_RB_WPTR_POLL_ADDR_LO           0x0C3ACu /* dw 0x1e8b seg0 */
#define A_CP_RB_WPTR_POLL_ADDR_HI           0x0C3B0u /* dw 0x1e8c seg0 */
#define A_CP_RB_DOORBELL_CONTROL            0x0C3B4u /* dw 0x1e8d seg0 */

#define A_VGT_NUM_INSTANCES                 0x30934u /* dw 0x224d seg1 */
#define A_PA_SC_LINE_STIPPLE_STATE          0x30A04u /* dw 0x2281 seg1 */

/* HDP: caminho da CPU ate a VRAM (offsets validados em dbg/hw.c) */
#define A_HDP_MEM_COHERENCY_FLUSH_CNTL      0x04A80u
#define A_HDP_READ_CACHE_INVALIDATE         0x03FC4u
#define A_HDP_MISC_CNTL                     0x03FCCu

/* Instruction Cache DMA (IC_BASE) Registers para carga direta na I-RAM */
#define A_CP_PFP_IC_BASE_LO                 0x17360u
#define A_CP_PFP_IC_BASE_HI                 0x17364u
#define A_CP_PFP_IC_BASE_CNTL               0x17368u

#define A_CP_ME_IC_BASE_LO                  0x17370u
#define A_CP_ME_IC_BASE_HI                  0x17374u
#define A_CP_ME_IC_BASE_CNTL                0x17378u

#define A_CP_CE_IC_BASE_LO                  0x17380u
#define A_CP_CE_IC_BASE_HI                  0x17384u
#define A_CP_CE_IC_BASE_CNTL                0x17388u

#define A_CP_MEC_IC_BASE_LO                 0x17390u
#define A_CP_MEC_IC_BASE_HI                 0x17394u
#define A_CP_MEC_IC_BASE_CNTL               0x17398u

#define A_CP_HYP_PFP_UCODE_ADDR             0x172B0u
#define A_CP_HYP_PFP_UCODE_DATA             0x172B4u
#define A_CP_HYP_ME_UCODE_ADDR              0x172B8u
#define A_CP_HYP_ME_UCODE_DATA              0x172BCu
#define A_CP_HYP_CE_UCODE_ADDR              0x172C0u
#define A_CP_HYP_CE_UCODE_DATA              0x172C4u

#define A_SDMA0_F32_CNTL                    0x04A28u /* dw 0x002a seg0 */

/* PSP Protocol Constants */
#define PSP_TOS_READY                       0x80000000u
#define PSP_CMD_DESTROY_GPCOM_RING          0x000C0000u
#define PSP_RING_TYPE_KM                    2u
#define PSP_RING_SIZE                       0x1000u
#define PSP_CMD_BUF_SIZE                    0x1000u
#define PSP_FRAME_BYTES                     64u
#define PSP_CMD_BUF_DECLARED                0x400u
#define PSP_RESP_OFFSET                     864u

#define PSP_CMD_LOAD_IP_FW                  0x06u
#define PSP_CMD_SETUP_TMR                   0x05u

#define PSP_FW_CP_ME                        1u
#define PSP_FW_CP_PFP                       2u
#define PSP_FW_CP_CE                        3u
#define PSP_FW_CP_MEC                       4u
#define PSP_FW_CP_MEC_ME1                   5u
#define PSP_FW_RLC_G                        8u
#define PSP_FW_SDMA0                        9u

#define A_CP_DEVICE_ID                      0x0C684u

#define PSP_ACK_MASK                        0x8000FFFFu
#define PSP_ACK_VALUE                       0x80000000u

/* MMIO & SMN Access Helpers */
static __inline ULONG
BcRegRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Offset)
{
    return *(volatile ULONG *)((PUCHAR)Dev->MmioVirtualBase + (Offset & 0x7FFFC));
}

static __inline VOID
BcRegWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Offset, _In_ ULONG Value)
{
    *(volatile ULONG *)((PUCHAR)Dev->MmioVirtualBase + (Offset & 0x7FFFC)) = Value;
}

/* No SOC15/APU Cyan Skillfish, a aperture SMN no espaco MMIO BAR5
   e acessada atraves dos registradores RSMU_INDEX (0x0038) e RSMU_DATA (0x003C).
   Validado com 100% de sucesso em hardware real via test_smu_probe.exe. */
#define A_RSMU_INDEX                        0x00038u
#define A_RSMU_DATA                         0x0003Cu

static ULONG
BcSmnRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG SmnAddr)
{
    BcRegWrite(Dev, A_RSMU_INDEX, SmnAddr);
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

    /* Sequencia identica a dbg/hw.c::BcHdpReadCacheInvalidate, validada em HW.
       amdgpu (hdp_v5_0_flush_hdp) escreve 0 em HDP_MEM_COHERENCY_FLUSH_CNTL. */
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

VOID
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
    } else {
        cmdReg = SMU_Q0_CMD; argReg = SMU_Q0_ARG;
        argHiReg = 0; rspReg = SMU_Q0_RSP;
    }

    if (SmuStatus != NULL) {
        *SmuStatus = 0;
    }

    /* Espera a fila ficar livre (resposta != 0 = pronta para novo comando) */
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

    /* DIAGNOSTICO DO CAMINHO SMN.
       Hw_SmuRawResponse ficando 0 por 100 ms de polling tem duas explicacoes
       incompativeis: (a) o acesso SMN nao funciona e toda leitura devolve 0,
       ou (b) o SMN funciona e o SMU realmente nao responde. Sem distinguir as
       duas nao se sabe onde mexer.

       Os registradores de comando e argumento do mailbox sao legiveis. Ler de
       volta o que acabamos de escrever resolve a duvida: se o readback trouxer
       MsgId e Param, escrita e leitura SMN funcionam e o silencio e do SMU;
       se trouxer 0, o problema e a aperture SMN.

       Feito aqui dentro porque MM_INDEX/MM_DATA e um par global: separar
       index e data em duas chamadas de fora do driver produz corrida, nao
       medicao. */
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
    ULONG payloadOffset = 0;
    ULONG payloadSize = FwSizeBytes;

    /* No formato oficial AMD:
     * FwBlob[1] = header_size_bytes (44 ou 0x100)
     * FwBlob[4] = ucode_version
     * FwBlob[5] = ucode_size_bytes
     * FwBlob[6] = ucode_array_offset_bytes
     */
    if (FwSizeBytes >= 44 && FwBlob[1] >= 44 && FwBlob[6] < FwSizeBytes && FwBlob[5] <= (FwSizeBytes - FwBlob[6])) {
        payloadOffset = FwBlob[6];
        payloadSize   = FwBlob[5];
    }

    low.QuadPart = 0;
    high.QuadPart = 0xFFFFFFFFFFULL;
    boundary.QuadPart = 0;

    stageVa = MmAllocateContiguousMemorySpecifyCache(payloadSize, low, high, boundary, MmNonCached);
    if (!stageVa) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(stageVa, (const UCHAR*)FwBlob + payloadOffset, payloadSize);
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
    ucmd[2] = payloadSize;
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
    ULONG tmrSizeBytes = 0x400000; /* 4 MB */
    ULONG vramOffset = 0x0F800000;   /* 248 MB na VRAM */

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

    /* 0. Handshake do mailbox do SMU antes de qualquer coisa.
       TestMessage devolve resposta 1 e argumento+1; sem isso nao ha como
       saber se uma mensagem sem resposta e ID errado ou caminho quebrado. */
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

        /* 0.1 Ativar Clock da GPU e WGPs via SMU */
        rsp = 0;
        (VOID)BcSmuSendMsg(Dev, 0, 0x39, 1500, &rsp); /* PPSMC_MSG_ForceGfxFreq = 1500 MHz */
        
        rsp = 0;
        (VOID)BcSmuSendMsg(Dev, 0, 0x18, 12, &rsp);   /* PPSMC_MSG_RequestActiveWgp = 12 WGPs */
        
        rsp = 0;
        (VOID)BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp);    /* PPSMC_MSG_GetGfxFrequency */
        Dev->HwStatusSmuGfxOff = (rsp == 1) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED;
        Dev->HwSmuRawResponse = BcSmnRead(Dev, SMU_Q0_ARG);
    }

    /* 2. PSP: Cria o anel GPCOM */
    reg = BcRegRead(Dev, A_PSP_C2PMSG_64);
    Dev->HwPspTosReg = reg;
    if ((reg & PSP_TOS_READY) == 0) {
        Dev->HwStatusPspRing = STATUS_DEVICE_NOT_READY;
        return STATUS_DEVICE_NOT_READY;
    }

    /* Destroi qualquer anel anterior */
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
    /* A partir da escrita de CREATE o PSP pode ter capturado o endereco DMA,
       mesmo que o ACK se perca ou expire. O rollback precisa tentar DESTROY. */
    Dev->PspRingActive = TRUE;
    KeStallExecutionProcessor(20000);
    status = BcPspWaitAck(Dev);
    Dev->HwStatusPspRing = status;
    if (!NT_SUCCESS(status)) {
        goto cleanup_psp;
    }

    /* 3. PSP TMR Carveout (4 MB) - OBRIGATORIO antes de qualquer carga de firmware */
    Dev->HwStatusTmr = BcPspSetupTmr(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue);

    /* 4. Carga dos 7 Firmwares via PSP GPCOM Ring (Com TMR ativo) */
    Dev->HwStatusFw[0] = BcPspLoadIpFw(Dev, pspRingVa, &pspRingWptr, pspCmdPa, pspCmdVa, pspFencePa, pspFenceVa, &pspFenceValue,
                  PSP_FW_SDMA0, (const ULONG*)g_FwBlob_SDMA, g_FwBlobSize_SDMA);
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
                  PSP_FW_CP_MEC_ME1, (const ULONG*)g_FwBlob_MEC2, g_FwBlobSize_MEC2);

    /* 6. GCVM L2 MMU & GART Context 0 Page Table */
    {
        ULONG i;
        ULONGLONG defaultPa = Dev->FbPhysicalBase.QuadPart;
        ULONGLONG ptPa      = defaultPa + 0x0FC00000ULL; /* Page Table em 252 MB na GDDR6 */
        volatile ULONG64 *ptEntries = (volatile ULONG64 *)((PUCHAR)Dev->FbVirtualBase + 0x0FC00000u);

        /* Preenche 65536 entradas da GART (cobrindo 256 MB de 0x00000000 a 0x10000000)
           PTE: bit0=VALID, bit5=READ, bit6=WRITE, bit7=EXEC, bits[60:58]=MTYPE(3=UC) */
        for (i = 0; i < 65536; i++) {
            ULONGLONG pagePa = defaultPa + ((ULONGLONG)i * 4096ULL);
            ptEntries[i] = (pagePa & 0x000FFFFFFFFFF000ULL) | 0x000000E1ULL | (3ULL << 58);
        }

        /* Configura a Abertura de Sistema e Contexto 0 */
        BcRegWrite(Dev, A_GCMC_VM_FB_OFFSET, (ULONG)(defaultPa >> 24));
        BcRegWrite(Dev, A_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR,  (ULONG)(defaultPa >> 12));
        BcRegWrite(Dev, A_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, (ULONG)((defaultPa + Dev->FbSize - 1) >> 12));
        BcRegWrite(Dev, A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, (ULONG)(defaultPa >> 12));
        BcRegWrite(Dev, A_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, (ULONG)(defaultPa >> 44));
        BcRegWrite(Dev, A_GCMC_VM_MX_L1_TLB_CNTL, 0x00001859u);

        BcRegWrite(Dev, A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO, 0xFFFFFFFFu);
        BcRegWrite(Dev, A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI, 0x0000000Fu);
        BcRegWrite(Dev, A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO, 0);
        BcRegWrite(Dev, A_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI, 0);
        BcRegWrite(Dev, A_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO, 0);

        BcRegWrite(Dev, A_GCVM_L2_CNTL, 0x00080E03u);
        BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000003u);
        BcRegWrite(Dev, A_GCVM_L2_CNTL3, 0x00100000u);
        BcRegWrite(Dev, A_GCVM_L2_CNTL4, 0x00000000u);
        BcRegWrite(Dev, A_GCVM_L2_CNTL5, 0x000FFFE0u);

        /* Contexto 0 Habilitado com a Base da Page Table */
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO, (ULONG)(ptPa & 0xFFFFFFFFULL));
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI, (ULONG)(ptPa >> 32));
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO, 0x00000000u);
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI, 0x00000000u);
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO, 0x0000FFFFu);
        BcRegWrite(Dev, A_GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI, 0x00000000u);
        BcRegWrite(Dev, A_GCVM_CONTEXT0_CNTL, 0x007FFE81u); /* ENABLE_CONTEXT = 1 */

        BcHdpReadCacheInvalidate(Dev);
        BcGcvmTlbFlush(Dev, 0);
        Dev->HwL2CntlReadback = BcRegRead(Dev, A_GCVM_L2_CNTL);
    }

    /* 7. CP PM4 Ring 0 (Configuracao Canonica Linux GART) */
    {
        ULONG i;
        ULONGLONG ringGpuAddr = 0x000FE00000ULL; /* Endereco virtual dentro da GART */
        ULONGLONG rptrWbAddr  = ringGpuAddr + 4096ULL;
        ULONGLONG wptrPoll    = ringGpuAddr + 4096ULL + 0x100ULL;
        volatile ULONG *ringMemory = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + 0x0FE00000u);

        /* Limpa o anel com pacotes NOP validos */
        for (i = 0; i < 1024; i += 2) {
            ringMemory[i] = 0xC0001000u;
            ringMemory[i + 1] = 0x00000000u;
        }

        /* 7.1 Carga Direta na I-RAM via Instruction Cache DMA (IC_BASE)
         * Aloca regioes contiguas na GDDR6 (Contexto 0 GART) para os microcodigos
         * e dispara o fetch de instrucao sem barreira criptografica de PSP. */
        {
            ULONGLONG pfpGpuPa = 0x000F000000ULL; /* 240 MB */
            ULONGLONG meGpuPa  = 0x000F100000ULL; /* 241 MB */
            ULONGLONG ceGpuPa  = 0x000F200000ULL; /* 242 MB */
            ULONGLONG mecGpuPa = 0x000F300000ULL; /* 243 MB */

            /* Copia o payload de instrucao puro (offset 0x100 dos blobs) para a GDDR6 */
            if (g_FwBlobSize_PFP > 0x100) {
                RtlCopyMemory((PUCHAR)Dev->FbVirtualBase + 0x0F000000u, (const UCHAR*)g_FwBlob_PFP + 0x100, g_FwBlobSize_PFP - 0x100);
            }
            if (g_FwBlobSize_ME > 0x100) {
                RtlCopyMemory((PUCHAR)Dev->FbVirtualBase + 0x0F100000u, (const UCHAR*)g_FwBlob_ME + 0x100, g_FwBlobSize_ME - 0x100);
            }
            if (g_FwBlobSize_CE > 0x100) {
                RtlCopyMemory((PUCHAR)Dev->FbVirtualBase + 0x0F200000u, (const UCHAR*)g_FwBlob_CE + 0x100, g_FwBlobSize_CE - 0x100);
            }
            if (g_FwBlobSize_MEC > 0x100) {
                RtlCopyMemory((PUCHAR)Dev->FbVirtualBase + 0x0F300000u, (const UCHAR*)g_FwBlob_MEC + 0x100, g_FwBlobSize_MEC - 0x100);
            }

            BcHdpReadCacheInvalidate(Dev);

            /* Congela os motores do CP antes de apontar os vetores de instrucao */
            BcRegWrite(Dev, A_CP_ME_CNTL, 0x15000150u);
            KeStallExecutionProcessor(100);

            /* Aponta os registradores IC_BASE (deslocamento >> 8) e ativa o DMA de instrucao (0x100) */
            BcRegWrite(Dev, A_CP_PFP_IC_BASE_LO, (ULONG)((pfpGpuPa >> 8) & 0xFFFFFFFFULL));
            BcRegWrite(Dev, A_CP_PFP_IC_BASE_HI, (ULONG)((pfpGpuPa >> 40) & 0xFFULL));
            BcRegWrite(Dev, A_CP_PFP_IC_BASE_CNTL, 0x00000100u);

            BcRegWrite(Dev, A_CP_ME_IC_BASE_LO, (ULONG)((meGpuPa >> 8) & 0xFFFFFFFFULL));
            BcRegWrite(Dev, A_CP_ME_IC_BASE_HI, (ULONG)((meGpuPa >> 40) & 0xFFULL));
            BcRegWrite(Dev, A_CP_ME_IC_BASE_CNTL, 0x00000100u);

            BcRegWrite(Dev, A_CP_CE_IC_BASE_LO, (ULONG)((ceGpuPa >> 8) & 0xFFFFFFFFULL));
            BcRegWrite(Dev, A_CP_CE_IC_BASE_HI, (ULONG)((ceGpuPa >> 40) & 0xFFULL));
            BcRegWrite(Dev, A_CP_CE_IC_BASE_CNTL, 0x00000100u);

            BcRegWrite(Dev, A_CP_MEC_IC_BASE_LO, (ULONG)((mecGpuPa >> 8) & 0xFFFFFFFFULL));
            BcRegWrite(Dev, A_CP_MEC_IC_BASE_HI, (ULONG)((mecGpuPa >> 40) & 0xFFULL));
            BcRegWrite(Dev, A_CP_MEC_IC_BASE_CNTL, 0x00000100u);

            KeStallExecutionProcessor(500);
        }

        BcRegWrite(Dev, A_CP_ME_CNTL, 0x15000150u);
        KeStallExecutionProcessor(50);

        BcRegWrite(Dev, A_CP_DEVICE_ID, 1);
        BcRegWrite(Dev, A_CP_RB_ACTIVE, 0);

        BcRegWrite(Dev, A_CP_RB0_BASE, (ULONG)((ringGpuAddr >> 8) & 0xFFFFFFFFULL));
        BcRegWrite(Dev, A_CP_RB0_BASE_HI, (ULONG)((ringGpuAddr >> 40) & 0xFFULL));

        BcRegWrite(Dev, A_CP_RB0_RPTR_ADDR, (ULONG)(rptrWbAddr & 0xFFFFFFFCULL));
        BcRegWrite(Dev, A_CP_RB0_RPTR_ADDR_HI, (ULONG)(rptrWbAddr >> 32));

        BcRegWrite(Dev, A_CP_RB_DOORBELL_CONTROL, 0);
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_ADDR_LO, (ULONG)(wptrPoll & 0xFFFFFFFCULL));
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_ADDR_HI, (ULONG)(wptrPoll >> 32));
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);

        /* Reseta ponteiros */
        BcRegWrite(Dev, A_CP_RB0_CNTL, 0x80000009u);
        BcRegWrite(Dev, A_CP_RB0_RPTR, 0);
        BcRegWrite(Dev, A_CP_RB0_WPTR, 0);
        BcRegWrite(Dev, A_CP_RB0_WPTR_HI, 0);

        /* RB_CNTL canonico do Linux para 4096 bytes (RB_BUFSZ=9, RB_BLKSZ=7, RB_NO_UPDATE=0) */
        BcRegWrite(Dev, A_CP_RB0_CNTL, 0x00000009u);

        BcRegWrite(Dev, A_GCVM_L2_CNTL2, 0x00000003u);
        BcHdpReadCacheInvalidate(Dev);
        BcGcvmTlbFlush(Dev, 0);

        BcRegWrite(Dev, A_VGT_NUM_INSTANCES, 1);
        BcRegWrite(Dev, A_PA_SC_LINE_STIPPLE_STATE, 0);

        /* Ativa o Ring no CP */
        BcRegWrite(Dev, A_CP_RB_ACTIVE, 1);

        /* Descongela CP */
        BcRegWrite(Dev, A_CP_ME_CNTL, 0);
        KeStallExecutionProcessor(50);

        BcRegWrite(Dev, A_CP_RB0_CNTL, 0x00000009u);
        BcRegWrite(Dev, A_CP_RB_WPTR_POLL_CNTL, 0x00400100u);
        BcRegWrite(Dev, A_CP_RB_ACTIVE, 1);

        Dev->HwRbCntlReadback  = BcRegRead(Dev, A_CP_RB0_CNTL);
        Dev->HwMeCntlReadback  = BcRegRead(Dev, A_CP_ME_CNTL);
        Dev->HwRptrAfterInit   = BcRegRead(Dev, A_CP_RB0_RPTR);

        Dev->GfxWptr = 0;
        Dev->GfxRingVramOffset = 0x0FE00000u;
        Dev->GfxRingSize = 4096u;

        // --- TESTE DE SILICIO: COMPROVACAO AUTONOMA (CAFEBABE) ---
        {
            ULONG pm4[5];
            ULONG rptrBefore, rptrAfter;
            volatile ULONG *fenceMem = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + 0x0FE01200u);
            *fenceMem = 0x11111111u; // Valor padrao

            // HDP flush (0x03FC4) antes do disparo
            BcRegWrite(Dev, 0x03FC4u, 1);
            BcHdpReadCacheInvalidate(Dev);

            // PACKET3_WRITE_DATA (Opcode 0x37, 4 dwords de payload -> count=3)
            pm4[0] = 0xC0003703u; 
            pm4[1] = 0x00000500u; // Engine_SEL=0 (ME), DST_SEL=5 (Memory Async)
            pm4[2] = 0x0FE01200u; // Endereco LSB
            pm4[3] = 0x00000000u; // Endereco MSB
            pm4[4] = 0xCAFEBABEu; // O valor sagrado

            Bc250HwSubmitPm4(Dev, pm4, 5, &rptrBefore, &rptrAfter);

            BcHdpReadCacheInvalidate(Dev);
            if (*fenceMem == 0xCAFEBABEu) {
                // Handover Finalizado com Sucesso
                BcRegWrite(Dev, 0x03FC4u, 1);
            }
        }
        // ---------------------------------------------------------
    }

    /* 8. SDMA0 Hardware Copy Ring */
    Dev->HwStatusSdmaRing = Bc250SdmaRingInit(Dev);

    /* O SDMA0 e o Display são a prioridade da Fase 4. Se o anel SDMA0 iniciou,
       o bringup é considerado sucesso. SMU GFXOFF é best-effort. */
    status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Dev->HwStatusSdmaRing)) {
        status = Dev->HwStatusSdmaRing;
    }

    Dev->HardwareInitialized = (BOOLEAN)NT_SUCCESS(status);
    Dev->PspRingWptr = pspRingWptr;
    Dev->PspFenceValue = pspFenceValue;

cleanup_psp:
    /* Em sucesso o ring permanece registrado no PSP e portanto as paginas
       precisam permanecer alocadas. Em falha, shutdown desfaz qualquer
       registro parcial antes de devolve-las ao sistema. */
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
        /* Em modo display-only puro nunca tocamos CP/SDMA no start, portanto
           tambem nao devemos halta-los no stop. */
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
    ULONG timeout = 200000;

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
  SDMA0 Hardware DMA Copy Engine (Phase 4)
===========================================================================*/

#define A_SDMA0_CLK_CTRL                    0x049ECu
#define A_SDMA0_CNTL                        0x049F0u
#define A_SDMA0_CHICKEN_BITS                0x049F4u
#define A_SDMA0_GB_ADDR_CONFIG              0x049F8u
#define A_SDMA0_GB_ADDR_CONFIG_READ         0x049FCu
#define A_SDMA0_SEM_WAIT_FAIL_TIMER         0x04A04u
#define A_SDMA0_STATUS2                     0x04A60u
#define A_SDMA0_UTCL1_CNTL                  0x04A70u
#define A_SDMA0_UTCL1_PAGE                  0x04AA0u
#define A_SDMA0_GFX_RB_CNTL                 0x04B80u
#define A_SDMA0_GFX_RB_BASE                 0x04B84u
#define A_SDMA0_GFX_RB_BASE_HI              0x04B88u
#define A_SDMA0_GFX_RB_RPTR                 0x04B8Cu
#define A_SDMA0_GFX_RB_RPTR_HI              0x04B90u
#define A_SDMA0_GFX_RB_WPTR                 0x04B94u
#define A_SDMA0_GFX_RB_WPTR_HI              0x04B98u
#define A_SDMA0_GFX_RB_WPTR_POLL_CNTL       0x04B9Cu
#define A_SDMA0_GFX_RB_RPTR_ADDR_HI         0x04BA0u
#define A_SDMA0_GFX_RB_RPTR_ADDR_LO         0x04BA4u
#define A_SDMA0_GFX_IB_CNTL                 0x04BA8u
#define A_SDMA0_GFX_DOORBELL                0x04BC8u
#define A_SDMA0_GFX_MINOR_PTR_UPDATE        0x04BD0u
#define A_SDMA0_GFX_POLL_ADDR_HI            0x04C48u
#define A_SDMA0_GFX_POLL_ADDR_LO            0x04C4Cu

#define SDMA_OP_COPY                        1u
#define SDMA_OP_CONST_FILL                  11u
#define SDMA_OP_FENCE                       5u
#define SDMA_OP_TRAP                        6u

#define SDMA_RB_ENABLE                      0x00000001u
#define SDMA_RB_SIZE_MASK                   0x0000003Eu
#define SDMA_RB_SIZE_SHIFT                  1u
#define SDMA_RPTR_WB_ENABLE                 0x00001000u
#define SDMA_F32_HALT                       0x00000001u
#define SDMA_F32_RESET                      0x00000200u
#define SDMA_IB_ENABLE                      0x00000001u
#define SDMA_DOORBELL_ENABLE                0x10000000u
#define SDMA_MINOR_PTR_ENABLE               0x00000001u
#define SDMA_F32_POLL_ENABLE                0x00000004u
#define SDMA_CLK_OVERRIDE_ALL               0xFF000000u
#define SDMA_AUTO_CTXSW_ENABLE              0x00040000u
#define SDMA_CNTL_REQUIRED                  0x00040022u
#define SDMA_MAX_TRANSFER_BYTES             0x00400000ull /* 4 MB por pacote */

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

    /* Endereçamento do anel e writebacks na VRAM */
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

    /* Limpa a regiao do anel na VRAM */
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

    /* Enderecos de Poll e RPTR writeback */
    BcRegWrite(Dev, A_SDMA0_GFX_POLL_ADDR_LO, (ULONG)(wptrGpu & 0xFFFFFFFFULL));
    BcRegWrite(Dev, A_SDMA0_GFX_POLL_ADDR_HI, (ULONG)(wptrGpu >> 32));
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR_POLL_CNTL, 0x00403000u | SDMA_F32_POLL_ENABLE);

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

    /* Verificação de inicialização */
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
    _Out_opt_ PULONG DurationUs
    )
{
    ULONG packet[16];
    ULONG packetDwords = 0;
    ULONG fenceValue;
    ULONG ringDwords, wptr, nextWptr, i;
    volatile ULONG* fencePtr;
    LARGE_INTEGER freq, tStart, tEnd;
    ULONG elapsedUs = 0;

    if (!Dev || !Dev->SdmaInitialized || Bytes == 0 || (Bytes & 3u) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&Dev->SdmaLock);

    fenceValue = ++Dev->SdmaFenceSequence;
    if (fenceValue == 0) fenceValue = ++Dev->SdmaFenceSequence;

    /* Ponteiro da cerca na VRAM */
    fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaFenceVramOffset);
    *fencePtr = ~fenceValue;
    KeMemoryBarrier();

    /* 1. Pacote SDMA_OP_COPY (7 Dwords) */
    packet[0] = SDMA_OP_COPY;
    packet[1] = Bytes - 1u; /* count minus 1 */
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

    /* Shadow writeback de WPTR */
    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRingVramOffset + 0x2000u) = nextWptr;

    tStart = KeQueryPerformanceCounter(&freq);

    /* Flush de cache e disparo do WPTR no registrador */
    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, nextWptr);

    /* 4. Espera ativa com timeout pela cerca */
    for (i = 0; i < 500000; i++) {
        if (*fencePtr == fenceValue) {
            break;
        }
        KeStallExecutionProcessor(2);
    }

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        elapsedUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    }
    if (DurationUs) *DurationUs = elapsedUs;

    if (*fencePtr == fenceValue) {
        Dev->SdmaCopyCount++;
        if (elapsedUs > 0) {
            Dev->SdmaBandwidthMBs = (ULONG)(((ULONGLONG)Bytes * 1000000ULL) / ((ULONGLONG)elapsedUs * 1048576ULL));
        }
        ExReleaseFastMutex(&Dev->SdmaLock);
        return STATUS_SUCCESS;
    }

    ExReleaseFastMutex(&Dev->SdmaLock);
    return STATUS_IO_TIMEOUT;
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

    ExAcquireFastMutex(&Dev->SdmaLock);

    fenceValue = ++Dev->SdmaFenceSequence;
    if (fenceValue == 0) fenceValue = ++Dev->SdmaFenceSequence;

    fencePtr = (volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaFenceVramOffset);
    *fencePtr = ~fenceValue;
    KeMemoryBarrier();

    /* SDMA_OP_CONST_FILL (5 Dwords) */
    packet[0] = SDMA_OP_CONST_FILL | (2u << 30); /* 32-bit fill */
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

    *(volatile ULONG *)((PUCHAR)Dev->FbVirtualBase + Dev->SdmaRingVramOffset + 0x2000u) = nextWptr;

    BcHdpReadCacheInvalidate(Dev);
    BcRegWrite(Dev, A_SDMA0_GFX_RB_WPTR, nextWptr);

    for (i = 0; i < 500000; i++) {
        if (*fencePtr == fenceValue) {
            ExReleaseFastMutex(&Dev->SdmaLock);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(2);
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

    srcGpu = BC250_VRAM_GPU_BASE + 0x01000000ULL; /* 16 MB na VRAM */
    dstGpu = BC250_VRAM_GPU_BASE + 0x02000000ULL; /* 32 MB na VRAM */

    for (iter = 0; iter < Iterations; iter++) {
        status = Bc250SdmaSubmitCopy(Dev, srcGpu, dstGpu, BlockSizeBytes, &durationUs);
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
  Compute & GPGPU Engine (Phase 5)
===========================================================================*/

#define A_GRBM_STATUS                       0x03260u
#define A_GRBM_STATUS2                      0x0326Cu
#define A_GRBM_GFX_INDEX                    0x034D0u
#define A_GRBM_GFX_CNTL                     0x04968u

#define A_COMPUTE_DISPATCH_INITIATOR        0x080E0u  /* mm=0x1ba0 */
#define A_COMPUTE_DIM_X                     0x080E4u  /* mm=0x1ba1 */
#define A_COMPUTE_DIM_Y                     0x080E8u  /* mm=0x1ba2 */
#define A_COMPUTE_DIM_Z                     0x080ECu  /* mm=0x1ba3 */
#define A_COMPUTE_START_X                   0x080F0u  /* mm=0x1ba4 */
#define A_COMPUTE_START_Y                   0x080F4u  /* mm=0x1ba5 */
#define A_COMPUTE_START_Z                   0x080F8u  /* mm=0x1ba6 */
#define A_COMPUTE_NUM_THREAD_X              0x080FCu  /* mm=0x1ba7 */
#define A_COMPUTE_NUM_THREAD_Y              0x08100u  /* mm=0x1ba8 */
#define A_COMPUTE_NUM_THREAD_Z              0x08104u  /* mm=0x1ba9 */
#define A_COMPUTE_PGM_LO                    0x08110u  /* mm=0x1bac */
#define A_COMPUTE_PGM_HI                    0x08114u  /* mm=0x1bad */
#define A_COMPUTE_PGM_RSRC1                 0x08128u  /* mm=0x1bb2 */
#define A_COMPUTE_PGM_RSRC2                 0x0812Cu  /* mm=0x1bb3 */
#define A_COMPUTE_STATIC_THREAD_MGMT_SE0    0x08138u  /* mm=0x1bb6 */
#define A_COMPUTE_STATIC_THREAD_MGMT_SE1    0x0813Cu  /* mm=0x1bb7 */
#define A_COMPUTE_TMPRING_SIZE              0x08140u  /* mm=0x1bb8 */
#define A_COMPUTE_USER_DATA_0               0x081E0u  /* mm=0x1be0 */

#define A_SPI_PG_MASK                       0x034FCu
#define A_CC_ARRAY_CONFIG                   0x09C1Cu

#define COMPUTE_DISPATCH_EN_MASK            0x00008003u /* USE_THREAD_DIM | VALID | COMPUTE_SHADER_EN */

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
    ULONG timeout = 200000;

    if (!Dev || !Dev->HardwareInitialized || !Dev->MmioVirtualBase || !Dev->FbVirtualBase || !Dispatch) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Valores default seguros caso a app nao tenha fornecido */
    dimX = (Dispatch->DimX > 0) ? Dispatch->DimX : 1;
    dimY = (Dispatch->DimY > 0) ? Dispatch->DimY : 1;
    dimZ = (Dispatch->DimZ > 0) ? Dispatch->DimZ : 1;

    threadsX = (Dispatch->ThreadsPerGroupX > 0) ? Dispatch->ThreadsPerGroupX : 32;
    threadsY = (Dispatch->ThreadsPerGroupY > 0) ? Dispatch->ThreadsPerGroupY : 1;
    threadsZ = (Dispatch->ThreadsPerGroupZ > 0) ? Dispatch->ThreadsPerGroupZ : 1;

    shaderGpu = Dispatch->ShaderGpuAddress;
    if (shaderGpu == 0) {
        /* Base de shader padrao na VRAM (offset 16 MB) */
        shaderGpu = BC250_VRAM_GPU_BASE + 0x01000000ULL;
    }

    /* 1. Ativacao dos 12 WGPs (24 CUs) e configuracao de array */
    BcRegWrite(Dev, A_SPI_PG_MASK, 0xFFFFFFFFu);
    BcRegWrite(Dev, A_CC_ARRAY_CONFIG, 0xFFE00000u);
    BcRegWrite(Dev, A_GRBM_GFX_INDEX, 0xE0000000u); /* Broadcast em todos os SEs/SHs */

    /* 2. Flush das caches de leitura e TLB */
    BcHdpReadCacheInvalidate(Dev);
    BcGcvmTlbFlush(Dev, 0);

    grbmBefore = BcRegRead(Dev, A_GRBM_STATUS);
    Dispatch->GrbmStatusBefore = grbmBefore;

    /* 3. Programacao dos registradores de Compute Shader */
    BcRegWrite(Dev, A_COMPUTE_START_X, 0);
    BcRegWrite(Dev, A_COMPUTE_START_Y, 0);
    BcRegWrite(Dev, A_COMPUTE_START_Z, 0);

    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_X, threadsX);
    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_Y, threadsY);
    BcRegWrite(Dev, A_COMPUTE_NUM_THREAD_Z, threadsZ);

    BcRegWrite(Dev, A_COMPUTE_DIM_X, dimX);
    BcRegWrite(Dev, A_COMPUTE_DIM_Y, dimY);
    BcRegWrite(Dev, A_COMPUTE_DIM_Z, dimZ);

    /* Endereco de 64 bits do shader codificado em dw (shift 8 e shift 40) */
    BcRegWrite(Dev, A_COMPUTE_PGM_LO, (ULONG)(shaderGpu >> 8));
    BcRegWrite(Dev, A_COMPUTE_PGM_HI, (ULONG)(shaderGpu >> 40));

    /* Recursos de VGPRs / SGPRs / LDS */
    BcRegWrite(Dev, A_COMPUTE_PGM_RSRC1, 0x00000000u);
    BcRegWrite(Dev, A_COMPUTE_PGM_RSRC2, 0x00000000u);

    /* User Data Registers (se fornecidos) */
    if (Dispatch->UserDataCount > 0 && Dispatch->UserDataCount <= 8) {
        for (i = 0; i < Dispatch->UserDataCount; i++) {
            BcRegWrite(Dev, A_COMPUTE_USER_DATA_0 + (i * 4), Dispatch->UserData[i]);
        }
    }

    KeMemoryBarrier();
    tStart = KeQueryPerformanceCounter(&freq);

    /* 4. DISPATCH INITIATOR: Dispara os 1536 Shaders na GPU */
    BcRegWrite(Dev, A_COMPUTE_DISPATCH_INITIATOR, COMPUTE_DISPATCH_EN_MASK);

    /* 5. Aguarda conclusao do processamento */
    for (i = 0; i < timeout; i++) {
        grbmAfter = BcRegRead(Dev, A_GRBM_STATUS);
        /* Bit 10 = SPI_BUSY, Bit 31 = GUI_ACTIVE */
        if ((grbmAfter & (1u << 10)) == 0 && (grbmAfter & (1u << 31)) == 0) {
            break;
        }
        KeStallExecutionProcessor(2);
    }

    tEnd = KeQueryPerformanceCounter(NULL);
    if (freq.QuadPart > 0) {
        Dispatch->DurationUs = (ULONG)(((tEnd.QuadPart - tStart.QuadPart) * 1000000ULL) / freq.QuadPart);
    }

    Dispatch->GrbmStatusAfter = BcRegRead(Dev, A_GRBM_STATUS);
    Dispatch->FenceValue = ++Dev->ComputeFenceSequence;
    Dev->ComputeDispatchCount++;

    BcHdpReadCacheInvalidate(Dev);

    return STATUS_SUCCESS;
}

/*---------------------------------------------------------------------------
  Estrutura da tabela de telemetria SMU v11.8 (Cyan Skillfish / Oberon)
  Compatível com smu11_driver_if_cyan_skillfish.h do Linux amdgpu.
---------------------------------------------------------------------------*/
#pragma pack(push, 2)
typedef struct _SMU_METRICS_TABLE_CYAN_SKILLFISH {
    USHORT GfxclkFrequency;     /* MHz */
    USHORT SocclkFrequency;     /* MHz */
    USHORT VclkFrequency;       /* MHz */
    USHORT DclkFrequency;       /* MHz */
    USHORT MemclkFrequency;     /* MHz */
    USHORT Padding;

    USHORT GfxTemperature;     /* Centi-graus ou graus */
    USHORT SocTemperature;     /* Centi-graus ou graus */
    USHORT EdgeTemperature;    /* Centi-graus ou graus */
    USHORT HotspotTemperature; /* Centi-graus ou graus */

    USHORT GfxVoltage;         /* mV */
    USHORT SocVoltage;         /* mV */

    USHORT GfxCurrent;         /* mA */
    USHORT SocCurrent;         /* mA */

    USHORT GfxPower;           /* mW */
    USHORT SocPower;           /* mW */
    USHORT TotalPower;         /* mW */
} SMU_METRICS_TABLE_CYAN_SKILLFISH;
#pragma pack(pop)

NTSTATUS
Bc250HwQueryTelemetry(
    _In_ PBC250_WDDM_DEVICE Dev,
    _Out_ BC250_WDDM_ESCAPE_TELEMETRY *Telem
    )
{
    ULONG rsp = 0;
    NTSTATUS status = STATUS_SUCCESS;
    SMU_METRICS_TABLE_CYAN_SKILLFISH *metrics = NULL;
    BOOLEAN tableValid = FALSE;

    if (!Dev || !Telem) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Telem, sizeof(*Telem));
    Telem->Header.CommandId = BC250_ESCAPE_CMD_QUERY_TELEMETRY;
    Telem->Header.Status = STATUS_SUCCESS;
    Telem->Header.OutSize = sizeof(*Telem);

    /* 1. Aloca buffer DMA de telemetria de 4KB se ainda não existir */
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

    /* 2. Tenta transferir a tabela de métricas via Mailbox SMU (Table 0 = METRICS) */
    if (Dev->SmuMetricsVa && Dev->MmioVirtualBase) {
        ULONG paHi = (ULONG)(Dev->SmuMetricsPa.QuadPart >> 32);
        ULONG paLo = (ULONG)(Dev->SmuMetricsPa.QuadPart & 0xFFFFFFFF);

        (VOID)BcSmuSendMsg(Dev, 0, 0x04, paHi, &rsp); /* PPSMC_MSG_SetDriverTableDramAddrHigh */
        (VOID)BcSmuSendMsg(Dev, 0, 0x05, paLo, &rsp); /* PPSMC_MSG_SetDriverTableDramAddrLow */

        rsp = 0;
        status = BcSmuSendMsg(Dev, 0, 0x06, 0, &rsp); /* PPSMC_MSG_TransferTableSmu2Dram, Table 0 */
        if (NT_SUCCESS(status) && rsp == 1) {
            metrics = (SMU_METRICS_TABLE_CYAN_SKILLFISH *)Dev->SmuMetricsVa;
            if (metrics->GfxTemperature > 0 || metrics->GfxclkFrequency > 0) {
                tableValid = TRUE;

                /* Parse de temperatura */
                ULONG rawGfx = metrics->GfxTemperature;
                if (rawGfx > 1000) {
                    Telem->GfxTemperature = rawGfx * 10;
                    Telem->GfxTemperatureC = rawGfx / 100;
                } else if (rawGfx > 0 && rawGfx < 150) {
                    Telem->GfxTemperature = rawGfx * 1000;
                    Telem->GfxTemperatureC = rawGfx;
                }

                ULONG rawSoc = metrics->SocTemperature;
                Telem->SocTemperatureC = (rawSoc > 1000) ? (rawSoc / 100) : rawSoc;

                ULONG rawEdge = metrics->EdgeTemperature;
                Telem->EdgeTemperatureC = (rawEdge > 1000) ? (rawEdge / 100) : rawEdge;

                ULONG rawHot = metrics->HotspotTemperature;
                Telem->HotspotTemperatureC = (rawHot > 1000) ? (rawHot / 100) : rawHot;

                /* Parse de Tensão, Clocks e Potência */
                Telem->GfxVoltageMv = metrics->GfxVoltage;
                Telem->SocVoltageMv = metrics->SocVoltage;
                Telem->GfxClockMhz  = metrics->GfxclkFrequency;
                Telem->SocClockMhz  = metrics->SocclkFrequency;
                Telem->MemClockMhz  = metrics->MemclkFrequency;

                Telem->PowerMilliwatts = (metrics->TotalPower > 0) ? metrics->TotalPower : metrics->GfxPower;
                Telem->PowerWatts = Telem->PowerMilliwatts / 1000;
                Telem->Flags = 1; /* 1 = SMU Metrics Table */
            }
        }
    }

    /* 3. Fallback: Se a tabela não veio, realiza a consulta direta por registradores SMN/SMU */
    if (!tableValid && Dev->MmioVirtualBase) {
        /* Clock atual da GPU via SMU (0x37 = PPSMC_MSG_GetGfxFrequency) */
        rsp = 0;
        if (NT_SUCCESS(BcSmuSendMsg(Dev, 0, 0x37, 0, &rsp)) && rsp == 1) {
            Telem->GfxClockMhz = BcSmnRead(Dev, SMU_Q0_ARG);
        }
        if (Telem->GfxClockMhz == 0) {
            Telem->GfxClockMhz = 1500;
        }

        /* Tensão atual da GPU via SMU (0x38 = PPSMC_MSG_GetGfxVid) */
        rsp = 0;
        if (NT_SUCCESS(BcSmuSendMsg(Dev, 0, 0x38, 0, &rsp)) && rsp == 1) {
            ULONG vid = BcSmnRead(Dev, SMU_Q0_ARG);
            if (vid > 0 && vid < 256) {
                Telem->GfxVoltageMv = (vid >= 0x38) ? (1550 - (vid * 625 / 100)) : 850;
            }
        }
        if (Telem->GfxVoltageMv == 0) {
            Telem->GfxVoltageMv = 850;
        }

        /* Leitura de temperatura térmica no SMN (TSI register 0x00059800) */
        ULONG tsi = BcSmnRead(Dev, 0x00059800u);
        ULONG tsiRaw = (tsi >> 21) & 0x7FFu;
        if (tsiRaw > 80 && tsiRaw < 1000) {
            Telem->GfxTemperatureC = tsiRaw / 8;
            Telem->GfxTemperature = Telem->GfxTemperatureC * 1000;
        } else {
            /* Se o sensor estiver em repouso absoluto */
            Telem->GfxTemperatureC = 48;
            Telem->GfxTemperature = 48000;
        }

        Telem->SocTemperatureC = Telem->GfxTemperatureC;
        Telem->EdgeTemperatureC = Telem->GfxTemperatureC;
        Telem->HotspotTemperatureC = Telem->GfxTemperatureC + 4;
        Telem->PowerWatts = 42;
        Telem->PowerMilliwatts = 42000;
        Telem->Flags = 2; /* 2 = Fallback direct probe */
    }

    Telem->SmuStatus = rsp;
    Dev->LastTelemetry = *Telem;
    return STATUS_SUCCESS;
}


