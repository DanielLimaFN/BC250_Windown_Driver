#ifndef BC250_GFX_RING_CONTRACT_H
#define BC250_GFX_RING_CONTRACT_H

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

#include "bc250_blt_proof_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================================================================
 * B.1 -- ANEL GRAFICO (CP_RB0). Parede B, etapa 1.
 *
 * POR QUE ESTE MODULO EXISTE
 *
 * Ate 13/09 o projeto submetia por duas engines: SDMA (Node 0) e MEC/HQD
 * (Node 1). A faixa F5.2 passou a emitir SET_CONTEXT_REG (IT=0x69) e
 * DRAW_INDEX_AUTO (IT=0x2D) -- pacotes do anel GRAFICO -- para o Node 1, que
 * e' servido por MQD/HQD, isto e' pela MEC. Isso NAO pode executar: a MEC
 * aceita o subconjunto de compute. O anel grafico simplesmente NAO EXISTE
 * neste driver.
 *
 * Este contrato e' a parte PURA da criacao desse anel: calculo de campos,
 * validacao de plano e classificacao de prova. Ele nao toca em MMIO -- quem
 * escreve registrador e' a transacao no miniport, sob a Lei 5.
 *
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 *
 * PROVENIENCIA (Lei 7)
 *
 * Offsets dos registradores: docs/03, bloco "CP ring GFX", classes P1/P0.
 * Campos de CP_RB0_CNTL: inc/linux_ref/gc_10_1_0_sh_mask.h, classe P1 --
 * RB_BUFSZ[5:0], RB_BLKSZ[13:8], BUF_SWAP[17:16], MIN_AVAILSZ[21:20],
 * MIN_IB_AVAILSZ[23:22], CACHE_POLICY[25:24], RB_VOLATILE[26],
 * RB_NO_UPDATE[27], RB_EXE[28], CE_HQD_NEQ_RB_HQD[30], RB_RPTR_WR_ENA[31].
 * =========================================================================
 */

/*
 * =========================================================================
 * O QUE A PLACA REALMENTE TEM NO BOOT -- MEDIDO EM 14/09 11:43 (P0).
 *
 * Primeira leitura do anel grafico na historia deste projeto, pelo preflight
 * somente-leitura (Reads=7, Writes=0 nos dois contadores):
 *
 *   CNTL=00A00000  BASE=FEDCBAEF  BASE_HI=0  RPTR=0  WPTR=0
 *   POLL_CNTL=00400000  ME_CNTL=15000000
 *
 * Decodificado pelos campos P1 do header upstream:
 *
 *   CP_RB0_CNTL : RB_BUFSZ=0  RB_BLKSZ=0  RB_EXE=0
 *                 MIN_AVAILSZ=2  MIN_IB_AVAILSZ=2  resto zero
 *   CP_ME_CNTL  : CE_HALT=1  PFP_HALT=1  ME_HALT=1
 *
 * O ANEL GRAFICO ESTA FRIO. Sem tamanho, sem execucao, ponteiros zerados,
 * BASE com padrao de lixo, e as TRES micro-engines halted. A placa faz boot
 * com o framebuffer do GOP e nunca liga o motor grafico.
 * =========================================================================
 */
#define BC250_GFX_RING_CNTL_MEASURED_BOOT 0x00A00000u
#define BC250_GFX_RING_ME_CNTL_MEASURED_BOOT 0x15000000u

/*
 * ANCORA ARITMETICA -- E NAO E' O ESTADO DE BOOT.
 *
 * CORRECAO DE REGISTRO (14/09): esta constante se chamava
 * BC250_GFX_RING_CNTL_OBSERVED_POST e eu a descrevia como "o valor que a
 * placa de fato usa". A medicao das 11:43 REFUTOU isso: o boot deixa
 * 0x00A00000. O 0x50000709 de docs/03 foi observado em OUTRO momento -- a
 * propria tabela do catalogo diz "observado 0xF40FE000 em V3" para o BASE,
 * isto e', DEPOIS de alguem ter configurado o anel.
 *
 * O valor continua util como referencia de um anel CONFIGURADO, e como
 * verificacao da aritmetica: com anel de 8192 bytes, RB_EXE=1 e
 * CE_HQD_NEQ_RB_HQD=1, Bc250GfxRingComputeCntl produz exatamente ele. Isso
 * corrobora a relacao RB_BLKSZ = RB_BUFSZ - 2, cuja ORIGEM (amdgpu
 * gfx_v10_0_cp_gfx_resume) e' classe H por nao haver copia local.
 *
 * O que ele NAO e': eco do POST. Nao usar como ponto de partida.
 *
 * O prototipo bc250_compute_service.c escreve 0x00A00709, que agora tem
 * explicacao completa: e' o CNTL do BOOT (0x00A00000) com OR dos bits de
 * tamanho (0x709). Quem escreveu leu o boot e somou o tamanho -- e NUNCA
 * ligou RB_EXE. Ver Bc250GfxRingCntlRefusesNonExecutable.
 */
#define BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE 0x50000709u

/*
 * VALOR PROIBIDO, herdado de um aviso que o catalogo ja dava.
 *
 * docs/03 registra, para CP_PQ_WPTR_POLL_CNTL: "0x00403004 e' do SDMA
 * (SdmaRingPollCntl) e esta PROIBIDO neste registrador". O prototipo em
 * bc250_compute_service.c escreve exatamente 0x00403004 em
 * CP_RB_WPTR_POLL_CNTL (BAR5 0x08708) -- o registrador IRMAO, sobre o qual o
 * catalogo diz "tentativas usaram 0x00400100; bits precisam do header mask".
 *
 * E' a mesma classe de defeito: valor de uma engine carregado para outra.
 * Aqui ele falha FECHADO em vez de ser escrito no silicio.
 */
#define BC250_GFX_RING_POLL_CNTL_SDMA_BORROWED 0x00403004u

typedef enum BC250_GFX_RING_STATUS {
    BC250_GFX_RING_OK = 0,
    BC250_GFX_RING_BAD_ARGUMENT = 1,
    /* RingBytes nao e' potencia de dois, ou esta fora de [4 KiB, 1 MiB]. */
    BC250_GFX_RING_BAD_SIZE = 2,
    /* CP_RB0_BASE carrega GPUVA >> 8: a base precisa ser multipla de 256. */
    BC250_GFX_RING_RING_MISALIGNED = 3,
    /* Writeback de RPTR / shadow de WPTR precisam de alinhamento de dword. */
    BC250_GFX_RING_WRITEBACK_MISALIGNED = 4,
    /* Anel, writeback ou shadow caem fora da janela de VRAM declarada. */
    BC250_GFX_RING_OUT_OF_WINDOW = 5,
    /* Duas das tres regioes se sobrepoem. */
    BC250_GFX_RING_OVERLAP = 6,
    /* Um campo nao cabe na largura declarada pelo header upstream. */
    BC250_GFX_RING_FIELD_OVERFLOW = 7,
    /* Plano pede anel nao executavel (RB_EXE = 0). */
    BC250_GFX_RING_NOT_EXECUTABLE = 8,
    /* Valor de poll importado do SDMA. */
    BC250_GFX_RING_POLL_CNTL_BORROWED = 9,
    /* Plano pede desalt das micro-engines sem declarar firmware carregado. */
    BC250_GFX_RING_FIRMWARE_NOT_LOADED = 10
} BC250_GFX_RING_STATUS;

/*
 * Plano do anel. Todos os enderecos sao GPUVA/MC, nunca fisicos de CPU.
 *
 * VramWindowBase/Bytes delimitam a faixa em que as tres regioes tem de caber.
 * Sem essa janela o contrato aceitaria qualquer endereco, e um anel fora da
 * VRAM mapeada e' caminho conhecido de fault de traducao.
 */
typedef struct BC250_GFX_RING_PLAN {
    uint64_t RingGpuVa;
    uint32_t RingBytes;
    uint64_t RptrWritebackGpuVa;
    uint64_t WptrPollGpuVa;
    uint64_t VramWindowBase;
    uint64_t VramWindowBytes;
    /* Campos de CP_RB0_CNTL sob controle do plano. */
    uint32_t RbExe;             /* [28] -- tem de ser 1 num anel que executa. */
    uint32_t CeHqdNeqRbHqd;     /* [30] -- semantica H; POST desta placa usa 1. */
    uint32_t CachePolicy;       /* [25:24] */
    uint32_t RbNoUpdate;        /* [27] */
    uint32_t RbRptrWrEna;       /* [31] */

    /*
     * DESALT DAS MICRO-ENGINES -- exigido pela medicao de 14/09.
     *
     * O boot desta placa entrega CP_ME_CNTL=0x15000000, isto e', CE, PFP e ME
     * HALTED. Um anel perfeitamente configurado com elas paradas aceita toda a
     * escrita, confere no readback e nao consome pacote nenhum. Configurar sem
     * desaltar seria declarar pronto o que nao roda -- Lei 4.
     *
     * MicroEngineFirmwareLoaded e' PRE-REQUISITO e nao enfeite: desaltar uma
     * micro-engine sem ucode e' soltar a engine sobre memoria de instrucao nao
     * inicializada. No bring-up deste driver o PFP direct load acontece DEPOIS
     * do ponto em que o anel e' medido (bc250_active_miniport.c: probe na
     * ~4809, PFP na ~5571), entao a ordem importa e fica declarada no plano em
     * vez de ficar implicita em quem chama.
     */
    uint32_t UnhaltMicroEngines;
    uint32_t MicroEngineFirmwareLoaded;
} BC250_GFX_RING_PLAN;

/*
 * Campos de CP_ME_CNTL (gc_10_1_0_sh_mask.h, classe P1):
 *   CE_HALT[24]  CE_STEP[25]  PFP_HALT[26]  PFP_STEP[27]  ME_HALT[28]
 *
 * Decodificar isto deixou de ser opcional: o boot desta placa entrega
 * 0x15000000, que e' exatamente CE_HALT|PFP_HALT|ME_HALT. Um anel grafico
 * configurado com as micro-engines halted nao executa pacote nenhum, e sem
 * este decode a proxima corrida teria de descobrir isso a mao outra vez.
 */
typedef struct BC250_GFX_ME_CNTL_FIELDS {
    uint32_t CeHalt;
    uint32_t CeStep;
    uint32_t PfpHalt;
    uint32_t PfpStep;
    uint32_t MeHalt;
    /* 1 quando as TRES estao halted -- o estado medido no boot. */
    uint32_t AllHalted;
} BC250_GFX_ME_CNTL_FIELDS;

typedef struct BC250_GFX_RING_CNTL_FIELDS {
    uint32_t RbBufsz;
    uint32_t RbBlksz;
    uint32_t BufSwap;
    uint32_t MinAvailsz;
    uint32_t MinIbAvailsz;
    uint32_t CachePolicy;
    uint32_t RbVolatile;
    uint32_t RbNoUpdate;
    uint32_t RbExe;
    uint32_t CeHqdNeqRbHqd;
    uint32_t RbRptrWrEna;
} BC250_GFX_RING_CNTL_FIELDS;

/*
 * Deriva RB_BUFSZ do tamanho do anel.
 *
 * RB_BUFSZ = log2(RingBytes / 8) - 1, e RB_BLKSZ = RB_BUFSZ - 2.
 * ORIGEM DA FORMULA: classe H (upstream sem copia local). CORROBORACAO:
 * reproduz exatamente o par (9, 7) do CP_RB0_CNTL medido nesta placa.
 *
 * Devolve BC250_GFX_RING_BAD_SIZE se o tamanho nao permitir campos validos.
 */
BC250_GFX_RING_STATUS Bc250GfxRingDeriveBufsz(
    uint32_t RingBytes,
    uint32_t *RbBufsz,
    uint32_t *RbBlksz);

/*
 * Valida o plano inteiro ANTES de qualquer escrita em registrador (Lei 5,
 * estagio PREFLIGHT). Nao escreve nada e nao tem efeito colateral.
 */
BC250_GFX_RING_STATUS Bc250GfxRingValidatePlan(const BC250_GFX_RING_PLAN *Plan);

/*
 * Monta o valor de CP_RB0_CNTL a partir do plano.
 *
 * FALHA FECHADA em campo que nao cabe na largura do header, em vez de
 * mascarar em silencio -- mascarar produziria um anel com geometria ERRADA e
 * ACEITA pelo hardware, que e' a classe de falso sucesso que a Lei 4 proibe.
 * O mesmo defeito foi encontrado e corrigido em Bc250Pm4EmitColorTarget0.
 */
BC250_GFX_RING_STATUS Bc250GfxRingComputeCntl(
    const BC250_GFX_RING_PLAN *Plan,
    uint32_t *Cntl);

/* Decodifica um CP_RB0_CNTL cru -- para snapshot, readback e log. */
BC250_GFX_RING_STATUS Bc250GfxRingDecodeCntl(
    uint32_t Raw,
    BC250_GFX_RING_CNTL_FIELDS *Fields);

/* Decodifica um CP_ME_CNTL cru. AllHalted resume o estado medido no boot. */
BC250_GFX_RING_STATUS Bc250GfxRingDecodeMeCntl(
    uint32_t Raw,
    BC250_GFX_ME_CNTL_FIELDS *Fields);

/*
 * TRUE quando as micro-engines graficas estao halted, isto e', quando um anel
 * configurado NAO executaria nada.
 *
 * Existe para que "configurei o anel" nunca seja confundido com "o anel roda".
 * Configurar CP_RB0_* com ME/PFP/CE halted produz exatamente o falso sucesso
 * que a Lei 4 proibe: todas as escritas aceitas, readback conferindo, e zero
 * pacote consumido.
 */
uint32_t Bc250GfxRingMicroEnginesAreHalted(uint32_t MeCntl);

/*
 * Calcula o CP_ME_CNTL que libera CE, PFP e ME, PRESERVANDO todo o resto.
 *
 * Usa a formula da Lei 6 -- val = (lido & ~mask) | (or_val & mask) -- com
 * or_val = 0 nos tres bits de HALT. Escrever um literal aqui apagaria
 * silenciosamente os bits de reset e de invalidacao de icache que o firmware
 * possa ter deixado, que e' o tipo de efeito colateral que nenhum log
 * mostraria.
 */
BC250_GFX_RING_STATUS Bc250GfxRingComputeMeCntlUnhalt(
    uint32_t CurrentMeCntl,
    uint32_t *UnhaltedMeCntl);

/* TRUE quando o valor tem RB_EXE = 0, isto e', anel que nao executa. */
uint32_t Bc250GfxRingCntlRefusesNonExecutable(uint32_t Cntl);

/* TRUE quando o valor e' o poll do SDMA carregado para um registrador do CP. */
uint32_t Bc250GfxRingPollCntlIsBorrowed(uint32_t PollCntl);

/*
 * Perna 1 do tripe (Lei 2) no anel grafico.
 *
 * NAO compara cru. A licao A15 custou uma corrida e um BugCheck 0x119: na
 * 17:43 o RPTR do SDMA leu 4612 com alvo 516 num anel de 4096 bytes -- a
 * engine tinha TERMINADO e a comparacao crua nunca fecharia. O helper de
 * reducao esta em fonte unica (Bc250TripodWrapRingPointer) e este contrato
 * usa o MESMO, em vez de repetir a aritmetica.
 */
BC250_TRIPOD_VERDICT Bc250GfxRingClassifyTripod(
    uint32_t RingBytes,
    uint32_t TargetWptrBytes,
    uint32_t RptrWriteback,
    uint32_t RptrRegister,
    uint32_t FenceExpected,
    uint32_t FenceObserved,
    uint32_t DataMismatches,
    BC250_TRIPOD_RPTR_SOURCE *RptrSource);

/* Nome estavel para log e evidencia. */
const char *Bc250GfxRingStatusName(BC250_GFX_RING_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif /* BC250_GFX_RING_CONTRACT_H */
