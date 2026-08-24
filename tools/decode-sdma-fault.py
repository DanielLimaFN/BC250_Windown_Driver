#!/usr/bin/env python3
"""Decodifica os dumps de `sdma fault` gravados por test-sdma-robustness.ps1.

Motivo de existir: os registradores de status da SDMA nao sao flags simples.
Medido em 2026-08-23 numa engine SAUDAVEL, ERROR_LOG ja vale 0x0000000F e os
XNACK ja sao diferentes de zero. Olhar valor bruto leva a diagnostico errado;
o que vale e comparar dois dumps campo a campo, com nomes de bit vindos do
header autoritativo do kernel.

Fonte dos nomes de bit:
  referencias/BC250-windowsDriverTest/docs/upstream/generated/
      gc_10_1_0_sh_mask_extract.h
que por sua vez foi extraido de gc_10_1_0_sh_mask.h do kernel, com o SHA-256 do
arquivo completo registrado no cabecalho do extrato.

Uso:
  python tools/decode-sdma-fault.py <pasta-de-evidencia> [rotuloA rotuloB]

Sem rotulos, compara 'saudavel' com todos os outros dumps da mesma execucao.
"""
import json
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
EXTRATO = (RAIZ / 'referencias' / 'BC250-windowsDriverTest' / 'docs' / 'upstream' /
           'generated' / 'gc_10_1_0_sh_mask_extract.h')

# Nome do campo no dump do CLI -> registrador do header.
CAMPO_PARA_REG = {
    'STATUS_REG': 'SDMA0_STATUS_REG',
    'STATUS1_REG': 'SDMA0_STATUS1_REG',
    'STATUS2_REG': 'SDMA0_STATUS2_REG',
    'STATUS3_REG': 'SDMA0_STATUS3_REG',
    'FREEZE': 'SDMA0_FREEZE',
    'ERROR_LOG': 'SDMA0_ERROR_LOG',
    'EDC_COUNTER': 'SDMA0_EDC_COUNTER',
    'POWER_CNTL': 'SDMA0_POWER_CNTL',
    'CNTL': 'SDMA0_CNTL',
    'CHICKEN_BITS': 'SDMA0_CHICKEN_BITS',
    'UTCL1_CNTL': 'SDMA0_UTCL1_CNTL',
    'UTCL1_RD_STAT': 'SDMA0_UTCL1_RD_STATUS',
    'UTCL1_WR_STAT': 'SDMA0_UTCL1_WR_STATUS',
    'UTCL1_TIMEOUT': 'SDMA0_UTCL1_TIMEOUT',
    'UTCL1_PAGE': 'SDMA0_UTCL1_PAGE',
    'RB_CNTL': 'SDMA0_GFX_RB_CNTL',
}


def carregar_tabelas():
    """Le o extrato e devolve {registrador: [(campo, shift, mask), ...]}."""
    if not EXTRATO.exists():
        sys.exit('extrato de bits ausente: %s' % EXTRATO)
    shifts, masks = {}, {}
    for linha in EXTRATO.read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.match(r'#define\s+([A-Z0-9_]+)__([A-Z0-9_]+)__SHIFT\s+(0x[0-9a-fA-F]+)', linha)
        if m:
            shifts.setdefault(m.group(1), {})[m.group(2)] = int(m.group(3), 16)
            continue
        m = re.match(r'#define\s+([A-Z0-9_]+)__([A-Z0-9_]+)_MASK\s+(0x[0-9a-fA-F]+)L?', linha)
        if m:
            masks.setdefault(m.group(1), {})[m.group(2)] = int(m.group(3), 16)
    tabelas = {}
    for reg, campos in shifts.items():
        tabelas[reg] = sorted(
            ((c, s, masks.get(reg, {}).get(c, 1 << s)) for c, s in campos.items()),
            key=lambda t: t[1])
    return tabelas


def decodificar(tabelas, reg, valor):
    """Devolve {campo: valor_do_campo} para os campos diferentes de zero."""
    if reg not in tabelas:
        return None
    saida = {}
    for campo, shift, mask in tabelas[reg]:
        v = (valor & mask) >> shift
        if v:
            saida[campo] = v
    return saida


def primeiro_hex(texto):
    m = re.search(r'0x([0-9A-Fa-f]{8})', texto or '')
    return int(m.group(1), 16) if m else None


def comparar(tabelas, rotulo_a, dump_a, rotulo_b, dump_b):
    print('=' * 78)
    print('%s  x  %s' % (rotulo_a, rotulo_b))
    print('=' * 78)
    mudou = 0
    for campo, reg in CAMPO_PARA_REG.items():
        va, vb = primeiro_hex(dump_a.get(campo)), primeiro_hex(dump_b.get(campo))
        if va is None or vb is None or va == vb:
            continue
        mudou += 1
        print('\n%-14s 0x%08X -> 0x%08X   (xor 0x%08X)' % (campo, va, vb, va ^ vb))
        da, db = decodificar(tabelas, reg, va), decodificar(tabelas, reg, vb)
        if da is None:
            print('    (sem tabela de bits para %s)' % reg)
            continue
        for nome in sorted(set(da) | set(db)):
            a, b = da.get(nome, 0), db.get(nome, 0)
            if a != b:
                print('    %-34s %s -> %s' % (nome, a, b))
    if not mudou:
        print('nenhum campo com tabela de bits mudou entre os dois dumps.')
    print()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    pasta = Path(sys.argv[1])
    arquivo = pasta if pasta.is_file() else pasta / 'result.json'
    dados = json.loads(arquivo.read_text(encoding='utf-8-sig'))
    dumps = dados.get('fault_dumps') or {}
    if not dumps:
        sys.exit('esta evidencia nao contem fault_dumps (exige ABI 7 ou superior).')

    tabelas = carregar_tabelas()

    if len(sys.argv) >= 4:
        a, b = sys.argv[2], sys.argv[3]
        comparar(tabelas, a, dumps[a], b, dumps[b])
        return

    base = 'saudavel' if 'saudavel' in dumps else list(dumps)[0]
    print('referencia: %s\n' % base)
    for rotulo, dump in dumps.items():
        if rotulo != base:
            comparar(tabelas, base, dumps[base], rotulo, dump)

    print('-' * 78)
    print('Estado decodificado da referencia %s:' % base)
    for campo, reg in CAMPO_PARA_REG.items():
        v = primeiro_hex(dumps[base].get(campo))
        if v is None:
            continue
        d = decodificar(tabelas, reg, v)
        if d:
            print('  %-14s 0x%08X  %s' % (campo, v, ' '.join('%s=%s' % kv for kv in d.items())))


if __name__ == '__main__':
    main()
