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
