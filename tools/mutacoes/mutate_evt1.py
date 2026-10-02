import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("E1 submit SDMA sem evento", "driver/wdk/bc250_active_miniport.c",
     "Bc250CrumbEvent(BC250_CRUMB_EVT_SUBMIT_SDMA, 0u,", "(void)(BC250_CRUMB_EVT_SUBMIT_SDMA, 0u,"),
    ("E2 FILL sem evento", "driver/wdk/bc250_mcdm_callbacks.c",
     "Bc250CrumbEvent(BC250_CRUMB_EVT_PAGING_FILL, 0u,", "(void)(BC250_CRUMB_EVT_PAGING_FILL, 0u,"),
    ("E3 leitor aceita evento degradado", "driver/wdk/bc250_active_miniport.c",
     "if (e[BC250_CRUMB_EVT_CHECK] != Bc250CrumbEvtCheck(", "if (0u && e[BC250_CRUMB_EVT_CHECK] == Bc250CrumbEvtCheck("),
    ("E4 reporte nao chama o leitor", "driver/wdk/bc250_active_miniport.c",
     "        Bc250CrumbReportEvents(events, BC250_CRUMB_EVT_COUNT * BC250_CRUMB_EVT_DWORDS);\n", ""),
    ("E5 vazamento no retorno antecipado", "driver/wdk/bc250_active_miniport.c",
     "        if (events != NULL) ExFreePoolWithTag(events, 'vEcB');\n        return;", "        return;"),
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
