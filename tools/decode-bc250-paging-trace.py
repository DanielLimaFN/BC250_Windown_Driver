"""Decode the DX.56 durable KMD paging-stage trace after a reboot.

Usage: python tools/decode-bc250-paging-trace.py C:\\BC250_PAGING_TRACE.bin
Records are append-only; the last incomplete or torn record is reported, not used.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


HEADER = struct.Struct("<IIIIIIQ")
SLOT = struct.Struct("<IIIiQQ")
TRAILER = struct.Struct("<II")
RECORD_SIZE = HEADER.size + 16 * SLOT.size + TRAILER.size
MAGIC = 0x36585042


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def decode_record(data: bytes) -> dict:
    if len(data) != RECORD_SIZE:
        raise ValueError("registro incompleto")
    magic, version, count, active, latest, reserved, timestamp = HEADER.unpack_from(data)
    checksum, padding = TRAILER.unpack_from(data, RECORD_SIZE - TRAILER.size)
    # v1: Reserved=0. v2 (DX.65): Reserved = batimento do escritor, gravado a
    # cada periodo mesmo sem paginacao nova.
    if magic != MAGIC or version not in (1, 2) or count != 16 or padding or \
            (version == 1 and reserved):
        raise ValueError("cabecalho ou layout invalido")
    if checksum != fnv1a(data[: RECORD_SIZE - TRAILER.size]):
        raise ValueError("checksum divergente")
    slots = []
    for index in range(count):
        seq, stage, operation, status, thread, when = SLOT.unpack_from(
            data, HEADER.size + index * SLOT.size
        )
        if seq:
            slots.append(
                {
                    "sequence": seq,
                    "stage": stage,
                    "operation": operation,
                    "status": f"0x{status & 0xFFFFFFFF:08X}",
                    "thread": f"0x{thread:X}",
                    "time_100ns": when,
                }
            )
    return {
        "version": version,
        "writer_tick": reserved if version == 2 else None,
        "latest_sequence": latest,
        "active_count": active,
        "snapshot_time_100ns": timestamp,
        "slots": slots,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--last", type=int, default=5, help="valid snapshots to print")
    args = parser.parse_args()
    valid = []
    invalid = []
    contents = args.path.read_bytes()
    magic = struct.pack("<I", MAGIC)
    offset = 0
    while offset < len(contents):
        candidate = contents.find(magic, offset)
        if candidate < 0:
            if offset < len(contents):
                invalid.append({"file_offset": offset, "reason": "bytes sem registro valido"})
            break
        if candidate > offset:
            invalid.append({"file_offset": offset, "reason": f"{candidate - offset} bytes ignorados"})
        data = contents[candidate : candidate + RECORD_SIZE]
        try:
            record = decode_record(data)
            record["file_offset"] = candidate
            valid.append(record)
            offset = candidate + RECORD_SIZE
        except ValueError as error:
            invalid.append({"file_offset": candidate, "reason": str(error)})
            offset = candidate + len(magic)
    # DX.65: batimento depois da ultima paginacao. So' registros v2 com a
    # MESMA latest_sequence no fim, em sequencia de writer_tick, contam.
    heartbeat = None
    boot_valid = [r for r in valid if r["latest_sequence"] != 0]
    if boot_valid and boot_valid[-1]["version"] == 2:
        last = boot_valid[-1]
        tail = [last]
        for record in reversed(boot_valid[:-1]):
            if record["version"] != 2 or record["latest_sequence"] != last["latest_sequence"]:
                break
            tail.append(record)
        tail.reverse()
        heartbeat = {
            "latest_sequence": last["latest_sequence"],
            "records_after_last_paging": len(tail) - 1,
            "writer_alive_ms_after_last_paging":
                (tail[-1]["snapshot_time_100ns"] - tail[0]["snapshot_time_100ns"]) / 10000.0,
            "last_writer_tick": last["writer_tick"],
        }
    print(
        json.dumps(
            {
                "path": str(args.path),
                "record_size": RECORD_SIZE,
                "valid_records": len(valid),
                "invalid_records": invalid[-5:],
                "heartbeat_after_last_paging": heartbeat,
                "last_snapshots": valid[-args.last :],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
