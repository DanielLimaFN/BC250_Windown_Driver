"""Mutation gates for the defects measured in Corrida 41. Uses temp copies."""
import importlib.util
from pathlib import Path
import tempfile
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "safety_c41", ROOT / "tools/audit-bc250mcdm-safety.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

FILES = [
    "driver/tests/d3d11_native_conformance_test.cpp",
    "tools/test-d3d11-native-conformance.ps1",
    "external/radv-wddm/bc250-radv-wddm.patch",
    "driver/umd/bc250_umd_descriptors.c",
    "driver/umd/bc250_d3d_umd.c",
    "driver/umd/bc250_d3d_umd.h",
    "driver/umd/bc250_umd_resource_vk.c",
]

MUTATIONS = [
    ("tools/test-d3d11-native-conformance.ps1",
     "New-Item -Path $umdRegistryPath -ErrorAction Stop",
     "New-Item -Path $umdRegistryPath -Force -ErrorAction Stop",
     audit.audit_conformance_harness_checks_present),
    ("tools/test-d3d11-native-conformance.ps1",
     "if (-not (Test-Path -LiteralPath $umdRegistryPath))",
     "if ($true)", audit.audit_conformance_harness_checks_present),
    ("driver/umd/bc250_d3d_umd.c",
     "src + (SIZE_T)y * RowPitch, lineBytes",
     "src, lineBytes", audit.audit_texture_path_is_named_and_complete),
    ("driver/umd/bc250_umd_resource_vk.c",
     "region.bufferRowLength = bufferRowLength",
     "region.bufferRowLength = 0",
     audit.audit_texture_path_is_named_and_complete),
    ("driver/umd/bc250_umd_descriptors.c",
     "device->LastVkDrawFailureReason = refusedReason",
     "device->LastVkDrawFailureReason = NULL",
     audit.audit_texture_path_is_named_and_complete),
]

with tempfile.TemporaryDirectory(prefix="bc250-c41-mutations-") as directory:
    audit.ROOT = Path(directory)
    originals = {}
    for name in FILES:
        source = ROOT / name
        target = audit.ROOT / name
        target.parent.mkdir(parents=True, exist_ok=True)
        originals[name] = source.read_text(encoding="utf-8", errors="replace")
        target.write_text(originals[name], encoding="utf-8")

    for gate in (audit.audit_conformance_harness_checks_present,
                 audit.audit_texture_path_is_named_and_complete):
        errors = []
        gate(errors)
        assert not errors, errors

    for index, (name, old, new, gate) in enumerate(MUTATIONS, 1):
        assert old in originals[name], (index, old)
        target = audit.ROOT / name
        target.write_text(originals[name].replace(old, new, 1), encoding="utf-8")
        errors = []
        gate(errors)
        target.write_text(originals[name], encoding="utf-8")
        assert errors, f"M{index} escaped: {old}"
        print(f"M{index}: REJECTED: {errors[0]}")

print(f"CORRIDA41_MUTATIONS: PASS ({len(MUTATIONS)} rejected)")
