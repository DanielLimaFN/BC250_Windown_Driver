#!/usr/bin/env python3
"""DX.51: each mutation must make audit-bc250mcdm-safety.py FAIL.

Draw batching switched off for bisection. Mutates in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
DRAW = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"

MUTATIONS = [
    (DRAW, "lote religado",
     "static const BOOL s_Bc250Dx51DrawBatchEnabled = FALSE;",
     "static const BOOL s_Bc250Dx51DrawBatchEnabled = TRUE;"),
    (DRAW, "draw ignora o interruptor",
     "if (s_Bc250Dx51DrawBatchEnabled &&", "if (TRUE &&"),
    (DRAW, "anel criado com lote desligado",
     "ringFree = s_Bc250Dx51DrawBatchEnabled ? Bc250UmdVkDrawRingFree(&pDevice->VkBridge) : 0;",
     "ringFree = Bc250UmdVkDrawRingFree(&pDevice->VkBridge);"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.51 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("latin-1")
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
        print("DX.51 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.51 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
