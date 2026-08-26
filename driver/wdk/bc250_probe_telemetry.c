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
