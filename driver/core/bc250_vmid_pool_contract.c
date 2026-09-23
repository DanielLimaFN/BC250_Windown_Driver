#include "bc250_vmid_pool_contract.h"

/*
 * Pool de VMID por contexto. Ver o bloco longo no header: aritmetica pura,
 * sem hardware, sem lock e sem alocacao -- testavel offline por inteiro.
 *
 * A serializacao e responsabilidade do chamador. No miniport ela e feita pelo
 * VmidPoolLock dedicado; ObjectLock nao cobre SubmitCommandVirtual, DestroyContext
 * e o snapshot de TDR ao mesmo tempo. Este contrato nao trava nada de proposito,
 * para poder ser exercitado pela suite.
 */

uint32_t Bc250VmidIsReserved(uint32_t Vmid)
{
    if (Vmid >= BC250_VMID_COUNT) return 1u;   /* fora da faixa conta como reservado */
    return ((BC250_VMID_RESERVED_MASK >> Vmid) & 1u);
}

void Bc250VmidPoolInitialize(BC250_VMID_POOL *Pool)
{
    uint32_t i;

    if (Pool == NULL) return;
    /*
     * Zeragem EXPLICITA, campo a campo. memset sobre esta estrutura e'
     * reemitido pelo /O2 como chamada real a memset, que NAO existe no link
     * offline das suites -- armadilha ja paga duas vezes neste projeto.
     */
    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        Pool->Entries[i].Context = NULL;
        Pool->Entries[i].Vmid = i;
        Pool->Entries[i].RootPhysical = 0ull;
        Pool->Entries[i].InstalledRootPhysical = 0ull;
        Pool->Entries[i].InstallState = BC250_VMID_POOL_INSTALL_NEVER;
        Pool->Entries[i].LastInstallStatus = BC250_GCVM_VMID8_BAD_ARGUMENT;
        Pool->Entries[i].Generation = 0u;
        Pool->RetainedRoots[i].RootPhysical = 0ull;
        Pool->RetainedRoots[i].Context = NULL;
        Pool->RetainedRoots[i].Generation = 0u;
    }
    Pool->InUseMask = 0u;
    Pool->AllocCount = 0u;
    Pool->FreeCount = 0u;
    Pool->ExhaustedCount = 0u;
    Pool->NextGeneration = 1u;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolLookup(
    const BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *Vmid)
{
    uint32_t i;

    if (Vmid != NULL) *Vmid = BC250_VMID_COUNT;
    if (Pool == NULL || Context == NULL) return BC250_VMID_POOL_BAD_ARGUMENT;

    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        if (Pool->Entries[i].Context == Context) {
            if (Vmid != NULL) *Vmid = Pool->Entries[i].Vmid;
            return BC250_VMID_POOL_OK;
        }
    }
    return BC250_VMID_POOL_NOT_FOUND;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolAcquire(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *Vmid,
    uint32_t *WasAlreadyAssigned)
{
    uint32_t i;

    if (Vmid != NULL) *Vmid = BC250_VMID_COUNT;
    if (WasAlreadyAssigned != NULL) *WasAlreadyAssigned = 0u;
    if (Pool == NULL || Context == NULL || Vmid == NULL) {
        return BC250_VMID_POOL_BAD_ARGUMENT;
    }

    /* Reentrante: o mesmo contexto tem de receber sempre o MESMO VMID. */
    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        if (Pool->Entries[i].Context == Context) {
            *Vmid = Pool->Entries[i].Vmid;
            if (WasAlreadyAssigned != NULL) *WasAlreadyAssigned = 1u;
            return BC250_VMID_POOL_OK;
        }
    }

    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        /*
         * Reservado NUNCA entra no pool, nem quando tudo mais acabou. O VMID 0
         * e' do microengine MEC (tradutor da propria fila), 7 do usuario, 8 da
         * paginacao e 9 do compute -- todos medidos, nao supostos.
         */
        if (Bc250VmidIsReserved(i) != 0u) continue;
        if (Pool->Entries[i].Context != NULL) continue;

        Pool->Entries[i].Context = Context;
        Pool->Entries[i].Vmid = i;
        Pool->Entries[i].RootPhysical = 0ull;
        Pool->Entries[i].InstalledRootPhysical = 0ull;
        Pool->Entries[i].InstallState = BC250_VMID_POOL_INSTALL_NEVER;
        Pool->Entries[i].LastInstallStatus = BC250_GCVM_VMID8_BAD_ARGUMENT;
        Pool->Entries[i].Generation = Pool->NextGeneration++;
        Pool->InUseMask |= (1u << i);
        ++Pool->AllocCount;
        *Vmid = i;
        return BC250_VMID_POOL_OK;
    }

    /*
     * Exaustao e' FALHA, nao degradacao. Cair de volta no VMID compartilhado
     * devolveria exatamente o defeito que esta faixa existe para consertar,
     * e o log diria "funcionou".
     */
    ++Pool->ExhaustedCount;
    return BC250_VMID_POOL_EXHAUSTED;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolRelease(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t *ReleasedVmid)
{
    uint32_t i;

    if (ReleasedVmid != NULL) *ReleasedVmid = BC250_VMID_COUNT;
    if (Pool == NULL || Context == NULL) return BC250_VMID_POOL_BAD_ARGUMENT;

    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        if (Pool->Entries[i].Context != Context) continue;
        /*
         * Slot em transacao de instalacao NAO pode ser reciclado. Entre o
         * BeginInstall e o RecordInstall o MMIO e' escrito FORA do lock; se o
         * VMID fosse devolvido aqui, outro contexto poderia adquirir o mesmo
         * slot enquanto a instalacao anterior ainda o programa -- duas raizes
         * disputando um VMID, que e' exatamente o defeito que este pool
         * existe para eliminar.
         *
         * ReleasedVmid fica em BC250_VMID_COUNT de proposito: nada foi
         * devolvido, e devolver um numero aqui faria o chamador registrar uma
         * reciclagem que nao aconteceu.
         */
        if (Pool->Entries[i].InstallState == BC250_VMID_POOL_INSTALLING) {
            return BC250_VMID_POOL_BUSY;
        }
        if (ReleasedVmid != NULL) *ReleasedVmid = Pool->Entries[i].Vmid;
        Pool->Entries[i].Context = NULL;
        Pool->Entries[i].RootPhysical = 0ull;
        Pool->Entries[i].InstalledRootPhysical = 0ull;
        Pool->Entries[i].InstallState = BC250_VMID_POOL_INSTALL_NEVER;
        Pool->Entries[i].LastInstallStatus = BC250_GCVM_VMID8_BAD_ARGUMENT;
        Pool->InUseMask &= ~(1u << i);
        ++Pool->FreeCount;
        return BC250_VMID_POOL_OK;
    }
    return BC250_VMID_POOL_NOT_FOUND;
}

uint64_t Bc250VmidPoolInstalledRootForVmid(
    const BC250_VMID_POOL *Pool,
    uint32_t Vmid)
{
    const BC250_VMID_POOL_ENTRY *entry;

    if (Pool == NULL || Vmid >= BC250_VMID_COUNT) return 0ull;
    entry = &Pool->Entries[Vmid];
    /*
     * Somente READY vale. INSTALLING significa que a escrita ainda esta em
     * curso e FAILED que ela nao valeu -- devolver a raiz nesses estados
     * faria a caminhada do fault usar um endereco que o silicio talvez nunca
     * tenha aceitado, e o resultado pareceria medicao.
     */
    if (entry->InstallState != BC250_VMID_POOL_INSTALL_READY) return 0ull;
    return entry->InstalledRootPhysical;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolRootForFaultWalk(
    const BC250_VMID_POOL *Pool,
    uint32_t Vmid,
    BC250_VMID_POOL_WALK_ROOT *WalkRoot)
{
    const BC250_VMID_POOL_ENTRY *entry;
    const BC250_VMID_POOL_RETAINED_ROOT *retained;

    if (WalkRoot != NULL) {
        WalkRoot->RootPhysical = 0ull;
        WalkRoot->Context = NULL;
        WalkRoot->Generation = 0u;
        WalkRoot->Source = BC250_VMID_POOL_WALK_ROOT_NONE;
    }
    if (Pool == NULL || WalkRoot == NULL || Vmid >= BC250_VMID_COUNT ||
        Bc250VmidIsReserved(Vmid) != 0u) {
        return BC250_VMID_POOL_BAD_ARGUMENT;
    }

    entry = &Pool->Entries[Vmid];
    if (entry->InstallState == BC250_VMID_POOL_INSTALL_READY &&
        entry->InstalledRootPhysical != 0ull) {
        WalkRoot->RootPhysical = entry->InstalledRootPhysical;
        WalkRoot->Context = entry->Context;
        WalkRoot->Generation = entry->Generation;
        WalkRoot->Source = BC250_VMID_POOL_WALK_ROOT_LIVE_READY;
        return BC250_VMID_POOL_OK;
    }

    retained = &Pool->RetainedRoots[Vmid];
    if (retained->RootPhysical != 0ull) {
        WalkRoot->RootPhysical = retained->RootPhysical;
        WalkRoot->Context = retained->Context;
        WalkRoot->Generation = retained->Generation;
        WalkRoot->Source = BC250_VMID_POOL_WALK_ROOT_RETAINED;
        return BC250_VMID_POOL_OK;
    }
    return BC250_VMID_POOL_NOT_FOUND;
}

/*
 * DX.12 -- QUEM PRECISA SER INVALIDADO QUANDO A TABELA MUDA.
 *
 * POR QUE EXISTE (achado por leitura de codigo apos a Corrida 58):
 *
 * Bc250ActiveFlushMappedGfxhubTlb invalidava apenas os VMIDs 8, 7 e 9 -- os
 * tres FIXOS que existiam quando ela foi escrita. Desde a Corrida 54 o
 * INDIRECT_BUFFER do anel grafico carrega o VMID DINAMICO do pool (na Corrida
 * 58 foram 1 a 4), e Bc250GcvmInstallVmid invalida o slot SO' na instalacao
 * inicial. Uma UpdatePageTable/FlushTlb posterior, portanto, nao invalidava
 * nenhum VMID do pool: a CPU passava a enxergar a folha nova enquanto a GPU
 * podia conservar a traducao velha.
 *
 * Isso casa com a medicao da Corrida 58 -- escrita em 0x2469A6000 falhando
 * com a janela de publicacao INALTERADA e a PTE contemporanea 0x458F72071
 * valida e R/W/X na visao da CPU --, mas CASAR NAO E' PROVAR. A relacao causal
 * continua HIPOTESE ate uma corrida com mascara, ACK e carga equivalente.
 *
 * ESCOPO: funcao PURA. Nao toca hardware, nao toma lock e nao muda estado. O
 * chamador copia a mascara sob o lock do pool e SOLTA o lock antes de qualquer
 * MMIO -- o flush por VMID pode gastar centenas de polls, e segurar um
 * spinlock durante isso em DISPATCH_LEVEL e' a classe do BugCheck 0x133 que
 * este projeto ja pagou em 10/09.
 *
 * O CRITERIO E' ESTREITO DE PROPOSITO. So' entra o slot que satisfaz as TRES
 * condicoes:
 *   - ocupado por um contexto vivo (Context != NULL);
 *   - InstallState == READY -- NEVER/INSTALLING/FAILED nunca entram, porque
 *     nenhum deles prova que o silicio aceitou a raiz;
 *   - InstalledRootPhysical != 0 -- READY sem raiz seria contradicao interna,
 *     e invalidar um VMID que nunca recebeu tabela gastaria polls a toa.
 * A mascara e' cruzada com BC250_VMID_POOL_MASK no fim, entao os reservados
 * 0/7/8/9 NUNCA saem daqui: eles tem politica propria e sao responsabilidade
 * do chamador, que os soma explicitamente.
 */
uint32_t Bc250VmidPoolReadyFlushMask(const BC250_VMID_POOL *Pool)
{
    uint32_t mask = 0u;
    uint32_t i;

    if (Pool == NULL) return 0u;
    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        const BC250_VMID_POOL_ENTRY *entry = &Pool->Entries[i];

        if (entry->Context == NULL) continue;
        if (entry->InstallState != BC250_VMID_POOL_INSTALL_READY) continue;
        if (entry->InstalledRootPhysical == 0ull) continue;
        if (entry->Vmid >= BC250_VMID_COUNT) continue;
        mask |= (1u << entry->Vmid);
    }
    return mask & BC250_VMID_POOL_MASK;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolNoteRoot(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint64_t RootPhysical)
{
    uint32_t i;

    if (Pool == NULL || Context == NULL) return BC250_VMID_POOL_BAD_ARGUMENT;
    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        if (Pool->Entries[i].Context == Context) {
            Pool->Entries[i].RootPhysical = RootPhysical;
            return BC250_VMID_POOL_OK;
        }
    }
    return BC250_VMID_POOL_NOT_FOUND;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolBeginInstall(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t Vmid,
    uint64_t RootPhysical,
    uint32_t *NeedsHardwareInstall)
{
    BC250_VMID_POOL_ENTRY *entry;

    if (NeedsHardwareInstall != NULL) *NeedsHardwareInstall = 0u;
    if (Pool == NULL || Context == NULL || NeedsHardwareInstall == NULL ||
        Vmid >= BC250_VMID_COUNT || Bc250VmidIsReserved(Vmid) != 0u ||
        RootPhysical == 0ull || (RootPhysical & 0xFFFull) != 0ull) {
        return BC250_VMID_POOL_BAD_ARGUMENT;
    }
    entry = &Pool->Entries[Vmid];
    if (entry->Context != Context || entry->Vmid != Vmid) {
        return BC250_VMID_POOL_NOT_FOUND;
    }
    if (entry->InstallState == BC250_VMID_POOL_INSTALLING) {
        return BC250_VMID_POOL_BUSY;
    }
    if (entry->InstallState == BC250_VMID_POOL_INSTALL_READY &&
        entry->InstalledRootPhysical == RootPhysical &&
        entry->LastInstallStatus == BC250_GCVM_VMID8_OK) {
        return BC250_VMID_POOL_OK;
    }

    entry->RootPhysical = RootPhysical;
    entry->InstalledRootPhysical = 0ull;
    entry->InstallState = BC250_VMID_POOL_INSTALLING;
    entry->LastInstallStatus = BC250_GCVM_VMID8_BAD_ARGUMENT;
    *NeedsHardwareInstall = 1u;
    return BC250_VMID_POOL_OK;
}

BC250_VMID_POOL_STATUS Bc250VmidPoolRecordInstall(
    BC250_VMID_POOL *Pool,
    BC250_MCDM_HANDLE Context,
    uint32_t Vmid,
    uint64_t RootPhysical,
    BC250_GCVM_VMID8_STATUS InstallStatus)
{
    BC250_VMID_POOL_ENTRY *entry;

    if (Pool == NULL || Context == NULL || Vmid >= BC250_VMID_COUNT ||
        Bc250VmidIsReserved(Vmid) != 0u) {
        return BC250_VMID_POOL_BAD_ARGUMENT;
    }
    entry = &Pool->Entries[Vmid];
    if (entry->Context != Context || entry->Vmid != Vmid) {
        return BC250_VMID_POOL_NOT_FOUND;
    }
    if (entry->InstallState != BC250_VMID_POOL_INSTALLING) {
        return BC250_VMID_POOL_BUSY;
    }
    if (InstallStatus == BC250_GCVM_VMID8_OK &&
        (RootPhysical == 0ull || (RootPhysical & 0xFFFull) != 0ull)) {
        return BC250_VMID_POOL_BAD_ARGUMENT;
    }

    entry->LastInstallStatus = InstallStatus;
    if (InstallStatus == BC250_GCVM_VMID8_OK) {
        entry->InstalledRootPhysical = RootPhysical;
        entry->InstallState = BC250_VMID_POOL_INSTALL_READY;
        /*
         * Historico diagnostico: sobrevive a Release e so ganha valor depois
         * da escrita + readback + flush terem devolvido OK para ESTE VMID.
         */
        Pool->RetainedRoots[Vmid].RootPhysical = RootPhysical;
        Pool->RetainedRoots[Vmid].Context = Context;
        Pool->RetainedRoots[Vmid].Generation = entry->Generation;
    } else {
        entry->InstalledRootPhysical = 0ull;
        entry->InstallState = BC250_VMID_POOL_INSTALL_FAILED;
    }
    return BC250_VMID_POOL_OK;
}
