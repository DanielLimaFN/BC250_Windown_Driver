#!/usr/bin/env python3
"""Bloqueia o build se os blobs CP divergirem do contrato somente-leitura."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

from fwinspect import inspect_file


EXPECTED = {
    "cyan_skillfish2_pfp.bin": {
        "firmware_type": 2,
        "ucode_version": 148,
        "payload_bytes": 263168,
        "sha256": "50e56dc1913571ff589425d2b059bddfc5e8a2b4bc22d3eb78cac75d1e0479a1",
        "payload_sha256": "5aa30f3b2e1c697ad2bfe02643777552d5351ec6813572271f01320f65ca7367",
        "macro": "BC250_CP_PFP_PAYLOAD_BYTES",
        "contract_payload_sha256": "5aa30f3b2e1c697ad2bfe02643777552d5351ec6813572271f01320f65ca7367",
        "sha_macro": "BC250_CP_PFP_PAYLOAD_SHA256_HEX",
        "sha_array": "g_Bc250PfpSha256",
    },
    "cyan_skillfish2_me.bin": {
        "firmware_type": 1,
        "ucode_version": 99,
        "payload_bytes": 263168,
        "sha256": "d4ed4d968de0d7720f7c8215d42fdae727bf99cdf43cee6f8f864dc2584b17e2",
        "payload_sha256": "47eb26bb1a22a4fcd6d3350012a52a7eb9192278e810b670a39651a1d71ce030",
        "macro": "BC250_CP_ME_PAYLOAD_BYTES",
        "contract_payload_sha256": "47eb26bb1a22a4fcd6d3350012a52a7eb9192278e810b670a39651a1d71ce030",
        "sha_macro": "BC250_CP_ME_PAYLOAD_SHA256_HEX",
        "sha_array": "g_Bc250MeSha256",
    },
    "cyan_skillfish2_ce.bin": {
        "firmware_type": 3,
        "ucode_version": 37,
        "payload_bytes": 263040,
        "sha256": "5946efbf7e46ccfe7e9f965da56c35b1187469d1c1a49c8a9eff433eb3ab4723",
        "payload_sha256": "faeb5f1356057e3c16f7d07f5f30783bdc4e17394570a1497a97458517130057",
        "macro": "BC250_CP_CE_PAYLOAD_BYTES",
        "contract_payload_sha256": "faeb5f1356057e3c16f7d07f5f30783bdc4e17394570a1497a97458517130057",
        "sha_macro": "BC250_CP_CE_PAYLOAD_SHA256_HEX",
        "sha_array": "g_Bc250CeSha256",
    },
    "cyan_skillfish2_mec.bin": {
        "firmware_type": 4,
        "ucode_version": 144,
        "payload_bytes": 268336,
        "sha256": "b1c1f843a8faaa2de537d6ba05052a3776b5a1b73f8a3c3f5ed9d568489776a4",
        "payload_sha256": "3e1fdf268a6580fe755f4a8a506181a670da71c28e599a77318e5b2d65e597a2",
        "macro": "BC250_CP_MEC_PAYLOAD_BYTES",
        "contract_payload_sha256": "76889d227da33204002d1c7dee5f77d81e51380847d113e56cdcaccc48851f48",
        "sha_macro": "BC250_CP_MEC_PAYLOAD_SHA256_HEX",
        "sha_array": "g_Bc250MecSha256",
    },
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    firmware_dir = root / "firmware"
    saved = json.loads((firmware_dir / "manifest.json").read_text(encoding="utf-8"))
    saved_by_name = {entry["name"]: entry for entry in saved["firmware"]}
    header = (root / "driver" / "wdk" / "bc250_cp_firmware_manifest.h").read_text(
        encoding="utf-8"
    )
    source = (root / "driver" / "wdk" / "bc250_cp_firmware_manifest.c").read_text(
        encoding="utf-8"
    )

    for name, expected in EXPECTED.items():
        actual = inspect_file(firmware_dir / name)
        if not actual["structurally_valid"]:
            fail(f"{name}: estrutura invalida: {actual['issues']}")
        for field in ("sha256", "payload_sha256"):
            if actual[field] != expected[field]:
                fail(f"{name}: {field} divergiu")
        if actual["header"]["ucode_version"] != expected["ucode_version"]:
            fail(f"{name}: ucode_version divergiu")
        if actual["psp_fw_type"] != expected["firmware_type"]:
            fail(f"{name}: tipo PSP divergiu")
        if actual["header"]["ucode_size_bytes"] != expected["payload_bytes"]:
            fail(f"{name}: payload divergiu")
        if saved_by_name.get(name) != actual:
            fail(f"{name}: firmware/manifest.json esta desatualizado")

        contract_bytes = expected["payload_bytes"]
        if name == "cyan_skillfish2_mec.bin":
            gfx = actual["gfx_header"]
            if (
                gfx["jt_offset_dwords"] != 0x1052C
                or gfx["jt_size_dwords"] != 0xE0
                or gfx["mec_main_payload_bytes"] != 267440
                or gfx["mec_jt_payload_bytes"] != 896
            ):
                fail("MEC/JT: faixa derivada divergiu")
            contract_bytes = gfx["mec_main_payload_bytes"]
            main = (firmware_dir / name).read_bytes()[
                actual["header"]["ucode_offset_bytes"] :
                actual["header"]["ucode_offset_bytes"] + contract_bytes
            ]
            if hashlib.sha256(main).hexdigest() != expected["contract_payload_sha256"]:
                fail("MEC principal: SHA-256 derivado divergiu")

        pattern = rf"#define\s+{expected['macro']}\s+{contract_bytes}u\b"
        if re.search(pattern, header) is None:
            fail(f"{expected['macro']}: contrato C divergiu")
        sha_pattern = (
            rf'#define\s+{expected["sha_macro"]}\s+\\\s*\n\s*'
            rf'"{expected["contract_payload_sha256"]}"'
        )
        if re.search(sha_pattern, header) is None:
            fail(f"{expected['sha_macro']}: contrato SHA-256 divergiu")
        array_match = re.search(
            rf"{expected['sha_array']}\s*\[32\]\s*=\s*\{{([^}}]+)\}}",
            source,
            re.DOTALL,
        )
        if array_match is None:
            fail(f"{expected['sha_array']}: array C ausente")
        array_bytes = bytes(
            int(value, 16)
            for value in re.findall(r"0x([0-9A-Fa-f]{2})", array_match.group(1))
        )
        if array_bytes.hex() != expected["contract_payload_sha256"]:
            fail(f"{expected['sha_array']}: array C divergiu")

    if re.search(r"#define\s+BC250_CP_MEC_JT1_PAYLOAD_BYTES\s+896u\b", header) is None:
        fail("BC250_CP_MEC_JT1_PAYLOAD_BYTES: contrato C divergiu")
    mec = inspect_file(firmware_dir / "cyan_skillfish2_mec.bin")
    mec_bytes = (firmware_dir / "cyan_skillfish2_mec.bin").read_bytes()
    payload_offset = mec["header"]["ucode_offset_bytes"]
    jt_offset = mec["gfx_header"]["mec_main_payload_bytes"]
    jt_bytes = mec["gfx_header"]["mec_jt_payload_bytes"]
    jt_sha256 = hashlib.sha256(
        mec_bytes[payload_offset + jt_offset : payload_offset + jt_offset + jt_bytes]
    ).hexdigest()
    expected_jt_sha256 = "f90ef91243032015617b927714a071b17c112b4492fda40f1f1a0e67ad89ebd3"
    if jt_sha256 != expected_jt_sha256:
        fail("MEC_JT1: SHA-256 derivado divergiu")
    jt_pattern = (
        r'#define\s+BC250_CP_MEC_JT1_PAYLOAD_SHA256_HEX\s+\\\s*\n\s*'
        rf'"{expected_jt_sha256}"'
    )
    if re.search(jt_pattern, header) is None:
        fail("BC250_CP_MEC_JT1_PAYLOAD_SHA256_HEX: contrato divergiu")
    jt_array_match = re.search(
        r"g_Bc250MecJt1Sha256\s*\[32\]\s*=\s*\{([^}]+)\}",
        source,
        re.DOTALL,
    )
    if jt_array_match is None:
        fail("g_Bc250MecJt1Sha256: array C ausente")
    jt_array = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9A-Fa-f]{2})", jt_array_match.group(1))
    )
    if jt_array.hex() != expected_jt_sha256:
        fail("g_Bc250MecJt1Sha256: array C divergiu")

    print("CP manifest: OK (PFP/ME/CE + MEC principal/JT1 derivados)")
    print("mec2.bin nao e fonte de MEC_JT1")
    print("Zero escrita de firmware; zero chamada PSP.")
    print("Veredito: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"CP manifest: FALHA: {error}", file=sys.stderr)
        raise SystemExit(1)
