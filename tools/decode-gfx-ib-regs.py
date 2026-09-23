#!/usr/bin/env python3
"""Decodifica os registradores escritos num despejo de IB grafico (gfx-ib-dump).

Faixa DX.26 / Fase A do docs/337 (22/09/2026). Usa as bases de registradores do
PROPRIO Mesa do fork (src/amd/registers/gfx10.json e gfx103.json) para:
  1. listar cada SET_CONTEXT_REG / SET_SH_REG / SET_UCONFIG_REG com nome e valor;
  2. marcar registradores que so' existem no GFX10.3 (RDNA2) -- se o RADV os
     escreve num GFX10.1, ele acha que o chip e' outro;
  3. imprimir o estado de contexto efetivo antes de cada draw.

Somente leitura. Uso:
  python tools/decode-gfx-ib-regs.py <arquivo.bin> [--mesa-root DIR] [--estado]
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

DEFAULT_MESA = Path(r"C:\Users\Daniel\Desktop\bc250-win\bc250-win\mesa-radv-wddm")
CTX_BASE, SH_BASE, UCFG_BASE = 0x28000, 0x2C00 * 4, 0x30000
OPS = {0x69: ("CTX", CTX_BASE), 0x76: ("SH", SH_BASE), 0x79: ("UCFG", UCFG_BASE)}
DRAWS = {0x2D: "DRAW_INDEX_AUTO", 0x27: "DRAW_INDEX_2", 0x35: "DRAW_INDEX_OFFSET_2", 0x2E: "DRAW_INDEX_IMMD"}


def load_names(mesa: Path, fname: str) -> dict[int, str]:
    d = json.loads((mesa / "src/amd/registers" / fname).read_text(encoding="utf-8"))
    out: dict[int, str] = {}
    for r in d["register_mappings"]:
        m = r.get("map", {})
        if m.get("to") == "mm":
            out[m["at"]] = r["name"]
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ib", type=Path)
    ap.add_argument("--mesa-root", type=Path, default=DEFAULT_MESA)
    ap.add_argument("--estado", action="store_true", help="imprime o estado antes de cada draw")
    a = ap.parse_args()

    g10 = load_names(a.mesa_root, "gfx10.json")
    g103 = load_names(a.mesa_root, "gfx103.json")
    only103 = {k: v for k, v in g103.items() if k not in g10}

    data = a.ib.read_bytes()
    w = struct.unpack("<%dI" % (len(data) // 4), data[: len(data) // 4 * 4])
    state: dict[int, int] = {}
    hits103: dict[str, int] = {}
    unknown: dict[int, int] = {}
    i = 0
    ndraw = 0
    while i < len(w):
        h = w[i]
        if h >> 30 != 3:
            i += 1
            continue
        op = (h >> 8) & 0xFF
        n = ((h >> 16) & 0x3FFF) + 1
        if op in OPS and i + 1 < len(w):
            kind, base = OPS[op]
            start = (w[i + 1] & 0xFFFF) * 4 + base
            for k in range(n - 1):
                addr = start + 4 * k
                val = w[i + 2 + k] if i + 2 + k < len(w) else 0
                state[addr] = val
                if addr in only103:
                    hits103[only103[addr]] = val
                elif addr not in g10 and addr not in g103:
                    unknown[addr] = val
        elif op in DRAWS:
            ndraw += 1
            if a.estado:
                print(f"--- estado antes do {DRAWS[op]} #{ndraw} (dw {i}) ---")
                for addr in sorted(state):
                    name = g10.get(addr) or g103.get(addr) or "?"
                    tag = " [SO GFX10.3]" if addr in only103 else ""
                    print(f"  {addr:05X} {name:40s} = 0x{state[addr]:08X}{tag}")
        i += n + 1

    print(f"IB {a.ib.name}: {len(w)} DW, {ndraw} draw(s), {len(state)} registradores escritos")
    print(f"Registradores que SO existem no GFX10.3 e foram escritos: {len(hits103)}")
    for name, val in sorted(hits103.items()):
        print(f"  {name} = 0x{val:08X}")
    if unknown:
        print(f"Enderecos fora das duas bases: {len(unknown)} (ex.: " +
              ", ".join(f"{k:05X}" for k in list(unknown)[:6]) + ")")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
