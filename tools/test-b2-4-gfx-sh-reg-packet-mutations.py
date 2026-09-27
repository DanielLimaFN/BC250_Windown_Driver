#!/usr/bin/env python3
"""Mutacoes da etapa B.2.4 (emissao PM4 SET_SH_REG de shaders graficos e submissao no CP_RB0)
contra audit_gfx_sh_reg_packet_is_honest.

Cada mutacao reproduz uma violacao de invariante de hardware (proibicao do bit
compute no anel grafico, contagem exata de dwords, offsets de registradores P1,
reuso de Bc250Pm4EmitSetShReg contra escrita plana, politica fail-closed de ring cheio,
integridade do stream emitido, e tripe de submissao com poison na fence da Lei 2):

  M1  opcode de SET_SH_REG alterado no header (0x76 -> 0x75)
  M2  header Type-3 adulterado com bit compute (0xC0047600 -> 0xC0047602)
  M3  contagem de dwords por estagio alterada (6 -> 5)
  M4  contagem de dwords do pipeline alterada (12 -> 10)
  M5  definicao de bit compute proibido removida do header
  M6  injecao do macro de bit compute no .c
  M7  injecao do macro de bit compute no .h
  M8  emissao do VS usa offset errado em vez de BC250_GFX_SH_PGM_LO_VS
  M9  emissao do PS usa offset errado em vez de BC250_GFX_SH_PGM_LO_PS
  M10 recusa nomeada de ring cheio suprimida
  M11 recusa nomeada de registradores invalidos suprimida
  M12 recusa nomeada de divergencia em stream suprimida
  M13 checagem ativa contra bit compute no header suprimida no .c
  M14 o TESTE perde a emissao de pipeline completa
  M15 o TESTE perde a verificacao de stream integro
  M16 o TESTE perde o caso de ring cheio
  M17 o TESTE perde o caso de registradores invalidos
  M18 o TESTE perde o caso de divergencia em stream
  M19 o TESTE perde a verificacao de bit compute proibido
  M20 reuso de Bc250Pm4EmitSetShReg substituido por escrita plana (perde wrap circular)
  M21 escrita plana direta de VS sem tratar wrap inserida no .c
  M22 escrita plana direta de PS sem tratar wrap inserida no .c
  M23 submissao no anel suprime injecao fisica de poison na fence antes do disparo
  M24 submissao no anel suprime checagem de readback de poison na fence
  M25 submissao no anel suprime emissao de fence WRITE_DATA
  M26 submissao no anel aceita retorno sem tripe provado (Lei 2)
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_sh_reg_packet.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_sh_reg_packet.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_sh_reg_packet_test.c"
TXN_FILE = ROOT / "driver" / "core" / "bc250_gfx_ring_transaction.c"


def _run_gate_inner(src_text: str, hdr_text: str, test_text: str, txn_text: str = None) -> list[str]:
    if txn_text is None:
        txn_text = TXN_FILE.read_text(encoding="utf-8", errors="replace")
    saved = (
        SRC_FILE.read_text(encoding="utf-8", errors="replace"),
        HDR_FILE.read_text(encoding="utf-8", errors="replace"),
        TEST_FILE.read_text(encoding="utf-8", errors="replace"),
        TXN_FILE.read_text(encoding="utf-8", errors="replace"),
    )
    try:
        SRC_FILE.write_text(src_text, encoding="utf-8")
        HDR_FILE.write_text(hdr_text, encoding="utf-8")
        TEST_FILE.write_text(test_text, encoding="utf-8")
        TXN_FILE.write_text(txn_text, encoding="utf-8")
        errors: list[str] = []
        safety.audit_gfx_sh_reg_packet_is_honest(errors)
        return errors
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")
        TXN_FILE.write_text(saved[3], encoding="utf-8")


def _run_gate(src_text: str, hdr_text: str, test_text: str, txn_text: str = None) -> list[str]:
    with preserve_mtimes(SRC_FILE, HDR_FILE, TEST_FILE, TXN_FILE):
        return _run_gate_inner(src_text, hdr_text, test_text, txn_text)


def main() -> int:
    base_src = SRC_FILE.read_text(encoding="utf-8")
    base_hdr = HDR_FILE.read_text(encoding="utf-8")
    base_test = TEST_FILE.read_text(encoding="utf-8")
    base_txn = TXN_FILE.read_text(encoding="utf-8")

    errors = _run_gate(base_src, base_hdr, base_test, base_txn)
    if errors:
        print("[FAIL] baseline ja tem erros:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("[B.2.4 MUTATION SUITE] emissao PM4 SET_SH_REG de shaders graficos")
    print("  baseline limpo")

    mutations = [
        ("M1  opcode de SET_SH_REG alterado no header",
         base_src,
         base_hdr.replace("BC250_GFX_PM4_SET_SH_REG_OPCODE             0x76u",
                          "BC250_GFX_PM4_SET_SH_REG_OPCODE             0x75u"),
         base_test),

        ("M2  header Type-3 adulterado com bit compute",
         base_src,
         base_hdr.replace("BC250_GFX_PM4_SET_SH_REG_HEADER_4DW         0xC0047600u",
                          "BC250_GFX_PM4_SET_SH_REG_HEADER_4DW         0xC0047602u"),
         base_test),

        ("M3  contagem de dwords por estagio alterada (6 -> 5)",
         base_src,
         base_hdr.replace("BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE       6u",
                          "BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE       5u"),
         base_test),

        ("M4  contagem de dwords do pipeline alterada (12 -> 10)",
         base_src,
         base_hdr.replace("BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS  12u",
                          "BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS  10u"),
         base_test),

        ("M5  definicao de bit compute proibido removida do header",
         base_src,
         base_hdr.replace("BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN      0x02u",
                          "BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN      0x00u"),
         base_test),

        ("M6  injecao do macro de bit compute no .c",
         base_src + "\n/* teste */ uint32_t foo = BC250_PM4_SHADER_TYPE_COMPUTE;\n",
         base_hdr,
         base_test),

        ("M7  injecao do macro de bit compute no .h",
         base_src,
         base_hdr + "\n/* teste */ #define FOO BC250_PM4_SHADER_TYPE_COMPUTE\n",
         base_test),

        ("M8  emissao do VS usa offset errado",
         base_src.replace("BC250_GFX_SH_PGM_LO_VS, vsValues, 4u);",
                          "0x040u, vsValues, 4u);"),
         base_hdr,
         base_test),

        ("M9  emissao do PS usa offset errado",
         base_src.replace("BC250_GFX_SH_PGM_LO_PS, psValues, 4u);",
                          "0x000u, psValues, 4u);"),
         base_hdr,
         base_test),

        ("M10 recusa nomeada de ring cheio suprimida",
         base_src.replace("return BC250_GFX_SH_REG_RING_FULL;",
                          "return BC250_GFX_SH_REG_BAD_ARGUMENT;"),
         base_hdr,
         base_test),

        ("M11 recusa nomeada de registradores invalidos suprimida",
         base_src.replace("return BC250_GFX_SH_REG_INVALID_REGS;",
                          "return BC250_GFX_SH_REG_BAD_ARGUMENT;"),
         base_hdr,
         base_test),

        ("M12 recusa nomeada de divergencia em stream suprimida",
         base_src.replace("return BC250_GFX_SH_REG_STREAM_MISMATCH;",
                          "return BC250_GFX_SH_REG_BAD_ARGUMENT;"),
         base_hdr,
         base_test),

        ("M13 checagem ativa contra bit compute no header suprimida no .c",
         base_src.replace("(header & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) != 0u",
                          "0"),
         base_hdr,
         base_test),

        ("M14 o TESTE perde a emissao de pipeline completa",
         base_src,
         base_hdr,
         base_test.replace("Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &psRegs, &ev)",
                           "/* neutralizado */ 0")),

        ("M15 o TESTE perde a verificacao de stream integro",
         base_src,
         base_hdr,
         base_test.replace("Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs)",
                           "/* neutralizado */ 0")),

        ("M16 o TESTE perde o caso de ring cheio",
         base_src,
         base_hdr,
         base_test.replace("BC250_GFX_SH_REG_RING_FULL",
                           "/* neutralizado */ 999")),

        ("M17 o TESTE perde o caso de registradores invalidos",
         base_src,
         base_hdr,
         base_test.replace("BC250_GFX_SH_REG_INVALID_REGS",
                           "/* neutralizado */ 999")),

        ("M18 o TESTE perde o caso de divergencia em stream",
         base_src,
         base_hdr,
         base_test.replace("BC250_GFX_SH_REG_STREAM_MISMATCH",
                           "/* neutralizado */ 999")),

        ("M19 o TESTE perde a verificacao de bit compute proibido",
         base_src,
         base_hdr,
         base_test.replace("BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN",
                           "/* neutralizado */ 999")),

        ("M20 reuso de Bc250Pm4EmitSetShReg substituido por escrita plana",
         base_src.replace("Bc250Pm4EmitSetShReg(RingBuffer, RingDwords, CursorDwords,",
                          "/* neutralizado */ 0; //"),
         base_hdr,
         base_test),

        ("M21 escrita plana direta de VS sem tratar wrap inserida no .c",
         base_src + "\nvoid foo_wrap_vs(uint32_t *RingBuffer, uint32_t cursor) { RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_VS; }\n",
         base_hdr,
         base_test),

        ("M22 escrita plana direta de PS sem tratar wrap inserida no .c",
         base_src + "\nvoid foo_wrap_ps(uint32_t *RingBuffer, uint32_t cursor) { RingBuffer[cursor++] = BC250_GFX_SH_PGM_LO_PS; }\n",
         base_hdr,
         base_test),

        ("M23 submissao no anel suprime injecao fisica de poison na fence",
         base_src,
         base_hdr,
         base_test,
         base_txn.replace("*SubmitPlan->FenceCpuAddress = SubmitPlan->FencePoisonValue;",
                          "/* neutralizado */")),

        ("M24 submissao no anel suprime checagem de readback de poison na fence",
         base_src,
         base_hdr,
         base_test,
         base_txn.replace("SubmitEvidence->FencePoisonReadback != SubmitPlan->FencePoisonValue",
                          "0")),

        ("M25 submissao no anel suprime emissao de fence WRITE_DATA",
         base_src,
         base_hdr,
         base_test,
         base_txn.replace("Bc250Pm4EmitWriteData(",
                          "/* neutralizado */ 0; //")),

        ("M26 submissao no anel aceita retorno sem tripe provado",
         base_src,
         base_hdr,
         base_test,
         base_txn.replace("verdict == BC250_TRIPOD_PROVEN",
                          "1")),
    ]

    detected = 0
    total = len(mutations)
    for m in mutations:
        name = m[0]
        s = m[1]
        h = m[2]
        t = m[3]
        tx = m[4] if len(m) > 4 else None
        errs = _run_gate(s, h, t, tx)
        if not errs:
            print(f"  {name}: ESCAPOU (falso negativo)")
        else:
            print(f"  {name}: DETECTADA ({errs[0]})")
            detected += 1

    print(f"B.2.4 MUTATION SUITE: {'PASS' if detected == total else 'FAILED'} "
          f"({detected}/{total} detectadas)")
    return 0 if detected == total else 1


if __name__ == "__main__":
    sys.exit(main())
