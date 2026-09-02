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
    case BC250_PROBE_PFP_SCHEDULER_BEFORE:
        return L"PfpSchedulerBefore";
    case BC250_PROBE_PFP_SCHEDULER_BEFORE_READ_PASSED:
        return L"PfpSchedulerBeforeReadPassed";
    case BC250_PROBE_PFP_SCHEDULER_AFTER:
        return L"PfpSchedulerAfter";
    case BC250_PROBE_PFP_SCHEDULER_AFTER_READ_PASSED:
        return L"PfpSchedulerAfterReadPassed";
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
    case BC250_PROBE_PFP_DIRECT_IC_INV_ENTRY_MASK:
        return L"PfpDirectIcInvEntryMask";
    case BC250_PROBE_PFP_DIRECT_IC_INV_UNHALT_MASK:
        return L"PfpDirectIcInvUnhaltMask";
    case BC250_PROBE_PFP_DIRECT_IC_PRIME_UNHALT_MASK:
        return L"PfpDirectIcPrimeUnhaltMask";
    case BC250_PROBE_PFP_DIRECT_JT_STAT_PFP: return L"PfpDirectJtStatPfp";
    case BC250_PROBE_PFP_DIRECT_JT_STAT_CE:  return L"PfpDirectJtStatCe";
    case BC250_PROBE_PFP_DIRECT_JT_STAT_MEC: return L"PfpDirectJtStatMec";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_00:
        return L"PfpDirectPcTrace00";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_01:
        return L"PfpDirectPcTrace01";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_02:
        return L"PfpDirectPcTrace02";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_03:
        return L"PfpDirectPcTrace03";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_04:
        return L"PfpDirectPcTrace04";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_05:
        return L"PfpDirectPcTrace05";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_06:
        return L"PfpDirectPcTrace06";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_07:
        return L"PfpDirectPcTrace07";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_08:
        return L"PfpDirectPcTrace08";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_09:
        return L"PfpDirectPcTrace09";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_10:
        return L"PfpDirectPcTrace10";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_11:
        return L"PfpDirectPcTrace11";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_12:
        return L"PfpDirectPcTrace12";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_13:
        return L"PfpDirectPcTrace13";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_14:
        return L"PfpDirectPcTrace14";
    case BC250_PROBE_PFP_DIRECT_PC_TRACE_15:
        return L"PfpDirectPcTrace15";
    case BC250_PROBE_PFP_DIRECT_INV_ENGINE:
        return L"PfpDirectInvEngine";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_STAGING:
        return L"PfpDirectInvAckStaging";
    case BC250_PROBE_PFP_DIRECT_INV_POLLS_STAGING:
        return L"PfpDirectInvPollsStaging";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_PRE_WPTR:
        return L"PfpDirectInvAckPreWptr";
    case BC250_PROBE_PFP_DIRECT_INV_POLLS_PRE_WPTR:
        return L"PfpDirectInvPollsPreWptr";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_STAGING:
        return L"PfpDirectInvAckBeforeStaging";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_BEFORE_PRE_WPTR:
        return L"PfpDirectInvAckBeforePreWptr";
    case BC250_PROBE_PFP_DIRECT_INV_USE_SEMAPHORE:
        return L"PfpDirectInvUseSemaphore";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_STAGING:
        return L"PfpDirectInvAckRawStaging";
    case BC250_PROBE_PFP_DIRECT_INV_ACK_RAW_PRE_WPTR:
        return L"PfpDirectInvAckRawPreWptr";
    case BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_STAGING:
        return L"PfpDirectInvSemAcqStaging";
    case BC250_PROBE_PFP_DIRECT_INV_SEM_ACQ_PRE_WPTR:
        return L"PfpDirectInvSemAcqPreWptr";
    case BC250_PROBE_PFP_DIRECT_CP_SOFT_RESET:
        return L"PfpDirectCpSoftReset";
    case BC250_PROBE_PFP_DIRECT_VMID0_PT:
        return L"PfpDirectVmid0PageTable";
    case BC250_PROBE_PFP_DIRECT_VMID0_INV_ACK:
        return L"PfpDirectVmid0InvAck";
    case BC250_PROBE_PFP_DIRECT_VMID0_ACK_AFTER:
        return L"PfpDirectVmid0AckAfter";
    case BC250_PROBE_TUNABLE_READ_OK:
        return L"TunableReadOkMask";
    case BC250_PROBE_SDMA_RING_ERROR_LOG_BEFORE:
        return L"SdmaRingErrorLogBefore";
    case BC250_PROBE_SDMA_RING_RD_XNACK0_BEFORE:
        return L"SdmaRingUtcl1ReadXnack0Before";
    case BC250_PROBE_SDMA_RING_WR_XNACK0_BEFORE:
        return L"SdmaRingUtcl1WriteXnack0Before";
    case BC250_PROBE_SDMA_RING_UTCL1_TIMEOUT_BEFORE:
        return L"SdmaRingUtcl1TimeoutBefore";
    case BC250_PROBE_SDMA_RING_CNTL_BEFORE:
        return L"SdmaRingCntlBefore";
    case BC250_PROBE_SDMA_RING_CNTL_APPLIED:
        return L"SdmaRingCntlApplied";
    case BC250_PROBE_SDMA_RING_EX_IDLE_EXPIRED:
        return L"SdmaRingExIdlePollExpired";
    case BC250_PROBE_PFP_DIRECT_COMPARED_DWORDS:
        return L"PfpDirectComparedDwords";
    case BC250_PROBE_PFP_DIRECT_MISMATCHES:
        return L"PfpDirectMismatches";
    case BC250_PROBE_PFP_DIRECT_DEST_POLLS:
        return L"PfpDirectDestPolls";
    case BC250_PROBE_PFP_DIRECT_DEST_OBSERVED:
        return L"PfpDirectDestObserved";
    case BC250_PROBE_SDMA_RING_EX_IDLE_AT_ENTRY:
        return L"SdmaRingExIdleAtEntry";
    case BC250_PROBE_SDMA_RING_WAKE_APPLIED:
        return L"SdmaRingWakeApplied";
    case BC250_PROBE_SDMA_RING_EX_IDLE_AFTER_WAKE:
        return L"SdmaRingExIdleAfterWake";
    case BC250_PROBE_SDMA_RING_EX_IDLE_CLEAR_POLLS:
        return L"SdmaRingExIdleClearPolls";
    case BC250_PROBE_SDMA_RING_CLK_AT_ENTRY:
        return L"SdmaRingClkCtrlAtEntry";
    case BC250_PROBE_SDMA_RING_EARLY_CLK_UNGATE:
        return L"SdmaRingEarlyClkUngate";
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
    case BC250_PROBE_HQD_ACTIVE_EOP_RPTR_BEFORE: return L"HqdActiveEopRptrBefore";
    case BC250_PROBE_HQD_ACTIVE_EOP_RPTR_APPLIED: return L"HqdActiveEopRptrApplied";
    case BC250_PROBE_HQD_ACTIVE_EOP_RPTR_READBACK: return L"HqdActiveEopRptrReadback";
    case BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORED: return L"HqdActiveEopRptrRestored";
    case BC250_PROBE_HQD_ACTIVE_EOP_RPTR_RESTORE_PASSED: return L"HqdActiveEopRptrRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_BEFORE: return L"HqdActiveWptrPollCntl1Before";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_APPLIED: return L"HqdActiveWptrPollCntl1Applied";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_READBACK: return L"HqdActiveWptrPollCntl1Readback";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORED: return L"HqdActiveWptrPollCntl1Restored";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_RESTORE_PASSED: return L"HqdActiveWptrPollCntl1RestorePassed";
    case BC250_PROBE_HQD_ACTIVE_QUEUE_MASK_BIT: return L"HqdActiveQueueMaskBit";
    case BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_APPLIED: return L"HqdActivePreActiveWptrApplied";
    case BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_READBACK: return L"HqdActivePreActiveWptrReadback";
    case BC250_PROBE_HQD_ACTIVE_PRE_ACTIVE_WPTR_PUBLISHED: return L"HqdActivePreActiveWptrPublished";
    case BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_STAGE: return L"HqdActiveFirstRestoreFailureStage";
    case BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OFFSET: return L"HqdActiveFirstRestoreFailureOffset";
    case BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_EXPECTED: return L"HqdActiveFirstRestoreFailureExpected";
    case BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_OBSERVED: return L"HqdActiveFirstRestoreFailureObserved";
    case BC250_PROBE_HQD_ACTIVE_FIRST_RESTORE_FAIL_TRANSPORT: return L"HqdActiveFirstRestoreFailureTransport";
    case BC250_PROBE_HQD_ACTIVE_RESTORE_FAILURE_COUNT: return L"HqdActiveRestoreFailureCount";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_APPLIED: return L"HqdActiveWptrPollCntlProbeApplied";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_PROBE_READBACK: return L"HqdActiveWptrPollCntlProbeReadback";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_WRITABLE: return L"HqdActiveWptrPollCntlWritable";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL_EN_LATCHED: return L"HqdActiveWptrPollCntlEnLatched";
    case BC250_PROBE_HQD_ACTIVE_POST_ACTIVE_WPTR_OBSERVED: return L"HqdActivePostActiveWptrObserved";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_DIRTY_ENTRY: return L"HqdActiveWptrPollCntl1DirtyEntry";
    case BC250_PROBE_HQD_ACTIVE_WPTR_POLL_CNTL1_CLEAR_ATTEMPTS: return L"HqdActiveWptrPollCntl1ClearAttempts";
    case BC250_PROBE_HQD_ACTIVE_PQ_CONTROL_FIRST_SAMPLE: return L"HqdActivePqControlFirstSample";
    case BC250_PROBE_HQD_ACTIVE_HQ_STATUS0_FIRST_SAMPLE: return L"HqdActiveHqStatus0FirstSample";
    case BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_POLL_LAST: return L"HqdActiveDequeueRequestPollLast";
    case BC250_PROBE_HQD_ACTIVE_VMID_POLL_LAST: return L"HqdActiveVmidPollLast";
    case BC250_PROBE_HQD_ACTIVE_PERSISTENT_STATE_POLL_LAST: return L"HqdActivePersistentStatePollLast";
    case BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_FIRST: return L"HqdActiveRptrReportMemFirst";
    case BC250_PROBE_HQD_ACTIVE_RPTR_REPORT_MEM_LAST: return L"HqdActiveRptrReportMemLast";
    case BC250_PROBE_HQD_ACTIVE_RPTR_NON_ZERO_SAMPLES: return L"HqdActiveRptrNonZeroSamples";
    case BC250_PROBE_HQD_ACTIVE_CPC_STATUS_FIRST: return L"HqdActiveCpcStatusFirst";
    case BC250_PROBE_HQD_ACTIVE_CPC_STATUS_LAST: return L"HqdActiveCpcStatusLast";
    case BC250_PROBE_HQD_ACTIVE_CPC_BUSY_STAT_LAST: return L"HqdActiveCpcBusyStatLast";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_STAT_LAST: return L"HqdActiveCpfBusyStatLast";
    case BC250_PROBE_HQD_ACTIVE_CPC_STALLED_STAT1_LAST: return L"HqdActiveCpcStalledStat1Last";
    case BC250_PROBE_HQD_ACTIVE_CP_STAT_FIRST: return L"HqdActiveCpStatFirst";
    case BC250_PROBE_HQD_ACTIVE_CP_STAT_LAST: return L"HqdActiveCpStatLast";
    case BC250_PROBE_HQD_ACTIVE_CP_STALLED_STAT1_AFTER: return L"HqdActiveCpStalledStat1After";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP0: return L"HqdActiveMecMe1HeaderDump0";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP1: return L"HqdActiveMecMe1HeaderDump1";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP2: return L"HqdActiveMecMe1HeaderDump2";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME1_HEADER_DUMP3: return L"HqdActiveMecMe1HeaderDump3";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP0: return L"HqdActiveMecMe2HeaderDump0";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP1: return L"HqdActiveMecMe2HeaderDump1";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP2: return L"HqdActiveMecMe2HeaderDump2";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_HEADER_DUMP3: return L"HqdActiveMecMe2HeaderDump3";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE0: return L"HqdActiveMecMe2BeforeActive0";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE1: return L"HqdActiveMecMe2BeforeActive1";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE2: return L"HqdActiveMecMe2BeforeActive2";
    case BC250_PROBE_HQD_ACTIVE_MEC_ME2_BEFORE_ACTIVE3: return L"HqdActiveMecMe2BeforeActive3";
    case BC250_PROBE_HQD_ACTIVE_CPC_STATUS_BEFORE_KICK: return L"HqdActiveCpcStatusBeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_BUSY_BEFORE_KICK: return L"HqdActiveCpcBusyStatBeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_KICK: return L"HqdActiveCpfBusyStatBeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_STALLED_BEFORE_KICK: return L"HqdActiveCpcStalledStat1BeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CP_STAT_BEFORE_KICK: return L"HqdActiveCpStatBeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CP_STALLED_BEFORE_KICK: return L"HqdActiveCpStalledStat1BeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_STATUS_AFTER_KICK: return L"HqdActiveCpcStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPC_BUSY_AFTER_KICK: return L"HqdActiveCpcBusyStatAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_KICK: return L"HqdActiveCpfBusyStatAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_ACTIVE: return L"HqdActiveCpfBusyStatBeforeActive";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_ACTIVE: return L"HqdActiveCpfBusyStatAfterActive";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_BEFORE_WPTR: return L"HqdActiveCpfBusyStatBeforeWptr";
    case BC250_PROBE_HQD_ACTIVE_CPF_BUSY_AFTER_WPTR: return L"HqdActiveCpfBusyStatAfterWptr";
    case BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_ACTIVE: return L"HqdActiveCpfStalledStat1BeforeActive";
    case BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_ACTIVE: return L"HqdActiveCpfStalledStat1AfterActive";
    case BC250_PROBE_HQD_ACTIVE_CPF_STALLED_BEFORE_KICK: return L"HqdActiveCpfStalledStat1BeforeKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_STALLED_AFTER_KICK: return L"HqdActiveCpfStalledStat1AfterKick";
    case BC250_PROBE_HQD_ACTIVE_CPF_STALLED_LAST: return L"HqdActiveCpfStalledStat1Last";
    case BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_ACTIVE: return L"HqdActiveCpfStatusAfterActive";
    case BC250_PROBE_HQD_ACTIVE_CPF_STATUS_AFTER_KICK: return L"HqdActiveCpfStatusAfterKick";
    case BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_APPLIED: return L"HqdActiveDequeueRequestApplied";
    case BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_READBACK: return L"HqdActiveDequeueRequestReadback";
    case BC250_PROBE_HQD_ACTIVE_DEQUEUE_REQUEST_RESTORE_PASSED: return L"HqdActiveDequeueRequestRestorePassed";
    case BC250_PROBE_HQD_ACTIVE_CPC_STALLED_AFTER_KICK: return L"HqdActiveCpcStalledStat1AfterKick";
    case BC250_PROBE_HQD_ACTIVE_CP_STAT_AFTER_KICK: return L"HqdActiveCpStatAfterKick";
    case BC250_PROBE_HQD_ACTIVE_CP_STALLED_AFTER_KICK: return L"HqdActiveCpStalledStat1AfterKick";
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
    case BC250_PROBE_SHADER_DISPATCH_ENABLED:
        return L"ShaderDispatchEnabled";
    case BC250_PROBE_SHADER_DISPATCH_DWORDS:
        return L"ShaderDispatchDwords";
    case BC250_PROBE_SHADER_DISPATCH_PACKET_INDEX:
        return L"ShaderDispatchPacketIndex";
    case BC250_PROBE_SHADER_FENCE_PACKET_INDEX:
        return L"ShaderFencePacketIndex";
    case BC250_PROBE_SHADER_FENCE_AFTER_DISPATCH:
        return L"ShaderFenceAfterDispatch";
    case BC250_PROBE_SHADER_CC_ARRAY_PASS1:
        return L"ShaderCcArrayPass1";
    case BC250_PROBE_SHADER_CC_ARRAY_PASS2:
        return L"ShaderCcArrayPass2";
    case BC250_PROBE_SHADER_SPI_WGP_MASK_PASS1:
        return L"ShaderSpiWgpMaskPass1";
    case BC250_PROBE_SHADER_SPI_WGP_MASK_PASS2:
        return L"ShaderSpiWgpMaskPass2";
    case BC250_PROBE_SHADER_RLC_AON_WGP_PASS1:
        return L"ShaderRlcAonWgpPass1";
    case BC250_PROBE_SHADER_RLC_AON_WGP_PASS2:
        return L"ShaderRlcAonWgpPass2";
    case BC250_PROBE_SHADER_NON_HARVESTED_WGP:
        return L"ShaderNonHarvestedWgpMask";
    case BC250_PROBE_SHADER_AON_NON_HARVESTED_WGP:
        return L"ShaderAonNonHarvestedWgpMask";
    case BC250_PROBE_SHADER_WGP_PREFLIGHT_PASSED:
        return L"ShaderWgpPreflightPassed";
    case BC250_PROBE_SHADER_RING_GPUVA_LOW:
        return L"ShaderRingGpuVaLow";
    case BC250_PROBE_SHADER_RING_GPUVA_HIGH:
        return L"ShaderRingGpuVaHigh";
    case BC250_PROBE_SHADER_RING_PHYSICAL_LOW:
        return L"ShaderRingPhysicalLow";
    case BC250_PROBE_SHADER_RING_PHYSICAL_HIGH:
        return L"ShaderRingPhysicalHigh";
    case BC250_PROBE_SHADER_RING_PTE_LOW:
        return L"ShaderRingPteLow";
    case BC250_PROBE_SHADER_RING_PTE_HIGH:
        return L"ShaderRingPteHigh";
    case BC250_PROBE_SHADER_RING_TRANSLATION_PASSED:
        return L"ShaderRingTranslationPassed";
    case BC250_PROBE_SHADER_RING_READBACK_PASSED:
        return L"ShaderRingReadbackPassed";
    case BC250_PROBE_SHADER_RING_READBACK_MISMATCHES:
        return L"ShaderRingReadbackMismatches";
    case BC250_PROBE_SHADER_RING_DWORD0: return L"ShaderRingDword0";
    case BC250_PROBE_SHADER_RING_DWORD1: return L"ShaderRingDword1";
    case BC250_PROBE_SHADER_RING_DWORD2: return L"ShaderRingDword2";
    case BC250_PROBE_SHADER_RING_DWORD3: return L"ShaderRingDword3";
    case BC250_PROBE_SHADER_RING_DWORD4: return L"ShaderRingDword4";
    case BC250_PROBE_SHADER_RING_DWORD5: return L"ShaderRingDword5";
    case BC250_PROBE_SHADER_RING_DWORD6: return L"ShaderRingDword6";
    case BC250_PROBE_SHADER_RING_DWORD7: return L"ShaderRingDword7";
    case BC250_PROBE_SHADER_READBACK_PASSED:
        return L"ShaderReadbackPassed";
    case BC250_PROBE_SHADER_READBACK_MISMATCHES:
        return L"ShaderReadbackMismatches";
    case BC250_PROBE_SHADER_RESULT_BEFORE:
        return L"ShaderResultBefore";
    case BC250_PROBE_SHADER_RESULT_AFTER:
        return L"ShaderResultAfter";
    case BC250_PROBE_SHADER_RESULT_MISMATCHES:
        return L"ShaderResultMismatches";
    case BC250_PROBE_SHADER_FENCE_BEFORE:
        return L"ShaderFenceBefore";
    case BC250_PROBE_SHADER_FENCE_AFTER:
        return L"ShaderFenceAfter";
    case BC250_PROBE_SHADER_RPTR_AFTER:
        return L"ShaderRptrAfter";
    case BC250_PROBE_SHADER_WPTR_AFTER:
        return L"ShaderWptrAfter";
    case BC250_PROBE_SHADER_REG_RESTORE:
        return L"ShaderRegisterRestorePassed";
    case BC250_PROBE_SHADER_MEM_RESTORE:
        return L"ShaderMemoryRestorePassed";
    case BC250_PROBE_SHADER_TRIAD_PASSED:
        return L"ShaderTriadPassed";
    case BC250_PROBE_SHADER_PHYSICAL_READY:
        return L"ShaderPhysicalReady";
    case BC250_PROBE_P1_ENGINE_STATE:
        return L"P1EngineState";
    case BC250_PROBE_P1_ACTIVATE_STATUS:
        return L"P1ActivateStatus";
    case BC250_PROBE_P1_TEARDOWN_STATUS:
        return L"P1TeardownStatus";
    case BC250_PROBE_P1_SUBMIT_CALLS:
        return L"P1SubmitCalls";
    case BC250_PROBE_P1_SUBMIT_ACCEPTED:
        return L"P1SubmitAccepted";
    case BC250_PROBE_P1_SUBMIT_REJECTED_NODE:
        return L"P1SubmitRejectedNode";
    case BC250_PROBE_P1_SUBMIT_REJECTED_PLAN:
        return L"P1SubmitRejectedPlan";
    case BC250_PROBE_P1_SUBMIT_REJECTED_EMIT:
        return L"P1SubmitRejectedEmit";
    case BC250_PROBE_P1_RING_FULL_COUNT:
        return L"P1RingFullCount";
    case BC250_PROBE_P1_FENCE_REGRESSION_COUNT:
        return L"P1FenceRegressionCount";
    case BC250_PROBE_P1_DOORBELL_KICKS:
        return L"P1DoorbellKicks";
    case BC250_PROBE_P1_DIRECT_WPTR_WRITES:
        return L"P1DirectWptrWrites";
    case BC250_PROBE_P1_POLL_CALLS:
        return L"P1PollCalls";
    case BC250_PROBE_P1_RETIRED_TOTAL:
        return L"P1RetiredTotal";
    case BC250_PROBE_P1_LAST_PLAN_PAD_DWORDS:
        return L"P1LastPlanPadDwords";
    case BC250_PROBE_P1_LAST_WPTR_PUBLISHED:
        return L"P1LastWptrPublished";
    case BC250_PROBE_P1_LAST_WPTR_SHADOW_READBACK:
        return L"P1LastWptrShadowReadback";
    case BC250_PROBE_P1_LAST_WPTR_MMIO_READBACK:
        return L"P1LastWptrMmioReadback";
    case BC250_PROBE_P1_LAST_RPTR_MMIO:
        return L"P1LastRptrMmio";
    case BC250_PROBE_P1_LAST_RPTR_REPORT_MEM:
        return L"P1LastRptrReportMem";
    case BC250_PROBE_P1_LAST_FENCE_OBSERVED:
        return L"P1LastFenceObserved";
    case BC250_PROBE_P1_LAST_SUBMIT_STATUS:
        return L"P1LastSubmitStatus";
    case BC250_PROBE_P1_LAST_SUBMITTED_FENCE:
        return L"P1LastSubmittedFence";
    case BC250_PROBE_P1_LAST_RETIRED_FENCE:
        return L"P1LastRetiredFence";
    case BC250_PROBE_P1_COHER_HDP_APPLIED:
        return L"P1CoherHdpApplied";
    case BC250_PROBE_P1_COHER_L2_READBACK:
        return L"P1CoherL2Readback";
    case BC250_PROBE_P1_COHER_INVALIDATE_ACK:
        return L"P1CoherInvalidateAck";
    case BC250_PROBE_P1_COHER_INVALIDATE_POLLS:
        return L"P1CoherInvalidatePolls";
    case BC250_PROBE_P1_COHERENCY_PASSED:
        return L"P1CoherencyPassed";
    case BC250_PROBE_P1_RING_WPTR:
        return L"P1RingWptr";
    case BC250_PROBE_P1_RING_RPTR_SHADOW:
        return L"P1RingRptrShadow";
    case BC250_PROBE_P1_RING_WRAP_COUNT:
        return L"P1RingWrapCount";
    case BC250_PROBE_P1_RING_PAD_DWORDS_TOTAL:
        return L"P1RingPadDwordsTotal";
    case BC250_PROBE_P1_SELFTEST_ATTEMPTED:
        return L"P1SelfTestAttempted";
    case BC250_PROBE_P1_SELFTEST_PLAN_STATUS:
        return L"P1SelfTestPlanStatus";
    case BC250_PROBE_P1_SELFTEST_SUBMIT_STATUS:
        return L"P1SelfTestSubmitStatus";
    case BC250_PROBE_P1_SELFTEST_IB_DWORDS:
        return L"P1SelfTestIbDwords";
    case BC250_PROBE_P1_SELFTEST_IB_GPUVA_LOW:
        return L"P1SelfTestIbGpuVaLow";
    case BC250_PROBE_P1_SELFTEST_ASSIGNED_FENCE:
        return L"P1SelfTestAssignedFence";
    case BC250_PROBE_P1_SELFTEST_RING_WPTR_AFTER:
        return L"P1SelfTestRingWptrAfter";
    case BC250_PROBE_P1_SELFTEST_RPTR_MMIO_AFTER:
        return L"P1SelfTestRptrMmioAfter";
    case BC250_PROBE_P1_SELFTEST_RPTR_REPORT_AFTER:
        return L"P1SelfTestRptrReportAfter";
    case BC250_PROBE_P1_SELFTEST_DISPATCH_FENCE_BEFORE:
        return L"P1SelfTestDispatchFenceBefore";
    case BC250_PROBE_P1_SELFTEST_DISPATCH_FENCE_AFTER:
        return L"P1SelfTestDispatchFenceAfter";
    case BC250_PROBE_P1_SELFTEST_RESULT_BEFORE:
        return L"P1SelfTestResultBefore";
    case BC250_PROBE_P1_SELFTEST_RESULT_AFTER:
        return L"P1SelfTestResultAfter";
    case BC250_PROBE_P1_SELFTEST_RESULT_MISMATCHES:
        return L"P1SelfTestResultMismatches";
    case BC250_PROBE_P1_SELFTEST_RING_FENCE_OBSERVED:
        return L"P1SelfTestRingFenceObserved";
    case BC250_PROBE_P1_SELFTEST_RETIRED_COUNT:
        return L"P1SelfTestRetiredCount";
    case BC250_PROBE_P1_SELFTEST_POLLS:
        return L"P1SelfTestPolls";
    case BC250_PROBE_P1_SELFTEST_SHADER_STAGED:
        return L"P1SelfTestShaderStaged";
    case BC250_PROBE_P1_SELFTEST_IB_READBACK:
        return L"P1SelfTestIbReadback";
    case BC250_PROBE_P1_SELFTEST_TRIAD_PASSED:
        return L"P1SelfTestTriadPassed";
    case BC250_PROBE_P1_SELFTEST_PHYSICAL_READY:
        return L"P1SelfTestPhysicalReady";
    case BC250_PROBE_P1_FENCE_BASELINE_WRITTEN:
        return L"P1FenceBaselineWritten";
    case BC250_PROBE_P1_FENCE_BASELINE_READBACK:
        return L"P1FenceBaselineReadback";
    case BC250_PROBE_P1_SNAP_ACT_ACTIVE:
        return L"P1SnapActActive";
    case BC250_PROBE_P1_SNAP_ACT_HQSTATUS0:
        return L"P1SnapActHqStatus0";
    case BC250_PROBE_P1_SNAP_ACT_PQCONTROL:
        return L"P1SnapActPqControl";
    case BC250_PROBE_P1_SNAP_ACT_DOORBELLCONTROL:
        return L"P1SnapActDoorbellControl";
    case BC250_PROBE_P1_SNAP_ACT_CPPQSTATUS:
        return L"P1SnapActCpPqStatus";
    case BC250_PROBE_P1_SNAP_ACT_CPCSTATUS:
        return L"P1SnapActCpcStatus";
    case BC250_PROBE_P1_SNAP_ACT_HQDERROR:
        return L"P1SnapActHqdError";
    case BC250_PROBE_P1_SNAP_ACT_PQWPTRLO:
        return L"P1SnapActPqWptrLo";
    case BC250_PROBE_P1_SNAP_ACT_PQRPTR:
        return L"P1SnapActPqRptr";
    case BC250_PROBE_P1_SNAP_ACT_MECME2HEADERDUMP:
        return L"P1SnapActMecMe2";
    case BC250_PROBE_P1_SNAP_SUB_ACTIVE:
        return L"P1SnapSubActive";
    case BC250_PROBE_P1_SNAP_SUB_HQSTATUS0:
        return L"P1SnapSubHqStatus0";
    case BC250_PROBE_P1_SNAP_SUB_PQCONTROL:
        return L"P1SnapSubPqControl";
    case BC250_PROBE_P1_SNAP_SUB_DOORBELLCONTROL:
        return L"P1SnapSubDoorbellControl";
    case BC250_PROBE_P1_SNAP_SUB_CPPQSTATUS:
        return L"P1SnapSubCpPqStatus";
    case BC250_PROBE_P1_SNAP_SUB_CPCSTATUS:
        return L"P1SnapSubCpcStatus";
    case BC250_PROBE_P1_SNAP_SUB_HQDERROR:
        return L"P1SnapSubHqdError";
    case BC250_PROBE_P1_SNAP_SUB_PQWPTRLO:
        return L"P1SnapSubPqWptrLo";
    case BC250_PROBE_P1_SNAP_SUB_PQRPTR:
        return L"P1SnapSubPqRptr";
    case BC250_PROBE_P1_SNAP_SUB_MECME2HEADERDUMP:
        return L"P1SnapSubMecMe2";
    case BC250_PROBE_P1_SNAP_RE_ACTIVE:
        return L"P1SnapReActive";
    case BC250_PROBE_P1_SNAP_RE_HQSTATUS0:
        return L"P1SnapReHqStatus0";
    case BC250_PROBE_P1_SNAP_RE_PQCONTROL:
        return L"P1SnapRePqControl";
    case BC250_PROBE_P1_SNAP_RE_DOORBELLCONTROL:
        return L"P1SnapReDoorbellControl";
    case BC250_PROBE_P1_SNAP_RE_CPPQSTATUS:
        return L"P1SnapReCpPqStatus";
    case BC250_PROBE_P1_SNAP_RE_CPCSTATUS:
        return L"P1SnapReCpcStatus";
    case BC250_PROBE_P1_SNAP_RE_HQDERROR:
        return L"P1SnapReHqdError";
    case BC250_PROBE_P1_SNAP_RE_PQWPTRLO:
        return L"P1SnapRePqWptrLo";
    case BC250_PROBE_P1_SNAP_RE_PQRPTR:
        return L"P1SnapRePqRptr";
    case BC250_PROBE_P1_SNAP_RE_MECME2HEADERDUMP:
        return L"P1SnapReMecMe2";
    case BC250_PROBE_P1_REARM_ATTEMPTED:
        return L"P1RearmAttempted";
    case BC250_PROBE_P1_REARM_ACTIVE_BEFORE:
        return L"P1RearmActiveBefore";
    case BC250_PROBE_P1_REARM_DEACTIVATE_POLLS:
        return L"P1RearmDeactivatePolls";
    case BC250_PROBE_P1_REARM_ACTIVE_AFTER_OFF:
        return L"P1RearmActiveAfterOff";
    case BC250_PROBE_P1_REARM_ACTIVE_AFTER_ON:
        return L"P1RearmActiveAfterOn";
    case BC250_PROBE_P1_REARM_POLLS:
        return L"P1RearmPolls";
    case BC250_PROBE_P1_REARM_RPTR_MMIO:
        return L"P1RearmRptrMmio";
    case BC250_PROBE_P1_REARM_RPTR_REPORT:
        return L"P1RearmRptrReport";
    case BC250_PROBE_P1_REARM_DISPATCH_FENCE:
        return L"P1RearmDispatchFence";
    case BC250_PROBE_P1_REARM_RESULT:
        return L"P1RearmResult";
    case BC250_PROBE_P1_REARM_RING_FENCE:
        return L"P1RearmRingFence";
    case BC250_PROBE_P1_REARM_COMPLETED:
        return L"P1RearmCompleted";
    case BC250_PROBE_P1_SNAP_ACT_CP_MEC_CNTL:
        return L"P1SnapActCpMecCntl";
    case BC250_PROBE_P1_SNAP_ACT_CP_ME_CNTL:
        return L"P1SnapActCpMeCntl";
    case BC250_PROBE_P1_SNAP_ACT_RLC_SCHEDULERS:
        return L"P1SnapActRlcSchedulers";
    case BC250_PROBE_P1_SNAP_ACT_WPTR_POLL_CNTL:
        return L"P1SnapActWptrPollCntl";
    case BC250_PROBE_P1_SNAP_ACT_WPTR_POLL_CNTL1:
        return L"P1SnapActWptrPollCntl1";
    case BC250_PROBE_P1_SNAP_SUB_CP_MEC_CNTL:
        return L"P1SnapSubCpMecCntl";
    case BC250_PROBE_P1_SNAP_SUB_CP_ME_CNTL:
        return L"P1SnapSubCpMeCntl";
    case BC250_PROBE_P1_SNAP_SUB_RLC_SCHEDULERS:
        return L"P1SnapSubRlcSchedulers";
    case BC250_PROBE_P1_SNAP_SUB_WPTR_POLL_CNTL:
        return L"P1SnapSubWptrPollCntl";
    case BC250_PROBE_P1_SNAP_SUB_WPTR_POLL_CNTL1:
        return L"P1SnapSubWptrPollCntl1";
    case BC250_PROBE_P1_SNAP_RE_CP_MEC_CNTL:
        return L"P1SnapReCpMecCntl";
    case BC250_PROBE_P1_SNAP_RE_CP_ME_CNTL:
        return L"P1SnapReCpMeCntl";
    case BC250_PROBE_P1_SNAP_RE_RLC_SCHEDULERS:
        return L"P1SnapReRlcSchedulers";
    case BC250_PROBE_P1_SNAP_RE_WPTR_POLL_CNTL:
        return L"P1SnapReWptrPollCntl";
    case BC250_PROBE_P1_SNAP_RE_WPTR_POLL_CNTL1:
        return L"P1SnapReWptrPollCntl1";
    case BC250_PROBE_P1_REARM_QUEUE_MASK_APPLIED:
        return L"P1RearmQueueMaskApplied";
    case BC250_PROBE_P1_REARM_QUEUE_MASK_READBACK:
        return L"P1RearmQueueMaskReadback";
    case BC250_PROBE_P1_REARM_QUEUE_MASK_PASSED:
        return L"P1RearmQueueMaskPassed";
    case BC250_PROBE_P1_RING_DWORD_0:
        return L"P1RingDword0";
    case BC250_PROBE_P1_RING_DWORD_1:
        return L"P1RingDword1";
    case BC250_PROBE_P1_RING_DWORD_2:
        return L"P1RingDword2";
    case BC250_PROBE_P1_RING_DWORD_3:
        return L"P1RingDword3";
    case BC250_PROBE_P1_RING_DWORD_4:
        return L"P1RingDword4";
    case BC250_PROBE_P1_RING_DWORD_5:
        return L"P1RingDword5";
    case BC250_PROBE_P1_RING_DWORD_6:
        return L"P1RingDword6";
    case BC250_PROBE_P1_RING_DWORD_7:
        return L"P1RingDword7";
    case BC250_PROBE_P1_RING_DWORD_8:
        return L"P1RingDword8";
    case BC250_PROBE_P1_RING_DWORD_9:
        return L"P1RingDword9";
    case BC250_PROBE_P1_RING_DWORD_10:
        return L"P1RingDword10";
    case BC250_PROBE_P1_RING_DWORD_11:
        return L"P1RingDword11";
    case BC250_PROBE_P1_RING_DWORDS_MISMATCH:
        return L"P1RingDwordsMismatch";
    case BC250_PROBE_P1_HQD_PQ_BASE_LO:
        return L"P1HqdPqBaseLo";
    case BC250_PROBE_P1_HQD_PQ_BASE_HI:
        return L"P1HqdPqBaseHi";
    case BC250_PROBE_P1_HQD_RPTR_REPORT_ADDR_LO:
        return L"P1HqdRptrReportAddrLo";
    case BC250_PROBE_P1_HQD_RPTR_REPORT_ADDR_HI:
        return L"P1HqdRptrReportAddrHi";
    case BC250_PROBE_P1_HQD_WPTR_POLL_ADDR_LO:
        return L"P1HqdWptrPollAddrLo";
    case BC250_PROBE_P1_HQD_WPTR_POLL_ADDR_HI:
        return L"P1HqdWptrPollAddrHi";
    case BC250_PROBE_P1_HQD_VMID:
        return L"P1HqdVmid";
    case BC250_PROBE_P1_HQD_MQD_CONTROL:
        return L"P1HqdMqdControl";
    case BC250_PROBE_P1_HQD_IB_CONTROL:
        return L"P1HqdIbControl";
    case BC250_PROBE_P1_POST_CPC_STATUS:
        return L"P1PostCpcStatus";
    case BC250_PROBE_P1_POST_CPC_BUSY_STAT:
        return L"P1PostCpcBusyStat";
    case BC250_PROBE_P1_POST_CPC_STALLED_STAT1:
        return L"P1PostCpcStalledStat1";
    case BC250_PROBE_P1_POST_CPF_STATUS:
        return L"P1PostCpfStatus";
    case BC250_PROBE_P1_POST_CPF_BUSY_STAT:
        return L"P1PostCpfBusyStat";
    case BC250_PROBE_P1_POST_CPF_STALLED_STAT1:
        return L"P1PostCpfStalledStat1";
    case BC250_PROBE_P1_POST_CP_STAT:
        return L"P1PostCpStat";
    case BC250_PROBE_P1_POST_CP_STALLED_STAT1:
        return L"P1PostCpStalledStat1";
    case BC250_PROBE_P1_POST_MEC_PC:
        return L"P1PostMecPc";
    case BC250_PROBE_P2_STREAM_ATTEMPTED:
        return L"P2StreamAttempted";
    case BC250_PROBE_P2_BATCH_COUNT:
        return L"P2BatchCount";
    case BC250_PROBE_P2_SUBMISSIONS_ACCEPTED:
        return L"P2SubmissionsAccepted";
    case BC250_PROBE_P2_SUBMISSIONS_RETIRED:
        return L"P2SubmissionsRetired";
    case BC250_PROBE_P2_WRAP_COUNT:
        return L"P2WrapCount";
    case BC250_PROBE_P2_RESULT_MISMATCHES:
        return L"P2ResultMismatches";
    case BC250_PROBE_P2_LAST_FENCE_OBSERVED:
        return L"P2LastFenceObserved";
    case BC250_PROBE_P2_FINAL_WPTR:
        return L"P2FinalWptr";
    case BC250_PROBE_P2_FINAL_RPTR_MMIO:
        return L"P2FinalRptrMmio";
    case BC250_PROBE_P2_FINAL_RPTR_REPORT:
        return L"P2FinalRptrReport";
    case BC250_PROBE_P2_TRIAD_PASSED:
        return L"P2TriadPassed";
    case BC250_PROBE_P2_PHYSICAL_READY:
        return L"P2PhysicalReady";
    case BC250_PROBE_P2_DOORBELL_WRITE_FAILURES:
        return L"P2DoorbellWriteFailures";
    case BC250_PROBE_P2_DOORBELL_LAST_VALUE_LOW:
        return L"P2DoorbellLastValueLow";
    case BC250_PROBE_P2_DOORBELL_LAST_VALUE_HIGH:
        return L"P2DoorbellLastValueHigh";
    case BC250_PROBE_P2_DOORBELL_CONTROL_BEFORE:
        return L"P2DoorbellControlBefore";
    case BC250_PROBE_P2_DOORBELL_CONTROL_AFTER:
        return L"P2DoorbellControlAfter";
    case BC250_PROBE_P2_DOORBELL_HIT_OBSERVED:
        return L"P2DoorbellHitObserved";

    /* Fase V2 — retirada de fence para o dxgkrnl. */
    case BC250_PROBE_V2_RETIREMENT_DPC_RUNS:
        return L"V2RetirementDpcRuns";
    case BC250_PROBE_V2_POLL_FAILURES:
        return L"V2PollFailures";
    case BC250_PROBE_V2_NOTIFY_BATCHES:
        return L"V2NotifyBatches";
    case BC250_PROBE_V2_NOTIFIED_TOTAL:
        return L"V2NotifiedTotal";
    case BC250_PROBE_V2_PUSH_FAILURES:
        return L"V2PushFailures";
    case BC250_PROBE_V2_BACKPRESSURE:
        return L"V2Backpressure";
    case BC250_PROBE_V2_FENCES_IN_FLIGHT:
        return L"V2FencesInFlight";
    case BC250_PROBE_V2_TRACKER_PUSHED_TOTAL:
        return L"V2TrackerPushedTotal";
    case BC250_PROBE_V2_TRACKER_OVERFLOW:
        return L"V2TrackerOverflow";
    case BC250_PROBE_V2_TRACKER_OUT_OF_ORDER:
        return L"V2TrackerOutOfOrder";
    case BC250_PROBE_V2_LAST_NOTIFIED_FENCE_ID:
        return L"V2LastNotifiedFenceId";
    case BC250_PROBE_V2_INTERRUPTS_SEEN:
        return L"V2InterruptsSeen";

    /* Fase V5 — modo de registro no dxgkrnl. */
    case BC250_PROBE_V5_RENDER_NODE_REQUESTED:
        return L"V5RenderNodeRequested";
    case BC250_PROBE_V5_RENDER_NODE_TUNABLE_PRESENT:
        return L"V5RenderNodeTunablePresent";
    case BC250_PROBE_V5_REGISTRATION_MODE:
        return L"V5RegistrationMode";
    case BC250_PROBE_V5_START_DEVICE_STATUS:
        return L"V5StartDeviceStatus";
    case BC250_PROBE_V5_START_DEVICE_SOURCES:
        return L"V5StartDeviceSources";
    case BC250_PROBE_V5_START_DEVICE_CHILDREN:
        return L"V5StartDeviceChildren";
    case BC250_PROBE_V5_QAI_CALLS:
        return L"V5QaiCalls";
    case BC250_PROBE_V5_QAI_LAST_TYPE:
        return L"V5QaiLastType";
    case BC250_PROBE_V5_QAI_LAST_STATUS:
        return L"V5QaiLastStatus";
    case BC250_PROBE_V5_QAI_LAST_UNSUPPORTED_TYPE:
        return L"V5QaiLastUnsupportedType";
    case BC250_PROBE_V5_QAI_UNSUPPORTED_COUNT:
        return L"V5QaiUnsupportedCount";
    case BC250_PROBE_V5_LAST_DDI:
        return L"V5LastDdi";
    case BC250_PROBE_V5_PREV_DDI:
        return L"V5PrevDdi";
    case BC250_PROBE_V5_PREV2_DDI:
        return L"V5Prev2Ddi";
    case BC250_PROBE_V5_DDI_MARKS:
        return L"V5DdiMarks";
    case BC250_PROBE_V5_QI_CALLS:
        return L"V5QiCalls";
    case BC250_PROBE_V5_QI_LAST_GUID_D1:
        return L"V5QiLastGuidD1";
    case BC250_PROBE_V5_QI_PREV_GUID_D1:
        return L"V5QiPrevGuidD1";
    case BC250_PROBE_V5_QI_PREV2_GUID_D1:
        return L"V5QiPrev2GuidD1";
    case BC250_PROBE_V5_QI_LAST_VERSION:
        return L"V5QiLastVersion";
    case BC250_PROBE_V5_QI_LAST_SIZE:
        return L"V5QiLastSize";
    case BC250_PROBE_GPUVA_GATE_ELIGIBLE:
        return L"GpuvaGateEligible";
    case BC250_PROBE_GPUVA_GATE_OPERATIONAL:
        return L"GpuvaGateOperational";
    case BC250_PROBE_GPUVA_GATE_BLOCK_MASK:
        return L"GpuvaGateBlockMask";
    case BC250_PROBE_GPUVA_SNAPSHOT_GENERATION:
        return L"GpuvaSnapshotGeneration";
    case BC250_PROBE_GPUVA_SNAPSHOT_EPOCH:
        return L"GpuvaSnapshotEpoch";
    case BC250_PROBE_GPUVA_SNAPSHOT_LIVE_COUNT:
        return L"GpuvaSnapshotLiveCount";
    case BC250_PROBE_GPUVA_LAB_PROMOTION_REQUESTED:
        return L"GpuvaLabPromotionRequested";
    case BC250_PROBE_GPUVA_LAB_PROMOTION_PRESENT:
        return L"GpuvaLabPromotionPresent";
    case BC250_PROBE_V5_STOP_FENCE_QUERY_MODE:
        return L"V5StopFenceQueryMode";
    case BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS:
        return L"V5StopFenceSyncStatus";
    case BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT:
        return L"V5StopFenceSyncResult";
    case BC250_PROBE_V5_STOP_FENCE_PENDING_COUNT:
        return L"V5StopFencePendingCount";
    case BC250_PROBE_V5_STOP_FENCE_RESERVED_COUNT:
        return L"V5StopFenceReservedCount";

    /* Schema 130 — quiescencia do teardown. */
    case BC250_PROBE_V5_STOP_REASON: return L"V5StopReason";
    case BC250_PROBE_V5_STOP_TIMER_DRAINED: return L"V5StopTimerDrained";
    case BC250_PROBE_V5_STOP_DPC_DRAINED: return L"V5StopDpcDrained";
    case BC250_PROBE_V5_STOP_SUBMIT_INFLIGHT:
        return L"V5StopSubmitInFlight";
    case BC250_PROBE_V5_STOP_DRAIN_ITERATIONS:
        return L"V5StopDrainIterations";
    case BC250_PROBE_V5_STOP_SNAPSHOT_OK: return L"V5StopSnapshotOk";
    case BC250_PROBE_V5_STOP_LOCK_FAILURES: return L"V5StopLockFailures";
    case BC250_PROBE_V5_STOP_REJECTED_WHILE_STOPPING:
        return L"V5StopRejectedWhileStopping";
    case BC250_PROBE_V5_STOP_FIRST_STATUS: return L"V5StopFirstStatus";
    case BC250_PROBE_V5_LIFECYCLE_STATE: return L"V5LifecycleState";

    /* Schema 130 — rastro ampliado pos-StartDevice. */
    case BC250_PROBE_V5_CHILD_RELATIONS_STATUS:
        return L"V5ChildRelationsStatus";
    case BC250_PROBE_V5_CHILD_RELATIONS_COUNT:
        return L"V5ChildRelationsCount";
    case BC250_PROBE_V5_CHILD_RELATIONS_CALLS:
        return L"V5ChildRelationsCalls";
    case BC250_PROBE_V5_POST_START_STAGE: return L"V5PostStartStage";
    case BC250_PROBE_V5_QI_SLOTS_CAPTURED: return L"V5QiSlotsCaptured";
    case BC250_PROBE_V5_CONTRACT_VERSION: return L"V5ContractVersion";

    /* Schema 132 — perfil de adaptador (Gate 1.5). */
    case BC250_PROBE_V5_ADAPTER_PROFILE:
        return L"V5AdapterProfile";
    case BC250_PROBE_V5_RENDER_ONLY_LAB_REQUESTED:
        return L"V5RenderOnlyLabRequested";
    case BC250_PROBE_V5_RENDER_ONLY_LAB_TUNABLE_PRESENT:
        return L"V5RenderOnlyLabTunablePresent";
    case BC250_PROBE_V5_PROFILE_RESOLVE_STATUS:
        return L"V5ProfileResolveStatus";
    case BC250_PROBE_V5_DISPLAY_SURFACE_ANNOUNCED:
        return L"V5DisplaySurfaceAnnounced";
    case BC250_PROBE_V5_CHILD_ENUMERATION_ENABLED:
        return L"V5ChildEnumerationEnabled";

    /* Schema 133 — marcadores de entrada. */
    case BC250_PROBE_V5_POST_DISPLAY_ATTEMPTED:
        return L"V5PostDisplayAttempted";
    case BC250_PROBE_V5_DISPLAY_INIT_ENTERED:
        return L"V5DisplayInitEntered";
    case BC250_PROBE_V5_UNSAFE_BRINGUP_ENTERED:
        return L"V5UnsafeBringupEntered";
    case BC250_PROBE_V5_RENDER_ONLY_EARLY_RETURN:
        return L"V5RenderOnlyEarlyReturn";
    case BC250_PROBE_V5_START_DEVICE_RETURN_STAGE:
        return L"V5StartDeviceReturnStage";
    case BC250_PROBE_V5_UNSAFE_BRINGUP_COMPILED:
        return L"V5UnsafeBringupCompiled";

    /* Schema 134 — as tres ABIs e o contador que prova a publicacao. */
    case BC250_PROBE_V5_ABI_READY_CONTEXT:
        return L"AbiReadyContext";
    case BC250_PROBE_V5_ABI_READY_ALLOCATION:
        return L"AbiReadyAllocation";
    case BC250_PROBE_V5_ABI_READY_ROOT_PAGE_TABLE:
        return L"AbiReadyRootPageTable";
    case BC250_PROBE_V5_ABI_READY_PUBLISH_COUNT:
        return L"AbiReadyPublishCount";

    /* Schema 134 — bitmap de DXGKQAITYPE perguntados. */
    case BC250_PROBE_V5_QAI_TYPE_SEEN_LO:
        return L"V5QaiTypeSeenLo";
    case BC250_PROBE_V5_QAI_TYPE_SEEN_HI:
        return L"V5QaiTypeSeenHi";
    case BC250_PROBE_V5_COMPUTE_ONLY_DECLARED:
        return L"V5ComputeOnlyDeclared";

    /*
     * Orfaos corrigidos em 2026-09-02. Os quatro sao gravados por
     * bc250_dxgk_readonly_ddi.c em todo bind e nunca tiveram nome: o valor
     * jamais apareceu no registro e o handle vazava a cada chamada.
     */
    case BC250_PROBE_REBAR_READ_COUNT:
        return L"RebarReadCount";
    case BC250_PROBE_MANIFEST_CONTRACT_VERSION:
        return L"ManifestContractVersion";
    case BC250_PROBE_ADDRESS_STATUS:
        return L"AddressStatus";
    case BC250_PROBE_PSP_STATUS:
        return L"PspStatus";

    /* Schema 134 — anel de rastro de DDI. Hist00 e' o mais recente. */
    case BC250_PROBE_V5_DDI_HIST_00: return L"V5DdiHist00";
    case BC250_PROBE_V5_DDI_HIST_01: return L"V5DdiHist01";
    case BC250_PROBE_V5_DDI_HIST_02: return L"V5DdiHist02";
    case BC250_PROBE_V5_DDI_HIST_03: return L"V5DdiHist03";
    case BC250_PROBE_V5_DDI_HIST_04: return L"V5DdiHist04";
    case BC250_PROBE_V5_DDI_HIST_05: return L"V5DdiHist05";
    case BC250_PROBE_V5_DDI_HIST_06: return L"V5DdiHist06";
    case BC250_PROBE_V5_DDI_HIST_07: return L"V5DdiHist07";
    case BC250_PROBE_V5_DDI_HIST_08: return L"V5DdiHist08";
    case BC250_PROBE_V5_DDI_HIST_09: return L"V5DdiHist09";
    case BC250_PROBE_V5_DDI_HIST_10: return L"V5DdiHist10";
    case BC250_PROBE_V5_DDI_HIST_11: return L"V5DdiHist11";
    case BC250_PROBE_V5_DDI_HIST_12: return L"V5DdiHist12";
    case BC250_PROBE_V5_DDI_HIST_13: return L"V5DdiHist13";
    case BC250_PROBE_V5_DDI_HIST_14: return L"V5DdiHist14";
    case BC250_PROBE_V5_DDI_HIST_15: return L"V5DdiHist15";
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
        /*
         * O ZwClose fica FORA do teste de fieldName.
         *
         * Ate 2026-09-02 ele vivia dentro do bloco, e um campo sem nome
         * mapeado vazava o handle de kernel aberto acima. Nao era teorico:
         * quatro campos gravados por bc250_dxgk_readonly_ddi.c nao tinham
         * case no switch, entao todo bind vazava quatro handles, e cada
         * Bc250ProbeResetAllVolatile vazava mais quatro.
         *
         * Fechar sempre que a chave abriu e a correcao; os quatro cases
         * faltantes foram acrescentados no mesmo lote; e o audit offline
         * passou a exigir cobertura total do enum, para que a classe inteira
         * de defeito reprove no build em vez de vazar em silencio.
         */
        if (NT_SUCCESS(status)) {
            if (fieldName != NULL) {
                Bc250ProbeWriteValue(key, L"Sequence", sequence);
                Bc250ProbeWriteValue(key, L"LastField", (ULONG)Field);
                Bc250ProbeWriteValue(key, L"LastValue", Value);
                Bc250ProbeWriteValue(key, fieldName, Value);
            }
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

/*
 * Rastro de DDI. Tres marcas em janela deslizante: basta para reconstruir a
 * ordem das ultimas chamadas antes de o dxgkrnl desistir, e e' barato o
 * suficiente para ficar ligado em qualquer DDI, inclusive as de alta
 * frequencia como ControlInterrupt.
 */
static volatile LONG g_Bc250V5LastDdi;
static volatile LONG g_Bc250V5PrevDdi;
static volatile LONG g_Bc250V5Prev2Ddi;
static volatile LONG g_Bc250V5DdiMarks;

/*
 * Anel de 16 marcas (schema 134). A janela de tres slots do 3.79 devolveu
 * "58, 1, 58" e nao permitiu reconstruir o carregamento inteiro. O anel
 * guarda apenas TRANSICOES, pela mesma razao da supressao abaixo.
 */
static volatile LONG g_Bc250V5DdiHistory[BC250_V5_DDI_HISTORY_COUNT];
static volatile LONG g_Bc250V5DdiHistoryIndex;

/* Bitmap de DXGKQAITYPE perguntados. Lo = 0..31, Hi = 32..63. */
static volatile LONG g_Bc250V5QaiTypeSeenLo;
static volatile LONG g_Bc250V5QaiTypeSeenHi;

/*
 * Zera os acumuladores do schema 134 que vivem em memoria.
 *
 * A varredura do enum em Bc250ProbeResetAllVolatile zera o REGISTRO, nunca a
 * variavel estatica que o alimenta: sem isto, um bitmap de QAI do
 * carregamento anterior voltaria a ser publicado no carregamento seguinte.
 * Laco em vez de RtlZeroMemory porque o array e' volatile (C4090 sob /WX).
 */
static VOID Bc250ProbeResetSchema134State(VOID)
{
    ULONG i;

    for (i = 0u; i < BC250_V5_DDI_HISTORY_COUNT; ++i) {
        InterlockedExchange(&g_Bc250V5DdiHistory[i], 0);
    }
    InterlockedExchange(&g_Bc250V5DdiHistoryIndex, 0);
    InterlockedExchange(&g_Bc250V5QaiTypeSeenLo, 0);
    InterlockedExchange(&g_Bc250V5QaiTypeSeenHi, 0);
}

VOID Bc250ProbeMarkDdi(ULONG DdiId)
{
    LONG previous = InterlockedExchange(&g_Bc250V5LastDdi, (LONG)DdiId);

    /*
     * Repeticao nao desloca a janela: ControlInterrupt sozinho apagaria o
     * rastro util em poucos milissegundos.
     */
    if (previous != (LONG)DdiId) {
        LONG older = InterlockedExchange(&g_Bc250V5PrevDdi, previous);
        LONG slot = InterlockedIncrement(&g_Bc250V5DdiHistoryIndex) - 1;
        InterlockedExchange(&g_Bc250V5Prev2Ddi, older);
        InterlockedExchange(
            &g_Bc250V5DdiHistory[((ULONG)slot) % BC250_V5_DDI_HISTORY_COUNT],
            (LONG)DdiId);
    }
    InterlockedIncrement(&g_Bc250V5DdiMarks);
}

VOID Bc250ProbeMarkQaiType(ULONG Type)
{
    /*
     * O enum DXGK_QUERYADAPTERINFOTYPE do WDK 10.0.26100 termina em 48.
     * Qualquer coisa acima de 63 nao cabe no bitmap e e' descartada em vez de
     * envenenar um bit alheio.
     */
    if (Type >= 64u) {
        return;
    }
    if (Type < 32u) {
        InterlockedOr(&g_Bc250V5QaiTypeSeenLo, (LONG)(1u << Type));
    } else {
        InterlockedOr(&g_Bc250V5QaiTypeSeenHi, (LONG)(1u << (Type - 32u)));
    }
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_TYPE_SEEN_LO,
        (ULONG)InterlockedCompareExchange(&g_Bc250V5QaiTypeSeenLo, 0, 0));
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_TYPE_SEEN_HI,
        (ULONG)InterlockedCompareExchange(&g_Bc250V5QaiTypeSeenHi, 0, 0));
}

VOID Bc250ProbeSnapshotDdiTrace(VOID)
{
    LONG rawWritten = InterlockedCompareExchange(
        &g_Bc250V5DdiHistoryIndex, 0, 0);
    ULONG written = (rawWritten < 0) ? 0u : (ULONG)rawWritten;
    ULONG age;

    /*
     * Hist00 e' a marca mais recente. Slots ainda nao preenchidos saem em
     * zero explicitamente, para nao herdarem o conteudo do boot anterior.
     * A indexacao por aritmetica exige que os dezesseis campos do enum
     * fiquem contiguos a partir de BC250_PROBE_V5_DDI_HIST_00.
     */
    for (age = 1u; age <= BC250_V5_DDI_HISTORY_COUNT; ++age) {
        ULONG value = 0u;
        if (written >= age) {
            value = (ULONG)g_Bc250V5DdiHistory[
                (written - age) % BC250_V5_DDI_HISTORY_COUNT];
        }
        Bc250ProbeRecord(
            (BC250_PROBE_FIELD)((ULONG)BC250_PROBE_V5_DDI_HIST_00 + (age - 1u)),
            value);
    }

    Bc250ProbeRecord(BC250_PROBE_V5_LAST_DDI, (ULONG)g_Bc250V5LastDdi);
    Bc250ProbeRecord(BC250_PROBE_V5_PREV_DDI, (ULONG)g_Bc250V5PrevDdi);
    Bc250ProbeRecord(BC250_PROBE_V5_PREV2_DDI, (ULONG)g_Bc250V5Prev2Ddi);
    /* DDI_MARKS permanece a ULTIMA gravacao: ha' teste que depende disso. */
    Bc250ProbeRecord(BC250_PROBE_V5_DDI_MARKS, (ULONG)g_Bc250V5DdiMarks);
}

/*
 * Maior marco alcancado depois do StartDevice. Monotonico: um marco menor
 * chegando depois nao rebaixa o valor, para que uma DDI de alta frequencia
 * nao apague a informacao de quao longe o bring-up chegou.
 */
static volatile LONG g_Bc250V5PostStartStage;

VOID Bc250ProbeMarkPostStartStage(ULONG Stage)
{
    LONG desired = (LONG)Stage;
    for (;;) {
        LONG current = InterlockedCompareExchange(
            &g_Bc250V5PostStartStage, 0, 0);
        if (current >= desired) {
            return;
        }
        if (InterlockedCompareExchange(
                &g_Bc250V5PostStartStage, desired, current) == current) {
            return;
        }
    }
}

/* --------------------------------------------------------------------------
   Buffer circular das chamadas de DxgkDdiQueryInterface.
   -------------------------------------------------------------------------- */

static BC250_V5_QI_RECORD g_Bc250V5QiSlots[BC250_V5_QI_SLOT_COUNT];
static volatile LONG g_Bc250V5QiWriteIndex;

VOID Bc250ProbeRecordQueryInterface(const BC250_V5_QI_RECORD *Record)
{
    LONG index;
    ULONG slot;

    if (Record == NULL) {
        return;
    }
    index = InterlockedIncrement(&g_Bc250V5QiWriteIndex);
    slot = ((ULONG)(index - 1)) % BC250_V5_QI_SLOT_COUNT;
    g_Bc250V5QiSlots[slot] = *Record;
    g_Bc250V5QiSlots[slot].Ordinal = (ULONG)index;
}

#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)

static const PCWSTR g_Bc250V5QiSlotNames[BC250_V5_QI_SLOT_COUNT] = {
    L"V5QiSlot00", L"V5QiSlot01", L"V5QiSlot02", L"V5QiSlot03",
    L"V5QiSlot04", L"V5QiSlot05", L"V5QiSlot06", L"V5QiSlot07",
    L"V5QiSlot08", L"V5QiSlot09", L"V5QiSlot10", L"V5QiSlot11",
    L"V5QiSlot12", L"V5QiSlot13", L"V5QiSlot14", L"V5QiSlot15"
};

static VOID Bc250ProbeWriteBinary(
    HANDLE Key, PCWSTR Name, const VOID *Data, ULONG Length)
{
    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, Name);
    (void)ZwSetValueKey(
        Key, &valueName, 0, REG_BINARY, (PVOID)Data, Length);
}

#endif

VOID Bc250ProbeSnapshotQueryInterfaces(VOID)
{
    LONG total = InterlockedCompareExchange(&g_Bc250V5QiWriteIndex, 0, 0);
    ULONG captured = (total < 0) ? 0u : (ULONG)total;

    if (captured > BC250_V5_QI_SLOT_COUNT) {
        captured = BC250_V5_QI_SLOT_COUNT;
    }
    Bc250ProbeRecord(BC250_PROBE_V5_QI_SLOTS_CAPTURED, captured);
    Bc250ProbeRecord(BC250_PROBE_V5_POST_START_STAGE,
        (ULONG)InterlockedCompareExchange(&g_Bc250V5PostStartStage, 0, 0));

#if defined(BC250_ENABLE_PERSISTENT_TELEMETRY)
    {
        OBJECT_ATTRIBUTES attributes;
        HANDLE key = NULL;
        NTSTATUS status;
        ULONG i;

        InitializeObjectAttributes(
            &attributes, (PUNICODE_STRING)&g_Bc250ProbeRegistryPathMcdm,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        status = ZwOpenKey(&key, KEY_SET_VALUE, &attributes);
        if (!NT_SUCCESS(status)) {
            InitializeObjectAttributes(
                &attributes,
                (PUNICODE_STRING)&g_Bc250ProbeRegistryPathReadonly,
                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
            status = ZwOpenKey(&key, KEY_SET_VALUE, &attributes);
        }
        if (!NT_SUCCESS(status)) {
            return;
        }
        /*
         * Publica todos os slots, inclusive os nao preenchidos: um slot
         * zerado precisa aparecer como zerado, e nao herdar o conteudo do
         * boot anterior.
         */
        for (i = 0u; i < BC250_V5_QI_SLOT_COUNT; ++i) {
            Bc250ProbeWriteBinary(key, g_Bc250V5QiSlotNames[i],
                &g_Bc250V5QiSlots[i], (ULONG)sizeof(g_Bc250V5QiSlots[i]));
        }
        ZwClose(key);
    }
#endif
}

VOID Bc250ProbeResetRenderTrace(VOID)
{
    static const BC250_PROBE_FIELD s_renderFields[] = {
        /* Bloco V5: registro e StartDevice. */
        BC250_PROBE_V5_RENDER_NODE_REQUESTED,
        BC250_PROBE_V5_RENDER_NODE_TUNABLE_PRESENT,
        BC250_PROBE_V5_REGISTRATION_MODE,
        BC250_PROBE_V5_START_DEVICE_STATUS,
        BC250_PROBE_V5_START_DEVICE_SOURCES,
        BC250_PROBE_V5_START_DEVICE_CHILDREN,
        BC250_PROBE_V5_ABI_READY_CONTEXT,
        BC250_PROBE_V5_ABI_READY_ALLOCATION,
        BC250_PROBE_V5_ABI_READY_ROOT_PAGE_TABLE,
        BC250_PROBE_V5_ABI_READY_PUBLISH_COUNT,
        /* Bloco V5: QueryAdapterInfo. */
        BC250_PROBE_V5_QAI_CALLS,
        BC250_PROBE_V5_QAI_LAST_TYPE,
        BC250_PROBE_V5_QAI_LAST_STATUS,
        BC250_PROBE_V5_QAI_LAST_UNSUPPORTED_TYPE,
        BC250_PROBE_V5_QAI_UNSUPPORTED_COUNT,
        BC250_PROBE_V5_QAI_TYPE_SEEN_LO,
        BC250_PROBE_V5_QAI_TYPE_SEEN_HI,
        BC250_PROBE_V5_COMPUTE_ONLY_DECLARED,
        /* Bloco V5: rastro de DDI e QueryInterface. */
        BC250_PROBE_V5_LAST_DDI,
        BC250_PROBE_V5_PREV_DDI,
        BC250_PROBE_V5_PREV2_DDI,
        BC250_PROBE_V5_DDI_MARKS,
        BC250_PROBE_V5_DDI_HIST_00,
        BC250_PROBE_V5_DDI_HIST_01,
        BC250_PROBE_V5_DDI_HIST_02,
        BC250_PROBE_V5_DDI_HIST_03,
        BC250_PROBE_V5_DDI_HIST_04,
        BC250_PROBE_V5_DDI_HIST_05,
        BC250_PROBE_V5_DDI_HIST_06,
        BC250_PROBE_V5_DDI_HIST_07,
        BC250_PROBE_V5_DDI_HIST_08,
        BC250_PROBE_V5_DDI_HIST_09,
        BC250_PROBE_V5_DDI_HIST_10,
        BC250_PROBE_V5_DDI_HIST_11,
        BC250_PROBE_V5_DDI_HIST_12,
        BC250_PROBE_V5_DDI_HIST_13,
        BC250_PROBE_V5_DDI_HIST_14,
        BC250_PROBE_V5_DDI_HIST_15,
        BC250_PROBE_V5_QI_CALLS,
        BC250_PROBE_V5_QI_LAST_GUID_D1,
        BC250_PROBE_V5_QI_PREV_GUID_D1,
        BC250_PROBE_V5_QI_PREV2_GUID_D1,
        BC250_PROBE_V5_QI_LAST_VERSION,
        BC250_PROBE_V5_QI_LAST_SIZE,
        BC250_PROBE_V5_QI_SLOTS_CAPTURED,
        BC250_PROBE_V5_POST_START_STAGE,
        BC250_PROBE_V5_CONTRACT_VERSION,
        /* Bloco V5: teardown e quiescencia. */
        BC250_PROBE_STOP_DEVICE_ENTERED,
        BC250_PROBE_STOP_DEVICE_STATUS,
        BC250_PROBE_REMOVE_DEVICE_ENTERED,
        BC250_PROBE_REMOVE_DEVICE_STATUS,
        BC250_PROBE_V5_STOP_FENCE_QUERY_MODE,
        BC250_PROBE_V5_STOP_FENCE_SYNC_STATUS,
        BC250_PROBE_V5_STOP_FENCE_SYNC_RESULT,
        BC250_PROBE_V5_STOP_FENCE_PENDING_COUNT,
        BC250_PROBE_V5_STOP_FENCE_RESERVED_COUNT,
        BC250_PROBE_V5_STOP_REASON,
        BC250_PROBE_V5_STOP_TIMER_DRAINED,
        BC250_PROBE_V5_STOP_DPC_DRAINED,
        BC250_PROBE_V5_STOP_SUBMIT_INFLIGHT,
        BC250_PROBE_V5_STOP_DRAIN_ITERATIONS,
        BC250_PROBE_V5_STOP_SNAPSHOT_OK,
        BC250_PROBE_V5_STOP_LOCK_FAILURES,
        BC250_PROBE_V5_STOP_REJECTED_WHILE_STOPPING,
        BC250_PROBE_V5_STOP_FIRST_STATUS,
        BC250_PROBE_V5_LIFECYCLE_STATE,
        BC250_PROBE_V5_CHILD_RELATIONS_STATUS,
        BC250_PROBE_V5_CHILD_RELATIONS_COUNT,
        BC250_PROBE_V5_CHILD_RELATIONS_CALLS,
        /*
         * Bloco DMM. Enumerado a mao de proposito: a faixa do enum nao e'
         * limpa (BC250_PROBE_SKIP_HW_BRINGUP mora no meio dela e ecoa um
         * tunable, nao uma medicao — zera-lo produziria evidencia errada).
         */
        BC250_PROBE_DMM_QUERY_ADAPTER_TYPE,
        BC250_PROBE_DMM_CHILD_RELATIONS_CALLED,
        BC250_PROBE_DMM_CHILD_STATUS_CALLED,
        BC250_PROBE_DMM_DESCRIPTOR_CALLED,
        BC250_PROBE_DMM_VIDPN_QUERY_CALLED,
        BC250_PROBE_DMM_QAI_OUTSIZE,
        BC250_PROBE_DMM_QAI_STATUS,
        BC250_PROBE_DMM_DISPLAY_MAPPED,
        BC250_PROBE_DMM_VSYNC_COUNT,
        BC250_PROBE_DMM_IS_SUPPORTED_COUNT,
        BC250_PROBE_DMM_IS_SUPPORTED_STATUS,
        BC250_PROBE_DMM_IS_SUPPORTED_RESULT,
        BC250_PROBE_DMM_COMMIT_COUNT,
        BC250_PROBE_DMM_COMMIT_STATUS,
        BC250_PROBE_DMM_COMMIT_HAS_PATH,
        BC250_PROBE_DMM_COMMIT_SUPPORTED,
        BC250_PROBE_DMM_CONTROL_INTERRUPT_COUNT,
        BC250_PROBE_DMM_CONTROL_INTERRUPT_TYPE,
        BC250_PROBE_DMM_CONTROL_INTERRUPT_ENABLE,
        BC250_PROBE_DMM_PRESENT_ENTERED,
        BC250_PROBE_DMM_PRESENT_STATUS,
        BC250_PROBE_DMM_PRESENT_SOURCE_PITCH
    };
    ULONG i;

    /* Estado em memoria primeiro, para nao republicar valor velho. */
    InterlockedExchange(&g_Bc250V5LastDdi, 0);
    InterlockedExchange(&g_Bc250V5PrevDdi, 0);
    InterlockedExchange(&g_Bc250V5Prev2Ddi, 0);
    InterlockedExchange(&g_Bc250V5DdiMarks, 0);
    InterlockedExchange(&g_Bc250V5PostStartStage, 0);
    InterlockedExchange(&g_Bc250V5QiWriteIndex, 0);
    RtlZeroMemory(g_Bc250V5QiSlots, sizeof(g_Bc250V5QiSlots));
    Bc250ProbeResetSchema134State();

    for (i = 0u; i < (ULONG)RTL_NUMBER_OF(s_renderFields); ++i) {
        Bc250ProbeRecord(s_renderFields[i], 0u);
    }
    /* Os slots binarios voltam a zero no mesmo carregamento. */
    Bc250ProbeSnapshotQueryInterfaces();
}

VOID Bc250ProbeResetAllVolatile(VOID)
{
    BC250_PROBE_FIELD field;

    /*
     * Estado em memoria primeiro, para nao republicar valor velho durante a
     * propria varredura.
     */
    InterlockedExchange(&g_Bc250V5LastDdi, 0);
    InterlockedExchange(&g_Bc250V5PrevDdi, 0);
    InterlockedExchange(&g_Bc250V5Prev2Ddi, 0);
    InterlockedExchange(&g_Bc250V5DdiMarks, 0);
    InterlockedExchange(&g_Bc250V5PostStartStage, 0);
    InterlockedExchange(&g_Bc250V5QiWriteIndex, 0);
    RtlZeroMemory(g_Bc250V5QiSlots, sizeof(g_Bc250V5QiSlots));
    Bc250ProbeResetSchema134State();

    /*
     * Varredura integral. Nao ha' lista de campos a manter em dia: qualquer
     * campo novo entra no reset automaticamente por estar no enum.
     */
    for (field = BC250_PROBE_DRIVER_ENTRY_REACHED;
         field < BC250_PROBE_FIELD_COUNT;
         field = (BC250_PROBE_FIELD)((ULONG)field + 1u)) {
        Bc250ProbeRecord(field, 0u);
    }

    Bc250ProbeSnapshotQueryInterfaces();
}

VOID Bc250ProbeResetSubmissionEvidence(VOID)
{
    BC250_PROBE_FIELD field;

    /*
     * O intervalo de BC250_PROBE_P1_ENGINE_STATE ate
     * BC250_PROBE_P2_DOORBELL_HIT_OBSERVED e' contiguo e contem apenas campos
     * P1/P2 — conferido sobre o enum. Zerar por varredura mantem o reset
     * automaticamente completo quando campos novos forem inseridos no bloco.
     */
    for (field = BC250_PROBE_P1_ENGINE_STATE;
         field <= BC250_PROBE_P2_DOORBELL_HIT_OBSERVED;
         field = (BC250_PROBE_FIELD)((ULONG)field + 1u)) {
        Bc250ProbeRecord(field, 0u);
    }
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
