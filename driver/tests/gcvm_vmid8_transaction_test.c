#include "../core/bc250_gcvm_vmid8_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t Registers[0xA600u / 4u];
    uint32_t FlushResult;
    uint32_t FlushCalls;
    uint32_t LastFlushVmid;
    uint32_t FailReadbackOffset;
    uint32_t WriteCalls;
    uint32_t FailWriteOnceOffset;
} FIXTURE;

static FIXTURE g_fixture;

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if (Value == 0 || (Offset & 3u) != 0u || Offset >= 0xA600u) return 0u;
    if (f->FailReadbackOffset != 0u && Offset == f->FailReadbackOffset) {
        *Value = ~f->Registers[Offset / 4u];
        return 1u;
    }
    *Value = f->Registers[Offset / 4u];
    return 1u;
}

static uint32_t Write32(void *Context, uint32_t Offset, uint32_t Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if ((Offset & 3u) != 0u || Offset >= 0xA600u) return 0u;
    ++f->WriteCalls;
    if (f->FailWriteOnceOffset == Offset) {
        f->FailWriteOnceOffset = 0u;
        return 0u;
    }
    f->Registers[Offset / 4u] = Value;
    return 1u;
}

static uint32_t Flush(void *Context)
{
    FIXTURE *f = (FIXTURE *)Context;
    ++f->FlushCalls;
    return f->FlushResult;
}

static uint32_t FlushVmid(void *Context, uint32_t Vmid)
{
    FIXTURE *f = (FIXTURE *)Context;
    ++f->FlushCalls;
    f->LastFlushVmid = Vmid;
    return f->FlushResult;
}

static int Run(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GCVM_VMID8_INTERFACE io;
    BC250_GCVM_VMID8_EVIDENCE evidence;
    BC250_GCVM_VMID8_INSTALL_EVIDENCE instEv;
    BC250_GCVM_COMPUTE_BINDING binding = {0};
    uint32_t original[6] = { 1u, 2u, 3u, 4u, 5u, 6u };
    const uint32_t offsets[6] = {
        BC250_GCVM_VMID8_PT_BASE_LO, BC250_GCVM_VMID8_PT_BASE_HI,
        BC250_GCVM_VMID8_PT_START_LO, BC250_GCVM_VMID8_PT_START_HI,
        BC250_GCVM_VMID8_PT_END_LO, BC250_GCVM_VMID8_PT_END_HI
    };
    uint32_t i;
    io.Read32 = Read32;
    io.Write32 = Write32;
    io.Flush = Flush;
    io.FlushVmid = FlushVmid;
    io.Context = f;
    io.Identity = BC250_GCVM_VMID8_IDENTITY;
    f->FlushResult = 1u;
    f->FailReadbackOffset = 0u;
    for (i = 0u; i < 6u; ++i) f->Registers[offsets[i] / 4u] = original[i];

    /* 1. Transacao inerte legada */
    CHECK(Bc250RunGcvmVmid8InertTransaction(
        0x45FF10000ull, &io, &evidence) == BC250_GCVM_VMID8_OK);
    CHECK(evidence.PageTablePhysical == 0x45FF10000ull);
    CHECK(evidence.ProgramReadbackPassed == 1u);
    CHECK(evidence.TlbFlushPassed == 1u);
    CHECK(evidence.RestorePassed == 1u);
    CHECK(evidence.PhysicalReady == 1u);
    CHECK(f->FlushCalls == 1u);
    for (i = 0u; i < 6u; ++i) CHECK(f->Registers[offsets[i] / 4u] == original[i]);

    f->Registers[BC250_GCVM_VMID8_CNTL / 4u] = 1u;
    CHECK(Bc250RunGcvmVmid8InertTransaction(
        0x45FF10000ull, &io, &evidence) == BC250_GCVM_VMID8_CONTEXT_ENABLED);
    CHECK(f->FlushCalls == 1u);

    /* 2. Bc250GcvmVmid8Install - Sucesso */
    f->Registers[BC250_GCVM_VMID8_CNTL / 4u] = 0u;
    f->FlushCalls = 0u;
    f->FailReadbackOffset = 0u;
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_OK);
    CHECK(instEv.PageTablePhysical == 0x000000F445FF1000ull);
    CHECK(instEv.Vmid == BC250_GCVM_VMID8);
    CHECK(f->LastFlushVmid == BC250_GCVM_VMID8);
    /*
     * P6_VMID8_ROOT_VALID_BIT_OFFLINE
     *
     * O ponteiro de root carrega o bit VALID (bit 0), como amdgpu_gmc_pd_addr()
     * faz para Vega10+: pd_addr |= AMDGPU_PTE_VALID. Sem ele o walker do GCVM
     * trata a root como invalida.
     */
    CHECK(instEv.BaseLowProgrammed == (0x45FF1000u | 0x1u));
    CHECK(instEv.BaseHighProgrammed == 0x000000F4u);
    CHECK(instEv.StartLowProgrammed == 0x00000000u);
    CHECK(instEv.StartHighProgrammed == 0x00000000u);
    CHECK(instEv.EndLowProgrammed == 0x003FFFFFu);
    CHECK(instEv.EndHighProgrammed == 0x00000000u);
    CHECK(instEv.ContextControlProgrammed == BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
    CHECK(instEv.ProgramReadbackPassed == 1u);
    CHECK(instEv.TlbFlushPassed == 1u);
    CHECK(instEv.PhysicalReady == 1u);
    CHECK(f->FlushCalls == 1u);
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_BASE_LO / 4u] == (0x45FF1000u | 0x1u));
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_BASE_HI / 4u] == 0x000000F4u);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_START_LO / 4u] == 0x00000000u);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_START_HI / 4u] == 0x00000000u);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_END_LO / 4u] == 0x003FFFFFu);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_END_HI / 4u] == 0x00000000u);

    /* 3. Bc250GcvmVmid8Install - Falha de Flush da TLB (deve desabilitar CNTL) */
    f->FlushResult = 0u;
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_TLB_FAILED);
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == 0u);
    f->FlushResult = 1u;

    /* 4. Bc250GcvmVmid8Install - Falha de readback nos registradores de endereco */
    f->FailReadbackOffset = BC250_GCVM_VMID8_PT_BASE_LO;
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_READBACK_FAILED);
    f->FailReadbackOffset = 0u;

    /* 5. Bc250GcvmVmid8Install - Falha de readback no CNTL (deve zerar CNTL) */
    f->FailReadbackOffset = BC250_GCVM_VMID8_CNTL;
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_READBACK_FAILED);
    f->FailReadbackOffset = 0u;

    /* 6. Bc250GcvmVmid8Install - Argumentos invalidos */
    io.Identity = 0u;
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_BAD_ARGUMENT);
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, NULL, &instEv) == BC250_GCVM_VMID8_BAD_ARGUMENT);
    io.Identity = BC250_GCVM_VMID8_IDENTITY;

    /* 7. Bc250GcvmVmid8Uninstall - Sucesso */
    CHECK(Bc250GcvmVmid8Install(0x000000F445FF1000ull, &io, &instEv) == BC250_GCVM_VMID8_OK);
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
    f->FlushCalls = 0u;
    CHECK(Bc250GcvmVmid8Uninstall(&io, &instEv) == BC250_GCVM_VMID8_OK);
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == 0u);
    for (i = 0u; i < 6u; ++i) CHECK(f->Registers[offsets[i] / 4u] == 0u);
    CHECK(f->FlushCalls == 1u);

    /* VMID9 isolation: exact catalog addresses, busy refusal without writes,
     * switching after GPU retirement, baseline restore and fault rollback. */
    CHECK(BC250_GCVM_VMID9_CNTL == (0x1260u + 0x1629u) * 4u);
    CHECK(BC250_GCVM_VMID9_PT_BASE_LO == (0x1260u + 0x169Du) * 4u);
    CHECK(BC250_GCVM_VMID9_PT_END_HI == (0x1260u + 0x16DEu) * 4u);
    CHECK(Bc250GcvmIsComputeRegister(BC250_GCVM_VMID9_PT_START_HI));
    CHECK(!Bc250GcvmIsComputeRegister(BC250_GCVM_VMID8_CNTL));
    CHECK(!Bc250GcvmIsComputeRegister(BC250_GCVM_VMID9_CNTL + 4u));
    CHECK(!Bc250GcvmComputeCanSwitch(18ull, 17ull));
    CHECK(!Bc250GcvmComputeCanSwitch(18ull, 19ull));
    CHECK(Bc250GcvmComputeCanSwitch(18ull, 18ull));
    f->Registers[BC250_GCVM_VMID8_PT_BASE_LO / 4u] = 0x50A8C001u;
    f->Registers[BC250_GCVM_VMID9_CNTL / 4u] = 0x80000000u;
    f->Registers[BC250_GCVM_VMID9_PT_BASE_LO / 4u] = 0x123000u;
    i = f->WriteCalls;
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 18ull, 17ull, &io, &binding) == BC250_GCVM_VMID8_CONTEXT_ENABLED);
    CHECK(f->WriteCalls == i && !binding.SnapshotValid);
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 17ull, 17ull, &io, &binding) == BC250_GCVM_VMID8_OK);
    CHECK(binding.Ready && binding.Root == 0x45FFB0000ull);
    CHECK(f->Registers[BC250_GCVM_VMID9_PT_BASE_LO / 4u] == 0x5FFB0001u);
    CHECK(f->Registers[BC250_GCVM_VMID9_PT_BASE_HI / 4u] == 4u);
    CHECK(f->Registers[BC250_GCVM_VMID9_CNTL / 4u] == 0x80555403u);
    CHECK(f->Registers[BC250_GCVM_VMID8_PT_BASE_LO / 4u] == 0x50A8C001u);
    i = f->WriteCalls;
    CHECK(Bc250GcvmComputeInstall(0x45FFC0000ull, 18ull, 17ull, &io, &binding) == BC250_GCVM_VMID8_CONTEXT_ENABLED);
    CHECK(f->WriteCalls == i && binding.Root == 0x45FFB0000ull);
    CHECK(Bc250GcvmComputeInstall(0x45FFC0000ull, 18ull, 18ull, &io, &binding) == BC250_GCVM_VMID8_OK);
    CHECK(Bc250GcvmComputeRestore(&io, &binding) == BC250_GCVM_VMID8_OK);
    CHECK(!binding.Ready && !binding.SnapshotValid);
    CHECK(f->Registers[BC250_GCVM_VMID9_CNTL / 4u] == 0x80000000u);
    CHECK(f->Registers[BC250_GCVM_VMID9_PT_BASE_LO / 4u] == 0x123000u);
    f->FailWriteOnceOffset = BC250_GCVM_VMID9_PT_START_LO;
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 18ull, 18ull, &io, &binding) == BC250_GCVM_VMID8_READBACK_FAILED);
    CHECK(!binding.Ready && !binding.SnapshotValid);
    CHECK(f->Registers[BC250_GCVM_VMID9_PT_BASE_LO / 4u] == 0x123000u);
    f->FlushResult = 0u;
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 18ull, 18ull, &io, &binding) == BC250_GCVM_VMID8_RESTORE_FAILED);
    CHECK(!binding.Ready && binding.SnapshotValid);
    i = f->WriteCalls;
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 18ull, 18ull, &io, &binding) == BC250_GCVM_VMID8_RESTORE_FAILED);
    CHECK(f->WriteCalls == i);
    f->FlushResult = 1u;
    CHECK(Bc250GcvmComputeRestore(&io, &binding) == BC250_GCVM_VMID8_OK);
    CHECK(!binding.SnapshotValid);

    /* VMID7 (User/DWM) validation: exact catalog addresses, register checker,
     * separate installation from VMID8/9. */
    CHECK(BC250_GCVM_VMID7_CNTL == (0x1260u + 0x1627u) * 4u);
    CHECK(BC250_GCVM_VMID7_PT_BASE_LO == (0x1260u + 0x1699u) * 4u);
    CHECK(BC250_GCVM_VMID7_PT_END_HI == (0x1260u + 0x16DAu) * 4u);
    CHECK(Bc250GcvmIsUserRegister(BC250_GCVM_VMID7_PT_START_HI));
    CHECK(!Bc250GcvmIsUserRegister(BC250_GCVM_VMID8_CNTL));
    CHECK(!Bc250GcvmIsUserRegister(BC250_GCVM_VMID9_CNTL));
    CHECK(!Bc250GcvmIsUserRegister(BC250_GCVM_VMID7_CNTL + 4u));
    f->Registers[BC250_GCVM_VMID7_CNTL / 4u] = 0u;
    f->FlushCalls = 0u;
    CHECK(Bc250GcvmInstallVmid(BC250_GCVM_USER_VMID, 0x000000F445FF2000ull, &io, &instEv) == BC250_GCVM_VMID8_OK);
    CHECK(instEv.PageTablePhysical == 0x000000F445FF2000ull);
    CHECK(instEv.Vmid == BC250_GCVM_USER_VMID);
    CHECK(f->LastFlushVmid == BC250_GCVM_USER_VMID);
    CHECK(instEv.ContextControlProgrammed == BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
    CHECK(f->Registers[BC250_GCVM_VMID7_CNTL / 4u] == BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_BASE_LO / 4u] == (0x45FF2000u | 0x1u));
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_BASE_HI / 4u] == 0x000000F4u);
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_START_LO / 4u] == 0x00000000u);
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_START_HI / 4u] == 0x00000000u);
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_END_LO / 4u] == 0x003FFFFFu);
    CHECK(f->Registers[BC250_GCVM_VMID7_PT_END_HI / 4u] == 0x00000000u);

    /* O callback recebe o VMID exato; nao existe mais wrapper fixo em 7/8. */
    {
        const uint32_t poolVmids[2] = { 1u, 15u };
        uint32_t p;
        for (p = 0u; p < 2u; ++p) {
            const uint32_t target = poolVmids[p];
            const uint64_t root = 0x000000F445FD0000ull +
                ((uint64_t)target << 12);
            f->FlushCalls = 0u;
            f->LastFlushVmid = BC250_GCVM_CONTEXT_COUNT;
            CHECK(Bc250GcvmInstallVmid(target, root, &io, &instEv) ==
                BC250_GCVM_VMID8_OK);
            CHECK(instEv.Vmid == target);
            CHECK(f->FlushCalls == 1u);
            CHECK(f->LastFlushVmid == target);
            CHECK(f->Registers[Bc250GcvmContextCntlOffset(target) / 4u] ==
                BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE);
            CHECK(f->Registers[Bc250GcvmContextPtBaseLoOffset(target) / 4u] ==
                ((uint32_t)root | 0x1u));
        }
        /*
         * DX.15 -- DESARME DO SLOT NO SILICIO.
         *
         * Ate 21/09 Bc250VmidPoolRelease era SOMENTE SOFTWARE: o VMID voltava
         * para os livres com ENABLE_CONTEXT ainda ligado e PT_BASE apontando
         * para a raiz de um processo morto. Estes casos existem para que o
         * teardown possa FALHAR -- APPLY sem TEARDOWN e' a Lei 5 quebrada.
         */
        {
            BC250_GCVM_VMID8_INSTALL_EVIDENCE offEv;
            const uint32_t target = 1u;
            const uint64_t root = 0x000000F445FD1000ull;

            CHECK(Bc250GcvmInstallVmid(target, root, &io, &instEv) ==
                BC250_GCVM_VMID8_OK);
            CHECK(f->Registers[Bc250GcvmContextCntlOffset(target) / 4u] != 0u);
            CHECK(f->Registers[Bc250GcvmContextPtBaseLoOffset(target) / 4u]
                != 0u);

            f->FlushCalls = 0u;
            f->LastFlushVmid = BC250_GCVM_CONTEXT_COUNT;
            CHECK(Bc250GcvmUninstallVmid(target, &io, &offEv) ==
                BC250_GCVM_VMID8_OK);
            CHECK(offEv.Vmid == target);
            CHECK(offEv.ProgramReadbackPassed == 1u);
            CHECK(offEv.TlbFlushPassed == 1u);
            /* CNTL desarmado E raiz zerada -- as duas coisas, nao uma. */
            CHECK(f->Registers[Bc250GcvmContextCntlOffset(target) / 4u] == 0u);
            CHECK(f->Registers[Bc250GcvmContextPtBaseLoOffset(target) / 4u]
                == 0u);
            /*
             * A TLB invalidada e' a DESTE VMID. Invalidar outro deixaria a
             * traducao velha viva exatamente no slot devolvido -- e o teste
             * passaria se so' contasse chamadas.
             */
            CHECK(f->FlushCalls == 1u);
            CHECK(f->LastFlushVmid == target);

            /* Argumentos invalidos nao podem "desarmar" nada. */
            f->FlushCalls = 0u;
            CHECK(Bc250GcvmUninstallVmid(BC250_GCVM_CONTEXT_COUNT, &io, &offEv)
                == BC250_GCVM_VMID8_BAD_ARGUMENT);
            CHECK(f->FlushCalls == 0u);
            io.FlushVmid = NULL;
            CHECK(Bc250GcvmUninstallVmid(target, &io, &offEv) ==
                BC250_GCVM_VMID8_BAD_ARGUMENT);
            io.FlushVmid = FlushVmid;

            /*
             * CONTRAPONTO: desarmar o VMID 1 nao pode tocar o VMID 15. Sem
             * isto, uma formula de offset errada passaria despercebida.
             */
            CHECK(Bc250GcvmInstallVmid(1u, 0x000000F445FD1000ull, &io, &instEv)
                == BC250_GCVM_VMID8_OK);
            CHECK(Bc250GcvmInstallVmid(15u, 0x000000F445FDF000ull, &io, &instEv)
                == BC250_GCVM_VMID8_OK);
            CHECK(Bc250GcvmUninstallVmid(1u, &io, &offEv) ==
                BC250_GCVM_VMID8_OK);
            CHECK(f->Registers[Bc250GcvmContextCntlOffset(15u) / 4u] != 0u);
            CHECK(f->Registers[Bc250GcvmContextPtBaseLoOffset(15u) / 4u] != 0u);
            CHECK(Bc250GcvmUninstallVmid(15u, &io, &offEv) ==
                BC250_GCVM_VMID8_OK);
            CHECK(f->Registers[Bc250GcvmContextCntlOffset(15u) / 4u] == 0u);
        }
        f->FlushCalls = 0u;
        CHECK(Bc250GcvmInstallVmid(BC250_GCVM_CONTEXT_COUNT,
            0x000000F445FD0000ull, &io, &instEv) ==
            BC250_GCVM_VMID8_BAD_ARGUMENT);
        CHECK(f->FlushCalls == 0u);
        io.FlushVmid = NULL;
        CHECK(Bc250GcvmInstallVmid(1u, 0x000000F445FD0000ull,
            &io, &instEv) == BC250_GCVM_VMID8_BAD_ARGUMENT);
        io.FlushVmid = FlushVmid;
    }
    /*
     * INVENTARIO DOS 16 CONTEXTOS (21/09, apos a Corrida 49).
     * A formula dos offsets e conferida contra os enderecos JA travados acima
     * para VMID 7 e 8 -- que por sua vez vieram do upstream com base 0x1260.
     */
    {
        BC250_GCVM_CONTEXT_INVENTORY inv;
        uint32_t k;

        CHECK(Bc250GcvmContextCntlOffset(7u) == BC250_GCVM_VMID7_CNTL);
        CHECK(Bc250GcvmContextCntlOffset(8u) == BC250_GCVM_VMID8_CNTL);
        CHECK(Bc250GcvmContextPtBaseLoOffset(7u) == BC250_GCVM_VMID7_PT_BASE_LO);
        CHECK(Bc250GcvmContextPtBaseLoOffset(8u) == BC250_GCVM_VMID8_PT_BASE_LO);
        CHECK(Bc250GcvmContextCntlOffset(0u) == 0x0000A200u);
        CHECK(Bc250GcvmContextPtBaseLoOffset(0u) == 0x0000A3ACu);

        /*
         * Zerar so' os registradores do inventario. Um laco sobre o array
         * inteiro e' reemitido pelo /O2 como memset, que nao existe no link
         * offline -- a mesma armadilha ja registrada no projeto.
         */
        for (k = 0u; k < BC250_GCVM_CONTEXT_COUNT; ++k) {
            f->Registers[Bc250GcvmContextCntlOffset(k) / 4u] = 0u;
            f->Registers[Bc250GcvmContextPtBaseLoOffset(k) / 4u] = 0u;
            f->Registers[(Bc250GcvmContextPtBaseLoOffset(k) + 4u) / 4u] = 0u;
        }
        f->Registers[Bc250GcvmContextCntlOffset(3u) / 4u] = 1u;
        f->Registers[Bc250GcvmContextPtBaseLoOffset(5u) / 4u] = 0xFDD5000u;
        f->Registers[(Bc250GcvmContextPtBaseLoOffset(5u) + 4u) / 4u] = 4u;

        CHECK(Bc250GcvmScanContexts(&io, &inv) == BC250_GCVM_VMID8_OK);
        CHECK(inv.Scanned == BC250_GCVM_CONTEXT_COUNT);
        CHECK(inv.Entries[3].InUse == 1u);
        CHECK(inv.Entries[5].InUse == 1u);
        CHECK(inv.Entries[4].InUse == 0u);
        CHECK((inv.FreeMask & (1u << 3)) == 0u);
        CHECK((inv.FreeMask & (1u << 5)) == 0u);
        CHECK((inv.FreeMask & (1u << 4)) != 0u);
        CHECK(inv.InUseCount + inv.FreeCount + inv.UnreadableCount ==
              BC250_GCVM_CONTEXT_COUNT);

        /* CONTRAPONTO: flag na PT_BASE nao pode fazer livre parecer ocupado. */
        /*
         * Zerar so' os registradores do inventario. Um laco sobre o array
         * inteiro e' reemitido pelo /O2 como memset, que nao existe no link
         * offline -- a mesma armadilha ja registrada no projeto.
         */
        for (k = 0u; k < BC250_GCVM_CONTEXT_COUNT; ++k) {
            f->Registers[Bc250GcvmContextCntlOffset(k) / 4u] = 0u;
            f->Registers[Bc250GcvmContextPtBaseLoOffset(k) / 4u] = 0u;
            f->Registers[(Bc250GcvmContextPtBaseLoOffset(k) + 4u) / 4u] = 0u;
        }
        f->Registers[Bc250GcvmContextPtBaseLoOffset(6u) / 4u] = 0x1u; /* bit de flag, nao endereco */
        CHECK(Bc250GcvmScanContexts(&io, &inv) == BC250_GCVM_VMID8_OK);
        CHECK(inv.Entries[6].InUse == 0u);
        CHECK((inv.FreeMask & (1u << 6)) != 0u);

        /* Interface invalida NAO pode devolver "tudo livre". */
        CHECK(Bc250GcvmScanContexts(NULL, &inv) == BC250_GCVM_VMID8_BAD_ARGUMENT);
        CHECK(inv.FreeMask == 0u);
    }

    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
