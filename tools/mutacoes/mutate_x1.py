import pathlib, subprocess, sys

ROOT = pathlib.Path(r"C:\Users\Daniel\Desktop\NamixWDDM")
AUDIT = [sys.executable, str(ROOT / "tools" / "audit-bc250mcdm-safety.py")]

MUTATIONS = [
    ("X1a aposentadoria volta a ler a fence antes do portao", "driver/wdk/bc250_sdma_present.c",
     "    {\n        uint32_t rptrGate = Bc250SdmaRegRead(",
     "    fenceObserved = g_Bc250SdmaPresent.Work[slotFenceOffset / 4u];\n    {\n        uint32_t rptrGate = Bc250SdmaRegRead("),
    ("X1b blt volta a ler VRAM a cada volta", "driver/wdk/bc250_sdma_present.c",
     "        uint32_t fenceObserved;\n        if (!Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {\n            KeStallExecutionProcessor(5u);\n            continue;\n        }\n        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);\n        if (fenceObserved == nextFence) {\n            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);",
     "        uint32_t fenceObserved = g_Bc250SdmaPresent.Work[BC250_SDMA_PRESENT_FENCE_OFFSET / 4u];\n        if (!Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {\n            KeStallExecutionProcessor(5u);\n            continue;\n        }\n        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);\n        if (fenceObserved == nextFence) {\n            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR - BC250_SDMA0_MMIO_WINDOW_START);"),
    ("X1c leitura de fence sem migalha", "driver/wdk/bc250_sdma_present.c",
     "    crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_FENCE_READ, FenceOffset);",
     "    crumb = 0xFFFFFFFFu;"),
    ("X1d teste perde o caso dos NOPs", "driver/tests/blt_proof_contract_test.c",
     "    CHECK(Bc250TripodRingEntryConsumed(4050u, 0u, 88u, 88u, 4096u) == 0u);\n", ""),
    ("X1e aposentadoria sem portao", "driver/wdk/bc250_sdma_present.c",
     "        if (Bc250TripodRingEntryConsumed(rptrGate,", "        if (0u && Bc250TripodRingEntryConsumed(rptrGate,"),
    ("X1f portao do blt desligado", "driver/wdk/bc250_sdma_present.c",
     "    for (poll = 1u; poll <= 10000u; ++poll) {\n        uint32_t fenceObserved;\n        if (!Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {\n            KeStallExecutionProcessor(5u);\n            continue;\n        }\n        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);\n        if (fenceObserved == nextFence) {\n            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR",
     "    for (poll = 1u; poll <= 10000u; ++poll) {\n        uint32_t fenceObserved;\n        if (0 && !Bc250SdmaSyncEntryConsumed(x1StartBytes, x1TargetBytes)) {\n            KeStallExecutionProcessor(5u);\n            continue;\n        }\n        fenceObserved = Bc250SdmaReadFenceAfterConsumed(BC250_SDMA_PRESENT_FENCE_OFFSET);\n        if (fenceObserved == nextFence) {\n            uint32_t rptrReg = Bc250SdmaRegRead(&g_Bc250SdmaPresent, BC250_SDMA0_REG_RB_RPTR"),
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
    # (mutacao X1f abaixo e' adicionada na lista; ver MUTATIONS)
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
