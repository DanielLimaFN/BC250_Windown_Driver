"""Check that the safety audit catches an invalid zero allocation priority."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
SOURCE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
GOOD = b"info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;"
BAD = b"info->AllocationPriority = 0u;"


def audit() -> bool:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True).returncode == 0


def main() -> int:
    if not audit():
        print("DX.58: arvore inicial nao passou na auditoria")
        return 1
    original = SOURCE.read_bytes()
    if original.count(GOOD) != 1:
        print("DX.58: alvo da mutacao ausente ou ambiguo")
        return 1
    try:
        SOURCE.write_bytes(original.replace(GOOD, BAD, 1))
        caught = not audit()
    finally:
        SOURCE.write_bytes(original)
    restored = audit()
    print(f"DX.58 prioridade zero: {'REPROVOU' if caught else 'FALHA DO PORTAO'}")
    return 0 if caught and restored else 1


if __name__ == "__main__":
    raise SystemExit(main())
