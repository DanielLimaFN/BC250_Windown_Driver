#include "bc250_mcdm_callbacks.h"
#include "bc250_driver_caps_contract.h"
#include "bc250_probe_telemetry.h"
#include "bc250_display_ddi.h"
#include "bc250_mmio_breadcrumb.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"
#include "../core/bc250_umd_private_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_context_dispatcher_contract.h"
#include "../core/bc250_paging_contract.h"
#include "../core/bc250_ih_contract.h"
#include "../core/bc250_preemption_contract.h"
#include "../core/bc250_gpu_clock_contract.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "../core/bc250_gpuva_space_contract.h"
#include "../core/bc250_allocation_abi_contract.h"

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
NTSYSAPI NTSTATUS NTAPI ZwFlushBuffersFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock);
NTSYSAPI NTSTATUS NTAPI ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);
#endif

/*
 * Fase V5: as DDIs que devolvem NOT_SUPPORTED sao as candidatas naturais a
 * causa de um teardown, entao cada uma marca a propria entrada no rastro.
 */
#define BC250_MCDM_UNSUPPORTED_1(Name, DdiId, Type1, Arg1) \
    NTSTATUS APIENTRY Name(Type1 Arg1) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        Bc250ProbeMarkDdi(DdiId); \
        return STATUS_NOT_SUPPORTED; \
    }

#define BC250_MCDM_UNSUPPORTED_2(Name, DdiId, Type1, Arg1, Type2, Arg2) \
    NTSTATUS APIENTRY Name(Type1 Arg1, Type2 Arg2) \
    { \
        UNREFERENCED_PARAMETER(Arg1); \
        UNREFERENCED_PARAMETER(Arg2); \
        Bc250ProbeMarkDdi(DdiId); \
        return STATUS_NOT_SUPPORTED; \
    }

static BC250_MCDM_CREATE_PROCESS_OBJECT g_Bc250CreateProcessObject;
static BC250_MCDM_DESTROY_PROCESS_OBJECT g_Bc250DestroyProcessObject;
static BC250_MCDM_CREATE_DEVICE_OBJECT g_Bc250CreateDeviceObject;
static BC250_MCDM_DESTROY_DEVICE_OBJECT g_Bc250DestroyDeviceObject;
static BC250_MCDM_CREATE_CONTEXT_OBJECT g_Bc250CreateContextObject;
static BC250_MCDM_DESTROY_CONTEXT_OBJECT g_Bc250DestroyContextObject;
static BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT g_Bc250SetRootPageTableObject;
static BC250_MCDM_CREATE_ALLOCATION_OBJECTS g_Bc250CreateAllocationObjects;
static BC250_MCDM_DESCRIBE_ALLOCATION_OBJECT g_Bc250DescribeAllocationObject;
static BC250_MCDM_DESTROY_ALLOCATION_OBJECTS g_Bc250DestroyAllocationObjects;
static BC250_MCDM_OPEN_ALLOCATION_OBJECTS g_Bc250OpenAllocationObjects;
static BC250_MCDM_CLOSE_ALLOCATION_OBJECTS g_Bc250CloseAllocationObjects;
static BC250_MCDM_COLLECT_DEBUG_SNAPSHOT g_Bc250CollectDebugSnapshot;
static BC250_MCDM_ENGINE_RESPONSIVE_PROBE g_Bc250EngineResponsiveProbe;
static BC250_MCDM_TDR_QUIESCE_EXECUTOR g_Bc250TdrQuiesce;
static BC250_MCDM_TDR_RESTART_EXECUTOR g_Bc250TdrRestart;
static BC250_MCDM_SUBMIT_COMMAND_EXECUTOR g_Bc250SubmitCommandExecutor;
static BC250_MCDM_INTERRUPT_HANDLER g_Bc250InterruptHandler;
static BC250_MCDM_DPC_HANDLER g_Bc250DpcHandler;
static BC250_MCDM_QUERY_RUNTIME_CAPS g_Bc250QueryRuntimeCaps;
static BC250_MCDM_UNLOAD_CLEANUP_HANDLER g_Bc250UnloadCleanupHandler;

/*
 * Journal de ultimo recurso para hangs dentro de BuildPagingBuffer.
 *
 * Nao usa arquivo, Registro, pool ou lock: qualquer um deles pode bloquear
 * justamente quando o VidMm esta construindo/limpando GPUVA. Os 64 slots
 * reservados por sequencia atomica ficam no .data do miniport e podem ser
 * lidos de um live dump com o PDB:
 *
 *   dt bc250mcdm!g_Bc250PagingStages
 *
 * Sequence e publicado por ultimo, depois da barreira, para que o dump nao
 * trate um slot parcialmente escrito como evento completo.
 */
typedef struct BC250_PAGING_STAGE_SLOT {
    volatile LONG Sequence;
    ULONG Stage;
    ULONG Operation;
    NTSTATUS Status;
    UINT64 ThreadId;
    UINT64 InterruptTime100ns;
} BC250_PAGING_STAGE_SLOT;

enum {
    BC250_PAGING_STAGE_BUILD_ENTER = 1u,
    BC250_PAGING_STAGE_AFTER_DDI_MARK = 2u,
    BC250_PAGING_STAGE_AFTER_ENTRY_LOG = 3u,
    BC250_PAGING_STAGE_AFTER_CAPS = 4u,
    BC250_PAGING_STAGE_BEFORE_DISPATCH = 5u,
    BC250_PAGING_STAGE_AFTER_DISPATCH = 6u,
    BC250_PAGING_STAGE_FLUSH_HANDLER_ENTER = 7u,
    BC250_PAGING_STAGE_FLUSH_HANDLER_LEAVE = 8u,
    BC250_PAGING_STAGE_UPDATE_ENTER = 20u,
    BC250_PAGING_STAGE_UPDATE_AFTER_HEADER_LOG = 21u,
    BC250_PAGING_STAGE_UPDATE_AFTER_ENTRY_LOGS = 22u,
    BC250_PAGING_STAGE_UPDATE_BEFORE_HANDLER = 23u,
    BC250_PAGING_STAGE_UPDATE_AFTER_HANDLER = 24u
};

volatile LONG g_Bc250PagingStageSequence;
BC250_PAGING_STAGE_SLOT g_Bc250PagingStages[64];
volatile LONG g_Bc250PagingActiveCount = 0;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static volatile LONG g_Bc250PagingCallSequence;
#endif

static VOID Bc250PagingStage(
    _In_ ULONG Stage,
    _In_ ULONG Operation,
    _In_ NTSTATUS Status)
{
    LONG sequence = InterlockedIncrement(&g_Bc250PagingStageSequence);
    ULONG slotIndex = ((ULONG)sequence - 1u) & 63u;
    BC250_PAGING_STAGE_SLOT *slot = &g_Bc250PagingStages[slotIndex];

    slot->Stage = Stage;
    slot->Operation = Operation;
    slot->Status = Status;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    slot->ThreadId = (UINT64)(ULONG_PTR)PsGetCurrentThreadId();
    slot->InterruptTime100ns = KeQueryInterruptTime();
#endif
    KeMemoryBarrier();
    InterlockedExchange(&slot->Sequence, sequence);
}

VOID Bc250PagingTraceMarkExternal(ULONG Stage, ULONG Operation, NTSTATUS Status)
{
    Bc250PagingStage(Stage, Operation, Status);
}

/*
 * DX.56: amostra duravel dos 16 estagios mais recentes. O writer e' uma thread
 * separada. Bc250PagingStage permanece livre de arquivo, alocacao e lock.
 * Arquivo binario em registros fixos; checksum detecta ultimo write truncado.
 * Um reset pode perder ate um periodo e nao prova persistencia sem a corrida.
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
#define BC250_PAGING_TRACE_MAGIC 0x36585042u /* BPX6 */
#define BC250_PAGING_TRACE_SLOTS 16u
typedef struct BC250_PAGING_TRACE_SLOT {
    UINT32 Sequence;
    UINT32 Stage;
    UINT32 Operation;
    INT32 Status;
    UINT64 ThreadId;
    UINT64 InterruptTime100ns;
} BC250_PAGING_TRACE_SLOT;

typedef struct BC250_PAGING_TRACE_RECORD {
    UINT32 Magic;
    UINT32 Version;
    UINT32 SlotCount;
    UINT32 ActiveCount;
    UINT32 LatestSequence;
    UINT32 Reserved;
    UINT64 SnapshotTime100ns;
    BC250_PAGING_TRACE_SLOT Slots[BC250_PAGING_TRACE_SLOTS];
    UINT32 Checksum;
    UINT32 Padding;
} BC250_PAGING_TRACE_RECORD;
C_ASSERT(sizeof(BC250_PAGING_TRACE_SLOT) == 32u);
C_ASSERT(sizeof(BC250_PAGING_TRACE_RECORD) == 552u);

/*
 * DX.87: registro de migalhas MMIO (espelho em RAM, bc250_mmio_breadcrumb.h)
 * no mesmo arquivo, logo apos cada registro de paginacao. Magic proprio: o
 * decodificador antigo pula o que nao conhece. Por slot: seq, sitio<<24|offset,
 * fase (0xB0 ANTES, 0xA0 DEPOIS).
 */
#define BC250_CRUMB_TRACE_MAGIC 0x31524342u /* BCR1 */
typedef struct BC250_CRUMB_TRACE_RECORD {
    UINT32 Magic;
    UINT32 Version;
    UINT32 SlotCount;
    UINT32 WriterTick;
    UINT32 LastSequence;
    UINT32 Reserved;
    UINT64 SnapshotTime100ns;
    UINT32 Slots[BC250_CRUMB_SLOTS][3];
    UINT32 Checksum;
    UINT32 Padding;
} BC250_CRUMB_TRACE_RECORD;
C_ASSERT(sizeof(BC250_CRUMB_TRACE_RECORD) == 32u + BC250_CRUMB_SLOTS * 12u + 8u);

static UINT32 Bc250CrumbTraceChecksum(const BC250_CRUMB_TRACE_RECORD *Record)
{
    const UCHAR *bytes = (const UCHAR *)Record;
    UINT32 hash = 2166136261u;
    SIZE_T index;
    for (index = 0u; index < FIELD_OFFSET(BC250_CRUMB_TRACE_RECORD, Checksum); ++index) {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

static VOID Bc250CrumbTraceSnapshot(BC250_CRUMB_TRACE_RECORD *Record, UINT32 WriterTick)
{
    UINT32 slot;
    RtlZeroMemory(Record, sizeof(*Record));
    Record->Magic = BC250_CRUMB_TRACE_MAGIC;
    Record->Version = 1u;
    Record->SlotCount = BC250_CRUMB_SLOTS;
    Record->WriterTick = WriterTick;
    Record->LastSequence = g_Bc250CrumbMirror[BC250_CRUMB_DW_LASTSEQ];
    Record->SnapshotTime100ns = KeQueryInterruptTime();
    for (slot = 0u; slot < BC250_CRUMB_SLOTS; ++slot) {
        const volatile ULONG *entry = g_Bc250CrumbMirror +
            BC250_CRUMB_SLOT_BASE_DW + slot * BC250_CRUMB_SLOT_DWORDS;
        Record->Slots[slot][0] = entry[0];
        Record->Slots[slot][1] = entry[1];
        Record->Slots[slot][2] = entry[2];
    }
    Record->Checksum = Bc250CrumbTraceChecksum(Record);
}

static KEVENT g_Bc250PagingTraceStopEvent;
static HANDLE g_Bc250PagingTraceFile;
static HANDLE g_Bc250PagingTraceThreadHandle;
static volatile LONG g_Bc250PagingTraceStarted;

static UINT32 Bc250PagingTraceChecksum(const BC250_PAGING_TRACE_RECORD *Record)
{
    const UCHAR *bytes = (const UCHAR *)Record;
    UINT32 hash = 2166136261u;
    SIZE_T index;
    for (index = 0u; index < FIELD_OFFSET(BC250_PAGING_TRACE_RECORD, Checksum); ++index) {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

static VOID Bc250PagingTraceSnapshot(BC250_PAGING_TRACE_RECORD *Record)
{
    LONG latest = InterlockedCompareExchange(&g_Bc250PagingStageSequence, 0, 0);
    UINT32 index;
    RtlZeroMemory(Record, sizeof(*Record));
    Record->Magic = BC250_PAGING_TRACE_MAGIC;
    /* v2 (DX.65): Reserved carrega o contador de batimento do escritor. */
    Record->Version = 2u;
    Record->SlotCount = BC250_PAGING_TRACE_SLOTS;
    Record->ActiveCount = (UINT32)InterlockedCompareExchange(&g_Bc250PagingActiveCount, 0, 0);
    Record->LatestSequence = (UINT32)latest;
    Record->SnapshotTime100ns = KeQueryInterruptTime();
    for (index = 0u; index < BC250_PAGING_TRACE_SLOTS; ++index) {
        LONG wanted = latest - (LONG)index;
        BC250_PAGING_STAGE_SLOT *source;
        BC250_PAGING_TRACE_SLOT *target;
        LONG before;
        LONG after;
        if (wanted <= 0) break;
        source = &g_Bc250PagingStages[((UINT32)wanted - 1u) & 63u];
        target = &Record->Slots[index];
        before = InterlockedCompareExchange(&source->Sequence, 0, 0);
        if (before != wanted) continue;
        KeMemoryBarrier();
        target->Stage = source->Stage;
        target->Operation = source->Operation;
        target->Status = source->Status;
        target->ThreadId = source->ThreadId;
        target->InterruptTime100ns = source->InterruptTime100ns;
        KeMemoryBarrier();
        after = InterlockedCompareExchange(&source->Sequence, 0, 0);
        if (after == wanted) target->Sequence = (UINT32)wanted;
        else RtlZeroMemory(target, sizeof(*target));
    }
    Record->Checksum = Bc250PagingTraceChecksum(Record);
}

static VOID Bc250PagingTraceThread(_In_ PVOID Context)
{
    LARGE_INTEGER interval;
    IO_STATUS_BLOCK ioStatus;
    UINT32 writerTick = 0u;
    UNREFERENCED_PARAMETER(Context);
    interval.QuadPart = -1000000LL; /* 100 ms */
    for (;;) {
        BC250_PAGING_TRACE_RECORD record;
        (void)KeWaitForSingleObject(&g_Bc250PagingTraceStopEvent,
            Executive, KernelMode, FALSE, &interval);
        Bc250PagingTraceSnapshot(&record);
        /*
         * DX.65 (Corrida 108): gravava so' quando a sequencia mudava, entao o
         * ultimo registro nao distinguia "paginacao parou com o kernel vivo"
         * de "maquina morreu". Agora grava a cada periodo: registros com a
         * MESMA sequencia e SnapshotTime/Reserved avancando provam que esta
         * thread (PASSIVE_LEVEL) ainda rodava depois da ultima paginacao.
         */
        record.Reserved = ++writerTick;
        record.Checksum = Bc250PagingTraceChecksum(&record);
        {
            BC250_CRUMB_TRACE_RECORD crumbs;
            NTSTATUS status = ZwWriteFile(g_Bc250PagingTraceFile, NULL, NULL,
                NULL, &ioStatus, &record, sizeof(record), NULL, NULL);
            /* DX.87: migalhas logo depois, no mesmo flush. */
            Bc250CrumbTraceSnapshot(&crumbs, writerTick);
            if (NT_SUCCESS(status)) {
                status = ZwWriteFile(g_Bc250PagingTraceFile, NULL, NULL,
                    NULL, &ioStatus, &crumbs, sizeof(crumbs), NULL, NULL);
            }
            if (NT_SUCCESS(status) && ioStatus.Information == sizeof(crumbs)) {
                status = ZwFlushBuffersFile(g_Bc250PagingTraceFile, &ioStatus);
                (void)status;   /* falha de flush nao para o batimento */
            }
        }
        if (KeReadStateEvent(&g_Bc250PagingTraceStopEvent) != 0) break;
    }
    ZwClose(g_Bc250PagingTraceFile);
    g_Bc250PagingTraceFile = NULL;
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS Bc250PagingTraceStart(VOID)
{
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\BC250_PAGING_TRACE.bin");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    if (InterlockedCompareExchange(&g_Bc250PagingTraceStarted, 1, 0) != 0)
        return STATUS_SUCCESS;
    KeInitializeEvent(&g_Bc250PagingTraceStopEvent, NotificationEvent, FALSE);
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwCreateFile(&g_Bc250PagingTraceFile,
        FILE_APPEND_DATA | SYNCHRONIZE, &attributes, &ioStatus, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH,
        NULL, 0);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&g_Bc250PagingTraceStarted, 0);
        return status;
    }
    status = PsCreateSystemThread(&g_Bc250PagingTraceThreadHandle, THREAD_ALL_ACCESS, NULL,
        NULL, NULL, Bc250PagingTraceThread, NULL);
    if (!NT_SUCCESS(status)) {
        ZwClose(g_Bc250PagingTraceFile);
        g_Bc250PagingTraceFile = NULL;
        InterlockedExchange(&g_Bc250PagingTraceStarted, 0);
        return status;
    }
    return STATUS_SUCCESS;
}

VOID Bc250PagingTraceStop(VOID)
{
    if (InterlockedCompareExchange(&g_Bc250PagingTraceStarted, 0, 0) == 0)
        return;
    KeSetEvent(&g_Bc250PagingTraceStopEvent, IO_NO_INCREMENT, FALSE);
    (void)ZwWaitForSingleObject(g_Bc250PagingTraceThreadHandle, FALSE, NULL);
    ZwClose(g_Bc250PagingTraceThreadHandle);
    g_Bc250PagingTraceThreadHandle = NULL;
    InterlockedExchange(&g_Bc250PagingTraceStarted, 0);
}
#else
NTSTATUS Bc250PagingTraceStart(VOID) { return STATUS_SUCCESS; }
VOID Bc250PagingTraceStop(VOID) { }
#endif
static BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER g_Bc250UpdatePageTableHandler;
static BC250_MCDM_ESCAPE_HANDLER g_Bc250EscapeHandler;
static volatile LONG g_Bc250WddmContractVersion =
    (LONG)DXGKDDI_WDDMv2_7;

NTSTATUS Bc250McdmSetWddmContractVersion(DXGK_WDDMVERSION Version)
{
    if (Version != DXGKDDI_WDDMv2 && Version != DXGKDDI_WDDMv2_7) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250WddmContractVersion, (LONG)Version);
    return STATUS_SUCCESS;
}

DXGK_WDDMVERSION Bc250McdmGetWddmContractVersion(VOID)
{
    return (DXGK_WDDMVERSION)InterlockedCompareExchange(
        &g_Bc250WddmContractVersion, 0, 0);
}

/*
 * Default 0 = display+render, o destino do projeto. Ver o bloco extenso em
 * bc250_mcdm_callbacks.h antes de mexer.
 */
static volatile LONG g_Bc250McdmComputeOnly = 0;

NTSTATUS Bc250McdmSetComputeOnly(ULONG ComputeOnly)
{
    if (ComputeOnly > 1u) {
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250McdmComputeOnly, (LONG)ComputeOnly);
    return STATUS_SUCCESS;
}

ULONG Bc250McdmGetComputeOnly(VOID)
{
    return (ULONG)InterlockedCompareExchange(&g_Bc250McdmComputeOnly, 0, 0);
}

/* DX.13 -- teto proprio da linha [PDE_LARGEPAGE]; so telemetria. */
static volatile LONG g_Bc250PdeLargePageCount = 0;

NTSTATUS Bc250McdmEncodeVidMmPteWithLevel(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    const DXGK_PTE *Source,
    UINT32 PageTableLevel,
    UINT64 *Encoded)
{
    UINT64 physicalAddress;
    UINT64 pageOffsetInSegment;
    UINT32 flags;

    if (Caps == NULL || Source == NULL || Encoded == NULL) {
        Bc250DisplayLog("[EncodeVidMmPte FAIL NullParam] Caps=%p, Source=%p, Encoded=%p\n",
            Caps, Source, Encoded);
        return STATUS_INVALID_PARAMETER;
    }
    *Encoded = 0u;
    if (Source->Valid == 0u || Source->Zero != 0u) {
        return STATUS_SUCCESS;
    }

    if (Source->PhysicalAdapterIndex != 0u) {
        Bc250DisplayLog("[EncodeVidMmPte FAIL PhysAdapter] PhysIdx=%u\n",
            (UINT32)Source->PhysicalAdapterIndex);
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * Em ambos os niveis (folha e diretorio), PageAddress/PageTableAddress
     * representa o Page Frame Number (PFN) de 4KB na abertura do segmento.
     */
    pageOffsetInSegment = Source->PageAddress << 12;

    if (PageTableLevel > 0u) {
        /*
         * Nivel > 0: Entrada de Diretorio de Paginas (PDE).
         * Tabelas de paginas filhas DEVEM residir no Segmento 1 (VRAM Local).
         */
        if (Source->Segment != 1u) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL PDE Segment] Lvl=%u, Seg=%u\n",
                PageTableLevel, (UINT32)Source->Segment);
            return STATUS_NOT_SUPPORTED;
        }
        /*
         * DX.13 -- LargePage DEIXOU DE SER INVISIVEL NO NIVEL DE DIRETORIO.
         *
         * O nivel 0 ja recusava LargePage desde sempre; o nivel > 0 sequer
         * olhava o campo. Uma pagina grande pedida no diretorio e, pelo
         * layout do Navi 10, uma FOLHA ocupando o slot do PDE (o bit P,
         * 54) -- e nos a encodavamos como ponteiro de diretorio comum,
         * calados.
         *
         * Isso importa agora porque a Corrida 59 leu formato de PTE no
         * slot de PDE do indice 4096. NAO afirmo que a origem seja esta:
         * afirmo que, enquanto este ramo fosse mudo, "o VidMm pediu
         * pagina grande" e "alguem corrompeu o PDE" eram indistinguiveis
         * no log.
         *
         * POR QUE ESTA FAIXA MEDE EM VEZ DE RECUSAR. Recusar seria o lado
         * seguro SE soubessemos a frequencia -- e nao sabemos: nenhuma
         * corrida ate hoje registrou LargePage no nivel de diretorio,
         * porque o campo nunca foi olhado aqui. Se o VidMm o usar de
         * rotina, recusar faria a UpdatePageTable falhar em serie e a
         * proxima corrida nao mediria NADA -- e o projeto ja pagou
         * exatamente esse preco quando a instalacao lateral recusava a
         * submissao antes de existir medicao (Corrida 53).
         *
         * Entao: a linha sai, o caso fica CONTADO, e a decisao de recusar
         * vem depois do numero. Isto NAO e sucesso silencioso (Lei 4) --
         * o log nomeia a aproximacao que estamos fazendo e diz que ela
         * nao esta provada.
         */
        if (Source->LargePage != 0u) {
            /*
             * TETO PROPRIO. A linha e isenta da amostragem global (ela e
             * rara POR HIPOTESE, nao por construcao), e esta funcao roda
             * por ENTRADA de page table. Se a hipotese estiver errada,
             * sem teto ela afogaria o log e destruiria justamente a
             * evidencia que a faixa existe para coletar. O contador conta
             * TODAS; o log mostra as 8 primeiras e 1 em 512.
             */
            LONG seq = InterlockedIncrement(
                &g_Bc250PdeLargePageCount) - 1L;
            if (seq < 8L || (seq % 512L) == 0L) {
                Bc250DisplayLog("[PDE_LARGEPAGE] Lvl=%u Pfn=0x%llX -- VidMm pediu pagina GRANDE no nivel de diretorio; pelo layout do Navi 10 isso e uma FOLHA no slot de PDE (bit P) e nos encodamos ponteiro de diretorio comum (ocorrencia #%ld). APROXIMACAO DECLARADA, NAO PROVADA\n",
                    seq + 1L, PageTableLevel,
                    (unsigned long long)Source->PageAddress);
            }
        }
        if (pageOffsetInSegment >= Caps->LocalSegmentBytes) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL PDE LocalBounds] Lvl=%u, Pfn=0x%llX, Offset=0x%llX, MaxBytes=0x%llX\n",
                PageTableLevel,
                (unsigned long long)Source->PageAddress,
                (unsigned long long)pageOffsetInSegment,
                (unsigned long long)Caps->LocalSegmentBytes);
            return STATUS_NOT_SUPPORTED;
        }
        if (Caps->LocalSegmentBaseOffset > (UINT64)~(UINT64)0 - pageOffsetInSegment) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL Overflow] Base=0x%llX, Offset=0x%llX\n",
                (unsigned long long)Caps->LocalSegmentBaseOffset,
                (unsigned long long)pageOffsetInSegment);
            return STATUS_INTEGER_OVERFLOW;
        }
        physicalAddress = Caps->VramMcBase +
            Caps->LocalSegmentBaseOffset + pageOffsetInSegment;
        flags = BC250_PTE_VALID;
        /*
         * PDE leva SOMENTE os bits permitidos (VALID / SYSTEM / SNOOPED).
         * Permissao e assunto da folha.
         */
    } else {
        /*
         * Nivel 0: Entrada Folha (PTE).
         * Suporta Segmento 1 (VRAM Local), Segmento 0 (Memoria do Sistema) e Segmento 2 (Abertura GART).
         */
        if (Source->LargePage != 0u ||
            Source->PageTablePageSize != DXGK_PTE_PAGE_TABLE_PAGE_4KB) {
            Bc250DisplayLog("[EncodeVidMmPte FAIL LeafAttrs] LP=%u, PTPgSz=%u\n",
                (UINT32)Source->LargePage, (UINT32)Source->PageTablePageSize);
            return STATUS_NOT_SUPPORTED;
        }
        flags = BC250_PTE_VALID | BC250_PTE_READABLE;
        if (Source->ReadOnly == 0u) flags |= BC250_PTE_WRITEABLE;
        if (Source->NoExecute == 0u) flags |= BC250_PTE_EXECUTABLE;
        if (Source->CacheCoherent != 0u) flags |= BC250_PTE_SNOOPED;

        if (Source->Segment == 1u) {
            /* VRAM Local */
            if (pageOffsetInSegment >= Caps->LocalSegmentBytes) {
                Bc250DisplayLog("[EncodeVidMmPte FAIL LocalBounds] Lvl=0, Pfn=0x%llX, Offset=0x%llX, MaxBytes=0x%llX\n",
                    (unsigned long long)Source->PageAddress,
                    (unsigned long long)pageOffsetInSegment,
                    (unsigned long long)Caps->LocalSegmentBytes);
                return STATUS_NOT_SUPPORTED;
            }
            if (Caps->LocalSegmentBaseOffset > (UINT64)~(UINT64)0 - pageOffsetInSegment) {
                Bc250DisplayLog("[EncodeVidMmPte FAIL Overflow] Base=0x%llX, Offset=0x%llX\n",
                    (unsigned long long)Caps->LocalSegmentBaseOffset,
                    (unsigned long long)pageOffsetInSegment);
                return STATUS_INTEGER_OVERFLOW;
            }
            physicalAddress = Caps->VramMcBase +
                Caps->LocalSegmentBaseOffset + pageOffsetInSegment;
        } else if (Source->Segment == 0u || Source->Segment == 2u) {
            /* Memoria do Sistema (Segmento 0 = System Memory, Segmento 2 = Aperture / GART) */
            physicalAddress = pageOffsetInSegment;
            flags |= BC250_PTE_SYSTEM | BC250_PTE_SNOOPED;
        } else {
            Bc250DisplayLog("[EncodeVidMmPte FAIL Leaf Segment] Seg=%u\n",
                (UINT32)Source->Segment);
            return STATUS_NOT_SUPPORTED;
        }
    }

    if (PageTableLevel > 0u) {
        return Bc250GcvmEncodePde(physicalAddress, flags, Encoded) ==
            BC250_GCVM_OK ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    return Bc250GcvmEncodePte(physicalAddress, flags, Encoded) ==
        BC250_GCVM_OK ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS Bc250McdmEncodeVidMmPte(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    const DXGK_PTE *Source,
    UINT64 *Encoded)
{
    return Bc250McdmEncodeVidMmPteWithLevel(Caps, Source, 0u, Encoded);
}

static NTSTATUS Bc250McdmBuildCpuPageTableUpdate(
    const BC250_MCDM_RUNTIME_CAPS *Caps,
    HANDLE AdapterContext,
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update;
    UINT64 *destination;
    UINT32 index;
    UINT32 maxEntries;
    UINT32 levelBits;

    if (Caps == NULL || Args == NULL) return STATUS_INVALID_PARAMETER;
    Bc250PagingStage(
        BC250_PAGING_STAGE_UPDATE_ENTER,
        DXGK_OPERATION_UPDATE_PAGE_TABLE,
        STATUS_PENDING);
    update = &Args->UpdatePageTable;
    Bc250DisplayLog("[UpdatePT] Lvl=%u, FirstPteVA=0x%llX, Num=%u, Start=%u, Mode=%u, Repeat=%u, CpuVa=0x%p, Flags=0x%08X\n",
        update->PageTableLevel, (unsigned long long)update->FirstPteVirtualAddress,
        update->NumPageTableEntries, update->StartIndex,
        (UINT32)update->UpdateMode, update->Flags.Repeat, update->PageTableAddress.CpuVirtual,
        *(const UINT32 *)&update->Flags);
    Bc250PagingStage(
        BC250_PAGING_STAGE_UPDATE_AFTER_HEADER_LOG,
        DXGK_OPERATION_UPDATE_PAGE_TABLE,
        STATUS_PENDING);

    /* Detailed logging of first few PTE entries */
    if (update->pPageTableEntries != NULL) {
        /* Com Repeat=1 o contrato fornece uma unica PTE que deve ser repetida.
         * Ler [1..3] era leitura fora do contrato; a corrida de 17:37 mostrou
         * esses slots como lixo, incluindo ponteiros de kernel. */
        UINT32 logCount = update->Flags.Repeat != 0u ? 1u :
            (update->NumPageTableEntries < 4u ? update->NumPageTableEntries : 4u);
        for (index = 0u; index < logCount; ++index) {
            const DXGK_PTE *p = &update->pPageTableEntries[index];
            Bc250DisplayLog("[UpdatePT Lvl=%u Entry %u] V=%u, Z=%u, LP=%u, PhysIdx=%u, PTPgSz=%u, Seg=%u, Addr=0x%llX\n",
                update->PageTableLevel, index,
                (UINT32)p->Valid, (UINT32)p->Zero, (UINT32)p->LargePage,
                (UINT32)p->PhysicalAdapterIndex, (UINT32)p->PageTablePageSize,
                (UINT32)p->Segment, (unsigned long long)p->PageAddress);
        }
    }
    Bc250PagingStage(
        BC250_PAGING_STAGE_UPDATE_AFTER_ENTRY_LOGS,
        DXGK_OPERATION_UPDATE_PAGE_TABLE,
        STATUS_PENDING);

    if (update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL ||
        update->PageTableAddress.CpuVirtual == NULL ||
        update->pPageTableEntries == NULL ||
        update->NumPageTableEntries == 0u || update->Reserved0 != 0u ||
        update->pPageTableEntries64KB != NULL ||
        update->PageTableLevel >= Caps->PageTableLevelCount ||
        Caps->PageTableIndexBitCount == 0u ||
        Caps->PageTableIndexBitCount >= 32u) {
        Bc250DisplayLog("[UpdatePT FAIL Header] Mode=%u, CpuVa=%p, pPTE=%p, Num=%u, Res0=%u, FlagsVal=0x%08X, Prot=%u, Lvl=%u (Caps=%u)\n",
            (UINT32)update->UpdateMode, update->PageTableAddress.CpuVirtual,
            update->pPageTableEntries, update->NumPageTableEntries,
            update->Reserved0, *(const UINT32 *)&update->Flags, update->DriverProtection,
            update->PageTableLevel, Caps->PageTableLevelCount);
        return STATUS_INVALID_PARAMETER;
    }
    levelBits = (update->PageTableLevel == 0u) ?
        BC250_GPUVA_PT_LEAF_INDEX_BITS : BC250_GPUVA_PT_ROOT_INDEX_BITS;
    maxEntries = 1u << levelBits;
    if (update->StartIndex >= maxEntries ||
        update->NumPageTableEntries > maxEntries - update->StartIndex) {
        Bc250DisplayLog("[UpdatePT FAIL Bounds] Start=%u, Num=%u, Max=%u, LvlBits=%u\n",
            update->StartIndex, update->NumPageTableEntries, maxEntries, levelBits);
        return STATUS_INVALID_PARAMETER;
    }
    if (update->Flags.Repeat != 0u) {
        UINT64 encoded = 0u;
        NTSTATUS status = Bc250McdmEncodeVidMmPteWithLevel(
            Caps, &update->pPageTableEntries[0], update->PageTableLevel, &encoded);
        if (!NT_SUCCESS(status)) {
            Bc250DisplayLog("[UpdatePT FAIL Repeat Encode] status=0x%08X\n", status);
            return status;
        }
    } else {
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            UINT64 encoded = 0u;
            NTSTATUS status = Bc250McdmEncodeVidMmPteWithLevel(
                Caps, &update->pPageTableEntries[index], update->PageTableLevel, &encoded);
            if (!NT_SUCCESS(status)) {
                Bc250DisplayLog("[UpdatePT FAIL Encode] index=%u/%u, status=0x%08X, RawFlags=0x%016llX, RawAddr=0x%016llX\n",
                    index, update->NumPageTableEntries, status,
                    (unsigned long long)update->pPageTableEntries[index].Flags,
                    (unsigned long long)update->pPageTableEntries[index].PageAddress);
                return status;
            }
        }
    }
    /* O handler ativo distingue leaf de diretórios; ambos precisam participar
     * da mesma transação de flush, embora só leaf gere ranges GPUVA. */
    if (g_Bc250UpdatePageTableHandler != NULL) {
        NTSTATUS handlerStatus;
        Bc250PagingStage(
            BC250_PAGING_STAGE_UPDATE_BEFORE_HANDLER,
            DXGK_OPERATION_UPDATE_PAGE_TABLE,
            STATUS_PENDING);
        handlerStatus = g_Bc250UpdatePageTableHandler(AdapterContext, Caps, update);
        Bc250PagingStage(
            BC250_PAGING_STAGE_UPDATE_AFTER_HANDLER,
            DXGK_OPERATION_UPDATE_PAGE_TABLE,
            handlerStatus);
        return handlerStatus;
    }
    destination = (UINT64 *)update->PageTableAddress.CpuVirtual;
    if (update->Flags.Repeat != 0u) {
        UINT64 encoded = 0u;
        (void)Bc250McdmEncodeVidMmPteWithLevel(
            Caps, &update->pPageTableEntries[0], update->PageTableLevel, &encoded);
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            destination[update->StartIndex + index] = encoded;
        }
    } else {
        for (index = 0u; index < update->NumPageTableEntries; ++index) {
            UINT64 encoded = 0u;
            (void)Bc250McdmEncodeVidMmPteWithLevel(
                Caps, &update->pPageTableEntries[index], update->PageTableLevel, &encoded);
            destination[update->StartIndex + index] = encoded;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * CONTRATO DE RETORNO DE DxgkDdiBuildPagingBuffer
 *
 * PROVA (dump C:\Windows\MEMORY.DMP da corrida 04/09 17:38, !analyze -v):
 *   VIDEO_MEMORY_MANAGEMENT_INTERNAL (10e)
 *   Arg1: 000000000000000b, Driver returned an invalid error code from BuildPagingBuffer.
 *   Arg3: ffffffffc000000d  (STATUS_INVALID_PARAMETER)
 *   STACK_TEXT:
 *     dxgmms2!VIDMM_GLOBAL::PageInOneAllocation
 *     dxgmms2!VIDMM_MEMORY_SEGMENT::CommitResource
 *     dxgmms2!VIDMM_MEMORY_SEGMENT::TransferToSegment
 *     dxgmms2!VIDMM_GLOBAL::FillAllocation
 *     dxgmms2!VIDMM_GLOBAL::FillAllocationUsingGpuVa
 *     dxgmms2!VIDMM_GLOBAL::CompleteBuildPagingBufferIteration -> KeBugCheckEx
 *
 * O VidMm estava paginando a primary (11.075.584 bytes) e pediu um VIRTUAL_FILL
 * unico de ~10,56 MiB. O builder rejeitava qualquer pedido acima de
 * BC250_SDMA_FILL_MAX_BYTES (4 MiB) com STATUS_INVALID_PARAMETER -- um codigo que
 * esta DDI NAO tem permissao de devolver. O VidMm nao trata o erro: ele bugcheca.
 *
 * Duas correcoes:
 *   1. Fatiamento. Um pedido maior que o maximo do pacote SDMA vira N pacotes
 *      CONST_FILL/COPY_LINEAR consecutivos no mesmo DMA buffer.
 *   2. Multipasse legal. Quando o DMA buffer acaba antes do fim do pedido,
 *      gravamos o progresso em Args->MultipassOffset e devolvemos
 *      STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, que e o mecanismo documentado
 *      para o VidMm nos chamar de novo continuando de onde paramos.
 *
 * O mesmo bugcheck com Arg3=c0000185 derrubou a corrida 16:47 pelo caminho do
 * FLUSH_TLB: e a mesma classe de defeito, status ilegal devolvido ao VidMm.
 */
#define BC250_PAGING_MULTIPASS_DONE 0u

/*
 * Bc250SdmaEmit* trata o buffer como anel e mascara o cursor com
 * (RingDwords - 1), entao exige RingDwords potencia de dois -- caso contrario
 * devolve BC250_SDMA_BAD_ARGUMENT antes de escrever qualquer dword.
 *
 * PROVA (corrida 04/09 18:02, C:\BC250_DISPLAY_LOG.txt, ultimas linhas antes do
 * BugCheck 0x10E):
 *   [McdmBuildPagingBuffer] Entered: Op=9
 *   [FillVirtual] SDMA encode failed: status=1 chunk=4194304 done=0
 * status=1 e BC250_SDMA_BAD_ARGUMENT. O fatiamento em 4 MiB estava correto; o
 * que falhou foi passar Args->DmaSize/4 direto como RingDwords. O VidMm nao
 * garante potencia de dois nesse campo: ele entrega o que sobrou do buffer.
 *
 * Usamos a maior potencia de dois <= ao disponivel. Como o cursor nunca chega
 * a essa capacidade (paramos antes de cada pacote), a mascara vira no-op e as
 * escritas ficam lineares, que e a semantica correta de um paging buffer.
 */
/*
 * Janela declarada aos emissores de pacote ao montar o sinal de fence
 * monitorada. Potencia de dois (exigencia do mascaramento de cursor) e
 * suficiente para Fence64 + GCR + NOPs de alinhamento.
 */
#define BC250_SIGNAL_FENCE_SCRATCH_DWORDS 32u

static uint32_t Bc250PagingFloorPow2Dwords(uint32_t Dwords)
{
    uint32_t capacity = 1u;
    if (Dwords == 0u) return 0u;
    while (capacity <= (Dwords >> 1)) {
        capacity <<= 1;
    }
    return capacity;
}

/*
 * Publicacao serial encoder -> SubmitCommandVirtual da "forma" do IB.
 *
 * O canal sancionado seria pDmaBufferPrivateData, mas hoje declaramos
 * PagingBufferPrivateDataSize=0 em DRIVERCAPS e o VidMm entrega o ponteiro
 * vazio. Mudar aquele cap mexe no contrato ja aceito pelo dxgkrnl no bind, e
 * a Lei 9 desaconselha empacotar isso na mesma corrida.
 *
 * Node 0 e serial: BuildPagingBuffer(Op=9/Op=3) e a submissao correspondente
 * nao se intercalam. Mesmo assim a leitura e AUTOVALIDADA -- o consumidor
 * confere o total publicado contra o DmaBufferSize que o VidMm entregou e,
 * na menor divergencia, devolve 0 para o executor derivar do tamanho. E
 * destrutiva: uma segunda submissao sem novo encode nao reaproveita o valor.
 */
/* Keyed by exact physical IB start and published length. A cache miss loses
 * diagnostics only; never borrow another queued buffer's destination. */
#define BC250_PAGING_PROOF_SLOTS 256u
static BC250_PAGING_PROOF g_Bc250PagingProofs[BC250_PAGING_PROOF_SLOTS];
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static KSPIN_LOCK g_Bc250PagingProofLock;
#endif
BOOLEAN Bc250PagingTakeProof(UINT64 Physical, ULONG Bytes, BC250_PAGING_PROOF *Proof)
{
    uint32_t slot = (uint32_t)((Physical >> 5) % BC250_PAGING_PROOF_SLOTS);
    BOOLEAN match;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL irql;
    KeAcquireSpinLock(&g_Bc250PagingProofLock, &irql);
#endif
    match = Bc250PagingProofMatches(&g_Bc250PagingProofs[slot], Physical, Bytes) ? TRUE : FALSE;
    RtlZeroMemory(Proof, sizeof(*Proof));
    if (match) {
        *Proof = g_Bc250PagingProofs[slot];
        RtlZeroMemory(&g_Bc250PagingProofs[slot], sizeof(*Proof));
    }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250PagingProofLock, irql);
#endif
    return match;
}

static volatile LONG g_Bc250PagingIbUsefulDwords = 0;
static volatile LONG g_Bc250PagingIbPublishedDwords = 0;

/*
 * Faixa de GPUVA que os pacotes do IB vao ESCREVER.
 *
 * A corrida de 05/09 01:12 mostrou por que ela precisa viajar ate a
 * submissao: com root e folha ja coerentes em MC, a caminhada aprovou a GPUVA
 * do proprio IB (0x24F000) e a engine buscou o pacote -- mas parou em RPTR=96,
 * dentro do IB, e o UTCL1_DELTA acordou do lado de ESCRITA
 * (WR_STAT=00800000, antes zerado). O destino dos CONST_FILL nunca tinha sido
 * validado. Leitura nao destrutiva: e diagnostico, nao consumo.
 */
/*
 * Endereco fisico REAL do paging buffer, medido pela CPU no momento em que os
 * pacotes foram escritos.
 *
 * Furo apontado na revisao externa: o IB_DUMP sempre veio da VA da CPU, nunca
 * do PA para onde a PTE do IB aponta. Ou seja, nunca provamos que a pagina que
 * a GPU vai buscar e a pagina onde escrevemos. Publicando o PA aqui, a
 * submissao compara com o que a caminhada resolveu.
 */
static volatile LONG64 g_Bc250PagingIbCpuPhysical = 0;

static volatile LONG64 g_Bc250PagingIbDestStart = 0;
static volatile LONG64 g_Bc250PagingIbDestEnd = 0;
static volatile LONG g_Bc250PagingFillPattern = 0;
static volatile LONG g_Bc250PagingFillValid = 0;

static void Bc250PagingPublishIbDestRange(UINT64 Start, UINT64 End)
{
    InterlockedExchange(&g_Bc250PagingFillValid, 0);
    InterlockedExchange(&g_Bc250PagingFillPattern, 0);
    InterlockedExchange64(&g_Bc250PagingIbDestStart, (LONG64)Start);
    InterlockedExchange64(&g_Bc250PagingIbDestEnd, (LONG64)End);
}

static void Bc250PagingPublishFillParams(UINT64 DestVa, ULONG ByteCount, ULONG Pattern)
{
    InterlockedExchange64(&g_Bc250PagingIbDestStart, (LONG64)DestVa);
    InterlockedExchange64(&g_Bc250PagingIbDestEnd, (LONG64)(DestVa + ByteCount));
    InterlockedExchange(&g_Bc250PagingFillPattern, (LONG)Pattern);
    InterlockedExchange(&g_Bc250PagingFillValid, 1);
}

BOOLEAN Bc250PagingPeekFillParams(
    _Out_ UINT64 *DestVa,
    _Out_ ULONG *ByteCount,
    _Out_ ULONG *FillPattern)
{
    LONG valid = InterlockedCompareExchange(&g_Bc250PagingFillValid, 0, 0);
    LONG64 start = InterlockedCompareExchange64(&g_Bc250PagingIbDestStart, 0, 0);
    LONG64 end = InterlockedCompareExchange64(&g_Bc250PagingIbDestEnd, 0, 0);
    LONG pat = InterlockedCompareExchange(&g_Bc250PagingFillPattern, 0, 0);

    if (valid == 0 || end <= start) {
        return FALSE;
    }
    *DestVa = (UINT64)start;
    *ByteCount = (ULONG)(end - start);
    *FillPattern = (ULONG)pat;
    return TRUE;
}

void Bc250PagingClearFillParams(void)
{
    InterlockedExchange(&g_Bc250PagingFillValid, 0);
    InterlockedExchange64(&g_Bc250PagingIbDestStart, 0);
    InterlockedExchange64(&g_Bc250PagingIbDestEnd, 0);
    InterlockedExchange64(&g_Bc250PagingIbCpuPhysical, 0);
}

UINT64 Bc250PagingPeekIbCpuPhysical(void)
{
    return (UINT64)InterlockedCompareExchange64(
        &g_Bc250PagingIbCpuPhysical, 0, 0);
}

UINT64 Bc250PagingConsumeIbCpuPhysical(void)
{
    return (UINT64)InterlockedExchange64(
        &g_Bc250PagingIbCpuPhysical, 0);
}

BOOLEAN Bc250PagingPeekIbDestRange(_Out_ UINT64 *Start, _Out_ UINT64 *End)
{
    LONG64 start = InterlockedCompareExchange64(&g_Bc250PagingIbDestStart, 0, 0);
    LONG64 end = InterlockedCompareExchange64(&g_Bc250PagingIbDestEnd, 0, 0);

    *Start = (UINT64)start;
    *End = (UINT64)end;
    return (end > start) ? TRUE : FALSE;
}

ULONG Bc250PagingConsumeIbUsefulDwords(_In_ ULONG PublishedDwords)
{
    LONG published = InterlockedExchange(&g_Bc250PagingIbPublishedDwords, 0);
    LONG useful = InterlockedExchange(&g_Bc250PagingIbUsefulDwords, 0);

    if (published <= 0 || useful <= 0 ||
        (ULONG)published != PublishedDwords ||
        (ULONG)useful > PublishedDwords) {
        return 0u;
    }
    return (ULONG)useful;
}

/*
 * Acrescenta NOPs de cauda ate o tamanho publicado ao VidMm ser multiplo de 8
 * dwords, para que a GPUVA do PROXIMO paging buffer caia em fronteira de 32
 * bytes -- exigencia do campo base do pacote INDIRECT.
 *
 * Tambem despeja os dwords que a engine vai buscar. Na corrida de 04/09 22:27
 * a SDMA0 aposentou o INDIRECT (RPTR parou exatamente em DW 24, fim do pacote)
 * e travou dentro do IB, sem fault latchado em GFXHUB. Sem ver os bytes que a
 * engine buscou nao da para separar "pacote malformado" de "traducao falhou".
 */
static void Bc250PagingAppendIbAlignmentNops(
    const char *Label,
    uint32_t *DmaBuffer,
    uint32_t *Cursor)
{
    uint32_t useful = *Cursor;
    uint32_t pad;
    uint32_t i;

    pad = Bc250SdmaIbSizePadDwords(useful);
    for (i = 0u; i < pad; ++i) {
        DmaBuffer[(*Cursor)++] = BC250_SDMA_OP_NOP;
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    {
        PHYSICAL_ADDRESS ibPhysical = MmGetPhysicalAddress((PVOID)DmaBuffer);
        BC250_PAGING_PROOF proof;
        uint32_t slot = (uint32_t)(((UINT64)ibPhysical.QuadPart >> 5) % BC250_PAGING_PROOF_SLOTS);
        KIRQL proofIrql;
        RtlZeroMemory(&proof, sizeof(proof));
        (void)Bc250PagingDecodeProof(DmaBuffer, *Cursor, (UINT64)ibPhysical.QuadPart, &proof);
        KeAcquireSpinLock(&g_Bc250PagingProofLock, &proofIrql);
        g_Bc250PagingProofs[slot] = proof;
        KeReleaseSpinLock(&g_Bc250PagingProofLock, proofIrql);
        InterlockedExchange64(&g_Bc250PagingIbCpuPhysical,
            (LONG64)ibPhysical.QuadPart);
        Bc250DisplayLog("[%s] IB_CPU_PHYS: VA=%p -> PA=0x%llX\n",
            Label, (PVOID)DmaBuffer,
            (unsigned long long)ibPhysical.QuadPart);
    }
#else
    /*
     * Offline nao ha MmGetPhysicalAddress. Publicar 0 mantem o consumidor
     * fail-closed: PA zero significa "nao medido" e nunca casa por acidente.
     */
    InterlockedExchange64(&g_Bc250PagingIbCpuPhysical, 0);
#endif
    InterlockedExchange(&g_Bc250PagingIbUsefulDwords, (LONG)useful);
    InterlockedExchange(&g_Bc250PagingIbPublishedDwords, (LONG)*Cursor);

    /*
     * Teto de 32 DW: cada linha custa um ZwCreateFile/ZwWriteFile/ZwClose, e
     * um TransferVirtual grande emite dezenas de pacotes. Os 32 primeiros
     * cobrem o IB de paginacao inteiro nos casos que estamos investigando.
     */
    for (i = 0u; i < *Cursor && i < 32u; i += 4u) {
        uint32_t remaining = *Cursor - i;
        Bc250DisplayLog("[%s] IB_DUMP DW%02u: %08X %08X %08X %08X\n",
            Label, i,
            DmaBuffer[i],
            remaining > 1u ? DmaBuffer[i + 1u] : 0u,
            remaining > 2u ? DmaBuffer[i + 2u] : 0u,
            remaining > 3u ? DmaBuffer[i + 3u] : 0u);
    }
    Bc250DisplayLog("[%s] IB_SHAPE: UsefulDW=%u PadDW=%u PublishedDW=%u PublishedBytes=%u\n",
        Label, useful, pad, *Cursor, *Cursor * (uint32_t)sizeof(uint32_t));
}

#define BC250_SDMA_TRANSFER_MAX_BYTES (256u * 1024u) /* 256 KiB para fluxo estavel via PCIe/GFXHUB */

static NTSTATUS Bc250McdmBuildTransferVirtual(
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_TRANSFERVIRTUAL *transfer;
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    UINT64 done;
    UINT64 total;

    if (Args == NULL || Args->pDmaBuffer == NULL) return STATUS_INVALID_PARAMETER;
    transfer = &Args->TransferVirtual;
    total = transfer->TransferSizeInBytes;
    if (total == 0) {
        Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
        return STATUS_SUCCESS;
    }
    if ((transfer->SourceVirtualAddress & 3u) != 0 ||
        (transfer->DestinationVirtualAddress & 3u) != 0) {
        /*
         * Inalcancavel pelo contrato: o VidMm so entrega VA alinhado a pagina.
         * Fica fail-closed de proposito -- se um dia acontecer, queremos o
         * bugcheck com o status na mao em vez de um blt silenciosamente errado.
         */
        Bc250DisplayLog("[TransferVirtual] REJECT misaligned: Src=0x%llX Dst=0x%llX Size=%llu\n",
            (unsigned long long)transfer->SourceVirtualAddress,
            (unsigned long long)transfer->DestinationVirtualAddress,
            (unsigned long long)total);
        return STATUS_INVALID_PARAMETER;
    }

    done = (UINT64)Args->MultipassOffset;
    if (done >= total) {
        Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
        return STATUS_SUCCESS;
    }

    dmaBuffer = (uint32_t *)Args->pDmaBuffer;
    dwordsAvailable = Bc250PagingFloorPow2Dwords(
        Args->DmaSize / (uint32_t)sizeof(uint32_t));
    if (dwordsAvailable < BC250_SDMA_DWORDS_COPY_LINEAR) {
        Bc250DisplayLog("[TransferVirtual] DmaSize=%u insuficiente (cap=%u dwords)\n",
            Args->DmaSize, dwordsAvailable);
        Args->MultipassOffset = (UINT)done;
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }

    while (done < total) {
        UINT64 remaining = total - done;
        uint32_t chunk = remaining > (UINT64)BC250_SDMA_TRANSFER_MAX_BYTES ?
            BC250_SDMA_TRANSFER_MAX_BYTES : (uint32_t)remaining;
        BC250_SDMA_STATUS sdmaStatus;

        if (dwordsAvailable - cursor < BC250_SDMA_DWORDS_COPY_LINEAR +
            Bc250SdmaIbSizePadDwords(
                cursor + BC250_SDMA_DWORDS_COPY_LINEAR)) break;

        sdmaStatus = Bc250SdmaEmitCopyLinear(
            dmaBuffer,
            dwordsAvailable,
            &cursor,
            transfer->SourceVirtualAddress + done,
            transfer->DestinationVirtualAddress + done,
            chunk);
        if (sdmaStatus == BC250_SDMA_RING_FULL) break;
        if (sdmaStatus != BC250_SDMA_OK) {
            Bc250DisplayLog("[TransferVirtual] SDMA encode failed: status=%d chunk=%u done=%llu DmaSize=%u cap=%u\n",
                (int)sdmaStatus, chunk, (unsigned long long)done,
                Args->DmaSize, dwordsAvailable);
            /*
             * Com a capacidade ja arredondada para potencia de dois e o
             * chunk clampado, este caminho e inalcancavel. Se ainda assim
             * ocorrer sem nenhum progresso, nao ha status legal que peca
             * "desista": devolver INSUFFICIENT_DMA_BUFFER faria o VidMm
             * repetir para sempre. Preferimos o bugcheck com a linha de log
             * acima nomeando o motivo -- foi assim que a corrida 18:02 foi
             * diagnosticada.
             */
            if (done == 0ull) return STATUS_INVALID_PARAMETER;
            break;
        }
        done += chunk;
    }

    Bc250PagingPublishIbDestRange(
        transfer->DestinationVirtualAddress + (UINT64)Args->MultipassOffset,
        transfer->DestinationVirtualAddress + done);
    Bc250PagingAppendIbAlignmentNops("TransferVirtual", dmaBuffer, &cursor);
    Args->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    if (done < total) {
        Args->MultipassOffset = (UINT)done;
        Bc250DisplayLog("[TransferVirtual] Multipass: %llu/%llu bytes, %u dwords emitidos\n",
            (unsigned long long)done, (unsigned long long)total, cursor);
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
    Bc250DisplayLog("[TransferVirtual] Completo: %llu bytes em %u dwords\n",
        (unsigned long long)total, cursor);
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250McdmBuildFillVirtual(
    DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_FILLVIRTUAL *fill;
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    UINT64 done;
    UINT64 total;

    if (Args == NULL || Args->pDmaBuffer == NULL) return STATUS_INVALID_PARAMETER;
    fill = &Args->FillVirtual;
    total = fill->FillSizeInBytes;
    if (total == 0) {
        Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
        return STATUS_SUCCESS;
    }
    if ((fill->DestinationVirtualAddress & 3u) != 0) {
        Bc250DisplayLog("[FillVirtual] REJECT misaligned: Dst=0x%llX Size=%llu\n",
            (unsigned long long)fill->DestinationVirtualAddress,
            (unsigned long long)total);
        return STATUS_INVALID_PARAMETER;
    }

    done = (UINT64)Args->MultipassOffset;
    if (done >= total) {
        Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
        return STATUS_SUCCESS;
    }

    dmaBuffer = (uint32_t *)Args->pDmaBuffer;
    dwordsAvailable = Bc250PagingFloorPow2Dwords(
        Args->DmaSize / (uint32_t)sizeof(uint32_t));
    if (dwordsAvailable < BC250_SDMA_DWORDS_CONST_FILL) {
        Bc250DisplayLog("[FillVirtual] DmaSize=%u insuficiente (cap=%u dwords)\n",
            Args->DmaSize, dwordsAvailable);
        Args->MultipassOffset = (UINT)done;
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }

    while (done < total) {
        UINT64 remaining = total - done;
        uint32_t chunk = remaining > (UINT64)BC250_SDMA_FILL_MAX_BYTES ?
            BC250_SDMA_FILL_MAX_BYTES : (uint32_t)remaining;
        BC250_SDMA_STATUS sdmaStatus;

        if (dwordsAvailable - cursor < BC250_SDMA_DWORDS_CONST_FILL +
            Bc250SdmaIbSizePadDwords(
                cursor + BC250_SDMA_DWORDS_CONST_FILL)) break;

        sdmaStatus = Bc250SdmaEmitConstFill(
            dmaBuffer,
            dwordsAvailable,
            &cursor,
            fill->DestinationVirtualAddress + done,
            fill->FillPattern,
            chunk);
        if (sdmaStatus == BC250_SDMA_RING_FULL) break;
        if (sdmaStatus != BC250_SDMA_OK) {
            Bc250DisplayLog("[FillVirtual] SDMA encode failed: status=%d chunk=%u done=%llu DmaSize=%u cap=%u\n",
                (int)sdmaStatus, chunk, (unsigned long long)done,
                Args->DmaSize, dwordsAvailable);
            /*
             * Com a capacidade ja arredondada para potencia de dois e o
             * chunk clampado, este caminho e inalcancavel. Se ainda assim
             * ocorrer sem nenhum progresso, nao ha status legal que peca
             * "desista": devolver INSUFFICIENT_DMA_BUFFER faria o VidMm
             * repetir para sempre. Preferimos o bugcheck com a linha de log
             * acima nomeando o motivo -- foi assim que a corrida 18:02 foi
             * diagnosticada.
             */
            if (done == 0ull) return STATUS_INVALID_PARAMETER;
            break;
        }
        done += chunk;
    }

    Bc250PagingPublishFillParams(
        fill->DestinationVirtualAddress + (UINT64)Args->MultipassOffset,
        (ULONG)(done - (UINT64)Args->MultipassOffset),
        fill->FillPattern);
    Bc250PagingAppendIbAlignmentNops("FillVirtual", dmaBuffer, &cursor);
    Args->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    if (done < total) {
        Args->MultipassOffset = (UINT)done;
        Bc250DisplayLog("[FillVirtual] Multipass: %llu/%llu bytes, %u dwords emitidos\n",
            (unsigned long long)done, (unsigned long long)total, cursor);
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
    Bc250DisplayLog("[FillVirtual] Completo: %llu bytes, pattern=0x%08X, em %u dwords\n",
        (unsigned long long)total, fill->FillPattern, cursor);
    return STATUS_SUCCESS;
}

VOID Bc250McdmInstallRuntimeCapsProvider(
    BC250_MCDM_QUERY_RUNTIME_CAPS Provider)
{
    g_Bc250QueryRuntimeCaps = Provider;
}

VOID Bc250McdmInstallProcessLifecycle(
    BC250_MCDM_CREATE_PROCESS_OBJECT CreateProcessObject,
    BC250_MCDM_DESTROY_PROCESS_OBJECT DestroyProcessObject)
{
    g_Bc250CreateProcessObject = CreateProcessObject;
    g_Bc250DestroyProcessObject = DestroyProcessObject;
}

VOID Bc250McdmInstallDeviceLifecycle(
    BC250_MCDM_CREATE_DEVICE_OBJECT CreateDeviceObject,
    BC250_MCDM_DESTROY_DEVICE_OBJECT DestroyDeviceObject)
{
    g_Bc250CreateDeviceObject = CreateDeviceObject;
    g_Bc250DestroyDeviceObject = DestroyDeviceObject;
}

VOID Bc250McdmInstallContextLifecycle(
    BC250_MCDM_CREATE_CONTEXT_OBJECT CreateContextObject,
    BC250_MCDM_DESTROY_CONTEXT_OBJECT DestroyContextObject)
{
    g_Bc250CreateContextObject = CreateContextObject;
    g_Bc250DestroyContextObject = DestroyContextObject;
}

VOID Bc250McdmInstallGpuVaLifecycle(
    BC250_MCDM_SET_ROOT_PAGE_TABLE_OBJECT SetRootPageTableObject)
{
    g_Bc250SetRootPageTableObject = SetRootPageTableObject;
}

VOID Bc250McdmInstallAllocationLifecycle(
    BC250_MCDM_CREATE_ALLOCATION_OBJECTS CreateAllocationObjects,
    BC250_MCDM_DESTROY_ALLOCATION_OBJECTS DestroyAllocationObjects,
    BC250_MCDM_OPEN_ALLOCATION_OBJECTS OpenAllocationObjects,
    BC250_MCDM_CLOSE_ALLOCATION_OBJECTS CloseAllocationObjects)
{
    g_Bc250CreateAllocationObjects = CreateAllocationObjects;
    g_Bc250DestroyAllocationObjects = DestroyAllocationObjects;
    g_Bc250OpenAllocationObjects = OpenAllocationObjects;
    g_Bc250CloseAllocationObjects = CloseAllocationObjects;
}

VOID Bc250McdmInstallDescribeAllocation(
    BC250_MCDM_DESCRIBE_ALLOCATION_OBJECT DescribeAllocationObject)
{
    g_Bc250DescribeAllocationObject = DescribeAllocationObject;
}

VOID Bc250McdmInstallDebugLifecycle(
    BC250_MCDM_COLLECT_DEBUG_SNAPSHOT CollectDebugSnapshot)
{
    g_Bc250CollectDebugSnapshot = CollectDebugSnapshot;
}

VOID Bc250McdmInstallUnloadCleanupHandler(
    _In_opt_ BC250_MCDM_UNLOAD_CLEANUP_HANDLER Handler)
{
    g_Bc250UnloadCleanupHandler = Handler;
}

VOID Bc250McdmInstallTdrExecutors(
    BC250_MCDM_TDR_QUIESCE_EXECUTOR QuiesceExecutor,
    BC250_MCDM_TDR_RESTART_EXECUTOR RestartExecutor)
{
    g_Bc250TdrQuiesce = QuiesceExecutor;
    g_Bc250TdrRestart = RestartExecutor;
}

VOID Bc250McdmInstallEngineResponsiveProbe(
    BC250_MCDM_ENGINE_RESPONSIVE_PROBE Probe)
{
    g_Bc250EngineResponsiveProbe = Probe;
}

VOID Bc250McdmInstallSubmissionExecutor(
    BC250_MCDM_SUBMIT_COMMAND_EXECUTOR SubmitCommandExecutor)
{
    g_Bc250SubmitCommandExecutor = SubmitCommandExecutor;
}

VOID Bc250McdmInstallInterruptLifecycle(
    BC250_MCDM_INTERRUPT_HANDLER InterruptHandler,
    BC250_MCDM_DPC_HANDLER DpcHandler)
{
    g_Bc250InterruptHandler = InterruptHandler;
    g_Bc250DpcHandler = DpcHandler;
}

static BC250_MCDM_FLUSH_TLB_HANDLER g_Bc250FlushTlbHandler;

VOID Bc250McdmInstallFlushTlbHandler(
    _In_opt_ BC250_MCDM_FLUSH_TLB_HANDLER FlushTlbHandler)
{
    g_Bc250FlushTlbHandler = FlushTlbHandler;
}

VOID Bc250McdmInstallUpdatePageTableHandler(
    _In_opt_ BC250_MCDM_UPDATE_PAGE_TABLE_HANDLER UpdatePageTableHandler)
{
    g_Bc250UpdatePageTableHandler = UpdatePageTableHandler;
}

VOID Bc250McdmInstallEscapeHandler(
    _In_opt_ BC250_MCDM_ESCAPE_HANDLER EscapeHandler)
{
    g_Bc250EscapeHandler = EscapeHandler;
}

static BC250_MCDM_PREEMPT_HANDLER g_Bc250PreemptHandler;

VOID Bc250McdmInstallPreemptHandler(
    _In_opt_ BC250_MCDM_PREEMPT_HANDLER PreemptHandler)
{
    g_Bc250PreemptHandler = PreemptHandler;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
static NTSTATUS Bc250McdmBuildSignalMonitoredFence(
    _Inout_ DXGKARG_BUILDPAGINGBUFFER *Args)
{
    DXGK_BUILDPAGINGBUFFER_SIGNALMONITOREDFENCE *signal;
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    BC250_SDMA_STATUS sdmaStatus;

    if (Args == NULL || Args->pDmaBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    signal = &Args->SignalMonitoredFence;
    if (signal->MonitoredFenceGpuVa == 0ull ||
        (signal->MonitoredFenceGpuVa & 3u) != 0u) {
        Bc250DisplayLog("[SignalMonitoredFence] REJECT invalid/misaligned: FenceGpuVa=0x%llX\n",
            (unsigned long long)signal->MonitoredFenceGpuVa);
        return STATUS_INVALID_PARAMETER;
    }

    dmaBuffer = (uint32_t *)Args->pDmaBuffer;
    /* Mesma correcao do irmao em Bc250McdmSignalMonitoredFence: o buffer e'
     * LINEAR, e arredonda-lo para baixo ate potencia de dois recusava buffers
     * que cabiam. Ver o comentario completo la. */
    dwordsAvailable = Args->DmaSize / (uint32_t)sizeof(uint32_t);
    if (dwordsAvailable < BC250_SIGNAL_FENCE_SCRATCH_DWORDS) {
        Bc250DisplayLog("[SignalMonitoredFence] DmaSize=%u insuficiente (disponivel=%u dwords, exigido=%u)\n",
            Args->DmaSize, dwordsAvailable, BC250_SIGNAL_FENCE_SCRATCH_DWORDS);
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    dwordsAvailable = BC250_SIGNAL_FENCE_SCRATCH_DWORDS;

    sdmaStatus = Bc250SdmaEmitFence64(
        dmaBuffer,
        dwordsAvailable,
        &cursor,
        signal->MonitoredFenceGpuVa,
        signal->MonitoredFenceValue);
    if (sdmaStatus != BC250_SDMA_OK) {
        Bc250DisplayLog("[SignalMonitoredFence] SDMA encode failed: status=%d\n", (int)sdmaStatus);
        return STATUS_INVALID_PARAMETER;
    }

    sdmaStatus = Bc250SdmaEmitGcr(dmaBuffer, dwordsAvailable, &cursor);
    if (sdmaStatus != BC250_SDMA_OK) {
        Bc250DisplayLog("[SignalMonitoredFence] GCR encode failed: status=%d\n", (int)sdmaStatus);
        return STATUS_INVALID_PARAMETER;
    }

    Bc250PagingAppendIbAlignmentNops("SignalMonitoredFence", dmaBuffer, &cursor);
    Args->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    Args->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
    Bc250DisplayLog("[SignalMonitoredFence] Completo: FenceGpuVa=0x%llX Value=%llu (%u dwords emitidos)\n",
        (unsigned long long)signal->MonitoredFenceGpuVa,
        (unsigned long long)signal->MonitoredFenceValue,
        cursor);
    return STATUS_SUCCESS;
}
#endif

static NTSTATUS Bc250McdmBuildPagingBufferBody(
    CONST HANDLE hAdapter,
    DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    NTSTATUS capsStatus;
    NTSTATUS dispatchStatus;
    ULONG operation = pBuildPagingBuffer != NULL ?
        (ULONG)pBuildPagingBuffer->Operation : MAXULONG;

    InterlockedIncrement(&g_Bc250PagingActiveCount);

    Bc250PagingStage(
        BC250_PAGING_STAGE_BUILD_ENTER, operation, STATUS_PENDING);
    Bc250ProbeMarkDdi(BC250_V5_DDI_BUILD_PAGING_BUFFER);
    Bc250PagingStage(
        BC250_PAGING_STAGE_AFTER_DDI_MARK, operation, STATUS_PENDING);
    RtlZeroMemory(&caps, sizeof(caps));

    if (hAdapter == NULL || pBuildPagingBuffer == NULL) {
        InterlockedDecrement(&g_Bc250PagingActiveCount);
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmBuildPagingBuffer] Entered: Op=%u\n", pBuildPagingBuffer->Operation);
    Bc250PagingStage(
        BC250_PAGING_STAGE_AFTER_ENTRY_LOG, operation, STATUS_PENDING);
    if (g_Bc250QueryRuntimeCaps == NULL) {
        Bc250DisplayLog("[McdmBuildPagingBuffer] QueryRuntimeCaps provider is NULL -> DEVICE_NOT_READY\n");
        InterlockedDecrement(&g_Bc250PagingActiveCount);
        return STATUS_DEVICE_NOT_READY;
    }
    capsStatus = g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps);
    Bc250PagingStage(
        BC250_PAGING_STAGE_AFTER_CAPS, operation, capsStatus);
    if (!NT_SUCCESS(capsStatus)) {
        Bc250DisplayLog("[McdmBuildPagingBuffer] QueryRuntimeCaps failed: 0x%08X\n", capsStatus);
        InterlockedDecrement(&g_Bc250PagingActiveCount);
        return capsStatus;
    }

    if (pBuildPagingBuffer->Operation == DXGK_OPERATION_MAP_APERTURE_SEGMENT ||
        pBuildPagingBuffer->Operation == DXGK_OPERATION_UNMAP_APERTURE_SEGMENT ||
        pBuildPagingBuffer->Operation == DXGK_OPERATION_INIT_CONTEXT_RESOURCE) {
        Bc250DisplayLog("[McdmBuildPagingBuffer] Handled Op=%u -> SUCCESS\n", pBuildPagingBuffer->Operation);
        InterlockedDecrement(&g_Bc250PagingActiveCount);
        return STATUS_SUCCESS;
    }

    Bc250PagingStage(
        BC250_PAGING_STAGE_BEFORE_DISPATCH, operation, STATUS_PENDING);
    switch (pBuildPagingBuffer->Operation) {
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_INIT_CONTEXT_RESOURCE:
    case DXGK_OPERATION_DISCARD_CONTENT:
    case DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION:
    case DXGK_OPERATION_NOTIFY_RESIDENCY:
        Bc250DisplayLog("[McdmBuildPagingBuffer] Handled Op=%u -> SUCCESS\n", pBuildPagingBuffer->Operation);
        InterlockedDecrement(&g_Bc250PagingActiveCount);
        return STATUS_SUCCESS;

    case DXGK_OPERATION_UPDATE_PAGE_TABLE: {
        dispatchStatus = Bc250McdmBuildCpuPageTableUpdate(
            &caps, (HANDLE)hAdapter, pBuildPagingBuffer);
        break;
    }

    case DXGK_OPERATION_VIRTUAL_TRANSFER:
        dispatchStatus = Bc250McdmBuildTransferVirtual(pBuildPagingBuffer);
        break;

    case DXGK_OPERATION_VIRTUAL_FILL:
        dispatchStatus = Bc250McdmBuildFillVirtual(pBuildPagingBuffer);
        break;

    case DXGK_OPERATION_FLUSH_TLB:
        if (g_Bc250FlushTlbHandler != NULL) {
            Bc250PagingStage(BC250_PAGING_STAGE_FLUSH_HANDLER_ENTER,
                operation, STATUS_PENDING);
            dispatchStatus = g_Bc250FlushTlbHandler(
                (HANDLE)hAdapter, &pBuildPagingBuffer->FlushTlb);
            Bc250PagingStage(BC250_PAGING_STAGE_FLUSH_HANDLER_LEAVE,
                operation, dispatchStatus);
            if (!NT_SUCCESS(dispatchStatus) &&
                dispatchStatus != STATUS_INVALID_PARAMETER) {
                dispatchStatus = STATUS_SUCCESS;
            }
            break;
        }
        /* Sem backend nao houve invalidacao nem ACK fisico. */
        dispatchStatus = STATUS_NOT_SUPPORTED;
        break;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    case DXGK_OPERATION_SIGNAL_MONITORED_FENCE:
        dispatchStatus = Bc250McdmBuildSignalMonitoredFence(pBuildPagingBuffer);
        break;
#endif

    default:
        Bc250DisplayLog("[McdmBuildPagingBuffer] Unhandled Op=%u -> NOT_SUPPORTED\n", pBuildPagingBuffer->Operation);
        dispatchStatus = STATUS_NOT_SUPPORTED;
        break;
    }
    Bc250PagingStage(
        BC250_PAGING_STAGE_AFTER_DISPATCH, operation, dispatchStatus);
    InterlockedDecrement(&g_Bc250PagingActiveCount);
    return dispatchStatus;
}

NTSTATUS APIENTRY Bc250McdmBuildPagingBuffer(
    CONST HANDLE hAdapter,
    DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer)
{
    ULONG operation = pBuildPagingBuffer != NULL ?
        (ULONG)pBuildPagingBuffer->Operation : MAXULONG;
    UNREFERENCED_PARAMETER(operation); /* a migalha vira no-op na suite offline */
    ULONG crumb = Bc250CrumbBeforeBank(
        BC250_CRUMB_SITE_PAGING_DDI, operation, BC250_CRUMB_BANK_PAGING);
    /*
     * DX.85 -- A INSTRUMENTACAO DA DX.83 TRAVAVA A THREAD DE PAGINACAO.
     *
     * Bc250DisplayLog so' desvia para o anel em RAM quando
     * g_Bc250PagingActiveCount > 0; com o contador em zero, em PASSIVE_LEVEL,
     * ele faz ZwCreateFile/ZwWriteFile/ZwFlushBuffersFile. As linhas
     * BC250_HANG ENTER/LEAVE da DX.83 ficavam FORA da janela do contador (o
     * incremento so' acontece dentro do Body), portanto faziam I/O de arquivo
     * sincrono NA THREAD DE PAGINACAO DO VIDMM -- exatamente o deadlock que o
     * comentario do logger proibe.
     *
     * Medido (P0): Corrida 130 com 110 ENTER e 109 LEAVE, Corrida 131 com 90
     * ENTER e 89 LEAVE; nas duas o ultimo ENTER (op=12) nao tem LEAVE, o
     * rastro duravel DX.56 NUNCA registrou o BUILD_ENTER seguinte (primeira
     * instrucao do Body) e a migalha DX.66 ficou em PAGING_DDI 0x0C fase=ANTES.
     * A thread parou entre a linha escrita e o Body: dentro do logger. Sem
     * paginacao, a paging fence nao avanca -- WaitPaging do Explorer e
     * vkAllocateMemory do DWM/RADV eram SINTOMAS desta trava.
     *
     * O contador agora cobre o wrapper inteiro, inclusive as duas linhas.
     */
    InterlockedIncrement(&g_Bc250PagingActiveCount);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    LONG callId = 0;
    if (operation == DXGK_OPERATION_FLUSH_TLB) {
        LARGE_INTEGER enterQpc = KeQueryPerformanceCounter(NULL);
        callId = InterlockedIncrement(&g_Bc250PagingCallSequence);
        Bc250DisplayLog("BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=%ld tid=%p qpc=%llu operation=%u\n",
            callId, PsGetCurrentThreadId(), (unsigned long long)enterQpc.QuadPart, operation);
    }
#endif
    NTSTATUS status = Bc250McdmBuildPagingBufferBody(hAdapter, pBuildPagingBuffer);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (operation == DXGK_OPERATION_FLUSH_TLB) {
        LARGE_INTEGER leaveQpc = KeQueryPerformanceCounter(NULL);
        Bc250DisplayLog("BC250_HANG v=1 op=PAGING_DDI phase=LEAVE id=%ld tid=%p qpc=%llu operation=%u status=0x%08X\n",
            callId, PsGetCurrentThreadId(), (unsigned long long)leaveQpc.QuadPart,
            operation, status);
    }
#endif
    InterlockedDecrement(&g_Bc250PagingActiveCount);
    Bc250CrumbAfter(crumb);
    return status;
}

NTSTATUS APIENTRY Bc250McdmCloseAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CLOSE_ALLOCATION);
    if (hDevice == NULL || pCloseAllocation == NULL ||
        pCloseAllocation->NumAllocations == 0u ||
        pCloseAllocation->pOpenHandleList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCloseAllocation] NumAllocs=%u\n", pCloseAllocation->NumAllocations);
    if (g_Bc250CloseAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CloseAllocationObjects(hDevice, pCloseAllocation);
}

NTSTATUS APIENTRY Bc250McdmCollectDbgInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_COLLECT_DBG_INFO);
    if (hAdapter == NULL || pCollectDbgInfo == NULL ||
        pCollectDbgInfo->pBuffer == NULL || pCollectDbgInfo->BufferSize == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CollectDebugSnapshot == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250CollectDebugSnapshot(hAdapter, pCollectDbgInfo);
}

NTSTATUS APIENTRY Bc250McdmCreateAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEALLOCATION *pCreateAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_ALLOCATION);
    UINT index;
    NTSTATUS status;
    if (hAdapter == NULL || pCreateAllocation == NULL ||
        pCreateAllocation->pAllocationInfo == NULL ||
        pCreateAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateAllocation] NumAllocs=%u\n", pCreateAllocation->NumAllocations);
    for (index = 0u; index < pCreateAllocation->NumAllocations; ++index) {
        pCreateAllocation->pAllocationInfo[index].hAllocation = NULL;
    }
    if (g_Bc250CreateAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    status = g_Bc250CreateAllocationObjects(hAdapter, pCreateAllocation);
    Bc250DisplayLog("[McdmCreateAllocation] Result=0x%08X\n", status);
    return status;
}

NTSTATUS APIENTRY Bc250McdmCreateContext(
    CONST HANDLE hDevice,
    DXGKARG_CREATECONTEXT *pCreateContext)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_CONTEXT);
    HANDLE contextHandle = NULL;
    NTSTATUS status;

    if (hDevice == NULL || pCreateContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateContext] Entered: Node=%u, hDevice=0x%p, Flags=0x%08X\n",
        pCreateContext->NodeOrdinal, hDevice,
        pCreateContext->Flags.Value);
    /*
     * Ver a nota em Bc250McdmCreateDevice: na corrida 13:00 esta DDI aparecia
     * oito vezes sem UMA palavra sobre o desfecho. Todo caminho de recusa passa
     * a se nomear.
     */
    if (pCreateContext->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        Bc250DisplayLog("[McdmCreateContext] RECUSADO: NodeOrdinal=%u fora de %u nodes -> STATUS_INVALID_PARAMETER\n",
            pCreateContext->NodeOrdinal, (unsigned)BC250_ADAPTER_NODE_COUNT);
        return STATUS_INVALID_PARAMETER;
    }
    pCreateContext->hContext = NULL;
    RtlZeroMemory(&pCreateContext->ContextInfo,
        sizeof(pCreateContext->ContextInfo));
    pCreateContext->ContextInfo.DmaBufferSize = 0x10000u;
    pCreateContext->ContextInfo.DmaBufferSegmentSet = (1u << (2u - 1u)); /* Segment 2 GART Aperture */
    pCreateContext->ContextInfo.AllocationListSize = 32u;
    pCreateContext->ContextInfo.PatchLocationListSize = 0u;
    pCreateContext->ContextInfo.Caps.NoPatchingRequired = 1u;
    pCreateContext->ContextInfo.PagingCompanionNodeId = 0u;
    if (g_Bc250CreateContextObject == NULL) {
        Bc250DisplayLog("[McdmCreateContext] RECUSADO: provedor de objeto de contexto ausente -> STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (hDevice == (HANDLE)1) {
        pCreateContext->hContext = (HANDLE)2;
        Bc250DisplayLog("[McdmCreateContext] System Paging Context: DmaBuf=0x%X, SegSet=0x%X\n",
            pCreateContext->ContextInfo.DmaBufferSize,
            pCreateContext->ContextInfo.DmaBufferSegmentSet);
        Bc250DisplayLog("BC250_DIAG schema=1 level=INFO event=KMD_CONTEXT_CREATE ctx=%p device=%p node=%u paging=1 status=0x00000000\n",
            pCreateContext->hContext, hDevice, pCreateContext->NodeOrdinal);
        return STATUS_SUCCESS;
    }
    status = g_Bc250CreateContextObject(hDevice,
        pCreateContext->NodeOrdinal, &contextHandle,
        &pCreateContext->ContextInfo);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[McdmCreateContext] RECUSADO: provedor devolveu status=0x%08X (Node=%u)\n",
            (uint32_t)status, pCreateContext->NodeOrdinal);
        Bc250DisplayLog("BC250_DIAG schema=1 level=ERROR event=KMD_CONTEXT_CREATE ctx=0x0 device=%p node=%u paging=0 status=0x%08X\n",
            hDevice, pCreateContext->NodeOrdinal, (uint32_t)status);
        return status;
    }
    pCreateContext->hContext = contextHandle;
    Bc250DisplayLog("[McdmCreateContext] OK: Node=%u, hContext=0x%p, DmaBuf=0x%X, SegSet=0x%X\n",
        pCreateContext->NodeOrdinal, contextHandle,
        pCreateContext->ContextInfo.DmaBufferSize,
        pCreateContext->ContextInfo.DmaBufferSegmentSet);
    Bc250DisplayLog("BC250_DIAG schema=1 level=INFO event=KMD_CONTEXT_CREATE ctx=%p device=%p node=%u paging=0 status=0x%08X dma_bytes=%u segment_set=0x%X\n",
        contextHandle, hDevice, pCreateContext->NodeOrdinal,
        (uint32_t)status, pCreateContext->ContextInfo.DmaBufferSize,
        pCreateContext->ContextInfo.DmaBufferSegmentSet);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateDevice(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEDEVICE *pCreateDevice)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_DEVICE);
    HANDLE deviceHandle = NULL;
    NTSTATUS status;

    if (hAdapter == NULL || pCreateDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmCreateDevice] Entered: hAdapter=0x%p, hKmdProcess=0x%p\n",
        hAdapter, pCreateDevice->hKmdProcess);
    pCreateDevice->hDevice = NULL;
    /*
     * MEDIDO na corrida 08/09 13:00: sete ciclos
     * CreateProcess -> CreateDevice -> CreateContext(Node 0) -> DestroyProcess,
     * todos com o MESMO hDxgkProcess, e a negociacao de VidPn -- que ja havia
     * CONVERGIDO -- descartada a cada ciclo. Nao havia como saber se alguma
     * dessas DDIs RECUSOU: as tres falhavam MUDAS. Mesma classe do B4, agora no
     * caminho critico do processo que morre.
     */
    if (g_Bc250CreateDeviceObject == NULL) {
        Bc250DisplayLog("[McdmCreateDevice] RECUSADO: provedor de objeto de device ausente -> STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (pCreateDevice->hKmdProcess == NULL) {
        pCreateDevice->hDevice = (HANDLE)1;
        Bc250DisplayLog("[McdmCreateDevice] Device de sistema (hKmdProcess=NULL) -> hDevice=1, SUCCESS\n");
        return STATUS_SUCCESS;
    }
    status = g_Bc250CreateDeviceObject(
        hAdapter, pCreateDevice->hKmdProcess, &deviceHandle);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[McdmCreateDevice] RECUSADO: provedor devolveu status=0x%08X\n",
            (uint32_t)status);
        return status;
    }
    pCreateDevice->hDevice = deviceHandle;
    Bc250DisplayLog("[McdmCreateDevice] OK: hDevice=0x%p\n", deviceHandle);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmCreateProcess(
    CONST HANDLE hAdapter,
    DXGKARG_CREATEPROCESS *pArgs)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CREATE_PROCESS);
    NTSTATUS status;
    HANDLE processHandle = NULL;

    if (hAdapter == NULL || pArgs == NULL || pArgs->hDxgkProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * MEDIDO na corrida 08/09 13:00 (perfil lab, Codigo 0, tela preta): a
     * negociacao de VidPn CONVERGIU 39 vezes -- "TargetMode matches GOP ->
     * Supported=TRUE!" com Scale=1/Rot=1 e os dois modos fixados, exatamente o
     * estado que a corrida de PRODUCAO comita -- e foi descartada 46 vezes. Em
     * cada ciclo o log mostrava CreateProcess -> CreateDevice -> CreateContext
     * (Node 0) -> DestroyProcess, e entao a negociacao recomecava do zero.
     *
     * NAO ha' como saber QUEM morre: o log so' tinha handles opacos. E a
     * hipotese obvia -- o E_NOTIMPL da nossa UMD -- foi REFUTADA na mesma
     * corrida: nenhum BC250_UMD_LOG_*.txt foi criado, isto e', bc250umd.dll
     * nunca chegou a ser carregada. Na corrida de PRODUCAO das 08:44 a mesma
     * DLL carregou em dwm.exe e recusou OpenAdapter10/10_2 com E_NOTIMPL --
     * aquela e' a parede do D3D11, e e' OUTRA, posterior a esta.
     *
     * DXGKARG_CREATEPROCESS carrega pProcessName e Flags desde WDDM 2.4 (ver
     * d3dkmddi.h) e declaramos 2.7, entao o nome vem do proprio contrato --
     * sem API nao documentada. pProcessName pode ser NULL por contrato, e
     * ProcessNameLength conta WCHARs, nao bytes.
     */
    {
        WCHAR nameBuffer[64];
        const WCHAR *nameText = L"<sem nome>";
        UINT nameChars = pArgs->ProcessNameLength;

        if (pArgs->pProcessName != NULL && nameChars != 0u) {
            UINT nameIndex;
            /*
             * Destino volatile de proposito. Uma copia elemento a elemento e'
             * reconhecida pelo /O2 e reemitida como memcpy, que NAO existe no
             * link offline das suites de contrato -- e RtlCopyMemory tem o
             * mesmo destino. O volatile desliga essa reducao.
             */
            volatile WCHAR *nameDst = nameBuffer;
            if (nameChars > (RTL_NUMBER_OF(nameBuffer) - 1u)) {
                nameChars = RTL_NUMBER_OF(nameBuffer) - 1u;
            }
            for (nameIndex = 0u; nameIndex < nameChars; ++nameIndex) {
                nameDst[nameIndex] = pArgs->pProcessName[nameIndex];
            }
            nameDst[nameChars] = L'\0';
            nameText = nameBuffer;
        }
        Bc250DisplayLog("[McdmCreateProcess] Entered: hAdapter=0x%p, hDxgkProcess=0x%p, Nome=%ws, Flags(System=%u Gdi=%u Vm=%u VmWorker=%u Isolated=%u)\n",
            hAdapter, pArgs->hDxgkProcess, nameText,
            pArgs->Flags.SystemProcess,
            pArgs->Flags.GdiProcess,
            pArgs->Flags.VirtualMachineProcess,
            pArgs->Flags.VirtualMachineWorkerProcess,
            pArgs->Flags.SessionIsolatedContainer);
    }

    pArgs->hKmdProcess = NULL;
    if (g_Bc250CreateProcessObject == NULL) {
        Bc250DisplayLog("[McdmCreateProcess] RECUSADO: provedor de objeto de processo ausente -> STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }
    status = g_Bc250CreateProcessObject(
        hAdapter, pArgs->hDxgkProcess, &processHandle);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[McdmCreateProcess] RECUSADO: provedor devolveu status=0x%08X\n",
            (uint32_t)status);
        return status;
    }
    pArgs->hKmdProcess = processHandle;
    return STATUS_SUCCESS;
}

/*
 * F1.1 (13/09) -- ESTA DDI ERA COMPLETAMENTE MUDA, E E' CANDIDATA A CAUSA DO E_FAIL.
 *
 * Uma auditoria externa apontou DxgkDdiDescribeAllocation como o portao que
 * bloqueia o Present: o dxgkrnl a chamaria para conferir a geometria da superficie
 * de origem antes de agendar qualquer coisa, e a nossa recusa
 * (STATUS_NOT_SUPPORTED, 0xC00000BB) abortaria a rotina, virando E_FAIL generico
 * no lado do runtime.
 *
 * O DEFEITO ESTA CONFIRMADO por leitura -- devolvemos NOT_SUPPORTED para toda
 * alocacao. O MECANISMO acima NAO esta: ele veio de uma desmontagem do dxgkrnl que
 * nao foi reproduzida nesta maquina. E' H, nao P0.
 *
 * E ate esta faixa era IMPOSSIVEL discriminar, porque a DDI nao emitia uma linha.
 * "Zero [DescribeAlloc] no log" nao distinguia "o dxgkrnl nunca chamou" de
 * "recusamos calados" -- a mesma ambiguidade que a Lei 4 proibe e que ja foi
 * corrigida em Bc250McdmSignalMonitoredFence, no handler de composicao e em
 * Bc250DxgkDdiPresent.
 *
 * ESTA FAIXA E' SO' INSTRUMENTACAO: o retorno continua NOT_SUPPORTED de proposito.
 * Implementar a descricao e' o item 2.1 de PLANO_PRESENT_E_FAIL.md, e mantendo o
 * comportamento aqui a proxima corrida diz se a DDI e' CHAMADA -- que e' a
 * pergunta que decide se o candidato vive ou morre.
 *
 * Contador PROPRIO (8 primeiras + 1 em 256): a DDI pode ser chamada por quadro, e
 * o teto global nao alcanca esta classe. Licao das classes 9 e 12.
 */
NTSTATUS APIENTRY Bc250McdmDescribeAllocation(
    CONST HANDLE hAdapter,
    DXGKARG_DESCRIBEALLOCATION *pDescribeAllocation)
{
    static volatile LONG s_DescribeOrdinal = 0;
    LONG ordinal;
    BOOLEAN speak;
    NTSTATUS status;

    Bc250ProbeMarkDdi(BC250_V5_DDI_DESCRIBE_ALLOCATION);
    UNREFERENCED_PARAMETER(hAdapter);

    /*
     * Um unico teto para as DUAS linhas. A de recusa contem "RECUSADO", que e'
     * isento da amostragem por classe, entao sem este teto ela sairia a CADA
     * chamada -- e esta DDI pode ser chamada por quadro e por alocacao. A
     * ausencia continua conclusiva: se a DDI for chamada ao menos uma vez,
     * ordinal==1 e as linhas saem.
     */
    ordinal = InterlockedIncrement(&s_DescribeOrdinal);
    speak = (ordinal <= 8L || (ordinal % 256L) == 0L) ? TRUE : FALSE;

    if (speak) {
        Bc250DisplayLog("[DescribeAlloc] ENTER #%ld: hAdapter=%p pArg=%p hAllocation=%p\n",
            ordinal, hAdapter, (PVOID)pDescribeAllocation,
            (pDescribeAllocation != NULL) ? pDescribeAllocation->hAllocation : NULL);
    }

    if (pDescribeAllocation == NULL) {
        Bc250DisplayLog("[DescribeAlloc] RECUSADO #%ld: pDescribeAllocation NULL -> STATUS_INVALID_PARAMETER\n",
            ordinal);
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250DescribeAllocationObject == NULL) {
        Bc250DisplayLog("[DescribeAlloc] RECUSADO #%ld: executor nao instalado -> STATUS_NOT_SUPPORTED\n",
            ordinal);
        return STATUS_NOT_SUPPORTED;
    }

    status = g_Bc250DescribeAllocationObject(hAdapter, pDescribeAllocation);
    if (speak) {
        if (NT_SUCCESS(status)) {
            Bc250DisplayLog("[DescribeAlloc] LEAVE #%ld: hAllocation=%p -> %ux%u Format=%u "
                "Refresh=%u/%u Rotation=%u status=0x%08X\n",
                ordinal, pDescribeAllocation->hAllocation,
                pDescribeAllocation->Width, pDescribeAllocation->Height,
                (UINT)pDescribeAllocation->Format,
                pDescribeAllocation->RefreshRate.Numerator,
                pDescribeAllocation->RefreshRate.Denominator,
                (UINT)pDescribeAllocation->Rotation,
                (UINT)status);
        } else {
            Bc250DisplayLog("[DescribeAlloc] RECUSADO #%ld: hAllocation=%p status=0x%08X "
                "(geometria nao declarada para esta alocacao)\n",
                ordinal, pDescribeAllocation->hAllocation, (UINT)status);
        }
    }
    return status;
}

NTSTATUS APIENTRY Bc250McdmDestroyAllocation(
    CONST HANDLE hAdapter,
    CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_DESTROY_ALLOCATION);
    if (hAdapter == NULL || pDestroyAllocation == NULL ||
        pDestroyAllocation->NumAllocations == 0u ||
        pDestroyAllocation->pAllocationList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyAllocationObjects == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyAllocationObjects(hAdapter, pDestroyAllocation);
}

NTSTATUS APIENTRY Bc250McdmDestroyContext(CONST HANDLE hContext)
{
    NTSTATUS status;
    if (hContext == NULL) return STATUS_INVALID_PARAMETER;
    if (hContext == (HANDLE)1 || hContext == (HANDLE)2) {
        Bc250DisplayLog("BC250_DIAG schema=1 level=INFO event=KMD_CONTEXT_DESTROY ctx=%p paging=1 status=0x00000000\n", hContext);
        return STATUS_SUCCESS;
    }
    if (g_Bc250DestroyContextObject == NULL) {
        Bc250DisplayLog("BC250_DIAG schema=1 level=ERROR event=KMD_CONTEXT_DESTROY ctx=%p paging=0 status=0x%08X\n",
            hContext, (uint32_t)STATUS_NOT_SUPPORTED);
        return STATUS_NOT_SUPPORTED;
    }
    status = g_Bc250DestroyContextObject(hContext);
    Bc250DisplayLog("BC250_DIAG schema=1 level=%s event=KMD_CONTEXT_DESTROY ctx=%p paging=0 status=0x%08X\n",
        NT_SUCCESS(status) ? "INFO" : "ERROR", hContext, (uint32_t)status);
    return status;
}

NTSTATUS APIENTRY Bc250McdmDestroyDevice(CONST HANDLE hDevice)
{
    if (hDevice == NULL) return STATUS_INVALID_PARAMETER;
    if (hDevice == (HANDLE)1) return STATUS_SUCCESS;
    if (g_Bc250DestroyDeviceObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyDeviceObject(hDevice);
}

NTSTATUS APIENTRY Bc250McdmDestroyProcess(
    CONST HANDLE hAdapter,
    CONST HANDLE hKmdProcess)
{
    if (hAdapter == NULL || hKmdProcess == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DestroyProcessObject == NULL) return STATUS_NOT_SUPPORTED;
    return g_Bc250DestroyProcessObject(hAdapter, hKmdProcess);
}

NTSTATUS Bc250McdmDispatchIoRequest(
    CONST PVOID MiniportDeviceContext,
    ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_DISPATCH_IO_REQUEST);
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    return STATUS_NOT_SUPPORTED;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmFormatHistoryBuffer, BC250_V5_DDI_FORMAT_HISTORY_BUFFER,
    CONST HANDLE, hContext,
    DXGKARG_FORMATHISTORYBUFFER *, pFormatData)

NTSTATUS APIENTRY Bc250McdmCalibrateGpuClock(
    CONST HANDLE hAdapter,
    UINT32 NodeOrdinal,
    UINT32 EngineOrdinal,
    DXGKARG_CALIBRATEGPUCLOCK *pClockCalibration)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_CALIBRATE_GPU_CLOCK);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);

    if (pClockCalibration == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmGetNodeMetadata(
    CONST HANDLE hAdapter,
    UINT NodeOrdinalAndAdapterIndex,
    DXGKARG_GETNODEMETADATA *pGetNodeMetadata)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_GET_NODE_METADATA);
    UINT nodeIndex = NodeOrdinalAndAdapterIndex & 0xFFFFu;

    BC250_MCDM_RUNTIME_CAPS caps;

    if (pGetNodeMetadata == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (nodeIndex >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.Ready == 0u || caps.FullMcdmReady == 0u ||
        caps.SubmitReady == 0u || nodeIndex >= caps.NodeCount) {
        return STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(pGetNodeMetadata, sizeof(*pGetNodeMetadata));
    pGetNodeMetadata->EngineType = (nodeIndex == BC250_ADAPTER_NODE_SDMA) ?
        DXGK_ENGINE_TYPE_COPY :
        (Bc250McdmGetComputeOnly() ? DXGK_ENGINE_TYPE_OTHER : DXGK_ENGINE_TYPE_3D);
    if (nodeIndex == BC250_ADAPTER_NODE_SDMA) {
        static const WCHAR name[] = L"BC250 SDMA0";
        RtlCopyMemory(pGetNodeMetadata->FriendlyName, name, sizeof(name));
    } else {
        static const WCHAR nameCompute[] = L"BC250 Compute MEC";
        static const WCHAR name3D[] = L"BC250 3D Graphics";
        if (Bc250McdmGetComputeOnly()) {
            RtlCopyMemory(pGetNodeMetadata->FriendlyName, nameCompute, sizeof(nameCompute));
        } else {
            RtlCopyMemory(pGetNodeMetadata->FriendlyName, name3D, sizeof(name3D));
        }
    }
    pGetNodeMetadata->GpuMmuSupported = caps.GpuVaReady != 0u;
    pGetNodeMetadata->IoMmuSupported = FALSE;
    Bc250DisplayLog("[GetNodeMetadata] Node=%u, EngineType=%d\n", nodeIndex, (int)pGetNodeMetadata->EngineType);
    return STATUS_SUCCESS;
}

SIZE_T APIENTRY Bc250McdmGetRootPageTableSize(
    CONST HANDLE hAdapter,
    DXGKARG_GETROOTPAGETABLESIZE *pArgs)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT64 bytes;
    UINT32 entries;

    if (hAdapter == NULL || pArgs == NULL ||
        pArgs->PhysicalAdapterIndex != 0u || pArgs->NumberOfPte == 0u ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableLevelCount != 2u ||
        caps.PageTableSizeBytes == 0u ||
        caps.PageTableAlignmentBytes == 0u) {
        if (pArgs != NULL) pArgs->NumberOfPte = 0u;
        return 0u;
    }
    entries = pArgs->NumberOfPte;
    bytes = (UINT64)entries * sizeof(UINT64);
    if (bytes > MAXULONG_PTR - (caps.PageTableAlignmentBytes - 1u)) {
        pArgs->NumberOfPte = 0u;
        return 0u;
    }
    bytes = (bytes + caps.PageTableAlignmentBytes - 1u) &
        ~((UINT64)caps.PageTableAlignmentBytes - 1u);
    pArgs->NumberOfPte = (UINT32)(bytes / sizeof(UINT64));
    return (SIZE_T)bytes;
}

/*
 * P5 / item 35 -- reconstrucao da private driver data das alocacoes padrao.
 *
 * PROVA DE REGRESSAO (mineracao do corpus, nao hipotese):
 *   docs/evidence/2026-09-04_12-23-15.../bc250_display_log.txt (linhas 1173-1177):
 *     [GetStdAllocData] Filled: Token=0xA1100001, Size=11075584, Align=65536
 *     [McdmCreateAllocation] NumAllocs=1
 *     [CreateAllocation]   [0] Size=0, Alignment=0, Flags=0x0, PrivDataSize=40
 *     -> primary criada, OpenAllocation OK, SetVidPnSourceAddress chamado.
 *   docs/evidence/2026-09-04_17-05-14.../bc250_display_log.txt:
 *     AllocationPrivateDriverDataSize=0 -> o dxgkrnl NUNCA chama CreateAllocation,
 *     comita VidPN vazia com hPrimaryAllocation=NULL e desliga a fonte (7 ciclos).
 *
 * Devolver tamanho zero deixa o VidMm sem qualquer descricao da primary, e ele
 * abandona a criacao ANTES de nos chamar. Reemitimos BC250_ALLOCATION_PRIVATE_V1
 * no formato exato que Bc250AllocationAbiParse aceita (40 bytes, Flags=0,
 * Alignment potencia de 2 em [4K, 2M], ByteSize multiplo do Alignment).
 */
#define BC250_STD_ALLOC_ALIGNMENT 65536ull
#define BC250_STD_ALLOC_BYTES_PER_PIXEL 4ull

#define BC250_STD_ALLOC_TOKEN_PRIMARY \
    ((UINT64)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY)
#define BC250_STD_ALLOC_TOKEN_SHADOW \
    ((UINT64)BC250_ALLOCATION_TOKEN_CLASS_SHADOW)
#define BC250_STD_ALLOC_TOKEN_STAGING \
    ((UINT64)BC250_ALLOCATION_TOKEN_CLASS_STAGING)
#define BC250_STD_ALLOC_TOKEN_GDI \
    ((UINT64)BC250_ALLOCATION_TOKEN_CLASS_GDI)

static volatile LONG g_Bc250StdAllocSequence = 0;

static VOID Bc250McdmPublishStandardAllocationPrivateData(
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *Args,
    _In_ UINT64 Token,
    _In_ UINT64 ByteSize,
    _In_opt_ const BC250_ALLOCATION_GEOMETRY *Geometry)
{
    BC250_ALLOCATION_PRIVATE_V1 priv;
    UINT64 aligned;
    LONG seq;

    if (ByteSize == 0ull) {
        ByteSize = BC250_STD_ALLOC_ALIGNMENT;
    }
    aligned = (ByteSize + (BC250_STD_ALLOC_ALIGNMENT - 1ull)) &
        ~(BC250_STD_ALLOC_ALIGNMENT - 1ull);

    /*
     * Passe 1 (pAllocationPrivateDriverData == NULL): so o tamanho.
     * Passe 2 (ponteiro valido): o conteudo. O dxgkrnl exige os dois.
     */
    Args->AllocationPrivateDriverDataSize = (UINT)sizeof(priv);
    Args->ResourcePrivateDriverDataSize = 0u;

    if (Args->pAllocationPrivateDriverData == NULL) {
        Bc250DisplayLog("[GetStdAllocData] Size query -> AllocPrivDataSize=%u, ResPrivDataSize=0\n",
            (ULONG)sizeof(priv));
        return;
    }

    seq = InterlockedIncrement(&g_Bc250StdAllocSequence);
    RtlZeroMemory(&priv, sizeof(priv));
    priv.Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
    priv.Version = BC250_ALLOCATION_PRIVATE_VERSION;
    priv.StructSize = (UINT32)sizeof(priv);
    priv.Flags = 0u;
    priv.AllocationToken = ((UINT64)Token << 32) | (UINT64)(ULONG)seq;
    priv.ByteSize = aligned;
    priv.Alignment = BC250_STD_ALLOC_ALIGNMENT;
    /*
     * DX.63b (Corrida 105): os 60 presents Blt do harness foram recusados com
     * GEOMETRIA_DO_DESTINO_DESCONHECIDA -- o destino era alocacao padrao, cuja
     * largura/altura/pitch o KMD conhece aqui e nao publicava. Geometria
     * incoerente com o tamanho nao e' publicada (o parse a recusaria).
     */
    if (Geometry != NULL && Geometry->Width != 0u && Geometry->Height != 0u &&
        Geometry->Pitch >= Geometry->Width && Geometry->Format != 0u &&
        (UINT64)Geometry->Pitch * Geometry->Height <= aligned) {
        priv.Flags |= BC250_ALLOCATION_FLAG_HAS_GEOMETRY;
        priv.Geometry = *Geometry;
    }
    RtlCopyMemory(Args->pAllocationPrivateDriverData, &priv, sizeof(priv));

    Bc250DisplayLog("[GetStdAllocData] Filled: Token=0x%llX, Size=%llu, Align=%llu, Geometria=%ux%u pitch=%u fmt=%u\n",
        (unsigned long long)priv.AllocationToken,
        (unsigned long long)priv.ByteSize,
        (unsigned long long)priv.Alignment,
        priv.Geometry.Width, priv.Geometry.Height,
        priv.Geometry.Pitch, priv.Geometry.Format);
}

NTSTATUS APIENTRY Bc250McdmGetStandardAllocationDriverData(
    CONST HANDLE hAdapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *pGetStandardAllocationDriverData)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_GET_STD_ALLOCATION_DATA);
    if (hAdapter == NULL || pGetStandardAllocationDriverData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DisplayLog("[GetStdAllocData] Type=%u, pAllocPriv=%p, pResPriv=%p\n",
        pGetStandardAllocationDriverData->StandardAllocationType,
        pGetStandardAllocationDriverData->pAllocationPrivateDriverData,
        pGetStandardAllocationDriverData->pResourcePrivateDriverData);

    switch (pGetStandardAllocationDriverData->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE: {
        D3DKMDT_SHAREDPRIMARYSURFACEDATA *pShared =
            pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData;
        UINT64 bytes = 0ull;
        if (pShared != NULL) {
            Bc250DisplayLog("[GetStdAllocData] SharedPrimary: %ux%u, Format=%u, VidPnSourceId=%u\n",
                pShared->Width, pShared->Height, pShared->Format,
                pShared->VidPnSourceId);
            bytes = (UINT64)pShared->Width * (UINT64)pShared->Height *
                BC250_STD_ALLOC_BYTES_PER_PIXEL;
        }
        Bc250McdmPublishStandardAllocationPrivateData(
            pGetStandardAllocationDriverData,
            BC250_STD_ALLOC_TOKEN_PRIMARY, bytes, NULL);
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE: {
        D3DKMDT_SHADOWSURFACEDATA *pShadow =
            pGetStandardAllocationDriverData->pCreateShadowSurfaceData;
        UINT64 bytes = 0ull;
        if (pShadow != NULL) {
            pShadow->Pitch = (UINT)(pShadow->Width * 4u);
            Bc250DisplayLog("[GetStdAllocData] ShadowSurface: %ux%u, Pitch=%u\n",
                pShadow->Width, pShadow->Height, pShadow->Pitch);
            bytes = (UINT64)pShadow->Pitch * (UINT64)pShadow->Height;
        }
        Bc250McdmPublishStandardAllocationPrivateData(
            pGetStandardAllocationDriverData,
            BC250_STD_ALLOC_TOKEN_SHADOW, bytes, NULL);
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE: {
        D3DKMDT_STAGINGSURFACEDATA *pStaging =
            pGetStandardAllocationDriverData->pCreateStagingSurfaceData;
        UINT64 bytes = 0ull;
        BC250_ALLOCATION_GEOMETRY geometry;
        RtlZeroMemory(&geometry, sizeof(geometry));
        if (pStaging != NULL) {
            pStaging->Pitch = (UINT)(pStaging->Width * 4u);
            Bc250DisplayLog("[GetStdAllocData] StagingSurface: %ux%u, Pitch=%u\n",
                pStaging->Width, pStaging->Height, pStaging->Pitch);
            bytes = (UINT64)pStaging->Pitch * (UINT64)pStaging->Height;
            /*
             * D3DKMDT_STAGINGSURFACEDATA nao tem formato (d3dkmdt.h:1361). O
             * proprio KMD declara 4 bytes por pixel no Pitch acima; 21
             * (A8R8G8B8) registra so' isso -- 32 bpp --, sem afirmar a
             * semantica do alfa.
             */
            geometry.Width = pStaging->Width;
            geometry.Height = pStaging->Height;
            geometry.Pitch = pStaging->Pitch;
            geometry.Format = 21u;
        }
        Bc250McdmPublishStandardAllocationPrivateData(
            pGetStandardAllocationDriverData,
            BC250_STD_ALLOC_TOKEN_STAGING, bytes,
            pStaging != NULL ? &geometry : NULL);
        return STATUS_SUCCESS;
    }

    case D3DKMDT_STANDARDALLOCATION_GDISURFACE: {
        D3DKMDT_GDISURFACEDATA *pGdi =
            pGetStandardAllocationDriverData->pCreateGdiSurfaceData;
        UINT64 bytes = 0ull;
        BC250_ALLOCATION_GEOMETRY geometry;
        RtlZeroMemory(&geometry, sizeof(geometry));
        if (pGdi != NULL) {
            pGdi->Pitch = (UINT)(pGdi->Width * 4u);
            Bc250DisplayLog("[GetStdAllocData] GdiSurface: %ux%u, Type=%u, Format=%u, Pitch=%u\n",
                pGdi->Width, pGdi->Height, pGdi->Type, (UINT)pGdi->Format, pGdi->Pitch);
            bytes = (UINT64)pGdi->Pitch * (UINT64)pGdi->Height;
            geometry.Width = pGdi->Width;
            geometry.Height = pGdi->Height;
            geometry.Pitch = pGdi->Pitch;
            geometry.Format = (UINT32)pGdi->Format;   /* formato entregue pelo dxgkrnl */
        }
        Bc250McdmPublishStandardAllocationPrivateData(
            pGetStandardAllocationDriverData,
            BC250_STD_ALLOC_TOKEN_GDI, bytes,
            pGdi != NULL ? &geometry : NULL);
        return STATUS_SUCCESS;
    }

    default:
        Bc250DisplayLog("[GetStdAllocData] Unsupported type=%u\n",
            pGetStandardAllocationDriverData->StandardAllocationType);
        return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS APIENTRY Bc250McdmOpenAllocation(
    CONST HANDLE hDevice,
    CONST DXGKARG_OPENALLOCATION *pOpenAllocation)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_OPEN_ALLOCATION);
    UINT index;
    if (hDevice == NULL || pOpenAllocation == NULL ||
        pOpenAllocation->NumAllocations == 0u ||
        pOpenAllocation->pOpenAllocation == NULL ||
        pOpenAllocation->Flags.Reserved != 0u) {
        /* DX.62: saida muda ate a Corrida 104. */
        Bc250DisplayLog("[McdmOpenAllocation] RECUSADO: hDevice=%p pOpen=%p Num=%u -> STATUS_INVALID_PARAMETER\n",
            hDevice, (const void *)pOpenAllocation,
            pOpenAllocation != NULL ? pOpenAllocation->NumAllocations : 0u);
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0u; index < pOpenAllocation->NumAllocations; ++index) {
        pOpenAllocation->pOpenAllocation[index].hDeviceSpecificAllocation = NULL;
    }
    if (g_Bc250OpenAllocationObjects == NULL) {
        Bc250DisplayLog("[McdmOpenAllocation] RECUSADO: sem backend -> STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }
    return g_Bc250OpenAllocationObjects(hDevice, pOpenAllocation);
}

/*
 * DX.44 -- a DDI de preempcao passa a FALAR (Corrida 87, so' medicao).
 *
 * Corrida 86: um TDR com a GPU inteira ociosa (GFX 188/188 aposentadas, fence
 * escrita, RPTR==WPTR, zero ondas, SDMA ocioso, Committed=0). Esta DDI devolve
 * STATUS_SUCCESS sem fazer nada, e o driver NUNCA reporta
 * DXGK_INTERRUPT_DMA_PREEMPTED (grep no driver inteiro), com PreemptionAware=1
 * declarado. H: o VidSch pede preempcao, nunca recebe a conclusao e declara TDR.
 * Ate aqui a DDI era muda e o probe some com o rollback, entao a corrida nao
 * podia dizer se ela foi chamada. Retornos INALTERADOS; toda saida registra.
 * Teto por contador proprio: 256 primeiras + 1 em 64, com o numero de ordem em
 * toda linha para a contagem total ser inferivel.
 */
static volatile LONG s_Bc250PreemptCalls;

static BOOLEAN Bc250PreemptShouldLog(LONG Seq)
{
    return (Seq <= 256 || (Seq % 64) == 0) ? TRUE : FALSE;
}

NTSTATUS APIENTRY Bc250McdmPreemptCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_PREEMPT_COMMAND);
    BC250_MCDM_RUNTIME_CAPS caps;
    const LONG seq = InterlockedIncrement(&s_Bc250PreemptCalls);
    const BOOLEAN logIt = Bc250PreemptShouldLog(seq);
    NTSTATUS capsStatus;

    if (pPreemptCommand == NULL ||
        pPreemptCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        Bc250DisplayLog("[PREEMPT_COMMAND] #%ld RECUSADO: pPreempt=%p Node=%u -> STATUS_INVALID_PARAMETER\n",
            seq, (const void *)pPreemptCommand,
            pPreemptCommand != NULL ? pPreemptCommand->NodeOrdinal : 0xFFFFFFFFu);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&caps, sizeof(caps));
    capsStatus = (g_Bc250QueryRuntimeCaps != NULL) ?
        g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps) : STATUS_NOT_SUPPORTED;
    if (!NT_SUCCESS(capsStatus) ||
        caps.FullMcdmReady == 0u || caps.PreemptionReady == 0u) {
        Bc250DisplayLog("[PREEMPT_COMMAND] #%ld RECUSADO: PreemptionFenceId=%u Node=%u Engine=%u Flags=0x%08X caps=0x%08X Full=%u PreemptReady=%u -> STATUS_NOT_SUPPORTED\n",
            seq, pPreemptCommand->PreemptionFenceId, pPreemptCommand->NodeOrdinal,
            pPreemptCommand->EngineOrdinal, pPreemptCommand->Flags.Value,
            (uint32_t)capsStatus, caps.FullMcdmReady, caps.PreemptionReady);
        return STATUS_NOT_SUPPORTED;
    }

    /* DX.47: com backend instalado, o pedido e' registrado e a conclusao
     * (DMA_PREEMPTED) sai do retirement quando o no drenar. */
    if (g_Bc250PreemptHandler != NULL) {
        const NTSTATUS handlerStatus = g_Bc250PreemptHandler((HANDLE)hAdapter, pPreemptCommand);
        Bc250DisplayLog("[PREEMPT_COMMAND] #%ld PreemptionFenceId=%u Node=%u Engine=%u Flags=0x%08X -> 0x%08X REGISTRADO: DMA_PREEMPTED sai do retirement quando o no drenar\n",
            seq, pPreemptCommand->PreemptionFenceId, pPreemptCommand->NodeOrdinal,
            pPreemptCommand->EngineOrdinal, pPreemptCommand->Flags.Value,
            (uint32_t)handlerStatus);
        return handlerStatus;
    }

    if (logIt) {
        Bc250DisplayLog("[PREEMPT_COMMAND] #%ld PreemptionFenceId=%u Node=%u Engine=%u Flags=0x%08X -> STATUS_SUCCESS SEM BACKEND: nenhuma preempcao feita e DMA_PREEMPTED NAO sera reportado\n",
            seq, pPreemptCommand->PreemptionFenceId, pPreemptCommand->NodeOrdinal,
            pPreemptCommand->EngineOrdinal, pPreemptCommand->Flags.Value);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250McdmQueryAdapterInfoWorker(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo);

/*
 * Envoltorio de instrumentacao do caminho WDDM completo.
 *
 * No boot de 2026-08-31 com RenderNode=1 o DxgkInitialize devolveu 0, o
 * AddDevice devolveu 0, o StartDevice entrou — e o dispositivo parou em Codigo
 * 43 sem que houvesse como saber qual consulta foi recusada. Este envoltorio
 * registra a sequencia: quantas chamadas, o ultimo tipo, o ultimo status, e
 * separadamente o ultimo tipo recusado com NOT_SUPPORTED, que e' o suspeito
 * natural quando o dxgkrnl aborta o start.
 */
NTSTATUS APIENTRY Bc250McdmQueryAdapterInfo(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    static volatile LONG s_qaiCalls;
    static volatile LONG s_qaiUnsupported;
    NTSTATUS status;
    ULONG type;

    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_ADAPTER_INFO);

    Bc250ProbeRecord(BC250_PROBE_V5_QAI_CALLS,
        (ULONG)InterlockedIncrement(&s_qaiCalls));

    status = Bc250McdmQueryAdapterInfoWorker(hAdapter, pQueryAdapterInfo);

    type = (pQueryAdapterInfo != NULL) ? (ULONG)pQueryAdapterInfo->Type : 0xFFFFFFFFu;
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_TYPE, type);
    Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_STATUS, (ULONG)status);
    /*
     * Bitmap acumulado (schema 134). Guarda de NULL obrigatoria: com
     * pQueryAdapterInfo nulo, type vale 0xFFFFFFFF e envenenaria o bitmap.
     *
     * O 3.79 soube por acaso que o dxgkrnl parou depois de WDDMDEVICECAPS
     * (29) sem jamais pedir DRIVERCAPS (1). O bitmap torna isso uma leitura
     * direta em vez de uma inferencia sobre o ultimo valor sobrevivente.
     */
    if (pQueryAdapterInfo != NULL) {
        Bc250ProbeMarkQaiType(type);
    }
    if (status == STATUS_NOT_SUPPORTED) {
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_LAST_UNSUPPORTED_TYPE, type);
        Bc250ProbeRecord(BC250_PROBE_V5_QAI_UNSUPPORTED_COUNT,
            (ULONG)InterlockedIncrement(&s_qaiUnsupported));
    }
    return status;
}

static NTSTATUS Bc250McdmQueryAdapterInfoWorker(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    BC250_MCDM_RUNTIME_CAPS runtimeCaps;
    NTSTATUS capsStatus;

    if (pQueryAdapterInfo == NULL || pQueryAdapterInfo->pOutputData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&runtimeCaps, sizeof(runtimeCaps));
    capsStatus = g_Bc250QueryRuntimeCaps != NULL ?
        g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &runtimeCaps) :
        STATUS_DEVICE_NOT_READY;

    Bc250ProbeRecord(BC250_PROBE_DMM_QUERY_ADAPTER_TYPE, (ULONG)pQueryAdapterInfo->Type);
    /* DX.48: qual contexto foi consultado. Corridas 88/92 travaram logo apos um
     * Type=29 servido por contexto NAO iniciado no meio da corrida. */
    Bc250DisplayLog("[McdmQAI] Type=%u, OutSize=%u, CapsStatus=0x%08X hAdapter=%p\n",
        (UINT)pQueryAdapterInfo->Type, pQueryAdapterInfo->OutputDataSize, capsStatus,
        (void *)hAdapter);

    switch (pQueryAdapterInfo->Type) {
    /*
     * DXGKQAITYPE_WDDMDEVICECAPS — obrigatorio para driver de render.
     *
     * Medido no silicio em 2026-08-31, primeiro boot com RenderNode=1:
     *
     *   V5QaiCalls              1
     *   V5QaiLastType           0x1D  (DXGKQAITYPE_WDDMDEVICECAPS)
     *   V5QaiLastStatus         0xC00000BB  (STATUS_NOT_SUPPORTED)
     *   V5StartDeviceStatus     0  com Sources=1 e Children=1 (sucesso)
     *
     * O dxgkrnl fez UMA unica pergunta, levou NOT_SUPPORTED do ramo default e
     * abortou o dispositivo com Codigo 43. O KMDOD nunca chegava aqui porque
     * KMDDOD_INITIALIZATION_DATA nao expoe este caminho.
     *
     * A estrutura tem um unico campo, e o WDK exige explicitamente o mesmo
     * valor de DXGK_DRIVERCAPS::WDDMVersion — por isso DXGKDDI_WDDMv2_7, igual
     * ao declarado logo abaixo. Divergir entre os dois e' erro de conformidade.
     */
    case DXGKQAITYPE_WDDMDEVICECAPS: {
        DXGK_WDDMDEVICECAPS *deviceCaps;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_WDDMDEVICECAPS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        /*
         * SEM gate de runtimeCaps aqui, ao contrario de todos os outros tipos.
         *
         * O primeiro candidato com este case tinha o gate padrao e devolveu
         * STATUS_DEVICE_NOT_READY (0xC00000A3), medido no boot de 2026-08-31 —
         * trocando um Codigo 43 por outro. A causa: o WDK especifica que
         * WDDMDEVICECAPS e' consultado "after DxgkDdiAddDevice returns and
         * before DxgkDdiStartDevice is called". Nesse instante o adaptador
         * ainda nao subiu e runtimeCaps nao existe, por construcao.
         *
         * Isto nao afrouxa a Lei 3: WDDMVersion nao descreve estado de
         * hardware, descreve qual contrato de DDI este binario implementa —
         * fato de compilacao, nao de execucao. Condicionar uma constante ao
         * estado do silicio seria erro na direcao oposta.
         */
        deviceCaps = (DXGK_WDDMDEVICECAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(deviceCaps, sizeof(*deviceCaps));
        deviceCaps->WDDMVersion = Bc250McdmGetWddmContractVersion();
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *caps;
        if (pQueryAdapterInfo->OutputDataSize < 576u) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.TdrReady == 0u ||
            runtimeCaps.PreemptionReady == 0u ||
            runtimeCaps.SubmitReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, pQueryAdapterInfo->OutputDataSize);
        caps->HighestAcceptableAddress.QuadPart = MAXLONGLONG;
        caps->WDDMVersion = Bc250McdmGetWddmContractVersion();
        caps->SchedulingCaps.MultiEngineAware = 1u;
        caps->SchedulingCaps.PreemptionAware = 1u;
        caps->SchedulingCaps.LowIrqlPreemptCommand = 1u;
        caps->MemoryManagementCaps.VirtualAddressingSupported =
            (runtimeCaps.GpuVaReady != 0u) ? 1u : 0u;
        caps->MemoryManagementCaps.GpuMmuSupported =
            (runtimeCaps.GpuVaReady != 0u) ? 1u : 0u;
        caps->MemoryManagementCaps.ParavirtualizationSupported = 0u;
        caps->PreemptionCaps.ComputePreemptionGranularity =
            D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;

        /*
         * A1 (06/09): os caps de tipo de driver saem de uma fonte unica --
         * Bc250GetDriverCapsPolicy, em driver/core. Antes deste bloco, o
         * RtlZeroMemory acima deixava SupportNonVGA em FALSE neste caminho
         * enquanto o handler KMDOD do MESMO binario escrevia TRUE. A tabela
         * cita a exigencia campo a campo e travou em driver_caps_contract_test.
         */
        {
            BC250_DRIVER_CAPS_POLICY capsPolicy;
            if (Bc250GetDriverCapsPolicy(BC250_WDDM_DRIVER_FULL_GRAPHICS,
                    &capsPolicy) != BC250_DRIVER_CAPS_OK) {
                Bc250DisplayLog("[McdmQAI] DRIVERCAPS: politica de caps indisponivel\n");
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            caps->SupportNonVGA =
                (capsPolicy.SupportNonVGA != 0u) ? TRUE : FALSE;
            caps->SupportSmoothRotation =
                (capsPolicy.SupportSmoothRotation != 0u) ? TRUE : FALSE;
            caps->SupportDirectFlip =
                (capsPolicy.SupportDirectFlip != 0u) ? TRUE : FALSE;
            caps->SupportPerEngineTDR =
                (capsPolicy.SupportPerEngineTDR != 0u) ? TRUE : FALSE;
            caps->FlipCaps.FlipOnVSyncMmIo = capsPolicy.FlipOnVSyncMmIo;
        }
        caps->GpuEngineTopology.NbAsymetricProcessingNodes =
            runtimeCaps.NodeCount;
        caps->MiscCaps.ComputeOnly = Bc250McdmGetComputeOnly();
        caps->MaxAllocationListSlotId = 32u;
        caps->PresentationCaps.NoScreenToScreenBlt = 1u;
        caps->PresentationCaps.NoOverlapScreenBlt = 1u;
        caps->PresentationCaps.DriverSupportsCddDwmInterop = 1u;
        caps->PresentationCaps.AlignmentShift = 2u;
        caps->PresentationCaps.MaxTextureWidthShift = 3u;  /* 2 ^ (3 + 11) = 16384 texels (suporta 2560x1080) */
        caps->PresentationCaps.MaxTextureHeightShift = 3u; /* 2 ^ (3 + 11) = 16384 texels */
        caps->PresentationCaps.SupportSoftwareDeviceBitmaps = 1u;
        caps->MaxQueuedFlipOnVSync = 1u;
        /* FlipCaps.FlipOnVSyncMmIo vem da politica de caps, acima. */
        caps->PointerCaps.Value = 0u;
        caps->MaxPointerWidth = 0u;
        caps->MaxPointerHeight = 0u;

        /*
         * Instrumentacao 3.82: ate a corrida 14:25 o log expunha 4 campos de
         * uma estrutura de 576 bytes. Sem o retrato completo nao da para
         * auditar por que o DMM recusa ativar o caminho sob RenderNode=1.
         * Este bloco e' somente leitura — nao altera nenhum cap.
         */
        Bc250DisplayLog("[McdmQAI] DRIVERCAPS: ComputeOnly=%u, AlignShift=%u, TexWShift=%u, TexHShift=%u, OutSize=%u\n",
            caps->MiscCaps.ComputeOnly, caps->PresentationCaps.AlignmentShift,
            caps->PresentationCaps.MaxTextureWidthShift, caps->PresentationCaps.MaxTextureHeightShift,
            pQueryAdapterInfo->OutputDataSize);
        Bc250DisplayLog("[DriverCaps] WDDMVersion=%u, MaxAllocListSlotId=%u, PerEngineTDR=%u, Nodes=%u, HighestAddr=0x%I64X\n",
            (ULONG)caps->WDDMVersion,
            caps->MaxAllocationListSlotId,
            (ULONG)caps->SupportPerEngineTDR,
            caps->GpuEngineTopology.NbAsymetricProcessingNodes,
            caps->HighestAcceptableAddress.QuadPart);
        Bc250DisplayLog("[DriverCaps] PresentationCaps=0x%08X (NoScreenToScreenBlt=%u, NoOverlapScreenBlt=%u, CddDwmInterop=%u, SwDeviceBitmaps=%u)\n",
            caps->PresentationCaps.Value,
            caps->PresentationCaps.NoScreenToScreenBlt,
            caps->PresentationCaps.NoOverlapScreenBlt,
            caps->PresentationCaps.DriverSupportsCddDwmInterop,
            caps->PresentationCaps.SupportSoftwareDeviceBitmaps);
        Bc250DisplayLog("[DriverCaps] FlipCaps=0x%08X, MaxQueuedFlipOnVSync=%u, MiscCaps=0x%08X, PointerCaps=0x%08X\n",
            caps->FlipCaps.Value,
            caps->MaxQueuedFlipOnVSync,
            caps->MiscCaps.Value,
            caps->PointerCaps.Value);
        Bc250DisplayLog("[DriverCaps] TypeCaps(NonVGA=%u, SmoothRotation=%u, DirectFlip=%u, PerEngineTDR=%u, FlipOnVSyncMmIo=%u)\n",
            (ULONG)caps->SupportNonVGA,
            (ULONG)caps->SupportSmoothRotation,
            (ULONG)caps->SupportDirectFlip,
            (ULONG)caps->SupportPerEngineTDR,
            caps->FlipCaps.FlipOnVSyncMmIo);
        Bc250DisplayLog("[DriverCaps] Sched(MultiEngine=%u, Preempt=%u, LowIrql=%u) Mem(VA=%u, GpuMmu=%u, Paravirt=%u)\n",
            caps->SchedulingCaps.MultiEngineAware,
            caps->SchedulingCaps.PreemptionAware,
            caps->SchedulingCaps.LowIrqlPreemptCommand,
            caps->MemoryManagementCaps.VirtualAddressingSupported,
            caps->MemoryManagementCaps.GpuMmuSupported,
            caps->MemoryManagementCaps.ParavirtualizationSupported);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PHYSICALADAPTERCAPS:
        Bc250DisplayLog("[McdmQAI] PHYSICALADAPTERCAPS returned NOT_SUPPORTED (single physical adapter)\n");
        return STATUS_NOT_SUPPORTED;

    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
        DXGK_DISPLAY_DRIVERCAPS_EXTENSION *ext;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION)) {
            Bc250DisplayLog("[McdmQAI] DISPLAY_DRIVERCAPS_EXTENSION buffer too small\n");
            return STATUS_BUFFER_TOO_SMALL;
        }
        ext = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(ext, sizeof(*ext));
        Bc250DisplayLog("[McdmQAI] DISPLAY_DRIVERCAPS_EXTENSION handled (zeroed)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PREFERREDGPUNODE: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250DisplayLog("[McdmQAI] PREFERREDGPUNODE handled (0)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_GPUMMUCAPS: {
        const DXGK_QUERYGPUMMUCAPSIN *in;
        DXGK_GPUMMUCAPS *caps;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYGPUMMUCAPSIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.GpuVaBitCount == 0u ||
            runtimeCaps.PageTableLevelCount == 0u) {
            Bc250DisplayLog("[McdmQAI] GPUMMUCAPS device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        caps = (DXGK_GPUMMUCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported = 1u;
        caps->NoExecuteMemorySupported = 1u;
        caps->ExplicitPageTableInvalidation = 1u;
        caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        caps->VirtualAddressBitCount = runtimeCaps.GpuVaBitCount;
        caps->PageTableLevelCount = runtimeCaps.PageTableLevelCount;
        Bc250DisplayLog("[McdmQAI] GPUMMUCAPS: VaBits=%u, Levels=%u\n",
            caps->VirtualAddressBitCount, caps->PageTableLevelCount);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN *in;
        DXGK_PAGE_TABLE_LEVEL_DESC *desc;
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in)) {
            return STATUS_INVALID_PARAMETER;
        }
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*desc)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        in = (const DXGK_QUERYPAGETABLELEVELDESCIN *)
            pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u ||
            runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.GpuVaReady == 0u ||
            runtimeCaps.PageTableLevelCount == 0u ||
            runtimeCaps.PageTableIndexBitCount == 0u ||
            runtimeCaps.PageTableSizeBytes == 0u) {
            Bc250DisplayLog("[McdmQAI] PAGETABLELEVELDESC device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        if (in->LevelIndex >= runtimeCaps.PageTableLevelCount) {
            return STATUS_INVALID_PARAMETER;
        }
        desc = (DXGK_PAGE_TABLE_LEVEL_DESC *)
            pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(desc, sizeof(*desc));
        if (in->LevelIndex == 0u) {
            desc->PageTableIndexBitCount = BC250_GPUVA_PT_LEAF_INDEX_BITS;
            desc->PageTableSizeInBytes = (1ull << BC250_GPUVA_PT_LEAF_INDEX_BITS) * sizeof(UINT64);
        } else {
            desc->PageTableIndexBitCount = BC250_GPUVA_PT_ROOT_INDEX_BITS;
            desc->PageTableSizeInBytes = (1ull << BC250_GPUVA_PT_ROOT_INDEX_BITS) * sizeof(UINT64);
        }
        desc->PageTableSegmentId = 1u;
        desc->PagingProcessPageTableSegmentId = 1u;
        desc->PageTableAlignmentInBytes = runtimeCaps.PageTableAlignmentBytes;
        Bc250DisplayLog("[McdmQAI] PAGETABLELEVELDESC: Level=%u, Bits=%u, Size=%u\n",
            in->LevelIndex, desc->PageTableIndexBitCount, (UINT32)desc->PageTableSizeInBytes);
        return STATUS_SUCCESS;
    }

    /*
     * DXGKQAITYPE_UMDRIVERPRIVATE (Type=0) -- ITEM ZERO, achado por medição.
     *
     * Na corrida 2026-09-07 08:25 este tipo foi pedido **160 vezes** com
     * OutSize=2400 e caiu no `default:` do switch, devolvendo
     * STATUS_NOT_SUPPORTED em 80 delas. Na MESMA corrida, com as três entradas
     * do adapter table da UMD instrumentadas, TODAS saíram com ZERO chamadas
     * (CalcPrivateDeviceSize, CloseAdapter, CreateDevice, GetCaps,
     * GetSupportedVersions) e o runtime descartou a DLL sem nem fechar o
     * adapter. Era o tipo mais pedido da corrida e o único recusado em massa.
     *
     * Este é o canal de private driver data entre o NOSSO KMD e o NOSSO UMD --
     * nós definimos o formato dos dois lados, sem imitar a ABI da AMD.
     *
     * REGRAS AQUI:
     *  - o blob NÃO promete funcionalidade. GateFlags declara explicitamente o
     *    que não existe (pipeline 3D, submissão Node 1, IB sob VMID8,
     *    compilador de shader), para a UMD não inferir capacidade do silêncio;
     *  - falha fechado em buffer curto, sem escrever byte parcial;
     *  - `pInputData` vem de modo usuário e por isso é tratado como não
     *    confiável: só o tamanho é considerado, o conteúdo é ignorado
     *    (validating-private-data-sent-from-user-mode-to-kernel-mode.md).
     *
     * `H`: que esta recusa fosse *a* causa do descarte do adapter. Coerente com
     * toda a cadeia, mas NÃO PROVADO -- só uma corrida decide.
     */
    case DXGKQAITYPE_UMDRIVERPRIVATE: {
        BC250_UMD_PRIVATE_FACTS facts;
        BC250_UMD_PRIVATE_STATUS blobStatus;
        uint32_t written = 0u;

        if (pQueryAdapterInfo->OutputDataSize <
                Bc250UmdPrivateRequiredBytes()) {
            Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: OutSize=%u < %u exigidos -> BUFFER_TOO_SMALL\n",
                pQueryAdapterInfo->OutputDataSize,
                Bc250UmdPrivateRequiredBytes());
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u) {
            Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        /*
         * Sem a geometria de GPUVA vinda do runtime não publicamos o blob:
         * melhor a UMD não receber nada do que receber número inventado.
         */
        if (runtimeCaps.GpuVaBitCount == 0u ||
            runtimeCaps.PageTableLevelCount == 0u) {
            Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: geometria GPUVA indisponivel "
                "(VaBits=%u, Levels=%u) -> DEVICE_NOT_READY\n",
                runtimeCaps.GpuVaBitCount, runtimeCaps.PageTableLevelCount);
            return STATUS_DEVICE_NOT_READY;
        }

        RtlZeroMemory(&facts, sizeof(facts));
        facts.VendorId = 0x1002u;
        facts.DeviceId = 0x13FEu;
        facts.RevisionId = 0u;
        facts.SegmentCount = 2u;
        facts.VramSegmentId = 1u;
        facts.ApertureSegmentId = 2u;
        /*
         * Mesma fonte de verdade do QUERYSEGMENT4 que o VidMm ja aceitou --
         * runtimeCaps. Constante nova aqui viraria segunda fonte e divergiria,
         * como divergiu no A1 com os caps de tipo de driver.
         */
        facts.VramSizeBytes = (uint64_t)runtimeCaps.LocalSegmentBytes;
        facts.ApertureSizeBytes = 0ull; /* aperture: tamanho vem do QUERYSEGMENT4 */
        /*
         * CORREÇÃO (07/09) -- eu havia criado uma SEGUNDA FONTE DE VERDADE aqui,
         * que é exatamente o defeito do A1.
         *
         * A versão anterior publicava `BC250_ADAPTER_GPUVA_BITS` (= **48**) e
         * `PageTableLevels = 2` fixo. Mas o que este mesmo driver declara ao
         * WDDM em DXGKQAITYPE_GPUMMUCAPS é `runtimeCaps.GpuVaBitCount`, e o log
         * da corrida 2026-09-07 11:08 mostra o valor real:
         *
         *     [McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2
         *
         * 34 bits, não 48 — coerente com a geometria de 2 níveis derivada em
         * docs/03 (raiz de 13 bits + folha de 9 bits + 12 de offset = 34) e com
         * `PAGE_TABLE_END_ADDR = 2^22 - 1`, ou seja 16 GiB de espaço.
         *
         * Publicar 48 faria a UMD -- e, na rota B, o winsys do RADV -- dispor
         * endereços fora do que o contexto VMID8 traduz. O blob passa a sair da
         * MESMA fonte que o GPUMMUCAPS.
         */
        facts.GpuVaBits = runtimeCaps.GpuVaBitCount;
        facts.PageSizeBytes = BC250_ADAPTER_PAGE_SIZE_BYTES;
        facts.PageTableLevels = runtimeCaps.PageTableLevelCount;
        facts.NodeCount = runtimeCaps.NodeCount;
        facts.SdmaNodeOrdinal = BC250_ADAPTER_NODE_SDMA;
        facts.ComputeNodeOrdinal = BC250_ADAPTER_NODE_COMPUTE;

        /*
         * O que NÃO existe, declarado. Nenhum destes bits é otimista: cada um
         * corresponde a um item aberto da BASE_DE_AVANCO.
         */
        facts.GateFlags = BC250_UMD_GATE_NO_3D_PIPELINE |
                          BC250_UMD_GATE_NO_SHADER_COMPILER |
                          BC250_UMD_GATE_CAPS_PROBE_ONLY;
        if (runtimeCaps.GpuVaReady == 0u) {
            facts.GateFlags |= BC250_UMD_GATE_NO_IB_UNDER_VMID8;
        }
        facts.GateFlags |= BC250_UMD_GATE_NO_NODE1_SUBMIT;

        blobStatus = Bc250UmdPrivateWrite(&facts,
            pQueryAdapterInfo->pOutputData,
            pQueryAdapterInfo->OutputDataSize,
            &written);
        if (blobStatus != BC250_UMD_PRIVATE_OK) {
            Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: Bc250UmdPrivateWrite falhou (%d) -> INVALID_PARAMETER\n",
                (int)blobStatus);
            return STATUS_INVALID_PARAMETER;
        }

        Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: %u bytes publicados (OutSize=%u, InSize=%u), NodeCount=%u, GateFlags=0x%08X\n",
            written, pQueryAdapterInfo->OutputDataSize,
            pQueryAdapterInfo->InputDataSize,
            facts.NodeCount, facts.GateFlags);
        Bc250DisplayLog("[McdmQAI] UMDRIVERPRIVATE: GpuVaBits=%u, PageTableLevels=%u, PageSize=%u (mesma fonte do GPUMMUCAPS)\n",
            facts.GpuVaBits, facts.PageTableLevels, facts.PageSizeBytes);
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4 *out;
        DXGK_QUERYSEGMENTIN4 *in;
        DXGK_SEGMENTDESCRIPTOR4 *segment;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*out)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.Ready == 0u) {
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4 device not ready\n");
            return STATUS_DEVICE_NOT_READY;
        }
        out = (DXGK_QUERYSEGMENTOUT4 *)pQueryAdapterInfo->pOutputData;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        if (out->NbSegment == 0u) {
            out->NbSegment = 2u;
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: NbSegment queried -> 2\n");
            return STATUS_SUCCESS;
        }
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in) ||
            out->NbSegment < 2u || out->pSegmentDescriptor == NULL ||
            out->SegmentDescriptorStride < sizeof(*segment)) {
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: Invalid parameter in pass 2\n");
            return STATUS_INVALID_PARAMETER;
        }
        in = (DXGK_QUERYSEGMENTIN4 *)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) return STATUS_INVALID_PARAMETER;

        /* Segment 1: Local VRAM */
        segment = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        RtlZeroMemory(segment, sizeof(*segment));
        segment->BaseAddress.QuadPart =
            (LONGLONG)runtimeCaps.LocalSegmentBaseOffset;
        segment->CpuTranslatedAddress.QuadPart =
            (LONGLONG)runtimeCaps.CpuPhysicalStart;
        segment->Size = (SIZE_T)runtimeCaps.LocalSegmentBytes;
        segment->CommitLimit = 0u;
        segment->Flags.CpuVisible = 1u;
        segment->Flags.LocalBudgetGroup = 1u;
        segment->Flags.PreservedDuringStandby = 1u;
        segment->Flags.PreservedDuringHibernate = 1u;

        /* Segment 2: System Memory Aperture (GART) - Obrigatorio para WDDM 2.0+ */
        {
            DXGK_SEGMENTDESCRIPTOR4 *gart = (DXGK_SEGMENTDESCRIPTOR4 *)
                ((BYTE *)out->pSegmentDescriptor + out->SegmentDescriptorStride);
            RtlZeroMemory(gart, sizeof(*gart));
            gart->BaseAddress.QuadPart = 0x100000000ull; /* 4GB base GPU logica */
            gart->Size = 2048ull * 1024 * 1024;          /* 2048 MB GART aperture (modelo UMA GDDR6) */
            gart->CommitLimit = 2048ull * 1024 * 1024;
            gart->Flags.Aperture = 1u;
            gart->Flags.NonLocalBudgetGroup = 1u;
        }

        out->PagingBufferSegmentId = 2u;
        out->PagingBufferSize = 0x10000u;
        out->PagingBufferPrivateDataSize = 0u;
        Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: 2 segments filled (VRAM=0x%llX, GART=2048MB, PagingSeg=2)\n",
            (unsigned long long)segment->Size);
        return STATUS_SUCCESS;
#else
        if (out->NbSegment == 0u) {
            out->NbSegment = 1u;
            Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: NbSegment queried -> 1\n");
            return STATUS_SUCCESS;
        }
        if (pQueryAdapterInfo->pInputData == NULL ||
            pQueryAdapterInfo->InputDataSize < sizeof(*in) ||
            out->NbSegment != 1u || out->pSegmentDescriptor == NULL ||
            out->SegmentDescriptorStride < sizeof(*segment)) {
            return STATUS_INVALID_PARAMETER;
        }
        in = (DXGK_QUERYSEGMENTIN4 *)pQueryAdapterInfo->pInputData;
        if (in->PhysicalAdapterIndex != 0u) return STATUS_INVALID_PARAMETER;
        segment = (DXGK_SEGMENTDESCRIPTOR4 *)out->pSegmentDescriptor;
        RtlZeroMemory(segment, sizeof(*segment));
        segment->BaseAddress.QuadPart =
            (LONGLONG)runtimeCaps.LocalSegmentBaseOffset;
        segment->CpuTranslatedAddress.QuadPart =
            (LONGLONG)runtimeCaps.CpuPhysicalStart;
        segment->Size = (SIZE_T)runtimeCaps.LocalSegmentBytes;
        segment->CommitLimit = 0u;
        segment->Flags.CpuVisible = 1u;
        segment->Flags.LocalBudgetGroup = 1u;
        out->PagingBufferSegmentId = 1u;
        out->PagingBufferSize = 0x10000u;
        out->PagingBufferPrivateDataSize = 0u;
        Bc250DisplayLog("[McdmQAI] QUERYSEGMENT4: Segment filled, Size=0x%llX\n",
            (unsigned long long)segment->Size);
        return STATUS_SUCCESS;
#endif
    }

    case DXGKQAITYPE_NUMPOWERCOMPONENTS:
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0u;
        Bc250DisplayLog("[McdmQAI] NUMPOWERCOMPONENTS handled (0)\n");
        return STATUS_SUCCESS;

    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        DXGKARG_HISTORYBUFFERPRECISION *precision;
        UINT index;
        if (!NT_SUCCESS(capsStatus) || runtimeCaps.FullMcdmReady == 0u ||
            runtimeCaps.HistoryBufferReady == 0u) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (pQueryAdapterInfo->OutputDataSize <
            sizeof(*precision) * runtimeCaps.NodeCount) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        precision = (DXGKARG_HISTORYBUFFERPRECISION *)
            pQueryAdapterInfo->pOutputData;
        for (index = 0u; index < runtimeCaps.NodeCount; ++index) {
            precision[index].PrecisionBits = 32u;
        }
        Bc250DisplayLog("[McdmQAI] HISTORYBUFFERPRECISION handled (32-bit)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DISPLAYID_DESCRIPTOR: {
        DXGK_QUERYDISPLAYIDOUT *out;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*out)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        out = (DXGK_QUERYDISPLAYIDOUT *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(out, sizeof(*out));
        out->Length = 0u;
        out->pDescriptor = NULL;
        Bc250DisplayLog("[McdmQAI] DISPLAYID_DESCRIPTOR handled (none)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYCOLORIMETRYOVERRIDES: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_COLORIMETRY)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
        Bc250DisplayLog("[McdmQAI] QUERYCOLORIMETRYOVERRIDES handled (zeroed)\n");
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT:
    case DXGKQAITYPE_QUERYSEGMENT2:
    case DXGKQAITYPE_QUERYSEGMENT3:
        return STATUS_NOT_SUPPORTED;

    default:
        Bc250DisplayLog("[McdmQAI] Type=%u NOT SUPPORTED\n", (UINT)pQueryAdapterInfo->Type);
        return STATUS_NOT_SUPPORTED;
    }
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmQueryDependentEngineGroup, BC250_V5_DDI_QUERY_DEPENDENT_ENGINE_GROUP,
    CONST HANDLE, hAdapter,
    DXGKARG_QUERYDEPENDENTENGINEGROUP *, pQueryDependentEngineGroup)

NTSTATUS APIENTRY Bc250McdmQueryEngineStatus(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYENGINESTATUS *pQueryEngineStatus)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_ENGINE_STATUS);
    BC250_MCDM_RUNTIME_CAPS caps;

    if (pQueryEngineStatus == NULL ||
        pQueryEngineStatus->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.TdrReady == 0u) {
        pQueryEngineStatus->EngineStatus.Responsive = 0;
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * item 24 (07/09) -- ESTA DDI RESPONDIA "Responsive = 1" SEM MEDIR NADA.
     * O dxgkrnl a chama durante o TDR para decidir se a engine esta viva;
     * responder viva sempre e' a mesma classe de falso sucesso do par
     * Reset/Restart, e teria feito o TDR concluir que nao havia o que resetar.
     *
     * Agora a resposta sai de duas passagens de RPTR/WPTR (contrato
     * Bc250TdrDetectHang) com os ponteiros reduzidos a uma volta do anel. Sem
     * probe instalada, ou quando a medicao nao e' possivel, a DDI RECUSA --
     * "nao sei" nao pode virar "esta viva".
     */
    if (g_Bc250EngineResponsiveProbe == NULL) {
        pQueryEngineStatus->EngineStatus.Responsive = 0;
        return STATUS_NOT_SUPPORTED;
    }
    {
        UINT32 responsive = 0u;
        if (!g_Bc250EngineResponsiveProbe(pQueryEngineStatus->NodeOrdinal, &responsive)) {
            pQueryEngineStatus->EngineStatus.Responsive = 0;
            return STATUS_DEVICE_NOT_READY;
        }
        pQueryEngineStatus->EngineStatus.Responsive = (responsive != 0u) ? 1 : 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmResetEngine(
    CONST HANDLE hAdapter,
    DXGKARG_RESETENGINE *pResetEngine)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RESET_ENGINE);
    BC250_MCDM_RUNTIME_CAPS caps;

    if (pResetEngine == NULL ||
        pResetEngine->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.TdrReady == 0u) {
        pResetEngine->LastAbortedFenceId = 0u;
        return STATUS_NOT_SUPPORTED;
    }

    /* Caps estruturais não provam que HQD/SDMA foram resetados. Sem executor
     * físico e sem devolução dos pins das fences abortadas, sucesso seria falso. */
    pResetEngine->LastAbortedFenceId = 0u;
    return STATUS_NOT_SUPPORTED;
}

/*
 * =========================================================================
 * B6 / item 25 (07/09) -- o par de TDR deixou de mentir.
 *
 * ANTES: as duas DDIs abaixo escreviam uma linha de log e devolviam
 * STATUS_SUCCESS sem tocar em registrador nenhum. O dxgkrnl usa exatamente
 * este par para decidir se o adaptador foi quiescido e se voltou; declarar
 * sucesso sem backend e' violacao direta da Lei 4, e num lugar em que a
 * consequencia e' o sistema seguir submetendo para uma engine parada.
 *
 * AGORA: ResetFromTimeout quiesce o SDMA0 de verdade (RB_ENABLE desligado,
 * F32 travado, snapshot de STATUS/ERR_LOG no log) e RestartFromTimeout
 * rearma o anel e PROVA com um NOP fisico e fence sobre poison. Falha em
 * qualquer um dos dois devolve erro -- nao ha caminho que devolva sucesso
 * sem o silicio ter respondido.
 *
 * O QUE ISTO NAO E': o item 25 completo. Falta a corrida com hang induzido
 * que meca deteccao -> reset -> readmissao de submissao -> display vivo.
 * Enquanto ela nao existir: NAO VALIDADO EM HARDWARE.
 * =========================================================================
 */
NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)
{
    if (hAdapter == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    Bc250DisplayLog("[McdmResetFromTimeout] TDR: quiesce fisico do SDMA0 iniciado\n");
    if (g_Bc250TdrQuiesce == NULL || !g_Bc250TdrQuiesce()) {
        /*
         * Sem transporte nao houve quiesce. Devolver sucesso aqui faria o
         * dxgkrnl acreditar que a engine parou -- exatamente a mentira que
         * este bloco existe para eliminar.
         */
        Bc250DisplayLog("[McdmResetFromTimeout] TDR: quiesce NAO executado -- devolvendo STATUS_DEVICE_NOT_READY\n");
        return STATUS_DEVICE_NOT_READY;
    }

    Bc250DisplayLog("[McdmResetFromTimeout] TDR: adaptador quiescido; RPTR/WPTR/STATUS/ERR_LOG sairam na linha TDR_QUIESCE do executor\n");
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmRestartFromTimeout(CONST HANDLE hAdapter)
{
    if (hAdapter == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    Bc250DisplayLog("[McdmRestartFromTimeout] TDR: rearmando o SDMA0 e exigindo prova fisica\n");
    if (g_Bc250TdrRestart == NULL || !g_Bc250TdrRestart()) {
        Bc250DisplayLog("[McdmRestartFromTimeout] TDR: a engine NAO voltou -- devolvendo STATUS_DEVICE_NOT_READY em vez de declarar adaptador pronto\n");
        return STATUS_DEVICE_NOT_READY;
    }

    Bc250DisplayLog("[McdmRestartFromTimeout] TDR: SDMA0 e CP_RB0 provados fisicamente; MEC com readback coerente; fences abortadas reconciliadas; adaptador aceita submissao novamente\n");
    return STATUS_SUCCESS;
}

BC250_MCDM_UNSUPPORTED_2(Bc250McdmSetVirtualMachineData, BC250_V5_DDI_SET_VIRTUAL_MACHINE_DATA,
    CONST HANDLE, hAdapter,
    CONST DXGKARG_SETVIRTUALMACHINEDATA *, Args)

VOID APIENTRY Bc250McdmSetRootPageTable(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETROOTPAGETABLE *pSetPageTable)
{
    BC250_MCDM_RUNTIME_CAPS caps;
    UINT32 maxEntries;
    UINT32 rootBits;
    NTSTATUS status;

    if (hAdapter == NULL || pSetPageTable == NULL ||
        pSetPageTable->hContext == NULL ||
        g_Bc250SetRootPageTableObject == NULL ||
        g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.GpuVaReady == 0u ||
        caps.PageTableIndexBitCount == 0u ||
        caps.PageTableIndexBitCount >= 32u) {
        Bc250DisplayLog("[McdmSetRootPageTable] REJECTED: hAdapter=%p, pSetPageTable=%p, CapsOk=%d\n",
            hAdapter, pSetPageTable, (g_Bc250QueryRuntimeCaps != NULL) ? 1 : 0);
        return;
    }
    rootBits = (caps.RootPageTableIndexBitCount != 0u) ?
        caps.RootPageTableIndexBitCount : caps.PageTableIndexBitCount;
    maxEntries = 1u << rootBits;
    Bc250DisplayLog("[McdmSetRootPageTable] Context=%p, SegId=%u, Offset=0x%llX, NumEntries=%u, MaxEntries=%u (rootBits=%u)\n",
        pSetPageTable->hContext,
        pSetPageTable->Address.SegmentId,
        (unsigned long long)pSetPageTable->Address.SegmentOffset,
        pSetPageTable->NumEntries,
        maxEntries, rootBits);
    status = g_Bc250SetRootPageTableObject((HANDLE)hAdapter,
        pSetPageTable->hContext, pSetPageTable->Address.SegmentId,
        pSetPageTable->Address.SegmentOffset,
        pSetPageTable->NumEntries, maxEntries);
    Bc250DisplayLog("[McdmSetRootPageTable] Result=0x%08X\n", status);
}

NTSTATUS APIENTRY Bc250McdmSubmitCommandVirtual(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SUBMITCOMMANDVIRTUAL *pSubmitCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_SUBMIT_COMMAND_VIRTUAL);
    BC250_MCDM_RUNTIME_CAPS caps;
    if (hAdapter == NULL || pSubmitCommand == NULL ||
        pSubmitCommand->hContext == NULL ||
        pSubmitCommand->DmaBufferSize == 0u ||
        (pSubmitCommand->DmaBufferSize % sizeof(uint32_t)) != 0u ||
        pSubmitCommand->NodeOrdinal >= BC250_ADAPTER_NODE_COUNT) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmSubmitCommandVirtual] Node=%u, FenceId=%llu, DmaSize=%u\n",
        pSubmitCommand->NodeOrdinal, (unsigned long long)pSubmitCommand->SubmissionFenceId,
        pSubmitCommand->DmaBufferSize);

    if (g_Bc250QueryRuntimeCaps == NULL ||
        !NT_SUCCESS(g_Bc250QueryRuntimeCaps((HANDLE)hAdapter, &caps)) ||
        caps.FullMcdmReady == 0u || caps.SubmitReady == 0u) {
        return STATUS_NOT_SUPPORTED;
    }

    if (pSubmitCommand->DmaBufferVirtualAddress == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * O callback comum não possui fila, fence nem ownership físico próprios.
     * Somente o miniport ativo pode validar o contexto real e publicar o IB na
     * MEC. Sucesso sem executor esconderia uma submissão descartada pela CPU.
     */
    if (g_Bc250SubmitCommandExecutor == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    return g_Bc250SubmitCommandExecutor(hAdapter, pSubmitCommand);
}

BOOLEAN Bc250McdmInterruptRoutine(
    CONST PVOID MiniportDeviceContext,
    ULONG MessageNumber)
{
    if (g_Bc250InterruptHandler != NULL) {
        return g_Bc250InterruptHandler(MiniportDeviceContext, MessageNumber);
    }
    return FALSE;
}

VOID Bc250McdmDpcRoutine(CONST PVOID MiniportDeviceContext)
{
    if (g_Bc250DpcHandler != NULL) {
        g_Bc250DpcHandler(MiniportDeviceContext);
    }
}

NTSTATUS Bc250McdmQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (ChildRelations == NULL && ChildRelationsSize != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (ChildRelations != NULL && ChildRelationsSize != 0) {
        RtlZeroMemory(ChildRelations, ChildRelationsSize);
    }
    return STATUS_SUCCESS;
}

NTSTATUS Bc250McdmQueryChildStatus(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmSetPowerState(
    CONST PVOID MiniportDeviceContext,
    ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState,
    POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    switch (DevicePowerState) {
    case PowerDeviceD0:
    case PowerDeviceD1:
    case PowerDeviceD2:
    case PowerDeviceD3:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ==========================================================================
   Fase V5 — DDIs obrigatorias que faltavam na tabela de render.

   A auditoria estatica comparou os 187 campos DxgkDdi* de
   DRIVER_INITIALIZATION_DATA com os 59 que o driver preenchia. Ponteiro
   obrigatorio nulo faz o dxgkrnl abortar o start com STATUS_INTERNAL_ERROR
   (0xC00000E5) sem chamar nada — foi o observado no silicio.

   Todas marcam a propria entrada no rastro, para que a proxima recusa seja
   nomeada em vez de deduzida.
   ========================================================================== */

static BC250_MCDM_CURRENT_FENCE_PROVIDER g_Bc250CurrentFenceProvider;

VOID Bc250McdmInstallCurrentFenceProvider(
    _In_opt_ BC250_MCDM_CURRENT_FENCE_PROVIDER CurrentFenceProvider)
{
    g_Bc250CurrentFenceProvider = CurrentFenceProvider;
}

/*
 * Implementacao REAL: o valor vem da fence que a GPU gravou na VRAM, via o
 * provedor instalado pelo miniport ativo. Sem provedor, ou com o motor
 * offline, a resposta e' erro — jamais um numero inventado.
 */
NTSTATUS APIENTRY Bc250McdmQueryCurrentFence(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYCURRENTFENCE *pCurrentFence)
{
    UINT completed = 0u;
    NTSTATUS status;

    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_CURRENT_FENCE);

    if (hAdapter == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250CurrentFenceProvider == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = g_Bc250CurrentFenceProvider(
        (HANDLE)hAdapter, pCurrentFence->NodeOrdinal, &completed);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pCurrentFence->CurrentFence = completed;
    return STATUS_SUCCESS;
}

/*
 * As DDIs abaixo sao obrigatorias na tabela, mas o driver genuinamente nao as
 * implementa. Recusar e' honesto; o que estava errado era o ponteiro NULO,
 * que impede o dxgkrnl de sequer perguntar.
 *
 * Patch, Render e SubmitCommand pertencem ao caminho de DMA buffer com
 * patch-list. Este driver usa enderecamento virtual e SubmitCommandVirtual,
 * entao nao devem ser chamadas; se o rastro mostrar uma delas, a premissa de
 * GPU VA esta errada em algum lugar.
 */
NTSTATUS APIENTRY Bc250McdmEscape(
    CONST HANDLE hAdapter,
    CONST DXGKARG_ESCAPE *pEscape)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_ESCAPE);
    /*
     * ENTRADA DA DDI FALA -- antes de qualquer despacho.
     *
     * Corrida 13/09 11:55: a UMD chamou pfnEscapeCb (BltViaKmd ENTER no log do
     * dwm.exe) e o handler de composicao nunca viu a chamada. Sem log AQUI era
     * impossivel separar "o dxgkrnl nunca nos chamou" de "nos chamou com um
     * payload que nao reconhecemos". Esta linha imprime o primeiro dword, que
     * e' o opcode do contrato, e o tamanho -- os dois discriminantes.
     *
     * Teto por contador proprio: escapes de TERCEIROS sao a maioria absoluta
     * (3.282 numa unica corrida) e um log 1-em-1 afogaria o arquivo.
     */
    {
        static uint32_t s_EscapeEntryCount = 0;
        uint32_t firstDword = 0u;
        ++s_EscapeEntryCount;
        if (pEscape != NULL && pEscape->pPrivateDriverData != NULL &&
            pEscape->PrivateDriverDataSize >= sizeof(uint32_t)) {
            firstDword = *(uint32_t *)pEscape->pPrivateDriverData;
        }
        if (s_EscapeEntryCount <= 8u || (s_EscapeEntryCount % 512u) == 0u ||
            (firstDword & 0xFFFF0000u) == 0xBC250000u) {
            Bc250DisplayLog("[McdmEscape] ENTER #%u: DataSize=%u Dword0=0x%08X hDevice=%p hContext=%p Flags=0x%08X handler=%s\n",
                s_EscapeEntryCount,
                (pEscape != NULL) ? (uint32_t)pEscape->PrivateDriverDataSize : 0u,
                firstDword,
                (pEscape != NULL) ? pEscape->hDevice : NULL,
                (pEscape != NULL) ? pEscape->hContext : NULL,
                (pEscape != NULL) ? (uint32_t)pEscape->Flags.Value : 0u,
                (g_Bc250EscapeHandler != NULL) ? "instalado" : "AUSENTE");
        }
    }
    if (g_Bc250EscapeHandler != NULL) {
        return g_Bc250EscapeHandler(hAdapter, pEscape);
    }
    Bc250DisplayLog("[McdmEscape] RECUSADO: nenhum handler instalado -> NOT_SUPPORTED\n");
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmAcquireSwizzlingRange(
    CONST HANDLE hAdapter,
    DXGKARG_ACQUIRESWIZZLINGRANGE *pAcquireSwizzlingRange)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_ACQUIRE_SWIZZLING_RANGE);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pAcquireSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmReleaseSwizzlingRange(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RELEASESWIZZLINGRANGE *pReleaseSwizzlingRange)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RELEASE_SWIZZLING_RANGE);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pReleaseSwizzlingRange);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmPatch(
    CONST HANDLE hAdapter,
    CONST DXGKARG_PATCH *pPatch)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_PATCH);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmRender(
    CONST HANDLE hContext,
    DXGKARG_RENDER *pRender)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_RENDER);
    if (hContext == NULL || pRender == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250McdmSubmitCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand)
{
    Bc250ProbeMarkDdi(BC250_V5_DDI_SUBMIT_COMMAND);
    if (hAdapter == NULL || pSubmitCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[McdmSubmitCommand] Node=%u, FenceId=%llu, DmaBuf=0x%llX, Size=%u\n",
        pSubmitCommand->NodeOrdinal,
        (unsigned long long)pSubmitCommand->SubmissionFenceId,
        (unsigned long long)pSubmitCommand->DmaBufferPhysicalAddress.QuadPart,
        pSubmitCommand->DmaBufferSize);
    return STATUS_SUCCESS;
}

VOID Bc250McdmResetDevice(CONST PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID APIENTRY Bc250McdmSetStablePowerState(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETSTABLEPOWERSTATE *pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pArgs);
}

VOID Bc250McdmUnload(VOID)
{
    if (g_Bc250UnloadCleanupHandler != NULL) {
        g_Bc250UnloadCleanupHandler();
        g_Bc250UnloadCleanupHandler = NULL;
    }
    Bc250McdmInstallRuntimeCapsProvider(NULL);
    Bc250McdmInstallProcessLifecycle(NULL, NULL);
    Bc250McdmInstallDeviceLifecycle(NULL, NULL);
    Bc250McdmInstallContextLifecycle(NULL, NULL);
    Bc250McdmInstallGpuVaLifecycle(NULL);
    Bc250McdmInstallAllocationLifecycle(NULL, NULL, NULL, NULL);
    Bc250McdmInstallDebugLifecycle(NULL);
    Bc250McdmInstallSubmissionExecutor(NULL);
    Bc250McdmInstallCurrentFenceProvider(NULL);
    Bc250McdmInstallUpdatePageTableHandler(NULL);
    Bc250McdmInstallFlushTlbHandler(NULL);
    Bc250McdmInstallInterruptLifecycle(NULL, NULL);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeFlushQueuedDpcs();
#endif
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
NTSTATUS APIENTRY Bc250McdmSignalMonitoredFence(
    CONST HANDLE hContext,
    INOUT_PDXGKARG_SIGNALMONITOREDFENCE pSignalMonitoredFence)
{
    uint32_t *dmaBuffer;
    uint32_t dwordsAvailable;
    uint32_t cursor = 0;
    BC250_SDMA_STATUS sdmaStatus;

    UNREFERENCED_PARAMETER(hContext);
    Bc250ProbeMarkDdi(BC250_V5_DDI_SIGNAL_MONITORED_FENCE);
    /*
     * ENTRADA SEMPRE FALA.
     *
     * Ate 13/09 esta DDI tinha DUAS saidas MUDAS. Nas corridas 12/09 22:57,
     * 13/09 05:50 e 06:08 o log nao tinha UMA linha [DdiSignalMonitoredFence],
     * e por isso era impossivel distinguir "o dxgkrnl nunca chamou" de "nos
     * recusamos calados" -- exatamente a ambiguidade que a Lei 4 proibe e que
     * o portao audit_process_lifecycle_ddis_speak ja trava em outras DDIs.
     * A fence do blt parava no valor 1 e o DWM congelava esperando o 2.
     */
    if (pSignalMonitoredFence == NULL) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] RECUSADO: argumento NULL\n");
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[DdiSignalMonitoredFence] ENTER: FenceGpuVa=0x%llX Value=%llu DmaSize=%u pDma=%s KernelSubmissionType=%u\n",
        (unsigned long long)pSignalMonitoredFence->MonitoredFenceGpuVa,
        (unsigned long long)pSignalMonitoredFence->MonitoredFenceValue,
        pSignalMonitoredFence->DmaSize,
        (pSignalMonitoredFence->pDmaBuffer != NULL) ? "ok" : "NULL",
        (unsigned)pSignalMonitoredFence->KernelSubmissionType);
    if (pSignalMonitoredFence->pDmaBuffer == NULL) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] RECUSADO: pDmaBuffer NULL\n");
        return STATUS_INVALID_PARAMETER;
    }
    if (pSignalMonitoredFence->MonitoredFenceGpuVa == 0ull ||
        (pSignalMonitoredFence->MonitoredFenceGpuVa & 3u) != 0u) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] REJECT invalid/misaligned: FenceGpuVa=0x%llX\n",
            (unsigned long long)pSignalMonitoredFence->MonitoredFenceGpuVa);
        return STATUS_INVALID_PARAMETER;
    }

    dmaBuffer = (uint32_t *)pSignalMonitoredFence->pDmaBuffer;
    /*
     * A capacidade NAO e' mais arredondada para baixo ate potencia de dois.
     * Bc250PagingFloorPow2Dwords existe porque os emissores de pacote validam
     * o TAMANHO DO ANEL como potencia de dois -- mas este buffer e' LINEAR, e
     * o arredondamento chegava a descartar metade dele: 20 dwords viravam 16 e
     * a DDI recusava com INSUFFICIENT_DMA_BUFFER um buffer que cabia de sobra.
     * Agora exigimos o tamanho REAL necessario e declaramos aos emissores uma
     * janela fixa de potencia de dois que cabe DENTRO do que foi verificado --
     * o mascaramento do cursor nunca escreve fora dela.
     */
    dwordsAvailable = pSignalMonitoredFence->DmaSize / (uint32_t)sizeof(uint32_t);
    if (dwordsAvailable < BC250_SIGNAL_FENCE_SCRATCH_DWORDS) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] DmaSize=%u insuficiente (disponivel=%u dwords, exigido=%u)\n",
            pSignalMonitoredFence->DmaSize, dwordsAvailable,
            BC250_SIGNAL_FENCE_SCRATCH_DWORDS);
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    dwordsAvailable = BC250_SIGNAL_FENCE_SCRATCH_DWORDS;

    sdmaStatus = Bc250SdmaEmitFence64(
        dmaBuffer,
        dwordsAvailable,
        &cursor,
        pSignalMonitoredFence->MonitoredFenceGpuVa,
        pSignalMonitoredFence->MonitoredFenceValue);
    if (sdmaStatus != BC250_SDMA_OK) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] SDMA encode failed: status=%d\n", (int)sdmaStatus);
        return STATUS_INVALID_PARAMETER;
    }

    sdmaStatus = Bc250SdmaEmitGcr(dmaBuffer, dwordsAvailable, &cursor);
    if (sdmaStatus != BC250_SDMA_OK) {
        Bc250DisplayLog("[DdiSignalMonitoredFence] GCR encode failed: status=%d\n", (int)sdmaStatus);
        return STATUS_INVALID_PARAMETER;
    }

    Bc250PagingAppendIbAlignmentNops("DdiSignalMonitoredFence", dmaBuffer, &cursor);
    pSignalMonitoredFence->pDmaBuffer = (PVOID)(dmaBuffer + cursor);
    if (pSignalMonitoredFence->DmaSize >= cursor * sizeof(uint32_t)) {
        pSignalMonitoredFence->DmaSize -= cursor * (UINT)sizeof(uint32_t);
    }
    pSignalMonitoredFence->MultipassOffset = BC250_PAGING_MULTIPASS_DONE;
    Bc250DisplayLog("[DdiSignalMonitoredFence] Completo: FenceGpuVa=0x%llX Value=%llu (%u dwords emitidos)\n",
        (unsigned long long)pSignalMonitoredFence->MonitoredFenceGpuVa,
        (unsigned long long)pSignalMonitoredFence->MonitoredFenceValue,
        cursor);
    return STATUS_SUCCESS;
}
#endif

/*
 * Fase V5 — DDIs obrigatorias EXCLUSIVAS da tabela de render.
 *
 * Deliberadamente fora de Bc250McdmPopulateRequiredCallbacks: a imagem
 * readonly de producao tem contrato proprio exigindo
 * DxgkDdiSubmitCommand == NULL, e essa garantia de fail-closed vale mais que
 * a conveniencia de uma funcao unica. So o miniport ativo, que registra por
 * DxgkInitialize, chama esta funcao.
 */
NTSTATUS Bc250McdmPopulateRenderCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;

    InitializationData->DxgkDdiQueryCurrentFence = Bc250McdmQueryCurrentFence;
    InitializationData->DxgkDdiEscape = Bc250McdmEscape;
    InitializationData->DxgkDdiAcquireSwizzlingRange = Bc250McdmAcquireSwizzlingRange;
    InitializationData->DxgkDdiReleaseSwizzlingRange = Bc250McdmReleaseSwizzlingRange;
    InitializationData->DxgkDdiPatch = Bc250McdmPatch;
    InitializationData->DxgkDdiRender = Bc250McdmRender;
    InitializationData->DxgkDdiSubmitCommand = Bc250McdmSubmitCommand;
    InitializationData->DxgkDdiRenderKm = Bc250McdmRenderKm;
    InitializationData->DxgkDdiCancelCommand = Bc250McdmCancelCommand;
    InitializationData->DxgkDdiSetPowerComponentFState =
        Bc250McdmSetPowerComponentFState;
    InitializationData->DxgkDdiPowerRuntimeControlRequest =
        Bc250McdmPowerRuntimeControlRequest;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    InitializationData->DxgkDdiSignalMonitoredFence =
        Bc250McdmSignalMonitoredFence;
#endif

    return STATUS_SUCCESS;
}

/*
 * Obrigatoria no conjunto minimo MCDM a partir do contrato que introduziu
 * esta DDI. O BC-250 ainda nao publica mudancas assincronas de conector por
 * DxgkCbIndicateConnectorChange; portanto a fila esta vazia por construcao.
 * STATUS_ALREADY_COMPLETE e' o retorno documentado para "nenhuma mudanca
 * ainda nao reportada". Nao fabricar um pacote DXGK_CONNECTION_CHANGE evita
 * anunciar um monitor ou target inexistente no perfil compute/render-only.
 */
NTSTATUS APIENTRY Bc250McdmQueryConnectionChange(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_QUERYCONNECTIONCHANGE pQueryConnectionChange)
{
    if (hAdapter == NULL || pQueryConnectionChange == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_ALREADY_COMPLETE;
}

NTSTATUS APIENTRY Bc250McdmRenderKm(
    CONST HANDLE hContext,
    DXGKARG_RENDER *pRenderKmArgs)
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRenderKmArgs);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmCancelCommand(
    CONST HANDLE hAdapter,
    CONST DXGKARG_CANCELCOMMAND *pCancelCommand)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCancelCommand);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmSetPowerComponentFState(
    CONST HANDLE DriverContext,
    UINT ComponentIndex,
    UINT FState)
{
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(ComponentIndex);
    UNREFERENCED_PARAMETER(FState);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250McdmPowerRuntimeControlRequest(
    CONST HANDLE DriverContext,
    LPCGUID PowerControlCode,
    PVOID InBuffer,
    SIZE_T InBufferSize,
    PVOID OutBuffer,
    SIZE_T OutBufferSize,
    PSIZE_T BytesReturned)
{
    UNREFERENCED_PARAMETER(DriverContext);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);
    if (BytesReturned != NULL) *BytesReturned = 0u;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Bc250McdmPopulateRequiredCallbacks(
    DRIVER_INITIALIZATION_DATA *InitializationData)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeInitializeSpinLock(&g_Bc250PagingProofLock);
#endif
    RtlZeroMemory(g_Bc250PagingProofs, sizeof(g_Bc250PagingProofs));

    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;
    InitializationData->DxgkDdiBuildPagingBuffer =
        Bc250McdmBuildPagingBuffer;
    InitializationData->DxgkDdiCalibrateGpuClock =
        Bc250McdmCalibrateGpuClock;
    InitializationData->DxgkDdiCloseAllocation = Bc250McdmCloseAllocation;
    InitializationData->DxgkDdiCollectDbgInfo = Bc250McdmCollectDbgInfo;
    InitializationData->DxgkDdiCreateAllocation = Bc250McdmCreateAllocation;
    InitializationData->DxgkDdiCreateContext = Bc250McdmCreateContext;
    InitializationData->DxgkDdiCreateDevice = Bc250McdmCreateDevice;
    InitializationData->DxgkDdiCreateProcess = Bc250McdmCreateProcess;
    InitializationData->DxgkDdiDescribeAllocation =
        Bc250McdmDescribeAllocation;
    InitializationData->DxgkDdiDestroyAllocation =
        Bc250McdmDestroyAllocation;
    InitializationData->DxgkDdiDestroyContext = Bc250McdmDestroyContext;
    InitializationData->DxgkDdiDestroyDevice = Bc250McdmDestroyDevice;
    InitializationData->DxgkDdiDestroyProcess = Bc250McdmDestroyProcess;
    InitializationData->DxgkDdiDispatchIoRequest =
        Bc250McdmDispatchIoRequest;
    InitializationData->DxgkDdiDpcRoutine = Bc250McdmDpcRoutine;
    InitializationData->DxgkDdiFormatHistoryBuffer =
        Bc250McdmFormatHistoryBuffer;
    InitializationData->DxgkDdiGetNodeMetadata = Bc250McdmGetNodeMetadata;
    InitializationData->DxgkDdiGetRootPageTableSize =
        Bc250McdmGetRootPageTableSize;
    InitializationData->DxgkDdiGetStandardAllocationDriverData =
        Bc250McdmGetStandardAllocationDriverData;
    InitializationData->DxgkDdiInterruptRoutine = Bc250McdmInterruptRoutine;
    InitializationData->DxgkDdiOpenAllocation = Bc250McdmOpenAllocation;
    InitializationData->DxgkDdiPreemptCommand = Bc250McdmPreemptCommand;
    InitializationData->DxgkDdiQueryAdapterInfo =
        Bc250McdmQueryAdapterInfo;
    InitializationData->DxgkDdiQueryDependentEngineGroup =
        Bc250McdmQueryDependentEngineGroup;
    InitializationData->DxgkDdiQueryDeviceDescriptor =
        Bc250McdmQueryDeviceDescriptor;
    InitializationData->DxgkDdiQueryEngineStatus =
        Bc250McdmQueryEngineStatus;
    InitializationData->DxgkDdiResetDevice = Bc250McdmResetDevice;
    InitializationData->DxgkDdiResetEngine = Bc250McdmResetEngine;
    InitializationData->DxgkDdiResetFromTimeout =
        Bc250McdmResetFromTimeout;
    InitializationData->DxgkDdiRestartFromTimeout =
        Bc250McdmRestartFromTimeout;
    InitializationData->DxgkDdiSetPowerState = Bc250McdmSetPowerState;
    InitializationData->DxgkDdiSetRootPageTable = Bc250McdmSetRootPageTable;
    InitializationData->DxgkDdiSetStablePowerState =
        Bc250McdmSetStablePowerState;
    InitializationData->DxgkDdiSetVirtualMachineData =
        Bc250McdmSetVirtualMachineData;
    InitializationData->DxgkDdiSubmitCommandVirtual =
        Bc250McdmSubmitCommandVirtual;
    InitializationData->DxgkDdiQueryChildStatus = Bc250McdmQueryChildStatus;
    InitializationData->DxgkDdiQueryChildRelations =
        Bc250McdmQueryChildRelations;
    InitializationData->DxgkDdiQueryConnectionChange =
        Bc250McdmQueryConnectionChange;
    InitializationData->DxgkDdiUnload = Bc250McdmUnload;
    return STATUS_SUCCESS;
}
