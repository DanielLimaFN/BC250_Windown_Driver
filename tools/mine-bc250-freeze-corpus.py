#!/usr/bin/env python3
"""Mineracao do corpus de corridas para a trava dura da BC-250 (somente leitura).

Uso:
    python tools/mine-bc250-freeze-corpus.py [--glob "2026-09-2[78]_*lab-binding"] [--tags]

Para cada pasta de evidencia, pega o maior log de KMD disponivel
(RECUPERADO > tail-recovered > FINAL > this-bind), corta no reboot (a segunda
linha "AddDevice ENTER" e o boot seguinte) e imprime:
  - TRAVA/limpa (trava = houve reboot dentro do trecho) e a duracao pelo QPC;
  - quantos FLUSH_TLB (PAGING_DDI operation=12) em cada janela de tempo;
  - se as migalhas DX.66 do boot seguinte sobreviveram.
Com --tags, compara os tipos de linha nas 400 linhas antes de cada trava com
janelas das corridas limpas NO MESMO INTERVALO de tempo (40..180 s). Comparar
com janelas da segunda metade das limpas da' falso positivo: as classes
amostradas do log (ex.: [UpdatePT ...]) so' aparecem no comeco de qualquer
corrida (doc 518 secao 6).

QPC do log = 10 MHz (QueryPerformanceFrequency desta maquina).
Documentado em PLANO_DE_TRABALHO_TRAVA_E_DESKTOP.md. NAO VALIDA NADA EM HARDWARE.
"""
import argparse
import glob
import os
import re
from collections import Counter

EVIDENCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "evidence")
CANDIDATES = ("display-log-this-bind-RECUPERADO.txt", "bc250_display_log-tail-recovered.txt",
              "display-log-this-bind-FINAL.txt", "display-log-this-bind.txt")
RQ = re.compile(rb"qpc=(\d+)")
RFLUSH = re.compile(rb"op=PAGING_DDI phase=ENTER .*qpc=(\d+) operation=12")
QPC_HZ = 1e7


def load(folder):
    present = [f for f in CANDIDATES if os.path.exists(os.path.join(folder, f))]
    if not present:
        return None
    best = max(present, key=lambda f: os.path.getsize(os.path.join(folder, f)))
    data = open(os.path.join(folder, best), "rb").read()
    lines = data.split(b"\n")
    adds = [i for i, l in enumerate(lines) if b"AddDevice ENTER" in l]
    froze = len(adds) > 1
    body = lines[:adds[1] - 1] if froze else lines  # -1: linha START do rastro do boot seguinte
    return best, body, froze, (b"ANTERIOR PRESENTE" in data)


def timeline(body):
    t0, t, out = None, 0.0, []
    for l in body:
        m = RQ.search(l)
        if m:
            q = int(m.group(1))
            t0 = t0 or q
            t = (q - t0) / QPC_HZ
        out.append(t)
    return out


def tag(line):
    m = re.match(rb"\s*(\[[^\]]{2,40}\]|BC250_\w+ v=1 op=\w+|BC250_DIAG .*?event=\w+)", line)
    return re.sub(rb"\d+", b"#", m.group(1)).decode("latin1") if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glob", default="2026-09-2[78]_*lab-binding")
    ap.add_argument("--tags", action="store_true")
    args = ap.parse_args()
    windows = ((0, 60), (60, 120), (120, 200), (200, 400), (400, 10**9))
    fro, cln = Counter(), Counter()
    nf = nc = 0
    for folder in sorted(glob.glob(os.path.join(EVIDENCE, args.glob))):
        got = load(folder)
        if not got:
            continue
        name, body, froze, crumbs = got
        ts = timeline(body)
        flush = [int(m.group(1)) for l in body for m in [RFLUSH.search(l)] if m]
        f0 = flush[0] if flush else 0
        per = [sum(1 for x in flush if lo <= (x - f0) / QPC_HZ < hi) for lo, hi in windows]
        print(f"{os.path.basename(folder)[:19]} {'TRAVA' if froze else 'limpa'} "
              f"dur={ts[-1] if ts else 0:6.1f}s FLUSH_TLB[0-60,60-120,120-200,200-400,400+]={per} "
              f"{'migalhas' if crumbs else ''} ({name})")
        if not args.tags:
            continue
        if froze:
            fro.update({tag(l) for l in body[-400:]} - {None})
            nf += 1
        else:
            for tt in (40, 60, 80, 100, 120, 150, 180):
                i = next((k for k, x in enumerate(ts) if x >= tt), None)
                if i is None or i < 400:
                    continue
                cln.update({tag(l) for l in body[i - 400:i]} - {None})
                nc += 1
    if args.tags and nf and nc:
        print(f"\ntravas={nf} janelas limpas (mesmo intervalo)={nc}  fracao_trava fracao_limpa tipo")
        rows = sorted(((fro[t] / nf, cln[t] / nc, t) for t in set(fro) | set(cln)),
                      key=lambda r: -(r[0] - r[1]))
        for a, b, t in rows[:15]:
            print(f"{a:5.2f} {b:5.2f}  {t}")


if __name__ == "__main__":
    main()
