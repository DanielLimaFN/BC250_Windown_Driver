#!/usr/bin/env python3
"""Audit the structural MCDM/GPUVA callback matrix for the BC-250 KMD.

This gate proves only that required function pointers are populated and that
display-only/prohibited entries remain absent. It does not prove the semantic
implementation of any DDI or authorize StartDevice to succeed.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]

MINIMUM_MCDM = {
    "DxgkDdiAddDevice",
    "DxgkDdiBuildPagingBuffer",
    "DxgkDdiCalibrateGpuClock",
    "DxgkDdiCloseAllocation",
    "DxgkDdiCollectDbgInfo",
    "DxgkDdiCreateAllocation",
    "DxgkDdiCreateContext",
    "DxgkDdiCreateDevice",
    "DxgkDdiDescribeAllocation",
    "DxgkDdiDestroyAllocation",
    "DxgkDdiDestroyContext",
    "DxgkDdiDestroyDevice",
    "DxgkDdiDpcRoutine",
    "DxgkDdiFormatHistoryBuffer",
    "DxgkDdiGetNodeMetadata",
    "DxgkDdiGetStandardAllocationDriverData",
    "DxgkDdiInterruptRoutine",
    "DxgkDdiOpenAllocation",
    "DxgkDdiPreemptCommand",
    "DxgkDdiQueryAdapterInfo",
    "DxgkDdiQueryDependentEngineGroup",
    "DxgkDdiQueryDeviceDescriptor",
    "DxgkDdiQueryEngineStatus",
    "DxgkDdiRemoveDevice",
    "DxgkDdiResetDevice",
    "DxgkDdiResetEngine",
    "DxgkDdiResetFromTimeout",
    "DxgkDdiRestartFromTimeout",
    "DxgkDdiSetPowerState",
    "DxgkDdiSetStablePowerState",
    "DxgkDdiSetVirtualMachineData",
    "DxgkDdiStartDevice",
    "DxgkDdiStopDevice",
    "DxgkDdiUnload",
    "DxgkDdiQueryChildStatus",
    "DxgkDdiQueryChildRelations",
}

# MCDM requires virtual addressing. These five callbacks are the WDDM GPUVA
# extension to the minimum table.
GPUVA_REQUIRED = {
    "DxgkDdiCreateProcess",
    "DxgkDdiDestroyProcess",
    "DxgkDdiGetRootPageTableSize",
    "DxgkDdiSetRootPageTable",
    "DxgkDdiSubmitCommandVirtual",
}

# Local activation evidence showed Dxgkrnl rejecting a null dispatch callback,
# so it remains part of this project's compatibility baseline.
LOCAL_COMPATIBILITY = {"DxgkDdiDispatchIoRequest"}

# Microsoft Learn currently lists QueryConnectionChange both in the minimum
# section and in the explicit "must not be provided" section. The explicit
# prohibition is the safer and more specific rule for a render-only MCDM KMD.
PROHIBITED = {
    "DxgkDdiAcquireSwizzlingRange",
    "DxgkDdiCheckMultiPlaneOverlaySupport",
    "DxgkDdiCheckMultiPlaneOverlaySupport2",
    "DxgkDdiCheckMultiPlaneOverlaySupport3",
    "DxgkDdiCommitVidPn",
    "DxgkDdiControlInterrupt",
    "DxgkDdiControlInterrupt2",
    "DxgkDdiCreateOverlay",
    "DxgkDdiDestroyOverlay",
    "DxgkDdiDisplayDetectControl",
    "DxgkDdiEnumVidPnCofuncModality",
    "DxgkDdiFlipOverlay",
    "DxgkDdiGetScanLine",
    "DxgkDdiIsSupportedVidPn",
    "DxgkDdiPresent",
    "DxgkDdiQueryConnectionChange",
    "DxgkDdiQueryCurrentFence",
    "DxgkDdiSetVidPnSourceAddress",
    "DxgkDdiSetVidPnSourceVisibility",
}


def main() -> int:
    sources = (
        ROOT / "driver" / "wdk" / "bc250_active_miniport.c",
        ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c",
    )
    text = "\n".join(path.read_text(encoding="utf-8") for path in sources)
    assigned = set(re.findall(r"InitializationData->(DxgkDdi[A-Za-z0-9_]+)\s*=", text))

    required = MINIMUM_MCDM | GPUVA_REQUIRED | LOCAL_COMPATIBILITY
    missing_set = required - assigned
    missing = sorted(missing_set)
    forbidden = sorted(PROHIBITED & assigned)

    print("BC250 MCDM DDI MATRIX")
    print(f"  minimum MCDM: {len(MINIMUM_MCDM - missing_set)}/{len(MINIMUM_MCDM)} pointers")
    print(f"  GPUVA extension: {len(GPUVA_REQUIRED - missing_set)}/{len(GPUVA_REQUIRED)} pointers")
    print(f"  local compatibility: {len(LOCAL_COMPATIBILITY - missing_set)}/{len(LOCAL_COMPATIBILITY)} pointers")
    print("  semantic/hardware validation: NOT PROVEN")
    print("  QueryConnectionChange conflict: explicit MCDM prohibition applied")

    if missing or forbidden:
        print("BC250 MCDM DDI MATRIX: FAILED")
        for name in missing:
            print(f"  - missing required pointer: {name}")
        for name in forbidden:
            print(f"  - prohibited pointer populated: {name}")
        return 1

    print("BC250 MCDM DDI MATRIX: PASS (STRUCTURAL ONLY)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
