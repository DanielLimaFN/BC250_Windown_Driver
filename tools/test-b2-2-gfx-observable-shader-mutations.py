#!/usr/bin/env python3
"""Mutacoes da etapa B.2.2 (blobs e manifestos de shader grafico observavel) contra
audit_gfx_observable_shader_is_honest.

Cada mutacao reproduz uma violacao de proveniencia (Lei 1 / Lei 8), defeito de
instrucao RDNA 1 (GFX10) que causaria hang de pipeline, silenciamento de defeito
(D1/D4) ou apagamento de assercao/divida declarada (D2/D3):

  M1  classe epistemica P1 declarada falsamente sem compilador
  M2  remocao de s_endpgm no VS
  M3  remocao de s_endpgm no PS
  M4  adulteracao do opcode EXP pos0 no VS
  M5  adulteracao do opcode EXP mrt0 no PS
  M6  hash SHA-256 do VS corrompido no header
  M7  hash SHA-256 do PS corrompido no header
  M8  hash SHA-256 dinamico do VS detecta dword adulterado no blob C (D1)
  M9  hash SHA-256 dinamico do PS detecta dword adulterado no blob C (D1)
  M10 remocao da ressalva de circularidade epistemologica no header (D2)
  M11 remocao da declaracao formal de divida de triangulo de area zero (D3)
  M12 assinatura de Bc250PopulateGfxShaderPlanFromObservable revertida para void (D4)
  M13 o TESTE perde o teste de falha fechada com NULL em populate (D4)
  M14 o TESTE perde a validacao contra o contrato B.2.1
  M15 o TESTE perde a verificacao de calculo de registradores
  M16 o TESTE perde a rejeicao de blob sem s_endpgm
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_observable_shader.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_observable_shader.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_observable_shader_test.c"


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
        errors: list[str] = []
        safety.audit_gfx_observable_shader_is_honest(errors)
        return errors
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")


def run_gate(*args):
    with preserve_mtimes(SRC_FILE, HDR_FILE, TEST_FILE):
        return _run_gate_inner(*args)


def main() -> int:
    src = SRC_FILE.read_text(encoding="utf-8", errors="replace")
    hdr = HDR_FILE.read_text(encoding="utf-8", errors="replace")
    test = TEST_FILE.read_text(encoding="utf-8", errors="replace")

    print("[B.2.2 MUTATION SUITE] blobs e manifestos de shader grafico observavel")

    baseline = run_gate(src, hdr, test)
    if baseline:
        print("BASELINE REPROVOU -- o portao acusa o codigo intacto:")
        for item in baseline:
            print(f"  - {item}")
        return 1
    print("  baseline limpo")

    mutations = [
        ("M1  falsa declaracao de classe P1",
         src,
         hdr.replace('#define BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS "H"',
                     '#define BC250_GFX_OBSERVABLE_EPISTEMIC_CLASS "P1"', 1),
         test),

        ("M2  remocao de s_endpgm no VS",
         src.replace("BC250_OPCODE_S_ENDPGM\n};\n\nstatic const uint32_t g_Bc250GfxObservablePsShader",
                     "0x00000000u\n};\n\nstatic const uint32_t g_Bc250GfxObservablePsShader", 1),
         hdr,
         test),

        ("M3  remocao de s_endpgm no PS",
         src.replace("BC250_OPCODE_S_ENDPGM\n};\n\nstatic const BC250_GFX_OBSERVABLE_STAGE_MANIFEST g_Bc250GfxObservableVsManifest",
                     "0x00000000u\n};\n\nstatic const BC250_GFX_OBSERVABLE_STAGE_MANIFEST g_Bc250GfxObservableVsManifest", 1),
         hdr,
         test),

        ("M4  adulteracao do opcode EXP pos0 no VS",
         src.replace("0xF80008CFu,", "0x00000000u,", 1),
         hdr,
         test),

        ("M5  adulteracao do opcode EXP mrt0 no PS",
         src.replace("0xF800180Fu,", "0x00000000u,", 1),
         hdr,
         test),

        ("M6  hash SHA-256 do VS corrompido no header",
         src,
         hdr.replace("219387cf4964de41ac24153b4669b8d1313ef23ecb63ce07a886db69434b9539",
                     "0000000000000000000000000000000000000000000000000000000000000000", 1),
         test),

        ("M7  hash SHA-256 do PS corrompido no header",
         src,
         hdr.replace("f5787965e08da58c5bef2b296dee47dce32de9f62f8b7e93ec0c06a9bb2a16c2",
                     "0000000000000000000000000000000000000000000000000000000000000000", 1),
         test),

        ("M8  hash SHA-256 dinamico do VS detecta dword adulterado no blob C (D1)",
         src.replace("0x3F800000u,\n    0xF80008CFu", "0x40000000u,\n    0xF80008CFu", 1),
         hdr,
         test),

        ("M9  hash SHA-256 dinamico do PS detecta dword adulterado no blob C (D1)",
         src.replace("0x7E0002FFu,\n    0x3F800000u", "0x7E0002FFu,\n    0x40000000u", 1),
         hdr,
         test),

        ("M10 remocao da ressalva de circularidade epistemologica no header (D2)",
         src,
         hdr.replace("RESSALVA EPISTEMOLOGICA (D2)", "RESSALVA OMITIDA (D2)", 1),
         test),

        ("M11 remocao da declaracao formal de divida de triangulo de area zero (D3)",
         src,
         hdr.replace("DIVIDA DECLARADA B.2.2/D3", "DIVIDA OMITIDA B.2.2/D3", 1),
         test),

        ("M12 assinatura de Bc250PopulateGfxShaderPlanFromObservable revertida para void (D4)",
         src,
         hdr.replace("int Bc250PopulateGfxShaderPlanFromObservable(",
                     "void Bc250PopulateGfxShaderPlanFromObservable(", 1),
         test),

        ("M13 o TESTE perde o teste de falha fechada com NULL em populate (D4)",
         src,
         hdr,
         test.replace("Bc250PopulateGfxShaderPlanFromObservable(NULL",
                      "Bc250PopulateGfxShaderPlanFromObservable(&vs", 1)),

        ("M14 o TESTE perde validacao contra contrato B.2.1",
         src,
         hdr,
         test.replace("Bc250GfxShaderValidateBlob(vs->ShaderDwords", "Bc250GfxShaderValidateDummy(vs->ShaderDwords", 1)),

        ("M15 o TESTE perde verificacao de calculo de registradores",
         src,
         hdr,
         test.replace("Bc250GfxShaderComputeRegs(&vsPlan", "Bc250GfxShaderComputeDummy(&vsPlan", 1)),

        ("M16 o TESTE perde rejeicao sem s_endpgm",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_NO_ENDPGM", "BC250_GFX_SHADER_OK", 1)),
    ]

    failed = 0
    for name, m_src, m_hdr, m_test in mutations:
        errors = run_gate(m_src, m_hdr, m_test)
        if not errors:
            print(f"  {name}: ESCAPOU DO PORTAO")
            failed += 1
        else:
            print(f"  {name}: DETECTADA ({errors[0]})")

    if failed:
        print(f"B.2.2 MUTATION SUITE: FAILED ({failed}/{len(mutations)} escaparam)")
        return 1

    print(f"B.2.2 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
