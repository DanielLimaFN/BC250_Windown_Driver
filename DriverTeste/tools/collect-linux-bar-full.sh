#!/bin/sh
# -----------------------------------------------------------------------------
# VARREDURA DO BAR5 INTEIRO, SEM EXCLUSOES
#
# Isto e o irmao SEM TRAVAS do collect-linux-gc-sweep.sh. Rode o outro PRIMEIRO
# e guarde a saida: se este travar a maquina, o dado do primeiro ja esta salvo.
#
# O QUE ELE FAZ DE DIFERENTE
#
# Le TODO o BAR5, dword por dword, do inicio ao fim. Nenhuma faixa, nenhuma
# exclusao, nenhuma lista.
#
# O RISCO, SEM EUFEMISMO
#
# Alguns registradores de MMIO tem EFEITO COLATERAL DE LEITURA. Neste projeto ja
# medimos um: ler GCVM_INVALIDATE_ENG0_SEM ADQUIRE o semaforo — este script VAI
# ler esse registrador, e vai tomar esse semaforo do driver vivo. Existem tambem
# FIFOs que avancam ponteiro ao serem lidas, status latchados que se limpam, e
# faixas nao decodificadas que podem gerar erro de barramento.
#
# Resultado possivel: a sessao grafica trava, o amdgpu entra em recuperacao, ou a
# maquina para e precisa de reset. Nao ha perda de dado em disco, mas ha perda da
# sessao. Rode com o que importa salvo.
#
# O QUE FOI FEITO PARA O RISCO RENDER ALGO
#
# A saida e gravada INCREMENTALMENTE com flush+fsync a cada 16 dwords, e cada
# offset tambem vai para a TELA. Se a maquina
# travar, o arquivo parcial termina na ultima faixa lida com sucesso — e o offset
# seguinte e, com boa aproximacao, O REGISTRADOR QUE CAUSOU O TRAVAMENTO. Isso e
# informacao de verdade, e nao se consegue de outra forma.
#
# E da para retomar de onde parou:
#
#   sudo sh collect-linux-bar-full.sh                        # tudo
#   sudo sh collect-linux-bar-full.sh 0x00000000 0x00010000  # so uma fatia
#   sudo sh collect-linux-bar-full.sh 0x0004A800             # retoma apos travar
#
# Pule o ofensor somando 4 ao ultimo offset gravado, e anote qual foi.
#
# COMO RODAR
#
#   sudo sh collect-linux-bar-full.sh
#
# Da 5 segundos para Ctrl-C antes de comecar.
# -----------------------------------------------------------------------------
set -eu

INICIO="${1:-0}"
FIM="${2:-0}"          # 0 = ate o fim do BAR
HOST="$(hostname 2>/dev/null || echo desconhecido)"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
OUT_TXT="linux-bar-full-${HOST}-${STAMP}.txt"

if [ "$(id -u)" != "0" ]; then
    echo "ERRO: precisa de root para mapear o BAR de MMIO. Use: sudo sh $0" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERRO: python3 nao encontrado." >&2
    exit 1
fi

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
    echo "ERRO: nenhum 1002:13FE encontrado." >&2
    exit 1
fi

RES5="/sys/bus/pci/devices/${BDF}/resource5"
if [ ! -r "$RES5" ]; then
    echo "ERRO: $RES5 nao legivel." >&2
    exit 1
fi

echo "-----------------------------------------------------------------------"
echo " VARREDURA SEM TRAVAS DO BAR5 INTEIRO"
echo ""
echo " Este script LE registradores com efeito colateral conhecido, inclusive"
echo " o semaforo de invalidacao do GCVM, que e ADQUIRIDO na leitura."
echo ""
echo " A sessao grafica pode travar. A maquina pode precisar de reset."
echo " Rode o collect-linux-gc-sweep.sh ANTES e guarde a saida dele."
echo ""
echo " placa   : $BDF"
echo " inicio  : $INICIO"
echo " saida   : $OUT_TXT   (gravada incrementalmente)"
echo "-----------------------------------------------------------------------"
echo ""
printf " comecando em 5s — Ctrl-C para desistir: "
n=5
while [ "$n" -gt 0 ]; do
    printf "%d " "$n"
    sleep 1
    n=$((n - 1))
done
echo ""
echo ""

python3 - "$RES5" "$BDF" "$OUT_TXT" "$INICIO" "$FIM" <<'PYEOF'
import mmap, os, sys

res5, bdf, out_txt, inicio_s, fim_s = sys.argv[1:6]
inicio = int(inicio_s, 0)
fim_arg = int(fim_s, 0)

fd = os.open(res5, os.O_RDONLY | os.O_SYNC)
tam = os.fstat(fd).st_size
mm = mmap.mmap(fd, tam, mmap.MAP_SHARED, mmap.PROT_READ)

# 'buffering=1' com write() de linha + flush explicito: o que importa e que o
# arquivo esteja no disco ANTES da proxima leitura arriscada, nao depois.
f = open(out_txt, "a", buffering=1)
f.write("# BAR5 completo, sem exclusoes. placa=%s tamanho=%d inicio=0x%X\n"
        % (bdf, tam, inicio))
f.write("# Se este arquivo termina abruptamente, o offset seguinte ao ultimo\n"
        "# gravado e o candidato a causador do travamento.\n")
f.flush()
os.fsync(f.fileno())

# 16 dwords, nao 256. Em 2026-08-24 09:09 esta varredura travou a maquina ANTES
# do primeiro flush de 256 e o arquivo voltou do reset com ZERO BYTE — nem o
# cabecalho. Um dump que nao sobrevive ao travamento que ele mesmo causa nao
# serve para nada. 8192 fsyncs custam alguns segundos; perder a coleta custa um
# boot e uma ida ao Linux.
FLUSH = 16 * 4
prox_flush = inicio + FLUSH
lidos = 0
naodecodifica = 0

try:
    limite = fim_arg if (fim_arg and fim_arg <= tam) else tam
    for byte in range(inicio, limite, 4):
        v = int.from_bytes(mm[byte:byte + 4], 'little')
        lidos += 1
        if v == 0xFFFFFFFF:
            naodecodifica += 1
        f.write("0x%06X 0x%08X\n" % (byte, v))
        if byte >= prox_flush:
            f.flush()
            os.fsync(f.fileno())
            prox_flush = byte + FLUSH
            pct = (byte - inicio) * 100 // max(1, limite - inicio)
            # SEM \\r: cada offset fica no scrollback. Se o arquivo se perder, a
            # tela ainda diz onde parou — foi exatamente o que faltou em 09:09.
            sys.stderr.write("  0x%06X  %3d%%\n" % (byte, pct))
            sys.stderr.flush()
except KeyboardInterrupt:
    sys.stderr.write("\n  interrompido em 0x%06X\n" % byte)
finally:
    f.flush()
    os.fsync(f.fileno())
    f.write("# fim: lidos=%d  todos_os_bits_em_1=%d\n" % (lidos, naodecodifica))
    f.flush()
    os.fsync(f.fileno())
    f.close()
    mm.close()
    os.close(fd)

sys.stderr.write("\n")
print("lidos              : %d dwords" % lidos)
print("0xFFFFFFFF         : %d  (nao decodificam)" % naodecodifica)
print("arquivo            : %s" % out_txt)
PYEOF

sync

echo ""
ls -l "$OUT_TXT"
echo ""
echo "Se chegou aqui sem travar, COPIE O ARQUIVO PARA FORA AGORA."
echo "Se travou, o arquivo parcial vale: o offset seguinte ao ultimo gravado e o"
echo "suspeito. Retome com:  sudo sh $0 <ultimo+4>"
