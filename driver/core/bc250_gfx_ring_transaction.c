#include "bc250_gfx_ring_transaction.h"

static int Bc250IsPowerOfTwo32(uint32_t Value)
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

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
    if (ok && Evidence != NULL) {
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
    if (ok && Evidence != NULL) {
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
            Bc250GfxRingEncodeBaseLo(Plan->RingGpuVa)) ||
        !Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_BASE_HI,
            Bc250GfxRingEncodeBaseHi(Plan->RingGpuVa)) ||
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

    /*
     * D0.4 -- CADENCIA DE POLL DE WPTR, depois do endereco e antes de ligar.
     *
     * O endereco do shadow (CP_RB_WPTR_POLL_ADDR_LO/HI) ja foi escrito acima e
     * sempre foi escrito. O que NUNCA foi escrito e' a cadencia. Na corrida
     * 2026-09-18 14:20:07 o resultado disso ficou visivel: doorbell em 0,
     * cadencia de poll como a placa deixou, e o unico canal restante -- a
     * escrita MMIO de CP_RB0_WPTR -- nao retendo o valor (4 de 4 submissoes
     * com WPTR_READBACK_DIVERGENTE).
     *
     * O plano MANDA: sem ProgramPollCntl nada e' escrito e o registrador fica
     * exatamente como estava, que e' o comportamento de todos os candidatos
     * anteriores. Pedido invalido (campo largo demais, ou o valor do SDMA) NAO
     * degrada para "escreve o que der": aborta a transacao, e o rollback tem o
     * PollCntlBefore do snapshot para voltar.
     *
     * ESTE REGISTRADOR NAO TEM BIT DE ENABLE (gc_10_1_0_sh_mask.h: so'
     * POLL_FREQUENCY[15:0] e IDLE_POLL_COUNT[31:16]). Programa-lo nao liga o
     * mecanismo -- e essa limitacao esta declarada no header do plano para que
     * um resultado nulo na proxima corrida nao seja lido como refutacao do
     * canal de doorbell, que e' o mecanismo provado nesta placa no compute.
     */
    if (Plan->ProgramPollCntl != 0u) {
        uint32_t pollPlanned = 0u;
        const BC250_GFX_RING_STATUS pollStatus =
            Bc250GfxRingComputePollCntl(Plan, &pollPlanned);

        Evidence->PollCntlApplyAttempted = 1u;
        if (pollStatus != BC250_GFX_RING_OK) {
            return (pollStatus == BC250_GFX_RING_POLL_CNTL_BORROWED)
                ? BC250_GFX_RING_TXN_POLL_CNTL_BORROWED
                : BC250_GFX_RING_TXN_PLAN_REJECTED;
        }
        Evidence->PollCntlPlanned = pollPlanned;

        if (!Bc250GfxTxnWrite(Interface, Evidence,
                BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL, pollPlanned)) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
        if (Interface->Stall != NULL) {
            Interface->Stall(Interface->Context, 50u);
        }
        if (!Bc250GfxTxnRead(Interface, Evidence,
                BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL, &Evidence->PollCntlReadback)) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
        /*
         * Escrita sem conferencia nao e' prova (Lei 4). Readback divergente
         * aqui e' informacao boa: significa que este registrador tambem nao
         * retem, e nesse caso o canal de poll esta descartado junto com o MMIO
         * -- sobra a doorbell. Por isso a transacao ABORTA em vez de seguir:
         * seguir declararia configurado o que nao esta.
         */
        if (Evidence->PollCntlReadback != pollPlanned) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
        Evidence->PollCntlApplyPassed = 1u;
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
        Evidence->BaseReadback != Bc250GfxRingEncodeBaseLo(Plan->RingGpuVa) ||
        Evidence->BaseHiReadback != Bc250GfxRingEncodeBaseHi(Plan->RingGpuVa)) {
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

    /*
     * D0.4 -- o poll cntl volta ao valor do snapshot, e volta ANTES dos
     * registradores do anel porque foi escrito DEPOIS deles no APPLY (ordem
     * inversa, Lei 6). So' tenta se o APPLY tentou: restaurar um registrador
     * que nunca tocamos escreveria o snapshot sobre um valor que pode ter
     * mudado por outro caminho.
     */
    if (Evidence->PollCntlApplyAttempted == 1u) {
        if (!Bc250GfxTxnWrite(Interface, Evidence,
                BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL, Evidence->PollCntlBefore)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if (!Bc250GfxTxnRead(Interface, Evidence,
                BC250_GFX_REG_CP_RB_WPTR_POLL_CNTL, &readback)) {
            return BC250_GFX_RING_TXN_ROLLBACK_FAILED;
        }
        if (readback != Evidence->PollCntlBefore) {
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

BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmitShReg(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *RingPlan,
    const BC250_GFX_RING_TXN_EVIDENCE *ApplyEvidence,
    const BC250_GFX_RING_SUBMIT_SH_REG_PLAN *SubmitPlan,
    BC250_GFX_RING_SUBMIT_SH_REG_EVIDENCE *SubmitEvidence)
{
    unsigned char *raw;
    uint32_t index;
    uint32_t initialWptr = 0u;
    uint32_t cursor;
    BC250_GFX_SH_REG_EMISSION_EVIDENCE shEv;
    BC250_GFX_SH_REG_STATUS shStatus;
    BC250_PM4_STATUS pm4Status;
    uint32_t maxPolls;
    uint32_t stallUs;
    uint32_t ringBytes;
    uint32_t targetWptrBytes;
    uint32_t iter;
    BC250_TRIPOD_VERDICT verdict = BC250_TRIPOD_INDETERMINATE;
    BC250_TRIPOD_RPTR_SOURCE rptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;

    if (SubmitEvidence != NULL) {
        raw = (unsigned char *)SubmitEvidence;
        for (index = 0u; index < (uint32_t)sizeof(*SubmitEvidence); ++index) {
            raw[index] = 0u;
        }
    }

    if (!Bc250GfxTxnInterfaceUsable(Interface) || RingPlan == NULL ||
        ApplyEvidence == NULL || SubmitPlan == NULL || SubmitEvidence == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /*
     * Submeter sem anel ativo e conferido e' disparar sobre pipeline desligado (Lei 4).
     * Exige que ApplyEvidence tenha completado com RB0_ACTIVE=1.
     */
    if (ApplyEvidence->RingActivatePassed != 1u || ApplyEvidence->ApplyReadbackPassed != 1u) {
        return BC250_GFX_RING_TXN_NOT_ACTIVE;
    }

    if (SubmitPlan->RingCpuAddress == NULL || SubmitPlan->FenceCpuAddress == NULL ||
        SubmitPlan->VsRegs == NULL || SubmitPlan->PsRegs == NULL ||
        SubmitPlan->RingDwords < BC250_GFX_PM4_SUBMIT_MIN_DWORDS ||
        (SubmitPlan->RingDwords & (SubmitPlan->RingDwords - 1u)) != 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /* 1. Injeta poison na fence com readback proof antes de qualquer gravacao no anel (Lei 2). */
    *SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;
    SubmitEvidence->FencePoisonPlanted = SubmitPlan->FencePoisonValue;
    SubmitEvidence->FencePoisonReadback = *SubmitPlan->FenceCpuAddress;
    if (SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue) {
        return BC250_GFX_RING_TXN_FENCE_POISON_FAILED;
    }

    /* 2. Le WPTR inicial do registrador de hardware. */
    if (!Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR, &initialWptr)) {
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    SubmitEvidence->InitialWptrDwords = initialWptr;
    cursor = initialWptr;

    /* 3. Emite os 12 dwords de pipeline de shaders (VS + PS) reusando Bc250Pm4EmitSetShReg. */
    shStatus = Bc250GfxShRegEmitPipeline(SubmitPlan->RingCpuAddress,
                                         SubmitPlan->RingDwords,
                                         &cursor,
                                         SubmitPlan->VsRegs,
                                         SubmitPlan->PsRegs,
                                         &shEv);
    if (shStatus != BC250_GFX_SH_REG_OK) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    /* 4. Emite fence fisica na VRAM sobre o poison via WRITE_DATA (DST_SEL=5 Memory Direct MC).
     * Sob VMID 0 no anel grafico CP_RB0 sem GCVM page tables, WRITE_DATA e' a forma
     * comprovada no silicio (bc250_pfp_direct_load) para gravacao direta de fence em VRAM MC. */
    {
        uint32_t wdControl = BC250_PM4_WD_CONFIRM | BC250_PM4_WD_DST_MEM; /* 0x00100500u */
        pm4Status = Bc250Pm4EmitWriteData(SubmitPlan->RingCpuAddress,
                                          SubmitPlan->RingDwords,
                                          &cursor,
                                          wdControl,
                                          SubmitPlan->FenceGpuVa,
                                          &SubmitPlan->FenceExpectedValue,
                                          1u);
        if (pm4Status != BC250_PM4_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }

        /* Pad de 3 NOPs (PACKET2) para manter alinhamento de 4 dwords (12 dw shader + 5 dw fence + 3 dw NOP = 20 dwords) */
        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
    }

    SubmitEvidence->TargetWptrDwords = cursor;
    SubmitEvidence->EmittedDwords = cursor - initialWptr;

    /* 5. Atualiza shadow de WPTR em memoria (se configurado). */
    if (SubmitPlan->WptrPollCpuAddress != NULL) {
        *SubmitPlan->WptrPollCpuAddress = cursor;
    }

    /* 6. Dispara o anel avancando CP_RB0_WPTR no silicio. */
    if (!Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR, cursor) ||
        !Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR_HI, 0u)) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    /* 7. Laco de amostragem e classificacao do tripe da Lei 2. */
    maxPolls = (SubmitPlan->MaxPollIterations > 0u) ? SubmitPlan->MaxPollIterations : 1000u;
    stallUs = (SubmitPlan->StallMicroseconds > 0u) ? SubmitPlan->StallMicroseconds : 10u;
    ringBytes = SubmitPlan->RingDwords * (uint32_t)sizeof(uint32_t);
    targetWptrBytes = cursor * (uint32_t)sizeof(uint32_t);

    for (iter = 0u; iter < maxPolls; ++iter) {
        uint32_t rptrMmio = 0u;
        uint32_t rptrWb = 0u;
        uint32_t fenceObs;

        /* Dreno e coerencia HDP (PCIe) para que a leitura de CPU sobre VRAM
         * enxergue os dados gravados pelo motor CP da GPU na VRAM (Lei 2) */
        (void)Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u);

        fenceObs = *(volatile const uint32_t *)SubmitPlan->FenceCpuAddress;

        if (!Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_RB0_RPTR, &rptrMmio)) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }

        if (SubmitPlan->RptrWbCpuAddress != NULL) {
            rptrWb = *(volatile const uint32_t *)SubmitPlan->RptrWbCpuAddress;
        }

        SubmitEvidence->RptrObservedMmio = rptrMmio;
        SubmitEvidence->RptrObservedWb = rptrWb;
        SubmitEvidence->FenceObserved = fenceObs;

        /* Perna 1 usa Bc250GfxRingClassifyTripod com reducao de volta A15. */
        verdict = Bc250GfxRingClassifyTripod(
            ringBytes,
            targetWptrBytes,
            rptrWb * (uint32_t)sizeof(uint32_t),
            rptrMmio * (uint32_t)sizeof(uint32_t),
            SubmitPlan->FenceExpectedValue,
            fenceObs,
            0u,
            &rptrSource);

        SubmitEvidence->TripodVerdict = verdict;
        SubmitEvidence->RptrSource = rptrSource;

        if (verdict == BC250_TRIPOD_PROVEN) {
            SubmitEvidence->PollsCompleted = iter + 1u;
            SubmitEvidence->SubmitCompleted = 1u;
            return BC250_GFX_RING_TXN_OK;
        }

        if (Interface->Stall != NULL && stallUs > 0u) {
            Interface->Stall(Interface->Context, stallUs);
        }
    }

    SubmitEvidence->PollsCompleted = maxPolls;
    SubmitEvidence->SubmitCompleted = 0u;
    return BC250_GFX_RING_TXN_SUBMIT_TIMEOUT;
}

BC250_GFX_RING_TXN_STATUS Bc250GfxVerify3DDrawStream(
    const uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t CursorDwords,
    const BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN *SubmitPlan)
{
    BC250_GFX_VIEWPORT_SCISSOR_PACKET vpPacket;
    BC250_GFX_PRIMITIVE_PACKET primPacket;
    BC250_GFX_DRAW_PACKET drawPacket;
    uint32_t bpp = 4u;
    uint32_t pitchPixels;
    uint32_t pitchTiles;
    uint32_t sliceTiles;
    uint32_t colorInfo;
    uint32_t mask;

    uint32_t stage;

    if (RingBuffer == NULL || SubmitPlan == NULL || !Bc250IsPowerOfTwo32(RingDwords)) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }
    if (SubmitPlan->VsRegs == NULL || SubmitPlan->PsRegs == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (SubmitPlan->IsolationStage == BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        stage = BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT;
    } else if (SubmitPlan->IsolationStage != 0u) {
        stage = SubmitPlan->IsolationStage;
    } else if (SubmitPlan->SkipDraw != 0u) {
        stage = 3u;
    } else {
        stage = 4u;
    }
    if (stage > 5u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    mask = RingDwords - 1u;

    /* 1. Shaders (12 DW) */
    if (RingBuffer[(CursorDwords + 0u) & mask] != 0xC0047600u ||
        RingBuffer[(CursorDwords + 1u) & mask] != 0x00000048u ||
        RingBuffer[(CursorDwords + 2u) & mask] != SubmitPlan->VsRegs->PgmLo ||
        RingBuffer[(CursorDwords + 3u) & mask] != SubmitPlan->VsRegs->PgmHi ||
        RingBuffer[(CursorDwords + 4u) & mask] != SubmitPlan->VsRegs->Rsrc1 ||
        RingBuffer[(CursorDwords + 5u) & mask] != SubmitPlan->VsRegs->Rsrc2 ||
        RingBuffer[(CursorDwords + 6u) & mask] != 0xC0047600u ||
        RingBuffer[(CursorDwords + 7u) & mask] != 0x00000008u ||
        RingBuffer[(CursorDwords + 8u) & mask] != SubmitPlan->PsRegs->PgmLo ||
        RingBuffer[(CursorDwords + 9u) & mask] != SubmitPlan->PsRegs->PgmHi ||
        RingBuffer[(CursorDwords + 10u) & mask] != SubmitPlan->PsRegs->Rsrc1 ||
        RingBuffer[(CursorDwords + 11u) & mask] != SubmitPlan->PsRegs->Rsrc2) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }

    /* 2. Color Target 0 (8 DW) */
    if (stage >= 1u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        pitchPixels = SubmitPlan->ColorTargetPitchBytes / bpp;
        if (pitchPixels == 0u) pitchPixels = SubmitPlan->ColorTargetWidth;
        pitchTiles = (pitchPixels + 7u) / 8u;
        if (pitchTiles > 0u) pitchTiles -= 1u;
        sliceTiles = ((pitchPixels + 7u) / 8u) * ((SubmitPlan->ColorTargetHeight + 7u) / 8u);
        if (sliceTiles > 0u) sliceTiles -= 1u;
        colorInfo = ((SubmitPlan->ColorTargetFormatIndex & 0x1Fu) << 2) |
                    ((SubmitPlan->ColorTargetCompSwap & 0x3u) << 11) | (1u << 16);

        if (RingBuffer[(CursorDwords + 12u) & mask] != 0xC0066900u ||
            RingBuffer[(CursorDwords + 13u) & mask] != 0x00000318u ||
            RingBuffer[(CursorDwords + 14u) & mask] != (uint32_t)(SubmitPlan->ColorTargetBaseGpuVa >> 8) ||
            RingBuffer[(CursorDwords + 15u) & mask] != pitchTiles ||
            RingBuffer[(CursorDwords + 16u) & mask] != sliceTiles ||
            RingBuffer[(CursorDwords + 17u) & mask] != 0u ||
            RingBuffer[(CursorDwords + 18u) & mask] != colorInfo ||
            RingBuffer[(CursorDwords + 19u) & mask] != 0u) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_COLOR_DWORDS; ++nIdx) {
            if (RingBuffer[(CursorDwords + 12u + nIdx) & mask] != BC250_PM4_PACKET2_NOP) {
                return BC250_GFX_RING_TXN_READBACK_MISMATCH;
            }
        }
    }

    /* 3. Target Mask (4 DW) */
    if (stage >= 1u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        if (RingBuffer[(CursorDwords + 20u) & mask] != 0xC0016900u ||
            RingBuffer[(CursorDwords + 21u) & mask] != 0x0000008Eu ||
            RingBuffer[(CursorDwords + 22u) & mask] != 0x0000000Fu ||
            RingBuffer[(CursorDwords + 23u) & mask] != 0x80000000u) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_TARGET_MASK_DWORDS; ++nIdx) {
            if (RingBuffer[(CursorDwords + 20u + nIdx) & mask] != BC250_PM4_PACKET2_NOP) {
                return BC250_GFX_RING_TXN_READBACK_MISMATCH;
            }
        }
    }

    /* 4. Viewport & Scissor (20 DW) */
    if (stage >= 2u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        if (Bc250GfxComputeViewportScissor(&SubmitPlan->Viewport, &SubmitPlan->Scissor, &vpPacket) != BC250_GFX_VPORT_OK) {
            return BC250_GFX_RING_TXN_BAD_ARGUMENT;
        }
        if (Bc250GfxVerifyViewportScissorStream(RingBuffer, RingDwords, CursorDwords + 44u, &vpPacket) != BC250_GFX_VPORT_OK) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_VPORT_DWORDS; ++nIdx) {
            if (RingBuffer[(CursorDwords + 24u + nIdx) & mask] != BC250_PM4_PACKET2_NOP) {
                return BC250_GFX_RING_TXN_READBACK_MISMATCH;
            }
        }
    }

    /* 5. Primitive Assembler (8 DW) */
    if (stage >= 3u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        if (Bc250GfxComputePrimitiveAssembler(SubmitPlan->PrimitiveTopology, SubmitPlan->NumInstances, &primPacket) != BC250_GFX_PRIM_OK) {
            return BC250_GFX_RING_TXN_BAD_ARGUMENT;
        }
        if (Bc250GfxVerifyPrimitiveAssemblerStream(RingBuffer, RingDwords, CursorDwords + 52u, &primPacket) != BC250_GFX_PRIM_OK) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_PRIM_DWORDS; ++nIdx) {
            if (RingBuffer[(CursorDwords + 44u + nIdx) & mask] != BC250_PM4_PACKET2_NOP) {
                return BC250_GFX_RING_TXN_READBACK_MISMATCH;
            }
        }
    }

    /* 6. Draw Initiator (4 DW) ou 4 NOPs (SkipDraw / Stage < 4 / Stage == 5) */
    if (stage >= 4u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        if (Bc250GfxComputeDrawInitiator(SubmitPlan->VertexCount, SubmitPlan->DrawInitiator, &drawPacket) != BC250_GFX_DRAW_OK) {
            return BC250_GFX_RING_TXN_BAD_ARGUMENT;
        }
        if (Bc250GfxVerifyDrawInitiatorStream(RingBuffer, RingDwords, CursorDwords + 56u, &drawPacket) != BC250_GFX_DRAW_OK) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_INIT_DWORDS; ++nIdx) {
            if (RingBuffer[(CursorDwords + 52u + nIdx) & mask] != BC250_PM4_PACKET2_NOP) {
                return BC250_GFX_RING_TXN_READBACK_MISMATCH;
            }
        }
    }

    /* 7. Physical Fence WRITE_DATA (DST_SEL=5 Memory Direct MC) + 3 NOPs (8 DW) */
    if (RingBuffer[(CursorDwords + 56u) & mask] != 0xC0033700u ||
        RingBuffer[(CursorDwords + 57u) & mask] != 0x00100500u ||
        RingBuffer[(CursorDwords + 58u) & mask] != (uint32_t)(SubmitPlan->FenceGpuVa & 0xFFFFFFFFull) ||
        RingBuffer[(CursorDwords + 59u) & mask] != (uint32_t)(SubmitPlan->FenceGpuVa >> 32) ||
        RingBuffer[(CursorDwords + 60u) & mask] != SubmitPlan->FenceExpectedValue ||
        RingBuffer[(CursorDwords + 61u) & mask] != BC250_PM4_PACKET2_NOP ||
        RingBuffer[(CursorDwords + 62u) & mask] != BC250_PM4_PACKET2_NOP ||
        RingBuffer[(CursorDwords + 63u) & mask] != BC250_PM4_PACKET2_NOP) {
        return BC250_GFX_RING_TXN_READBACK_MISMATCH;
    }

    return BC250_GFX_RING_TXN_OK;
}

BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmit3DDraw(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_GFX_RING_PLAN *RingPlan,
    const BC250_GFX_RING_TXN_EVIDENCE *ApplyEvidence,
    const BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN *SubmitPlan,
    BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE *SubmitEvidence)
{
    unsigned char *raw;
    uint32_t index;
    uint32_t initialWptr = 0u;
    uint32_t cursor;
    BC250_GFX_SH_REG_EMISSION_EVIDENCE shEv;
    BC250_GFX_SH_REG_STATUS shStatus;
    BC250_PM4_STATUS pm4Status;
    BC250_GFX_VIEWPORT_SCISSOR_PACKET vpPacket;
    BC250_GFX_VPORT_STATUS vpStatus;
    BC250_GFX_PRIMITIVE_PACKET primPacket;
    BC250_GFX_PRIM_STATUS primStatus;
    BC250_GFX_DRAW_PACKET drawPacket;
    BC250_GFX_DRAW_STATUS drawStatus;
    BC250_GFX_RING_TXN_STATUS verifyStatus;
    uint32_t maxPolls;
    uint32_t stallUs;
    uint32_t ringBytes;
    uint32_t targetWptrBytes;
    uint32_t iter;
    uint32_t stage = 0u;
    BC250_TRIPOD_VERDICT verdict = BC250_TRIPOD_INDETERMINATE;
    BC250_TRIPOD_RPTR_SOURCE rptrSource = BC250_TRIPOD_RPTR_SOURCE_NONE;

    if (SubmitEvidence != NULL) {
        raw = (unsigned char *)SubmitEvidence;
        for (index = 0u; index < (uint32_t)sizeof(*SubmitEvidence); ++index) {
            raw[index] = 0u;
        }
    }

    if (!Bc250GfxTxnInterfaceUsable(Interface) || RingPlan == NULL ||
        ApplyEvidence == NULL || SubmitPlan == NULL || SubmitEvidence == NULL) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (ApplyEvidence->RingActivatePassed != 1u || ApplyEvidence->ApplyReadbackPassed != 1u) {
        return BC250_GFX_RING_TXN_NOT_ACTIVE;
    }

    if (SubmitPlan->RingCpuAddress == NULL || SubmitPlan->FenceCpuAddress == NULL ||
        SubmitPlan->VsRegs == NULL || SubmitPlan->PsRegs == NULL ||
        SubmitPlan->RingDwords < BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS ||
        !Bc250IsPowerOfTwo32(SubmitPlan->RingDwords)) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (SubmitPlan->ColorTargetBaseGpuVa == 0u || (SubmitPlan->ColorTargetBaseGpuVa & 0xFFull) != 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (SubmitPlan->ColorTargetWidth == 0u || SubmitPlan->ColorTargetHeight == 0u ||
        SubmitPlan->ColorTargetPitchBytes == 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (SubmitPlan->VertexCount == 0u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    if (SubmitPlan->IsolationStage == BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        stage = BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT;
    } else if (SubmitPlan->IsolationStage != 0u) {
        stage = SubmitPlan->IsolationStage;
    } else if (SubmitPlan->SkipDraw != 0u) {
        stage = 3u;
    } else {
        stage = 4u;
    }
    if (stage > 5u) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /* Valida parametros do pipeline antes de qualquer alteracao no anel (Lei 4) */
    vpStatus = Bc250GfxComputeViewportScissor(&SubmitPlan->Viewport, &SubmitPlan->Scissor, &vpPacket);
    if (vpStatus != BC250_GFX_VPORT_OK) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    primStatus = Bc250GfxComputePrimitiveAssembler(SubmitPlan->PrimitiveTopology, SubmitPlan->NumInstances, &primPacket);
    if (primStatus != BC250_GFX_PRIM_OK) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    drawStatus = Bc250GfxComputeDrawInitiator(SubmitPlan->VertexCount, SubmitPlan->DrawInitiator, &drawPacket);
    if (drawStatus != BC250_GFX_DRAW_OK) {
        return BC250_GFX_RING_TXN_BAD_ARGUMENT;
    }

    /* 0. Limpa a memoria de writeback de RPTR e shadow de WPTR para impedir falso positivo residual (Lei 2) */
    if (SubmitPlan->RptrWbCpuAddress != NULL) {
        *(volatile uint32_t *)SubmitPlan->RptrWbCpuAddress = 0u;
    }
    if (SubmitPlan->WptrPollCpuAddress != NULL) {
        *SubmitPlan->WptrPollCpuAddress = 0u;
    }

    /* 1. Injeta poison na fence com readback proof antes de qualquer escrita no anel (Lei 2) */
    *SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;
    SubmitEvidence->FencePoisonPlanted = SubmitPlan->FencePoisonValue;
    SubmitEvidence->FencePoisonReadback = *SubmitPlan->FenceCpuAddress;
    if (SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue) {
        return BC250_GFX_RING_TXN_FENCE_POISON_FAILED;
    }

    /* 2. Le WPTR inicial do registrador de hardware */
    if (!Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR, &initialWptr)) {
        return BC250_GFX_RING_TXN_PREFLIGHT_FAILED;
    }
    SubmitEvidence->InitialWptrDwords = initialWptr;
    cursor = initialWptr;

    /* 3. Bloco 1: Shaders VS + PS (12 DW) */
    shStatus = Bc250GfxShRegEmitPipeline(SubmitPlan->RingCpuAddress,
                                         SubmitPlan->RingDwords,
                                         &cursor,
                                         SubmitPlan->VsRegs,
                                         SubmitPlan->PsRegs,
                                         &shEv);
    if (shStatus != BC250_GFX_SH_REG_OK) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    /* 4. Bloco 2: Color Target 0 (8 DW) */
    if (stage >= 1u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        pm4Status = Bc250Pm4EmitColorTarget0(SubmitPlan->RingCpuAddress,
                                             SubmitPlan->RingDwords,
                                             &cursor,
                                             SubmitPlan->ColorTargetBaseGpuVa,
                                             SubmitPlan->ColorTargetWidth,
                                             SubmitPlan->ColorTargetHeight,
                                             SubmitPlan->ColorTargetPitchBytes,
                                             SubmitPlan->ColorTargetFormatIndex,
                                             SubmitPlan->ColorTargetCompSwap);
        if (pm4Status != BC250_PM4_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_COLOR_DWORDS; ++nIdx) {
            SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            cursor++;
        }
    }

    /* 5. Bloco 3: Target Mask (4 DW: SET_CONTEXT_REG 0x008E + NOP) */
    if (stage >= 1u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        uint32_t targetMaskVal = 0x0000000Fu; /* Habilita RT0 RGBA */
        pm4Status = Bc250Pm4EmitSetContextReg(SubmitPlan->RingCpuAddress,
                                              SubmitPlan->RingDwords,
                                              &cursor,
                                              BC250_PM4_REG_CB_TARGET_MASK,
                                              &targetMaskVal,
                                              1u);
        if (pm4Status != BC250_PM4_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
        pm4Status = Bc250Pm4EmitPacket2Nop(SubmitPlan->RingCpuAddress,
                                           SubmitPlan->RingDwords,
                                           &cursor);
        if (pm4Status != BC250_PM4_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_TARGET_MASK_DWORDS; ++nIdx) {
            SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            cursor++;
        }
    }

    /* 6. Bloco 4: Viewport & Scissor (20 DW) */
    if (stage >= 2u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        vpStatus = Bc250GfxEmitViewportScissorStream(SubmitPlan->RingCpuAddress,
                                                     SubmitPlan->RingDwords,
                                                     &cursor,
                                                     &vpPacket);
        if (vpStatus != BC250_GFX_VPORT_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_VPORT_DWORDS; ++nIdx) {
            SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            cursor++;
        }
    }

    /* 7. Bloco 5: Primitive Assembler (8 DW) */
    if (stage >= 3u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        primStatus = Bc250GfxEmitPrimitiveAssemblerStream(SubmitPlan->RingCpuAddress,
                                                          SubmitPlan->RingDwords,
                                                          &cursor,
                                                          &primPacket);
        if (primStatus != BC250_GFX_PRIM_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_PRIM_DWORDS; ++nIdx) {
            SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            cursor++;
        }
    }

    /* 8. Bloco 6: Draw Initiator (4 DW) ou 4 NOPs se SkipDraw / Stage < 4 / Stage == 5 */
    if (stage >= 4u && stage != BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT) {
        drawStatus = Bc250GfxEmitDrawInitiatorStream(SubmitPlan->RingCpuAddress,
                                                    SubmitPlan->RingDwords,
                                                    &cursor,
                                                    &drawPacket);
        if (drawStatus != BC250_GFX_DRAW_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }
    } else {
        uint32_t nIdx;
        for (nIdx = 0u; nIdx < BC250_GFX_3D_DRAW_INIT_DWORDS; ++nIdx) {
            SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
            cursor++;
        }
    }

    /* 9. Bloco 7: Fence fisica WRITE_DATA (DST_SEL=5 Memory Direct MC) sobre poison em VRAM (8 DW) */
    {
        uint32_t wdControl = BC250_PM4_WD_CONFIRM | BC250_PM4_WD_DST_MEM; /* 0x00100500u */
        pm4Status = Bc250Pm4EmitWriteData(SubmitPlan->RingCpuAddress,
                                          SubmitPlan->RingDwords,
                                          &cursor,
                                          wdControl,
                                          SubmitPlan->FenceGpuVa,
                                          &SubmitPlan->FenceExpectedValue,
                                          1u);
        if (pm4Status != BC250_PM4_OK) {
            return BC250_GFX_RING_TXN_WRITE_FAILED;
        }

        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
        SubmitPlan->RingCpuAddress[cursor & (SubmitPlan->RingDwords - 1u)] = BC250_PM4_PACKET2_NOP;
        cursor++;
    }

    if ((cursor - initialWptr) != BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    SubmitEvidence->TargetWptrDwords = cursor;
    SubmitEvidence->EmittedDwords = cursor - initialWptr;

    /* 10. Verificacao estrita do stream emitido antes de disparar o CP no silicio */
    verifyStatus = Bc250GfxVerify3DDrawStream(SubmitPlan->RingCpuAddress,
                                             SubmitPlan->RingDwords,
                                             initialWptr,
                                             SubmitPlan);
    if (verifyStatus != BC250_GFX_RING_TXN_OK) {
        return verifyStatus;
    }

    /* 11. Atualiza shadow de WPTR em memoria (se configurado) */
    if (SubmitPlan->WptrPollCpuAddress != NULL) {
        *SubmitPlan->WptrPollCpuAddress = cursor;
    }

    /* 12. Dispara o anel avancando CP_RB0_WPTR no hardware */
    if (!Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR, cursor) ||
        !Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_CP_RB0_WPTR_HI, 0u)) {
        return BC250_GFX_RING_TXN_WRITE_FAILED;
    }

    /* 13. Laco de polling e afericao do tripe da Lei 2 */
    maxPolls = (SubmitPlan->MaxPollIterations > 0u) ? SubmitPlan->MaxPollIterations : 2000u;
    stallUs = (SubmitPlan->StallMicroseconds > 0u) ? SubmitPlan->StallMicroseconds : 10u;
    ringBytes = SubmitPlan->RingDwords * (uint32_t)sizeof(uint32_t);
    targetWptrBytes = cursor * (uint32_t)sizeof(uint32_t);

    for (iter = 0u; iter < maxPolls; ++iter) {
        uint32_t rptrMmio = 0u;
        uint32_t rptrWb = 0u;
        uint32_t fenceObs;

        /* Dreno e coerencia HDP (PCIe) para que a leitura de CPU sobre VRAM
         * enxergue os dados gravados pelo motor CP da GPU na VRAM (Lei 2) */
        (void)Bc250GfxTxnWrite(Interface, NULL, BC250_GFX_REG_HDP_MEM_COHERENCY_FLUSH_CNTL, 1u);

        fenceObs = *(volatile const uint32_t *)SubmitPlan->FenceCpuAddress;

        if (!Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_RB0_RPTR, &rptrMmio)) {
            return BC250_GFX_RING_TXN_READBACK_MISMATCH;
        }

        if (SubmitPlan->RptrWbCpuAddress != NULL) {
            rptrWb = *(volatile const uint32_t *)SubmitPlan->RptrWbCpuAddress;
        }

        SubmitEvidence->RptrObservedMmio = rptrMmio;
        SubmitEvidence->RptrObservedWb = rptrWb;
        SubmitEvidence->FenceObserved = fenceObs;

        verdict = Bc250GfxRingClassifyTripod(
            ringBytes,
            targetWptrBytes,
            rptrWb * (uint32_t)sizeof(uint32_t),
            rptrMmio * (uint32_t)sizeof(uint32_t),
            SubmitPlan->FenceExpectedValue,
            fenceObs,
            0u,
            &rptrSource);

        SubmitEvidence->TripodVerdict = verdict;
        SubmitEvidence->RptrSource = rptrSource;

        if (verdict == BC250_TRIPOD_PROVEN) {
            SubmitEvidence->PollsCompleted = iter + 1u;
            SubmitEvidence->SubmitCompleted = 1u;
            (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_GRBM_STATUS, &SubmitEvidence->GrbmStatus);
            (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_STAT, &SubmitEvidence->CpStat);
            (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_PFP_INSTR_PNTR, &SubmitEvidence->PfpPntr);
            (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_ME_INSTR_PNTR, &SubmitEvidence->MePntr);
            return BC250_GFX_RING_TXN_OK;
        }

        if (Interface->Stall != NULL && stallUs > 0u) {
            Interface->Stall(Interface->Context, stallUs);
        }
    }

    (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_GRBM_STATUS, &SubmitEvidence->GrbmStatus);
    (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_STAT, &SubmitEvidence->CpStat);
    (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_PFP_INSTR_PNTR, &SubmitEvidence->PfpPntr);
    (void)Bc250GfxTxnRead(Interface, NULL, BC250_GFX_REG_CP_ME_INSTR_PNTR, &SubmitEvidence->MePntr);

    SubmitEvidence->PollsCompleted = maxPolls;
    SubmitEvidence->SubmitCompleted = 0u;
    return BC250_GFX_RING_TXN_SUBMIT_TIMEOUT;
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
    case BC250_GFX_RING_TXN_NOT_ACTIVE:         return "NOT_ACTIVE";
    case BC250_GFX_RING_TXN_FENCE_POISON_FAILED: return "FENCE_POISON_FAILED";
    case BC250_GFX_RING_TXN_SUBMIT_TIMEOUT:     return "SUBMIT_TIMEOUT";
    case BC250_GFX_RING_TXN_TRIPOD_FAILED:      return "TRIPOD_FAILED";
    default:                                    return "DESCONHECIDO";
    }
}


/*
 * =========================================================================
 * SONDA DE ONDA. Ver o bloco longo em bc250_gfx_ring_transaction.h.
 * =========================================================================
 * Toda a aritmetica e a ordem de operacoes vivem aqui, atras da interface de
 * ponteiros de funcao, para que a suite offline exercite a sequencia INTEIRA
 * -- inclusive a restauracao do seletor -- contra um MMIO falso, sem placa.
 */

uint32_t Bc250WaveProbeEncodeIndIndex(uint32_t IxAddress, uint32_t WaveId)
{
    /* SQ_IND_INDEX: INDEX em 31:16, WAVE_ID em 4:0. WORKITEM_ID e AUTO_INCR
     * ficam ZERADOS -- nao varremos work-items e nao queremos o ponteiro
     * andando sozinho entre leituras. */
    return ((IxAddress & 0xFFFFu) << 16) | (WaveId & 0x1Fu);
}

uint32_t Bc250WaveProbeEncodeGfxIndex(
    uint32_t SeIndex, uint32_t SaIndex, uint32_t InstanceIndex)
{
    /*
     * GRBM_GFX_INDEX com os TRES bits de broadcast em ZERO -- e' isso que
     * torna a selecao efetiva. Deixar qualquer um ligado faz a escrita valer
     * para todos e a leitura devolver um agregado sem sentido.
     * INSTANCE_INDEX 7:0 | SA_INDEX 15:8 | SE_INDEX 23:16
     */
    return ((SeIndex & 0xFFu) << 16) |
           ((SaIndex & 0xFFu) << 8) |
           (InstanceIndex & 0xFFu);
}

uint32_t Bc250WaveProbeStatusIsValid(uint32_t WaveStatus)
{
    /*
     * Um slot vazio devolve 0 e um barramento mudo tambem devolve 0. Nao ha
     * como separar os dois por esta leitura, entao "onda encontrada" exige
     * status NAO-ZERO e o log diz que zero e' INDISTINGUIVEL de slot vazio.
     * Nao se conta slot vazio como onda.
     */
    /* gc_10_1_0_sh_mask.h SQ_WAVE_STATUS__VALID_MASK; poison is not a wave. */
    return (WaveStatus != 0xBEBEBEEFu && WaveStatus != 0xFFFFFFFFu &&
            (WaveStatus & 0x00010000u) != 0u) ? 1u : 0u;
}

BC250_WAVE_PROBE_STATUS Bc250WaveProbeValidatePlan(const BC250_WAVE_PROBE_PLAN *Plan)
{
    if (Plan == NULL) {
        return BC250_WAVE_PROBE_BAD_ARGUMENT;
    }
    if (Plan->WavesInFlight == 0u) {
        /* Sem onda em voo a sonda nao escreve NADA. Fail-closed. */
        return BC250_WAVE_PROBE_NO_WAVES;
    }
    if (Plan->SlotCount == 0u || Plan->SlotCount > BC250_WAVE_PROBE_MAX_SLOTS) {
        return BC250_WAVE_PROBE_PLAN_REJECTED;
    }
    if (Plan->ReadBudget == 0u || Plan->ReadBudget > BC250_WAVE_PROBE_MAX_READS) {
        return BC250_WAVE_PROBE_PLAN_REJECTED;
    }
    /*
     * Cada slot custa ate 6 leituras (STATUS, PC_LO, PC_HI, IB_STS, INST_DW0,
     * TRAPSTS). Plano que nao cabe no proprio orcamento e' rejeitado ANTES de
     * tocar no silicio -- e' a guarda contra o laco longo em DISPATCH_LEVEL
     * que produziu o 0x133 em 10/09 12:16.
     */
    if (Plan->SlotCount > Plan->ReadBudget) {
        return BC250_WAVE_PROBE_PLAN_REJECTED;
    }
    return BC250_WAVE_PROBE_OK;
}

static uint32_t Bc250WaveProbeReadIndirect(
    const BC250_GFX_RING_INTERFACE *Interface,
    uint32_t IxAddress,
    uint32_t WaveId,
    uint32_t *Value,
    uint32_t ReadBudget,
    BC250_WAVE_PROBE_EVIDENCE *Evidence)
{
    uint32_t raw = 0u;

    *Value = 0u;
    if (Evidence->ReadsIssued >= ReadBudget) return 0u;
    if (Interface->Write32(Interface->Context, BC250_GFX_REG_SQ_IND_INDEX,
                           Bc250WaveProbeEncodeIndIndex(IxAddress, WaveId)) == 0u) {
        Evidence->ReadRefused = 1u;
        return 0u;
    }
    Evidence->ReadsIssued++; /* attempts consume budget, including refused reads */
    if (Interface->Read32(Interface->Context, BC250_GFX_REG_SQ_IND_DATA, &raw) == 0u) {
        Evidence->ReadRefused = 1u;
        return 0u;
    }
    if (raw == 0xBEBEBEEFu || raw == 0xFFFFFFFFu) {
        Evidence->PoisonReads++;
        return 0u;
    }
    *Value = raw;
    return 1u;
}

BC250_WAVE_PROBE_STATUS Bc250WaveProbeRun(
    const BC250_GFX_RING_INTERFACE *Interface,
    const BC250_WAVE_PROBE_PLAN *Plan,
    BC250_WAVE_PROBE_EVIDENCE *Evidence)
{
    BC250_WAVE_PROBE_STATUS planStatus;
    BC250_WAVE_PROBE_STATUS result = BC250_WAVE_PROBE_OK;
    uint32_t i;

    if (Interface == NULL || Interface->Read32 == NULL ||
        Interface->Write32 == NULL || Evidence == NULL) {
        return BC250_WAVE_PROBE_BAD_ARGUMENT;
    }

    Evidence->SlotsVisited = 0u;
    Evidence->ReadsIssued = 0u;
    Evidence->WavesFound = 0u;
    Evidence->SelectorRestored = 0u;
    Evidence->SelectorReadback = 0u;
    Evidence->ReadRefused = 0u;
    Evidence->PoisonReads = 0u;
    for (i = 0u; i < BC250_WAVE_PROBE_MAX_REPORTED; ++i) {
        Evidence->Found[i].SeIndex = 0u;
        Evidence->Found[i].SaIndex = 0u;
        Evidence->Found[i].InstanceIndex = 0u;
        Evidence->Found[i].WaveId = 0u;
        Evidence->Found[i].Status = 0u;
        Evidence->Found[i].PcLo = 0u;
        Evidence->Found[i].PcHi = 0u;
        Evidence->Found[i].IbSts = 0u;
        Evidence->Found[i].InstDw0 = 0u;
        Evidence->Found[i].TrapSts = 0u;
        Evidence->Found[i].ValidMask = 0u;
    }

    planStatus = Bc250WaveProbeValidatePlan(Plan);
    if (planStatus != BC250_WAVE_PROBE_OK) {
        /* Nenhuma escrita aconteceu: o seletor nunca saiu de broadcast. */
        return planStatus;
    }

    for (i = 0u; i < Plan->SlotCount; ++i) {
        const BC250_WAVE_PROBE_SLOT *slot = &Plan->Slots[i];
        uint32_t waveStatus = 0u;

        if (Evidence->ReadsIssued >= Plan->ReadBudget) {
            break;
        }
        if (Evidence->WavesFound >= BC250_WAVE_PROBE_MAX_REPORTED) {
            break;
        }

        if (Interface->Write32(Interface->Context, BC250_GFX_REG_GRBM_GFX_INDEX,
                Bc250WaveProbeEncodeGfxIndex(slot->SeIndex, slot->SaIndex,
                                             slot->InstanceIndex)) == 0u) {
            result = BC250_WAVE_PROBE_SELECT_FAILED;
            break;
        }
        Evidence->SlotsVisited++;

        if (Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_STATUS,
                                       slot->WaveId, &waveStatus, Plan->ReadBudget, Evidence) == 0u) {
            continue;
        }
        if (Bc250WaveProbeStatusIsValid(waveStatus) == 0u) {
            continue;
        }

        {
            BC250_WAVE_PROBE_FOUND *found = &Evidence->Found[Evidence->WavesFound];
            found->SeIndex = slot->SeIndex;
            found->SaIndex = slot->SaIndex;
            found->InstanceIndex = slot->InstanceIndex;
            found->WaveId = slot->WaveId;
            found->Status = waveStatus;
            found->ValidMask = 1u;
            found->ValidMask |= Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_PC_LO,
                slot->WaveId, &found->PcLo, Plan->ReadBudget, Evidence) << 1;
            found->ValidMask |= Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_PC_HI,
                slot->WaveId, &found->PcHi, Plan->ReadBudget, Evidence) << 2;
            found->ValidMask |= Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_IB_STS,
                slot->WaveId, &found->IbSts, Plan->ReadBudget, Evidence) << 3;
            found->ValidMask |= Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_INST_DW0,
                slot->WaveId, &found->InstDw0, Plan->ReadBudget, Evidence) << 4;
            found->ValidMask |= Bc250WaveProbeReadIndirect(Interface, BC250_IX_SQ_WAVE_TRAPSTS,
                slot->WaveId, &found->TrapSts, Plan->ReadBudget, Evidence) << 5;
            Evidence->WavesFound++;
        }
    }

    /*
     * RESTAURACAO DO SELETOR -- incondicional, inclusive quando a varredura
     * abortou. Sair daqui fora de broadcast redireciona TODO acesso posterior
     * a um unico SE, e o proximo acesso e' o soft-reset do quiesce.
     */
    if (Interface->Write32(Interface->Context, BC250_GFX_REG_GRBM_GFX_INDEX,
                           BC250_GRBM_GFX_INDEX_BROADCAST_ALL) != 0u &&
        Interface->Read32(Interface->Context, BC250_GFX_REG_GRBM_GFX_INDEX,
                          &Evidence->SelectorReadback) != 0u &&
        Evidence->SelectorReadback == BC250_GRBM_GFX_INDEX_BROADCAST_ALL) {
        Evidence->SelectorRestored = 1u;
    } else {
        Evidence->SelectorRestored = 0u;
        return BC250_WAVE_PROBE_SELECTOR_STUCK;
    }

    return result;
}

const char *Bc250WaveProbeStatusName(BC250_WAVE_PROBE_STATUS Status)
{
    switch (Status) {
    case BC250_WAVE_PROBE_OK: return "ok";
    case BC250_WAVE_PROBE_BAD_ARGUMENT: return "bad_argument";
    case BC250_WAVE_PROBE_NO_WAVES: return "no_waves";
    case BC250_WAVE_PROBE_PLAN_REJECTED: return "plan_rejected";
    case BC250_WAVE_PROBE_SELECT_FAILED: return "select_failed";
    case BC250_WAVE_PROBE_SELECTOR_STUCK: return "selector_stuck";
    default: return "unknown";
    }
}
