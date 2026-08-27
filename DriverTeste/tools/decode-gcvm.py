#!/usr/bin/env python3
"""Decodifica o dump de `gcvm dump` e compara com o que o upstream exige.

Por que existe: toda a execucao de SDMA demonstrada neste projeto acontece sem
page table nossa. O driver definitivo precisa montar VMID 0 de verdade, e o
primeiro passo e saber, campo a campo, o que o POST deixou configurado e o que
`gfxhub_v2_0_gart_enable()` do upstream programaria diferente.

Este script nao escreve nada e nao acessa hardware: ele le a saida textual do
comando `bc250ctl gcvm dump` e cruza com duas fontes:

  - nomes e posicoes de bit: gc_10_1_0_sh_mask_extract.h, extraido do header
    autoritativo do kernel (SHA-256 no cabecalho do extrato);
  - valores exigidos: gfxhub_v2_0.c do upstream, funcao por funcao.

Uso:
  bc250ctl gcvm dump > dump.txt
  python tools/decode-gcvm.py dump.txt
"""
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
EXTRATO = (RAIZ / 'referencias' / 'BC250-windowsDriverTest' / 'docs' / 'upstream' /
           'generated' / 'gc_10_1_0_sh_mask_extract.h')

# Campo impresso pelo CLI -> registrador do header.
CAMPO_PARA_REG = {
    'L2_CNTL': 'GCVM_L2_CNTL',
    'L2_CNTL2': 'GCVM_L2_CNTL2',
    'L2_CNTL3': 'GCVM_L2_CNTL3',
    'L2_CNTL4': 'GCVM_L2_CNTL4',
    'L2_CNTL5': 'GCVM_L2_CNTL5',
    'FAULT_CNTL': 'GCVM_L2_PROTECTION_FAULT_CNTL',
    'FAULT_CNTL2': 'GCVM_L2_PROTECTION_FAULT_CNTL2',
    'FAULT_STATUS': 'GCVM_L2_PROTECTION_FAULT_STATUS',
    'CONTEXT0_CNTL': 'GCVM_CONTEXT0_CNTL',
    'CONTEXT1_CNTL': 'GCVM_CONTEXT1_CNTL',
    'MX_L1_TLB_CNTL': 'GCMC_VM_MX_L1_TLB_CNTL',
    'INVALIDATE_ENG0_REQ': 'GCVM_INVALIDATE_ENG0_REQ',
    'INVALIDATE_ENG0_ACK': 'GCVM_INVALIDATE_ENG0_ACK',
}

# O que gfxhub_v2_0.c programa. Cada entrada: (campo, valor exigido, funcao de
# origem no upstream). Somente campos com valor explicito no codigo entram aqui;
# o resto e reportado como observacao, nunca como divergencia.
EXIGIDO = {
    'GCMC_VM_MX_L1_TLB_CNTL': [
        ('ENABLE_L1_TLB', 1, 'init_tlb_regs'),
        ('SYSTEM_ACCESS_MODE', 3, 'init_tlb_regs'),
        ('ENABLE_ADVANCED_DRIVER_MODEL', 1, 'init_tlb_regs'),
        ('SYSTEM_APERTURE_UNMAPPED_ACCESS', 0, 'init_tlb_regs'),
    ],
    'GCVM_L2_CNTL': [
        ('ENABLE_L2_CACHE', 1, 'init_cache_regs'),
        ('ENABLE_L2_FRAGMENT_PROCESSING', 0, 'init_cache_regs'),
        ('ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY', 1, 'init_cache_regs'),
        ('L2_PDE0_CACHE_TAG_GENERATION_MODE', 0, 'init_cache_regs'),
        ('PDE_FAULT_CLASSIFICATION', 0, 'init_cache_regs'),
        ('CONTEXT1_IDENTITY_ACCESS_MODE', 1, 'init_cache_regs'),
        ('IDENTITY_MODE_FRAGMENT_SIZE', 0, 'init_cache_regs'),
    ],
    'GCVM_L2_CNTL3': [
        ('BANK_SELECT', 9, 'init_cache_regs (translate_further=0)'),
        ('L2_CACHE_BIGK_FRAGMENT_SIZE', 6, 'init_cache_regs (translate_further=0)'),
    ],
    'GCVM_L2_CNTL4': [
        ('VMC_TAP_PDE_REQUEST_PHYSICAL', 0, 'init_cache_regs'),
        ('VMC_TAP_PTE_REQUEST_PHYSICAL', 0, 'init_cache_regs'),
    ],
    'GCVM_L2_CNTL5': [
        ('L2_CACHE_SMALLK_FRAGMENT_SIZE', 0, 'init_cache_regs'),
    ],
    'GCVM_CONTEXT0_CNTL': [
        ('ENABLE_CONTEXT', 1, 'enable_system_domain'),
        ('PAGE_TABLE_DEPTH', 0, 'enable_system_domain'),
        ('RETRY_PERMISSION_OR_INVALID_PAGE_FAULT', 0, 'enable_system_domain'),
    ],
}


def carregar_tabelas():
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
    return shifts, masks


def ler_dump(texto):
    """Extrai {campo: valor} da saida do CLI. Aceita valores de 32 e 64 bits."""
    valores = {}
    for linha in texto.splitlines():
        m = re.match(r'\s+([A-Z0-9_]+)\s*:\s*0x([0-9A-Fa-f]+)\s*$', linha)
        if m:
            valores[m.group(1)] = int(m.group(2), 16)
    return valores


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    texto = Path(sys.argv[1]).read_text(encoding='utf-8', errors='replace')
    valores = ler_dump(texto)
    if not valores:
        sys.exit('nenhum registrador reconhecido na entrada.')
    shifts, masks = carregar_tabelas()

    print('=' * 78)
    print('Estado decodificado (o que o POST deixou)')
    print('=' * 78)
    for campo, valor in valores.items():
        reg = CAMPO_PARA_REG.get(campo)
        print('\n%-22s 0x%08X' % (campo, valor))
        if not reg or reg not in shifts:
            continue
        for nome, sh in sorted(shifts[reg].items(), key=lambda kv: kv[1]):
            mk = masks.get(reg, {}).get(nome, 1 << sh)
            v = (valor & mk) >> sh
            if v:
                print('    %-44s %s' % (nome, v))

    print()
    print('=' * 78)
    print('Comparacao com gfxhub_v2_0.c do upstream')
    print('=' * 78)
    divergencias = 0
    ausentes = 0
    for campo, reg in CAMPO_PARA_REG.items():
        if reg not in EXIGIDO:
            continue
        if campo not in valores:
            print('\n%-22s AUSENTE no dump' % campo)
            ausentes += 1
            continue
        valor = valores[campo]
        print('\n%-22s 0x%08X' % (campo, valor))
        for nome, exigido, origem in EXIGIDO[reg]:
            sh = shifts.get(reg, {}).get(nome)
            if sh is None:
                print('    %-44s campo ausente no header' % nome)
                ausentes += 1
                continue
            mk = masks.get(reg, {}).get(nome, 1 << sh)
            atual = (valor & mk) >> sh
            if atual == exigido:
                print('    %-44s %s == exigido' % (nome, atual))
            else:
                print('    %-44s %s != %s   <-- %s' % (nome, atual, exigido, origem))
                divergencias += 1

    print()
    print('-' * 78)
    print('Divergencias contra o upstream: %d' % divergencias)
    if ausentes:
        print('Campos nao avaliados (ausentes no dump ou no header): %d' % ausentes)
    print()
    print('Leitura correta deste resultado: divergencia NAO significa defeito. O')
    print('POST configurou a placa para o proprio uso, nao para o modelo de driver')
    print('do amdgpu. A lista acima e o escopo de trabalho de montar VMID 0, nao')
    print('uma lista de erros.')
    print()
    print('O que este script NAO responde: se as page tables existem e onde. As')
    print('bases de CONTEXT0 apontam para memoria que ninguem inspecionou ainda.')


if __name__ == '__main__':
    main()
