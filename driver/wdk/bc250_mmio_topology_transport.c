#include "bc250_mmio_topology_transport.h"
#include "bc250_mmio_breadcrumb.h"
#include "../core/bc250_fault_latch_contract.h"
#include "../core/bc250_golden_settings_contract.h"
#include "../core/bc250_hdp_contract.h"
#include "../core/bc250_mmhub_gart.h"
#include "../core/bc250_sdma1_bringup.h"

static void Bc250MmioTopologyClearMapping(
    BC250_MMIO_TOPOLOGY_CONTEXT *Context)
{
    Context->State = BC250_MMIO_TOPOLOGY_EMPTY;
    Context->DeviceHandle = NULL;
    Context->UnmapMemory = NULL;
    Context->VirtualBase = NULL;
    Context->PhysicalStart = 0;
    Context->Length = 0;
    Context->BarIndex = 0;
    Context->ReadCount = 0;
    Context->WriteCount = 0;
}

/*
 * Allowlist fechada dos 26 registradores transacionais da MMU global.
 * Nao aceitar intervalos: um vizinho nao catalogado continua recusado.
 */
static uint32_t Bc250MmioTopologyGlobalMmuOffsetAllowed(uint32_t ByteOffset)
{
    switch (ByteOffset) {
    case BC250_REG_GCMC_VM_AGP_BASE:
    case BC250_REG_GCMC_VM_AGP_BOT:
    case BC250_REG_GCMC_VM_AGP_TOP:
    case BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR:
    case BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR:
    case BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB:
    case BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB:
    case BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_LO:
    case BC250_REG_GCVM_L2_PROT_FAULT_DEFAULT_ADDR_HI:
    case BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2:
    case BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO:
    case BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI:
    case BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO:
    case BC250_REG_GCVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI:
    case BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO:
    case BC250_REG_GCVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI:
    case BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_LO:
    case BC250_REG_GCVM_INVALIDATE_ENG0_ADDR_RANGE_HI:
    case BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO:
    case BC250_REG_GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI:
    case BC250_REG_GCMC_VM_MX_L1_TLB_CNTL:
    case BC250_REG_GCVM_L2_CNTL:
    case BC250_REG_GCVM_L2_CNTL2:
    case BC250_REG_GCVM_L2_CNTL3:
    case BC250_REG_GCVM_L2_CNTL4:
    case BC250_REG_GCVM_L2_CNTL5:
        return 1u;
    default:
        return 0u;
    }
}

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyInitialize(
    BC250_MMIO_TOPOLOGY_CONTEXT *Context)
{
    if (Context == NULL) return BC250_MMIO_TOPOLOGY_BAD_ARGUMENT;
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = BC250_MMIO_TOPOLOGY_SIGNATURE;
    Context->LastCallbackStatus = STATUS_NOT_SUPPORTED;
    return BC250_MMIO_TOPOLOGY_OK;
}

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyMapReadonly(
    BC250_MMIO_TOPOLOGY_CONTEXT *Context,
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t MmioBarIndex)
{
    const SIZE_T minimumInterfaceBytes =
        FIELD_OFFSET(DXGKRNL_INTERFACE, DxgkCbUnmapMemory) +
        sizeof(DxgkInterface->DxgkCbUnmapMemory);
    const uint64_t minimumLength =
        (uint64_t)BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET +
        sizeof(uint32_t);
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS translatedAddress;
    PVOID virtualAddress = NULL;
    NTSTATUS status;
    size_t matchCount = 0;
    size_t index;

    if (Context == NULL || DxgkInterface == NULL || ResolvedBars == NULL ||
        ResolvedBarCount == 0 ||
        Context->Signature != BC250_MMIO_TOPOLOGY_SIGNATURE) {
        return BC250_MMIO_TOPOLOGY_BAD_ARGUMENT;
    }
    if (Context->State != BC250_MMIO_TOPOLOGY_EMPTY ||
        Context->VirtualBase != NULL) {
        return BC250_MMIO_TOPOLOGY_WRONG_STATE;
    }
    if (DxgkInterface->Size < minimumInterfaceBytes ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbMapMemory == NULL ||
        DxgkInterface->DxgkCbUnmapMemory == NULL) {
        return BC250_MMIO_TOPOLOGY_INTERFACE_TOO_SMALL;
    }
    for (index = 0; index < ResolvedBarCount; ++index) {
        if (ResolvedBars[index].Index == MmioBarIndex) {
            bar = &ResolvedBars[index];
            ++matchCount;
        }
    }
    if (matchCount != 1 || bar == NULL) {
        return BC250_MMIO_TOPOLOGY_BAR_NOT_UNIQUE;
    }
    if (bar->Length < minimumLength) {
        return BC250_MMIO_TOPOLOGY_BAR_TOO_SHORT;
    }
    if (bar->Length > MAXULONG) {
        return BC250_MMIO_TOPOLOGY_BAR_TOO_LARGE;
    }
    if (bar->Start > MAXLONGLONG ||
        bar->Start > MAXULONGLONG - (bar->Length - 1ull)) {
        return BC250_MMIO_TOPOLOGY_PHYSICAL_RANGE_INVALID;
    }

    translatedAddress.QuadPart = (LONGLONG)bar->Start;
    status = DxgkInterface->DxgkCbMapMemory(
        DxgkInterface->DeviceHandle, translatedAddress, (ULONG)bar->Length,
        FALSE, FALSE, MmNonCached, &virtualAddress);
    Context->LastCallbackStatus = status;
    if (!NT_SUCCESS(status)) return BC250_MMIO_TOPOLOGY_MAP_FAILED;
    if (virtualAddress == NULL) {
        return BC250_MMIO_TOPOLOGY_MAP_RETURNED_NULL;
    }

    Context->DeviceHandle = DxgkInterface->DeviceHandle;
    Context->UnmapMemory = DxgkInterface->DxgkCbUnmapMemory;
    Context->VirtualBase = virtualAddress;
    Context->PhysicalStart = bar->Start;
    Context->Length = bar->Length;
    Context->BarIndex = MmioBarIndex;
    Context->ReadCount = 0;
    Context->WriteCount = 0;
    Context->State = BC250_MMIO_TOPOLOGY_MAPPED;
    return BC250_MMIO_TOPOLOGY_OK;
}

/*
 * DX.26 -- LEITURA dos golden settings do GC e do Clear State Buffer.
 *
 * Fase A das auditorias externas (docs/338): o KMD nunca aplica nem LE os 34
 * golden settings do Cyan Skillfish, nem os RLC_CSIB_*. Esta allowlist e' so'
 * de LEITURA -- a de escrita nao ganha nada aqui. Offsets com procedencia:
 * a tabela de bc250_golden_settings_contract.c e inc/linux_ref/gc_10_1_0_offset.h
 * (mmRLC_CSIB_ADDR_LO/HI/LENGTH = 0x4CA2..0x4CA4, BASE_IDX 1 -> (0xA000+x)*4).
 */
#define BC250_REG_RLC_CSIB_ADDR_LO_BYTE 0x0003B288u
#define BC250_REG_RLC_CSIB_ADDR_HI_BYTE 0x0003B28Cu
#define BC250_REG_RLC_CSIB_LENGTH_BYTE  0x0003B290u

static uint32_t Bc250MmioTopologyGoldenReadAllowed(uint32_t ByteOffset)
{
    uint32_t count = 0u;
    const BC250_GOLDEN_ENTRY *table = Bc250GoldenSettingsGetTable(&count);
    if (ByteOffset == BC250_REG_RLC_CSIB_ADDR_LO_BYTE ||
        ByteOffset == BC250_REG_RLC_CSIB_ADDR_HI_BYTE ||
        ByteOffset == BC250_REG_RLC_CSIB_LENGTH_BYTE) {
        return 1u;
    }
    for (uint32_t i = 0; table != NULL && i < count; ++i) {
        if (table[i].BarOffset == ByteOffset) return 1u;
    }
    return 0u;
}

/*
 * G.1 (doc 645): ESCRITA dos 34 golden settings do Cyan Skillfish, so' os
 * offsets da tabela (bc250_golden_settings_contract.c, igual a
 * golden_settings_gc_10_0_cyan_skillfish). Unicos escritores:
 * Bc250ActiveG1ApplyGolden/RestoreGolden (snapshot, Lei 6, readback, restauracao).
 * O RLC_CSIB_* continua com a lista propria do DX.27.
 */
static uint32_t Bc250MmioTopologyG1GoldenWriteAllowed(uint32_t ByteOffset)
{
    uint32_t count = 0u;
    const BC250_GOLDEN_ENTRY *table = Bc250GoldenSettingsGetTable(&count);
    for (uint32_t i = 0; table != NULL && i < count; ++i) {
        if (table[i].BarOffset == ByteOffset) return 1u;
    }
    return 0u;
}

/* P3.0 (doc 609): foto do MMHUB, so' leitura. A escrita da P3.0b tem lista propria abaixo. */
static uint32_t Bc250MmioTopologyMmhubReadAllowed(uint32_t ByteOffset)
{
    static const uint32_t s_mmhub[BC250_MMHUB_P30_READ_COUNT] = {
        BC250_MMHUB_REG_VM_L2_CNTL, BC250_MMHUB_REG_VM_L2_PROT_FAULT_STATUS,
        BC250_MMHUB_REG_VM_L2_PROT_FAULT_ADDR_LO32, BC250_MMHUB_REG_VM_CONTEXT0_CNTL,
        BC250_MMHUB_REG_VM_CONTEXT0_PT_BASE_LO32, BC250_MMHUB_REG_VM_CONTEXT0_PT_BASE_HI32,
        BC250_MMHUB_REG_VM_CONTEXT0_PT_START_LO32, BC250_MMHUB_REG_VM_CONTEXT0_PT_END_LO32,
        BC250_MMHUB_REG_MC_VM_FB_OFFSET, BC250_MMHUB_REG_MC_VM_SYS_APERTURE_DEFAULT_LSB,
        BC250_MMHUB_REG_MC_VM_FB_LOCATION_BASE, BC250_MMHUB_REG_MC_VM_FB_LOCATION_TOP,
        BC250_MMHUB_REG_MC_VM_AGP_TOP, BC250_MMHUB_REG_MC_VM_AGP_BOT,
        BC250_MMHUB_REG_MC_VM_SYS_APERTURE_LOW, BC250_MMHUB_REG_MC_VM_SYS_APERTURE_HIGH,
        BC250_MMHUB_REG_MC_VM_MX_L1_TLB_CNTL
    };
    uint32_t i;
    for (i = 0u; i < BC250_MMHUB_P30_READ_COUNT; ++i) {
        if (s_mmhub[i] == ByteOffset) return 1u;
    }
    /*
     * P3.0b (doc 611): o conjunto que a GART do MMHUB escreve tambem e' lido
     * (snapshot e readback), mais o ACK do engine 17. O SEM do engine 17 entra
     * pela mesma regra: LER o SEM ADQUIRE o semaforo (gmc_v10_0_flush_vm_hub),
     * e o unico leitor e' o flush do MMHUB, que o libera escrevendo 0.
     */
    return Bc250MmhubGartIsWritable(ByteOffset) ||
        ByteOffset == BC250_MMHUB_VM_INV_ENG17_ACK;
}

/*
 * P3.0b (doc 611): ESCRITA no MMHUB so' no conjunto da GART da VMID 0
 * (Bc250MmhubGartIsWritable, fonte unica em ../core/bc250_mmhub_gart.c). O FB,
 * o FB_OFFSET e os contextos 1..15 ficam fora. Unicos escritores:
 * Bc250MmhubGartProgram/Restore e o flush do MMHUB no miniport.
 */
static uint32_t Bc250MmioTopologyMmhubP3bWriteAllowed(uint32_t ByteOffset)
{
    return Bc250MmhubGartIsWritable(ByteOffset);
}

static uint32_t Bc250MmioTopologyOffsetAllowed(uint32_t ByteOffset)
{
    return Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||
        Bc250MmioTopologyMmhubReadAllowed(ByteOffset) ||
        Bc250MmioTopologyGoldenReadAllowed(ByteOffset) ||
        /* 4c (doc 567): REQ/ACK do engine 1 so' em LEITURA -- quem escreve o
         * REQ e' o SDMA0 por SRBM_WRITE. Ler o SEM adquire o semaforo
         * (docs/03, AVISO) e por isso ele NAO entra aqui. */
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_REQ ||
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_ACK ||
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_LO ||
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_HI ||
        ByteOffset == BC250_RCC_CONFIG_MEMSIZE_BYTE_OFFSET ||
        ByteOffset == BC250_DRIVER_SCRATCH_0_BYTE_OFFSET ||
        ByteOffset == BC250_DRIVER_SCRATCH_1_BYTE_OFFSET ||
        ByteOffset == BC250_DRIVER_SCRATCH_2_BYTE_OFFSET ||
        ByteOffset == BC250_GCMC_VM_FB_OFFSET_BYTE_OFFSET ||
        ByteOffset == BC250_GCMC_VM_FB_LOCATION_BASE_BYTE_OFFSET ||
        ByteOffset == BC250_GCMC_VM_FB_LOCATION_TOP_BYTE_OFFSET ||
        ByteOffset == BC250_REG_GCVM_PT_BASE_LO ||
        ByteOffset == BC250_REG_GCVM_PT_BASE_HI ||
        ByteOffset == BC250_REG_GCVM_PT_START_LO ||
        ByteOffset == BC250_REG_GCVM_PT_START_HI ||
        ByteOffset == BC250_REG_GCVM_PT_END_LO ||
        ByteOffset == BC250_REG_GCVM_PT_END_HI ||
        ByteOffset == BC250_REG_CP_RB0_BASE ||
        ByteOffset == BC250_REG_CP_RB0_BASE_HI ||
        ByteOffset == BC250_REG_CP_RB0_CNTL ||
        ByteOffset == BC250_REG_SDMA0_RB_BASE ||
        ByteOffset == BC250_REG_SDMA0_RB_BASE_HI ||
        ByteOffset == BC250_REG_SDMA0_RB_CNTL ||
        ByteOffset == BC250_REG_CP_RB0_RPTR ||
        ByteOffset == BC250_REG_CP_RB0_RPTR_ADDR_LO ||
        ByteOffset == BC250_REG_CP_RB0_RPTR_ADDR_HI ||
        ByteOffset == BC250_REG_CP_RB0_WPTR ||
        ByteOffset == BC250_REG_CP_RB0_WPTR_HI ||
        /* EOP do CP (doc 618): snapshot e readback do CP_INT_CNTL_RING0. */
        ByteOffset == BC250_REG_CP_INT_CNTL_RING0 ||
        /* MEC.1 (doc 640): faixas de doorbell do CP, LEITURA (snapshot e registro). */
        ByteOffset == BC250_REG_CP_MEC_DOORBELL_RANGE_LOWER ||
        ByteOffset == BC250_REG_CP_MEC_DOORBELL_RANGE_UPPER ||
        ByteOffset == BC250_REG_CP_RB_DOORBELL_RANGE_LOWER ||
        ByteOffset == BC250_REG_CP_RB_DOORBELL_RANGE_UPPER ||
        ByteOffset == BC250_REG_CP_RB_DOORBELL_CONTROL ||
        /* P4.1 (doc 620): registradores do SDMA1 do contrato (fonte unica). */
        Bc250Sdma1IsRegister(ByteOffset) ||
        ByteOffset == BC250_REG_SDMA0_RB_RPTR ||
        ByteOffset == BC250_REG_SDMA0_RB_RPTR_HI ||
        ByteOffset == BC250_REG_SDMA0_RB_WPTR ||
        ByteOffset == BC250_REG_SDMA0_RB_WPTR_HI ||
        ByteOffset == BC250_REG_SDMA0_RB_RPTR_ADDR_LO ||
        ByteOffset == BC250_REG_SDMA0_RB_RPTR_ADDR_HI ||
        ByteOffset == BC250_REG_PSP_C2PMSG_35 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_36 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_37 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_81 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_64 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_67 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_69 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_70 ||
        ByteOffset == BC250_REG_PSP_C2PMSG_71 ||
        ByteOffset == 0xA2D0u ||
        ByteOffset == 0xA318u ||
        ByteOffset == 0xA3A4u ||
        ByteOffset == 0xA3A8u ||
        Bc250VmidPoolIsWritableGcvmRegister(ByteOffset) ||
        Bc250GcvmIsUserRegister(ByteOffset) ||
        Bc250GcvmIsComputeRegister(ByteOffset) ||
        ByteOffset == BC250_GCVM_VMID8_CNTL ||
        ByteOffset == BC250_GCVM_VMID8_PT_BASE_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_BASE_HI ||
        ByteOffset == BC250_GCVM_VMID8_PT_START_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_START_HI ||
        ByteOffset == BC250_GCVM_VMID8_PT_END_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_END_HI ||
        ByteOffset == BC250_PSP_RING_C2PMSG_64 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_67 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_69 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_70 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_71 ||
        ByteOffset == 0x00004A24u ||
        ByteOffset == BC250_SDMA0_CLK_CTRL ||
        ByteOffset == BC250_SDMA0_CNTL ||
        ByteOffset == BC250_SDMA0_F32_CNTL ||
        ByteOffset == BC250_SDMA0_STATUS2_REG ||
        ByteOffset == 0x00003FC4u || ByteOffset == 0x00003FCCu ||
        ByteOffset == 0x00004A04u || ByteOffset == 0x000049F4u ||
        ByteOffset == 0x000049F8u || ByteOffset == 0x000049FCu ||
        ByteOffset == 0x00004A14u || ByteOffset == 0x00004A18u ||
        ByteOffset == 0x00004A60u || ByteOffset == 0x00004A70u ||
        ByteOffset == 0x00004A78u || ByteOffset == 0x00004A7Cu ||
        ByteOffset == 0x00004A80u || ByteOffset == 0x00004AA0u ||
        ByteOffset == 0x00004A8Cu || ByteOffset == 0x00004A90u ||
        ByteOffset == 0x00004A94u || ByteOffset == 0x00004A98u ||
        ByteOffset == 0x00004A9Cu || ByteOffset == 0x00004AB0u ||
        ByteOffset == 0x00004AC0u ||
        ByteOffset == 0x00004B9Cu || ByteOffset == 0x00004BA0u ||
        ByteOffset == 0x00004BA4u || ByteOffset == 0x00004BA8u ||
        ByteOffset == 0x00004BC8u || ByteOffset == 0x00004C48u ||
        ByteOffset == 0x00004C4Cu || ByteOffset == 0x00004C54u ||
        ByteOffset == 0x0003B000u || ByteOffset == 0x0003B014u || ByteOffset == 0x0003B9B8u ||
        ByteOffset == 0x000086D8u || ByteOffset == 0x00008694u ||
        ByteOffset == 0x00008698u || ByteOffset == 0x0000869Cu ||
        /*
         * 0xA120 GCVM_L2_PROTECTION_FAULT_CNTL -- SOMENTE LEITURA.
         *
         * Nunca foi lido por este driver, e decide o significado de tudo que
         * lemos em 0xA130/0xA134/0xA138. O bit 1
         * (ALLOW_SUBSEQUENT_PROTECTION_FAULT_STATUS_ADDR_UPDATES, sh_mask
         * 12092) diz se o par STATUS/ADDR acompanha o fault mais RECENTE ou
         * fica preso no PRIMEIRO ate alguem escrever o bit 0
         * (CLEAR_PROTECTION_FAULT_STATUS_ADDR). Se for o segundo caso, toda
         * analise de fault desde a Corrida 45 leu um latch velho.
         *
         * A allowlist de ESCRITA nao ganha este offset: limpar o latch muda o
         * estado do silicio e e' decisao separada, com criterio proprio.
         */
        ByteOffset == 0x0000A120u ||
        ByteOffset == 0x0000A130u || ByteOffset == 0x0000A134u ||
        ByteOffset == 0x0000A138u ||
        ByteOffset == 0x0003E050u || ByteOffset == 0x0003E054u ||
        ByteOffset == 0x0003E058u || ByteOffset == 0x0003E05Cu ||
        ByteOffset == 0x0003E060u || ByteOffset == 0x0003E064u ||
        ByteOffset == 0x0003E100u || ByteOffset == 0x0003E104u ||
        ByteOffset == 0x0003E108u || ByteOffset == 0x0003E10Cu ||
        ByteOffset == 0x0003E110u || ByteOffset == 0x0003E114u ||
        ByteOffset == 0x0003E118u || ByteOffset == 0x0003E11Cu ||
        ByteOffset == 0x0003E120u || ByteOffset == 0x0003E124u ||
        ByteOffset == 0x0003E128u || ByteOffset == 0x0003E12Cu ||
        ByteOffset == 0x0000C2B8u || ByteOffset == 0x0000C12Cu ||
        ByteOffset == 0x0000C680u || ByteOffset == 0x0000C144u ||
        ByteOffset == 0x00008704u || ByteOffset == 0x00008708u ||
        ByteOffset == 0x0000C168u || ByteOffset == 0x0000C16Cu ||
        ByteOffset == 0x0000C170u || ByteOffset == 0x0000C174u ||
        ByteOffset == 0x0000C170u || ByteOffset == 0x0000C174u ||
        ByteOffset == 0x0000C2E0u || ByteOffset == 0x0000C20Cu || ByteOffset == 0x0000C210u ||
        /* 3.34/3.109: diagnostico de stall do CP/CPC/CPF, SOMENTE leitura. */
        ByteOffset == BC250_HQD_MMIO_CP_CPC_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CP_CPC_BUSY_STAT ||
        ByteOffset == BC250_HQD_MMIO_CP_CPC_STALLED_STAT1 ||
        ByteOffset == BC250_HQD_MMIO_CP_CPF_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CP_CPF_BUSY_STAT ||
        ByteOffset == BC250_HQD_MMIO_CP_CPF_STALLED_STAT1 ||
        ByteOffset == BC250_HQD_MMIO_CP_MEC_ME1_HEADER_DUMP ||
        ByteOffset == BC250_HQD_MMIO_CP_MEC_ME2_HEADER_DUMP ||
        ByteOffset == BC250_HQD_MMIO_CP_STALLED_STAT1 ||
        ByteOffset == BC250_HQD_MMIO_CP_STALLED_STAT2 ||
        ByteOffset == BC250_HQD_MMIO_CP_BUSY_STAT ||
        ByteOffset == BC250_HQD_MMIO_CP_STAT ||
        ByteOffset == BC250_HQD_MMIO_CP_MEC1_INSTR_PNTR ||
        ByteOffset == BC250_HQD_MMIO_CP_MEC2_INSTR_PNTR ||
        ByteOffset == 0x0000C3B4u ||
        ByteOffset == BC250_HQD_MMIO_RLC_CP_SCHEDULERS ||
        /* WGP topology/power policy: catalogued read-only preflight. */
        ByteOffset == BC250_HQD_MMIO_CC_GC_SHADER_ARRAY_CONFIG ||
        ByteOffset == BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK ||
        ByteOffset == BC250_HQD_MMIO_RLC_PG_CNTL ||
        ByteOffset == BC250_HQD_MMIO_RLC_PG_ALWAYS_ON_WGP_MASK ||
        ByteOffset == BC250_HQD_MMIO_SMUIO_GFX_MISC_CNTL ||
        ByteOffset == BC250_HQD_MMIO_CPC_UTCL1_ERROR ||
        ByteOffset == BC250_HQD_MMIO_CP_ME1_PIPE0_INT_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CP_ME2_PIPE1_INT_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CP_ME2_PIPE0_INT_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CPC_INT_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CPC_UTCL1_STATUS ||
        ByteOffset == BC250_HQD_MMIO_CPF_UTCL1_STATUS ||
        ByteOffset == BC250_HQD_MMIO_HQ_STATUS0 ||
        ByteOffset == BC250_HQD_MMIO_HQ_CONTROL0 ||
        ByteOffset == BC250_HQD_MMIO_HQD_ERROR ||
        ByteOffset == 0x0000C10Cu || ByteOffset == 0x0000C110u ||
        ByteOffset == 0x0000C3ACu || ByteOffset == 0x0000C3B0u ||
        ByteOffset == 0x00008234u || ByteOffset == 0x0000A104u ||
        ByteOffset == 0x00030934u || ByteOffset == 0x00030A04u ||
        ByteOffset == 0x0000A28Cu || ByteOffset == 0x0000A2D4u ||
        ByteOffset == 0x0003C2C4u ||
        ByteOffset == 0x00008920u || ByteOffset == 0x0000C6DCu ||
        ByteOffset == 0x0000C4CCu || ByteOffset == 0x0000C4D0u ||
        ByteOffset == 0x0000C4D4u || ByteOffset == 0x00030804u ||
        ByteOffset == 0x0000A200u || ByteOffset == 0x0000A3ACu ||
        ByteOffset == 0x0000A3B0u || ByteOffset == 0x0000A42Cu ||
        ByteOffset == 0x0000A430u || ByteOffset == 0x0000A4ACu ||
        ByteOffset == 0x0003E068u || ByteOffset == 0x0003E06Cu ||
        ByteOffset == 0x0003E130u || ByteOffset == 0x0003E134u ||
        ByteOffset == 0x0003E138u || ByteOffset == 0x0003E13Cu ||
        ByteOffset == 0x00008008u || ByteOffset == 0x00008010u ||
        ByteOffset == 0x00008020u ||
        /*
         * 19/09: sonda do ponto de parada do CP grafico, SOMENTE LEITURA.
         * Offsets derivados de inc/linux_ref/gc_10_1_0_offset.h pela regra de
         * docs/03; a justificativa completa esta em
         * driver/core/bc250_gfx_ring_transaction.h. Nenhum deles esta na
         * allowlist de ESCRITA, e isso e' deliberado.
         */
        ByteOffset == 0x00008014u || ByteOffset == 0x00008018u ||
        ByteOffset == 0x00008684u || ByteOffset == 0x00008688u ||
        ByteOffset == 0x00008C24u ||
        /* SQ_IND_DATA: janela de leitura indireta da sonda de onda (19/09). */
        ByteOffset == 0x00008DE4u ||
        /* DX.32: COMPUTE_VMID no snapshot de TDR -- SOMENTE LEITURA, fora da allowlist de escrita. */
        ByteOffset == 0x0000B850u ||
        /* DX.33: CP_ME0_PIPE0_VMID (VMID do pipe grafico ME0/pipe0) -- SOMENTE LEITURA. */
        ByteOffset == 0x0000C148u ||
        ByteOffset == 0x00030330u || ByteOffset == 0x00030334u ||
        ByteOffset == 0x00030338u ||
        ByteOffset == 0x00008680u || ByteOffset == 0x00008694u ||
        ByteOffset == 0x00008698u ||
        ByteOffset == BC250_GRBM_GFX_CNTL_BYTE_OFFSET ||
        ByteOffset == BC250_HQD_MMIO_MQD_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_MQD_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_MQD_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_EOP_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_EOP_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_EOP_RPTR ||
        ByteOffset == BC250_HQD_MMIO_EOP_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_ACTIVE ||
        ByteOffset == BC250_HQD_MMIO_VMID ||
        ByteOffset == BC250_HQD_MMIO_PERSISTENT_STATE ||
        ByteOffset == BC250_HQD_MMIO_PIPE_PRIORITY ||
        ByteOffset == BC250_HQD_MMIO_QUEUE_PRIORITY ||
        ByteOffset == BC250_HQD_MMIO_QUANTUM ||
        ByteOffset == BC250_HQD_MMIO_PQ_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_PQ_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_PQ_RPTR ||
        ByteOffset == BC250_HQD_MMIO_RPTR_REPORT_LO ||
        ByteOffset == BC250_HQD_MMIO_RPTR_REPORT_HI ||
        ByteOffset == BC250_HQD_MMIO_WPTR_POLL_LO ||
        ByteOffset == BC250_HQD_MMIO_WPTR_POLL_HI ||
        ByteOffset == BC250_HQD_MMIO_DOORBELL_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_PQ_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_IB_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_IB_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_IB_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_IB_RPTR ||
        ByteOffset == BC250_HQD_MMIO_DEQUEUE_REQUEST ||
        ByteOffset == BC250_HQD_MMIO_PQ_WPTR_LO ||
        ByteOffset == BC250_HQD_MMIO_PQ_WPTR_HI ||
        ByteOffset == 0x00030100u ||
        ByteOffset == 0x000086A0u || ByteOffset == 0x000086A4u ||
        ByteOffset == 0x0000A4B0u || ByteOffset == 0x00030800u ||
        /*
         * INVENTARIO DOS 16 CONTEXTOS GCVM -- SOMENTE LEITURA (21/09).
         *
         * A Corrida 49 provou que varios contextos compartilham o VMID 7 e que
         * suas raizes se alternam: 5 faults, 5 comparacoes, divergencia de 2 a
         * 15 carimbos de 16, e o MESMO IB submetido sob TRES raizes distintas.
         * O conserto e' VMID por contexto -- e para isso e preciso saber quais
         * VMIDs estao livres, em vez de assumir.
         *
         * Layout regular, conferido contra gc_10_1_0_offset.h E contra as
         * constantes de CONTEXT0 ja presentes em bc250_compute_service.c:
         *   CNTL        = 0xA200 + vmid*4
         *   PT_BASE_LO  = 0xA3AC + vmid*8   (HI = +4)
         * Liberar a FAIXA inteira aqui vale so' para LEITURA; a allowlist de
         * escrita continua restrita aos VMIDs 7 e 8, entao um inventario nao
         * pode, por construcao, reprogramar contexto nenhum.
         */
        (ByteOffset >= 0x0000A200u && ByteOffset < 0x0000A240u &&
         (ByteOffset & 3u) == 0u) ||
        (ByteOffset >= 0x0000A3ACu && ByteOffset < 0x0000A42Cu &&
         (ByteOffset & 3u) == 0u) ||
        /*
         * I.PLAT (doc 553) -- LEITURA da amostra de plataforma.
         * SDMA1 STATUS/F32_CNTL/GFX_RB_CNTL/RPTR/WPTR: gc_10_1_0_offset.h,
         * BASE_IDX 0, base 0x1260 dwords (P1). O SDMA1 nunca e' escrito.
         * RSMU_INDEX/DATA 0x38/0x3C: docs/03 secao "Acesso indireto SMN" (P0).
         */
        ByteOffset == 0x00006214u || ByteOffset == 0x00006228u ||
        ByteOffset == 0x00006380u || ByteOffset == 0x0000638Cu ||
        ByteOffset == 0x00006394u ||
        ByteOffset == 0x00000038u || ByteOffset == 0x0000003Cu ||
        /*
         * I.SDMA0 / I.CG (doc 556) -- LEITURA. gc_10_1_0_offset.h (P1):
         * SDMA0_GFX_IB_RPTR 0x4BAC, SDMA0_GFX_CONTEXT_STATUS 0x4BC4,
         * SDMA0_POWER_CNTL 0x49E8, SDMA1_POWER_CNTL 0x61E8,
         * SDMA1_CLK_CTRL 0x61EC, RLC_CGTT_MGCG_OVERRIDE 0x3B120,
         * RLC_CGCG_CGLS_CTRL 0x3B124, CGTT_SPI_CLK_CTRL 0x3C200.
         * Os demais da sonda (0x4A14/0x4A18/0x4A60/0x4B8C/0x4B94/0x49EC) ja'
         * estavam liberados. Nenhum entra na allowlist de escrita.
         */
        ByteOffset == 0x00004BACu || ByteOffset == 0x00004BC4u ||
        ByteOffset == 0x000049E8u || ByteOffset == 0x000061E8u ||
        ByteOffset == 0x000061ECu || ByteOffset == 0x0003B120u ||
        ByteOffset == 0x0003B124u || ByteOffset == 0x0003C200u ||
        /*
         * RLC.1 (doc 642) -- LEITURA: RLC_PG_CNTL 0x3B10C e RLC_SPM_MC_CNTL
         * 0x3B1C4 (gc_10_1_0_offset.h 0x4C43/0x4C71, BASE_IDX 1). RLC_CNTL,
         * RLC_GPM_STAT, RLC_CGCG_CGLS_CTRL, GRBM_STATUS2 e CP_MEC_CNTL ja' estavam.
         */
        ByteOffset == 0x0003B10Cu || ByteOffset == 0x0003B1C4u;
}

static uint32_t Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)
{
    return Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||
        Bc250MmioTopologyMmhubP3bWriteAllowed(ByteOffset) ||
        Bc250MmioTopologyG1GoldenWriteAllowed(ByteOffset) ||
        /*
         * 4c (doc 567): faixa do engine 1 (SDMA0), ESCRITA. O UNICO escritor e'
         * Bc250ActiveArmSdmaInvalidateEngine: snapshot, 0xFFFFFFFF/0x1F como
         * gfxhub_v2_0_program_invalidation, readback, e restaura se divergir.
         */
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_LO ||
        ByteOffset == BC250_REG_GCVM_INVALIDATE_ENG1_ADDR_RANGE_HI ||
        /*
         * 0xA120 GCVM_L2_PROTECTION_FAULT_CNTL -- ESCRITA, aberta na Corrida 56.
         *
         * JUSTIFICATIVA MEDIDA, nao conveniencia: a Corrida 55 leu
         * LATCH_CNTL=0x3FFFFFFC, isto e Clear=0 e AceitaAtualizacoes=0. O par
         * STATUS/ADDR trava no PRIMEIRO fault e so se solta quando alguem
         * escreve o bit 0. Sem esta escrita cada TDR le um residuo antigo --
         * foi o que aconteceu em dez corridas.
         *
         * O UNICO escritor autorizado e Bc250FaultLatchDrain, que faz snapshot,
         * pulsa somente o bit 0 preservando os demais (Lei 6), restaura o valor
         * de entrada e PROVA por readback. Um portao exige esse formato.
         */
        ByteOffset == BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL ||
        /*
         * DX.27 -- RLC_CSIB_ADDR_LO/HI/LENGTH, ESCRITA aberta apos medicao.
         *
         * A Corrida 72 leu os tres em zero (CSB nao programado). O UNICO
         * escritor autorizado e' Bc250ActiveProgramClearStateBuffer, que faz
         * snapshot, escreve, confere por readback e restaura se nao conferir
         * (Lei 5); um portao exige esse formato. Os golden settings continuam
         * FORA desta allowlist: so' leitura.
         */
        ByteOffset == BC250_REG_RLC_CSIB_ADDR_LO_BYTE ||
        ByteOffset == BC250_REG_RLC_CSIB_ADDR_HI_BYTE ||
        ByteOffset == BC250_REG_RLC_CSIB_LENGTH_BYTE ||
        ByteOffset == 0xA2D0u ||
        ByteOffset == 0xA3A4u ||
        ByteOffset == 0xA3A8u ||
        Bc250VmidPoolIsWritableGcvmRegister(ByteOffset) ||
        Bc250GcvmIsUserRegister(ByteOffset) ||
        Bc250GcvmIsComputeRegister(ByteOffset) ||
        ByteOffset == BC250_GCVM_VMID8_CNTL ||
        ByteOffset == BC250_GCVM_VMID8_PT_BASE_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_BASE_HI ||
        ByteOffset == BC250_GCVM_VMID8_PT_START_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_START_HI ||
        ByteOffset == BC250_GCVM_VMID8_PT_END_LO ||
        ByteOffset == BC250_GCVM_VMID8_PT_END_HI ||
        ByteOffset == BC250_PSP_RING_C2PMSG_64 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_67 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_69 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_70 ||
        ByteOffset == BC250_PSP_RING_C2PMSG_71 ||
        ByteOffset == BC250_SDMA0_CLK_CTRL ||
        ByteOffset == BC250_SDMA0_CNTL ||
        ByteOffset == BC250_SDMA0_F32_CNTL ||
        ByteOffset == 0x00003FC4u || ByteOffset == 0x00003FCCu ||
        /*
         * HDP.1 (28/09, doc 519): 0x385C e' o flush de ESCRITA real do HDP
         * (BIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL, proveniencia em
         * ../core/bc250_hdp_contract.h). 0x4A80 SAIU desta lista: e'
         * SDMA0_UTCL1_INV0, que o upstream nunca escreve e que era zerado aqui
         * como se fosse flush. Continua na allowlist de LEITURA (sonda).
         */
        ByteOffset == BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL ||
        ByteOffset == 0x00004A04u || ByteOffset == 0x000049F4u ||
        ByteOffset == 0x000049F8u || ByteOffset == 0x000049FCu ||
        ByteOffset == 0x00004A70u ||
        ByteOffset == 0x00004AA0u || ByteOffset == 0x00004B80u ||
        ByteOffset == 0x00004B84u || ByteOffset == 0x00004B88u ||
        ByteOffset == 0x00004B8Cu || ByteOffset == 0x00004B90u ||
        ByteOffset == 0x00004B94u || ByteOffset == 0x00004B98u ||
        ByteOffset == 0x00004B9Cu || ByteOffset == 0x00004BA0u ||
        ByteOffset == 0x00004BA4u || ByteOffset == 0x00004BA8u ||
        ByteOffset == 0x00004BC8u || ByteOffset == 0x00004C48u ||
        ByteOffset == 0x00004C4Cu || ByteOffset == 0x00004C54u ||
        ByteOffset == 0x0003B000u || ByteOffset == 0x0003B014u || ByteOffset == 0x000086D8u ||
        ByteOffset == 0x0003E050u || ByteOffset == 0x0003E054u ||
        ByteOffset == 0x0003E058u || ByteOffset == 0x0003E05Cu ||
        ByteOffset == 0x0003E060u || ByteOffset == 0x0003E064u ||
        ByteOffset == 0x0003E100u || ByteOffset == 0x0003E104u ||
        ByteOffset == 0x0003E108u || ByteOffset == 0x0003E10Cu ||
        ByteOffset == 0x0003E110u || ByteOffset == 0x0003E114u ||
        ByteOffset == 0x0003E118u || ByteOffset == 0x0003E11Cu ||
        ByteOffset == 0x0003E120u || ByteOffset == 0x0003E124u ||
        ByteOffset == 0x0003E128u || ByteOffset == 0x0003E12Cu ||
        ByteOffset == 0x0000C2B8u || ByteOffset == 0x0000C12Cu ||
        ByteOffset == BC250_REG_CP_RB0_BASE ||
        ByteOffset == BC250_REG_CP_RB0_BASE_HI ||
        ByteOffset == BC250_REG_CP_RB0_CNTL ||
        ByteOffset == BC250_REG_CP_RB0_RPTR ||
        ByteOffset == BC250_REG_CP_RB0_WPTR ||
        ByteOffset == BC250_REG_CP_RB0_WPTR_HI ||
        /*
         * EOP do CP (doc 618): CP_INT_CNTL_RING0, ESCRITA. Unicos escritores:
         * Bc250ActiveCpEopIrqArm/Disarm, que mudam SO o TIME_STAMP_INT_ENABLE
         * (Lei 6), com snapshot, readback e restauracao.
         */
        ByteOffset == BC250_REG_CP_INT_CNTL_RING0 ||
        /*
         * MEC.1 (doc 640): CP_MEC_DOORBELL_RANGE_UPPER, ESCRITA. Unicos
         * escritores: Bc250ActiveMec1Arm/Disarm (snapshot, readback, restauracao).
         * O LOWER NAO e' escrito: a fila do MEC deriva dele o offset da doorbell.
         */
        ByteOffset == BC250_REG_CP_MEC_DOORBELL_RANGE_UPPER ||
        /*
         * C1 (doc 672): CP_RB_DOORBELL_RANGE_LOWER/UPPER, ESCRITA, como
         * gfx_v10_0_cp_gfx_set_doorbell (gc_10_1_0_offset.h 0x1DFA/0x1DFB,
         * BASE_IDX 0). Unicos escritores: Bc250ActiveC1GfxDoorbellArm/Disarm
         * (snapshot, readback e restauracao). CP_RB_DOORBELL_CONTROL ja' estava.
         */
        ByteOffset == BC250_REG_CP_RB_DOORBELL_RANGE_LOWER ||
        ByteOffset == BC250_REG_CP_RB_DOORBELL_RANGE_UPPER ||
        /*
         * RLC.1 (doc 642), ESCRITA: RLC_CGCG_CGLS_CTRL 0x3B124, RLC_PG_CNTL
         * 0x3B10C e RLC_SPM_MC_CNTL 0x3B1C4. Unicos escritores:
         * Bc250ActiveRlc1Resume/Disarm (gfx_v10_0_rlc_resume, snapshot,
         * readback e restauracao). RLC_CNTL e CP_MEC_CNTL ja' estavam.
         */
        ByteOffset == 0x0003B124u || ByteOffset == 0x0003B10Cu ||
        ByteOffset == 0x0003B1C4u ||
        /*
         * P4.1 (doc 620): SDMA1, ESCRITA. Unicos escritores: Bc250Sdma1BringUp e
         * Bc250Sdma1Shutdown (snapshot, Lei 6, readback, desligamento provado).
         * A lista e' a do contrato; POWER_CNTL e o resto do bloco ficam fora.
         */
        Bc250Sdma1IsRegister(ByteOffset) ||
        ByteOffset == 0x0000C680u || ByteOffset == 0x0000C144u ||
        ByteOffset == 0x00008704u || ByteOffset == 0x00008708u ||
        ByteOffset == 0x0000C168u || ByteOffset == 0x0000C16Cu ||
        ByteOffset == 0x0000C170u || ByteOffset == 0x0000C174u ||
        ByteOffset == 0x0000C170u || ByteOffset == 0x0000C174u ||
        ByteOffset == 0x0000C2E0u || ByteOffset == 0x0000C20Cu || ByteOffset == 0x0000C210u ||
        ByteOffset == 0x0000C3B4u ||
        ByteOffset == BC250_HQD_MMIO_RLC_CP_SCHEDULERS ||
        ByteOffset == 0x0000C10Cu || ByteOffset == 0x0000C110u ||
        ByteOffset == 0x0000C3ACu || ByteOffset == 0x0000C3B0u ||
        ByteOffset == 0x00008234u || ByteOffset == 0x0000A104u ||
        ByteOffset == 0x00030934u || ByteOffset == 0x00030A04u ||
        ByteOffset == 0x0000A28Cu || ByteOffset == 0x0000A2D4u ||
        ByteOffset == 0x0003C2C4u ||
        ByteOffset == 0x00008920u || ByteOffset == 0x0000C6DCu ||
        ByteOffset == 0x00030804u ||
        ByteOffset == 0x0000A200u || ByteOffset == 0x0000A3ACu ||
        ByteOffset == 0x0000A3B0u || ByteOffset == 0x0000A42Cu ||
        ByteOffset == 0x0000A430u || ByteOffset == 0x0000A4ACu ||
        ByteOffset == 0x0003E068u || ByteOffset == 0x0003E06Cu ||
        ByteOffset == 0x0003E130u || ByteOffset == 0x0003E134u ||
        ByteOffset == 0x0003E138u || ByteOffset == 0x0003E13Cu ||
        ByteOffset == 0x00008008u || ByteOffset == 0x00008010u ||
        ByteOffset == 0x00008020u ||
        ByteOffset == BC250_GRBM_GFX_CNTL_BYTE_OFFSET ||
        ByteOffset == BC250_HQD_MMIO_MQD_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_MQD_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_MQD_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_EOP_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_EOP_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_EOP_RPTR ||
        ByteOffset == BC250_HQD_MMIO_EOP_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_VMID ||
        ByteOffset == BC250_HQD_MMIO_PERSISTENT_STATE ||
        ByteOffset == BC250_HQD_MMIO_PIPE_PRIORITY ||
        ByteOffset == BC250_HQD_MMIO_QUEUE_PRIORITY ||
        ByteOffset == BC250_HQD_MMIO_QUANTUM ||
        ByteOffset == BC250_HQD_MMIO_PQ_BASE_LO ||
        ByteOffset == BC250_HQD_MMIO_PQ_BASE_HI ||
        ByteOffset == BC250_HQD_MMIO_PQ_RPTR ||
        ByteOffset == BC250_HQD_MMIO_RPTR_REPORT_LO ||
        ByteOffset == BC250_HQD_MMIO_RPTR_REPORT_HI ||
        ByteOffset == BC250_HQD_MMIO_WPTR_POLL_LO ||
        ByteOffset == BC250_HQD_MMIO_WPTR_POLL_HI ||
        ByteOffset == BC250_HQD_MMIO_DOORBELL_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_PQ_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_IB_CONTROL ||
        ByteOffset == BC250_HQD_MMIO_DEQUEUE_REQUEST ||
        ByteOffset == BC250_HQD_MMIO_ACTIVE ||
        ByteOffset == BC250_HQD_MMIO_PQ_WPTR_LO ||
        ByteOffset == BC250_HQD_MMIO_PQ_WPTR_HI ||
        ByteOffset == 0x00030100u ||
        ByteOffset == 0x000086A0u ||
        ByteOffset == 0x0000A4B0u || ByteOffset == 0x00030800u ||
        /*
         * SQ_IND_INDEX -- UNICA escrita nova de 19/09, e ela e um SELETOR:
         * nao altera estado de engine, so escolhe qual registrador de onda o
         * SQ_IND_DATA devolve. A sonda que a usa restaura GRBM_GFX_INDEX para
         * broadcast com readback e devolve SELECTOR_STUCK se nao conseguir.
         */
        ByteOffset == 0x00008DE0u ||
        /*
         * I.PLAT (doc 553) -- RSMU_INDEX 0x38, tambem um SELETOR: escolhe qual
         * endereco SMN o RSMU_DATA devolve. O UNICO escritor e'
         * Bc250ActivePlatformProbe, com o valor fixo 0x59800 (Tctl, k10temp),
         * sob lock e com releitura. RSMU_DATA 0x3C NAO entra aqui: escrever o
         * dado escreveria no SMN. Portao audit_iplat_platform_probe.
         */
        ByteOffset == 0x00000038u;
}

uint32_t Bc250MmioTopologyRead32(
    void *Context, uint32_t ByteOffset, uint32_t *Value)
{
    BC250_MMIO_TOPOLOGY_CONTEXT *transport =
        (BC250_MMIO_TOPOLOGY_CONTEXT *)Context;
    volatile ULONG *address;

    if (Value != NULL) *Value = 0;
    if (transport == NULL || Value == NULL ||
        transport->Signature != BC250_MMIO_TOPOLOGY_SIGNATURE ||
        transport->State != BC250_MMIO_TOPOLOGY_MAPPED ||
        transport->VirtualBase == NULL ||
        !Bc250MmioTopologyOffsetAllowed(ByteOffset) ||
        (ByteOffset & (sizeof(uint32_t) - 1u)) != 0 ||
        transport->Length < sizeof(uint32_t) ||
        (uint64_t)ByteOffset > transport->Length - sizeof(uint32_t)) {
        return 0;
    }
    address = (volatile ULONG *)((PUCHAR)transport->VirtualBase + ByteOffset);
    {
        ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_TRANSPORT_READ, ByteOffset);
        *Value = READ_REGISTER_ULONG(address);
        Bc250CrumbAfter(crumb);
    }
    ++transport->ReadCount;
    return 1;
}

uint32_t Bc250MmioTopologyWrite32(
    void *Context, uint32_t ByteOffset, uint32_t Value)
{
    BC250_MMIO_TOPOLOGY_CONTEXT *transport =
        (BC250_MMIO_TOPOLOGY_CONTEXT *)Context;
    volatile ULONG *address;

    if (transport == NULL ||
        transport->Signature != BC250_MMIO_TOPOLOGY_SIGNATURE ||
        transport->State != BC250_MMIO_TOPOLOGY_MAPPED ||
        transport->VirtualBase == NULL ||
        !Bc250MmioTopologyWriteOffsetAllowed(ByteOffset) ||
        (ByteOffset & (sizeof(uint32_t) - 1u)) != 0 ||
        transport->Length < sizeof(uint32_t) ||
        (uint64_t)ByteOffset > transport->Length - sizeof(uint32_t)) {
        return 0u;
    }
    address = (volatile ULONG *)((PUCHAR)transport->VirtualBase + ByteOffset);
    {
        ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_TRANSPORT_WRITE, ByteOffset);
        WRITE_REGISTER_ULONG(address, Value);
        KeMemoryBarrier();
        Bc250CrumbAfter(crumb);
    }
    ++transport->WriteCount;
    return 1u;
}

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyUnmap(
    BC250_MMIO_TOPOLOGY_CONTEXT *Context)
{
    NTSTATUS status;

    if (Context == NULL ||
        Context->Signature != BC250_MMIO_TOPOLOGY_SIGNATURE) {
        return BC250_MMIO_TOPOLOGY_BAD_ARGUMENT;
    }
    if (Context->State == BC250_MMIO_TOPOLOGY_EMPTY &&
        Context->VirtualBase == NULL) {
        return BC250_MMIO_TOPOLOGY_OK;
    }
    if (Context->State != BC250_MMIO_TOPOLOGY_MAPPED ||
        Context->VirtualBase == NULL || Context->DeviceHandle == NULL ||
        Context->UnmapMemory == NULL) {
        return BC250_MMIO_TOPOLOGY_WRONG_STATE;
    }
    status = Context->UnmapMemory(Context->DeviceHandle, Context->VirtualBase);
    Context->LastCallbackStatus = status;
    if (!NT_SUCCESS(status)) return BC250_MMIO_TOPOLOGY_UNMAP_FAILED;
    Bc250MmioTopologyClearMapping(Context);
    return BC250_MMIO_TOPOLOGY_OK;
}

const char *Bc250MmioTopologyStatusName(BC250_MMIO_TOPOLOGY_STATUS Status)
{
    switch (Status) {
    case BC250_MMIO_TOPOLOGY_OK: return "ok";
    case BC250_MMIO_TOPOLOGY_BAD_ARGUMENT: return "bad_argument";
    case BC250_MMIO_TOPOLOGY_WRONG_STATE: return "wrong_state";
    case BC250_MMIO_TOPOLOGY_INTERFACE_TOO_SMALL:
        return "interface_too_small";
    case BC250_MMIO_TOPOLOGY_BAR_NOT_UNIQUE: return "bar_not_unique";
    case BC250_MMIO_TOPOLOGY_BAR_TOO_SHORT: return "bar_too_short";
    case BC250_MMIO_TOPOLOGY_BAR_TOO_LARGE: return "bar_too_large";
    case BC250_MMIO_TOPOLOGY_PHYSICAL_RANGE_INVALID:
        return "physical_range_invalid";
    case BC250_MMIO_TOPOLOGY_MAP_FAILED: return "map_failed";
    case BC250_MMIO_TOPOLOGY_MAP_RETURNED_NULL: return "map_returned_null";
    case BC250_MMIO_TOPOLOGY_UNMAP_FAILED: return "unmap_failed";
    default: return "unknown";
    }
}
