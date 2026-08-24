#!/usr/bin/env python3
"""Inventaria e valida estruturalmente os firmwares locais da BC-250."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

COMMON_HEADER = struct.Struct("<IIHHHHIIII")
FW_TYPES = {
    "cyan_skillfish2_me.bin": 1,
    "cyan_skillfish2_pfp.bin": 2,
    "cyan_skillfish2_ce.bin": 3,
    "cyan_skillfish2_mec.bin": 4,
    "cyan_skillfish2_mec2.bin": 5,
    "cyan_skillfish2_rlc.bin": 8,
    "cyan_skillfish2_sdma.bin": 9,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect_common(path: Path, data: bytes) -> dict[str, object]:
    (
        declared_size,
        header_size,
        header_major,
        header_minor,
        ip_major,
        ip_minor,
        ucode_version,
        ucode_size,
        ucode_offset,
        expected_crc32,
    ) = COMMON_HEADER.unpack_from(data)
    issues: list[str] = []

    if declared_size != len(data):
        issues.append(f"declared_size={declared_size} file_size={len(data)}")
    if header_size < COMMON_HEADER.size or header_size > len(data):
        issues.append(f"header_size invalido: {header_size}")
    if ucode_offset < header_size or ucode_offset > len(data):
        issues.append(f"ucode_offset invalido: {ucode_offset}")
    if ucode_offset + ucode_size > len(data):
        issues.append("payload ultrapassa o arquivo")

    payload = data[ucode_offset : ucode_offset + ucode_size]
    # O campo common_firmware_header.crc32 cobre todos os bytes depois do
    # cabecalho comum de 32 bytes, incluindo a extensao especifica do IP e o
    # padding anterior ao payload. Ele nao cobre somente ucode_size_bytes.
    actual_crc32 = zlib.crc32(data[COMMON_HEADER.size :]) & 0xFFFFFFFF
    if actual_crc32 != expected_crc32:
        issues.append(
            f"crc32 firmware esperado=0x{expected_crc32:08x} "
            f"calculado=0x{actual_crc32:08x}"
        )

    return {
        "name": path.name,
        "kind": "amdgpu_common_firmware",
        "psp_fw_type": FW_TYPES.get(path.name),
        "size_bytes": len(data),
        "sha256": sha256(data),
        "header": {
            "size_bytes": header_size,
            "version": f"{header_major}.{header_minor}",
            "ip_version": f"{ip_major}.{ip_minor}",
            "ucode_version": ucode_version,
            "ucode_size_bytes": ucode_size,
            "ucode_offset_bytes": ucode_offset,
            "crc32": f"0x{expected_crc32:08x}",
        },
        "payload_sha256": sha256(payload),
        "structurally_valid": not issues,
        "issues": issues,
    }


def inspect_file(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if path.name == "vbios_bc250.bin":
        issues = [] if data[:2] == b"\x55\xaa" else ["assinatura VBIOS 55 AA ausente"]
        return {
            "name": path.name,
            "kind": "atom_vbios",
            "size_bytes": len(data),
            "sha256": sha256(data),
            "structurally_valid": not issues,
            "issues": issues,
        }
    if len(data) < COMMON_HEADER.size:
        return {
            "name": path.name,
            "kind": "unknown",
            "size_bytes": len(data),
            "sha256": sha256(data),
            "structurally_valid": False,
            "issues": ["arquivo menor que common_firmware_header"],
        }
    return inspect_common(path, data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware_dir", nargs="?", type=Path, default=Path("firmware"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    files = sorted(args.firmware_dir.glob("*.bin"))
    entries = [inspect_file(path) for path in files]
    manifest = {
        "schema_version": 1,
        "purpose": "identidade e validacao estrutural; nao prova aceitacao ou execucao no silicio",
        "firmware": entries,
    }
    rendered = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)

    invalid = [entry["name"] for entry in entries if not entry["structurally_valid"]]
    if invalid:
        print("firmware estruturalmente invalido: " + ", ".join(invalid), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
