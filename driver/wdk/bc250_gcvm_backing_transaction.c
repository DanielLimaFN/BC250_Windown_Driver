#include "bc250_gcvm_backing_transaction.h"

BC250_GCVM_BACKING_STATUS Bc250RunGcvmBackingTransaction(
    BC250_VRAM_ALLOCATOR *Allocator,
    uint64_t VramBaseOffset,
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t VramApertureBarIndex,
    BC250_GCVM_BACKING_EXECUTE Execute,
    void *ExecuteContext,
    BC250_GCVM_BACKING_EVIDENCE *Evidence)
{
    BC250_VRAM_TOKEN targetToken = {0}, tableToken = {0};
    BC250_VRAM_REGION targetRegion, tableRegion;
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    PVOID mapping = NULL;
    uint64_t *backup = NULL;
    volatile uint64_t *table;
    uint64_t firstPte = 0;
    NTSTATUS callbackStatus;
    BC250_GCVM_BACKING_STATUS result = BC250_GCVM_BACKING_OK;
    uint32_t targetAllocated = 0u, tableAllocated = 0u;
    size_t i, matches = 0u;

    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (Allocator == NULL || DxgkInterface == NULL || ResolvedBars == NULL ||
        ResolvedBarCount == 0u || Execute == NULL || Evidence == NULL ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbMapMemory == NULL ||
        DxgkInterface->DxgkCbUnmapMemory == NULL) {
        return BC250_GCVM_BACKING_BAD_ARGUMENT;
    }
    vramStatus = Bc250VramAllocate(Allocator, BC250_GCVM_BACKING_BYTES,
        BC250_GCVM_BACKING_BYTES, BC250_VRAM_ROLE_TEST_BUFFER,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
        &targetToken);
    if (vramStatus != BC250_VRAM_OK) return BC250_GCVM_BACKING_ALLOCATION_FAILED;
    targetAllocated = 1u;
    vramStatus = Bc250VramAllocate(Allocator, BC250_GCVM_BACKING_BYTES,
        BC250_GCVM_BACKING_BYTES, BC250_VRAM_ROLE_PAGE_TABLE,
        BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
        &tableToken);
    if (vramStatus != BC250_VRAM_OK) {
        result = BC250_GCVM_BACKING_ALLOCATION_FAILED;
        goto Cleanup;
    }
    tableAllocated = 1u;
    if (Bc250VramGetRegion(Allocator, targetToken, &targetRegion) != BC250_VRAM_OK ||
        Bc250VramGetRegion(Allocator, tableToken, &tableRegion) != BC250_VRAM_OK) {
        result = BC250_GCVM_BACKING_REGION_FAILED;
        goto Cleanup;
    }
    for (i = 0u; i < ResolvedBarCount; ++i) {
        if (ResolvedBars[i].Index == VramApertureBarIndex) {
            bar = &ResolvedBars[i];
            ++matches;
        }
    }
    if (matches != 1u || bar == NULL ||
        tableRegion.Offset > bar->Length ||
        BC250_GCVM_BACKING_BYTES > bar->Length - tableRegion.Offset ||
        bar->Start > MAXULONGLONG - tableRegion.Offset ||
        VramBaseOffset > MAXULONGLONG - tableRegion.Offset ||
        VramBaseOffset > MAXULONGLONG - targetRegion.Offset) {
        result = BC250_GCVM_BACKING_APERTURE_INVALID;
        goto Cleanup;
    }
    Evidence->PageTableOffset = tableRegion.Offset;
    Evidence->PageTablePhysical = VramBaseOffset + tableRegion.Offset;
    Evidence->TargetOffset = targetRegion.Offset;
    Evidence->TargetPhysical = VramBaseOffset + targetRegion.Offset;
    Evidence->PageTableBytes = BC250_GCVM_BACKING_BYTES;
    Evidence->OwnershipPassed = 1u;
    physical.QuadPart = (LONGLONG)(bar->Start + tableRegion.Offset);
    callbackStatus = DxgkInterface->DxgkCbMapMemory(
        DxgkInterface->DeviceHandle, physical, BC250_GCVM_BACKING_BYTES,
        FALSE, FALSE, MmNonCached, &mapping);
    if (!NT_SUCCESS(callbackStatus) || mapping == NULL) {
        result = BC250_GCVM_BACKING_MAP_FAILED;
        goto Cleanup;
    }
    backup = (uint64_t *)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, BC250_GCVM_BACKING_BYTES,
        BC250_GCVM_BACKING_POOL_TAG);
    if (backup == NULL) {
        result = BC250_GCVM_BACKING_POOL_FAILED;
        goto Cleanup;
    }
    if (Bc250GcvmEncodePte(Evidence->TargetPhysical,
            BC250_PTE_RW_DEFAULT, &firstPte) != BC250_GCVM_OK) {
        result = BC250_GCVM_BACKING_PTE_FAILED;
        goto Cleanup;
    }
    Evidence->FirstPte = firstPte;
    table = (volatile uint64_t *)mapping;
    for (i = 0u; i < BC250_GCVM_BACKING_BYTES / sizeof(uint64_t); ++i) {
        backup[i] = table[i];
        table[i] = i == 0u ? firstPte : 0u;
    }
    KeMemoryBarrier();
    for (i = 0u; i < BC250_GCVM_BACKING_BYTES / sizeof(uint64_t); ++i) {
        uint64_t expected = i == 0u ? firstPte : 0u;
        if (table[i] != expected) {
            result = BC250_GCVM_BACKING_READBACK_FAILED;
            goto Restore;
        }
    }
    Evidence->WriteReadbackPassed = 1u;
    if (!Execute(ExecuteContext, Evidence->PageTablePhysical)) {
        result = BC250_GCVM_BACKING_EXECUTE_FAILED;
        goto Restore;
    }
    Evidence->ExecutePassed = 1u;

Restore:
    for (i = 0u; i < BC250_GCVM_BACKING_BYTES / sizeof(uint64_t); ++i) {
        table[i] = backup[i];
    }
    KeMemoryBarrier();
    Evidence->RestorePassed = 1u;
    for (i = 0u; i < BC250_GCVM_BACKING_BYTES / sizeof(uint64_t); ++i) {
        if (table[i] != backup[i]) {
            Evidence->RestorePassed = 0u;
            result = BC250_GCVM_BACKING_RESTORE_FAILED;
            break;
        }
    }

Cleanup:
    if (backup != NULL) ExFreePoolWithTag(backup, BC250_GCVM_BACKING_POOL_TAG);
    if (mapping != NULL) {
        callbackStatus = DxgkInterface->DxgkCbUnmapMemory(
            DxgkInterface->DeviceHandle, mapping);
        if (!NT_SUCCESS(callbackStatus) && result == BC250_GCVM_BACKING_OK) {
            result = BC250_GCVM_BACKING_UNMAP_FAILED;
        }
    }
    if (tableAllocated != 0u &&
        Bc250VramRelease(Allocator, tableToken) != BC250_VRAM_OK &&
        result == BC250_GCVM_BACKING_OK) {
        result = BC250_GCVM_BACKING_RELEASE_FAILED;
    }
    if (targetAllocated != 0u &&
        Bc250VramRelease(Allocator, targetToken) != BC250_VRAM_OK &&
        result == BC250_GCVM_BACKING_OK) {
        result = BC250_GCVM_BACKING_RELEASE_FAILED;
    }
    return result;
}
