#ifndef BC250_DISPLAY_DDI_H
#define BC250_DISPLAY_DDI_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_vram_cache_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BC250_DISPLAY_MODE {
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    D3DDDIFORMAT Format;
    ULONG RefreshRateNumerator;
    ULONG RefreshRateDenominator;
} BC250_DISPLAY_MODE;

typedef struct BC250_DISPLAY_STATE {
    DXGKRNL_INTERFACE DxgkInterface;
    DXGK_DISPLAY_INFORMATION PostDisplayInfo;
    ULONG DisplayAcquired;
    volatile LONG SourceVisible;
    volatile LONG VidPnCommitted;
    PHYSICAL_ADDRESS FrameBufferPhysical;
    PVOID FrameBufferVirtual;
    ULONG FrameBufferSize;
    BC250_DISPLAY_MODE CurrentMode;
    ULONG ModeCount;
    KTIMER VsyncTimer;
    KDPC VsyncDpc;
    BOOLEAN VsyncTimerInitialized;
    volatile LONG DisplayStarted;
    volatile LONG PowerStateD0;
    volatile LONG VsyncRequested;
    volatile LONG VsyncEnabled;
    volatile LONG VsyncCount;
    volatile LONG VsyncProofRecorded;
    volatile LONG IsSupportedVidPnCount;
    volatile LONG CommitVidPnCount;
    volatile LONG ControlInterruptCount;
    volatile LONG ActiveInterruptType;
    volatile LONG PresentCount;
    volatile LONG ForceFullPresent;
    PHYSICAL_ADDRESS MmioPhysical;
    volatile ULONG *OtgFrameCountRegister;
    ULONG LastOtgFrameCount;
    volatile LONG HardwareVsyncCount;
    BOOLEAN OtgVsyncActive;
    EX_RUNDOWN_REF PresentRundown;
    BOOLEAN PresentRundownInitialized;
    UCHAR Edid[128];
    BOOLEAN EdidValid;
    HANDLE hPrimaryAllocation;
    PHYSICAL_ADDRESS PrimaryAddress;
    BOOLEAN PrimaryAddressValid;
    /*
     * Contrato DXGKARG_SETVIDPNSOURCEADDRESS (WDK 10.0.26100, d3dkmddi.h):
     * o DMM entrega PrimarySegment + PrimaryAddress + hAllocation. Essa e' a
     * preservado para telemetria e diagnostico. No Present WDDM virtual, a
     * origem autoritativa e' o Slot 1 resolvido pelo snapshot GPUVA comitado.
     */
    UINT PrimarySegment;
    HANDLE hPrimarySourceAllocation;
} BC250_DISPLAY_STATE;

extern BC250_DISPLAY_STATE g_Bc250DisplayState;

NTSTATUS Bc250DisplayInit(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_ const DXGK_DISPLAY_INFORMATION *PostDisplayInfo);

void Bc250DisplaySetMmioBase(_In_ PHYSICAL_ADDRESS MmioPhysical);

/* DCN.1 (doc 710): flip pelo HUBP0 do DCN 2.0.1. Liga so' com a chave DcnFlip
 * do INF e o pipeline do GOP elegivel; 0 = copia do SDMA0 como antes. */
void Bc250DisplaySetDcnFlipEnabled(ULONG Enabled);
ULONG Bc250DisplayDcnFlipArmed(void);

/* DCN.2 (doc 713): bits da chave DcnExt do INF (padrao 0). */
#define BC250_DCN2_KEY_IRQ     0x01u /* VSYNC e flip pela interrupcao do DCN */
#define BC250_DCN2_KEY_CRC     0x02u /* CRC continuo do OTG0 */
#define BC250_DCN2_KEY_CURSOR  0x04u /* cursor por hardware no HUBP0/DPP0 */
#define BC250_DCN2_KEY_EDID    0x08u /* EDID real por AUX (+ HPD so' leitura) */
#define BC250_DCN2_KEY_DPMS    0x10u /* apagar/acender o OTG com o monitor */
#define BC250_DCN2_KEY_TIMING  0x20u /* timing medido do OTG0 no VidPn */
/* Propostos para a 274 (doc 716 par.5, doc 718). */
#define BC250_DCN2_KEY_IRQ_DEST    0x40u  /* D06, SO' LEITURA: roteamento OTG0_INTERRUPT_DEST/DCHUB_INTERRUPT_DEST2 */
#define BC250_DCN2_KEY_AUX_HW_MODE 0x80u  /* D12, ESCREVE: pad DDC1/AUX0 em modo hardware/AUX so' durante o EDID */
/* Proposto para a 275 (doc 720). */
#define BC250_DCN2_KEY_VTOTAL_TEST 0x100u /* D19, ESCREVE: prova que V_TOTAL muda o refresh; autocontido, devolve antes do desktop */
#define BC250_DCN2_KEY_ALL     0x1FFu
VOID Bc250DisplaySetDcnExt(ULONG Keys);
ULONG Bc250DisplayDcnExtKeys(void);
VOID Bc250DisplaySetDcnCursorVram(UINT64 VramOffset, UINT64 Bytes);
ULONG Bc250DisplayDcnCursorReady(void);
/* ISR do IH (DIRQL), cliente DCE: ack e DPC de VSYNC. TRUE = consumido. */
BOOLEAN Bc250DisplayDcnIsr(ULONG SrcId, ULONG ExtId);

void Bc250DisplaySetSyntheticEdidEnabled(BOOLEAN Enabled);
BOOLEAN Bc250DisplayGetSyntheticEdidEnabled(void);

void Bc250DisplayTeardown(void);

typedef BOOLEAN (*BC250_DISPLAY_GPUVA_RESOLVER)(
    _In_ HANDLE Context,
    _In_ UINT64 Va,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId);

void Bc250DisplayInstallGpuVaResolver(
    _In_opt_ BC250_DISPLAY_GPUVA_RESOLVER Resolver);

/*
 * DX.63: geometria (largura, altura, pitch, formato D3DDDI) da alocacao que o
 * dxgkrnl entrega em DXGK_PRESENTALLOCATIONINFO.hDeviceSpecificAllocation.
 * FALSE = geometria nao declarada ou handle desconhecido; o chamador falha
 * fechado, nunca inventa numeros.
 */
typedef BOOLEAN (*BC250_DISPLAY_ALLOCATION_GEOMETRY_RESOLVER)(
    _In_ HANDLE DeviceSpecificAllocation,
    _Out_ UINT32 *Width,
    _Out_ UINT32 *Height,
    _Out_ UINT32 *Pitch,
    _Out_ UINT32 *Format);

void Bc250DisplayInstallAllocationGeometryResolver(
    _In_opt_ BC250_DISPLAY_ALLOCATION_GEOMETRY_RESOLVER Resolver);

void Bc250DisplayRecordGpuVaMapping(
    _In_opt_ HANDLE ProcessHandle,
    _In_ UINT64 GpuVa,
    _In_ UINT64 PhysicalAddress,
    _In_ UINT64 ByteSize,
    _In_ UINT32 SegmentId);

BOOLEAN Bc250DisplayLookupPhysicalAddress(
    _In_opt_ HANDLE ProcessHandle,
    _In_ UINT64 GpuVa,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId);

void Bc250DisplayRegisterContextProcess(
    _In_ HANDLE ContextHandle,
    _In_opt_ HANDLE ProcessHandle);

void Bc250DisplayUnregisterContextProcess(
    _In_ HANDLE ContextHandle);

HANDLE Bc250DisplayGetContextProcess(
    _In_ HANDLE ContextHandle);

void Bc250DisplayClearGpuVaMappings(void);

BOOLEAN Bc250DisplayResolvePresentSource(
    _In_ HANDLE Context,
    _In_ const DXGKARG_PRESENT *Present,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId);

BOOLEAN Bc250DisplayResolvePresentDestination(
    _In_ HANDLE Context,
    _In_ const DXGKARG_PRESENT *Present,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId);

/*
 * Origem de scanout com autoridade: o par PrimarySegment/PrimaryAddress
 * publicado pelo ultimo DxgkDdiSetVidPnSourceAddress. Devolve FALSE enquanto
 * o DMM nao tiver publicado um endereco no segmento 1 (VRAM local).
 */
BOOLEAN Bc250DisplayResolvePrimarySource(
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId);

/*
 * Amostragem distribuida de uma superficie mapeada na abertura de VRAM.
 * Percorre Samples pontos igualmente espacados e devolve quantos sao
 * diferentes de zero e o XOR acumulado. Substitui a leitura de 4 DWORDs,
 * que nao distingue "buffer zerado" de "primeiros pixels pretos".
 */
BOOLEAN Bc250DisplaySampleSurface(
    _In_ UINT64 ByteOffset,
    _In_ ULONG ByteCount,
    _In_ ULONG Samples,
    _Out_ ULONG *NonZeroCount,
    _Out_ ULONG *XorHash);

/* Prova integral do blit: compara cada byte de duas faixas da abertura VRAM. */
BOOLEAN Bc250DisplayCompareSurfaceBytes(
    _In_ UINT64 SourceOffset,
    _In_ UINT64 DestinationOffset,
    _In_ ULONG ByteCount,
    _Out_ UINT64 *FirstMismatchOffset);

DXGKDDI_QUERY_CHILD_RELATIONS           Bc250DxgkDdiQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS              Bc250DxgkDdiQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR         Bc250DxgkDdiQueryDeviceDescriptor;
DXGKDDI_ISSUPPORTEDVIDPN                Bc250DxgkDdiIsSupportedVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN        Bc250DxgkDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY         Bc250DxgkDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEADDRESS           Bc250DxgkDdiSetVidPnSourceAddress;
DXGKDDI_SETVIDPNSOURCEVISIBILITY        Bc250DxgkDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN                    Bc250DxgkDdiCommitVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH   Bc250DxgkDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES          Bc250DxgkDdiRecommendMonitorModes;
DXGKDDI_RECOMMENDVIDPNTOPOLOGY         Bc250DxgkDdiRecommendVidPnTopology;
DXGKDDI_PRESENT                        Bc250DxgkDdiPresent;
DXGKDDI_CONTROLINTERRUPT               Bc250DxgkDdiControlInterrupt;
DXGKDDI_CONTROLINTERRUPT2              Bc250DxgkDdiControlInterrupt2;
DXGKDDI_PRESENTDISPLAYONLY             Bc250DxgkDdiPresentDisplayOnly;
DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
DXGKDDI_SYSTEM_DISPLAY_ENABLE          Bc250DxgkDdiSystemDisplayEnable;
DXGKDDI_SYSTEM_DISPLAY_WRITE           Bc250DxgkDdiSystemDisplayWrite;
DXGKDDI_SETPOINTERPOSITION             Bc250DxgkDdiSetPointerPosition;
DXGKDDI_SETPOINTERSHAPE                Bc250DxgkDdiSetPointerShape;
DXGKDDI_QUERYVIDPNHWCAPABILITY         Bc250DxgkDdiQueryVidPnHWCapability;
DXGKDDI_GET_CHILD_CONTAINER_ID         Bc250DxgkDdiGetChildContainerId;
DXGKDDI_QUERY_INTERFACE                 Bc250DxgkDdiQueryInterface;
DXGKDDI_QUERYADAPTERINFO                Bc250DxgkDdiQueryAdapterInfoDisplay;
DXGKDDI_SET_POWER_STATE                 Bc250DxgkDdiSetPowerState;
DXGKDDI_NOTIFY_ACPI_EVENT               Bc250DxgkDdiNotifyAcpiEvent;
DXGKDDI_SETPALETTE                      Bc250DxgkDdiSetPalette;
DXGKDDI_GETSCANLINE                     Bc250DxgkDdiGetScanLine;
DXGKDDI_CONTROL_ETW_LOGGING             Bc250DxgkDdiControlEtwLogging;
DXGKDDI_COLLECTDBGINFO                  Bc250DxgkDdiCollectDbgInfo;
DXGKDDI_SETDISPLAYPRIVATEDRIVERFORMAT   Bc250DxgkDdiSetDisplayPrivateDriverFormat;
DXGKDDI_NOTIFY_SURPRISE_REMOVAL         Bc250DxgkDdiNotifySurpriseRemoval;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT              Bc250DxgkDdiCheckMultiPlaneOverlaySupport;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2 Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2              Bc250DxgkDdiCheckMultiPlaneOverlaySupport2;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3;
DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3              Bc250DxgkDdiCheckMultiPlaneOverlaySupport3;
DXGKDDI_POSTMULTIPLANEOVERLAYPRESENT                Bc250DxgkDdiPostMultiPlaneOverlayPresent;
DXGKDDI_UPDATEMONITORLINKINFO            Bc250DxgkDdiUpdateMonitorLinkInfo;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
DXGKDDI_QUERYCONNECTIONCHANGE          Bc250DxgkDdiQueryConnectionChange;
DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY    Bc250DxgkDdiSetTargetAdjustedColorimetry;
DXGKDDI_SETTARGETCONTENTTYPE            Bc250DxgkDdiSetTargetContentType;
DXGKDDI_SETTARGETGAMMA                  Bc250DxgkDdiSetTargetGamma;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
DXGKDDI_SETTARGETADJUSTEDCOLORIMETRY2   Bc250DxgkDdiSetTargetAdjustedColorimetry2;
#endif

NTSTATUS Bc250DisplayPopulateCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData);

/*
 * Anuncia apenas as DDIs de base que a DRIVER_INITIALIZATION_DATA exige de
 * QUALQUER miniport, incluindo um adaptador sem display: enumeracao de
 * children (obrigatoria na estrutura, ainda que a lista venha vazia),
 * energia, QueryInterface, ETW, ACPI e surprise removal.
 *
 * Fica de fora toda a superficie de apresentacao — VidPn, Present, scanout,
 * ponteiro, palette, scanline, SystemDisplay e post-display ownership.  Essa
 * separacao e' o contrato do Gate 1.5: nao existe caminho de display
 * anunciado pela metade.
 */
NTSTATUS Bc250DisplayPopulateBaseOnlyCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData);

/*
 * Liga/desliga a enumeracao de children de video.  Deve ser chamada antes de
 * DxgkInitialize; o valor vale para todo o carregamento.
 *
 * O estado NAO vive em BC250_DISPLAY_STATE de proposito: Bc250DisplayInit
 * zera a estrutura inteira, e um perfil resolvido em DriverEntry nao pode
 * depender de nao ser sobrescrito por um init posterior.  O default (nunca
 * chamada) e' habilitado, preservando o comportamento herdado.
 */
void Bc250DisplaySetChildEnumeration(_In_ BOOLEAN Enabled);

_Check_return_
BOOLEAN Bc250DisplayGetChildEnumeration(void);

extern volatile LONG g_Bc250PagingActiveCount;

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
void Bc250DisplayLog(const char *Format, ...);
void Bc250DisplayLogSamplingSummary(void);
/*
 * LOG.1 (doc 585): drena o anel de log em RAM para o arquivo. So' a thread
 * do rastro duravel chama, em PASSIVE com APCs ligadas, a cada tique.
 */
void Bc250DisplayLogDrainFromWorker(void);
/*
 * TDRLOG.1 (Corrida 301, doc 762): passo do TDR gravado em
 * C:\BC250_TDR_PASSOS.txt com write-through e flush ANTES de o passo rodar.
 * Na 300 a maquina morreu entre o ResetFromTimeout e o restart, e as linhas
 * do anel em RAM se perderam. Tambem vai ao log normal. Ativa uma janela de
 * "TDR em curso" em que DDIs de paginacao/submissao deixam a propria linha.
 */
void Bc250TdrStep(const char *Format, ...);
/* Mesma linha, sem disco: para DDIs que podem rodar na thread de paginacao. */
void Bc250TdrStepRam(const char *Format, ...);
void Bc250TdrWindowSet(_In_ BOOLEAN Open);
_Check_return_ BOOLEAN Bc250TdrWindowIsOpen(void);
#else
static __inline void Bc250DisplayLog(const char *Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}
static __inline void Bc250TdrStep(const char *Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}
static __inline void Bc250TdrStepRam(const char *Format, ...)
{
    UNREFERENCED_PARAMETER(Format);
}
static __inline void Bc250TdrWindowSet(_In_ BOOLEAN Open)
{
    UNREFERENCED_PARAMETER(Open);
}
static __inline BOOLEAN Bc250TdrWindowIsOpen(void)
{
    return FALSE;
}
static __inline void Bc250DisplayLogSamplingSummary(void)
{
}
static __inline void Bc250DisplayLogDrainFromWorker(void)
{
}
#endif

/*
 * T3.1 (28/09, doc 520): todo mapeamento UNCACHED permanente da VRAM declara a
 * propria faixa e se ela cai inteira na faixa privada UC (fora da janela WC do
 * framebuffer). A proxima corrida sai com o mapa de tipos de cache MEDIDO, e
 * uma sobreposicao nova se declara em vez de passar calada.
 */
static __inline void Bc250LogUcVramMapping(
    const char *Site,
    unsigned long long Offset,
    unsigned long long Length)
{
    unsigned int privateUc = Bc250VramRangeIsPrivateUc(Offset, Length);
    Bc250DisplayLog("[T31_UC_MAP] %s: VRAM [0x%llX,+0x%llX) UNCACHED privada=%u%s\n",
        Site, Offset, Length, privateUc,
        privateUc ? "" : " -- SOBREPOE a janela WC (aliasing WC/UC)");
}

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
VOID Bc250ActiveSdmaRetirePendingOnAllAdapters(_In_ BOOLEAN FromDpc);
#else
static __inline VOID Bc250ActiveSdmaRetirePendingOnAllAdapters(BOOLEAN FromDpc)
{
    UNREFERENCED_PARAMETER(FromDpc);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
