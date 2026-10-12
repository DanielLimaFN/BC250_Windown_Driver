#!/usr/bin/env bash
# =============================================================================
# bc250-l1-dc0.sh -- Ensaio L1 (doc 661): Linux com amdgpu.dc=0
#
# Pergunta: com o amdgpu dono de GFX/SDMA/MMHUB/GART/IH/SMU, mas SEM ser dono
# do DCN (o display fica como o GOP deixou, como no nosso driver Windows), a
# BC-250 trava sob carga grafica + paginacao?
#
# Com dc=0 o amdgpu nao tem display: nao ha console nem desktop depois que ele
# carrega. Por isso o roteiro e':
#   1. boot com  amdgpu.dc=0 modprobe.blacklist=amdgpu systemd.unit=multi-user.target
#   2. login no console de texto (ainda no framebuffer do GOP)
#   3. sudo bash bc250-l1-dc0.sh -o /caminho/no/pendrive
#   4. o script carrega o amdgpu com dc=0 (a tela some ou congela: esperado),
#      roda sozinho, grava tudo no disco com sync a cada 2 s e REINICIA a
#      maquina sozinho no fim. Reiniciar sozinho = nao travou.
#
# USO:
#   sudo bash bc250-l1-dc0.sh -o /run/media/liveuser/PENDRIVE/l1      # 600 s
#   sudo bash bc250-l1-dc0.sh -o ... -d 600 --sessao 2                 # 2a sessao
#   sudo bash bc250-l1-dc0.sh -o ... --controle-dc1                    # controle (so' se a L1 travar)
#   sudo bash bc250-l1-dc0.sh -o ... --pos-trava                       # depois de travar
#   Opcoes: --sem-reboot (nao reinicia no fim), --sem-dcn (nao le OTG/HUBP)
#
# Nada aqui escreve em registrador. As leituras do DCN sao pelo debugfs
# amdgpu_regs (so' leitura, offsets do bc250_dcn_probe_contract.h).
# =============================================================================

set -u
VERSAO="L1.2 (08/10/2026, docs 661 e 663: perfil l1b)"
DURACAO=600
INTERVALO=2
FASE1=120
SAIDA_BASE=""
SESSAO=1
CONTROLE_DC1=0
POS_TRAVA=0
REBOOT=1
LER_DCN=1
AUTO=0
USUARIO_L1=""
XDG_L1=""
PERFIL="l1"
SWEEP=0
ALVO_MB=210

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--duracao)   DURACAO="$2"; shift 2 ;;
        -o|--saida)     SAIDA_BASE="$2"; shift 2 ;;
        --sessao)       SESSAO="$2"; shift 2 ;;
        --controle-dc1) CONTROLE_DC1=1; shift ;;
        --pos-trava)    POS_TRAVA=1; shift ;;
        --sem-reboot)   REBOOT=0; shift ;;
        --sem-dcn)      LER_DCN=0; shift ;;
        --auto)         AUTO=1; shift ;;          # sem perguntas (usado pelo bc250-l1-auto.sh)
        --usuario)      USUARIO_L1="$2"; shift 2 ;;
        --xdg)          XDG_L1="$2"; shift 2 ;;
        --perfil)       PERFIL="$2"; shift 2 ;;   # l1 (doc 661) ou l1b (doc 663: VRAM cheia)
        --sweep)        SWEEP=1; shift ;;         # l1b: varredura de registradores e gpu_metrics em regime
        --alvo-mb)      ALVO_MB="$2"; shift 2 ;;
        -h|--help)      sed -n '2,31p' "$0"; exit 0 ;;
        *) echo "Opcao desconhecida: $1 (use -h)"; exit 2 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "ERRO: rode com sudo."; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
[ -n "$SAIDA_BASE" ] || SAIDA_BASE="$SCRIPT_DIR"
mkdir -p "$SAIDA_BASE" 2>/dev/null
touch "$SAIDA_BASE/.l1_escrita" 2>/dev/null || { echo "ERRO: nao consigo gravar em $SAIDA_BASE"; exit 1; }
rm -f "$SAIDA_BASE/.l1_escrita"

sincronizar() { sync -f "$1" 2>/dev/null || sync; }

# --- Pos-trava: rodar no boot seguinte (com o display normal) ----------------
if [ "$POS_TRAVA" -eq 1 ]; then
    ULT="$(ls -1dt "$SAIDA_BASE"/bc250-l1-*/ 2>/dev/null | head -1)"; ULT="${ULT%/}"
    [ -n "$ULT" ] || { echo "Nenhuma pasta bc250-l1-* em $SAIDA_BASE"; exit 1; }
    P="$ULT/pos-trava-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$P"
    {
        echo "=== POS-TRAVA L1 ($(date -Is)) -- pasta $ULT"
        echo "--- ultimo batimento ---";        cat "$ULT/batimento.txt" 2>/dev/null
        echo "--- ultimas 15 amostras ---";     tail -15 "$ULT/monitor.csv" 2>/dev/null
        echo "--- eventos ---";                tail -25 "$ULT/eventos.txt" 2>/dev/null
        echo "--- dmesg continuo (fim) ---";   tail -60 "$ULT/dmesg-continuo.txt" 2>/dev/null
        echo "--- resumo existe? ---";         [ -f "$ULT/RESUMO.txt" ] && echo "SIM (o ensaio terminou)" || echo "NAO (o ensaio nao terminou)"
        echo "--- motivo do reset (kernel deste boot) ---"
        dmesg 2>/dev/null | grep -iE "reset reason|previous system reset" || echo "(kernel nao imprimiu)"
    } > "$P/resumo-pos-trava.txt" 2>&1
    journalctl -b -1 -k --no-pager > "$P/journal-boot-anterior-kernel.txt" 2>&1
    journalctl --list-boots --no-pager > "$P/lista-de-boots.txt" 2>&1
    sincronizar "$P/resumo-pos-trava.txt"
    cat "$P/resumo-pos-trava.txt"
    echo; echo "Copie a pasta $ULT inteira para docs/evidence/ no Windows."
    exit 0
fi

# --- Saida tem de sobreviver a uma trava -------------------------------------
case "$(df -P "$SAIDA_BASE" | awk 'NR==2{print $1}')" in
    tmpfs|overlay|/dev/loop*|rootfs|/dev/mapper/live-rw)
        echo "ERRO: $SAIDA_BASE esta na RAM da sessao. Numa trava tudo se perde."
        echo "Use -o numa particao gravavel de pendrive ou disco."
        exit 1 ;;
esac

MODO="L1_DC0"; [ "$CONTROLE_DC1" -eq 1 ] && MODO="L1C_DC1"
if [ "$PERFIL" = "l1b" ]; then
    MODO="L1B_DC0"; [ "$CONTROLE_DC1" -eq 1 ] && MODO="L1BC_DC1"
    [ "$SWEEP" -eq 1 ] && MODO="${MODO}_SWEEP"
    [ -x "$SCRIPT_DIR/bc250_vram_stress" ] || { echo "ERRO: falta $SCRIPT_DIR/bc250_vram_stress (rode o preparar --perfil l1b)"; exit 1; }
    [ -f "$SCRIPT_DIR/bc250-regs-sweep.py" ] && [ -f "$SCRIPT_DIR/bc250-l1b-regs.txt" ] || { echo "ERRO: falta bc250-regs-sweep.py ou bc250-l1b-regs.txt"; exit 1; }
fi
OUT="$SAIDA_BASE/bc250-l1-$(date +%Y%m%d-%H%M%S)-${MODO}-s${SESSAO}"
mkdir -p "$OUT/inventario"
INV="$OUT/inventario"
marcar() { echo "$(date -Is) $*" >> "$OUT/eventos.txt"; sincronizar "$OUT/eventos.txt"; }
rodar() { local a="$1"; shift; { echo "\$ $*"; "$@" 2>&1; echo "[saida=$?]"; } > "$INV/$a" 2>&1; }

echo "bc250-l1-dc0 $VERSAO modo=$MODO sessao=$SESSAO inicio=$(date -Is)" | tee "$OUT/LEIA-ME.txt"
marcar "INICIO versao=$VERSAO modo=$MODO sessao=$SESSAO duracao=${DURACAO}s"

# --- Pre-voo (o console ainda esta visivel aqui) -------------------------------
CL="$(cat /proc/cmdline)"
rodar cmdline.txt cat /proc/cmdline
rodar uname.txt uname -a
rodar lspci.txt lspci -nnk -d 1002:
rodar os-release.txt cat /etc/os-release
if [ -x "$SCRIPT_DIR/bc250memcfg" ]; then rodar cmos-apcb.txt "$SCRIPT_DIR/bc250memcfg"; fi

[ -n "$USUARIO_L1" ] && SUDO_USER="$USUARIO_L1"
if ! id -u "${SUDO_USER:-root}" >/dev/null 2>&1 || [ "${SUDO_USER:-root}" = "root" ]; then
    echo "ERRO: rode com sudo a partir de um usuario logado (o weston nao roda como root)."; exit 1
fi
UIDU="$(id -u "$SUDO_USER")"; RUNDIR="/run/user/$UIDU"
if [ -n "$XDG_L1" ]; then
    RUNDIR="$XDG_L1"; mkdir -p "$RUNDIR"; chown "$SUDO_USER" "$RUNDIR"; chmod 700 "$RUNDIR"
fi
[ -d "$RUNDIR" ] || { echo "ERRO: $RUNDIR nao existe (faca login no console com o usuario)."; exit 1; }

for prog in weston vkcube; do
    command -v "$prog" >/dev/null 2>&1 || { echo "ERRO: falta '$prog'. Instale antes: sudo dnf install -y weston vulkan-tools"; exit 1; }
done

if lsmod | grep -q '^amdgpu '; then
    DCATUAL="$(cat /sys/module/amdgpu/parameters/dc 2>/dev/null)"
    if [ "$CONTROLE_DC1" -eq 0 ] && [ "$DCATUAL" != "0" ]; then
        echo "ERRO: o amdgpu ja esta carregado com dc=$DCATUAL. Para a L1 o boot precisa de"
        echo "      amdgpu.dc=0 modprobe.blacklist=amdgpu systemd.unit=multi-user.target"
        exit 1
    fi
    JA_CARREGADO=1
else
    JA_CARREGADO=0
fi
if [ "$CONTROLE_DC1" -eq 0 ] && ! echo "$CL" | grep -qw "systemd.unit=multi-user.target"; then
    echo "AVISO: sem systemd.unit=multi-user.target o desktop grafico pode estar rodando no framebuffer do GOP."
fi

echo
echo "=================================================================="
echo " PRONTO. Ao continuar, o amdgpu carrega com dc=$([ $CONTROLE_DC1 -eq 1 ] && echo 1 || echo 0)."
[ "$CONTROLE_DC1" -eq 0 ] && echo " A TELA VAI SUMIR OU CONGELAR -- e' esperado (o amdgpu nao tem display)."
echo " O ensaio dura ${DURACAO}s + ~60 s e depois a maquina REINICIA SOZINHA."
echo " Se NAO reiniciar ate $(date -d "+$((DURACAO + 240)) seconds" +%H:%M:%S): travou."
echo "   Confira o LED do Num Lock (nao muda = kernel morto), anote a hora e de Reset."
echo " Anote o que a tela mostra depois que o amdgpu carregar."
echo "=================================================================="
echo -n "Tecle Enter para comecar (Ctrl+C cancela): "
[ "$AUTO" -eq 1 ] && echo "(modo automatico)" || read -r _
echo 1 > /proc/sys/kernel/sysrq 2>/dev/null

# --- Carga do amdgpu -----------------------------------------------------------
dmesg -T > "$INV/dmesg-antes-do-amdgpu.txt" 2>&1
if [ "$JA_CARREGADO" -eq 0 ]; then
    marcar "MODPROBE amdgpu dc=$([ $CONTROLE_DC1 -eq 1 ] && echo -1 || echo 0)"
    if [ "$CONTROLE_DC1" -eq 1 ]; then modprobe amdgpu >"$INV/modprobe.txt" 2>&1
    else modprobe amdgpu dc=0 >"$INV/modprobe.txt" 2>&1; fi
    marcar "MODPROBE saida=$?"
fi
for _ in $(seq 1 30); do ls /dev/dri/renderD* >/dev/null 2>&1 && break; sleep 1; done
sleep 3
DC_LIDO="$(cat /sys/module/amdgpu/parameters/dc 2>/dev/null)"
marcar "AMDGPU dc_lido=$DC_LIDO"

DRI_IDX=""
for d in /sys/kernel/debug/dri/*; do
    [ -r "$d/name" ] && grep -q amdgpu "$d/name" && { DRI_IDX="$(basename "$d")"; break; }
done
D="/sys/kernel/debug/dri/$DRI_IDX"
DEV="$(for p in /sys/bus/pci/devices/*; do [ "$(cat "$p/vendor")" = "0x1002" ] && [ "$(cat "$p/device")" = "0x13fe" ] && echo "$p"; done | head -1)"
marcar "DRI_IDX=${DRI_IDX:-nenhum} DEV=${DEV:-nenhum}"

dmesg -T > "$INV/dmesg-depois-do-amdgpu.txt" 2>&1
rodar drm-conectores.txt sh -c 'ls -la /sys/class/drm/; for c in /sys/class/drm/card*-*; do echo "$c $(cat $c/status 2>/dev/null)"; done'
rodar renderD.txt ls -la /dev/dri
if [ -n "$DRI_IDX" ]; then
    for f in amdgpu_firmware_info amdgpu_fence_info amdgpu_vram_mm amdgpu_gtt_mm amdgpu_pm_info; do
        [ -r "$D/$f" ] && rodar "$f.txt" cat "$D/$f"
    done
fi

# --- Leitura do DCN pelo debugfs (so' leitura) ---------------------------------
# Offsets de bc250_dcn_probe_contract.h (DMU_BASE seg2 0x34C0 + mm) * 4.
REG_OTG0_CONTROL=$((0x14004)); REG_OTG0_STATUS=$((0x14024))
REG_OTG0_POS=$((0x14028));     REG_OTG0_FRAME=$((0x14030))
REG_HUBP0_ADDR=$((0xEB28));    REG_HUBP0_ADDR_HI=$((0xEB2C))
REG_HUBP0_INUSE=$((0xEB84));   REG_AUX0_CONTROL=$((0x15040))
ler_reg() {  # $1 = offset em bytes na BAR de registradores
    [ "$LER_DCN" -eq 1 ] && [ -r "$D/amdgpu_regs" ] || { echo ""; return; }
    dd if="$D/amdgpu_regs" bs=4 skip=$(( $1 / 4 )) count=1 2>/dev/null | od -An -tx4 | tr -d ' \n'
}
foto_dcn() {
    echo "OTG0_CONTROL=0x$(ler_reg $REG_OTG0_CONTROL) OTG0_STATUS=0x$(ler_reg $REG_OTG0_STATUS) OTG0_POS=0x$(ler_reg $REG_OTG0_POS) OTG0_FRAME=0x$(ler_reg $REG_OTG0_FRAME) HUBP0_ADDR=0x$(ler_reg $REG_HUBP0_ADDR_HI):$(ler_reg $REG_HUBP0_ADDR) HUBP0_INUSE=0x$(ler_reg $REG_HUBP0_INUSE) AUX0_CONTROL=0x$(ler_reg $REG_AUX0_CONTROL)"
}
FOTO0="$(foto_dcn)"; echo "$FOTO0" > "$INV/dcn-foto-inicial.txt"; marcar "DCN_INICIAL $FOTO0"
if [ "$PERFIL" = "l1b" ]; then
    python3 "$SCRIPT_DIR/bc250-regs-sweep.py" once --saida "$INV/regs-antes-da-carga.tsv" >>"$OUT/eventos.txt" 2>&1
    python3 "$SCRIPT_DIR/bc250-regs-sweep.py" otg --segundos 20 --saida "$INV/otg-refresh-antes.json" >>"$OUT/eventos.txt" 2>&1
    sincronizar "$OUT/eventos.txt"
fi

# --- Monitor (2 s, sync) ---------------------------------------------------------
fence_de() {  # $1 = trecho do nome do anel (gfx_0.0.0, sdma0, sdma1)
    awk -v r="$1" '/--- ring/ {on = index($0, r) > 0} on && /Last signaled fence/ {print $NF; exit}' "$D/amdgpu_fence_info" 2>/dev/null
}
ler() { [ -r "$1" ] && head -1 "$1" 2>/dev/null || echo ""; }
HWMON=""; CPU_HWMON=""
for h in /sys/class/hwmon/hwmon*; do
    case "$(cat "$h/name" 2>/dev/null)" in
        amdgpu) HWMON="$h" ;;
        k10temp|zenpower) CPU_HWMON="$h" ;;
    esac
done
echo "epoch,hora,gfx_fence,sdma0_fence,sdma1_fence,vram_mb,gtt_mb,gpu_busy,sclk,gpu_edge_c,cpu_tctl_c,otg0_frame,otg0_pos,load1,evicts" > "$OUT/monitor.csv"
echo 0 > "$OUT/.evicts"
(
    while true; do
        E="$(date +%s)"; H="$(date +%H:%M:%S)"
        GF="$(fence_de gfx_0.0.0)"; S0="$(fence_de sdma0)"; S1="$(fence_de sdma1)"
        VR="$(awk '{printf "%.0f",$1/1048576}' "$DEV/mem_info_vram_used" 2>/dev/null)"
        GT="$(awk '{printf "%.0f",$1/1048576}' "$DEV/mem_info_gtt_used" 2>/dev/null)"
        BUSY="$(ler "$DEV/gpu_busy_percent")"
        SCLK="$(grep '\*' "$DEV/pp_dpm_sclk" 2>/dev/null | awk '{print $2}' | head -1)"
        T1=""; [ -n "$HWMON" ] && T1="$(awk '{printf "%.1f",$1/1000}' "$HWMON/temp1_input" 2>/dev/null)"
        TC=""; [ -n "$CPU_HWMON" ] && TC="$(awk '{printf "%.1f",$1/1000}' "$CPU_HWMON/temp1_input" 2>/dev/null)"
        FR="$(ler_reg $REG_OTG0_FRAME)"; PO="$(ler_reg $REG_OTG0_POS)"
        LD="$(cut -d' ' -f1 /proc/loadavg)"; EV="$(cat "$OUT/.evicts" 2>/dev/null)"
        echo "$E,$H,$GF,$S0,$S1,$VR,$GT,$BUSY,$SCLK,$T1,$TC,$FR,$PO,$LD,$EV" >> "$OUT/monitor.csv"
        echo "$(date -Is) gfx=$GF sdma0=$S0 sdma1=$S1 vram=${VR}MB gtt=${GT}MB edge=${T1}C tctl=${TC}C otg_frame=$FR evicts=$EV ult=$(tail -1 "$OUT/eventos.txt" 2>/dev/null)" > "$OUT/batimento.txt"
        sincronizar "$OUT/monitor.csv"; sincronizar "$OUT/batimento.txt"
        sleep "$INTERVALO"
    done
) >/dev/null 2>"$OUT/monitor-erros.txt" &
PID_MON=$!
( dmesg -w -T 2>&1 | while IFS= read -r l; do echo "$l"; sync -f "$OUT/dmesg-continuo.txt" 2>/dev/null; done ) >> "$OUT/dmesg-continuo.txt" &
PID_DMESG=$!
PID_EXTRA=""
if [ "$PERFIL" = "l1b" ]; then
    (
        n=0
        while true; do
            { echo "$(date +%s.%N) $(grep -E 'amdgpu|AMD-Vi' /proc/interrupts | tr -s ' ')"; } >> "$OUT/interrupcoes.txt"
            sincronizar "$OUT/interrupcoes.txt"
            if [ $((n % 30)) -eq 0 ]; then
                { echo "=== $(date -Is)"; cat "$D/amdgpu_vram_mm" 2>&1 | head -12; cat "$D/amdgpu_gem_info" 2>&1 | head -80; } >> "$OUT/memoria-periodica.txt"
                sincronizar "$OUT/memoria-periodica.txt"
            fi
            n=$((n + 1)); sleep 2
        done
    ) >/dev/null 2>&1 &
    PID_EXTRA="$!"
    if [ "$SWEEP" -eq 1 ]; then
        python3 "$SCRIPT_DIR/bc250-regs-sweep.py" periodic --intervalo 10 --saida "$OUT/regs-periodico.jsonl" >/dev/null 2>"$OUT/regs-periodico-erros.txt" &
        PID_EXTRA="$PID_EXTRA $!"
        (
            while true; do
                [ -r "$DEV/gpu_metrics" ] && echo "$(date +%s.%N) $(od -An -tx1 -v "$DEV/gpu_metrics" | tr -d ' \n')" >> "$OUT/gpu-metrics.txt"
                { echo "$(date +%s) sclk=$(cat "$HWMON/freq1_input" 2>/dev/null) vddgfx=$(cat "$HWMON/in0_input" 2>/dev/null) vddnb=$(cat "$HWMON/in1_input" 2>/dev/null) pot=$(cat "$HWMON/power1_average" 2>/dev/null || cat "$HWMON/power1_input" 2>/dev/null)"; } >> "$OUT/smu-hwmon.txt"
                sincronizar "$OUT/gpu-metrics.txt"; sincronizar "$OUT/smu-hwmon.txt"
                sleep 2
            done
        ) >/dev/null 2>&1 &
        PID_EXTRA="$PID_EXTRA $!"
        marcar "SWEEP ligado: registradores a cada 10 s, gpu_metrics e hwmon a cada 2 s"
    fi
fi

# --- Carga: compositor sem tela + clientes 3D + paginacao forcada --------------
usuario() { sudo -u "$SUDO_USER" env XDG_RUNTIME_DIR="$RUNDIR" WAYLAND_DISPLAY=bc250-l1 "$@"; }
sudo -u "$SUDO_USER" env XDG_RUNTIME_DIR="$RUNDIR" weston --backend=headless --renderer=gl \
    --width=2560 --height=1080 --socket=bc250-l1 --idle-time=0 >"$OUT/weston.txt" 2>&1 &
PID_W=$!
sleep 4
if ! kill -0 "$PID_W" 2>/dev/null; then
    marcar "WESTON sintaxe nova falhou; tentando a antiga (--use-gl)"
    sudo -u "$SUDO_USER" env XDG_RUNTIME_DIR="$RUNDIR" weston --backend=headless-backend.so --use-gl \
        --width=2560 --height=1080 --socket=bc250-l1 --idle-time=0 >>"$OUT/weston.txt" 2>&1 &
    PID_W=$!; sleep 4
fi
kill -0 "$PID_W" 2>/dev/null && marcar "WESTON vivo pid=$PID_W" || marcar "WESTON MORTO (carga invalida: ver weston.txt)"
grep -iE "renderer|EGL|GL vendor|GL renderer|pixman" "$OUT/weston.txt" | head -10 >> "$OUT/eventos.txt"

usuario vkcube --wsi wayland >"$OUT/vkcube.txt" 2>&1 &
PID_VK=$!
PID_EGL=""
if command -v weston-simple-egl >/dev/null 2>&1; then
    usuario weston-simple-egl >"$OUT/simple-egl.txt" 2>&1 & PID_EGL=$!
fi
PID_GLM=""
if command -v glmark2-es2-wayland >/dev/null 2>&1; then
    if [ "$PERFIL" = "l1b" ]; then
        usuario glmark2-es2-wayland --run-forever --size 2560x1080 >"$OUT/glmark2.txt" 2>&1 & PID_GLM=$!
    else
        usuario glmark2-es2-wayland --run-forever >"$OUT/glmark2.txt" 2>&1 & PID_GLM=$!
    fi
fi
PID_ST=""
if [ "$PERFIL" = "l1b" ]; then
    # zerovram: o amdgpu zera cada alocacao nova pelo SDMA (o analogo do nosso FILL de zeramento).
    usuario env RADV_DEBUG=zerovram RADV_PERFTEST=transfer_queue \
        "$SCRIPT_DIR/bc250_vram_stress" "$ALVO_MB" "$((DURACAO + 30))" 2 >"$OUT/vram-stress.txt" 2>&1 &
    PID_ST=$!
fi
marcar "CLIENTES vkcube=$PID_VK simple_egl=${PID_EGL:-nenhum} glmark2=${PID_GLM:-nenhum} vram_stress=${PID_ST:-nenhum} alvo=${ALVO_MB}MB"

GF_A="$(fence_de gfx_0.0.0)"; sleep 10; GF_B="$(fence_de gfx_0.0.0)"
if [ -n "$GF_A" ] && [ -n "$GF_B" ] && [ "$GF_A" != "$GF_B" ]; then
    marcar "CARGA_NA_GPU OK fence gfx $GF_A -> $GF_B em 10 s"
else
    marcar "CARGA_NA_GPU FALHOU fence gfx $GF_A -> $GF_B (ensaio invalido se continuar assim)"
fi
kill -0 "$PID_VK" 2>/dev/null && marcar "VKCUBE vivo" || marcar "VKCUBE MORTO (ver vkcube.txt)"
if [ -n "$PID_ST" ]; then
    kill -0 "$PID_ST" 2>/dev/null && marcar "VRAM_STRESS vivo: $(grep -E '^(GPU|FILAS|MEMORIA|CHEIO)' "$OUT/vram-stress.txt" | tr '\n' ' ')" \
        || marcar "VRAM_STRESS MORTO (ensaio invalido: ver vram-stress.txt)"
fi

INICIO=$(date +%s); FIM=$(( INICIO + DURACAO )); N=0; G=0
marcar "FASE1 so grafico ${FASE1}s"
while [ "$(date +%s)" -lt $(( INICIO + FASE1 )) ] && [ "$(date +%s)" -lt "$FIM" ]; do sleep 2; done
marcar "FASE2 grafico + evict_vram a cada 2 s ate ${DURACAO}s"
while [ "$(date +%s)" -lt "$FIM" ]; do
    cat "$D/amdgpu_evict_vram" >/dev/null 2>&1; N=$((N + 1)); echo "$N" > "$OUT/.evicts"
    if [ $((N % 3)) -eq 0 ]; then cat "$D/amdgpu_evict_gtt" >/dev/null 2>&1; G=$((G + 1)); fi
    [ $((N % 15)) -eq 0 ] && marcar "PAGINACAO evict_vram=$N evict_gtt=$G $(foto_dcn)"
    sleep 2
done
marcar "FIM_CARGA evict_vram=$N evict_gtt=$G"

FOTO1="$(foto_dcn)"; echo "$FOTO1" > "$INV/dcn-foto-final.txt"; marcar "DCN_FINAL $FOTO1"
if [ "$PERFIL" = "l1b" ]; then
    python3 "$SCRIPT_DIR/bc250-regs-sweep.py" once --saida "$INV/regs-depois-da-carga.tsv" >>"$OUT/eventos.txt" 2>&1
fi
for p in $PID_ST $PID_VK $PID_EGL $PID_GLM $PID_W $PID_EXTRA; do kill "$p" 2>/dev/null; done
sleep 2
kill "$PID_MON" "$PID_DMESG" 2>/dev/null; pkill -P "$PID_DMESG" 2>/dev/null
[ -n "$DRI_IDX" ] && rodar final-fence_info.txt cat "$D/amdgpu_fence_info"
[ -n "$DRI_IDX" ] && rodar final-gpu_recover_count.txt cat "$D/amdgpu_gpu_recover_count" 2>/dev/null
[ "$PERFIL" = "l1b" ] && python3 "$SCRIPT_DIR/bc250-regs-sweep.py" otg --segundos 10 --saida "$INV/otg-refresh-depois.json" >>"$OUT/eventos.txt" 2>&1
rodar final-dmesg.txt sh -c 'dmesg -T | grep -iE "amdgpu|drm|ring|fence|reset|fault|timeout|hang|mce"'

{
    echo "=== RESUMO L1 ($(date -Is)) modo=$MODO sessao=$SESSAO"
    echo "A MAQUINA NAO TRAVOU durante ${DURACAO}s."
    echo "dc lido no modulo: $DC_LIDO"
    echo "despejos de VRAM: $N  de GTT: $G"
    echo "fence gfx inicio/fim: $(awk -F, 'NR==2{a=$3} END{print a" -> "$3}' "$OUT/monitor.csv")"
    echo "fence sdma0 inicio/fim: $(awk -F, 'NR==2{a=$4} END{print a" -> "$4}' "$OUT/monitor.csv")"
    echo "fence sdma1 inicio/fim: $(awk -F, 'NR==2{a=$5} END{print a" -> "$5}' "$OUT/monitor.csv")"
    echo "OTG0 frame inicio/fim: $(awk -F, 'NR==2{a=$12} END{print a" -> "$12}' "$OUT/monitor.csv")"
    echo "DCN inicial: $FOTO0"
    echo "DCN final:   $FOTO1"
    if [ "$PERFIL" = "l1b" ]; then
        echo "VRAM acima de 200 MB: $(awk -F, 'NR>1 && $6>200' "$OUT/monitor.csv" | wc -l) de $(( $(wc -l < "$OUT/monitor.csv") - 1 )) amostras"
        echo "VRAM mediana (MB): $(awk -F, 'NR>1{print $6}' "$OUT/monitor.csv" | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')"
        echo "estresse de VRAM (ultima linha): $(grep -E '^t=|^FIM' "$OUT/vram-stress.txt" | tail -1)"
        echo "estresse de VRAM, timeouts: $(grep -c TIMEOUT "$OUT/vram-stress.txt")"
        echo "refresh antes/depois: $(cat "$INV/otg-refresh-antes.json" 2>/dev/null) / $(cat "$INV/otg-refresh-depois.json" 2>/dev/null)"
    fi
    echo "--- erros do amdgpu no dmesg ---"
    grep -iE "amdgpu.*(error|timeout|reset|fault|hang|failed)|ring .* timeout|page fault" "$OUT/dmesg-continuo.txt" | tail -25 || true
} > "$OUT/RESUMO.txt" 2>&1
sincronizar "$OUT/RESUMO.txt"; sync
marcar "FIM_ENSAIO"

if [ "$REBOOT" -eq 1 ]; then
    marcar "REBOOT automatico em 15 s"
    sleep 15; sync; systemctl reboot
else
    cat "$OUT/RESUMO.txt"
fi
