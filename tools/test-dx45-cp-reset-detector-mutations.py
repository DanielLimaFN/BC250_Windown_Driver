#!/usr/bin/env python3
"""DX.45: each mutation of the silent-CP-reset detector must make audit-bc250mcdm-safety.py FAIL.

Mutates the working tree in place and ALWAYS restores the original bytes.
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
    (MINI, "detector fora do poll",
     '    Bc250ActiveGfxDx45Check(Context, "POLL", rptrReg, rptrRegRead, wptr, observedFence);\n', ""),
    (MINI, "detector fora do submit",
     '    Bc250ActiveGfxDx45Check(Context, "SUBMIT", rptr, TRUE, cursor,', '    (void)(0, "X", rptr, TRUE, cursor,'),
    (MINI, "detector passa a escrever",
     "    ok = Bc250MmioTopologyRead32(&Context->Core.MmioTransport, ByteOffset, &value);",
     "    ok = Bc250MmioTopologyWrite32(&Context->Core.MmioTransport, ByteOffset, 0u);"),
    (MINI, "sem RLC_PG_CNTL no log",
     '    Bc250ActiveGfxDx45StagedRead(Context, n, "RLC_PG_CNTL", BC250_HQD_MMIO_RLC_PG_CNTL);\n', ""),
    (MINI, "DX.45b leitura antes da linha",
     "    LONG n;\n    const LONG pending = InterlockedExchange(&s_Bc250Dx45Pending, 0);\n",
     "    LONG n;\n    uint32_t early = 0u;\n    const LONG pending = InterlockedExchange(&s_Bc250Dx45Pending, 0);\n"
     "    (void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport, BC250_GFX_REG_CP_STAT, &early);\n"),
    (MINI, "DX.45b leitura sem LENDO",
     '    Bc250DisplayLog("[CP_RESET_DETECTED] #%ld LENDO %s (0x%05X)', '    Bc250DisplayLog("[X] #%ld LENDO %s (0x%05X)'),
    (MINI, "DX.45b detalhe em DPC",
     "        if (KeGetCurrentIrql() > PASSIVE_LEVEL) {", "        if (0) {"),
    (MINI, "acusa o proprio quiesce",
     "        if (InterlockedCompareExchange(&s_Bc250Dx45OwnQuiesce, 0, 0) != 0) return;\n", ""),
    (MINI, "marca do quiesce removida",
     "                InterlockedExchange(&s_Bc250Dx45OwnQuiesce, 1);\n", ""),
    (MINI, "mascara de halt trocada",
     "#define BC250_DX45_CP_HALT_MASK        0x15000000u", "#define BC250_DX45_CP_HALT_MASK        0x10000000u"),
    (DISP, "linha amostrada fora",
     'Bc250LogTextContains(Text, "CP_RESET_DETECTED") ||', ""),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.45 MUTATION TEST: FAILED -- arvore pristina nao passa")
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
        print("DX.45 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.45 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
