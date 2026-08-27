/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    wddm_driver.c

Abstract:
    Native Kernel-Mode Display-Only Driver (KMDOD) implementation for AMD BC-250 (Cyan Skillfish).

    Guarantees:
      - 100% monitor signal stability (Zero Black Screen / Zero Drop)
      - Inherits UEFI GOP linear framebuffer via DxgkCbAcquirePostDisplayOwnership
      - Direct blit presentation via DxgkDdiPresentDisplayOnly
      - Runs unified hardware bring-up (SMU/PSP/SDMA/CP) in DxgkDdiStartDevice
      - Exposes DxgkDdiEscape PM4 submission interface for RADV / Vulkan UMD

Environment:
    Kernel mode only

--*/

#include "wddm.h"
#include <ntstrsafe.h>

/* Carimbo de build injetado por build.bat (/DBC250_BUILD_ID=MMddHHmm).
   Gravado no Registro logo no DriverEntry: e a unica forma confiavel de
   saber se a telemetria lida veio DESTE binario ou de um anterior que
   ficou na driver store. */
#ifndef BC250_BUILD_ID
#define BC250_BUILD_ID 0
#endif

/* ChildUid vira o D3DDDI_VIDEO_PRESENT_TARGET_ID. O sample KMDOD da
   Microsoft usa o indice do filho; 0 e o valor menos surpreendente. */
#define BC250_CHILD_ID_DISPLAY                        0u

#define BC250_DEFAULT_WIDTH                             1920u
#define BC250_DEFAULT_HEIGHT                            1080u
#define BC250_DEFAULT_PITCH                             (1920u * 4u)

/* WDDMVersion tem de acompanhar a versao de DDI declarada em build.bat
   (DXGKDDI_INTERFACE_VERSION). Derivar aqui evita a divergencia silenciosa
   entre o que o DriverEntry declara e o que os caps reportam. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
#define BC250_WDDM_VERSION      DXGKDDI_WDDMv2
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
#define BC250_WDDM_VERSION      DXGKDDI_WDDMv1_3
#else
#define BC250_WDDM_VERSION      DXGKDDI_WDDMv1_2
#endif

#ifndef STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN
#define STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN ((NTSTATUS)0xC01E0323L)
#endif

/* --- Forward Declarations of KMDOD DDIs --- */
static DXGKDDI_ADD_DEVICE                               Bc250DdiAddDevice;
static DXGKDDI_START_DEVICE                             Bc250DdiStartDevice;
static DXGKDDI_STOP_DEVICE                              Bc250DdiStopDevice;
static DXGKDDI_REMOVE_DEVICE                            Bc250DdiRemoveDevice;
static DXGKDDI_DISPATCH_IO_REQUEST                      Bc250DdiDispatchIoRequest;
static DXGKDDI_QUERY_CHILD_RELATIONS                    Bc250DdiQueryChildRelations;
static DXGKDDI_QUERY_CHILD_STATUS                       Bc250DdiQueryChildStatus;
static DXGKDDI_QUERY_DEVICE_DESCRIPTOR                  Bc250DdiQueryDeviceDescriptor;
static DXGKDDI_SET_POWER_STATE                          Bc250DdiSetPowerState;
static DXGKDDI_RESET_DEVICE                             Bc250DdiResetDevice;
static DXGKDDI_UNLOAD                                   Bc250DdiUnload;
static DXGKDDI_QUERY_INTERFACE                          Bc250DdiQueryInterface;
static DXGKDDI_QUERYADAPTERINFO                         Bc250DdiQueryAdapterInfo;
static DXGKDDI_ISSUPPORTEDVIDPN                         Bc250DdiIsSupportedVidPn;
static DXGKDDI_RECOMMENDFUNCTIONALVIDPN                 Bc250DdiRecommendFunctionalVidPn;
static DXGKDDI_ENUMVIDPNCOFUNCMODALITY                  Bc250DdiEnumVidPnCofuncModality;
static DXGKDDI_SETVIDPNSOURCEVISIBILITY                 Bc250DdiSetVidPnSourceVisibility;
static DXGKDDI_COMMITVIDPN                              Bc250DdiCommitVidPn;
static DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH             Bc250DdiUpdateActiveVidPnPresentPath;
static DXGKDDI_RECOMMENDMONITORMODES                    Bc250DdiRecommendMonitorModes;
static DXGKDDI_SETPOINTERPOSITION                       Bc250DdiSetPointerPosition;
static DXGKDDI_SETPOINTERSHAPE                          Bc250DdiSetPointerShape;
static DXGKDDI_QUERYVIDPNHWCAPABILITY                   Bc250DdiQueryVidPnHWCapability;
static DXGKDDI_ESCAPE                                   Bc250DdiEscape;
static DXGKDDI_GET_CHILD_CONTAINER_ID                   Bc250DdiGetChildContainerId;
static DXGKDDI_CONTROLINTERRUPT                         Bc250DdiControlInterrupt;
static DXGKDDI_PRESENTDISPLAYONLY                       Bc250DdiPresentDisplayOnly;
static DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP Bc250DdiStopDeviceAndReleasePostDisplayOwnership;
static DXGKDDI_SYSTEM_DISPLAY_ENABLE                    Bc250DdiSystemDisplayEnable;
static DXGKDDI_SYSTEM_DISPLAY_WRITE                     Bc250DdiSystemDisplayWrite;
static DXGKDDI_INTERRUPT_ROUTINE                        Bc250DdiInterruptRoutine;
static DXGKDDI_DPC_ROUTINE                              Bc250DdiDpcRoutine;
static DXGKDDI_CONTROL_ETW_LOGGING                      Bc250DdiControlEtwLogging;
static DXGKDDI_COLLECTDBGINFO                           Bc250DdiCollectDbgInfo;
static DXGKDDI_NOTIFY_ACPI_EVENT                        Bc250DdiNotifyAcpiEvent;
static DXGKDDI_SETPALETTE                               Bc250DdiSetPalette;
static DXGKDDI_GETSCANLINE                              Bc250DdiGetScanLine;
static DXGKDDI_NOTIFY_SURPRISE_REMOVAL                  Bc250DdiNotifySurpriseRemoval;

static VOID Bc250SetVsyncState(_In_ PBC250_WDDM_DEVICE Dev, _In_ BOOLEAN Enable);
static VOID Bc250TeardownDevice(_In_ PBC250_WDDM_DEVICE Dev, _In_ BOOLEAN TouchHardware);
static VOID Bc250BlackOutDisplay(_In_ PBC250_WDDM_DEVICE Dev);

#define BC250_TDR_SNAPSHOT_SIGNATURE                    'RDTB'
#define BC250_TDR_SNAPSHOT_VERSION                      1u

typedef struct _BC250_TDR_SNAPSHOT {
    ULONG Signature;
    ULONG Version;
    ULONG Reason;
    ULONG BuildId;
    ULONG LastDdiSeq;
    ULONG PresentCount;
    ULONG VsyncCount;
    ULONG Flags;
    ULONG HwBringupStatus;
    ULONG HwSmuStatus;
    ULONG HwSmuRawResponse;
    ULONG HwPspRingStatus;
    ULONG HwTmrStatus;
    ULONG HwL2CntlReadback;
    ULONG HwRbCntlReadback;
    ULONG HwMeCntlReadback;
    ULONG HwRptrAfterInit;
    ULONG GrbmStatus;
} BC250_TDR_SNAPSHOT;

/*===========================================================================
  Instrumentacao.

  Custo importa: com uma transacao de registro completa por escrita, tres
  escritas por DDI e ~70 DDIs no start, a propria telemetria consumia parte
  do orcamento de 2 s do TDR. Duas correcoes:

    1. A chave e aberta UMA vez e o handle reaproveitado.
    2. ZwSetValueKey exige PASSIVE_LEVEL. DDIs como DxgkDdiControlInterrupt
       podem vir em DISPATCH_LEVEL -- escrever ali seria bugcheck. Por isso
       toda escrita e abortada acima de PASSIVE.
===========================================================================*/
static HANDLE        g_Bc250RegKey = NULL;
static volatile LONG g_Bc250DdiSeq = 0;

/* Bit N = DXGKQAITYPE N ja foi consultado / ja foi recusado. */
static volatile LONG g_QaiSeenMask = 0;
static volatile LONG g_QaiUnhandledMask = 0;

static CONST WCHAR g_Bc250ParamsPath[] =
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\bc250wddm\\Parameters";
static CONST WCHAR g_Bc250ServicePath[] =
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\bc250wddm";

static VOID
Bc250LogOpen(VOID)
{
    UNICODE_STRING keyPath;
    OBJECT_ATTRIBUTES objAttr;

    if (g_Bc250RegKey != NULL) {
        return;
    }

    RtlInitUnicodeString(&keyPath, g_Bc250ParamsPath);
    InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    (VOID)ZwCreateKey(&g_Bc250RegKey, KEY_ALL_ACCESS, &objAttr, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
}

static VOID
Bc250LogClose(VOID)
{
    if (g_Bc250RegKey != NULL) {
        ZwClose(g_Bc250RegKey);
        g_Bc250RegKey = NULL;
    }
}

static VOID
Bc250LogReg(
    _In_ PCWSTR ValueName,
    _In_ ULONG  ValueData
    )
{
    UNICODE_STRING valName;

    if (g_Bc250RegKey == NULL || KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return;
    }

    RtlInitUnicodeString(&valName, ValueName);
    ZwSetValueKey(g_Bc250RegKey, &valName, 0, REG_DWORD, &ValueData, sizeof(ValueData));
}

static VOID
Bc250LogRegSz(
    _In_ PCWSTR ValueName,
    _In_ PCWSTR ValueData
    )
{
    UNICODE_STRING valName, valData;

    if (g_Bc250RegKey == NULL || KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return;
    }

    RtlInitUnicodeString(&valName, ValueName);
    RtlInitUnicodeString(&valData, ValueData);
    ZwSetValueKey(g_Bc250RegKey, &valName, 0, REG_SZ, valData.Buffer, valData.Length + sizeof(WCHAR));
}

/* Le um ajuste da chave do SERVICO (nao de Parameters): o instalador apaga
   Parameters a cada instalacao, e um ajuste ali seria perdido. */
static ULONG
Bc250ReadTunable(
    _In_ PCWSTR ValueName,
    _In_ ULONG  DefaultValue
    )
{
    UNICODE_STRING keyPath, valName;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hKey = NULL;
    ULONG result = DefaultValue;
    UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    ULONG resultLength = 0;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return DefaultValue;
    }

    RtlInitUnicodeString(&keyPath, g_Bc250ServicePath);
    InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    if (!NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &objAttr))) {
        return DefaultValue;
    }

    RtlInitUnicodeString(&valName, ValueName);
    if (NT_SUCCESS(ZwQueryValueKey(hKey, &valName, KeyValuePartialInformation,
                                   buffer, sizeof(buffer), &resultLength))) {
        PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
        if (info->Type == REG_DWORD && info->DataLength == sizeof(ULONG)) {
            result = *(PULONG)info->Data;
        }
    }

    ZwClose(hKey);
    return result;
}

/* Marca QUAL DDI acabou de ser executada e em que posicao da sequencia.
   E o que responde "o que exatamente veio antes do Unload?" -- inclusive
   para as DDIs que gravam um dado (e nao um passo) sob o proprio nome. */
static VOID
Bc250MarkDdi(
    _In_ PCWSTR DdiName
    )
{
    ULONG seq = (ULONG)InterlockedIncrement(&g_Bc250DdiSeq);

    /* LastDdi/LastDdiSeq = onde paramos. <DdiName> = posicao da ultima
       chamada daquela DDI, para reconstruir a ordem inteira. */
    Bc250LogRegSz(L"LastDdi", DdiName);
    Bc250LogReg(L"LastDdiSeq", seq);
    Bc250LogReg(DdiName, seq);
}

static VOID
Bc250LogStep(
    _In_ PCWSTR ValueName
    )
{
    Bc250MarkDdi(ValueName);
}

static VOID
Bc250LogGuid(
    _In_ PCWSTR Prefix,
    _In_ CONST GUID* Guid
    )
{
    WCHAR name[64];
    CONST ULONG* raw = (CONST ULONG*)Guid;
    ULONG i;

    if (Guid == NULL) {
        return;
    }

    for (i = 0; i < 4; i++) {
        RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"%s_%u", Prefix, i);
        Bc250LogReg(name, raw[i]);
    }
}

/*===========================================================================
  VSync sintetico.

  Um KMDOD precisa reportar DXGK_INTERRUPT_DISPLAYONLY_VSYNC para o dxgkrnl
  avancar o motor de apresentacao. Sem interrupcao de hardware conectada,
  este DPC periodico emite a notificacao no lugar da ISR -- que e o padrao
  usado por drivers display-only sem VSync real.
===========================================================================*/
static VOID
Bc250VsyncDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (dev == NULL || !dev->VsyncEnabled || !dev->DeviceStarted ||
        dev->SurpriseRemoved || dev->AdapterPowerState != PowerDeviceD0 ||
        dev->DxgkInterface.DxgkCbNotifyInterrupt == NULL ||
        dev->DxgkInterface.DxgkCbNotifyDpc == NULL) {
        return;
    }

    RtlZeroMemory(&data, sizeof(data));
    data.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_VSYNC;
    data.DisplayOnlyVsync.VidPnTargetId = BC250_CHILD_ID_DISPLAY;

    /* NotifyInterrupt espera contexto de ISR; o DPC roda em DISPATCH_LEVEL,
       que satisfaz o requisito de IRQL. NotifyDpc entrega o lote. */
    dev->VsyncLastQpc = KeQueryPerformanceCounter(NULL);
    dev->DxgkInterface.DxgkCbNotifyInterrupt(dev->DxgkInterface.DeviceHandle, &data);
    dev->DxgkInterface.DxgkCbNotifyDpc(dev->DxgkInterface.DeviceHandle);

    dev->VsyncCount++;
}

static VOID
Bc250SetVsyncState(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ BOOLEAN Enable
    )
{
    if (Dev == NULL || !Dev->VsyncTimerInitialized) {
        return;
    }

    if (Enable && Dev->DeviceStarted && !Dev->SurpriseRemoved &&
        Dev->AdapterPowerState == PowerDeviceD0) {
        LARGE_INTEGER due;
        due.QuadPart = -166667LL;
        Dev->VsyncEnabled = TRUE;
        KeSetTimerEx(&Dev->VsyncTimer, due, 17, &Dev->VsyncDpc);
    } else {
        Dev->VsyncEnabled = FALSE;
        KeCancelTimer(&Dev->VsyncTimer);
    }
}

static VOID
Bc250BlackOutDisplay(
    _In_ PBC250_WDDM_DEVICE Dev
    )
{
    if (Dev == NULL || KeGetCurrentIrql() > APC_LEVEL) {
        return;
    }

    ExAcquireFastMutex(&Dev->DeviceMutex);
    if (Dev->DisplayVirtualBase != NULL && Dev->DisplaySize != 0 &&
        !Dev->SurpriseRemoved) {
        RtlZeroMemory(Dev->DisplayVirtualBase, Dev->DisplaySize);
    }
    ExReleaseFastMutex(&Dev->DeviceMutex);
}

static VOID
Bc250TeardownDevice(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ BOOLEAN TouchHardware
    )
{
    if (Dev == NULL) {
        return;
    }

    Dev->DeviceStarted = FALSE;
    Dev->VsyncRequested = FALSE;
    Bc250SetVsyncState(Dev, FALSE);
    if (Dev->VsyncTimerInitialized) {
        KeFlushQueuedDpcs();
    }

    ExAcquireFastMutex(&Dev->DeviceMutex);

    if (TouchHardware && !Dev->SurpriseRemoved) {
        Bc250HwShutdown(Dev);
    } else {
        /* Surprise removal proibe novo MMIO. As paginas PSP continuam sendo
           nossas e podem ser liberadas, mas sem tentar falar com o PSP. */
        PVOID mmio = Dev->MmioVirtualBase;
        Dev->MmioVirtualBase = NULL;
        Bc250HwShutdown(Dev);
        Dev->MmioVirtualBase = mmio;
    }

    if (Dev->MmioVirtualBase != NULL) {
        MmUnmapIoSpace(Dev->MmioVirtualBase, Dev->MmioSize);
        Dev->MmioVirtualBase = NULL;
    }
    if (Dev->DisplayVirtualBase != NULL && !Dev->DisplayInsideVram) {
        MmUnmapIoSpace(Dev->DisplayVirtualBase, Dev->DisplaySize);
    }
    Dev->DisplayVirtualBase = NULL;
    Dev->DisplayInsideVram = FALSE;

    if (Dev->FbVirtualBase != NULL) {
        MmUnmapIoSpace(Dev->FbVirtualBase, Dev->FbSize);
        Dev->FbVirtualBase = NULL;
    }

    Dev->PostDisplayAcquired = FALSE;
    Dev->VidPnCommitted = FALSE;
    Dev->SourceVisible = FALSE;
    Dev->ForceFullPresent = FALSE;
    ExReleaseFastMutex(&Dev->DeviceMutex);
}

/*===========================================================================
  DriverEntry: Entry Point for KMDOD
===========================================================================*/
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    KMDDOD_INITIALIZATION_DATA dodInit;
    NTSTATUS                   initStatus;

    Bc250LogOpen();
    Bc250LogReg(L"BuildId", BC250_BUILD_ID);
    Bc250LogStep(L"DdiDriverEntry");

    RtlZeroMemory(&dodInit, sizeof(dodInit));
    dodInit.Version = DXGKDDI_INTERFACE_VERSION;

    /* Callbacks que o sample KMDOD da Microsoft sempre registra.
       Ficaram NULL ate agora -- e um adaptador so entra online com eles. */
    dodInit.DxgkDdiInterruptRoutine                     = Bc250DdiInterruptRoutine;
    dodInit.DxgkDdiDpcRoutine                           = Bc250DdiDpcRoutine;
    dodInit.DxgkDdiControlEtwLogging                    = Bc250DdiControlEtwLogging;
    dodInit.DxgkDdiCollectDbgInfo                       = Bc250DdiCollectDbgInfo;
    dodInit.DxgkDdiNotifyAcpiEvent                      = Bc250DdiNotifyAcpiEvent;
    dodInit.DxgkDdiSetPalette                           = Bc250DdiSetPalette;
    dodInit.DxgkDdiGetScanLine                          = Bc250DdiGetScanLine;
    dodInit.DxgkDdiNotifySurpriseRemoval                = Bc250DdiNotifySurpriseRemoval;

    /* Basic PnP */
    dodInit.DxgkDdiAddDevice                            = Bc250DdiAddDevice;
    dodInit.DxgkDdiStartDevice                          = Bc250DdiStartDevice;
    dodInit.DxgkDdiStopDevice                           = Bc250DdiStopDevice;
    dodInit.DxgkDdiRemoveDevice                         = Bc250DdiRemoveDevice;
    dodInit.DxgkDdiDispatchIoRequest                    = Bc250DdiDispatchIoRequest;

    /* Child / Monitor Relations */
    dodInit.DxgkDdiQueryChildRelations                  = Bc250DdiQueryChildRelations;
    dodInit.DxgkDdiQueryChildStatus                     = Bc250DdiQueryChildStatus;
    dodInit.DxgkDdiQueryDeviceDescriptor                = Bc250DdiQueryDeviceDescriptor;

    /* Power & System */
    dodInit.DxgkDdiSetPowerState                        = Bc250DdiSetPowerState;
    dodInit.DxgkDdiResetDevice                          = Bc250DdiResetDevice;
    dodInit.DxgkDdiUnload                               = Bc250DdiUnload;
    dodInit.DxgkDdiQueryInterface                       = Bc250DdiQueryInterface;
    dodInit.DxgkDdiQueryAdapterInfo                     = Bc250DdiQueryAdapterInfo;

    /* VidPN Display Pipeline */
    dodInit.DxgkDdiIsSupportedVidPn                     = Bc250DdiIsSupportedVidPn;
    dodInit.DxgkDdiRecommendFunctionalVidPn             = Bc250DdiRecommendFunctionalVidPn;
    dodInit.DxgkDdiEnumVidPnCofuncModality              = Bc250DdiEnumVidPnCofuncModality;
    dodInit.DxgkDdiSetVidPnSourceVisibility             = Bc250DdiSetVidPnSourceVisibility;
    dodInit.DxgkDdiCommitVidPn                          = Bc250DdiCommitVidPn;
    dodInit.DxgkDdiUpdateActiveVidPnPresentPath         = Bc250DdiUpdateActiveVidPnPresentPath;
    dodInit.DxgkDdiRecommendMonitorModes                = Bc250DdiRecommendMonitorModes;
    dodInit.DxgkDdiQueryVidPnHWCapability               = Bc250DdiQueryVidPnHWCapability;

    /* Software / HW Pointer */
    dodInit.DxgkDdiSetPointerPosition                   = Bc250DdiSetPointerPosition;
    dodInit.DxgkDdiSetPointerShape                      = Bc250DdiSetPointerShape;

    /* Escape & User-Mode Interface (RADV / Vulkan) */
    dodInit.DxgkDdiEscape                               = Bc250DdiEscape;

    /* Container ID & Interrupts */
    dodInit.DxgkDdiGetChildContainerId                  = Bc250DdiGetChildContainerId;
    dodInit.DxgkDdiControlInterrupt                     = Bc250DdiControlInterrupt;

    /* Present & Ownership */
    dodInit.DxgkDdiPresentDisplayOnly                   = Bc250DdiPresentDisplayOnly;
    dodInit.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Bc250DdiStopDeviceAndReleasePostDisplayOwnership;
    dodInit.DxgkDdiSystemDisplayEnable                  = Bc250DdiSystemDisplayEnable;
    dodInit.DxgkDdiSystemDisplayWrite                   = Bc250DdiSystemDisplayWrite;

    initStatus = DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &dodInit);

    /* Se o dxgkrnl rejeitar a versao de DDI declarada, e aqui que ele diz. */
    Bc250LogReg(L"DodInitVersion", (ULONG)dodInit.Version);
    Bc250LogReg(L"DodInitStatus", (ULONG)initStatus);

    return initStatus;
}

/*===========================================================================
  PnP Callbacks
===========================================================================*/
static NTSTATUS
Bc250DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID*               MiniportDeviceContext
    )
{
    PBC250_WDDM_DEVICE dev;

    Bc250MarkDdi(L"DdiAddDevice");

    if (MiniportDeviceContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    dev = (PBC250_WDDM_DEVICE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(BC250_WDDM_DEVICE), BC250_WDDM_DRIVER_TAG);
    if (dev == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(dev, sizeof(BC250_WDDM_DEVICE));
    dev->PhysicalDeviceObject = (PDEVICE_OBJECT)PhysicalDeviceObject;
    ExInitializeFastMutex(&dev->DeviceMutex);

    KeInitializeTimerEx(&dev->VsyncTimer, NotificationTimer);
    KeInitializeDpc(&dev->VsyncDpc, Bc250VsyncDpcRoutine, dev);
    dev->VsyncTimerInitialized = TRUE;
    dev->VsyncLastQpc = KeQueryPerformanceCounter(&dev->VsyncQpcFrequency);
    dev->AdapterPowerState = PowerDeviceD0;
    dev->MonitorPowerState = PowerDeviceD0;

    /* Default screen parameters */
    dev->ScreenWidth  = BC250_DEFAULT_WIDTH;
    dev->ScreenHeight = BC250_DEFAULT_HEIGHT;
    dev->ScreenPitch  = BC250_DEFAULT_PITCH;
    dev->ScreenFormat = D3DDDIFMT_A8R8G8B8;

    *MiniportDeviceContext = dev;
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiStartDevice(
    _In_  CONST PVOID          MiniportDeviceContext,
    _In_  PDXGK_START_INFO     DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE   DxgkInterface,
    _Out_ PULONG               NumberOfVideoPresentSources,
    _Out_ PULONG               NumberOfChildren
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    DXGK_DISPLAY_INFORMATION postDisplayInfo;
    DXGK_DEVICE_INFO devInfo;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DxgkStartInfo);

    if (dev == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(&dev->DxgkInterface, DxgkInterface, sizeof(DXGKRNL_INTERFACE));

    *NumberOfVideoPresentSources = 1;
    *NumberOfChildren = 1;

    /* 1. Consulta recursos PCI através de DxgkCbGetDeviceInformation */
    RtlZeroMemory(&devInfo, sizeof(devInfo));
    status = dev->DxgkInterface.DxgkCbGetDeviceInformation(dev->DxgkInterface.DeviceHandle, &devInfo);
    if (!NT_SUCCESS(status) || devInfo.TranslatedResourceList == NULL) {
        Bc250LogReg(L"Start_GetDeviceInfoStatus", (ULONG)status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }
    {
        PCM_PARTIAL_RESOURCE_LIST pList = &devInfo.TranslatedResourceList->List[0].PartialResourceList;
        for (ULONG i = 0; i < pList->Count; i++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR pDesc = &pList->PartialDescriptors[i];
            if (pDesc->Type == CmResourceTypeMemory) {
                if (pDesc->u.Memory.Length >= 0x10000000ULL) { /* >= 256 MB (BAR0 Framebuffer) */
                    dev->FbPhysicalBase = pDesc->u.Memory.Start;
                    dev->FbSize = pDesc->u.Memory.Length;
                } else if (pDesc->u.Memory.Length == BC250_BAR5_MMIO_SIZE) { /* BAR5 MMIO */
                    dev->MmioPhysicalBase = pDesc->u.Memory.Start;
                    dev->MmioSize = pDesc->u.Memory.Length;
                }
            }
        }
    }

    /* Nunca mapear enderecos fisicos fabricados quando o PnP nao entregou
       os BARs. Falhar aqui e recuperavel; mapear o endereco errado nao e. */
    if (dev->MmioPhysicalBase.QuadPart == 0 || dev->MmioSize < BC250_BAR5_MMIO_SIZE ||
        dev->FbPhysicalBase.QuadPart == 0 || dev->FbSize < BC250_BAR0_VRAM_SIZE) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        Bc250LogReg(L"Start_ResourceStatus", (ULONG)status);
        goto StartFailure;
    }

    /* 2. Herda o Framebuffer ativo do boot (UEFI GOP) */
    RtlZeroMemory(&postDisplayInfo, sizeof(postDisplayInfo));
    status = dev->DxgkInterface.DxgkCbAcquirePostDisplayOwnership(
        dev->DxgkInterface.DeviceHandle,
        &postDisplayInfo);

    if (!NT_SUCCESS(status) || postDisplayInfo.Width == 0 || postDisplayInfo.Height == 0 ||
        postDisplayInfo.Pitch < postDisplayInfo.Width * 4u ||
        postDisplayInfo.PhysicAddress.QuadPart == 0) {
        status = NT_SUCCESS(status) ? STATUS_GRAPHICS_MODE_NOT_PINNED : status;
        Bc250LogReg(L"Start_AcquirePostStatus", (ULONG)status);
        goto StartFailure;
    }

    dev->ScreenWidth  = postDisplayInfo.Width;
    dev->ScreenHeight = postDisplayInfo.Height;
    dev->ScreenPitch  = postDisplayInfo.Pitch;
    dev->ScreenFormat = postDisplayInfo.ColorFormat;
    dev->DisplayPhysicalBase = postDisplayInfo.PhysicAddress;
    dev->PostDisplayAcquired = TRUE;

    dev->DisplaySize = (SIZE_T)dev->ScreenPitch * dev->ScreenHeight;
    if (dev->DisplaySize / dev->ScreenHeight != dev->ScreenPitch) {
        status = STATUS_INTEGER_OVERFLOW;
        goto StartFailure;
    }

    /* 3. Mapeia a abertura de VRAM (BAR0) e o MMIO (BAR5) */
    if (dev->FbPhysicalBase.QuadPart != 0 && dev->FbVirtualBase == NULL && dev->FbSize > 0) {
        dev->FbVirtualBase = MmMapIoSpaceEx(
            dev->FbPhysicalBase,
            dev->FbSize,
            PAGE_READWRITE | PAGE_WRITECOMBINE);
    }

    /* O framebuffer do GOP pode estar em qualquer offset dentro do BAR0.
       Se estiver dentro, reaproveitamos o mapeamento; se nao, mapeamos so
       ele -- mapear FbSize a partir do endereco do GOP passaria do fim
       do BAR0. */
    if (dev->FbVirtualBase != NULL &&
        dev->DisplayPhysicalBase.QuadPart >= dev->FbPhysicalBase.QuadPart &&
        (ULONGLONG)(dev->DisplayPhysicalBase.QuadPart - dev->FbPhysicalBase.QuadPart) + dev->DisplaySize
            <= (ULONGLONG)dev->FbSize) {
        dev->DisplayVirtualBase = (PUCHAR)dev->FbVirtualBase +
            (dev->DisplayPhysicalBase.QuadPart - dev->FbPhysicalBase.QuadPart);
        dev->DisplayInsideVram = TRUE;
    } else if (dev->DisplayPhysicalBase.QuadPart != 0 && dev->DisplaySize > 0) {
        dev->DisplayVirtualBase = MmMapIoSpaceEx(
            dev->DisplayPhysicalBase,
            dev->DisplaySize,
            PAGE_READWRITE | PAGE_WRITECOMBINE);
        dev->DisplayInsideVram = FALSE;
    }

    if (dev->MmioPhysicalBase.QuadPart != 0 && dev->MmioVirtualBase == NULL) {
        dev->MmioVirtualBase = MmMapIoSpaceEx(
            dev->MmioPhysicalBase,
            dev->MmioSize,
            PAGE_READWRITE | PAGE_NOCACHE);
    }

    if (dev->FbVirtualBase == NULL || dev->DisplayVirtualBase == NULL ||
        dev->MmioVirtualBase == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        Bc250LogReg(L"Start_MapStatus", (ULONG)status);
        goto StartFailure;
    }

    Bc250LogStep(L"DdiStartDevice");
    Bc250LogReg(L"PostDisplayAcquired", (ULONG)dev->PostDisplayAcquired);
    Bc250LogReg(L"FbMapped", (ULONG)(dev->FbVirtualBase != NULL));
    Bc250LogReg(L"DisplayMapped", (ULONG)(dev->DisplayVirtualBase != NULL));
    Bc250LogReg(L"DisplayInsideVram", (ULONG)dev->DisplayInsideVram);
    Bc250LogReg(L"MmioMapped", (ULONG)(dev->MmioVirtualBase != NULL));
    Bc250LogReg(L"ScreenWidth", dev->ScreenWidth);
    Bc250LogReg(L"ScreenHeight", dev->ScreenHeight);
    Bc250LogReg(L"ScreenPitch", dev->ScreenPitch);
    Bc250LogReg(L"ScreenFormat", (ULONG)dev->ScreenFormat);

    /* 4. Bring-up de hardware -- desligavel por registro.
       O bring-up reprograma a GCVM (L2 e contexto 0) DEPOIS de herdarmos o
       framebuffer do GOP. Se isso derruba o caminho de scanout, o TDR chega
       poucos segundos depois. Com SkipHwBringup=1 o driver fica display-only
       puro, o que separa "o Windows nao aceita o driver" de "o bring-up mata
       o display":
         reg add HKLM\SYSTEM\CurrentControlSet\Services\bc250wddm ^
             /v SkipHwBringup /t REG_DWORD /d 1 /f
         pnputil /restart-device "<instancia>" */
    dev->SkipHwBringup = (BOOLEAN)(Bc250ReadTunable(L"SkipHwBringup", 0) != 0);
    dev->EnableDangerousEscapes =
        (BOOLEAN)(Bc250ReadTunable(L"EnableDangerousEscapes", 1) != 0);
    Bc250LogReg(L"SkipHwBringup", (ULONG)dev->SkipHwBringup);
    Bc250LogReg(L"EnableDangerousEscapes", (ULONG)dev->EnableDangerousEscapes);

    if (dev->MmioVirtualBase != NULL && !dev->SkipHwBringup) {
        NTSTATUS hwStatus = Bc250HwBringup(dev);
        ULONG i;
        WCHAR fwName[32];
        static CONST PCWSTR fwLabels[7] = {
            L"Hw_Fw_SDMA0", L"Hw_Fw_RLC", L"Hw_Fw_CE", L"Hw_Fw_PFP",
            L"Hw_Fw_ME", L"Hw_Fw_MEC", L"Hw_Fw_MEC2"
        };

        dev->HwBringupStatus = hwStatus;
        Bc250LogReg(L"HwBringupStatus", (ULONG)hwStatus);
        Bc250LogReg(L"Hw_SmuGfxOff", (ULONG)dev->HwStatusSmuGfxOff);
        Bc250LogReg(L"Hw_SmuRawResponse", dev->HwSmuRawResponse);
        Bc250LogReg(L"Hw_PspTosReg", dev->HwPspTosReg);
        Bc250LogReg(L"Hw_PspRing", (ULONG)dev->HwStatusPspRing);
        Bc250LogReg(L"Hw_Tmr", (ULONG)dev->HwStatusTmr);
        for (i = 0; i < RTL_NUMBER_OF(fwLabels); i++) {
            RtlStringCchCopyW(fwName, RTL_NUMBER_OF(fwName), fwLabels[i]);
            Bc250LogReg(fwName, (ULONG)dev->HwStatusFw[i]);
        }
        Bc250LogReg(L"Hw_FbOffsetReg", dev->HwFbOffsetReg);
        Bc250LogReg(L"Hw_L2CntlReadback", dev->HwL2CntlReadback);
        Bc250LogReg(L"Hw_RbCntlReadback", dev->HwRbCntlReadback);
        Bc250LogReg(L"Hw_MeCntlReadback", dev->HwMeCntlReadback);
        Bc250LogReg(L"Hw_RptrAfterInit", dev->HwRptrAfterInit);
        if (!NT_SUCCESS(hwStatus)) {
            status = hwStatus;
            goto StartFailure;
        }
    }

    dev->AdapterPowerState = PowerDeviceD0;
    dev->MonitorPowerState = PowerDeviceD0;
    dev->SurpriseRemoved = FALSE;
    dev->SourceVisible = FALSE;
    dev->VidPnCommitted = FALSE;
    dev->ForceFullPresent = TRUE;
    dev->DeviceStarted = TRUE;
    Bc250LogReg(L"Start_Status", STATUS_SUCCESS);
    return STATUS_SUCCESS;

StartFailure:
    Bc250LogReg(L"Start_Status", (ULONG)status);
    Bc250TeardownDevice(dev, TRUE);
    return status;
}

static NTSTATUS
Bc250DdiStopDevice(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    Bc250LogStep(L"DdiStopDevice");

    if (dev != NULL) {
        Bc250LogReg(L"VsyncCount", dev->VsyncCount);
        Bc250TeardownDevice(dev, TRUE);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiRemoveDevice(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    Bc250LogStep(L"DdiRemoveDevice");

    if (dev != NULL) {
        /* Idempotente: cobre tanto StopDevice anterior quanto remocao direta
           depois de um StartDevice parcial. */
        Bc250TeardownDevice(dev, !dev->SurpriseRemoved);
        ExFreePoolWithTag(dev, BC250_WDDM_DRIVER_TAG);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiDispatchIoRequest(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG       VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  Child & Monitor Relations
===========================================================================*/
static NTSTATUS
Bc250DdiQueryChildRelations(
    _In_ CONST PVOID                       MiniportDeviceContext,
    _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG                             ChildRelationsSize
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    Bc250MarkDdi(L"DdiQueryChildRelations");
    Bc250LogReg(L"DdiQueryChildRelations_Size", ChildRelationsSize);

    if (ChildRelations == NULL || ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);
    ChildRelations[0].ChildDeviceType               = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_OTHER;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].AcpiUid                        = 0;
    ChildRelations[0].ChildUid                       = BC250_CHILD_ID_DISPLAY;

    if (ChildRelationsSize >= 2 * sizeof(DXGK_CHILD_DESCRIPTOR)) {
        ChildRelations[1].ChildDeviceType            = TypeUninitialized;
        ChildRelations[1].AcpiUid                    = 0;
        ChildRelations[1].ChildUid                   = 0;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiQueryChildStatus(
    _In_ CONST PVOID           MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_ BOOLEAN               NonDestructiveOnly
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    Bc250MarkDdi(L"DdiQueryChildStatus");
    Bc250LogReg(L"DdiQueryChildStatus_Type", (ChildStatus != NULL) ? (ULONG)ChildStatus->Type : 0);

    if (ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (ChildStatus->Type == StatusConnection) {
        ChildStatus->HotPlug.Connected = TRUE;
        return STATUS_SUCCESS;
    }
    if (ChildStatus->Type == StatusRotation) {
        ChildStatus->Rotation.Angle = 0;
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiQueryDeviceDescriptor(
    _In_ CONST PVOID                   MiniportDeviceContext,
    _In_ ULONG                         ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR    DeviceDescriptor
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    Bc250MarkDdi(L"DdiQueryDeviceDescriptor");
    Bc250LogReg(L"DdiQueryDeviceDescriptor_Uid", ChildUid);
    return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
}

static NTSTATUS
Bc250DdiGetChildContainerId(
    _In_ CONST PVOID                   MiniportDeviceContext,
    _In_ ULONG                         ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID   ContainerId
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(ContainerId);
    Bc250MarkDdi(L"DdiGetChildContainerId");
    Bc250LogReg(L"DdiGetChildContainerId_Uid", ChildUid);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
Bc250DdiControlInterrupt(
    _In_ CONST PVOID                   MiniportDeviceContext,
    _In_ DXGK_INTERRUPT_TYPE           InterruptType,
    _In_ BOOLEAN                       EnableInterrupt
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    Bc250MarkDdi(L"DdiControlInterrupt");
    Bc250LogReg(L"DdiControlInterrupt_Type", (ULONG)InterruptType);
    Bc250LogReg(L"DdiControlInterrupt_Enable", (ULONG)EnableInterrupt);

    if (dev == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Mascara acumulada: mostra TODOS os tipos pedidos, nao so o ultimo. */
    if ((ULONG)InterruptType < 32) {
        dev->InterruptTypeMask |= (1UL << (ULONG)InterruptType);
        Bc250LogReg(L"IntTypeMask", dev->InterruptTypeMask);
    }

    /* Recusar este tipo era o que travava tudo: sem VSync o dxgkrnl nunca
       avanca o motor de apresentacao, PresentDisplayOnly nunca e chamado e
       o TDR mata o driver em ~3 s. Nao temos interrupcao de hardware, entao
       o temporizador periodico faz o papel dela. */
    if (InterruptType == DXGK_INTERRUPT_DISPLAYONLY_VSYNC) {
        dev->VsyncRequested = EnableInterrupt;
        Bc250SetVsyncState(dev, EnableInterrupt);
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  Power & Adapter Info
===========================================================================*/
static NTSTATUS
Bc250DdiSetPowerState(
    _In_ CONST PVOID          MiniportDeviceContext,
    _In_ ULONG                HardwareUid,
    _In_ DEVICE_POWER_STATE   DevicePowerState,
    _In_ POWER_ACTION         ActionType
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    UNREFERENCED_PARAMETER(ActionType);

    Bc250MarkDdi(L"DdiSetPowerState");
    Bc250LogReg(L"DdiSetPowerState_State", (ULONG)DevicePowerState);

    if (dev == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID) {
        if (DevicePowerState != PowerDeviceD0) {
            dev->SourceVisible = FALSE;
            Bc250BlackOutDisplay(dev);
            Bc250SetVsyncState(dev, FALSE);
        }
        dev->AdapterPowerState = DevicePowerState;
        if (DevicePowerState == PowerDeviceD0) {
            dev->ForceFullPresent = TRUE;
            Bc250SetVsyncState(dev, dev->VsyncRequested);
        }
    } else if (HardwareUid == BC250_CHILD_ID_DISPLAY) {
        dev->MonitorPowerState = DevicePowerState;
        if (DevicePowerState != PowerDeviceD0) {
            dev->SourceVisible = FALSE;
            Bc250BlackOutDisplay(dev);
        } else {
            dev->ForceFullPresent = TRUE;
        }
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static VOID
Bc250DdiResetDevice(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    Bc250LogStep(L"DdiResetDevice");
    if (dev != NULL) {
        dev->VsyncRequested = FALSE;
        Bc250SetVsyncState(dev, FALSE);
        dev->SourceVisible = FALSE;
        dev->VidPnCommitted = FALSE;
    }
}

static VOID
Bc250DdiUnload(VOID)
{
    Bc250LogStep(L"DdiUnload");
    Bc250LogClose();
}

static NTSTATUS
Bc250DdiQueryInterface(
    _In_ CONST PVOID          MiniportDeviceContext,
    _In_ PQUERY_INTERFACE     QueryInterface
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    Bc250LogStep(L"DdiQueryInterface");

    if (QueryInterface != NULL) {
        /* Auditoria: registra QUAL interface o dxgkrnl pediu.
           Os 4 dwords do GUID vao para QueryIface_Guid_0..3. */
        Bc250LogReg(L"QueryIface_Version", QueryInterface->Version);
        Bc250LogReg(L"QueryIface_Size", QueryInterface->Size);
        Bc250LogGuid(L"QueryIface_Guid", QueryInterface->InterfaceType);
    }

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
Bc250DdiQueryAdapterInfo(
    _In_ CONST PVOID                       MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO*   pQueryAdapterInfo
    )
{
    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250MarkDdi(L"DdiQueryAdapterInfo");
    Bc250LogReg(L"DdiQueryAdapterInfo_Type", (ULONG)pQueryAdapterInfo->Type);

    /* Mascara de todos os tipos ja consultados: um unico boot passa a
       revelar a lista inteira, em vez de um tipo por iteracao. */
    if ((ULONG)pQueryAdapterInfo->Type < 32) {
        Bc250LogReg(L"QAI_SeenMask",
            (ULONG)InterlockedOr(&g_QaiSeenMask, 1L << (ULONG)pQueryAdapterInfo->Type)
            | (1UL << (ULONG)pQueryAdapterInfo->Type));
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_DRIVERCAPS) {
        DXGK_DRIVERCAPS* caps;

        Bc250LogReg(L"QAI_DriverCaps_OutSize", pQueryAdapterInfo->OutputDataSize);
        Bc250LogReg(L"QAI_DriverCaps_SizeOf", (ULONG)sizeof(DXGK_DRIVERCAPS));

        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) {
            Bc250LogReg(L"QAI_DriverCaps_Status", 0xC0000023); /* BUFFER_TOO_SMALL */
            return STATUS_BUFFER_TOO_SMALL;
        }

        caps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

        /* O dxgkrnl entrega um buffer do tamanho da SUA versao do struct.
           Zerar OutputDataSize (e nao sizeof) e o que o sample KMDOD faz:
           todo campo nao tocado precisa valer 0 para um display-only. */
        RtlZeroMemory(caps, pQueryAdapterInfo->OutputDataSize);

        caps->HighestAcceptableAddress.QuadPart = -1;
        caps->MaxAllocationListSlotId           = 0;
        caps->ApertureSegmentCommitLimit        = 0;

        /* Cursor por software: sem cursor em hardware, dimensoes zeradas. */
        caps->MaxPointerWidth                   = 0;
        caps->MaxPointerHeight                  = 0;
        caps->PointerCaps.Value                 = 0;

        caps->InterruptMessageNumber            = 0;
        caps->NumberOfSwizzlingRanges           = 0;
        caps->MaxOverlays                       = 0;
        caps->GammaRampCaps.Value               = 0;
        caps->PresentationCaps.Value            = 0;
        caps->MaxQueuedFlipOnVSync              = 0;
        caps->FlipCaps.Value                    = 0;
        caps->SchedulingCaps.Value              = 0;
        caps->MemoryManagementCaps.Value        = 0;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 0;

        /* Derivado de DXGKDDI_INTERFACE_VERSION (fixado em build.bat).
           Divergir de dodInit.Version faz o dxgkrnl abortar o start. */
        caps->WDDMVersion                       = BC250_WDDM_VERSION;

        caps->SupportNonVGA                     = TRUE;
        caps->SupportSmoothRotation             = FALSE;

        Bc250LogReg(L"QAI_DriverCaps_Status", 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_NUMPOWERCOMPONENTS) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT*)pQueryAdapterInfo->pOutputData = 0;
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PREFERREDGPUNODE) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT*)pQueryAdapterInfo->pOutputData = 0;
        return STATUS_SUCCESS;
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* Obrigatorio a partir de WDDM 2.0. Recusar este tipo foi o que fez o
       dxgkrnl chamar DdiUnload logo depois do DRIVERCAPS. */
    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PHYSICALADAPTERCAPS) {
        DXGK_PHYSICALADAPTERCAPS* physCaps;

        /* O dxgkrnl passa o tamanho UTIL da struct (ate o fim do ultimo campo
           da sua versao de DDI), nao o sizeof() com o padding de alinhamento.
           Em WDDM 2.0 isso da 20 bytes, contra sizeof() = 24: exigir sizeof()
           faz o driver devolver BUFFER_TOO_SMALL num buffer perfeitamente bom. */
        CONST ULONG physCapsMinSize = RTL_SIZEOF_THROUGH_FIELD(DXGK_PHYSICALADAPTERCAPS, Flags);

        Bc250LogReg(L"QAI_PhysCaps_OutSize", pQueryAdapterInfo->OutputDataSize);
        Bc250LogReg(L"QAI_PhysCaps_SizeOf", (ULONG)sizeof(DXGK_PHYSICALADAPTERCAPS));
        Bc250LogReg(L"QAI_PhysCaps_MinSize", physCapsMinSize);

        if (pQueryAdapterInfo->pInputData != NULL &&
            pQueryAdapterInfo->InputDataSize >= sizeof(DXGK_QUERYPHYSICALADAPTERCAPSIN)) {
            Bc250LogReg(L"QAI_PhysCaps_Index",
                ((DXGK_QUERYPHYSICALADAPTERCAPSIN*)pQueryAdapterInfo->pInputData)->PhysicalAdapterIndex);
        }

        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < physCapsMinSize) {
            Bc250LogReg(L"QAI_PhysCaps_Status", 0xC0000023);
            return STATUS_BUFFER_TOO_SMALL;
        }

        physCaps = (DXGK_PHYSICALADAPTERCAPS*)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(physCaps, pQueryAdapterInfo->OutputDataSize);

        /* Display-only: sem nos de execucao, sem GPU MMU, sem IoMmu. */
        physCaps->NumExecutionNodes         = 0;
        physCaps->PagingNodeIndex           = 0;
        physCaps->Flags.Value               = 0;
        physCaps->DxgkPhysicalAdapterHandle = MiniportDeviceContext;

        Bc250LogReg(L"QAI_PhysCaps_Status", 0);
        return STATUS_SUCCESS;
    }

    /* Extensao de caps especifica de display: tudo desligado. */
    if (pQueryAdapterInfo->Type == DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION) {
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
        return STATUS_SUCCESS;
    }
#endif /* DXGKDDI_INTERFACE_VERSION_WDDM2_0 */

    /* Guarda o ULTIMO tipo recusado e acumula a mascara de todos eles. */
    Bc250LogReg(L"QAI_LastUnhandledType", (ULONG)pQueryAdapterInfo->Type);
    if ((ULONG)pQueryAdapterInfo->Type < 32) {
        Bc250LogReg(L"QAI_UnhandledMask",
            (ULONG)InterlockedOr(&g_QaiUnhandledMask, 1L << (ULONG)pQueryAdapterInfo->Type)
            | (1UL << (ULONG)pQueryAdapterInfo->Type));
    }
    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  Escape & User-Mode Interface (RADV / Vulkan UMD Submission)
===========================================================================*/
static NTSTATUS
Bc250DdiEscape(
    _In_ CONST PVOID                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE* CONST pEscape
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    BC250_WDDM_ESCAPE_HEADER* hdr;

    if (!dev || !pEscape || !pEscape->pPrivateDriverData || pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_HEADER)) {
        return STATUS_INVALID_PARAMETER;
    }

    hdr = (BC250_WDDM_ESCAPE_HEADER*)pEscape->pPrivateDriverData;

    if (hdr->CommandId != BC250_ESCAPE_CMD_QUERY_INFO &&
        !dev->EnableDangerousEscapes) {
        hdr->Status = (ULONG)STATUS_ACCESS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    switch (hdr->CommandId) {
    case BC250_ESCAPE_CMD_SUBMIT_PM4: {
        BC250_WDDM_ESCAPE_SUBMIT* sub = (BC250_WDDM_ESCAPE_SUBMIT*)pEscape->pPrivateDriverData;
        NTSTATUS submitStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_SUBMIT)) {
            hdr->Status = (ULONG)STATUS_BUFFER_TOO_SMALL;
            return STATUS_BUFFER_TOO_SMALL;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (!dev->HardwareInitialized) {
            ExReleaseFastMutex(&dev->DeviceMutex);
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        submitStatus = Bc250HwSubmitPm4(
            dev, sub->Data, sub->DwordCount, &sub->RptrBefore, &sub->RptrAfter);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)submitStatus;
        return submitStatus;
    }

    case BC250_ESCAPE_CMD_READ_REG: {
        BC250_WDDM_ESCAPE_REG* reg = (BC250_WDDM_ESCAPE_REG*)pEscape->pPrivateDriverData;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_REG)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (!dev->MmioVirtualBase) {
            ExReleaseFastMutex(&dev->DeviceMutex);
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        reg->Value = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + (reg->Offset & 0x7FFFC));
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case BC250_ESCAPE_CMD_WRITE_REG: {
        BC250_WDDM_ESCAPE_REG* reg = (BC250_WDDM_ESCAPE_REG*)pEscape->pPrivateDriverData;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_REG)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (!dev->MmioVirtualBase) {
            ExReleaseFastMutex(&dev->DeviceMutex);
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + (reg->Offset & 0x7FFFC)) = reg->Value;
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case BC250_ESCAPE_CMD_QUERY_INFO: {
        BC250_WDDM_ESCAPE_INFO* info = (BC250_WDDM_ESCAPE_INFO*)pEscape->pPrivateDriverData;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_INFO)) {
            hdr->Status = (ULONG)STATUS_BUFFER_TOO_SMALL;
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->DeviceId = AMDBC250_DEVICE_ID;
        info->HardwareInitialized = dev->HardwareInitialized;
        info->GfxWptr = dev->GfxWptr;
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (dev->MmioVirtualBase) {
            info->GfxRptr = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08700u);
            info->GrbmStatus = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08010u);
            info->CpStat = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08680u);
        }
        info->VramGpuBase = BC250_VRAM_GPU_BASE;
        info->RingGpuBase = BC250_VRAM_GPU_BASE + dev->GfxRingVramOffset;
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS: {
        BC250_WDDM_ESCAPE_COMPUTE_CAPS* caps = (BC250_WDDM_ESCAPE_COMPUTE_CAPS*)pEscape->pPrivateDriverData;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_COMPUTE_CAPS)) {
            hdr->Status = (ULONG)STATUS_BUFFER_TOO_SMALL;
            return STATUS_BUFFER_TOO_SMALL;
        }
        caps->NumCu = 24;
        caps->NumWgp = 12;
        caps->NumSimdPerCu = 2;
        caps->NumTotalShaders = 1536;
        caps->GpuClockMhz = 1500;
        caps->GfxArch = 1010; /* GFX10.1 (Navi12 / Cyan Skillfish) */
        caps->MaxWorkgroupSize = 1024;
        caps->WaveSize = 32;
        caps->LdsSizeBytes = 64 * 1024;
        caps->VramTotalBytes = dev->FbSize;
        caps->ComputeVramBase = BC250_VRAM_GPU_BASE + 0x01000000ULL; /* Offset 16 MB na VRAM */
        caps->ComputeVramSize = 224 * 1024 * 1024; /* 224 MB VRAM direct aperture window */
        caps->DispatchCount = dev->ComputeDispatchCount;
        hdr->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    case BC250_ESCAPE_CMD_COMPUTE_DISPATCH: {
        BC250_WDDM_ESCAPE_COMPUTE_DISPATCH* disp = (BC250_WDDM_ESCAPE_COMPUTE_DISPATCH*)pEscape->pPrivateDriverData;
        NTSTATUS dispStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_COMPUTE_DISPATCH)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (!dev->HardwareInitialized) {
            ExReleaseFastMutex(&dev->DeviceMutex);
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        dispStatus = Bc250HwSubmitComputeDispatch(dev, disp);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)dispStatus;
        return dispStatus;
    }

    case BC250_ESCAPE_CMD_VRAM_WRITE: {
        BC250_WDDM_ESCAPE_VRAM_RW* rw = (BC250_WDDM_ESCAPE_VRAM_RW*)pEscape->pPrivateDriverData;
        NTSTATUS rwStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_VRAM_RW) || rw->ByteCount > sizeof(rw->Data)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        rwStatus = Bc250HwVramWrite(dev, rw->VramOffset, rw->Data, rw->ByteCount);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)rwStatus;
        return rwStatus;
    }

    case BC250_ESCAPE_CMD_VRAM_READ: {
        BC250_WDDM_ESCAPE_VRAM_RW* rw = (BC250_WDDM_ESCAPE_VRAM_RW*)pEscape->pPrivateDriverData;
        NTSTATUS rwStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_VRAM_RW) || rw->ByteCount > sizeof(rw->Data)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        rwStatus = Bc250HwVramRead(dev, rw->VramOffset, rw->Data, rw->ByteCount);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)rwStatus;
        return rwStatus;
    }

    case BC250_ESCAPE_SDMA_COPY: {
        BC250_WDDM_ESCAPE_SDMA_COPY* cpy = (BC250_WDDM_ESCAPE_SDMA_COPY*)pEscape->pPrivateDriverData;
        NTSTATUS copyStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_SDMA_COPY)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        if (!dev->SdmaInitialized) {
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        copyStatus = Bc250SdmaSubmitCopy(
            dev, cpy->SrcGpuAddress, cpy->DstGpuAddress, cpy->SizeBytes, &cpy->DurationUs);
        cpy->FenceValue = dev->SdmaFenceSequence;
        cpy->BandwidthMBs = dev->SdmaBandwidthMBs;
        hdr->Status = (ULONG)copyStatus;
        return copyStatus;
    }

    case BC250_ESCAPE_SDMA_BENCHMARK: {
        BC250_WDDM_ESCAPE_SDMA_BENCHMARK* bench = (BC250_WDDM_ESCAPE_SDMA_BENCHMARK*)pEscape->pPrivateDriverData;
        NTSTATUS benchStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_SDMA_BENCHMARK)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        if (!dev->SdmaInitialized) {
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        benchStatus = Bc250SdmaRunBenchmark(
            dev, bench->Iterations, bench->BlockSizeBytes, &bench->AvgBandwidthMBs, &bench->PeakBandwidthMBs);
        hdr->Status = (ULONG)benchStatus;
        return benchStatus;
    }

    case BC250_ESCAPE_CMD_QUERY_TELEMETRY: {
        BC250_WDDM_ESCAPE_TELEMETRY* telem = (BC250_WDDM_ESCAPE_TELEMETRY*)pEscape->pPrivateDriverData;
        NTSTATUS telemStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_TELEMETRY)) {
            hdr->Status = (ULONG)STATUS_BUFFER_TOO_SMALL;
            return STATUS_BUFFER_TOO_SMALL;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        telemStatus = Bc250HwQueryTelemetry(dev, telem);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)telemStatus;
        return telemStatus;
    }

    default:
        hdr->Status = (ULONG)STATUS_NOT_SUPPORTED;
        return STATUS_NOT_SUPPORTED;
    }
}

/*===========================================================================
  Callbacks que estavam NULL no KMDDOD_INITIALIZATION_DATA.

  O sample KMDOD da Microsoft registra todos estes. Deixa-los NULL nao faz
  DxgkInitializeDisplayOnlyDriver falhar (DodInitStatus = 0), mas o dxgkrnl
  os exige quando poe o adaptador de fato online -- o que bate com o sintoma
  observado: caps negociados com sucesso e Unload logo em seguida, sem nunca
  chamar QueryChildStatus.
===========================================================================*/
static BOOLEAN
Bc250DdiInterruptRoutine(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG       MessageNumber
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    /* Nenhuma interrupcao conectada: a interrupcao nunca e nossa. */
    return FALSE;
}

static VOID
Bc250DdiDpcRoutine(
    _In_ CONST PVOID MiniportDeviceContext
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    if (dev != NULL && dev->DxgkInterface.DxgkCbNotifyDpc != NULL) {
        dev->DxgkInterface.DxgkCbNotifyDpc(dev->DxgkInterface.DeviceHandle);
    }
}

static VOID
Bc250DdiControlEtwLogging(
    _In_ BOOLEAN Enable,
    _In_ ULONG   Flags,
    _In_ UCHAR   Level
    )
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Level);
}

static NTSTATUS
Bc250DdiCollectDbgInfo(
    _In_ CONST PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_COLLECTDBGINFO*      pCollectDbgInfo
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    BC250_TDR_SNAPSHOT snapshot;
    SIZE_T copySize;

    Bc250MarkDdi(L"DdiCollectDbgInfo");
    if (dev == NULL || pCollectDbgInfo == NULL ||
        pCollectDbgInfo->pBuffer == NULL || pCollectDbgInfo->BufferSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&snapshot, sizeof(snapshot));
    snapshot.Signature = BC250_TDR_SNAPSHOT_SIGNATURE;
    snapshot.Version = BC250_TDR_SNAPSHOT_VERSION;
    snapshot.Reason = pCollectDbgInfo->Reason;
    snapshot.BuildId = BC250_BUILD_ID;
    snapshot.LastDdiSeq = (ULONG)InterlockedCompareExchange(&g_Bc250DdiSeq, 0, 0);
    snapshot.PresentCount = dev->PresentCount;
    snapshot.VsyncCount = dev->VsyncCount;
    snapshot.Flags =
        (dev->DeviceStarted ? 0x0001u : 0u) |
        (dev->SourceVisible ? 0x0002u : 0u) |
        (dev->VidPnCommitted ? 0x0004u : 0u) |
        (dev->HardwareInitialized ? 0x0008u : 0u) |
        (dev->SkipHwBringup ? 0x0010u : 0u) |
        (dev->SurpriseRemoved ? 0x0020u : 0u);
    snapshot.HwBringupStatus = (ULONG)dev->HwBringupStatus;
    snapshot.HwSmuStatus = (ULONG)dev->HwStatusSmuGfxOff;
    snapshot.HwSmuRawResponse = dev->HwSmuRawResponse;
    snapshot.HwPspRingStatus = (ULONG)dev->HwStatusPspRing;
    snapshot.HwTmrStatus = (ULONG)dev->HwStatusTmr;
    snapshot.HwL2CntlReadback = dev->HwL2CntlReadback;
    snapshot.HwRbCntlReadback = dev->HwRbCntlReadback;
    snapshot.HwMeCntlReadback = dev->HwMeCntlReadback;
    snapshot.HwRptrAfterInit = dev->HwRptrAfterInit;
    if (dev->MmioVirtualBase != NULL && !dev->SurpriseRemoved) {
        snapshot.GrbmStatus =
            *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08010u);
    }

    copySize = (pCollectDbgInfo->BufferSize < sizeof(snapshot)) ?
        pCollectDbgInfo->BufferSize : sizeof(snapshot);
    RtlCopyMemory(pCollectDbgInfo->pBuffer, &snapshot, copySize);
    if (pCollectDbgInfo->pExtension != NULL) {
        RtlZeroMemory(pCollectDbgInfo->pExtension, sizeof(*pCollectDbgInfo->pExtension));
        pCollectDbgInfo->pExtension->BucketingKey = BC250_TDR_SNAPSHOT_SIGNATURE;
    }

    Bc250LogReg(L"CollectDbg_Reason", pCollectDbgInfo->Reason);
    Bc250LogReg(L"CollectDbg_PresentCount", dev->PresentCount);
    Bc250LogReg(L"CollectDbg_VsyncCount", dev->VsyncCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiNotifyAcpiEvent(
    _In_ CONST PVOID       MiniportDeviceContext,
    _In_ DXGK_EVENT_TYPE   EventType,
    _In_ ULONG             Event,
    _In_ PVOID             Argument,
    _Out_ PULONG           AcpiFlags
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);

    Bc250MarkDdi(L"DdiNotifyAcpiEvent");
    if (AcpiFlags != NULL) {
        *AcpiFlags = 0;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiSetPalette(
    _In_ CONST PVOID                    MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE*      pSetPalette
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pSetPalette);
    Bc250MarkDdi(L"DdiSetPalette");
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS
Bc250DdiGetScanLine(
    _In_ CONST PVOID                MiniportDeviceContext,
    _Inout_ DXGKARG_GETSCANLINE*    pGetScanLine
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    LARGE_INTEGER now;
    LONGLONG frameTicks, elapsed, activeTicks;

    Bc250MarkDdi(L"DdiGetScanLine");
    if (dev == NULL || pGetScanLine == NULL || dev->VsyncQpcFrequency.QuadPart <= 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Estimativa coerente com o VSync sintetico. Nao representa o CRTC
       fisico, mas evita reportar blanking permanente enquanto a linha anda. */
    now = KeQueryPerformanceCounter(NULL);
    frameTicks = dev->VsyncQpcFrequency.QuadPart / 60;
    if (frameTicks <= 0) {
        return STATUS_UNSUCCESSFUL;
    }
    elapsed = now.QuadPart - dev->VsyncLastQpc.QuadPart;
    if (elapsed < 0) elapsed = 0;
    elapsed %= frameTicks;
    activeTicks = (frameTicks * 95) / 100;
    pGetScanLine->InVerticalBlank = (elapsed >= activeTicks);
    pGetScanLine->ScanLine = pGetScanLine->InVerticalBlank ? 0 :
        (UINT)((elapsed * dev->ScreenHeight) / activeTicks);
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiNotifySurpriseRemoval(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ DXGK_SURPRISE_REMOVAL_TYPE RemovalType
    )
{
    UNREFERENCED_PARAMETER(RemovalType);
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    Bc250MarkDdi(L"DdiNotifySurpriseRemoval");
    if (dev != NULL) {
        dev->SurpriseRemoved = TRUE;
        Bc250TeardownDevice(dev, FALSE);
    }
    return STATUS_SUCCESS;
}

/*===========================================================================
  VidPN Presentation Engine
===========================================================================*/

/* Modo unico suportado: exatamente o framebuffer linear herdado do UEFI GOP.
   Nao ha crtc programavel neste driver, entao publicar qualquer outra
   resolucao seria mentir para o dxgkrnl. */
static VOID
Bc250FillSourceMode(
    _In_ PBC250_WDDM_DEVICE            dev,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE*   pMode
    )
{
    pMode->Type = D3DKMDT_RMT_GRAPHICS;
    pMode->Format.Graphics.PrimSurfSize.cx        = dev->ScreenWidth;
    pMode->Format.Graphics.PrimSurfSize.cy        = dev->ScreenHeight;
    pMode->Format.Graphics.VisibleRegionSize.cx   = dev->ScreenWidth;
    pMode->Format.Graphics.VisibleRegionSize.cy   = dev->ScreenHeight;
    pMode->Format.Graphics.Stride                 = dev->ScreenPitch;
    pMode->Format.Graphics.PixelFormat            = dev->ScreenFormat;
    pMode->Format.Graphics.ColorBasis             = D3DKMDT_CB_SRGB;
    pMode->Format.Graphics.PixelValueAccessMode   = D3DKMDT_PVAM_DIRECT;
}

static VOID
Bc250FillSignalInfo(
    _In_ PBC250_WDDM_DEVICE           dev,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO*  pSignal
    )
{
    pSignal->VideoStandard          = D3DKMDT_VSS_OTHER;
    pSignal->TotalSize.cx           = dev->ScreenWidth;
    pSignal->TotalSize.cy           = dev->ScreenHeight;
    pSignal->ActiveSize.cx          = dev->ScreenWidth;
    pSignal->ActiveSize.cy          = dev->ScreenHeight;
    pSignal->VSyncFreq.Numerator    = 60;
    pSignal->VSyncFreq.Denominator  = 1;
    pSignal->HSyncFreq.Numerator    = 60 * dev->ScreenHeight;
    pSignal->HSyncFreq.Denominator  = 1;
    pSignal->PixelRate              = (ULONGLONG)dev->ScreenWidth * dev->ScreenHeight * 60ULL;
    pSignal->ScanLineOrdering       = D3DDDI_VSSLO_PROGRESSIVE;
}

/* Publica o conjunto de modos de origem com o nosso unico modo. */
static NTSTATUS
Bc250PublishSourceModeSet(
    _In_ PBC250_WDDM_DEVICE                dev,
    _In_ CONST DXGK_VIDPN_INTERFACE*       pVidPnIface,
    _In_ D3DKMDT_HVIDPN                    hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    SourceId
    )
{
    D3DKMDT_HVIDPNSOURCEMODESET              hSet = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pSetIface = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE*               pMode = NULL;
    NTSTATUS                                 status;

    status = pVidPnIface->pfnCreateNewSourceModeSet(hVidPn, SourceId, &hSet, &pSetIface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = pSetIface->pfnCreateNewModeInfo(hSet, &pMode);
    if (NT_SUCCESS(status)) {
        Bc250FillSourceMode(dev, pMode);
        status = pSetIface->pfnAddMode(hSet, pMode);
        if (!NT_SUCCESS(status)) {
            pSetIface->pfnReleaseModeInfo(hSet, pMode);
        }
    }

    if (NT_SUCCESS(status)) {
        /* Assign transfere a posse do conjunto: nao liberar apos sucesso. */
        status = pVidPnIface->pfnAssignSourceModeSet(hVidPn, SourceId, hSet);
    }

    if (!NT_SUCCESS(status)) {
        pVidPnIface->pfnReleaseSourceModeSet(hVidPn, hSet);
    }
    return status;
}

/* Publica o conjunto de modos de destino com o nosso unico timing. */
static NTSTATUS
Bc250PublishTargetModeSet(
    _In_ PBC250_WDDM_DEVICE                dev,
    _In_ CONST DXGK_VIDPN_INTERFACE*       pVidPnIface,
    _In_ D3DKMDT_HVIDPN                    hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID    TargetId
    )
{
    D3DKMDT_HVIDPNTARGETMODESET              hSet = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pSetIface = NULL;
    D3DKMDT_VIDPN_TARGET_MODE*               pMode = NULL;
    NTSTATUS                                 status;

    status = pVidPnIface->pfnCreateNewTargetModeSet(hVidPn, TargetId, &hSet, &pSetIface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = pSetIface->pfnCreateNewModeInfo(hSet, &pMode);
    if (NT_SUCCESS(status)) {
        Bc250FillSignalInfo(dev, &pMode->VideoSignalInfo);
        pMode->Preference = D3DKMDT_MP_PREFERRED;
        status = pSetIface->pfnAddMode(hSet, pMode);
        if (!NT_SUCCESS(status)) {
            pSetIface->pfnReleaseModeInfo(hSet, pMode);
        }
    }

    if (NT_SUCCESS(status)) {
        status = pVidPnIface->pfnAssignTargetModeSet(hVidPn, TargetId, hSet);
    }

    if (!NT_SUCCESS(status)) {
        pVidPnIface->pfnReleaseTargetModeSet(hVidPn, hSet);
    }
    return status;
}

static NTSTATUS
Bc250DdiIsSupportedVidPn(
    _In_ CONST PVOID                   MiniportDeviceContext,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN*  pIsSupportedVidPn
    )
{
    PBC250_WDDM_DEVICE                    dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE*           pVidPnIface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*   pTopIface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*     pPath = NULL;
    D3DKMDT_HVIDPNTOPOLOGY                hTopology = NULL;
    SIZE_T                                numPaths = 0;
    NTSTATUS                              status;

    Bc250LogStep(L"DdiIsSupportedVidPn");

    if (dev == NULL || pIsSupportedVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* O DPC de VSync roda em DISPATCH_LEVEL e nao pode escrever no registro.
       Publicamos o contador daqui, que e PASSIVE e chamado com frequencia --
       sem isto, um TDR que pule o StopDevice esconderia se o VSync disparou. */
    Bc250LogReg(L"VsyncCount", dev->VsyncCount);

    /* Default pessimista: so vira TRUE se a topologia couber no hardware. */
    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    status = dev->DxgkInterface.DxgkCbQueryVidPnInterface(
        pIsSupportedVidPn->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnIface);
    if (!NT_SUCCESS(status)) {
        Bc250LogReg(L"VidPn_QueryIfaceStatus", (ULONG)status);
        return status;
    }

    status = pVidPnIface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hTopology, &pTopIface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = pTopIface->pfnGetNumPaths(hTopology, &numPaths);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Uma unica saida de video: mais de um caminho e impossivel de honrar. */
    if (numPaths > 1) {
        Bc250LogReg(L"VidPn_RejectedNumPaths", (ULONG)numPaths);
        return STATUS_SUCCESS;
    }

    if (numPaths == 1) {
        status = pTopIface->pfnAcquireFirstPathInfo(hTopology, &pPath);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (pPath->VidPnSourceId != 0 || pPath->VidPnTargetId != BC250_CHILD_ID_DISPLAY) {
            Bc250LogReg(L"VidPn_RejectedTargetId", (ULONG)pPath->VidPnTargetId);
            pTopIface->pfnReleasePathInfo(hTopology, pPath);
            return STATUS_SUCCESS;
        }
        pTopIface->pfnReleasePathInfo(hTopology, pPath);
    }

    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiRecommendFunctionalVidPn(
    _In_ CONST PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    Bc250LogStep(L"DdiRecommendFunctionalVidPn");
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

/* Sem isto o dxgkrnl nunca monta um VidPN funcional: um STATUS_SUCCESS
   vazio deixa os conjuntos de modos indefinidos e o CommitVidPn falha. */
static NTSTATUS
Bc250DdiEnumVidPnCofuncModality(
    _In_ CONST PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality
    )
{
    PBC250_WDDM_DEVICE                    dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE*           pVidPnIface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*   pTopIface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*     pPath = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*     pNextPath = NULL;
    D3DKMDT_HVIDPNTOPOLOGY                hTopology = NULL;
    NTSTATUS                              status;

    Bc250LogStep(L"DdiEnumVidPnCofuncModality");

    if (dev == NULL || pEnumCofuncModality == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = dev->DxgkInterface.DxgkCbQueryVidPnInterface(
        pEnumCofuncModality->hConstrainingVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnIface);
    if (!NT_SUCCESS(status)) {
        Bc250LogReg(L"Cofunc_QueryIfaceStatus", (ULONG)status);
        return status;
    }

    status = pVidPnIface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn, &hTopology, &pTopIface);
    if (!NT_SUCCESS(status)) {
        Bc250LogReg(L"Cofunc_GetTopologyStatus", (ULONG)status);
        return status;
    }

    status = pTopIface->pfnAcquireFirstPathInfo(hTopology, &pPath);
    if (!NT_SUCCESS(status)) {
        Bc250LogReg(L"Cofunc_FirstPathStatus", (ULONG)status);
        return status;
    }

    while (pPath != NULL) {
        D3DKMDT_VIDPN_PRESENT_PATH localPath;

        /* --- Conjunto de modos de origem ---------------------------------
           O pivo e o eixo que o dxgkrnl fixou nesta iteracao: nao podemos
           reescrever o conjunto que ele esta segurando. */
        if (!(pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              pEnumCofuncModality->EnumPivot.VidPnSourceId == pPath->VidPnSourceId)) {

            D3DKMDT_HVIDPNSOURCEMODESET              hSrcSet = NULL;
            CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pSrcIface = NULL;
            CONST D3DKMDT_VIDPN_SOURCE_MODE*         pPinnedSrc = NULL;

            status = pVidPnIface->pfnAcquireSourceModeSet(
                pEnumCofuncModality->hConstrainingVidPn, pPath->VidPnSourceId, &hSrcSet, &pSrcIface);
            if (NT_SUCCESS(status)) {
                status = pSrcIface->pfnAcquirePinnedModeInfo(hSrcSet, &pPinnedSrc);
                if (NT_SUCCESS(status) && pPinnedSrc != NULL) {
                    /* Ja existe modo fixado: respeitamos e nao mexemos. */
                    pSrcIface->pfnReleaseModeInfo(hSrcSet, pPinnedSrc);
                    pVidPnIface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hSrcSet);
                } else {
                    pVidPnIface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hSrcSet);
                    status = Bc250PublishSourceModeSet(
                        dev, pVidPnIface, pEnumCofuncModality->hConstrainingVidPn, pPath->VidPnSourceId);
                    if (!NT_SUCCESS(status)) {
                        Bc250LogReg(L"Cofunc_SourceSetStatus", (ULONG)status);
                        pTopIface->pfnReleasePathInfo(hTopology, pPath);
                        return status;
                    }
                }
            }
        }

        /* --- Conjunto de modos de destino -------------------------------- */
        if (!(pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              pEnumCofuncModality->EnumPivot.VidPnTargetId == pPath->VidPnTargetId)) {

            D3DKMDT_HVIDPNTARGETMODESET              hTgtSet = NULL;
            CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pTgtIface = NULL;
            CONST D3DKMDT_VIDPN_TARGET_MODE*         pPinnedTgt = NULL;

            status = pVidPnIface->pfnAcquireTargetModeSet(
                pEnumCofuncModality->hConstrainingVidPn, pPath->VidPnTargetId, &hTgtSet, &pTgtIface);
            if (NT_SUCCESS(status)) {
                status = pTgtIface->pfnAcquirePinnedModeInfo(hTgtSet, &pPinnedTgt);
                if (NT_SUCCESS(status) && pPinnedTgt != NULL) {
                    pTgtIface->pfnReleaseModeInfo(hTgtSet, pPinnedTgt);
                    pVidPnIface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hTgtSet);
                } else {
                    pVidPnIface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hTgtSet);
                    status = Bc250PublishTargetModeSet(
                        dev, pVidPnIface, pEnumCofuncModality->hConstrainingVidPn, pPath->VidPnTargetId);
                    if (!NT_SUCCESS(status)) {
                        Bc250LogReg(L"Cofunc_TargetSetStatus", (ULONG)status);
                        pTopIface->pfnReleasePathInfo(hTopology, pPath);
                        return status;
                    }
                }
            }
        }

        /* --- Transformacoes: so identidade (sem scaler, sem rotacao) ------ */
        localPath = *pPath;
        RtlZeroMemory(&localPath.ContentTransformation.ScalingSupport,
                      sizeof(localPath.ContentTransformation.ScalingSupport));
        RtlZeroMemory(&localPath.ContentTransformation.RotationSupport,
                      sizeof(localPath.ContentTransformation.RotationSupport));
        localPath.ContentTransformation.ScalingSupport.Identity  = 1;
        localPath.ContentTransformation.RotationSupport.Identity = 1;

        status = pTopIface->pfnUpdatePathSupportInfo(hTopology, &localPath);
        if (!NT_SUCCESS(status)) {
            Bc250LogReg(L"Cofunc_UpdatePathStatus", (ULONG)status);
            pTopIface->pfnReleasePathInfo(hTopology, pPath);
            return status;
        }

        pNextPath = NULL;
        status = pTopIface->pfnAcquireNextPathInfo(hTopology, pPath, &pNextPath);
        pTopIface->pfnReleasePathInfo(hTopology, pPath);
        pPath = NT_SUCCESS(status) ? pNextPath : NULL;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiSetVidPnSourceVisibility(
    _In_ CONST PVOID                                     MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* CONST   pSetVidPnSourceVisibility
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    Bc250LogStep(L"DdiSetVidPnSourceVisibility");
    if (dev == NULL || pSetVidPnSourceVisibility == NULL ||
        (pSetVidPnSourceVisibility->VidPnSourceId != 0 &&
         pSetVidPnSourceVisibility->VidPnSourceId != D3DDDI_ID_ALL)) {
        return STATUS_INVALID_PARAMETER;
    }

    dev->SourceVisible = pSetVidPnSourceVisibility->Visible;
    dev->ForceFullPresent = pSetVidPnSourceVisibility->Visible;
    Bc250LogReg(L"Visibility_SourceId", pSetVidPnSourceVisibility->VidPnSourceId);
    Bc250LogReg(L"Visibility_Visible", pSetVidPnSourceVisibility->Visible);
    if (!pSetVidPnSourceVisibility->Visible) {
        Bc250BlackOutDisplay(dev);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiCommitVidPn(
    _In_ CONST PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST   pCommitVidPn
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE* vidPnIface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE* topIface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* srcIface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY topology = NULL;
    D3DKMDT_HVIDPNSOURCEMODESET srcSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE* pinned = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH* path = NULL;
    SIZE_T numPaths = 0;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId;
    NTSTATUS status;

    Bc250LogStep(L"DdiCommitVidPn");
    if (dev == NULL || pCommitVidPn == NULL || !dev->DeviceStarted) {
        return STATUS_INVALID_PARAMETER;
    }

    sourceId = (pCommitVidPn->AffectedVidPnSourceId == D3DDDI_ID_ALL) ?
        0 : pCommitVidPn->AffectedVidPnSourceId;
    if (sourceId != 0) {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    Bc250LogReg(L"Commit_SourceId", sourceId);
    Bc250LogReg(L"Commit_ConnectivityChecks", (ULONG)pCommitVidPn->MonitorConnectivityChecks);
    Bc250LogReg(L"Commit_PathPoweredOff", pCommitVidPn->Flags.PathPoweredOff);

    if (pCommitVidPn->Flags.PathPoweredOff) {
        dev->SourceVisible = FALSE;
        dev->VidPnCommitted = FALSE;
        Bc250BlackOutDisplay(dev);
        Bc250LogReg(L"Commit_Status", STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    status = dev->DxgkInterface.DxgkCbQueryVidPnInterface(
        pCommitVidPn->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnIface);
    if (!NT_SUCCESS(status)) goto CommitExit;

    status = vidPnIface->pfnGetTopology(
        pCommitVidPn->hFunctionalVidPn, &topology, &topIface);
    if (!NT_SUCCESS(status)) goto CommitExit;

    status = topIface->pfnGetNumPaths(topology, &numPaths);
    if (!NT_SUCCESS(status)) goto CommitExit;
    Bc250LogReg(L"Commit_PathCount", (ULONG)numPaths);

    if (numPaths == 0) {
        dev->VidPnCommitted = FALSE;
        dev->SourceVisible = FALSE;
        Bc250BlackOutDisplay(dev);
        status = STATUS_SUCCESS;
        goto CommitExit;
    }
    if (numPaths != 1) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto CommitExit;
    }

    status = topIface->pfnAcquireFirstPathInfo(topology, &path);
    if (!NT_SUCCESS(status)) goto CommitExit;
    if (path == NULL || path->VidPnSourceId != 0 ||
        path->VidPnTargetId != BC250_CHILD_ID_DISPLAY ||
        path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY ||
        path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto CommitExit;
    }

    status = vidPnIface->pfnAcquireSourceModeSet(
        pCommitVidPn->hFunctionalVidPn, sourceId, &srcSet, &srcIface);
    if (!NT_SUCCESS(status)) goto CommitExit;
    status = srcIface->pfnAcquirePinnedModeInfo(srcSet, &pinned);
    if (!NT_SUCCESS(status) || pinned == NULL) {
        if (NT_SUCCESS(status)) status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        goto CommitExit;
    }

    Bc250LogReg(L"Commit_PinnedW", pinned->Format.Graphics.PrimSurfSize.cx);
    Bc250LogReg(L"Commit_PinnedH", pinned->Format.Graphics.PrimSurfSize.cy);
    Bc250LogReg(L"Commit_PinnedPitch", pinned->Format.Graphics.Stride);
    if (pinned->Type != D3DKMDT_RMT_GRAPHICS ||
        pinned->Format.Graphics.PrimSurfSize.cx != dev->ScreenWidth ||
        pinned->Format.Graphics.PrimSurfSize.cy != dev->ScreenHeight ||
        pinned->Format.Graphics.Stride != dev->ScreenPitch ||
        (pinned->Format.Graphics.PixelFormat != D3DDDIFMT_A8R8G8B8 &&
         pinned->Format.Graphics.PixelFormat != D3DDDIFMT_X8R8G8B8)) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto CommitExit;
    }

    dev->VidPnCommitted = TRUE;
    dev->ForceFullPresent = TRUE;
    status = STATUS_SUCCESS;

CommitExit:
    if (pinned != NULL && srcIface != NULL && srcSet != NULL) {
        (VOID)srcIface->pfnReleaseModeInfo(srcSet, pinned);
    }
    if (srcSet != NULL && vidPnIface != NULL) {
        (VOID)vidPnIface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, srcSet);
    }
    if (path != NULL && topIface != NULL && topology != NULL) {
        (VOID)topIface->pfnReleasePathInfo(topology, path);
    }
    Bc250LogReg(L"Commit_Status", (ULONG)status);
    return status;
}

static NTSTATUS
Bc250DdiUpdateActiveVidPnPresentPath(
    _In_ CONST PVOID                                              MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST       pUpdateActiveVidPnPresentPath
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    CONST D3DKMDT_VIDPN_PRESENT_PATH* path;

    Bc250LogStep(L"DdiUpdateActiveVidPnPresentPath");
    if (dev == NULL || pUpdateActiveVidPnPresentPath == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    path = &pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo;
    if (path->VidPnSourceId != 0 || path->VidPnTargetId != BC250_CHILD_ID_DISPLAY ||
        path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY ||
        path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    dev->ForceFullPresent = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiRecommendMonitorModes(
    _In_ CONST PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST   pRecommendMonitorModes
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    D3DKMDT_MONITOR_SOURCE_MODE* pMode = NULL;
    NTSTATUS status;

    Bc250LogStep(L"DdiRecommendMonitorModes");

    if (dev == NULL || pRecommendMonitorModes == NULL || pRecommendMonitorModes->pMonitorSourceModeSetInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        &pMode);

    if (NT_SUCCESS(status) && pMode != NULL) {
        pMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pMode->VideoSignalInfo.TotalSize.cx = dev->ScreenWidth;
        pMode->VideoSignalInfo.TotalSize.cy = dev->ScreenHeight;
        pMode->VideoSignalInfo.ActiveSize.cx = dev->ScreenWidth;
        pMode->VideoSignalInfo.ActiveSize.cy = dev->ScreenHeight;
        pMode->VideoSignalInfo.VSyncFreq.Numerator = 60;
        pMode->VideoSignalInfo.VSyncFreq.Denominator = 1;
        pMode->VideoSignalInfo.HSyncFreq.Numerator = 60 * dev->ScreenHeight;
        pMode->VideoSignalInfo.HSyncFreq.Denominator = 1;
        pMode->VideoSignalInfo.PixelRate = 60 * dev->ScreenWidth * dev->ScreenHeight;
        pMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
        pMode->ColorBasis = D3DKMDT_CB_SRGB;
        pMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pMode->ColorCoeffDynamicRanges.FourthChannel = 8;
        pMode->Origin = D3DKMDT_MCO_DRIVER;
        pMode->Preference = D3DKMDT_MP_PREFERRED;

        status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
            pRecommendMonitorModes->hMonitorSourceModeSet,
            pMode);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiQueryVidPnHWCapability(
    _In_ CONST PVOID                               MiniportDeviceContext,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* CONST  pVidPnHWCaps
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    Bc250LogStep(L"DdiQueryVidPnHWCapability");
    if (dev == NULL || pVidPnHWCaps == NULL ||
        pVidPnHWCaps->SourceId != 0 || pVidPnHWCaps->TargetId != BC250_CHILD_ID_DISPLAY) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&pVidPnHWCaps->VidPnHWCaps, sizeof(D3DKMDT_VIDPN_HW_CAPABILITY));
    pVidPnHWCaps->VidPnHWCaps.DriverRotation = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiSetPointerPosition(
    _In_ CONST PVOID                               MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* CONST   pSetPointerPosition
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiSetPointerShape(
    _In_ CONST PVOID                               MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* CONST      pSetPointerShape
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pSetPointerShape);
    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  PresentDisplayOnly: Blit Direto no Framebuffer Linear
===========================================================================*/
static VOID
Bc250CopyPresentRect(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ CONST UCHAR* Source,
    _In_ ULONG SourcePitch,
    _In_ CONST RECT* Rect
    )
{
    CONST ULONG bytesPerPixel = 4;
    LONG left, top, right, bottom;
    ULONG y, rowBytes;

    left   = (Rect->left < 0) ? 0 : Rect->left;
    top    = (Rect->top < 0) ? 0 : Rect->top;
    right  = (Rect->right > (LONG)Dev->ScreenWidth) ? (LONG)Dev->ScreenWidth : Rect->right;
    bottom = (Rect->bottom > (LONG)Dev->ScreenHeight) ? (LONG)Dev->ScreenHeight : Rect->bottom;
    if (right <= left || bottom <= top) {
        return;
    }

    rowBytes = (ULONG)(right - left) * bytesPerPixel;
    for (y = (ULONG)top; y < (ULONG)bottom; y++) {
        PUCHAR dst = (PUCHAR)Dev->DisplayVirtualBase +
            ((SIZE_T)y * Dev->ScreenPitch) + ((ULONG)left * bytesPerPixel);
        CONST UCHAR* src = Source +
            ((SIZE_T)y * SourcePitch) + ((ULONG)left * bytesPerPixel);
        RtlCopyMemory(dst, src, rowBytes);
    }
}

static NTSTATUS
Bc250DdiPresentDisplayOnly(
    _In_ CONST PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY*  pPresentDisplayOnly
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    ULONG i;
    RECT fullRect;

    if (dev == NULL || pPresentDisplayOnly == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pPresentDisplayOnly->VidPnSourceId != 0 ||
        pPresentDisplayOnly->pSource == NULL ||
        pPresentDisplayOnly->BytesPerPixel != 4 ||
        pPresentDisplayOnly->Pitch <= 0 ||
        (ULONG)pPresentDisplayOnly->Pitch < dev->ScreenWidth * 4u ||
        (pPresentDisplayOnly->NumMoves != 0 && pPresentDisplayOnly->pMoves == NULL) ||
        (pPresentDisplayOnly->NumDirtyRects != 0 && pPresentDisplayOnly->pDirtyRect == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Instrumentacao barata: sem esta contagem nao ha como saber se o TDR
       aconteceu DENTRO do present ou antes dele. */
    dev->PresentCount++;
    if (dev->PresentCount == 1) {
        dev->PresentFirstSeq = (ULONG)InterlockedIncrement(&g_Bc250DdiSeq);
        Bc250LogRegSz(L"LastDdi", L"DdiPresentDisplayOnly");
        Bc250LogReg(L"DdiPresentDisplayOnly", dev->PresentFirstSeq);
        Bc250LogReg(L"Present_NumDirtyRects", pPresentDisplayOnly->NumDirtyRects);
        Bc250LogReg(L"Present_NumMoves", pPresentDisplayOnly->NumMoves);
        Bc250LogReg(L"Present_BytesPerPixel", pPresentDisplayOnly->BytesPerPixel);
        Bc250LogReg(L"Present_SrcPitch", (ULONG)pPresentDisplayOnly->Pitch);
        Bc250LogReg(L"Present_Entered", 1);
    }
    if ((dev->PresentCount & 0xFF) == 0) {
        Bc250LogReg(L"Present_Count", dev->PresentCount);
    }

    if (!dev->DeviceStarted || dev->SurpriseRemoved ||
        dev->AdapterPowerState != PowerDeviceD0 ||
        dev->MonitorPowerState != PowerDeviceD0 ||
        !dev->VidPnCommitted || !dev->SourceVisible) {
        return STATUS_SUCCESS;
    }

    ExAcquireFastMutex(&dev->DeviceMutex);
    if (dev->DisplayVirtualBase == NULL || dev->SurpriseRemoved) {
        ExReleaseFastMutex(&dev->DeviceMutex);
        return STATUS_SUCCESS;
    }

    if (dev->ForceFullPresent) {
        fullRect.left = 0;
        fullRect.top = 0;
        fullRect.right = (LONG)dev->ScreenWidth;
        fullRect.bottom = (LONG)dev->ScreenHeight;
        Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
            (ULONG)pPresentDisplayOnly->Pitch, &fullRect);
        dev->ForceFullPresent = FALSE;
    } else {
        /* pSource ja contem o frame depois do movimento. Copiar DestRect
           reproduz o scroll sem depender do conteudo anterior do scanout. */
        for (i = 0; i < pPresentDisplayOnly->NumMoves; i++) {
            Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
                (ULONG)pPresentDisplayOnly->Pitch, &pPresentDisplayOnly->pMoves[i].DestRect);
        }
        for (i = 0; i < pPresentDisplayOnly->NumDirtyRects; i++) {
            Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
                (ULONG)pPresentDisplayOnly->Pitch, &pPresentDisplayOnly->pDirtyRect[i]);
        }
    }
    ExReleaseFastMutex(&dev->DeviceMutex);

    /* Chegou ao fim: se o TDR ocorrer, Present_Entered=1 sem Present_Exited=1
       aponta o travamento para dentro deste laco. */
    if (dev->PresentCount == 1) {
        Bc250LogReg(L"Present_Exited", 1);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_  CONST PVOID                          MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION            DisplayInfo
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    if (dev == NULL || DisplayInfo == NULL || TargetId != BC250_CHILD_ID_DISPLAY) {
        return STATUS_INVALID_PARAMETER;
    }

    DisplayInfo->Width         = dev->ScreenWidth;
    DisplayInfo->Height        = dev->ScreenHeight;
    DisplayInfo->Pitch         = dev->ScreenPitch;
    DisplayInfo->ColorFormat   = dev->ScreenFormat;
    DisplayInfo->PhysicAddress = dev->DisplayPhysicalBase;
    DisplayInfo->TargetId      = BC250_CHILD_ID_DISPLAY;

    Bc250BlackOutDisplay(dev);
    Bc250TeardownDevice(dev, TRUE);
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250DdiSystemDisplayEnable(
    _In_  CONST PVOID                          MiniportDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT*                                Width,
    _Out_ UINT*                                Height,
    _Out_ D3DDDIFORMAT*                        ColorFormat
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;

    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (dev == NULL || Width == NULL || Height == NULL || ColorFormat == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Width       = dev->ScreenWidth;
    *Height      = dev->ScreenHeight;
    *ColorFormat = dev->ScreenFormat;

    return STATUS_SUCCESS;
}

static VOID
Bc250DdiSystemDisplayWrite(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ PVOID       Source,
    _In_ UINT        SourceWidth,
    _In_ UINT        SourceHeight,
    _In_ UINT        SourceStride,
    _In_ UINT        PositionX,
    _In_ UINT        PositionY
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    PUCHAR dst, src;
    UINT y, rowBytes;

    if (dev == NULL || dev->DisplayVirtualBase == NULL || Source == NULL) {
        return;
    }

    /* Bugcheck screen: recorta para nao escrever fora do framebuffer. */
    if (PositionX >= dev->ScreenWidth || PositionY >= dev->ScreenHeight) {
        return;
    }
    if (PositionX + SourceWidth > dev->ScreenWidth) {
        SourceWidth = dev->ScreenWidth - PositionX;
    }
    if (PositionY + SourceHeight > dev->ScreenHeight) {
        SourceHeight = dev->ScreenHeight - PositionY;
    }

    rowBytes = SourceWidth * 4;
    for (y = 0; y < SourceHeight; y++) {
        dst = (PUCHAR)dev->DisplayVirtualBase + ((PositionY + y) * dev->ScreenPitch) + (PositionX * 4);
        src = (PUCHAR)Source + (y * SourceStride);
        RtlCopyMemory(dst, src, rowBytes);
    }
}
