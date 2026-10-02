"""umd_inventory.py -- Frente R1 do PLANO_DESKTOP_ACELERADO.md (30/09/2026).

Inventario do que o DWM e os clientes do shell pedem a nossa UMD, extraido dos
logs das corridas (bc250_umd_log_*). E' medicao do que PASSOU pela nossa UMD,
nao engenharia reversa: cada numero sai de uma linha de log.

Uso:
    python tools/umd_inventory.py <pasta_evidencia> [<pasta_evidencia> ...]
        [--md saida.md] [--json saida.json]

Regras de leitura (declaradas no relatorio):
  * Logs por processo vem de tres lugares: raiz (bc250_umd_log_<pid>-FINAL.txt),
    final-elevado/ (copia elevada apos trava) e umd-logs-appcontainer/ (menu,
    Pesquisa, ShellExperienceHost). O agregado bc250_umd_log-FINAL.txt e os
    snapshots sem -FINAL sao ignorados quando existe a versao FINAL do mesmo pid.
  * Linhas prefixadas por [DX70_SAMPLE c=K n=N] sao AMOSTRADAS: a contagem direta
    subestima. Para essas classes o total real e' o maior N visto.
  * Um log so' e' atribuido a um processo pelo prefixo [pid=N imagem]; linhas de
    outros pids no mesmo arquivo (UserTemp reaproveitado) sao separadas.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys
from typing import Dict, List

LINE_RE = re.compile(r"^(\d\d:\d\d:\d\d\.\d{3}) \[pid=(\d+) ([^\]]+)\] (.*)$")
SAMPLE_RE = re.compile(r"^\[DX70_SAMPLE c=(\d+) n=(\d+)\] (.*)$")
DDI_RE = re.compile(r"^([A-Z][A-Za-z0-9_]+)(?: RECUSADO)?[:(]")


def find_logs(folders: List[str]) -> List[str]:
    """Um arquivo por (pasta, pid): prefere -FINAL e o maior."""
    chosen: Dict[tuple, str] = {}
    for folder in folders:
        for root, _dirs, files in os.walk(folder):
            for name in files:
                low = name.lower()
                if "umd_log" not in low or not low.endswith(".txt"):
                    continue
                if "-radv" in low:
                    continue  # stderr da RADV (I.1): outro formato, lido a parte
                if low in ("bc250_umd_log-final.txt", "bc250_umd_log.txt"):
                    continue  # agregados: duplicam os logs por pid
                rl = root.lower()
                if "residuais" in rl or low.startswith("usertemp__"):
                    continue  # binds anteriores / programas de teste de outros dias
                in_sub = ("final-elevado" in rl) or ("appcontainer" in rl)
                if not in_sub and "-final" not in low:
                    continue  # snapshots e logs antigos copiados na raiz
                m = re.search(r"umd_log_(\d+)", low)
                if not m:
                    continue
                path = os.path.join(root, name)
                key = (os.path.abspath(folder), m.group(1))
                prev = chosen.get(key)
                score = (("-final" in low) or ("final-elevado" in root.lower()) or ("appcontainer" in root.lower()),
                         os.path.getsize(path))
                if prev is None or score > prev[0]:
                    chosen[key] = (score, path)
    return [v[1] for v in chosen.values()]


def new_proc():
    return {
        "logs": set(), "linhas": 0, "ddi": collections.Counter(), "amostradas_max_n": {},
        "amostradas_linhas": collections.Counter(),
        "recusas": collections.Counter(),
        "recursos": collections.Counter(), "recursos_tamanhos": collections.Counter(),
        "vk_recursos": collections.Counter(),
        "check_format": {}, "srv": collections.Counter(), "rtv": collections.Counter(),
        "open_resource": collections.Counter(),
        "shaders": collections.Counter(), "shader_tokens": collections.Counter(),
        "traducao": collections.Counter(),
        "blend": collections.Counter(), "raster": collections.Counter(), "depth": collections.Counter(),
        "sampler": collections.Counter(), "topologia": collections.Counter(), "layout": collections.Counter(),
        "draws": collections.Counter(), "map": collections.Counter(), "present": collections.Counter(),
        "interface": collections.Counter(), "cargas_dll": 0,
    }


def norm(text: str) -> str:
    text = re.sub(r"0x[0-9A-Fa-f]+", "X", text)
    text = re.sub(r"\b[0-9A-F]{16}\b", "H", text)
    return re.sub(r"\d+", "N", text)[:170]


def parse_file(path: str, procs: Dict[str, dict]) -> None:
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            m = LINE_RE.match(raw.rstrip("\r\n"))
            if not m:
                continue
            image, msg = m.group(3), m.group(4)
            p = procs.setdefault(image, new_proc())
            p["logs"].add(path)
            p["linhas"] += 1
            s = SAMPLE_RE.match(msg)
            if s:
                cls, n, msg = int(s.group(1)), int(s.group(2)), s.group(3)
                p["amostradas_linhas"][cls] += 1
                key = f"c={cls}"
                p["amostradas_max_n"][key] = max(p["amostradas_max_n"].get(key, 0), n)
            d = DDI_RE.match(msg)
            if d:
                p["ddi"][d.group(1)] += 1
            if "RECUSADO" in msg or "RECUSA " in msg or "falhou" in msg:
                p["recusas"][norm(msg)] += 1
            if msg.startswith("=== bc250umd.dll CARREGADA"):
                p["cargas_dll"] += 1
            r = msg.startswith("CreateResource: res=") and re.match(r"CreateResource: res=\S+ Dim=(\d+) Fmt=(\d+) Size=(\d+) \((\d+)x(\d+)", msg)
            if r:
                p["recursos"][f"Dim={r.group(1)} Fmt={r.group(2)}"] += 1
                p["recursos_tamanhos"][f"Dim={r.group(1)} {r.group(4)}x{r.group(5)} Fmt={r.group(2)}"] += 1
            r = msg.startswith("[VK_RES] Bc250UmdVkCreateResource:") and re.match(r"\[VK_RES\] Bc250UmdVkCreateResource: (\w+) (\S+).*bindFlags=(0x[0-9A-Fa-f]+)", msg)
            if r:
                p["vk_recursos"][f"{r.group(1)} bind={r.group(3)}"] += 1
            r = msg.startswith("CheckFormatSupport:") and re.match(r"CheckFormatSupport: Format=(\d+) -> Caps=(0x[0-9A-Fa-f]+)", msg)
            if r:
                p["check_format"][int(r.group(1))] = r.group(2)
            r = msg.startswith("CreateShaderResourceView:") and re.match(r"CreateShaderResourceView: \S+ \S+ Format=(\d+) Dim=(\d+)", msg)
            if r:
                p["srv"][f"Fmt={r.group(1)} Dim={r.group(2)}"] += 1
            r = msg.startswith("CreateRenderTargetView:") and re.match(r"CreateRenderTargetView: \S+ \S+ Dim=(\d+) Fmt=(\d+)", msg)
            if r:
                p["rtv"][f"Fmt={r.group(2)} Dim={r.group(1)}"] += 1
            r = msg.startswith("OpenResource: geometria") and re.match(r"OpenResource: geometria extraida (\d+)x(\d+) pitch=\d+ fmt=\d+ \(dxgi=(\d+)\)", msg)
            if r:
                p["open_resource"][f"{r.group(1)}x{r.group(2)} dxgi={r.group(3)}"] += 1
            r = msg.startswith("Create") and "Shader:" in msg[:26] and re.match(r"Create(Pixel|Vertex|Geometry|Hull|Domain|Compute)Shader: \S+ SM=(\S+) tokens=(\d+)", msg)
            if r:
                p["shaders"][f"{r.group(1)} SM={r.group(2)}"] += 1
                p["shader_tokens"][f"{r.group(1)}:{r.group(3)}"] += 1
            if msg.startswith("SHADER_TRANSLATOR: TokenStream -> SPIR-V"):
                p["traducao"]["ok"] += 1
            elif msg.startswith("SHADER_TRANSLATOR") and ("falhou" in msg or "RECUSADO" in msg or "recusado" in msg):
                p["traducao"]["falha"] += 1
            r = msg.startswith("CreateBlendState:") and re.match(r"CreateBlendState: \S+ AlphaToCoverage=(\d) IndBlend=(\d)", msg)
            if r:
                p["blend"][f"AlphaToCoverage={r.group(1)} IndBlend={r.group(2)}"] += 1
            r = msg.startswith("CreateRasterizerState:") and re.match(r"CreateRasterizerState: \S+ FillMode=(\d+) CullMode=(\d+) Scissor=(\d)", msg)
            if r:
                p["raster"][f"Fill={r.group(1)} Cull={r.group(2)} Scissor={r.group(3)}"] += 1
            r = msg.startswith("CreateDepthStencilState:") and re.match(r"CreateDepthStencilState: \S+ DepthEnable=(\d) StencilEnable=(\d)", msg)
            if r:
                p["depth"][f"Depth={r.group(1)} Stencil={r.group(2)}"] += 1
            r = msg.startswith("CreateSampler:") and re.match(r"CreateSampler: \S+ Filter=(\d+) AddressU=(\d+)", msg)
            if r:
                p["sampler"][f"Filter={r.group(1)} AddressU={r.group(2)}"] += 1
            r = msg.startswith("IaSetTopology:") and re.match(r"IaSetTopology: Topology=(\d+)", msg)
            if r:
                p["topologia"][r.group(1)] += 1
            r = msg.startswith("CreateElementLayout:") and re.match(r"CreateElementLayout: \S+ NumElements=(\d+)", msg)
            if r:
                p["layout"][r.group(1)] += 1
            r = "DRAW GRAVADO" in msg and re.search(r"Bc250UmdExecuteVkDraw: DRAW GRAVADO.*Inst=(\d+), Indexed=(\d)", msg)
            if r:
                p["draws"][f"Indexed={r.group(2)} Inst={'1' if r.group(1) == '1' else '>1'}"] += 1
            r = msg.startswith("Draw") and re.match(r"(Draw\w*): RECUSADO -- (?:backend Vulkan motivo=)?(\S+)", msg)
            if r:
                p["draws"][f"RECUSADO {r.group(1)} {r.group(2)}"] += 1
            r = msg.startswith("ResourceMap:") and re.match(r"ResourceMap: \S+ Sub=\d+ MapType=(\d+)", msg)
            if r:
                p["map"][f"MapType={r.group(1)}"] += 1
            r = msg.startswith("DXGI Present ENTER cb:") and re.match(r"DXGI Present ENTER cb:.*Flags=(0x[0-9A-Fa-f]+)", msg)
            if r:
                p["present"][f"Flags={r.group(1)}"] += 1
            r = msg.startswith("D3D10 CreateDevice:") and re.match(r"D3D10 CreateDevice: INTERFACE ACORDADA=(0x[0-9A-Fa-f]+)", msg)
            if r:
                p["interface"][r.group(1)] += 1


DXGI_FMT = {0: "UNKNOWN", 1: "R32G32B32A32_TYPELESS", 2: "R32G32B32A32_FLOAT", 10: "R16G16B16A16_FLOAT",
            24: "R10G10B10A2_UNORM", 28: "R8G8B8A8_UNORM", 29: "R8G8B8A8_UNORM_SRGB", 41: "R32_FLOAT",
            42: "R32_UINT", 45: "D24_UNORM_S8_UINT", 49: "R8G8_UNORM", 54: "R16_FLOAT", 56: "R16_UNORM",
            57: "R16_UINT", 61: "R8_UNORM", 62: "R8_UINT", 65: "A8_UNORM", 71: "BC1_UNORM", 77: "BC3_UNORM",
            87: "B8G8R8A8_UNORM", 88: "B8G8R8X8_UNORM", 91: "B8G8R8A8_UNORM_SRGB", 98: "BC7_UNORM"}


def fmt_name(key: str) -> str:
    return re.sub(r"(Fmt=|dxgi=)(\d+)", lambda m: f"{m.group(1)}{m.group(2)}({DXGI_FMT.get(int(m.group(2)), '?')})", key)


def top(counter, n=12, fmt=False) -> str:
    items = counter.most_common(n)
    if not items:
        return "_nenhum_"
    return "<br>".join(f"{fmt_name(k) if fmt else k}: {v}" for k, v in items)


def report(procs: Dict[str, dict], folders: List[str]) -> str:
    order = sorted(procs, key=lambda k: -procs[k]["linhas"])
    out = ["# Inventário da UMD por processo (Frente R1)", "",
           f"Pastas: {', '.join(os.path.basename(os.path.normpath(f)) for f in folders)}", ""]
    out += ["| Processo | Linhas | Logs | Cargas da DLL | Interface |", "|---|---|---|---|---|"]
    for k in order:
        p = procs[k]
        out.append(f"| {k} | {p['linhas']} | {len(p['logs'])} | {p['cargas_dll']} | {top(p['interface'], 3)} |")
    for k in order:
        p = procs[k]
        if p["linhas"] < 200:
            continue
        out += ["", f"## {k}", ""]
        rows = [
            ("DDIs mais chamadas", top(p["ddi"], 25)),
            ("Recursos criados (Dim, formato)", top(p["recursos"], 12, True)),
            ("Recursos: tamanhos mais comuns", top(p["recursos_tamanhos"], 10, True)),
            ("Recursos Vulkan (tipo, bind)", top(p["vk_recursos"], 10)),
            ("SRV (formato, dimensão)", top(p["srv"], 10, True)),
            ("RTV (formato, dimensão)", top(p["rtv"], 10, True)),
            ("OpenResource (superfícies de outros processos)", top(p["open_resource"], 10, True)),
            ("Shaders (tipo, SM)", top(p["shaders"])),
            ("Shaders distintos por tamanho (tipo:tokens)", f"{len(p['shader_tokens'])} tamanhos distintos"),
            ("Tradução DXBC→SPIR-V", top(p["traducao"])),
            ("Blend state (campos registrados)", top(p["blend"])),
            ("Rasterizer (Fill, Cull, Scissor)", top(p["raster"])),
            ("Depth/stencil", top(p["depth"])),
            ("Sampler (Filter, AddressU)", top(p["sampler"])),
            ("Topologias (D3D10_DDI_PRIMITIVE_TOPOLOGY)", top(p["topologia"])),
            ("Input layout (nº de elementos)", top(p["layout"])),
            ("Draws", top(p["draws"], 15)),
            ("Map (MapType; linha amostrada)", top(p["map"])),
            ("Present (Flags)", top(p["present"])),
            ("Formatos consultados (CheckFormatSupport)", f"{len(p['check_format'])} formatos; com Caps≠0: "
             + ", ".join(f"{f}({DXGI_FMT.get(f, '?')})" for f, c in sorted(p['check_format'].items()) if c != '0x00000000')[:900]),
            ("Classes amostradas (maior n = total real)", ", ".join(f"{c}: linhas={p['amostradas_linhas'][int(c[2:])]} n_max={n}"
                                                            for c, n in sorted(p['amostradas_max_n'].items())) or "_nenhuma_"),
            ("Recusas e falhas", top(p["recusas"], 15)),
        ]
        out += ["| Item | Medido |", "|---|---|"]
        out += [f"| {a} | {b} |" for a, b in rows]
    return "\n".join(out) + "\n"


def jsonable(procs):
    res = {}
    for k, p in procs.items():
        res[k] = {kk: (sorted(v) if isinstance(v, set) else dict(v) if isinstance(v, (collections.Counter, dict)) else v)
                  for kk, v in p.items()}
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--md")
    ap.add_argument("--json")
    a = ap.parse_args()
    logs = find_logs(a.folders)
    if not logs:
        print("nenhum log de UMD encontrado", file=sys.stderr)
        return 1
    procs: Dict[str, dict] = {}
    for path in sorted(logs):
        parse_file(path, procs)
    md = report(procs, a.folders)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as h:
            h.write(md)
    else:
        print(md)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as h:
            json.dump({"pastas": a.folders, "logs": sorted(logs), "processos": jsonable(procs)}, h, indent=1, default=str)
    print(f"[inventario] {len(logs)} logs, {len(procs)} processos", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
