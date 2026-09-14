#include "../core/bc250_golden_settings_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static BC250_GOLDEN_SNAPSHOT g_testSnapshot;
static BC250_GOLDEN_TRANSACTION_LOG g_testLog;
static uint32_t g_restoreOffsets[BC250_GOLDEN_SETTINGS_COUNT];
static uint32_t g_restoreValues[BC250_GOLDEN_SETTINGS_COUNT];

static int Bc250RunGoldenSettingsContractTest(void)
{
    uint32_t count = 0;
    const BC250_GOLDEN_ENTRY *table = NULL;
    uint32_t rmwResult;
    uint32_t outCount = 0;
    uint32_t i;

    /* 1. Test table metadata */
    table = Bc250GoldenSettingsGetTable(&count);
    CHECK(table != NULL);
    CHECK(count == 34u);

    /* Check entry 0 (GRBM_GFX_INDEX) */
    CHECK(table[0].BaseIndex == 1u);
    CHECK(table[0].BarOffset == 0x00030800u);
    CHECK(table[0].Mask == 0xFFFFFFFFu);
    CHECK(table[0].ValueOr == 0xE0000000u);

    /* Check entry 5 (CB_HW_CONTROL_4) */
    CHECK(table[5].BaseIndex == 0u);
    CHECK(table[5].BarOffset == 0x00009A08u);
    CHECK(table[5].Mask == 0x00008000u);
    CHECK(table[5].ValueOr == 0x003C8014u);

    /* Check entry 33 (UTCL1_CTRL) */
    CHECK(table[33].BaseIndex == 0u);
    CHECK(table[33].BarOffset == 0x00009FA0u);
    CHECK(table[33].Mask == 0x00800000u);
    CHECK(table[33].ValueOr == 0x00800000u);

    /* 2. Test RMW logic */
    /* Full mask: output equals ValueOr */
    rmwResult = Bc250GoldenSettingsEvaluateRmw(0x12345678u, 0xFFFFFFFFu, 0xE0000000u);
    CHECK(rmwResult == 0xE0000000u);

    /* Partial mask: CB_HW_CONTROL_4 only modifies bit 15 */
    rmwResult = Bc250GoldenSettingsEvaluateRmw(0x12340000u, 0x00008000u, 0x003C8014u);
    CHECK(rmwResult == 0x12348000u);

    /* Partial mask: RMI_SPARE (Mask 0xFFFF3109, ValueOr 0xFFFF3101) */
    rmwResult = Bc250GoldenSettingsEvaluateRmw(0x0000CEF6u, 0xFFFF3109u, 0xFFFF3101u);
    /* Unmasked bits: 0x0000CEF6 & ~0xFFFF3109 = 0x0000CEF6 & 0x0000CEF6 = 0x0000CEF6 */
    /* Masked bits:   0xFFFF3101 & 0xFFFF3109 = 0xFFFF3101 */
    CHECK(rmwResult == (0x0000CEF6u | 0xFFFF3101u));

    /* 3. Test Snapshot stability verification */
    memset(&g_testSnapshot, 0, sizeof(g_testSnapshot));
    for (i = 0; i < BC250_GOLDEN_SETTINGS_COUNT; ++i) {
        g_testSnapshot.Pass1Values[i] = 0x1000u + i;
        g_testSnapshot.Pass2Values[i] = 0x1000u + i;
    }
    g_testSnapshot.CpgPspDebugPass1 = 0x1234u;
    g_testSnapshot.CpgPspDebugPass2 = 0x1234u;
    g_testSnapshot.CpcPspDebugPass1 = 0x5678u;
    g_testSnapshot.CpcPspDebugPass2 = 0x5678u;

    CHECK(Bc250GoldenSettingsVerifySnapshotStability(&g_testSnapshot) == BC250_GOLDEN_OK);

    /* Inject instability in pass 2 */
    g_testSnapshot.Pass2Values[10] = 0x9999u;
    CHECK(Bc250GoldenSettingsVerifySnapshotStability(&g_testSnapshot) == BC250_GOLDEN_UNSTABLE_SNAPSHOT);
    g_testSnapshot.Pass2Values[10] = 0x1000u + 10u; /* Restore */

    /* 4. Test Apply and Rollback Plan generation */
    memset(&g_testLog, 0, sizeof(g_testLog));
    CHECK(Bc250GoldenSettingsBuildApplyPlan(&g_testSnapshot, &g_testLog) == BC250_GOLDEN_OK);
    CHECK(g_testLog.ItemCount == 34u);
    CHECK(g_testLog.Items[0].BarOffset == 0x00030800u);
    CHECK(g_testLog.Items[0].OriginalValue == 0x1000u);
    CHECK(g_testLog.Items[0].ModifiedValue == 0xE0000000u);

    /* Generate rollback plan */
    memset(g_restoreOffsets, 0, sizeof(g_restoreOffsets));
    memset(g_restoreValues, 0, sizeof(g_restoreValues));
    CHECK(Bc250GoldenSettingsBuildRollbackPlan(
        &g_testLog, g_restoreOffsets, g_restoreValues,
        BC250_GOLDEN_SETTINGS_COUNT, &outCount) == BC250_GOLDEN_OK);
    CHECK(outCount == 34u);

    /* Verify LIFO reverse order restore */
    CHECK(g_restoreOffsets[0] == table[33].BarOffset);
    CHECK(g_restoreValues[0] == g_testSnapshot.Pass1Values[33]);
    CHECK(g_restoreOffsets[33] == table[0].BarOffset);
    CHECK(g_restoreValues[33] == g_testSnapshot.Pass1Values[0]);

    /* 5. Error handling */
    CHECK(Bc250GoldenSettingsVerifySnapshotStability(NULL) == BC250_GOLDEN_BAD_ARGUMENT);
    CHECK(Bc250GoldenSettingsBuildApplyPlan(NULL, &g_testLog) == BC250_GOLDEN_BAD_ARGUMENT);
    CHECK(Bc250GoldenSettingsBuildRollbackPlan(
        &g_testLog, g_restoreOffsets, g_restoreValues, 10u /* too small */, &outCount) == BC250_GOLDEN_BAD_ARGUMENT);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGoldenSettingsContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGoldenSettingsContractTest() ? 0 : 1;
}
#endif
