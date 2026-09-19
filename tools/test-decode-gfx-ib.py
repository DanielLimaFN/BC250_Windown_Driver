#!/usr/bin/env python3
"""Suite offline do decodificador de IB grafico.

Existe porque o decodificador vai ser a lente pela qual leremos o stream que
travou o CP. Lente sem teste vira fonte de conclusao errada -- e este projeto ja
publicou conclusao errada por decodificar registrador com a tabela do vizinho.
"""
from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("decode_gfx_ib", ROOT / "tools" / "decode-gfx-ib.py")
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

failures: list[str] = []


def check(cond: bool, label: str) -> None:
    if cond:
        print(f"  OK   {label}")
    else:
        print(f"  FAIL {label}")
        failures.append(label)


def t3(opcode: int, payload_dw: int) -> int:
    return (3 << 30) | (((payload_dw - 1) & 0x3FFF) << 16) | ((opcode & 0xFF) << 8)


print("[TEST] cabecalho tipo 3: opcode, contagem e nome")
pkts = mod.decode([t3(0x2D, 2), 0x00000003, 0x00000002])
check(len(pkts) == 1, "um pacote")
check(pkts[0]["opcode"] == 0x2D, "opcode 0x2D extraido")
check(pkts[0]["name"] == "DRAW_INDEX_AUTO", "nome resolvido")
check(pkts[0]["payload_dw"] == 2, "payload de 2 DW")
check(pkts[0]["total_dw"] == 3, "total de 3 DW")

print("[TEST] o preambulo MEDIDO na corrida 34 decodifica como o log do KMD disse")
# GfxIbInspect publicou DW0=C0004A00 DW1=00000000 DW2=C0012800 DW3=80000000
# O log so imprimiu DW0..DW3; CONTEXT_CONTROL declara 2 DW de payload, entao os
# quatro dwords sozinhos sao um stream INCOMPLETO -- e o decodificador tem de
# dizer isso, nao completar por conta propria. Esta assercao nasceu de um erro
# meu no fixture, que a suite pegou.
medido_parcial = [0xC0004A00, 0x00000000, 0xC0012800, 0x80000000]
pkts = mod.decode(medido_parcial)
check(pkts[0]["name"] == "PREAMBLE_CNTL", "primeiro e PREAMBLE_CNTL (0x4A)")
check(pkts[1]["name"] == "CONTEXT_CONTROL", "segundo e CONTEXT_CONTROL (0x28)")
check(pkts[1].get("truncated") is True,
      "os 4 DW do log sao incompletos e o decodificador NAO completa sozinho")

# Stream completo: CONTEXT_CONTROL com os dois dwords que o PM4 exige.
medido = [0xC0004A00, 0x00000000, 0xC0012800, 0x80000000, 0x80000000]
pkts = mod.decode(medido)
check(len(pkts) == 2, "dois pacotes no preambulo completo")
check(pkts[1]["payload"] == [0x80000000, 0x80000000], "payload do CONTEXT_CONTROL preservado")
check(pkts[1]["total_dw"] == 3, "CONTEXT_CONTROL ocupa 3 DW (cabecalho + 2)")

print("[TEST] SET_* resolve o endereco de registrador pela base certa")
pkts = mod.decode([t3(0x69, 2), 0x0000000B, 0x3F800000])
check(pkts[0]["reg"] == 0xA00B, "SET_CONTEXT_REG base 0xA000 + offset")
pkts = mod.decode([t3(0x76, 2), 0x00000007, 0x00000001])
check(pkts[0]["reg"] == 0x2C07, "SET_SH_REG base 0x2C00 + offset")
pkts = mod.decode([t3(0x79, 2), 0x00000242, 0x00000004])
check(pkts[0]["reg"] == 0xC242, "SET_UCONFIG_REG base 0xC000 + offset")

print("[TEST] opcode fora da tabela NAO ganha nome inventado")
pkts = mod.decode([t3(0xEE, 1), 0])
check(pkts[0]["name"] == "desconhecido", "opcode 0xEE sai como desconhecido")

print("[TEST] fail-closed: payload que ultrapassa o buffer e TRUNCADO e para a varredura")
# declara 8 DW de payload mas so ha 2
pkts = mod.decode([t3(0x69, 8), 0x1, 0x2])
check(len(pkts) == 1, "varredura parou no pacote malformado")
check(pkts[0].get("truncated") is True, "pacote marcado TRUNCADO")

print("[TEST] CONTRAPONTO: stream integro NAO pode ser marcado truncado")
# sem este caso, 'marcar tudo como truncado' passaria nos testes acima
pkts = mod.decode([t3(0x10, 1), 0x0, t3(0x2D, 2), 0x3, 0x2])
check(len(pkts) == 2, "dois pacotes reconhecidos")
check(all(not p.get("truncated") for p in pkts), "nenhum marcado truncado")

print("[TEST] PACKET2 (filler) e reconhecido e ocupa 1 DW")
pkts = mod.decode([(2 << 30), t3(0x10, 1), 0x0])
check(pkts[0]["type"] == 2, "tipo 2 identificado")
check(pkts[0]["total_dw"] == 1, "PACKET2 ocupa 1 DW")

print("[TEST] tipo inesperado interrompe em vez de adivinhar")
pkts = mod.decode([0x00000000])
check(pkts[0].get("unexpected") is True, "tipo 0 marcado inesperado")

print()
if failures:
    print(f"REPROVADO: {len(failures)} assercao(oes)")
    sys.exit(1)
print("DECODE GFX IB: OK (offline)")
print("verdict: NAO VALIDADO EM HARDWARE")
