#!/usr/bin/env python3
"""Detecta IB grafico CUMULATIVO num gfx-ib-dump de corrida (faixa DX.22, 22/09/2026).

MEDIDO na Corrida 67: todos os IBs sairam da MESMA VA (o CS do preambulo),
crescendo ~392 DW por submissao, e o IB n tinha prefixo byte a byte igual ao
IB n-1 -- a GPU reexecutava todos os draws anteriores a cada submissao.

Assinatura de cumulativo (as tres juntas, para nao confundir reuso legitimo de
um slot do pool com replay):
  1. dois despejos na MESMA VA;
  2. o menor, sem os 4 DW finais, e' prefixo exato do maior;
  3. o maior tem MAIS pacotes de draw que o menor.

Saida: 0 = nenhum cumulativo; 1 = cumulativo encontrado; 2 = sem despejos
(inconclusivo -- ausencia de arquivo nao prova nada).
"""

from __future__ import annotations

import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

NAME = re.compile(r"bc250_ib_(\d+)_va([0-9a-fA-F]+)_dw(\d+)\.bin$")
DRAW_OPCODES = {0x2D, 0x27, 0x35, 0x2E, 0x38}  # DRAW_INDEX_AUTO/2/OFFSET_2/IMMD/INDIRECT_MULTI


def draw_count(words: tuple[int, ...]) -> int:
    i, n = 0, 0
    while i < len(words):
        header = words[i]
        if header >> 30 == 3:
            if (header >> 8) & 0xFF in DRAW_OPCODES:
                n += 1
            i += ((header >> 16) & 0x3FFF) + 2
        else:
            i += 1
    return n


def main() -> int:
    if len(sys.argv) != 2:
        print("uso: check-gfx-ib-dump-cumulative.py <pasta gfx-ib-dump>")
        return 2
    folder = Path(sys.argv[1])
    by_va: dict[str, list[tuple[bytes, int, str]]] = defaultdict(list)
    for path in sorted(folder.glob("*.bin")):
        match = NAME.search(path.name)
        if not match:
            continue
        data = path.read_bytes()
        words = struct.unpack("<%dI" % (len(data) // 4), data[: len(data) // 4 * 4])
        by_va[match.group(2).lower()].append((data, draw_count(words), path.name))

    total = sum(len(v) for v in by_va.values())
    if total == 0:
        print(f"GFX IB DUMP: INCONCLUSIVO -- nenhum despejo em {folder}")
        return 2

    hits: list[str] = []
    for va, dumps in by_va.items():
        dumps.sort(key=lambda d: len(d[0]))
        for a in range(len(dumps)):
            small, small_draws, small_name = dumps[a]
            prefix = small[: max(0, len(small) - 16)]
            for b in range(a + 1, len(dumps)):
                big, big_draws, big_name = dumps[b]
                if len(big) > len(small) and big.startswith(prefix) and big_draws > small_draws:
                    hits.append(f"VA=0x{va}: {small_name} ({small_draws} draws) e' prefixo de "
                                f"{big_name} ({big_draws} draws)")
                    break

    print(f"GFX IB DUMP: {total} despejo(s) em {len(by_va)} VA(s) distinta(s)")
    if hits:
        print(f"GFX IB DUMP: CUMULATIVO -- {len(hits)} par(es) de replay de draws anteriores")
        for hit in hits[:10]:
            print(f"  - {hit}")
        return 1
    print("GFX IB DUMP: SEM CUMULATIVO -- nenhum IB contem os draws de uma submissao anterior")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
