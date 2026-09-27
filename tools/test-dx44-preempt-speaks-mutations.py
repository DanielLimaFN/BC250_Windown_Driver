#!/usr/bin/env python3
"""DX.44: each mutation of the speaking preemption DDI must make audit-bc250mcdm-safety.py FAIL.

Mutates the working tree in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
CB = ROOT / "driver" / "wdk" / "bc250_mcdm_callbacks.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"

MUTATIONS = [
    (CB, "recusa de parametro muda",
     'Bc250DisplayLog("[PREEMPT_COMMAND] #%ld RECUSADO: pPreempt=%p', 'Bc250DisplayLog("[X] #%ld RECUSADO: pPreempt=%p'),
    (CB, "recusa por caps muda",
     'Bc250DisplayLog("[PREEMPT_COMMAND] #%ld RECUSADO: PreemptionFenceId', 'Bc250DisplayLog("[X] #%ld RECUSADO: PreemptionFenceId'),
    (CB, "sucesso volta a ser mudo",
     'Bc250DisplayLog("[PREEMPT_COMMAND] #%ld PreemptionFenceId=%u Node=%u Engine=%u Flags=0x%08X -> STATUS_SUCCESS',
     'Bc250DisplayLog("[X] #%ld PreemptionFenceId=%u Node=%u Engine=%u Flags=0x%08X -> STATUS_SUCCESS'),
    (CB, "sucesso sem declarar ausencia de backend",
     "SEM BACKEND: nenhuma preempcao feita e DMA_PREEMPTED NAO sera reportado", "ok"),
    (CB, "sem teto proprio",
     "return (Seq <= 256 || (Seq % 64) == 0) ? TRUE : FALSE;", "return TRUE;"),
    (DISP, "linha amostrada fora",
     'Bc250LogTextContains(Text, "PREEMPT_COMMAND") ||', ""),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.44 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.44 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.44 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
