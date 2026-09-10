#include "../core/bc250_gcvm_vmid8_transaction.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t Registers[0xA500u / 4u];
    uint32_t FlushResult;
    uint32_t FlushCalls;
    uint32_t FailReadbackOffset;
    uint32_t WriteCalls;
    uint32_t FailWriteOnceOffset;
} FIXTURE;

static FIXTURE g_fixture;

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if (Value == 0 || (Offset & 3u) != 0u || Offset >= 0xA500u) return 0u;
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
    if ((Offset & 3u) != 0u || Offset >= 0xA500u) return 0u;
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
    CHECK(instEv.ContextControlProgrammed == 0x00555403u);
    CHECK(instEv.ProgramReadbackPassed == 1u);
    CHECK(instEv.TlbFlushPassed == 1u);
    CHECK(instEv.PhysicalReady == 1u);
    CHECK(f->FlushCalls == 1u);
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == 0x00555403u);
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
    CHECK(f->Registers[BC250_GCVM_VMID8_CNTL / 4u] == 0x00555403u);
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
    f->Registers[BC250_GCVM_VMID9_CNTL / 4u] = 1u;
    CHECK(Bc250GcvmComputeInstall(0x45FFB0000ull, 18ull, 18ull, &io, &binding) == BC250_GCVM_VMID8_CONTEXT_ENABLED);
    CHECK(!binding.SnapshotValid);
    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
