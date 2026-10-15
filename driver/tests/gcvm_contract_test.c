#include "../core/bc250_gcvm_contract.h"
#include "../core/bc250_vram_allocator.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 0; } } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#endif

static BC250_GCVM_PAGE_TABLE_PLAN g_plan;
static uint64_t g_pteBuffer[512];
static BC250_VRAM_ALLOCATOR g_allocator;

static int Bc250RunGcvmContractTest(void)
{
    uint64_t pte = 0;
    uint64_t decodedPhys = 0;
    uint32_t decodedFlags = 0;
    uint64_t translatedPhys = 0;
    BC250_VRAM_TOKEN ptToken;
    BC250_VRAM_REGION region;

    /* 1. Test individual PTE encoding and decoding */
    CHECK(Bc250GcvmEncodePte(
        0x0FD01000ull, BC250_PTE_RW_DEFAULT, &pte) == BC250_GCVM_OK);
    CHECK(pte != 0);
    CHECK((pte & 0x1) == 1); /* valid */
    CHECK((pte & (1ull << 5)) != 0); /* readable */
    CHECK((pte & (1ull << 6)) != 0); /* writeable */
    CHECK((pte & (1ull << 1)) == 0); /* vram (not system) */

    CHECK(Bc250GcvmDecodePte(
        pte, &decodedPhys, &decodedFlags) == BC250_GCVM_OK);
    CHECK(decodedPhys == 0x0FD01000ull);
    CHECK(decodedFlags == BC250_PTE_RW_DEFAULT);

    /* P6 (doc 618): MTYPE_UC = 3 nos bits 50:48; sem a flag, MTYPE = 0 (NC). */
    CHECK((pte & 0x0007000000000000ull) == 0ull);
    CHECK(Bc250GcvmEncodePte(0x0FD01000ull,
        BC250_PTE_RW_DEFAULT | BC250_PTE_MTYPE_UC, &pte) == BC250_GCVM_OK);
    CHECK((pte & 0x0007000000000000ull) == 0x0003000000000000ull);
    CHECK((pte & 0x0000FFFFFFFFF000ull) == 0x0FD01000ull);
    /* O PDE recusa a flag (so VALID/SYSTEM/SNOOPED). */
    CHECK(Bc250GcvmEncodePde(0x0FD01000ull,
        BC250_PTE_VALID | BC250_PTE_MTYPE_UC, &pte) != BC250_GCVM_OK);

    /* Test misaligned encoding rejection */
    CHECK(Bc250GcvmEncodePte(
        0x0FD01001ull, BC250_PTE_RW_DEFAULT, &pte) == BC250_GCVM_MISALIGNED);

    /* 2. Test Page Table planning */
    memset(&g_plan, 0, sizeof(g_plan));
    CHECK(Bc250GcvmBuildPageTablePlan(
        0x0000080000000000ull, /* 8 TB VA */
        0x0FD00000ull,         /* VRAM 0x0FD00000 */
        0x00200000ull,         /* 2 MiB */
        BC250_PTE_RW_DEFAULT,
        &g_plan) == BC250_GCVM_OK);
    CHECK(g_plan.PageCount == 512u);
    CHECK(g_plan.TableSizeBytes == 512u * sizeof(uint64_t));
    CHECK(g_plan.TableAlignment == 4096u);
    CHECK(g_plan.VaEnd == 0x00000800001FFFFFull);

    /* Test misaligned plan arguments */
    {
        BC250_GCVM_PAGE_TABLE_PLAN dummyPlan;
        CHECK(Bc250GcvmBuildPageTablePlan(
            0x0000080000000001ull, 0x0FD00000ull, 0x00200000ull,
            BC250_PTE_RW_DEFAULT, &dummyPlan) == BC250_GCVM_MISALIGNED);
        CHECK(Bc250GcvmBuildPageTablePlan(
            0x0000080000000000ull, 0x0FD00000ull, 0x00200100ull,
            BC250_PTE_RW_DEFAULT, &dummyPlan) == BC250_GCVM_MISALIGNED);
    }

    /* 3. Test Page Table population */
    memset(g_pteBuffer, 0, sizeof(g_pteBuffer));
    CHECK(Bc250GcvmPopulatePageTable(
        &g_plan, g_pteBuffer, sizeof(g_pteBuffer)) == BC250_GCVM_OK);

    /* Verify first PTE (index 0) */
    CHECK(Bc250GcvmDecodePte(
        g_pteBuffer[0], &decodedPhys, &decodedFlags) == BC250_GCVM_OK);
    CHECK(decodedPhys == 0x0FD00000ull);

    /* Verify second PTE (index 1 -> 0x0FD01000) */
    CHECK(Bc250GcvmDecodePte(
        g_pteBuffer[1], &decodedPhys, &decodedFlags) == BC250_GCVM_OK);
    CHECK(decodedPhys == 0x0FD01000ull);

    /* Verify last PTE (index 511 -> 0x0FEFF000) */
    CHECK(Bc250GcvmDecodePte(
        g_pteBuffer[511], &decodedPhys, &decodedFlags) == BC250_GCVM_OK);
    CHECK(decodedPhys == 0x0FEFF000ull);

    /* 4. Test Translation */
    /* Exact page match (VA 8 TB + 4 KiB -> VRAM 0x0FD01000) */
    CHECK(Bc250GcvmTranslateVa(
        &g_plan, g_pteBuffer, 0x0000080000001000ull,
        &translatedPhys) == BC250_GCVM_OK);
    CHECK(translatedPhys == 0x0FD01000ull);

    /* In-page offset translation */
    CHECK(Bc250GcvmTranslateVa(
        &g_plan, g_pteBuffer, 0x0000080000001ABCull,
        &translatedPhys) == BC250_GCVM_OK);
    CHECK(translatedPhys == 0x0FD01ABCull);

    /* Last page in-page offset translation */
    CHECK(Bc250GcvmTranslateVa(
        &g_plan, g_pteBuffer, 0x00000800001FF123ull,
        &translatedPhys) == BC250_GCVM_OK);
    CHECK(translatedPhys == 0x0FEFF123ull);

    /* Fault tests (out of range VA) */
    CHECK(Bc250GcvmTranslateVa(
        &g_plan, g_pteBuffer, 0x000007FFFFFFFFFFull,
        &translatedPhys) == BC250_GCVM_TRANSLATION_FAULT);
    CHECK(Bc250GcvmTranslateVa(
        &g_plan, g_pteBuffer, 0x0000080000200000ull,
        &translatedPhys) == BC250_GCVM_TRANSLATION_FAULT);

    /* 5. Integration with VRAM Allocator */
    memset(&g_allocator, 0, sizeof(g_allocator));
    CHECK(Bc250VramAllocatorInit(
        &g_allocator, 0x20000000ull, 0x10000000ull) == BC250_VRAM_OK);
    CHECK(Bc250VramAllocate(
        &g_allocator, g_plan.TableSizeBytes, g_plan.TableAlignment,
        BC250_VRAM_ROLE_PAGE_TABLE, 0, &ptToken) == BC250_VRAM_OK);
    CHECK(Bc250VramGetRegion(&g_allocator, ptToken, &region) ==
        BC250_VRAM_OK);
    CHECK(region.Role == BC250_VRAM_ROLE_PAGE_TABLE);
    CHECK(region.Length >= g_plan.TableSizeBytes);

    /*
     * P6_PTE_BIT_LAYOUT_OFFLINE
     *
     * Layout do Navi 10 (gmc_v10_0.c, amdgpu_vm.h):
     *   11:7 fragment | 6 write | 5 read | 4 exe | 3 Z | 2 snooped
     *   1 system | 0 valid
     *
     * Ate 05/09 EXECUTABLE era 0x80 codificado em (1ull << 7): ligava FRAG[0]
     * e nunca ligava a permissao de execucao. A PTE do IB saia 0xE7 no lugar
     * de 0x77 -- e a busca de um indirect buffer e leitura de INSTRUCAO.
     */
    {
        uint64_t layoutPte = 0;
        uint64_t layoutPhys = 0;
        uint32_t layoutFlags = 0;

        CHECK(Bc250GcvmEncodePte(0x450A91000ull,
            BC250_PTE_VALID | BC250_PTE_SYSTEM | BC250_PTE_SNOOPED |
            BC250_PTE_EXECUTABLE | BC250_PTE_READABLE | BC250_PTE_WRITEABLE,
            &layoutPte) == BC250_GCVM_OK);
        CHECK((layoutPte & 0xFFFull) == 0x77ull);

        /* Nenhum bit de fragmento pode sair ligado numa pagina de 4 KiB. */
        CHECK((layoutPte & 0x0000000000000F80ull) == 0ull);

        /* Ida e volta preserva a permissao de execucao. */
        CHECK(Bc250GcvmDecodePte(layoutPte, &layoutPhys, &layoutFlags) == BC250_GCVM_OK);
        CHECK((layoutFlags & BC250_PTE_EXECUTABLE) != 0u);
        CHECK(layoutPhys == 0x450A91000ull);

        /* Sem EXECUTABLE o bit 4 fica limpo -- e o caso que travava o IB. */
        CHECK(Bc250GcvmEncodePte(0x450A91000ull,
            BC250_PTE_VALID | BC250_PTE_READABLE | BC250_PTE_WRITEABLE,
            &layoutPte) == BC250_GCVM_OK);
        CHECK((layoutPte & 0xFFFull) == 0x61ull);
        CHECK((layoutPte & (1ull << 4)) == 0ull);
    }

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGcvmContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGcvmContractTest() ? 0 : 1;
}
#endif
