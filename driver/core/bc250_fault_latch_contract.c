#include "bc250_fault_latch_contract.h"

/*
 * Dreno do latch de fault. Ver o bloco longo no header para a medicao que
 * justifica cada decisao. Aqui so' a mecanica.
 */

static void Bc250FaultLatchZeroEvidence(BC250_FAULT_LATCH_EVIDENCE *E)
{
    uint32_t i;

    /*
     * Zeragem EXPLICITA, campo a campo. memset sobre esta estrutura e'
     * reemitido pelo /O2 como chamada real a memset, que NAO existe no link
     * offline das suites -- armadilha ja paga tres vezes neste projeto.
     */
    E->CntlBefore = 0u;
    E->CntlReadback = 0u;
    E->RestorePassed = 0u;
    E->Drained = 0u;
    E->ClearAcked = 0u;
    E->ClearIneficaz = 0u;
    E->HitCeiling = 0u;
    E->MoreFaultsSeen = 0u;
    for (i = 0u; i < BC250_FAULT_LATCH_MAX_DRAIN; ++i) {
        E->Entries[i].Status = 0u;
        E->Entries[i].AddrLo = 0u;
        E->Entries[i].AddrHi = 0u;
    }
}

BC250_FAULT_LATCH_STATUS Bc250FaultLatchDrain(
    const BC250_FAULT_LATCH_IO *Io,
    BC250_FAULT_LATCH_EVIDENCE *Evidence)
{
    uint32_t cntlBefore = 0u;
    uint32_t readback = 0u;
    uint32_t anterior = 0u;
    uint32_t temAnterior = 0u;
    uint32_t i;

    if (Evidence == NULL) return BC250_FAULT_LATCH_BAD_ARGUMENT;
    Bc250FaultLatchZeroEvidence(Evidence);
    if (Io == NULL || Io->Read32 == NULL || Io->Write32 == NULL) {
        return BC250_FAULT_LATCH_BAD_ARGUMENT;
    }

    if (Io->Read32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL,
            &cntlBefore) == 0u) {
        /*
         * Sem o CNTL nao se pulsa o CLEAR sem destruir os outros bits. Falhar
         * aqui e' correto: escrever um valor inventado no registrador que
         * governa as sete classes de fault seria bem pior que nao drenar.
         */
        return BC250_FAULT_LATCH_CNTL_UNREADABLE;
    }
    Evidence->CntlBefore = cntlBefore;

    for (i = 0u; i < BC250_FAULT_LATCH_MAX_DRAIN; ++i) {
        uint32_t status = 0u;
        uint32_t addrLo = 0u;
        uint32_t addrHi = 0u;

        if (Io->Read32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_STATUS,
                &status) == 0u) {
            break;
        }
        if (status == 0u) {
            /* Latch vazio: ou nunca houve fault, ou o CLEAR anterior pegou. */
            if (i > 0u) Evidence->ClearAcked = 1u;
            break;
        }
        /*
         * REPETICAO NAO E' FAULT NOVO. Se o CLEAR nao funcionar, o mesmo
         * Status voltaria a cada volta e o dreno relataria N faults que sao
         * UM SO'. Contar repeticao como fault novo seria falso sucesso.
         */
        if (temAnterior != 0u && status == anterior) {
            Evidence->ClearIneficaz = 1u;
            break;
        }

        (void)Io->Read32(Io->Context,
            BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_LO32, &addrLo);
        (void)Io->Read32(Io->Context,
            BC250_REG_GCVM_L2_PROTECTION_FAULT_ADDR_HI32, &addrHi);

        Evidence->Entries[Evidence->Drained].Status = status;
        Evidence->Entries[Evidence->Drained].AddrLo = addrLo;
        Evidence->Entries[Evidence->Drained].AddrHi = addrHi;
        ++Evidence->Drained;
        if ((status & 0x00000001u) != 0u) Evidence->MoreFaultsSeen = 1u;

        anterior = status;
        temAnterior = 1u;

        /*
         * Pulso do CLEAR preservando todo o resto (Lei 6): liga o bit 0 e o
         * desliga em seguida. Os demais bits saem do valor lido, nunca de
         * constante.
         */
        (void)Io->Write32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL,
            cntlBefore | BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT);
        (void)Io->Write32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL,
            cntlBefore & ~BC250_FAULT_CNTL_CLEAR_STATUS_ADDR_BIT);
    }
    if (i >= BC250_FAULT_LATCH_MAX_DRAIN) Evidence->HitCeiling = 1u;

    /* RESTAURACAO E READBACK, sempre -- inclusive se nada foi drenado. */
    (void)Io->Write32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL,
        cntlBefore);
    if (Io->Read32(Io->Context, BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL,
            &readback) == 0u) {
        return BC250_FAULT_LATCH_RESTORE_FAILED;
    }
    Evidence->CntlReadback = readback;
    if (readback != cntlBefore) return BC250_FAULT_LATCH_RESTORE_FAILED;
    Evidence->RestorePassed = 1u;
    return BC250_FAULT_LATCH_OK;
}
