#ifndef BC250_BLT_PROOF_CONTRACT_H
#define BC250_BLT_PROOF_CONTRACT_H

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
 * A9/A10 (06/09) -- classificacao honesta da prova do blt de apresentacao.
 *
 * MOTIVO, medido na corrida 2026-09-06 22:42: a instrumentacao imprimiu
 * "BYTE_PROOF PASS" em 63 de 63 amostras que tinham NonZero=0/256. A
 * comparacao de bytes passava porque origem E destino eram zero -- ou seja, o
 * rotulo de sucesso aparecia exatamente no caso em que a tela estava apagada.
 * Isso e' falso sucesso (Lei 4) e mascarou por horas a causa real (o DWM sem
 * device D3D11 nunca desenhou na primaria).
 *
 * Este contrato separa tres coisas que estavam confundidas:
 *   - a copia foi FIEL?        (comparacao byte a byte de origem contra destino)
 *   - o frame tinha CONTEUDO?  (amostra pos-blt com pixel nao-zero)
 *   - podemos dizer "TRIPE"?   (Lei 2: as tres pernas provadas)
 *
 * Copia fiel de frame em branco nao e' PASS e nao e' FAIL: e' INCONCLUSIVA.
 * Nao e' falha porque o DWM pode legitimamente apresentar preto; nao e' prova
 * porque zero contra zero nao demonstra caminho de pixel.
 */

/* F3.2: content sampling is deliberately NOT an input to execution success.
 * SameSurface is a genuine no-copy operation, not suppression of a new frame. */
uint32_t Bc250PresentCopySucceeded(uint32_t CopyCompleted, uint32_t SameSurface);

typedef enum BC250_BLT_PROOF_VERDICT {
    /* Comparacao nao pode ser feita (argumento invalido, mapeamento ausente). */
    BC250_BLT_PROOF_INDETERMINATE = 0,
    /* Bytes conferem E a amostra pos-blt tem conteudo: perna de dados provada. */
    BC250_BLT_PROOF_CONTENT_OK = 1,
    /* Bytes conferem, mas o frame esta todo zerado: inconclusivo. */
    BC250_BLT_PROOF_BLANK = 2,
    /* Bytes divergem: falha real, fecha o caminho. */
    BC250_BLT_PROOF_MISMATCH = 3
} BC250_BLT_PROOF_VERDICT;

/*
 * BytesMatch      -- resultado da comparacao byte a byte origem vs destino.
 * PostNonZero     -- quantas amostras pos-blt tinham valor diferente de zero.
 * SampleCount     -- total de amostras coletadas (0 = nao amostrado).
 */
BC250_BLT_PROOF_VERDICT Bc250ClassifyBltProof(
    uint32_t BytesMatch,
    uint32_t PostNonZero,
    uint32_t SampleCount);

/*
 * TRUE somente quando a terceira perna da Lei 2 esta de fato provada, isto e',
 * quando o log tem direito de usar a palavra "TRIPE".
 */
uint32_t Bc250BltProofAllowsTripodClaim(BC250_BLT_PROOF_VERDICT Verdict);

/*
 * TRUE quando o veredito exige fechar o caminho de SDMA (falhar fechado).
 * Frame em branco NAO fecha: desligar o blt porque a tela esta preta
 * transformaria um sintoma legitimo em regressao de driver.
 */
uint32_t Bc250BltProofRequiresFailClosed(BC250_BLT_PROOF_VERDICT Verdict);

/*
 * =========================================================================
 * A13 (07/09) -- a perna 1 do tripe passou a ser EXIGIDA, nao so' impressa.
 *
 * MOTIVO, medido na corrida 2026-09-07 16:19 (linha 15 de
 * bc250_display_log.txt):
 *
 *   [SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=68 RPTR=52
 *                 Fence=00001001 Dados=4/4 OK em 1 polls!
 *
 * O rotulo saiu com WPTR != RPTR. Nao era erro de impressao: o gate de
 * Bc250SdmaPresentBringup so' testava fence + divergencia de bytes -- a
 * igualdade RPTR==WPTR nunca entrava na decisao, e o poll aceitava
 * writeback OU registrador sem registrar QUAL deles casou. Um timeout de
 * 2000 polls tambem caia no mesmo "COMPROVADO" desde que a fence e os dados
 * estivessem certos.
 *
 * Pela Lei 2 as tres pernas sao obrigatorias e a primeira e' RPTR==WPTR.
 * Este contrato torna a decisao pura, testavel e nomeada, e obriga o log a
 * dizer de onde veio o RPTR aceito -- writeback e registrador sao leituras
 * distintas do mesmo ponteiro, e o writeback pode ficar atras.
 * =========================================================================
 */

typedef enum BC250_TRIPOD_VERDICT {
    /* Entrada degenerada (alvo zero, ponteiro de saida nulo): nada a afirmar. */
    BC250_TRIPOD_INDETERMINATE = 0,
    /* As tres pernas da Lei 2 provadas: o log tem direito a "TRIPE". */
    BC250_TRIPOD_PROVEN = 1,
    /* Perna 1: o RPTR nao alcancou o WPTR -- a engine nao drenou o anel. */
    BC250_TRIPOD_RPTR_NOT_RETIRED = 2,
    /* Perna 2: a fence esperada nao foi escrita pela GPU (poison intacto). */
    BC250_TRIPOD_FENCE_NOT_WRITTEN = 3,
    /* Perna 3: os bytes do destino divergem do gabarito da CPU. */
    BC250_TRIPOD_DATA_MISMATCH = 4
} BC250_TRIPOD_VERDICT;

typedef enum BC250_TRIPOD_RPTR_SOURCE {
    BC250_TRIPOD_RPTR_SOURCE_NONE = 0,
    BC250_TRIPOD_RPTR_SOURCE_WRITEBACK = 1,
    BC250_TRIPOD_RPTR_SOURCE_REGISTER = 2,
    BC250_TRIPOD_RPTR_SOURCE_BOTH = 3
} BC250_TRIPOD_RPTR_SOURCE;

/*
 * TargetWptrBytes -- WPTR publicado no anel, em bytes.
 * RptrWriteback   -- RPTR lido do writeback em memoria.
 * RptrRegister    -- RPTR lido do registrador da engine.
 * FenceExpected   -- valor que a GPU deveria gravar (poison + 1).
 * FenceObserved   -- valor lido do slot de fence apos o polling.
 * DataMismatches  -- quantos dwords do destino divergiram do gabarito.
 * RptrSource      -- opcional; recebe qual leitura satisfez a perna 1.
 *
 * A perna 1 e' satisfeita por QUALQUER uma das duas leituras igual ao alvo;
 * ambas sao leituras legitimas do mesmo ponteiro. O que este contrato proibe
 * e' declarar tripe sem NENHUMA delas casar.
 */
BC250_TRIPOD_VERDICT Bc250ClassifySdmaTripod(
    uint32_t TargetWptrBytes,
    uint32_t RptrWriteback,
    uint32_t RptrRegister,
    uint32_t FenceExpected,
    uint32_t FenceObserved,
    uint32_t DataMismatches,
    BC250_TRIPOD_RPTR_SOURCE *RptrSource);

/* TRUE somente para BC250_TRIPOD_PROVEN. */
uint32_t Bc250TripodAllowsClaim(BC250_TRIPOD_VERDICT Verdict);

/*
 * A15 (07/09) -- volta do anel na perna 1.
 *
 * MEDIDO na corrida 17:43, submissao FenceId=24:
 *
 *   IB_TRIPOD TIMEOUT: TargetWPTR=516, WPTR=516, RPTR=4612, FenceExp=00001030,
 *                      FenceObs=00001030
 *
 * 4612 = 4096 + 516, e o anel tem 4096 bytes: o RPTR do registrador conta ALEM
 * de uma volta. A fence bateu (a GPU terminou), mas a comparacao crua
 * rptr == target nunca fecharia. O timeout devolveu STATUS_IO_DEVICE_ERROR ao
 * VidMm e a maquina caiu em BugCheck 0x119 (Arg1=2, Arg2=0xC0000185).
 *
 * O caminho de fill JA tinha esta aritmetica; o caminho INDIRECT comparava
 * cru. Aqui ela fica em fonte unica para os dois.
 */
uint32_t Bc250TripodWrapRingPointer(uint32_t Value, uint32_t RingBytes);

/* Nome estavel para log e evidencia. */
const char *Bc250TripodVerdictName(BC250_TRIPOD_VERDICT Verdict);
const char *Bc250TripodRptrSourceName(BC250_TRIPOD_RPTR_SOURCE Source);

/*
 * =========================================================================
 * A14 (07/09) -- a perna 3 do tripe passou a EXISTIR no caminho INDIRECT.
 *
 * MOTIVO, medido na corrida 2026-09-07 17:13: dez linhas
 *
 *   IB_TRIPOD PASS #N: WPTR=.. RPTR=.. Fence=.. Poison=.. Size=64 Vmid=8
 *
 * O rotulo diz TRIPOD, mas o gate que o produzia exigia SO' duas pernas --
 * RPTR==WPTR e fence sobre poison. Nenhum byte do destino era conferido.
 * Mesma classe de falso sucesso da A13, num lugar que a A13 nao tocou.
 *
 * A prova de dados aqui NAO pode ser "comparar destino com gabarito", porque
 * o padrao dos fills de paginacao e' 0x00000000 e zero contra zero e'
 * inconclusivo -- e' o que reprovou o blt de apresentacao em 63/63 amostras.
 * A saida e' plantar CANARIES nao-nulos ANTES do disparo e conferir que a GPU
 * os substituiu pelo padrao esperado: zero SOBRE poison e' conclusivo.
 *
 * Este contrato e' a fonte unica do layout dos canaries, compartilhada pelo
 * fill direto (que ja fazia isso inline) e pelo caminho INDIRECT (que nao
 * fazia nada).
 * =========================================================================
 */

#define BC250_DATA_PROOF_CANARY_COUNT 5u

typedef struct BC250_DATA_PROOF_PLAN {
    /* 1 = o destino esta na abertura mapeada pela CPU e da' para conferir. */
    uint32_t Usable;
    uint32_t CanaryCount;
    /* Deslocamentos em bytes a partir do inicio do destino. */
    uint32_t Offsets[BC250_DATA_PROOF_CANARY_COUNT];
    /* Valores nao-nulos a plantar, todos distintos do padrao esperado. */
    uint32_t Values[BC250_DATA_PROOF_CANARY_COUNT];
    /* Deslocamento do destino dentro da abertura de framebuffer. */
    uint64_t VramOffset;
} BC250_DATA_PROOF_PLAN;

/*
 * DestMcAddress    -- endereco MC do destino da operacao.
 * ByteCount        -- tamanho da operacao.
 * ExpectedPattern  -- o que a GPU deve deixar no destino.
 * VramMcStart      -- base MC da VRAM; 0 = desconhecida.
 * FrameBufferSize  -- tamanho da abertura mapeada pela CPU.
 * FrameBufferMapped-- 1 quando existe ponteiro de CPU para a abertura.
 *
 * Devolve 1 quando o plano e' utilizavel. Plano nao utilizavel NAO e' falha:
 * significa que a perna 3 fica NAO CONFERIDA e o log nao pode dizer "TRIPE".
 */
uint32_t Bc250PlanDataProofCanaries(
    uint64_t DestMcAddress,
    uint32_t ByteCount,
    uint32_t ExpectedPattern,
    uint64_t VramMcStart,
    uint64_t FrameBufferSize,
    uint32_t FrameBufferMapped,
    BC250_DATA_PROOF_PLAN *Plan);

/*
 * Conta quantos canaries NAO foram substituidos pelo padrao esperado. Plano
 * nao utilizavel ou ponteiro nulo devolve BC250_DATA_PROOF_CANARY_COUNT, isto
 * e', "nada provado" -- nunca zero, para que ausencia de leitura jamais seja
 * confundida com sucesso.
 */
uint32_t Bc250CountDataProofMismatches(
    const BC250_DATA_PROOF_PLAN *Plan,
    const uint32_t *PostValues,
    uint32_t ExpectedPattern);

#ifdef __cplusplus
}
#endif

#endif /* BC250_BLT_PROOF_CONTRACT_H */
