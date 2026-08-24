#!/usr/bin/env bash
#
# Coleta de referência no Linux, com o amdgpu funcionando, para resolver por
# medição o que no Windows está em especulação.
#
# A PERGUNTA QUE ISTO RESPONDE
#
# No Windows, a vizinhança dw 0x5814..0x5848 do segmento 1 do GC
# (CP_*_UCODE_ADDR e CP_*_IC_*) lê 0xFFFFFFFF e descarta escrita, em boot limpo
# e com a GPU em repouso. Tudo até dw 0x4c05 no mesmo segmento responde.
#
# Duas hipóteses já caíram por medição: "bloco de GFX sem energia" (RLC_CNTL e
# GRBM_GFX_INDEX respondem) e "estado pendurado de sessão anterior" (boot limpo,
# GPU em repouso, mesma não-resposta).
#
# No Linux o amdgpu faz o bring-up do CP com sucesso. Ler os MESMOS offsets de
# byte com o bloco vivo decide, sem chutar:
#
#   1. Se 0x3E100 responder no Linux, o bloco é alcançável e o caminho do
#      Windows está sem um passo de habilitação. O passo aparece no diff.
#   2. Se não responder nem no Linux, o offset de byte está errado — e aí a base
#      do segmento 1 (0xA000) precisa ser revista, apesar do banco de rascunho.
#   3. Os valores de CGTT_*_CLK_CTRL, RLC_CNTL e CP_ME_CNTL com o CP vivo dão o
#      alvo exato: é clock gating, é o RLC, ou é outra coisa.
#
# SEGURANÇA, E O QUE ESTE SCRIPT NÃO FAZ
#
# É SOMENTE LEITURA. Nenhuma escrita em MMIO, nenhum módulo carregado ou
# descarregado, nenhum firmware tocado.
#
# A lista de registradores é CURADA, não uma varredura. Varrer o BAR inteiro
# leria registradores cuja leitura tem efeito colateral, e este projeto já pagou
# por isso: ler GCVM_INVALIDATE_ENG0_SEM ADQUIRE o semáforo, e um dump que se
# anunciava somente leitura ficou dias retendo um semáforo de hardware.
#
# Ficam FORA da lista, de propósito:
#   *_UCODE_DATA        porta de dados; ler pode avançar ponteiro interno
#   *_SEM               ler é aquisição, não observação
#   *_REQ               escrever dispara; ler é ambíguo
#   qualquer read-to-clear
#
# Ainda assim: isto lê MMIO de uma GPU em uso pelo amdgpu. É o que o `umr` faz
# rotineiramente, mas se a máquina for de produção, rode com o desktop parado.
#
# USO
#
#   sudo bash collect-linux-cp-info.sh
#
# Gera dois arquivos no diretório atual:
#   linux-cp-info-<host>-<data>.json   para diff mecânico
#   linux-cp-info-<host>-<data>.txt    para leitura
#
# Mande os dois. O JSON é o que importa.

set -u

STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
HOST="$(hostname 2>/dev/null || echo desconhecido)"
OUT_JSON="linux-cp-info-${HOST}-${STAMP}.json"
OUT_TXT="linux-cp-info-${HOST}-${STAMP}.txt"

if [ "$(id -u)" != "0" ]; then
    echo "ERRO: precisa de root para mapear o BAR de MMIO. Use: sudo bash $0" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERRO: python3 nao encontrado. Ele faz o mmap do BAR." >&2
    exit 1
fi

# ---------------------------------------------------------------- localizar a placa
# 1002:13FE e a BC-250. Se houver mais de uma GPU AMD, pega a que bate o ID.
BDF=""
for d in /sys/bus/pci/devices/*; do
    [ -r "$d/vendor" ] || continue
    v="$(cat "$d/vendor" 2>/dev/null)"
    p="$(cat "$d/device" 2>/dev/null)"
    if [ "$v" = "0x1002" ] && [ "$p" = "0x13fe" ]; then
        BDF="$(basename "$d")"
        break
    fi
done

if [ -z "$BDF" ]; then
    echo "AVISO: nenhum 1002:13FE encontrado. Placas AMD presentes:" >&2
    for d in /sys/bus/pci/devices/*; do
        v="$(cat "$d/vendor" 2>/dev/null || true)"
        if [ "$v" = "0x1002" ]; then
            echo "   $(basename "$d")  device=$(cat "$d/device" 2>/dev/null)" >&2
        fi
    done
    echo "ERRO: sem a BC-250 a coleta nao serve para comparacao." >&2
    exit 1
fi

RES5="/sys/bus/pci/devices/${BDF}/resource5"
if [ ! -r "$RES5" ]; then
    echo "ERRO: $RES5 nao legivel. O BAR5 e onde o MMIO do GC vive." >&2
    exit 1
fi

echo "Placa: $BDF"
echo "BAR5 : $RES5 ($(stat -c %s "$RES5" 2>/dev/null || echo '?') bytes)"
echo

# ---------------------------------------------------------------- contexto
DRM_CARD=""
for c in /sys/class/drm/card*; do
    [ -e "$c/device" ] || continue
    if [ "$(basename "$(readlink -f "$c/device")")" = "$BDF" ]; then
        DRM_CARD="$(basename "$c")"
        break
    fi
done
DEBUG_DIR=""
for n in 0 1 2 3; do
    if [ -d "/sys/kernel/debug/dri/$n" ]; then
        if [ -r "/sys/kernel/debug/dri/$n/name" ] && grep -q "$BDF" "/sys/kernel/debug/dri/$n/name" 2>/dev/null; then
            DEBUG_DIR="/sys/kernel/debug/dri/$n"
            break
        fi
        [ -z "$DEBUG_DIR" ] && DEBUG_DIR="/sys/kernel/debug/dri/$n"
    fi
done

{
    echo "=== sistema ==="
    uname -a
    echo
    echo "=== driver em uso ==="
    lspci -nnks "$BDF" 2>/dev/null || echo '(lspci ausente)'
    echo
    echo "=== dmesg: amdgpu, gfx, psp, rlc, cp, ring ==="
    # As linhas de ring test sao o que diz se o CP realmente subiu.
    dmesg 2>/dev/null | grep -iE 'amdgpu|gfx|psp|rlc|\bcp\b|ring .* test|cyan|skillfish' | tail -200 \
        || echo '(dmesg indisponivel; rode como root ou veja journalctl -k)'
    echo
    echo "=== amdgpu_firmware_info (tipo de load e versoes) ==="
    if [ -n "$DEBUG_DIR" ] && [ -r "$DEBUG_DIR/amdgpu_firmware_info" ]; then
        cat "$DEBUG_DIR/amdgpu_firmware_info"
    else
        echo "(ausente: $DEBUG_DIR/amdgpu_firmware_info)"
    fi
    echo
    echo "=== amdgpu_gpu_recover / gfxoff, se existirem ==="
    for f in amdgpu_gfxoff_state amdgpu_gfxoff_status amdgpu_gfxoff_count amdgpu_gfxoff_residency; do
        if [ -n "$DEBUG_DIR" ] && [ -r "$DEBUG_DIR/$f" ]; then
            echo "--- $f"; cat "$DEBUG_DIR/$f" 2>/dev/null
        fi
    done
    echo
    echo "=== pp_dpm / power, para contexto de clock ==="
    for f in pp_dpm_sclk pp_dpm_mclk power_dpm_force_performance_level; do
        if [ -r "/sys/class/drm/${DRM_CARD}/device/$f" ]; then
            echo "--- $f"; cat "/sys/class/drm/${DRM_CARD}/device/$f" 2>/dev/null
        fi
    done
} > "$OUT_TXT" 2>&1

# ---------------------------------------------------------------- leitura de MMIO
# Os offsets de byte sao EXATAMENTE os usados no Windows, pela regra
#   byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4
# com 0x1260 para BASE_IDX 0 e 0xA000 para BASE_IDX 1.
#
# Se o Linux ler valores coerentes nos mesmos offsets, a aritmetica esta certa e
# a comparacao vale. Se ler lixo nos que o Windows le bem, a aritmetica esta
# errada e e isso que descobrimos.
python3 - "$RES5" "$BDF" "$OUT_JSON" "$OUT_TXT" <<'PYEOF'
import json, mmap, os, sys, platform, subprocess

res5, bdf, out_json, out_txt = sys.argv[1:5]

SEG = {0: 0x1260, 1: 0xA000}
def b(seg, dw):
    return (SEG[seg] + dw) * 4

# nome, dw, segmento, papel
REGS = [
    # --- segmento 0: controle, sabemos que responde no Windows ---
    ("GRBM_STATUS",            0x0da4, 0, "controle; Windows em repouso: 0x00003028"),
    ("GRBM_STATUS2",           0x0da2, 0, "controle; Windows em repouso: 0x00000008"),
    ("GRBM_GFX_CNTL",          0x0dc2, 0, "usado pela interface RLCG"),
    ("CP_STAT",                0x0f40, 0, "ocupacao do CP; Windows em repouso: 0x00000000"),
    ("CP_ME_CNTL",             0x0f56, 0, "HALT: Windows le 0x15000000 (as tres haltadas)"),
    ("CP_PFP_INSTR_PNTR",      0x0f45, 0, "PC do PFP; Windows le 0x00000000"),
    ("CP_ME_INSTR_PNTR",       0x0f46, 0, "PC do ME"),
    ("CP_CE_INSTR_PNTR",       0x0f47, 0, "PC do CE"),
    ("CP_MEC1_INSTR_PNTR",     0x0f48, 0, "PC do MEC1"),
    ("CP_CPF_STATUS",          0x0e27, 0, "status do prefetch"),
    ("CP_CPC_STATUS",          0x0e24, 0, "status do compute"),
    ("CP_RB0_BASE",            0x1de0, 0, "base do anel GFX 0"),
    ("CP_RB0_CNTL",            0x1de1, 0, "Windows em repouso: 0x00A00000"),
    ("CP_RB0_RPTR",            0x0f60, 0, "RPTR do anel GFX 0"),
    ("CP_RB0_WPTR",            0x1df4, 0, "WPTR do anel GFX 0"),
    ("CP_RB_VMID",             0x1df1, 0, "VMID do anel"),
    ("CP_MAX_CONTEXT",         0x1e4e, 0, "contextos de hardware"),
    ("CP_DEVICE_ID",           0x1deb, 0, "id do dispositivo para o CP"),
    # --- segmento 1: onde esta a duvida ---
    ("SCRATCH_REG0",           0x2040, 1, "controle do segmento 1; responde no Windows"),
    ("SCRATCH_REG1",           0x2041, 1, "banco de rascunho"),
    ("SCRATCH_REG7",           0x2047, 1, "banco de rascunho"),
    ("GRBM_GFX_INDEX",         0x2200, 1, "responde no Windows"),
    ("CP_MES_DOORBELL_CTRL1",  0x283c, 1, "sonda de fronteira, abaixo da zona morta"),
    ("RLC_CNTL",               0x4c00, 1, "portao do GFX; Windows boot limpo: 0x00000000"),
    ("RLC_SAFE_MODE",          0x4c05, 1, "modo seguro; Windows: 0x00000000"),
    ("RLC_PG_CNTL",            0x4c43, 1, "power gating do GFX"),
    ("RLC_CGCG_CGLS_CTRL",     0x4c49, 1, "clock gating de granularidade grossa"),
    ("RLC_SPARE_INT",          0x4ccc, 1, "usado pela interface RLCG"),
    ("RLC_GPM_STAT",           0x4e6e, 1, "estado do GPM do RLC"),
    ("CGTT_CPF_CLK_CTRL",      0x50b1, 1, "SUSPEITO PRINCIPAL: clock gating do CPF"),
    ("CGTT_CPC_CLK_CTRL",      0x50b2, 1, "SUSPEITO PRINCIPAL: clock gating do CPC"),
    # --- a zona morta no Windows: tudo aqui le 0xFFFFFFFF la ---
    ("CP_PFP_UCODE_ADDR",      0x5814, 1, "ZONA MORTA no Windows"),
    ("CP_CE_UCODE_ADDR",       0x5818, 1, "ZONA MORTA no Windows"),
    ("CP_MEC_ME1_UCODE_ADDR",  0x581a, 1, "ZONA MORTA no Windows"),
    ("CP_PFP_IC_BASE_LO",      0x5840, 1, "ZONA MORTA; amdgpu programa com o endereco do ucode"),
    ("CP_PFP_IC_BASE_HI",      0x5841, 1, "ZONA MORTA"),
    ("CP_PFP_IC_BASE_CNTL",    0x5842, 1, "ZONA MORTA; VMID/ADDRESS_CLAMP/CACHE_POLICY"),
    ("CP_PFP_IC_OP_CNTL",      0x5843, 1, "ZONA MORTA; causou o falso positivo de COMPLETE"),
    ("CP_ME_IC_BASE_LO",       0x5844, 1, "ZONA MORTA"),
    ("CP_ME_IC_BASE_CNTL",     0x5846, 1, "ZONA MORTA"),
    ("CP_CE_IC_BASE_LO",       0x5848, 1, "ZONA MORTA"),
    ("CP_CE_IC_BASE_CNTL",     0x584a, 1, "ZONA MORTA"),
    # --- sonda ACIMA da zona morta: decide se o limite e do bloco ou da janela ---
    ("RLC_GPM_UCODE_ADDR",     0x5b61, 1, "DECIDE: acima da zona morta"),
]

# NAO LER, e a razao. Mantido no JSON para que a exclusao seja auditavel em vez
# de silenciosa.
EXCLUIDOS = [
    ("CP_PFP_UCODE_DATA",  0x5815, 1, "porta de dados: ler pode avancar ponteiro interno"),
    ("CP_ME_UCODE_DATA",   0x5817, 1, "porta de dados"),
    ("CP_CE_UCODE_DATA",   0x5819, 1, "porta de dados"),
    ("RLC_GPM_UCODE_DATA", 0x5b62, 1, "porta de dados"),
    ("GCVM_INVALIDATE_ENG0_SEM", 0x1631, 0, "ler ADQUIRE o semaforo (medido em 2026-08-23)"),
    ("GCVM_INVALIDATE_ENG0_REQ", 0x1643, 0, "escrever dispara invalidacao"),
]

size = os.path.getsize(res5)
try:
    fd = os.open(res5, os.O_RDONLY | getattr(os, "O_SYNC", 0))
    mm = mmap.mmap(fd, size, prot=mmap.PROT_READ)
except Exception as e:
    print("ERRO ao mapear %s: %s" % (res5, e), file=sys.stderr)
    print("", file=sys.stderr)
    print("Alternativas, na ordem que eu tentaria:", file=sys.stderr)
    print("  1. Confirmar que rodou com sudo.", file=sys.stderr)
    print("  2. Se o kernel tem CONFIG_IO_STRICT_DEVMEM, o mmap do BAR pode ser", file=sys.stderr)
    print("     barrado com o amdgpu ligado. Tente com o modulo descarregado:", file=sys.stderr)
    print("       sudo systemctl isolate multi-user.target", file=sys.stderr)
    print("       sudo modprobe -r amdgpu && sudo bash %s" % sys.argv[0], file=sys.stderr)
    print("     ATENCAO: sem amdgpu a leitura perde o valor de referencia, porque", file=sys.stderr)
    print("     o objetivo e ver o bloco COM o CP vivo. Use isso apenas se o", file=sys.stderr)
    print("     mapa com amdgpu carregado for impossivel, e diga qual dos dois", file=sys.stderr)
    print("     casos rodou.", file=sys.stderr)
    print("  3. Se existir o 'umr' instalado, ele resolve sem mmap:", file=sys.stderr)
    print("       sudo umr -r gc_10_1_0.*.mmCP_PFP_IC_BASE_LO", file=sys.stderr)
    sys.exit(2)

def rd32(off):
    if off + 4 > size:
        return None
    return int.from_bytes(mm[off:off+4], "little")

leituras = []
for nome, dw, seg, papel in REGS:
    off = b(seg, dw)
    # Le tres vezes: valor unico nao distingue parado de em movimento, licao do
    # UTCL1_PAGE e dos ponteiros de instrucao.
    vals = [rd32(off) for _ in range(3)]
    ok = all(v is not None for v in vals)
    todos_um = ok and all(v == 0xFFFFFFFF for v in vals)
    leituras.append({
        "nome": nome,
        "dw": "0x%04x" % dw,
        "segmento": seg,
        "byte": "0x%05X" % off,
        "papel": papel,
        "leituras": [("0x%08X" % v) if v is not None else None for v in vals],
        "valores_distintos": len(set(v for v in vals if v is not None)),
        "responde": bool(ok and not todos_um),
        "todos_bits_em_um": bool(todos_um),
        "fora_da_janela": not ok,
    })

mm.close()
os.close(fd)

def sh(cmd):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True, text=True,
                              timeout=20).stdout.strip()
    except Exception as e:
        return "(falhou: %s)" % e

mortos = [l for l in leituras if l["todos_bits_em_um"]]
vivos  = [l for l in leituras if l["responde"]]

registro = {
    "schema_version": 1,
    "objective": ("Linux reference read of the same GC MMIO byte offsets used on "
                  "Windows, with amdgpu working, to decide why dw 0x5814..0x5848 "
                  "of segment 1 does not decode there"),
    "captured_at": sh("date -Is"),
    "host": platform.node(),
    "kernel": platform.release(),
    "pci_bdf": bdf,
    "bar5_path": res5,
    "bar5_size": size,
    "read_only": True,
    "segment_base_dw": {"0": "0x1260", "1": "0xA000"},
    "derivation": "byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4",
    "windows_reference": {
        "GRBM_STATUS_em_repouso": "0x00003028",
        "GRBM_STATUS2_em_repouso": "0x00000008",
        "CP_STAT_em_repouso": "0x00000000",
        "CP_ME_CNTL": "0x15000000 (CE_HALT|PFP_HALT|ME_HALT)",
        "CP_RB0_CNTL_em_repouso": "0x00A00000",
        "RLC_CNTL_boot_limpo": "0x00000000",
        "zona_morta": "dw 0x5814..0x5848 do segmento 1 leem 0xFFFFFFFF",
    },
    "nao_lidos_de_proposito": [
        {"nome": n, "dw": "0x%04x" % d, "segmento": s, "motivo": m}
        for (n, d, s, m) in EXCLUIDOS
    ],
    "resumo": {
        "total": len(leituras),
        "respondem": len(vivos),
        "nao_respondem": len(mortos),
        "nomes_que_nao_respondem": [l["nome"] for l in mortos],
    },
    "leituras": leituras,
}

with open(out_json, "w") as f:
    json.dump(registro, f, indent=1, ensure_ascii=False)

# Tabela legivel no fim do txt e no terminal.
linhas = []
linhas.append("")
linhas.append("=== leitura de MMIO, mesmos offsets do Windows ===")
linhas.append("%-24s %-7s %-4s %-9s %-12s %s" % ("registrador","dw","seg","byte","leitura","veredito"))
for l in leituras:
    if l["fora_da_janela"]:
        v = "FORA DA JANELA"
    elif l["todos_bits_em_um"]:
        v = "NAO RESPONDE"
    else:
        v = "responde"
    linhas.append("%-24s %-7s %-4d %-9s %-12s %s" % (
        l["nome"], l["dw"], l["segmento"], l["byte"], l["leituras"][0] or "-", v))
linhas.append("")
if mortos and vivos:
    abaixo = [l for l in vivos if int(l["byte"],16) < int(mortos[0]["byte"],16)]
    acima  = [l for l in vivos if int(l["byte"],16) > int(mortos[-1]["byte"],16)]
    linhas.append("Fronteira da zona que nao responde:")
    if abaixo:
        linhas.append("  ultimo que responde ABAIXO : %-24s %s" % (abaixo[-1]["nome"], abaixo[-1]["byte"]))
    linhas.append("  zona morta                 : %s .. %s" % (mortos[0]["byte"], mortos[-1]["byte"]))
    if acima:
        linhas.append("  primeiro que responde ACIMA: %-24s %s" % (acima[0]["nome"], acima[0]["byte"]))
    else:
        linhas.append("  nada acima da zona morta respondeu")
elif not mortos:
    linhas.append("TODOS os registradores responderam no Linux.")
    linhas.append("Isso significa que o bloco do CP e alcancavel nesta placa, e o")
    linhas.append("caminho do Windows esta sem um passo de habilitacao. O diff dos")
    linhas.append("valores diz qual.")
linhas.append("")
texto = "\n".join(linhas)

with open(out_txt, "a") as f:
    f.write(texto + "\n")
print(texto)
print("JSON: %s" % out_json)
print("TXT : %s" % out_txt)
PYEOF

echo
echo "Pronto. Mande os dois arquivos:"
echo "  $OUT_JSON"
echo "  $OUT_TXT"
