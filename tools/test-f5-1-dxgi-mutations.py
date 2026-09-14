"""Mutate in-memory source snapshots: never touch the candidate on disk."""
from pathlib import Path
from unittest.mock import patch
import runpy
import sys

ROOT = Path(__file__).resolve().parents[1]
audit = runpy.run_path(str(ROOT / "tools/audit-bc250mcdm-safety.py"))
gate = audit["audit_dxgi_1_6_table_complete"]
U = "driver/umd/bc250_d3d_umd.c"
T = "driver/tests/bc250_d3d_umd_test.c"
snapshot = {(ROOT / p): (ROOT / p).read_text(encoding="utf-8") for p in (U, T)}

mutations = [
    (
        "M1: omit pfnPresent1 in Bc250UmdPopulateDxgiBaseFuncs",
        U,
        "pFuncs->pfnPresent1 = Bc250DxgiPresent1;",
        "// pFuncs->pfnPresent1 = Bc250DxgiPresent1;",
        "missing slot pfnPresent1 na tabela DXGI 1.6",
    ),
    (
        "M2: omit pfnBlt1 in Bc250UmdPopulateDxgiBaseFuncs",
        U,
        "pFuncs->pfnBlt1 = Bc250DxgiBlt1;",
        "// pFuncs->pfnBlt1 = Bc250DxgiBlt1;",
        "missing slot pfnBlt1 na tabela DXGI 1.6",
    ),
    (
        "M3: fake MPO hardware planes (MaxPlanes = 2)",
        U,
        "pCapsData->MultiplaneOverlayCaps.MaxPlanes = 1;",
        "pCapsData->MultiplaneOverlayCaps.MaxPlanes = 2;",
        "missing declaracao honesta de 1 plano maximo",
    ),
    (
        "M4: fake MPO capability groups (NumCapabilityGroups = 1)",
        U,
        "pCapsData->MultiplaneOverlayCaps.NumCapabilityGroups = 0;",
        "pCapsData->MultiplaneOverlayCaps.NumCapabilityGroups = 1;",
        "missing declaracao honesta de zero grupos MPO",
    ),
    (
        "M5: omit pDXGIDDIBaseFunctions6_1 in CreateDevice",
        U,
        "pDXGIDDIBaseFunctions6_1",
        "pDXGIDDIBaseFunctions",
        "missing wiring de pDXGIDDIBaseFunctions6_1 no CreateDevice",
    ),
    (
        "M6: delete pfnPresent1 assert in contract test",
        T,
        "assert(dxgiFuncs.pfnPresent1 != NULL);",
        "// assert(dxgiFuncs.pfnPresent1 != NULL);",
        "missing teste de contrato assegurando presenca de pfnPresent1",
    ),
    (
        "M7: delete pfnBlt1 assert in contract test",
        T,
        "assert(dxgiFuncs.pfnBlt1 != NULL);",
        "// assert(dxgiFuncs.pfnBlt1 != NULL);",
        "missing teste de contrato assegurando presenca de pfnBlt1",
    ),
]

failed = False
print("[F5.1 MUTATION SUITE] Starting in-memory mutation checks on audit_dxgi_1_6_table_complete...")

for label, path_rel, target, replacement, expected_diag in mutations:
    p = ROOT / path_rel
    orig = snapshot[p]
    if target not in orig:
        print(f"  [ERROR] Target substring not found in {path_rel} for {label}")
        failed = True
        continue
    mutated = orig.replace(target, replacement)
    patched = {k: (mutated if k == p else v) for k, v in snapshot.items()}

    def fake_read_text(self, encoding="utf-8", errors="replace"):
        return patched.get(self, "")

    with patch.object(Path, "read_text", fake_read_text):
        errs = []
        gate(errs)
        matched = any(expected_diag.lower() in e.lower() for e in errs)
        if not matched:
            print(f"  [FAIL] {label}: expected diagnostic '{expected_diag}', got: {errs}")
            failed = True
        else:
            print(f"  [PASS] {label} correctly caught: {errs[0]}")

if failed:
    print("[F5.1 MUTATION SUITE] FAILED")
    sys.exit(1)
else:
    print("[F5.1 MUTATION SUITE] ALL 7/7 MUTATIONS DETECTED (EXIT=0)")
    sys.exit(0)
