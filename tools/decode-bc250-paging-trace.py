"""Decode the DX.56 durable KMD paging-stage trace after a reboot.

Usage: python tools/decode-bc250-paging-trace.py C:\\BC250_PAGING_TRACE.bin
Records are append-only; the last incomplete or torn record is reported, not used.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


HEADER = struct.Struct("<IIIIIIQ")
SLOT = struct.Struct("<IIIiQQ")
TRAILER = struct.Struct("<II")
RECORD_SIZE = HEADER.size + 16 * SLOT.size + TRAILER.size
MAGIC = 0x36585042
STAGE_NAMES = {
    1: "BUILD_ENTER", 2: "AFTER_DDI_MARK", 3: "AFTER_ENTRY_LOG",
    4: "AFTER_CAPS", 5: "BEFORE_DISPATCH", 6: "AFTER_DISPATCH",
    7: "FLUSH_HANDLER_ENTER", 8: "FLUSH_HANDLER_LEAVE",
    20: "UPDATE_ENTER", 21: "UPDATE_AFTER_HEADER_LOG",
    22: "UPDATE_AFTER_ENTRY_LOGS", 23: "UPDATE_BEFORE_HANDLER",
    24: "UPDATE_AFTER_HANDLER", 40: "FLUSH_OBJECT_LOCK_WAIT",
    41: "FLUSH_OBJECT_LOCK_HELD", 42: "FLUSH_GFXHUB_WAIT",
    43: "FLUSH_GFXHUB_DONE", 44: "FLUSH_JOURNAL_WAIT",
    45: "FLUSH_JOURNAL_DONE", 46: "FLUSH_OBJECT_LOCK_RELEASE",
}


# DX.87: registro de migalhas MMIO (espelho em RAM) gravado depois de cada
# registro de paginacao. Layout em bc250_mcdm_callbacks.c (BC250_CRUMB_TRACE_RECORD).
CRUMB_MAGIC = 0x31524342
CRUMB_SLOTS = 96
CRUMB_CPUS = 32
CRUMB_HEADER = struct.Struct("<IIIIIIQ")
CRUMB_SIZE = CRUMB_HEADER.size + CRUMB_SLOTS * 12 + 8
CRUMB_SITES = {
    1: "TRANSPORT_READ", 2: "TRANSPORT_WRITE", 3: "SDMA_READ", 4: "SDMA_WRITE",
    5: "OTG_READ", 6: "RINGWORK_READ", 7: "RINGWORK_WRITE", 8: "VRAM_SAMPLE",
    9: "VRAM_COMPARE", 10: "PAGING_DDI", 11: "SUBMIT_DDI",
}
CRUMB_BANKS = {0: "INTERNA", 1: "PAGINACAO", 2: "SUBMISSAO"}
# DX.92: no banco SUBMISSAO o offset e' (no' << 16) | etapa (bc250_active_miniport.c).
SUBMIT_STAGES = {
    0x01: "ENTRADA", 0x10: "RETIRE_AND_NOTIFY", 0x11: "RETIRE_FEITO",
    0x12: "INSTALL_VMID", 0x13: "INSTALL_VMID_FEITO", 0x14: "SNAPSHOT_IB",
    0x15: "PT_WALK_DEST", 0x16: "PT_WALK_IB", 0x17: "PT_WALK_FEITO",
    0x18: "ANEL_SDMA", 0x19: "ANEL_SDMA_FEITO",
    0x1A: "CAMINHO_ESCOLHA", 0x1B: "PRE_RETIRE",
}


def decode_crumb_record(data: bytes) -> dict:
    if len(data) != CRUMB_SIZE:
        raise ValueError("registro de migalha incompleto")
    magic, version, count, tick, last, _reserved, when = CRUMB_HEADER.unpack_from(data)
    checksum, padding = struct.unpack_from("<II", data, CRUMB_SIZE - 8)
    if magic != CRUMB_MAGIC or version != 1 or count != CRUMB_SLOTS or padding:
        raise ValueError("cabecalho de migalha invalido")
    if checksum != fnv1a(data[: CRUMB_SIZE - 8]):
        raise ValueError("checksum de migalha divergente")
    in_flight = []
    for slot in range(CRUMB_SLOTS):
        seq, site_offset, phase = struct.unpack_from("<III", data, CRUMB_HEADER.size + slot * 12)
        if seq and phase == 0xB0:
            item = {
                "sequence": seq,
                "cpu": slot % CRUMB_CPUS,
                "bank": CRUMB_BANKS.get(slot // CRUMB_CPUS, "?"),
                "site": CRUMB_SITES.get(site_offset >> 24, f"SITE_{site_offset >> 24}"),
                "offset": f"0x{site_offset & 0xFFFFFF:06X}",
            }
            if (site_offset >> 24) == 11:
                offset = site_offset & 0xFFFFFF
                item["submit_node"] = (offset >> 16) & 0xFF
                stage = offset & 0xFFFF
                # Antes da DX.92 o offset era so' o no' (etapa 0).
                item["submit_stage"] = SUBMIT_STAGES.get(stage, f"ETAPA_0x{stage:X}")
            in_flight.append(item)
    in_flight.sort(key=lambda item: item["sequence"])
    return {"writer_tick": tick, "last_sequence": last,
            "snapshot_time_100ns": when, "in_flight": in_flight}


# DX.90: batimento do GPU (BC250_GPU_HEARTBEAT_RECORD). Fase 0xB0 e' gravada
# e descarregada ANTES das leituras de registrador; 0xA0 traz os valores.
# Um ANTES sem o DEPOIS do mesmo tick no fim do arquivo = leitura que nao voltou.
GPUHB_MAGIC = 0x31484742
GPUHB_HEADER = struct.Struct("<IIIIIIQ")
GPUHB_BODY = struct.Struct("<8I")
GPUHB_SIZE = GPUHB_HEADER.size + GPUHB_BODY.size + 8
# Anel grafico: 64 KB = 16384 dwords (bc250_active_miniport.c, ringBytes do
# GfxEngine). CP_RB0_RPTR do hardware da' a volta no anel; o WPTR que
# publicamos e' cursor monotono. Comparar cru e' a armadilha da A15: 0x88D4
# contra 0x08D4 e' anel OCIOSO (Corrida 142).
GFX_RING_DWORDS = 16384
GPUHB_FIELDS = ("read_mask", "grbm_status", "cp_stat", "cp_rb0_rptr",
                "cp_rb0_wptr", "rptr_writeback", "fence_memory",
                "last_submitted_fence")


def decode_gpu_heartbeat(data: bytes) -> dict:
    if len(data) != GPUHB_SIZE:
        raise ValueError("batimento incompleto")
    magic, version, tick, phase, measured, _reserved, when = GPUHB_HEADER.unpack_from(data)
    checksum, padding = struct.unpack_from("<II", data, GPUHB_SIZE - 8)
    if magic != GPUHB_MAGIC or version != 1 or phase not in (0xB0, 0xA0) or padding:
        raise ValueError("cabecalho de batimento invalido")
    if checksum != fnv1a(data[: GPUHB_SIZE - 8]):
        raise ValueError("checksum de batimento divergente")
    values = dict(zip(GPUHB_FIELDS, GPUHB_BODY.unpack_from(data, GPUHB_HEADER.size)))
    out = {"writer_tick": tick, "phase": "ANTES" if phase == 0xB0 else "DEPOIS",
           "measured": measured, "snapshot_time_100ns": when}
    if phase == 0xA0:
        for key, value in values.items():
            out[key] = f"0x{value:08X}"
        rptr = values["cp_rb0_rptr"] % GFX_RING_DWORDS
        wptr = values["cp_rb0_wptr"] % GFX_RING_DWORDS
        out["gfx_ring_pending_dw"] = (wptr - rptr) % GFX_RING_DWORDS
        out["gfx_ring_idle"] = rptr == wptr
        # Fence em memoria atras da ultima submetida = trabalho grafico pendente.
        out["gfx_fence_behind"] = (values["last_submitted_fence"] - values["fence_memory"]) & 0xFFFFFFFF
    return out


def summarize_gpu_heartbeats(records: list) -> dict | None:
    if not records:
        return None
    last = records[-1]
    tail = [r for r in records if r["phase"] == "DEPOIS"][-5:]
    return {
        "records": len(records),
        "last_phase": last["phase"],
        "last_writer_tick": last["writer_tick"],
        # ANTES como ultimo registro: a thread entrou nas leituras e nao voltou.
        "read_never_returned": last["phase"] == "ANTES",
        "last_after_records": tail,
    }


# I.PLAT (doc 553): amostra de plataforma (BC250_PLATFORM_SAMPLE_RECORD), 1 s.
# Mesmo layout e mesma regra ANTES/DEPOIS do BGH1. Tctl pela conversao do
# k10temp (P1); o par RSMU alcancar 0x59800 e' H, entao valor fora de 0..125 C
# sai marcado e nao conta como temperatura.
PLAT_MAGIC = 0x31535042
PLAT_FIELDS = ("read_mask", "sdma1_status", "sdma1_f32_cntl", "sdma1_rb_cntl",
               "sdma1_rb_rptr", "sdma1_rb_wptr", "tctl_raw", "tctl_milli_c_kmd")
PLAT_READ_TCTL = 0x20


def tctl_milli_c(raw: int) -> int:
    value = (raw >> 21) * 125
    if (raw & 0x00080000) or (raw & 0x00030000) == 0x00030000:
        value -= 49000
    return value


def decode_platform_sample(data: bytes) -> dict:
    if len(data) != GPUHB_SIZE:
        raise ValueError("amostra de plataforma incompleta")
    magic, version, tick, phase, measured, _reserved, when = GPUHB_HEADER.unpack_from(data)
    checksum, padding = struct.unpack_from("<II", data, GPUHB_SIZE - 8)
    if magic != PLAT_MAGIC or version != 1 or phase not in (0xB0, 0xA0) or padding:
        raise ValueError("cabecalho de amostra de plataforma invalido")
    if checksum != fnv1a(data[: GPUHB_SIZE - 8]):
        raise ValueError("checksum de amostra de plataforma divergente")
    values = dict(zip(PLAT_FIELDS, GPUHB_BODY.unpack_from(data, GPUHB_HEADER.size)))
    out = {"writer_tick": tick, "phase": "ANTES" if phase == 0xB0 else "DEPOIS",
           "measured": measured, "snapshot_time_100ns": when}
    if phase == 0xA0:
        for key in PLAT_FIELDS[:7]:
            out[key] = f"0x{values[key]:08X}"
        out["sdma1_idle"] = bool(values["sdma1_status"] & 1)
        out["sdma1_halted"] = bool(values["sdma1_f32_cntl"] & 1)
        out["sdma1_rb_enable"] = bool(values["sdma1_rb_cntl"] & 1)
        if values["read_mask"] & PLAT_READ_TCTL:
            milli = tctl_milli_c(values["tctl_raw"])
            kmd = values["tctl_milli_c_kmd"]
            if kmd >= 0x80000000:
                kmd -= 0x100000000
            out["tctl_c"] = milli / 1000.0
            out["tctl_kmd_agrees"] = milli == kmd
            out["tctl_plausible"] = 0 <= milli <= 125000
    return out


def summarize_platform_samples(records: list) -> dict | None:
    if not records:
        return None
    last = records[-1]
    after = [r for r in records if r["phase"] == "DEPOIS"]
    temps = [r["tctl_c"] for r in after if r.get("tctl_plausible")]
    return {
        "records": len(records),
        "last_phase": last["phase"],
        "last_writer_tick": last["writer_tick"],
        "read_never_returned": last["phase"] == "ANTES",
        "tctl_samples": len(temps),
        "tctl_implausible": sum(1 for r in after if "tctl_c" in r and not r["tctl_plausible"]),
        "tctl_min_c": min(temps) if temps else None,
        "tctl_max_c": max(temps) if temps else None,
        "tctl_last_c": temps[-1] if temps else None,
        "sdma1_state_changes": len({(r.get("sdma1_status"), r.get("sdma1_f32_cntl"),
                                     r.get("sdma1_rb_cntl")) for r in after}),
        "last_after_records": after[-5:],
    }


# I.SDMA0 / I.CG (doc 556): BSQ1 (a cada 100 ms) e BCG1 (a cada 10 s), mesmo
# layout de 72 bytes. Words[0] = mascara das leituras que voltaram.
SQ_MAGIC = 0x31515342
CG_MAGIC = 0x31474342
SQ_FIELDS = ("sdma0_status", "sdma0_status1", "sdma0_status2", "sdma0_rb_rptr",
             "sdma0_rb_wptr", "sdma0_ib_rptr", "sdma0_context_status")
CG_FIELDS = ("sdma0_power_cntl", "sdma0_clk_ctrl", "sdma1_power_cntl", "sdma1_clk_ctrl",
             "rlc_cgtt_mgcg_override", "rlc_cgcg_cgls_ctrl", "cgtt_spi_clk_ctrl")
SDMA_RING_BYTES = 4096  # anel do SDMA0 (A15: comparar ponteiros reduzidos a uma volta)


def decode_words_record(data: bytes, magic_expected: int, fields: tuple) -> dict:
    if len(data) != GPUHB_SIZE:
        raise ValueError("registro de palavras incompleto")
    magic, version, tick, phase, measured, _reserved, when = GPUHB_HEADER.unpack_from(data)
    checksum, padding = struct.unpack_from("<II", data, GPUHB_SIZE - 8)
    if magic != magic_expected or version != 1 or phase not in (0xB0, 0xA0) or padding:
        raise ValueError("cabecalho de registro de palavras invalido")
    if checksum != fnv1a(data[: GPUHB_SIZE - 8]):
        raise ValueError("checksum de registro de palavras divergente")
    words = GPUHB_BODY.unpack_from(data, GPUHB_HEADER.size)
    out = {"writer_tick": tick, "phase": "ANTES" if phase == 0xB0 else "DEPOIS",
           "measured": measured, "snapshot_time_100ns": when}
    if phase == 0xA0:
        out["read_mask"] = f"0x{words[0]:02X}"
        for i, name in enumerate(fields):
            if words[0] & (1 << i):
                out[name] = f"0x{words[i + 1]:08X}"
        if magic_expected == SQ_MAGIC and (words[0] & 0x19) == 0x19:
            status, rptr, wptr = words[1], words[4], words[5]
            out["sdma0_idle"] = bool(status & 0x1)
            out["sdma0_rb_empty"] = bool(status & 0x4)
            out["sdma0_inside_ib"] = bool(status & 0x200)
            out["sdma0_ring_drained"] = (rptr % SDMA_RING_BYTES) == (wptr % SDMA_RING_BYTES)
    return out


def scan_words(contents: bytes, magic: int, fields: tuple) -> list:
    records = []
    m = struct.pack("<I", magic)
    position = contents.find(m)
    while position >= 0:
        try:
            records.append(decode_words_record(contents[position:position + GPUHB_SIZE], magic, fields))
            position = contents.find(m, position + GPUHB_SIZE)
        except ValueError:
            position = contents.find(m, position + 4)
    return records


def summarize_words(records: list) -> dict | None:
    if not records:
        return None
    after = [r for r in records if r["phase"] == "DEPOIS"]
    last = records[-1]
    states = {}
    for r in after:
        key = tuple((k, v) for k, v in r.items() if k not in ("writer_tick", "phase", "measured", "snapshot_time_100ns"))
        states.setdefault(key, 0)
        states[key] += 1
    return {
        "records": len(records),
        "last_phase": last["phase"],
        "last_writer_tick": last["writer_tick"],
        "read_never_returned": last["phase"] == "ANTES",
        "distinct_states": len(states),
        "last_after_records": after[-5:],
    }


# I.PG (doc 571): BPG1 a cada 1 s, contadores de paginacao CUMULATIVOS (software,
# nenhuma leitura de registrador). Words[0] = 0x7F.
PG_MAGIC = 0x31475042
PG_FIELDS = ("evict_count", "evict_kib", "pagein_count", "pagein_kib",
             "fill_count", "flush_tlb_count", "ring_flush_emitted")


def cut_at_reboot(rows: list) -> list:
    """O arquivo RECUPERADO inclui registros do boot de recuperacao (doc 582):
    a corrida termina no primeiro recuo do writer_tick. Sem este corte o
    resumo pega o ultimo registro do boot seguinte (zerado), como na 263b."""
    out = []
    for row in rows:
        if out and row["writer_tick"] < out[-1]["writer_tick"]:
            break
        out.append(row)
    return out


def summarize_paging_stats(records: list) -> dict | None:
    after = [r for r in records if r["phase"] == "DEPOIS"]
    if not after:
        return None
    rows = []
    for r in after:
        row = {"writer_tick": r["writer_tick"]}
        for name in PG_FIELDS:
            row[name] = int(r[name], 16) if name in r else None
        rows.append(row)
    rows = cut_at_reboot(rows)
    # deltas por segundo nos ultimos 20 registros: mostram o vai-e-volta antes do fim
    deltas = []
    window = rows[-21:]  # com menos de 21 registros o par antigo comparava cada registro com ele mesmo
    for prev, cur in zip(window[:-1], window[1:]):
        d = {"writer_tick": cur["writer_tick"]}
        for name in PG_FIELDS:
            if cur[name] is not None and prev[name] is not None:
                d[name] = cur[name] - prev[name]
        deltas.append(d)
    return {"records": len(rows), "last": rows[-1], "last_deltas_per_second": deltas}


# 263b (doc 695): BFT1 no mesmo tique do BPG1, contadores CUMULATIVOS de software.
# flush_paging = FLUSH_TLB do processo de paginacao; flush_user_vmid = processo cuja
# raiz esta instalada em alguma VMID; flush_user_sem_vmid = processo sem VMID;
# vmids_invalidadas = soma das VMIDs na mascara de cada FLUSH_TLB; faixas_pte_sdma1 =
# faixas roteadas ao SDMA1 (P4.2); leituras_sq = SQ_DEBUG_STS_GLOBAL pela CPU;
# invalidacoes_cpu_eng17 = invalidacoes de TLB do GFXHUB pela CPU.
FT_MAGIC = 0x31544642
FT_FIELDS = ("flush_paging", "flush_user_vmid", "flush_user_sem_vmid",
             "vmids_invalidadas", "faixas_pte_sdma1", "leituras_sq", "invalidacoes_cpu_eng17")


def summarize_flush_classes(records: list, fields: tuple = FT_FIELDS) -> dict | None:
    after = [r for r in records if r["phase"] == "DEPOIS"]
    if not after:
        return None
    rows = []
    for r in after:
        row = {"writer_tick": r["writer_tick"]}
        for name in fields:
            row[name] = int(r[name], 16) if name in r else None
        rows.append(row)
    rows = cut_at_reboot(rows)
    deltas = []
    window = rows[-21:]  # com menos de 21 registros o par antigo comparava cada registro com ele mesmo
    for prev, cur in zip(window[:-1], window[1:]):
        d = {"writer_tick": cur["writer_tick"]}
        for name in fields:
            if cur[name] is not None and prev[name] is not None:
                d[name] = cur[name] - prev[name]
        deltas.append(d)
    return {"records": len(rows), "last": rows[-1], "last_deltas_per_second": deltas}


# MEC.2 (doc 701): BMP1 -- polls do MEC (timer de 1 ms), cumulativos: total, com
# seletor GRBM e leitura de PQ_RPTR, e sem MMIO. Com a MEC.2 armada, o com_seletor
# so' cresce quando ha fence do MEC pendente ainda nao vista.
MP_MAGIC = 0x31504D42
# DBG.1 (doc 704), so' em modo debug: BPA1 = contadores cumulativos do validador
# de endereco fisico das folhas de memoria de sistema; BPV1 = amostras (tipo 1 =
# violacao, tipo 2 = endereco cru de folha do segmento 2), tag = seg<<8 | classe
# (0 RAM, 1 fora da RAM, 2 atravessa, 3 carve-out da VRAM, 4 abaixo de 1 MiB, 5 sem tabela).
PA_MAGIC = 0x31415042
PA_FIELDS = ("pte_sistema_conferidas", "seg0", "seg2", "fora_da_ram", "carve_out_vram",
             "abaixo_1mib", "sem_tabela", "reservado")
PV_MAGIC = 0x31565042
PV_FIELDS = ("tipo", "tag", "pa_lo", "pa_hi")


def summarize_pa_samples(records: list) -> list:
    out = []
    for r in records:
        if r.get("phase") != "DEPOIS" or "pa_lo" not in r:
            continue
        tag = int(r["tag"], 16)
        out.append({"writer_tick": r["writer_tick"],
                    "tipo": "violacao" if int(r["tipo"], 16) == 1 else "seg2_cru",
                    "segmento": tag >> 8, "classe": tag & 0xFF,
                    "pa": f"0x{(int(r['pa_hi'], 16) << 32) | int(r['pa_lo'], 16):X}"})
    return out
MP_FIELDS = ("polls_mec", "polls_com_seletor", "polls_sem_mmio")


# P1 (doc 594): BEV1 -- eventos de submissao/retirada do espelho em RAM, um por
# registro de palavras. Substitui o anel EVT1 da pagina na VRAM (nao armada na P1).
EV_MAGIC = 0x31564542
EV_FIELDS = ("seq", "kind_word", "bytes", "addr_lo", "addr_hi", "fence_lo", "qpc_lo")
EV_KINDS = {1: "PAGING_FILL", 2: "PAGING_TRANSFER", 3: "PAGING_DISCARD", 4: "SUBMIT_SDMA",
            5: "SUBMIT_GFX", 6: "RETIRE_SDMA", 7: "RETIRE_GFX", 8: "PAGING_PTE",
            9: "PAGING_OP", 10: "PAGING_DW", 11: "SPLIT_REC", 12: "SUBMIT_SPLIT"}

# DBG.2 (doc 707): nomes das operacoes do DXGK_BUILDPAGINGBUFFER (d3dkmddi.h).
DXGK_OPS = {1: "TRANSFER", 2: "FILL", 3: "DISCARD", 4: "READ_PHYS_CFG", 5: "WRITE_PHYS_CFG",
            6: "MAP_APERTURE", 7: "UNMAP_APERTURE", 8: "SPECIAL_LOCK_TRANSFER",
            9: "VIRTUAL_TRANSFER", 10: "VIRTUAL_FILL", 11: "UPDATE_PAGE_TABLE",
            12: "FLUSH_TLB", 13: "UPDATE_CONTEXT_ALLOCATION", 14: "NOTIFY_RESIDENCY",
            15: "SIGNAL_MONITORED_FENCE"}
# Opcode SDMA (byte baixo do cabecalho), so' os da fonte unica
# driver/core/bc250_sdma_packet_builder.h; o resto sai como opN.
SDMA_OPS = {0: "NOP", 1: "COPY", 2: "WRITE", 4: "INDIRECT", 5: "FENCE", 6: "TRAP",
            7: "SEM", 8: "POLL_REGMEM", 11: "CONST_FILL", 14: "SRBM_WRITE", 17: "GCR_REQ"}


def all_events(records: list) -> list:
    """Eventos BEV1 do boot da corrida, em ordem de seq (para no recuo)."""
    rows = cut_at_reboot([r for r in records if r.get("phase") == "DEPOIS" and "seq" in r])
    out = {}
    for r in rows:
        kw = int(r["kind_word"], 16)
        seq = int(r["seq"], 16)
        out[seq] = {
            "seq": seq, "kind": EV_KINDS.get(kw >> 24, f"KIND_{kw >> 24}"),
            "node": (kw >> 16) & 0xFF, "op": kw & 0xFFFF,
            "bytes": int(r["bytes"], 16),
            "addr": (int(r["addr_hi"], 16) << 32) | int(r["addr_lo"], 16),
            "fence": int(r["fence_lo"], 16), "qpc_lo": int(r["qpc_lo"], 16),
        }
    return [out[k] for k in sorted(out)]


def reconstruct_ibs(events: list, last: int = 6) -> list | None:
    """DBG.2: remonta os ultimos IBs do SDMA0 a partir de PAGING_OP/PAGING_DW
    (o que o build escreveu e onde), SPLIT_REC e SUBMIT_SPLIT."""
    ops = []
    by_link = {}
    for e in events:
        if e["kind"] == "PAGING_OP":
            op = {"seq": e["seq"], "op": DXGK_OPS.get(e["op"], e["op"]), "va": e["addr"],
                  "bytes": e["bytes"], "status": f"0x{e['fence']:08X}", "dw": []}
            ops.append(op)
            by_link[e["node"]] = op
        elif e["kind"] == "PAGING_DW" and e["node"] in by_link:
            op = by_link[e["node"]]
            if e["op"] == len(op["dw"]):
                op["dw"] += [e["bytes"], e["addr"] & 0xFFFFFFFF, e["addr"] >> 32, e["fence"]]
    if not ops:
        return None
    splits_rec = [e for e in events if e["kind"] == "SPLIT_REC"]
    out = []
    subs = [e for e in events if e["kind"] == "SUBMIT_SDMA"]
    retired = {e["fence"] for e in events if e["kind"] == "RETIRE_SDMA"}
    for s in subs[-last:]:
        ib_va, ib_bytes = s["addr"], s["bytes"]
        # O VidMm reusa paging buffers: so' contam as operacoes montadas depois
        # do envio anterior que cobria o mesmo intervalo.
        prev = max((p["seq"] for p in subs if p["seq"] < s["seq"]
                    and p["addr"] < ib_va + ib_bytes and ib_va < p["addr"] + p["bytes"]),
                   default=0)
        inside = [o for o in ops if o["va"] and ib_va <= o["va"] <= ib_va + ib_bytes
                  and prev < o["seq"] < s["seq"]]
        body = []
        for o in sorted(inside, key=lambda x: (x["va"], x["seq"])):
            dws = o["dw"][: max(0, o["bytes"] // 4)]
            body.append({
                "dw": (o["va"] - ib_va) // 4, "op": o["op"], "bytes": o["bytes"],
                "status": o["status"],
                "sdma": [SDMA_OPS.get(d & 0xFF, f"op{d & 0xFF}") for d in dws[:1]],
                "dwords": [f"0x{d:08X}" for d in dws],
            })
        header = [e for e in events if e["kind"] == "SUBMIT_SPLIT" and e["node"] == 0xFF
                  and e["addr"] == ib_va and e["seq"] < s["seq"]]
        split_info = None
        if header:
            h = header[-1]
            n = h["op"] & 0xFF
            pts = [e for e in events if e["kind"] == "SUBMIT_SPLIT" and e["node"] != 0xFF
                   and e["addr"] == ib_va and h["seq"] < e["seq"] < s["seq"]]
            split_info = {"pontos": n, "orfaos": h["op"] >> 8,
                          "mascara_antes_do_ib": f"0x{h['bytes']:04X}",
                          "divisoes": [{"dw": p["bytes"], "mascara": f"0x{p['fence']:04X}"} for p in pts[:n]]}
        flushes = [{"dw": (e["addr"] - ib_va) // 4, "mascara": f"0x{e['bytes']:04X}"}
                   for e in splits_rec if e["addr"] and ib_va <= e["addr"] <= ib_va + ib_bytes
                   and prev < e["seq"] < s["seq"]]
        out.append({"fence": s["fence"], "va": f"0x{ib_va:X}", "bytes": ib_bytes,
                    "aposentado": s["fence"] in retired, "operacoes": body,
                    "flushtlb_no_buffer": flushes, "envio": split_info})
    return out


def summarize_events(records: list, last: int = 24) -> dict | None:
    events = {}
    for r in records:
        if r.get("phase") != "DEPOIS" or "seq" not in r:
            continue
        seq = int(r["seq"], 16)
        kw = int(r["kind_word"], 16)
        events[seq] = {
            "seq": seq,
            "writer_tick": r["writer_tick"],
            "kind": EV_KINDS.get(kw >> 24, f"KIND_{kw >> 24}"),
            "node": (kw >> 16) & 0xFF,
            "op": f"0x{kw & 0xFFFF:X}",
            "bytes": int(r["bytes"], 16),
            "addr": f"0x{(int(r['addr_hi'], 16) << 32) | int(r['addr_lo'], 16):X}",
            "fence": int(r["fence_lo"], 16),
            "qpc_lo": int(r["qpc_lo"], 16),
        }
    if not events:
        return None
    ordered = [events[k] for k in sorted(events)]
    return {"events": len(ordered), "last_seq": ordered[-1]["seq"],
            "last_writer_tick": ordered[-1]["writer_tick"], "last_events": ordered[-last:]}


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def decode_record(data: bytes) -> dict:
    if len(data) != RECORD_SIZE:
        raise ValueError("registro incompleto")
    magic, version, count, active, latest, reserved, timestamp = HEADER.unpack_from(data)
    checksum, padding = TRAILER.unpack_from(data, RECORD_SIZE - TRAILER.size)
    # v1: Reserved=0. v2 (DX.65): Reserved = batimento do escritor, gravado a
    # cada periodo mesmo sem paginacao nova.
    if magic != MAGIC or version not in (1, 2) or count != 16 or padding or \
            (version == 1 and reserved):
        raise ValueError("cabecalho ou layout invalido")
    if checksum != fnv1a(data[: RECORD_SIZE - TRAILER.size]):
        raise ValueError("checksum divergente")
    slots = []
    for index in range(count):
        seq, stage, operation, status, thread, when = SLOT.unpack_from(
            data, HEADER.size + index * SLOT.size
        )
        if seq:
            slot = {
                "sequence": seq,
                "stage": stage,
                "stage_name": STAGE_NAMES.get(stage, "UNKNOWN"),
                "operation": operation,
                "status": f"0x{status & 0xFFFFFFFF:08X}",
                "thread": f"0x{thread:X}",
                "time_100ns": when,
            }
            # DX.85+: o estagio 44 carrega (M << 16) | N do commit do journal.
            # Binarios DX.83/DX.84 gravavam STATUS_PENDING (0x103) aqui.
            if stage == 44 and (status & 0xFFFFFFFF) != 0x103:
                slot["commit_changes_m"] = (status >> 16) & 0xFFFF
                slot["commit_live_ranges_n"] = status & 0xFFFF
            slots.append(slot)
    return {
        "version": version,
        "writer_tick": reserved if version == 2 else None,
        "latest_sequence": latest,
        "active_count": active,
        "snapshot_time_100ns": timestamp,
        "slots": slots,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--last", type=int, default=5, help="valid snapshots to print")
    args = parser.parse_args()
    valid = []
    invalid = []
    contents = args.path.read_bytes()
    magic = struct.pack("<I", MAGIC)
    offset = 0
    while offset < len(contents):
        candidate = contents.find(magic, offset)
        if candidate < 0:
            if offset < len(contents):
                invalid.append({"file_offset": offset, "reason": "bytes sem registro valido"})
            break
        if candidate > offset:
            invalid.append({"file_offset": offset, "reason": f"{candidate - offset} bytes ignorados"})
        data = contents[candidate : candidate + RECORD_SIZE]
        try:
            record = decode_record(data)
            record["file_offset"] = candidate
            valid.append(record)
            offset = candidate + RECORD_SIZE
        except ValueError as error:
            invalid.append({"file_offset": candidate, "reason": str(error)})
            offset = candidate + len(magic)
    # DX.87: registros de migalhas. Um acesso em fase ANTES que aparece com a
    # mesma sequencia nos ultimos registros e' o candidato a acesso que parou.
    crumbs = []
    crumb_magic = struct.pack("<I", CRUMB_MAGIC)
    position = contents.find(crumb_magic)
    while position >= 0:
        try:
            crumbs.append(decode_crumb_record(contents[position : position + CRUMB_SIZE]))
            position = contents.find(crumb_magic, position + CRUMB_SIZE)
        except ValueError:
            position = contents.find(crumb_magic, position + 4)
    crumb_summary = None
    if crumbs:
        last_crumb = crumbs[-1]
        persistent = []
        for item in last_crumb["in_flight"]:
            streak = 0
            for record in reversed(crumbs):
                if any(o["sequence"] == item["sequence"] and o["cpu"] == item["cpu"]
                       for o in record["in_flight"]):
                    streak += 1
                else:
                    break
            persistent.append(dict(item, records_in_flight=streak))
        crumb_summary = {
            "crumb_records": len(crumbs),
            "last_writer_tick": last_crumb["writer_tick"],
            "last_sequence": last_crumb["last_sequence"],
            "in_flight_at_last_record": persistent,
        }
    # DX.90: batimentos do GPU, na ordem do arquivo.
    gpu_heartbeats = []
    gpuhb_magic = struct.pack("<I", GPUHB_MAGIC)
    position = contents.find(gpuhb_magic)
    while position >= 0:
        try:
            gpu_heartbeats.append(decode_gpu_heartbeat(contents[position : position + GPUHB_SIZE]))
            position = contents.find(gpuhb_magic, position + GPUHB_SIZE)
        except ValueError:
            position = contents.find(gpuhb_magic, position + 4)
    # I.PLAT: amostras de plataforma, na ordem do arquivo.
    platform_samples = []
    plat_magic = struct.pack("<I", PLAT_MAGIC)
    position = contents.find(plat_magic)
    while position >= 0:
        try:
            platform_samples.append(decode_platform_sample(contents[position : position + GPUHB_SIZE]))
            position = contents.find(plat_magic, position + GPUHB_SIZE)
        except ValueError:
            position = contents.find(plat_magic, position + 4)
    # DX.65: batimento depois da ultima paginacao. So' registros v2 com a
    # MESMA latest_sequence no fim, em sequencia de writer_tick, contam.
    heartbeat = None
    boot_valid = [r for r in valid if r["latest_sequence"] != 0]
    if boot_valid and boot_valid[-1]["version"] == 2:
        last = boot_valid[-1]
        tail = [last]
        for record in reversed(boot_valid[:-1]):
            if record["version"] != 2 or record["latest_sequence"] != last["latest_sequence"]:
                break
            tail.append(record)
        tail.reverse()
        heartbeat = {
            "latest_sequence": last["latest_sequence"],
            "records_after_last_paging": len(tail) - 1,
            "writer_alive_ms_after_last_paging":
                (tail[-1]["snapshot_time_100ns"] - tail[0]["snapshot_time_100ns"]) / 10000.0,
            "last_writer_tick": last["writer_tick"],
        }
    print(
        json.dumps(
            {
                "path": str(args.path),
                "record_size": RECORD_SIZE,
                "valid_records": len(valid),
                "invalid_records": invalid[-5:],
                "heartbeat_after_last_paging": heartbeat,
                "mmio_crumbs": crumb_summary,
                "gpu_heartbeat": summarize_gpu_heartbeats(gpu_heartbeats),
                "platform_sample": summarize_platform_samples(platform_samples),
                "sdma0_sample": summarize_words(scan_words(contents, SQ_MAGIC, SQ_FIELDS)),
                "clock_gating_sample": summarize_words(scan_words(contents, CG_MAGIC, CG_FIELDS)),
                "paging_stats": summarize_paging_stats(scan_words(contents, PG_MAGIC, PG_FIELDS)),
                "flush_classes": summarize_flush_classes(scan_words(contents, FT_MAGIC, FT_FIELDS)),
                "mec_poll": summarize_flush_classes(scan_words(contents, MP_MAGIC, MP_FIELDS), MP_FIELDS),
                "pa_validator": summarize_flush_classes(scan_words(contents, PA_MAGIC, PA_FIELDS), PA_FIELDS),
                "pa_samples": summarize_pa_samples(scan_words(contents, PV_MAGIC, PV_FIELDS)),
                "gpu_events_ram": summarize_events(scan_words(contents, EV_MAGIC, EV_FIELDS)),
                "ib_reconstruido": reconstruct_ibs(all_events(scan_words(contents, EV_MAGIC, EV_FIELDS))),
                "last_snapshots": valid[-args.last :],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
