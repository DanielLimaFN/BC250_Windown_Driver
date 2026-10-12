#ifndef BC250_GPUVA_SPACE_CONTRACT_H
#define BC250_GPUVA_SPACE_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "bc250_mcdm_object_contract.h"
#include "bc250_gcvm_contract.h"
#include "bc250_gpuva_geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GPUVA_SPACE_SIGNATURE 0x53415647u /* "GVAS" */
/*
 * C3.14 (12/09) -- CAPACIDADE DO SNAPSHOT.
 *
 * A evidencia integral da corrida 17:45 refuta o diagnostico inicial de que
 * havia apenas um estouro temporario no Scratch. Scratch comeca vazio e recebe
 * exatamente o estado final MAPPED; portanto, NO_SPACE ali significa que o
 * estado final nao cabe no teto antigo. O log mediu ate 252 faixas ativas, e
 * uma publicacao pode acrescentar ate 256 runs do journal. O teto 1024 cobre
 * esse envelope observado com margem para os dois fragmentos de fronteira de
 * cada replace. Custo estatico: ~110 KiB por BC250_GPUVA_SPACE; existem dois
 * slots publicados mais um Scratch no contexto ativo (~324 KiB ao todo).
 *
 * Isto nao e' prova no silicio. O teste de snapshot reproduz 252 + 256 runs e
 * impede que o teto volte a 256 silenciosamente.
 *
 * DX.85 -- DE VOLTA A 1024 (a DX.81 tinha subido para 4096).
 * O custo do commit de FLUSH_TLB escala com este teto, e nao so com o uso:
 * cada Bc250GpuvaSpaceReplaceRangeTransactional zera um Scratch do tamanho do
 * espaco inteiro, reinsere cada faixa viva com InsertRange, que varre TODOS os
 * slots, e copia o espaco inteiro byte a byte. Custo ~ M * (N * teto + tamanho
 * do espaco), tudo com ObjectLock na thread de paginacao do VidMm (P1 por
 * leitura). As Corridas 127, 128 e 129 terminam com a thread de paginacao DENTRO
 * do despacho de FLUSH_TLB; 128 (214 s) e 129 (90 s) sao as duas primeiras com
 * 4096. Que o teto maior tenha causado ou antecipado o travamento e' HIPOTESE:
 * a Corrida 115 ja travou aos ~2m19s com o mesmo ultimo estagio. 1024 e' o
 * estado medido das Corridas 118..127, inclusive os 600 s da 127.
 *
 * DX.94 (28/09, Corrida 152) -- DE VOLTA A 4096, SO' O ESPACO.
 * A condicao da DX.85 ("medir e cortar o custo do commit antes de reabrir a
 * capacidade") foi cumprida pela FPS.1: reinsercao confiavel sem varrer os
 * slots e copia em bloco. Benchmark offline com o codigo atual: 18,8 us por
 * substituicao com teto 1024; 85,5 us (N=1000) e 122,8 us (N=4000) com 4096 --
 * ~14x abaixo dos 1.770 us que rodaram 600 s estaveis na Corrida 150 e ~57x
 * abaixo do que as 128/129 pagavam com 4096. O custo agora e' dominado pelo
 * tamanho do espaco (zerar e copiar), nao por N. Medido na 152: o espaco
 * fica em Mapped=1024 Livres=0 com 26.195 PUBLICACAO_FALHA; cada falha
 * aborta o journal com as PTEs de hardware ja escritas, e a vista de software
 * deixa de conter mapeamentos que a GPU tem. BC250_GPUVA_JOURNAL_MAX_CHANGES
 * fica em 1024 de proposito: ele limita o lote, e 1024 x ~123 us ~ 126 ms e'
 * o pior caso do commit; com 4096 nos dois seriam ~500 ms. Custo estatico:
 * ~440 KiB por espaco, quatro no contexto ativo. NAO VALIDADO EM HARDWARE.
 */
#define BC250_GPUVA_SPACE_MAX_RANGES 4096u
#define BC250_GPUVA_PAGE_BYTES 4096ull
#define BC250_GPUVA_PAGE_MASK (BC250_GPUVA_PAGE_BYTES - 1ull)
/*
 * TETO ARQUITETURAL, NAO O LIMITE TRADUZIVEL.
 *
 * BC250_GPUVA_ARCH_MAX_BITS e' o teto arquitetural de 48 bits aceito para
 * testes sinteticos parametrizados. O padrao real do silicio e'
 * BC250_GPUVA_PT_VA_BITS (34 bits, definido abaixo).
 *
 * NAO use nenhum dos dois como limite de traducao: o contexto VMID8 traduz
 * 2^34, e um VA acima disso nao tem PTE alcancavel. Para isso existe
 * BC250_GPUVA_TRANSLATABLE_LIMIT, abaixo.
 */
#define BC250_GPUVA_ARCH_MAX_BITS 48u
#define BC250_GPUVA_MAX_LIMIT 0x0001000000000000ull /* 2^48 */

/*
 * =========================================================================
 * Geometria da page table declarada ao dxgkrnl (modelo GpuMmu do WDDM)
 *
 * PROVENIENCIA (Lei 7): docs/03_CATALOGO_DE_REGISTRADORES.md, secao
 * "Derivacao Matematica da Geometria (34 bits, 2 niveis)". Classificacao P1,
 * derivada de gfxhub_v2_0_setup_vmid_config() com as mascaras de
 * gc_10_1_0_sh_mask.h.
 *
 * A hierarquia em uso e' de DOIS niveis, e o GCVM e' programado de acordo:
 *     PAGE_TABLE_DEPTH      = Levels - 1 = 1
 *     PAGE_TABLE_BLOCK_SIZE = block_size - 9 = 0
 *     GCVM_CONTEXT8_CNTL    = 0x00555403
 *     PAGE_TABLE_END_ADDR   = 2^22 - 1 = 0x003FFFFF
 * Os quatro estao travados por driver/tests/gcvm_context8_cntl_test.c e
 * declarados em BC250_GCVM_CONTEXT8_CNTL_TARGET_VALUE.
 *
 * ------------------------------------------------------------------------
 * HISTORICO -- o texto abaixo descrevia o desenho ANTERIOR e ficou FALSO.
 * Nada foi apagado porque a proveniencia do que mudou tambem e' proveniencia,
 * mas nada dele vale hoje:
 *
 *   "PAGE_TABLE_DEPTH -> valor 0. Profundidade 0 no GCVM significa page table
 *    PLANA, de um unico nivel. A hierarquia multi-nivel do amdgpu NAO esta
 *    implementada neste driver e nao pode ser declarada.
 *    BC250_GPUVA_PT_INDEX_BITS = 22 cobre 16 GiB de VA com uma tabela de
 *    32 MiB por processo. Implementar PAGE_TABLE_DEPTH > 0 e' trabalho novo,
 *    nao troca de constante."
 *
 * O que aconteceu: WDDM 2.0+ exige DXGK_MIN_PAGE_TABLE_LEVEL_COUNT = 2, o
 * trabalho novo FOI feito, e BC250_GPUVA_PT_INDEX_BITS passou a ser os 9 bits
 * da FOLHA -- nao os 22 do modelo plano. Ler o paragrafo historico como norma
 * levaria alguem a programar depth 0 e a declarar 1 nivel ao dxgkrnl, que e'
 * abaixo do minimo do contrato.
 * ------------------------------------------------------------------------
 *
 * WDDM 2.0+ (d3dukmdt.h) exige DXGK_MIN_PAGE_TABLE_LEVEL_COUNT = 2.
 * Para 16 GiB de VA (34 bits) com paginas de 4 KiB (12 bits de offset):
 *   Nivel 0 (Folha): 9 bits de indice -> 512 PTEs de 8 bytes = 4 KiB por folha (cobre 2 MiB)
 *   Nivel 1 (Raiz):  13 bits de indice -> ate 8192 PDEs na raiz redimensionavel (cobre 16 GiB)
 * Total: 12 + 9 + 13 = 34 bits.
 * ========================================================================= */
/*
 * P5 (doc 667): LEVEL_COUNT, LEAF_INDEX_BITS e ROOT_INDEX_BITS vem de
 * bc250_gpuva_geometry.h (fonte unica com o GCVM). Com BC250_P5_QUATRO_NIVEIS=1
 * a geometria e' 4 niveis x 9 bits = 48 bits, como o amdgpu; o texto acima
 * descreve a geometria de 2 niveis, que segue valendo com a chave em 0.
 */
#define BC250_GPUVA_PT_INDEX_BITS  BC250_GPUVA_PT_LEAF_INDEX_BITS
#define BC250_GPUVA_PT_ENTRY_BYTES 8u
#define BC250_GPUVA_PT_SIZE_BYTES \
    (BC250_GPUVA_PT_ENTRY_BYTES << BC250_GPUVA_PT_LEAF_INDEX_BITS)
#define BC250_GPUVA_PT_ALIGN_BYTES 4096u
#define BC250_GPUVA_PT_VA_BITS (12u + BC250_GPUVA_PT_LEAF_INDEX_BITS * (BC250_GPUVA_PT_LEVEL_COUNT - 1u) + BC250_GPUVA_PT_ROOT_INDEX_BITS)
#define BC250_GPUVA_PT_ROOT_MAX_ENTRIES (1u << BC250_GPUVA_PT_ROOT_INDEX_BITS)
#define BC250_GPUVA_DEFAULT_BITS BC250_GPUVA_PT_VA_BITS

/*
 * O QUE O SILICIO REALMENTE TRADUZ: 2^34 = 16 GiB.
 *
 * Todo caminho que produz ou registra PTE deve recusar VA a partir daqui.
 * Antes de 07/09 o journal usava BC250_GPUVA_MAX_LIMIT (2^48) como teto --
 * um fail-OPEN: aceitava registrar uma pagina num VA que o contexto VMID8
 * nao alcanca, e o sintoma disso e' indistinguivel do B1 (RPTR chega no
 * WPTR sem efeito). Nao era alcancavel pelo alocador vigente, que e'
 * inicializado com BC250_GPUVA_PT_VA_BITS, mas era uma porta aberta.
 */
#define BC250_GPUVA_TRANSLATABLE_LIMIT (1ull << BC250_GPUVA_PT_VA_BITS)

typedef enum BC250_GPUVA_STATUS {
    BC250_GPUVA_OK = 0,
    BC250_GPUVA_BAD_ARGUMENT,
    BC250_GPUVA_BAD_STATE,
    BC250_GPUVA_NO_SPACE,
    BC250_GPUVA_INVALID_ADDRESS,
    BC250_GPUVA_MISALIGNED,
    BC250_GPUVA_RANGE_OVERFLOW,
    BC250_GPUVA_OUT_OF_BOUNDS,
    BC250_GPUVA_INVALID_OVERLAP,
    BC250_GPUVA_NOT_FOUND,
    BC250_GPUVA_OWNER_MISMATCH,
    BC250_GPUVA_PERMISSION_DENIED,
    BC250_GPUVA_GAP_DETECTED,
    BC250_GPUVA_NOT_RESIDENT,
    BC250_GPUVA_STALE_EPOCH,
    BC250_GPUVA_IN_TEARDOWN,
    BC250_GPUVA_BUSY
} BC250_GPUVA_STATUS;

typedef enum BC250_GPUVA_RANGE_STATE {
    BC250_GPUVA_RANGE_FREE = 0,
    BC250_GPUVA_RANGE_MAPPED = 1,
    BC250_GPUVA_RANGE_UNMAPPED = 2,
    BC250_GPUVA_RANGE_TEARDOWN = 3
} BC250_GPUVA_RANGE_STATE;

typedef struct BC250_GPUVA_RANGE_RECORD {
    uint32_t State;             /* BC250_GPUVA_RANGE_STATE */
    uint32_t Generation;
    uint64_t RangeId;
    uint64_t BaseGpuVa;
    uint64_t Size;
    uint64_t EndGpuVa;          /* BaseGpuVa + Size (com overflow check) */
    BC250_MCDM_HANDLE OwnerProcess;
    BC250_MCDM_HANDLE OwnerContext;
    BC250_MCDM_HANDLE Allocation;
    BC250_MCDM_HANDLE OpenAllocation;
    /* Nome imutavel da alocacao, preservado no snapshot para o TDR. */
    uint64_t AllocationToken;
    uint32_t BackingSegmentId;
    uint64_t BackingSegmentOffset;
    uint32_t Permissions;       /* BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE */
    uint32_t IsResident;
    uint32_t PageTableEpoch;
    uint32_t TeardownState;
} BC250_GPUVA_RANGE_RECORD;

typedef struct BC250_GPUVA_SPACE {
    uint32_t Signature;
    uint32_t NextGeneration;
    uint64_t NextRangeId;
    uint32_t AddressBits;
    uint64_t MaxAddress;
    uint32_t PageTableEpoch;
    uint32_t Teardown;
    uint32_t LiveCount;
    uint32_t SortedCount;
    uint32_t SortedIndices[BC250_GPUVA_SPACE_MAX_RANGES];
    BC250_GPUVA_RANGE_RECORD Ranges[BC250_GPUVA_SPACE_MAX_RANGES];
} BC250_GPUVA_SPACE;

/*
 * C3.12 (12/09) -- OCUPACAO DO ESPACO POR ESTADO.
 *
 * MEDIDO na corrida 16:31: Bc250GpuvaSnapshotPublishBatch falhou com
 * PublishStatus=3 (BC250_GPUVA_NO_SPACE) e Attempts=1 Busy=0 -- ou seja, o
 * publicador pegou o lock de primeira e a falha veio de DENTRO, por exaustao
 * de slots. Isso REFUTOU a hipotese da C3.8 (contencao leitor/publicador),
 * que a propria instrumentacao da C3.8 permitiu derrubar.
 *
 * O que o numero sozinho NAO diz: se as 256 faixas estao legitimamente em uso
 * (capacidade insuficiente para um dwm.exe vivo) ou se slots pararam de ser
 * reciclados (vazamento). A busca por slot livre em Bc250GpuvaSpaceInsertRange
 * so' reaproveita FREE e UNMAPPED -- TEARDOWN NAO e' reaproveitado. Contar por
 * estado separa as duas causas, e a resposta muda completamente o conserto:
 * aumentar o limite, ou reciclar o que ficou preso.
 *
 * Aritmetica pura sobre a estrutura. Sem hardware, sem lock, testavel offline.
 */
typedef struct BC250_GPUVA_SPACE_OCCUPANCY {
    uint32_t Capacity;   /* BC250_GPUVA_SPACE_MAX_RANGES */
    uint32_t Free;       /* reaproveitavel */
    uint32_t Mapped;     /* em uso de verdade */
    uint32_t Unmapped;   /* reaproveitavel */
    uint32_t Teardown;   /* NAO reaproveitavel -- candidato a vazamento */
    uint32_t Reusable;   /* Free + Unmapped */
    uint32_t LiveCount;  /* o que a estrutura declara */
} BC250_GPUVA_SPACE_OCCUPANCY;

BC250_GPUVA_STATUS Bc250GpuvaSpaceDescribeOccupancy(
    const BC250_GPUVA_SPACE *Space,
    BC250_GPUVA_SPACE_OCCUPANCY *Occupancy);

/*
 * =========================================================================
 * DISCRIMINADOR DE ENDERECO DE FAULT (20/09, apos a Corrida 46).
 *
 * POR QUE EXISTE: o registrador GCVM_L2_PROTECTION_FAULT_ADDR_* guarda
 * LOGICAL_PAGE_ADDR, e o header upstream vendorizado NAO declara o tamanho
 * da pagina. Duas conversoes sao defensaveis e levam a lugares OPOSTOS --
 * com o valor medido 0x2431:
 *
 *   << 12  ->  0x2431000    (36 MB)   regiao VAZIA, nenhuma alocacao
 *   << 20  ->  0x243100000  (9,05 GB) cai ENTRE duas alocacoes vivas da winsys
 *
 * A primeira leitura acusa ponteiro perdido; a segunda, acesso alem do fim de
 * um buffer. Escolher por intuicao ja quase aconteceu duas vezes nesta
 * investigacao (a aritmetica do ">> 8" e o "IB aponta para 36 MB", ambas
 * derrubadas por leitura de codigo). Entao quem escolhe e' a MEDICAO: cada
 * candidato e' procurado no snapshot publicado, e o que aterrissar numa faixa
 * conhecida nomeia a alocacao.
 *
 * Nao encontrar tambem informa, desde que se diga QUAO LONGE ficou: um
 * endereco 4 KB depois do fim de uma faixa viva e estouro de buffer; um
 * endereco a gigabytes de qualquer faixa e outra coisa.
 *
 * Leitor PURO: nao escreve nada, nao pega lock, nao exige dono. No TDR nao se
 * sabe de quem e' o endereco que falhou, entao exigir OwnerProcess (como
 * Bc250GpuvaSpaceResolveAddress faz) tornaria a funcao inutil justamente aqui.
 * =========================================================================
 */
typedef struct BC250_GPUVA_ADDRESS_SITE {
    uint32_t Found;              /* 1 = o endereco cai DENTRO de uma faixa */
    uint32_t State;              /* BC250_GPUVA_RANGE_STATE da faixa achada */
    uint64_t BaseGpuVa;          /* base da faixa achada */
    uint64_t Size;               /* tamanho da faixa achada */
    uint64_t OffsetInRange;      /* GpuVa - BaseGpuVa, quando Found */
    BC250_MCDM_HANDLE OwnerProcess;
    BC250_MCDM_HANDLE Allocation;
    uint64_t AllocationToken;
    /*
     * SEGMENTO DE BACKING -- acrescentado em 20/09, depois da Corrida 47.
     *
     * O driver declara DOIS segmentos (QUERYSEGMENT4): o 1 e' local, ~243 MB,
     * CpuVisible, limitado pela janela BAR de 256 MB; o 2 e' GART/aperture, com
     * 2 GB e backing em MEMORIA DE SISTEMA. Sao codificacoes de PTE e semanticas
     * de coerencia DIFERENTES.
     *
     * A Corrida 47 nomeou as alocacoes que falham traduzir (0xF3 e 0xDF, ambas
     * do processo 0x2B, uma delas com Size = 2560*1080*4 exato) e NAO disse em
     * qual segmento elas vivem -- eu esqueci de publicar o campo. Sem ele, "a
     * faixa esta MAPPED e o walker falha" nao distingue defeito de tabela local
     * de defeito de mapeamento de memoria de sistema, que exigem consertos
     * opostos. O campo ja existia no registro; faltava mostra-lo.
     */
    uint32_t BackingSegmentId;
    uint64_t BackingSegmentOffset;
    /* Vizinhanca, valida SEMPRE -- e' ela que separa estouro de ponteiro perdido. */
    uint32_t HasBelow;
    uint64_t NearestBelowEnd;    /* fim da faixa viva imediatamente abaixo */
    uint64_t DistanceAboveEnd;   /* GpuVa - NearestBelowEnd (0 se Found) */
    uint32_t HasAbove;
    uint64_t NearestAboveBase;   /* base da faixa viva imediatamente acima */
    uint64_t DistanceBelowBase;  /* NearestAboveBase - GpuVa (0 se Found) */
    uint32_t LiveRangesScanned;
} BC250_GPUVA_ADDRESS_SITE;

BC250_GPUVA_STATUS Bc250GpuvaSpaceDescribeAddress(
    const BC250_GPUVA_SPACE *Space,
    uint64_t GpuVa,
    BC250_GPUVA_ADDRESS_SITE *Site);

BC250_GPUVA_STATUS Bc250GpuvaSpaceInitialize(
    BC250_GPUVA_SPACE *Space,
    uint32_t AddressBits);

BC250_GPUVA_STATUS Bc250GpuvaSpaceInsertRange(
    BC250_GPUVA_SPACE *Space,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint64_t *RangeId);

BC250_GPUVA_STATUS Bc250GpuvaSpaceRemoveRange(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    uint64_t BaseGpuVa,
    uint64_t Size);

/*
 * Reconstrói uma cópia completa em Scratch e só publica em Space no sucesso.
 * Deve ser usado em PASSIVE_LEVEL sobre o snapshot INATIVO; leitores nunca
 * podem observar Space enquanto a cópia final é publicada.
 */
BC250_GPUVA_STATUS Bc250GpuvaSpaceReplaceRangeTransactional(
    BC250_GPUVA_SPACE *Space,
    BC250_GPUVA_SPACE *Scratch,
    const BC250_GPUVA_RANGE_RECORD *Param,
    uint32_t MapRange);

BC250_GPUVA_STATUS Bc250GpuvaSpaceInvalidateForObject(
    BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    BC250_MCDM_HANDLE Allocation);

BC250_GPUVA_STATUS Bc250GpuvaSpaceValidateRangeCoverage(
    const BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE Context,
    uint64_t BaseGpuVa,
    uint64_t Size,
    uint32_t RequiredPermissions,
    uint32_t ExpectedPageTableEpoch);

/*
 * Resolve um unico endereco no snapshot publicado do processo. O retorno e'
 * um offset de segmento, nao um endereco CPU nem um MC address.
 */
BC250_GPUVA_STATUS Bc250GpuvaSpaceResolveAddress(
    const BC250_GPUVA_SPACE *Space,
    BC250_MCDM_HANDLE OwnerProcess,
    uint64_t GpuVa,
    uint32_t RequiredPermissions,
    uint32_t *BackingSegmentId,
    uint64_t *BackingSegmentOffset);

const char *Bc250GpuvaStatusName(BC250_GPUVA_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
