"""Mutation gates for the drained MEC cursor reconciliation (offline only)."""
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "mec_cursor_safety", Path(__file__).with_name("audit-bc250mcdm-safety.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
read_text = Path.read_text
core_path = audit.ROOT / "driver/core/bc250_persistent_ring_contract.c"
engine_path = audit.ROOT / "driver/wdk/bc250_submission_engine.c"


def check(label, path=None, old=None, new=None):
    def mutated_read(file, *args, **kwargs):
        text = read_text(file, *args, **kwargs)
        if file == path:
            assert old in text, label
            return text.replace(old, new, 1)
        return text
    errors = []
    with patch.object(Path, "read_text", mutated_read):
        audit.audit_mec_cursor_reconciliation(errors)
    assert bool(errors) == (path is not None), (label, errors)
    print(f"PASS: {label}")


check("current cursor reconciliation accepted")
check("pending-fence gate removal rejected", core_path,
      "Ring->LastSubmittedFence != Ring->LastRetiredFence",
      "Ring->LastSubmittedFence == Ring->LastRetiredFence")
check("physical-drain gate removal rejected", core_path,
      "physicalRptr != physicalWptr", "physicalRptr == physicalWptr")
check("reconciliation after planning rejected", engine_path,
      "status = Bc250SubmissionReconcileCursorLocked(Context, engine);",
      "status = STATUS_SUCCESS; /* reconciliation removed */")
check("physical RPTR write rejected", engine_path,
      "Bc250SubmissionRead32(Engine, Context,\n                BC250_HQD_MMIO_PQ_RPTR, &physicalRptr)",
      "Bc250SubmissionWrite32(Engine, Context,\n                BC250_HQD_MMIO_PQ_RPTR, physicalWptr)")
print("MEC CURSOR RECONCILIATION MUTATIONS: PASS (offline only)")
