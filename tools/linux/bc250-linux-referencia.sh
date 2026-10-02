#!/usr/bin/env bash
# =============================================================================
# bc250-linux-referencia.sh -- Captura Total, Diagnóstico e Estabilidade BC-250
# Frente L do PLANO_DESKTOP_ACELERADO.md (Atualizado em 01/10/2026)
#
# Alvo: AMD BC-250 (1002:13FE, GC 10.1.3 / Cyan Skillfish 2)
#
# OBJETIVOS:
# 1. Leitura e ajuste da CMOS APCB VRAM (UMA_SIZE) via bc250memcfg integrado
# 2. Captura exaustiva de hardware: BARs PCI, EDID real via AUX, PowerPlay Table (SMU),
#    microcodes de firmware carregados, mapeamento de memória física (/proc/iomem)
# 3. Monitoramento contínuo em tempo real com gravação segura (sync a cada 2s)
#    para registrar exatamente o segundo e as condições caso ocorra freeze
# 4. Modo pós-trava (--pos-trava) para recuperação forense após reinicialização
#
# USO (como root, a partir do pendrive gravável):
#   sudo bash bc250-linux-referencia.sh                     # Inventário completo + monitor 600s
#   sudo bash bc250-linux-referencia.sh -d 900              # Monitor por 900 segundos
#   sudo bash bc250-linux-referencia.sh --apenas-dados      # Só inventário (rápido, ~20s)
#   sudo bash bc250-linux-referencia.sh --ler-vram          # Lê e exibe UMA_SIZE da CMOS APCB
#   sudo bash bc250-linux-referencia.sh --set-vram 2048     # Grava 2048 MB na CMOS APCB e sai
#   sudo bash bc250-linux-referencia.sh --verificar-amdgpu  # Só diagnostica amdgpu + Mesa e a tabela de IP discovery
#   sudo bash bc250-linux-referencia.sh --instalar          # Instala SÓ o que faltar da pilha amdgpu + Mesa (mínimo)
#   sudo bash bc250-linux-referencia.sh --instalar-extras   # Idem + lm_sensors/edid-decode/glmark2
#        (com --sim confirma sozinho; com --forcar ignora o piso de espaço livre)
#   sudo bash bc250-linux-referencia.sh --pos-trava         # Executar após reboot se travar
#   sudo bash bc250-linux-referencia.sh -o /caminho/pasta   # Especifica pasta de saída
#
# O ensaio de estabilidade só separa "placa" de "driver Windows" se o amdgpu
# estiver ATIVO. Com llvmpipe a máquina não trava, mas isso não mede a GPU.
# =============================================================================

set -u
VERSAO="2.1 (01/10/2026 - pilha amdgpu, sonda de IP discovery, carga de paginação)"
DURACAO=600
INTERVALO=2
INSTALAR=0
INSTALAR_EXTRAS=0
VERIFICAR_AMDGPU=0
SIM=0
FORCAR=0
ESPACO_MINIMO_MB=300
POS_TRAVA=0
APENAS_DADOS=0
LER_VRAM=0
SET_VRAM=""
AUTO_CARGA=1
SAIDA_BASE=""

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--duracao)        DURACAO="$2"; shift 2 ;;
        -o|--saida)          SAIDA_BASE="$2"; shift 2 ;;
        --instalar)          INSTALAR=1; shift ;;
        --instalar-extras)   INSTALAR=1; INSTALAR_EXTRAS=1; shift ;;
        --verificar-amdgpu)  VERIFICAR_AMDGPU=1; shift ;;
        --sim|-y)            SIM=1; shift ;;
        --forcar)            FORCAR=1; shift ;;
        --pos-trava)         POS_TRAVA=1; shift ;;
        --apenas-dados|--rapido|--inventario) APENAS_DADOS=1; shift ;;
        --ler-vram)          LER_VRAM=1; shift ;;
        --set-vram)          SET_VRAM="$2"; shift 2 ;;
        --sem-carga)         AUTO_CARGA=0; shift ;;
        -h|--help)
            sed -n '2,36p' "$0"
            exit 0
            ;;
        *)
            echo "Opção desconhecida: $1"
            echo "Use -h ou --help para ver as instruções."
            exit 2
            ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "ERRO: Este script exige privilégios de root (acesso a MMIO, I/O ports e debugfs)."
    echo "Execute: sudo bash $0 $*"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Helper para compilar bc250memcfg se necessário --------------------------
compilar_memcfg() {
    local bin="$SCRIPT_DIR/bc250memcfg"
    if [ -x "$bin" ]; then
        return 0
    fi
    local src="$SCRIPT_DIR/bc250_memcfg.cpp"
    if [ ! -f "$src" ]; then
        src="$SCRIPT_DIR/../../referencias/bc250_memcfg/main.cpp"
    fi
    if [ -f "$src" ]; then
        if command -v g++ >/dev/null 2>&1; then
            echo "Compilando bc250memcfg via g++..."
            g++ -O2 "$src" -o "$bin" >/dev/null 2>&1 && chmod +x "$bin"
        elif command -v clang++ >/dev/null 2>&1; then
            echo "Compilando bc250memcfg via clang++..."
            clang++ -O2 "$src" -o "$bin" >/dev/null 2>&1 && chmod +x "$bin"
        fi
    fi
    [ -x "$bin" ]
}

# =============================================================================
# PILHA amdgpu + Mesa: verificação, sonda da tabela de IP discovery e
# instalação mínima. Na coleta de 01/10 (doc 549) módulo e firmware JÁ estavam
# instalados e o amdgpu falhou no probe ("invalid ip discovery binary
# signature"). Por isso a verificação separa "falta pacote" de "o probe falhou":
# no segundo caso instalar pacote não resolve nada.
# =============================================================================
FW_ESPERADOS="ce me mec pfp rlc sdma sdma1"
PILHA_FALTA=""
ESSENCIAL_FALTA=""
AMDGPU_VINCULADO=0
PILHA_ESTADO="NAO_VERIFICADA"
PROBE_ERRO=""

achar_bdf() {
    local d
    for d in /sys/bus/pci/devices/*; do
        [ "$(cat "$d/vendor" 2>/dev/null)" = "0x1002" ] || continue
        [ "$(cat "$d/device" 2>/dev/null)" = "0x13fe" ] || continue
        basename "$d"
        return 0
    done
    return 1
}

achar_arquivo() {
    local p
    for p in "$@"; do
        [ -e "$p" ] && { echo "$p"; return 0; }
    done
    return 1
}

faltou() {  # $1 = chave, $2 = 1 se essencial para o amdgpu/Mesa
    PILHA_FALTA="$PILHA_FALTA $1"
    [ "$2" = "1" ] && ESSENCIAL_FALTA="$ESSENCIAL_FALTA $1"
}

versao_mesa() {
    if command -v rpm >/dev/null 2>&1; then rpm -q mesa-dri-drivers mesa-vulkan-drivers 2>/dev/null | tr '\n' ' '
    elif command -v dpkg-query >/dev/null 2>&1; then dpkg-query -W libgl1-mesa-dri mesa-vulkan-drivers 2>/dev/null | tr '\n' ' '
    elif command -v pacman >/dev/null 2>&1; then pacman -Q mesa vulkan-radeon 2>/dev/null | tr '\n' ' '
    fi
}

verificar_pilha_amdgpu() {
    PILHA_FALTA=""; ESSENCIAL_FALTA=""; AMDGPU_VINCULADO=0; PROBE_ERRO=""
    local kv bdf mod f fw_falta="" drv dri icd cl bl
    kv="$(uname -r)"
    echo "=== PILHA amdgpu + Mesa ($(date -Is)) ==="
    echo "Kernel: $kv"

    bdf="$(achar_bdf)"
    if [ -n "$bdf" ]; then echo "[OK]    BC-250 (1002:13FE) em $bdf"
    else echo "[FALHA] BC-250 (1002:13FE) não aparece no barramento PCI"; fi

    mod="$(modinfo -n amdgpu 2>/dev/null)"
    if [ -n "$mod" ] && [ -e "$mod" ]; then echo "[OK]    módulo amdgpu: $mod"
    elif grep -qs '/amdgpu.ko' "/lib/modules/$kv/modules.builtin"; then echo "[OK]    amdgpu embutido no kernel"
    else echo "[FALTA] amdgpu.ko para o kernel $kv"; faltou modulo 1; fi
    if [ -d /sys/module/amdgpu ]; then echo "[OK]    módulo amdgpu carregado"
    else echo "[--]    módulo amdgpu NÃO carregado"; fi

    for f in $FW_ESPERADOS; do
        achar_arquivo "/lib/firmware/amdgpu/cyan_skillfish2_$f.bin" \
                      "/lib/firmware/amdgpu/cyan_skillfish2_$f.bin.xz" \
                      "/lib/firmware/amdgpu/cyan_skillfish2_$f.bin.zst" >/dev/null || fw_falta="$fw_falta $f"
    done
    if [ -z "$fw_falta" ]; then echo "[OK]    firmware cyan_skillfish2_{${FW_ESPERADOS// /,}}"
    else echo "[FALTA] firmware cyan_skillfish2:$fw_falta"; faltou firmware 1; fi

    cl="$(cat /proc/cmdline)"
    case " $cl " in
        *" nomodeset "*|*" amdgpu.modeset=0 "*) echo "[FALHA] a linha do kernel DESLIGA o amdgpu: $cl" ;;
    esac
    echo "        parâmetros amdgpu na cmdline: $(echo "$cl" | tr ' ' '\n' | grep -E '^amdgpu\.|^modprobe\.blacklist=' | tr '\n' ' ')"
    bl="$(grep -rhsE '^[[:space:]]*(blacklist|install)[[:space:]]+amdgpu' /etc/modprobe.d /usr/lib/modprobe.d /lib/modprobe.d 2>/dev/null)"
    [ -n "$bl" ] && echo "[FALHA] amdgpu bloqueado em modprobe.d: $bl"

    if [ -n "$bdf" ] && [ -e "/sys/bus/pci/devices/$bdf/driver" ]; then
        drv="$(basename "$(readlink -f "/sys/bus/pci/devices/$bdf/driver")")"
    else
        drv="nenhum"
    fi
    if [ "$drv" = "amdgpu" ]; then echo "[OK]    amdgpu VINCULADO à placa"; AMDGPU_VINCULADO=1
    else echo "[FALHA] driver vinculado à placa: $drv (o amdgpu não assumiu)"; fi

    PROBE_ERRO="$(dmesg 2>/dev/null | grep -E 'amdgpu.*(ip discovery|Fatal error during GPU init|probe with driver amdgpu failed|failed to load|firmware)|Direct firmware load for amdgpu' | tail -8)"
    if [ -n "$PROBE_ERRO" ]; then
        echo "        dmesg do probe:"
        echo "$PROBE_ERRO" | sed 's/^/          /'
    fi

    dri="$(achar_arquivo /usr/lib64/dri/radeonsi_dri.so /usr/lib/x86_64-linux-gnu/dri/radeonsi_dri.so /usr/lib/dri/radeonsi_dri.so)"
    if [ -n "$dri" ]; then echo "[OK]    Mesa OpenGL radeonsi: $dri"
    else echo "[FALTA] Mesa OpenGL radeonsi (radeonsi_dri.so)"; faltou radeonsi 1; fi
    icd="$(ls /usr/share/vulkan/icd.d/radeon_icd*.json 2>/dev/null | head -1)"
    if [ -n "$icd" ]; then echo "[OK]    Mesa Vulkan RADV: $icd"
    else echo "[FALTA] Mesa Vulkan RADV (radeon_icd*.json)"; faltou radv 1; fi
    echo "        versão Mesa: $(versao_mesa)"

    if command -v vulkaninfo >/dev/null 2>&1 && command -v vkcube >/dev/null 2>&1; then echo "[OK]    vulkaninfo e vkcube"
    else echo "[FALTA] vulkan-tools (vulkaninfo/vkcube)"; faltou vulkan_tools 0; fi
    if command -v glxinfo >/dev/null 2>&1; then echo "[OK]    glxinfo"
    else echo "[FALTA] glxinfo"; faltou glx_utils 0; fi

    if [ "$AMDGPU_VINCULADO" -eq 1 ]; then PILHA_ESTADO="AMDGPU_ATIVO"
    elif [ -n "$ESSENCIAL_FALTA" ]; then PILHA_ESTADO="PILHA_INCOMPLETA"
    else PILHA_ESTADO="PILHA_COMPLETA_PROBE_FALHOU"; fi

    echo
    echo "ESTADO: $PILHA_ESTADO"
    case "$PILHA_ESTADO" in
        AMDGPU_ATIVO)
            echo "  O amdgpu assumiu a placa. O ensaio de carga mede a GPU de verdade." ;;
        PILHA_INCOMPLETA)
            echo "  Falta:$ESSENCIAL_FALTA. Rode com --instalar e depois faça boot frio." ;;
        PILHA_COMPLETA_PROBE_FALHOU)
            echo "  Módulo, firmware e Mesa estão instalados e o amdgpu NÃO assumiu a placa."
            echo "  Instalar pacote NÃO resolve. Veja a sonda de IP discovery abaixo e o doc 549 §3." ;;
    esac
}

# Lê (só leitura, /dev/mem) o topo do carveout da UMA, onde o firmware deixa a
# tabela de IP discovery, e procura as assinaturas. Segundo o upstream
# (amdgpu_discovery.c, P1 a conferir no fonte do kernel em uso) o amdgpu lê a
# tabela em "tamanho da VRAM - 64 KiB" e exige binary_signature 0x28211407; o
# cabeçalho de tabela tem 0x53445049 ("IPDS").
sonda_tabela_discovery() {
    local destino="$1" l r s e tam=0 ini=0 fim=0 topo jan bin esperado achados
    mkdir -p "$destino"
    while IFS= read -r l; do
        case "$l" in " "*) continue ;; esac
        case "$l" in *" : Reserved") ;; *) continue ;; esac
        r="${l%% : *}"
        s=$((16#${r%-*})); e=$((16#${r#*-}))
        if [ "$s" -ge $((1 << 32)) ] && [ $((e - s + 1)) -gt "$tam" ]; then
            tam=$((e - s + 1)); ini=$s; fim=$e
        fi
    done < /proc/iomem
    echo "=== SONDA DA TABELA DE IP DISCOVERY ($(date -Is)) ==="
    if [ "$tam" -lt $((256 << 20)) ]; then
        echo "Nenhuma faixa Reserved >= 256 MiB acima de 4 GiB em /proc/iomem. Sonda abortada."
        return 1
    fi
    topo=$((fim + 1)); jan=$((topo - (1 << 20)))
    printf "Carveout da UMA (maior Reserved acima de 4 GiB): 0x%X-0x%X = %d MiB\n" "$ini" "$fim" $((tam >> 20))
    printf "Janela lida: 0x%X..0x%X (último 1 MiB); posição esperada da tabela: 0x%X (topo - 64 KiB)\n" "$jan" "$((topo - 1))" "$((topo - 65536))"
    bin="$destino/uma-topo-1MiB.bin"
    if ! dd if=/dev/mem of="$bin" bs=65536 skip=$((jan / 65536)) count=16 status=none 2>"$destino/dd-erro.txt" \
       || [ "$(stat -c %s "$bin" 2>/dev/null || echo 0)" -ne $((1 << 20)) ]; then
        echo "LEITURA RECUSADA: $(cat "$destino/dd-erro.txt")"
        echo "  Provável STRICT_DEVMEM. Para um boot só, acrescente 'iomem=relaxed' na linha do kernel"
        echo "  (no GRUB: tecla e, fim da linha linux, Ctrl+X) e repita --verificar-amdgpu."
        return 1
    fi
    sha256sum "$bin" > "$bin.sha256" 2>/dev/null
    esperado="$(od -A n -t x4 -j $(((1 << 20) - 65536)) -N 4 "$bin" | tr -d ' ')"
    echo "dword na posição esperada: 0x$esperado"
    achados="$(od -A x -t x4 -v -w4 "$bin" | awk '$2=="28211407"{print "  binary_signature 0x28211407 em +0x" $1} $2=="53445049"{print "  IPDS 0x53445049 em +0x" $1}' | head -20)"
    if [ "$esperado" = "28211407" ]; then
        echo "RESULTADO: assinatura PRESENTE onde o amdgpu procura (P0 da leitura)."
        echo "  Então a falha do probe não é tabela ausente no topo do carveout; o amdgpu deve estar"
        echo "  calculando outro tamanho/posição (H). Compare 'VRAM' no dmesg com o tamanho acima."
    elif [ -n "$achados" ]; then
        echo "RESULTADO: assinatura FORA da posição esperada (P0 da leitura). Offsets na janela:"
        echo "$achados"
        echo "  O firmware gravou a tabela em outro lugar relativo ao topo da UMA (H: relação com o UMA_SIZE)."
    else
        echo "RESULTADO: NENHUMA assinatura no último 1 MiB do carveout (P0 da leitura)."
        echo "  Reforça a H1 do doc 549: com este UMA_SIZE o firmware não deixou a tabela onde o amdgpu lê."
        echo "  Teste que separa: UMA_SIZE=512 na CMOS, boot frio, repetir --verificar-amdgpu."
    fi
    echo "Dump arquivado: $bin"
}

# Instala SÓ o que faltar, sem dependências fracas/recomendadas, sem docs, e
# limpa o cache do gerenciador no fim. Nunca instala kernel novo.
instalar_pilha_minima() {
    local kv pm="" pkgs="" k livre raiz resp
    kv="$(uname -r)"
    verificar_pilha_amdgpu
    echo
    if command -v dnf >/dev/null 2>&1; then pm=dnf
    elif command -v apt-get >/dev/null 2>&1; then pm=apt
    elif command -v pacman >/dev/null 2>&1; then pm=pacman
    else echo "ERRO: gerenciador de pacotes desconhecido (dnf/apt/pacman)."; return 1; fi

    for k in $PILHA_FALTA; do
        case "$pm:$k" in
            dnf:modulo)        pkgs="$pkgs kernel-modules-core-$kv kernel-modules-$kv" ;;
            dnf:firmware)      pkgs="$pkgs amd-gpu-firmware" ;;
            dnf:radeonsi)      pkgs="$pkgs mesa-dri-drivers" ;;
            dnf:radv)          pkgs="$pkgs mesa-vulkan-drivers" ;;
            dnf:vulkan_tools)  pkgs="$pkgs vulkan-tools" ;;
            dnf:glx_utils)     pkgs="$pkgs glx-utils" ;;
            apt:modulo)        pkgs="$pkgs linux-modules-extra-$kv" ;;
            apt:firmware)      if apt-cache show firmware-amd-graphics >/dev/null 2>&1; then pkgs="$pkgs firmware-amd-graphics"; else pkgs="$pkgs linux-firmware"; fi ;;
            apt:radeonsi)      pkgs="$pkgs libgl1-mesa-dri" ;;
            apt:radv)          pkgs="$pkgs mesa-vulkan-drivers" ;;
            apt:vulkan_tools)  pkgs="$pkgs vulkan-tools" ;;
            apt:glx_utils)     pkgs="$pkgs mesa-utils" ;;
            pacman:modulo)     echo "AVISO: no Arch o amdgpu vem no pacote do kernel; não instalo kernel novo." ;;
            pacman:firmware)   if pacman -Si linux-firmware-amdgpu >/dev/null 2>&1; then pkgs="$pkgs linux-firmware-amdgpu"; else pkgs="$pkgs linux-firmware"; fi ;;
            pacman:radeonsi)   pkgs="$pkgs mesa" ;;
            pacman:radv)       pkgs="$pkgs vulkan-radeon" ;;
            pacman:vulkan_tools) pkgs="$pkgs vulkan-tools" ;;
            pacman:glx_utils)  pkgs="$pkgs mesa-utils" ;;
        esac
    done
    if [ "$INSTALAR_EXTRAS" -eq 1 ]; then
        command -v sensors >/dev/null 2>&1 || case "$pm" in dnf|pacman) pkgs="$pkgs lm_sensors" ;; apt) pkgs="$pkgs lm-sensors" ;; esac
        command -v edid-decode >/dev/null 2>&1 || case "$pm" in dnf|apt) pkgs="$pkgs edid-decode" ;; esac
        command -v glmark2 >/dev/null 2>&1 || case "$pm" in dnf|apt) pkgs="$pkgs glmark2" ;; esac
    fi
    pkgs="$(echo "$pkgs" | tr ' ' '\n' | awk 'NF && !v[$0]++' | tr '\n' ' ')"

    if [ -z "$pkgs" ]; then
        echo "Nada a instalar."
        [ "$PILHA_ESTADO" = "PILHA_COMPLETA_PROBE_FALHOU" ] && \
            echo "A pilha está completa; o que falha é a inicialização do amdgpu. Use --verificar-amdgpu."
        return 0
    fi

    raiz="$(df -P / | awk 'NR==2{print $1}')"
    livre="$(df -Pm / | awk 'NR==2{print $4}')"
    echo "Pacotes a instalar ($pm):$pkgs"
    echo "Espaço livre em /: ${livre} MB (piso exigido: ${ESPACO_MINIMO_MB} MB)"
    case "$raiz" in
        overlay|tmpfs|/dev/loop*|/dev/mapper/live-rw)
            echo "AVISO: sessão Live. A instalação vai para a RAM e some no próximo boot." ;;
    esac
    if [ "${livre:-0}" -lt "$ESPACO_MINIMO_MB" ] && [ "$FORCAR" -ne 1 ]; then
        echo "RECUSADO: pouco espaço. Libere espaço (ex.: 'dnf clean all', 'journalctl --vacuum-size=50M')"
        echo "ou repita com --forcar se o resumo abaixo couber."
        return 1
    fi

    echo "--- Simulação (tamanho de download/instalação) ---"
    case "$pm" in
        dnf)    dnf install --assumeno --setopt=install_weak_deps=False --setopt=tsflags=nodocs $pkgs 2>&1 | tail -15 ;;
        apt)    apt-get update -qq >/dev/null 2>&1
                apt-get install -s --no-install-recommends $pkgs 2>&1 | grep -E '^(Inst|[0-9]+ upgraded|[0-9]+ atualizados|Need to get|After this)' | tail -15 ;;
        pacman) pacman -Sp --needed --print-format '%n %s' $pkgs 2>&1 | awk '{s+=$2; print} END{printf "download total: %.1f MB\n", s/1048576}' ;;
    esac

    if [ "$SIM" -ne 1 ]; then
        echo -n "Instalar? [s/N]: "
        read -r resp
        [ "$resp" = "s" ] || [ "$resp" = "S" ] || { echo "Cancelado."; return 1; }
    fi

    case "$pm" in
        dnf)    dnf install -y --setopt=install_weak_deps=False --setopt=tsflags=nodocs --setopt=keepcache=False $pkgs
                dnf clean packages >/dev/null 2>&1 ;;
        apt)    apt-get install -y --no-install-recommends $pkgs
                apt-get clean ;;
        pacman) pacman -S --needed --noconfirm $pkgs
                pacman -Sc --noconfirm >/dev/null 2>&1 ;;
    esac
    echo "Espaço livre em / depois: $(df -Pm / | awk 'NR==2{print $4}') MB"
    echo
    echo "Se instalou módulo ou firmware: desligue da tomada (boot frio) e rode --verificar-amdgpu."
}

# Programas gráficos precisam da sessão do usuário, não do ambiente do sudo.
como_usuario() {
    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        local uid; uid="$(id -u "$SUDO_USER")"
        sudo -u "$SUDO_USER" env XDG_RUNTIME_DIR="/run/user/$uid" \
            WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" DISPLAY="${DISPLAY:-:0}" "$@"
    else
        "$@"
    fi
}

if [ "$INSTALAR" -eq 1 ]; then
    instalar_pilha_minima
    exit $?
fi

# --- Modo leitura rápida de VRAM CMOS -----------------------------------------
if [ "$LER_VRAM" -eq 1 ]; then
    echo "=== LEITURA DE CMOS APCB (VRAM / UMA_SIZE) ==="
    if compilar_memcfg; then
        "$SCRIPT_DIR/bc250memcfg"
    else
        echo "ERRO: bc250memcfg não encontrado e g++ não disponível para compilar."
        echo "Instale g++ (ex: sudo dnf install -y gcc-c++) ou compile previamente."
    fi
    exit 0
fi

# --- Modo gravação de VRAM na CMOS APCB ---------------------------------------
if [ -n "$SET_VRAM" ]; then
    echo "=== AJUSTE DE VRAM NA CMOS APCB (UMA_SIZE = $SET_VRAM MB) ==="
    if compilar_memcfg; then
        "$SCRIPT_DIR/bc250memcfg" UMA_SIZE "$SET_VRAM"
        echo
        echo "------------------------------------------------------------------"
        echo "VALOR GRAVADO NA CMOS COM SUCESSO."
        echo "ATENÇÃO: Desligue a máquina completamente (boot frio / power cycle)"
        echo "para que a ABL e o BIOS apliquem a nova geometria de VRAM."
        echo "------------------------------------------------------------------"
    else
        echo "ERRO: bc250memcfg não encontrado e compilador indisponível."
    fi
    exit 0
fi

# --- Onde gravar os resultados -----------------------------------------------
if [ -z "$SAIDA_BASE" ]; then SAIDA_BASE="$SCRIPT_DIR"; fi
if ! touch "$SAIDA_BASE/.bc250_teste_escrita" 2>/dev/null; then
    echo "ERRO: Não consigo gravar em $SAIDA_BASE."
    echo "Use -o com uma pasta em pendrive GRAVÁVEL (pendrives de ISO puro costumam ser read-only)."
    exit 1
fi
rm -f "$SAIDA_BASE/.bc250_teste_escrita"

case "$(df -P "$SAIDA_BASE" | awk 'NR==2{print $1}')" in
    tmpfs|overlay|/dev/loop*|rootfs|/dev/mapper/live-rw)
        echo "AVISO: $SAIDA_BASE está na RAM da sessão ao vivo (Live USB)."
        echo "       Se a máquina travar durante o teste, todos os dados na RAM serão perdidos!"
        echo "       Recomenda-se fortemente usar -o apontando para um pendrive persistente gravável."
        echo -n "Deseja continuar mesmo assim na RAM? [s/N]: "
        read -r resp; [ "$resp" = "s" ] || [ "$resp" = "S" ] || exit 1
        ;;
esac

sincronizar() { sync -f "$1" 2>/dev/null || sync; }

# --- Modo pós-trava (Forense após reinicialização forçada) --------------------
if [ "$POS_TRAVA" -eq 1 ]; then
    ULT="$(ls -1dt "$SAIDA_BASE"/bc250-linux-*/ 2>/dev/null | head -1)"; ULT="${ULT%/}"
    if [ -z "$ULT" ]; then
        echo "Nenhuma pasta bc250-linux-* encontrada em $SAIDA_BASE para análise pós-trava."
        exit 1
    fi
    P="$ULT/pos-trava-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$P"
    {
        echo "=================================================================="
        echo "=== ANÁLISE FORENSE PÓS-TRAVA ($(date -Is)) ==="
        echo "=== Diretório de evidência original: $ULT"
        echo "=================================================================="
        echo
        echo "--- Último batimento registrado antes do congelamento ---"
        tail -5 "$ULT/batimento.txt" 2>/dev/null || echo "(arquivo batimento.txt não encontrado)"
        echo
        echo "--- Últimas 20 amostras do monitor contínuo (CSV) ---"
        tail -20 "$ULT/monitor.csv" 2>/dev/null || echo "(arquivo monitor.csv não encontrado)"
        echo
        echo "--- Últimos eventos marcados ---"
        tail -15 "$ULT/eventos.txt" 2>/dev/null || echo "(arquivo eventos.txt não encontrado)"
        echo
        echo "--- Últimas 80 linhas do dmesg gravadas em disco antes da parada ---"
        tail -80 "$ULT/dmesg-continuo.txt" 2>/dev/null || echo "(arquivo dmesg-continuo.txt não encontrado)"
    } > "$P/resumo-pos-trava.txt"

    journalctl -b -1 -k --no-pager > "$P/journal-boot-anterior-kernel.txt" 2>&1 \
        || echo "journal do boot anterior indisponível (normal em sessões Live sem persistência)" > "$P/journal-boot-anterior-kernel.txt"
    journalctl --list-boots --no-pager > "$P/lista-de-boots.txt" 2>&1

    sincronizar "$P/resumo-pos-trava.txt"
    cat "$P/resumo-pos-trava.txt"
    echo
    echo "Evidência pós-trava salva com sucesso em: $P"
    exit 0
fi

# --- Modo diagnóstico da pilha amdgpu (rápido, sem monitor) -------------------
if [ "$VERIFICAR_AMDGPU" -eq 1 ]; then
    OUT="$SAIDA_BASE/bc250-amdgpu-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$OUT"
    {
        verificar_pilha_amdgpu
        echo
        if compilar_memcfg; then
            echo "=== CMOS APCB ==="
            "$SCRIPT_DIR/bc250memcfg" 2>&1 | grep -E "Signature|Checksum|UMA_SIZE" || true
            echo
        fi
        echo "=== /proc/iomem (faixas de topo) ==="
        grep -E '^[0-9a-f]+-[0-9a-f]+ : (System RAM|Reserved)' /proc/iomem
        echo
        echo "=== dmesg amdgpu ==="
        dmesg -T 2>/dev/null | grep -i amdgpu | head -60
        echo
        if [ "$PILHA_ESTADO" != "AMDGPU_ATIVO" ]; then
            sonda_tabela_discovery "$OUT/discovery"
        fi
    } 2>&1 | tee "$OUT/pilha-amdgpu.txt"
    sincronizar "$OUT/pilha-amdgpu.txt"
    echo
    echo "Relatório em: $OUT (copie para docs/evidence/ no Windows)"
    exit 0
fi

# --- Criação da pasta de saída -----------------------------------------------
OUT="$SAIDA_BASE/bc250-linux-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT/inventario"
mkdir -p "$OUT/firmware"
mkdir -p "$OUT/edid"
mkdir -p "$OUT/powerplay"
INV="$OUT/inventario"
FW_DIR="$OUT/firmware"
EDID_DIR="$OUT/edid"
PP_DIR="$OUT/powerplay"

echo "=================================================================="
echo "    BC-250 LINUX REFERENCE CAPTURE & STABILITY TOOLKIT"
echo "    Versão: $VERSAO"
echo "    Início: $(date -Is)"
echo "    Destino: $OUT"
echo "=================================================================="

echo "bc250-linux-referencia $VERSAO -- início $(date -Is)" > "$OUT/LEIA-ME.txt"

rodar() {
    local arq="$1"; shift
    { echo "\$ $*"; "$@" 2>&1; echo "[saida=$?]"; } > "$INV/$arq" 2>&1
}

copiar() {
    if [ -r "$2" ]; then
        { echo "# $2"; cat "$2" 2>&1; } > "$INV/$1"
    else
        echo "# $2 AUSENTE" > "$INV/$1"
    fi
}

# --- Pilha amdgpu: define se o ensaio mede a GPU ------------------------------
echo "  -> Verificando a pilha amdgpu + Mesa..."
verificar_pilha_amdgpu > "$OUT/pilha-amdgpu.txt" 2>&1
sincronizar "$OUT/pilha-amdgpu.txt"
grep -E '^\[(FALTA|FALHA)\]|^ESTADO' "$OUT/pilha-amdgpu.txt"
if [ "$PILHA_ESTADO" != "AMDGPU_ATIVO" ]; then
    sonda_tabela_discovery "$OUT/discovery" > "$OUT/discovery-sonda.txt" 2>&1
    grep -E '^RESULTADO|^LEITURA' "$OUT/discovery-sonda.txt"
    if [ "$APENAS_DADOS" -ne 1 ]; then
        echo
        echo "ATENÇÃO: amdgpu INATIVO ($PILHA_ESTADO). Sem ele o ensaio roda em llvmpipe e"
        echo "NÃO separa placa de driver: 'não travou' não diria nada sobre a GPU."
        echo -n "Continuar só com o inventário e o monitor de CPU? [s/N]: "
        read -r resp
        [ "$resp" = "s" ] || [ "$resp" = "S" ] || { echo "Abortado. Evidência parcial em $OUT"; exit 1; }
    fi
fi

# --- Localização da GPU AMD (BC-250 Cyan Skillfish 1002:13FE) ----------------
CARD=""
for c in /sys/class/drm/card[0-9]*; do
    [ -e "$c/device/vendor" ] || continue
    case "$c" in *-*) continue ;; esac
    if [ "$(cat "$c/device/vendor")" = "0x1002" ]; then
        CARD="$c"
        break
    fi
done

DEV="${CARD:+$CARD/device}"
HWMON=""
if [ -n "$DEV" ]; then
    HWMON="$(ls -d "$DEV"/hwmon/hwmon* 2>/dev/null | head -1)"
fi
DRI_IDX=""
if [ -n "$CARD" ]; then
    DRI_IDX="$(basename "$CARD" | sed 's/card//')"
fi

# =============================================================================
# ETAPA 1: INVENTÁRIO EXAUSTIVO DE HARDWARE E PLATAFORMA
# =============================================================================
echo
echo "[1/4] Coletando inventário exaustivo da plataforma e GPU..."

# 1.1 CMOS APCB (UMA_SIZE e Timings de memória)
echo "  -> Lendo configuração de memória CMOS APCB (portas 0x72/0x73)..."
if compilar_memcfg; then
    "$SCRIPT_DIR/bc250memcfg" > "$INV/cmos-apcb-memcfg.txt" 2>&1
    cp "$INV/cmos-apcb-memcfg.txt" "$OUT/cmos-apcb-resumo.txt"
else
    echo "bc250memcfg indisponível para leitura de CMOS APCB" > "$INV/cmos-apcb-memcfg.txt"
fi

# 1.2 Sistema e Kernel
echo "  -> Coletando parâmetros do kernel, CPU e memória..."
rodar sistema-uname.txt uname -a
copiar sistema-os-release.txt /etc/os-release
copiar sistema-cmdline.txt /proc/cmdline
rodar sistema-lscpu.txt lscpu
rodar sistema-memoria.txt free -m
copiar sistema-meminfo.txt /proc/meminfo
copiar sistema-proc-iomem.txt /proc/iomem
copiar sistema-proc-mtrr.txt /proc/mtrr
copiar sistema-proc-interrupts.txt /proc/interrupts
copiar sistema-proc-zoneinfo.txt /proc/zoneinfo
rodar sistema-dmidecode-bios.txt dmidecode -t bios -t baseboard -t system -t processor -t memory

# 1.3 Módulos de Driver (amdgpu, ttm, drm)
echo "  -> Coletando drivers carregados e parâmetros de módulo..."
rodar sistema-lsmod.txt sh -c 'lsmod | grep -E "amdgpu|drm|ttm|gpu_sched|kvm"'
rodar sistema-modinfo-amdgpu.txt modinfo amdgpu
rodar sistema-parametros-amdgpu.txt sh -c 'for f in /sys/module/amdgpu/parameters/*; do printf "%s=%s\n" "$(basename "$f")" "$(cat "$f" 2>/dev/null)"; done'
rodar sistema-parametros-ttm.txt sh -c 'for f in /sys/module/ttm/parameters/*; do printf "%s=%s\n" "$(basename "$f")" "$(cat "$f" 2>/dev/null)"; done'

# 1.4 PCI Bus, BARs e Config Space da BC-250
echo "  -> Mapeando barramento PCI, BARs e hex dump do config space..."
rodar pci-lspci-nnk.txt lspci -nnk
rodar pci-lspci-tree.txt lspci -tv
rodar pci-gpu-vvv.txt sh -c 'lspci -vvv -d 1002: 2>&1'
rodar pci-bc250-config-space-4096b-hex.txt sh -c 'lspci -xxxx -d 1002:13fe 2>&1'
if [ -n "$DEV" ]; then
    copiar pci-resource-bars.txt "$DEV/resource"
fi

# 1.5 IOMMU
rodar iommu-sysfs.txt sh -c 'ls -la /sys/class/iommu/ 2>&1; ls /sys/kernel/iommu_groups/ 2>&1 | wc -l'
rodar iommu-dmesg.txt sh -c 'dmesg | grep -iE "AMD-Vi|IOMMU|iommu"'

# 1.6 Dmesg Completo do Boot
echo "  -> Capturando dmesg completo do boot com timestamps..."
rodar dmesg-boot-completo.txt dmesg -T
rodar dmesg-amdgpu.txt sh -c 'dmesg -T | grep -iE "amdgpu|drm|gpu|vram|gtt|smu|psp|ring|fence|reset|fault|mc|gmc|sdma"'

# 1.7 Gráficos, Mesa, Vulkan e Sessão
echo "  -> Verificando GL, Vulkan e sessão gráfica..."
rodar graficos-glxinfo.txt glxinfo -B
rodar graficos-vulkaninfo-summary.txt vulkaninfo --summary
rodar graficos-vulkaninfo-completo.txt vulkaninfo
rodar graficos-sessao.txt sh -c 'echo XDG_SESSION_TYPE=$XDG_SESSION_TYPE; loginctl list-sessions 2>&1; ps -eo pid,comm | grep -iE "gnome-shell|kwin|Xorg|Xwayland|mutter"'

# 1.8 Sensores Térmicos
rodar sensores-sensors.txt sensors
rodar sensores-hwmon-todos.txt sh -c 'for h in /sys/class/hwmon/hwmon*; do echo "== $h name=$(cat $h/name 2>/dev/null)"; for f in $h/*_input $h/*_label $h/*_crit $h/*_max; do [ -r "$f" ] && echo "$(basename $f)=$(cat $f 2>/dev/null)"; done; done'

# 1.9 EDID e Display via AUX
echo "  -> Extraindo EDID real de displays conectados e GOP framebuffer..."
for conn in /sys/class/drm/card*-*/; do
    [ -d "$conn" ] || continue
    cname="$(basename "$conn")"
    copiar "display-$cname-status.txt" "$conn/status"
    copiar "display-$cname-enabled.txt" "$conn/enabled"
    copiar "display-$cname-modes.txt" "$conn/modes"
    if [ -s "$conn/edid" ]; then
        cp "$conn/edid" "$EDID_DIR/$cname-edid.bin"
        od -A x -t x1z "$conn/edid" > "$EDID_DIR/$cname-edid-hex.txt"
        if command -v edid-decode >/dev/null 2>&1; then
            edid-decode "$conn/edid" > "$EDID_DIR/$cname-edid-decoded.txt" 2>&1
        fi
        echo "     [+] EDID encontrado e copiado de $cname ($(wc -c < "$conn/edid") bytes)"
    fi
done
copiar gop-proc-fb.txt /proc/fb
for fb in /sys/class/graphics/fb*; do
    [ -d "$fb" ] || continue
    fbname="$(basename "$fb")"
    copiar "gop-$fbname-modes.txt" "$fb/modes"
    copiar "gop-$fbname-bits_per_pixel.txt" "$fb/bits_per_pixel"
    copiar "gop-$fbname-stride.txt" "$fb/stride"
    copiar "gop-$fbname-virtual_size.txt" "$fb/virtual_size"
done

# 1.10 Sysfs da GPU e PowerPlay Table (SMU)
if [ -n "$DEV" ]; then
    echo "  -> Copiando métricas, PowerPlay Table binária e perfis de clock..."
    rodar gpu-sysfs-lista.txt ls -la "$DEV/"
    for f in pp_dpm_sclk pp_dpm_mclk pp_dpm_fclk pp_dpm_socclk pp_dpm_dcefclk pp_dpm_pcie \
             pp_od_clk_voltage power_dpm_force_performance_level power_dpm_state pp_power_profile_mode \
             gpu_busy_percent mem_busy_percent mem_info_vram_total mem_info_vram_used \
             mem_info_vis_vram_total mem_info_vis_vram_used mem_info_gtt_total mem_info_gtt_used \
             vbios_version current_link_speed current_link_width max_link_speed max_link_width \
             pp_features gpu_metrics unique_id; do
        copiar "gpu-$f.txt" "$DEV/$f"
    done

    # PowerPlay Table binária (curvas de tensão/frequência e limites térmicos/potência do SMU)
    if [ -r "$DEV/pp_table" ]; then
        cp "$DEV/pp_table" "$PP_DIR/pp_table.bin"
        od -A x -t x1z "$DEV/pp_table" > "$PP_DIR/pp_table-hex.txt" 2>&1
        echo "     [+] PowerPlay Table binária arquivada ($(wc -c < "$DEV/pp_table") bytes)"
    fi
    if [ -r "$DEV/gpu_metrics" ]; then
        cp "$DEV/gpu_metrics" "$PP_DIR/gpu_metrics.bin"
        od -A x -t x1z "$DEV/gpu_metrics" > "$PP_DIR/gpu_metrics-hex.txt" 2>&1
    fi
fi

# 1.11 DebugFS da GPU (Fences, Rings, VRAM MM, Firmware)
echo "  -> Acessando debugfs dri (firmwares, fences e alocadores de memória)..."
mount | grep -q " /sys/kernel/debug " || mount -t debugfs none /sys/kernel/debug 2>/dev/null
if [ -n "$DRI_IDX" ] && [ -d "/sys/kernel/debug/dri/$DRI_IDX" ]; then
    D="/sys/kernel/debug/dri/$DRI_IDX"
    for f in amdgpu_pm_info amdgpu_firmware_info amdgpu_gpu_recover_count amdgpu_fence_info \
             amdgpu_gem_info amdgpu_vram_mm amdgpu_gtt_mm amdgpu_sa_info \
             amdgpu_vm_info name clients; do
        # amdgpu_gpu_recover NUNCA deve ser lido (leitura dispara reset emergencial de GPU).
        # amdgpu_evict_vram/gtt também não entram aqui: ler DESPEJA a VRAM. Eles só
        # são lidos de propósito na carga de paginação da etapa 3.
        case "$f" in amdgpu_vbios|amdgpu_gpu_recover|amdgpu_evict_*) continue ;; esac
        copiar "debugfs-$f.txt" "$D/$f"
    done
    # Rings de hardware (se disponíveis)
    for r in "$D"/ring_*; do
        [ -r "$r" ] || continue
        copiar "debugfs-$(basename "$r").txt" "$r"
    done
    if [ -r "$D/amdgpu_firmware_info" ]; then
        cp "$D/amdgpu_firmware_info" "$FW_DIR/amdgpu_firmware_info.txt"
    fi
fi

# 1.12 Firmwares instalados em disco
rodar firmware-disco-cyan-skillfish.txt sh -c 'ls -la /lib/firmware/amdgpu/cyan_skillfish* 2>&1 || true'

# --- Validação de Aceleração e Validade do Ensaio (L-C) ------------------------
echo
echo "=== VERIFICAÇÃO DE VALIDADE DO ENSAIO ==="
{
    echo "=== VALIDADE DO ENSAIO (desfecho L-C se faltar aceleração) ==="
    KV="$(uname -r)"; echo "Kernel: $KV"
    KM="$(echo "$KV" | cut -d. -f1)"; KN="$(echo "$KV" | cut -d. -f2)"; KP="$(echo "$KV" | cut -d. -f3 | sed 's/[^0-9].*//')"
    KP="${KP:-0}"
    if [ "$KM" = "6" ] && [ "$KN" = "15" ] && [ "$KP" -le 6 ]; then
        echo "  AVISO: 6.15.0-6.15.6 tem defeito conhecido na BC-250 (comunidade)"
    elif [ "$KM" = "6" ] && [ "$KN" = "17" ] && [ "$KP" -ge 8 ] && [ "$KP" -le 10 ]; then
        echo "  AVISO: 6.17.8-6.17.10 tem defeito conhecido na BC-250 (comunidade)"
    else
        echo "  Kernel: OK (fora das faixas problemáticas conhecidas)"
    fi

    if lspci -nnk -d 1002: 2>/dev/null | grep -q "Kernel driver in use: amdgpu"; then
        echo "Driver de Kernel: amdgpu EM USO (OK)"
    else
        echo "Driver de Kernel: amdgpu NÃO ESTÁ EM USO! (FALHA: nomodeset ativo?)"
    fi

    R="$(glxinfo -B 2>/dev/null | grep -i 'OpenGL renderer' || true)"
    echo "Renderizador GL: ${R:-glxinfo indisponível (use --instalar)}"
    case "$R" in
        *llvmpipe*|*softpipe*) echo "  ALERTA CRÍTICO: Renderização por software detectada (desfecho L-C)!" ;;
        *Cyan*|*Skillfish*|*AMD*|*Radeon*) echo "  Aceleração 3D de hardware CONFIRMADA!" ;;
    esac

    M="$(glxinfo -B 2>/dev/null | grep -i 'OpenGL version' || true)"
    echo "Mesa Version: ${M:-?}"
    VK="$(vulkaninfo --summary 2>/dev/null | grep -iE 'deviceName|driverInfo' | head -4 || true)"
    echo "Vulkan Info:"
    echo "${VK:-  vulkaninfo indisponível}"
    echo "GPU Sysfs Path: ${DEV:-NÃO ENCONTRADA}"
    echo "Hwmon Path: ${HWMON:-não encontrado}"

    if [ -n "$DEV" ] && [ -r "$DEV/mem_info_vram_total" ]; then
        V_TOT="$(awk '{printf "%.0f", $1/1048576}' "$DEV/mem_info_vram_total" 2>/dev/null)"
        V_VIS="$(awk '{printf "%.0f", $1/1048576}' "$DEV/mem_info_vis_vram_total" 2>/dev/null)"
        echo "VRAM Total no Kernel: ${V_TOT} MB (Visível / CPU BAR: ${V_VIS} MB)"
    fi
} | tee "$OUT/validade.txt"
sincronizar "$OUT/validade.txt"

# Se o usuário pediu apenas dados, encerra com empacotamento
if [ "$APENAS_DADOS" -eq 1 ]; then
    echo
    echo "Modo --apenas-dados solicitado. Gerando pacote sem monitor contínuo..."
    tar -czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")" 2>/dev/null
    sincronizar "$OUT.tar.gz"
    echo "Pacote concluído: $OUT.tar.gz"
    echo "Copie a pasta $OUT ou o arquivo $OUT.tar.gz para o Windows."
    exit 0
fi

# =============================================================================
# ETAPA 2: MONITOR CONTÍNUO COM PERSISTÊNCIA EM DISCO
# =============================================================================
echo
echo "[2/4] Iniciando monitor contínuo (amostras a cada ${INTERVALO}s) com gravação síncrona..."

ler() { [ -r "$1" ] && cat "$1" 2>/dev/null | head -1 || echo ""; }
dpm_atual() { [ -r "$1" ] && grep '\*' "$1" 2>/dev/null | awk '{print $2}' | tr -d 'Mhz' | head -1 || echo ""; }
milli_para_c() { [ -n "$1" ] && awk -v v="$1" 'BEGIN{printf "%.1f", v/1000}' || echo ""; }
micro_para_w() { [ -n "$1" ] && awk -v v="$1" 'BEGIN{printf "%.1f", v/1000000}' || echo ""; }
bytes_para_mb() { [ -n "$1" ] && awk -v v="$1" 'BEGIN{printf "%.0f", v/1048576}' || echo ""; }

CPU_HWMON=""
for h in /sys/class/hwmon/hwmon*; do
    case "$(cat "$h/name" 2>/dev/null)" in
        k10temp|zenpower) CPU_HWMON="$h"; break ;;
    esac
done

echo "epoch,hora,sclk_mhz,mclk_mhz,fclk_mhz,socclk_mhz,gpu_busy_pct,vram_usada_mb,gtt_usada_mb,gpu_temp_edge_c,gpu_temp_junction_c,gpu_temp_mem_c,gpu_potencia_w,vddgfx_mv,vddnb_mv,fan_rpm,cpu_tctl_c,load1,mem_disponivel_mb" > "$OUT/monitor.csv"

(
    while true; do
        E="$(date +%s)"; H="$(date +%H:%M:%S)"
        SCLK="$(dpm_atual "$DEV/pp_dpm_sclk")"; MCLK="$(dpm_atual "$DEV/pp_dpm_mclk")"
        FCLK="$(dpm_atual "$DEV/pp_dpm_fclk")"; SOC="$(dpm_atual "$DEV/pp_dpm_socclk")"
        BUSY="$(ler "$DEV/gpu_busy_percent")"
        VRAM="$(bytes_para_mb "$(ler "$DEV/mem_info_vram_used")")"
        GTT="$(bytes_para_mb "$(ler "$DEV/mem_info_gtt_used")")"
        T1=""; T2=""; T3=""; PW=""; V0=""; V1=""; FAN=""
        if [ -n "$HWMON" ]; then
            T1="$(milli_para_c "$(ler "$HWMON/temp1_input")")"
            T2="$(milli_para_c "$(ler "$HWMON/temp2_input")")"
            T3="$(milli_para_c "$(ler "$HWMON/temp3_input")")"
            PW="$(micro_para_w "$(ler "$HWMON/power1_average")")"
            [ -z "$PW" ] && PW="$(micro_para_w "$(ler "$HWMON/power1_input")")"
            V0="$(ler "$HWMON/in0_input")"; V1="$(ler "$HWMON/in1_input")"
            FAN="$(ler "$HWMON/fan1_input")"
        fi
        TC=""; [ -n "$CPU_HWMON" ] && TC="$(milli_para_c "$(ler "$CPU_HWMON/temp1_input")")"
        LD="$(cut -d' ' -f1 /proc/loadavg)"
        MA="$(awk '/MemAvailable/{printf "%.0f",$2/1024}' /proc/meminfo)"
        echo "$E,$H,$SCLK,$MCLK,$FCLK,$SOC,$BUSY,$VRAM,$GTT,$T1,$T2,$T3,$PW,$V0,$V1,$FAN,$TC,$LD,$MA" >> "$OUT/monitor.csv"
        echo "$(date -Is) gpu_edge=${T1}C gpu_junc=${T2}C gpu_mem=${T3}C cpu=${TC}C sclk=${SCLK}MHz pot=${PW}W vram=${VRAM}MB ult_evento=$(tail -1 "$OUT/eventos.txt" 2>/dev/null)" > "$OUT/batimento.txt"
        sincronizar "$OUT/monitor.csv"
        sincronizar "$OUT/batimento.txt"
        sleep "$INTERVALO"
    done
) > /dev/null 2>"$OUT/monitor-erros.txt" &
PID_MON=$!

( dmesg -w -T 2>&1 | while IFS= read -r l; do echo "$l"; sync -f "$OUT/dmesg-continuo.txt" 2>/dev/null; done ) >> "$OUT/dmesg-continuo.txt" &
PID_DMESG=$!

(
    while true; do
        if [ -n "$DRI_IDX" ] && [ -r "/sys/kernel/debug/dri/$DRI_IDX/amdgpu_pm_info" ]; then
            { echo "=== $(date -Is)"; cat "/sys/kernel/debug/dri/$DRI_IDX/amdgpu_pm_info" 2>&1; } >> "$OUT/pm_info-periodico.txt"
            sincronizar "$OUT/pm_info-periodico.txt"
        fi
        sleep 30
    done
) &
PID_PM=$!

parar_fundo() {
    kill "$PID_MON" "$PID_DMESG" "$PID_PM" 2>/dev/null
    pkill -P "$PID_DMESG" 2>/dev/null
}
trap 'parar_fundo; echo; echo "Interrompido pelo operador. Resultados parciais em $OUT"; exit 130' INT TERM

marcar() {
    echo "$(date -Is) $*" >> "$OUT/eventos.txt"
    sincronizar "$OUT/eventos.txt"
}
marcar "INICIO_MONITOR duracao=${DURACAO}s intervalo=${INTERVALO}s"

# =============================================================================
# ETAPA 3: CARGA DE GPU / SESSÃO DE TESTE DE ESTABILIDADE
# =============================================================================
echo
echo "[3/4] Preparando carga de estresse e sessão de uso..."

# Carga de paginação: cada leitura de amdgpu_evict_vram despeja a VRAM para a
# GTT e o TTM move os buffers de volta com cópias no anel de buffer do amdgpu
# (o SDMA nesta geração, P1). É a mesma classe de operação da nossa trava dura
# (FILL/TRANSFER de paginação no SDMA que não conclui), com o vkcube mantendo
# o 3D ocupado ao mesmo tempo. Não usa disco nem pacote extra.
EVICT_TOTAL=0
carga_paginacao() {
    local dur="$1" D="/sys/kernel/debug/dri/$DRI_IDX" fim n=0 g=0 pid_vk=""
    if [ "$PILHA_ESTADO" != "AMDGPU_ATIVO" ] || [ ! -r "$D/amdgpu_evict_vram" ]; then
        marcar "CARGA paginacao indisponivel (amdgpu inativo ou sem amdgpu_evict_vram)"
        return
    fi
    if command -v vkcube >/dev/null 2>&1; then
        como_usuario vkcube >"$OUT/carga-paginacao-vkcube.txt" 2>&1 &
        pid_vk=$!
    fi
    marcar "CARGA paginacao inicio ${dur}s vkcube_pid=${pid_vk:-nenhum}"
    fim=$(( $(date +%s) + dur ))
    while [ "$(date +%s)" -lt "$fim" ]; do
        cat "$D/amdgpu_evict_vram" >/dev/null 2>&1; n=$((n + 1))
        if [ $((n % 3)) -eq 0 ]; then cat "$D/amdgpu_evict_gtt" >/dev/null 2>&1; g=$((g + 1)); fi
        [ $((n % 15)) -eq 0 ] && marcar "CARGA paginacao evict_vram=$n evict_gtt=$g"
        sleep 2
    done
    [ -n "$pid_vk" ] && { kill "$pid_vk" 2>/dev/null; pkill -P "$pid_vk" 2>/dev/null; }
    EVICT_TOTAL=$n
    marcar "CARGA paginacao fim evict_vram=$n evict_gtt=$g"
}

if [ "$AUTO_CARGA" -eq 1 ]; then
    echo -n "Deseja disparar carga automática de GPU (vkcube / glmark2 se instalados)? [S/n]: "
    read -r -t 15 RESP || RESP="s"
    if [ "$RESP" != "n" ] && [ "$RESP" != "N" ]; then
        if command -v vkcube >/dev/null 2>&1; then
            echo "Iniciando vkcube por 120s..."
            marcar "CARGA vkcube 120s inicio"
            como_usuario timeout 120 vkcube >"$OUT/carga-vkcube.txt" 2>&1
            marcar "CARGA vkcube fim saida=$?"
        else
            marcar "CARGA vkcube indisponível"
        fi

        echo "Iniciando carga de paginação (evict VRAM a cada 2 s + vkcube) por 300s..."
        carga_paginacao 300

        if command -v glmark2 >/dev/null 2>&1; then
            echo "Iniciando glmark2 por até 300s..."
            marcar "CARGA glmark2 inicio"
            como_usuario timeout 300 glmark2 >"$OUT/carga-glmark2.txt" 2>&1
            marcar "CARGA glmark2 fim saida=$?"
        else
            marcar "CARGA glmark2 indisponível"
        fi
    fi
fi

cat <<'TXT'

==================================================================
                    SESSÃO DE INTERAÇÃO MANUAL
==================================================================
Durante o período de monitoramento, aplique carga típica do desktop:
  - Abra e feche o menu de aplicativos repetidamente
  - Abra um navegador web com abas pesadas ou vídeo (ex: YouTube)
  - Mova janelas rapidamente pelo desktop
  - Se quiser registrar um marco no log: digite o texto e tecle Enter
    Exemplo: "abri navegador", "video 1080p iniciado", "fechei tudo"
  - Para encerrar antes do tempo: digite fim e tecle Enter

SE A MÁQUINA TRAVAR / CONGELAR:
  1. Anote se a tela congelou ou apagou
  2. Pressione Reset / desligue a energia
  3. Dê boot novamente no Linux e rode:
     sudo bash bc250-linux-referencia.sh --pos-trava
==================================================================

TXT

FIM=$(( $(date +%s) + DURACAO ))
while [ "$(date +%s)" -lt "$FIM" ]; do
    RESTA=$(( FIM - $(date +%s) ))
    printf "\r[%4ds restantes] evento> " "$RESTA"
    if read -r -t 5 LINHA; then
        [ "$LINHA" = "fim" ] && { marcar "FIM antecipado pelo operador"; break; }
        [ -n "$LINHA" ] && marcar "OPERADOR: $LINHA"
    fi
done
echo
marcar "FIM_MONITOR"
parar_fundo

# =============================================================================
# ETAPA 4: COLETA FINAL E RESUMO EXECUTIVO
# =============================================================================
echo
echo "[4/4] Coleta final e consolidação de evidência..."

rodar final-dmesg-amdgpu.txt sh -c 'dmesg -T | grep -iE "amdgpu|drm|gpu|ring|fence|reset|fault|timeout|hang"'
rodar final-sensors.txt sensors
if [ -n "$DRI_IDX" ] && [ -r "/sys/kernel/debug/dri/$DRI_IDX/amdgpu_gpu_recover_count" ]; then
    copiar final-gpu_recover_count.txt "/sys/kernel/debug/dri/$DRI_IDX/amdgpu_gpu_recover_count"
fi

{
    echo "=================================================================="
    echo "=== RESUMO EXECUTIVO BC-250 (Frente L - $(date -Is)) ==="
    echo "=================================================================="
    echo "Duração monitorada: ${DURACAO}s"
    echo "Amostras registradas: $(( $(wc -l < "$OUT/monitor.csv") - 1 ))"
    echo "Veredito imediato de sobrevivência: A MÁQUINA NÃO TRAVOU."
    echo "Pilha amdgpu durante o ensaio: $PILHA_ESTADO"
    if [ "$PILHA_ESTADO" = "AMDGPU_ATIVO" ]; then
        echo "Despejos de VRAM forçados (paginação no SDMA): $EVICT_TOTAL"
    else
        echo "  SEM amdgpu: o ensaio NÃO mediu a GPU e não separa placa de driver."
    fi
    echo
    echo "--- Geometria de Memória e VRAM ---"
    if [ -f "$OUT/cmos-apcb-resumo.txt" ]; then
        grep -E "Signature|Checksum|UMA_SIZE|ClockSpeed|tCL|tRAS|tRFC" "$OUT/cmos-apcb-resumo.txt" || true
    fi
    awk -F, 'NR>1 && $8!="" {if($8>m)m=$8} END{print "VRAM usada MÁXIMA observada: " (m==""?"?":m) " MB"}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $9!="" {if($9>m)m=$9} END{print "GTT usada MÁXIMA observada: " (m==""?"?":m) " MB"}' "$OUT/monitor.csv"
    echo
    echo "--- Térmica e Potência ---"
    awk -F, 'NR>1 && $10!="" {if($10>m)m=$10} END{print "GPU Temp Edge MÁXIMA     : " (m==""?"?":m) " °C"}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $11!="" {if($11>m)m=$11} END{print "GPU Temp Junction MÁXIMA : " (m==""?"?":m) " °C"}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $12!="" {if($12>m)m=$12} END{print "GPU Temp Mem GDDR6 MÁXIMA: " (m==""?"?":m) " °C"}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $17!="" {if($17>m)m=$17} END{print "CPU Temp Tctl MÁXIMA     : " (m==""?"?":m) " °C"}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $13!="" {if($13>m)m=$13} END{print "GPU Potência MÁXIMA      : " (m==""?"?":m) " W"}' "$OUT/monitor.csv"
    echo
    echo "--- Clocks da GPU Observados (MHz:Amostras) ---"
    awk -F, 'NR>1 && $3!="" {v[$3]++} END{printf "sclk (Graphics):"; for(k in v) printf " %s:%d", k, v[k]; print ""}' "$OUT/monitor.csv"
    awk -F, 'NR>1 && $4!="" {v[$4]++} END{printf "mclk (Memory)  :"; for(k in v) printf " %s:%d", k, v[k]; print ""}' "$OUT/monitor.csv"
    echo
    echo "--- Erros e Anomalias do amdgpu no dmesg contínuo ---"
    ERRS="$(grep -iE "amdgpu.*(error|timeout|reset|fault|hang|failed)|ring .* timeout|GPU reset|page fault" "$OUT/dmesg-continuo.txt" 2>/dev/null | tail -25 || true)"
    if [ -n "$ERRS" ]; then
        echo "$ERRS"
    else
        echo "Nenhum erro, timeout, reset ou fault registrado pelo amdgpu no dmesg!"
    fi
    echo
    echo "--- Displays / EDID Detectados ---"
    ls -la "$EDID_DIR" 2>/dev/null || true
    echo
    echo "--- Eventos Registrados na Sessão ---"
    cat "$OUT/eventos.txt" 2>/dev/null || true
    echo "=================================================================="
} | tee "$OUT/RESUMO_EXECUTIVO.txt"
sincronizar "$OUT/RESUMO_EXECUTIVO.txt"

# Criação do arquivo .tar.gz para transporte
tar -czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")" 2>/dev/null
sincronizar "$OUT.tar.gz"

echo
echo "=================================================================="
echo "CONCLUÍDO COM SUCESSO!"
echo "Resultados em: $OUT"
echo "Arquivo comprimido: $OUT.tar.gz"
echo
echo "Para analisar no Windows:"
echo "Copie a pasta (ou o arquivo .tar.gz) para:"
echo "  docs/evidence/$(basename "$OUT")/"
echo "=================================================================="
