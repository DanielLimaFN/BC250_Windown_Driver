#include "bc250_mmio_topology_transport.h"

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

static uint32_t Bc250MmioTopologyOffsetAllowed(uint32_t ByteOffset)
{
    return Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||
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
        ByteOffset == 0x0003B000u || ByteOffset == 0x0003B9B8u ||
        ByteOffset == 0x000086D8u || ByteOffset == 0x00008694u ||
        ByteOffset == 0x00008698u || ByteOffset == 0x0000869Cu ||
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
        ByteOffset == 0x0000A4B0u || ByteOffset == 0x00030800u;
}

static uint32_t Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)
{
    return Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||
        ByteOffset == 0xA2D0u ||
        ByteOffset == 0xA3A4u ||
        ByteOffset == 0xA3A8u ||
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
        ByteOffset == 0x00004A04u || ByteOffset == 0x000049F4u ||
        ByteOffset == 0x000049F8u || ByteOffset == 0x000049FCu ||
        ByteOffset == 0x00004A70u || ByteOffset == 0x00004A80u ||
        ByteOffset == 0x00004AA0u || ByteOffset == 0x00004B80u ||
        ByteOffset == 0x00004B84u || ByteOffset == 0x00004B88u ||
        ByteOffset == 0x00004B8Cu || ByteOffset == 0x00004B90u ||
        ByteOffset == 0x00004B94u || ByteOffset == 0x00004B98u ||
        ByteOffset == 0x00004B9Cu || ByteOffset == 0x00004BA0u ||
        ByteOffset == 0x00004BA4u || ByteOffset == 0x00004BA8u ||
        ByteOffset == 0x00004BC8u || ByteOffset == 0x00004C48u ||
        ByteOffset == 0x00004C4Cu || ByteOffset == 0x00004C54u ||
        ByteOffset == 0x0003B000u || ByteOffset == 0x000086D8u ||
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
        ByteOffset == 0x00008DE0u;
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
    *Value = READ_REGISTER_ULONG(address);
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
    WRITE_REGISTER_ULONG(address, Value);
    KeMemoryBarrier();
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
