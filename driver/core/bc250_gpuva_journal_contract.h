#ifndef BC250_GPUVA_JOURNAL_CONTRACT_H
#define BC250_GPUVA_JOURNAL_CONTRACT_H

#include "bc250_gpuva_snapshot_contract.h"

#define BC250_GPUVA_JOURNAL_SIGNATURE 0x4A415647u /* "GVAJ" */
/*
 * C3.15 elevou o envelope depois de medir 461 runs. A corrida 18:45 refutou
 * a premissa de que bastava dimensionar pelo maior UpdatePT: houve 239
 * NO_SPACE com 1024, enquanto o snapshot final atingiu somente 876 ranges.
 *
 * C3.16 mantem o mesmo envelope do snapshot, mas o journal agora faz overlay:
 * updates mais novos substituem intervalos antigos antes do commit. Assim a
 * capacidade mede o ESTADO FINAL ainda publicavel, nao o historico bruto de
 * chamadas do VidMm. Se o estado final exceder 1024, a transacao inteira e'
 * abortada; nenhum prefixo pode ficar pronto para publicacao.
 *
 * DX.85: de volta a 1024 junto com BC250_GPUVA_SPACE_MAX_RANGES. DX.94 subiu
 * so' o espaco para 4096; este teto limita o lote do commit e fica. Ver o bloco
 * DX.85 em bc250_gpuva_space_contract.h.
 */
#define BC250_GPUVA_JOURNAL_MAX_CHANGES 1024u

typedef enum BC250_GPUVA_JOURNAL_STATUS {
    BC250_GPUVA_JOURNAL_OK = 0,
    BC250_GPUVA_JOURNAL_BAD_ARGUMENT,
    BC250_GPUVA_JOURNAL_BAD_STATE,
    BC250_GPUVA_JOURNAL_BUSY,
    BC250_GPUVA_JOURNAL_NO_SPACE,
    BC250_GPUVA_JOURNAL_MISMATCH,
    BC250_GPUVA_JOURNAL_RANGE_NOT_FLUSHED,
    BC250_GPUVA_JOURNAL_PUBLISH_FAILED
} BC250_GPUVA_JOURNAL_STATUS;

typedef struct BC250_GPUVA_JOURNAL_PTE {
    uint32_t Valid;
    uint32_t Permissions;
    uint32_t SegmentId;
    uint64_t SegmentOffset;
    BC250_MCDM_HANDLE Allocation;
    BC250_MCDM_HANDLE OpenAllocation;
    uint64_t AllocationToken;
} BC250_GPUVA_JOURNAL_PTE;

typedef struct BC250_GPUVA_JOURNAL {
    uint32_t Signature;
    uint32_t Pending;
    BC250_MCDM_HANDLE Process;
    uint64_t ExpectedRootPageTable;
    uint64_t MinimumGpuVa;
    uint64_t MaximumGpuVa;
    uint32_t ChangeCount;
    BC250_GPUVA_RANGE_RECORD Changes[BC250_GPUVA_JOURNAL_MAX_CHANGES];
    uint32_t MapChanges[BC250_GPUVA_JOURNAL_MAX_CHANGES];
} BC250_GPUVA_JOURNAL;

/*
 * C3.9 (12/09) -- VISTA POR PROCESSO DO JOURNAL.
 *
 * Bc250GpuvaJournalSetAnyPending responde "algum slot do adaptador esta
 * pendente?". O portao do Node 1 usava essa resposta global, e por isso
 * fechava sempre que QUALQUER processo estivesse paginando.
 *
 * Medido na corrida 12/09 13:29: com o dwm.exe vivo por 3 min 13 s (o que so'
 * acontece com D3D11LabPromotion armado), o journal nunca drena, o gate ficou
 * em BlockMask=0x1 (JOURNAL) e a submissao de compute do RADV foi RECUSADA --
 * o shader nao escreveu e o byte proof leu poison 0x43DAFFFE. Ou seja: compute
 * e desktop nao coexistiam por causa da GRANULARIDADE do gate, nao por causa
 * do hardware.
 *
 * Cada slot ja carrega o Process dono. A vista por processo apenas usa um dado
 * que sempre esteve la'.
 */

/* Pendencia restrita ao processo indicado. Process==NULL responde 0. */
uint32_t Bc250GpuvaJournalSetAnyPendingForProcess(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    BC250_MCDM_HANDLE Process);

/*
 * Telemetria: mascara de bits dos slots pendentes (bit i = slot i) e contagem.
 * Existe para que a proxima corrida diga QUEM segura o journal, em vez de so'
 * dizer que alguem segura. Nao decide nada.
 */
uint32_t Bc250GpuvaJournalSetDescribePending(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    uint32_t *PendingSlotMask,
    uint32_t *PendingCount);

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalInitialize(
    BC250_GPUVA_JOURNAL *Journal);

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalStageLeafUpdate(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_MCDM_HANDLE Process,
    uint64_t ExpectedRootPageTable,
    uint64_t FirstGpuVa,
    const BC250_GPUVA_JOURNAL_PTE *Entries,
    uint32_t EntryCount);

BC250_GPUVA_JOURNAL_STATUS Bc250GpuvaJournalCommitFlush(
    BC250_GPUVA_JOURNAL *Journal,
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    BC250_MCDM_HANDLE Process,
    uint64_t RootPageTable,
    uint64_t FlushStartGpuVa,
    uint64_t FlushEndGpuVa,
    uint32_t *PublishedGeneration);

void Bc250GpuvaJournalAbort(BC250_GPUVA_JOURNAL *Journal);

/*
 * CONJUNTO DE JOURNALS POR PROCESSO
 *
 * Um BC250_GPUVA_JOURNAL descreve UMA transacao de UM processo. Com um journal
 * unico no adaptador, dois processos paginando ao mesmo tempo se atropelam: cada
 * troca de produtor aborta a transacao pendente do outro.
 *
 * PROVA (corrida 04/09 18:25, docs/evidence/2026-09-04_18-25-10-.../bc250_display_log.txt):
 *   1335: [ActiveUpdatePT] Lvl=0, FirstPteVA=0xAC0000, hProc=...980   (V=1, Seg=1, Addr=0xAD0)
 *   1345: [ActiveUpdatePT] Journal process switch (old=...980, new=...740)
 *                          -> Aborting orphaned journal
 *   1450: [ActiveFlushTlb] StartVA=0xAC0000, EndVA=0x1550000, hProc=...980
 *   1453: [ActiveFlushTlb] Journal commit GLOBAL ... Generation=5 -> SUCCESS
 * O commit da geracao 5 publica o que foi estagiado DEPOIS da linha 1399; o range
 * 0xAC0000 morreu no abort da 1345 e nunca foi reencenado (so 4 ocorrencias de
 * FirstPteVA=0xAC0000 em 2560 linhas, nenhuma apos 1399). Resultado:
 *   [ResolveGpuVa] SpaceResolveAddress failed: status=9 (NOT_FOUND) VA=0xAC0000
 * e o gate fail-closed do Present recusa todos os blts (Submitted=0).
 *
 * Bc250GpuvaJournalSetSelect da a cada processo o seu proprio journal, de modo
 * que a intercalacao deixa de destruir transacoes.
 */
#define BC250_GPUVA_JOURNAL_EVICT_NONE     0u
#define BC250_GPUVA_JOURNAL_EVICT_ROOTLESS 1u
#define BC250_GPUVA_JOURNAL_EVICT_SMALLEST 2u

BC250_GPUVA_JOURNAL *Bc250GpuvaJournalSetSelect(
    BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount,
    BC250_MCDM_HANDLE Process,
    uint64_t ExpectedRootPageTable,
    uint32_t AllowClaim,
    uint32_t *EvictionKind);

uint32_t Bc250GpuvaJournalSetAnyPending(
    const BC250_GPUVA_JOURNAL *Journals,
    uint32_t JournalCount);

#endif
