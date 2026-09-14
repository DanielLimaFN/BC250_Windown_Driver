"""Mutate in-memory source snapshots: never touch the candidate on disk."""
from pathlib import Path
from unittest.mock import patch
import json
import runpy

ROOT = Path(__file__).resolve().parents[1]
audit = runpy.run_path(str(ROOT / "tools/audit-bc250mcdm-safety.py"))
gate = audit["audit_primary_is_not_overbroad"]
U = "driver/umd/bc250_d3d_umd.c"
T = "driver/tests/bc250_d3d_umd_test.c"
snapshot = {(ROOT / p): (ROOT / p).read_text(encoding="utf-8") for p in (U, T)}

# Each expected diagnostic belongs to the deliberately broken invariant.
mutations = [
    (
        "M1: overbroad primary derivation restored",
        U,
        "if (pCreate->pPrimaryDesc != NULL) {\n        if (pCreate->pPrimaryDesc->ModeDesc.Width != 0u",
        "if ((pCreate->BindFlags & D3D10_DDI_BIND_PRESENT) != 0u || pCreate->pPrimaryDesc != NULL) {\n        if (pCreate->pPrimaryDesc->ModeDesc.Width != 0u",
        "forbidden derivacao de primariedade excessivamente larga",
    ),
    (
        "M2b: delete dwm primary assert",
        T,
        "assert(resDwm.IsPrimary);",
        "assert(resDwm.Created == TRUE);",
        "missing teste de contrato exige que caso dwm.exe seja primaria",
    ),
    (
        "M2: comment out dwm primary assert with /* */",
        T,
        "assert(resDwm.IsPrimary);",
        "/* assert(resDwm.IsPrimary); */",
        "missing teste de contrato exige que caso dwm.exe seja primaria",
    ),
    (
        "M2c: neutralize with assert(1) and line comment",
        T,
        "assert(resDwm.IsPrimary);",
        "assert(1); // assert(resDwm.IsPrimary);",
        "missing teste de contrato exige que caso dwm.exe seja primaria",
    ),
    (
        "M2d: single-line comment //",
        T,
        "assert(resDwm.IsPrimary);",
        "// assert(resDwm.IsPrimary);",
        "missing teste de contrato exige que caso dwm.exe seja primaria",
    ),
    (
        "M3: comment out explorer non-primary assert",
        T,
        "assert(!resExplorer.IsPrimary);",
        "/* assert(!resExplorer.IsPrimary); */",
        "missing teste de contrato exige que caso Explorer.EXE nao seja primaria",
    ),
    (
        "M3b: delete explorer non-primary assert",
        T,
        "assert(!resExplorer.IsPrimary);",
        "assert(resExplorer.Created == TRUE);",
        "missing teste de contrato exige que caso Explorer.EXE nao seja primaria",
    ),
    (
        "M4: bypass assert with if (0)",
        T,
        "assert(resDwm.IsPrimary);",
        "if (0) assert(resDwm.IsPrimary);",
        "bypass de assert no teste de primariedade",
    ),
]


def check(files):
    errors = []
    with patch.object(Path, "read_text", lambda path, *a, **kw: files[path]):
        gate(errors)
    return errors


assert check(snapshot) == [], check(snapshot)
results = []
for name, file, old, new, diagnostic in mutations:
    files = dict(snapshot)
    path = ROOT / file
    assert old in files[path], (name, "mutation target missing")
    files[path] = files[path].replace(old, new, 1)
    errors = check(files)
    assert any(diagnostic in e for e in errors), (name, errors)
    results.append({"mutation": name, "detected_by": errors, "expected": diagnostic})
    print("PASS:", name, "->", diagnostic)

out = ROOT / "docs/evidence/f5-offline-2026-09-13"
out.mkdir(parents=True, exist_ok=True)
(out / "mutations.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
print(f"{len(results)}/{len(mutations)} mutations detected; baseline clean; sources unchanged")
