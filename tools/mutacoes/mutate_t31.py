import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("N1 janela WC volta a 256 MB",
     "driver/wdk/bc250_display_ddi.c",
     "ULONG fbBytes = (ULONG)BC250_VRAM_WC_WINDOW_BYTES;",
     "ULONG fbBytes = 256u * 1024u * 1024u;"),
    ("N2 fallback UNCACHED do framebuffer volta",
     "driver/wdk/bc250_display_ddi.c",
     "        Bc250DisplayLog(\"[DisplayInit] FrameBuffer mapping:",
     "        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {\n            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(\n                g_Bc250DisplayState.FrameBufferPhysical, fbBytes, PAGE_READWRITE | PAGE_NOCACHE);\n        }\n        Bc250DisplayLog(\"[DisplayInit] FrameBuffer mapping:"),
    ("N3 sonda volta a mapear a BAR inteira",
     "driver/wdk/bc250_vram_probe_transaction.c",
     "physical, (ULONG)pageSpan,",
     "physical, (ULONG)bar->Length,"),
    ("N4 amarracao com as migalhas removida",
     "driver/wdk/bc250_display_ddi.c",
     "C_ASSERT(BC250_VRAM_WC_WINDOW_BYTES == BC250_CRUMB_VRAM_OFFSET);",
     ""),
    ("N5 anel grafico perde a declaracao UC",
     "driver/wdk/bc250_active_miniport.c",
     '    Bc250LogUcVramMapping("GFX_RING_ONLINE", region.Offset, totalBytes);\n',
     ""),
    ("N6 helper deixa de conferir a sobreposicao",
     "driver/wdk/bc250_display_ddi.h",
     "unsigned int privateUc = Bc250VramRangeIsPrivateUc(Offset, Length);",
     "unsigned int privateUc = 1u;"),
    ("N7 limite da politica alterado",
     "driver/core/bc250_vram_cache_policy.h",
     "#define BC250_VRAM_WC_WINDOW_BYTES   0x0FA00000ull",
     "#define BC250_VRAM_WC_WINDOW_BYTES   0x10000000ull"),
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
        for l in lines[:2]:
            print("      " + l.strip())
        ok = ok and failed
    finally:
        path.write_bytes(original)

r = subprocess.run(AUDIT, capture_output=True, text=True, cwd=ROOT)
print("restaurado, auditoria exit=%d" % r.returncode)
print("TODAS AS MUTACOES REPROVARAM" if ok and r.returncode == 0 else "HA PROBLEMA")
