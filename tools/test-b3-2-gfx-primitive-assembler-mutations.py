#!/usr/bin/env python3
"""Mutacoes da etapa B.3.2 (Primitive Assembler e Topologia de Triangulos PM4)
contra audit_gfx_primitive_assembler_is_honest.

Cada mutacao reproduz uma violacao de invariante arquitetural (opcode de SET_UCONFIG_REG,
contagem exata de dwords, registradores de catalogo P1, topologia DI_PT_TRILIST,
reuso de Bc250Pm4EmitSetUconfigReg e Bc250Pm4EmitNop contra escrita plana sem wrap circular,
e politica fail-closed de validacao de limites):

  M1  opcode de SET_UCONFIG_REG alterado no header (0x79 -> 0x78)
  M2  contagem de dwords do stream alterada (8 -> 6)
  M3  header canonico de SET_UCONFIG_REG adulterado no header
  M4  header canonico de NOP adulterado no header
  M5  topologia canonica DI_PT_TRILIST adulterada (0x04 -> 0x05)
  M6  emissao de PRIM_TYPE usa registrador errado em vez de BC250_PM4_REG_VGT_PRIMITIVE_TYPE
  M7  emissao de INSTANCES usa registrador errado em vez de BC250_PM4_REG_VGT_NUM_INSTANCES
  M8  reuso de Bc250Pm4EmitSetUconfigReg substituido por escrita plana direta
  M9  reuso de Bc250Pm4EmitNop substituido por escrita plana direta
  M10 recusa nomeada de argumentos invalidos ou topologia invalida suprimida
  M11 recusa nomeada de divergencia no stream verificado suprimida
  M12 o TESTE perde a verificacao de configuracao de primitive assembler
  M13 o TESTE perde a verificacao de emissao de stream
  M14 o TESTE perde a verificacao de integridade de stream
  M15 o TESTE perde o caso de argumento invalido
  M16 o TESTE perde o caso de deteccao de corrupcao no stream
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_primitive_assembler.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_primitive_assembler.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_primitive_assembler_test.c"


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
        safety.audit_gfx_primitive_assembler_is_honest(errs)
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
        print("FAIL: baseline de Primitive Assembler contem erros:", file=sys.stderr)
        for e in clean_errs:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("  baseline limpo")

    mutations = [
        # M1: Opcode de SET_UCONFIG_REG alterado
        (
            "M1  opcode de SET_UCONFIG_REG alterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_UCONFIG_REG_OPCODE        0x79u",
                "#define BC250_GFX_PM4_SET_UCONFIG_REG_OPCODE        0x78u",
            ),
            base_test,
        ),
        # M2: Contagem de dwords do stream alterada
        (
            "M2  contagem de dwords do stream alterada (8 -> 6)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PRIM_STREAM_TOTAL_DWORDS          8u",
                "#define BC250_GFX_PRIM_STREAM_TOTAL_DWORDS          6u",
            ),
            base_test,
        ),
        # M3: Header canonico de SET_UCONFIG_REG adulterado
        (
            "M3  header canonico de SET_UCONFIG_REG adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW        0xC0017900u",
                "#define BC250_GFX_PM4_SET_UCONFIG_HEADER_1DW        0xC0027900u",
            ),
            base_test,
        ),
        # M4: Header canonico de NOP adulterado
        (
            "M4  header canonico de NOP adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_NOP_HEADER_1DW                0xC0001000u",
                "#define BC250_GFX_PM4_NOP_HEADER_1DW                0xC0011000u",
            ),
            base_test,
        ),
        # M5: Topologia canonica DI_PT_TRILIST adulterada
        (
            "M5  topologia canonica DI_PT_TRILIST adulterada (0x04 -> 0x05)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PRIM_TRILIST                      0x04u",
                "#define BC250_GFX_PRIM_TRILIST                      0x05u",
            ),
            base_test,
        ),
        # M6: Emissao de PRIM_TYPE usa registrador errado
        (
            "M6  emissao de PRIM_TYPE usa registrador errado em vez de BC250_PM4_REG_VGT_PRIMITIVE_TYPE",
            base_src.replace(
                "BC250_PM4_REG_VGT_PRIMITIVE_TYPE",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M7: Emissao de INSTANCES usa registrador errado
        (
            "M7  emissao de INSTANCES usa registrador errado em vez de BC250_PM4_REG_VGT_NUM_INSTANCES",
            base_src.replace(
                "BC250_PM4_REG_VGT_NUM_INSTANCES",
                "0x00000000u",
            ),
            base_hdr,
            base_test,
        ),
        # M8: Reuso de Bc250Pm4EmitSetUconfigReg substituido por escrita plana
        (
            "M8  reuso de Bc250Pm4EmitSetUconfigReg substituido por escrita plana direta",
            base_src.replace(
                "pm4Status = Bc250Pm4EmitSetUconfigReg(",
                "/* flat write */ RingBuffer[0] = 0; if (0) pm4Status = (BC250_PM4_STATUS)0; else pm4Status = (BC250_PM4_STATUS)0;",
            ),
            base_hdr,
            base_test,
        ),
        # M9: Reuso de Bc250Pm4EmitNop substituido por escrita plana
        (
            "M9  reuso de Bc250Pm4EmitNop substituido por escrita plana direta",
            base_src.replace(
                "pm4Status = Bc250Pm4EmitNop(",
                "/* flat write */ RingBuffer[0] = 0; if (0) pm4Status = (BC250_PM4_STATUS)0; else pm4Status = (BC250_PM4_STATUS)0;",
            ),
            base_hdr,
            base_test,
        ),
        # M10: Recusa de argumentos invalidos ou topologia invalida suprimida
        (
            "M10 recusa nomeada de argumentos invalidos ou topologia invalida suprimida",
            base_src.replace(
                "return BC250_GFX_PRIM_INVALID_ARGUMENT;",
                "return BC250_GFX_PRIM_OK;",
            ),
            base_hdr,
            base_test,
        ),
        # M11: Recusa de divergencia no stream verificado suprimida
        (
            "M11 recusa nomeada de divergencia no stream verificado suprimida",
            base_src.replace(
                "return BC250_GFX_PRIM_STREAM_MISMATCH;",
                "return BC250_GFX_PRIM_OK;",
            ),
            base_hdr,
            base_test,
        ),
        # M12: Teste perde verificacao de configuracao de primitive assembler
        (
            "M12 o TESTE perde a verificacao de configuracao de primitive assembler",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxComputePrimitiveAssembler",
                "Bc250MutedComputePrimitiveAssembler",
            ),
        ),
        # M13: Teste perde verificacao de emissao de stream
        (
            "M13 o TESTE perde a verificacao de emissao de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxEmitPrimitiveAssemblerStream",
                "Bc250MutedEmitPrimitiveAssemblerStream",
            ),
        ),
        # M14: Teste perde verificacao de integridade de stream
        (
            "M14 o TESTE perde a verificacao de integridade de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxVerifyPrimitiveAssemblerStream",
                "Bc250MutedVerifyPrimitiveAssemblerStream",
            ),
        ),
        # M15: Teste perde caso de argumento invalido
        (
            "M15 o TESTE perde o caso de argumento invalido",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_PRIM_INVALID_ARGUMENT",
                "0",
            ),
        ),
        # M16: Teste perde caso de deteccao de corrupcao no stream
        (
            "M16 o TESTE perde o caso de deteccao de corrupcao no stream",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_PRIM_STREAM_MISMATCH",
                "0",
            ),
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
        print(f"\nB.3.2 MUTATION SUITE: FAILED ({failed}/{len(mutations)} escaparam)", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"\nB.3.2 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")


if __name__ == "__main__":
    print("[B.3.2 MUTATION SUITE] Primitive Assembler e Topologia de Triangulos PM4")
    test_mutations()
