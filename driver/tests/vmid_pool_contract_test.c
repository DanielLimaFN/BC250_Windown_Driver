/*
 * Suite do pool de VMID por contexto (DX.7, fatia minima).
 *
 * Os numeros aqui nao sao inventados: saem das Corridas 49, 50 e 51.
 *   - pico de 4 contextos simultaneos medido na Corrida 52
 *   - 16 contextos GCVM legiveis, inventario CONFIRMADO na Corrida 51
 *   - 4 reservados por medicao: 0 (MEC), 7 (usuario), 8 (paginacao), 9 (compute)
 */
#include "../core/bc250_vmid_pool_contract.h"

/* Mesmo padrao das demais suites: declaracao direta, sem arrastar windows.h. */
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static int g_failureLine = 0;

#define CHECK(cond) do { if (!(cond)) { g_failureLine = __LINE__; return 0; } } while (0)

static BC250_VMID_POOL g_pool;

static BC250_MCDM_HANDLE FakeCtx(uint32_t n)
{
    return (BC250_MCDM_HANDLE)((uint64_t)0x1000u + n);
}

static int Run(void)
{
    uint32_t vmid = 0u, again = 0u, released = 0u;
    uint32_t needsInstall = 0u;
    uint32_t i;
    BC250_VMID_POOL_WALK_ROOT walkRoot;

    /*
     * Allowlist de escrita: sete offsets exatos para cada VMID do pool.
     * Reservados 0/7/8/9 continuam fora DESTA politica, mesmo que tenham
     * allowlists dedicadas em outros subsistemas.
     */
    for (i = 0u; i < BC250_VMID_COUNT; ++i) {
        const uint32_t pairOffset = i * BC250_GCVM_PT_BASE_STRIDE;
        const uint32_t expected = Bc250VmidIsReserved(i) == 0u ? 1u : 0u;
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_CNTL_BASE + i * BC250_GCVM_CNTL_STRIDE) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_BASE_LO_BASE + pairOffset) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_BASE_LO_BASE + pairOffset + 4u) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_START_LO_BASE + pairOffset) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_START_LO_BASE + pairOffset + 4u) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_END_LO_BASE + pairOffset) == expected);
        CHECK(Bc250VmidPoolIsWritableGcvmRegister(
            BC250_GCVM_PT_END_LO_BASE + pairOffset + 4u) == expected);
    }
    /* Desalinhados, vizinhos e VMID fora da faixa nunca entram. */
    CHECK(Bc250VmidPoolIsWritableGcvmRegister(BC250_GCVM_CNTL_BASE - 4u) == 0u);
    CHECK(Bc250VmidPoolIsWritableGcvmRegister(
        BC250_GCVM_CNTL_BASE + BC250_GCVM_CONTEXT_COUNT * BC250_GCVM_CNTL_STRIDE) == 0u);
    CHECK(Bc250VmidPoolIsWritableGcvmRegister(BC250_GCVM_PT_BASE_LO_BASE - 4u) == 0u);
    CHECK(Bc250VmidPoolIsWritableGcvmRegister(BC250_GCVM_PT_END_LO_BASE +
        BC250_GCVM_CONTEXT_COUNT * BC250_GCVM_PT_BASE_STRIDE) == 0u);
    CHECK(Bc250VmidPoolIsWritableGcvmRegister(BC250_GCVM_PT_BASE_LO_BASE + 2u) == 0u);

    /* --- reservados sao os QUATRO medidos, e a mascara deriva deles --- */
    CHECK(Bc250VmidIsReserved(BC250_VMID_RESERVED_MEC) != 0u);      /* 0 */
    CHECK(Bc250VmidIsReserved(BC250_VMID_RESERVED_USER) != 0u);     /* 7 */
    CHECK(Bc250VmidIsReserved(BC250_VMID_RESERVED_PAGING) != 0u);   /* 8 */
    CHECK(Bc250VmidIsReserved(BC250_VMID_RESERVED_COMPUTE) != 0u);  /* 9 */
    CHECK(Bc250VmidIsReserved(1u) == 0u);
    CHECK(Bc250VmidIsReserved(6u) == 0u);
    CHECK(Bc250VmidIsReserved(10u) == 0u);
    CHECK(Bc250VmidIsReserved(15u) == 0u);
    /* Fora da faixa conta como reservado -- nunca vira slot valido. */
    CHECK(Bc250VmidIsReserved(16u) != 0u);
    CHECK(Bc250VmidIsReserved(0xFFFFFFFFu) != 0u);

    /* O pool tem exatamente 12 slots: 16 menos os 4 reservados. */
    {
        uint32_t livres = 0u;
        for (i = 0u; i < BC250_VMID_COUNT; ++i) {
            if (Bc250VmidIsReserved(i) == 0u) ++livres;
        }
        CHECK(livres == 12u);
    }

    Bc250VmidPoolInitialize(&g_pool);
    CHECK(g_pool.InUseMask == 0u);
    CHECK(g_pool.AllocCount == 0u);
    CHECK(g_pool.ExhaustedCount == 0u);

    /* --- alocacao nunca devolve reservado --- */
    for (i = 0u; i < 12u; ++i) {
        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(i), &vmid, &again) == BC250_VMID_POOL_OK);
        CHECK(again == 0u);
        CHECK(Bc250VmidIsReserved(vmid) == 0u);
    }
    CHECK(g_pool.AllocCount == 12u);

    /*
     * PICO MEDIDO NA CORRIDA 52: quatro contextos simultaneos (55 amostras de
     * InUseMask, distribuicao 0:6 1:17 2:19 3:12 4:1), zero exaustoes em 27
     * aquisicoes. Doze slots dao folga de 3x sobre o pico OBSERVADO.
     *
     * A margem de 3x nao e' estetica: o pico depende da carga, e esta corrida
     * rodou UM aplicativo de conformidade, nao um desktop com varios clientes.
     * Esta assercao existe para que reduzir o pool sem medir de novo REPROVE.
     */
    CHECK(g_pool.AllocCount >= 4u * 3u);

    /* --- exaustao FALHA, nao degrada --- */
    CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(99u), &vmid, &again) == BC250_VMID_POOL_EXHAUSTED);
    CHECK(g_pool.ExhaustedCount == 1u);
    /*
     * CONTRAPONTO que impede o retorno do defeito: na exaustao o VMID devolvido
     * NAO pode ser o compartilhado. Cair no 7 reintroduziria exatamente a
     * disputa que a Corrida 49 mediu, e o log diria "funcionou".
     */
    CHECK(vmid != BC250_VMID_RESERVED_USER);
    CHECK(vmid >= BC250_VMID_COUNT);

    /* --- reentrancia: mesmo contexto, mesmo VMID --- */
    {
        uint32_t primeiro = 0u, segundo = 0u;
        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(3u), &primeiro, &again) == BC250_VMID_POOL_OK);
        CHECK(again == 1u);
        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(3u), &segundo, &again) == BC250_VMID_POOL_OK);
        CHECK(primeiro == segundo);
        CHECK(Bc250VmidPoolLookup(&g_pool, FakeCtx(3u), &vmid) == BC250_VMID_POOL_OK);
        CHECK(vmid == primeiro);
    }

    /* --- reciclagem devolve o slot, e so' ele --- */
    {
        uint32_t antes = g_pool.InUseMask;
        CHECK(Bc250VmidPoolRelease(&g_pool, FakeCtx(5u), &released) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidIsReserved(released) == 0u);
        CHECK(g_pool.InUseMask == (antes & ~(1u << released)));
        CHECK(Bc250VmidPoolLookup(&g_pool, FakeCtx(5u), &vmid) == BC250_VMID_POOL_NOT_FOUND);
        /* o slot liberado volta a ser alocavel, e e o MESMO */
        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(77u), &vmid, &again) == BC250_VMID_POOL_OK);
        CHECK(vmid == released);
        CHECK(again == 0u);
    }

    /* --- raiz registrada pertence ao contexto certo --- */
    CHECK(Bc250VmidPoolNoteRoot(&g_pool, FakeCtx(2u), 0x45FDCE000ull) == BC250_VMID_POOL_OK);
    CHECK(Bc250VmidPoolLookup(&g_pool, FakeCtx(2u), &vmid) == BC250_VMID_POOL_OK);
    CHECK(g_pool.Entries[vmid].RootPhysical == 0x45FDCE000ull);
    CHECK(g_pool.Entries[vmid].InstalledRootPhysical == 0ull);
    CHECK(g_pool.Entries[vmid].InstallState == BC250_VMID_POOL_INSTALL_NEVER);
    CHECK(Bc250VmidPoolNoteRoot(&g_pool, FakeCtx(123u), 1ull) == BC250_VMID_POOL_NOT_FOUND);

    /* Estado de hardware e separado da raiz apenas observada no VMID 7. */
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDCE000ull, &needsInstall) == BC250_VMID_POOL_OK);
    CHECK(needsInstall == 1u);
    CHECK(g_pool.Entries[vmid].InstallState ==
        BC250_VMID_POOL_INSTALLING);
    /* Slot em transacao nao pode ser reciclado. */
    CHECK(Bc250VmidPoolRelease(&g_pool, FakeCtx(2u), &released) ==
        BC250_VMID_POOL_BUSY);
    CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDCE000ull, BC250_GCVM_VMID8_OK) == BC250_VMID_POOL_OK);
    CHECK(g_pool.Entries[vmid].InstalledRootPhysical == 0x45FDCE000ull);
    CHECK(g_pool.Entries[vmid].InstallState == BC250_VMID_POOL_INSTALL_READY);
    CHECK(g_pool.Entries[vmid].LastInstallStatus == BC250_GCVM_VMID8_OK);
    CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool, vmid, &walkRoot) ==
        BC250_VMID_POOL_OK);
    CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_LIVE_READY);
    CHECK(walkRoot.RootPhysical == 0x45FDCE000ull);
    CHECK(walkRoot.Context == FakeCtx(2u));
    CHECK(walkRoot.Generation == g_pool.Entries[vmid].Generation);
    /* Mesma raiz READY nao reescreve MMIO nem refaz flush. */
    needsInstall = 1u;
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDCE000ull, &needsInstall) == BC250_VMID_POOL_OK);
    CHECK(needsInstall == 0u);
    /* Falha posterior nunca pode deixar o estado READY antigo visivel. */
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDD0000ull, &needsInstall) == BC250_VMID_POOL_OK);
    CHECK(needsInstall == 1u);
    CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDD0000ull, BC250_GCVM_VMID8_TLB_FAILED) == BC250_VMID_POOL_OK);
    CHECK(g_pool.Entries[vmid].InstalledRootPhysical == 0ull);
    CHECK(g_pool.Entries[vmid].InstallState == BC250_VMID_POOL_INSTALL_FAILED);
    CHECK(g_pool.Entries[vmid].RootPhysical == 0x45FDD0000ull);
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(2u),
        BC250_VMID_RESERVED_USER, 0x45FDCE000ull,
        &needsInstall) == BC250_VMID_POOL_BAD_ARGUMENT);
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDCE001ull, &needsInstall) ==
        BC250_VMID_POOL_BAD_ARGUMENT);
    /* Completion sem BeginInstall nunca publica autoridade. */
    CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(2u), vmid,
        0x45FDCE000ull, BC250_GCVM_VMID8_OK) == BC250_VMID_POOL_BUSY);

    /*
     * Liberar apaga a autoridade operacional da entrada, mas preserva fora
     * dela a ultima raiz PROVADA para a caminhada post-mortem do TDR.
     */
    CHECK(Bc250VmidPoolRelease(&g_pool, FakeCtx(2u), &released) ==
        BC250_VMID_POOL_OK);
    CHECK(released == vmid);
    CHECK(g_pool.Entries[released].InstalledRootPhysical == 0ull);
    CHECK(g_pool.Entries[released].InstallState == BC250_VMID_POOL_INSTALL_NEVER);
    CHECK(Bc250VmidPoolInstalledRootForVmid(&g_pool, released) == 0ull);
    CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool, released, &walkRoot) ==
        BC250_VMID_POOL_OK);
    CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_RETAINED);
    CHECK(walkRoot.RootPhysical == 0x45FDCE000ull);
    CHECK(walkRoot.Context == FakeCtx(2u));
    CHECK(walkRoot.Generation != 0u);
    CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(88u), &vmid, &again) ==
        BC250_VMID_POOL_OK);
    CHECK(vmid == released);
    CHECK(g_pool.Entries[vmid].InstallState == BC250_VMID_POOL_INSTALL_NEVER);
    /* Slot reciclado ainda nao instalado: a fonte retida e declarada como tal. */
    CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool, vmid, &walkRoot) ==
        BC250_VMID_POOL_OK);
    CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_RETAINED);
    CHECK(walkRoot.Context == FakeCtx(2u));

    /* Uma nova instalacao comprovada substitui o historico do VMID. */
    CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(88u), vmid,
        0x45FEE0000ull, &needsInstall) == BC250_VMID_POOL_OK);
    CHECK(needsInstall == 1u);
    CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(88u), vmid,
        0x45FEE0000ull, BC250_GCVM_VMID8_OK) == BC250_VMID_POOL_OK);
    CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool, vmid, &walkRoot) ==
        BC250_VMID_POOL_OK);
    CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_LIVE_READY);
    CHECK(walkRoot.RootPhysical == 0x45FEE0000ull);
    CHECK(walkRoot.Context == FakeCtx(88u));

    CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool,
        BC250_VMID_RESERVED_USER, &walkRoot) == BC250_VMID_POOL_BAD_ARGUMENT);
    CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_NONE);

    /*
     * ---------------------------------------------------------------------
     * DX.12 -- MASCARA DE INVALIDACAO.
     *
     * Estes casos existem porque o defeito que a faixa conserta era
     * invisivel: Bc250ActiveFlushMappedGfxhubTlb invalidava 8/7/9 e o anel
     * grafico submetia em 1..6/10..15 desde a Corrida 54. A regra vive aqui,
     * no contrato, e nao apenas no miniport, justamente para poder falhar.
     * ---------------------------------------------------------------------
     */
    {
        uint32_t maskVmidA = 0u, maskVmidB = 0u;
        uint32_t ignored = 0u;

        Bc250VmidPoolInitialize(&g_pool);
        /* Pool vazio: mascara vazia, nunca os reservados por carona. */
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);
        CHECK(Bc250VmidPoolReadyFlushMask(NULL) == 0u);

        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(201u), &maskVmidA,
            &ignored) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidPoolAcquire(&g_pool, FakeCtx(202u), &maskVmidB,
            &ignored) == BC250_VMID_POOL_OK);
        CHECK(maskVmidA != maskVmidB);

        /*
         * Apenas ALOCADO nao entra: o silicio ainda nao recebeu raiz nenhuma
         * nesse slot, e invalidar TLB de um VMID sem tabela gastaria polls
         * sem responder pergunta nenhuma.
         */
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);

        /* INSTALLING tambem nao: a escrita ainda esta em curso. */
        CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(201u), maskVmidA,
            0x450111000ull, &needsInstall) == BC250_VMID_POOL_OK);
        CHECK(needsInstall == 1u);
        CHECK(g_pool.Entries[maskVmidA].InstallState ==
            BC250_VMID_POOL_INSTALLING);
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);
        /* FT.1 (doc 697): a mascara por raiz JA conta a VMID em instalacao. */
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0x450111000ull) == (1u << maskVmidA));
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0x450999000ull) == 0u);

        /* READY com raiz provada entra. */
        CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(201u), maskVmidA,
            0x450111000ull, BC250_GCVM_VMID8_OK) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == (1u << maskVmidA));

        /* FAILED nao entra -- o silicio recusou a raiz. */
        CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(202u), maskVmidB,
            0x450222000ull, &needsInstall) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(202u), maskVmidB,
            0x450222000ull, BC250_GCVM_VMID8_READBACK_FAILED) ==
            BC250_VMID_POOL_OK);
        CHECK(g_pool.Entries[maskVmidB].InstallState ==
            BC250_VMID_POOL_INSTALL_FAILED);
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == (1u << maskVmidA));

        /* Dois slots READY saem JUNTOS -- o caso que a Corrida 58 tinha. */
        CHECK(Bc250VmidPoolBeginInstall(&g_pool, FakeCtx(202u), maskVmidB,
            0x450222000ull, &needsInstall) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidPoolRecordInstall(&g_pool, FakeCtx(202u), maskVmidB,
            0x450222000ull, BC250_GCVM_VMID8_OK) == BC250_VMID_POOL_OK);
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) ==
            ((1u << maskVmidA) | (1u << maskVmidB)));
        /* 263b (doc 695): mascara por raiz -- so' a VMID que tem ESTA raiz. */
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0x450111000ull) == (1u << maskVmidA));
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0x450222000ull) == (1u << maskVmidB));
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0x450333000ull) == 0u);
        CHECK(Bc250VmidPoolMaskForRoot(&g_pool, 0ull) == 0u);
        CHECK(Bc250VmidPoolMaskForRoot(NULL, 0x450111000ull) == 0u);

        /* Nenhum reservado pode aparecer, sob nenhuma combinacao. */
        CHECK((Bc250VmidPoolReadyFlushMask(&g_pool) &
            BC250_VMID_RESERVED_MASK) == 0u);

        /*
         * CONTRAPONTO, para a assercao nao virar carimbo: o contexto morre e
         * o slot SAI da mascara, ainda que a raiz fique retida para a
         * caminhada post-mortem. Invalidar TLB de contexto morto nao e
         * errado, e' desperdicio -- e a retencao nao pode dar autoridade
         * operacional a nada, que e' o contrato do DX.10.
         */
        CHECK(Bc250VmidPoolRelease(&g_pool, FakeCtx(201u), &released) ==
            BC250_VMID_POOL_OK);
        CHECK(released == maskVmidA);
        CHECK(Bc250VmidPoolRootForFaultWalk(&g_pool, maskVmidA, &walkRoot) ==
            BC250_VMID_POOL_OK);
        CHECK(walkRoot.Source == BC250_VMID_POOL_WALK_ROOT_RETAINED);
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == (1u << maskVmidB));

        /*
         * READY sem raiz e' contradicao interna e nao entra. Nao ha caminho
         * de API que produza esse estado; e' forcado aqui de proposito, para
         * que a guarda exista mesmo se um caminho futuro o produzir.
         */
        g_pool.Entries[maskVmidB].InstalledRootPhysical = 0ull;
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);

        /*
         * INSTALLING e FAILED nao entram NEM COM RAIZ presente.
         *
         * Forcado de proposito: pelo caminho vivo, RecordInstall zera
         * InstalledRootPhysical em toda saida que nao seja READY, entao as
         * duas guardas se cobrem e NENHUMA das duas consegue falhar sozinha.
         * Guarda que nenhuma assercao alcanca e carimbo. Aqui cada uma passa a
         * ser load-bearing: se a de estado cair, esta linha reprova.
         */
        g_pool.Entries[maskVmidB].InstallState = BC250_VMID_POOL_INSTALLING;
        g_pool.Entries[maskVmidB].InstalledRootPhysical = 0x450444000ull;
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);
        g_pool.Entries[maskVmidB].InstallState =
            BC250_VMID_POOL_INSTALL_FAILED;
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);
        g_pool.Entries[maskVmidB].InstallState = BC250_VMID_POOL_INSTALL_READY;
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == (1u << maskVmidB));

        /*
         * Slot SEM contexto tambem nao entra, ainda que READY e com raiz.
         *
         * Este caso e forcado porque a API nao o produz hoje: Release limpa
         * InstallState junto com o contexto, entao a guarda de Context e
         * REDUNDANTE pelo caminho vivo -- e uma guarda que nenhuma assercao
         * alcanca e carimbo, nao portao. Se um caminho futuro liberar o
         * contexto preservando o estado de instalacao, esta linha reprova em
         * vez de o driver gastar polls invalidando VMID de ninguem.
         */
        g_pool.Entries[maskVmidB].Context = NULL;
        g_pool.Entries[maskVmidB].InstallState = BC250_VMID_POOL_INSTALL_READY;
        g_pool.Entries[maskVmidB].InstalledRootPhysical = 0x450333000ull;
        CHECK(Bc250VmidPoolReadyFlushMask(&g_pool) == 0u);
    }

    Bc250VmidPoolInitialize(&g_pool);

    /* --- argumentos invalidos nao podem devolver VMID utilizavel --- */
    CHECK(Bc250VmidPoolAcquire(NULL, FakeCtx(1u), &vmid, &again) == BC250_VMID_POOL_BAD_ARGUMENT);
    CHECK(Bc250VmidPoolAcquire(&g_pool, NULL, &vmid, &again) == BC250_VMID_POOL_BAD_ARGUMENT);
    CHECK(vmid >= BC250_VMID_COUNT);
    CHECK(Bc250VmidPoolRelease(&g_pool, NULL, &released) == BC250_VMID_POOL_BAD_ARGUMENT);

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : (unsigned long)g_failureLine);
}
