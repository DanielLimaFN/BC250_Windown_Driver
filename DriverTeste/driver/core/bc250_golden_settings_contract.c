#include "bc250_golden_settings_contract.h"

static const BC250_GOLDEN_ENTRY g_Bc250GoldenSettingsTable[BC250_GOLDEN_SETTINGS_COUNT] = {
    { "GRBM_GFX_INDEX",                    1u, 0x00030800u, 0xFFFFFFFFu, 0xE0000000u },
    { "GE_FAST_CLKS",                      0u, 0x00008920u, 0x3FFFFFFFu, 0x0000493Eu },
    { "CGTT_CPF_CLK_CTRL",                 1u, 0x0003C2C4u, 0xFCFF8FFFu, 0xF8000100u },
    { "CGTT_SPI_CLK_CTRL",                 1u, 0x0003C200u, 0xFF7F0FFFu, 0x3C000100u },
    { "CB_HW_CONTROL_3",                   0u, 0x00009A0Cu, 0xA0000000u, 0xA0000000u },
    { "CB_HW_CONTROL_4",                   0u, 0x00009A08u, 0x00008000u, 0x003C8014u },
    { "CH_DRAM_BURST_CTRL",                1u, 0x00033610u, 0x00000010u, 0x00000017u },
    { "CH_PIPE_STEER",                     1u, 0x00033640u, 0xFFFFFFFFu, 0xD8D8D8D8u },
    { "CH_VC5_ENABLE",                     1u, 0x00033650u, 0x00000003u, 0x00000003u },
    { "CP_SD_CNTL",                        0u, 0x0000C6DCu, 0x800007FFu, 0x000005FFu },
    { "DB_DEBUG",                          0u, 0x00009830u, 0xFFFFFFFFu, 0x20000000u },
    { "DB_DEBUG3",                         0u, 0x00009838u, 0xFFFFFFFFu, 0x00000200u },
    { "DB_DEBUG4",                         0u, 0x0000983Cu, 0xFFFFFFFFu, 0x04800000u },
    { "DB_LAST_OF_BURST_CONFIG",           0u, 0x00009868u, 0xFFFFFFFFu, 0x03860210u },
    { "GB_ADDR_CONFIG",                    0u, 0x000098F8u, 0x0C1800FFu, 0x00000044u },
    { "GCR_GENERAL_CNTL",                  0u, 0x00009F8Cu, 0x00009D00u, 0x00008500u },
    { "GCMC_VM_CACHEABLE_DRAM_ADDRESS_END",0u, 0x0000A5C8u, 0xFFFFFFFFu, 0x000FFFFFu },
    { "GL1_DRAM_BURST_CTRL",               1u, 0x00033410u, 0x00000010u, 0x00000017u },
    { "GL1_PIPE_STEER",                    1u, 0x00033440u, 0xFCFCFCFCu, 0xD8D8D8D8u },
    { "GL2_PIPE_STEER_0",                  1u, 0x00033894u, 0x77707770u, 0x21302130u },
    { "GL2_PIPE_STEER_1",                  1u, 0x00033898u, 0x77707770u, 0x21302130u },
    { "GL2A_ADDR_MATCH_MASK",              1u, 0x00033884u, 0xFFFFFFFFu, 0xFFFFFFCFu },
    { "GL2C_ADDR_MATCH_MASK",              1u, 0x0003380Cu, 0xFFFFFFFFu, 0xFFFFFFCFu },
    { "GL2C_CGTT_SCLK_CTRL",               1u, 0x0003C2B0u, 0x10000000u, 0x10000100u },
    { "GL2C_CTRL2",                        1u, 0x00033804u, 0xFC02002Fu, 0x9402002Fu },
    { "GL2C_CTRL3",                        1u, 0x00033830u, 0x00002188u, 0x00000188u },
    { "PA_SC_ENHANCE",                     0u, 0x00008BF0u, 0x08000009u, 0x08000009u },
    { "PA_SC_BINNER_EVENT_CNTL_0",         0u, 0x00008B30u, 0xCC3FCC03u, 0x842A4C02u },
    { "PA_SC_LINE_STIPPLE_STATE",          1u, 0x00030A04u, 0x0000000Fu, 0x00000000u },
    { "RMI_SPARE",                         0u, 0x00009E7Cu, 0xFFFF3109u, 0xFFFF3101u },
    { "SQ_ARB_CONFIG",                     0u, 0x00008C30u, 0x00000100u, 0x00000130u },
    { "SQ_LDS_CLK_CTRL",                   1u, 0x0003C240u, 0xFFFFFFFFu, 0xFFFFFFFFu },
    { "TA_CNTL_AUX",                       0u, 0x00009508u, 0x00030008u, 0x01030000u },
    { "UTCL1_CTRL",                        0u, 0x00009FA0u, 0x00800000u, 0x00800000u }
};

const BC250_GOLDEN_ENTRY *Bc250GoldenSettingsGetTable(uint32_t *OutCount)
{
    if (OutCount != NULL) {
        *OutCount = BC250_GOLDEN_SETTINGS_COUNT;
    }
    return g_Bc250GoldenSettingsTable;
}

uint32_t Bc250GoldenSettingsEvaluateRmw(
    uint32_t CurrentValue,
    uint32_t Mask,
    uint32_t ValueOr)
{
    return (CurrentValue & ~Mask) | (ValueOr & Mask);
}

BC250_GOLDEN_STATUS Bc250GoldenSettingsVerifySnapshotStability(
    const BC250_GOLDEN_SNAPSHOT *Snapshot)
{
    uint32_t i;

    if (Snapshot == NULL) {
        return BC250_GOLDEN_BAD_ARGUMENT;
    }

    for (i = 0; i < BC250_GOLDEN_SETTINGS_COUNT; ++i) {
        if (Snapshot->Pass1Values[i] != Snapshot->Pass2Values[i]) {
            return BC250_GOLDEN_UNSTABLE_SNAPSHOT;
        }
    }

    if (Snapshot->CpgPspDebugPass1 != Snapshot->CpgPspDebugPass2 ||
        Snapshot->CpcPspDebugPass1 != Snapshot->CpcPspDebugPass2) {
        return BC250_GOLDEN_UNSTABLE_SNAPSHOT;
    }

    return BC250_GOLDEN_OK;
}

BC250_GOLDEN_STATUS Bc250GoldenSettingsBuildApplyPlan(
    const BC250_GOLDEN_SNAPSHOT *Snapshot,
    BC250_GOLDEN_TRANSACTION_LOG *Log)
{
    BC250_GOLDEN_STATUS status;
    uint32_t i;

    if (Snapshot == NULL || Log == NULL) {
        return BC250_GOLDEN_BAD_ARGUMENT;
    }

    status = Bc250GoldenSettingsVerifySnapshotStability(Snapshot);
    if (status != BC250_GOLDEN_OK) {
        return status;
    }

    for (i = 0; i < BC250_GOLDEN_SETTINGS_COUNT; ++i) {
        const BC250_GOLDEN_ENTRY *entry = &g_Bc250GoldenSettingsTable[i];
        uint32_t currentVal = Snapshot->Pass1Values[i];
        uint32_t modifiedVal = Bc250GoldenSettingsEvaluateRmw(
            currentVal, entry->Mask, entry->ValueOr);

        Log->Items[i].BarOffset = entry->BarOffset;
        Log->Items[i].OriginalValue = currentVal;
        Log->Items[i].ModifiedValue = modifiedVal;
        Log->Items[i].Mask = entry->Mask;
    }

    Log->ItemCount = BC250_GOLDEN_SETTINGS_COUNT;
    Log->IsApplied = 0;

    return BC250_GOLDEN_OK;
}

BC250_GOLDEN_STATUS Bc250GoldenSettingsBuildRollbackPlan(
    const BC250_GOLDEN_TRANSACTION_LOG *Log,
    uint32_t *OutOffsets,
    uint32_t *OutRestoreValues,
    uint32_t MaxCount,
    uint32_t *OutCount)
{
    uint32_t i;

    if (Log == NULL || OutOffsets == NULL || OutRestoreValues == NULL || OutCount == NULL) {
        return BC250_GOLDEN_BAD_ARGUMENT;
    }
    if (MaxCount < Log->ItemCount) {
        return BC250_GOLDEN_BAD_ARGUMENT;
    }

    /* Reverse order (LIFO) for clean hardware state restore */
    for (i = 0; i < Log->ItemCount; ++i) {
        uint32_t srcIdx = Log->ItemCount - 1u - i;
        OutOffsets[i] = Log->Items[srcIdx].BarOffset;
        OutRestoreValues[i] = Log->Items[srcIdx].OriginalValue;
    }

    *OutCount = Log->ItemCount;
    return BC250_GOLDEN_OK;
}

const char *Bc250GoldenStatusName(BC250_GOLDEN_STATUS Status)
{
    switch (Status) {
    case BC250_GOLDEN_OK: return "ok";
    case BC250_GOLDEN_BAD_ARGUMENT: return "bad_argument";
    case BC250_GOLDEN_UNSTABLE_SNAPSHOT: return "unstable_snapshot";
    case BC250_GOLDEN_MISMATCHED_COUNT: return "mismatched_count";
    case BC250_GOLDEN_BROADCAST_BLOCKED: return "broadcast_blocked";
    default: return "unknown";
    }
}
