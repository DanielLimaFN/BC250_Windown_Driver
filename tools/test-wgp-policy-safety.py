"""Mutation checks for the firmware-owned 24-CU WGP/power policy gate.

Source substitutions happen in memory only; the working tree is never changed.
"""
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "wgp_policy_safety", Path(__file__).with_name("audit-bc250mcdm-safety.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
read_text = Path.read_text
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
        audit.audit_firmware_wgp_policy_is_readonly(errors)
    assert bool(errors) == (path is not None), (label, errors)
    print(f"PASS: {label}")


check("current firmware policy accepted")
check(
    "SPI WGP write allowlist rejected", transport_path,
    "ByteOffset == 0x000086A0u ||\n        ByteOffset == 0x0000A4B0u",
    "ByteOffset == 0x000086A0u ||\n        ByteOffset == BC250_HQD_MMIO_SPI_PG_STATIC_WGP_MASK ||\n        ByteOffset == 0x0000A4B0u")
check(
    "raw RLC power-gating write rejected", active_path,
    "(void)Bc250MmioTopologyRead32(&Context->Core.MmioTransport,\n            BC250_HQD_MMIO_RLC_PG_CNTL, &rlcPgCntl);",
    "(void)Bc250MmioTopologyWrite32(&Context->Core.MmioTransport,\n            BC250_HQD_MMIO_RLC_PG_CNTL, 0u);")
check(
    "firmware-policy verdict omission rejected", active_path,
    "Policy=FIRMWARE_PRESERVED", "Policy=REMOVED")
print("WGP POLICY SAFETY MUTATIONS: PASS (offline only)")
