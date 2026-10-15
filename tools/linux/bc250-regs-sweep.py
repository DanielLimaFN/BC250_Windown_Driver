#!/usr/bin/env python3
"""bc250-regs-sweep.py -- leitura (so' leitura) dos registradores da lista
bc250-l1b-regs.txt pelo debugfs amdgpu_regs (doc 663).

Modos:
  once     --saida ARQ               uma foto (TSV: bloco nome offset valor)
  periodic --saida ARQ --intervalo S fotos periodicas (JSON por linha, fsync)
  otg      --saida ARQ --segundos S  mede o refresh pelo contador de quadros do OTG0
"""
import argparse
import glob
import json
import os
import struct
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
OTG0_FRAME = 0x14030  # bc250_dcn_probe_contract.h


def achar_regs():
    for d in sorted(glob.glob("/sys/kernel/debug/dri/*")):
        try:
            if "amdgpu" in open(os.path.join(d, "name")).read():
                p = os.path.join(d, "amdgpu_regs")
                if os.path.exists(p):
                    return p
        except OSError:
            pass
    return None


def ler_lista(caminho):
    regs = []
    for linha in open(caminho, encoding="utf-8"):
        if linha.startswith("#") or not linha.strip():
            continue
        bloco, nome, off = linha.rstrip("\n").split("\t")[:3]
        regs.append((bloco, nome, int(off, 16)))
    return regs


def ler(fd, off):
    try:
        b = os.pread(fd, 4, off)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None
    except OSError:
        return None


def foto(fd, regs):
    return {f"{b}.{n}": (None if (v := ler(fd, o)) is None else f"0x{v:08X}") for b, n, o in regs}


def gravar(arq, texto):
    arq.write(texto)
    arq.flush()
    os.fsync(arq.fileno())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("modo", choices=["once", "periodic", "otg"])
    ap.add_argument("--saida", required=True)
    ap.add_argument("--lista", default=os.path.join(AQUI, "bc250-l1b-regs.txt"))
    ap.add_argument("--intervalo", type=float, default=10.0)
    ap.add_argument("--segundos", type=float, default=30.0)
    a = ap.parse_args()

    caminho = achar_regs()
    if caminho is None:
        print("ERRO: amdgpu_regs nao encontrado (amdgpu carregado? debugfs montado?)", file=sys.stderr)
        return 1
    fd = os.open(caminho, os.O_RDONLY)

    if a.modo == "otg":
        t0, f0 = time.monotonic(), ler(fd, OTG0_FRAME)
        time.sleep(a.segundos)
        t1, f1 = time.monotonic(), ler(fd, OTG0_FRAME)
        hz = None if f0 is None or f1 is None else ((f1 - f0) & 0xFFFFFFFF) / (t1 - t0)
        with open(a.saida, "w") as s:
            gravar(s, json.dumps({"inicio_quadro": f0, "fim_quadro": f1, "segundos": round(t1 - t0, 3),
                                  "hz": None if hz is None else round(hz, 3)}) + "\n")
        print(f"refresh medido: {hz} Hz em {t1 - t0:.2f} s")
        return 0

    regs = ler_lista(a.lista)
    if a.modo == "once":
        with open(a.saida, "w") as s:
            linhas = [f"{b}\t{n}\t0x{o:06X}\t" + ("FALHOU" if (v := ler(fd, o)) is None else f"0x{v:08X}")
                      for b, n, o in regs]
            gravar(s, "\n".join(linhas) + "\n")
        print(f"{len(regs)} registradores em {a.saida}")
        return 0

    with open(a.saida, "a") as s:
        while True:
            gravar(s, json.dumps({"t": round(time.time(), 3), "r": foto(fd, regs)}) + "\n")
            time.sleep(a.intervalo)


if __name__ == "__main__":
    sys.exit(main())
