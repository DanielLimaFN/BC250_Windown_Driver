#!/usr/bin/env bash
# =============================================================================
# bc250-l1-auto.sh -- L1 (doc 661) inteira, automatica, no Fedora INSTALADO
#
# Um comando prepara tudo; depois o Daniel so' liga a maquina:
#
#   sudo bash bc250-l1-auto.sh preparar        # instala, cria a entrada do GRUB, arma 2 sessoes e DESLIGA
#   (ligar)  -> sessao 1 roda sozinha (sem tela) e DESLIGA a maquina no fim
#   (ligar)  -> sessao 2 roda sozinha e DESLIGA
#   (ligar)  -> boot normal: o resultado sai em ~/BC250_L1_RESULTADO.txt e num .tar.gz
#
# Desligar sozinho = a sessao nao travou. Se a maquina NAO desligar em ~15 min
# depois de ligada: travou. Anote a hora e de Reset; o boot seguinte e' normal
# (a entrada do GRUB e' de uma vez so') e a coleta pos-trava roda sozinha.
#
# Outros comandos:
#   sudo bash bc250-l1-auto.sh status          # mostra em que passo esta
#   sudo bash bc250-l1-auto.sh controle        # arma 1 sessao de controle (dc normal), so' se a L1 travar
#   sudo bash bc250-l1-auto.sh cancelar        # desfaz tudo (entrada, servicos, grupo)
#   Opcoes de preparar: --sessoes N (padrao 2), --duracao S (padrao 600), --sem-desligar,
#                       --perfil l1b (doc 663: VRAM cheia + coleta de referencia dc=1 + varredura na sessao 2)
#
# Exige Fedora instalado em disco (numa sessao Live a entrada do GRUB e os
# servicos se perdem no reboot). Nada aqui escreve em registrador da GPU.
# =============================================================================

set -u
DIR_KIT="/opt/bc250-l1"
DIR_DADOS="/var/lib/bc250-l1"
ESTADO="$DIR_DADOS/estado"
ENTRADA_ID="bc250-l1"
ENTRADA_ARQ="/boot/loader/entries/${ENTRADA_ID}.conf"
SRV_SESSAO="/etc/systemd/system/bc250-l1-sessao.service"
SRV_COLETA="/etc/systemd/system/bc250-l1-coleta.service"
ARGS_DC0="amdgpu.dc=0 systemd.unit=multi-user.target bc250.l1=1"
ARGS_DC1="systemd.unit=multi-user.target bc250.l1=1"

CMD="${1:-}"; [ $# -gt 0 ] && shift
SESSOES=2; DURACAO=600; DESLIGAR=1; PERFIL="l1"
while [ $# -gt 0 ]; do
    case "$1" in
        --sessoes)      SESSOES="$2"; shift 2 ;;
        --perfil)       PERFIL="$2"; shift 2 ;;   # l1 (doc 661) ou l1b (doc 663)
        --duracao)      DURACAO="$2"; shift 2 ;;
        --sem-desligar) DESLIGAR=0; shift ;;
        *) echo "Opcao desconhecida: $1"; exit 2 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "ERRO: rode com sudo."; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

log() { echo "$(date -Is) $*" | tee -a "$DIR_DADOS/auto.log"; sync; }
ler_estado() { [ -f "$ESTADO" ] && . "$ESTADO"; }
gravar_estado() {
    cat > "$ESTADO" <<EOF
USUARIO="$USUARIO"
TOTAL="$TOTAL"
PROXIMA="$PROXIMA"
MODO="$MODO"
DURACAO="$DURACAO"
GRUPO_ADICIONADO="$GRUPO_ADICIONADO"
PERFIL="$PERFIL"
EOF
    sync
}

# --- Entrada do GRUB (BLS) copiada da padrao, com argumentos a mais ----------
criar_entrada() {  # $1 = argumentos extras
    local kern base origem
    kern="$(grubby --default-kernel 2>/dev/null)"
    [ -n "$kern" ] || { echo "ERRO: grubby nao achou o kernel padrao."; return 1; }
    base="$(basename "$kern")"
    origem="$(grep -l "^linux.*/${base}\$\|^linux.*/${base} " /boot/loader/entries/*.conf 2>/dev/null | grep -v "/${ENTRADA_ID}.conf" | head -1)"
    [ -n "$origem" ] || origem="$(grep -l "${base}" /boot/loader/entries/*.conf 2>/dev/null | grep -v "/${ENTRADA_ID}.conf" | grep -v rescue | head -1)"
    [ -n "$origem" ] || { echo "ERRO: nao achei a entrada BLS do kernel $base em /boot/loader/entries."; return 1; }
    # version baixa: a entrada fica no fim do menu e nunca vira a padrao.
    awk -v extra="$1" '
        /^title /   { print "title BC-250 L1 (" extra ")"; next }
        /^version / { print "version 0-bc250-l1"; next }
        /^options / { print $0 " " extra; next }
        { print }' "$origem" > "$ENTRADA_ARQ"
    grep -q "^options .*bc250.l1=1" "$ENTRADA_ARQ" || { echo "ERRO: a entrada copiada de $origem nao tem linha options."; rm -f "$ENTRADA_ARQ"; return 1; }
    echo "Entrada $ENTRADA_ARQ criada a partir de $origem"
}
armar_proximo_boot() {
    grub2-reboot "$ENTRADA_ID" || return 1
    grub2-editenv list 2>/dev/null | grep -q "^next_entry=${ENTRADA_ID}\$" || { echo "ERRO: next_entry nao ficou gravado no grubenv."; return 1; }
}
desarmar_proximo_boot() { grub2-editenv - unset next_entry 2>/dev/null; }

instalar_servicos() {
    cat > "$SRV_SESSAO" <<EOF
[Unit]
Description=BC-250 L1: sessao de ensaio (doc 661)
ConditionKernelCommandLine=bc250.l1=1
After=systemd-user-sessions.service local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/bash $DIR_KIT/bc250-l1-auto.sh _sessao
TimeoutStartSec=infinity
StandardOutput=append:$DIR_DADOS/servico-sessao.log
StandardError=append:$DIR_DADOS/servico-sessao.log

[Install]
WantedBy=multi-user.target
EOF
    cat > "$SRV_COLETA" <<EOF
[Unit]
Description=BC-250 L1: coleta no boot normal (doc 661)
ConditionKernelCommandLine=!bc250.l1=1
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/bash $DIR_KIT/bc250-l1-auto.sh _apos-boot
StandardOutput=append:$DIR_DADOS/servico-coleta.log
StandardError=append:$DIR_DADOS/servico-coleta.log

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable bc250-l1-sessao.service bc250-l1-coleta.service
}

limpar() {
    ler_estado
    systemctl disable bc250-l1-sessao.service bc250-l1-coleta.service 2>/dev/null
    rm -f "$SRV_SESSAO" "$SRV_COLETA"; systemctl daemon-reload
    desarmar_proximo_boot
    rm -f "$ENTRADA_ARQ"
    if [ -n "${GRUPO_ADICIONADO:-}" ] && [ -n "${USUARIO:-}" ]; then
        for g in $GRUPO_ADICIONADO; do gpasswd -d "$USUARIO" "$g" >/dev/null 2>&1; done
    fi
    sync
}

empacotar() {  # $1 = veredito curto
    local home dest tgz
    home="$(getent passwd "$USUARIO" | cut -d: -f6)"
    tgz="$home/bc250-l1-resultado-$(date +%Y%m%d-%H%M%S).tar.gz"
    dest="$home/BC250_L1_RESULTADO.txt"
    {
        echo "=== BC-250 L1 (doc 661) -- $(date -Is)"
        echo "VEREDITO AUTOMATICO: $1"
        echo "(o veredito final e' o da analise no Windows, pelo criterio do doc 661 sec. 4)"
        echo
        for s in "$DIR_DADOS"/bc250-l1-*/; do
            s="${s%/}"; [ -d "$s" ] || continue
            echo "##### $(basename "$s")"
            if [ -f "$s/RESUMO.txt" ]; then cat "$s/RESUMO.txt"; else echo "SEM RESUMO: a sessao nao terminou (trava)"; fi
            grep -E "dc_lido|CARGA_NA_GPU|WESTON|VKCUBE|FIM_CARGA" "$s/eventos.txt" 2>/dev/null
            echo "ultimo batimento: $(cat "$s/batimento.txt" 2>/dev/null)"
            echo
        done
        echo "Pacote: $tgz -- copie para docs/evidence/ no Windows."
    } > "$dest"
    tar -czf "$tgz" -C "$(dirname "$DIR_DADOS")" "$(basename "$DIR_DADOS")" 2>/dev/null
    chown "$USUARIO" "$dest" "$tgz" 2>/dev/null
    cp "$dest" "$DIR_DADOS/RESULTADO.txt"
    sync
    echo "Resultado: $dest"; echo "Pacote:    $tgz"
}

coleta_dc1() {  # doc 663: o amdgpu completo e' o gabarito do DCN, do EDID e dos registradores
    local R="$DIR_DADOS/referencia-dc1-$(date +%Y%m%d-%H%M%S)" c d dev
    mkdir -p "$R/edid" "$R/debugfs" "$R/conectores"
    echo "      referencia dc=1 em $R"
    cat /proc/cmdline > "$R/cmdline.txt"
    for c in /sys/class/drm/card*-*; do
        [ -d "$c" ] || continue
        n="$(basename "$c")"
        { echo "status=$(cat "$c/status" 2>/dev/null) enabled=$(cat "$c/enabled" 2>/dev/null)"; cat "$c/modes" 2>/dev/null; } > "$R/conectores/$n.txt"
        [ -s "$c/edid" ] && cp "$c/edid" "$R/edid/$n.bin"
    done
    command -v edid-decode >/dev/null && for e in "$R"/edid/*.bin; do [ -f "$e" ] && edid-decode "$e" > "${e%.bin}.txt" 2>&1; done
    for d in /sys/kernel/debug/dri/*; do
        [ -r "$d/name" ] && grep -q amdgpu "$d/name" || continue
        for f in name state clients amdgpu_dm_dtn_log amdgpu_dm_capabilities amdgpu_dm_visual_confirm \
                 amdgpu_firmware_info amdgpu_pm_info amdgpu_vram_mm amdgpu_gtt_mm amdgpu_fence_info amdgpu_gem_info; do
            [ -r "$d/$f" ] && timeout 10 cat "$d/$f" > "$R/debugfs/$(basename "$d")-$f.txt" 2>&1
        done
        for c in "$d"/DP-* "$d"/HDMI-A-*; do
            [ -d "$c" ] || continue
            for f in link_settings output_bpc vrr_range psr_capability psr_state dp_dsc_fec_support; do
                [ -r "$c/$f" ] && timeout 10 cat "$c/$f" > "$R/debugfs/$(basename "$d")-$(basename "$c")-$f.txt" 2>&1
            done
        done
    done
    dev="$(for x in /sys/bus/pci/devices/*; do [ "$(cat "$x/vendor")" = "0x1002" ] && [ "$(cat "$x/device")" = "0x13fe" ] && echo "$x"; done | head -1)"
    if [ -n "$dev" ]; then
        for f in pp_dpm_sclk pp_dpm_mclk pp_dpm_fclk pp_dpm_socclk pp_dpm_dcefclk pp_od_clk_voltage \
                 power_dpm_force_performance_level power_dpm_state mem_info_vram_total mem_info_vis_vram_total \
                 mem_info_gtt_total current_link_speed current_link_width; do
            [ -r "$dev/$f" ] && { echo "== $f"; cat "$dev/$f" 2>&1; }
        done > "$R/sysfs-gpu.txt"
        [ -r "$dev/gpu_metrics" ] && od -An -tx1 -v "$dev/gpu_metrics" > "$R/gpu-metrics.hex"
        for h in "$dev"/hwmon/hwmon*; do [ -d "$h" ] && grep -H . "$h"/* 2>/dev/null > "$R/hwmon.txt"; done
    fi
    dmesg -T 2>/dev/null | grep -iE "amdgpu|drm|\[drm\]" > "$R/dmesg-amdgpu.txt"
    python3 "$DIR_KIT/bc250-regs-sweep.py" once --saida "$R/regs-dc1-desktop.tsv" --lista "$DIR_KIT/bc250-l1b-regs.txt" > "$R/sweep.txt" 2>&1
    python3 "$DIR_KIT/bc250-regs-sweep.py" otg --segundos 20 --saida "$R/otg-refresh-dc1.json" >> "$R/sweep.txt" 2>&1
    sync
    echo "      EDID: $(ls "$R"/edid/*.bin 2>/dev/null | wc -l) binario(s); DTN log: $(wc -c < "$R/debugfs/"*dtn_log*.txt 2>/dev/null | tail -1) bytes; $(tail -2 "$R/sweep.txt" | tr '\n' ' ')"
}

desligar() {
    sync
    if [ "$DESLIGAR" -eq 1 ]; then log "DESLIGANDO em 10 s"; sleep 10; sync; systemctl --no-block poweroff
    else log "(--sem-desligar) desligue a maquina pelo botao/tomada"; fi
}

case "$CMD" in
# -----------------------------------------------------------------------------
preparar)
    mkdir -p "$DIR_DADOS"
    if grep -qw "rd.live.image" /proc/cmdline || [ -d /run/initramfs/live ]; then
        echo "ERRO: sessao Live. A entrada do GRUB e os servicos nao sobrevivem ao reboot."
        echo "      Use o roteiro manual do doc 661 sec. 3 com bc250-l1-dc0.sh."
        exit 1
    fi
    USUARIO="${SUDO_USER:-}"
    [ -n "$USUARIO" ] && [ "$USUARIO" != "root" ] || { echo "ERRO: rode com sudo a partir do seu usuario."; exit 1; }
    [ -f "$SCRIPT_DIR/bc250-l1-dc0.sh" ] || { echo "ERRO: bc250-l1-dc0.sh nao esta ao lado deste script."; exit 1; }
    for t in grubby grub2-reboot grub2-editenv; do command -v $t >/dev/null || { echo "ERRO: falta $t (Fedora com GRUB e BLS)."; exit 1; }; done
    [ -d /boot/loader/entries ] || { echo "ERRO: sem /boot/loader/entries (GRUB sem BLS)."; exit 1; }
    [ -f "$ESTADO" ] && { echo "Ja existe uma L1 armada (veja 'status'). Use 'cancelar' antes."; exit 1; }

    case "$PERFIL" in l1|l1b) ;; *) echo "ERRO: --perfil deve ser l1 ou l1b"; exit 1 ;; esac
    echo "[1/5] Pacotes (weston, vulkan-tools; glmark2 se houver; perfil $PERFIL)..."
    falta=""
    command -v weston >/dev/null || falta="$falta weston"
    command -v vkcube >/dev/null || falta="$falta vulkan-tools"
    if [ -n "$falta" ]; then dnf install -y --setopt=install_weak_deps=False $falta || { echo "ERRO: dnf falhou (rede?)."; exit 1; }; fi
    command -v glmark2-es2-wayland >/dev/null || dnf install -y --setopt=install_weak_deps=False glmark2 >/dev/null 2>&1 || true
    command -v weston >/dev/null && command -v vkcube >/dev/null || { echo "ERRO: weston/vkcube ausentes depois do dnf."; exit 1; }
    if [ "$PERFIL" = "l1b" ]; then
        for pk in gcc vulkan-headers vulkan-loader-devel python3 edid-decode; do rpm -q "$pk" >/dev/null 2>&1 || falta_b="${falta_b:-} $pk"; done
        [ -n "${falta_b:-}" ] && { dnf install -y --setopt=install_weak_deps=False $falta_b || { echo "ERRO: dnf falhou para$falta_b"; exit 1; }; }
        command -v glmark2-es2-wayland >/dev/null || echo "      AVISO: sem glmark2-es2-wayland; a carga grafica fica no vkcube + estresse de VRAM"
    fi

    echo "[2/5] Kit em $DIR_KIT e dados em $DIR_DADOS..."
    mkdir -p "$DIR_KIT"
    cp "$SCRIPT_DIR/bc250-l1-dc0.sh" "$SCRIPT_DIR/bc250-l1-auto.sh" "$DIR_KIT/"
    if [ "$PERFIL" = "l1b" ]; then
        for f in bc250-regs-sweep.py bc250-l1b-regs.txt bc250_vram_stress.c; do
            [ -f "$SCRIPT_DIR/$f" ] || { echo "ERRO: falta $f ao lado do script."; exit 1; }
            cp "$SCRIPT_DIR/$f" "$DIR_KIT/"
        done
        gcc -O2 -Wall -o "$DIR_KIT/bc250_vram_stress" "$DIR_KIT/bc250_vram_stress.c" -lvulkan \
            || { echo "ERRO: bc250_vram_stress nao compilou."; exit 1; }
        echo "      estresse de VRAM compilado; teste rapido (5 s, 64 MB):"
        sudo -u "$USUARIO" env RADV_DEBUG=zerovram RADV_PERFTEST=transfer_queue "$DIR_KIT/bc250_vram_stress" 64 5 2 2>&1 | sed "s/^/        /" | tail -6
        # Sessoes antigas saem do caminho para o resultado listar so as desta L1b.
        if ls -d "$DIR_DADOS"/bc250-l1-* >/dev/null 2>&1; then
            ant="$DIR_DADOS/anteriores-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$ant"; mv "$DIR_DADOS"/bc250-l1-* "$ant/"
        fi
        echo "      coleta de referencia com o amdgpu completo (este boot, dc=1)..."
        coleta_dc1
    fi
    [ -x "$SCRIPT_DIR/bc250memcfg" ] && cp "$SCRIPT_DIR/bc250memcfg" "$DIR_KIT/"
    if [ ! -x "$DIR_KIT/bc250memcfg" ] && [ -f "$SCRIPT_DIR/bc250_memcfg.cpp" ] && command -v g++ >/dev/null; then
        g++ -O2 "$SCRIPT_DIR/bc250_memcfg.cpp" -o "$DIR_KIT/bc250memcfg" 2>/dev/null || true
    fi
    if [ -x "$DIR_KIT/bc250memcfg" ]; then
        uma="$("$DIR_KIT/bc250memcfg" 2>/dev/null | grep -i "UMA_SIZE" | head -1)"
        echo "      CMOS: ${uma:-(nao lido)}"
        echo "$uma" | grep -q "256" || echo "      AVISO: a UMA nao parece 256 MB; as corridas do Windows usam 256."
    fi

    echo "[3/5] Acesso do usuario $USUARIO a /dev/dri sem sessao grafica..."
    GRUPO_ADICIONADO=""
    for g in render video; do
        getent group "$g" >/dev/null || continue
        id -nG "$USUARIO" | tr ' ' '\n' | grep -qx "$g" || { usermod -aG "$g" "$USUARIO"; GRUPO_ADICIONADO="$GRUPO_ADICIONADO $g"; }
    done

    echo "[4/5] Entrada do GRUB e servicos..."
    criar_entrada "$ARGS_DC0" || exit 1
    instalar_servicos
    rm -f "$DIR_DADOS/sessao-marcas"
    TOTAL="$SESSOES"; PROXIMA=1; MODO="dc0"; PERFIL="$PERFIL"
    gravar_estado
    armar_proximo_boot || { echo "ERRO ao armar o GRUB; desfazendo."; limpar; rm -f "$ESTADO"; exit 1; }
    log "PREPARADA L1 sessoes=$TOTAL duracao=${DURACAO}s usuario=$USUARIO"

    echo "[5/5] Pronto."
    echo "=================================================================="
    echo " A maquina vai DESLIGAR. Espere uns segundos (boot frio) e LIGUE."
    echo " Cada sessao: tela some ou congela (esperado), ~11-12 min, e a maquina"
    echo " DESLIGA SOZINHA. Ligue de novo para a proxima."
    echo " Se depois de ligada ela NAO desligar em ~15 min: travou. Anote a hora,"
    echo " de Reset; o boot seguinte e' normal e coleta tudo sozinho."
    echo " No fim: ~/BC250_L1_RESULTADO.txt e ~/bc250-l1-resultado-*.tar.gz"
    echo "=================================================================="
    desligar
    ;;
# -----------------------------------------------------------------------------
_sessao)   # roda no boot com bc250.l1=1, pelo servico
    ler_estado
    [ -n "${PROXIMA:-}" ] && [ "$PROXIMA" -gt 0 ] || { log "SESSAO sem estado armado; nada a fazer"; exit 0; }
    N="$PROXIMA"; MARCAS="$DIR_DADOS/sessao-marcas"
    log "SESSAO $N/$TOTAL INICIADA modo=$MODO cmdline=$(cat /proc/cmdline)"
    echo "INICIADA_$N=1" >> "$MARCAS"; sync
    extra=""; [ "$MODO" = "controle" ] && extra="--controle-dc1"
    # l1b: sessao 1 limpa (sem leitura de registrador nem SMU em regime); da 2 em diante com varredura.
    if [ "${PERFIL:-l1}" = "l1b" ]; then
        extra="$extra --perfil l1b"; [ "$N" -ge 2 ] && extra="$extra --sweep"
    fi
    bash "$DIR_KIT/bc250-l1-dc0.sh" --auto --sem-reboot -o "$DIR_DADOS" --sessao "$N" \
        -d "$DURACAO" --usuario "$USUARIO" --xdg /run/bc250-l1-xdg $extra \
        > "$DIR_DADOS/sessao-$N-console.txt" 2>&1
    rc=$?
    ult="$(ls -1dt "$DIR_DADOS"/bc250-l1-*-s"$N"/ 2>/dev/null | head -1)"; ult="${ult%/}"
    if [ -n "$ult" ] && [ -f "$ult/RESUMO.txt" ]; then
        echo "TERMINADA_$N=1" >> "$MARCAS"; log "SESSAO $N TERMINADA rc=$rc pasta=$ult"
    else
        # o script recusou ou abortou antes da carga (ex.: dc errado): nao e' trava
        echo "RECUSADA_$N=1" >> "$MARCAS"; log "SESSAO $N SEM RESUMO rc=$rc (ver sessao-$N-console.txt)"
    fi
    sync
    if [ "$N" -lt "$TOTAL" ]; then
        PROXIMA=$((N + 1)); gravar_estado
        armar_proximo_boot && log "PROXIMO BOOT = sessao $PROXIMA" || log "ERRO ao armar a sessao $PROXIMA"
    else
        PROXIMA=0; gravar_estado
        log "TODAS AS SESSOES FEITAS; proximo boot e' o normal (coleta)"
    fi
    desligar
    ;;
# -----------------------------------------------------------------------------
_apos-boot)  # roda no boot normal, pelo servico
    ler_estado
    [ -n "${TOTAL:-}" ] || exit 0
    MARCAS="$DIR_DADOS/sessao-marcas"
    # Sessao iniciada e sem marca de termino = a maquina morreu durante ela.
    TRAVOU=""
    for k in $(seq 1 "$TOTAL"); do
        if grep -qx "INICIADA_$k=1" "$MARCAS" 2>/dev/null && ! grep -qx "TERMINADA_$k=1" "$MARCAS" 2>/dev/null \
           && ! grep -qx "RECUSADA_$k=1" "$MARCAS" 2>/dev/null; then TRAVOU="$k"; fi
    done
    if [ -n "$TRAVOU" ]; then
        log "BOOT NORMAL depois de TRAVA na sessao $TRAVOU: coleta pos-trava"
        bash "$DIR_KIT/bc250-l1-dc0.sh" --pos-trava -o "$DIR_DADOS" > "$DIR_DADOS/pos-trava-console.txt" 2>&1
        dmesg 2>/dev/null | grep -iE "reset reason|previous system reset" > "$DIR_DADOS/reset-reason-boot-pos-trava.txt"
        empacotar "TRAVOU na sessao $TRAVOU (candidato ao desfecho B; rodar 'controle' so' depois da analise)"
        limpar; rm -f "$ESTADO"
        exit 0
    fi
    if [ "${PROXIMA:-0}" -eq 0 ]; then
        log "BOOT NORMAL com todas as sessoes feitas: empacotando"
        n_ok="$(grep -c "^TERMINADA_" "$MARCAS" 2>/dev/null)"
        empacotar "$n_ok de $TOTAL sessoes terminaram sem trava (modo $MODO, perfil ${PERFIL:-l1})"
        limpar; rm -f "$ESTADO"
        exit 0
    fi
    # Boot normal no meio do caminho (alguem escolheu a entrada normal): rearmar.
    armar_proximo_boot && log "BOOT NORMAL no meio do ensaio: sessao $PROXIMA rearmada para o proximo boot"
    ;;
# -----------------------------------------------------------------------------
controle)
    mkdir -p "$DIR_DADOS"
    [ -f "$ESTADO" ] && { echo "Ja existe ensaio armado ('status' / 'cancelar')."; exit 1; }
    [ -x "$DIR_KIT/bc250-l1-dc0.sh" ] || [ -f "$DIR_KIT/bc250-l1-dc0.sh" ] || { echo "Rode 'preparar' antes (kit ausente em $DIR_KIT)."; exit 1; }
    USUARIO="${SUDO_USER:-}"; GRUPO_ADICIONADO=""
    for g in render video; do
        getent group "$g" >/dev/null || continue
        id -nG "$USUARIO" | tr ' ' '\n' | grep -qx "$g" || { usermod -aG "$g" "$USUARIO"; GRUPO_ADICIONADO="$GRUPO_ADICIONADO $g"; }
    done
    criar_entrada "$ARGS_DC1" || exit 1
    instalar_servicos
    rm -f "$DIR_DADOS/sessao-marcas"
    TOTAL=1; PROXIMA=1; MODO="controle"; gravar_estado
    armar_proximo_boot || { limpar; rm -f "$ESTADO"; exit 1; }
    log "PREPARADO CONTROLE dc normal, 1 sessao"
    echo "Controle armado. A maquina vai desligar; ligue e espere ela desligar sozinha."
    desligar
    ;;
# -----------------------------------------------------------------------------
status)
    ler_estado
    echo "estado:";            cat "$ESTADO" 2>/dev/null || echo "  (nenhum ensaio armado)"
    echo "marcas:";            cat "$DIR_DADOS/sessao-marcas" 2>/dev/null || echo "  (nenhuma)"
    echo "grubenv:";           grub2-editenv list 2>/dev/null | grep next_entry || echo "  (sem next_entry)"
    echo "entrada:";           [ -f "$ENTRADA_ARQ" ] && grep -E "^(title|options)" "$ENTRADA_ARQ" || echo "  (nao existe)"
    echo "servicos:";          systemctl is-enabled bc250-l1-sessao.service bc250-l1-coleta.service 2>/dev/null
    echo "ultimas linhas do log:"; tail -8 "$DIR_DADOS/auto.log" 2>/dev/null
    ;;
# -----------------------------------------------------------------------------
cancelar)
    limpar; rm -f "$ESTADO"
    log "CANCELADO: entrada, servicos e next_entry removidos (dados mantidos em $DIR_DADOS)"
    ;;
*)
    sed -n '2,26p' "$0"; exit 2 ;;
esac
