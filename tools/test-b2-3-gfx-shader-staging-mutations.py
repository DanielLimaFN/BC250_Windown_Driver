#!/usr/bin/env python3
"""Mutacoes da etapa B.2.3 (staging e alocacao de shaders na VRAM) contra
audit_gfx_shader_staging_is_honest.

Cada mutacao reproduz uma violacao de contrato formal, afrouxamento de alinhamento
de hardware (Lei 7), supressao de delimitacao de janela de VRAM e reservas (D2),
violacao de readback proof e barreira (Lei 2 / D1), evasao de fail-closed e limpeza
de memoria (Lei 4 / D3) ou afrouxamento do teste unitario:

  M1  alinhamento de hardware afrouxado no header (256 para 4 bytes)
  M2  mascara de alinhamento suprimida na implementacao
  M3  recusa nomeada de desalinhamento suprimida
  M4  zeragem de VsPlanOut na entrada suprimida (falha fechada Lei 4)
  M5  zeragem de PsPlanOut na entrada suprimida (falha fechada Lei 4)
  M6  zeragem de EvidenceOut na entrada suprimida (falha fechada Lei 4)
  M7  checagem de sobreposicao GPUVA suprimida
  M8  checagem de sobreposicao virtual CPU suprimida
  M9  recusa nomeada de sobreposicao suprimida
  M10 recusa nomeada de buffer pequeno demais suprimida
  M11 conferencia de readback do VS suprimida (Lei 2 / D1)
  M12 conferencia de readback do PS suprimida (Lei 2 / D1)
  M13 recusa nomeada de readback failed suprimida
  M14 validacao de manifesto do VS suprimida
  M15 validacao de manifesto do PS suprimida
  M16 calculo de registradores SPI suprimido
  M17 o TESTE perde a verificacao de desalinhamento de hardware
  M18 o TESTE perde a verificacao de sobreposicao de buffers
  M19 o TESTE perde a verificacao de buffer pequeno demais
  M20 o TESTE perde a verificacao de manifesto invalido
  M21 recusa nomeada de colisao com regiao reservada suprimida (D2)
  M22 recusa nomeada de endereco fora da janela de VRAM suprimida (D2)
  M23 barreira de memoria antes do readback suprimida (D1)
  M24 limpeza de memoria na falha de readback suprimida (D3)
  M25 o TESTE perde a injecao de falha de readback (D1)
  M26 o TESTE perde a verificacao de colisao de reserva (D2)
  M27 o TESTE perde a verificacao de janela de VRAM (D2)
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_shader_staging_contract.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_shader_staging_contract.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_shader_staging_test.c"


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
        safety.audit_gfx_shader_staging_is_honest(errors)
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

    print("[B.2.3 MUTATION SUITE] staging e alocacao de shaders na VRAM")

    baseline = run_gate(src, hdr, test)
    if baseline:
        print("BASELINE REPROVOU -- o portao acusa o codigo intacto:")
        for item in baseline:
            print(f"  - {item}")
        return 1
    print("  baseline limpo")

    mutations = [
        ("M1  alinhamento de hardware afrouxado no header (256 para 4 bytes)",
         src,
         hdr.replace("#define BC250_GFX_SHADER_STAGING_ALIGNMENT 256ull",
                     "#define BC250_GFX_SHADER_STAGING_ALIGNMENT 4ull", 1),
         test),

        ("M2  mascara de alinhamento suprimida na implementacao",
         src.replace("BC250_GFX_SHADER_STAGING_ALIGNMENT - 1ull", "0ull", 1),
         hdr,
         test),

        ("M3  recusa nomeada de desalinhamento suprimida",
         src.replace("return BC250_GFX_SHADER_STAGING_MISALIGNED;",
                     "return BC250_GFX_SHADER_STAGING_OK;", 1),
         hdr,
         test),

        ("M4  zeragem de VsPlanOut na entrada suprimida (falha fechada Lei 4)",
         src.replace("Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));",
                     "/* bypass VsPlanOut zero */", 1),
         hdr,
         test),

        ("M5  zeragem de PsPlanOut na entrada suprimida (falha fechada Lei 4)",
         src.replace("Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));",
                     "/* bypass PsPlanOut zero */", 1),
         hdr,
         test),

        ("M6  zeragem de EvidenceOut na entrada suprimida (falha fechada Lei 4)",
         src.replace("Bc250StagingZeroMemory(EvidenceOut, (uint32_t)sizeof(*EvidenceOut));",
                     "/* bypass EvidenceOut zero */", 1),
         hdr,
         test),

        ("M7  checagem de sobreposicao GPUVA suprimida",
         src.replace("vsGpuStart < psGpuEnd && psGpuStart < vsGpuEnd",
                     "0 /* bypass gpuva overlap */", 1),
         hdr,
         test),

        ("M8  checagem de sobreposicao virtual CPU suprimida",
         src.replace("vsVirtStart < psVirtEnd && psVirtStart < vsVirtEnd",
                     "0 /* bypass virtual overlap */", 1),
         hdr,
         test),

        ("M9  recusa nomeada de sobreposicao suprimida",
         src.replace("return BC250_GFX_SHADER_STAGING_OVERLAP;",
                     "return BC250_GFX_SHADER_STAGING_OK;", 1),
         hdr,
         test),

        ("M10 recusa nomeada de buffer pequeno demais suprimida",
         src.replace("return BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL;",
                     "return BC250_GFX_SHADER_STAGING_OK;", 1),
         hdr,
         test),

        ("M11 conferencia de readback do VS suprimida (Lei 2 / D1)",
         src.replace("val != VsManifest->ShaderDwords[i]",
                     "0 /* bypass vs readback check */", 1),
         hdr,
         test),

        ("M12 conferencia de readback do PS suprimida (Lei 2 / D1)",
         src.replace("val != PsManifest->ShaderDwords[i]",
                     "0 /* bypass ps readback check */", 1),
         hdr,
         test),

        ("M13 recusa nomeada de readback failed suprimida",
         src.replace("return BC250_GFX_SHADER_STAGING_READBACK_FAILED;",
                     "return BC250_GFX_SHADER_STAGING_OK;", 1),
         hdr,
         test),

        ("M14 validacao de manifesto do VS suprimida",
         src.replace("Bc250ValidateGfxObservableManifest(VsManifest)",
                     "1 /* bypass vs manifest check */", 1),
         hdr,
         test),

        ("M15 validacao de manifesto do PS suprimida",
         src.replace("Bc250ValidateGfxObservableManifest(PsManifest)",
                     "1 /* bypass ps manifest check */", 1),
         hdr,
         test),

        ("M16 calculo de registradores SPI suprimido",
         src.replace("Bc250GfxShaderComputeRegs(VsPlanOut",
                     "Bc250GfxShaderComputeDummy(VsPlanOut", 1),
         hdr,
         test),

        ("M17 o TESTE perde a verificacao de desalinhamento de hardware",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_MISALIGNED",
                      "BC250_GFX_SHADER_STAGING_OK")),

        ("M18 o TESTE perde a verificacao de sobreposicao de buffers",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_OVERLAP",
                      "BC250_GFX_SHADER_STAGING_OK")),

        ("M19 o TESTE perde a verificacao de buffer pequeno demais",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL",
                      "BC250_GFX_SHADER_STAGING_OK")),

        ("M20 o TESTE perde a verificacao de manifesto invalido",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_MANIFEST_INVALID",
                      "BC250_GFX_SHADER_STAGING_OK")),

        ("M21 recusa nomeada de colisao com regiao reservada suprimida (D2)",
         src.replace("return BC250_GFX_SHADER_STAGING_RESERVED_COLLISION;",
                     "return BC250_GFX_SHADER_STAGING_OK;"),
         hdr,
         test),

        ("M22 recusa nomeada de endereco fora da janela de VRAM suprimida (D2)",
         src.replace("return BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW;",
                     "return BC250_GFX_SHADER_STAGING_OK;", 1),
         hdr,
         test),

        ("M23 barreira de memoria antes do readback suprimida (D1)",
         src.replace("Bc250StagingMemoryBarrier(Transport);",
                     "/* bypass barrier */", 1),
         hdr,
         test),

        ("M24 limpeza de memoria na falha de readback suprimida (D3)",
         src.replace("EvidenceOut->MemoryClearedOnFailure = 1u;",
                     "/* bypass wipe flag */"),
         hdr,
         test),

        ("M25 o TESTE perde a injecao de falha de readback (D1)",
         src,
         hdr,
         test.replace("fixture.FailReadbackOffset = 2u;",
                      "/* bypass readback injection */")),

        ("M26 o TESTE perde a verificacao de colisao de reserva (D2)",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_RESERVED_COLLISION",
                      "BC250_GFX_SHADER_STAGING_OK")),

        ("M27 o TESTE perde a verificacao de janela de VRAM (D2)",
         src,
         hdr,
         test.replace("BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW",
                      "BC250_GFX_SHADER_STAGING_OK")),
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
        print(f"B.2.3 MUTATION SUITE: FAILED ({failed}/{len(mutations)} escaparam)")
        return 1

    print(f"B.2.3 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
