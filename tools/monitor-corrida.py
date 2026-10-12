"""Monitor da corrida (doc 658): painel a cada 2 s, sem tocar a placa.

Fontes, todas somente leitura:
  - Windows: CPU (uso e clock medio), RAM, memoria de video do adaptador (PDH).
  - KMD: C:\\BC250_DISPLAY_LOG.txt, so' os bytes novos desde a ultima volta
    (IBs, Presents, TDR, faults, as linhas do TMR.1 e o ultimo [P3_IH]).
  - Rastro do KMD: C:\\BC250_PAGING_TRACE.bin (Tctl e SDMA1 pela amostra de
    plataforma do proprio KMD, contadores de paginacao BPG1, batimento do GPU).

Nada aqui abre o servico bc250compute nem escreve MMIO/SMN: o par indice/dado
SMN 0x38/0x3C e' do KMD (Tctl), e um segundo ator nele corromperia os dois.

MON.1 (doc 672): uso da GPU por engine (3D, Copy) pelo contador do Windows
"GPU Engine(*)\\Utilization Percentage" (o Windows mede pelo escalonamento das
nossas submissoes, sem tocar a placa), Tctl atual e maximo, estado da doorbell
do GFX (C1) e submissoes por segundo do GFX e do SDMA0. Clock da GPU: NAO
disponivel; so' o SMU sabe, e perguntar a ele e' mensagem nova ao SMU (etapa
propria, com proveniencia do protocolo).
Cada linha tambem vai para C:\\BC250_MONITOR.csv com fsync, para sobreviver
a trava dura.
"""
import ctypes
import ctypes.wintypes as wt
import importlib.util
import os
import sys
import time

LOG = r"C:\BC250_DISPLAY_LOG.txt"
TRACE = r"C:\BC250_PAGING_TRACE.bin"
CSV = r"C:\BC250_MONITOR.csv"
PERIOD = 2.0

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dx56", os.path.join(HERE, "decode-bc250-paging-trace.py"))
dx56 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dx56)

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
powrprof = ctypes.WinDLL("powrprof")
pdh = ctypes.WinDLL("pdh")


class FILETIME(ctypes.Structure):
    _fields_ = [("lo", wt.DWORD), ("hi", wt.DWORD)]


def ft(v):
    return (v.hi << 32) | v.lo


class MEMSTAT(ctypes.Structure):
    _fields_ = [("dwLength", wt.DWORD), ("dwMemoryLoad", wt.DWORD),
                ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]


class PPI(ctypes.Structure):
    _fields_ = [("Number", wt.ULONG), ("MaxMhz", wt.ULONG), ("CurrentMhz", wt.ULONG),
                ("MhzLimit", wt.ULONG), ("MaxIdleState", wt.ULONG), ("CurrentIdleState", wt.ULONG)]


def cpu_times():
    i, k, u = FILETIME(), FILETIME(), FILETIME()
    kernel32.GetSystemTimes(ctypes.byref(i), ctypes.byref(k), ctypes.byref(u))
    return ft(i), ft(k) + ft(u)


def cpu_mhz():
    n = os.cpu_count() or 1
    arr = (PPI * n)()
    if powrprof.CallNtPowerInformation(11, None, 0, arr, ctypes.sizeof(arr)) != 0:
        return None, None
    cur = [p.CurrentMhz for p in arr]
    return sum(cur) / len(cur), max(p.MaxMhz for p in arr)


def ram():
    m = MEMSTAT()
    m.dwLength = ctypes.sizeof(m)
    kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    used = (m.ullTotalPhys - m.ullAvailPhys) / 2**30
    return used, m.ullTotalPhys / 2**30


class PdhItem(ctypes.Structure):
    _fields_ = [("name", wt.LPWSTR), ("status", wt.DWORD), ("pad", wt.DWORD), ("value", ctypes.c_double)]


class Pdh:
    """Soma de um contador curinga do PDH (ex.: \\GPU Adapter Memory(*)\\Dedicated Usage)."""

    def __init__(self, paths):
        self.q = wt.HANDLE()
        self.c = {}
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.q)) != 0:
            self.q = None
            return
        for key, path in paths.items():
            h = wt.HANDLE()
            if pdh.PdhAddEnglishCounterW(self.q, path, 0, ctypes.byref(h)) == 0:
                self.c[key] = h
        pdh.PdhCollectQueryData(self.q)

    def read(self):
        out = {}
        if not self.q:
            return out
        pdh.PdhCollectQueryData(self.q)
        for key, h in self.c.items():
            size, count = wt.DWORD(0), wt.DWORD(0)
            pdh.PdhGetFormattedCounterArrayW(h, 0x200, ctypes.byref(size), ctypes.byref(count), None)
            if size.value == 0:
                continue
            buf = ctypes.create_string_buffer(size.value)
            if pdh.PdhGetFormattedCounterArrayW(h, 0x200, ctypes.byref(size), ctypes.byref(count), buf) != 0:
                continue
            items = ctypes.cast(buf, ctypes.POINTER(PdhItem))
            out[key] = sum(items[i].value for i in range(count.value) if items[i].status == 0)
            out[key + "_itens"] = [(items[i].name, items[i].value) for i in range(count.value)
                                   if items[i].status == 0 and items[i].name]
        return out


def luid_of(name):
    parts = name.split("_")
    i = parts.index("luid")
    return parts[i + 1] + parts[i + 2]


def our_luid(ded_items):
    """O adaptador com mais memoria dedicada em uso e' o nosso (o Basic Render
    da Microsoft, que tambem aparece nos contadores, nao tem memoria dedicada)."""
    best, val = None, 0.0
    for name, value in ded_items:
        try:
            l = luid_of(name)
        except (ValueError, IndexError):
            continue
        if value > val:
            best, val = l, value
    return best


def engine_util(items, luid=None):
    """Instancias "pid_X_luid_..._phys_0_eng_N_engtype_T": soma por engine
    fisica (luid+eng) e devolve o maximo por tipo (3D, Copy, ...), so' do
    adaptador pedido. Sem adaptador identificado, nada (nao mistura com o
    Basic Render)."""
    if luid is None:
        return {}
    per_eng = {}
    for name, value in items:
        try:
            parts = name.split("_")
            l = luid_of(name)
            eng = parts[parts.index("eng") + 1]
            typ = name.split("engtype_", 1)[1]
        except (ValueError, IndexError):
            continue
        if luid is not None and l != luid:
            continue
        key = (l, eng, typ)
        per_eng[key] = per_eng.get(key, 0.0) + value
    out = {}
    for (_, _, typ), v in per_eng.items():
        out[typ] = max(out.get(typ, 0.0), min(v, 100.0))
    return out


class LogTail:
    def __init__(self, path):
        self.path = path
        self.pos = os.path.getsize(path) if os.path.exists(path) else 0
        self.last_growth = time.time()
        self.tot = {"ib": 0, "present": 0, "tdr": 0, "fault": 0, "gfx": 0}
        self.tmr = []
        self.ih = ""
        self.c1 = "sem linha [C1_GFX_DOORBELL] ainda"

    def poll(self):
        new = {"ib": 0, "present": 0, "tdr": 0, "fault": 0, "gfx": 0}
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return new
        if size < self.pos:
            self.pos = 0
        if size > self.pos:
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read(min(size - self.pos, 64 * 1024 * 1024))
            self.pos += len(data)
            self.last_growth = time.time()
            text = data.decode("utf-8", "replace")
            new["ib"] = text.count("IB EXECUTADO")
            new["present"] = text.count("[Present #")
            new["tdr"] = text.count("TDR_QUIESCE")
            new["fault"] = text.count("GFXHUB_FAULT") + text.count("GCVM_FAULT")
            new["gfx"] = text.count("[SubmitVirtual Node1 GFX] ENTER")
            for line in text.splitlines():
                if line.startswith("[C1_GFX_DOORBELL]"):
                    self.c1 = line[18:140]
                elif line.startswith("[TMR1]"):
                    self.tmr.append(line[:150])
                elif line.startswith("[P3_IH] regime"):
                    self.ih = line[:120]
        for k in new:
            self.tot[k] += new[k]
        return new


TRACE_TAIL = 512 * 1024
START_100NS = None  # definido em main()


def trace_state():
    """So' a cauda do rastro (o arquivo e' cumulativo entre corridas e passa
    de 1 GB) e so' amostras gravadas depois que o monitor abriu."""
    try:
        with open(TRACE, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - TRACE_TAIL))
            contents = f.read()
    except OSError:
        return {}
    out = {}
    plats = []
    m = dx56.struct.pack("<I", dx56.PLAT_MAGIC)
    p = contents.find(m)
    while p >= 0:
        try:
            plats.append(dx56.decode_platform_sample(contents[p:p + dx56.GPUHB_SIZE]))
            p = contents.find(m, p + dx56.GPUHB_SIZE)
        except ValueError:
            p = contents.find(m, p + 4)
    after = [r for r in plats if r.get("phase") == "DEPOIS" and "tctl_c" in r
             and r.get("snapshot_time_100ns", 0) >= START_100NS]
    if after:
        last = max(after, key=lambda r: r["snapshot_time_100ns"])
        out["tctl"] = last["tctl_c"]
        out["sdma1"] = "ocioso" if last.get("sdma1_idle") else "ocupado"
    words = [r for r in dx56.scan_words(contents, dx56.PG_MAGIC, dx56.PG_FIELDS)
             if r.get("snapshot_time_100ns", 0) >= START_100NS]
    pg = dx56.summarize_paging_stats(words)
    if pg:
        out["pg"] = pg["last"]
    return out


def main():
    global START_100NS
    # O KMD carimba o rastro com KeQueryInterruptTime (100 ns desde o boot);
    # GetTickCount64 conta o mesmo intervalo em ms. Margem de 2 s.
    kernel32.GetTickCount64.restype = ctypes.c_ulonglong
    START_100NS = max(0, (kernel32.GetTickCount64() - 2000) * 10_000)
    print("Monitor da corrida BC-250 (doc 658): so' leitura, sem tocar a placa. Ctrl+C para sair.")
    path = CSV
    try:
        csv = open(path, "a", encoding="utf-8")
    except PermissionError:
        path = os.path.join(os.environ.get("TEMP", "."), "BC250_MONITOR.csv")
        csv = open(path, "a", encoding="utf-8")
        print(f"(sem elevacao: CSV em {path})")
    new_csv = csv.tell() == 0
    if new_csv:
        csv.write("hora,cpu_pct,cpu_mhz,ram_gb,vram_dedicada_mb,gpu_compartilhada_mb,tctl_c,ib_s,present_s,fills,evicts,flush_tlb,tdr,fault,log_parado_s,gpu3d_pct,gpucopy_pct,gfx_s\n")
    csv.write(f"# inicio {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    csv.flush()
    os.fsync(csv.fileno())
    gpu = Pdh({"ded": r"\GPU Adapter Memory(*)\Dedicated Usage",
               "shr": r"\GPU Adapter Memory(*)\Shared Usage",
               "eng": r"\GPU Engine(*)\Utilization Percentage"})
    tmax = None
    log = LogTail(LOG)
    idle0, tot0 = cpu_times()
    t0 = time.time()
    while True:
        time.sleep(PERIOD)
        idle1, tot1 = cpu_times()
        dt_cpu = max(tot1 - tot0, 1)
        cpu_pct = 100.0 * (1.0 - (idle1 - idle0) / dt_cpu)
        idle0, tot0 = idle1, tot1
        mhz, maxmhz = cpu_mhz()
        used, total = ram()
        g = gpu.read()
        now = time.time()
        dt = max(now - t0, 0.001)
        t0 = now
        new = log.poll()
        tr = trace_state()
        pg = tr.get("pg") or {}
        stale = now - log.last_growth
        tctl = tr.get("tctl")
        if tctl is not None:
            tmax = tctl if tmax is None else max(tmax, tctl)
        eu = engine_util(g.get("eng_itens", []), our_luid(g.get("ded_itens", [])))
        os.system("cls")
        print(f"BC-250 | {time.strftime('%H:%M:%S')} | so' leitura (KMD + Windows)")
        print("-" * 72)
        print(f" CPU   {cpu_pct:5.1f} %   clock medio {mhz or 0:6.0f} MHz (max {maxmhz or 0} MHz)")
        print(f" RAM   {used:5.2f} / {total:5.2f} GB")
        print(f" VRAM  dedicada {g.get('ded', 0) / 2**20:7.1f} MB   compartilhada {g.get('shr', 0) / 2**20:7.1f} MB")
        print(f" Temp  Tctl {('%.1f C' % tctl) if tctl is not None else 'sem amostra'}  (max {('%.1f C' % tmax) if tmax is not None else '-'})   SDMA1 {tr.get('sdma1', '?')}")
        if eu:
            print(f" GPU   uso 3D {eu.get('3D', 0.0):5.1f} %   Copy {eu.get('Copy', 0.0):5.1f} %   (Windows, pelo escalonamento)   clock: indisponivel (so' o SMU sabe)")
        else:
            print(" GPU   uso: - (adaptador BC-250 sem memoria dedicada em uso: driver fora?)   clock: indisponivel")
        print("-" * 72)
        print(f" IBs/s {new['ib'] / dt:7.1f}   GFX/s {new['gfx'] / dt:6.1f}   Presents/s {new['present'] / dt:6.1f}   (total IB {log.tot['ib']}, GFX {log.tot['gfx']}, Present {log.tot['present']})")
        print(f" C1    {log.c1}")
        print(f" Paginacao (KMD, cumulativa): FILL {pg.get('fill_count', '?')}  EVICT {pg.get('evict_count', '?')}  FLUSH_TLB {pg.get('flush_tlb_count', '?')}")
        print(f" TDR {log.tot['tdr']}   faults {log.tot['fault']}   log do KMD parado ha {stale:4.0f} s")
        if log.ih:
            print(f" {log.ih}")
        for line in log.tmr[-2:]:
            print(f" {line}")
        if stale > 10:
            print(" !!! o log do KMD parou de crescer: driver fora, trava ou fim da corrida")
        csv.write(",".join(str(x) for x in (
            time.strftime("%H:%M:%S"), f"{cpu_pct:.1f}", f"{mhz or 0:.0f}", f"{used:.2f}",
            f"{g.get('ded', 0) / 2**20:.1f}", f"{g.get('shr', 0) / 2**20:.1f}",
            f"{tctl:.1f}" if tctl is not None else "", f"{new['ib'] / dt:.1f}", f"{new['present'] / dt:.1f}",
            pg.get("fill_count", ""), pg.get("evict_count", ""), pg.get("flush_tlb_count", ""),
            log.tot["tdr"], log.tot["fault"], f"{stale:.0f}",
            f"{eu.get('3D', 0.0):.1f}", f"{eu.get('Copy', 0.0):.1f}", f"{new['gfx'] / dt:.1f}")) + "\n")
        csv.flush()
        os.fsync(csv.fileno())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
