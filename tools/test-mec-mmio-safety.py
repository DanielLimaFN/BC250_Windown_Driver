"""Mutation checks for the defects measured in the 23:15 hardware run.

Substitute source text in memory only: never mutate the live working tree.
These check the regression gate, not GPU execution.
"""
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "mec_safety", Path(__file__).with_name("audit-bc250mcdm-safety.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
read_text = Path.read_text
engine_path = audit.ROOT / "driver/wdk/bc250_submission_engine.c"
active_path = audit.ROOT / "driver/wdk/bc250_active_miniport.c"


def check(label, path=None, old=None, new=None):
    def mutated_read(file, *args, **kwargs):
        text = read_text(file, *args, **kwargs)
        if file == path:
            assert old in text, label
            return text.replace(old, new, 1)
        return text
    errors = []
    with patch.object(Path, "read_text", mutated_read):
        audit.audit_mec_shared_mmio_lifetime(errors)
    assert bool(errors) == (path is not None), (label, errors)
    print(f"PASS: {label}")


check("current implementation accepted")
check("shared mapping released too early rejected", engine_path,
      "Engine->MmioMapped = 0u;",
      "Bc250MmioTopologyUnmap(&Context->Core.MmioTransport); Engine->MmioMapped = 0u;")
check("missing TLB acquire rejected", engine_path,
      "KeAcquireSpinLock(&Context->TlbLock, &irql);", "/* acquire removed */")
check("missing TLB release rejected", engine_path,
      "KeReleaseSpinLock(&Context->TlbLock, irql);", "/* release removed */")
check("unlocked caller rejected", engine_path,
      "status = Bc250SubmissionApplyCoherency(Context, Engine);",
      "status = Bc250SubmissionApplyCoherencyLocked(Context, Engine);")
check("MMU restore omitted rejected", active_path,
      "mmuRestoreStatus = Bc250GcvmGlobalMmuRestore(",
      "mmuRestoreStatus = MissingRestore(")
check("passive evidence archive omitted rejected", active_path,
      "[MEC_PENDING_ARCHIVE]", "[ARCHIVE_REMOVED]")
check("independent stop snapshot omitted rejected", active_path,
      "Bc250SubmissionEngineArchiveDiagnostics(context);", "/* snapshot removed */")
check("first poll capture omitted rejected", engine_path,
      "engine->DiagnosticPendingFence = pendingFence;", "/* capture removed */")
check("CPU IB archive omitted rejected", active_path,
      "[NODE1_IB_CPU_ARCHIVE]", "[ARCHIVE_REMOVED]")
print("MEC MMIO SAFETY MUTATIONS: PASS (offline only)")
