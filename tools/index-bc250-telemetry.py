#!/usr/bin/env python3
"""Indice de diagnostico: alertas primeiro, depois filas e fences correlacionadas.

Uso: python tools/index-bc250-telemetry.py docs/evidence/<corrida>
Escreve telemetry-index.md na pasta. Nao atribui causa nem prova execucao GPU.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path

DIAG = re.compile(r"BC250_DIAG\s+(.*)")
NAME = re.compile(r"^bc250_ib_pid\d+_t\d+_\d+_va[0-9a-fA-F]{16}_dw\d+\.bin$")
LEGACY = ("[TDR_GFX_QUIESCE]", "BYTE_PROOF FAIL", "BugCheck",
          "DEVICE_LOST", "FENCE COMMIT FALHOU", "SUBMIT RECUSA")


def fields(line: str) -> dict[str, str]:
    return dict(part.split("=", 1) for part in line.split() if "=" in part)


def fnv64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def make_index(evidence: Path) -> tuple[str, int]:
    log = evidence / "display-log-this-bind-FINAL.txt"
    if not log.is_file():
        raise ValueError(f"log FINAL ausente: {log}")
    events: list[tuple[int, dict[str, str]]] = []
    alerts: list[tuple[str, str, str]] = []
    linux_ib_valid_lines: list[int] = []
    counts: collections.Counter[str] = collections.Counter()
    for number, line in enumerate(log.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = DIAG.search(line)
        if match:
            event = fields(match[1])
            if event.get("schema") == "1":
                events.append((number, event))
                counts[event.get("event", "SEM_EVENTO")] += 1
                if event.get("level") == "ERROR":
                    alerts.append(("ERROR", f"FINAL:{number}", event.get("event", "?")))
                if event.get("event") == "KMD_GFX_SUBMIT":
                    for key in ("hash_complete", "committed", "rptr_read", "wptr_read", "sq_read"):
                        if event.get(key) != "1":
                            alerts.append(("INCONCLUSIVO", f"FINAL:{number}",
                                           f"KMD_GFX_SUBMIT {key}={event.get(key, '?')} fence={event.get('ring_fence', '?')}"))
                if event.get("event") == "KMD_TDR_WAVE" and event.get("vmid_valid") == "1":
                    if event.get("vmid") != event.get("last_vmid"):
                        alerts.append(("DIVERGENCIA", f"FINAL:{number}",
                                       f"onda VMID{event['vmid']} / ultimo VMID{event.get('last_vmid', '?')}"))
                if event.get("event") == "KMD_GFX_SWITCH":
                    if event.get("ib_valid") == "1":
                        linux_ib_valid_lines.append(number)
                    if event.get("switch") == "1" and (
                            event.get("pm4_dw") != "34" or event.get("context_control") != "1"):
                        alerts.append(("ERROR", f"FINAL:{number}",
                                       "switch GFX sem PM4DW=34/CONTEXT_CONTROL=1"))
        for marker in LEGACY:
            if marker in line:
                alerts.append(("LEGADO", f"FINAL:{number}", marker))
                counts["LEGACY:" + marker] += 1

    dump_dir = evidence / "gfx-ib-dump"
    dumps: dict[tuple[int, int, int], list[str]] = collections.defaultdict(list)
    queues: collections.Counter[tuple[str, str, str]] = collections.Counter()
    # submit_id pode reiniciar se outra instancia da DLL carregar no mesmo PID.
    # O contexto RADV separa as duas sequencias no arquivo de fluxo do processo.
    flows: dict[tuple[str, str, str], list[str]] = collections.defaultdict(list)
    flow_begins: dict[tuple[str, str, str], dict[str, str]] = {}
    flow_errors: set[tuple[str, str, str]] = set()
    flow_files = 0
    meta_count = 0
    for flow_path in sorted(dump_dir.glob("bc250_flow_pid*_start*.log")) if dump_dir.is_dir() else []:
        flow_files += 1
        if flow_path.stat().st_size >= 4 * 1024 * 1024:
            alerts.append(("INCONCLUSIVO", flow_path.name, "limite de 4 MiB do fluxo RADV atingido"))
        for number, line in enumerate(flow_path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = DIAG.search(line)
            if not match:
                continue
            event = fields(match[1])
            if event.get("schema") != "1":
                continue
            name = event.get("event", "SEM_EVENTO")
            counts[name] += 1
            if event.get("level") == "ERROR":
                alerts.append(("ERROR", f"{flow_path.name}:{number}", name))
            submit_id = event.get("submit_id")
            if submit_id:
                key = (flow_path.name, event.get("ctx", "?"), submit_id)
                flows[key].append(name)
                if event.get("level") == "ERROR":
                    flow_errors.add(key)
                if name == "RADV_QUEUE_SUBMIT_BEGIN":
                    flow_begins[key] = event
    for key, begin in flow_begins.items():
        stages = flows[key]
        expected = []
        if begin.get("waits", "0") != "0":
            expected.append("RADV_QUEUE_WAIT")
        if begin.get("cs", "0") != "0" and "RADV_GANG_NO_HWQUEUE" not in stages:
            expected.extend(("RADV_CS_LAYOUT", "RADV_QUEUE_SUBMIT_END"))
        if begin.get("signals", "0") != "0" and "RADV_GANG_NO_HWQUEUE" not in stages:
            expected.append("RADV_QUEUE_SIGNAL")
        missing = [name for name in expected if name not in stages]
        if missing and key not in flow_errors:
            alerts.append(("INCONCLUSIVO", f"{key[0]} ctx={key[1]} submit_id={key[2]}",
                           "etapas ausentes: " + ",".join(missing)))
    for key in flows.keys() - flow_begins.keys():
        alerts.append(("INCONCLUSIVO", f"{key[0]} ctx={key[1]} submit_id={key[2]}",
                       "inicio da submissao ausente"))
    for path in sorted(dump_dir.glob("*.bin")) if dump_dir.is_dir() else []:
        if not NAME.fullmatch(path.name):
            continue
        meta_path = path.with_name(path.name + ".meta")
        if not meta_path.is_file():
            alerts.append(("INCONCLUSIVO", path.name, "metadados RADV ausentes"))
            continue
        meta = fields(meta_path.read_text(encoding="utf-8", errors="replace"))
        meta_count += 1
        data = path.read_bytes()
        try:
            va, dw = int(meta["va"], 16), int(meta["dw"])
            hash_value = int(meta["fnv64"], 16)
        except (KeyError, ValueError):
            alerts.append(("ERROR", meta_path.name, "metadados incompletos"))
            continue
        if len(data) != dw * 4 or fnv64(data) != hash_value:
            alerts.append(("ERROR", path.name, "tamanho/hash do dump divergente"))
            continue
        if len(data) >= 4 and data[:4] == b"\0\0\0\0":
            prefix = next((i for i in range(0, len(data), 4)
                           if data[i:i + 4] != b"\0\0\0\0"), len(data)) // 4
            alerts.append(("ERROR" if meta.get("accepted") == "1" else "DIAGNOSTICO",
                           path.name,
                           f"IB_PREFIXO_ZERO={prefix} DW accepted={meta.get('accepted', '?')} "
                           f"ctx={meta.get('ctx', '?')} submit_id={meta.get('submit_id', '?')}"))
        queues[(meta.get("pid", "?"), meta.get("ip", "?"), meta.get("ctx", "?"))] += 1
        if meta.get("called") != "1" or meta.get("accepted") != "1":
            alerts.append(("ERROR", meta_path.name,
                           f"D3DKMT nao aceitou: status={meta.get('status', '?')}"))
            continue
        dumps[(va, dw, hash_value)].append(path.name)

    archive = evidence / "display-log-final-archive.json"
    complete = "desconhecida"
    if archive.is_file():
        try:
            info = json.loads(archive.read_text(encoding="utf-8-sig"))
            complete = str(info.get("complete_at_capture", "desconhecida"))
        except (ValueError, OSError):
            complete = "JSON invalido"
    if complete.lower() != "true":
        alerts.insert(0, ("INCONCLUSIVO", archive.name,
                          f"cauda FINAL completa={complete}; conferir resgate"))

    submit_rows: list[str] = []
    submit_count = 0
    for number, event in events:
        if event.get("event") != "KMD_GFX_SUBMIT":
            continue
        submit_count += 1
        if event.get("committed") != "1" or event.get("level") == "ERROR":
            result = "RECUSADO/NAO CONFIRMADO"
        elif event.get("hash_complete") != "1":
            result = "INDETERMINADO (hash incompleto)"
        else:
            try:
                key = (int(event["va"], 16), int(event["dw"]), int(event["fnv64"], 16))
                candidates = dumps.get(key, [])
                result = ("MATCH_UNICO: " + candidates[0] if len(candidates) == 1 else
                          f"AMBIGUO ({len(candidates)} dumps)" if candidates else "AUSENTE")
                if len(candidates) != 1:
                    alerts.append(("INCONCLUSIVO", f"FINAL:{number}",
                                   f"{result} para ring_fence={event.get('ring_fence', '?')}"))
            except (KeyError, ValueError):
                result = "INDETERMINADO (campos invalidos)"
                alerts.append(("ERROR", f"FINAL:{number}", result))
        submit_rows.append(f"| {number} | {event.get('ring_fence', '?')} | {event.get('vmid', '?')} | "
                           f"`{event.get('ctx', '?')}` | `{event.get('fnv64', '?')}` | {result} |")

    lines = ["# Índice de telemetria BC-250", "",
             f"Fonte: `{log.name}`. Cauda completa na captura: **{complete}**.",
             "Este índice localiza evidências; não conclui execução da GPU nem causa do TDR.", "",
             "## Alertas primeiro", "",
             f"Total: **{len(alerts)}**. Mostrando os primeiros 100; use o arquivo fonte para todos.", "",
             "| Nível | Local | Sinal |", "|---|---|---|"]
    for level, location, detail in alerts[:100]:
        lines.append(f"| {level} | `{location}` | `{detail}` |")
    if len(alerts) > 100:
        lines.append(f"\n**{len(alerts)-100} alertas adicionais** omitidos do índice; presentes no log bruto.")
    if not alerts:
        lines.append("| — | — | Nenhum sinal classificado neste escopo. |")

    lines += ["", "## Comparação estática com emissor GFX10 Linux", "",
              f"IBs com bit 23 VALID=1: **{len(linux_ib_valid_lines)}**. "
              "O emissor GFX Linux v6.6 omite esse bit; diferença de fonte, "
              "sem causalidade de TDR demonstrada.", "",
              "Primeiras linhas: " + (", ".join(f"`FINAL:{n}`" for n in linux_ib_valid_lines[:10])
                                     if linux_ib_valid_lines else "nenhuma medição DX.40"), ""]

    lines += ["", "## Filas RADV observadas", "",
              f"Metadados de dump: **{meta_count}**. Cada linha agrupa PID, IP e contexto RADV.", "",
              "| PID | IP | Contexto RADV | Dumps |", "|---:|---:|---|---:|"]
    for (pid, ip, ctx), count in sorted(queues.items()):
        lines.append(f"| {pid} | {ip} | `{ctx}` | {count} |")
    if not queues:
        lines.append("| — | — | Nenhum .meta DX.39 válido | 0 |")

    lines += ["", "## Fluxo RADV por submissão", "",
              f"Arquivos de fluxo: **{flow_files}**. IDs são locais ao processo; "
              "a ordem esperada é BEGIN → WAIT (se houver) → CS_LAYOUT → SUBMIT_END → SIGNAL (se houver).", "",
              "| Arquivo | Contexto RADV | Submit ID | Etapas observadas |", "|---|---|---:|---|"]
    for (filename, ctx, submit_id), stages in sorted(
        flows.items(), key=lambda item: (item[0][0],
                                       item[0][1],
                                       int(item[0][2]) if item[0][2].isdigit() else -1)):
        lines.append(f"| `{filename}` | `{ctx}` | {submit_id} | {' → '.join(stages)} |")
    if not flows:
        lines.append("| — | — | — | Nenhum fluxo DX.40 arquivado |")

    lines += ["", "## Submissões gráficas KMD", "",
              "`MATCH_UNICO` vincula bytes RADV/KMD; duplicatas permanecem ambíguas.", "",
              "| Linha | Ring fence | VMID | Contexto KMD | Hash | Dump RADV |", "|---:|---:|---:|---|---|---|"]
    lines.extend(submit_rows)
    if submit_count == 0:
        lines.append("| — | — | — | — | — | Nenhum KMD_GFX_SUBMIT DX.39 |")

    lines += ["", "## Contagem de eventos", "", "| Evento | Ocorrências |", "|---|---:|"]
    for name, count in counts.most_common():
        lines.append(f"| `{name}` | {count} |")
    if not counts:
        lines.append("| Nenhum evento reconhecido | 0 |")
    lines += ["", "## Busca rápida", "",
              "- `rg -n 'BC250_DIAG.*level=ERROR|BYTE_PROOF FAIL|TDR_GFX_QUIESCE' display-log-this-bind-FINAL.txt`",
              "- `rg -n 'ring_fence=68|RingFence=68|Fence=68' display-log-this-bind-FINAL.txt`",
              "- `python tools/correlate-gfx-ib-identity.py <pasta-da-corrida>`",
              ""]
    return "\n".join(lines), len(alerts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        content, alert_count = make_index(args.evidence)
    except ValueError as exc:
        parser.error(str(exc))
    output = args.output or args.evidence / "telemetry-index.md"
    output.write_text(content, encoding="utf-8")
    print(f"BC250 TELEMETRY INDEX: {output} ({alert_count} alertas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
