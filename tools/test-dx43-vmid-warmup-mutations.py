#!/usr/bin/env python3
"""DX.43: each mutation of the VMID warm-up IB must make audit-bc250mcdm-safety.py FAIL.

Mutates the working-tree file in place, runs the audit, and ALWAYS restores the
original bytes (try/finally). The pristine tree must PASS first.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
DISP = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"

MUTATIONS = [
    (MINI, "aquecimento sem guarda de cabeca",
     "} else if (IbHeadDw0 != BC250_GFX_DX43_WARMUP_HEAD) {", "} else if (0) {"),
    (MINI, "sem reserva de anel para o aquecimento",
     "(warmupIb ? BC250_PM4_DWORDS_INDIRECT_BUFFER : 0u);", "0u;"),
    (MINI, "aquecimento depois do IB real",
     "    /* 0. DX.43: IB de aquecimento sob o VMID novo, antes do IB real. */\n    if (warmupIb) {",
     "    /* 0. DX.43 movido */\n    if (0) {"),
    (MINI, "cabeca nao lida pelo KMD",
     "ibHeadValid = Bc250ActiveGfxIbReadDword(&headReader, 0u, &ibHeadDw0) ? TRUE : FALSE;",
     "ibHeadValid = TRUE;"),
    (MINI, "linha por troca removida",
     "DX43_VMID_WARMUP_IB: Fence=%llu VMID %u->%u Emitido=%u Motivo=%s", "DX43 silencioso"),
    (MINI, "cabeca trocada",
     "#define BC250_GFX_DX43_WARMUP_HEAD   0xC0012800u", "#define BC250_GFX_DX43_WARMUP_HEAD   0xC0001000u"),
    (DISP, "linha amostrada fora",
     'Bc250LogTextContains(Text, "DX43_VMID_WARMUP") ||', ""),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.43 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("utf-8")
        crlf = "\r\n" in text
        target = old.replace("\n", "\r\n") if crlf else old
        repl = new.replace("\n", "\r\n") if crlf else new
        if target not in text:
            print(f"  [ERRO] alvo ausente: {label}")
            failures += 1
            continue
        try:
            path.write_bytes(text.replace(target, repl, 1).encode("utf-8"))
            ok = audit() != 0
        finally:
            path.write_bytes(original)
        print(f"  [{'REPROVOU' if ok else 'PASSOU (FALHA DO PORTAO)'}] {label}")
        failures += 0 if ok else 1
    if audit() != 0:
        print("DX.43 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.43 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
