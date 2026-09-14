#!/usr/bin/env python3
"""Mutacoes da etapa B.1 (anel grafico CP_RB0) contra audit_gfx_ring_contract_is_honest.

Cada mutacao reproduz um defeito que JA custou corrida neste projeto, ou o
apagamento da assercao que o impede de voltar:

  M1  RB_EXE deixa de ser exigido -> anel que nao executa e' aceito
  M2  montagem do CNTL deixa de validar o plano antes
  M3  montagem do CNTL mascara RB_BUFSZ em vez de recusar (defeito do ColorTarget0)
  M4  perna 1 volta a comparar RPTR cru (A15 / BugCheck 0x119 da 17:43)
  M5  reducao por volta reimplementada com % em vez da fonte unica
  M6  deteccao do valor de poll herdado do SDMA desaparece do header
  M7  o TESTE deixa de exigir que o CNTL reproduza o POST medido
  M8  o TESTE deixa de detectar o CP_RB0_CNTL nao executavel do prototipo
  M9  o TESTE deixa de cobrir o caso de volta do anel (4612)
  M10 o TESTE perde o contraponto de plano valido -- sem ele o contrato podia
      recusar TUDO e ainda passar nos negativos
  M11 assercao do teste neutralizada com assert(1) em vez de apagada
  M12 recusa de sobreposicao entre anel, writeback e shadow desaparece
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.c"
TXN_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_contract.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
MINI_FILE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"
INF_FILE = ROOT / "driver" / "wdk" / "package" / "bc250mcdm.inf"
PKG_FILE = ROOT / "tools" / "build-bc250mcdm-package.ps1"
LABPKG_FILE = ROOT / "tools" / "package-bc250mcdm-lab.ps1"
BIND_FILE = ROOT / "tools" / "bind-bc250mcdm-lab.ps1"


def _run_gate_inner(src_text: str, hdr_text: str, test_text: str, txn_text: str = None,
             mini_text: str = None, inf_text: str = None,
             pipeline: dict = None) -> list[str]:
    if txn_text is None:
        txn_text = TXN_FILE.read_text(encoding="utf-8", errors="replace")
    if mini_text is None:
        mini_text = MINI_FILE.read_text(encoding="utf-8", errors="replace")
    if inf_text is None:
        inf_text = INF_FILE.read_text(encoding="utf-8", errors="replace")
    saved = (
        SRC_FILE.read_text(encoding="utf-8", errors="replace"),
        HDR_FILE.read_text(encoding="utf-8", errors="replace"),
        TEST_FILE.read_text(encoding="utf-8", errors="replace"),
        TXN_FILE.read_text(encoding="utf-8", errors="replace"),
        MINI_FILE.read_text(encoding="utf-8", errors="replace"),
        INF_FILE.read_text(encoding="utf-8", errors="replace"),
    )
    pipeline = pipeline or {}
    saved_pipeline = {f: f.read_text(encoding="utf-8", errors="replace")
                      for f in (PKG_FILE, LABPKG_FILE, BIND_FILE)}
    try:
        SRC_FILE.write_text(src_text, encoding="utf-8")
        HDR_FILE.write_text(hdr_text, encoding="utf-8")
        TEST_FILE.write_text(test_text, encoding="utf-8")
        TXN_FILE.write_text(txn_text, encoding="utf-8")
        MINI_FILE.write_text(mini_text, encoding="utf-8")
        INF_FILE.write_text(inf_text, encoding="utf-8")
        for f, text in pipeline.items():
            f.write_text(text, encoding="utf-8")
        errors: list[str] = []
        safety.audit_gfx_ring_contract_is_honest(errors)
        return errors
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")
        TXN_FILE.write_text(saved[3], encoding="utf-8")
        MINI_FILE.write_text(saved[4], encoding="utf-8")
        INF_FILE.write_text(saved[5], encoding="utf-8")
        for f, text in saved_pipeline.items():
            f.write_text(text, encoding="utf-8")


def run_gate(*args, **kwargs):
    """Mesma semantica da original, sem mexer no relogio dos fontes.

    A guarda de obsolescencia de package-bc250mcdm-lab.ps1 compara DATAS.
    Restaurar o conteudo sem restaurar o mtime invalidava o pacote de
    laboratorio e exigia um rebuild destrutivo por nada (medido 14/09).
    """
    with preserve_mtimes(SRC_FILE, TXN_FILE, HDR_FILE, TEST_FILE, MINI_FILE, INF_FILE, PKG_FILE, LABPKG_FILE, BIND_FILE):
        return _run_gate_inner(*args, **kwargs)


def main() -> int:
    src = SRC_FILE.read_text(encoding="utf-8", errors="replace")
    hdr = HDR_FILE.read_text(encoding="utf-8", errors="replace")
    test = TEST_FILE.read_text(encoding="utf-8", errors="replace")
    txn = TXN_FILE.read_text(encoding="utf-8", errors="replace")
    mini = MINI_FILE.read_text(encoding="utf-8", errors="replace")
    inf = INF_FILE.read_text(encoding="utf-8", errors="replace")
    pkg = PKG_FILE.read_text(encoding="utf-8", errors="replace")
    labpkg = LABPKG_FILE.read_text(encoding="utf-8", errors="replace")
    binder = BIND_FILE.read_text(encoding="utf-8", errors="replace")

    print("[B.1 MUTATION SUITE] anel grafico CP_RB0")

    baseline = run_gate(src, hdr, test)
    if baseline:
        print("BASELINE REPROVOU -- o portao acusa o codigo intacto:")
        for item in baseline:
            print(f"  - {item}")
        return 1
    print("  baseline limpo")

    # M22 move a leitura do tunable para dentro do ramo de laboratorio -- que e'
    # exatamente o defeito A13/B4/ContextAbi, cometido de novo na primeira
    # versao deste bloco e corrigido antes de qualquer corrida.
    TUNABLE_BLOCK_MARK = "            ULONG gfxRingPresent = 0u;"
    MOVED = ("#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)\n"
             "            ULONG gfxRingPresent = 0u;")

    mutations = [
        ("M1  RB_EXE deixa de ser exigido",
         src.replace("        return BC250_GFX_RING_NOT_EXECUTABLE;", "        (void)0;", 1), hdr, test),

        ("M2  CNTL montado sem validar o plano",
         src.replace("    status = Bc250GfxRingValidatePlan(Plan);\n"
                     "    if (status != BC250_GFX_RING_OK) {\n"
                     "        return status;\n    }\n\n"
                     "    status = Bc250GfxRingDeriveBufsz",
                     "    status = Bc250GfxRingDeriveBufsz", 1), hdr, test),

        ("M3  CNTL mascara RB_BUFSZ em vez de recusar",
         src.replace("    value = (bufsz << GFX_RB_BUFSZ_SHIFT) |",
                     "    value = ((bufsz & GFX_RB_BUFSZ_MAX) << GFX_RB_BUFSZ_SHIFT) |", 1), hdr, test),

        ("M4  perna 1 compara RPTR cru",
         src.replace("    target = Bc250TripodWrapRingPointer(TargetWptrBytes, RingBytes);\n"
                     "    wb = Bc250TripodWrapRingPointer(RptrWriteback, RingBytes);\n"
                     "    reg = Bc250TripodWrapRingPointer(RptrRegister, RingBytes);",
                     "    target = TargetWptrBytes;\n    wb = RptrWriteback;\n    reg = RptrRegister;", 1),
         hdr, test),

        ("M5  volta do anel reimplementada com %",
         src.replace("    target = Bc250TripodWrapRingPointer(TargetWptrBytes, RingBytes);",
                     "    target = TargetWptrBytes % RingBytes;", 1), hdr, test),

        ("M6  deteccao do poll do SDMA some do header",
         src, hdr.replace("BC250_GFX_RING_POLL_CNTL_SDMA_BORROWED 0x00403004u",
                          "BC250_GFX_RING_POLL_CNTL_SDMA_BORROWED 0xFFFFFFFFu", 1), test),

        ("M7  teste nao exige reproduzir o POST medido",
         src, hdr, test.replace("    CHECK(cntl == BC250_GFX_RING_CNTL_CONFIGURED_REFERENCE);\n", "", 1)),

        ("M8  teste nao detecta o CNTL nao executavel do prototipo",
         src, hdr, test.replace(
             "    CHECK(Bc250GfxRingCntlRefusesNonExecutable(0x00A00709u) == 1u);\n", "", 1)),

        ("M9  teste perde o caso de volta do anel (4612)",
         src, hdr, test.replace("4612u", "516u")),

        ("M10 teste perde o contraponto de plano valido",
         src, hdr, test.replace("== BC250_GFX_RING_OK", "!= BC250_GFX_RING_BAD_ARGUMENT")),

        ("M11 assercao do teste neutralizada com assert(1)",
         src, hdr, test.replace(
             "    CHECK(cntl == 0x50000709u);",
             "    assert(1); /* CHECK(cntl == 0x50000709u); */", 1)),

        ("M12 recusa de sobreposicao desaparece",
         src.replace("        return BC250_GFX_RING_OVERLAP;", "        (void)0;", 1), hdr, test),

        ("M13 PREFLIGHT passa a escrever",
         src, hdr, test,
         txn.replace("    (void)Bc250GfxRingDecodeCntl(Evidence->CntlBefore, &Evidence->FieldsBefore);",
                     "    (void)Bc250GfxTxnWrite(Interface, Evidence, BC250_GFX_REG_CP_RB0_CNTL, 0u);\n"
                     "    (void)Bc250GfxRingDecodeCntl(Evidence->CntlBefore, &Evidence->FieldsBefore);", 1)),

        ("M14 APPLY deixa de exigir snapshot",
         src, hdr, test,
         txn.replace("    if (Evidence->SnapshotTaken != 1u || Evidence->CntlPlanned == 0u) {",
                     "    if (Evidence->CntlPlanned == 0u) {", 1)),

        ("M15 APPLY deixa de barrar o poll do SDMA",
         src, hdr, test,
         txn.replace("    if (Bc250GfxRingPollCntlIsBorrowed(Evidence->PollCntlBefore)) {",
                     "    if (0) {", 1)),

        ("M16 APPLY declara o anel pronto em vez de parar na proveniencia",
         src, hdr, test,
         txn.replace("    return BC250_GFX_RING_TXN_NOT_STARTABLE;",
                     "    return BC250_GFX_RING_TXN_OK;", 1)),

        # A M17 original mutava a parada de procedencia, que deixou de existir
        # em 14/09 quando gc_10_1_0_offset.h deu fonte a CP_RB0_ACTIVE. O
        # invariante que a substitui e' mais forte: escrever um dos seis
        # enderecos que o prototipo usa ERRADO tem de reprovar.
        ("M17 APPLY escreve endereco errado do prototipo",
         src, hdr, test,
         txn.replace("    Evidence->RingActivateAttempted = 1u;",
                     "    Evidence->RingActivateAttempted = 1u;\n"
                     "    (void)Bc250GfxTxnWrite(Interface, Evidence, 0x0000C4D0u, 1u);", 1)),

        ("M36 APPLY declara partida sem conferir o readback",
         src, hdr, test,
         txn.replace("    if ((Evidence->RingActivateReadback & 1u) == 0u) {",
                     "    if (0) {", 1)),

        ("M37 ROLLBACK nao desativa o anel antes de reverter",
         src, hdr, test,
         txn.replace("    if (Evidence->RingActivateAttempted == 1u) {",
                     "    if (0) {", 1)),

        ("M18 ROLLBACK deixa de conferir por readback",
         src, hdr, test,
         txn.replace("    if (readback != Evidence->CntlBefore) {", "    if (0) {", 1)),

        # Remove TODAS as ocorrencias: "ev.Writes == 0u" aparece em tres casos
        # legitimos, e apagar so' a primeira deixava o portao passar -- a
        # mutacao e' que estava fraca, nao o invariante.
        ("M19 teste deixa de exigir PREFLIGHT sem escrita",
         src, hdr, test.replace("    CHECK(ev.Writes == 0u);\n", ""), txn),

        # Como em M19: a ancora aparece em mais de um caso legitimo do
        # preflight (plano proposto e medicao pura). Neutralizar so' a
        # primeira nao e' perda do invariante -- perder TODAS e' que e'.
        ("M21 teste perde a ancora de leitura do preflight",
         src, hdr, test.replace("CHECK(ev.Reads == 7u);", "CHECK(1);"), txn),

        ("M20 teste perde a guarda do cenario discriminante",
         src, hdr, test.replace("CHECK(ev.CntlBefore != ev.CntlPlanned);", "CHECK(1);", 1), txn),

        ("M22 tunable volta para dentro do #if (A13/B4/ContextAbi)",
         src, hdr, test, txn,
         mini.replace(TUNABLE_BLOCK_MARK, MOVED, 1), inf),

        ("M23 INF perde a declaracao fail-closed do tunable",
         src, hdr, test, txn, mini,
         inf.replace("HKR,Parameters,GfxRingPreflight,0x00010001,0", "; removido", 1)),

        ("M24 probe deixa de conferir o contador do transporte",
         src, hdr, test, txn,
         mini.replace("    if (writes != 0u || reads != Evidence->Reads) {",
                      "    if (0) {", 1), inf),

        ("M25 probe passa a propor plano em vez de medir",
         src, hdr, test, txn,
         mini.replace("Bc250GfxRingTxnPreflight(&interfaceData, NULL, Evidence)",
                      "Bc250GfxRingTxnPreflight(&interfaceData, &planCandidate, Evidence)", 1), inf),

        ("M26 empacotador deixa de derivar o tunable do INF",
         src, hdr, test, txn, mini, inf,
         {PKG_FILE: pkg.replace("GfxRingPreflight = $gfxRingValue", "# removido", 1)}),

        ("M27 INF de laboratorio perde o NOCLOBBER (doc 186)",
         src, hdr, test, txn, mini, inf,
         {LABPKG_FILE: labpkg.replace(
             "'HKR,Parameters,GfxRingPreflight,0x00010003,0'",
             "'HKR,Parameters,GfxRingPreflight,0x00010001,0'", 1)}),

        ("M28 binder perde o tunable da politica pre-bind",
         src, hdr, test, txn, mini, inf,
         {BIND_FILE: binder.replace(
             "GfxRingPreflight = $(if ($EnableGfxRingPreflight) { 1 } else { 0 })",
             "# removido", 1)}),
    ]

    failures = 0
    for entry in mutations:
        name, mut_src, mut_hdr, mut_test = entry[0], entry[1], entry[2], entry[3]
        mut_txn = entry[4] if len(entry) > 4 else txn
        mut_mini = entry[5] if len(entry) > 5 else mini
        mut_inf = entry[6] if len(entry) > 6 else inf
        mut_pipe = entry[7] if len(entry) > 7 else None
        if mut_pipe is None and (mut_src, mut_hdr, mut_test, mut_txn, mut_mini,
                                 mut_inf) == (src, hdr, test, txn, mini, inf):
            print(f"  {name}: MUTACAO NAO APLICOU -- ancora errada")
            failures += 1
            continue
        if (mut_src, mut_hdr, mut_test, mut_txn, mut_mini, mut_inf, mut_pipe) == (
                src, hdr, test, txn, mini, inf, None):
            pass
        errors = run_gate(mut_src, mut_hdr, mut_test, mut_txn, mut_mini, mut_inf,
                          mut_pipe)
        if errors:
            print(f"  {name}: DETECTADA ({errors[0]})")
        else:
            print(f"  {name}: NAO DETECTADA -- portao fraco")
            failures += 1

    if failures:
        print(f"B.1 MUTATION SUITE: FAILED ({failures} de {len(mutations)})")
        return 1
    print(f"B.1 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
