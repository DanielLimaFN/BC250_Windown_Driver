#!/bin/sh
# -----------------------------------------------------------------------------
# VARREDURA AMPLA DAS JANELAS DO GC, NO LINUX, COM O CP EXECUTANDO
#
# POR QUE ESTE COLETOR EXISTE
#
# O collect-linux-cp-info.sh le uma LISTA CURADA de registradores. Oito boots
# limpos no Windows fecharam toda diferenca conhecida contra essa lista — RLC
# fora da ROM, CGCG, power gating, clock do CPF, traducao do VMID 0 — e o CP
# continua parado no mesmo 0x0000041E nas tres engines.
#
# Se o passo que falta esta num registrador que ninguem pensou em incluir na
# lista, nenhuma quantidade de diff sobre a lista vai encontra-lo. Comparamos o
# que escolhemos comparar. Este coletor troca isso: le a FAIXA, nao a lista, e a
# pergunta passa de "qual registrador falta?" para "quais registradores diferem?".
#
# O QUE ELE NAO FAZ, E POR QUE
#
# NAO le o BAR inteiro. Alguns registradores tem EFEITO COLATERAL DE LEITURA, e
# este projeto ja topou com um: ler GCVM_INVALIDATE_ENG0_SEM ADQUIRE o semaforo.
# Ha tambem FIFOs que estouram e status latchados que se limpam ao ler. Varredura
# cega de 512 KB numa GPU renderizando pode perturbar ou travar a sessao.
#
# As travas:
#   1. faixa restrita as janelas do GC (segmentos 0 e 1), nao o BAR todo;
#   2. lista de exclusao explicita dos offsets com efeito de leitura conhecido;
#   3. DUAS passagens na mesma sessao. O que diferir entre elas e volatil
#      (contador, PC, status) e sai do diff sozinho — sem depender do meu
#      julgamento sobre o que e ruido;
#   4. so leitura. Nenhuma escrita, em nenhum ponto.
#
# COMO RODAR
#
#   sudo sh collect-linux-gc-sweep.sh
#
# Com o CP EXECUTANDO — desktop rodando, ou pelo menos o amdgpu carregado e a
# GPU inicializada. E o estado que interessa: e o lado que funciona.
#
# Se a maquina nao for descartavel, pare o desktop antes. A varredura e somente
# leitura e restrita, mas 'somente leitura' em MMIO nunca e garantia absoluta, e
# eu prefiro dizer isso do que omitir.
#
# SAIDA
#
# Dois arquivos. Traga os dois para o Windows e ponha em referencias/linuxDump/.
# -----------------------------------------------------------------------------
set -eu

HOST="$(hostname 2>/dev/null || echo desconhecido)"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
OUT_JSON="linux-gc-sweep-${HOST}-${STAMP}.json"
OUT_TXT="linux-gc-sweep-${HOST}-${STAMP}.txt"

if [ "$(id -u)" != "0" ]; then
    echo "ERRO: precisa de root para mapear o BAR de MMIO. Use: sudo sh $0" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERRO: python3 nao encontrado. Ele faz o mmap do BAR." >&2
    exit 1
fi

# ---------------------------------------------------------------- localizar a placa
BDF=""
for d in /sys/bus/pci/devices/*; do
    [ -r "$d/vendor" ] || continue
    v="$(cat "$d/vendor" 2>/dev/null || true)"
    p="$(cat "$d/device" 2>/dev/null || true)"
    if [ "$v" = "0x1002" ] && [ "$p" = "0x13fe" ]; then
        BDF="$(basename "$d")"
        break
    fi
done

if [ -z "$BDF" ]; then
    echo "ERRO: nenhum 1002:13FE encontrado. Placas AMD presentes:" >&2
    for d in /sys/bus/pci/devices/*; do
        v="$(cat "$d/vendor" 2>/dev/null || true)"
        [ "$v" = "0x1002" ] || continue
        echo "   $(basename "$d")  device=$(cat "$d/device" 2>/dev/null || true)" >&2
    done
    exit 1
fi

RES5="/sys/bus/pci/devices/${BDF}/resource5"
if [ ! -r "$RES5" ]; then
    echo "ERRO: $RES5 nao existe ou nao e legivel." >&2
    exit 1
fi

echo "placa      : $BDF"
echo "BAR5       : $RES5"
echo "saida      : $OUT_JSON"
echo ""

python3 - "$RES5" "$BDF" "$OUT_JSON" "$OUT_TXT" <<'PYEOF'
import json, mmap, os, sys, time

res5, bdf, out_json, out_txt = sys.argv[1:5]

# A mesma convencao usada no Windows:
#   byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4
SEG = {0: 0x1260, 1: 0xA000}

# ------------------------------------------------------------------ faixas
# Cobrem as janelas do GC onde estao GRBM, CP, RLC, CGTT, GCVM e GCMC. Os
# limites vem dos maiores dw que este projeto ja enderecou em cada segmento,
# com folga, e NAO do BAR inteiro.
#
# seg 0: dw 0x0d00..0x2100  -> GRBM, CP, GCVM/GCMC, SH_MEM
# seg 1: dw 0x2000..0x5900  -> SCRATCH, RLC, CGTT e, no fim, a vizinhanca
#                              CP_*_UCODE_ADDR / CP_*_IC_* (dw 0x5814..0x5848,
#                              bytes 0x3E050..0x3E120). Uma primeira versao desta
#                              faixa parava em 0x5200 e deixava justamente esses
#                              de fora — os registradores no centro da duvida.
FAIXAS = [
    ("seg0", 0, 0x0d00, 0x2100),
    ("seg1", 1, 0x2000, 0x5900),
]

# ------------------------------------------------------------------ exclusoes
# Offsets de BYTE que NAO sao lidos. Nao e precaucao generica: cada um tem
# motivo registrado.
#
# GCVM_INVALIDATE_ENG0_SEM: ler ADQUIRE o semaforo. Medido neste projeto, e a
# razao pela qual existe o comando 'gcvm semrelease'. Ler aqui num sistema em
# producao seria tomar um semaforo do driver vivo.
#
# As portas de UCODE_DATA sao FIFOs de escrita; leitura nelas nao tem semantica
# definida e pode avancar ponteiro interno.
EXCLUIDOS = set()

def excl(seg, dw, quantos=1, motivo=""):
    for k in range(quantos):
        EXCLUIDOS.add((SEG[seg] + dw + k) * 4)

# GCVM_INVALIDATE_ENG0..17_SEM: dw 0x1690 + engine, seg 0 (faixa conservadora)
for eng in range(0, 18):
    excl(0, 0x1690 + eng, 1, "semaforo: leitura adquire")
# GCVM_INVALIDATE_ENG*_REQ tambem fica fora: e gatilho, nao configuracao.
for eng in range(0, 18):
    excl(0, 0x16b0 + eng, 1, "gatilho de invalidacao")
# Portas de dados de ucode (CP e RLC): FIFOs de escrita.
excl(1, 0x5815, 1, "CP_PFP_UCODE_DATA")
excl(1, 0x5817, 1, "CP_ME_UCODE_DATA")
excl(1, 0x5819, 1, "CP_CE_UCODE_DATA")
excl(1, 0x4c01, 1, "RLC_GPM_UCODE_DATA")

fd = os.open(res5, os.O_RDONLY | os.O_SYNC)
tam = os.fstat(fd).st_size
mm = mmap.mmap(fd, tam, mmap.MAP_SHARED, mmap.PROT_READ)

def ler(byte):
    return int.from_bytes(mm[byte:byte + 4], 'little')

def passagem():
    out = {}
    for nome, seg, dw_ini, dw_fim in FAIXAS:
        for dw in range(dw_ini, dw_fim):
            byte = (SEG[seg] + dw) * 4
            if byte in EXCLUIDOS:
                continue
            if byte + 4 > tam:
                continue
            out[byte] = ler(byte)
    return out

print("passagem 1 ...")
p1 = passagem()
time.sleep(0.5)
print("passagem 2 ...")
p2 = passagem()

# ------------------------------------------------------------------ volateis
# O que mudou entre as duas passagens e volatil por definicao: contador, PC,
# status de ocupacao. Sai do diff sozinho, sem eu decidir o que e ruido.
volateis = sorted(b for b in p1 if p1[b] != p2[b])
estaveis = {b: p1[b] for b in p1 if p1[b] == p2[b]}

# ------------------------------------------------------------------ saida
meta = {
    "bdf": bdf,
    "bar5_bytes": tam,
    "convencao": "byte = (segment_base_dw[BASE_IDX] + mmREG_dw) * 4; seg0=0x1260, seg1=0xA000",
    "faixas": [{"nome": n, "segmento": s, "dw_inicio": a, "dw_fim": b} for n, s, a, b in FAIXAS],
    "excluidos": sorted(EXCLUIDOS),
    "lidos": len(p1),
    "estaveis": len(estaveis),
    "volateis": len(volateis),
    "nota": ("Duas passagens com 0.5 s de intervalo. 'volateis' mudaram entre elas e "
             "devem ser tratados como ruido, nao como diferenca de configuracao. "
             "Somente leitura, nenhuma escrita em nenhum ponto."),
}

# fsync explicito nos dois arquivos. A primeira versao confiava no page cache:
# em 2026-08-24 09:08 a saida foi escrita, a maquina travou em seguida durante a
# varredura sem travas, e o reset trouxe os arquivos de volta com ZERO BYTE.
# Coleta valida perdida por falta de duas linhas.
with open(out_json, "w") as f:
    json.dump({
        "meta": meta,
        "estaveis": {("0x%05X" % b): ("0x%08X" % v) for b, v in sorted(estaveis.items())},
        "volateis": {("0x%05X" % b): ["0x%08X" % p1[b], "0x%08X" % p2[b]] for b in volateis},
    }, f, indent=1)
    f.flush()
    os.fsync(f.fileno())

with open(out_txt, "w") as f:
    f.write("VARREDURA DAS JANELAS DO GC — %s\n" % bdf)
    f.write("BAR5 %d bytes; lidos %d; estaveis %d; volateis %d\n\n"
            % (tam, len(p1), len(estaveis), len(volateis)))
    f.write("--- VOLATEIS (mudaram entre duas passagens; ruido, nao configuracao) ---\n")
    for b in volateis:
        f.write("0x%05X  0x%08X -> 0x%08X\n" % (b, p1[b], p2[b]))
    f.write("\n--- ESTAVEIS ---\n")
    for b, v in sorted(estaveis.items()):
        if v == 0xFFFFFFFF or v == 0:
            continue          # nao decodifica, ou zero: nao ajuda a ler de olho
        f.write("0x%05X  0x%08X\n" % (b, v))
    f.flush()
    os.fsync(f.fileno())

mm.close()
os.close(fd)

print("")
print("lidos    : %d" % len(p1))
print("estaveis : %d" % len(estaveis))
print("volateis : %d   (saem do diff sozinhos)" % len(volateis))
print("")
print("arquivos : %s" % out_json)
print("           %s" % out_txt)
PYEOF

sync

echo ""
echo "Gravados E SINCRONIZADOS:"
ls -l "$OUT_JSON" "$OUT_TXT"
echo ""
echo "COPIE OS DOIS PARA FORA DESTA MAQUINA AGORA, antes de rodar qualquer"
echo "outra coisa. Em 2026-08-24 09:08 esta coleta foi perdida porque a"
echo "varredura sem travas travou a maquina em seguida e o reset zerou os"
echo "arquivos. O fsync corrige a causa; copiar e o que garante."
echo ""
echo "Nenhuma escrita foi feita no hardware em nenhum momento."
