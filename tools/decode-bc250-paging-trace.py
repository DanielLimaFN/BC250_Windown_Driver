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
STAGE_NAMES = {
    1: "BUILD_ENTER", 2: "AFTER_DDI_MARK", 3: "AFTER_ENTRY_LOG",
    4: "AFTER_CAPS", 5: "BEFORE_DISPATCH", 6: "AFTER_DISPATCH",
    7: "FLUSH_HANDLER_ENTER", 8: "FLUSH_HANDLER_LEAVE",
    20: "UPDATE_ENTER", 21: "UPDATE_AFTER_HEADER_LOG",
    22: "UPDATE_AFTER_ENTRY_LOGS", 23: "UPDATE_BEFORE_HANDLER",
    24: "UPDATE_AFTER_HANDLER", 40: "FLUSH_OBJECT_LOCK_WAIT",
    41: "FLUSH_OBJECT_LOCK_HELD", 42: "FLUSH_GFXHUB_WAIT",
    43: "FLUSH_GFXHUB_DONE", 44: "FLUSH_JOURNAL_WAIT",
    45: "FLUSH_JOURNAL_DONE", 46: "FLUSH_OBJECT_LOCK_RELEASE",
}


# DX.87: registro de migalhas MMIO (espelho em RAM) gravado depois de cada
# registro de paginacao. Layout em bc250_mcdm_callbacks.c (BC250_CRUMB_TRACE_RECORD).
CRUMB_MAGIC = 0x31524342
CRUMB_SLOTS = 96
CRUMB_CPUS = 32
CRUMB_HEADER = struct.Struct("<IIIIIIQ")
CRUMB_SIZE = CRUMB_HEADER.size + CRUMB_SLOTS * 12 + 8
CRUMB_SITES = {
    1: "TRANSPORT_READ", 2: "TRANSPORT_WRITE", 3: "SDMA_READ", 4: "SDMA_WRITE",
    5: "OTG_READ", 6: "RINGWORK_READ", 7: "RINGWORK_WRITE", 8: "VRAM_SAMPLE",
    9: "VRAM_COMPARE", 10: "PAGING_DDI", 11: "SUBMIT_DDI",
}
CRUMB_BANKS = {0: "INTERNA", 1: "PAGINACAO", 2: "SUBMISSAO"}


def decode_crumb_record(data: bytes) -> dict:
    if len(data) != CRUMB_SIZE:
        raise ValueError("registro de migalha incompleto")
    magic, version, count, tick, last, _reserved, when = CRUMB_HEADER.unpack_from(data)
    checksum, padding = struct.unpack_from("<II", data, CRUMB_SIZE - 8)
    if magic != CRUMB_MAGIC or version != 1 or count != CRUMB_SLOTS or padding:
        raise ValueError("cabecalho de migalha invalido")
    if checksum != fnv1a(data[: CRUMB_SIZE - 8]):
        raise ValueError("checksum de migalha divergente")
    in_flight = []
    for slot in range(CRUMB_SLOTS):
        seq, site_offset, phase = struct.unpack_from("<III", data, CRUMB_HEADER.size + slot * 12)
        if seq and phase == 0xB0:
            in_flight.append({
                "sequence": seq,
                "cpu": slot % CRUMB_CPUS,
                "bank": CRUMB_BANKS.get(slot // CRUMB_CPUS, "?"),
                "site": CRUMB_SITES.get(site_offset >> 24, f"SITE_{site_offset >> 24}"),
                "offset": f"0x{site_offset & 0xFFFFFF:06X}",
            })
    in_flight.sort(key=lambda item: item["sequence"])
    return {"writer_tick": tick, "last_sequence": last,
            "snapshot_time_100ns": when, "in_flight": in_flight}


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
            slot = {
                "sequence": seq,
                "stage": stage,
                "stage_name": STAGE_NAMES.get(stage, "UNKNOWN"),
                "operation": operation,
                "status": f"0x{status & 0xFFFFFFFF:08X}",
                "thread": f"0x{thread:X}",
                "time_100ns": when,
            }
            # DX.85+: o estagio 44 carrega (M << 16) | N do commit do journal.
            # Binarios DX.83/DX.84 gravavam STATUS_PENDING (0x103) aqui.
            if stage == 44 and (status & 0xFFFFFFFF) != 0x103:
                slot["commit_changes_m"] = (status >> 16) & 0xFFFF
                slot["commit_live_ranges_n"] = status & 0xFFFF
            slots.append(slot)
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
    # DX.87: registros de migalhas. Um acesso em fase ANTES que aparece com a
    # mesma sequencia nos ultimos registros e' o candidato a acesso que parou.
    crumbs = []
    crumb_magic = struct.pack("<I", CRUMB_MAGIC)
    position = contents.find(crumb_magic)
    while position >= 0:
        try:
            crumbs.append(decode_crumb_record(contents[position : position + CRUMB_SIZE]))
            position = contents.find(crumb_magic, position + CRUMB_SIZE)
        except ValueError:
            position = contents.find(crumb_magic, position + 4)
    crumb_summary = None
    if crumbs:
        last_crumb = crumbs[-1]
        persistent = []
        for item in last_crumb["in_flight"]:
            streak = 0
            for record in reversed(crumbs):
                if any(o["sequence"] == item["sequence"] and o["cpu"] == item["cpu"]
                       for o in record["in_flight"]):
                    streak += 1
                else:
                    break
            persistent.append(dict(item, records_in_flight=streak))
        crumb_summary = {
            "crumb_records": len(crumbs),
            "last_writer_tick": last_crumb["writer_tick"],
            "last_sequence": last_crumb["last_sequence"],
            "in_flight_at_last_record": persistent,
        }
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
                "mmio_crumbs": crumb_summary,
                "last_snapshots": valid[-args.last :],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
