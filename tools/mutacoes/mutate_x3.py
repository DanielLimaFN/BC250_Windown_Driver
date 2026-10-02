import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("Z1 anel ocioso ignora outros IBs em voo", "driver/wdk/bc250_sdma_present.c",
     "BOOLEAN ringIdle = (g_Bc250SdmaPresent.InFlightCount == 1u &&",
     "BOOLEAN ringIdle = (g_Bc250SdmaPresent.InFlightCount >= 1u &&"),
    ("Z2 portao nao zera a perna de dados", "driver/wdk/bc250_sdma_present.c",
     "                    dataPlan.Usable = 0u;\n                } else {\n                    InterlockedIncrement64(&g_Bc250X3CanaryRead);",
     "                } else {\n                    InterlockedIncrement64(&g_Bc250X3CanaryRead);"),
    ("Z3 leitura de tabela sem migalha", "driver/wdk/bc250_active_miniport.c",
     "        ULONG ptCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_PT_READ,\n            (ULONG)(VramOffset >> 12));",
     "        ULONG ptCrumb = 0xFFFFFFFFu;"),
    ("Z4 adiamento calado", "driver/wdk/bc250_sdma_present.c",
     "[X3_CANARY] #%lld perna de dados NAO CONFERIDA", "[X3] #%lld adiado"),
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
