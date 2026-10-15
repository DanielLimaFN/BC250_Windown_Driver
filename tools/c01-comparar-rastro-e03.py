#!/usr/bin/env python3
"""C01 (doc 763/764): compara as escritas do amdgpu no bring-up da BC-250 (rastro E03 do Nagi) com o nosso fonte.

    python tools/c01-comparar-rastro-e03.py [--nagi referencias/amdgpu-wddm] [--saida docs/evidence/c01]

Entrada: referencias/amdgpu-wddm/evidence/linux/2026-09-21-E03-init-trace/amdgpu-events.txt (tracefs, kernel
6.18.52, 10.018 escritas). Os nomes vem dos headers do kernel pelo regcalc do Nagi (so leitura, nada copiado).

Para cada registrador que o amdgpu ESCREVE no init (fora DMU, PSP/SMU mailbox e as portas indice/dado), procura no
fonte do KMD (driver/core, driver/wdk) o nome do registrador ou o offset em bytes. Classifica:
  NUNCA     nem o nome nem o offset aparecem: o amdgpu escreve e nos nao (candidato a diferenca)
  CITADO    aparece; o script lista se o ultimo valor do amdgpu aparece literalmente perto
E um gabarito, nao uma prova: um registrador citado pode ser so lido, e um valor diferente pode ser derivado.
"""
import argparse
import csv
import re
import sys
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EVENT = re.compile(r"\s(\d+\.\d+): amdgpu_device_(wreg|rreg): 0x[0-9a-f]+, 0x([0-9a-f]+), 0x([0-9a-f]+)")
# Portas e blocos que nao sao configuracao de estado: indice/dado, mailbox de firmware, display (fora da P9 aqui).
SKIP_PREFIX = ("DMU.", "MP0.", "MP1.", "MP.")
SKIP_NAME = re.compile(r"(MM_INDEX|MM_DATA|_INDEX$|_DATA$|C2PMSG|SMN_|PCIE_INDEX|PCIE_DATA|RSMU_)")


def load_names(nagi):
    sys.path.insert(0, str(nagi / "tools" / "trace"))
    sys.path.insert(0, str(nagi / "tools" / "regcalc"))
    sys.path.insert(0, str(nagi / "tools" / "diagusb"))
    import summarize_init  # noqa: E402  (do Nagi, so leitura)
    return summarize_init.register_names()


def source_corpus():
    files = []
    # So o KMD de producao (driver/core, driver/wdk): o dbg/ de laboratorio e os testes nao contam como "fazemos".
    for top in ("driver/core", "driver/wdk"):
        for p in (ROOT / top).rglob("*"):
            if p.suffix.lower() in (".c", ".h", ".inl") and not any(x.startswith("output") for x in p.parts):
                files.append(p)
    text = {}
    for p in files:
        try:
            text[p] = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            pass
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nagi", default=str(ROOT / "referencias" / "amdgpu-wddm"))
    ap.add_argument("--saida", default=str(ROOT / "docs" / "evidence" / "c01-rastro-e03"))
    a = ap.parse_args()
    nagi = Path(a.nagi)
    trace = nagi / "evidence" / "linux" / "2026-09-21-E03-init-trace" / "amdgpu-events.txt"
    names = load_names(nagi)
    writes = OrderedDict()            # byte offset -> [valores na ordem]
    first_t = {}
    with trace.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            m = EVENT.search(line)
            if not m or m.group(2) != "wreg":
                continue
            off = int(m.group(3), 16) * 4
            writes.setdefault(off, []).append(int(m.group(4), 16))
            first_t.setdefault(off, float(m.group(1)))
    corpus = source_corpus()
    out = Path(a.saida)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for off, vals in writes.items():
        name = names.get(off, f"UNNAMED_0x{off:05X}")
        if name.startswith(SKIP_PREFIX) or SKIP_NAME.search(name):
            continue
        short = name.split(".", 1)[-1]
        pat_name = re.compile(r"\b(?:mm|BC250_[A-Z0-9_]*?)?" + re.escape(short) + r"\b")
        pat_off = re.compile(r"0x0*%X[uUlL]*\b" % off, re.IGNORECASE)
        hits, valhit = [], False
        last = vals[-1]
        pat_val = re.compile(r"0x0*%X[uUlL]*\b" % last, re.IGNORECASE) if last > 9 else None
        for p, t in corpus.items():
            if pat_name.search(t) or pat_off.search(t):
                hits.append(p.relative_to(ROOT).as_posix())
                if pat_val and pat_val.search(t):
                    valhit = True
        rows.append({
            "t_s": f"{first_t[off]:.6f}", "registrador": name, "offset": f"0x{off:05X}",
            "escritas": len(vals), "ultimo_valor": f"0x{last:08X}",
            "valores_distintos": " ".join(f"0x{v:08X}" for v in list(OrderedDict.fromkeys(vals))[:6]),
            "nosso": "CITADO" if hits else "NUNCA",
            "valor_no_nosso_fonte": "sim" if valhit else ("-" if not hits else "nao"),
            "arquivos": " ".join(sorted(set(hits))[:4]),
        })
    with (out / "c01-comparacao.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    # Segunda visao: o estado que o amdgpu DEIXA. Registrador cujo valor depois do init difere do valor do POST
    # (sweep antes do driver) e que o nosso KMD nunca cita = estado que o Linux muda e nos herdamos do firmware.
    base = trace.parent

    def sweep(name):
        vals = {}
        p = base / name
        if not p.exists():
            return vals
        for line in p.open(encoding="utf-8", errors="replace"):
            parts = line.split()
            if len(parts) == 3 and re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
                vals[parts[0]] = (parts[1], int(parts[2], 16))
        return vals
    before = sweep("sweep-before-run1-GC-complete-then-hang.log")
    before.update(sweep("sweep-before-run2-nonGC.log"))
    after = sweep("sweep-after-init.log")
    changed = []
    for reg, (off, val) in after.items():
        if reg not in before or before[reg][1] == val or reg.startswith(SKIP_PREFIX) or SKIP_NAME.search(reg):
            continue
        short = reg.split(".", 1)[-1]
        offv = int(off, 16)
        pat_name = re.compile(r"\b(?:mm|BC250_[A-Z0-9_]*?)?" + re.escape(short) + r"\b")
        pat_off = re.compile(r"0x0*%X[uUlL]*\b" % offv, re.IGNORECASE)
        cited = any(pat_name.search(t) or pat_off.search(t) for t in corpus.values())
        changed.append({"registrador": reg, "offset": off, "post": f"0x{before[reg][1]:08X}",
                        "depois_do_amdgpu": f"0x{val:08X}", "nosso": "CITADO" if cited else "NUNCA",
                        "escrito_no_rastro": "sim" if offv in writes else "nao"})
    if changed:
        with (out / "c01-estado-mudado-pelo-amdgpu.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(changed[0].keys()))
            w.writeheader()
            w.writerows(changed)
    print(f"registradores que o amdgpu deixa diferentes do POST: {len(changed)}; "
          f"nunca citados no KMD: {sum(1 for c in changed if c['nosso'] == 'NUNCA')}")
    nunca = [r for r in rows if r["nosso"] == "NUNCA"]
    print(f"registradores escritos pelo amdgpu (filtrados): {len(rows)}; nunca citados no nosso fonte: {len(nunca)}")
    blocks = OrderedDict()
    for r in nunca:
        blocks.setdefault(r["registrador"].split(".")[0], []).append(r)
    for b, rs in blocks.items():
        print(f"  {b}: {len(rs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
