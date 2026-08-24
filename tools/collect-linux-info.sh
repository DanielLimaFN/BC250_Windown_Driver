#!/usr/bin/env bash
# ==============================================================================
# AMD BC-250 (Cyan Skillfish) — Linux Evidence & Hardware Discovery Collector
# NamixWDDM Project — Hardware Diagnostic & Upstream Reference Suite
# Versao: 3.0 (Exaustiva / Forense com Gerador de Relatorio Estruturado)
# ==============================================================================
# Uso:
#   sudo bash collect-linux-info.sh
# ==============================================================================

set -u

TIMESTAMP=$(date +%Y-%m-%d_%H-%M-%S)
HOSTNAME_STR=$(hostname 2>/dev/null || echo "linux-host")
BASE_DIR="bc250-linux-evidence-${TIMESTAMP}-${HOSTNAME_STR}"
ARCHIVE_NAME="${BASE_DIR}.tar.gz"
REPORT_MD="${BASE_DIR}/RELATORIO_HARDWARE_LINUX.md"

echo "======================================================================"
echo " AMD BC-250 (Cyan Skillfish) — Coletor Avancado de Hardware (Linux)"
echo " Diretorio Alvo: ${BASE_DIR}"
echo "======================================================================"

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] AVISO: Este script nao esta rodando como root (sudo)."
    echo "    Privilegios de root sao necessarios para ler debugfs, VBIOS ROM, dmesg, dmidecode e ACPI."
    echo "    Execute com: sudo bash $0"
    echo ""
    read -r -p "Deseja continuar mesmo assim? [s/N]: " RESP
    case "$RESP" in
        [sS][iI][mM]|[sS]|[yY][eE][sS]|[yY])
            echo "[*] Continuando em modo com privilegios limitados..."
            ;;
        *)
            echo "[-] Abortado pelo usuario. Execute com sudo."
            exit 1
            ;;
    esac
fi

# Cria estrutura de pastas
mkdir -p "${BASE_DIR}/00_SYSTEM"
mkdir -p "${BASE_DIR}/01_PCI_MEMORY/iommu_groups"
mkdir -p "${BASE_DIR}/02_KERNEL_LOGS"
mkdir -p "${BASE_DIR}/03_AMDGPU_SYSFS/parameters"
mkdir -p "${BASE_DIR}/03_AMDGPU_SYSFS/hwmon"
mkdir -p "${BASE_DIR}/04_AMDGPU_DEBUGFS/rings"
mkdir -p "${BASE_DIR}/04_AMDGPU_DEBUGFS/psp"
mkdir -p "${BASE_DIR}/04_AMDGPU_DEBUGFS/memory_mm"
mkdir -p "${BASE_DIR}/04_AMDGPU_DEBUGFS/regs"
mkdir -p "${BASE_DIR}/04_AMDGPU_DEBUGFS/dcn"
mkdir -p "${BASE_DIR}/05_FIRMWARE/blobs"
mkdir -p "${BASE_DIR}/06_DISPLAY_KMS/edid"
mkdir -p "${BASE_DIR}/07_USERSPACE_APIS"
mkdir -p "${BASE_DIR}/08_ACPI_PLATFORM/tables_bin"

# ==============================================================================
# 00. SYSTEM, CPU & DMI INFO
# ==============================================================================
echo "[1/9] Coletando informacoes de sistema, CPU, frequencias e DMI/BIOS..."
uname -a > "${BASE_DIR}/00_SYSTEM/uname.txt" 2>&1
cat /proc/version > "${BASE_DIR}/00_SYSTEM/proc_version.txt" 2>&1
cat /proc/cmdline > "${BASE_DIR}/00_SYSTEM/kernel_cmdline.txt" 2>&1
cat /etc/os-release > "${BASE_DIR}/00_SYSTEM/os-release.txt" 2>&1 2>/dev/null || lsb_release -a > "${BASE_DIR}/00_SYSTEM/os-release.txt" 2>&1 || true

lscpu > "${BASE_DIR}/00_SYSTEM/lscpu.txt" 2>&1 || true
cat /proc/cpuinfo > "${BASE_DIR}/00_SYSTEM/cpuinfo.txt" 2>&1
cat /proc/meminfo > "${BASE_DIR}/00_SYSTEM/meminfo.txt" 2>&1
lsmem > "${BASE_DIR}/00_SYSTEM/lsmem.txt" 2>&1 || true
numactl --hardware > "${BASE_DIR}/00_SYSTEM/numactl.txt" 2>&1 || true

# CPUFreq e governadores
if [ -d /sys/devices/system/cpu/cpufreq ]; then
    for c in /sys/devices/system/cpu/cpufreq/policy*; do
        [ -d "$c" ] || continue
        PNAME=$(basename "$c")
        echo "--- $PNAME ---" >> "${BASE_DIR}/00_SYSTEM/cpufreq_policies.txt"
        for attr in scaling_governor scaling_available_governors scaling_cur_freq scaling_min_freq scaling_max_freq cpuinfo_cur_freq; do
            if [ -f "$c/$attr" ]; then
                echo "$attr: $(cat "$c/$attr" 2>/dev/null)" >> "${BASE_DIR}/00_SYSTEM/cpufreq_policies.txt"
            fi
        done
    done
fi

# DMI / BIOS
dmidecode > "${BASE_DIR}/00_SYSTEM/dmidecode_full.txt" 2>&1 || true
dmidecode -t bios,system,baseboard,processor,memory > "${BASE_DIR}/00_SYSTEM/dmidecode_summary.txt" 2>&1 || true
dmidecode -t 17 > "${BASE_DIR}/00_SYSTEM/dmidecode_memory_devices.txt" 2>&1 || true

# Config do kernel
if [ -f /proc/config.gz ]; then
    zcat /proc/config.gz > "${BASE_DIR}/00_SYSTEM/kernel_config.txt" 2>&1 || true
elif [ -f "/boot/config-$(uname -r)" ]; then
    cp "/boot/config-$(uname -r)" "${BASE_DIR}/00_SYSTEM/kernel_config.txt" 2>&1 || true
fi

# ==============================================================================
# 01. PCI, BARS, IOMMU & MEMORY MAP
# ==============================================================================
echo "[2/9] Coletando topologia PCI, IOMMU groups, espaco de configuracao e mapa de memoria..."
lspci -nn > "${BASE_DIR}/01_PCI_MEMORY/lspci_devices.txt" 2>&1
lspci -tv > "${BASE_DIR}/01_PCI_MEMORY/lspci_tree.txt" 2>&1
lspci -vvv -xxxx > "${BASE_DIR}/01_PCI_MEMORY/lspci_all_vvv_xxxx.txt" 2>&1

# Identifica especificamente a GPU AMD (1002:13fe ou classe VGA/Display)
GPU_PCI=$(lspci -nn | grep -E -i '1002:13fe|1002:13ff|vga.*amd|display.*amd' | head -n 1 | awk '{print $1}')
if [ -n "${GPU_PCI}" ]; then
    echo "    -> GPU detectada no slot PCI: ${GPU_PCI}"
    lspci -vvv -xxxx -s "${GPU_PCI}" > "${BASE_DIR}/01_PCI_MEMORY/lspci_bc250_vvv_xxxx.txt" 2>&1
fi

cat /proc/iomem > "${BASE_DIR}/01_PCI_MEMORY/iomem.txt" 2>&1
cat /proc/ioports > "${BASE_DIR}/01_PCI_MEMORY/ioports.txt" 2>&1

# IOMMU groups topology
if [ -d /sys/kernel/iommu_groups ]; then
    for g in /sys/kernel/iommu_groups/*; do
        [ -d "$g" ] || continue
        GNAME=$(basename "$g")
        echo "Group $GNAME:" >> "${BASE_DIR}/01_PCI_MEMORY/iommu_groups/groups.txt"
        for d in "$g"/devices/*; do
            [ -e "$d" ] || continue
            DNAME=$(basename "$d")
            echo "  $DNAME $(lspci -s "$DNAME" 2>/dev/null)" >> "${BASE_DIR}/01_PCI_MEMORY/iommu_groups/groups.txt"
        done
    done
fi

# Dump dos resources do sysfs para todos os dispositivos PCI AMD
for dev in /sys/bus/pci/devices/*; do
    if [ -f "$dev/vendor" ]; then
        VEND=$(cat "$dev/vendor" 2>/dev/null || echo "")
        if [ "$VEND" = "0x1002" ]; then
            DEV_NAME=$(basename "$dev")
            echo "--- $DEV_NAME ---" >> "${BASE_DIR}/01_PCI_MEMORY/amd_pci_resources.txt"
            cat "$dev/resource" >> "${BASE_DIR}/01_PCI_MEMORY/amd_pci_resources.txt" 2>/dev/null || true
            if [ -f "$dev/config" ]; then
                hexdump -C "$dev/config" > "${BASE_DIR}/01_PCI_MEMORY/${DEV_NAME}_config_hex.txt" 2>/dev/null || true
            fi
        fi
    fi
done

# ==============================================================================
# 02. KERNEL LOGS (DMESG & BOOT LOGS)
# ==============================================================================
echo "[3/9] Coletando logs do kernel (dmesg completo, timestamps e filtrados)..."
dmesg > "${BASE_DIR}/02_KERNEL_LOGS/dmesg_raw.txt" 2>&1 || true
dmesg -T > "${BASE_DIR}/02_KERNEL_LOGS/dmesg_human_time.txt" 2>&1 || true

# Filtragem de submodulos para inspecao rapida
grep -i -E 'amdgpu|drm|pci|bc-250|cyan|13fe|psp|smu|sdma|gfx|gmc|dcn|iommu|amd-vi|ivrs|gart|vmid|vram|fence|ring|discovery' \
    "${BASE_DIR}/02_KERNEL_LOGS/dmesg_raw.txt" > "${BASE_DIR}/02_KERNEL_LOGS/dmesg_amdgpu_filtered.txt" 2>&1 || true

if command -v journalctl >/dev/null 2>&1; then
    journalctl -b 0 > "${BASE_DIR}/02_KERNEL_LOGS/journalctl_current_boot.txt" 2>&1 || true
fi

# Logs de display / Xorg / Wayland se existirem
cp /var/log/Xorg.*.log "${BASE_DIR}/02_KERNEL_LOGS/" 2>/dev/null || true
cp "$HOME/.local/share/xorg/Xorg."*.log "${BASE_DIR}/02_KERNEL_LOGS/" 2>/dev/null || true

# ==============================================================================
# 03. AMDGPU SYSFS, DPM & VBIOS / ROM EXTRACTION
# ==============================================================================
echo "[4/9] Coletando parametros do modulo, DPM, PowerPlay e tentando extrair VBIOS ROM..."
modinfo amdgpu > "${BASE_DIR}/03_AMDGPU_SYSFS/modinfo_amdgpu.txt" 2>&1 || true
lsmod | grep -i -E 'amdgpu|drm|ttm' > "${BASE_DIR}/03_AMDGPU_SYSFS/lsmod_drm.txt" 2>&1 || true

# Parametros do modulo amdgpu
if [ -d /sys/module/amdgpu/parameters ]; then
    for param in /sys/module/amdgpu/parameters/*; do
        if [ -f "$param" ] && [ -r "$param" ]; then
            PNAME=$(basename "$param")
            VAL=$(cat "$param" 2>/dev/null || echo "<erro de leitura>")
            echo "${PNAME}=${VAL}" >> "${BASE_DIR}/03_AMDGPU_SYSFS/parameters/all_parameters.txt"
        fi
    done
fi

if [ -f /sys/module/amdgpu/version ]; then
    cat /sys/module/amdgpu/version > "${BASE_DIR}/03_AMDGPU_SYSFS/amdgpu_driver_version.txt" 2>&1 || true
fi

# Inspeciona todas as placas DRM (/sys/class/drm/card*)
for card in /sys/class/drm/card[0-9]; do
    [ -d "$card" ] || continue
    CARD_NAME=$(basename "$card")
    echo "    -> Inspecionando Sysfs para ${CARD_NAME}..."
    CARD_OUT="${BASE_DIR}/03_AMDGPU_SYSFS/${CARD_NAME}"
    mkdir -p "$CARD_OUT"

    # Atributos basicos do device
    for attr in vendor device subsystem_vendor subsystem_device revision uevent \
                gpu_busy_percent mem_busy_percent current_link_speed current_link_width \
                power_dpm_state power_dpm_force_performance_level pp_dpm_sclk pp_dpm_mclk \
                pp_dpm_socclk pp_dpm_fclk pp_power_profile_mode pp_features pp_num_states \
                mem_info_vram_total mem_info_vram_used mem_info_gtt_total mem_info_gtt_used \
                mem_info_vis_vram_total mem_info_vis_vram_used unique_id product_name product_number; do
        if [ -f "$card/device/$attr" ] && [ -r "$card/device/$attr" ]; then
            cat "$card/device/$attr" > "${CARD_OUT}/${attr}.txt" 2>/dev/null || true
        fi
    done

    # PowerPlay table binaria se presente
    if [ -f "$card/device/pp_table" ] && [ -r "$card/device/pp_table" ]; then
        cp "$card/device/pp_table" "${CARD_OUT}/pp_table.bin" 2>/dev/null || true
        hexdump -C "$card/device/pp_table" > "${CARD_OUT}/pp_table_hex.txt" 2>/dev/null || true
    fi

    # Extrai VBIOS / ROM bar se disponivel
    if [ -f "$card/device/rom" ]; then
        echo "    -> Tentando extrair VBIOS ROM de $CARD_NAME..."
        echo 1 > "$card/device/rom" 2>/dev/null || true
        if cat "$card/device/rom" > "${CARD_OUT}/vbios.bin" 2>/dev/null; then
            if [ -s "${CARD_OUT}/vbios.bin" ]; then
                echo "       [OK] VBIOS extraida ($(wc -c < "${CARD_OUT}/vbios.bin") bytes)"
                hexdump -C -n 1024 "${CARD_OUT}/vbios.bin" > "${CARD_OUT}/vbios_header_hex.txt" 2>/dev/null || true
            else
                rm -f "${CARD_OUT}/vbios.bin"
                echo "       [!] VBIOS retornou vazia."
            fi
        else
            echo "       [!] Nao foi possivel ler /rom diretamente."
        fi
        echo 0 > "$card/device/rom" 2>/dev/null || true
    fi

    # Sensores de HWMON
    if [ -d "$card/device/hwmon" ]; then
        for hw in "$card/device/hwmon"/hwmon*; do
            [ -d "$hw" ] || continue
            HW_NAME=$(basename "$hw")
            mkdir -p "${CARD_OUT}/hwmon/${HW_NAME}"
            for sattr in "$hw"/*; do
                if [ -f "$sattr" ] && [ -r "$sattr" ]; then
                    SNAME=$(basename "$sattr")
                    VAL=$(cat "$sattr" 2>/dev/null || echo "<erro>")
                    echo "${SNAME}: ${VAL}" >> "${CARD_OUT}/hwmon/${HW_NAME}/sensors.txt"
                fi
            done
        done
    fi
done

# ==============================================================================
# 04. AMDGPU DEBUGFS (DADOS INTERNOS DO DRIVER: IP DISCOVERY, RINGS, PSP, MMU)
# ==============================================================================
echo "[5/9] Coletando dados internos do Driver via DebugFS..."
if [ ! -d /sys/kernel/debug/dri ]; then
    mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
fi

if [ -d /sys/kernel/debug/dri ]; then
    for dri_node in /sys/kernel/debug/dri/*; do
        [ -d "$dri_node" ] || continue
        DRI_NAME=$(basename "$dri_node")
        NODE_OUT="${BASE_DIR}/04_AMDGPU_DEBUGFS/${DRI_NAME}"
        mkdir -p "$NODE_OUT/rings"
        mkdir -p "$NODE_OUT/psp"
        mkdir -p "$NODE_OUT/regs"
        mkdir -p "$NODE_OUT/dcn"

        # Arquivos de texto principais
        for dbg_file in amdgpu_gpu_info amdgpu_firmware_info amdgpu_fence_info \
                        amdgpu_pm_info amdgpu_sensors amdgpu_vram_mm amdgpu_gtt_mm \
                        amdgpu_vm_info amdgpu_sa_info amdgpu_nbio \
                        amdgpu_smn amdgpu_evict_vram amdgpu_evict_gtt amdgpu_benchmark \
                        amdgpu_ras_features amdgpu_gpr; do
            if [ -f "$dri_node/$dbg_file" ] && [ -r "$dri_node/$dbg_file" ]; then
                timeout 2s cat "$dri_node/$dbg_file" > "${NODE_OUT}/${dbg_file}.txt" 2>/dev/null || true
            fi
        done

        # IP Discovery Binary Dump se presente
        if [ -f "$dri_node/amdgpu_discovery" ] && [ -r "$dri_node/amdgpu_discovery" ]; then
            cp "$dri_node/amdgpu_discovery" "${NODE_OUT}/amdgpu_discovery.bin" 2>/dev/null || true
            hexdump -C "$dri_node/amdgpu_discovery" > "${NODE_OUT}/amdgpu_discovery_hex.txt" 2>/dev/null || true
        fi

        # Rings status
        for ring_file in "$dri_node"/amdgpu_ring_*; do
            if [ -f "$ring_file" ] && [ -r "$ring_file" ]; then
                RNAME=$(basename "$ring_file")
                timeout 2s cat "$ring_file" > "${NODE_OUT}/rings/${RNAME}.txt" 2>/dev/null || true
            fi
        done

        # PSP status
        for psp_file in "$dri_node"/amdgpu_psp_*; do
            if [ -f "$psp_file" ] && [ -r "$psp_file" ]; then
                PNAME=$(basename "$psp_file")
                timeout 2s cat "$psp_file" > "${NODE_OUT}/psp/${PNAME}.txt" 2>/dev/null || true
            fi
        done

        # DCN / Display manager debug se presente
        for dcn_file in "$dri_node"/dcn* "$dri_node"/amdgpu_dm_*; do
            if [ -f "$dcn_file" ] && [ -r "$dcn_file" ]; then
                FNAME=$(basename "$dcn_file")
                timeout 2s cat "$dcn_file" > "${NODE_OUT}/dcn/${FNAME}.txt" 2>/dev/null || true
            fi
        done

        # Safe MMIO dumps (apenas leitura dos declarados no debugfs)
        for reg_file in amdgpu_regs amdgpu_regs_didt amdgpu_regs_pcie amdgpu_regs_smc; do
            if [ -f "$dri_node/$reg_file" ] && [ -r "$dri_node/$reg_file" ]; then
                timeout 2s cat "$dri_node/$reg_file" > "${NODE_OUT}/regs/${reg_file}.txt" 2>/dev/null || true
            fi
        done
    done
else
    echo "    [!] Debugfs nao esta disponivel ou /sys/kernel/debug/dri nao existe."
fi

# ==============================================================================
# 05. FIRMWARE BLOBS, HASHES & COPY
# ==============================================================================
echo "[6/9] Coletando catalogo, hashes e COPIA dos firmwares da GPU..."
if [ -d /lib/firmware/amdgpu ]; then
    ls -la /lib/firmware/amdgpu > "${BASE_DIR}/05_FIRMWARE/firmware_full_listing.txt" 2>&1 || true
    
    # Copia os firmwares especificos relevantes (Cyan Skillfish, Navi10, VanGogh) e gera hashes
    find /lib/firmware/amdgpu -maxdepth 1 \( -name "cyan_skillfish*" -o -name "navi10*" -o -name "vangogh*" \) | sort | while read -r fw; do
        [ -f "$fw" ] || continue
        sha256sum "$fw" >> "${BASE_DIR}/05_FIRMWARE/firmware_sha256.txt" 2>&1 || true
        md5sum "$fw" >> "${BASE_DIR}/05_FIRMWARE/firmware_md5.txt" 2>&1 || true
        ls -la "$fw" >> "${BASE_DIR}/05_FIRMWARE/firmware_sizes.txt" 2>&1 || true
        cp "$fw" "${BASE_DIR}/05_FIRMWARE/blobs/" 2>/dev/null || true
    done
    echo "    -> Firmwares copiados: $(ls -1 "${BASE_DIR}/05_FIRMWARE/blobs/" 2>/dev/null | wc -l) arquivos."
fi

# ==============================================================================
# 06. DISPLAY, KMS & CONNECTORS / EDID
# ==============================================================================
echo "[7/9] Coletando informacoes de Display, Modos KMS, Conectores e EDID..."
for conn in /sys/class/drm/card[0-9]-*; do
    [ -d "$conn" ] || continue
    CNAME=$(basename "$conn")
    echo "    -> Conector: $CNAME"
    mkdir -p "${BASE_DIR}/06_DISPLAY_KMS/${CNAME}"
    
    for attr in status enabled dpms modes; do
        if [ -f "$conn/$attr" ] && [ -r "$conn/$attr" ]; then
            cat "$conn/$attr" > "${BASE_DIR}/06_DISPLAY_KMS/${CNAME}/${attr}.txt" 2>/dev/null || true
        fi
    done

    # Dump do EDID binario e hexadecimal se conectado
    if [ -f "$conn/edid" ] && [ -s "$conn/edid" ]; then
        cp "$conn/edid" "${BASE_DIR}/06_DISPLAY_KMS/edid/${CNAME}_edid.bin" 2>/dev/null || true
        hexdump -C "$conn/edid" > "${BASE_DIR}/06_DISPLAY_KMS/edid/${CNAME}_edid_hex.txt" 2>/dev/null || true
        if command -v parse-edid >/dev/null 2>&1; then
            parse-edid < "$conn/edid" > "${BASE_DIR}/06_DISPLAY_KMS/edid/${CNAME}_edid_parsed.txt" 2>/dev/null || true
        elif command -v edid-decode >/dev/null 2>&1; then
            edid-decode "$conn/edid" > "${BASE_DIR}/06_DISPLAY_KMS/edid/${CNAME}_edid_decoded.txt" 2>/dev/null || true
        fi
    fi
done

# modetest (se disponivel na distribuicao)
if command -v modetest >/dev/null 2>&1; then
    modetest -M amdgpu > "${BASE_DIR}/06_DISPLAY_KMS/modetest_amdgpu.txt" 2>&1 || true
    modetest > "${BASE_DIR}/06_DISPLAY_KMS/modetest_default.txt" 2>&1 || true
fi

# ==============================================================================
# 07. USERSPACE GPU & COMPUTE APIS (SE DISPONIVEIS)
# ==============================================================================
echo "[8/9] Coletando dados de APIs do Userspace (Vulkan, OpenCL, DRM, ROCm)..."
vulkaninfo --summary > "${BASE_DIR}/07_USERSPACE_APIS/vulkaninfo_summary.txt" 2>&1 || true
vulkaninfo > "${BASE_DIR}/07_USERSPACE_APIS/vulkaninfo_full.txt" 2>&1 || true
clinfo > "${BASE_DIR}/07_USERSPACE_APIS/clinfo.txt" 2>&1 || true
glxinfo -B > "${BASE_DIR}/07_USERSPACE_APIS/glxinfo_summary.txt" 2>&1 || true
glxinfo > "${BASE_DIR}/07_USERSPACE_APIS/glxinfo_full.txt" 2>&1 || true
vainfo > "${BASE_DIR}/07_USERSPACE_APIS/vainfo.txt" 2>&1 || true
rocm-smi > "${BASE_DIR}/07_USERSPACE_APIS/rocm-smi.txt" 2>&1 || true
radeontop -d - -l 1 > "${BASE_DIR}/07_USERSPACE_APIS/radeontop_sample.txt" 2>&1 || true

# ==============================================================================
# 08. ACPI TABLES (DSDT, SSDT, IVRS) & EFI VARS
# ==============================================================================
echo "[9/9] Coletando tabelas ACPI (DSDT, SSDT, IVRS) e variaveis EFI..."
if command -v acpidump >/dev/null 2>&1; then
    acpidump > "${BASE_DIR}/08_ACPI_PLATFORM/acpidump.txt" 2>&1 || true
fi

if [ -d /sys/firmware/acpi/tables ]; then
    for tab in /sys/firmware/acpi/tables/*; do
        if [ -f "$tab" ] && [ -r "$tab" ]; then
            TNAME=$(basename "$tab")
            cp "$tab" "${BASE_DIR}/08_ACPI_PLATFORM/tables_bin/${TNAME}.bin" 2>/dev/null || true
            hexdump -C "$tab" > "${BASE_DIR}/08_ACPI_PLATFORM/${TNAME}_hex.txt" 2>/dev/null || true
        fi
    done
fi

# Lista variaveis EFI se existirem
if [ -d /sys/firmware/efi/efivars ]; then
    ls -la /sys/firmware/efi/efivars > "${BASE_DIR}/08_ACPI_PLATFORM/efivars_list.txt" 2>&1 || true
fi

# ==============================================================================
# GERADOR DE RELATORIO ESTRUTURADO (MARKDOWN & CONSOLE)
# ==============================================================================
echo ""
echo "[*] Sintetizando e gerando Relatorio de Hardware (${REPORT_MD})..."

# Extracao de variaveis para o relatorio
KERNEL_VER=$(uname -r 2>/dev/null || echo "Desconhecido")
DISTRO_NAME=$(grep -E '^PRETTY_NAME=' "${BASE_DIR}/00_SYSTEM/os-release.txt" 2>/dev/null | cut -d= -f2 | tr -d '"' || echo "Linux")
CPU_MODEL=$(grep -m1 -E 'model name|Model name:' "${BASE_DIR}/00_SYSTEM/lscpu.txt" "${BASE_DIR}/00_SYSTEM/cpuinfo.txt" 2>/dev/null | cut -d: -f2 | sed 's/^[ \t]*//' || echo "AMD CPU")
CPU_CORES=$(grep -m1 -E '^CPU\(s\):' "${BASE_DIR}/00_SYSTEM/lscpu.txt" 2>/dev/null | awk '{print $2}' || echo "N/A")
CPU_THREADS_PER_CORE=$(grep -m1 -E '^Thread\(s\) per core:' "${BASE_DIR}/00_SYSTEM/lscpu.txt" 2>/dev/null | awk '{print $NF}' || echo "N/A")
BIOS_VENDOR=$(grep -A5 "BIOS Information" "${BASE_DIR}/00_SYSTEM/dmidecode_full.txt" 2>/dev/null | grep "Vendor:" | cut -d: -f2 | sed 's/^[ \t]*//' || echo "N/A")
BIOS_VER=$(grep -A5 "BIOS Information" "${BASE_DIR}/00_SYSTEM/dmidecode_full.txt" 2>/dev/null | grep "Version:" | cut -d: -f2 | sed 's/^[ \t]*//' || echo "N/A")
BIOS_DATE=$(grep -A5 "BIOS Information" "${BASE_DIR}/00_SYSTEM/dmidecode_full.txt" 2>/dev/null | grep "Release Date:" | cut -d: -f2 | sed 's/^[ \t]*//' || echo "N/A")
RAM_TOTAL=$(grep -m1 "MemTotal:" "${BASE_DIR}/00_SYSTEM/meminfo.txt" 2>/dev/null | awk '{printf "%.2f GB (%s kB)", $2/1024/1024, $2}' || echo "N/A")

# GPU PCI & BARs
GPU_PCI_LINE=$(grep -E -i '1002:13fe|vga.*amd|display.*amd' "${BASE_DIR}/01_PCI_MEMORY/lspci_devices.txt" 2>/dev/null | head -n1 || echo "")
GPU_PCI_SLOT=$(echo "$GPU_PCI_LINE" | awk '{print $1}')
GPU_PCI_NAME=$(echo "$GPU_PCI_LINE" | cut -d' ' -f2-)
BAR0_LINE=$(grep -E -i 'Region 0: Memory at' "${BASE_DIR}/01_PCI_MEMORY/lspci_bc250_vvv_xxxx.txt" 2>/dev/null | head -n1 | sed 's/^[ \t]*//' || echo "N/A")
BAR5_LINE=$(grep -E -i 'Region 5: Memory at' "${BASE_DIR}/01_PCI_MEMORY/lspci_bc250_vvv_xxxx.txt" 2>/dev/null | head -n1 | sed 's/^[ \t]*//' || echo "N/A")
LNK_STA=$(grep -E 'LnkSta:' "${BASE_DIR}/01_PCI_MEMORY/lspci_bc250_vvv_xxxx.txt" 2>/dev/null | head -n1 | sed 's/^[ \t]*//' || echo "N/A")
LNK_CAP=$(grep -E 'LnkCap:' "${BASE_DIR}/01_PCI_MEMORY/lspci_bc250_vvv_xxxx.txt" 2>/dev/null | head -n1 | sed 's/^[ \t]*//' || echo "N/A")

# Driver & Memoria GPU
DRIVER_VER=$(cat "${BASE_DIR}/03_AMDGPU_SYSFS/amdgpu_driver_version.txt" 2>/dev/null || echo "in-tree kernel module")
VRAM_TOTAL=$(cat "${BASE_DIR}/03_AMDGPU_SYSFS/"card*/mem_info_vram_total 2>/dev/null | head -n1 | awk '{printf "%.2f MB (%s bytes)", $1/1024/1024, $1}' || echo "N/A")
VRAM_VIS=$(cat "${BASE_DIR}/03_AMDGPU_SYSFS/"card*/mem_info_vis_vram_total 2>/dev/null | head -n1 | awk '{printf "%.2f MB (%s bytes)", $1/1024/1024, $1}' || echo "N/A")
GTT_TOTAL=$(cat "${BASE_DIR}/03_AMDGPU_SYSFS/"card*/mem_info_gtt_total 2>/dev/null | head -n1 | awk '{printf "%.2f MB (%s bytes)", $1/1024/1024, $1}' || echo "N/A")

# Conectores de Display
CONNECTORS_ACTIVE=$(grep -s "connected" "${BASE_DIR}"/06_DISPLAY_KMS/*/status.txt 2>/dev/null | awk -F'/' '{print $(NF-1)}' | tr '\n' ', ' | sed 's/,$//')
[ -z "$CONNECTORS_ACTIVE" ] && CONNECTORS_ACTIVE="Nenhum conector detectado como ativo"

# Gera o arquivo Markdown
cat << 'EOF' > "${REPORT_MD}"
# Relatório de Diagnóstico de Hardware — AMD BC-250 (Linux Reference)

> Documento gerado automaticamente pelo coletor de evidências da suíte NamixWDDM.
EOF

cat << EOF >> "${REPORT_MD}"

* **Data da Coleta:** ${TIMESTAMP}
* **Host:** \`${HOSTNAME_STR}\`
* **Kernel:** \`${KERNEL_VER}\`
* **Distribuição:** ${DISTRO_NAME}

---

## 1. Identificação da Plataforma e CPU

| Item | Valor Detectado |
|---|---|
| **Modelo da CPU** | ${CPU_MODEL} |
| **Topologia da CPU** | ${CPU_CORES} núcleos / threads (${CPU_THREADS_PER_CORE} threads por core) |
| **Memória RAM do Sistema** | ${RAM_TOTAL} |
| **Fabricante da BIOS** | ${BIOS_VENDOR} |
| **Versão / Data da BIOS** | ${BIOS_VER} (${BIOS_DATE}) |

---

## 2. Identificação PCI da GPU BC-250

| Parâmetro PCI | Valor no Silício |
|---|---|
| **Slot PCI** | \`${GPU_PCI_SLOT}\` |
| **Identificação PCI** | ${GPU_PCI_NAME} |
| **BAR0 (VRAM Aperture)** | \`${BAR0_LINE}\` |
| **BAR5 (MMIO Register Space)** | \`${BAR5_LINE}\` |
| **PCIe Link Capability** | \`${LNK_CAP}\` |
| **PCIe Link Status Atual** | \`${LNK_STA}\` |

---

## 3. Topologia de Memória GPU (AMDGPU Sysfs)

| Segmento de Memória | Tamanho Reportado pelo Driver Upstream |
|---|---|
| **VRAM Total** | ${VRAM_TOTAL} |
| **VRAM Visível (CPU BAR0)** | ${VRAM_VIS} |
| **GTT Total (System RAM)** | ${GTT_TOTAL} |
| **Versão do Driver amdgpu** | \`${DRIVER_VER}\` |

---

## 4. Firmwares Carregados (Microcódigo do Silício)

EOF

if [ -f "${BASE_DIR}/04_AMDGPU_DEBUGFS/0/amdgpu_firmware_info.txt" ]; then
    echo '```text' >> "${REPORT_MD}"
    cat "${BASE_DIR}/04_AMDGPU_DEBUGFS/0/amdgpu_firmware_info.txt" >> "${REPORT_MD}"
    echo '```' >> "${REPORT_MD}"
elif [ -f "${BASE_DIR}/04_AMDGPU_DEBUGFS/1/amdgpu_firmware_info.txt" ]; then
    echo '```text' >> "${REPORT_MD}"
    cat "${BASE_DIR}/04_AMDGPU_DEBUGFS/1/amdgpu_firmware_info.txt" >> "${REPORT_MD}"
    echo '```' >> "${REPORT_MD}"
else
    echo "*Tabela de firmware do debugfs não disponível; consulte \`05_FIRMWARE/\` ou \`02_KERNEL_LOGS/dmesg_amdgpu_filtered.txt\`.*" >> "${REPORT_MD}"
fi

cat << EOF >> "${REPORT_MD}"

---

## 5. DPM, PowerPlay e Frequências de Clock

EOF

CARD_SYSFS=$(find "${BASE_DIR}/03_AMDGPU_SYSFS" -maxdepth 1 -name "card*" | head -n1)
if [ -n "$CARD_SYSFS" ] && [ -d "$CARD_SYSFS" ]; then
    if [ -f "$CARD_SYSFS/pp_dpm_sclk.txt" ]; then
        echo "### GPU SCLK (GFX Clock DPM States):" >> "${REPORT_MD}"
        echo '```text' >> "${REPORT_MD}"
        cat "$CARD_SYSFS/pp_dpm_sclk.txt" >> "${REPORT_MD}"
        echo '```' >> "${REPORT_MD}"
    fi
    if [ -f "$CARD_SYSFS/pp_dpm_mclk.txt" ]; then
        echo "### Memory MCLK (GDDR6 Clock DPM States):" >> "${REPORT_MD}"
        echo '```text' >> "${REPORT_MD}"
        cat "$CARD_SYSFS/pp_dpm_mclk.txt" >> "${REPORT_MD}"
        echo '```' >> "${REPORT_MD}"
    fi
    if [ -f "$CARD_SYSFS/power_dpm_force_performance_level.txt" ]; then
        echo "* **Performance Level:** \`$(cat "$CARD_SYSFS/power_dpm_force_performance_level.txt")\`" >> "${REPORT_MD}"
    fi
fi

cat << EOF >> "${REPORT_MD}"

---

## 6. Display, Conectividade & KMS

* **Conectores com Sinal Ativo:** ${CONNECTORS_ACTIVE}
* **Status do Dump VBIOS:** $([ -f "${BASE_DIR}"/03_AMDGPU_SYSFS/card*/vbios.bin ] && echo "Extraído com sucesso (\`vbios.bin\` disponível)" || echo "Não acessível diretamente via Sysfs")

---

## 7. Estrutura dos Dados Coletados no Pacote

\`\`\`text
${BASE_DIR}/
├── 00_SYSTEM/          -> uname, lscpu, dmidecode, meminfo, kernel config
├── 01_PCI_MEMORY/      -> lspci -vvv -xxxx (4KB config space), BARs, iomem, IOMMU groups
├── 02_KERNEL_LOGS/     -> dmesg completo, dmesg filtrado amdgpu, boot journalctl
├── 03_AMDGPU_SYSFS/    -> DPM states, powerPlay tables, VBIOS ROM, hwmon sensors
├── 04_AMDGPU_DEBUGFS/  -> IP discovery bin, rings status, PSP, memory managers (vram_mm/gtt_mm)
├── 05_FIRMWARE/        -> Hashes SHA256 e copias dos blobs .bin (cyan_skillfish*, navi10*)
├── 06_DISPLAY_KMS/     -> Status dos conectores, EDID binario, modos suportados, modetest
├── 07_USERSPACE_APIS/  -> vulkaninfo, clinfo, glxinfo, vainfo, rocm-smi
└── 08_ACPI_PLATFORM/   -> Tabelas ACPI (DSDT, SSDT, IVRS binarios) e acpidump
\`\`\`
EOF

# ==============================================================================
# COMPACTACAO FINAL DO PACOTE
# ==============================================================================
echo ""
echo "[*] Compactando todos os dados em ${ARCHIVE_NAME}..."
tar -czf "${ARCHIVE_NAME}" "${BASE_DIR}" 2>/dev/null || tar -cf - "${BASE_DIR}" | gzip -c > "${ARCHIVE_NAME}" 2>/dev/null || true

if [ -f "${ARCHIVE_NAME}" ]; then
    SHA=$(sha256sum "${ARCHIVE_NAME}" | awk '{print $1}')
    echo "======================================================================"
    echo " [OK] Coleta e Relatorio finalizados com sucesso!"
    echo " Relatorio:     $(pwd)/${REPORT_MD}"
    echo " Pacote Gerado: $(pwd)/${ARCHIVE_NAME}"
    echo " SHA-256:       ${SHA}"
    echo " Tamanho:       $(du -h "${ARCHIVE_NAME}" | awk '{print $1}')"
    echo "======================================================================"
    echo ""
    echo "--- RESUMO EXECUTIVO DO HARDWARE DETECTADO ---"
    echo "  * CPU:         ${CPU_MODEL} (${CPU_CORES} cores)"
    echo "  * RAM Sistema: ${RAM_TOTAL}"
    echo "  * GPU Slot:    ${GPU_PCI_SLOT} - ${GPU_PCI_NAME}"
    echo "  * BAR0 (VRAM): ${BAR0_LINE}"
    echo "  * BAR5 (MMIO): ${BAR5_LINE}"
    echo "  * VRAM Total:  ${VRAM_TOTAL}"
    echo "  * VRAM Visivel:${VRAM_VIS}"
    echo "  * GTT Total:   ${GTT_TOTAL}"
    echo "  * Display:     ${CONNECTORS_ACTIVE}"
    echo "----------------------------------------------"
    echo "Copie o arquivo ${ARCHIVE_NAME} de volta para a pasta de desenvolvimento no Windows."
else
    echo "[!] AVISO: Falha ao gerar o arquivo .tar.gz. A pasta ${BASE_DIR} contem os dados brutos."
fi
