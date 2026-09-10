"""Mutation gates for drained MEC IB_VMID retargeting (offline only)."""
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "mec_vmid_retarget_safety", Path(__file__).with_name("audit-bc250mcdm-safety.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
read_text = Path.read_text
engine_path = audit.ROOT / "driver/wdk/bc250_submission_engine.c"
active_path = audit.ROOT / "driver/wdk/bc250_active_miniport.c"
transport_path = audit.ROOT / "driver/wdk/bc250_mmio_topology_transport.c"


def check(label, path=None, old=None, new=None):
    def mutated_read(file, *args, **kwargs):
        text = read_text(file, *args, **kwargs)
        if file == path:
            assert old in text, label
            return text.replace(old, new, 1)
        return text
    errors = []
    with patch.object(Path, "read_text", mutated_read):
        audit.audit_mec_vmid_retarget(errors)
    assert bool(errors) == (path is not None), (label, errors)
    print(f"PASS: {label}")


check("current drained VMID retarget accepted")
check("live-HQD ordering regression rejected", engine_path,
      "BC250_HQD_MMIO_ACTIVE, 0u",
      "BC250_HQD_MMIO_ACTIVE, 1u /* mutation: no deactivate */")
check("physical drain proof removal rejected", engine_path,
      "ringStatus = Bc250PersistentRingReconcileDrainedCursor(\n        &engine->ComputeRing, rptr, wptr",
      "ringStatus = Bc250PersistentRingPlanSubmission(\n        &engine->ComputeRing, rptr, wptr")
check("direct active-miniport VMID write rejected", active_path,
      "(void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,\n                BC250_HQD_MMIO_VMID, &readbackHqdVmid);",
      "(void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,\n                BC250_HQD_MMIO_VMID, targetHqdVmid);")
check("physical RPTR fabrication rejected", engine_path,
      "Bc250SubmissionRead32(engine, Context, BC250_HQD_MMIO_PQ_RPTR, &rptr)",
      "Bc250SubmissionWrite32(engine, Context, BC250_HQD_MMIO_PQ_RPTR, wptr)")
check("CP_BUSY_STAT write access rejected", transport_path,
      "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)\n{\n    return Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||",
      "static uint32_t Bc250MmioTopologyWriteOffsetAllowed(uint32_t ByteOffset)\n{\n    return ByteOffset == BC250_HQD_MMIO_CP_BUSY_STAT ||\n        Bc250MmioTopologyGlobalMmuOffsetAllowed(ByteOffset) ||")
print("MEC VMID RETARGET MUTATIONS: PASS (offline only)")
