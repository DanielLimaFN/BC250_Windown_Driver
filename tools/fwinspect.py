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
GFX_HEADER_V1_0 = struct.Struct("<III")
FW_TYPES = {
    "cyan_skillfish2_me.bin": 1,
    "cyan_skillfish2_pfp.bin": 2,
    "cyan_skillfish2_ce.bin": 3,
    "cyan_skillfish2_mec.bin": 4,
    "cyan_skillfish2_mec2.bin": 5,
    "cyan_skillfish2_rlc.bin": 8,
    "cyan_skillfish2_sdma.bin": 9,
}

GPU_INFO_NAMES = {
    "cyan_skillfish_gpu_info.bin",
    "cyan_skillfish1_gpu_info.bin",
}
GPU_INFO_V1_0 = struct.Struct("<15I")
GPU_INFO_V1_1_EXTRA = struct.Struct("<2I")
GPU_INFO_FIELDS = (
    "gc_num_se",
    "gc_num_cu_per_sh",
    "gc_num_sh_per_se",
    "gc_num_rb_per_se",
    "gc_num_tccs",
    "gc_num_gprs",
    "gc_num_max_gs_thds",
    "gc_gs_table_depth",
    "gc_gsprim_buff_depth",
    "gc_parameter_cache_depth",
    "gc_double_offchip_lds_buffer",
    "gc_wave_size",
    "gc_max_waves_per_simd",
    "gc_max_scratch_slots_per_cu",
    "gc_lds_size",
)


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

    result: dict[str, object] = {
        "name": path.name,
        "kind": (
            "amdgpu_gpu_info_firmware"
            if path.name in GPU_INFO_NAMES
            else "amdgpu_common_firmware"
        ),
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

    if path.name in GPU_INFO_NAMES:
        inspect_gpu_info(
            data=data,
            header_size=header_size,
            ucode_offset=ucode_offset,
            ucode_size=ucode_size,
            issues=issues,
            result=result,
        )
        result["structurally_valid"] = not issues

    if path.name in {
        "cyan_skillfish2_ce.bin",
        "cyan_skillfish2_me.bin",
        "cyan_skillfish2_mec.bin",
        "cyan_skillfish2_mec2.bin",
        "cyan_skillfish2_pfp.bin",
    }:
        inspect_gfx_header(
            data=data,
            header_size=header_size,
            ucode_size=ucode_size,
            issues=issues,
            result=result,
        )
        result["structurally_valid"] = not issues

    return result


def inspect_gfx_header(
    *,
    data: bytes,
    header_size: int,
    ucode_size: int,
    issues: list[str],
    result: dict[str, object],
) -> None:
    """Decodifica gfx_firmware_header_v1_0 e valida a faixa MEC/JT."""
    required_header = COMMON_HEADER.size + GFX_HEADER_V1_0.size
    if header_size < required_header or len(data) < required_header:
        issues.append("cabecalho gfx menor que 44 bytes")
        return

    feature_version, jt_offset_dwords, jt_size_dwords = (
        GFX_HEADER_V1_0.unpack_from(data, COMMON_HEADER.size)
    )
    gfx = {
        "ucode_feature_version": feature_version,
        "jt_offset_dwords": jt_offset_dwords,
        "jt_size_dwords": jt_size_dwords,
    }
    result["gfx_header"] = gfx

    if result["name"] not in {
        "cyan_skillfish2_mec.bin",
        "cyan_skillfish2_mec2.bin",
    }:
        return

    jt_offset_bytes = jt_offset_dwords * 4
    jt_size_bytes = jt_size_dwords * 4
    jt_end_bytes = jt_offset_bytes + jt_size_bytes
    gfx["mec_main_payload_bytes"] = jt_offset_bytes
    gfx["mec_jt_payload_bytes"] = jt_size_bytes
    gfx["mec_jt_end_bytes"] = jt_end_bytes
    if jt_size_bytes == 0:
        issues.append("MEC jump table vazia")
    if jt_offset_bytes > ucode_size or jt_end_bytes != ucode_size:
        issues.append(
            "faixa MEC/JT invalida: "
            f"offset={jt_offset_bytes} size={jt_size_bytes} "
            f"end={jt_end_bytes} ucode={ucode_size}"
        )


def inspect_gpu_info(
    *,
    data: bytes,
    header_size: int,
    ucode_offset: int,
    ucode_size: int,
    issues: list[str],
    result: dict[str, object],
) -> None:
    """Decodifica gpu_info_firmware_header_v1_0 e seu payload little-endian."""
    if header_size < COMMON_HEADER.size + 4 or len(data) < COMMON_HEADER.size + 4:
        issues.append("cabecalho gpu_info menor que 36 bytes")
        return

    payload_major, payload_minor = struct.unpack_from("<HH", data, COMMON_HEADER.size)
    result["gpu_info_version"] = f"{payload_major}.{payload_minor}"
    if payload_major != 1:
        issues.append(f"gpu_info version_major nao suportada: {payload_major}")
        return

    required_size = GPU_INFO_V1_0.size
    if payload_minor >= 1:
        required_size += GPU_INFO_V1_1_EXTRA.size
    if ucode_size < required_size or ucode_offset + required_size > len(data):
        issues.append(
            f"payload gpu_info v{payload_major}.{payload_minor} truncado: "
            f"{ucode_size} < {required_size} bytes"
        )
        return

    values = GPU_INFO_V1_0.unpack_from(data, ucode_offset)
    decoded = dict(zip(GPU_INFO_FIELDS, values))
    if payload_minor >= 1:
        num_sc_per_sh, num_packer_per_sc = GPU_INFO_V1_1_EXTRA.unpack_from(
            data, ucode_offset + GPU_INFO_V1_0.size
        )
        decoded["num_sc_per_sh"] = num_sc_per_sh
        decoded["num_packer_per_sc"] = num_packer_per_sc

    for field in ("gc_num_se", "gc_num_cu_per_sh", "gc_num_sh_per_se"):
        if decoded[field] == 0:
            issues.append(f"topologia invalida: {field}=0")
    for field in ("gc_num_se", "gc_num_sh_per_se"):
        if decoded[field] > 256:
            issues.append(f"topologia excede seletor GRBM de 8 bits: {field}={decoded[field]}")

    result["gpu_info"] = decoded
    result["topology"] = {
        "shader_engines": decoded["gc_num_se"],
        "shader_arrays_per_se": decoded["gc_num_sh_per_se"],
        "max_cu_per_shader_array": decoded["gc_num_cu_per_sh"],
        "max_cu_total": (
            decoded["gc_num_se"]
            * decoded["gc_num_sh_per_se"]
            * decoded["gc_num_cu_per_sh"]
        ),
        "grbm_instance_count": None,
        "rollback_scope": (
            "SE/SA dimensionados; INSTANCE_INDEX nao e descrito pelo gpu_info "
            "e continua bloqueando snapshot integral de registradores replicados"
        ),
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
