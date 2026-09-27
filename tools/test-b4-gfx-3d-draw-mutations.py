#!/usr/bin/env python3
"""Mutacoes da etapa B.4 (Pipeline Integrado de Renderizacao de Triangulo 3D)
contra audit_gfx_3d_draw_is_honest.

Cada mutacao reproduz uma violacao de invariante arquitetural (dimensao canonica
de 64 dwords / 256 B, alinhamento a 16 B, emissao dos 7 blocos estruturais do pipeline,
verificacao estrita de integridade pre-disparo, tripe da Lei 2 com poison readback proof,
e integracao ao bringup Level 4 do miniport):

  M1  tamanho total do stream adulterado no header (64u -> 68u)
  M2  declaracao de Bc250GfxRingTxnSubmit3DDraw suprimida do header
  M3  declaracao de Bc250GfxVerify3DDrawStream suprimida do header
  M4  emissao de pipeline de shaders no anel suprimida
  M5  emissao de color target 0 no anel suprimida
  M6  emissao de target mask de catalogo suprimida
  M7  emissao de viewport e scissor no anel suprimida
  M8  emissao de primitive assembler no anel suprimida
  M9  emissao de draw initiator no anel suprimida
  M10 emissao de physical fence WRITE_DATA suprimida
  M11 verificacao estrita de stream pre-disparo suprimida
  M12 classificacao estrita de tripe suprimida
  M13 injecao de poison na fence antes do disparo suprimida
  M14 checagem de readback de poison na fence suprimida
  M15 condicao de sucesso estrito de tripe provado suprimida
  M16 recusa fail-closed quando anel inativo suprimida
  M17 suite de testes de submit 3D draw suprimida do teste
  M18 chamada de submissao 3D draw no bringup Level 4 do miniport suprimida
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_ring_contract_test.c"
MINI_FILE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"


def _run_gate_inner(src_text: str, hdr_text: str, test_text: str, mini_text: str) -> list[str]:
    saved = (
        SRC_FILE.read_text(encoding="utf-8", errors="replace"),
        HDR_FILE.read_text(encoding="utf-8", errors="replace"),
        TEST_FILE.read_text(encoding="utf-8", errors="replace"),
        MINI_FILE.read_text(encoding="utf-8", errors="replace"),
    )
    try:
        SRC_FILE.write_text(src_text, encoding="utf-8")
        HDR_FILE.write_text(hdr_text, encoding="utf-8")
        TEST_FILE.write_text(test_text, encoding="utf-8")
        MINI_FILE.write_text(mini_text, encoding="utf-8")
        errs: list[str] = []
        safety.audit_gfx_3d_draw_is_honest(errs)
        return errs
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")
        MINI_FILE.write_text(saved[3], encoding="utf-8")


def _run_gate(src_text: str, hdr_text: str, test_text: str, mini_text: str) -> list[str]:
    with preserve_mtimes(SRC_FILE, HDR_FILE, TEST_FILE, MINI_FILE):
        return _run_gate_inner(src_text, hdr_text, test_text, mini_text)


def test_mutations():
    base_src = SRC_FILE.read_text(encoding="utf-8")
    base_hdr = HDR_FILE.read_text(encoding="utf-8")
    base_test = TEST_FILE.read_text(encoding="utf-8")
    base_mini = MINI_FILE.read_text(encoding="utf-8")

    clean_errs = _run_gate(base_src, base_hdr, base_test, base_mini)
    if clean_errs:
        print("FAIL: baseline de 3D Draw contem erros:", file=sys.stderr)
        for e in clean_errs:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("  baseline limpo")

    mutations = [
        # M1: Tamanho total do stream adulterado no header
        (
            "M1  tamanho total do stream adulterado no header (64u -> 68u)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS 64u",
                "#define BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS 68u",
            ),
            base_test,
            base_mini,
        ),
        # M2: Declaracao de Bc250GfxRingTxnSubmit3DDraw suprimida do header
        (
            "M2  declaracao de Bc250GfxRingTxnSubmit3DDraw suprimida do header",
            base_src,
            base_hdr.replace(
                "BC250_GFX_RING_TXN_STATUS Bc250GfxRingTxnSubmit3DDraw(",
                "/* suprimido */ /* Bc250GfxRingTxnSubmit3DDraw(",
            ),
            base_test,
            base_mini,
        ),
        # M3: Declaracao de Bc250GfxVerify3DDrawStream suprimida do header
        (
            "M3  declaracao de Bc250GfxVerify3DDrawStream suprimida do header",
            base_src,
            base_hdr.replace(
                "BC250_GFX_RING_TXN_STATUS Bc250GfxVerify3DDrawStream(",
                "/* suprimido */ /* Bc250GfxVerify3DDrawStream(",
            ),
            base_test,
            base_mini,
        ),
        # M4: Emissao de pipeline de shaders no anel suprimida
        (
            "M4  emissao de pipeline de shaders no anel suprimida",
            base_src.replace(
                "Bc250GfxShRegEmitPipeline(",
                "/* Bc250GfxShRegEmitPipeline */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M5: Emissao de color target 0 no anel suprimida
        (
            "M5  emissao de color target 0 no anel suprimida",
            base_src.replace(
                "Bc250Pm4EmitColorTarget0(",
                "/* Bc250Pm4EmitColorTarget0 */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M6: Emissao de target mask de catalogo suprimida
        (
            "M6  emissao de target mask de catalogo suprimida",
            base_src.replace(
                "BC250_PM4_REG_CB_TARGET_MASK",
                "0x1234u /* nao catalogo */",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M7: Emissao de viewport e scissor no anel suprimida
        (
            "M7  emissao de viewport e scissor no anel suprimida",
            base_src.replace(
                "Bc250GfxEmitViewportScissorStream(",
                "/* Bc250GfxEmitViewportScissorStream */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M8: Emissao de primitive assembler no anel suprimida
        (
            "M8  emissao de primitive assembler no anel suprimida",
            base_src.replace(
                "Bc250GfxEmitPrimitiveAssemblerStream(",
                "/* Bc250GfxEmitPrimitiveAssemblerStream */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M9: Emissao de draw initiator no anel suprimida
        (
            "M9  emissao de draw initiator no anel suprimida",
            base_src.replace(
                "Bc250GfxEmitDrawInitiatorStream(",
                "/* Bc250GfxEmitDrawInitiatorStream */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M10: Emissao de physical fence WRITE_DATA suprimida
        (
            "M10 emissao de physical fence WRITE_DATA suprimida",
            base_src.replace(
                "Bc250Pm4EmitWriteData(",
                "/* Bc250Pm4EmitWriteData */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M11: Verificacao estrita de stream pre-disparo suprimida
        (
            "M11 verificacao estrita de stream pre-disparo suprimida",
            base_src.replace(
                "Bc250GfxVerify3DDrawStream(",
                "/* Bc250GfxVerify3DDrawStream */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M12: Classificacao estrita de tripe suprimida
        (
            "M12 classificacao estrita de tripe suprimida",
            base_src.replace(
                "Bc250GfxRingClassifyTripod(",
                "/* Bc250GfxRingClassifyTripod */ 0 && (",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M13: Injecao de poison na fence antes do disparo suprimida
        (
            "M13 injecao de poison na fence antes do disparo suprimida",
            base_src.replace(
                "*SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;",
                "/* suprimido poison */",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M14: Checagem de readback de poison na fence suprimida
        (
            "M14 checagem de readback de poison na fence suprimida",
            base_src.replace(
                "SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue",
                "0 /* suprimido check poison */",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M15: Condicao de sucesso estrito de tripe provado suprimida
        (
            "M15 condicao de sucesso estrito de tripe provado suprimida",
            base_src.replace(
                "verdict == BC250_TRIPOD_PROVEN",
                "1 /* falso sucesso */",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M16: Recusa fail-closed quando anel inativo suprimida
        (
            "M16 recusa fail-closed quando anel inativo suprimida",
            base_src.replace(
                "return BC250_GFX_RING_TXN_NOT_ACTIVE;",
                "/* suprimido */",
            ),
            base_hdr,
            base_test,
            base_mini,
        ),
        # M17: Suite de testes de submit 3D draw suprimida do teste
        (
            "M17 suite de testes de submit 3D draw suprimida do teste",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250RunGfxRingSubmit3DDrawTest(",
                "/* suprimido */ (",
            ),
            base_mini,
        ),
        # M18: Chamada de submissao 3D draw no bringup Level 4 do miniport suprimida
        (
            "M18 chamada de submissao 3D draw no bringup Level 4 do miniport suprimida",
            base_src,
            base_hdr,
            base_test,
            base_mini.replace(
                "Level >= 4u",
                "Level >= 99u",
            ),
        ),
    ]

    failed = 0
    for name, s, h, t, m in mutations:
        errs = _run_gate(s, h, t, m)
        if not errs:
            print(f"  MUTACAO ESCAPOU: {name}", file=sys.stderr)
            failed += 1
        else:
            print(f"  ok: {name} (barrada: {len(errs)} erro(s))")

    if failed:
        print(f"\nFAIL: {failed}/{len(mutations)} mutacoes escaparam do gate", file=sys.stderr)
        sys.exit(1)
    print(f"\nPASS: todas as {len(mutations)} mutacoes foram detectadas pelo gate")


if __name__ == "__main__":
    test_mutations()
