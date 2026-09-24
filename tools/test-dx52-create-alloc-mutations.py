#!/usr/bin/env python3
"""DX.52: each mutation must make audit-bc250mcdm-safety.py FAIL.

CreateAllocation refusals speak; UMD describes the refused resource. Mutates in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
DRAW = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
WINSYS = ROOT / "driver" / "umd" / "bc250_wddm_winsys.c"

MUTATIONS = [
    (MINI, "linha de recusa removida",
     "[CREATE_ALLOC_RECUSADO] status=0x%08X motivo=%s", "[X] status=0x%08X motivo=%s"),
    (MINI, "recusa de ABI volta a ser muda",
     'refuseReason = "ABI_PRIVADA_RECUSADA_PELO_PARSE";', ''),
    (MINI, "recusa do registro de objeto sem motivo",
     'refuseReason = "REGISTRO_DO_OBJETO_DE_ALOCACAO_FALHOU";', ''),
    (DISP, "recusa amostrada",
     'Bc250LogTextContains(Text, "CREATE_ALLOC_RECUSADO") ||', ''),
    (DRAW, "UMD nao descreve o recurso",
     "CreateResource RECUSADO desc: Dim=%u", "CreateResource X: Dim=%u"),
    (WINSYS, "winsys nao descreve a alocacao",
     "Allocate RECUSADO pelo runtime/KMD: hr=0x%08X", "Allocate X: hr=0x%08X"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.52 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.52 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.52 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
