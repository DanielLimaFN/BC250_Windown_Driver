import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("M1 TLB transaction volta a tocar em RangeLow", "driver/core/bc250_gfxhub_tlb_transaction.c",
     "    /* 1. Escreve REQ no engine 17 */",
     "    (void)Interface->Read32(Interface->Context, Plan->RangeLowRegisterOffset, &value);\n    /* 1. Escreve REQ no engine 17 */"),
    ("M2 FlushVmid volta a emitir log incondicional de Transport State", "driver/wdk/bc250_active_miniport.c",
     "    /*\n     * T3.4 (29/09): Silenciar logs sob o caminho critico de paginacao.",
     "    Bc250DisplayLog(\"[FlushMappedGfxhubTlb] Transport State=%u\\n\", 1);\n    /*\n     * T3.4 (29/09): Silenciar logs sob o caminho critico de paginacao."),
    ("M3 TLB transaction omite dummy read em Request", "driver/core/bc250_gfxhub_tlb_transaction.c",
     "    /* 2. Dummy read em REQ para drenar o buffer GRBM rapido (amdgpu gmc_v10_0.c:296-298) */\n    if (!Interface->Read32(Interface->Context,\n            Plan->RequestRegisterOffset, &Evidence->RequestDummyRead)) {\n        return BC250_TLB_TRANSACTION_READ_FAILED;\n    }\n    Evidence->DummyReadCompleted = 1u;",
     "    /* 2. Dummy read omitido */\n    Evidence->DummyReadCompleted = 1u;"),
    ("M4 FlushVmid perde log de falha de invalidacao", "driver/wdk/bc250_active_miniport.c",
     "        Bc250DisplayLog(\"[FlushMappedGfxhubTlb] FALHA VMID%02u status=%u, PhysReady=%u, AckObserved=%u, Polls=%u\\n\",",
     "        /* Log removido */\n        (void)Vmid;"),
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
