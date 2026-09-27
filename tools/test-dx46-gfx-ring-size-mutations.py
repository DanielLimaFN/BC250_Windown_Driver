#!/usr/bin/env python3
"""DX.46: each mutation of the full-size GFX ring must make audit-bc250mcdm-safety.py FAIL.

Mutates the working tree in place and ALWAYS restores the original bytes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
SRC = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.c"
HDR = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.h"
TEST = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
MINI = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"

MUTATIONS = [
    (SRC, "formula volta a subtrair 1",
     "    if (bufsz < 3u) {\n        return BC250_GFX_RING_BAD_SIZE;\n    }\n",
     "    if (bufsz < 3u) {\n        return BC250_GFX_RING_BAD_SIZE;\n    }\n    bufsz -= 1u;\n"),
    (HDR, "referencia do upstream trocada",
     "#define BC250_GFX_RING_CNTL_UPSTREAM_8K_REFERENCE 0x5000080Au",
     "#define BC250_GFX_RING_CNTL_UPSTREAM_8K_REFERENCE 0x50000709u"),
    (TEST, "teste perde o caso de 64 KB",
     "    CHECK(bufsz == 13u);\n", ""),
    (TEST, "teste perde a invariante anel do CP == buffer do KMD",
     "            CHECK((1u << (bufsz + 1u)) == ringBytesProbe / 4u);\n", ""),
    (TEST, "teste volta a exigir o anel pela metade",
     "    CHECK(cntl != BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);\n", ""),
    (MINI, "fallback fixo do restart volta a 0x50000A0C",
     "                    plannedCntl = 0x50000B0Du; /* 64 KB pelo upstream: (13, 11) */",
     "                    plannedCntl = 0x50000A0Cu;"),
]


def audit() -> int:
    return subprocess.run([sys.executable, str(AUDIT)], capture_output=True, text=True).returncode


def main() -> int:
    if audit() != 0:
        print("DX.46 MUTATION TEST: FAILED -- arvore pristina nao passa")
        return 1
    failures = 0
    for path, label, old, new in MUTATIONS:
        original = path.read_bytes()
        text = original.decode("utf-8")
        crlf = "\r\n" in text
        target, repl = old, new
        if target not in text and crlf:
            target, repl = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
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
        print("DX.46 MUTATION TEST: FAILED -- restauracao nao voltou a PASS")
        return 1
    print(f"DX.46 MUTATION TEST: {'PASS' if failures == 0 else 'FAILED'} "
          f"({len(MUTATIONS) - failures}/{len(MUTATIONS)} mutacoes reprovadas)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
