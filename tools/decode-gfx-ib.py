#!/usr/bin/env python3
"""Decodifica um IB grafico PM4 despejado pelo RADV (BC250_RADV_IB_DUMP).

POR QUE EXISTE
==============
Na Corrida 34 (19/09/2026) o CP_RB0 parou dentro de um IB de 1008 DW e nao
haviamos guardado um dword dele. O miniport confirmou que o fluxo era linear e
bem formado -- 264 pacotes, zero CHAIN -- mas "bem formado" nao diz QUAL pacote
travou, e o diagnostico publicado saiu com tres hipoteses e nenhuma medicao que
as separasse.

O despejo vem do RADV; a decodificacao vive AQUI, de proposito: assim ela e'
testavel offline (tools/test-decode-gfx-ib.py) e nao depende de reconstruir o
Mesa para ser corrigida.

COMO SE USA COM A SONDA DO KMD
==============================
A sonda [TDR_GFX_PACKET] do miniport imprime, no quiesce do TDR:
  ME_HEADER_DUMP=0x........ Opcode=0x.. (NOME)
  IB1 Base=0x............ RestamDW=N
`--stopped-at-opcode 0x..` marca nesta listagem todo pacote com aquele opcode, e
`--remaining-dw N` marca o pacote apontado por CP_IB1_BUFSZ. O cruzamento dos
dois localiza o ponto de parada dentro do stream.

    python tools\\decode-gfx-ib.py bc250_ib_000_va...bin --stopped-at-opcode 0x2D

VEREDITO: decodificacao de bytes. Nao prova execucao. NAO VALIDADO EM HARDWARE.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# Tabela de opcodes PM4 tipo 3. Os 16 primeiros sao os mesmos de
# driver/core/bc250_pm4_packet_builder.h (fonte unica do lado do KMD); os
# demais aparecem em IBs emitidos pelo RADV. Opcode fora da tabela sai como
# "desconhecido" -- nao se inventa nome.
TYPE3_OPCODES = {
    0x10: "NOP",
    0x11: "SET_BASE",
    0x12: "CLEAR_STATE",
    0x13: "INDEX_BUFFER_SIZE",
    0x15: "DISPATCH_DIRECT",
    0x16: "DISPATCH_INDIRECT",
    0x1E: "SET_PREDICATION",
    0x22: "COND_EXEC",
    0x23: "PRED_EXEC",
    0x24: "DRAW_INDIRECT",
    0x25: "DRAW_INDEX_INDIRECT",
    0x26: "INDEX_BASE",
    0x27: "DRAW_INDEX_2",
    0x28: "CONTEXT_CONTROL",
    0x2A: "INDEX_TYPE",
    0x2C: "DRAW_INDEX_AUTO",
    0x2D: "DRAW_INDEX_AUTO",
    0x2F: "NUM_INSTANCES",
    0x33: "INDIRECT_BUFFER_CONST",
    0x36: "PFP_SYNC_ME",
    0x37: "WRITE_DATA",
    0x3C: "WAIT_REG_MEM",
    0x3F: "INDIRECT_BUFFER",
    0x40: "COPY_DATA",
    0x43: "SURFACE_SYNC",
    0x46: "EVENT_WRITE",
    0x47: "EVENT_WRITE_EOP",
    0x49: "RELEASE_MEM",
    0x4A: "PREAMBLE_CNTL",
    0x50: "DMA_DATA",
    0x58: "ACQUIRE_MEM",
    0x68: "SET_CONFIG_REG",
    0x69: "SET_CONTEXT_REG",
    0x76: "SET_SH_REG",
    0x79: "SET_UCONFIG_REG",
    0x7A: "LOAD_CONTEXT_REG",
}

# Bases de registrador por tipo de pacote SET_*, para transformar o offset do
# payload em endereco de registrador. Iguais as de bc250_pm4_packet_builder.h.
SET_REG_BASE = {
    0x68: 0x2000,
    0x69: 0xA000,
    0x76: 0x2C00,
    0x79: 0xC000,
}


def opcode_name(opcode: int) -> str:
    return TYPE3_OPCODES.get(opcode, "desconhecido")


def decode(dwords: list[int]) -> list[dict]:
    """Varre o stream e devolve um pacote por entrada.

    Fail-closed: pacote cujo payload declarado ultrapassa o fim do buffer e'
    marcado TRUNCADO e a varredura para. Silenciar isso transformaria um IB
    corrompido numa listagem de aparencia saudavel, que e' exatamente a classe
    de erro que este projeto ja pagou varias vezes.
    """
    out: list[dict] = []
    i = 0
    n = len(dwords)
    while i < n:
        header = dwords[i]
        ptype = (header >> 30) & 0x3
        pkt = {"index": i, "header": header, "type": ptype}
        if ptype == 3:
            opcode = (header >> 8) & 0xFF
            count = ((header >> 16) & 0x3FFF) + 1
            pkt["opcode"] = opcode
            pkt["name"] = opcode_name(opcode)
            pkt["payload_dw"] = count
            pkt["total_dw"] = count + 1
            if i + count + 1 > n:
                pkt["truncated"] = True
                out.append(pkt)
                break
            pkt["payload"] = dwords[i + 1:i + 1 + count]
            if opcode in SET_REG_BASE and count >= 1:
                pkt["reg"] = SET_REG_BASE[opcode] + pkt["payload"][0]
            i += count + 1
        elif ptype == 2:
            pkt["name"] = "PACKET2 (filler)"
            pkt["total_dw"] = 1
            i += 1
        else:
            # Tipo 0 e tipo 1 nao aparecem em IB de GFX10; se aparecerem, a
            # varredura para e diz que parou, em vez de seguir adivinhando.
            pkt["name"] = f"TIPO {ptype} INESPERADO"
            pkt["total_dw"] = 1
            pkt["unexpected"] = True
            out.append(pkt)
            break
        out.append(pkt)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", type=Path, help="arquivo .bin despejado por BC250_RADV_IB_DUMP")
    ap.add_argument("--stopped-at-opcode", type=lambda v: int(v, 0), default=None,
                    help="opcode lido em CP_ME_HEADER_DUMP pela sonda do KMD")
    ap.add_argument("--remaining-dw", type=int, default=None,
                    help="valor de CP_IB1_BUFSZ no momento do stall")
    ap.add_argument("--max-payload", type=int, default=6,
                    help="dwords de payload mostrados por pacote (0 = nenhum)")
    args = ap.parse_args()

    if not args.dump.is_file():
        print(f"RECUSADO: arquivo inexistente: {args.dump}")
        return 2
    raw = args.dump.read_bytes()
    if len(raw) % 4 != 0:
        print(f"RECUSADO: {len(raw)} bytes nao e multiplo de 4 -- nao e um stream de dwords")
        return 2
    dwords = list(struct.unpack(f"<{len(raw)//4}I", raw))

    packets = decode(dwords)
    total_dw = len(dwords)
    stop_index = None
    if args.remaining_dw is not None and 0 <= args.remaining_dw <= total_dw:
        stop_index = total_dw - args.remaining_dw

    print(f"arquivo : {args.dump.name}")
    print(f"tamanho : {total_dw} DW ({len(raw)} bytes)")
    print(f"pacotes : {len(packets)}")
    if stop_index is not None:
        print(f"CP_IB1_BUFSZ={args.remaining_dw} -> o CP havia consumido ate o DW {stop_index}")
    print()

    truncated = [p for p in packets if p.get("truncated") or p.get("unexpected")]
    hits = 0
    for p in packets:
        mark = "  "
        if args.stopped_at_opcode is not None and p.get("opcode") == args.stopped_at_opcode:
            mark = ">>"
            hits += 1
        if stop_index is not None and p["index"] <= stop_index < p["index"] + p.get("total_dw", 1):
            mark = "**"
        line = (f"{mark} [{p['index']:5d}] 0x{p['header']:08X} "
                f"{p.get('name', '?'):<22}")
        if "opcode" in p:
            line += f" op=0x{p['opcode']:02X} dw={p.get('payload_dw', 0)}"
        if "reg" in p:
            line += f" reg=0x{p['reg']:04X}"
        if args.max_payload and p.get("payload"):
            shown = p["payload"][:args.max_payload]
            line += "  " + " ".join(f"{d:08X}" for d in shown)
            if len(p["payload"]) > len(shown):
                line += " ..."
        if p.get("truncated"):
            line += "   <<< TRUNCADO: payload ultrapassa o fim do buffer"
        if p.get("unexpected"):
            line += "   <<< TIPO INESPERADO: varredura interrompida"
        print(line)

    print()
    if args.stopped_at_opcode is not None:
        print(f"opcode 0x{args.stopped_at_opcode:02X} "
              f"({opcode_name(args.stopped_at_opcode)}) marcado com '>>': {hits} ocorrencia(s)")
        if hits == 0:
            print("  ATENCAO: o opcode que o ME reportou NAO aparece neste IB. "
                  "Ou o despejo e de outra submissao, ou o ME parou fora deste IB.")
    if stop_index is not None:
        print("o pacote marcado com '**' contem o ponto que CP_IB1_BUFSZ indica")
    if truncated:
        print(f"REPROVADO: {len(truncated)} pacote(s) malformado(s) -- o stream nao e integro")
        return 1
    print("stream integro: todo pacote cabe dentro do buffer")
    print("verdict: NAO VALIDADO EM HARDWARE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
