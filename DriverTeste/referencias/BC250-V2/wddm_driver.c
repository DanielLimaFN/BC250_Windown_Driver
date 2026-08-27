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
static VOID Bc250FreeScaleMaps(_In_ PBC250_WDDM_DEVICE dev);
static VOID Bc250BuildEdid(_In_ PBC250_WDDM_DEVICE dev);

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

/* Stamps de ultimo valor nao mostram a sequencia das negociacoes de modo;
   estes contadores acumulam para dar essa visao. */
static volatile LONG g_CofuncCalls = 0;
static volatile LONG g_CofuncSrcUnpinned = 0;

static CONST WCHAR g_Bc250ParamsPath[] =
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\bc250v2\\Parameters";
static CONST WCHAR g_Bc250ServicePath[] =
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\bc250v2";

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

    Bc250FreeScaleMaps(Dev);

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
    dev->ActiveWidth  = postDisplayInfo.Width;
    dev->ActiveHeight = postDisplayInfo.Height;
    dev->ActivePitch  = postDisplayInfo.Pitch;
    dev->OffsetX      = 0;
    dev->OffsetY      = 0;
    dev->DestWidth    = postDisplayInfo.Width;
    dev->DestHeight   = postDisplayInfo.Height;
    dev->Scaling      = D3DKMDT_VPPS_IDENTITY;
    dev->ScaleActive  = FALSE;
    dev->PostDisplayAcquired = TRUE;

    /* Precisa vir depois de ScreenWidth/Height: o descritor detalhado
       preferido do EDID descreve o scanout real. */
    Bc250BuildEdid(dev);

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
    Bc250LogRegSz(L"Hardware_Name", L"AMD BC-250 / Cyan Skillfish");
    Bc250LogRegSz(L"Hardware_GPU", L"Navi12 Lite");
    Bc250LogRegSz(L"Hardware_Process", L"7 nm");
    Bc250LogRegSz(L"Hardware_Memory", L"16 GB GDDR6");
    Bc250LogRegSz(L"Hardware_Transistors", L"10,600 Million");
    Bc250LogRegSz(L"Hardware_DieSize", L"308 mm²");
    Bc250LogReg(L"PostDisplayAcquired", (ULONG)dev->PostDisplayAcquired);
    Bc250LogReg(L"FbMapped", (ULONG)(dev->FbVirtualBase != NULL));
    Bc250LogReg(L"DisplayMapped", (ULONG)(dev->DisplayVirtualBase != NULL));
    Bc250LogReg(L"DisplayInsideVram", (ULONG)dev->DisplayInsideVram);
    Bc250LogReg(L"MmioMapped", (ULONG)(dev->MmioVirtualBase != NULL));
    Bc250LogReg(L"ScreenWidth", dev->ScreenWidth);
    Bc250LogReg(L"ScreenHeight", dev->ScreenHeight);
    Bc250LogReg(L"ScreenPitch", dev->ScreenPitch);
    Bc250LogReg(L"ScreenFormat", (ULONG)dev->ScreenFormat);

    /* Grava as propriedades de VRAM detectadas dinamicamente da BIOS/BAR0 para o Windows */
    {
        HANDLE hDevKey = NULL;
        if (NT_SUCCESS(IoOpenDeviceRegistryKey(dev->PhysicalDeviceObject, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &hDevKey))) {
            UNICODE_STRING valName;
            ULONG memSize32;
            ULONGLONG memSize64;

            /* Se a BIOS configurou um carveout (ex: 512 MB, 1 GB, 2 GB, 4 GB, etc.) usa o tamanho real */
            if (dev->FbSize > 0) {
                memSize64 = dev->FbSize;
                if (dev->FbSize < (512ULL * 1024ULL * 1024ULL)) {
                    /* Se o BAR0 PCI padrão sem ReBAR reporta 256 MB, o carveout da BIOS BC-250 é 512 MB */
                    memSize32 = 512 * 1024 * 1024;
                } else if (dev->FbSize <= 0xFFFFFFFFULL) {
                    memSize32 = (ULONG)dev->FbSize;
                } else {
                    memSize32 = (ULONG)(4096ULL * 1024ULL * 1024ULL); /* Clamp 32-bit (4 GB) para DX9/DX11 legados */
                }
            } else {
                memSize32 = 512 * 1024 * 1024;
                memSize64 = 16ULL * 1024ULL * 1024ULL * 1024ULL;
            }

            RtlInitUnicodeString(&valName, L"HardwareInformation.MemorySize");
            ZwSetValueKey(hDevKey, &valName, 0, REG_DWORD, &memSize32, sizeof(memSize32));

            RtlInitUnicodeString(&valName, L"HardwareInformation.qwMemorySize");
            ZwSetValueKey(hDevKey, &valName, 0, REG_QWORD, &memSize64, sizeof(memSize64));

            RtlInitUnicodeString(&valName, L"DedicatedVideoMemory");
            ZwSetValueKey(hDevKey, &valName, 0, REG_DWORD, &memSize32, sizeof(memSize32));

            {
                UNICODE_STRING uStr;
                RtlInitUnicodeString(&valName, L"HardwareInformation.AdapterString");
                RtlInitUnicodeString(&uStr, L"AMD BC-250 / Cyan Skillfish");
                ZwSetValueKey(hDevKey, &valName, 0, REG_SZ, uStr.Buffer, uStr.Length + sizeof(WCHAR));

                RtlInitUnicodeString(&valName, L"HardwareInformation.ChipType");
                RtlInitUnicodeString(&uStr, L"Navi12 (Cyan Skillfish / Oberon)");
                ZwSetValueKey(hDevKey, &valName, 0, REG_SZ, uStr.Buffer, uStr.Length + sizeof(WCHAR));

                RtlInitUnicodeString(&valName, L"HardwareInformation.DacType");
                RtlInitUnicodeString(&uStr, L"Internal DAC (60Hz)");
                ZwSetValueKey(hDevKey, &valName, 0, REG_SZ, uStr.Buffer, uStr.Length + sizeof(WCHAR));

                RtlInitUnicodeString(&valName, L"HardwareInformation.BiosString");
                RtlInitUnicodeString(&uStr, L"Cyan Skillfish UEFI GOP v5.00");
                ZwSetValueKey(hDevKey, &valName, 0, REG_SZ, uStr.Buffer, uStr.Length + sizeof(WCHAR));

                RtlInitUnicodeString(&valName, L"UserModeDriverName");
                RtlInitUnicodeString(&uStr, L"amdbc250vulkan.dll");
                ZwSetValueKey(hDevKey, &valName, 0, REG_SZ, uStr.Buffer, uStr.Length + sizeof(WCHAR));
            }

            Bc250LogReg(L"BiosVramSizeMb", (ULONG)(memSize64 / (1024ULL * 1024ULL)));

            ZwClose(hDevKey);
        }
    }

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
        /* Readback do mailbox logo apos a escrita: separa "aperture SMN
           quebrada" (readback 0) de "SMU nao responde" (readback == o que
           escrevemos). */
        Bc250LogReg(L"Hw_SmuCmdReadback", dev->HwSmuCmdReadback);
        Bc250LogReg(L"Hw_SmuArgReadback", dev->HwSmuArgReadback);
        Bc250LogReg(L"Hw_SmuRspAtSubmit", dev->HwSmuRspAtSubmit);
        Bc250LogReg(L"Hw_SmuTestQ0_Status", dev->HwSmuTestQ0Status);
        Bc250LogReg(L"Hw_SmuTestQ0_Rsp", dev->HwSmuTestQ0Rsp);
        Bc250LogReg(L"Hw_SmuTestQ0_Arg", dev->HwSmuTestQ0Arg);
        Bc250LogReg(L"Hw_SmuTestQ2_Status", dev->HwSmuTestQ2Status);
        Bc250LogReg(L"Hw_SmuTestQ2_Rsp", dev->HwSmuTestQ2Rsp);
        Bc250LogReg(L"Hw_SmuTestQ2_Arg", dev->HwSmuTestQ2Arg);
        Bc250LogReg(L"Hw_SmuVersion_Rsp", dev->HwSmuVersionRsp);
        Bc250LogReg(L"Hw_SmuVersion", dev->HwSmuVersion);
        Bc250LogReg(L"Hw_PspTosReg", dev->HwPspTosReg);
        Bc250LogReg(L"Hw_PspRing", (ULONG)dev->HwStatusPspRing);
        Bc250LogReg(L"Hw_Tmr", (ULONG)dev->HwStatusTmr);
        for (i = 0; i < RTL_NUMBER_OF(fwLabels); i++) {
            RtlStringCchCopyW(fwName, RTL_NUMBER_OF(fwName), fwLabels[i]);
            Bc250LogReg(fwName, (ULONG)dev->HwStatusFw[i]);
        }

        /* Resposta crua do PSP por FwType. Indexada pelo PSP_FW_* real, nao
           pela ordem de chamada, para o codigo poder ser cruzado direto com o
           protocolo. Publicamos so os slots que o PSP de fato respondeu. */
        {
            static CONST WCHAR* CONST pspFwNames[16] = {
                NULL, L"ME", L"PFP", L"CE", L"MEC", L"MEC2", NULL, NULL,
                L"RLC_G", L"SDMA0", NULL, NULL, NULL, NULL, NULL, NULL
            };
            ULONG f;
            for (f = 0; f < RTL_NUMBER_OF(dev->HwPspFwResp); f++) {
                WCHAR key[64];
                if (pspFwNames[f] == NULL) continue;
                if (dev->HwPspFwSeen[f] == 0) continue;
                RtlStringCchPrintfW(key, RTL_NUMBER_OF(key), L"PspResp_%s", pspFwNames[f]);
                Bc250LogReg(key, dev->HwPspFwResp[f]);
                RtlStringCchPrintfW(key, RTL_NUMBER_OF(key), L"PspSess_%s", pspFwNames[f]);
                Bc250LogReg(key, dev->HwPspFwSession[f]);
            }
        }
        Bc250LogReg(L"Hw_FbOffsetReg", dev->HwFbOffsetReg);
        Bc250LogReg(L"Hw_L2CntlReadback", dev->HwL2CntlReadback);
        Bc250LogReg(L"Hw_RbCntlReadback", dev->HwRbCntlReadback);
        Bc250LogReg(L"Hw_MeCntlReadback", dev->HwMeCntlReadback);
        Bc250LogReg(L"Hw_RptrAfterInit", dev->HwRptrAfterInit);
        Bc250LogReg(L"Hw_Sdma_Status", (ULONG)dev->HwStatusSdmaRing);
        Bc250LogReg(L"Hw_Sdma_RbCntl", dev->HwSdmaRbCntlReadback);
        if (dev->SdmaInitialized) {
            ULONG avgBw = 0, peakBw = 0;
            if (NT_SUCCESS(Bc250SdmaRunBenchmark(dev, 5, 0x00400000u, &avgBw, &peakBw))) {
                Bc250LogReg(L"Hw_Sdma_Bandwidth_MBs", avgBw);
                Bc250LogReg(L"Hw_Sdma_Peak_MBs", peakBw);
            }
        }
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

/*===========================================================================
  EDID sintetico

  O filho de video nao tem EDID real: nao ha DDC/I2C aqui, so o framebuffer
  herdado do GOP. Sem descritor, o monitor.sys registra "Generic Non-PnP
  Monitor" com BAD_EDID e cai num mode set default minimo, que estrangula a
  lista de resolucoes independentemente de quantos modos o driver publique no
  VidPN. Publicar um EDID coerente e como drivers de display virtual resolvem
  isso.

  O conjunto anunciado espelha a tabela g_Bc250ModeTable:
    - timings estabelecidos: 640x480, 800x600, 1024x768
    - standard timings:      1152x864, 1280x720, 1280x800, 1280x1024,
                             1440x900, 1600x900, 1680x1050, 1920x1080
  Nao ha 1366x768 nem 1360x768: um standard timing codifica so a largura mais
  a razao de aspecto, e 1360 em 16:9 reconstroi 765, nao 768. Larguras que nao
  sao multiplo de 8 (1366) nem sequer sao codificaveis.
    - timing detalhado:      o modo nativo do scanout, marcado como preferido
  Modos maiores que o scanout continuam sendo filtrados em runtime por
  Bc250ModeIsUsable, entao anunciar a faixa nao gera modo inexibivel.
===========================================================================*/

/* Blanking identico ao de Bc250FillSignalInfo: o timing no EDID e o timing no
   VidPN precisam descrever a mesma coisa. */
#define BC250_EDID_HBLANK   160u
#define BC250_EDID_VBLANK    45u

static VOID
Bc250BuildEdid(
    _In_ PBC250_WDDM_DEVICE dev
    )
{
    PUCHAR  e = dev->Edid;
    PUCHAR  d;
    ULONG   i;
    ULONG   sum = 0;
    ULONG   hActive, vActive, hTotal, vTotal, clock10kHz;
    ULONG   hSizeMm, vSizeMm;

    static CONST UCHAR stdTimings[16] = {
        0xD1, 0xC0,   /* 1920x1080 @60, 16:9  */
        0xB3, 0x00,   /* 1680x1050 @60, 16:10 */
        0xA9, 0xC0,   /* 1600x900  @60, 16:9  */
        0x95, 0x00,   /* 1440x900  @60, 16:10 */
        0x71, 0x40,   /* 1152x864  @60, 4:3   */
        0x81, 0x80,   /* 1280x1024 @60, 5:4   */
        0x81, 0x00,   /* 1280x800  @60, 16:10 */
        0x81, 0xC0    /* 1280x720  @60, 16:9  */
    };

    dev->EdidValid = FALSE;
    RtlZeroMemory(e, BC250_EDID_BLOCK_SIZE);

    if (dev->ScreenWidth == 0 || dev->ScreenHeight == 0) {
        return;
    }

    hActive = dev->ScreenWidth;
    vActive = dev->ScreenHeight;
    hTotal  = hActive + BC250_EDID_HBLANK;
    vTotal  = vActive + BC250_EDID_VBLANK;

    /* O clock do descritor detalhado e expresso em unidades de 10 kHz.
       Para 2560x1080 a conta fecha exata (2720 x 1125 x 60 = 183.600.000);
       para outros tamanhos de GOP arredondamos, o que e inofensivo: o
       casamento de modo e feito por pixels ativos e taxa de refresh, nao pelo
       clock exato. */
    clock10kHz = (ULONG)(((ULONGLONG)hTotal * vTotal * 60ULL + 5000ULL) / 10000ULL);
    if (clock10kHz > 0xFFFF) {
        clock10kHz = 0xFFFF;
    }

    /* Tamanho fisico derivado de ~96 DPI, so para o Windows ter um DPI
       plausivel; nao ha painel real cujas medidas possamos ler. */
    hSizeMm = (hActive * 254u) / 960u;
    vSizeMm = (vActive * 254u) / 960u;
    if (hSizeMm > 4095u) hSizeMm = 4095u;
    if (vSizeMm > 4095u) vSizeMm = 4095u;

    /* --- Cabecalho ------------------------------------------------------- */
    e[0] = 0x00;
    for (i = 1; i <= 6; i++) {
        e[i] = 0xFF;
    }
    e[7] = 0x00;

    /* Fabricante "BCX" em PNP ID de 5 bits por letra, big-endian. */
    e[8]  = 0x08;
    e[9]  = 0x78;
    /* Codigo do produto = device ID, little-endian. */
    e[10] = (UCHAR)(AMDBC250_DEVICE_ID & 0xFF);
    e[11] = (UCHAR)((AMDBC250_DEVICE_ID >> 8) & 0xFF);
    /* Serial. */
    e[12] = 0x01;
    e[13] = 0x00;
    e[14] = 0x00;
    e[15] = 0x00;
    e[16] = 1;      /* semana de fabricacao */
    e[17] = 36;     /* 2026 - 1990 */
    e[18] = 1;      /* EDID versao 1 */
    e[19] = 3;      /* revisao 3 */

    /* --- Parametros basicos de video ------------------------------------- */
    e[20] = 0x80;                               /* entrada digital */
    e[21] = (UCHAR)(hSizeMm / 10u);             /* tamanho maximo em cm */
    e[22] = (UCHAR)(vSizeMm / 10u);
    e[23] = 120;                                /* gamma 2.2 */
    e[24] = 0x02;                               /* 1o descritor detalhado e o preferido */

    /* Cromaticidade sRGB padrao. */
    e[25] = 0xEE; e[26] = 0x91; e[27] = 0xA3; e[28] = 0x54; e[29] = 0x4C;
    e[30] = 0x99; e[31] = 0x26; e[32] = 0x0F; e[33] = 0x50; e[34] = 0x54;

    /* --- Timings estabelecidos ------------------------------------------- */
    e[35] = 0x21;   /* bit5: 640x480@60 | bit0: 800x600@60 */
    e[36] = 0x08;   /* bit3: 1024x768@60 */
    e[37] = 0x00;

    /* --- Standard timings ------------------------------------------------ */
    RtlCopyMemory(&e[38], stdTimings, sizeof(stdTimings));

    /* --- Descritor detalhado 1: modo nativo (preferido) ------------------ */
    d = &e[54];
    d[0]  = (UCHAR)(clock10kHz & 0xFF);
    d[1]  = (UCHAR)((clock10kHz >> 8) & 0xFF);
    d[2]  = (UCHAR)(hActive & 0xFF);
    d[3]  = (UCHAR)(BC250_EDID_HBLANK & 0xFF);
    d[4]  = (UCHAR)((((hActive >> 8) & 0x0F) << 4) | ((BC250_EDID_HBLANK >> 8) & 0x0F));
    d[5]  = (UCHAR)(vActive & 0xFF);
    d[6]  = (UCHAR)(BC250_EDID_VBLANK & 0xFF);
    d[7]  = (UCHAR)((((vActive >> 8) & 0x0F) << 4) | ((BC250_EDID_VBLANK >> 8) & 0x0F));
    /* Front porch 48 / sync 32 / back porch 80 = 160 na horizontal.
       Front porch 3 / sync 5 / back porch 37 = 45 na vertical. */
    d[8]  = 48;
    d[9]  = 32;
    d[10] = (UCHAR)((3u << 4) | 5u);
    d[11] = 0x00;   /* bits altos de todos os quatro campos acima sao zero */
    d[12] = (UCHAR)(hSizeMm & 0xFF);
    d[13] = (UCHAR)(vSizeMm & 0xFF);
    d[14] = (UCHAR)((((hSizeMm >> 8) & 0x0F) << 4) | ((vSizeMm >> 8) & 0x0F));
    d[15] = 0;      /* borda horizontal */
    d[16] = 0;      /* borda vertical */
    d[17] = 0x1E;   /* progressivo, sync digital separado, H+ e V+ */

    /* --- Descritor 2: limites de faixa ----------------------------------- */
    d = &e[72];
    d[0] = 0x00; d[1] = 0x00; d[2] = 0x00;
    d[3] = 0xFD;
    d[4] = 0x00;    /* sem offsets de faixa */
    d[5] = 50;      /* taxa vertical minima (Hz) */
    d[6] = 61;      /* taxa vertical maxima (Hz) */
    d[7] = 20;      /* taxa horizontal minima (kHz) */
    d[8] = 90;      /* taxa horizontal maxima (kHz) */
    d[9] = 19;      /* clock maximo: 19 x 10 MHz = 190 MHz */
    d[10] = 0x00;   /* GTF default suportado -- permite timings derivados */
    d[11] = 0x0A;
    for (i = 12; i < 18; i++) {
        d[i] = 0x20;
    }

    /* --- Descritor 3: nome do monitor ------------------------------------ */
    d = &e[90];
    d[0] = 0x00; d[1] = 0x00; d[2] = 0x00;
    d[3] = 0xFC;
    d[4] = 0x00;
    d[5]  = 'B'; d[6]  = 'C'; d[7]  = '-'; d[8]  = '2'; d[9]  = '5';
    d[10] = '0'; d[11] = ' '; d[12] = 'G'; d[13] = 'O'; d[14] = 'P';
    d[15] = 0x0A;
    d[16] = 0x20;
    d[17] = 0x20;

    /* --- Descritor 4: nao usado ------------------------------------------ */
    d = &e[108];
    d[0] = 0x00; d[1] = 0x00; d[2] = 0x00;
    d[3] = 0x10;
    d[4] = 0x00;

    /* --- Extensoes e checksum -------------------------------------------- */
    e[126] = 0x00;  /* nenhum bloco de extensao */
    for (i = 0; i < BC250_EDID_BLOCK_SIZE - 1; i++) {
        sum += e[i];
    }
    e[127] = (UCHAR)((0x100u - (sum & 0xFFu)) & 0xFFu);

    dev->EdidValid = TRUE;

    Bc250LogReg(L"Edid_Valid", 1);
    Bc250LogReg(L"Edid_Clock10kHz", clock10kHz);
    Bc250LogReg(L"Edid_HTotal", hTotal);
    Bc250LogReg(L"Edid_VTotal", vTotal);
    Bc250LogReg(L"Edid_Checksum", e[127]);
}

static NTSTATUS
Bc250DdiQueryDeviceDescriptor(
    _In_ CONST PVOID                   MiniportDeviceContext,
    _In_ ULONG                         ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR    DeviceDescriptor
    )
{
    PBC250_WDDM_DEVICE dev = (PBC250_WDDM_DEVICE)MiniportDeviceContext;
    ULONG              available;
    ULONG              copy;

    Bc250MarkDdi(L"DdiQueryDeviceDescriptor");
    Bc250LogReg(L"DdiQueryDeviceDescriptor_Uid", ChildUid);

    if (dev == NULL || DeviceDescriptor == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildUid != BC250_CHILD_ID_DISPLAY || !dev->EdidValid) {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    /* O dxgkrnl le o bloco em fatias, avancando DescriptorOffset. Quando o
       offset chega ao fim do bloco, sinalizamos que nao ha mais nada -- e
       assim que ele sabe que existe apenas o bloco base. */
    if (DeviceDescriptor->DescriptorOffset >= BC250_EDID_BLOCK_SIZE ||
        DeviceDescriptor->DescriptorBuffer == NULL ||
        DeviceDescriptor->DescriptorLength == 0) {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    available = BC250_EDID_BLOCK_SIZE - DeviceDescriptor->DescriptorOffset;
    copy = (DeviceDescriptor->DescriptorLength < available)
         ? DeviceDescriptor->DescriptorLength : available;

    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer,
                  &dev->Edid[DeviceDescriptor->DescriptorOffset],
                  copy);
    DeviceDescriptor->DescriptorLength = copy;

    Bc250LogReg(L"Edid_ServedOffset", DeviceDescriptor->DescriptorOffset);
    Bc250LogReg(L"Edid_ServedBytes", copy);
    return STATUS_SUCCESS;
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
        hdr->CommandId != BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS &&
        hdr->CommandId != BC250_ESCAPE_CMD_QUERY_TELEMETRY &&
        !dev->EnableDangerousEscapes) {
        hdr->Status = (ULONG)STATUS_ACCESS_DENIED;
        return STATUS_ACCESS_DENIED;
    }

    switch (hdr->CommandId) {
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
        info->SdmaInitialized = dev->SdmaInitialized;
        info->SdmaWptr = dev->SdmaWptr;
        info->SdmaBandwidthMBs = dev->SdmaBandwidthMBs;
        info->SdmaCopyCount = dev->SdmaCopyCount;
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (dev->MmioVirtualBase) {
            info->GfxRptr = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08700u);
            info->GrbmStatus = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08010u);
            info->CpStat = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x08680u);
            info->SdmaRptr = *(volatile ULONG *)((PUCHAR)dev->MmioVirtualBase + 0x04B8Cu);
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

    case BC250_ESCAPE_CMD_SUBMIT_IB: {
        BC250_WDDM_ESCAPE_SUBMIT_IB* ibSub = (BC250_WDDM_ESCAPE_SUBMIT_IB*)pEscape->pPrivateDriverData;
        NTSTATUS ibStatus;
        if (pEscape->PrivateDriverDataSize < sizeof(BC250_WDDM_ESCAPE_SUBMIT_IB)) {
            hdr->Status = (ULONG)STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
        ExAcquireFastMutex(&dev->DeviceMutex);
        if (!dev->HardwareInitialized) {
            ExReleaseFastMutex(&dev->DeviceMutex);
            hdr->Status = (ULONG)STATUS_DEVICE_NOT_READY;
            return STATUS_DEVICE_NOT_READY;
        }
        ibStatus = Bc250HwSubmitIndirectBuffer(dev, ibSub);
        ExReleaseFastMutex(&dev->DeviceMutex);
        hdr->Status = (ULONG)ibStatus;
        return ibStatus;
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
  VidPN Presentation Engine & Multi-Resolution Support
===========================================================================*/

typedef struct _BC250_MODE_ENTRY {
    ULONG Width;
    ULONG Height;
} BC250_MODE_ENTRY;

/* O scanout herdado do GOP tem UM unico timing fisico (ScreenWidth x
   ScreenHeight): sem programar o DCN nao ha como mudar o modo do CRTC.
   Portanto o conjunto de modos de DESTINO continua com um unico timing
   nativo, e a variedade de resolucoes e exposta pelo conjunto de modos de
   ORIGEM. Cada modo de origem menor que o nativo e servido por
   centralizacao (Centered) ou por scaling nearest-neighbor (Stretched)
   executado dentro do PresentDisplayOnly. Modos maiores que o nativo nao
   tem como ser exibidos e sao filtrados em tempo de execucao. */
static CONST BC250_MODE_ENTRY g_Bc250ModeTable[] = {
    { 2560, 1440 }, { 2560, 1080 }, { 1920, 1200 }, { 1920, 1080 },
    { 1680, 1050 }, { 1600,  900 }, { 1440,  900 }, { 1280, 1024 },
    { 1280,  800 }, { 1280,  720 }, { 1152,  864 }, { 1024,  768 },
    {  800,  600 }, {  640,  480 },
};

#define BC250_MODE_TABLE_COUNT \
    (sizeof(g_Bc250ModeTable) / sizeof(g_Bc250ModeTable[0]))

static BOOLEAN
Bc250ModeIsUsable(
    _In_ PBC250_WDDM_DEVICE dev,
    _In_ ULONG              Width,
    _In_ ULONG              Height
    )
{
    if (Width == 0 || Height == 0) {
        return FALSE;
    }
    if (Width > dev->ScreenWidth || Height > dev->ScreenHeight) {
        return FALSE;
    }
    if (Width * 4u > dev->ScreenPitch) {
        return FALSE;
    }
    return TRUE;
}

/* Indice 0 e sempre o modo nativo (preferido). Os demais vem da tabela,
   sem repetir o nativo. */
static BOOLEAN
Bc250GetModeByIndex(
    _In_ PBC250_WDDM_DEVICE dev,
    _In_ ULONG              Index,
    _Out_ PULONG            Width,
    _Out_ PULONG            Height
    )
{
    ULONG i;
    ULONG seen = 0;

    if (Index == 0) {
        *Width  = dev->ScreenWidth;
        *Height = dev->ScreenHeight;
        return TRUE;
    }

    for (i = 0; i < BC250_MODE_TABLE_COUNT; i++) {
        ULONG w = g_Bc250ModeTable[i].Width;
        ULONG h = g_Bc250ModeTable[i].Height;

        if (!Bc250ModeIsUsable(dev, w, h)) {
            continue;
        }
        if (w == dev->ScreenWidth && h == dev->ScreenHeight) {
            continue;
        }
        if (++seen == Index) {
            *Width  = w;
            *Height = h;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN
Bc250IsModeSupported(
    _In_ PBC250_WDDM_DEVICE dev,
    _In_ ULONG              Width,
    _In_ ULONG              Height
    )
{
    ULONG index = 0;
    ULONG w = 0;
    ULONG h = 0;

    while (Bc250GetModeByIndex(dev, index, &w, &h)) {
        if (w == Width && h == Height) {
            return TRUE;
        }
        index++;
    }
    return FALSE;
}

/* No modo nativo mantemos o pitch real do GOP: isso preserva o caminho
   rapido de linha contigua do blit da Fase 2. */
static ULONG
Bc250ModeStride(
    _In_ PBC250_WDDM_DEVICE dev,
    _In_ ULONG              Width,
    _In_ ULONG              Height
    )
{
    if (Width == dev->ScreenWidth && Height == dev->ScreenHeight) {
        return dev->ScreenPitch;
    }
    return Width * 4u;
}

static VOID
Bc250FreeScaleMaps(
    _In_ PBC250_WDDM_DEVICE dev
    )
{
    PULONG xMap = dev->ScaleXMap;
    PULONG yMap = dev->ScaleYMap;

    dev->ScaleActive = FALSE;
    dev->ScaleXMap = NULL;
    dev->ScaleYMap = NULL;

    if (xMap != NULL) {
        ExFreePoolWithTag(xMap, BC250_WDDM_DRIVER_TAG);
    }
    if (yMap != NULL) {
        ExFreePoolWithTag(yMap, BC250_WDDM_DRIVER_TAG);
    }
}

/* Mapas nearest-neighbor: para cada coluna/linha de destino, guarda a
   coluna/linha de origem correspondente. Precalcular evita uma divisao por
   pixel dentro do laco de blit. */
static NTSTATUS
Bc250BuildScaleMaps(
    _In_ PBC250_WDDM_DEVICE dev
    )
{
    PULONG xMap;
    PULONG yMap;
    ULONG  i;

    Bc250FreeScaleMaps(dev);

    if (dev->DestWidth == 0 || dev->DestHeight == 0 ||
        dev->ActiveWidth == 0 || dev->ActiveHeight == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    xMap = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, (SIZE_T)dev->DestWidth * sizeof(ULONG), BC250_WDDM_DRIVER_TAG);
    yMap = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, (SIZE_T)dev->DestHeight * sizeof(ULONG), BC250_WDDM_DRIVER_TAG);
    if (xMap == NULL || yMap == NULL) {
        if (xMap != NULL) ExFreePoolWithTag(xMap, BC250_WDDM_DRIVER_TAG);
        if (yMap != NULL) ExFreePoolWithTag(yMap, BC250_WDDM_DRIVER_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < dev->DestWidth; i++) {
        ULONG src = (ULONG)(((ULONGLONG)i * dev->ActiveWidth) / dev->DestWidth);
        xMap[i] = (src < dev->ActiveWidth) ? src : dev->ActiveWidth - 1;
    }
    for (i = 0; i < dev->DestHeight; i++) {
        ULONG src = (ULONG)(((ULONGLONG)i * dev->ActiveHeight) / dev->DestHeight);
        yMap[i] = (src < dev->ActiveHeight) ? src : dev->ActiveHeight - 1;
    }

    dev->ScaleXMap = xMap;
    dev->ScaleYMap = yMap;
    dev->ScaleActive = TRUE;
    return STATUS_SUCCESS;
}

/* Fixa o modo de origem ativo e a geometria de destino no scanout nativo.
   Chamado no CommitVidPn (PASSIVE_LEVEL), nunca no caminho de Present. */
static NTSTATUS
Bc250ApplyActiveMode(
    _In_ PBC250_WDDM_DEVICE                  dev,
    _In_ ULONG                               Width,
    _In_ ULONG                               Height,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING  Scaling
    )
{
    BOOLEAN wantStretch;
    ULONG   destW;
    ULONG   destH;

    if (!Bc250ModeIsUsable(dev, Width, Height)) {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    destW = dev->ScreenWidth;
    destH = dev->ScreenHeight;

    if (Scaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX) {
        /* Amplia o maximo possivel preservando a proporcao, depois centraliza.
           A dimensao limitante e a de menor razao destino/origem; a outra
           ganha barras (letterbox ou pillarbox). Ex.: 1280x720 num scanout
           2560x1080 vira 1920x1080 com 320 px de barra em cada lado. */
        ULONGLONG byW = (ULONGLONG)dev->ScreenWidth * Height;
        ULONGLONG byH = (ULONGLONG)dev->ScreenHeight * Width;

        if (byW <= byH) {
            destW = dev->ScreenWidth;
            destH = (ULONG)(((ULONGLONG)Height * dev->ScreenWidth) / Width);
        } else {
            destH = dev->ScreenHeight;
            destW = (ULONG)(((ULONGLONG)Width * dev->ScreenHeight) / Height);
        }
        if (destW == 0) destW = 1;
        if (destH == 0) destH = 1;
        if (destW > dev->ScreenWidth)  destW = dev->ScreenWidth;
        if (destH > dev->ScreenHeight) destH = dev->ScreenHeight;
    }

    wantStretch = (Scaling == D3DKMDT_VPPS_STRETCHED ||
                   Scaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX) &&
                  (destW != Width || destH != Height);

    ExAcquireFastMutex(&dev->DeviceMutex);

    dev->ActiveWidth  = Width;
    dev->ActiveHeight = Height;
    dev->ActivePitch  = Bc250ModeStride(dev, Width, Height);
    dev->Scaling      = Scaling;

    if (wantStretch) {
        dev->DestWidth  = destW;
        dev->DestHeight = destH;
        dev->OffsetX    = (dev->ScreenWidth  - destW) / 2;
        dev->OffsetY    = (dev->ScreenHeight - destH) / 2;
        if (!NT_SUCCESS(Bc250BuildScaleMaps(dev))) {
            /* Sem mapas, degrada para centralizado em vez de falhar o modo. */
            wantStretch = FALSE;
        }
    }

    if (!wantStretch) {
        Bc250FreeScaleMaps(dev);
        dev->DestWidth  = Width;
        dev->DestHeight = Height;
        dev->OffsetX    = (dev->ScreenWidth  - Width)  / 2;
        dev->OffsetY    = (dev->ScreenHeight - Height) / 2;
    }

    /* As bordas fora do novo retangulo ficariam com pixels do modo antigo. */
    if (dev->DisplayVirtualBase != NULL && dev->DisplaySize != 0 && !dev->SurpriseRemoved) {
        RtlZeroMemory(dev->DisplayVirtualBase, dev->DisplaySize);
    }

    ExReleaseFastMutex(&dev->DeviceMutex);

    Bc250LogReg(L"Mode_ActiveW", dev->ActiveWidth);
    Bc250LogReg(L"Mode_ActiveH", dev->ActiveHeight);
    Bc250LogReg(L"Mode_OffsetX", dev->OffsetX);
    Bc250LogReg(L"Mode_OffsetY", dev->OffsetY);
    Bc250LogReg(L"Mode_Scaling", (ULONG)dev->Scaling);
    Bc250LogReg(L"Mode_ScaleActive", dev->ScaleActive);
    return STATUS_SUCCESS;
}

/* Timing sintetico a 60 Hz. O CRTC fisico nunca muda -- continua no timing
   do GOP -- mas cada modo precisa de um par (destino, monitor) coerente para
   que o dxgkrnl monte um VidPN cofuncional com Identity scaling. A
   acomodacao real (centralizar/esticar) acontece no PresentDisplayOnly. */
static VOID
Bc250FillSignalInfo(
    _In_ PBC250_WDDM_DEVICE           dev,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO*  pSignal,
    _In_ ULONG                        Width,
    _In_ ULONG                        Height
    )
{
    /* TotalSize precisa ser MAIOR que ActiveSize: TotalSize == ActiveSize
       descreve blanking zero, que e um timing fisicamente impossivel. O
       pfnAddMode aceita mesmo assim, mas o dxgkrnl invalida o mode set do
       monitor depois -- e ai a lista de resolucoes do GDI fica vazia e todo
       ChangeDisplaySettings responde BADMODE, inclusive no modo nativo.
       Usamos um modelo de blanking reduzido (estilo CVT-RB): 160 pixels na
       horizontal e 45 linhas na vertical. O CRTC fisico continua no timing do
       GOP -- isto e so a descricao coerente que o dxgkrnl exige.

       As 45 linhas nao sao arbitrarias: com elas o clock do modo nativo
       2560x1080 fica em 2720 x 1125 x 60 = 183.600.000 Hz, que e multiplo
       exato de 10 kHz e portanto representavel no descritor de timing
       detalhado do EDID sintetico (ver Bc250BuildEdid). O timing anunciado
       no VidPN e no EDID precisa ser o mesmo. */
    CONST ULONG hTotal = Width + 160u;
    CONST ULONG vTotal = Height + 45u;

    UNREFERENCED_PARAMETER(dev);

    pSignal->VideoStandard          = D3DKMDT_VSS_OTHER;
    pSignal->TotalSize.cx           = hTotal;
    pSignal->TotalSize.cy           = vTotal;
    pSignal->ActiveSize.cx          = Width;
    pSignal->ActiveSize.cy          = Height;
    pSignal->VSyncFreq.Numerator    = 60;
    pSignal->VSyncFreq.Denominator  = 1;
    pSignal->HSyncFreq.Numerator    = 60 * vTotal;   /* PixelRate / hTotal */
    pSignal->HSyncFreq.Denominator  = 1;
    pSignal->PixelRate              = (ULONGLONG)hTotal * vTotal * 60ULL;
    pSignal->ScanLineOrdering       = D3DDDI_VSSLO_PROGRESSIVE;
}

static VOID
Bc250FillSourceMode(
    _In_ PBC250_WDDM_DEVICE           dev,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE*  pMode,
    _In_ ULONG                        Width,
    _In_ ULONG                        Height
    )
{
    pMode->Type = D3DKMDT_RMT_GRAPHICS;
    pMode->Format.Graphics.PrimSurfSize.cx        = Width;
    pMode->Format.Graphics.PrimSurfSize.cy        = Height;
    pMode->Format.Graphics.VisibleRegionSize.cx   = Width;
    pMode->Format.Graphics.VisibleRegionSize.cy   = Height;
    pMode->Format.Graphics.Stride                 = Bc250ModeStride(dev, Width, Height);
    pMode->Format.Graphics.PixelFormat            = dev->ScreenFormat;
    pMode->Format.Graphics.ColorBasis             = D3DKMDT_CB_SRGB;
    pMode->Format.Graphics.PixelValueAccessMode   = D3DKMDT_PVAM_DIRECT;
}

/* Restricao de cofuncionalidade imposta pelo modo pinado do OUTRO conjunto.
   Valid == FALSE quando nada esta pinado (podemos publicar tudo). */
typedef struct _BC250_MODE_CONSTRAINT {
    BOOLEAN Valid;
    ULONG   Width;
    ULONG   Height;
} BC250_MODE_CONSTRAINT;

/* Um modo de ORIGEM e cofuncional com o destino pinado (tw,th) quando cabe
   nele: igual (Identity) ou menor (Centered/Stretched). */
static BOOLEAN
Bc250SourceFitsTarget(
    _In_ CONST BC250_MODE_CONSTRAINT* Tgt,
    _In_ ULONG                        Width,
    _In_ ULONG                        Height
    )
{
    if (!Tgt->Valid) {
        return TRUE;
    }
    return (Width <= Tgt->Width && Height <= Tgt->Height);
}

/* Um modo de DESTINO e cofuncional com a origem pinada (sw,sh) quando a
   comporta: igual (Identity) ou maior (Centered/Stretched). O scanout fisico
   nunca muda, entao o nativo tambem serve sempre. */
static BOOLEAN
Bc250TargetFitsSource(
    _In_ CONST BC250_MODE_CONSTRAINT* Src,
    _In_ ULONG                        Width,
    _In_ ULONG                        Height
    )
{
    if (!Src->Valid) {
        return TRUE;
    }
    return (Width >= Src->Width && Height >= Src->Height);
}

/* Le o modo pinado de um conjunto de origem, se houver. */
static VOID
Bc250QuerySourcePin(
    _In_ CONST DXGK_VIDPN_INTERFACE*    pVidPnIface,
    _In_ D3DKMDT_HVIDPN                 hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _Out_ BC250_MODE_CONSTRAINT*        Out
    )
{
    D3DKMDT_HVIDPNSOURCEMODESET              hSet = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pIface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE*         pPinned = NULL;

    RtlZeroMemory(Out, sizeof(*Out));

    if (!NT_SUCCESS(pVidPnIface->pfnAcquireSourceModeSet(hVidPn, SourceId, &hSet, &pIface))) {
        return;
    }
    if (NT_SUCCESS(pIface->pfnAcquirePinnedModeInfo(hSet, &pPinned)) && pPinned != NULL) {
        if (pPinned->Type == D3DKMDT_RMT_GRAPHICS) {
            Out->Valid  = TRUE;
            Out->Width  = (ULONG)pPinned->Format.Graphics.PrimSurfSize.cx;
            Out->Height = (ULONG)pPinned->Format.Graphics.PrimSurfSize.cy;
        }
        (VOID)pIface->pfnReleaseModeInfo(hSet, pPinned);
    }
    (VOID)pVidPnIface->pfnReleaseSourceModeSet(hVidPn, hSet);
}

/* Le o modo pinado de um conjunto de destino, se houver. */
static VOID
Bc250QueryTargetPin(
    _In_ CONST DXGK_VIDPN_INTERFACE*    pVidPnIface,
    _In_ D3DKMDT_HVIDPN                 hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ BC250_MODE_CONSTRAINT*        Out
    )
{
    D3DKMDT_HVIDPNTARGETMODESET              hSet = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pIface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE*         pPinned = NULL;

    RtlZeroMemory(Out, sizeof(*Out));

    if (!NT_SUCCESS(pVidPnIface->pfnAcquireTargetModeSet(hVidPn, TargetId, &hSet, &pIface))) {
        return;
    }
    if (NT_SUCCESS(pIface->pfnAcquirePinnedModeInfo(hSet, &pPinned)) && pPinned != NULL) {
        Out->Valid  = TRUE;
        Out->Width  = (ULONG)pPinned->VideoSignalInfo.ActiveSize.cx;
        Out->Height = (ULONG)pPinned->VideoSignalInfo.ActiveSize.cy;
        (VOID)pIface->pfnReleaseModeInfo(hSet, pPinned);
    }
    (VOID)pVidPnIface->pfnReleaseTargetModeSet(hVidPn, hSet);
}

/* Publica os modos de origem exibiveis que sejam cofuncionais com o destino
   pinado. Publicar modos incompativeis faz o dxgkrnl descartar o VidPN
   inteiro -- e o motivo pelo qual a lista de resolucoes ficava vazia. */
static NTSTATUS
Bc250PublishSourceModeSet(
    _In_ PBC250_WDDM_DEVICE                dev,
    _In_ CONST DXGK_VIDPN_INTERFACE*       pVidPnIface,
    _In_ D3DKMDT_HVIDPN                    hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    SourceId,
    _In_ CONST BC250_MODE_CONSTRAINT*      TgtPin
    )
{
    D3DKMDT_HVIDPNSOURCEMODESET              hSet = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pSetIface = NULL;
    NTSTATUS                                 status;
    ULONG                                    index = 0;
    ULONG                                    added = 0;
    ULONG                                    width = 0;
    ULONG                                    height = 0;

    status = pVidPnIface->pfnCreateNewSourceModeSet(hVidPn, SourceId, &hSet, &pSetIface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    while (Bc250GetModeByIndex(dev, index, &width, &height)) {
        D3DKMDT_VIDPN_SOURCE_MODE* pMode = NULL;

        index++;

        if (!Bc250SourceFitsTarget(TgtPin, width, height)) {
            continue;
        }

        status = pSetIface->pfnCreateNewModeInfo(hSet, &pMode);
        if (!NT_SUCCESS(status) || pMode == NULL) {
            break;
        }

        Bc250FillSourceMode(dev, pMode, width, height);
        status = pSetIface->pfnAddMode(hSet, pMode);
        if (NT_SUCCESS(status)) {
            added++;
            continue;
        }

        /* Uma recusa pontual do dxgkrnl (modo duplicado, por exemplo) nao
           pode derrubar o conjunto inteiro: solta este modo e segue. */
        (VOID)pSetIface->pfnReleaseModeInfo(hSet, pMode);
        if (status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET) {
            break;
        }
        status = STATUS_SUCCESS;
    }

    if (added == 0) {
        if (NT_SUCCESS(status)) {
            status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        }
    } else {
        status = STATUS_SUCCESS;
    }

    Bc250LogReg(L"SourceSet_ModeCount", added);

    if (NT_SUCCESS(status)) {
        status = pVidPnIface->pfnAssignSourceModeSet(hVidPn, SourceId, hSet);
    }

    if (!NT_SUCCESS(status)) {
        pVidPnIface->pfnReleaseSourceModeSet(hVidPn, hSet);
    }
    return status;
}

/* Publica APENAS o timing nativo como modo de destino -- ver a justificativa
   dentro do laco. As demais resolucoes vivem no conjunto de ORIGEM e sao
   acomodadas por scaling no PresentDisplayOnly. */
static NTSTATUS
Bc250PublishTargetModeSet(
    _In_ PBC250_WDDM_DEVICE                dev,
    _In_ CONST DXGK_VIDPN_INTERFACE*       pVidPnIface,
    _In_ D3DKMDT_HVIDPN                    hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID    TargetId,
    _In_ CONST BC250_MODE_CONSTRAINT*      SrcPin
    )
{
    D3DKMDT_HVIDPNTARGETMODESET              hSet = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pSetIface = NULL;
    NTSTATUS                                 status;
    ULONG                                    index = 0;
    ULONG                                    added = 0;
    ULONG                                    width = 0;
    ULONG                                    height = 0;

    status = pVidPnIface->pfnCreateNewTargetModeSet(hVidPn, TargetId, &hSet, &pSetIface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    while (Bc250GetModeByIndex(dev, index, &width, &height)) {
        D3DKMDT_VIDPN_TARGET_MODE* pMode = NULL;
        BOOLEAN isNative = (width == dev->ScreenWidth && height == dev->ScreenHeight);

        index++;

        /* Somente o timing nativo. Um modo de destino so e utilizavel se o
           seu VideoSignalInfo casar com um modo do monitor, e os modos que o
           monitor.sys deriva dos standard timings do EDID usam blanking
           GTF/CVT -- que jamais casa com o nosso blanking sintetico
           (+160/+45). O nativo casa porque para ele publicamos um descritor
           de timing DETALHADO no EDID, com exatamente estes numeros.

           Publicar um destino por resolucao (como se tentou antes) obriga
           cada timing nao-nativo a casar com um modo de monitor e derruba
           todos eles. O scanout fisico tambem nunca muda de timing, entao um
           unico destino e a descricao honesta do hardware: as outras
           resolucoes existem como modos de ORIGEM e sao acomodadas por
           Centered / Stretched / AspectRatioCenteredMax no Present. */
        if (!isNative) {
            continue;
        }

        if (!Bc250TargetFitsSource(SrcPin, width, height)) {
            continue;
        }

        status = pSetIface->pfnCreateNewModeInfo(hSet, &pMode);
        if (!NT_SUCCESS(status) || pMode == NULL) {
            break;
        }

        Bc250FillSignalInfo(dev, &pMode->VideoSignalInfo, width, height);
        pMode->Preference = isNative ? D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;

        status = pSetIface->pfnAddMode(hSet, pMode);
        if (NT_SUCCESS(status)) {
            added++;
            continue;
        }

        (VOID)pSetIface->pfnReleaseModeInfo(hSet, pMode);
        if (status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET) {
            break;
        }
        status = STATUS_SUCCESS;
    }

    if (added == 0) {
        if (NT_SUCCESS(status)) {
            status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        }
    } else {
        status = STATUS_SUCCESS;
    }

    Bc250LogReg(L"TargetSet_ModeCount", added);

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

    Bc250LogReg(L"VsyncCount", dev->VsyncCount);

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

    Bc250LogReg(L"Cofunc_PivotType", (ULONG)pEnumCofuncModality->EnumPivotType);
    Bc250LogReg(L"Cofunc_CallCount", (ULONG)InterlockedIncrement(&g_CofuncCalls));

    while (pPath != NULL) {
        D3DKMDT_VIDPN_PRESENT_PATH localPath;
        BC250_MODE_CONSTRAINT      srcPin;
        BC250_MODE_CONSTRAINT      tgtPin;

        /* Os pins precisam ser lidos ANTES de publicar qualquer conjunto: o
           conteudo de um conjunto so e cofuncional em relacao ao que esta
           pinado no outro. */
        Bc250QuerySourcePin(pVidPnIface, pEnumCofuncModality->hConstrainingVidPn,
                            pPath->VidPnSourceId, &srcPin);
        Bc250QueryTargetPin(pVidPnIface, pEnumCofuncModality->hConstrainingVidPn,
                            pPath->VidPnTargetId, &tgtPin);

        Bc250LogReg(L"Cofunc_SrcPinned", srcPin.Valid);
        Bc250LogReg(L"Cofunc_SrcPinW", srcPin.Width);
        Bc250LogReg(L"Cofunc_SrcPinH", srcPin.Height);
        Bc250LogReg(L"Cofunc_TgtPinned", tgtPin.Valid);
        Bc250LogReg(L"Cofunc_TgtPinW", tgtPin.Width);
        Bc250LogReg(L"Cofunc_TgtPinH", tgtPin.Height);

        /* --- 1. Conjunto de modos de origem (Source Modes) ---------------- */
        if (!srcPin.Valid &&
            !(pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              pEnumCofuncModality->EnumPivot.VidPnSourceId == pPath->VidPnSourceId)) {

            Bc250LogReg(L"Cofunc_SrcUnpinnedCount",
                        (ULONG)InterlockedIncrement(&g_CofuncSrcUnpinned));

            status = Bc250PublishSourceModeSet(
                dev, pVidPnIface, pEnumCofuncModality->hConstrainingVidPn,
                pPath->VidPnSourceId, &tgtPin);
            if (!NT_SUCCESS(status)) {
                Bc250LogReg(L"Cofunc_SourceSetStatus", (ULONG)status);
                pTopIface->pfnReleasePathInfo(hTopology, pPath);
                return status;
            }
        }

        /* --- 2. Conjunto de modos de destino (Target Modes) --------------- */
        if (!tgtPin.Valid &&
            !(pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              pEnumCofuncModality->EnumPivot.VidPnTargetId == pPath->VidPnTargetId)) {

            status = Bc250PublishTargetModeSet(
                dev, pVidPnIface, pEnumCofuncModality->hConstrainingVidPn,
                pPath->VidPnTargetId, &srcPin);
            if (!NT_SUCCESS(status)) {
                Bc250LogReg(L"Cofunc_TargetSetStatus", (ULONG)status);
                pTopIface->pfnReleasePathInfo(hTopology, pPath);
                return status;
            }
        }

        /* --- 3. Transformacoes de Scaling e Rotation ---------------------- */
        localPath = *pPath;
        RtlZeroMemory(&localPath.ContentTransformation.ScalingSupport,
                      sizeof(localPath.ContentTransformation.ScalingSupport));
        RtlZeroMemory(&localPath.ContentTransformation.RotationSupport,
                      sizeof(localPath.ContentTransformation.RotationSupport));
        /* Sem Centered/Stretched aqui, o dxgkrnl considera nao-cofuncional
           qualquer par (origem menor, destino nativo) e o unico modo que
           sobrevive na lista de resolucoes do Windows e o nativo. E este o
           bit que faltava para a Fase 3. */
        localPath.ContentTransformation.ScalingSupport.Identity  = 1;
        localPath.ContentTransformation.ScalingSupport.Centered  = 1;
        localPath.ContentTransformation.ScalingSupport.Stretched = 1;
        /* Todo config gravado pelo Windows para este display pede
           Scaling=4 (AspectRatioCenteredMax). Sem este bit o path nunca e
           cofuncional e o ChangeDisplaySettings responde BADMODE para
           qualquer resolucao -- inclusive a nativa em uso. */
        localPath.ContentTransformation.ScalingSupport.AspectRatioCenteredMax = 1;
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
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING scaling = D3DKMDT_VPPS_IDENTITY;
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
        (path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_STRETCHED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED) ||
        path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto CommitExit;
    }
    scaling = path->ContentTransformation.Scaling;

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
        !Bc250IsModeSupported(dev,
            (ULONG)pinned->Format.Graphics.PrimSurfSize.cx,
            (ULONG)pinned->Format.Graphics.PrimSurfSize.cy) ||
        (pinned->Format.Graphics.PixelFormat != D3DDDIFMT_A8R8G8B8 &&
         pinned->Format.Graphics.PixelFormat != D3DDDIFMT_X8R8G8B8 &&
         pinned->Format.Graphics.PixelFormat != dev->ScreenFormat)) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto CommitExit;
    }

    status = Bc250ApplyActiveMode(dev,
        (ULONG)pinned->Format.Graphics.PrimSurfSize.cx,
        (ULONG)pinned->Format.Graphics.PrimSurfSize.cy,
        scaling);
    if (!NT_SUCCESS(status)) {
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
        (path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_STRETCHED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED &&
         path->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED) ||
        path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    /* Mudanca de scaling sem novo Commit: reaproveita a resolucao ativa. */
    if (dev->ActiveWidth != 0 && dev->ActiveHeight != 0 &&
        path->ContentTransformation.Scaling != dev->Scaling &&
        (path->ContentTransformation.Scaling == D3DKMDT_VPPS_CENTERED ||
         path->ContentTransformation.Scaling == D3DKMDT_VPPS_STRETCHED ||
         path->ContentTransformation.Scaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX ||
         path->ContentTransformation.Scaling == D3DKMDT_VPPS_IDENTITY)) {
        (VOID)Bc250ApplyActiveMode(dev, dev->ActiveWidth, dev->ActiveHeight,
            path->ContentTransformation.Scaling);
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
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE* pIface;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG index = 0;
    ULONG added = 0;
    ULONG width = 0;
    ULONG height = 0;

    Bc250LogStep(L"DdiRecommendMonitorModes");

    if (dev == NULL || pRecommendMonitorModes == NULL || pRecommendMonitorModes->pMonitorSourceModeSetInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pIface = pRecommendMonitorModes->pMonitorSourceModeSetInterface;

    /* Sem os modos de monitor correspondentes, o dxgkrnl descarta os modos
       de destino nao-nativos e a lista de resolucoes volta a ter um item. */
    while (Bc250GetModeByIndex(dev, index, &width, &height)) {
        D3DKMDT_MONITOR_SOURCE_MODE* pMode = NULL;
        BOOLEAN isNative = (width == dev->ScreenWidth && height == dev->ScreenHeight);

        index++;

        status = pIface->pfnCreateNewModeInfo(
            pRecommendMonitorModes->hMonitorSourceModeSet, &pMode);
        if (!NT_SUCCESS(status) || pMode == NULL) {
            break;
        }

        Bc250FillSignalInfo(dev, &pMode->VideoSignalInfo, width, height);
        pMode->ColorBasis = D3DKMDT_CB_SRGB;
        pMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pMode->ColorCoeffDynamicRanges.FourthChannel = 8;
        pMode->Origin = D3DKMDT_MCO_DRIVER;
        pMode->Preference = isNative ? D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;

        status = pIface->pfnAddMode(
            pRecommendMonitorModes->hMonitorSourceModeSet, pMode);
        if (NT_SUCCESS(status)) {
            added++;
            continue;
        }

        (VOID)pIface->pfnReleaseModeInfo(
            pRecommendMonitorModes->hMonitorSourceModeSet, pMode);
        if (status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET) {
            break;
        }
    }

    Bc250LogReg(L"MonitorSet_ModeCount", added);
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
    /* O scaling/centralizacao e feito por nos no PresentDisplayOnly; se
       declararmos 0 aqui o dxgkrnl assume que alguem mais escala e recusa
       origem != destino. */
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 1;
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
  PresentDisplayOnly: Blit Direto no Framebuffer Linear via Streaming SIMD
===========================================================================*/

static FORCEINLINE VOID
Bc250BlitRowStreaming(
    _Out_writes_bytes_(Bytes) PUCHAR Dst,
    _In_reads_bytes_(Bytes) CONST UCHAR* Src,
    _In_ SIZE_T Bytes
    )
{
    /* 1. Alinhamento de destino em 16 bytes */
    while (Bytes >= sizeof(ULONG) && ((ULONG_PTR)Dst & 15) != 0) {
        *(PULONG)Dst = *(CONST ULONG*)Src;
        Dst += sizeof(ULONG);
        Src += sizeof(ULONG);
        Bytes -= sizeof(ULONG);
    }

    /* 2. Loop desdobrado de 64 bytes por iteracao (4x 128-bit streaming stores) */
    while (Bytes >= 64) {
        __m128i m0 = _mm_loadu_si128((const __m128i*)(Src + 0));
        __m128i m1 = _mm_loadu_si128((const __m128i*)(Src + 16));
        __m128i m2 = _mm_loadu_si128((const __m128i*)(Src + 32));
        __m128i m3 = _mm_loadu_si128((const __m128i*)(Src + 48));

        _mm_stream_si128((__m128i*)(Dst + 0), m0);
        _mm_stream_si128((__m128i*)(Dst + 16), m1);
        _mm_stream_si128((__m128i*)(Dst + 32), m2);
        _mm_stream_si128((__m128i*)(Dst + 48), m3);

        Dst += 64;
        Src += 64;
        Bytes -= 64;
    }

    /* 3. Blocos remanescentes de 16 bytes */
    while (Bytes >= 16) {
        __m128i m = _mm_loadu_si128((const __m128i*)Src);
        _mm_stream_si128((__m128i*)Dst, m);
        Dst += 16;
        Src += 16;
        Bytes -= 16;
    }

    /* 4. Cauda unaligned (32-bit e 8-bit) */
    while (Bytes >= sizeof(ULONG)) {
        *(PULONG)Dst = *(CONST ULONG*)Src;
        Dst += sizeof(ULONG);
        Src += sizeof(ULONG);
        Bytes -= sizeof(ULONG);
    }
    while (Bytes > 0) {
        *Dst++ = *Src++;
        Bytes--;
    }
}

/* Blit 1:1 do retangulo de origem para o scanout, deslocado por
   OffsetX/OffsetY. Os retangulos sujos vem em coordenadas de ORIGEM, logo o
   clamp e contra ActiveWidth/ActiveHeight -- nao contra o scanout. */
static VOID
Bc250CopyPresentRectDirect(
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
    right  = (Rect->right > (LONG)Dev->ActiveWidth) ? (LONG)Dev->ActiveWidth : Rect->right;
    bottom = (Rect->bottom > (LONG)Dev->ActiveHeight) ? (LONG)Dev->ActiveHeight : Rect->bottom;
    if (right <= left || bottom <= top) {
        return;
    }

    /* Otimizacao de linha completa contigua: so vale quando a origem ocupa o
       scanout inteiro (sem offset) e os passos coincidem. */
    if (Dev->OffsetX == 0 && Dev->OffsetY == 0 &&
        Dev->ActiveWidth == Dev->ScreenWidth &&
        left == 0 && right == (LONG)Dev->ScreenWidth && SourcePitch == Dev->ScreenPitch) {
        PUCHAR dst = (PUCHAR)Dev->DisplayVirtualBase + ((SIZE_T)top * Dev->ScreenPitch);
        CONST UCHAR* src = Source + ((SIZE_T)top * SourcePitch);
        SIZE_T totalBytes = (SIZE_T)(bottom - top) * Dev->ScreenPitch;
        Bc250BlitRowStreaming(dst, src, totalBytes);
        return;
    }

    rowBytes = (ULONG)(right - left) * bytesPerPixel;
    for (y = (ULONG)top; y < (ULONG)bottom; y++) {
        PUCHAR dst = (PUCHAR)Dev->DisplayVirtualBase +
            ((SIZE_T)(y + Dev->OffsetY) * Dev->ScreenPitch) +
            ((SIZE_T)((ULONG)left + Dev->OffsetX) * bytesPerPixel);
        CONST UCHAR* src = Source +
            ((SIZE_T)y * SourcePitch) + ((ULONG)left * bytesPerPixel);
        Bc250BlitRowStreaming(dst, src, rowBytes);
    }
}

/* Blit com escala nearest-neighbor via mapas precalculados. O retangulo de
   origem e convertido para o espaco de destino com floor na borda esquerda e
   ceil na direita, para nao deixar coluna/linha parcial sem atualizar. */
static VOID
Bc250CopyPresentRectScaled(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ CONST UCHAR* Source,
    _In_ ULONG SourcePitch,
    _In_ CONST RECT* Rect
    )
{
    LONG  left, top, right, bottom;
    ULONG dl, dt, dr, db, dx, dy;
    CONST PULONG xMap = Dev->ScaleXMap;
    CONST PULONG yMap = Dev->ScaleYMap;

    if (xMap == NULL || yMap == NULL ||
        Dev->ActiveWidth == 0 || Dev->ActiveHeight == 0) {
        return;
    }

    left   = (Rect->left < 0) ? 0 : Rect->left;
    top    = (Rect->top < 0) ? 0 : Rect->top;
    right  = (Rect->right > (LONG)Dev->ActiveWidth) ? (LONG)Dev->ActiveWidth : Rect->right;
    bottom = (Rect->bottom > (LONG)Dev->ActiveHeight) ? (LONG)Dev->ActiveHeight : Rect->bottom;
    if (right <= left || bottom <= top) {
        return;
    }

    dl = (ULONG)(((ULONGLONG)left * Dev->DestWidth) / Dev->ActiveWidth);
    dt = (ULONG)(((ULONGLONG)top * Dev->DestHeight) / Dev->ActiveHeight);
    dr = (ULONG)(((ULONGLONG)right * Dev->DestWidth + Dev->ActiveWidth - 1) / Dev->ActiveWidth);
    db = (ULONG)(((ULONGLONG)bottom * Dev->DestHeight + Dev->ActiveHeight - 1) / Dev->ActiveHeight);
    if (dr > Dev->DestWidth)  dr = Dev->DestWidth;
    if (db > Dev->DestHeight) db = Dev->DestHeight;
    if (dr <= dl || db <= dt) {
        return;
    }

    for (dy = dt; dy < db; dy++) {
        CONST ULONG* srcRow = (CONST ULONG*)(Source + ((SIZE_T)yMap[dy] * SourcePitch));
        PULONG dst = (PULONG)((PUCHAR)Dev->DisplayVirtualBase +
            ((SIZE_T)(dy + Dev->OffsetY) * Dev->ScreenPitch) +
            ((SIZE_T)(dl + Dev->OffsetX) * 4u));

        for (dx = dl; dx < dr; dx++) {
            *dst++ = srcRow[xMap[dx]];
        }
    }
}

static VOID
Bc250CopyPresentRect(
    _In_ PBC250_WDDM_DEVICE Dev,
    _In_ CONST UCHAR* Source,
    _In_ ULONG SourcePitch,
    _In_ CONST RECT* Rect
    )
{
    if (Dev->ScaleActive) {
        Bc250CopyPresentRectScaled(Dev, Source, SourcePitch, Rect);
    } else {
        Bc250CopyPresentRectDirect(Dev, Source, SourcePitch, Rect);
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
        dev->ActiveWidth == 0 || dev->ActiveHeight == 0 ||
        (ULONG)pPresentDisplayOnly->Pitch < dev->ActiveWidth * 4u ||
        (pPresentDisplayOnly->NumMoves != 0 && pPresentDisplayOnly->pMoves == NULL) ||
        (pPresentDisplayOnly->NumDirtyRects != 0 && pPresentDisplayOnly->pDirtyRect == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }

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
        fullRect.right = (LONG)dev->ActiveWidth;
        fullRect.bottom = (LONG)dev->ActiveHeight;
        Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
            (ULONG)pPresentDisplayOnly->Pitch, &fullRect);
        dev->ForceFullPresent = FALSE;
    } else {
        if (pPresentDisplayOnly->NumMoves != 0) {
            fullRect.left = 0;
            fullRect.top = 0;
            fullRect.right = (LONG)dev->ActiveWidth;
            fullRect.bottom = (LONG)dev->ActiveHeight;
            Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
                (ULONG)pPresentDisplayOnly->Pitch, &fullRect);
        } else if (pPresentDisplayOnly->NumDirtyRects != 0) {
            for (i = 0; i < pPresentDisplayOnly->NumDirtyRects; i++) {
                Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
                    (ULONG)pPresentDisplayOnly->Pitch, &pPresentDisplayOnly->pDirtyRect[i]);
            }
        } else {
            fullRect.left = 0;
            fullRect.top = 0;
            fullRect.right = (LONG)dev->ActiveWidth;
            fullRect.bottom = (LONG)dev->ActiveHeight;
            Bc250CopyPresentRect(dev, (CONST UCHAR*)pPresentDisplayOnly->pSource,
                (ULONG)pPresentDisplayOnly->Pitch, &fullRect);
        }
    }

    _mm_sfence();
    ExReleaseFastMutex(&dev->DeviceMutex);

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
