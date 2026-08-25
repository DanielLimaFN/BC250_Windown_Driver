#ifndef BC250_GOLDEN_SETTINGS_CONTRACT_H
#define BC250_GOLDEN_SETTINGS_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GOLDEN_SETTINGS_COUNT 34u

#define BC250_REG_CPG_PSP_DEBUG_OFFSET 0x0000C6F0u
#define BC250_REG_CPC_PSP_DEBUG_OFFSET 0x0000C6F4u

typedef enum BC250_GOLDEN_STATUS {
    BC250_GOLDEN_OK = 0,
    BC250_GOLDEN_BAD_ARGUMENT,
    BC250_GOLDEN_UNSTABLE_SNAPSHOT,
    BC250_GOLDEN_MISMATCHED_COUNT,
    BC250_GOLDEN_BROADCAST_BLOCKED
} BC250_GOLDEN_STATUS;

typedef struct BC250_GOLDEN_ENTRY {
    const char *Name;
    uint32_t BaseIndex;
    uint32_t BarOffset;
    uint32_t Mask;
    uint32_t ValueOr;
} BC250_GOLDEN_ENTRY;

typedef struct BC250_GOLDEN_SNAPSHOT {
    uint32_t Pass1Values[BC250_GOLDEN_SETTINGS_COUNT];
    uint32_t Pass2Values[BC250_GOLDEN_SETTINGS_COUNT];
    uint32_t CpgPspDebugPass1;
    uint32_t CpgPspDebugPass2;
    uint32_t CpcPspDebugPass1;
    uint32_t CpcPspDebugPass2;
    uint32_t IsStable;
} BC250_GOLDEN_SNAPSHOT;

typedef struct BC250_GOLDEN_TRANSACTION_ITEM {
    uint32_t BarOffset;
    uint32_t OriginalValue;
    uint32_t ModifiedValue;
    uint32_t Mask;
} BC250_GOLDEN_TRANSACTION_ITEM;

typedef struct BC250_GOLDEN_TRANSACTION_LOG {
    BC250_GOLDEN_TRANSACTION_ITEM Items[BC250_GOLDEN_SETTINGS_COUNT];
    uint32_t ItemCount;
    uint32_t IsApplied;
} BC250_GOLDEN_TRANSACTION_LOG;

const BC250_GOLDEN_ENTRY *Bc250GoldenSettingsGetTable(uint32_t *OutCount);

uint32_t Bc250GoldenSettingsEvaluateRmw(
    uint32_t CurrentValue,
    uint32_t Mask,
    uint32_t ValueOr);

BC250_GOLDEN_STATUS Bc250GoldenSettingsVerifySnapshotStability(
    const BC250_GOLDEN_SNAPSHOT *Snapshot);

BC250_GOLDEN_STATUS Bc250GoldenSettingsBuildApplyPlan(
    const BC250_GOLDEN_SNAPSHOT *Snapshot,
    BC250_GOLDEN_TRANSACTION_LOG *Log);

BC250_GOLDEN_STATUS Bc250GoldenSettingsBuildRollbackPlan(
    const BC250_GOLDEN_TRANSACTION_LOG *Log,
    uint32_t *OutOffsets,
    uint32_t *OutRestoreValues,
    uint32_t MaxCount,
    uint32_t *OutCount);

const char *Bc250GoldenStatusName(BC250_GOLDEN_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
