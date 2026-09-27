"""Mutation checks for VMID9 safety gates; operates only on temporary copies."""
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("safety", ROOT / "tools/audit-bc250mcdm-safety.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
paths = [
    "driver/wdk/bc250_active_miniport.c",
    "driver/core/bc250_gcvm_vmid8_transaction.c",
    "driver/wdk/bc250_mmio_topology_transport.c",
    "driver/wdk/bc250_submission_engine.c",
]
original = {name: (ROOT / name).read_text(encoding="utf-8") for name in paths}
mutations = [
    (paths[3], "engine->ComputeRing.Vmid = TargetVmid;", "engine->ComputeRing.Vmid = 8u;"),
    (paths[1], "return Submitted == Retired;", "return Submitted >= Retired;"),
    (paths[0], "io.Flush = Bc250ActiveFlushComputeTlb;", "io.Flush = Bc250ActiveFlushMappedGfxhubTlb;"),
    (paths[2], "Bc250GcvmIsComputeRegister(ByteOffset) ||", ""),
]
with tempfile.TemporaryDirectory(prefix="bc250-vmid9-audit-") as directory:
    audit.ROOT = Path(directory)
    for index, mutation in enumerate([None] + mutations):
        for name, source in original.items():
            target = audit.ROOT / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if mutation and name == mutation[0]:
                assert mutation[1] in source
                source = source.replace(mutation[1], mutation[2], 1)
            target.write_text(source, encoding="utf-8")
        errors = []
        audit.audit_compute_vmid_isolation(errors)
        if bool(errors) != bool(mutation):
            raise SystemExit(f"FAIL mutation {index}: {errors}")
        print(f"{index}: " + (f"REJECTED {errors}" if mutation else "BASELINE PASS"))
print("COMPUTE VMID AUDIT MUTATIONS: PASS; NAO VALIDADO EM HARDWARE")
