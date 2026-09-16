#ifndef BC250_SUBMISSION_ENGINE_H
#define BC250_SUBMISSION_ENGINE_H

#include <ntddk.h>

#include "bc250_compute_mqd_contract.h"
#include "bc250_persistent_ring_contract.h"
#include "bc250_vram_allocator.h"
#include "bc250_adapter_info_contract.h"

/*
 * Fase P1 — motor de submissao persistente.
 *
 * Tudo o que foi provado no silicio ate o candidato 3.43 e' de tiro unico: o
 * bind programa a fila, submete um lote fixo, confere o tripe e reverte.  O
 * pipeline do Windows exige o contrario — a fila fica viva e cada
 * DxgkDdiSubmitCommandVirtual anexa um Indirect Buffer.  Este modulo e' o dono
 * desse estado persistente: memoria, mapeamento, fila ativa e cursores.
 *
 * Regras que este modulo NAO relaxa:
 *   - Nenhuma fence e' incrementada pela CPU.  A unica escrita de fence e' a
 *     do RELEASE_MEM executado pela GPU.
 *   - Submit so devolve sucesso quando o lote entrou no anel e o cursor foi
 *     publicado.  Falha de emissao vira NTSTATUS de erro, nunca STATUS_SUCCESS.
 *   - Conclusao e' lida da VRAM.  Enquanto a GPU nao gravar, nada e' aposentado.
 */

#define BC250_SUBMISSION_ENGINE_SIGNATURE 0x50315351u /* 'QS1P' */

/* Node 0 = SDMA, Node 1 = Compute (BC250_ADAPTER_NODE_COUNT == 2). */
#define BC250_SUBMISSION_NODE_SDMA    0u
#define BC250_SUBMISSION_NODE_COMPUTE 1u

/* Bloco possuido pelo motor; o anel vive em +0x10000, alinhado a 64 KiB. */
#define BC250_SUBMISSION_BLOCK_BYTES     131072u
#define BC250_SUBMISSION_RING_OFFSET     0x10000u
#define BC250_SUBMISSION_RING_BYTES      65536u
#define BC250_SUBMISSION_RING_DWORDS     (BC250_SUBMISSION_RING_BYTES / 4u)

/*
 * Fase V2 — correlacao entre a fence do anel e a fence do WDDM.
 *
 * A fence do anel (64 bits) e' nossa: ela e' atribuida por
 * Bc250SubmissionEngineSubmit e escrita na VRAM pelo RELEASE_MEM da GPU.  A
 * fence do WDDM (UINT, 32 bits por definicao de DXGKARG_SUBMITCOMMANDVIRTUAL)
 * e' do dxgkrnl e e' o unico identificador que ele reconhece em
 * DxgkCbNotifyInterrupt/DXGK_INTERRUPT_DMA_COMPLETED.
 *
 * Sem esta correlacao o miniport submete e nunca informa a conclusao: o
 * dxgkrnl espera para sempre e o pipeline morre em TDR.  O rastreador e' um
 * FIFO simples porque o anel de computacao aposenta estritamente em ordem.
 *
 * Serializacao (revisto no candidato 3.67).
 *
 * Ate o 3.66 a unica serializacao era DxgkCbSynchronizeExecution.  O ensaio
 * fisico de 2026-09-01 com RenderNode=1 mostrou o custo dessa dependencia
 * unica: no StopDevice a chamada devolveu STATUS_UNSUCCESSFUL (0xC0000001)
 * com queried==FALSE, o ramo fail-closed leu isso como "ocupado" e o
 * dispositivo terminou em STATUS_DEVICE_BUSY — com PendingCount=0 e
 * ReservedCount=0, ou seja, sem nenhuma fence real.  Evidencia:
 * docs/evidence/2026-09-01_11-34-36-DESKTOP-HQAG6T4-rendernode-inspect.
 *
 * A correcao NAO e' ler os contadores direto.  Nao esta provado que a
 * interrupcao ja foi desconectada nesse ponto, e mesmo que estivesse ainda
 * restariam o timer de retirement, o DPC, submissoes concorrentes e o
 * cancelamento de reserva.  O rastreador passa a ter lock proprio, usado em
 * TODOS os caminhos, e o StopDevice so fotografa depois de tornar o
 * rastreador quiescente.
 */
#define BC250_WDDM_FENCE_TRACK_DEPTH 64u

/*
 * Lock proprio do rastreador.
 *
 * Precisa funcionar em tres contextos: PASSIVE_LEVEL (StopDevice),
 * DISPATCH_LEVEL (DPC de retirement) e DIRQL (dentro do callback de
 * DxgkCbSynchronizeExecution).  Um KSPIN_LOCK convencional adquirido em
 * PASSIVE sobe apenas ate DISPATCH_LEVEL e nao bloquearia a ISR no mesmo
 * processador; por isso a aquisicao e' explicita: sobe o IRQL quando esta
 * abaixo de DISPATCH e gira com Interlocked para o caso multiprocessador.
 *
 * O giro e' LIMITADO de proposito.  Sem watchdog no laboratorio, travar o
 * processador em giro infinito custa um boot frio as cegas; estourar o limite
 * devolve falha, e' registrado na telemetria e faz o chamador falhar fechado.
 * Perder o lock nunca e' interpretado como "rastreador vazio".
 */
#define BC250_FENCE_LOCK_SPIN_LIMIT 4000000u

typedef struct BC250_FENCE_LOCK {
    volatile LONG Owned;           /* 0 = livre, 1 = tomado */
    volatile LONG Initialized;
    volatile LONG AcquireFailures; /* estouros do limite de giro */
    volatile LONG Contentions;     /* aquisicoes que precisaram girar */
} BC250_FENCE_LOCK;

typedef struct BC250_WDDM_FENCE_ENTRY {
    uint64_t RingFence;          /* fence de 64 bits atribuida pelo anel */
    uint32_t SubmissionFenceId;  /* UINT do DXGKARG_SUBMITCOMMANDVIRTUAL */
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    uint32_t GpuvaSlot;
    uint32_t GpuvaGeneration;
    uint32_t GpuvaPinValid;
} BC250_WDDM_FENCE_ENTRY;

typedef struct BC250_WDDM_FENCE_RESERVATION {
    uint32_t Token;
    uint32_t SubmissionFenceId;
    uint32_t NodeOrdinal;
    uint32_t EngineOrdinal;
    uint32_t Active;
    uint32_t GpuvaSlot;
    uint32_t GpuvaGeneration;
    uint32_t GpuvaPinValid;
} BC250_WDDM_FENCE_RESERVATION;

typedef struct BC250_WDDM_FENCE_TRACKER {
    BC250_WDDM_FENCE_ENTRY Entries[BC250_WDDM_FENCE_TRACK_DEPTH];
    BC250_WDDM_FENCE_RESERVATION Reservations[BC250_WDDM_FENCE_TRACK_DEPTH];
    uint32_t Head;            /* proximo a aposentar */
    uint32_t Count;           /* em voo */
    uint32_t ReservedCount;   /* reservas ativas antes de commit */
    uint32_t NextReservationToken;
    uint32_t PushedTotal;
    uint32_t NotifiedTotal;
    uint32_t OverflowCount;   /* submissoes perdidas por FIFO cheio */
    uint32_t OutOfOrderCount; /* fence do anel fora de ordem: corrupcao */
    uint64_t LastPushedRingFence;
    uint32_t LastNotifiedFenceId;
    uint32_t LastNotifiedFenceIdByNode[2];

    /* 3.67: serializacao propria e portao de admissao de trabalho novo. */
    BC250_FENCE_LOCK Lock;
    /*
     * 1 = aceita reservas novas. Zerado quando o contexto entra em STOPPING.
     * Reserve/Push recusam com o portao fechado; Cancel, Commit, Drain e
     * Query continuam permitidos para que o trabalho ja aceito termine.
     */
    volatile LONG AcceptingWork;
    uint32_t RejectedWhileStopping;
} BC250_WDDM_FENCE_TRACKER;

/*
 * Auto-teste da Fase P1.
 *
 * Um DxgkDdiSubmitCommandVirtual real exige um cliente de modo usuario, que so
 * chega com o UMD (P4).  Para provar o MOTOR no silicio antes disso, o
 * auto-teste submete UM Indirect Buffer pelo mesmo caminho persistente, e o
 * conteudo desse IB e' exatamente a sequencia de dispatch aprovada no
 * candidato 3.43.
 *
 * O tripe fica mais forte que o do 3.43, nao mais fraco:
 *   - fence do dispatch (0xBC250D15) gravada pela GPU em +0x2300;
 *   - fence de sequencia do anel gravada pela GPU em +0x2200;
 *   - resultado do shader (0xBC250C01) conferido byte a byte em +0x5000;
 *   - RPTR == WPTR no anel persistente.
 * As duas fences vivem em enderecos distintos de proposito: se
 * compartilhassem endereco, uma sobrescreveria a outra e a aposentadoria do
 * anel leria lixo.
 */
#define BC250_SUBMISSION_IB_OFFSET          0x6000u
#define BC250_SUBMISSION_IB_BYTES           1024u
#define BC250_SUBMISSION_IB_DWORDS          (BC250_SUBMISSION_IB_BYTES / 4u)
#define BC250_SUBMISSION_DISPATCH_FENCE_OFFSET 0x2300u
#define BC250_SUBMISSION_SELFTEST_POLLS     20000u
#define BC250_SUBMISSION_REARM_POLLS        20000u
#define BC250_SUBMISSION_ACTIVE_POLLS       1000u

/*
 * Fotografia dos registradores da fila. O candidato 3.44 submeteu com
 * WPTR=12 confirmado no proprio registrador e o RPTR nao saiu de zero: sem
 * estes valores nao da para saber se a fila continuava ATIVA, se o doorbell
 * sobreviveu ou onde o MEC estava. Publicada em tres momentos.
 */
typedef struct BC250_SUBMISSION_QUEUE_SNAPSHOT {
    uint32_t Active;
    uint32_t HqStatus0;
    uint32_t PqControl;
    uint32_t DoorbellControl;
    uint32_t CpPqStatus;
    uint32_t CpcStatus;
    uint32_t CpMecCntl;
    uint32_t CpMeCntl;
    uint32_t RlcCpSchedulers;
    uint32_t WptrPollCntl;
    uint32_t WptrPollCntl1;
    uint32_t HqdError;
    uint32_t PqWptrLo;
    uint32_t PqRptr;
    uint32_t MecMe2HeaderDump;
} BC250_SUBMISSION_QUEUE_SNAPSHOT;

typedef struct BC250_SUBMISSION_SELFTEST_EVIDENCE {
    uint32_t Attempted;
    uint32_t PlanStatus;
    uint32_t SubmitStatus;
    uint32_t IbDwords;
    uint32_t IbGpuAddressLow;
    uint32_t AssignedFence;
    uint32_t RingWptrAfter;
    uint32_t RptrMmioAfter;
    uint32_t RptrReportAfter;
    uint32_t DispatchFenceBefore;
    uint32_t DispatchFenceAfter;
    uint32_t ResultBefore;
    uint32_t ResultAfter;
    uint32_t ResultMismatches;
    uint32_t RingFenceObserved;
    uint32_t RetiredCount;
    uint32_t Polls;
    uint32_t ShaderStagedPassed;
    uint32_t IbReadbackPassed;
    uint32_t TriadPassed;
    uint32_t PhysicalReady;

    /*
     * Re-arme controlado. O 3.49 mostrou que CNTL1 e' consumido/auto-limpo
     * depois da ativacao vazia. O 3.50 reproduz a sequencia one-shot completa:
     * ACTIVE=0, QUEUE_MASK novamente armado com readback e ACTIVE=1, agora com
     * o shadow contendo trabalho. Se o RPTR andar, o gatilho de load era a
     * diferenca que faltava; se nao, a hipotese fica eliminada.
     * Nada disto escreve fence: o veredito continua vindo da GPU.
     */
    uint32_t RearmAttempted;
    uint32_t RearmActiveBefore;
    uint32_t RearmDeactivatePolls;
    uint32_t RearmActiveAfterOff;
    uint32_t RearmQueueMaskApplied;
    uint32_t RearmQueueMaskReadback;
    uint32_t RearmQueueMaskPassed;
    uint32_t RearmActiveAfterOn;
    uint32_t RearmPolls;
    uint32_t RearmRptrMmio;
    uint32_t RearmRptrReport;
    uint32_t RearmDispatchFence;
    uint32_t RearmResult;
    uint32_t RearmRingFence;
    uint32_t RearmCompleted;

    /* Candidato 3.53 — observabilidade aprofundada da fila persistente. */
    uint32_t RingDwordsReal[12];
    uint32_t RingDwordsExpected[12];
    uint32_t RingDwordsMismatchCount;
    uint32_t HqdPqBaseLo;
    uint32_t HqdPqBaseHi;
    uint32_t HqdRptrReportAddrLo;
    uint32_t HqdRptrReportAddrHi;
    uint32_t HqdWptrPollAddrLo;
    uint32_t HqdWptrPollAddrHi;
    uint32_t HqdVmid;
    uint32_t HqdMqdControl;
    uint32_t HqdIbControl;
    uint32_t PostCpcStatus;
    uint32_t PostCpcBusyStat;
    uint32_t PostCpcStalledStat1;
    uint32_t PostCpfStatus;
    uint32_t PostCpfBusyStat;
    uint32_t PostCpfStalledStat1;
    uint32_t PostCpStat;
    uint32_t PostCpStalledStat1;
    uint32_t PostMecPc;
} BC250_SUBMISSION_SELFTEST_EVIDENCE;

#define BC250_P2_STREAM_BATCH_COUNT 16u

typedef struct BC250_SUBMISSION_P2_STREAM_EVIDENCE {
    uint32_t Attempted;
    uint32_t BatchCount;
    uint32_t SubmissionsAccepted;
    uint32_t SubmissionsRetired;
    uint32_t WrapCount;
    uint32_t ResultMismatches;
    uint32_t LastFenceObserved;
    uint32_t FinalWptr;
    uint32_t FinalRptrMmio;
    uint32_t FinalRptrReport;
    uint32_t TriadPassed;
    uint32_t PhysicalReady;
    uint32_t ResultsBefore[BC250_P2_STREAM_BATCH_COUNT];
    uint32_t ResultsAfter[BC250_P2_STREAM_BATCH_COUNT];
    uint32_t DispatchFencesBefore[BC250_P2_STREAM_BATCH_COUNT];
    uint32_t DispatchFencesAfter[BC250_P2_STREAM_BATCH_COUNT];
} BC250_SUBMISSION_P2_STREAM_EVIDENCE;

typedef enum BC250_SUBMISSION_ENGINE_STATE {
    BC250_SUBMISSION_ENGINE_OFFLINE = 0,
    BC250_SUBMISSION_ENGINE_ONLINE = 1,
    BC250_SUBMISSION_ENGINE_FAULTED = 2
} BC250_SUBMISSION_ENGINE_STATE;

typedef struct BC250_SUBMISSION_ENGINE {
    uint32_t Signature;
    volatile LONG State;
    KSPIN_LOCK Lock;

    /* Recursos possuidos enquanto o motor esta online. */
    BC250_VRAM_TOKEN Token;
    uint32_t TokenValid;
    volatile uint32_t *Mapped;
    uint32_t MappedBytes;
    uint64_t BasePhysical;
    volatile void *DoorbellVirtual;
    uint32_t DoorbellOffsetBytes;
    uint32_t DoorbellEnabled;
    uint32_t MmioMapped;
    uint64_t DiagnosticSubmitTime;
    uint64_t DiagnosticPendingFence;
    uint32_t DiagnosticDescriptor[4];
    uint32_t DiagnosticPendingDescriptor[4];
    uint32_t DiagnosticQueueRaw[6];
    uint32_t DiagnosticValidReads;
    uint32_t DiagnosticObservedFence;
    uint32_t DiagnosticRptr;
    uint32_t DiagnosticWriteback;

    /* Estado da fila de computacao mantida ativa. */
    BC250_HQD_MC_BUFFERS Buffers;
    BC250_COMPUTE_MQD_PROPERTIES Props;
    BC250_GFX10_COMPUTE_MQD Mqd;
    BC250_HQD_PERSISTENT_STATE HqdState;
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE ActivateEvidence;
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE TeardownEvidence;
    uint32_t SelectedSelector;
    uint32_t OriginalSelector;

    /* Anel persistente do no de computacao. */
    BC250_PERSISTENT_RING ComputeRing;
    volatile uint32_t *ComputeRingVirtual;
    volatile uint32_t *ComputeFenceVirtual;
    volatile uint32_t *ComputeRptrReportVirtual;
    volatile uint32_t *ComputeWptrShadowVirtual;

    /* Telemetria P1 — toda observada, nenhuma inferida. */
    uint32_t ActivateStatus;
    uint32_t TeardownStatus;
    uint32_t SubmitCalls;
    uint32_t SubmitAccepted;
    uint32_t SubmitRejectedNode;
    uint32_t SubmitRejectedPlan;
    uint32_t SubmitRejectedEmit;
    uint32_t RingFullCount;
    uint32_t FenceRegressionCount;
    uint32_t DoorbellKicks;
    uint32_t DoorbellWriteFailures;
    uint32_t DoorbellLastValueLow;
    uint32_t DoorbellLastValueHigh;
    uint32_t DoorbellControlBefore;
    uint32_t DoorbellControlAfter;
    uint32_t DoorbellHitObserved;
    uint32_t DirectWptrWrites;
    uint32_t PollCalls;
    uint32_t RetiredTotal;
    uint32_t LastPlanPadDwords;
    uint32_t LastWptrPublished;
    uint32_t LastWptrShadowReadback;
    uint32_t LastWptrMmioReadback;
    uint32_t LastRptrMmio;
    uint32_t LastRptrReportMem;
    uint32_t LastFenceObserved;
    uint32_t LastSubmitStatus;
    uint64_t LastSubmittedFence;
    uint64_t LastRetiredFence;

    /*
     * 3.116: dominio logico do anel versus cursores fisicos da HQD.
     * RebasePassed so sobe quando a fila foi medida drenada e nao havia
     * fence em voo. Nenhum caminho desta transacao escreve o RPTR.
     */
    uint32_t CursorReconcileAttempts;
    uint32_t CursorReconcilePassed;
    uint32_t CursorReconcileRejected;
    uint32_t CursorLogicalBefore;
    uint32_t CursorPhysicalRptrRaw;
    uint32_t CursorPhysicalWptrRaw;
    uint32_t CursorReconciled;
    uint32_t CursorReconcileStatus;

    /* 3.117: troca de IB_VMID somente com a HQD drenada e inativa. */
    uint32_t VmidRetargetAttempts;
    uint32_t VmidRetargetPassed;
    uint32_t VmidRetargetRejected;
    uint32_t VmidRetargetBefore;
    uint32_t VmidRetargetAfter;
    uint32_t VmidRetargetActiveBefore;
    uint32_t VmidRetargetActiveAfter;
    uint32_t VmidRetargetRptr;
    uint32_t VmidRetargetWptr;
    uint32_t VmidRetargetQueueMask;

    /*
     * Candidato 3.46 — coerencia por submissao antes do kick.
     * Estes campos descrevem somente a ultima tentativa.  Passed so e'
     * publicado depois de HDP, GL2 e invalidate/ACK do VMID0 fecharem.
     */
    uint32_t CoherHdpApplied;
    uint32_t CoherL2Readback;
    uint32_t CoherInvalidateAck;
    uint32_t CoherInvalidatePolls;
    uint32_t CoherencyPassed;

    /* Baseline da fence do anel, escrito na ativacao e conferido por readback. */
    uint32_t RingFenceBaselineWritten;
    uint32_t RingFenceBaselineReadback;

    BC250_SUBMISSION_QUEUE_SNAPSHOT SnapshotAfterActivate;
    BC250_SUBMISSION_QUEUE_SNAPSHOT SnapshotAfterSubmit;
    BC250_SUBMISSION_QUEUE_SNAPSHOT SnapshotAfterRearm;

    BC250_SUBMISSION_SELFTEST_EVIDENCE SelfTest;
    BC250_SUBMISSION_P2_STREAM_EVIDENCE P2Stream;

    /* Fase V2 — correlacao fence do anel <-> fence do WDDM. */
    BC250_WDDM_FENCE_TRACKER WddmFences;

    /* Diagnostico detalhado de falha no PublishWptr (visivel em PASSIVE_LEVEL) */
    uint32_t PublishFailStep;
    uint32_t PublishHqdActive;
    uint32_t PublishHqdRptr;
    uint32_t PublishHqdError;
    uint32_t PublishReadbackDoorbellControl;
    uint32_t PublishTargetWptr;
} BC250_SUBMISSION_ENGINE;

struct BC250_DXGK_ACTIVE_CONTEXT;

/*
 * Sobe o motor: aloca o bloco, mapeia, publica as PTEs do Contexto 0, ativa a
 * fila e inicializa o anel persistente vazio.  Em qualquer falha desfaz tudo o
 * que ja tinha feito e devolve o adaptador ao estado de entrada.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineOnline(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context);

/*
 * Anexa um Indirect Buffer ao anel do no indicado e publica o cursor.
 * Devolve a fence de 64 bits atribuida a submissao.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineSubmit(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t NodeOrdinal,
    _In_ uint64_t IbGpuVirtualAddress,
    _In_ uint32_t IbSizeDwords,
    _Out_ uint64_t *AssignedFence);

/*
 * Retargeta o IB_VMID da HQD persistente. Alterar CP_HQD_VMID com ACTIVE=1
 * nao faz parte do load upstream; por isso a transacao exige fila drenada,
 * desativa, programa/readback, rearma QUEUE_MASK e so entao reativa.
 * RPTR nunca e' escrito.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineRetargetDrainedIbVmid(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t TargetVmid);

/* Fotografia global somente leitura para localizar a primeira etapa do stall. */
VOID Bc250SubmissionEngineCaptureRciuStage(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_z_ const char *Tag);

/*
 * Submete comandos PM4 diretamente no anel do no indicado e publica o cursor.
 * Devolve a fence de 64 bits atribuida a submissao.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineSubmitDirect(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _In_ uint32_t NodeOrdinal,
    _In_ const uint32_t *Dwords,
    _In_ uint32_t DwordCount,
    _Out_ uint64_t *AssignedFence);

/*
 * Le a fence e o RPTR gravados pela GPU e aposenta o que estiver concluido.
 * Nao escreve fence nenhuma: se a GPU nao gravou, nada avanca.
 */
_Check_return_
NTSTATUS Bc250SubmissionEnginePoll(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context,
    _Out_opt_ uint32_t *RetiredNow);

/*
 * ==========================================================================
 * Fase V2 — rastreador de fences WDDM
 *
 * Operacoes puras sobre o FIFO, sem toque em hardware.  Desde o 3.67 cada uma
 * adquire o lock proprio do rastreador; continuam seguras em DIRQL por so
 * fazerem aritmetica sobre memoria nao paginada.  DxgkCbSynchronizeExecution
 * segue sendo usado nos caminhos quentes, mas deixou de ser a UNICA garantia:
 * o StopDevice consegue fotografar sem depender dele.
 * ==========================================================================
 */

/* Zera o FIFO preservando o lock.  Chamado na ativacao do motor. */
void Bc250WddmFenceTrackerReset(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker);

/*
 * Fecha ou reabre a admissao de trabalho novo.
 *
 * Com Accepting==FALSE nenhuma reserva nova e' aceita — e' o passo 1 da
 * quiescencia do StopDevice.  Idempotente.
 */
void Bc250WddmFenceTrackerSetAcceptingWork(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ BOOLEAN Accepting);

/*
 * Fotografa Count e ReservedCount sob o lock proprio.
 *
 * Devolve FALSE somente se o lock nao pode ser adquirido dentro do limite de
 * giro.  FALSE NAO significa "vazio": o chamador deve falhar fechado.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerSnapshotCounts(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _Out_ uint32_t *PendingCount,
    _Out_ uint32_t *ReservedCount);

/*
 * Reserva capacidade no rastreador antes da submissao ao motor.
 * Retorna FALSE se nao houver espaco no FIFO.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerReserve(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t SubmissionFenceId,
    _In_ uint32_t NodeOrdinal,
    _In_ uint32_t EngineOrdinal,
    _Out_ uint32_t *ReservationToken);

/*
 * Cancela uma reserva pendente caso a submissao ao motor falhe.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerCancel(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t ReservationToken);

_Check_return_
BOOLEAN Bc250WddmFenceTrackerAttachGpuvaPin(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t ReservationToken,
    _In_ uint32_t GpuvaSlot,
    _In_ uint32_t GpuvaGeneration);

/*
 * Confirma a reserva apos publicacao no anel, associando a fence fisica.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerCommit(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t ReservationToken,
    _In_ uint64_t RingFence);

/*
 * Registra uma submissao aceita (atalho reserve + commit). RingFence deve ser
 * estritamente crescente.
 */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerPush(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RingFence,
    _In_ uint32_t SubmissionFenceId,
    _In_ uint32_t NodeOrdinal,
    _In_ uint32_t EngineOrdinal);

/*
 * Retira do FIFO todas as entradas cuja RingFence ja foi aposentada pela GPU
 * (RingFence <= RetiredRingFence) e copia-as para Out.  Devolve quantas foram
 * copiadas.  Nao escreve fence nenhuma: RetiredRingFence vem da leitura da
 * VRAM feita por Bc250SubmissionEnginePoll.
 */
_Check_return_
uint32_t Bc250WddmFenceTrackerDrain(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RetiredRingFence,
    _Out_writes_to_(Max, return) BC250_WDDM_FENCE_ENTRY *Out,
    _In_ uint32_t Max);

_Check_return_
uint32_t Bc250WddmFenceTrackerDrainEx(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint64_t RetiredMecFence,
    _In_ uint64_t RetiredGfxFence,
    _Out_writes_to_(Max, return) BC250_WDDM_FENCE_ENTRY *Out,
    _In_ uint32_t Max);

/* Consulta somente o namespace WDDM já notificado, nunca a fence do ring. */
_Check_return_
BOOLEAN Bc250WddmFenceTrackerQueryCurrent(
    _Inout_ BC250_WDDM_FENCE_TRACKER *Tracker,
    _In_ uint32_t NodeOrdinal,
    _Out_ uint32_t *SubmissionFenceId);

/* Desativa a fila, executa o rollback LIFO e libera todos os recursos. */
NTSTATUS Bc250SubmissionEngineOffline(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context);

/* PASSIVE_LEVEL, after draining retirement DPCs and before releasing MMIO.
 * Captures current state even when polling faulted or never ran. */
VOID Bc250SubmissionEngineArchiveDiagnostics(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context);

/*
 * Submete UM Indirect Buffer com a sequencia de dispatch provada e aguarda a
 * GPU gravar as duas fences e o resultado.  Nada aqui e' escrito pela CPU
 * depois da submissao: se a GPU nao executar, o veredito reprova.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineSelfTest(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context);

/*
 * Fase P2 — Submete N=16 Indirect Buffers sequenciais em stream concorrente,
 * exercita o avanco monotico de fences e wraparound ciclico do anel no silicio.
 */
_Check_return_
NTSTATUS Bc250SubmissionEngineP2StreamTest(
    _Inout_ struct BC250_DXGK_ACTIVE_CONTEXT *Context);

/* Publica a telemetria P1/P2 no registro de probe. */
VOID Bc250SubmissionEngineRecordTelemetry(
    _In_ const struct BC250_DXGK_ACTIVE_CONTEXT *Context);

#endif
