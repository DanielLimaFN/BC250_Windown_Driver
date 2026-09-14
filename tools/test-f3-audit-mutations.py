"""Mutate in-memory source snapshots: never touch the candidate on disk."""
from pathlib import Path
from unittest.mock import patch
import json
import runpy

ROOT = Path(__file__).resolve().parents[1]
audit = runpy.run_path(str(ROOT / "tools/audit-bc250mcdm-safety.py"))
gate = audit["audit_f3_pixel_and_residency_truth"]
U = "driver/umd/bc250_d3d_umd.c"
W = "driver/umd/bc250_wddm_winsys.c"
D = "driver/wdk/bc250_display_ddi.c"
T = "driver/tests/bc250_d3d_umd_test.c"
WT = "driver/tests/bc250_wddm_winsys_test.c"
P = "driver/core/bc250_blt_proof_contract.c"
PT = "driver/tests/blt_proof_contract_test.c"
snapshot = {(ROOT / p): (ROOT / p).read_text(encoding="utf-8") for p in (U, W, D, T, WT, P, PT)}

# Each expected diagnostic belongs to the deliberately broken invariant.
mutations = [
    ("clear error removed", U, "pfnSetErrorCb(device->hRTCoreLayer, E_NOTIMPL)", "pfnSetErrorCb(device->hRTCoreLayer, S_OK)", "F3 clear reports error"),
    ("clear shadow restored", U, "UNREFERENCED_PARAMETER(hRenderTargetView);", "((BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate)->pResource->pCpuShadow = NULL;", "F3 clear writes"),
    ("query bypassed", U, "pfnQueryResidencyCb(device->hRTDevice.handle, &args)", "pfnQueryResidencyCb(NULL, &args)", "F3 residency queries OS"),
    ("nonresident reported successful", U, "result = S_NOT_RESIDENT;", "result = S_OK;", "F3 nonresident aggregate"),
    ("map error discarded", U, "wsStatus = Bc250WinsysMapAllocation(&device->Winsys", "(void)Bc250WinsysMapAllocation(&device->Winsys", "F3 creation discards MapAllocation"),
    ("make resident error discarded", U, "wsStatus = Bc250WinsysMakeResident(&device->Winsys", "(void)Bc250WinsysMakeResident(&device->Winsys", "F3 creation discards MakeResident"),
    ("wait failure ignored", W, "if (waitStatus != BC250_WINSYS_OK) return waitStatus;", "if (waitStatus != BC250_WINSYS_OK) Context->LastCallbackHr = S_OK;", "F3 wait failure propagated"),
    ("pending retry adds reference", W, "if (Allocation->ResidencyPending) goto complete_residency;", "/* retry reissues request */", "F3 pending retry"),
    ("black suppression restored", D, "/* F3.2: black is valid content.", "if (srcOk && srcNz == 0u) return STATUS_SUCCESS;\n                /* F3.2: black is valid content.", "F3 content controls presentation"),
    ("failed DMA returns success", D, "STATUS_SUCCESS : STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;", "STATUS_SUCCESS : STATUS_SUCCESS;", "F3 failed copy"),
    ("copy predicate succeeds without execution", P, "return (CopyCompleted != 0u || SameSurface != 0u) ? 1u : 0u;", "return 1u;", "F3 copy outcome"),
    ("meta clear permits no error", T, "assert(g_SetErrorCalls == before + 1u && g_LastUmdError == E_NOTIMPL);", "assert(g_SetErrorCalls == before);", "F3 test requires Clear"),
    ("meta shadow can change", T, "assert(((unsigned char*)resColor.pCpuShadow)[0] == 0x5A);", "assert(((unsigned char*)resColor.pCpuShadow)[0] == 0);", "F3 test preserves Map shadow"),
    ("meta local residency accepted", T, "assert(g_QueryCalls == 2u);", "assert(g_QueryCalls == 0u);", "F3 test requires OS query"),
    ("meta nonresident success frozen", T, "assert(hr == S_NOT_RESIDENT);", "assert(hr == S_OK);", "F3 test requires nonresident"),
    ("meta partial creation allowed", T, "assert(!res.Created);", "assert(res.Created);", "F3 test rejects partial"),
    ("meta pending resident frozen", WT, "assert(!allocation.Resident);", "assert(allocation.Resident);", "F3 test forbids resident"),
    ("meta failed copy allowed", PT, "CHECK(Bc250PresentCopySucceeded(0u, 0u) == 0u);", "CHECK(Bc250PresentCopySucceeded(0u, 0u) == 1u);", "F3 test forbids failed copy"),
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
    files[path] = files[path].replace(old, new)
    errors = check(files)
    assert any(diagnostic in e for e in errors), (name, errors)
    results.append({"mutation": name, "detected_by": errors, "expected": diagnostic})
    print("PASS:", name, "->", diagnostic)
out = ROOT / "docs/evidence/f3-offline-2026-09-13"
out.mkdir(parents=True, exist_ok=True)
(out / "mutations.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
print(f"{len(results)}/{len(mutations)} mutations detected; baseline clean; sources unchanged")
