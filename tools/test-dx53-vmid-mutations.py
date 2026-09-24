#!/usr/bin/env python3
"""DX.53: each mutation must make audit-bc250mcdm-safety.py FAIL.

VMID disarmed before it returns to the pool; failed disarm quarantines the slot. Mutates in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
TEST = ROOT / "driver" / "tests" / "active_miniport_test.c"

MUTATIONS = [
    (MINI, "devolucao volta a vir antes do desarme",
     "                        KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);\n\n                        if (lookupStatus == BC250_VMID_POOL_OK && alvo < BC250_VMID_COUNT &&",
     "                        (void)Bc250VmidPoolRelease(&context->VmidPool, ContextHandle, NULL);\n                        KeReleaseSpinLock(&context->VmidPoolLock, poolIrql);\n\n                        if (lookupStatus == BC250_VMID_POOL_OK && alvo < BC250_VMID_COUNT &&"),
    (MINI, "devolve mesmo com desarme falho",
     "if (disarmStatus == BC250_GCVM_VMID8_OK) {", "if (TRUE) {"),
    (MINI, "quarentena muda",
     "[VMID_POOL] QUARENTENA: VMID%02u NAO devolvido", "[VMID_POOL] X: VMID%02u NAO devolvido"),
    (MINI, "medicao sem sincronizar com a ISR",
     "sync.Operation = BC250_ACTIVE_FENCE_SYNC_PENDING;\n    st = Context->Core.DxgkInterface.DxgkCbSynchronizeExecution(",
     "sync.Operation = BC250_ACTIVE_FENCE_SYNC_PENDING;\n    st = STATUS_SUCCESS; ran = TRUE; (void)(\n        Context->Core.DxgkInterface.DxgkCbSynchronizeExecution == NULL) && ("),
    (TEST, "teste volta a exigir devolucao sem desarme",
     "CHECK(otherVmid != quarantined && otherAssigned == 0u);", "CHECK(otherAssigned == 0u);"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.53 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("latin-1")
        if old not in text and "\n" in old and "\r\n" in text:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if old not in text:
            print(f"  [ERRO] alvo ausente: {label}")
            failures += 1
            continue
        try:
            path.write_bytes(text.replace(old, new, 1).encode("latin-1"))
            ok = audit() != 0
        finally:
            path.write_bytes(original)
        print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
        failures += 0 if ok else 1
    if audit() != 0:
        print("DX.53 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.53 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
