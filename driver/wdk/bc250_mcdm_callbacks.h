#ifndef BC250_MCDM_CALLBACKS_H
#define BC250_MCDM_CALLBACKS_H

#include <ntddk.h>
#include <dispmprt.h>

DXGKDDI_BUILDPAGINGBUFFER Bc250McdmBuildPagingBuffer;
DXGKDDI_CALIBRATEGPUCLOCK Bc250McdmCalibrateGpuClock;
DXGKDDI_CLOSEALLOCATION Bc250McdmCloseAllocation;
DXGKDDI_COLLECTDBGINFO Bc250McdmCollectDbgInfo;
DXGKDDI_CREATEALLOCATION Bc250McdmCreateAllocation;
DXGKDDI_CREATECONTEXT Bc250McdmCreateContext;
DXGKDDI_CREATEDEVICE Bc250McdmCreateDevice;
DXGKDDI_CREATEPROCESS Bc250McdmCreateProcess;
DXGKDDI_DESCRIBEALLOCATION Bc250McdmDescribeAllocation;
DXGKDDI_DESTROYALLOCATION Bc250McdmDestroyAllocation;
DXGKDDI_DESTROYCONTEXT Bc250McdmDestroyContext;
DXGKDDI_DESTROYDEVICE Bc250McdmDestroyDevice;
DXGKDDI_DESTROYPROCESS Bc250McdmDestroyProcess;
DXGKDDI_DISPATCH_IO_REQUEST Bc250McdmDispatchIoRequest;
DXGKDDI_DPC_ROUTINE Bc250McdmDpcRoutine;
DXGKDDI_FORMATHISTORYBUFFER Bc250McdmFormatHistoryBuffer;
DXGKDDI_GETNODEMETADATA Bc250McdmGetNodeMetadata;
DXGKDDI_GETROOTPAGETABLESIZE Bc250McdmGetRootPageTableSize;
DXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA
    Bc250McdmGetStandardAllocationDriverData;
DXGKDDI_INTERRUPT_ROUTINE Bc250McdmInterruptRoutine;
DXGKDDI_OPENALLOCATIONINFO Bc250McdmOpenAllocation;
DXGKDDI_PREEMPTCOMMAND Bc250McdmPreemptCommand;
DXGKDDI_QUERYADAPTERINFO Bc250McdmQueryAdapterInfo;
DXGKDDI_QUERYDEPENDENTENGINEGROUP Bc250McdmQueryDependentEngineGroup;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR Bc250McdmQueryDeviceDescriptor;
DXGKDDI_QUERYENGINESTATUS Bc250McdmQueryEngineStatus;
/*
 * Fase V5 — DDIs obrigatorias que faltavam na tabela de render.
 *
 * A auditoria estatica de 2026-08-31 comparou DRIVER_INITIALIZATION_DATA (187
 * campos DxgkDdi*) com o que o driver preenchia (59) e encontrou estas
 * ausentes entre as classicamente obrigatorias de um miniport WDDM 2.x com
 * GPU VA. Ponteiro obrigatorio nulo faz o dxgkrnl abortar o start com
 * STATUS_INTERNAL_ERROR sem chamar nada — foi o que se observou.
 */
DXGKDDI_QUERYCURRENTFENCE Bc250McdmQueryCurrentFence;
DXGKDDI_ESCAPE Bc250McdmEscape;
DXGKDDI_ACQUIRESWIZZLINGRANGE Bc250McdmAcquireSwizzlingRange;
DXGKDDI_RELEASESWIZZLINGRANGE Bc250McdmReleaseSwizzlingRange;
DXGKDDI_PATCH Bc250McdmPatch;
DXGKDDI_RENDER Bc250McdmRender;
DXGKDDI_SUBMITCOMMAND Bc250McdmSubmitCommand;
DXGKDDI_RENDERKM Bc250McdmRenderKm;
DXGKDDI_CANCELCOMMAND Bc250McdmCancelCommand;
DXGKDDISETPOWERCOMPONENTFSTATE Bc250McdmSetPowerComponentFState;
DXGKDDIPOWERRUNTIMECONTROLREQUEST Bc250McdmPowerRuntimeControlRequest;

DXGKDDI_RESET_DEVICE Bc250McdmResetDevice;
DXGKDDI_RESETENGINE Bc250McdmResetEngine;
DXGKDDI_RESETFROMTIMEOUT Bc250McdmResetFromTimeout;
DXGKDDI_RESTARTFROMTIMEOUT Bc250McdmRestartFromTimeout;
DXGKDDI_SET_POWER_STATE Bc250McdmSetPowerState;
DXGKDDI_SETROOTPAGETABLE Bc250McdmSetRootPageTable;
DXGKDDI_SETSTABLEPOWERSTATE Bc250McdmSetStablePowerState;
DXGKDDI_SETVIRTUALMACHINEDATA Bc250McdmSetVirtualMachineData;
DXGKDDI_SUBMITCOMMANDVIRTUAL Bc250McdmSubmitCommandVirtual;
DXGKDDI_QUERY_CHILD_STATUS Bc250McdmQueryChildStatus;
DXGKDDI_QUERY_CHILD_RELATIONS Bc250McdmQueryChildRelations;
DXGKDDI_QUERYCONNECTIONCHANGE Bc250McdmQueryConnectionChange;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
DXGKDDI_SIGNALMONITOREDFENCE Bc250McdmSignalMonitoredFence;
#endif
DXGKDDI_UNLOAD Bc250McdmUnload;

typedef struct BC250_MCDM_RUNTIME_CAPS {
    UINT64 LocalSegmentBaseOffset;
    UINT64 LocalSegmentBytes;
    UINT64 CpuPhysicalStart;
    /*
     * Base da VRAM em espaco MC -- o endereco pelo qual a GPU enxerga o byte 0
     * da VRAM local. Todo endereco de VRAM gravado numa entrada de page table
     * PRECISA inclui-la; enderecos de memoria de sistema NAO.
     *
     * Medido na corrida de 05/09 00:55: o PDE em VRAM+0xA90008 continha
     * 0x00A91061, ou seja, o offset cru 0xA91000. A GPU caminha em MC e foi
     * buscar a tabela folha no MC 0x0000A91000, que nao e VRAM (ela comeca em
     * 0x450000000). O segundo nivel caiu no nada, o fetch do IB faultou e a
     * engine drenou o anel sem executar. Ver
     * docs/evidence/2026-09-05_P6_PT_WALK_GATE_OFFLINE.
     *
     * Este campo e a unica fonte da base, usada tanto pelo codificador de
     * entradas quanto pela instalacao da root, para que as duas nao possam
     * mais sair de fase.
     */
    UINT64 VramMcBase;
    UINT32 NodeCount;
    UINT32 Ready;
    UINT32 FullMcdmReady;
    UINT32 GpuVaReady;
    UINT32 PagingReady;
    UINT32 TdrReady;
    UINT32 PreemptionReady;
    UINT32 SubmitReady;
    UINT32 HistoryBufferReady;
    UINT32 GpuVaBitCount;
    UINT32 PageTableLevelCount;
    UINT32 PageTableIndexBitCount;
    UINT32 RootPageTableIndexBitCount;
    UINT32 PageTableSizeBytes;
    UINT32 PageTableAlignmentBytes;
} BC250_MCDM_RUNTIME_CAPS;

typedef NTSTATUS (*BC250_MCDM_QUERY_RUNTIME_CAPS)(
    _In_ HANDLE AdapterContext,
    _Out_ BC250_MCDM_RUNTIME_CAPS *Caps);

VOID Bc250McdmInstallRuntimeCapsProvider(
    _In_opt_ BC250_MCDM_QUERY_RUNTIME_CAPS Provider);

typedef NTSTATUS (*BC250_MCDM_CREATE_PROCESS_OBJECT)(
    _In_ HANDLE AdapterContext,
    _In_ const void *ExternalOwner,
    _Out_ HANDLE *ProcessHandle);

typedef NTSTATUS (*BC250_MCDM_DESTROY_PROCESS_OBJECT)(
    _In_ HANDLE AdapterContext,
    _In_ HANDLE ProcessHandle);

typedef NTSTATUS (*BC250_MCDM_CREATE_DEVICE_OBJECT)(
    _In_ HANDLE AdapterContext,
    _In_ HANDLE ProcessHandle,
    _Out_ HANDLE *DeviceHandle);

typedef NTSTATUS (*BC250_MCDM_DESTROY_DEVICE_OBJECT)(
    _In_ HANDLE DeviceHandle);

typedef NTSTATUS (*BC250_MCDM_CREATE_CONTEXT_OBJECT)(
    _In_ HANDLE DeviceHandle,
    _In_ UINT NodeOrdinal,
    _Out_ HANDLE *ContextHandle,
    _Out_ DXGK_CONTEXTINFO *ContextInfo);

typedef NTSTATUS (*BC250_MCDM_DESTROY_CONTEXT_OBJECT)(
    _In_ HANDLE ContextHandle);

typedef NTSTATUS (*BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT)(
    _In_ HANDLE AdapterContext,
    _In_ HANDLE ContextHandle,
    _In_ UINT32 SegmentId,
    _In_ UINT64 Address,
    _In_ UINT32 NumEntries,
    _In_ UINT32 MaxEntries);

typedef NTSTATUS (*BC250_MCDM_CREATE_ALLOCATION_OBJECTS)(
    _In_ HANDLE AdapterContext,
    _Inout_ DXGKARG_CREATEALLOCATION *CreateAllocation);

typedef NTSTATUS (*BC250_MCDM_DESTROY_ALLOCATION_OBJECTS)(
    _In_ HANDLE AdapterContext,
    _In_ const DXGKARG_DESTROYALLOCATION *DestroyAllocation);

typedef NTSTATUS (*BC250_MCDM_OPEN_ALLOCATION_OBJECTS)(
    _In_ HANDLE DeviceContext,
    _In_ const DXGKARG_OPENALLOCATION *OpenAllocation);

typedef NTSTATUS (*BC250_MCDM_CLOSE_ALLOCATION_OBJECTS)(
    _In_ HANDLE DeviceContext,
    _In_ const DXGKARG_CLOSEALLOCATION *CloseAllocation);

typedef NTSTATUS (*BC250_MCDM_COLLECT_DEBUG_SNAPSHOT)(
    _In_ HANDLE AdapterContext,
    _In_ const DXGKARG_COLLECTDBGINFO *CollectDebugInfo);

VOID Bc250McdmInstallProcessLifecycle(
    _In_opt_ BC250_MCDM_CREATE_PROCESS_OBJECT CreateProcessObject,
    _In_opt_ BC250_MCDM_DESTROY_PROCESS_OBJECT DestroyProcessObject);

VOID Bc250McdmInstallDeviceLifecycle(
    _In_opt_ BC250_MCDM_CREATE_DEVICE_OBJECT CreateDeviceObject,
    _In_opt_ BC250_MCDM_DESTROY_DEVICE_OBJECT DestroyDeviceObject);

VOID Bc250McdmInstallContextLifecycle(
    _In_opt_ BC250_MCDM_CREATE_CONTEXT_OBJECT CreateContextObject,
    _In_opt_ BC250_MCDM_DESTROY_CONTEXT_OBJECT DestroyContextObject);

VOID Bc250McdmInstallGpuVaLifecycle(
    _In_opt_ BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT SetRootPageTableObject);

VOID Bc250McdmInstallAllocationLifecycle(
    _In_opt_ BC250_MCDM_CREATE_ALLOCATION_OBJECTS CreateAllocationObjects,
    _In_opt_ BC250_MCDM_DESTROY_ALLOCATION_OBJECTS DestroyAllocationObjects,
    _In_opt_ BC250_MCDM_OPEN_ALLOCATION_OBJECTS OpenAllocationObjects,
    _In_opt_ BC250_MCDM_CLOSE_ALLOCATION_OBJECTS CloseAllocationObjects);

/*
 * B6 / item 25 (07/09): executor fisico do par de TDR. As DDIs
 * ResetFromTimeout/RestartFromTimeout devolviam STATUS_SUCCESS sem tocar em
 * registrador; agora elas dependem destes executores, e ponteiro NULO faz a
 * DDI falhar fechado em vez de mentir para o dxgkrnl.
 *
 * Executor de quiesce: desliga anel e trava o F32; devolve FALSE se nao houver
 * transporte. Executor de restart: rearma e PROVA por fence fisica.
 */
/*
 * item 24: probe de responsividade. Devolve FALSE quando nao foi possivel
 * medir; nesse caso a DDI recusa em vez de afirmar engine viva.
 */
typedef BOOLEAN (*BC250_MCDM_ENGINE_RESPONSIVE_PROBE)(UINT32 *IsResponsive);

typedef BOOLEAN (*BC250_MCDM_TDR_QUIESCE_EXECUTOR)(VOID);
typedef BOOLEAN (*BC250_MCDM_TDR_RESTART_EXECUTOR)(VOID);

VOID Bc250McdmInstallTdrExecutors(
    BC250_MCDM_TDR_QUIESCE_EXECUTOR QuiesceExecutor,
    BC250_MCDM_TDR_RESTART_EXECUTOR RestartExecutor);

VOID Bc250McdmInstallEngineResponsiveProbe(
    BC250_MCDM_ENGINE_RESPONSIVE_PROBE Probe);

VOID Bc250McdmInstallDebugLifecycle(
    _In_opt_ BC250_MCDM_COLLECT_DEBUG_SNAPSHOT CollectDebugSnapshot);

typedef VOID (*BC250_MCDM_UNLOAD_CLEANUP_HANDLER)(VOID);

VOID Bc250McdmInstallUnloadCleanupHandler(
    _In_opt_ BC250_MCDM_UNLOAD_CLEANUP_HANDLER Handler);

typedef NTSTATUS (*BC250_MCDM_SUBMIT_COMMAND_EXECUTOR)(
    _In_ HANDLE AdapterContext,
    _In_ CONST DXGKARG_SUBMITCOMMANDVIRTUAL *SubmitCommand);

VOID Bc250McdmInstallSubmissionExecutor(
    _In_opt_ BC250_MCDM_SUBMIT_COMMAND_EXECUTOR SubmitCommandExecutor);

/*
 * Provedor da fence concluida. Devolve a ultima fence que a GPU gravou na
 * VRAM para o no indicado. A CPU nunca inventa valor aqui: se o provedor nao
 * estiver instalado ou o motor nao estiver online, DxgkDdiQueryCurrentFence
 * falha em vez de reportar progresso.
 */
typedef NTSTATUS (*BC250_MCDM_CURRENT_FENCE_PROVIDER)(
    _In_ HANDLE AdapterContext,
    _In_ UINT NodeOrdinal,
    _Out_ UINT *CompletedFenceId);

/*
 * Preenche as DDIs obrigatorias exclusivas do caminho de render. Chamada
 * somente pelo miniport ativo; a imagem readonly nao deve expor submissao.
 */
NTSTATUS Bc250McdmPopulateRenderCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData);

VOID Bc250McdmInstallCurrentFenceProvider(
    _In_opt_ BC250_MCDM_CURRENT_FENCE_PROVIDER CurrentFenceProvider);

struct _DXGK_BUILDPAGINGBUFFER_FLUSHTLB;

typedef NTSTATUS (*BC250_MCDM_FLUSH_TLB_HANDLER)(
    _In_ HANDLE AdapterContext,
    _In_ CONST struct _DXGK_BUILDPAGINGBUFFER_FLUSHTLB *FlushTlb);

VOID Bc250McdmInstallFlushTlbHandler(
    _In_opt_ BC250_MCDM_FLUSH_TLB_HANDLER FlushTlbHandler);

typedef NTSTATUS (*BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER)(
    _In_ HANDLE AdapterContext,
    _In_ CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    _In_ DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *UpdatePageTable);

VOID Bc250McdmInstallUpdatePageTableHandler(
    _In_opt_ BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER UpdatePageTableHandler);

NTSTATUS Bc250McdmEncodeVidMmPte(
    _In_ CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    _In_ CONST DXGK_PTE *Source,
    _Out_ UINT64 *Encoded);

NTSTATUS Bc250McdmEncodeVidMmPteWithLevel(
    _In_ CONST BC250_MCDM_RUNTIME_CAPS *Caps,
    _In_ CONST DXGK_PTE *Source,
    _In_ UINT32 PageTableLevel,
    _Out_ UINT64 *Encoded);

typedef BOOLEAN (*BC250_MCDM_INTERRUPT_HANDLER)(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber);

typedef VOID (*BC250_MCDM_DPC_HANDLER)(
    _In_ CONST PVOID MiniportDeviceContext);

VOID Bc250McdmInstallInterruptLifecycle(
    _In_opt_ BC250_MCDM_INTERRUPT_HANDLER InterruptHandler,
    _In_opt_ BC250_MCDM_DPC_HANDLER DpcHandler);

NTSTATUS Bc250McdmPopulateRequiredCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData);

NTSTATUS Bc250McdmSetWddmContractVersion(
    _In_ DXGK_WDDMVERSION Version);
DXGK_WDDMVERSION Bc250McdmGetWddmContractVersion(VOID);

/*
 * ==========================================================================
 * MiscCaps.ComputeOnly declarado ao dxgkrnl.
 *
 * O DESTINO DO PROJETO E' WDDM DISPLAY+RENDER DE VERDADE: desktop acelerado,
 * Vulkan, jogos. ComputeOnly NAO e' direcao, e' andaime.
 *
 * Por que existe assim mesmo: o valor precisa concordar com o que o
 * StartDevice reporta em sources/children, e isso varia por perfil.
 *   - DISPLAY_RENDER  -> 1 source, 1 child  -> ComputeOnly = 0  (o produto)
 *   - DISPLAY_ONLY    -> KMDOD              -> ComputeOnly = 0
 *   - RENDER_ONLY_LAB -> 0 sources, 0 child -> ComputeOnly = 1  (laboratorio)
 *
 * A licao de 2026-08-31 esta' no comentario do case DRIVERCAPS: declarar
 * ComputeOnly=1 ENQUANTO se enumera uma fonte de video fez o dxgkrnl rejeitar
 * o adaptador com Kernel-PnP 411 / 0xC00000E5. O defeito nunca foi o bit
 * sozinho, foi o par incoerente. Amarrar o bit ao perfil e' o que impede que
 * o par volte a divergir.
 *
 * O default e' 0 de proposito: qualquer caminho que esqueca de chamar o
 * setter reporta o valor do PRODUTO, nunca o do laboratorio.
 *
 * O setter e' chamado exclusivamente por
 * Bc250BuildActiveInitializationDataForProfile, onde o perfil ja' foi
 * validado, e ha' teste de contrato exigindo 0 nos perfis de producao.
 * ==========================================================================
 */
NTSTATUS Bc250McdmSetComputeOnly(_In_ ULONG ComputeOnly);

/*
 * Devolve quantos dwords do ultimo paging buffer codificado sao UTEIS, isto e,
 * excluindo os NOPs de cauda que existem so para alinhar a GPUVA do proximo
 * buffer. Devolve 0 quando a publicacao nao casa com PublishedDwords -- nesse
 * caso o executor deve derivar o IB_SIZE do tamanho publicado pelo VidMm.
 * A leitura e destrutiva: so a primeira submissao apos o encode a consome.
 */
ULONG Bc250PagingConsumeIbUsefulDwords(_In_ ULONG PublishedDwords);

/*
 * Faixa de GPUVA [Start, End) que os pacotes do ultimo paging buffer
 * codificado vao ESCREVER. Leitura nao destrutiva. FALSE quando nada foi
 * publicado ainda.
 */
BOOLEAN Bc250PagingPeekIbDestRange(_Out_ UINT64 *Start, _Out_ UINT64 *End);

/*
 * Endereco fisico do ultimo paging buffer codificado, medido pela CPU. A
 * submissao compara com o PA que a caminhada de page table resolveu: se
 * divergirem, a GPU busca uma pagina que nao e a nossa. 0 = nada publicado.
 */
UINT64 Bc250PagingPeekIbCpuPhysical(void);
UINT64 Bc250PagingConsumeIbCpuPhysical(void);

/*
 * Parametros completos do ultimo FillVirtual codificado: GPUVA de destino,
 * contagem de bytes e padrao de preenchimento. Retorna FALSE se nao for FillVirtual.
 */
BOOLEAN Bc250PagingPeekFillParams(
    _Out_ UINT64 *DestVa,
    _Out_ ULONG *ByteCount,
    _Out_ ULONG *FillPattern);

void Bc250PagingClearFillParams(void);

ULONG Bc250McdmGetComputeOnly(VOID);

#endif
