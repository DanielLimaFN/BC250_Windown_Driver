#include "../core/bc250_gcvm_global_mmu.h"

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    while (Length--) {
        *d++ = (unsigned char)Value;
    }
    return Destination;
}

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

static unsigned long g_failureLine;
#define CHECK(x) do { if (!(x)) { g_failureLine = __LINE__; return 0; } } while (0)

typedef struct FIXTURE {
    uint32_t Registers[0xC000u / 4u];
    uint32_t FlushResult;
    uint32_t FlushCalls;
    uint32_t FailReadbackOffset;
    uint32_t FailReadbackAfterWrite;
    uint32_t FailReadbackOnce;
    uint32_t FailWriteOffset;
    uint32_t FailWriteOnce;
} FIXTURE;

static FIXTURE g_fixture;

static uint32_t Read32(void *Context, uint32_t Offset, uint32_t *Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if (Value == 0 || (Offset & 3u) != 0u || Offset >= 0xC000u) return 0u;
    if (f->FailReadbackOffset != 0u && Offset == f->FailReadbackOffset &&
        f->FailReadbackAfterWrite != 0u) {
        *Value = ~f->Registers[Offset / 4u];
        if (f->FailReadbackOnce != 0u) {
            f->FailReadbackOffset = 0u;
            f->FailReadbackOnce = 0u;
        }
        return 1u;
    }
    *Value = f->Registers[Offset / 4u];
    return 1u;
}

static uint32_t Write32(void *Context, uint32_t Offset, uint32_t Value)
{
    FIXTURE *f = (FIXTURE *)Context;
    if ((Offset & 3u) != 0u || Offset >= 0xC000u) return 0u;
    if (f->FailWriteOnce != 0u && Offset == f->FailWriteOffset) {
        f->FailWriteOnce = 0u;
        return 0u;
    }
    if (f->FailReadbackOffset != 0u && Offset == f->FailReadbackOffset) {
        f->FailReadbackAfterWrite = 1u;
    }
    /* Simula comportamento do hardware no L2_CNTL2: limpa bits 0 e 1 de trigger */
    if (Offset == BC250_REG_GCVM_L2_CNTL2) {
        Value &= ~BC250_GCVM_GLOBAL_L2_CNTL2_TRIGGERS;
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
    BC250_GCVM_GLOBAL_MMU_CONFIG config;
    BC250_GCVM_GLOBAL_MMU_INTERFACE io;
    BC250_GCVM_GLOBAL_MMU_EVIDENCE evidence;

    io.Read32 = Read32;
    io.Write32 = Write32;
    io.Flush = Flush;
    io.Context = f;
    io.Identity = BC250_GCVM_GLOBAL_MMU_IDENTITY;

    config.FbLocationStart = 0xF400000000ull;
    config.FbLocationEnd   = 0xF41FFFFFFFull;
    config.DummyPagePhysical = 0x450A80000ull; /* 4 KB aligned */
    config.AgpBytes = 0ull; /* sem D2: AGP desligada como antes */

    /* Valores iniciais do POST */
    f->FlushResult = 1u;
    f->FlushCalls = 0u;
    f->FailReadbackOffset = 0u;
    f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] = 0x00000501u;
    f->Registers[BC250_REG_GCVM_L2_CNTL4 / 4u] = 0x000000C1u;
    f->Registers[BC250_REG_GCVM_L2_CNTL / 4u] = 0x00080602u;
    f->Registers[BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2 / 4u] = 0x00020000u;

    /* 1. Sucesso na Programacao Global */
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(evidence.SnapshotPresent == 1u);
    CHECK(evidence.RegisterCount == BC250_GCVM_GLOBAL_MMU_REG_COUNT);
    CHECK(evidence.ProgramPassed == 1u);
    CHECK(evidence.ReadbackPassed == 1u);
    CHECK(evidence.TlbFlushPassed == 1u);
    CHECK(f->FlushCalls == 1u);

    /* Verifica valores aplicados */
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00001D59u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL / 4u] == 0x00080E01u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL3 / 4u] == 0x80130009u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL4 / 4u] == 0x00000001u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL5 / 4u] == 0x00003FE0u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR / 4u] == 0x003D0000u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR / 4u] == 0x003D07FFu);
    CHECK(f->Registers[BC250_REG_GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB / 4u] == (uint32_t)(0x450A80000ull >> 12));
    CHECK(f->Registers[BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2 / 4u] == 0x00060000u);

    /* 2. Sucesso na Restauracao (Restore) */
    f->FlushCalls = 0u;
    CHECK(Bc250GcvmGlobalMmuRestore(&io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(evidence.RestorePassed == 1u);
    CHECK(f->FlushCalls == 1u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00000501u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL4 / 4u] == 0x000000C1u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL / 4u] == 0x00080602u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_PROTECTION_FAULT_CNTL2 / 4u] == 0x00020000u);

    /* 3. Restore tenta o snapshot inteiro mesmo se uma escrita falhar. */
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) ==
        BC250_GCVM_GLOBAL_MMU_OK);
    f->FailWriteOffset = BC250_REG_GCVM_L2_CNTL3;
    f->FailWriteOnce = 1u;
    f->FlushCalls = 0u;
    CHECK(Bc250GcvmGlobalMmuRestore(&io, &evidence) ==
        BC250_GCVM_GLOBAL_MMU_RESTORE_FAILED);
    CHECK(evidence.RestorePassed == 0u);
    CHECK(evidence.RestoreFailureCount >= 1u);
    CHECK(evidence.FailureStep == 10u);
    CHECK(evidence.FailureRegister == BC250_REG_GCVM_L2_CNTL3);
    CHECK(f->FlushCalls == 1u);
    /* Um registrador posterior no LIFO ainda foi restaurado. */
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00000501u);
    /* Repara o registrador cuja escrita foi injetada como falha. */
    f->Registers[BC250_REG_GCVM_L2_CNTL3 / 4u] =
        evidence.ValuesBefore[23];

    /* 4. Falha de Readback com Rollback Automatico */
    f->FailReadbackOffset = BC250_REG_GCMC_VM_MX_L1_TLB_CNTL;
    f->FailReadbackOnce = 1u;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_READBACK_FAILED);
    CHECK(evidence.FailureStep == 5u);
    CHECK(evidence.FailureRegister == BC250_REG_GCMC_VM_MX_L1_TLB_CNTL);
    /* Verifica que o rollback restaurou o registrador anterior */
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00000501u);
    CHECK(f->Registers[BC250_REG_GCVM_L2_CNTL4 / 4u] == 0x000000C1u);
    CHECK(evidence.RollbackPassed == 1u);
    CHECK(evidence.RollbackFailureCount == 0u);
    f->FailReadbackOffset = 0u;
    f->FailReadbackAfterWrite = 0u;

    /* 5. Falha de Flush da TLB com Rollback Automatico */
    f->FlushResult = 0u;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_TLB_FAILED);
    CHECK(evidence.FailureStep == 6u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00000501u);
    /* O flush que provocou a falha tambem falha no rollback: nao mentir. */
    CHECK(evidence.RollbackPassed == 0u);
    CHECK(evidence.RollbackFailureCount == 1u);
    f->FlushResult = 1u;

    /* 6. Falha de escrita, seguida de rollback verificavel. */
    f->FailWriteOffset = BC250_REG_GCVM_L2_CNTL3;
    f->FailWriteOnce = 1u;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) ==
        BC250_GCVM_GLOBAL_MMU_WRITE_FAILED);
    CHECK(evidence.FailureStep == 4u);
    CHECK(evidence.RollbackPassed == 1u);
    CHECK(evidence.RollbackFailureCount == 0u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_MX_L1_TLB_CNTL / 4u] == 0x00000501u);

    /* 7. Argumentos Invalidos */
    config.DummyPagePhysical = 0x450A80001ull; /* desalinhado */
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    config.DummyPagePhysical = 0x450A80000ull;

    io.Identity = 0u;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    CHECK(Bc250GcvmGlobalMmuProgram(&config, NULL, &evidence) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    io.Identity = BC250_GCVM_GLOBAL_MMU_IDENTITY;

    /* Sem D2 a AGP continua vazia (BOT > TOP) e a abertura de sistema e' so' a FB. */
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(evidence.AgpEnabled == 0u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_BOT / 4u] == 0x00FFFFFFu);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_TOP / 4u] == 0x00000000u);
    CHECK(Bc250GcvmGlobalMmuRestore(&io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);

    /* 8. D2: AGP depois da FB, ALIGN(fb_end + 1, 16 GB), 32 GB. */
    {
        uint64_t s = 0ull, e = 0ull, mc = 1ull;
        CHECK(Bc250GcvmPlanAgpAfterFb(0xF400000000ull, 0xF41FFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES, &s, &e) == 1u);
        CHECK(s == 0xF800000000ull);
        CHECK(e == 0xFFFFFFFFFFull);
        /* FB ja alinhada termina exatamente na divisa: a AGP comeca nela. */
        CHECK(Bc250GcvmPlanAgpAfterFb(0xF000000000ull, 0xF3FFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES, &s, &e) == 1u);
        CHECK(s == 0xF400000000ull);
        /* Recusas: tamanho fora do granulo, FB invertida, passar de 2^48. */
        CHECK(Bc250GcvmPlanAgpAfterFb(0xF400000000ull, 0xF41FFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES + 0x1000ull, &s, &e) == 0u);
        CHECK(s == 0ull && e == 0ull);
        CHECK(Bc250GcvmPlanAgpAfterFb(0xF41FFFFFFFull, 0xF400000000ull,
            BC250_GCVM_AGP_D2_BYTES, &s, &e) == 0u);
        /* inicio 0xFFFC00000000 + 32 GB = 0x1000400000000 > 2^48 */
        CHECK(Bc250GcvmPlanAgpAfterFb(0xFFFB00000000ull, 0xFFFBFFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES, &s, &e) == 0u);
        /* o maior que ainda cabe: 0xFFF800000000 + 32 GB = 2^48 exato */
        CHECK(Bc250GcvmPlanAgpAfterFb(0xFFF000000000ull, 0xFFF7FFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES, &s, &e) == 1u);
        CHECK(e == 0xFFFFFFFFFFFFull);
        CHECK(Bc250GcvmPlanAgpAfterFb(0xF400000000ull, 0xF41FFFFFFFull,
            0ull, &s, &e) == 0u);

        /* MC = agp_start + PA, com a faixa inteira dentro da abertura. */
        CHECK(Bc250GcvmAgpSystemPaToMc(0xF800000000ull, 0xFFFFFFFFFFull,
            0x46A000000ull, 0x2000ull, &mc) == 1u);
        CHECK(mc == 0xFC6A000000ull);
        CHECK(Bc250GcvmAgpSystemPaToMc(0xF800000000ull, 0xFFFFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES - 0x2000ull, 0x2000ull, &mc) == 1u);
        CHECK(Bc250GcvmAgpSystemPaToMc(0xF800000000ull, 0xFFFFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES - 0x1000ull, 0x2000ull, &mc) == 0u);
        CHECK(mc == 0ull);
        CHECK(Bc250GcvmAgpSystemPaToMc(0xF800000000ull, 0xFFFFFFFFFFull,
            BC250_GCVM_AGP_D2_BYTES, 0x1000ull, &mc) == 0u);
        CHECK(Bc250GcvmAgpSystemPaToMc(0xF800000000ull, 0xFFFFFFFFFFull,
            0x1000ull, 0ull, &mc) == 0u);
    }

    /* 9. D2 no programa: registradores da AGP e abertura de sistema estendida. */
    config.AgpBytes = BC250_GCVM_AGP_D2_BYTES;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(evidence.AgpEnabled == 1u);
    CHECK(evidence.AgpStart == 0xF800000000ull);
    CHECK(evidence.AgpEnd == 0xFFFFFFFFFFull);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_BASE / 4u] == 0x00000000u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_BOT / 4u] == 0x0000F800u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_TOP / 4u] == 0x0000FFFFu);
    CHECK(f->Registers[BC250_REG_GCMC_VM_SYSTEM_APERTURE_LOW_ADDR / 4u] == 0x003D0000u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR / 4u] == 0x003FFFFFu);
    CHECK(Bc250GcvmGlobalMmuRestore(&io, &evidence) == BC250_GCVM_GLOBAL_MMU_OK);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_BOT / 4u] == evidence.ValuesBefore[1]);

    /* Plano invalido recusa ANTES de tocar em registrador. */
    config.AgpBytes = BC250_GCVM_AGP_D2_BYTES + 0x1000ull;
    f->Registers[BC250_REG_GCMC_VM_AGP_BOT / 4u] = 0x12345678u;
    CHECK(Bc250GcvmGlobalMmuProgram(&config, &io, &evidence) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    CHECK(evidence.FailureStep == 7u);
    CHECK(evidence.SnapshotPresent == 0u);
    CHECK(f->Registers[BC250_REG_GCMC_VM_AGP_BOT / 4u] == 0x12345678u);
    config.AgpBytes = 0ull;

    /* 10. D2 por GART: tabela plana de 512 PTEs. */
    {
        static uint64_t table[BC250_GCVM_GART_ENTRIES];
        uint32_t i;
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF000ull, 0x453B1F000ull, 1u, 2u) == 1u);
        CHECK(table[0] == (0x46FFFF000ull | 0x61u));          /* MC 0 na dummy */
        /* UC.1 (doc 592): MTYPE_UC nos bits 50:48, como gart_pte_flags do Linux. */
        CHECK(table[1] == (0x453B1F000ull | 0x0003000000000077ull)); /* anel */
        CHECK(table[2] == (0x453B20000ull | 0x0003000000000077ull)); /* Work */
        CHECK(((table[1] >> 48) & 7ull) == 3ull);             /* MTYPE_UC */
        CHECK((table[1] & 0x0000FFFFFFFFF000ull) == 0x453B1F000ull); /* endereco intacto */
        for (i = 3u; i < BC250_GCVM_GART_ENTRIES; ++i) {
            CHECK(table[i] == (0x46FFFF000ull | 0x61u));
        }
        /* Recusas: entrada 0, faixa alem da tabela, desalinhado, contagem errada. */
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF000ull, 0x453B1F000ull, 0u, 2u) == 0u);
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF000ull, 0x453B1F000ull, 511u, 2u) == 0u);
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF000ull, 0x453B1F800ull, 1u, 2u) == 0u);
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF800ull, 0x453B1F000ull, 1u, 2u) == 0u);
        CHECK(Bc250GcvmGartBuildTable(table, 256u,
            0x46FFFF000ull, 0x453B1F000ull, 1u, 2u) == 0u);
        CHECK(Bc250GcvmGartBuildTable(table, BC250_GCVM_GART_ENTRIES,
            0x46FFFF000ull, 0x453B1F000ull, 1u, 0u) == 0u);
    }

    /* 11. D2 por GART: contexto 0 programado como gfxhub_v2_0. */
    {
        BC250_GCVM_GART_EVIDENCE gart;
        f->Registers[BC250_REG_GCVM_CONTEXT0_CNTL / 4u] = 0x007FFE80u; /* POST */
        f->Registers[BC250_REG_GCVM_CONTEXT0_PT_BASE_LO / 4u] = 0x11111111u;
        f->Registers[BC250_REG_GCVM_CONTEXT0_PT_END_LO / 4u] = 0x22222222u;
        f->FlushCalls = 0u;
        f->FlushResult = 1u;
        CHECK(Bc250GcvmGartProgram(0x46FF02000ull, &io, &gart) == BC250_GCVM_GLOBAL_MMU_OK);
        CHECK(gart.SnapshotPresent == 1u && gart.ReadbackPassed == 1u && gart.TlbFlushPassed == 1u);
        CHECK(f->FlushCalls == 1u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_BASE_LO / 4u] == 0x6FF02001u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_BASE_HI / 4u] == 0x00000004u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_START_LO / 4u] == 0u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_START_HI / 4u] == 0u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_END_LO / 4u] == 0x1FFu);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_END_HI / 4u] == 0u);
        /* ENABLE=1, DEPTH=0, RETRY=0; os defaults de fault do POST ficam. */
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_CNTL / 4u] == 0x007FFE01u);

        /* Restore: contexto 0 desligado e snapshot de volta. */
        f->FlushCalls = 0u;
        CHECK(Bc250GcvmGartRestore(&io, &gart) == BC250_GCVM_GLOBAL_MMU_OK);
        CHECK(gart.RestorePassed == 1u && f->FlushCalls == 1u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_CNTL / 4u] == 0x007FFE80u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_BASE_LO / 4u] == 0x11111111u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_END_LO / 4u] == 0x22222222u);

        /* Readback divergente no CNTL: rollback devolve o contexto 0 desligado. */
        f->FailReadbackOffset = BC250_REG_GCVM_CONTEXT0_CNTL;
        f->FailReadbackOnce = 1u;
        f->FailReadbackAfterWrite = 0u;
        CHECK(Bc250GcvmGartProgram(0x46FF02000ull, &io, &gart) ==
            BC250_GCVM_GLOBAL_MMU_READBACK_FAILED);
        CHECK(gart.FailureStep == 5u && gart.RollbackPassed == 1u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_CNTL / 4u] == 0x007FFE80u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_PT_BASE_LO / 4u] == 0x11111111u);
        f->FailReadbackOffset = 0u;
        f->FailReadbackAfterWrite = 0u;

        /* Flush da VMID 0 que falha tambem desfaz (e o rollback admite a falha). */
        f->FlushResult = 0u;
        CHECK(Bc250GcvmGartProgram(0x46FF02000ull, &io, &gart) ==
            BC250_GCVM_GLOBAL_MMU_TLB_FAILED);
        CHECK(gart.FailureStep == 6u);
        CHECK(f->Registers[BC250_REG_GCVM_CONTEXT0_CNTL / 4u] == 0x007FFE80u);
        CHECK(gart.RollbackPassed == 0u);
        f->FlushResult = 1u;

        /* Tabela desalinhada recusa antes de qualquer leitura. */
        CHECK(Bc250GcvmGartProgram(0x46FF02800ull, &io, &gart) ==
            BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
        CHECK(gart.SnapshotPresent == 0u);
        /* Restore sem snapshot recusa. */
        CHECK(Bc250GcvmGartRestore(&io, &gart) == BC250_GCVM_GLOBAL_MMU_BAD_ARGUMENT);
    }

    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
