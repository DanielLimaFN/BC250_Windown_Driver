#!/usr/bin/env python3
"""Mutation testing for F5.2 Parede B (PM4 3D Draw Pipeline) safety gate.

Verifies that audit_f5_2_pm4_draw_pipeline catches:
  M1: Omitting Bc250Pm4EmitColorTarget0 from Bc250UmdEmitPm4Draw
  M2: Omitting Bc250Pm4EmitWindowScissor from Bc250UmdEmitPm4Draw
  M3: Omitting Bc250Pm4EmitDrawIndexAuto from Bc250UmdEmitPm4Draw
  M4: Omitting 256-byte alignment check for CB
  M5: Omitting NativePm4DrawCount++ telemetry
  M6: Omitting Bc250UmdEmitPm4Draw from Bc250Umd10Draw
  M7: Deleting assert(dev.NativePm4DrawCount == 1u) from contract test
  M8: Deleting negative misalignment test (0x10000004ull) from contract test
  M9: Omitting Bc250UmdExperimentalPm4DrawArmed check in Bc250UmdEmitPm4Draw
  M10: Deleting assert(dev.NativePm4DrawCount == 0u) fail-closed test from contract test
  M11: Omitting BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME from bc250_umd_caps_probe.h
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from _mutation_mtime import preserve_mtimes

import importlib
safety = importlib.import_module("audit-bc250mcdm-safety")

UMD_FILE = ROOT / "driver" / "umd" / "bc250_d3d_umd.c"
TEST_FILE = ROOT / "driver" / "tests" / "bc250_d3d_umd_test.c"
PM4_FILE = ROOT / "driver" / "core" / "bc250_pm4_packet_builder.c"
PROBE_FILE = ROOT / "driver" / "umd" / "bc250_umd_caps_probe.h"


def _run_gate_on_mock_inner(pm4_text: str, umd_text: str, test_text: str, probe_text: str) -> list[str]:
    saved_pm4 = PM4_FILE.read_text(encoding="utf-8", errors="replace")
    saved_umd = UMD_FILE.read_text(encoding="utf-8", errors="replace")
    saved_test = TEST_FILE.read_text(encoding="utf-8", errors="replace")
    saved_probe = PROBE_FILE.read_text(encoding="utf-8", errors="replace")
    try:
        PM4_FILE.write_text(pm4_text, encoding="utf-8")
        UMD_FILE.write_text(umd_text, encoding="utf-8")
        TEST_FILE.write_text(test_text, encoding="utf-8")
        PROBE_FILE.write_text(probe_text, encoding="utf-8")
        errors: list[str] = []
        safety.audit_f5_2_pm4_draw_pipeline(errors)
        return errors
    finally:
        PM4_FILE.write_text(saved_pm4, encoding="utf-8")
        UMD_FILE.write_text(saved_umd, encoding="utf-8")
        TEST_FILE.write_text(saved_test, encoding="utf-8")
        PROBE_FILE.write_text(saved_probe, encoding="utf-8")


def run_gate_on_mock(*args, **kwargs):
    """Mesma semantica da original, sem mexer no relogio dos fontes.

    A guarda de obsolescencia de package-bc250mcdm-lab.ps1 compara DATAS.
    Restaurar o conteudo sem restaurar o mtime invalidava o pacote de
    laboratorio e exigia um rebuild destrutivo por nada (medido 14/09).
    """
    with preserve_mtimes(UMD_FILE, TEST_FILE, PM4_FILE, PROBE_FILE):
        return _run_gate_on_mock_inner(*args, **kwargs)


def main() -> int:
    print("[F5.2 MUTATION SUITE] Starting in-memory mutation checks on audit_f5_2_pm4_draw_pipeline...")
    pm4_orig = PM4_FILE.read_text(encoding="utf-8", errors="replace")
    umd_orig = UMD_FILE.read_text(encoding="utf-8", errors="replace")
    test_orig = TEST_FILE.read_text(encoding="utf-8", errors="replace")
    probe_orig = PROBE_FILE.read_text(encoding="utf-8", errors="replace")

    baseline_errors = run_gate_on_mock(pm4_orig, umd_orig, test_orig, probe_orig)
    if baseline_errors:
        print(f"[FAIL] Baseline has errors: {baseline_errors}")
        return 1

    mutations = [
        ("M1: omit Bc250Pm4EmitColorTarget0 in Bc250UmdEmitPm4Draw",
         pm4_orig,
         umd_orig.replace("pm4Status = Bc250Pm4EmitColorTarget0(", "pm4Status = BC250_PM4_OK; // Bc250Pm4EmitColorTarget0("),
         test_orig,
         probe_orig,
         "emissao de CB_COLOR0 dentro do draw nativo"),

        ("M2: omit Bc250Pm4EmitWindowScissor in Bc250UmdEmitPm4Draw",
         pm4_orig,
         umd_orig.replace("pm4Status = Bc250Pm4EmitWindowScissor(", "pm4Status = BC250_PM4_OK; // Bc250Pm4EmitWindowScissor("),
         test_orig,
         probe_orig,
         "emissao de Scissor dentro do draw nativo"),

        ("M3: omit Bc250Pm4EmitDrawIndexAuto in Bc250UmdEmitPm4Draw",
         pm4_orig,
         umd_orig.replace("pm4Status = Bc250Pm4EmitDrawIndexAuto(", "pm4Status = BC250_PM4_OK; // Bc250Pm4EmitDrawIndexAuto("),
         test_orig,
         probe_orig,
         "emissao de DRAW_INDEX_AUTO dentro do draw nativo"),

        ("M4: omit 256-byte alignment check",
         pm4_orig,
         umd_orig.replace("dst->Allocation.GpuVirtualAddress & 0xFFull", "0 && dst->Allocation.GpuVirtualAddress"),
         test_orig,
         probe_orig,
         "exigencia de alinhamento de 256 bytes para CB"),

        ("M5: omit NativePm4DrawCount++ telemetry",
         pm4_orig,
         umd_orig.replace("pDevice->NativePm4DrawCount++;", "// pDevice->NativePm4DrawCount++;"),
         test_orig,
         probe_orig,
         "telemetria isolada de draws nativos PM4"),

        ("M6: reintroduce Bc250UmdEmitPm4Draw in Bc250Umd10Draw (D1.1)",
         pm4_orig,
         umd_orig.replace(
             "if (Bc250UmdExecuteVkDraw(device, VertexCount, StartVertexLocation, 1, 0, FALSE, 0)) return;",
             "if (Bc250UmdExecuteVkDraw(device, VertexCount, StartVertexLocation, 1, 0, FALSE, 0)) return;\n    if (Bc250UmdEmitPm4Draw(device, VertexCount, StartVertexLocation, 0, FALSE)) return;"
         ),
         test_orig,
         probe_orig,
         "D1.1: Bc250UmdEmitPm4Draw proibido no caminho de render (Bc250Umd10Draw)"),

        ("M7: delete assert(dev.NativePm4DrawCount == 1u) in contract test",
         pm4_orig,
         umd_orig,
         test_orig.replace("assert(dev.NativePm4DrawCount == 1u);", "// assert(dev.NativePm4DrawCount == 1u);"),
         probe_orig,
         "teste verifica incremento de NativePm4DrawCount"),

        ("M8: delete negative misalignment test in contract test",
         pm4_orig,
         umd_orig,
         test_orig.replace("0x10000004ull", "0x10000000ull"),
         probe_orig,
         "teste negativo de desalinhamento de base GPUVA"),

        ("M9: omit Bc250UmdExperimentalPm4DrawArmed check in Bc250UmdEmitPm4Draw",
         pm4_orig,
         umd_orig.replace("!Bc250UmdExperimentalPm4DrawArmed()", "/* !Bc250UmdExperimentalPm4DrawArmed() */ 0"),
         test_orig,
         probe_orig,
         "trava de tunable experimental para PM4 draw"),

        ("M10: delete assert(dev.NativePm4DrawCount == 0u) fail-closed test",
         pm4_orig,
         umd_orig,
         test_orig.replace("assert(dev.NativePm4DrawCount == 0u);", "// assert(dev.NativePm4DrawCount == 0u);"),
         probe_orig,
         "teste verifica rejeicao fail-closed com tunable desarmado"),

        ("M11: omit BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME in bc250_umd_caps_probe.h",
         pm4_orig,
         umd_orig,
         test_orig,
         probe_orig.replace('BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME "D3D11ExperimentalPm4Draw"', '// omit'),
         "definicao de tunable D3D11ExperimentalPm4Draw em bc250_umd_caps_probe.h"),

        ("M12: omit backend divergence check in Bc250UmdPresentVulkanDetileAndScanout (D1.2)",
         pm4_orig,
         umd_orig.replace(
             "device->NativePm4DrawCount > 0 && (device->NativeVkDrawCount > 0 || vkRendered)",
             "/* omitted */ 0"
         ),
         test_orig,
         probe_orig,
         "D1.2: deteccao de divergencia entre renderizador e apresentador"),
    ]

    passed = 0
    for name, pm4_m, umd_m, test_m, probe_m, expected_needle in mutations:
        errs = run_gate_on_mock(pm4_m, umd_m, test_m, probe_m)
        if any(expected_needle in e for e in errs):
            print(f"  [PASS] {name} correctly caught: {errs[0]}")
            passed += 1
        else:
            print(f"  [FAIL] {name} NOT caught! Errors: {errs}")
            return 1

    print(f"[F5.2 MUTATION SUITE] ALL {passed}/{len(mutations)} MUTATIONS DETECTED (EXIT=0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
