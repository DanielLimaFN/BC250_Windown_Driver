#include "bc250_blt_proof_contract.h"

BC250_BLT_PROOF_VERDICT Bc250ClassifyBltProof(
    uint32_t BytesMatch,
    uint32_t PostNonZero,
    uint32_t SampleCount)
{
    if (BytesMatch == 0u) {
        return BC250_BLT_PROOF_MISMATCH;
    }

    /*
     * Bytes conferem, mas ninguem amostrou o destino: nao ha como afirmar
     * conteudo. Indeterminado, e portanto sem direito a "TRIPE".
     */
    if (SampleCount == 0u) {
        return BC250_BLT_PROOF_INDETERMINATE;
    }

    if (PostNonZero == 0u) {
        return BC250_BLT_PROOF_BLANK;
    }

    return BC250_BLT_PROOF_CONTENT_OK;
}

uint32_t Bc250BltProofAllowsTripodClaim(BC250_BLT_PROOF_VERDICT Verdict)
{
    return (Verdict == BC250_BLT_PROOF_CONTENT_OK) ? 1u : 0u;
}

uint32_t Bc250BltProofRequiresFailClosed(BC250_BLT_PROOF_VERDICT Verdict)
{
    /*
     * Somente divergencia de bytes fecha o caminho. Frame em branco e
     * indeterminado sao informacao, nao defeito do blt.
     */
    return (Verdict == BC250_BLT_PROOF_MISMATCH) ? 1u : 0u;
}

/*
 * A13 -- classificacao do tripe de SDMA. Ordem das pernas conforme a Lei 2:
 * RPTR==WPTR primeiro, fence depois, bytes por ultimo. A ordem importa para o
 * diagnostico: fence certa com RPTR atrasado e' um estado diferente de fence
 * ausente, e antes desta correcao os dois saiam como "COMPROVADO".
 */
BC250_TRIPOD_VERDICT Bc250ClassifySdmaTripod(
    uint32_t TargetWptrBytes,
    uint32_t RptrWriteback,
    uint32_t RptrRegister,
    uint32_t FenceExpected,
    uint32_t FenceObserved,
    uint32_t DataMismatches,
    BC250_TRIPOD_RPTR_SOURCE *RptrSource)
{
    BC250_TRIPOD_RPTR_SOURCE source = BC250_TRIPOD_RPTR_SOURCE_NONE;
    uint32_t writebackMatch;
    uint32_t registerMatch;

    if (RptrSource != NULL) {
        *RptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;
    }

    /*
     * Alvo zero significa que nada foi publicado no anel. Nao ha submissao
     * para julgar, e "RPTR==WPTR==0" seria verdadeiro por vacuidade -- e' assim
     * que um anel que nunca armou passaria por provado.
     */
    if (TargetWptrBytes == 0u) {
        return BC250_TRIPOD_INDETERMINATE;
    }

    writebackMatch = (RptrWriteback == TargetWptrBytes) ? 1u : 0u;
    registerMatch = (RptrRegister == TargetWptrBytes) ? 1u : 0u;

    if (writebackMatch != 0u && registerMatch != 0u) {
        source = BC250_TRIPOD_RPTR_SOURCE_BOTH;
    } else if (writebackMatch != 0u) {
        source = BC250_TRIPOD_RPTR_SOURCE_WRITEBACK;
    } else if (registerMatch != 0u) {
        source = BC250_TRIPOD_RPTR_SOURCE_REGISTER;
    }

    if (RptrSource != NULL) {
        *RptrSource = source;
    }

    if (source == BC250_TRIPOD_RPTR_SOURCE_NONE) {
        return BC250_TRIPOD_RPTR_NOT_RETIRED;
    }
    if (FenceObserved != FenceExpected) {
        return BC250_TRIPOD_FENCE_NOT_WRITTEN;
    }
    if (DataMismatches != 0u) {
        return BC250_TRIPOD_DATA_MISMATCH;
    }

    return BC250_TRIPOD_PROVEN;
}

uint32_t Bc250TripodAllowsClaim(BC250_TRIPOD_VERDICT Verdict)
{
    return (Verdict == BC250_TRIPOD_PROVEN) ? 1u : 0u;
}

const char *Bc250TripodVerdictName(BC250_TRIPOD_VERDICT Verdict)
{
    switch (Verdict) {
    case BC250_TRIPOD_PROVEN:           return "TRIPE_PROVADO";
    case BC250_TRIPOD_RPTR_NOT_RETIRED: return "RPTR_NAO_ALCANCOU_WPTR";
    case BC250_TRIPOD_FENCE_NOT_WRITTEN:return "FENCE_NAO_ESCRITA_PELA_GPU";
    case BC250_TRIPOD_DATA_MISMATCH:    return "DADOS_DIVERGEM";
    case BC250_TRIPOD_INDETERMINATE:    return "INDETERMINADO";
    default:                            return "DESCONHECIDO";
    }
}

const char *Bc250TripodRptrSourceName(BC250_TRIPOD_RPTR_SOURCE Source)
{
    switch (Source) {
    case BC250_TRIPOD_RPTR_SOURCE_WRITEBACK: return "WRITEBACK";
    case BC250_TRIPOD_RPTR_SOURCE_REGISTER:  return "REGISTRADOR";
    case BC250_TRIPOD_RPTR_SOURCE_BOTH:      return "WRITEBACK+REGISTRADOR";
    case BC250_TRIPOD_RPTR_SOURCE_NONE:      return "NENHUMA";
    default:                                 return "DESCONHECIDA";
    }
}

/*
 * A14 -- layout dos canaries da perna 3. Formula preservada byte a byte da
 * implementacao inline que ja rodava em Bc250SdmaPresentFill (5 pontos: 0%,
 * 25%, 50%, 75% e ~100%, valores canaryBase ^ 0x1111*(i+1)), agora em fonte
 * unica para que o caminho INDIRECT use exatamente a mesma prova.
 */
uint32_t Bc250PlanDataProofCanaries(
    uint64_t DestMcAddress,
    uint32_t ByteCount,
    uint32_t ExpectedPattern,
    uint64_t VramMcStart,
    uint64_t FrameBufferSize,
    uint32_t FrameBufferMapped,
    BC250_DATA_PROOF_PLAN *Plan)
{
    uint32_t canaryBase;
    uint32_t index;
    uint64_t vramOffset;

    if (Plan == NULL) {
        return 0u;
    }

    Plan->Usable = 0u;
    Plan->CanaryCount = 0u;
    Plan->VramOffset = 0ull;
    for (index = 0u; index < BC250_DATA_PROOF_CANARY_COUNT; ++index) {
        Plan->Offsets[index] = 0u;
        Plan->Values[index] = 0u;
    }

    if (ByteCount < 4u || FrameBufferMapped == 0u || VramMcStart == 0ull ||
        DestMcAddress < VramMcStart) {
        return 0u;
    }

    vramOffset = DestMcAddress - VramMcStart;
    if (vramOffset >= FrameBufferSize ||
        (vramOffset + (uint64_t)ByteCount) > FrameBufferSize) {
        return 0u;
    }

    canaryBase = (ExpectedPattern == 0xDEADBEEFu) ? 0xCAFEBABEu : 0xDEADBEEFu;

    Plan->Offsets[0] = 0u;
    Plan->Offsets[1] = (ByteCount / 4u) & ~3u;
    Plan->Offsets[2] = (ByteCount / 2u) & ~3u;
    /* Formula original preservada: ByteCount cabe em 26 bits (teto de 64 MB),
     * entao ByteCount * 3 nao transborda 32 bits. */
    Plan->Offsets[3] = ((ByteCount * 3u) / 4u) & ~3u;
    Plan->Offsets[4] = (ByteCount > 16u) ? ((ByteCount - 16u) & ~3u) : 0u;

    for (index = 0u; index < BC250_DATA_PROOF_CANARY_COUNT; ++index) {
        Plan->Values[index] = canaryBase ^ (0x1111u * (index + 1u));
        /*
         * Canary igual ao padrao esperado anularia a prova: "substituido" e
         * "intacto" ficariam indistinguiveis. Nesse caso o plano nao serve.
         */
        if (Plan->Values[index] == ExpectedPattern) {
            return 0u;
        }
    }

    Plan->CanaryCount = BC250_DATA_PROOF_CANARY_COUNT;
    Plan->VramOffset = vramOffset;
    Plan->Usable = 1u;
    return 1u;
}

uint32_t Bc250CountDataProofMismatches(
    const BC250_DATA_PROOF_PLAN *Plan,
    const uint32_t *PostValues,
    uint32_t ExpectedPattern)
{
    uint32_t mismatches = 0u;
    uint32_t index;

    /*
     * Ausencia de leitura NAO e' sucesso. Sem plano utilizavel ou sem valores
     * lidos, devolvemos o numero maximo de divergencias -- assim quem chama
     * nao consegue declarar tripe por omissao.
     */
    if (Plan == NULL || PostValues == NULL || Plan->Usable == 0u ||
        Plan->CanaryCount == 0u) {
        return BC250_DATA_PROOF_CANARY_COUNT;
    }

    for (index = 0u; index < Plan->CanaryCount; ++index) {
        if (PostValues[index] != ExpectedPattern) {
            mismatches++;
        }
    }
    return mismatches;
}

uint32_t Bc250TripodWrapRingPointer(uint32_t Value, uint32_t RingBytes)
{
    /*
     * RingBytes zero seria divisao por zero; devolver o valor cru preserva o
     * comportamento de comparacao direta em vez de inventar um resultado.
     */
    if (RingBytes == 0u) {
        return Value;
    }
    return Value % RingBytes;
}

uint32_t Bc250TripodRingEntryConsumed(
    uint32_t RptrRaw,
    uint32_t StartBytes,
    uint32_t TargetBytes,
    uint32_t TailBytes,
    uint32_t RingBytes)
{
    uint32_t start, target, tail, rptr;
    uint32_t distRptr, distTarget, distTail;

    if (RingBytes == 0u) {
        return 0u;
    }
    start = StartBytes % RingBytes;
    target = TargetBytes % RingBytes;
    tail = TailBytes % RingBytes;
    rptr = RptrRaw % RingBytes;
    distTarget = (target + RingBytes - start) % RingBytes;
    distTail = (tail + RingBytes - start) % RingBytes;
    distRptr = (rptr + RingBytes - start) % RingBytes;
    /* Entrada vazia ou alvo depois do fim publicado: incoerente. */
    if (distTarget == 0u || distTarget > distTail) {
        return 0u;
    }
    return (distRptr >= distTarget && distRptr <= distTail) ? 1u : 0u;
}

uint32_t Bc250PresentCopySucceeded(uint32_t CopyCompleted, uint32_t SameSurface)
{
    return (CopyCompleted != 0u || SameSurface != 0u) ? 1u : 0u;
}
