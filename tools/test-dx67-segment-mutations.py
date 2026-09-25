"""DX.67: each unsafe segment/breadcrumb change must fail the safety audit."""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
ACTIVE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"

MUTATIONS = [
    ("no segment truncation", b"localEnd = region->Offset;", b"localEnd = localEnd;"),
    ("no bootstrap allocator", b"allocs[1] = &context->Core.StartDevice.Owned.Bootstrap.Allocator;",
     b"allocs[1] = &context->GlobalMmuAllocator;"),
    ("no MMU allocator", b"allocs[0] = &context->GlobalMmuAllocator;",
     b"allocs[0] = &context->Core.StartDevice.Owned.Bootstrap.Allocator;"),
    ("wrong end comparison", b"region->Offset < localEnd) {",
     b"region->Offset > localEnd) {"),
    ("no crumb reservation", b"crumb->Offset = BC250_CRUMB_VRAM_OFFSET;",
     b"crumb->Offset = 0ull;"),
    ("arm without reservation", b"if (!reserved) {",
     b"if (FALSE && !reserved) {"),
]


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    original = ACTIVE.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if not audit():
        print("DX.67: initial audit failed")
        return 1
    failures = 0
    for name, good, bad in MUTATIONS:
        if original.count(good) != 1:
            print(f"DX.67 {name}: target absent or ambiguous ({original.count(good)})")
            failures += 1
            continue
        try:
            ACTIVE.write_bytes(original.replace(good, bad, 1))
            caught = not audit()
        finally:
            ACTIVE.write_bytes(original)
        if hashlib.sha256(ACTIVE.read_bytes()).hexdigest() != digest:
            print("DX.67: original bytes were not restored")
            return 2
        print(f"DX.67 {name}: {'REJECTED' if caught else 'GATE FAILED'}")
        failures += 0 if caught else 1
    if not audit():
        print("DX.67: audit failed after restoration")
        return 1
    print(f"DX.67: {len(MUTATIONS) - failures}/{len(MUTATIONS)} rejected")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
