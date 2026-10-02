#!/usr/bin/env python3
"""Analise padrao de uma corrida do BC-250 (usada nas Corridas 166-172, docs 519-525).

Uso:
    python tools/analyze-bc250-freeze-run.py                      # pasta *-lab-binding mais recente
    python tools/analyze-bc250-freeze-run.py docs/evidence/<pasta>

O que faz (somente leitura sobre o hardware; escreve so' na pasta de evidencia):
  1. le start_offset de display-log-baseline.json e recupera C:\\BC250_DISPLAY_LOG.txt
     daquele offset ate o fim em display-log-this-bind-RECUPERADO.txt (com SHA-256);
  2. separa a corrida do boot seguinte pela SEGUNDA linha 'AddDevice ENTER';
  3. mede a duracao pelo QPC (10 MHz) da corrida;
  4. conta as assinaturas usadas nos criterios (tripe, timeout, fault, TDR, Present,
     linhas das faixas HDP.1/T3.1/X1/X2a/X3/X4);
  5. imprime, do boot seguinte, o reporte das migalhas DX.66 (ordenado por seq) e o
     anel de eventos da GPU EVT.1 ([EVT1_GPU] e [EVT1_EM_VOO]).

ATENCAO AO LER AS MIGALHAS: ANTES=0xB0 e DEPOIS=0xA0 diferem num unico bit e a VRAM que
decai na trava vira bits 0->1; um DEPOIS degradado aparece como ANTES (doc 525 s6).
Slot ANTES com seq muito antiga e' quase certamente decaimento.
"""
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIVE_LOG = pathlib.Path(r"C:\BC250_DISPLAY_LOG.txt")

TAGS = [
    "IB EXECUTADO", "TRIPE_PROVADO", "IB_ENGINE_2LEGS", "IB_TRIPOD TIMEOUT",
    "Blt Timeout", "GFXHUB_FAULT", "TDR_QUIESCE", "DEVICE_REMOVED", "StopDevice",
    "[Present #", "BYTE_PROOF PASS", "[DX91_PAGING_MOVE] op=FILL",
    "[DX91_PAGING_MOVE] op=TRANSFER", "HDP1_FLUSH] DIVERGENCIA", "HDP_PT_FLUSH] FALHA",
    "SOBREPOE", "[X1_POLL]", "FenceLida=0", "[X3_CANARY]", "[X4_CANARY]",
    "SDMA encode failed",
]


def pick_folder(argv):
    if len(argv) > 1:
        return pathlib.Path(argv[1])
    folders = sorted((ROOT / "docs" / "evidence").glob("*-lab-binding"))
    if not folders:
        sys.exit("nenhuma pasta *-lab-binding em docs/evidence")
    return folders[-1]


def main(argv):
    folder = pick_folder(argv)
    base = json.loads((folder / "display-log-baseline.json").read_text(encoding="utf-8-sig"))
    offset = int(base["start_offset"])
    with open(LIVE_LOG, "rb") as f:
        f.seek(offset)
        data = f.read()
    out = folder / "display-log-this-bind-RECUPERADO.txt"
    out.write_bytes(data)
    print("pasta:", folder.name)
    print("recuperado: %d bytes de offset %d, SHA-256 %s" % (
        len(data), offset, hashlib.sha256(data).hexdigest().upper()))

    text = data.decode("latin1")
    adds = [m.start() for m in re.finditer("AddDevice ENTER", text)]
    if len(adds) < 2:
        print("AVISO: sem segunda 'AddDevice ENTER' -- corrida limpa ou boot seguinte ainda nao aconteceu")
        run, nxt = text, ""
    else:
        run, nxt = text[:adds[1]], text[adds[1]:]
    qpcs = [int(x) for x in re.findall(r"qpc=(\d+)", run)]
    if qpcs:
        print("duracao pelo QPC: %.1f s" % ((max(qpcs) - min(qpcs)) / 1e7))
    for tag in TAGS:
        print("  %-34s %d" % (tag, run.count(tag)))

    print("--- migalhas DX.66 do boot seguinte (seq decrescente)")
    rows = [l for l in nxt.splitlines() if "[DX66_MIGALHA]" in l]
    head = [l for l in rows if "CPU" not in l[:20]]
    for l in head:
        print("  " + l[:220])
    cpu = [l for l in rows if "] CPU" in l]
    last = 0
    for l in head:
        m = re.search(r"ultimaSeq=(\d+)", l)
        if m:
            last = int(m.group(1))

    def seq(line):
        m = re.search(r"seq=(\d+)", line)
        return int(m.group(1)) if m else 0
    # seq maior que a ultima da maquina so' pode ser bit decaido na VRAM.
    sane = [l for l in cpu if last == 0 or seq(l) <= last]
    decayed = len(cpu) - len(sane)
    for l in sorted(sane, key=lambda l: -seq(l))[:16]:
        print("  " + l[15:220])
    if decayed:
        print("  (%d slots com seq > ultimaSeq omitidos: decaimento)" % decayed)

    print("--- anel de eventos da GPU (EVT.1)")
    for l in nxt.splitlines():
        if "[EVT1_EM_VOO]" in l or "anel de eventos" in l or "LEITURA RECUSADA" in l:
            print("  " + l[:240])
    for l in [l for l in nxt.splitlines() if "[EVT1_GPU] seq=" in l][:16]:
        print("  " + l[11:220])


if __name__ == "__main__":
    main(sys.argv)
