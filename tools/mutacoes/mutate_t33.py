import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("M1 FLUSH_TLB volta a usar FORCE_INVALIDATION", "driver/wdk/bc250_active_miniport.c",
     "    /*\n     * T3.3 (29/09, alinhamento com Linux Upstream",
     "    BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION;\n    /*\n     * T3.3 (29/09, alinhamento com Linux Upstream"),
    ("M2 Init do SDMA nao mascara FORCE_INVALIDATION", "driver/wdk/bc250_sdma_present.c",
     "Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,\n        (value & ~(0x00000E00u | 0x0000003Eu | BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION)) | (3u << 9) | (9u << 1));\n\n    {",
     "Bc250SdmaRegWrite(&g_Bc250SdmaPresent, BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,\n        (value & ~(0x00000E00u | 0x0000003Eu)) | (3u << 9) | (9u << 1));\n\n    {"),
    ("M3 Restart do SDMA nao mascara FORCE_INVALIDATION", "driver/wdk/bc250_sdma_present.c",
     "BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,\n        (value & ~(0x00000E00u | 0x0000003Eu | BC250_SDMA0_UTCL1_CNTL_FORCE_INVALIDATION)) | (3u << 9) | (9u << 1));\n    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,\n        BC250_SDMA0_REG_UTCL1_PAGE",
     "BC250_SDMA0_REG_UTCL1_CNTL - BC250_SDMA0_MMIO_WINDOW_START,\n        (value & ~(0x00000E00u | 0x0000003Eu)) | (3u << 9) | (9u << 1));\n    value = Bc250SdmaRegRead(&g_Bc250SdmaPresent,\n        BC250_SDMA0_REG_UTCL1_PAGE"),
]

ok = True
for label, rel, old, new in MUTATIONS:
    path = ROOT / rel
    original = path.read_bytes()
    text = original.decode("utf-8")
    crlf = "\r\n" in text
    probe = old.replace("\n", "\r\n") if crlf else old
    repl = new.replace("\n", "\r\n") if crlf else new
    if text.count(probe) != 1:
        print("NAO APLICADA (%d ocorrencias): %s" % (text.count(probe), label)); ok = False; continue
    try:
        path.write_bytes(text.replace(probe, repl).encode("utf-8"))
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
