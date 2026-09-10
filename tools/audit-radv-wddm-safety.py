#!/usr/bin/env python3
"""Fail-closed source audit for the external Mesa RADV WDDM2 fork."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesa-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.mesa_root.resolve()

    paths = {
        "bo": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_bo.c",
        "cs": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_cs.c",
        "wsi": root / "src/amd/vulkan/winsys/wddm2/radv_wddm2_wsi.c",
        "radv_wsi": root / "src/amd/vulkan/radv_wsi.c",
        "fence": root / "src/vulkan/runtime/vk_wddm2_monitored_fence.c",
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        print("RADV WDDM SAFETY AUDIT: FAILED")
        for path in missing:
            print(f"  - missing: {path}")
        return 1

    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    errors: list[str] = []

    forbidden = {
        "fake allocation handles": "fake_hAlloc",
        "CPU-invented GPU virtual addresses": "next_virtual_va",
        "CPU-invented mapped GPUVA": "static uint64_t next_va",
        "host allocation impersonating a GPU BO": "calloc(1, bo->base.size)",
        "device handle impersonating a context": "create_context.hContext = ws->device_h",
        "CPU-authored monitored fence": "p_atomic_set(mf->value_map",
        "software WSI advertisement": "wsi->sw = true",
        "direct PM4 escape bypass": "RADV_BC250_DIRECT_PM4_ESCAPE",
    }
    corpus = "\n".join(text.values())
    for label, needle in forbidden.items():
        if needle in corpus:
            errors.append(label)

    required = {
        "BC-250 allocation ABI magic": ("bo", "BC250_ALLOCATION_PRIVATE_MAGIC"),
        "BC-250 allocation token": ("bo", ".allocation_token = (uint64_t)(uintptr_t)bo"),
        "real CreateAllocation2 failure": ("bo", "CreateAllocation2 failed"),
        "real GPUVA map failure": ("bo", "map.VirtualAddress == 0"),
        "BC-250 node 1 selection": ("cs", "is_bc250 ? 1 : 0"),
        "BC-250 HW queue disabled": ("cs", ".HwQueueSupported = !is_bc250"),
        "context failure propagation": ("cs", "return VK_ERROR_INITIALIZATION_FAILED;"),
        "submit failure propagation": ("cs", "return VK_ERROR_DEVICE_LOST;"),
        "kernel fence wait": ("fence", "WaitForSynchronizationObjectFromCpu"),
        "hardware WSI on Windows": ("radv_wsi", "bool sw = false;"),
    }
    for label, (name, needle) in required.items():
        if needle not in text[name]:
            errors.append(f"missing {label}")

    if errors:
        print("RADV WDDM SAFETY AUDIT: FAILED")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("RADV WDDM SAFETY AUDIT: PASS")
    print("  fabricated handles/GPUVA/fences: absent")
    print("  WARP/software WSI advertisement: absent")
    print("  WDDM callback failures: fail-closed")
    print("  verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
