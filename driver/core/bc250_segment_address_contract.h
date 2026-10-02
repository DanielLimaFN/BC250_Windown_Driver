#ifndef BC250_SEGMENT_ADDRESS_CONTRACT_H
#define BC250_SEGMENT_ADDRESS_CONTRACT_H

/*
 * Mesmo preambulo dos demais contratos de core: em modo kernel nao existe
 * <stdint.h>, e os tipos de largura fixa vem do ntddk.
 */
#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fonte unica da conversao "offset de segmento -> endereco MC".
 *
 * POR QUE ESTE ARQUIVO EXISTE (defeito medido na corrida 12/09 12:29):
 *
 * Bc250GpuvaSpaceResolveAddress devolve um BackingSegmentOffset -- offset
 * DENTRO do segmento 1. O segmento 1 NAO comeca no byte zero da VRAM: ele
 * comeca em LocalSegmentBaseOffset, medido em 0xA8C000 (11.059.200 bytes),
 * logo depois da reserva de scanout. A conversao correta, usada pelo
 * codificador de PTE e pela instalacao da root, e'
 *
 *     MC = VramMcBase + LocalSegmentBaseOffset + SegmentOffset
 *
 * O caminho de present por escape fazia
 *
 *     MC = VramMcStart + SegmentOffset        <-- LocalSegmentBaseOffset AUSENTE
 *
 * e por isso o SDMA lia 11.059.200 bytes ANTES da imagem real. O blt concluia
 * com fence e RPTR==WPTR -- porque a engine executou -- mas copiava a regiao
 * errada: VRAM majoritariamente zerada, com restos nao nulos. Na tela isso deu
 * preto com uma faixa cinza embaixo e alguns pixels coloridos.
 *
 * O comentario de BC250_MCDM_RUNTIME_CAPS.VramMcBase ja declarava que aquele
 * campo era "a unica fonte da base, usada tanto pelo codificador de entradas
 * quanto pela instalacao da root, para que as duas nao possam mais sair de
 * fase". O present foi um TERCEIRO consumidor que saiu de fase. Dai a fonte
 * unica ser uma funcao, e nao uma convencao.
 *
 * Aritmetica pura: sem hardware, sem lock, sem alocacao. Testavel offline.
 */

typedef enum BC250_SEGMENT_ADDRESS_STATUS {
    BC250_SEGMENT_ADDRESS_OK = 0,
    BC250_SEGMENT_ADDRESS_BAD_ARGUMENT,
    BC250_SEGMENT_ADDRESS_NOT_LOCAL_SEGMENT,
    BC250_SEGMENT_ADDRESS_OUT_OF_SEGMENT,
    BC250_SEGMENT_ADDRESS_OVERFLOW
} BC250_SEGMENT_ADDRESS_STATUS;

/* Segmento 1 e' a VRAM local; e' o unico que o SDMA enderecca por MC. */
#define BC250_SEGMENT_ADDRESS_LOCAL_SEGMENT_ID 1u

/*
 * Converte um offset de segmento em endereco MC, recusando por construcao a
 * faixa que nao couber inteira no segmento. Recusar e' o comportamento certo:
 * um present cuja origem nao cabe no segmento estaria lendo memoria de outra
 * coisa, e devolver "sucesso" ali foi exatamente o defeito.
 *
 * SizeBytes = 0 significa "so' o endereco de base", sem verificacao de faixa.
 */
BC250_SEGMENT_ADDRESS_STATUS Bc250SegmentOffsetToMc(
    uint64_t VramMcBase,
    uint64_t LocalSegmentBaseOffset,
    uint64_t LocalSegmentBytes,
    uint32_t SegmentId,
    uint64_t SegmentOffset,
    uint64_t SizeBytes,
    uint64_t *McAddress);

/*
 * Mesma conta, outra VISTA: offset cru dentro da VRAM, contado a partir do byte
 * zero. E' o que o caminho de display precisa, porque ele indexa
 * g_Bc250DisplayState.FrameBufferVirtual -- um mapeamento de CPU que comeca no
 * byte zero da VRAM, e nao no espaco MC.
 *
 * POR QUE ESTA SEGUNDA VISTA EXISTE (defeito medido na corrida 12/09 13:11):
 * DxgkDdiPresent usava o offset de segmento DIRETO como indice do framebuffer,
 * exatamente como o escape fazia antes da C3.4. Com isso amostrou 11.059.200
 * bytes antes da superficie real, leu zeros, e o BYTE_PROOF reprovou um quadro
 * que estava CORRETO. A reacao aquela falsa reprovacao derrubou a maquina com
 * BugCheck 0x119. Dois caminhos, mesmo defeito -- dai as duas vistas saírem da
 * mesma implementacao, com as mesmas guardas de faixa.
 *
 * Bc250SegmentOffsetToMc e' definida como VramMcBase + esta funcao.
 */
BC250_SEGMENT_ADDRESS_STATUS Bc250SegmentOffsetToVramOffset(
    uint64_t LocalSegmentBaseOffset,
    uint64_t LocalSegmentBytes,
    uint32_t SegmentId,
    uint64_t SegmentOffset,
    uint64_t SizeBytes,
    uint64_t *VramOffset);

/*
 * M2 (doc 548) -- SEGMENTO 3: VRAM LOCAL QUE A CPU NAO ENXERGA.
 *
 * MEDIDO na Corrida 193: a placa reserva VramTotalMiB=512 (RCC_CONFIG_MEMSIZE,
 * conferido contra GCMC_VM_FB_LOCATION) e so' CpuVisibleMiB=256 (BAR). O
 * segmento 1 so' cobre a janela visivel; a outra metade era marcada como
 * "opaca" e ficava parada, enquanto o dwm.exe recriava o device 242 vezes por
 * vkAllocateMemory -2. A BIOS permite UMA de ate 8 GB (Daniel, 01/10), entao
 * o tamanho NUNCA e' constante: sai da topologia lida no boot.
 *
 *   [alinhado(CpuVisible), alinhado(Total - TopGuard))
 *
 * menos qualquer regiao interna do driver que caia nessa faixa (os alocadores
 * internos sao criados com a VRAM TOTAL e uma alocacao top-down sem a flag de
 * CPU cairia aqui). Regiao que toca o inicio empurra o inicio; regiao acima
 * dele trunca o fim -- o mesmo criterio do segmento 1 (A16).
 *
 * TopGuard e' HIPOTESE declarada: a tabela de descoberta nao foi localizada
 * (DiscoveryTmrStatus=5) e, no Linux, reservas de firmware (discovery TMR,
 * vram_usagebyfirmware) ficam no TOPO da VRAM. Trocar por valor medido quando
 * houver leitura delas.
 */
#define BC250_HIDDEN_SEGMENT_ID 3u
#define BC250_HIDDEN_SEGMENT_TOP_GUARD_BYTES (16ull * 1024ull * 1024ull)
#define BC250_HIDDEN_SEGMENT_MIN_BYTES (16ull * 1024ull * 1024ull)
#define BC250_HIDDEN_SEGMENT_ALIGNMENT (64ull * 1024ull)

typedef enum BC250_HIDDEN_SEGMENT_STATUS {
    BC250_HIDDEN_SEGMENT_OK = 0,
    BC250_HIDDEN_SEGMENT_BAD_ARGUMENT,
    BC250_HIDDEN_SEGMENT_NO_HIDDEN_VRAM,
    BC250_HIDDEN_SEGMENT_TOO_SMALL
} BC250_HIDDEN_SEGMENT_STATUS;

BC250_HIDDEN_SEGMENT_STATUS Bc250ComputeHiddenSegment(
    uint64_t VramTotalBytes,
    uint64_t CpuVisibleBytes,
    uint64_t TopGuardBytes,
    const uint64_t *BusyOffsets,
    const uint64_t *BusyLengths,
    uint32_t BusyCount,
    uint64_t *BaseOffset,
    uint64_t *Bytes);

const char *Bc250HiddenSegmentStatusName(BC250_HIDDEN_SEGMENT_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SEGMENT_ADDRESS_CONTRACT_H */
