#include "bc250_vram_probe_transaction.h"

BC250_VRAM_PROBE_STATUS Bc250RunVramProbeTransaction(
    const DXGKRNL_INTERFACE *DxgkInterface,
    const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    size_t ResolvedBarCount,
    uint32_t VramApertureBarIndex,
    uint64_t Offset,
    BC250_VRAM_PROBE_EVIDENCE *Evidence)
{
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    PVOID mapping = NULL;
    volatile UCHAR *target;
    UCHAR original[BC250_VRAM_PROBE_BYTES];
    UCHAR pattern[BC250_VRAM_PROBE_BYTES];
    NTSTATUS status;
    size_t count = 0, i;
    BC250_VRAM_PROBE_STATUS result = BC250_VRAM_PROBE_OK;

    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (DxgkInterface == NULL || ResolvedBars == NULL ||
        ResolvedBarCount == 0 || Evidence == NULL ||
        DxgkInterface->DeviceHandle == NULL ||
        DxgkInterface->DxgkCbMapMemory == NULL ||
        DxgkInterface->DxgkCbUnmapMemory == NULL) {
        return BC250_VRAM_PROBE_BAD_ARGUMENT;
    }
    for (i = 0; i < ResolvedBarCount; ++i) {
        if (ResolvedBars[i].Index == VramApertureBarIndex) {
            bar = &ResolvedBars[i];
            ++count;
        }
    }
    if (count != 1 || bar == NULL || bar->Length == 0) {
        return BC250_VRAM_PROBE_APERTURE_NOT_UNIQUE;
    }
    if (bar->Length > MAXULONG || Offset > bar->Length ||
        BC250_VRAM_PROBE_BYTES > bar->Length - Offset) {
        return BC250_VRAM_PROBE_OUT_OF_RANGE;
    }
    physical.QuadPart = (LONGLONG)bar->Start;
    status = DxgkInterface->DxgkCbMapMemory(
        DxgkInterface->DeviceHandle, physical, (ULONG)bar->Length,
        FALSE, FALSE, MmNonCached, &mapping);
    Evidence->MapStatus = status;
    Evidence->Offset = Offset;
    Evidence->Bytes = BC250_VRAM_PROBE_BYTES;
    if (!NT_SUCCESS(status)) return BC250_VRAM_PROBE_MAP_FAILED;
    if (mapping == NULL) return BC250_VRAM_PROBE_MAP_RETURNED_NULL;

    target = (volatile UCHAR *)mapping + (SIZE_T)Offset;
    for (i = 0; i < BC250_VRAM_PROBE_BYTES; ++i) {
        original[i] = target[i];
        pattern[i] = (UCHAR)(original[i] ^ (UCHAR)(0xA5u + (UCHAR)i));
        target[i] = pattern[i];
    }
    KeMemoryBarrier();
    for (i = 0; i < BC250_VRAM_PROBE_BYTES; ++i) {
        if (target[i] != pattern[i]) {
            result = BC250_VRAM_PROBE_WRITE_READBACK_FAILED;
            break;
        }
    }
    if (result == BC250_VRAM_PROBE_OK) Evidence->WriteReadbackPassed = 1u;

    for (i = 0; i < BC250_VRAM_PROBE_BYTES; ++i) target[i] = original[i];
    KeMemoryBarrier();
    Evidence->RestorePassed = 1u;
    for (i = 0; i < BC250_VRAM_PROBE_BYTES; ++i) {
        if (target[i] != original[i]) {
            Evidence->RestorePassed = 0u;
            result = BC250_VRAM_PROBE_RESTORE_FAILED;
            break;
        }
    }
    status = DxgkInterface->DxgkCbUnmapMemory(
        DxgkInterface->DeviceHandle, mapping);
    Evidence->UnmapStatus = status;
    if (!NT_SUCCESS(status)) return BC250_VRAM_PROBE_UNMAP_FAILED;
    return result;
}
