import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("Y1 FILL volta a 4 MiB", "driver/wdk/bc250_mcdm_callbacks.c",
     "        uint32_t chunk = remaining > (UINT64)BC250_SDMA_PAGING_FILL_MAX_BYTES ?\n            BC250_SDMA_PAGING_FILL_MAX_BYTES : (uint32_t)remaining;",
     "        uint32_t chunk = remaining > (UINT64)BC250_SDMA_FILL_MAX_BYTES ?\n            BC250_SDMA_FILL_MAX_BYTES : (uint32_t)remaining;"),
    ("Y2 teste deixa de exigir o teto por fatia", "driver/tests/mcdm_callbacks_test.c",
     "                    CHECK(g_pagingDmaBuffer[packet * 5u + 4u] <= 262144u - 1u);\n", ""),
    ("Y3 uma copia para o anel sem migalha", "driver/wdk/bc250_sdma_present.c",
     "    ULONG ringWriteCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_RING_WRITE, startCursor);",
     "    ULONG ringWriteCrumb = 0xFFFFFFFFu;"),
    ("Y4 canarios lidos sem migalha", "driver/wdk/bc250_sdma_present.c",
     "                canaryCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_CANARY_READ,\n                    (ULONG)(dataPlan.VramOffset >> 12));",
     "                canaryCrumb = 0xFFFFFFFFu;"),
]

ok = True
for label, rel, old, new in MUTATIONS:
    path = ROOT / rel
    original = path.read_bytes()
    text = original.decode("utf-8")
    crlf = "\r\n" in text
    probe = old.replace("\n", "\r\n") if crlf else old
    repl = new.replace("\n", "\r\n") if crlf else new
    n = text.count(probe)
    if n < 1:
        print("NAO APLICADA (0 ocorrencias): %s" % label); ok = False; continue
    try:
        path.write_bytes(text.replace(probe, repl, 1).encode("utf-8"))
        r = subprocess.run(AUDIT, capture_output=True, text=True, cwd=ROOT)
        failed = r.returncode != 0 and "FAILED" in r.stdout
        lines = [l for l in r.stdout.splitlines() if l.startswith("  - ")]
        print(("REPROVOU (ok)" if failed else "PASSOU (GATE FRACO)") + ": " + label)
        for l in lines[:2]: print("      " + l.strip())
        ok = ok and failed
    finally:
        path.write_bytes(original)
r = subprocess.run(AUDIT, capture_output=True, text=True, cwd=ROOT)
print("restaurado, auditoria exit=%d" % r.returncode)
print("TODAS AS MUTACOES REPROVARAM" if ok and r.returncode == 0 else "HA PROBLEMA")
