#!/usr/bin/env python3
"""Mutacoes da etapa B.2 (programa de shader grafico) contra
audit_gfx_shader_contract_is_honest.

Cada mutacao reproduz um defeito que produz dano SILENCIOSO no silicio -- wave
lendo registrador alheio, ou engine grafica travada -- ou o apagamento da
assercao que impede o defeito de voltar:

  M1  a regra H vira o valor publicado (Rsrc1Computed no lugar do manifesto)
  M2  a recusa por divergencia com o manifesto desaparece
  M3  a falha deixa de ser fechada: a saida nao e' mais zerada
  M4  PgmLo recebe o endereco CRU -- perde addr[7:0] em silencio
  M5  PgmHi recebe o endereco CRU -- enderecaria so' 40 dos 48 bits
  M6  a recusa de endereco nao alinhado a 256 desaparece
  M7  MEM_ORDERED do VS copiado do PS -- escreveria VGPR_COMP_CNT
  M8  segunda definicao de S_ENDPGM no contrato (classe A13/B4/ContextAbi)
  M9  a delegacao da regra de ISA a fonte unica desaparece
  M10 Lei 6 trocada por OR cru sobre o manifesto em VGPRS
  M11 SHADER_TYPE=COMPUTE entra no caminho grafico
  M12 o TESTE perde a ancora contra o manifesto de compilador REAL
  M13 o TESTE perde o endereco alto -- sem ele LO cru passaria
  M14 o TESTE perde o contraponto -- a conferencia viraria peneira
  M15 assercao do teste neutralizada em vez de apagada
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

SRC_FILE = ROOT / "driver" / "core" / "bc250_gfx_shader_contract.c"
HDR_FILE = ROOT / "driver" / "core" / "bc250_gfx_shader_contract.h"
TEST_FILE = ROOT / "driver" / "tests" / "gfx_shader_contract_test.c"


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
        safety.audit_gfx_shader_contract_is_honest(errors)
        return errors
    finally:
        SRC_FILE.write_text(saved[0], encoding="utf-8")
        HDR_FILE.write_text(saved[1], encoding="utf-8")
        TEST_FILE.write_text(saved[2], encoding="utf-8")


def run_gate(*args):
    """Preserva o mtime: a guarda de obsolescencia do pacote de laboratorio
    compara DATAS, e reescrever o fonte com o mesmo conteudo ja invalidou o
    pacote e exigiu um rebuild destrutivo por nada (medido 14/09)."""
    with preserve_mtimes(SRC_FILE, HDR_FILE, TEST_FILE):
        return _run_gate_inner(*args)


def main() -> int:
    src = SRC_FILE.read_text(encoding="utf-8", errors="replace")
    hdr = HDR_FILE.read_text(encoding="utf-8", errors="replace")
    test = TEST_FILE.read_text(encoding="utf-8", errors="replace")

    print("[B.2 MUTATION SUITE] programa de shader grafico SPI_SHADER_PGM_*")

    baseline = run_gate(src, hdr, test)
    if baseline:
        print("BASELINE REPROVOU -- o portao acusa o codigo intacto:")
        for item in baseline:
            print(f"  - {item}")
        return 1
    print("  baseline limpo")

    mutations = [
        ("M1  regra H vira o valor publicado",
         src.replace("    Regs->Rsrc1 = Plan->Rsrc1Manifest;",
                     "    Regs->Rsrc1 = Regs->Rsrc1Computed;", 1), hdr, test),

        ("M2  recusa por divergencia desaparece",
         src.replace("        return BC250_GFX_SHADER_MANIFEST_MISMATCH;",
                     "        (void)0;", 1), hdr, test),

        ("M3  falha deixa de ser fechada (saida nao zerada)",
         src.replace("        Regs->Rsrc1 = 0u;\n        Regs->Rsrc2 = 0u;",
                     "", 1), hdr, test),

        ("M4  PgmLo recebe o endereco CRU",
         src.replace("Regs->PgmLo = (uint32_t)(Plan->GpuAddress >> 8);",
                     "Regs->PgmLo = (uint32_t)(Plan->GpuAddress);", 1), hdr, test),

        ("M5  PgmHi recebe o endereco CRU",
         src.replace("Regs->PgmHi = (uint32_t)(Plan->GpuAddress >> 40);",
                     "Regs->PgmHi = (uint32_t)(Plan->GpuAddress >> 32);", 1),
         hdr, test),

        ("M6  recusa de endereco desalinhado desaparece",
         src.replace("        return BC250_GFX_SHADER_MISALIGNED;",
                     "        (void)0;", 1), hdr, test),

        ("M7  MEM_ORDERED do VS copiado do PS",
         src,
         hdr.replace("#define BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK   0x08000000u",
                     "#define BC250_GFX_RSRC1_VS_MEM_ORDERED_MASK   0x02000000u", 1),
         test),

        ("M8  segunda definicao de S_ENDPGM no contrato",
         src, hdr.replace("#define BC250_GFX_SH_REG_BASE_DW 0x2C00u",
                          "#define BC250_GFX_SH_REG_BASE_DW 0x2C00u\n"
                          "#define BC250_GFX_OPCODE_S_ENDPGM 0xBF810000u", 1), test),

        ("M9  delegacao da regra de ISA desaparece",
         src.replace("Bc250ValidateComputeShaderIsa(", "Bc250FakeLocalIsaCheck(", 1),
         hdr, test),

        ("M10 Lei 6 trocada por OR cru em VGPRS",
         src.replace("rsrc1 = (Plan->Rsrc1Manifest & ~BC250_GFX_RSRC1_VGPRS_MASK) |",
                     "rsrc1 = (Plan->Rsrc1Manifest) |", 1), hdr, test),

        # A primeira versao desta mutacao substituia a linha do RSRC2, e por
        # isso reprovava pelo invariante ERRADO -- "RSRC2 vem do manifesto"
        # em vez de "SHADER_TYPE=COMPUTE proibido". Falha nao-atribuivel e'
        # portao fraco: a insercao agora NAO toca em nenhuma linha-ancora.
        ("M11 SHADER_TYPE=COMPUTE no caminho grafico",
         src.replace("#define BC250_GFX_SHADER_VA_BITS 48u",
                     "#define BC250_GFX_SHADER_VA_BITS 48u\n"
                     "#define BC250_LOCAL_SHADER_TYPE_COMPUTE (1u << 1)", 1),
         hdr, test),

        ("M12 o TESTE perde a ancora do compilador real",
         src, hdr,
         test.replace("Bc250GetObservableShaderManifest()",
                      "((const BC250_OBSERVABLE_SHADER_MANIFEST *)0)")),

        ("M13 o TESTE perde o endereco alto",
         src, hdr, test.replace("    CHECK(regs.PgmHi != 0u);\n", "", 1)),

        ("M14 o TESTE perde o contraponto de politica do compilador",
         src, hdr,
         test.replace(
             "CHECK(Bc250GfxShaderComputeRegs(&plan, &regs) == BC250_GFX_SHADER_OK);",
             "CHECK(1);")),

        ("M15 assercao do teste neutralizada em vez de apagada",
         src, hdr,
         test.replace("CHECK(regs.PgmHi != 0u);", "CHECK(1 /* regs.PgmHi */);", 1)),
    ]

    failures = 0
    for name, mut_src, mut_hdr, mut_test in mutations:
        if (mut_src, mut_hdr, mut_test) == (src, hdr, test):
            print(f"  {name}: MUTACAO NAO APLICOU -- ancora errada")
            failures += 1
            continue
        errors = run_gate(mut_src, mut_hdr, mut_test)
        if errors:
            print(f"  {name}: DETECTADA ({errors[0]})")
        else:
            print(f"  {name}: NAO DETECTADA -- portao fraco")
            failures += 1

    if failures:
        print(f"B.2 MUTATION SUITE: FAILED ({failures} de {len(mutations)})")
        return 1
    print(f"B.2 MUTATION SUITE: PASS ({len(mutations)}/{len(mutations)} detectadas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
