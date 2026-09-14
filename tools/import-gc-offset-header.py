#!/usr/bin/env python3
"""Aceita (ou RECUSA) o gc_10_1_0_offset.h do upstream, provando-o contra as
nossas proprias medicoes no silicio.

POR QUE ISTO EXISTE
===================

Duas etapas do projeto estao paradas no MESMO arquivo ausente:

  B.1  o APPLY do anel grafico para em CP_RB0_ACTIVE, sem procedencia;
  B.2  SPI_SHADER_PGM_* e USER_DATA nao podem ser escritos pelo mesmo motivo.

inc/linux_ref/ so' tem gc_10_1_0_sh_mask.h, que traz LARGURAS de campo e nao
ENDERECOS. Inverter a formula do catalogo a partir dos bytes que o prototipo
usa seria lavar H em P1 -- o proprio byte veio de fonte desconhecida.

O QUE ESTE SCRIPT FAZ DE DIFERENTE
==================================

Nao confia no arquivo. Ele o submete a um teste que so' o header CERTO passa:
reproduzir, pela regra de calculo de docs/03, os enderecos que esta placa JA
confirmou no silicio.

    BAR5_byte = (segment_base_dw[BASE_IDX] + mmREG) * 4
    BASE_IDX 0 -> 0x1260      BASE_IDX 1 -> 0xA000

Se o header discordar de um endereco que ja medimos, ele nao e' deste ASIC --
ou a nossa regra esta errada. Nos dois casos o script RECUSA e nao escreve
nada. E' a mesma logica do tripe: o artefato tem de bater com a medicao, nao a
medicao com o artefato.

USO
===

    python tools/import-gc-offset-header.py CAMINHO/gc_10_1_0_offset.h
    python tools/import-gc-offset-header.py CAMINHO/... --install

Sem --install nada e' copiado: imprime o veredito e a tabela pronta para
docs/03. Com --install, copia para inc/linux_ref/ SOMENTE se todas as
ancoras passarem.

NAO VALIDA COMPORTAMENTO NO SILICIO. Aceitar o header eleva a classe do
ENDERECO de H para P1; o efeito de escrever nele continua sem prova.
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "inc" / "linux_ref" / "gc_10_1_0_offset.h"
SH_MASK = ROOT / "inc" / "linux_ref" / "gc_10_1_0_sh_mask.h"

# Regra de calculo de docs/03, secao "Regra de calculo".
SEGMENT_BASE_DW = {0: 0x1260, 1: 0xA000}

# ---------------------------------------------------------------------------
# ANCORAS. Cada linha e' um endereco que ESTA PLACA ja confirmou, com a fonte
# da confirmacao. O header tem de reproduzir todas.
# ---------------------------------------------------------------------------
ANCHORS = [
    # (nome, BASE_IDX, BAR5 byte esperado, procedencia da confirmacao)
    ("mmCP_ME_CNTL", 0, 0x086D8,
     "exemplo da regra de calculo em docs/03; lido 0x15000000 na corrida 14/09 11:43"),
    ("mmCP_RB0_CNTL", 0, 0x0C104,
     "lido 0x00A00000 na corrida 14/09 11:43 (P0)"),
    ("mmCP_RB0_BASE", 0, 0x0C100,
     "lido 0xFEDCBAEF na corrida 14/09 11:43 (P0)"),
    ("mmCP_RB0_RPTR", 0, 0x08700,
     "lido 0x00000000 na corrida 14/09 11:43 (P0)"),
    ("mmCP_RB0_WPTR", 0, 0x0C150,
     "lido 0x00000000 na corrida 14/09 11:43 (P0)"),
    ("mmCP_RB_WPTR_POLL_CNTL", 0, 0x08708,
     "lido 0x00400000 na corrida 14/09 11:43 (P0)"),
    ("mmCP_MEC_CNTL", 0, 0x08234,
     "docs/03: 'ja e escrito com sucesso no silicio -- ancora forte do offset'"),
    # Bloco de contexto GFX10, catalogado na etapa B.0 pela ordem do sh_mask.
    ("mmCB_COLOR0_BASE", 1, 0x28C60, "docs/03, bloco 'GFX10 3D'"),
    ("mmPA_SC_WINDOW_SCISSOR_TL", 1, 0x28204, "docs/03, bloco 'GFX10 3D'"),
]

# Registradores que o header vai DESBLOQUEAR, por etapa.
WANTED = {
    "B.1 -- partida do anel grafico": [
        "mmCP_RB0_ACTIVE", "mmCP_MAX_CONTEXT", "mmCP_RB_VMID",
        "mmCP_RB_WPTR_DELAY",
        "mmCP_PFP_IC_OP_CNTL", "mmCP_ME_IC_OP_CNTL",
        "mmCP_CE_IC_OP_CNTL", "mmCP_CPC_IC_OP_CNTL",
    ],
    "B.2 -- programa de shader": [
        "mmSPI_SHADER_PGM_LO_PS", "mmSPI_SHADER_PGM_HI_PS",
        "mmSPI_SHADER_PGM_RSRC1_PS", "mmSPI_SHADER_PGM_RSRC2_PS",
        "mmSPI_SHADER_PGM_LO_VS", "mmSPI_SHADER_PGM_HI_VS",
        "mmSPI_SHADER_PGM_RSRC1_VS", "mmSPI_SHADER_PGM_RSRC2_VS",
        "mmSPI_SHADER_USER_DATA_PS_0", "mmSPI_SHADER_USER_DATA_VS_0",
    ],
    "B.3 -- geometria e viewport": [
        "mmVGT_NUM_INSTANCES", "mmVGT_PRIMITIVE_TYPE",
        "mmPA_CL_VTE_CNTL", "mmPA_CL_CLIP_CNTL",
        "mmPA_SC_VPORT_SCISSOR_0_TL", "mmPA_SC_VPORT_SCISSOR_0_BR",
    ],
}

DEFINE_RE = re.compile(
    r"^#define\s+(mm[A-Za-z0-9_]+)\s+(0x[0-9a-fA-F]+|\d+)\s*$", re.M)
BASE_IDX_RE = re.compile(
    r"^#define\s+(mm[A-Za-z0-9_]+)_BASE_IDX\s+(\d+)\s*$", re.M)


def parse_header(text):
    offsets = {m.group(1): int(m.group(2), 0)
               for m in DEFINE_RE.finditer(text)
               if not m.group(1).endswith("_BASE_IDX")}
    base_idx = {m.group(1): int(m.group(2))
                for m in BASE_IDX_RE.finditer(text)}
    return offsets, base_idx


def bar5_byte(mmreg, idx):
    if idx not in SEGMENT_BASE_DW:
        return None
    return (SEGMENT_BASE_DW[idx] + mmreg) * 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("header", help="caminho do gc_10_1_0_offset.h do upstream")
    ap.add_argument("--install", action="store_true",
                    help="copia para inc/linux_ref/ se TODAS as ancoras passarem")
    args = ap.parse_args()

    src = Path(args.header)
    if not src.is_file():
        print(f"RECUSADO: arquivo inexistente: {src}")
        return 2
    if not SH_MASK.is_file():
        print(f"RECUSADO: {SH_MASK.name} ausente -- o par de headers tem de ser do "
              "mesmo IP, e o sh_mask e' a nossa referencia atual")
        return 2

    text = src.read_text(encoding="utf-8", errors="replace")
    if "_gc_10_1_0_OFFSET_HEADER" not in text:
        print("RECUSADO: nao tem a guarda _gc_10_1_0_OFFSET_HEADER -- "
              "este nao e' o header de offsets do GC 10.1.0")
        return 2

    offsets, base_idx = parse_header(text)
    if not offsets:
        print("RECUSADO: nenhum #define mmREG encontrado")
        return 2
    print(f"header lido: {len(offsets)} offsets, {len(base_idx)} BASE_IDX")

    # ---------------- ancoras ----------------
    print("\n=== ANCORAS: o header tem de reproduzir o que a placa ja confirmou ===")
    failures = 0
    for name, idx, expected, source in ANCHORS:
        mm = offsets.get(name)
        declared_idx = base_idx.get(name)
        if mm is None:
            print(f"  FALHOU  {name:28s} ausente do header")
            failures += 1
            continue
        if declared_idx is not None and declared_idx != idx:
            print(f"  FALHOU  {name:28s} BASE_IDX={declared_idx}, esperado {idx}")
            failures += 1
            continue
        got = bar5_byte(mm, idx)
        ok = (got == expected)
        print(f"  {'ok    ' if ok else 'FALHOU'}  {name:28s} "
              f"mm=0x{mm:04X} idx={idx} -> 0x{got:05X} "
              f"(esperado 0x{expected:05X})")
        if not ok:
            print(f"          procedencia da expectativa: {source}")
            failures += 1

    if failures:
        print(f"\nRECUSADO: {failures} ancora(s) divergiram.")
        print("Ou o header nao e' deste ASIC, ou a regra de calculo de docs/03 esta")
        print("errada. Nos DOIS casos nada e' instalado e nada sobe de classe.")
        return 1

    print("\nTODAS AS ANCORAS PASSARAM -- o header reproduz as medicoes desta placa.")

    # ---------------- o que ele desbloqueia ----------------
    missing_total = 0
    for stage, names in WANTED.items():
        print(f"\n=== {stage} ===")
        print("| Registro | mmREG | BASE_IDX | BAR5 byte |")
        print("|---|---:|---:|---:|")
        for name in names:
            mm = offsets.get(name)
            if mm is None:
                print(f"| `{name}` | **AUSENTE** | - | - |")
                missing_total += 1
                continue
            idx = base_idx.get(name, 0)
            byte = bar5_byte(mm, idx)
            if byte is None:
                print(f"| `{name}` | `0x{mm:04X}` | {idx} | **BASE_IDX desconhecido** |")
                missing_total += 1
                continue
            print(f"| `{name.replace('mm', '', 1)}` | `0x{mm:04X}` | {idx} | `0x{byte:05X}` |")

    if missing_total:
        print(f"\nAVISO: {missing_total} registrador(es) pedidos nao estao no header.")
        print("Isso NAO invalida o header -- alguns nomes mudam entre versoes do IP --")
        print("mas cada ausencia continua sem procedencia e segue proibida pela Lei 7.")

    if not args.install:
        print("\n(dry-run: nada foi copiado. Use --install para instalar em inc/linux_ref/)")
        return 0

    shutil.copyfile(src, DEST)
    print(f"\nINSTALADO: {DEST}")
    print("Proximos passos, nesta ordem:")
    print("  1. catalogar em docs/03 as linhas impressas acima, citando o header;")
    print("  2. promover a classe das entradas de H para P1;")
    print("  3. so' entao o APPLY do anel pode deixar de parar em CP_RB0_ACTIVE.")
    print("\nAceitar o header eleva a classe do ENDERECO. O EFEITO de escrever nele")
    print("continua NAO VALIDADO EM HARDWARE.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
