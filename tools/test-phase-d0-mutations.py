#!/usr/bin/env python3
"""Mutacoes da Fase D0 (recusa com tripe e veto de WPTR) contra audit_phase_d0_refusal_tripod_and_wptr_veto.

Verifica que o portao de seguranca da Fase D0 reprova deterministamente qualquer
afrouxamento fail-open no caminho de submissao do Node 1:

  M1  remocao da medicao de recusa (Bc250ActiveGfxLogRefusalMeasurement) no SubmitVirtual Node 1
  M2  remocao da emissao de GFX_TRIPE_VEREDITO_RECUSA no helper de medicao
  M3  afrouxamento de WPTR_READBACK_DIVERGENTE (troca de STATUS_DEVICE_NOT_READY por STATUS_SUCCESS)
  M4  remocao do inventario com DoorbellDesligadaNoApply=1
"""

import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

safety = importlib.import_module("audit-bc250mcdm-safety")

MINI_FILE = ROOT / "driver" / "wdk" / "bc250_active_miniport.c"


def _run_gate_inner(mini_text: str) -> list[str]:
    saved = MINI_FILE.read_text(encoding="utf-8", errors="replace")
    try:
        MINI_FILE.write_text(mini_text, encoding="utf-8")
        errors: list[str] = []
        safety.audit_phase_d0_refusal_tripod_and_wptr_veto(errors)
        return errors
    finally:
        MINI_FILE.write_text(saved, encoding="utf-8")


def run_gate(mini_text: str) -> list[str]:
    with preserve_mtimes(MINI_FILE):
        return _run_gate_inner(mini_text)


def main() -> int:
    mini = MINI_FILE.read_text(encoding="utf-8", errors="replace")

    print("[FASE D0 MUTATION SUITE] recusa com tripe e veto de WPTR")

    baseline = run_gate(mini)
    if baseline:
        print("BASELINE REPROVOU -- o portao acusa o codigo intacto:")
        for item in baseline:
            print(f"  - {item}")
        return 1
    print("  baseline limpo")

    mutations = [
        (
            "M1: remocao de Bc250ActiveGfxLogRefusalMeasurement no SubmitVirtual",
            mini.replace(
                "Bc250ActiveGfxLogRefusalMeasurement(context,",
                "/* Bc250ActiveGfxLogRefusalMeasurement(context, */ //",
            ),
        ),
        (
            "M2: remocao de GFX_TRIPE_VEREDITO_RECUSA no helper de medicao",
            mini.replace(
                "GFX_TRIPE_VEREDITO_RECUSA",
                "GFX_TRIPE_VEREDITO_OMITIDO",
            ),
        ),
        (
            "M3: afrouxamento de WPTR_READBACK_DIVERGENTE para STATUS_SUCCESS",
            mini.replace(
                'Context->GfxEngine.LastRefusalReason = "WPTR_READBACK_DIVERGENTE";\n            Context->GfxEngine.LastRefusalCursor = cursor;\n            KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);\n            Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: WPTR_READBACK_DIVERGENTE',
                'Context->GfxEngine.LastRefusalReason = "WPTR_READBACK_DIVERGENTE";\n            Context->GfxEngine.LastRefusalCursor = cursor;\n            KeReleaseSpinLock(&Context->GfxEngine.Lock, irql);\n            return STATUS_SUCCESS;\n            Bc250DisplayLog("[GfxEngine] SUBMIT RECUSA: WPTR_READBACK_DIVERGENTE',
            ),
        ),
        (
            "M4: remocao de DoorbellDesligadaNoApply=1 no inventario de canais",
            mini.replace(
                "DoorbellDesligadaNoApply=1",
                "DoorbellDesligadaNoApply=0",
            ),
        ),
    ]

    failed_mutations = 0
    for name, mutated_text in mutations:
        if mutated_text == mini:
            print(f"  ERRO INTERNO: mutacao {name} nao alterou o texto!")
            return 1

        errors = run_gate(mutated_text)
        if not errors:
            print(f"  FALHA DE COBERTURA: {name} PASSOU pelo portao sem ser detectada!")
            failed_mutations += 1
        else:
            print(f"  PASS: {name} foi barrada ({len(errors)} diagnostico(s))")

    if failed_mutations > 0:
        print(f"\nREPROVADO: {failed_mutations} mutacao(oes) passaram sem deteccao.")
        return 1

    print("\nFASE D0 MUTATION SUITE: 4/4 MUTATIONS BARRADAS COM SUCESSO (PASS)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
