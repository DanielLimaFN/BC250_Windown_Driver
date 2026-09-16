#!/usr/bin/env python3
"""Mutacoes da etapa B.3.3 (Draw Initiator e Disparo de Desenho PM4)
contra audit_gfx_draw_initiator_is_honest.

Cada mutacao reproduz uma violacao de invariante arquitetural (opcode de DRAW_INDEX_AUTO,
contagem exata de dwords, valor de Packet-2 NOP, modo canonico AUTO_INDEX,
reuso de Bc250Pm4EmitDrawIndexAuto e Bc250Pm4EmitPacket2Nop contra escrita plana sem wrap circular,
e politica fail-closed de validacao de limites):

  M1  opcode de DRAW_INDEX_AUTO alterado no header (0x2D -> 0x2C)
  M2  contagem de dwords do stream alterada (4 -> 6)
  M3  header canonico de DRAW_INDEX_AUTO adulterado no header
  M4  valor canonico do Packet-2 NOP adulterado no header
  M5  modo canonico AUTO_INDEX adulterado (0x02 -> 0x03)
  M6  emissao de DRAW_INDEX_AUTO suprime reuso do helper
  M7  emissao de Packet-2 NOP suprime reuso do helper
  M8  reuso de Bc250Pm4EmitDrawIndexAuto substituido por escrita plana direta
  M9  reuso de Bc250Pm4EmitPacket2Nop substituido por escrita plana direta
  M10 recusa nomeada de argumentos invalidos suprimida
  M11 recusa nomeada de divergencia no stream verificado suprimida
  M12 o TESTE perde a verificacao de configuracao de draw initiator
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

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_draw_initiator.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_draw_initiator.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_draw_initiator_test.c"


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
        safety.audit_gfx_draw_initiator_is_honest(errs)
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
        print("FAIL: baseline de Draw Initiator contem erros:", file=sys.stderr)
        for e in clean_errs:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("  baseline limpo")

    mutations = [
        # M1: Opcode de DRAW_INDEX_AUTO alterado
        (
            "M1  opcode de DRAW_INDEX_AUTO alterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_OPCODE        0x2Du",
                "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_OPCODE        0x2Cu",
            ),
            base_test,
        ),
        # M2: Contagem de dwords do stream alterada
        (
            "M2  contagem de dwords do stream alterada (4 -> 6)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_DRAW_STREAM_TOTAL_DWORDS          4u",
                "#define BC250_GFX_DRAW_STREAM_TOTAL_DWORDS          6u",
            ),
            base_test,
        ),
        # M3: Header canonico de DRAW_INDEX_AUTO adulterado
        (
            "M3  header canonico de DRAW_INDEX_AUTO adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_HEADER        0xC0012D00u",
                "#define BC250_GFX_PM4_DRAW_INDEX_AUTO_HEADER        0xC0022D00u",
            ),
            base_test,
        ),
        # M4: Header canonico de Packet-2 NOP adulterado
        (
            "M4  valor canonico do Packet-2 NOP adulterado no header",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_PM4_PACKET2_NOP                   0x80000000u",
                "#define BC250_GFX_PM4_PACKET2_NOP                   0x00000000u",
            ),
            base_test,
        ),
        # M5: Modo canonico AUTO_INDEX adulterado
        (
            "M5  modo canonico AUTO_INDEX adulterado (0x02 -> 0x03)",
            base_src,
            base_hdr.replace(
                "#define BC250_GFX_DRAW_INITIATOR_AUTO_INDEX         0x00000002u",
                "#define BC250_GFX_DRAW_INITIATOR_AUTO_INDEX         0x00000003u",
            ),
            base_test,
        ),
        # M6: Emissao de DRAW_INDEX_AUTO suprime reuso do helper
        (
            "M6  emissao de DRAW_INDEX_AUTO suprime reuso do helper",
            base_src.replace(
                "Bc250Pm4EmitDrawIndexAuto(",
                "Bc250MutedPm4EmitDrawIndexAuto(",
            ),
            base_hdr,
            base_test,
        ),
        # M7: Emissao de Packet-2 NOP suprime reuso do helper
        (
            "M7  emissao de Packet-2 NOP suprime reuso do helper",
            base_src.replace(
                "Bc250Pm4EmitPacket2Nop(",
                "Bc250MutedPm4EmitPacket2Nop(",
            ),
            base_hdr,
            base_test,
        ),
        # M8: Reuso de Bc250Pm4EmitDrawIndexAuto substituido por escrita plana
        (
            "M8  reuso de Bc250Pm4EmitDrawIndexAuto substituido por escrita plana direta",
            base_src.replace(
                "pm4Status = Bc250Pm4EmitDrawIndexAuto(",
                "/* flat write */ RingBuffer[0] = 0; if (0) pm4Status = (BC250_PM4_STATUS)0; else pm4Status = (BC250_PM4_STATUS)0;",
            ),
            base_hdr,
            base_test,
        ),
        # M9: Reuso de Bc250Pm4EmitPacket2Nop substituido por escrita plana
        (
            "M9  reuso de Bc250Pm4EmitPacket2Nop substituido por escrita plana direta",
            base_src.replace(
                "pm4Status = Bc250Pm4EmitPacket2Nop(",
                "/* flat write */ RingBuffer[0] = 0; if (0) pm4Status = (BC250_PM4_STATUS)0; else pm4Status = (BC250_PM4_STATUS)0;",
            ),
            base_hdr,
            base_test,
        ),
        # M10: Recusa nomeada de argumentos invalidos suprimida
        (
            "M10 recusa nomeada de argumentos invalidos suprimida",
            base_src.replace(
                "return BC250_GFX_DRAW_INVALID_ARGUMENT;",
                "return (BC250_GFX_DRAW_STATUS)99;",
            ),
            base_hdr,
            base_test,
        ),
        # M11: Recusa nomeada de divergencia no stream suprimida
        (
            "M11 recusa nomeada de divergencia no stream verificado suprimida",
            base_src.replace(
                "return BC250_GFX_DRAW_STREAM_MISMATCH;",
                "return (BC250_GFX_DRAW_STATUS)99;",
            ),
            base_hdr,
            base_test,
        ),
        # M12: Teste perde verificacao de calculo
        (
            "M12 o TESTE perde a verificacao de configuracao de draw initiator",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxComputeDrawInitiator(",
                "Bc250MutedComputeDrawInitiator(",
            ),
        ),
        # M13: Teste perde verificacao de emissao de stream
        (
            "M13 o TESTE perde a verificacao de emissao de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxEmitDrawInitiatorStream(",
                "Bc250MutedEmitDrawInitiatorStream(",
            ),
        ),
        # M14: Teste perde verificacao de integridade
        (
            "M14 o TESTE perde a verificacao de integridade de stream",
            base_src,
            base_hdr,
            base_test.replace(
                "Bc250GfxVerifyDrawInitiatorStream(",
                "Bc250MutedVerifyDrawInitiatorStream(",
            ),
        ),
        # M15: Teste perde caso de argumento invalido
        (
            "M15 o TESTE perde o caso de argumento invalido",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_DRAW_INVALID_ARGUMENT",
                "/* omitted */ (BC250_GFX_DRAW_STATUS)99",
            ),
        ),
        # M16: Teste perde caso de deteccao de corrupcao
        (
            "M16 o TESTE perde o caso de deteccao de corrupcao no stream",
            base_src,
            base_hdr,
            base_test.replace(
                "BC250_GFX_DRAW_STREAM_MISMATCH",
                "/* omitted */ (BC250_GFX_DRAW_STATUS)99",
            ),
        ),
    ]

    all_ok = True
    print(f"Executando {len(mutations)} mutacoes contra audit_gfx_draw_initiator_is_honest...")
    for label, src_m, hdr_m, test_m in mutations:
        errs = _run_gate(src_m, hdr_m, test_m)
        if not errs:
            print(f"  MUTACAO ESCAPOU: {label}", file=sys.stderr)
            all_ok = False
        else:
            print(f"  OK (capturada): {label}")

    if not all_ok:
        print("FAIL: Uma ou mais mutacoes escaparam do gate audit_gfx_draw_initiator_is_honest!", file=sys.stderr)
        sys.exit(1)

    print(f"\nTodas as {len(mutations)} mutacoes foram detectadas com sucesso pelo gate.")


if __name__ == "__main__":
    test_mutations()
