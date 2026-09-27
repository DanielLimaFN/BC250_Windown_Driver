#!/usr/bin/env python3
"""DX.31: prove that the static gate rejects lost Flip/MPO copy evidence."""

from pathlib import Path
import importlib.util
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
# preserve_mtimes: the live source is only read; every mutation is written to
# a TemporaryDirectory and removed there, so repository mtimes never change.
AUDIT = ROOT / "tools" / "audit-bc250mcdm-safety.py"
DISPLAY = ROOT / "driver" / "wdk" / "bc250_display_ddi.c"
DISPLAY_TEST = ROOT / "driver" / "tests" / "display_vidpn_test.c"

spec = importlib.util.spec_from_file_location("bc250_safety_dx31", AUDIT)
audit = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.dont_write_bytecode = True
spec.loader.exec_module(audit)

original = DISPLAY.read_text(encoding="utf-8")
mutations = {
    "discard_mpo3_failure": (
        'if (!NT_SUCCESS(copyStatus)) {',
        'if (FALSE) {',
        1,
    ),
    "remove_data_proof": (
        "if (mismatchCount != 0u)",
        "if (FALSE)",
        1,
    ),
    "remove_sdma_result_gate": (
        "if (!copied)",
        "if (FALSE)",
        1,
    ),
}

with tempfile.TemporaryDirectory(prefix="bc250-dx31-gate-") as temp:
    temp_root = Path(temp)
    target = temp_root / "driver" / "wdk" / "bc250_display_ddi.c"
    target.parent.mkdir(parents=True)
    target_test = temp_root / "driver" / "tests" / "display_vidpn_test.c"
    target_test.parent.mkdir(parents=True)
    target_test.write_text(DISPLAY_TEST.read_text(encoding="utf-8"), encoding="utf-8")
    audit.ROOT = temp_root
    target.write_text(original, encoding="utf-8")
    errors = []
    audit.audit_dx31_primary_copy_is_observable(errors)
    assert not errors, f"unmutated DX.31 gate failed: {errors}"
    for name, (before, after, occurrence) in mutations.items():
        assert original.count(before) >= occurrence, f"mutation target missing: {name}"
        start = -1
        for _ in range(occurrence):
            start = original.find(before, start + 1)
        mutated = original[:start] + after + original[start + len(before):]
        target.write_text(mutated, encoding="utf-8")
        errors = []
        audit.audit_dx31_primary_copy_is_observable(errors)
        assert errors, f"DX.31 gate missed mutation: {name}"
        print(f"MUTATION CAUGHT: {name}: {errors[0]}")

print("DX.31 PRIMARY COPY AUDIT MUTATIONS: PASS (offline)")
