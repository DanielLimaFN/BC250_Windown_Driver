#!/usr/bin/env python3
"""DX.50: each mutation must make audit-bc250mcdm-safety.py FAIL.

Dirty rects switched off for bisection, and residual UMD logs archived (never
deleted) by the binder. Mutates in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
BINDER = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"

MUTATIONS = [
    (DISP, "dirty rects religados",
     "static const BOOLEAN s_Bc250Dx50DirtyRectsEnabled = FALSE;",
     "static const BOOLEAN s_Bc250Dx50DirtyRectsEnabled = TRUE;"),
    (DISP, "Present ignora o interruptor",
     "} else if (s_Bc250Dx50DirtyRectsEnabled &&", "} else if (TRUE &&"),
    (DISP, "recusa sem motivo da bisseccao",
     '"DX50_BISSECCAO_DESLIGADO"', '"NAO_AVALIADO"'),
    (BINDER, "binder volta a mover (ACL de Windows\\Temp)",
     "Copy-Item -LiteralPath $residual.FullName -Destination $residualDest -Force -ErrorAction Stop",
     "Move-Item -LiteralPath $residual.FullName -Destination $residualDest -Force -ErrorAction Stop"),
    (BINDER, "origem apagada sem conferir o hash da copia",
     "if ($residualHash -ne (Get-FileHash -LiteralPath $residual.FullName -Algorithm SHA256).Hash) {",
     "if ($false) {"),
    (BINDER, "arquivo sem hash",
     "Add-Content -LiteralPath (Join-Path $residualUmdDir 'SHA256SUMS.txt') -Encoding UTF8",
     "Out-Null"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.50 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.50 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.50 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
