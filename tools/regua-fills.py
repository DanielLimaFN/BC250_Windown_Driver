#!/usr/bin/env python3
"""Regua de FILLs (doc 658 sec. 2.1, auditoria 653-TMR): contadores de paginacao
ate a trava ou ate o fim, em todas as corridas com rastro BPG1 (Corrida 207 em
diante), lidos do rastro recuperado de cada pasta de evidencia.

Uso: python tools/regua-fills.py [--csv saida.csv]

So' leitura. Fonte: paging-trace-this-bind-RECUPERADO.bin (ou -this-bind.bin),
decodificado com as mesmas funcoes de tools/decode-bc250-paging-trace.py.
Os contadores sao CUMULATIVOS desde o StartDevice; usa-se o maior valor visto.
"""
import argparse
import csv
import importlib.util
import pathlib
import sys

AQUI = pathlib.Path(__file__).resolve().parent
EVID = AQUI.parent / "docs" / "evidence"

spec = importlib.util.spec_from_file_location("dec", AQUI / "decode-bc250-paging-trace.py")
dec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dec)


def regua(pasta: pathlib.Path):
    for nome in ("paging-trace-this-bind-RECUPERADO.bin", "paging-trace-this-bind.bin"):
        f = pasta / nome
        if f.exists() and f.stat().st_size > 0:
            break
    else:
        return None
    recs = dec.scan_words(f.read_bytes(), dec.PG_MAGIC, dec.PG_FIELDS)
    linhas = []
    for r in recs:
        if r.get("phase") != "DEPOIS":
            continue
        try:
            linhas.append({k: int(r[k], 16) for k in dec.PG_FIELDS if k in r} | {"tick": r["writer_tick"]})
        except (KeyError, ValueError):
            continue
    if not linhas:
        return {"pasta": pasta.name, "registros": 0}
    return {
        "pasta": pasta.name,
        "registros": len(linhas),
        "fills": max(l.get("fill_count", 0) for l in linhas),
        "evicts": max(l.get("evict_count", 0) for l in linhas),
        "flush_tlb": max(l.get("flush_tlb_count", 0) for l in linhas),
        "ultimo_tick": max(l["tick"] for l in linhas),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv")
    a = ap.parse_args()
    linhas = []
    for pasta in sorted(EVID.glob("*-lab-binding")):
        r = regua(pasta)
        if r and r.get("registros"):
            linhas.append(r)
    campos = ["pasta", "registros", "fills", "evicts", "flush_tlb", "ultimo_tick"]
    w = csv.DictWriter(open(a.csv, "w", newline="") if a.csv else sys.stdout, fieldnames=campos)
    w.writeheader()
    for r in linhas:
        w.writerow(r)


if __name__ == "__main__":
    main()
