#include "bc250_gfx_ring_transaction.h"

static uint32_t Bc250GfxTxnInterfaceUsable(const BC250_GFX_RING_INTERFACE *Interface)
{
    if (Interface == NULL) {
        return 0u;
    }
    if (Interface->Read32 == NULL || Interface->Write32 == NULL) {
        return 0u;
    }
    if (Interface->Identity != BC250_GFX_RING_TXN_IDENTITY) {
        return 0u;
    }
    return 1u;
}

static uint32_t Bc250GfxTxnRead(
    const BC250_GFX_RING_INTERFACE *Interface,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence,
    uint32_t ByteOffset,
    uint32_t *Value)
{
    uint32_t ok = Interface->Read32(Interface->Context, ByteOffset, Value);
    if (ok) {
        Evidence->Reads++;
    }
    return ok;
}

static uint32_t Bc250GfxTxnWrite(
    const BC250_GFX_RING_INTERFACE *Interface,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence,
    uint32_t ByteOffset,
    uint32_t Value)
{
    uint32_t ok = Interface->Write32(Interface->Context, ByteOffset, Value);
    if (ok) {
        Evidence->Writes++;
    }
    return ok;
}

BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnPreflight(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *Plan,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence)
{
    BC250_GFX_RING_STATUS planStatus;
    uint32_t plannedCntl = 0u;
    uint32_t index;
    unsigned char *raw;

    if (!Bc250GfxTxnInterfaceUsable(Interface) || Evidence == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /* Zera a evidencia sem depender de memset: o link offline das suites nao
     * traz CRT, e o /O2 ja reemitiu copia manual como memcpy antes neste
     * projeto (armadilha registrada em docs/201). */
    raw = (unsigned char *)Evidence;
    for (index = 0u; index < (uint32_t)sizeof(*Evidence); ++index) {
        raw[index] = 0u;
    }

    /*
     * Plan == NULL e' MEDICAO PURA: tira o snapshot do anel como a placa o
     * deixou, sem propor nada. Existe porque a medicao nao pode depender de o
     * NOSSO plano ser valido -- se as caps ainda nao fecharam, queremos a
     * fotografia mesmo assim. CntlPlanned fica 0, e o APPLY ja recusa nesse
     * caso, entao o modo de medicao nao pode escorregar para escrita.
     *
     * Plano PRESENTE e invalido continua sendo recusado antes de qualquer
     * leitura: propor errado e' defeito, nao ausencia de proposta.
     */
    if (Plan != NULL) {
        planStatus = Bc250GfxRingValidatePlan(Plan);
        if (planStatus != BC250_GFX_RING_OK) {
            return BC250_GFX_RING_TXN_PLAN_REJECTED;
        }

        planStatus = Bc250GfxRingComputeCntl(Plan, &plannedCntl);
        if (planStatus != BC250_GFX_RING_OK) {
            return BC250_GFX_RING_TXN_PLAN_REJECTED;
        }
        Evidence->CntlPlanned = plannedCntl;
        (void)Bc250GfxRingDecodeCntl(plannedCntl, &Evidence->FieldsPlanned);
    }

    /* SNAPSHOT -- somente leitura, nada e' alterado por este estagio. */
    if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL,
            &Evidence->CntlBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE,
            &Evidence->BaseBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE_HI,
            &Evidence->BaseHiBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_RPTR,
            &Evidence->RptrBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_WPTR,
            &Evidence->WptrBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL,
            &Evidence->PollCntlBefore) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_ME_CNTL,
            &Evidence->MeCntlBefore)) {
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }

    (void)Bc250GfxRingDecodeCntl(Evidence->CntlBefore, &Evidence->FieldsBefore);
    (void)Bc250GfxRingDecodeMeCntl(Evidence->MeCntlBefore, &Evidence->MeCntlFields);
    Evidence->MicroEnginesHalted =
        Bc250GfxRingMicroEnginesAreHalted(Evidence->MeCntlBefore);
    Evidence->SnapshotTaken = 1u;
    return BC250_GFX_RING_TXN_OK;
}

BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnApply(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *Plan,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence)
{
    uint32_t readback = 0u;

    if (!Bc250GfxTxnInterfaceUsable(Interface) || Plan == NULL || Evidence == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /*
     * Aplicar sem snapshot e' aplicar sem rota de volta. A Lei 5 exige os dois
     * estagios, e o rollback so' sabe o que restaurar se o preflight rodou.
     */
    if (Evidence->SnapshotTaken != 1u || Evidence->CntlPlanned == 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /*
     * O valor de poll do SDMA e' barrado ANTES de qualquer escrita. O unico
     * prototipo do repositorio carrega exatamente esse valor para o CP; o
     * catalogo ja o proibia no registrador irmao.
     */
    if (Bc250GfxRingPollCntlIsBorrowed(Evidence->PollCntlBefore)) {
        return BC250_GFX_RING_TXN_POLL_CNTL_BORROWED;
    }

    /* Anel parado enquanto e' reconfigurado: WPTR e RPTR zerados primeiro. */
    if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB_DOORBELL_CONTROL, 0u) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_WPTR, 0u) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_WPTR_HI, 0u) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_RPTR, 0u)) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    /* CP_RB0_BASE carrega GPUVA >> 8 -- o alinhamento ja foi exigido no plano. */
    if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE,
            (uint32_t)(Plan->RingGpuVa >> 8)) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE_HI,
            (uint32_t)((Plan->RingGpuVa >> 8) >> 32)) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_RPTR_ADDR_LO,
            (uint32_t)Plan->RptrWritebackGpuVa) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_RPTR_ADDR_HI,
            (uint32_t)(Plan->RptrWritebackGpuVa >> 32)) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_LO,
            (uint32_t)Plan->WptrPollGpuVa) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB_WPTR_POLL_ADDR_HI,
            (uint32_t)(Plan->WptrPollGpuVa >> 32)) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL,
            Evidence->CntlPlanned)) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    if (Interface->Stall != NULL) {
        Interface->Stall(Interface->Context, 50u);
    }

    /* READBACK -- escrita sem conferencia nao e' prova. */
    if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL, &readback)) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }
    Evidence->CntlReadback = readback;

    if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE,
            &Evidence->BaseReadback) ||
        !Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE_HI,
            &Evidence->BaseHiReadback)) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }

    if (Evidence->CntlReadback != Evidence->CntlPlanned ||
        Evidence->BaseReadback != (uint32_t)(Plan->RingGpuVa >> 8) ||
        Evidence->BaseHiReadback != (uint32_t)((Plan->RingGpuVa >> 8) >> 32)) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }
    Evidence->ApplyReadbackPassed = 1u;

    /*
     * DESALT DAS MICRO-ENGINES -- depois da configuracao do anel, nunca antes.
     *
     * Ordem: soltar CE/PFP/ME com CP_RB0_BASE ainda em lixo (o boot entrega
     * 0xFEDCBAEF) seria apontar a engine para endereco invalido. O anel ja
     * esta escrito e conferido neste ponto.
     *
     * O plano tem de PEDIR e tem de declarar firmware carregado -- a
     * validacao ja recusou o contrario. Sem pedido, nada e' escrito.
     */
    if (Plan->UnhaltMicroEngines == 1u) {
        uint32_t unhalted = 0u;

        Evidence->UnhaltAttempted = 1u;
        if (Bc250GfxRingComputeMeCntlUnhalt(Evidence->MeCntlBefore, &unhalted)
                != BC250_GFX_RING_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
        Evidence->MeCntlPlanned = unhalted;

        if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_ME_CNTL, unhalted)) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
        if (Interface->Stall != NULL) {
            Interface->Stall(Interface->Context, 50u);
        }
        if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_ME_CNTL,
                &Evidence->MeCntlAfter)) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
        /*
         * A prova nao e' "o readback bate com o que escrevi" -- e' "as tres
         * HALT sairam". Um silicio que aceite a escrita e mantenha o bit
         * passaria no primeiro teste e falharia no unico que importa.
         */
        if (Bc250GfxRingMicroEnginesAreHalted(Evidence->MeCntlAfter) != 0u) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
        (void)Bc250GfxRingDecodeMeCntl(Evidence->MeCntlAfter, &Evidence->MeCntlFields);
        Evidence->MicroEnginesHalted = 0u;
        Evidence->UnhaltPassed = 1u;
    }

    /*
     * PARTIDA DO ANEL.
     *
     * Ate 14/09 este estagio nao existia: CP_RB0_ACTIVE nao tinha
     * procedencia e o APPLY parava aqui por Lei 7. Com
     * gc_10_1_0_offset.h vendorizado e provado contra nove enderecos que esta
     * placa ja confirmou, o offset e' P1 e a partida pode acontecer.
     *
     * ORDEM, e ela importa: anel configurado -> micro-engines liberadas ->
     * ATIVO. Ligar antes de qualquer um dos dois seria soltar a engine sobre
     * configuracao incompleta.
     *
     * A prova e' o READBACK: escrever 1 e seguir em frente declararia pronto
     * o que talvez nao tenha ligado -- sucesso sem backend (Lei 4).
     */
    Evidence->RingActivateAttempted = 1u;
    if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_ACTIVE, 1u)) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }
    if (Interface->Stall != NULL) {
        Interface->Stall(Interface->Context, 50u);
    }
    if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_ACTIVE,
            &Evidence->RingActivateReadback)) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }
    if ((Evidence->RingActivateReadback & 1u) == 0u) {
        return BC250_GFX_RING_TXN_NOT_STARTABLE;
    }
    Evidence->RingActivatePassed = 1u;

    /*
     * ANEL CONFIGURADO, MICRO-ENGINES LIBERADAS E ATIVO NO READBACK.
     *
     * Isto NAO e' "o anel executa". Executar so' se prova pelo tripe da Lei 2
     * -- RPTR==WPTR, fence fisica sobre poison e bytes conferidos -- sobre um
     * pacote real. Bc250GfxRingClassifyTripod existe para isso e nao foi
     * chamado aqui de proposito: a transacao configura, quem prova e' o
     * ensaio.
     */
    return BC250_GFX_RING_TXN_OK;
}

BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnRollback(
    const BC250_GFX_RING_INTERFACE *Interface,
    BC250_GFX_RING_TXN_EVIDENCE *Evidence)
{
    uint32_t readback = 0u;

    if (!Bc250GfxTxnInterfaceUsable(Interface) || Evidence == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }
    if (Evidence->SnapshotTaken != 1u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    Evidence->RollbackAttempted = 1u;

    /*
     * Ordem INVERSA do APPLY: desativa o anel, depois re-HALT, depois
     * restaura os registradores. Reverter a configuracao com o anel ainda
     * ATIVO e as engines soltas e' o pior estado possivel, e seria invisivel
     * no log.
     */
    if (Evidence->RingActivateAttempted == 1u) {
        if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_ACTIVE, 0u)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_ACTIVE, &readback)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if ((readback & 1u) != 0u) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
    }

    if (Evidence->UnhaltAttempted == 1u) {
        if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_ME_CNTL,
                Evidence->MeCntlBefore)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_ME_CNTL, &readback)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if (readback != Evidence->MeCntlBefore) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
    }

    if (!Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL,
            Evidence->CntlBefore) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE,
            Evidence->BaseBefore) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE_HI,
            Evidence->BaseHiBefore) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_WPTR,
            Evidence->WptrBefore) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_RPTR,
            Evidence->RptrBefore)) {
        return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
    }

    /* Rollback tambem se prova por readback, ou nao e' rollback. */
    if (!Bc250GfxTxnRead(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL, &readback)) {
        return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
    }
    if (readback != Evidence->CntlBefore) {
        return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
    }

    Evidence->RollbackPassed = 1u;
    return BC250_GFX_RING_TXN_OK;
}

const char *Bc250GfxRingTxnStatusName(BC250_GFX_RING_TXN_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_RING_TXN_OK:                 return "OK";
    case BC250_GFX_RING_TXN_BAD_ARGUMENT:       return "BAD_ARGUMENT";
    case BC250_GFX_RING_TXN_PLAN_REJECTED:      return "PLAN_REJECTED";
    case BC250_GFX_RING_TXN_PREFLIGHT_FAILED:   return "PREFLIGHT_FAILED";
    case BC250_GFX_RING_TXN_WRITE_FAILED:       return "WRITE_FAILED";
    case BC250_GFX_RING_TXN_READBACK_MISMATCH:  return "READBACK_MISMATCH";
    case BC250_GFX_RING_TXN_NOT_STARTABLE:      return "NOT_STARTABLE";
    case BC250_GFX_RING_TXN_ROLLBACK_FAILED:    return "ROLLBACK_FAILED";
    case BC250_GFX_RING_TXN_POLL_CNTL_BORROWED: return "POLL_CNTL_BORROWED";
    default:                                    return "DESCONHECIDO";
    }
}
