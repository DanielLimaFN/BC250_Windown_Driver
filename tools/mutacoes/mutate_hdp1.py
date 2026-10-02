import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("M1 SdmaPresentHdp volta a escrever UTCL1_INV0",
     "driver/wdk/bc250_sdma_present.c",
     "BC250_HDP_REG_MEM_COHERENCY_FLUSH_CNTL - BC250_SDMA0_MMIO_WINDOW_START,",
     "BC250_SDMA0_REG_UTCL1_INV0 - BC250_SDMA0_MMIO_WINDOW_START,"),
    ("M2 0x4A80 volta a allowlist de escrita",
     "driver/wdk/bc250_mmio_topology_transport.c",
     "        ByteOffset == 0x00004A70u ||\n        ByteOffset == 0x00004AA0u || ByteOffset == 0x00004B80u ||",
     "        ByteOffset == 0x00004A70u || ByteOffset == 0x00004A80u ||\n        ByteOffset == 0x00004AA0u || ByteOffset == 0x00004B80u ||"),
    ("M3 UpdatePageTable sem flush",
     "driver/wdk/bc250_active_miniport.c",
     '        (void)Bc250ActiveHdpWriteFlush(active, "UPDATE_PAGE_TABLE");\n',
     ""),
    ("M4 FLUSH_TLB sem flush real",
     "driver/wdk/bc250_active_miniport.c",
     '    hdpFlushed = Bc250ActiveHdpWriteFlush(active, "FLUSH_TLB");\n',
     "    hdpFlushed = 1u;\n"),
    ("M5 constante do MEC volta a 0x4A80",
     "driver/core/bc250_compute_mqd_contract.h",
     "#define BC250_HQD_MMIO_HDP_FLUSH                 0x0000385Cu",
     "#define BC250_HQD_MMIO_HDP_FLUSH                 0x00004A80u"),
    ("M6 ring work sem mfence",
     "driver/wdk/bc250_sdma_ring_work.c",
     "    _mm_mfence();\n    if (!Bc250SdmaWrite(Context, R_HDP_FLUSH_REAL",
     "    if (!Bc250SdmaWrite(Context, R_HDP_FLUSH_REAL"),
    ("M7 nome falso volta no anel grafico",
     "driver/core/bc250_gfx_ring_transaction.h",
     "#define BC250_GFX_REG_HDP_READ_CACHE_INVALIDATE 0x00003FC4u",
     "#define BC250_GFX_REG_HDP_READ_CACHE_INVALIDATE 0x00003FC4u\n#define BC250_GFX_REG_HDP_MEM_COHERENCY_FLUSH_CNTL 0x00003FC4u"),
    ("M8 helper do miniport sem mfence",
     "driver/wdk/bc250_active_miniport.c",
     "    _mm_mfence(); /* mb() do upstream: drena write-combining da BAR */\n    wrote = ",
     "    wrote = "),
    ("M9 MEC le de volta o flush em vez de 0x378C",
     "driver/wdk/bc250_submission_engine.c",
     "BC250_HQD_MMIO_HDP_FLUSH_POSTING_READ, &hdpDummy)",
     "BC250_HQD_MMIO_HDP_FLUSH, &hdpDummy)"),
    ("M10 SdmaPresentHdp perde a leitura de confirmacao",
     "driver/wdk/bc250_sdma_present.c",
     "    memsize = Bc250SdmaRegRead(Context,\n        BC250_HDP_REG_FLUSH_POSTING_READ - BC250_SDMA0_MMIO_WINDOW_START);\n\n    Bc250SdmaRegWrite(Context,\n        BC250_SDMA0_REG_HDP_INVALIDATE",
     "    memsize = 0u;\n\n    Bc250SdmaRegWrite(Context,\n        BC250_SDMA0_REG_HDP_INVALIDATE"),
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
        print("NAO APLICADA (%d ocorrencias): %s" % (text.count(probe), label))
        ok = False
        continue
    try:
        path.write_bytes(text.replace(probe, repl).encode("utf-8"))
        r = subprocess.run(AUDIT, capture_output=True, text=True, cwd=ROOT)
        failed = r.returncode != 0 and "FAILED" in r.stdout
        lines = [l for l in r.stdout.splitlines() if l.startswith("  - ")]
        print(("REPROVOU (ok)" if failed else "PASSOU (GATE FRACO)") + ": " + label)
        for l in lines[:3]:
            print("      " + l.strip())
        ok = ok and failed
    finally:
        path.write_bytes(original)

r = subprocess.run(AUDIT, capture_output=True, text=True, cwd=ROOT)
print("restaurado, auditoria exit=%d" % r.returncode)
print("TODAS AS MUTACOES REPROVARAM" if ok and r.returncode == 0 else "HA PROBLEMA")
