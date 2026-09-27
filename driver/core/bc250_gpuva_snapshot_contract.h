#ifndef BC250_GPUVA_SNAPSHOT_CONTRACT_H
#define BC250_GPUVA_SNAPSHOT_CONTRACT_H

#include "bc250_gpuva_space_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPUVA_SNAPSHOT_SIGNATURE 0x4E535647u /* "GVSN" */
#define BC250_GPUVA_SNAPSHOT_WRITER_LOCK (-2147483647L - 1L)

typedef struct BC250_GPUVA_SNAPSHOT_READER {
    const BC250_GPUVA_SPACE *Space;
    uint32_t Slot;
    uint32_t PublicationGeneration;
    uint32_t Acquired;
} BC250_GPUVA_SNAPSHOT_READER;

/*
 * C3.8 (12/09): quantas vezes o PUBLICADOR tenta tomar o slot inativo.
 *
 * Espelha as 16 tentativas que Bc250GpuvaSnapshotAcquire ja fazia do lado do
 * LEITOR. A assimetria -- leitor tentando 16 vezes e publicador UMA -- e' o
 * defeito medido: com o dwm.exe vivo, cada Present resolve uma GPUVA e segura
 * um slot, o publicador batia em BUSY na primeira tentativa, o journal devolvia
 * PUBLISH_FAILED, o ActiveFlushTlb virava STATUS_IO_DEVICE_ERROR e a admissao
 * do Node 1 NUNCA REABRIA -- terminando em
 * STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE na corrida 16:09.
 */
#define BC250_GPUVA_SNAPSHOT_PUBLISH_ATTEMPTS 16u

typedef struct BC250_GPUVA_SNAPSHOT_STORE {
    uint32_t Signature;
    volatile long ActiveSlot;
    volatile long ReaderState[2];
    volatile long PublicationGeneration;
    volatile long SlotGeneration[2];
    /*
     * C3.8: diagnostico da ULTIMA publicacao. Existe porque o log do KMD
     * colapsava BUSY (contencao transitoria com o leitor) e erro real de
     * substituicao de faixa no MESMO numero -- o journal so' reporta
     * PUBLISH_FAILED. Sem separar os dois nao ha' como dizer se retentar
     * resolve. Escritor unico: a propria publicacao, serializada pelo chamador.
     */
    volatile long LastPublishAttempts;
    volatile long LastPublishBusyCount;
    volatile long LastPublishStatus;
    volatile long LastPublishRangeCount;
    volatile long LastPublishFailedRangeIndex;
    volatile long LastPublishFailedMapRange;
    uint64_t LastPublishFailedBaseGpuVa;
    uint64_t LastPublishFailedSize;
    /*
     * COMPOSICAO DO LOTE -- maps e unmaps contados ANTES de qualquer mutacao.
     *
     * POR QUE (Corrida 44, 20/09): seis PUBLICACAO_FALHA sairam com
     * FailedIndex EXATAMENTE igual a Reusable do slot ativo, seis de seis
     * (655/369, 640/384, 603/421, 460/564, 430/594, 415/609, sempre somando
     * Cap=1024). Isso fecha o MECANISMO -- Bc250GpuvaCopySnapshot copia o slot
     * ativo para o inativo e o lote e' aplicado POR CIMA, logo a capacidade
     * livre e' Cap menos Mapped e um lote maior que isso morre no indice
     * exato --, e REFUTA o diagnostico anterior de exaustao do teto de 256
     * slots: Teardown=0 e reciclagem esta sa.
     *
     * O que NAO se sabe e' a composicao do lote. Se a maioria das 832 mudancas
     * for UNMAP, aplicar unmaps antes de maps derrubaria o pico de ocupacao sem
     * mudar o estado final (o journal garante intervalos nao sobrepostos, logo
     * operacoes disjuntas comutam). Se for maioria MAP, reordenar nao resolve
     * nada e o problema e' de capacidade de verdade.
     *
     * Propor a reordenacao sem esse numero seria fix de efeito imprevisivel.
     * Estes dois contadores existem para responder a pergunta na proxima
     * corrida, e por isso esta faixa NAO altera comportamento nenhum.
     */
    volatile long LastPublishMapCount;
    volatile long LastPublishUnmapCount;
    BC250_GPUVA_SPACE Slots[2];
} BC250_GPUVA_SNAPSHOT_STORE;

BC250_GPUVA_STATUS Bc250GpuvaSnapshotStoreInitialize(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    uint32_t AddressBits);

/* Pode ser chamado por leitores no caminho de submissao em DISPATCH_LEVEL. */
BC250_GPUVA_STATUS Bc250GpuvaSnapshotAcquire(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader);

void Bc250GpuvaSnapshotRelease(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SNAPSHOT_READER *Reader);

/*
 * Escritor PASSIVE_LEVEL. Publica somente depois de construir integralmente o
 * slot inativo. Scratch deve ser memoria persistente/nonpaged fornecida pelo
 * chamador; a funcao nunca aloca nem espera por leitores.
 */
BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishReplace(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange,
    uint32_t *PublishedGeneration);

BC250_GPUVA_STATUS Bc250GpuvaSnapshotPublishBatch(
    BC250_GPUVA_SNAPSHOT_STORE *Store,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Ranges,
    const uint32_t *MapRanges,
    uint32_t RangeCount,
    uint32_t *PublishedGeneration);

#ifdef __cplusplus
}
#endif

#endif
