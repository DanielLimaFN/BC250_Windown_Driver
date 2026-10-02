"""FPS.3 -- latencia de conclusao de IB (SDMA e GFX) e tempo do quadro do harness.

Uso:
  python tools/analyze-fps3-retire.py <log do KMD> [<log da UMD do harness>]

Mede, a partir das linhas com horario (qpc, unidade de 100 ns):
  SDMA  submit -> conclusao   KMD_SDMA_SUBMIT ... qpc=  ->  IB EXECUTADO ... retire_qpc=
  SDMA  conclusao -> proximo submit (tempo do VidMm, inclui nossas DDIs)
  GFX   submit -> conclusao   [GfxEngine] SUBMIT SUCESSO ... submit_qpc=  ->  [GfxRetire] UltimaFence ... retire_qpc=
  Harness: duracao de pfnPresentCb ([DX*_PRESENT_UMD_CB] ElapsedUs=) e tempo por quadro.

O histograma de 1 ms mostra se ainda existe o corte no tique de 15,6 ms do
relogio (Corrida 154: plano de 0 a 15 ms, queda em 16 ms). So' leitura.
"""
import collections
import re
import sys

QPC_PER_MS = 10000.0


def pct(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, int(len(v) * q))]


def describe(name, values):
    if not values:
        print(f"{name}: SEM DADOS")
        return
    print(f"{name}: n={len(values)} p10={pct(values, .1):.2f} p50={pct(values, .5):.2f} "
          f"p90={pct(values, .9):.2f} p99={pct(values, .99):.2f} max={max(values):.2f} ms")


def histogram(name, values, limit=24):
    h = collections.Counter(int(x) for x in values if x < limit)
    tail = sum(1 for x in values if x >= limit)
    print(f"{name} (1 ms por coluna, >= {limit} ms: {tail}):")
    print("  " + " ".join(f"{i}:{h.get(i, 0)}" for i in range(limit)))


def analyze_kmd(path):
    sdma_sub, gfx_sub = {}, {}
    sdma_lat, sdma_lat_dpc, sdma_lat_sub, sdma_next, gfx_lat = [], [], [], [], []
    sdma_done = {}
    hr_line = None
    re_ssub = re.compile(r"KMD_SDMA_SUBMIT ctx=\S+ node=0 dxg_fence=(\d+) .*qpc=(\d+)")
    re_sret = re.compile(r"IB EXECUTADO \(INDIRECT\).*FenceId=(\d+) .*retire_qpc=(\d+) FromDpc=(\d)")
    re_gsub = re.compile(r"\[GfxEngine\] SUBMIT SUCESSO: Fence=(\d+) .*submit_qpc=(\d+)")
    re_gret = re.compile(r"\[GfxRetire\] Retiradas=(\d+) UltimaFence=(\d+) retire_qpc=(\d+)")
    for line in open(path, encoding="utf-8", errors="replace"):
        if hr_line is None and "[FPS3B]" in line:
            hr_line = line.strip()
        m = re_ssub.search(line)
        if m:
            f, q = int(m.group(1)), int(m.group(2))
            sdma_sub[f] = q
            if f - 1 in sdma_done and q >= sdma_done[f - 1]:
                sdma_next.append((q - sdma_done[f - 1]) / QPC_PER_MS)
            continue
        m = re_sret.search(line)
        if m:
            f, q, dpc = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if f in sdma_sub and q >= sdma_sub[f]:
                d = (q - sdma_sub[f]) / QPC_PER_MS
                sdma_lat.append(d)
                (sdma_lat_dpc if dpc else sdma_lat_sub).append(d)
                sdma_done[f] = q
            continue
        m = re_gsub.search(line)
        if m:
            gfx_sub[int(m.group(1))] = int(m.group(2))
            continue
        m = re_gret.search(line)
        if m:
            count, last, q = int(m.group(1)), int(m.group(2)), int(m.group(3))
            # Cada fence retirada neste lote, do (last-count+1) ate last.
            for f in range(last - count + 1, last + 1):
                if f in gfx_sub and q >= gfx_sub[f]:
                    gfx_lat.append((q - gfx_sub.pop(f)) / QPC_PER_MS)
    print(hr_line or "[FPS3B] linha ausente (SYS anterior a FPS.3b ou log truncado)")
    describe("SDMA submit->conclusao          ", sdma_lat)
    describe("SDMA submit->conclusao (DPC)    ", sdma_lat_dpc)
    describe("SDMA submit->conclusao (submit) ", sdma_lat_sub)
    describe("SDMA conclusao->proximo submit  ", sdma_next)
    histogram("SDMA submit->conclusao", sdma_lat)
    describe("GFX  submit->conclusao          ", gfx_lat)
    histogram("GFX submit->conclusao", gfx_lat)


def analyze_umd(path):
    elapsed = [int(x) / 1000.0 for x in re.findall(
        r"_PRESENT_UMD_CB\] n=\d+ StartQpc=\d+ EndQpc=\d+ ElapsedUs=(\d+)",
        open(path, encoding="utf-8", errors="replace").read())]
    describe("Harness pfnPresentCb            ", elapsed)
    times = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"(\d+):(\d+):(\d+)\.(\d+) \[pid=\d+ [^]]+\] DXGI Present: hSurface", line)
        if m:
            times.append(int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]) + int(m[4]) / 1000.0)
    if len(times) > 1:
        per = (times[-1] - times[0]) * 1000.0 / (len(times) - 1)
        print(f"Harness: {len(times)} quadros, {per:.0f} ms por quadro, {1000.0 / per:.2f} FPS")


def frame_breakdown(kmd_path, umd_path):
    """Para cada pfnPresentCb do harness (StartQpc..EndQpc, mesmo relogio qpc
    do KMD): quantos IBs de paginacao do SDMA, quanto tempo dentro de
    FLUSH_TLB, quanto entre submit e conclusao, e quantos submits GFX. Diz
    para onde vai o tempo que o app passa esperando o kernel."""
    windows = [(int(a), int(b)) for a, b in re.findall(
        r"_PRESENT_UMD_CB\] n=\d+ StartQpc=(\d+) EndQpc=(\d+)",
        open(umd_path, encoding="utf-8", errors="replace").read())]
    if not windows:
        print("Quadro: SEM janelas de pfnPresentCb no log da UMD")
        return
    sdma_sub, sdma_ret, flush, gfx_sub = [], [], [], []
    enter = {}
    re_ssub = re.compile(r"KMD_SDMA_SUBMIT ctx=\S+ node=0 dxg_fence=(\d+) .*qpc=(\d+)")
    re_sret = re.compile(r"IB EXECUTADO \(INDIRECT\).*FenceId=(\d+) .*retire_qpc=(\d+)")
    re_ddi = re.compile(r"op=PAGING_DDI phase=(ENTER|LEAVE) id=(\d+) .*qpc=(\d+) operation=12")
    re_gsub = re.compile(r"\[GfxEngine\] SUBMIT SUCESSO: .*submit_qpc=(\d+)")
    sub_by_fence = {}
    for line in open(kmd_path, encoding="utf-8", errors="replace"):
        m = re_ssub.search(line)
        if m:
            sub_by_fence[int(m.group(1))] = int(m.group(2))
            sdma_sub.append(int(m.group(2)))
            continue
        m = re_sret.search(line)
        if m:
            f, q = int(m.group(1)), int(m.group(2))
            if f in sub_by_fence and q >= sub_by_fence[f]:
                sdma_ret.append((sub_by_fence[f], q))
            continue
        m = re_ddi.search(line)
        if m:
            if m.group(1) == "ENTER":
                enter[m.group(2)] = int(m.group(3))
            elif m.group(2) in enter:
                flush.append((enter.pop(m.group(2)), int(m.group(3))))
            continue
        m = re_gsub.search(line)
        if m:
            gfx_sub.append(int(m.group(1)))

    def overlap(a0, a1, b0, b1):
        return max(0, min(a1, b1) - max(a0, b0))

    rows = []
    for a, b in windows:
        n_ib = sum(1 for q in sdma_sub if a <= q <= b)
        wait = sum(overlap(s, r, a, b) for s, r in sdma_ret) / QPC_PER_MS
        fl = sum(overlap(s, e, a, b) for s, e in flush) / QPC_PER_MS
        n_gfx = sum(1 for q in gfx_sub if a <= q <= b)
        rows.append(((b - a) / QPC_PER_MS, n_ib, wait, fl, n_gfx))
    n = len(rows)
    avg = [sum(r[i] for r in rows) / n for i in range(5)]
    print(f"Quadro ({n} pfnPresentCb): {avg[0]:.1f} ms dentro de pfnPresentCb; "
          f"{avg[1]:.1f} IBs de paginacao; {avg[2]:.1f} ms esperando conclusao do SDMA; "
          f"{avg[3]:.1f} ms dentro de FLUSH_TLB; {avg[1] and (avg[0] / avg[1]) or 0:.1f} ms por IB; "
          f"{avg[4]:.1f} submits GFX")
    rest = avg[0] - avg[2] - avg[3]
    print(f"  resto (VidMm, outras DDIs, espera de GFX): {rest:.1f} ms por quadro")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    analyze_kmd(sys.argv[1])
    if len(sys.argv) > 2:
        analyze_umd(sys.argv[2])
        frame_breakdown(sys.argv[1], sys.argv[2])
    print("Somente leitura. NAO VALIDADO EM HARDWARE.")
