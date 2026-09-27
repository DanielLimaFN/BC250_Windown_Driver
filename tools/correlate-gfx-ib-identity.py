#!/usr/bin/env python3
"""Cruza IBs RADV DX.38 com fences KMD por VA, tamanho e FNV64 completo.

O resultado identifica bytes preparados e lidos antes do submit. Nao prova que
o CP buscou o IB ou que o shader executou. Colisao/repeticao => AMBIGUO.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

IDENTITY = re.compile(
    r"\[GFX_IB_IDENTITY\] Ctx=(?P<ctx>\S+) DxgFence=(?P<dxg>\d+) "
    r"RingFence=(?P<fence>\d+) VMID=(?P<vmid>\d+) Root=0x[0-9A-Fa-f]+ "
    r"VA=0x(?P<va>[0-9A-Fa-f]+) DW=(?P<dw>\d+) "
    r"Fnv64=(?P<hash>[0-9A-Fa-f]{16}) Complete=(?P<complete>[01])")
FILENAME = re.compile(
    r"^bc250_ib_pid(?P<pid>\d+)_t\d+_\d+_va(?P<va>[0-9a-fA-F]{16})_dw(?P<dw>\d+)\.bin$")


def fnv64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def fields(line: str) -> dict[str, str]:
    return dict(part.split("=", 1) for part in line.split() if "=" in part)


def correlate(evidence: Path) -> tuple[int, int, int, int, int]:
    log = evidence / "display-log-this-bind-FINAL.txt"
    dumps = evidence / "gfx-ib-dump"
    if not log.is_file() or not dumps.is_dir():
        raise ValueError("faltam display-log-this-bind-FINAL.txt ou gfx-ib-dump/")

    by_identity: dict[tuple[int, int, int], list[tuple[Path, dict[str, str]]]] = {}
    invalid = 0
    for path in sorted(dumps.glob("bc250_ib_pid*.bin")):
        match = FILENAME.fullmatch(path.name)
        if match is None:
            continue
        va, dw = int(match["va"], 16), int(match["dw"])
        data = path.read_bytes()
        if len(data) != dw * 4:
            print(f"DUMP_INCOMPLETO {path.name} bytes={len(data)} esperado={dw * 4}")
            invalid += 1
            continue
        meta_path = path.with_name(path.name + ".meta")
        if not meta_path.is_file():
            print(f"META_AUSENTE {path.name}")
            invalid += 1
            continue
        meta = fields(meta_path.read_text(encoding="utf-8", errors="replace"))
        actual_hash = fnv64(data)
        if (meta.get("event") != "RADV_SUBMIT" or
                meta.get("va", "").lower() != f"0x{va:x}" or
                meta.get("dw") != str(dw) or
                meta.get("fnv64", "").lower() != f"{actual_hash:016x}"):
            print(f"META_DIVERGENTE {path.name}")
            invalid += 1
            continue
        if meta.get("called") != "1" or meta.get("accepted") != "1":
            print(f"RADV_NAO_ACEITO {path.name} status={meta.get('status', '?')}")
            continue
        by_identity.setdefault((va, dw, actual_hash), []).append((path, meta))

    matched = ambiguous = missing = seen = 0
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = IDENTITY.search(line)
        if match is None:
            continue
        seen += 1
        label = (f"fence={match['fence']} dxg={match['dxg']} ctx={match['ctx']} "
                 f"vmid={match['vmid']} va=0x{match['va']} dw={match['dw']}")
        if match["complete"] != "1":
            print(f"INDETERMINADO {label} hash_KMD_incompleto")
            missing += 1
            continue
        key = (int(match["va"], 16), int(match["dw"]), int(match["hash"], 16))
        candidates = by_identity.get(key, [])
        if len(candidates) == 1:
            path, meta = candidates[0]
            print(f"MATCH_UNICO {label} dump={path.name} "
                  f"pid={meta.get('pid', '?')} radv_ctx={meta.get('ctx', '?')} "
                  f"ip={meta.get('ip', '?')} hwqueue={meta.get('hwqueue', '?')} "
                  f"gang={meta.get('gang', '?')}")
            matched += 1
        elif len(candidates) > 1:
            print(f"AMBIGUO {label} dumps={','.join(p.name for p, _ in candidates)}")
            ambiguous += 1
        else:
            print(f"AUSENTE {label} hash={match['hash']}")
            missing += 1
    print(f"TOTAL identidades={seen} unicos={matched} ambiguos={ambiguous} ausentes={missing} dumps_invalidos={invalid}")
    return seen, matched, ambiguous, missing, invalid


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path, help="pasta da corrida em docs/evidence/")
    args = parser.parse_args()
    try:
        seen, matched, ambiguous, missing, invalid = correlate(args.evidence)
    except ValueError as exc:
        parser.error(str(exc))
    if seen == 0:
        print("INCONCLUSIVO: nenhuma identidade DX.38 no log")
        return 2
    if ambiguous or missing or invalid:
        print("INCONCLUSIVO: identidades sem dump unico ou arquivo invalido")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
