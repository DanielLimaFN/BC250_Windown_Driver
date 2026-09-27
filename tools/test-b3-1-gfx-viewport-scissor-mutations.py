#!/usr/bin/env python3
"""Mutacoes da etapa B.3.1 (Viewport, Scissor e Registradores de Contexto PM4)
contra audit_gfx_viewport_scissor_is_honest.

Cada mutacao reproduz uma violacao de invariante arquitetural (opcode de SET_CONTEXT_REG,
contagem exata de dwords, registradores de catalogo P1, reuso de Bc250Pm4EmitSetContextReg
contra escrita plana sem wrap circular, e politica fail-closed de validacao de limites):

  M1  opcode de SET_CONTEXT_REG alterado no header (0x69 -> 0x68)
  M2  contagem de dwords do stream alterada (20 -> 18)
  M3  valor canonico de VTE_CNTL adulterado no header
  M4  bit WINDOW_OFFSET_DISABLE removido do header de scissor
  M5  emissao de VPORT usa registrador errado em vez de BC250_PM4_REG_PA_CL_VPORT_XSCALE
  M6  emissao de RASTER usa registrador errado em vez de BC250_PM4_REG_PA_SU_SC_MODE_CNTL
  M7  emissao de Window Scissor usa registrador errado em vez de BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL
  M8  emissao de Viewport Scissor usa registrador errado em vez de BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL
  M9  reuso de Bc250Pm4EmitSetContextReg substituido por escrita plana direta
  M10 recusa nomeada de argumentos invalidos / NaN suprimida
  M11 recusa nomeada de limites fora da faixa suprimida
  M12 recusa nomeada de divergencia no stream verificado suprimida
  M13 o TESTE perde a verificacao de calculo matematico de viewport/scissor
  M14 o TESTE perde a verificacao de emissao de stream
  M15 o TESTE perde a verificacao de integridade de stream
  M16 o TESTE perde o caso de argumento invalido
  M17 o TESTE perde o caso de limites fora da faixa
  M18 o TESTE perde o caso de deteccao de corrupcao no stream
  M19 header de VPORT com count adulterado no header
  M20 header de RASTER com count adulterado no header
  M21 header de Window Scissor com count adulterado no header
  M22 header de Viewport Scissor com count adulterado no header
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_viewport_scissor.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_viewport_scissor.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_viewport_scissor_test.c"


def _run_gate_inner(src_text: str, hdr_text: str, test_text: str) -> list[str]:
    saved = (
        SRC_FILE.read_text(encoding="utf-8", errors="replace"),
        HDR_FILE.read_text(encoding="utf-8", errors="replace"),
        TEST_FILE.read_text(encoding="utf-8", errors="replace"),
    )
    try:
        SRC_FILE.write_text(src_text, encoding="utf-8")
        HDR_FILE.write_text(hdr_text, encoding="utf-8")
        TEST_FILE.write_text(test_text, encoding="utf-8")
        errs: list[str] = []
        safety.audit_gfx_viewport_scissor_is_honest(errs)
        return errs
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")


def _run_gate(src_text: str, hdr_text: str, test_text: str) -> list[str]:
    with preserve_mtimes(SRC_FILE, HDR_FILE, TEST_FILE):
        return _run_gate_inner(src_text, hdr_text, test_text)


def test_mutations():
    base_src = SRC_FILE.read_text(encoding="utf-8")
    base_hdr = HDR_FILE.read_text(encoding="utf-8")
    base_test = TEST_FILE.read_text(encoding="utf-8")

    clean_errs = _run_gate(base_src, base_hdr, base_test)
    if clean_errs:
        print("FAIL: baseline de Viewport/Scissor contem erros:", file=sys.stderr)
        for e in clean_errs:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("  baseline limpo")

    mutations = [
        # M1: Opcode de SET_CONTEXT_REG alterado
        (
            "M1  opcode de SET_CONTEXT_REG alterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_CONTEXT_REG_OPCODE        0x69u",
                "#define BC250_GFX_PM4_SET_CONTEXT_REG_OPCODE        0x68u",
            ),
            base_test,
        ),
        # M2: Contagem de dwords do stream alterada
        (
            "M2  contagem de dwords do stream alterada (20 -> 18)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_VPORT_STREAM_TOTAL_DWORDS         20u",
                "#define BC250_GFX_VPORT_STREAM_TOTAL_DWORDS         18u",
            ),
            base_test,
        ),
        # M3: Valor canonico de VTE_CNTL adulterado no header
        (
            "M3  valor canonico de VTE_CNTL adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_VTE_CNTL_DEFAULT                  0x0000043Fu",
                "#define BC250_GFX_VTE_CNTL_DEFAULT                  0x00000000u",
            ),
            base_test,
        ),
        # M4: Bit WINDOW_OFFSET_DISABLE removido do header
        (
            "M4  bit WINDOW_OFFSET_DISABLE removido do header de scissor",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_SCISSOR_OFFSET_DISABLE            0x80000000u",
                "#define BC250_GFX_SCISSOR_OFFSET_DISABLE            0x00000000u",
            ),
            base_test,
        ),
        # M5: Emissao de VPORT usa registrador errado
        (
            "M5  emissao de VPORT usa registrador errado em vez de BC250_PM4_REG_PA_CL_VPORT_XSCALE",
            base_src.replace(
                "BC250_PM4_REG_PA_CL_VPORT_XSCALE",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M6: Emissao de RASTER usa registrador errado
        (
            "M6  emissao de RASTER usa registrador errado em vez de BC250_PM4_REG_PA_SU_SC_MODE_CNTL",
            base_src.replace(
                "BC250_PM4_REG_PA_SU_SC_MODE_CNTL",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M7: Emissao de Window Scissor usa registrador errado
        (
            "M7  emissao de Window Scissor usa registrador errado em vez de BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL",
            base_src.replace(
                "BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M8: Emissao de Viewport Scissor usa registrador errado
        (
            "M8  emissao de Viewport Scissor usa registrador errado em vez de BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL",
            base_src.replace(
                "BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M9: Reuso de Bc250Pm4EmitSetContextReg substituido por escrita plana
        (
            "M9  reuso de Bc250Pm4EmitSetContextReg substituido por escrita plana direta",
            base_src.replace(
                "pm4Status = Bc250Pm4EmitSetContextReg(",
                "/* flat write */ RingBuffer[0] = 0; if (0) pm4Status = (BC250_PM4_STATUS)0; else pm4Status = (BC250_PM4_STATUS)0;",
            ),
            base_hdr,
            base_test,
        ),
        # M10: Recusa de argumentos invalidos suprimida
        (
            "M10 recusa nomeada de argumentos invalidos / NaN suprimida",
            base_src.replace(
                "return BC250_GFX_VPORT_INVALID_ARGUMENT;",
                "return BC250_GFX_VPORT_OK;",
            ),
            base_hdr,
            base_test,
        ),
        # M11: Recusa de limites fora da faixa suprimida
        (
            "M11 recusa nomeada de limites fora da faixa suprimida",
            base_src.replace(
                "return BC250_GFX_VPORT_OUT_OF_BOUNDS;",
                "return BC250_GFX_VPORT_OK;",
            ),
            base_hdr,
            base_test,
        ),
        # M12: Recusa de divergencia no stream verificado suprimida
        (
            "M12 recusa nomeada de divergencia no stream verificado suprimida",
            base_src.replace(
                "return BC250_GFX_VPORT_STREAM_MISMATCH;",
                "return BC250_GFX_VPORT_OK;",
            ),
            base_hdr,
            base_test,
        ),
        # M13: Teste perde verificacao de calculo matematico
        (
            "M13 o TESTE perde a verificacao de calculo matematico de viewport/scissor",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxComputeViewportScissor",
                "Bc250MutedComputeViewportScissor",
            ),
        ),
        # M14: Teste perde verificacao de emissao de stream
        (
            "M14 o TESTE perde a verificacao de emissao de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxEmitViewportScissorStream",
                "Bc250MutedEmitViewportScissorStream",
            ),
        ),
        # M15: Teste perde verificacao de integridade de stream
        (
            "M15 o TESTE perde a verificacao de integridade de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxVerifyViewportScissorStream",
                "Bc250MutedVerifyViewportScissorStream",
            ),
        ),
        # M16: Teste perde caso de argumento invalido
        (
            "M16 o TESTE perde o caso de argumento invalido",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_VPORT_INVALID_ARGUMENT",
                "0",
            ),
        ),
        # M17: Teste perde caso de limites fora da faixa
        (
            "M17 o TESTE perde o caso de limites fora da faixa",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_VPORT_OUT_OF_BOUNDS",
                "0",
            ),
        ),
        # M18: Teste perde caso de deteccao de corrupcao no stream
        (
            "M18 o TESTE perde o caso de deteccao de corrupcao no stream",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_VPORT_STREAM_MISMATCH",
                "0",
            ),
        ),
        # M19: Header de VPORT com count adulterado
        (
            "M19 header de VPORT com count adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_CTX_VPORT_HEADER_6DW      0xC0066900u",
                "#define BC250_GFX_PM4_SET_CTX_VPORT_HEADER_6DW      0xC0076900u",
            ),
            base_test,
        ),
        # M20: Header de RASTER com count adulterado
        (
            "M20 header de RASTER com count adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_CTX_RASTER_HEADER_2DW     0xC0026900u",
                "#define BC250_GFX_PM4_SET_CTX_RASTER_HEADER_2DW     0xC0036900u",
            ),
            base_test,
        ),
        # M21: Header de Window Scissor com count adulterado
        (
            "M21 header de Window Scissor com count adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_CTX_WIN_SCISSOR_HEADER_2DW 0xC0026900u",
                "#define BC250_GFX_PM4_SET_CTX_WIN_SCISSOR_HEADER_2DW 0xC0036900u",
            ),
            base_test,
        ),
        # M22: Header de Viewport Scissor com count adulterado
        (
            "M22 header de Viewport Scissor com count adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_CTX_VP_SCISSOR_HEADER_2DW  0xC0026900u",
                "#define BC250_GFX_PM4_SET_CTX_VP_SCISSOR_HEADER_2DW  0xC0036900u",
            ),
            base_test,
        ),
    ]

    failed = 0
    for name, s, h, t in mutations:
        errs = _run_gate(s, h, t)
        if not errs:
            print(f"  {name}: ESCAPOU (nao detectada)", file=sys.stderr)
            failed += 1
        else:
            print(f"  {name}: DETECTADA ({errs[0]})")

    if failed:
        print(f"\nB.3.1 MUTATION SUITE: FAILED ({failed}/{len(mutations)} escaparam)", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"\nB.3.1 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")


if __name__ == "__main__":
    print("[B.3.1 MUTATION SUITE] Viewport, Scissor e Registradores de Contexto PM4")
    test_mutations()
