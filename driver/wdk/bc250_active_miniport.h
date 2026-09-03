#ifndef BC250_ACTIVE_MINIPORT_H
#define BC250_ACTIVE_MINIPORT_H

#include <ntddk.h>
#include <dispmprt.h>

#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_readonly_miniport.h"
#include "bc250_display_ddi.h"
#include "bc250_post_display_reservation.h"
#include "bc250_vram_probe_transaction.h"
#include "bc250_gcvm_backing_transaction.h"
#include "../core/bc250_mcdm_object_contract.h"
#include "../core/bc250_runtime_inventory.h"
#include "../core/bc250_gfxhub_tlb_transaction.h"
#include "../core/bc250_gcvm_vmid8_transaction.h"
#include "../core/bc250_psp_ring_transaction.h"
#include "../core/bc250_psp_load_fw_transaction.h"
#include "../core/bc250_sdma_f32_transaction.h"
#include "../core/bc250_vram_allocator.h"
#include "../core/bc250_mcdm_object_contract.h"
#include "../core/bc250_gpuva_space_contract.h"
#include "../core/bc250_gpuva_journal_contract.h"
#include "../core/bc250_gpuva_sdma_broker_contract.h"
#include "bc250_submission_engine.h"
#include "bc250_boot_fuse.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_ACTIVE_SIGNATURE 0x4D434431u /* "MCD1" */

#define BC250_GPUVA_GATE_BLOCK_JOURNAL_PENDING    0x00000001u
#define BC250_GPUVA_GATE_BLOCK_HIERARCHY_PENDING  0x00000002u
#define BC250_GPUVA_GATE_BLOCK_ADAPTER_INACTIVE   0x00000004u
#define BC250_GPUVA_GATE_BLOCK_CONTEXT_ABI        0x00000008u
#define BC250_GPUVA_GATE_BLOCK_ALLOCATION_ABI     0x00000010u
#define BC250_GPUVA_GATE_BLOCK_ROOT_ABI           0x00000020u
#define BC250_GPUVA_GATE_BLOCK_ENGINE_OFFLINE     0x00000040u
#define BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SLOT      0x00000080u
#define BC250_GPUVA_GATE_BLOCK_SNAPSHOT_SIGNATURE 0x00000100u
#define BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EMPTY     0x00000200u
#define BC250_GPUVA_GATE_BLOCK_SNAPSHOT_EPOCH     0x00000400u
#define BC250_GPUVA_GATE_BLOCK_NO_LEAF_COMMIT     0x00000800u
#define BC250_ACTIVE_POOL_TAG  'ADCB'

typedef enum BC250_SDMA_RING_WORK_STATUS {
    BC250_SDMA_RING_WORK_OK = 0,
    BC250_SDMA_RING_WORK_BAD_ARGUMENT,
    BC250_SDMA_RING_WORK_ALLOCATION_FAILED,
    BC250_SDMA_RING_WORK_REGION_FAILED,
    BC250_SDMA_RING_WORK_APERTURE_INVALID,
    BC250_SDMA_RING_WORK_MAP_FAILED,
    BC250_SDMA_RING_WORK_INHERITED_ACTIVE,
    BC250_SDMA_RING_WORK_PACKET_FAILED,
    BC250_SDMA_RING_WORK_MMIO_FAILED,
    BC250_SDMA_RING_WORK_READBACK_FAILED,
    BC250_SDMA_RING_WORK_TIMEOUT,
    BC250_SDMA_RING_WORK_FENCE_FAILED,
    BC250_SDMA_RING_WORK_DATA_FAILED,
    BC250_SDMA_RING_WORK_RESTORE_FAILED,
    BC250_SDMA_RING_WORK_UNMAP_FAILED,
    BC250_SDMA_RING_WORK_RELEASE_FAILED,
    /* 16 — o anel nao foi consumido E o motor ja' chegou com
     * EX_IDLE_POLL_TIMER_EXPIRE aceso. Em 249 ciclos de 29/08, as 5 falhas do
     * SDMA tiveram o bit e nenhuma ocorreu sem ele. Nome proprio para nao
     * confundir "o motor ja' estava bloqueado antes de comecarmos" com "o anel
     * nao foi consumido", que sao diagnosticos diferentes. */
    BC250_SDMA_RING_WORK_EX_IDLE_BLOCKED
} BC250_SDMA_RING_WORK_STATUS;

typedef struct BC250_SDMA_RING_WORK_EVIDENCE {
    uint32_t FixedReservationStatus;
    uint32_t AllocationMode;
    uint64_t RingMcAddress;
    uint64_t WorkMcAddress;
    uint64_t RingOffset;
    uint64_t WorkOffset;
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t WptrPublished;
    uint32_t WptrRegisterAfterKick;
    uint32_t WptrShadowAfterKick;
    uint32_t RbCntlApplied;
    uint32_t RingHeaderReadback;
    uint32_t RptrRegisterAfter;
    uint32_t WptrPollCntlApplied;
    uint32_t FencePoison;
    uint32_t FenceExpected;
    uint32_t FenceObserved;
    uint32_t Polls;
    uint32_t ComparedDwords;
    uint32_t MismatchCount;
    uint32_t StatusBefore;
    uint32_t StatusKick;
    uint32_t StatusAfter;
    uint32_t PcKick;
    uint32_t Status1After;
    uint32_t Status2After;
    uint32_t Status3After;
    uint32_t CntlBefore;
    uint32_t CntlApplied;
    uint32_t ExIdlePollExpiredBefore;
    uint32_t StatusAtEntry;
    uint32_t ExIdleAtEntry;
    uint32_t WakeApplied;
    uint32_t StatusAfterWake;
    uint32_t ExIdleAfterWake;
    uint32_t ExIdleClearPolls;
    uint32_t ClkCtrlAtEntry;
    uint32_t EarlyClkUngateApplied;
    uint32_t ErrorLogBefore;
    uint32_t Utcl1ReadXnack0Before;
    uint32_t Utcl1WriteXnack0Before;
    uint32_t Utcl1TimeoutBefore;
    uint32_t ErrorLogAfter;
    uint32_t Utcl1ReadStatusAfter;
    uint32_t Utcl1WriteStatusAfter;
    uint32_t Utcl1ReadXnack0After;
    uint32_t Utcl1ReadXnack1After;
    uint32_t Utcl1WriteXnack0After;
    uint32_t Utcl1WriteXnack1After;
    uint32_t Utcl1TimeoutAfter;
    uint32_t RegisterRestorePassed;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
} BC250_SDMA_RING_WORK_EVIDENCE;

typedef struct BC250_RLC_LOAD_EVIDENCE {
    uint32_t CntlBefore;
    uint32_t GpmStatBefore;
    uint32_t CntlAfter;
    uint32_t GpmStatAfter;
    uint32_t ExecRomBefore;
    uint32_t ExecRomAfter;
    uint32_t EnableF32After;
    uint32_t CntlRestorePassed;
    uint32_t AlreadyActive;
    uint32_t PhysicalReady;
} BC250_RLC_LOAD_EVIDENCE;

#define BC250_PFP_PC_TRACE_SAMPLES 16u

/* Engine de invalidacao de GCVM usada pelo carregador do CP.
 *
 * Ajustavel em runtime pelo valor de registro
 * HKLM\SYSTEM\CurrentControlSet\Servicesc250mcdm\Parameters\GcvmInvalidateEngine,
 * para que trocar de engine custe um valor de registro e um ciclo de PnP em vez
 * de build + assinatura + instalacao.
 *
 * ATENCAO: o valor 0 e' o Display Engine (VMID 0). Selecionar 0 e' uma decisao
 * deliberada de bisseccao e derruba o scanout - so' com o watchdog armado. */
#define BC250_GCVM_INVALIDATE_ENGINE_DEFAULT 17u
#define BC250_GCVM_INVALIDATE_ENGINE_MAX     17u

typedef enum BC250_PFP_DIRECT_LOAD_STATUS {
    BC250_PFP_DIRECT_LOAD_OK = 0,
    BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT,
    BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED,
    BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED,
    BC250_PFP_DIRECT_LOAD_REGION_FAILED,
    BC250_PFP_DIRECT_LOAD_APERTURE_INVALID,
    BC250_PFP_DIRECT_LOAD_MAP_FAILED,
    BC250_PFP_DIRECT_LOAD_MMIO_FAILED,
    BC250_PFP_DIRECT_LOAD_INHERITED_ACTIVE,
    BC250_PFP_DIRECT_LOAD_STAGING_FAILED,
    BC250_PFP_DIRECT_LOAD_PC_STATIC,
    BC250_PFP_DIRECT_LOAD_RESTORE_FAILED,
    BC250_PFP_DIRECT_LOAD_UNMAP_FAILED,
    BC250_PFP_DIRECT_LOAD_RELEASE_FAILED,
    BC250_PFP_DIRECT_LOAD_PM4_NOT_CONSUMED
} BC250_PFP_DIRECT_LOAD_STATUS;

typedef struct BC250_PFP_DIRECT_LOAD_EVIDENCE {
    uint64_t FirmwareMcAddress;
    uint64_t FirmwareOffset;
    uint32_t FirmwareBytes;
    uint32_t JumpTableDwords;
    uint32_t UcodeVersion;
    uint32_t StagedDwords;
    uint32_t StagingMismatches;
    uint32_t CpMeCntlBefore;
    uint32_t CpMeCntlApplied;
    uint32_t CpMeCntlAfter;
    uint32_t RlcCntlBefore;
    uint32_t RlcCntlApplied;
    uint32_t PfpPcBefore;
    uint32_t PfpPcFirst;
    uint32_t PfpPcLast;
    uint32_t PfpPcTransitions;
    uint32_t PfpPcSamples;
    uint32_t MePcBefore;
    uint32_t MePcLast;
    uint32_t CePcBefore;
    uint32_t CePcLast;
    uint32_t FaultStatusBefore;
    uint32_t FaultStatusAfter;
    uint32_t FaultAddressLow;
    uint32_t FaultAddressHigh;
    uint32_t WriteOnlyPortsProgrammed;
    uint32_t Rb0ActiveBefore;
    uint32_t Rb0ActiveApplied;
    uint32_t Rb0ActiveAfter;
    uint32_t Rb0RptrBefore;
    uint32_t Rb0RptrAfter;
    uint32_t Rb0WptrApplied;
    uint32_t Rb0FenceBefore;
    uint32_t Rb0FenceAfter;
    uint32_t Rb0ComparedDwords;
    uint32_t Rb0MismatchCount;
    uint32_t Rb0DestPolls;
    uint32_t Rb0DestObserved;
    uint32_t RegisterRestorePassed;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
    /* Mascaras PFP/ME/CE/CPC (bits 0..3) das operacoes de I-Cache que o
     * hardware confirmou. 0x0F = os quatro engines confirmaram. */
    uint32_t IcacheInvalidateEntryMask;
    uint32_t IcacheInvalidateUnhaltMask;
    uint32_t IcachePrimeUnhaltMask;
    /* JT_LOADED de CP_*_JT_STAT: 2 bits em PFP/CE, 8 bits no MEC. */
    uint32_t JtStatPfp;
    uint32_t JtStatCe;
    uint32_t JtStatMec;
    /* PC do PFP logo apos o UNHALT, antes da ativacao do anel. */
    uint32_t PfpPcTrace[BC250_PFP_PC_TRACE_SAMPLES];
    /* Invalidacao de GCVM: qual engine, e se ela realmente respondeu. */
    uint32_t InvalidateEngine;
    /* ACK lido ANTES de escrever o REQ. Se ja' vier 1, um ACK observado depois
     * pode ser residual: "0 polls" nao distingue resposta imediata de bit
     * preso. Sem este campo a medicao do ACK nao conclui nada. */
    uint32_t InvalidateUseSemaphore;
    uint32_t InvalidateAckBeforeStaging;
    uint32_t InvalidateAckRawStaging;
    uint32_t InvalidateAckStaging;
    uint32_t InvalidatePollsStaging;
    uint32_t InvalidateSemAcquiredStaging;
    uint32_t InvalidateAckBeforePreWptr;
    uint32_t InvalidateAckRawPreWptr;
    uint32_t InvalidateAckPreWptr;
    uint32_t InvalidatePollsPreWptr;
    uint32_t InvalidateSemAcquiredPreWptr;
    /* Blocos restaurados de 4df3484, o estado das 109 capturas de sucesso. */
    uint32_t CpSoftResetApplied;
    uint32_t Vmid0PageTableApplied;
    uint32_t Vmid0InvalidateAck;
    uint32_t Vmid0AckBefore;
    uint32_t Vmid0AckAfter;
    uint32_t Vmid0Polls;
    uint32_t Vmid0SemAcquired;
} BC250_PFP_DIRECT_LOAD_EVIDENCE;

typedef struct BC250_DXGK_ACTIVE_CONTEXT {
    uint32_t Signature;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BC250_DXGK_READONLY_CONTEXT Core;
    BC250_DISPLAY_STATE Display;
    BC250_POST_DISPLAY_STATUS PostDisplayStatus;
    NTSTATUS PostDisplayCallbackStatus;
    BC250_INHERITED_VRAM_RESERVATION PostDisplayReservation;
    BC250_POST_DISPLAY_EVIDENCE PostDisplayEvidence;
    BC250_RUNTIME_INVENTORY_STATUS RuntimeInventoryStatus;
    BC250_RUNTIME_INVENTORY_INPUT RuntimeInventoryInput;
    BC250_RUNTIME_INVENTORY_REPORT RuntimeInventoryReport;
    BC250_DXGK_READONLY_INPUT SealedRuntime;
    NTSTATUS RuntimeStageStatus;
    uint32_t SecondPreflightStage;
    BC250_VRAM_PROBE_STATUS VramProbeStatus;
    BC250_VRAM_PROBE_EVIDENCE VramProbeEvidence;
    BC250_TLB_TRANSACTION_STATUS TlbTransactionStatus;
    BC250_TLB_TRANSACTION_EVIDENCE TlbTransactionEvidence;
    BC250_GCVM_BACKING_STATUS GcvmBackingStatus;
    BC250_GCVM_BACKING_EVIDENCE GcvmBackingEvidence;
    BC250_GCVM_VMID8_STATUS GcvmVmid8Status;
    BC250_GCVM_VMID8_EVIDENCE GcvmVmid8Evidence;
    BC250_PSP_RING_STATUS PspRingStatus;
    BC250_PSP_RING_EVIDENCE PspRingEvidence;
    PVOID DeferredPspRingVirtual;
    BC250_PSP_LOAD_FW_STATUS PspLoadFwStatus;
    BC250_PSP_LOAD_FW_EVIDENCE PspLoadFwEvidence;
    BC250_PSP_LOAD_FW_STATUS PspRlcLoadStatus;
    BC250_PSP_LOAD_FW_EVIDENCE PspRlcLoadEvidence;
    BC250_RLC_LOAD_EVIDENCE RlcLoadEvidence;
    uint32_t GcvmInvalidateEngine;
    uint32_t GcvmInvalidateSemaphore;
    uint32_t CpSoftReset;
    uint32_t CpVmid0PageTable;
    uint32_t SdmaAutoCtxSw;
    uint32_t SdmaWakeOnExIdle;
    uint32_t SdmaEarlyClkUngate;
    uint32_t ShaderDispatch;
    uint32_t TunableEnginePresent;
    uint32_t TunableSemaphorePresent;
    uint32_t TunableSoftResetPresent;
    uint32_t TunableVmid0Present;
    uint32_t TunableShaderDispatchPresent;
    BC250_PFP_DIRECT_LOAD_STATUS PfpDirectLoadStatus;
    BC250_PFP_DIRECT_LOAD_EVIDENCE PfpDirectLoadEvidence;
    /* Candidato 3.47: leitura pura do scheduler nos limites do PFP. */
    uint32_t SchedulerBeforePfp;
    uint32_t SchedulerBeforePfpReadPassed;
    uint32_t SchedulerAfterPfp;
    uint32_t SchedulerAfterPfpReadPassed;
    BC250_HQD_PREFLIGHT_STATUS HqdPreflightStatus;
    BC250_HQD_READONLY_SNAPSHOT HqdPreflightSnapshot;
    uint32_t HqdPreflightReadCount;
    BC250_HQD_SELECTOR_STATUS MecHqdSelectorStatus;
    BC250_HQD_SELECTOR_EVIDENCE MecHqdSelectorEvidence;
    BC250_HQD_BANK_PROBE_STATUS MecHqdBankProbeStatus;
    BC250_HQD_BANK_PROBE_EVIDENCE MecHqdBankProbeEvidence;
    BC250_HQD_PROGRAM_STATUS HqdProgramStatus;
    BC250_HQD_PROGRAM_EVIDENCE HqdProgramEvidence;
    BC250_HQD_WPTR_DIAGNOSTIC_STATUS HqdWptrDiagStatus;
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE HqdWptrDiagEvidence;
    BC250_HQD_ACTIVE_STATUS HqdActiveStatus;
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE HqdActiveEvidence;
    BC250_HQD_SHADER_DISPATCH_EVIDENCE HqdShaderDispatchEvidence;
    BC250_SDMA_F32_STATUS SdmaF32Status;
    BC250_SDMA_F32_EVIDENCE SdmaF32Evidence;
    BC250_SDMA_RING_WORK_STATUS SdmaRingWorkStatus;
    BC250_SDMA_RING_WORK_EVIDENCE SdmaRingWorkEvidence;
    PVOID DeferredPspLoadRing;
    PVOID DeferredPspLoadCommand;
    PVOID DeferredPspLoadFence;
    PVOID DeferredPspLoadFirmware;
    EX_PUSH_LOCK ObjectLock;
    BC250_MCDM_OBJECT_REGISTRY Objects;
    /* In-memory authority only; it never performs MMIO or submission. */
    BC250_GPUVA_SDMA_BROKER GpuvaSdmaBroker;
    volatile LONG GpuvaSdmaBrokerReady;
    BC250_GPUVA_SPACE GpuvaSpace;
    volatile LONG GpuvaSpaceReady;
    BC250_GPUVA_SNAPSHOT_STORE GpuvaSnapshots;
    BC250_GPUVA_SPACE GpuvaSnapshotScratch;
    BC250_GPUVA_JOURNAL GpuvaJournal;
    BC250_GPUVA_JOURNAL_PTE GpuvaJournalStaging[BC250_GPUVA_JOURNAL_MAX_CHANGES];
    volatile LONG GpuvaJournalReady;
    uint32_t GpuvaHierarchyPending;
    BC250_MCDM_HANDLE GpuvaHierarchyProcess;
    uint64_t GpuvaHierarchyRootPageTable;
    uint32_t GpuvaHierarchyHighestLevel;
    /* Critérios offline satisfeitos; não equivale a autorização de produção. */
    volatile LONG GpuvaActivationEligible;
    uint32_t GpuvaLabPromotion;
    uint32_t GpuvaLabPromotionPresent;
    /*
     * Declara ao dxgkrnl o modelo de memoria GpuMmu (GpuVaReady/PagingReady
     * nos runtime caps). Separado de GpuvaLabPromotion, que governa se a
     * SUBMISSAO e' liberada. Default 0: sem ele o adaptador continua
     * declarando Mem(VA=0, GpuMmu=0), que e' o estado medido em 03/09.
     */
    uint32_t GpuMmuDeclare;
    uint32_t GpuMmuDeclarePresent;
    uint32_t GpuvaActivationBlockMask;
    uint32_t GpuvaActivationSnapshotGeneration;
    uint32_t GpuvaActivationSnapshotEpoch;
    uint32_t GpuvaActivationSnapshotLiveCount;
    /* Só vira 1 quando map/unmap reais do VidMm alimentarem GpuvaSpace. */
    volatile LONG GpuvaMappingsOperational;
    DXGK_CONTEXTINFO ContextAbi;
    volatile LONG ContextAbiReady;
    volatile LONG AllocationAbiReady;
    volatile LONG RootPageTableAbiReady;
    uint64_t AllocationSegmentBytes;
    uint32_t AllocationSegmentSet;
    uint32_t RootPageTableMaxEntries;
    volatile LONG IsActive;
    uint32_t SubmissionsCount;
    uint32_t ResetsCount;
    /* Fase P1: fila de computacao viva entre submissoes do Dxgkrnl. */
    uint32_t PersistentSubmission;
    uint32_t TunablePersistentSubmissionPresent;
    BC250_SUBMISSION_ENGINE SubmissionEngine;

    /*
     * Fase V2 — retirada de fence para o dxgkrnl.
     *
     * A interrupcao de conclusao do MEC nao esta provada no silicio (o anel de
     * IH nunca foi validado neste projeto), portanto a conclusao e' descoberta
     * por varredura periodica da fence que a GPU gravou na VRAM.  Isso e'
     * honesto — a fonte da verdade continua sendo memoria escrita pela GPU —
     * mas nao e' interrupt-driven, e a latencia e' a do intervalo do timer.
     * Trocar por interrupcao real e' item posterior deste roadmap.
     */
    KTIMER RetirementTimer;
    KDPC RetirementDpc;
    volatile LONG RetirementTimerInitialized;
    volatile LONG RetirementTimerRunning;
    uint32_t RetirementDpcRuns;
    uint32_t RetirementPollFailures;
    uint32_t RetirementNotifyBatches;
    uint32_t RetirementNotifiedTotal;
    uint32_t RetirementPushFailures;
    uint32_t RetirementBackpressure;
    uint32_t InterruptsSeen;

    /*
     * Ciclo de vida explicito (candidato 3.67).
     *
     * IsActive sozinho nao descreve o intervalo em que o adaptador ja esta
     * parando mas ainda tem trabalho em voo.  O ensaio 3.66 consultou o
     * rastreador de fences com o timer de retirement ainda armado em 1 ms e
     * com submissoes ainda admissiveis; a resposta so nao ficou corrompida
     * porque nao havia nenhuma fence.  LifecycleState fecha essa janela antes
     * de qualquer fotografia.
     */
    volatile LONG LifecycleState;
    /* Submissoes dentro de SubmitCommandVirtual, entre o portao e a saida. */
    volatile LONG SubmitInFlight;
    /* Diagnostico da quiescencia, publicado no teardown. */
    uint32_t StopReason;
    uint32_t StopSubmitInFlightAfterDrain;
    uint32_t StopDrainIterations;
    uint32_t StopSnapshotFailures;
} BC250_DXGK_ACTIVE_CONTEXT;

/*
 * Estados do ciclo de vida.
 *
 * NOT_STARTED — contexto existe (AddDevice) mas StartDevice ainda nao
 *               devolveu sucesso.  Precisa valer ZERO: um contexto recem
 *               alocado e' zerado, e o estado natural de memoria zerada tem
 *               de ser o que RECUSA trabalho.  Numerar RUNNING como 0 faria
 *               um contexto nunca iniciado aceitar submissao.
 * RUNNING     — admite submissao e reserva novas.
 * STOPPING    — nenhum trabalho novo entra; o que ja entrou termina ou e'
 *               cancelado.  Obrigatorio antes de fotografar o rastreador.
 * STOPPED     — teardown concluido.
 */
#define BC250_ACTIVE_LIFECYCLE_NOT_STARTED 0u
#define BC250_ACTIVE_LIFECYCLE_RUNNING     1u
#define BC250_ACTIVE_LIFECYCLE_STOPPING    2u
#define BC250_ACTIVE_LIFECYCLE_STOPPED     3u

/* Motivos de entrada em STOPPING; publicados em V5StopReason. */
#define BC250_ACTIVE_STOP_REASON_NONE           0u
#define BC250_ACTIVE_STOP_REASON_STOP_DEVICE    1u
#define BC250_ACTIVE_STOP_REASON_REMOVE_DEVICE  2u

/*
 * Fase V2 — varredura de retirada de fence.  Start e' idempotente e chamado na
 * primeira submissao aceita; Stop e' obrigatorio antes de liberar o contexto.
 */
void Bc250ActiveRetirementTimerStart(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context);

void Bc250ActiveRetirementTimerStop(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context);

/* Portao testavel usado por SubmitCommandVirtual e pela corrida Stop/Submit. */
BOOLEAN Bc250ActiveSubmissionGateEnter(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context);

void Bc250ActiveSubmissionGateLeave(
    _Inout_ BC250_DXGK_ACTIVE_CONTEXT *Context);

/*
 * ==========================================================================
 * Perfil de adaptador anunciado ao dxgkrnl.
 * ==========================================================================
 *
 * O perfil e' resolvido UMA VEZ em DriverEntry, a partir dos tunables
 * RenderNode e RenderOnlyLab, e governa tres decisoes que precisam ser
 * coerentes entre si:
 *
 *   1. qual rotina de registro e' usada (DxgkInitialize x
 *      DxgkInitializeDisplayOnlyDriver);
 *   2. quais callbacks a DRIVER_INITIALIZATION_DATA anuncia;
 *   3. quantas VidPn sources e quantos children o StartDevice devolve.
 *
 * A incoerencia entre (2) e (3) e' exatamente o defeito estatico encontrado
 * no 3.74: o perfil display+render anunciava uma source e um child fisicos
 * enquanto o unico caminho de apresentacao registrado — DxgkDdiPresent —
 * devolvia STATUS_NOT_SUPPORTED incondicionalmente.  Amarrar as tres
 * decisoes a um unico enum impede que a proxima corrida repita isso.
 *
 * RENDER_ONLY_LAB e' um perfil de DIAGNOSTICO (Gate 1.5), nao a arquitetura
 * final.  Ele existe para responder, sem tocar em DCN/GOP, se a tabela
 * render/base sobrevive em Codigo 0 quando nenhuma superficie de display e'
 * anunciada.  O destino do projeto continua sendo DISPLAY_RENDER.
 */
typedef enum BC250_ADAPTER_PROFILE {
    /* RenderNode=0 — KMDOD.  Display-only, PresentDisplayOnly implementado. */
    BC250_ADAPTER_PROFILE_DISPLAY_ONLY = 0u,
    /* RenderNode=1, RenderOnlyLab=0 — miniport WDDM completo (destino). */
    BC250_ADAPTER_PROFILE_DISPLAY_RENDER = 1u,
    /* RenderNode=1, RenderOnlyLab=1 — Gate 1.5.  Zero sources/children. */
    BC250_ADAPTER_PROFILE_RENDER_ONLY_LAB = 2u
} BC250_ADAPTER_PROFILE;

/*
 * Fixa o perfil do carregamento corrente.  Fail-closed: qualquer valor fora
 * do enum e' recusado e o perfil anterior permanece intacto.
 */
_Check_return_
NTSTATUS Bc250ActiveSetAdapterProfile(_In_ BC250_ADAPTER_PROFILE Profile);

_Check_return_
BC250_ADAPTER_PROFILE Bc250ActiveGetAdapterProfile(void);

/*
 * Traduz o par (RenderNode, RenderOnlyLab) — ja' acompanhado do respectivo
 * bit de presenca no registro — para um perfil.
 *
 * Recusa, com STATUS_DEVICE_CONFIGURATION_ERROR:
 *   - RenderNode fora de {0,1} ou RenderOnlyLab fora de {0,1};
 *   - RenderOnlyLab=1 com RenderNode!=1, porque o caminho KMDOD e' display
 *     por definicao e nao possui um perfil render-only coerente;
 *   - tunable pedido mas ilegivel (Present=0 com valor nao-default), que e'
 *     indistinguivel de configuracao corrompida.
 */
_Check_return_
NTSTATUS Bc250ActiveResolveAdapterProfile(
    _In_ ULONG RenderNode,
    _In_ ULONG RenderNodePresent,
    _In_ ULONG RenderOnlyLab,
    _In_ ULONG RenderOnlyLabPresent,
    _Out_ BC250_ADAPTER_PROFILE *Profile);

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData);

NTSTATUS Bc250BuildActiveInitializationDataForContract(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion);

/*
 * Constroi a tabela para um perfil explicito.
 *
 * DISPLAY_ONLY e DISPLAY_RENDER produzem a mesma DRIVER_INITIALIZATION_DATA:
 * o recorte KMDOD acontece depois, ao copiar para KMDDOD_INITIALIZATION_DATA.
 * RENDER_ONLY_LAB omite toda a superficie de display.
 */
NTSTATUS Bc250BuildActiveInitializationDataForProfile(
    DRIVER_INITIALIZATION_DATA *InitializationData,
    ULONG InterfaceVersion,
    DXGK_WDDMVERSION WddmVersion,
    BC250_ADAPTER_PROFILE Profile);

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren);

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceV5Telemetry(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren);

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle);

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle);

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle);

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle);

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo);

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle);

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries);

NTSTATUS Bc250ActiveIssueGpuvaSdmaLease(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId);

NTSTATUS Bc250ActiveValidateGpuvaSdmaLeasePair(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount);

BC250_SDMA_RING_WORK_STATUS Bc250ActiveRunSdmaRingWork(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SDMA_RING_WORK_EVIDENCE *Evidence);

BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence);

BC250_HQD_PROGRAM_STATUS Bc250ActiveRunInactiveHqdProgramTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
