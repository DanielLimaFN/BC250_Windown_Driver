#include "bc250_probe_telemetry.h"

static volatile LONG g_Bc250ProbeSequence;
static volatile LONG g_Bc250ProbeLastField;
static volatile LONG g_Bc250ProbeLastValue;
static volatile LONG g_Bc250ProbeFirstFailure;

#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)

static const UNICODE_STRING g_Bc250ProbeRegistryPathMcdm = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\"
    L"bc250mcdm\\Parameters\\Probe");

static const UNICODE_STRING g_Bc250ProbeRegistryPathReadonly = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\"
    L"bc250readonly\\Parameters\\Probe");

static PCWSTR Bc250ProbeFieldName(BC250_PROBE_FIELD Field)
{
    switch (Field) {
    case BC250_PROBE_DRIVER_ENTRY_REACHED: return L"DriverEntryReached";
    case BC250_PROBE_INITIALIZATION_DATA_READY:
        return L"InitializationDataReady";
    case BC250_PROBE_DXGK_INITIALIZE_STATUS:
        return L"DxgkInitializeStatus";
    case BC250_PROBE_ADD_DEVICE_ENTERED: return L"AddDeviceEntered";
    case BC250_PROBE_ADD_DEVICE_STATUS: return L"AddDeviceStatus";
    case BC250_PROBE_START_DEVICE_ENTERED: return L"StartDeviceEntered";
    case BC250_PROBE_INTERFACE_ACCEPTED: return L"InterfaceAccepted";
    case BC250_PROBE_DEVICE_INFO_STATUS: return L"DeviceInfoStatus";
    case BC250_PROBE_PCI_STATUS: return L"PciStatus";
    case BC250_PROBE_PCI_BYTES_READ: return L"PciBytesRead";
    case BC250_PROBE_BAR_COUNT: return L"BarCount";
    case BC250_PROBE_RUNTIME_MISSING: return L"RuntimeMissing";
    case BC250_PROBE_STOP_DEVICE_ENTERED: return L"StopDeviceEntered";
    case BC250_PROBE_STOP_DEVICE_STATUS: return L"StopDeviceStatus";
    case BC250_PROBE_REMOVE_DEVICE_ENTERED: return L"RemoveDeviceEntered";
    case BC250_PROBE_REMOVE_DEVICE_STATUS: return L"RemoveDeviceStatus";
    case BC250_PROBE_CM_STATUS: return L"CmStatus";
    case BC250_PROBE_RESOURCE_STATUS: return L"ResourceStatus";
    case BC250_PROBE_RESOLVED_BAR_COUNT: return L"ResolvedBarCount";
    case BC250_PROBE_MMIO_STATUS: return L"MmioStatus";
    case BC250_PROBE_TOPOLOGY_READER_STATUS:
        return L"TopologyReaderStatus";
    case BC250_PROBE_TOPOLOGY_STATUS: return L"TopologyStatus";
    case BC250_PROBE_TOPOLOGY_READ_COUNT: return L"TopologyReadCount";
    case BC250_PROBE_TOPOLOGY_STABLE_PASSES:
        return L"TopologyStablePasses";
    case BC250_PROBE_VRAM_TOTAL_MIB: return L"VramTotalMiB";
    case BC250_PROBE_CPU_VISIBLE_MIB: return L"CpuVisibleMiB";
    case BC250_PROBE_FB_BASE_16M: return L"FbBase16M";
    case BC250_PROBE_FB_TOP_16M: return L"FbTop16M";
    case BC250_PROBE_FB_OFFSET_16M: return L"FbOffset16M";
    case BC250_PROBE_ADAPTER_STATUS: return L"AdapterStatus";
    case BC250_PROBE_OWNED_STATUS: return L"OwnedStatus";
    case BC250_PROBE_BOOTSTRAP_STATUS: return L"BootstrapStatus";
    case BC250_PROBE_VRAM_STATUS: return L"VramStatus";
    case BC250_PROBE_OWNERSHIP_STATE: return L"OwnershipState";
    case BC250_PROBE_OWNERSHIP_FLAGS: return L"OwnershipFlags";
    case BC250_PROBE_REBAR_STATUS: return L"RebarStatus";
    case BC250_PROBE_REBAR_SUPPORTED_MASK: return L"RebarSupportedMask";
    case BC250_PROBE_REBAR_SELECTED_ENCODING:
        return L"RebarSelectedEncoding";
    case BC250_PROBE_REBAR_SELECTED_MIB: return L"RebarSelectedMiB";
    case BC250_PROBE_REBAR_ASSIGNED_MIB: return L"RebarAssignedMiB";
    case BC250_PROBE_PFP_PAYLOAD_BYTES: return L"PfpPayloadBytes";
    case BC250_PROBE_ME_PAYLOAD_BYTES: return L"MePayloadBytes";
    case BC250_PROBE_CE_PAYLOAD_BYTES: return L"CePayloadBytes";
    case BC250_PROBE_MEC_PAYLOAD_BYTES: return L"MecPayloadBytes";
    case BC250_PROBE_MEC_JT1_PAYLOAD_BYTES: return L"MecJt1PayloadBytes";
    case BC250_PROBE_CANDIDATE_TMR_OFFSET_LOW32:
        return L"CandidateTmrOffsetLow32";
    case BC250_PROBE_CANDIDATE_TMR_BYTES: return L"CandidateTmrBytes";
    case BC250_PROBE_STAGING_OFFSET_LOW32:
        return L"StagingOffsetLow32";
    case BC250_PROBE_STAGING_BYTES: return L"StagingBytes";
    case BC250_PROBE_FIRMWARE_BYTES_STAGED:
        return L"FirmwareBytesStaged";
    case BC250_PROBE_PSP_TRANSPORT_CALLS: return L"PspTransportCalls";
    case BC250_PROBE_READONLY_GATE_PASSED: return L"ReadonlyGatePassed";
    case BC250_PROBE_DISCOVERY_TMR_STATUS: return L"DiscoveryTmrStatus";
    case BC250_PROBE_DISCOVERY_TMR_OFFSET_LOW32:
        return L"DiscoveryTmrOffsetLow32";
    case BC250_PROBE_DISCOVERY_TMR_BYTES: return L"DiscoveryTmrBytes";
    case BC250_PROBE_DISCOVERY_TMR_READ_COUNT:
        return L"DiscoveryTmrReadCount";
    case BC250_PROBE_DISCOVERY_TMR_STABLE_PASSES:
        return L"DiscoveryTmrStablePasses";
    case BC250_PROBE_POST_DISPLAY_STATUS: return L"PostDisplayStatus";
    case BC250_PROBE_POST_DISPLAY_CALLBACK_STATUS:
        return L"PostDisplayCallbackStatus";
    case BC250_PROBE_POST_DISPLAY_USED_CALLBACK2:
        return L"PostDisplayUsedCallback2";
    case BC250_PROBE_POST_DISPLAY_FRAMEBUFFER_STATE:
        return L"PostDisplayFramebufferState";
    case BC250_PROBE_POST_DISPLAY_TARGET_ID: return L"PostDisplayTargetId";
    case BC250_PROBE_POST_DISPLAY_WIDTH: return L"PostDisplayWidth";
    case BC250_PROBE_POST_DISPLAY_HEIGHT: return L"PostDisplayHeight";
    case BC250_PROBE_POST_DISPLAY_PITCH: return L"PostDisplayPitch";
    case BC250_PROBE_POST_DISPLAY_PHYSICAL_LOW32:
        return L"PostDisplayPhysicalLow32";
    case BC250_PROBE_POST_DISPLAY_PHYSICAL_HIGH32:
        return L"PostDisplayPhysicalHigh32";
    case BC250_PROBE_POST_DISPLAY_BYTES: return L"PostDisplayBytes";
    case BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_LOW32:
        return L"PostDisplayVramOffsetLow32";
    case BC250_PROBE_POST_DISPLAY_VRAM_OFFSET_HIGH32:
        return L"PostDisplayVramOffsetHigh32";
    case BC250_PROBE_RUNTIME_INVENTORY_STATUS:
        return L"RuntimeInventoryStatus";
    case BC250_PROBE_RUNTIME_INVENTORY_EVIDENCE_FLAGS:
        return L"RuntimeInventoryEvidenceFlags";
    case BC250_PROBE_RUNTIME_INVENTORY_RESERVATION_COUNT:
        return L"RuntimeInventoryReservationCount";
    case BC250_PROBE_RUNTIME_INVENTORY_RESERVED_LOW32:
        return L"RuntimeInventoryReservedLow32";
    case BC250_PROBE_RUNTIME_INVENTORY_RESERVED_HIGH32:
        return L"RuntimeInventoryReservedHigh32";
    case BC250_PROBE_RUNTIME_INVENTORY_COMPLETE:
        return L"RuntimeInventoryComplete";
    case BC250_PROBE_PLATFORM_SNAPSHOT_STATUS:
        return L"PlatformSnapshotStatus";
    case BC250_PROBE_PLATFORM_SNAPSHOT_READ_COUNT:
        return L"PlatformSnapshotReadCount";
    case BC250_PROBE_PLATFORM_SNAPSHOT_STABLE_PASSES:
        return L"PlatformSnapshotStablePasses";
    case BC250_PROBE_GCVM_PT_BASE_LO: return L"GcvmPtBaseLo";
    case BC250_PROBE_GCVM_PT_BASE_HI: return L"GcvmPtBaseHi";
    case BC250_PROBE_GCVM_PT_START_LO: return L"GcvmPtStartLo";
    case BC250_PROBE_GCVM_PT_START_HI: return L"GcvmPtStartHi";
    case BC250_PROBE_GCVM_PT_END_LO: return L"GcvmPtEndLo";
    case BC250_PROBE_GCVM_PT_END_HI: return L"GcvmPtEndHi";
    case BC250_PROBE_CP_RB0_BASE: return L"CpRb0Base";
    case BC250_PROBE_CP_RB0_BASE_HI: return L"CpRb0BaseHi";
    case BC250_PROBE_CP_RB0_CNTL: return L"CpRb0Cntl";
    case BC250_PROBE_SDMA0_RB_BASE: return L"Sdma0RbBase";
    case BC250_PROBE_SDMA0_RB_BASE_HI: return L"Sdma0RbBaseHi";
    case BC250_PROBE_SDMA0_RB_CNTL: return L"Sdma0RbCntl";
    case BC250_PROBE_CP_RB0_RPTR: return L"CpRb0Rptr";
    case BC250_PROBE_CP_RB0_RPTR_ADDR_LO: return L"CpRb0RptrAddrLo";
    case BC250_PROBE_CP_RB0_RPTR_ADDR_HI: return L"CpRb0RptrAddrHi";
    case BC250_PROBE_CP_RB0_WPTR: return L"CpRb0Wptr";
    case BC250_PROBE_CP_RB0_WPTR_HI: return L"CpRb0WptrHi";
    case BC250_PROBE_SDMA0_RB_RPTR: return L"Sdma0RbRptr";
    case BC250_PROBE_SDMA0_RB_RPTR_HI: return L"Sdma0RbRptrHi";
    case BC250_PROBE_SDMA0_RB_WPTR: return L"Sdma0RbWptr";
    case BC250_PROBE_SDMA0_RB_WPTR_HI: return L"Sdma0RbWptrHi";
    case BC250_PROBE_SDMA0_RB_RPTR_ADDR_LO:
        return L"Sdma0RbRptrAddrLo";
    case BC250_PROBE_SDMA0_RB_RPTR_ADDR_HI:
        return L"Sdma0RbRptrAddrHi";
    case BC250_PROBE_PSP_C2PMSG_35: return L"PspC2pmsg35";
    case BC250_PROBE_PSP_C2PMSG_36: return L"PspC2pmsg36";
    case BC250_PROBE_PSP_C2PMSG_37: return L"PspC2pmsg37";
    case BC250_PROBE_PSP_C2PMSG_81: return L"PspC2pmsg81";
    case BC250_PROBE_PSP_C2PMSG_64: return L"PspC2pmsg64";
    case BC250_PROBE_PSP_C2PMSG_67: return L"PspC2pmsg67";
    case BC250_PROBE_PSP_C2PMSG_69: return L"PspC2pmsg69";
    case BC250_PROBE_PSP_C2PMSG_70: return L"PspC2pmsg70";
    case BC250_PROBE_PSP_C2PMSG_71: return L"PspC2pmsg71";
    case BC250_PROBE_RUNTIME_INVENTORY_FREE_LOW32:
        return L"RuntimeInventoryFreeLow32";
    case BC250_PROBE_RUNTIME_INVENTORY_FREE_HIGH32:
        return L"RuntimeInventoryFreeHigh32";
    case BC250_PROBE_RUNTIME_INVENTORY_CPU_FREE_LOW32:
        return L"RuntimeInventoryCpuFreeLow32";
    case BC250_PROBE_RUNTIME_STAGE_STATUS: return L"RuntimeStageStatus";
    case BC250_PROBE_SECOND_PREFLIGHT_STAGE:
        return L"SecondPreflightStage";
    case BC250_PROBE_SECOND_READONLY_GATE_PASSED:
        return L"SecondReadonlyGatePassed";
    case BC250_PROBE_VRAM_PROBE_STATUS: return L"VramProbeStatus";
    case BC250_PROBE_VRAM_PROBE_OFFSET_LOW32:
        return L"VramProbeOffsetLow32";
    case BC250_PROBE_VRAM_PROBE_BYTES: return L"VramProbeBytes";
    case BC250_PROBE_VRAM_PROBE_READBACK_PASSED:
        return L"VramProbeReadbackPassed";
    case BC250_PROBE_VRAM_PROBE_RESTORE_PASSED:
        return L"VramProbeRestorePassed";
    case BC250_PROBE_RUNTIME_CAPS_STATUS: return L"RuntimeCapsStatus";
    case BC250_PROBE_RUNTIME_CAPS_READY: return L"RuntimeCapsReady";
    case BC250_PROBE_RUNTIME_CAPS_BASE_LOW32:
        return L"RuntimeCapsBaseLow32";
    case BC250_PROBE_RUNTIME_CAPS_LOCAL_LOW32:
        return L"RuntimeCapsLocalLow32";
    case BC250_PROBE_RUNTIME_CAPS_NODE_COUNT:
        return L"RuntimeCapsNodeCount";
    case BC250_PROBE_MCDM_FULL_CAPS_READY:
        return L"McdmFullCapsReady";
    case BC250_PROBE_MCDM_SEGMENT4_READY:
        return L"McdmSegment4Ready";
    case BC250_PROBE_ALLOCATION_SEGMENT_SET:
        return L"AllocationSegmentSet";
    case BC250_PROBE_ALLOCATION_MAX_LOW32:
        return L"AllocationMaxLow32";
    case BC250_PROBE_GPUVA_DESCRIPTOR_READY:
        return L"GpuVaDescriptorReady";
    case BC250_PROBE_ROOT_PAGE_TABLE_ABI_READY:
        return L"RootPageTableAbiReady";
    case BC250_PROBE_ROOT_PAGE_TABLE_MAX_ENTRIES:
        return L"RootPageTableMaxEntries";
    case BC250_PROBE_PAGING_CPU_UPDATE_READY:
        return L"PagingCpuUpdateReady";
    case BC250_PROBE_TLB_FLUSH_PHYSICAL_READY:
        return L"TlbFlushPhysicalReady";
    case BC250_PROBE_TLB_TRANSACTION_STATUS:
        return L"TlbTransactionStatus";
    case BC250_PROBE_TLB_VMID: return L"TlbVmid";
    case BC250_PROBE_TLB_ENGINE_ID: return L"TlbEngineId";
    case BC250_PROBE_TLB_REQUEST_VALUE: return L"TlbRequestValue";
    case BC250_PROBE_TLB_RANGE_LOW_BEFORE: return L"TlbRangeLowBefore";
    case BC250_PROBE_TLB_RANGE_HIGH_BEFORE: return L"TlbRangeHighBefore";
    case BC250_PROBE_TLB_RANGE_LOW_PROGRAMMED:
        return L"TlbRangeLowProgrammed";
    case BC250_PROBE_TLB_RANGE_HIGH_PROGRAMMED:
        return L"TlbRangeHighProgrammed";
    case BC250_PROBE_TLB_REQUEST_DUMMY_READ:
        return L"TlbRequestDummyRead";
    case BC250_PROBE_TLB_ACK_BEFORE: return L"TlbAckBefore";
    case BC250_PROBE_TLB_ACK_AFTER: return L"TlbAckAfter";
    case BC250_PROBE_TLB_POLL_COUNT: return L"TlbPollCount";
    case BC250_PROBE_TLB_REQUEST_WRITTEN: return L"TlbRequestWritten";
    case BC250_PROBE_TLB_DUMMY_READ_COMPLETED:
        return L"TlbDummyReadCompleted";
    case BC250_PROBE_TLB_ACK_OBSERVED: return L"TlbAckObserved";
    case BC250_PROBE_TLB_RANGE_RESTORE_PASSED:
        return L"TlbRangeRestorePassed";
    case BC250_PROBE_GCVM_BACKING_STATUS: return L"GcvmBackingStatus";
    case BC250_PROBE_GCVM_PT_OFFSET_LOW32: return L"GcvmPtOffsetLow32";
    case BC250_PROBE_GCVM_PT_PHYSICAL_LOW32: return L"GcvmPtPhysicalLow32";
    case BC250_PROBE_GCVM_PT_PHYSICAL_HIGH32: return L"GcvmPtPhysicalHigh32";
    case BC250_PROBE_GCVM_TARGET_OFFSET_LOW32: return L"GcvmTargetOffsetLow32";
    case BC250_PROBE_GCVM_FIRST_PTE_LOW32: return L"GcvmFirstPteLow32";
    case BC250_PROBE_GCVM_FIRST_PTE_HIGH32: return L"GcvmFirstPteHigh32";
    case BC250_PROBE_GCVM_BACKING_OWNED: return L"GcvmBackingOwned";
    case BC250_PROBE_GCVM_BACKING_READBACK: return L"GcvmBackingReadback";
    case BC250_PROBE_GCVM_BACKING_RESTORE: return L"GcvmBackingRestore";
    case BC250_PROBE_GCVM_VMID8_STATUS: return L"GcvmVmid8Status";
    case BC250_PROBE_GCVM_VMID8_CNTL_BEFORE: return L"GcvmVmid8CntlBefore";
    case BC250_PROBE_GCVM_VMID8_READBACK: return L"GcvmVmid8Readback";
    case BC250_PROBE_GCVM_VMID8_TLB_PASSED: return L"GcvmVmid8TlbPassed";
    case BC250_PROBE_GCVM_VMID8_RESTORE: return L"GcvmVmid8Restore";
    case BC250_PROBE_GCVM_INERT_PHYSICAL_READY: return L"GcvmInertPhysicalReady";
    case BC250_PROBE_PSP_RING_STATUS: return L"PspRingStatus";
    case BC250_PROBE_PSP_RING_PHYSICAL_LOW32: return L"PspRingPhysicalLow32";
    case BC250_PROBE_PSP_RING_C2PMSG64_BEFORE: return L"PspRingC2pmsg64Before";
    case BC250_PROBE_PSP_RING_CREATE_ACK: return L"PspRingCreateAck";
    case BC250_PROBE_PSP_RING_DESTROY_ACK: return L"PspRingDestroyAck";
    case BC250_PROBE_PSP_RING_CREATE_POLLS: return L"PspRingCreatePolls";
    case BC250_PROBE_PSP_RING_DESTROY_POLLS: return L"PspRingDestroyPolls";
    case BC250_PROBE_PSP_RING_CREATE_PASSED: return L"PspRingCreatePassed";
    case BC250_PROBE_PSP_RING_DESTROY_PASSED: return L"PspRingDestroyPassed";
    case BC250_PROBE_PSP_RING_RESTORE_PASSED: return L"PspRingRestorePassed";
    case BC250_PROBE_PSP_RING_PHYSICAL_READY: return L"PspRingPhysicalReady";
    case BC250_PROBE_PSP_LOAD_FW_STATUS: return L"PspLoadFwStatus";
    case BC250_PROBE_PSP_LOAD_FW_BYTES: return L"PspLoadFwBytes";
    case BC250_PROBE_PSP_LOAD_FW_TYPE: return L"PspLoadFwType";
    case BC250_PROBE_PSP_LOAD_FW_RING_LOW: return L"PspLoadFwRingLow";
    case BC250_PROBE_PSP_LOAD_FW_RING_HIGH: return L"PspLoadFwRingHigh";
    case BC250_PROBE_PSP_LOAD_FW_COMMAND_LOW: return L"PspLoadFwCommandLow";
    case BC250_PROBE_PSP_LOAD_FW_COMMAND_HIGH: return L"PspLoadFwCommandHigh";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_LOW: return L"PspLoadFwFenceLow";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_HIGH: return L"PspLoadFwFenceHigh";
    case BC250_PROBE_PSP_LOAD_FW_PAYLOAD_LOW: return L"PspLoadFwPayloadLow";
    case BC250_PROBE_PSP_LOAD_FW_PAYLOAD_HIGH: return L"PspLoadFwPayloadHigh";
    case BC250_PROBE_PSP_LOAD_FW_CREATE_ACK: return L"PspLoadFwCreateAck";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_EXPECTED: return L"PspLoadFwFenceExpected";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_OBSERVED: return L"PspLoadFwFenceObserved";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_POLLS: return L"PspLoadFwFencePolls";
    case BC250_PROBE_PSP_LOAD_FW_RESPONSE_STATUS: return L"PspLoadFwResponseStatus";
    case BC250_PROBE_PSP_LOAD_FW_CHECKSUM_BEFORE: return L"PspLoadFwChecksumBefore";
    case BC250_PROBE_PSP_LOAD_FW_CHECKSUM_AFTER: return L"PspLoadFwChecksumAfter";
    case BC250_PROBE_PSP_LOAD_FW_DESTROY_ACK: return L"PspLoadFwDestroyAck";
    case BC250_PROBE_PSP_LOAD_FW_FENCE_PASSED: return L"PspLoadFwFencePassed";
    case BC250_PROBE_PSP_LOAD_FW_RESPONSE_PASSED: return L"PspLoadFwResponsePassed";
    case BC250_PROBE_PSP_LOAD_FW_CHECKSUM_PASSED: return L"PspLoadFwChecksumPassed";
    case BC250_PROBE_PSP_LOAD_FW_DESTROY_PASSED: return L"PspLoadFwDestroyPassed";
    case BC250_PROBE_PSP_LOAD_FW_RESTORE_PASSED: return L"PspLoadFwRestorePassed";
    case BC250_PROBE_PSP_LOAD_FW_PHYSICAL_READY: return L"PspLoadFwPhysicalReady";
    case BC250_PROBE_PSP_RLC_LOAD_STATUS: return L"PspRlcLoadStatus";
    case BC250_PROBE_PSP_RLC_LOAD_BYTES: return L"PspRlcLoadBytes";
    case BC250_PROBE_PSP_RLC_LOAD_TYPE: return L"PspRlcLoadType";
    case BC250_PROBE_PSP_RLC_LOAD_FENCE_EXPECTED:
        return L"PspRlcLoadFenceExpected";
    case BC250_PROBE_PSP_RLC_LOAD_FENCE_OBSERVED:
        return L"PspRlcLoadFenceObserved";
    case BC250_PROBE_PSP_RLC_LOAD_FENCE_POLLS:
        return L"PspRlcLoadFencePolls";
    case BC250_PROBE_PSP_RLC_LOAD_RESPONSE_STATUS:
        return L"PspRlcLoadResponseStatus";
    case BC250_PROBE_PSP_RLC_LOAD_DESTROY_PASSED:
        return L"PspRlcLoadDestroyPassed";
    case BC250_PROBE_PSP_RLC_LOAD_RESTORE_PASSED:
        return L"PspRlcLoadRestorePassed";
    case BC250_PROBE_PSP_RLC_LOAD_PHYSICAL_READY:
        return L"PspRlcLoadPhysicalReady";
    case BC250_PROBE_RLC_CNTL_BEFORE: return L"RlcCntlBefore";
    case BC250_PROBE_RLC_GPM_BEFORE: return L"RlcGpmBefore";
    case BC250_PROBE_RLC_CNTL_AFTER: return L"RlcCntlAfter";
    case BC250_PROBE_RLC_GPM_AFTER: return L"RlcGpmAfter";
    case BC250_PROBE_RLC_EXEC_ROM_BEFORE: return L"RlcExecRomBefore";
    case BC250_PROBE_RLC_EXEC_ROM_AFTER: return L"RlcExecRomAfter";
    case BC250_PROBE_RLC_ENABLE_F32_AFTER: return L"RlcEnableF32After";
    case BC250_PROBE_RLC_CNTL_RESTORE: return L"RlcCntlRestore";
    case BC250_PROBE_RLC_ALREADY_ACTIVE: return L"RlcAlreadyActive";
    case BC250_PROBE_RLC_PHYSICAL_READY: return L"RlcPhysicalReady";
    case BC250_PROBE_PFP_DIRECT_STATUS: return L"PfpDirectStatus";
    case BC250_PROBE_PFP_DIRECT_MC_LOW: return L"PfpDirectMcLow";
    case BC250_PROBE_PFP_DIRECT_MC_HIGH: return L"PfpDirectMcHigh";
    case BC250_PROBE_PFP_DIRECT_BYTES: return L"PfpDirectBytes";
    case BC250_PROBE_PFP_DIRECT_JT_DWORDS: return L"PfpDirectJtDwords";
    case BC250_PROBE_PFP_DIRECT_VERSION: return L"PfpDirectVersion";
    case BC250_PROBE_PFP_DIRECT_STAGED_DWORDS:
        return L"PfpDirectStagedDwords";
    case BC250_PROBE_PFP_DIRECT_STAGING_MISMATCHES:
        return L"PfpDirectStagingMismatches";
    case BC250_PROBE_PFP_DIRECT_ME_CNTL_BEFORE:
        return L"PfpDirectMeCntlBefore";
    case BC250_PROBE_PFP_DIRECT_ME_CNTL_APPLIED:
        return L"PfpDirectMeCntlApplied";
    case BC250_PROBE_PFP_DIRECT_ME_CNTL_AFTER:
        return L"PfpDirectMeCntlAfter";
    case BC250_PROBE_PFP_DIRECT_RLC_CNTL_BEFORE:
        return L"PfpDirectRlcCntlBefore";
    case BC250_PROBE_PFP_DIRECT_RLC_CNTL_APPLIED:
        return L"PfpDirectRlcCntlApplied";
    case BC250_PROBE_PFP_DIRECT_PC_BEFORE: return L"PfpDirectPcBefore";
    case BC250_PROBE_PFP_DIRECT_PC_FIRST: return L"PfpDirectPcFirst";
    case BC250_PROBE_PFP_DIRECT_PC_LAST: return L"PfpDirectPcLast";
    case BC250_PROBE_PFP_DIRECT_PC_TRANSITIONS:
        return L"PfpDirectPcTransitions";
    case BC250_PROBE_PFP_DIRECT_PC_SAMPLES: return L"PfpDirectPcSamples";
    case BC250_PROBE_PFP_DIRECT_ME_PC_BEFORE: return L"PfpDirectMePcBefore";
    case BC250_PROBE_PFP_DIRECT_ME_PC_LAST: return L"PfpDirectMePcLast";
    case BC250_PROBE_PFP_DIRECT_CE_PC_BEFORE: return L"PfpDirectCePcBefore";
    case BC250_PROBE_PFP_DIRECT_CE_PC_LAST: return L"PfpDirectCePcLast";
    case BC250_PROBE_PFP_DIRECT_FAULT_BEFORE:
        return L"PfpDirectFaultBefore";
    case BC250_PROBE_PFP_DIRECT_FAULT_AFTER:
        return L"PfpDirectFaultAfter";
    case BC250_PROBE_PFP_DIRECT_FAULT_ADDR_LOW:
        return L"PfpDirectFaultAddrLow";
    case BC250_PROBE_PFP_DIRECT_FAULT_ADDR_HIGH:
        return L"PfpDirectFaultAddrHigh";
    case BC250_PROBE_PFP_DIRECT_PORTS_PROGRAMMED:
        return L"PfpDirectPortsProgrammed";
    case BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_BEFORE:
        return L"PfpDirectRb0ActiveBefore";
    case BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_APPLIED:
        return L"PfpDirectRb0ActiveApplied";
    case BC250_PROBE_PFP_DIRECT_RB0_ACTIVE_AFTER:
        return L"PfpDirectRb0ActiveAfter";
    case BC250_PROBE_PFP_DIRECT_RB0_RPTR_BEFORE:
        return L"PfpDirectRb0RptrBefore";
    case BC250_PROBE_PFP_DIRECT_RB0_RPTR_AFTER:
        return L"PfpDirectRb0RptrAfter";
    case BC250_PROBE_PFP_DIRECT_RB0_WPTR_APPLIED:
        return L"PfpDirectRb0WptrApplied";
    case BC250_PROBE_PFP_DIRECT_RB0_FENCE_BEFORE:
        return L"PfpDirectRb0FenceBefore";
    case BC250_PROBE_PFP_DIRECT_RB0_FENCE_AFTER:
        return L"PfpDirectRb0FenceAfter";
    case BC250_PROBE_PFP_DIRECT_REG_RESTORE:
        return L"PfpDirectRegRestore";
    case BC250_PROBE_PFP_DIRECT_MEM_RESTORE:
        return L"PfpDirectMemRestore";
    case BC250_PROBE_PFP_DIRECT_PHYSICAL_READY:
        return L"PfpDirectPhysicalReady";
    case BC250_PROBE_SDMA_F32_STATUS: return L"SdmaF32Status";
    case BC250_PROBE_SDMA_F32_CLOCK_BEFORE: return L"SdmaF32ClockBefore";
    case BC250_PROBE_SDMA_F32_CONTROL_BEFORE: return L"SdmaF32ControlBefore";
    case BC250_PROBE_SDMA_F32_CONTROL_APPLIED: return L"SdmaF32ControlApplied";
    case BC250_PROBE_SDMA_F32_F32_BEFORE: return L"SdmaF32F32Before";
    case BC250_PROBE_SDMA_F32_F32_APPLIED: return L"SdmaF32F32Applied";
    case BC250_PROBE_SDMA_F32_PC_FIRST: return L"SdmaF32PcFirst";
    case BC250_PROBE_SDMA_F32_PC_LAST: return L"SdmaF32PcLast";
    case BC250_PROBE_SDMA_F32_PC_TRANSITIONS: return L"SdmaF32PcTransitions";
    case BC250_PROBE_SDMA_F32_APPLY_READBACK: return L"SdmaF32ApplyReadback";
    case BC250_PROBE_SDMA_F32_RESTORE: return L"SdmaF32Restore";
    case BC250_PROBE_SDMA_F32_PHYSICAL_READY: return L"SdmaF32PhysicalReady";
    case BC250_PROBE_SDMA_RING_WORK_STATUS: return L"SdmaRingWorkStatus";
    case BC250_PROBE_SDMA_RING_FIXED_RESERVATION_STATUS:
        return L"SdmaRingFixedReservationStatus";
    case BC250_PROBE_SDMA_RING_ALLOCATION_MODE:
        return L"SdmaRingAllocationMode";
    case BC250_PROBE_SDMA_RING_RING_LOW: return L"SdmaRingRingLow";
    case BC250_PROBE_SDMA_RING_RING_HIGH: return L"SdmaRingRingHigh";
    case BC250_PROBE_SDMA_RING_WORK_LOW: return L"SdmaRingWorkLow";
    case BC250_PROBE_SDMA_RING_WORK_HIGH: return L"SdmaRingWorkHigh";
    case BC250_PROBE_SDMA_RING_RPTR_BEFORE: return L"SdmaRingRptrBefore";
    case BC250_PROBE_SDMA_RING_RPTR_AFTER: return L"SdmaRingRptrAfter";
    case BC250_PROBE_SDMA_RING_WPTR: return L"SdmaRingWptr";
    case BC250_PROBE_SDMA_RING_WPTR_REGISTER: return L"SdmaRingWptrRegister";
    case BC250_PROBE_SDMA_RING_WPTR_SHADOW: return L"SdmaRingWptrShadow";
    case BC250_PROBE_SDMA_RING_RB_CNTL: return L"SdmaRingRbCntl";
    case BC250_PROBE_SDMA_RING_HEADER: return L"SdmaRingHeader";
    case BC250_PROBE_SDMA_RING_RPTR_REGISTER: return L"SdmaRingRptrRegister";
    case BC250_PROBE_SDMA_RING_POLL_CNTL: return L"SdmaRingPollCntl";
    case BC250_PROBE_SDMA_RING_FENCE_POISON: return L"SdmaRingFencePoison";
    case BC250_PROBE_SDMA_RING_FENCE_EXPECTED: return L"SdmaRingFenceExpected";
    case BC250_PROBE_SDMA_RING_FENCE_OBSERVED: return L"SdmaRingFenceObserved";
    case BC250_PROBE_SDMA_RING_POLLS: return L"SdmaRingPolls";
    case BC250_PROBE_SDMA_RING_COMPARED_DWORDS: return L"SdmaRingComparedDwords";
    case BC250_PROBE_SDMA_RING_MISMATCHES: return L"SdmaRingMismatches";
    case BC250_PROBE_SDMA_RING_STATUS_BEFORE: return L"SdmaRingStatusBefore";
    case BC250_PROBE_SDMA_RING_STATUS_KICK: return L"SdmaRingStatusKick";
    case BC250_PROBE_SDMA_RING_STATUS_AFTER: return L"SdmaRingStatusAfter";
    case BC250_PROBE_SDMA_RING_PC_KICK: return L"SdmaRingPcKick";
    case BC250_PROBE_SDMA_RING_STATUS1_AFTER: return L"SdmaRingStatus1After";
    case BC250_PROBE_SDMA_RING_STATUS2_AFTER: return L"SdmaRingStatus2After";
    case BC250_PROBE_SDMA_RING_STATUS3_AFTER: return L"SdmaRingStatus3After";
    case BC250_PROBE_SDMA_RING_ERROR_LOG_AFTER:
        return L"SdmaRingErrorLogAfter";
    case BC250_PROBE_SDMA_RING_UTCL1_RD_STATUS_AFTER:
        return L"SdmaRingUtcl1ReadStatusAfter";
    case BC250_PROBE_SDMA_RING_UTCL1_WR_STATUS_AFTER:
        return L"SdmaRingUtcl1WriteStatusAfter";
    case BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK0_AFTER:
        return L"SdmaRingUtcl1ReadXnack0After";
    case BC250_PROBE_SDMA_RING_UTCL1_RD_XNACK1_AFTER:
        return L"SdmaRingUtcl1ReadXnack1After";
    case BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK0_AFTER:
        return L"SdmaRingUtcl1WriteXnack0After";
    case BC250_PROBE_SDMA_RING_UTCL1_WR_XNACK1_AFTER:
        return L"SdmaRingUtcl1WriteXnack1After";
    case BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_AFTER:
        return L"SdmaRingUtcl1TimeoutAfter";
    case BC250_PROBE_SDMA_RING_REG_RESTORE: return L"SdmaRingRegRestore";
    case BC250_PROBE_SDMA_RING_MEM_RESTORE: return L"SdmaRingMemRestore";
    case BC250_PROBE_SDMA_RING_PHYSICAL_READY: return L"SdmaRingPhysicalReady";
    case BC250_PROBE_HQD_PREFLIGHT_STATUS: return L"HqdPreflightStatus";
    case BC250_PROBE_HQD_PREFLIGHT_READ_COUNT: return L"HqdPreflightReadCount";
    case BC250_PROBE_HQD_STABLE_PASSES: return L"HqdStablePasses";
    case BC250_PROBE_HQD_SELECTOR: return L"HqdSelector";
    case BC250_PROBE_HQD_ACTIVE: return L"HqdActive";
    case BC250_PROBE_HQD_MQD_BASE_LO: return L"HqdMqdBaseLo";
    case BC250_PROBE_HQD_MQD_BASE_HI: return L"HqdMqdBaseHi";
    case BC250_PROBE_HQD_VMID: return L"HqdVmid";
    case BC250_PROBE_HQD_PQ_BASE_LO: return L"HqdPqBaseLo";
    case BC250_PROBE_HQD_PQ_BASE_HI: return L"HqdPqBaseHi";
    case BC250_PROBE_HQD_PQ_RPTR: return L"HqdPqRptr";
    case BC250_PROBE_HQD_DOORBELL_CONTROL: return L"HqdDoorbellControl";
    case BC250_PROBE_HQD_PQ_CONTROL: return L"HqdPqControl";
    case BC250_PROBE_HQD_PQ_WPTR_LO: return L"HqdPqWptrLo";
    case BC250_PROBE_HQD_PQ_WPTR_HI: return L"HqdPqWptrHi";
    case BC250_PROBE_HQD_INACTIVE: return L"HqdInactive";
    case BC250_PROBE_MEC_HQD_SELECTOR_STATUS: return L"MecHqdSelectorStatus";
    case BC250_PROBE_MEC_HQD_ORIGINAL_SELECTOR: return L"MecHqdOriginalSelector";
    case BC250_PROBE_MEC_HQD_APPLIED_SELECTOR: return L"MecHqdAppliedSelector";
    case BC250_PROBE_MEC_HQD_APPLIED_READBACK_FIRST:
        return L"MecHqdAppliedReadbackFirst";
    case BC250_PROBE_MEC_HQD_APPLIED_READBACK_LAST:
        return L"MecHqdAppliedReadbackLast";
    case BC250_PROBE_MEC_HQD_RESTORED_SELECTOR: return L"MecHqdRestoredSelector";
    case BC250_PROBE_MEC_HQD_READ_CALLS: return L"MecHqdReadCalls";
    case BC250_PROBE_MEC_HQD_WRITE_CALLS: return L"MecHqdWriteCalls";
    case BC250_PROBE_MEC_HQD_APPLY_READBACK: return L"MecHqdApplyReadback";
    case BC250_PROBE_MEC_HQD_RESTORE: return L"MecHqdRestore";
    case BC250_PROBE_MEC_HQD_PHYSICAL_READY: return L"MecHqdPhysicalReady";
    case BC250_PROBE_MEC_HQD_STATUS: return L"MecHqdStatus";
    case BC250_PROBE_MEC_HQD_STABLE_PASSES: return L"MecHqdStablePasses";
    case BC250_PROBE_MEC_HQD_ACTIVE: return L"MecHqdActive";
    case BC250_PROBE_MEC_HQD_MQD_BASE_LO: return L"MecHqdMqdBaseLo";
    case BC250_PROBE_MEC_HQD_MQD_BASE_HI: return L"MecHqdMqdBaseHi";
    case BC250_PROBE_MEC_HQD_PQ_BASE_LO: return L"MecHqdPqBaseLo";
    case BC250_PROBE_MEC_HQD_PQ_BASE_HI: return L"MecHqdPqBaseHi";
    case BC250_PROBE_MEC_HQD_PQ_RPTR: return L"MecHqdPqRptr";
    case BC250_PROBE_MEC_HQD_PQ_WPTR_LO: return L"MecHqdPqWptrLo";
    case BC250_PROBE_MEC_HQD_PQ_WPTR_HI: return L"MecHqdPqWptrHi";
    case BC250_PROBE_HQD_BANK_PROBE_STATUS: return L"HqdBankProbeStatus";
    case BC250_PROBE_HQD_BANK_ORIGINAL_SELECTOR: return L"HqdBankOriginalSelector";
    case BC250_PROBE_HQD_BANK0_ACTIVE: return L"HqdBank0Active";
    case BC250_PROBE_HQD_BANK0_PRIORITY: return L"HqdBank0Priority";
    case BC250_PROBE_HQD_BANK_CANDIDATE_ACTIVE: return L"HqdBankCandidateActive";
    case BC250_PROBE_HQD_BANK_CANDIDATE_PRIORITY: return L"HqdBankCandidatePriority";
    case BC250_PROBE_HQD_BANK_POISON_PRIORITY: return L"HqdBankPoisonPriority";
    case BC250_PROBE_HQD_BANK_POISON_READBACK: return L"HqdBankPoisonReadback";
    case BC250_PROBE_HQD_BANK0_CROSS_READBACK: return L"HqdBank0CrossReadback";
    case BC250_PROBE_HQD_BANK_CANDIDATE_CROSS_READBACK:
        return L"HqdBankCandidateCrossReadback";
    case BC250_PROBE_HQD_BANK_CANDIDATE_RESTORE_READBACK:
        return L"HqdBankCandidateRestoreReadback";
    case BC250_PROBE_HQD_BANK0_FINAL_READBACK: return L"HqdBank0FinalReadback";
    case BC250_PROBE_HQD_BANK_FINAL_SELECTOR: return L"HqdBankFinalSelector";
    case BC250_PROBE_HQD_BANK_READ_CALLS: return L"HqdBankReadCalls";
    case BC250_PROBE_HQD_BANK_WRITE_CALLS: return L"HqdBankWriteCalls";
    case BC250_PROBE_HQD_BANK_POISON_PASSED: return L"HqdBankPoisonPassed";
    case BC250_PROBE_HQD_BANK_ISOLATION_PASSED: return L"HqdBankIsolationPassed";
    case BC250_PROBE_HQD_BANK_RESTORE_PASSED: return L"HqdBankRestorePassed";
    case BC250_PROBE_HQD_BANK_PHYSICAL_READY: return L"HqdBankPhysicalReady";
    case BC250_PROBE_HQD_BANK_HQD_CAPTURE_PASSED: return L"HqdBankHqdCapturePassed";
    case BC250_PROBE_HQD_BANK_HQD_STABLE_PASSES: return L"HqdBankHqdStablePasses";
    case BC250_PROBE_HQD_BANK_HQD_INACTIVE: return L"HqdBankHqdInactive";
    case BC250_PROBE_HQD_BANK_HQD_SELECTOR_OBSERVED: return L"HqdBankHqdSelectorObserved";
    case BC250_PROBE_HQD_BANK_HQD_ACTIVE: return L"HqdBankHqdActive";
    case BC250_PROBE_HQD_BANK_HQD_MQD_BASE_LO: return L"HqdBankHqdMqdBaseLo";
    case BC250_PROBE_HQD_BANK_HQD_MQD_BASE_HI: return L"HqdBankHqdMqdBaseHi";
    case BC250_PROBE_HQD_BANK_HQD_VMID: return L"HqdBankHqdVmid";
    case BC250_PROBE_HQD_BANK_HQD_PQ_BASE_LO: return L"HqdBankHqdPqBaseLo";
    case BC250_PROBE_HQD_BANK_HQD_PQ_BASE_HI: return L"HqdBankHqdPqBaseHi";
    case BC250_PROBE_HQD_BANK_HQD_PQ_RPTR: return L"HqdBankHqdPqRptr";
    case BC250_PROBE_HQD_BANK_HQD_DOORBELL_CONTROL: return L"HqdBankHqdDoorbellControl";
    case BC250_PROBE_HQD_BANK_HQD_PQ_CONTROL: return L"HqdBankHqdPqControl";
    case BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_LO: return L"HqdBankHqdPqWptrLo";
    case BC250_PROBE_HQD_BANK_HQD_PQ_WPTR_HI: return L"HqdBankHqdPqWptrHi";
    case BC250_PROBE_HQD_PROGRAM_STATUS: return L"HqdProgramStatus";
    case BC250_PROBE_HQD_PROGRAM_ORIGINAL_SELECTOR: return L"HqdProgramOriginalSelector";
    case BC250_PROBE_HQD_PROGRAM_SELECTED_SELECTOR: return L"HqdProgramSelectedSelector";
    case BC250_PROBE_HQD_PROGRAM_RESTORED_SELECTOR: return L"HqdProgramRestoredSelector";
    case BC250_PROBE_HQD_PROGRAM_READ_CALLS: return L"HqdProgramReadCalls";
    case BC250_PROBE_HQD_PROGRAM_WRITE_CALLS: return L"HqdProgramWriteCalls";
    case BC250_PROBE_HQD_PROGRAM_MEMORY_PASSED: return L"HqdProgramMemoryPassed";
    case BC250_PROBE_HQD_PROGRAM_READBACK_PASSED: return L"HqdProgramReadbackPassed";
    case BC250_PROBE_HQD_PROGRAM_ACTIVE_ZERO: return L"HqdProgramActiveZero";
    case BC250_PROBE_HQD_PROGRAM_RESTORE_PASSED: return L"HqdProgramRestorePassed";
    case BC250_PROBE_HQD_PROGRAM_MEM_RESTORE_PASSED: return L"HqdProgramMemRestorePassed";
    case BC250_PROBE_HQD_PROGRAM_PHYSICAL_READY: return L"HqdProgramPhysicalReady";
    case BC250_PROBE_HQD_PROGRAM_MQD_BASE_LO: return L"HqdProgramMqdBaseLo";
    case BC250_PROBE_HQD_PROGRAM_MQD_BASE_HI: return L"HqdProgramMqdBaseHi";
    case BC250_PROBE_HQD_PROGRAM_PQ_BASE_LO: return L"HqdProgramPqBaseLo";
    case BC250_PROBE_HQD_PROGRAM_PQ_BASE_HI: return L"HqdProgramPqBaseHi";
    case BC250_PROBE_HQD_PROGRAM_PQ_CONTROL: return L"HqdProgramPqControl";
    case BC250_PROBE_HQD_PROGRAM_ACTIVE_READBACK: return L"HqdProgramActiveReadback";
    case BC250_PROBE_HQD_PROGRAM_MISMATCH_MASK: return L"HqdProgramMismatchMask";
    case BC250_PROBE_HQD_PROGRAM_MISMATCH_OFFSET: return L"HqdProgramMismatchOffset";
    case BC250_PROBE_HQD_PROGRAM_MISMATCH_WRITTEN: return L"HqdProgramMismatchWritten";
    case BC250_PROBE_HQD_PROGRAM_MISMATCH_READ1: return L"HqdProgramMismatchRead1";
    case BC250_PROBE_HQD_PROGRAM_MISMATCH_READ2: return L"HqdProgramMismatchRead2";
    case BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_LO: return L"HqdProgramReadbackMqdBaseLo";
    case BC250_PROBE_HQD_PROGRAM_READBACK_MQD_BASE_HI: return L"HqdProgramReadbackMqdBaseHi";
    case BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_LO: return L"HqdProgramReadbackPqBaseLo";
    case BC250_PROBE_HQD_PROGRAM_READBACK_PQ_BASE_HI: return L"HqdProgramReadbackPqBaseHi";
    case BC250_PROBE_HQD_PROGRAM_READBACK_PQ_CONTROL: return L"HqdProgramReadbackPqControl";
    case BC250_PROBE_HQD_ACTIVE_STATUS: return L"HqdActiveStatus";
    case BC250_PROBE_HQD_ACTIVE_SELECTED_SELECTOR: return L"HqdActiveSelectedSelector";
    case BC250_PROBE_HQD_ACTIVE_BEFORE: return L"HqdActiveBefore";
    case BC250_PROBE_HQD_ACTIVE_APPLIED: return L"HqdActiveApplied";
    case BC250_PROBE_HQD_ACTIVE_AFTER: return L"HqdActiveAfter";
    case BC250_PROBE_HQD_ACTIVE_RPTR_BEFORE: return L"HqdActiveRptrBefore";
    case BC250_PROBE_HQD_ACTIVE_RPTR_AFTER: return L"HqdActiveRptrAfter";
    case BC250_PROBE_HQD_ACTIVE_WPTR_APPLIED: return L"HqdActiveWptrApplied";
    case BC250_PROBE_HQD_ACTIVE_FENCE_BEFORE: return L"HqdActiveFenceBefore";
    case BC250_PROBE_HQD_ACTIVE_FENCE_AFTER: return L"HqdActiveFenceAfter";
    case BC250_PROBE_HQD_ACTIVE_SCRATCH_BEFORE: return L"HqdActiveScratchBefore";
    case BC250_PROBE_HQD_ACTIVE_SCRATCH_AFTER: return L"HqdActiveScratchAfter";
    case BC250_PROBE_HQD_ACTIVE_POLL_ITERATIONS: return L"HqdActivePollIterations";
    case BC250_PROBE_HQD_ACTIVE_TRIAD_PASSED: return L"HqdActiveTriadPassed";
    case BC250_PROBE_HQD_ACTIVE_RESTORE_PASSED: return L"HqdActiveRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_MEM_RESTORE_PASSED: return L"HqdActiveMemRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_PHYSICAL_READY: return L"HqdActivePhysicalReady";
    case BC250_PROBE_HQD_ACTIVE_DOORBELL_RUNG: return L"HqdActiveDoorbellRung";
    case BC250_PROBE_HQD_ACTIVE_DOORBELL_INDEX: return L"HqdActiveDoorbellIndex";
    case BC250_PROBE_HQD_ACTIVE_DOORBELL_OFFSET: return L"HqdActiveDoorbellOffset";
    case BC250_PROBE_HQD_ACTIVE_DOORBELL_VALUE_APPLIED: return L"HqdActiveDoorbellValueApplied";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_BEFORE: return L"HqdActiveDbRangeLowerBefore";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_APPLIED: return L"HqdActiveDbRangeLowerApplied";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_LOWER_READBACK: return L"HqdActiveDbRangeLowerReadback";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_BEFORE: return L"HqdActiveDbRangeUpperBefore";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_APPLIED: return L"HqdActiveDbRangeUpperApplied";
    case BC250_PROBE_HQD_ACTIVE_DB_RANGE_UPPER_READBACK: return L"HqdActiveDbRangeUpperReadback";
    case BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_BEFORE: return L"HqdActiveCpPqStatusBefore";
    case BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_APPLIED: return L"HqdActiveCpPqStatusApplied";
    case BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_READBACK: return L"HqdActiveCpPqStatusReadback";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_BEFORE: return L"HqdActiveWptrPollCntlBefore";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_APPLIED: return L"HqdActiveWptrPollCntlApplied";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_READBACK: return L"HqdActiveWptrPollCntlReadback";
    case BC250_PROBE_HQD_ACTIVE_SELECTOR_RELEASED_BEFORE_KICK: return L"HqdActiveSelectorReleasedBeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CP_PQ_STATUS_AFTER_KICK: return L"HqdActiveCpPqStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_DOORBELL_CONTROL_AFTER_KICK: return L"HqdActiveDoorbellControlAfterKick";
    case BC250_PROBE_HQD_ACTIVE_WPTR_AFTER_KICK: return L"HqdActiveWptrAfterKick";
    case BC250_PROBE_HQD_ACTIVE_RPTR_AFTER_KICK: return L"HqdActiveRptrAfterKick";
    case BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_LO: return L"HqdActiveInitialWptrLo";
    case BC250_PROBE_HQD_ACTIVE_INITIAL_WPTR_HI: return L"HqdActiveInitialWptrHi";
    case BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_APPLIED: return L"HqdActiveBaselineWptrApplied";
    case BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_READBACK: return L"HqdActiveBaselineWptrReadback";
    case BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORED: return L"HqdActiveBaselineWptrRestored";
    case BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_POLLS: return L"HqdActiveBaselineWptrRestorePolls";
    case BC250_PROBE_HQD_ACTIVE_BASELINE_WPTR_RESTORE_PASSED: return L"HqdActiveBaselineWptrRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_KICK_APPLIED: return L"HqdActiveDirectWptrKickApplied";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_BEFORE: return L"HqdActiveDirectWptrBefore";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_READBACK: return L"HqdActiveDirectWptrReadback";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_RPTR_AFTER_KICK: return L"HqdActiveDirectRptrAfterKick";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORED: return L"HqdActiveDirectWptrRestored";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_POLLS: return L"HqdActiveDirectWptrRestorePolls";
    case BC250_PROBE_HQD_ACTIVE_DIRECT_WPTR_RESTORE_PASSED: return L"HqdActiveDirectWptrRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_RPTR_MMIO_LAST: return L"HqdActiveRptrMmioLast";
    case BC250_PROBE_HQD_ACTIVE_WPTR_MMIO_LAST: return L"HqdActiveWptrMmioLast";
    case BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_POLL_LAST: return L"HqdActiveHqStatus0PollLast";
    case BC250_PROBE_HQD_ACTIVE_HQD_ERROR_POLL_LAST: return L"HqdActiveHqdErrorPollLast";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_BEFORE: return L"HqdActiveRlcCpSchedulersBefore";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_APPLIED: return L"HqdActiveRlcCpSchedulersApplied";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_READBACK: return L"HqdActiveRlcCpSchedulersReadback";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_AFTER_KICK: return L"HqdActiveRlcCpSchedulersAfterKick";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORED: return L"HqdActiveRlcCpSchedulersRestored";
    case BC250_PROBE_HQD_ACTIVE_RLC_CP_SCHEDULERS_RESTORE_PASSED: return L"HqdActiveRlcCpSchedulersRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_AFTER_KICK: return L"HqdActiveHqStatus0AfterKick";
    case BC250_PROBE_HQD_ACTIVE_HQ_CONTROL0_AFTER_KICK: return L"HqdActiveHqControl0AfterKick";
    case BC250_PROBE_HQD_ACTIVE_HQD_ERROR_AFTER_KICK: return L"HqdActiveHqdErrorAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_ERROR_AFTER_KICK: return L"HqdActiveCpcUtcl1ErrorAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_UTCL1_STATUS_AFTER_KICK: return L"HqdActiveCpcUtcl1StatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_UTCL1_STATUS_AFTER_KICK: return L"HqdActiveCpfUtcl1StatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_INT_STATUS_AFTER_KICK: return L"HqdActiveCpcIntStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_ME1_PIPE0_INT_STATUS_AFTER_KICK: return L"HqdActiveMe1Pipe0IntStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_ME2_PIPE1_INT_STATUS_AFTER_KICK: return L"HqdActiveMe2Pipe1IntStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_ME2_PIPE0_INT_STATUS_AFTER_KICK: return L"HqdActiveMe2Pipe0IntStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_PQ_BASE_LO: return L"HqdActivePqBaseLo";
    case BC250_PROBE_HQD_ACTIVE_PQ_BASE_HI: return L"HqdActivePqBaseHi";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_LO: return L"HqdActiveWptrPollAddrLo";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_ADDR_HI: return L"HqdActiveWptrPollAddrHi";
    case BC250_PROBE_HQD_ACTIVE_WPTR_SHADOW: return L"HqdActiveWptrShadow";
    case BC250_PROBE_HQD_ACTIVE_CP_MEC_CNTL_READBACK: return L"HqdActiveCpMecCntlReadback";
    case BC250_PROBE_HQD_ACTIVE_MEC_PC_FIRST: return L"HqdActiveMecPcFirst";
    case BC250_PROBE_HQD_ACTIVE_MEC_PC_LAST: return L"HqdActiveMecPcLast";
    case BC250_PROBE_HQD_ACTIVE_MEC_PC_TRANSITIONS: return L"HqdActiveMecPcTransitions";
    case BC250_PROBE_HQD_ACTIVE_MEC_PC_SAMPLES: return L"HqdActiveMecPcSamples";
    case BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_OBSERVED: return L"HqdActivePqControlObserved";
    case BC250_PROBE_HQD_ACTIVE_HDP_INVALIDATE_APPLIED: return L"HqdActiveHdpInvalidateApplied";
    case BC250_PROBE_HQD_ACTIVE_GCVM_L2_BEFORE: return L"HqdActiveGcvmL2Before";
    case BC250_PROBE_HQD_ACTIVE_GCVM_L2_APPLIED: return L"HqdActiveGcvmL2Applied";
    case BC250_PROBE_HQD_ACTIVE_GCVM_L2_READBACK: return L"HqdActiveGcvmL2Readback";
    case BC250_PROBE_HQD_ACTIVE_GCVM_REQ_APPLIED: return L"HqdActiveGcvmReqApplied";
    case BC250_PROBE_HQD_ACTIVE_GCVM_REQ_READBACK: return L"HqdActiveGcvmReqReadback";
    case BC250_PROBE_HQD_ACTIVE_GCVM_ACK_BEFORE: return L"HqdActiveGcvmAckBefore";
    case BC250_PROBE_HQD_ACTIVE_GCVM_ACK_AFTER: return L"HqdActiveGcvmAckAfter";
    case BC250_PROBE_HQD_ACTIVE_GCVM_POLLS: return L"HqdActiveGcvmPolls";
    case BC250_PROBE_HQD_ACTIVE_COHERENCY_PASSED: return L"HqdActiveCoherencyPassed";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_APPLICABLE: return L"HqdActiveGfxOffApplicable";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_BEFORE: return L"HqdActiveGfxOffStateBefore";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_GUARDED: return L"HqdActiveGfxOffStateGuarded";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_STATE_AFTER: return L"HqdActiveGfxOffStateAfter";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_DISALLOW_RESPONSE: return L"HqdActiveGfxOffDisallowResponse";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_ALLOW_RESPONSE: return L"HqdActiveGfxOffAllowResponse";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_GUARD_PASSED: return L"HqdActiveGfxOffGuardPassed";
    case BC250_PROBE_HQD_ACTIVE_GFXOFF_RESTORE_PASSED: return L"HqdActiveGfxOffRestorePassed";
    case BC250_PROBE_HQD_WPTR_DIAG_STATUS: return L"HqdWptrDiagStatus";
    case BC250_PROBE_HQD_WPTR_DIAG_ORIGINAL_SELECTOR: return L"HqdWptrDiagOriginalSelector";
    case BC250_PROBE_HQD_WPTR_DIAG_TARGET_SELECTOR: return L"HqdWptrDiagTargetSelector";
    case BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_BEFORE: return L"HqdWptrDiagActiveBefore";
    case BC250_PROBE_HQD_WPTR_DIAG_RPTR_BEFORE: return L"HqdWptrDiagRptrBefore";
    case BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_BEFORE: return L"HqdWptrDiagWptrLoBefore";
    case BC250_PROBE_HQD_WPTR_DIAG_WPTR_HI_BEFORE: return L"HqdWptrDiagWptrHiBefore";
    case BC250_PROBE_HQD_WPTR_DIAG_PQ_CONTROL_BEFORE: return L"HqdWptrDiagPqControlBefore";
    case BC250_PROBE_HQD_WPTR_DIAG_STABLE_PREFLIGHT_PASSED: return L"HqdWptrDiagStablePreflightPassed";
    case BC250_PROBE_HQD_WPTR_DIAG_TEST_VALUE_WRITTEN: return L"HqdWptrDiagTestValueWritten";
    case BC250_PROBE_HQD_WPTR_DIAG_FIRST_READBACK: return L"HqdWptrDiagFirstReadback";
    case BC250_PROBE_HQD_WPTR_DIAG_SERIES_MATCH_INDEX: return L"HqdWptrDiagSeriesMatchIndex";
    case BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_UNMODIFIED: return L"HqdWptrDiagSelectorUnmodified";
    case BC250_PROBE_HQD_WPTR_DIAG_RESTORED_VALUE_OBSERVED: return L"HqdWptrDiagRestoredValueObserved";
    case BC250_PROBE_HQD_WPTR_DIAG_RESTORE_POLLS: return L"HqdWptrDiagRestorePolls";
    case BC250_PROBE_HQD_WPTR_DIAG_RESTORE_PASSED: return L"HqdWptrDiagRestorePassed";
    case BC250_PROBE_HQD_WPTR_DIAG_ACTIVE_AFTER: return L"HqdWptrDiagActiveAfter";
    case BC250_PROBE_HQD_WPTR_DIAG_WPTR_LO_AFTER: return L"HqdWptrDiagWptrLoAfter";
    case BC250_PROBE_HQD_WPTR_DIAG_SELECTOR_RESTORE_PASSED: return L"HqdWptrDiagSelectorRestorePassed";
    case BC250_PROBE_HQD_WPTR_DIAG_READ_CALLS: return L"HqdWptrDiagReadCalls";
    case BC250_PROBE_HQD_WPTR_DIAG_WRITE_CALLS: return L"HqdWptrDiagWriteCalls";
    case BC250_PROBE_HQD_WPTR_DIAG_PHYSICAL_READY: return L"HqdWptrDiagPhysicalReady";
    case BC250_PROBE_DMM_QUERY_ADAPTER_TYPE: return L"DmmQueryAdapterType";
    case BC250_PROBE_DMM_CHILD_RELATIONS_CALLED: return L"DmmChildRelationsCalled";
    case BC250_PROBE_DMM_CHILD_STATUS_CALLED: return L"DmmChildStatusCalled";
    case BC250_PROBE_DMM_DESCRIPTOR_CALLED: return L"DmmDescriptorCalled";
    case BC250_PROBE_DMM_VIDPN_QUERY_CALLED: return L"DmmVidPnQueryCalled";
    case BC250_PROBE_DMM_QAI_OUTSIZE: return L"DmmQaiOutSize";
    case BC250_PROBE_DMM_QAI_STATUS: return L"DmmQaiStatus";
    case BC250_PROBE_DMM_DISPLAY_MAPPED: return L"DmmDisplayMapped";
    case BC250_PROBE_DMM_VSYNC_COUNT: return L"DmmVsyncCount";
    case BC250_PROBE_DMM_IS_SUPPORTED_COUNT:
        return L"DmmIsSupportedCount";
    case BC250_PROBE_DMM_IS_SUPPORTED_STATUS:
        return L"DmmIsSupportedStatus";
    case BC250_PROBE_DMM_IS_SUPPORTED_RESULT:
        return L"DmmIsSupportedResult";
    case BC250_PROBE_DMM_COMMIT_COUNT: return L"DmmCommitCount";
    case BC250_PROBE_DMM_COMMIT_STATUS: return L"DmmCommitStatus";
    case BC250_PROBE_DMM_COMMIT_HAS_PATH: return L"DmmCommitHasPath";
    case BC250_PROBE_DMM_COMMIT_SUPPORTED:
        return L"DmmCommitSupported";
    case BC250_PROBE_DMM_CONTROL_INTERRUPT_COUNT:
        return L"DmmControlInterruptCount";
    case BC250_PROBE_DMM_CONTROL_INTERRUPT_TYPE:
        return L"DmmControlInterruptType";
    case BC250_PROBE_DMM_CONTROL_INTERRUPT_ENABLE:
        return L"DmmControlInterruptEnable";
    case BC250_PROBE_DMM_PRESENT_ENTERED: return L"DmmPresentEntered";
    case BC250_PROBE_DMM_PRESENT_STATUS: return L"DmmPresentStatus";
    case BC250_PROBE_DMM_PRESENT_SOURCE_PITCH:
        return L"DmmPresentSourcePitch";
    case BC250_PROBE_SKIP_HW_BRINGUP: return L"SkipHwBringup";
    default: return NULL;
    }
}

static VOID Bc250ProbeWriteValue(HANDLE Key, PCWSTR Name, ULONG Value)
{
    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, Name);
    (void)ZwSetValueKey(
        Key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
}

#endif

VOID Bc250ProbeRecord(BC250_PROBE_FIELD Field, ULONG Value)
{
    ULONG sequence = (ULONG)InterlockedIncrement(&g_Bc250ProbeSequence);
    InterlockedExchange(&g_Bc250ProbeLastField, (LONG)Field);
    InterlockedExchange(&g_Bc250ProbeLastValue, (LONG)Value);

#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)
    {
        OBJECT_ATTRIBUTES attributes;
        HANDLE key = NULL;
        PCWSTR fieldName = Bc250ProbeFieldName(Field);
        NTSTATUS status;

        InitializeObjectAttributes(
            &attributes, (PUNICODE_STRING)&g_Bc250ProbeRegistryPathMcdm,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        status = ZwOpenKey(&key, KEY_SET_VALUE, &attributes);
        if (!NT_SUCCESS(status)) {
            InitializeObjectAttributes(
                &attributes, (PUNICODE_STRING)&g_Bc250ProbeRegistryPathReadonly,
                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
            status = ZwOpenKey(&key, KEY_SET_VALUE, &attributes);
        }
        if (fieldName != NULL && NT_SUCCESS(status)) {
            Bc250ProbeWriteValue(key, L"Sequence", sequence);
            Bc250ProbeWriteValue(key, L"LastField", (ULONG)Field);
            Bc250ProbeWriteValue(key, L"LastValue", Value);
            Bc250ProbeWriteValue(key, fieldName, Value);
            ZwClose(key);
        }
    }
#else
    UNREFERENCED_PARAMETER(sequence);
#endif
}

VOID Bc250ProbeResetOffline(VOID)
{
    InterlockedExchange(&g_Bc250ProbeSequence, 0);
    InterlockedExchange(&g_Bc250ProbeLastField, 0);
    InterlockedExchange(&g_Bc250ProbeLastValue, 0);
    InterlockedExchange(&g_Bc250ProbeFirstFailure, (LONG)STATUS_SUCCESS);
}

ULONG Bc250ProbeGetOffline(
    BC250_PROBE_FIELD *LastField,
    ULONG *LastValue)
{
    if (LastField != NULL) {
        *LastField = (BC250_PROBE_FIELD)g_Bc250ProbeLastField;
    }
    if (LastValue != NULL) *LastValue = (ULONG)g_Bc250ProbeLastValue;
    return (ULONG)g_Bc250ProbeSequence;
}

VOID Bc250ProbeRecordFirstFailure(NTSTATUS Status)
{
    if (!NT_SUCCESS(Status)) {
        (void)InterlockedCompareExchange(
            &g_Bc250ProbeFirstFailure, (LONG)Status, (LONG)STATUS_SUCCESS);
    }
}

NTSTATUS Bc250ProbeGetFirstFailure(VOID)
{
    return (NTSTATUS)InterlockedCompareExchange(
        &g_Bc250ProbeFirstFailure,
        (LONG)STATUS_SUCCESS,
        (LONG)STATUS_SUCCESS);
}
