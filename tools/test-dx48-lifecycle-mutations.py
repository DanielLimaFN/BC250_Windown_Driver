#!/usr/bin/env python3
"""DX.48: each mutation of the adapter-lifecycle logging must make audit-bc250mcdm-safety.py FAIL.

Mutates the working tree in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
CB = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"

MUTATIONS = [
    (MINI, "AddDevice mudo", '"[DX48_LIFECYCLE] AddDevice ENTER', '"[X] AddDevice ENTER'),
    (MINI, "StartDevice sem LEAVE", '"[DX48_LIFECYCLE] StartDevice LEAVE', '"[X] StartDevice LEAVE'),
    (MINI, "StopDevice mudo", '"[DX48_LIFECYCLE] StopDevice ENTER', '"[X] StopDevice ENTER'),
    (MINI, "RemoveDevice mudo", '"[DX48_LIFECYCLE] RemoveDevice ENTER', '"[X] RemoveDevice ENTER'),
    (CB, "QAI sem hAdapter", "CapsStatus=0x%08X hAdapter=%p", "CapsStatus=0x%08X hA=%p"),
    (DISP, "ciclo de vida amostrado", 'Bc250LogTextContains(Text, "DX48_LIFECYCLE") ||', ""),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.48 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("utf-8")
        if old not in text:
            print(f"  [ERRO] alvo ausente: {label}")
            failures += 1
            continue
        try:
            path.write_bytes(text.replace(old, new, 1).encode("utf-8"))
            ok = audit() != 0
        finally:
            path.write_bytes(original)
        print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
        failures += 0 if ok else 1
    if audit() != 0:
        print("DX.48 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.48 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
