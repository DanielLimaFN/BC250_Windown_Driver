#!/usr/bin/env bash
# Coleta somente-leitura das duas fontes de topologia usadas pelo amdgpu:
# IP Discovery (preferida quando existe) e gpu_info (fallback). Nao acessa MMIO,
# nao carrega modulos e nao altera o firmware instalado.

set -eu

stamp="$(date +%Y-%m-%d_%H-%M-%S)"
host="$(hostname 2>/dev/null || printf desconhecido)"
out="${1:-linux-gpu-info-${host}-${stamp}}"
mkdir -p "$out"

manifest="$out/manifest.txt"
: > "$manifest"
{
    printf 'captured_at=%s\n' "$(date -Is 2>/dev/null || date)"
    printf 'host=%s\n' "$host"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'read_only=true\n'
    printf 'expected_kernel_name=cyan_skillfish_gpu_info.bin\n'
    printf 'gpu_info_role=fallback_when_ip_discovery_is_absent\n'
} >> "$manifest"

for dev in /sys/bus/pci/devices/*; do
    [ -r "$dev/vendor" ] || continue
    [ -r "$dev/device" ] || continue
    vendor="$(cat "$dev/vendor" 2>/dev/null || true)"
    device="$(cat "$dev/device" 2>/dev/null || true)"
    [ "$vendor" = 0x1002 ] || continue
    case "$device" in
        0x13fe|0x143f)
            printf 'pci_device=%s vendor=%s device=%s\n' \
                "$(basename "$dev")" "$vendor" "$device" >> "$manifest"
            if [ -d "$dev/ip_discovery" ]; then
                printf 'ip_discovery_sysfs=%s\n' "$dev/ip_discovery" >> "$manifest"
                find "$dev/ip_discovery" -type f -readable -print -exec sh -c \
                    'for f do printf "%s=" "$f"; tr "\n" "," < "$f"; printf "\n"; done' \
                    sh {} + > "$out/ip-discovery-sysfs.txt" 2>/dev/null || true
            else
                printf 'ip_discovery_sysfs=ABSENT\n' >> "$manifest"
            fi
            ;;
    esac
done

found=0
for base in /lib/firmware/amdgpu /usr/lib/firmware/amdgpu; do
    for name in cyan_skillfish_gpu_info.bin cyan_skillfish1_gpu_info.bin; do
        for suffix in '' .zst .xz .gz; do
            src="${base}/${name}${suffix}"
            [ -r "$src" ] || continue
            found=1
            dst="$out/$name"
            case "$suffix" in
                '')   cp -- "$src" "$dst" ;;
                .zst) command -v zstd >/dev/null 2>&1 || {
                          printf 'ERROR missing zstd for %s\n' "$src" >> "$manifest"
                          continue
                      }
                      zstd -q -d -c -- "$src" > "$dst" ;;
                .xz)  command -v xz >/dev/null 2>&1 || {
                          printf 'ERROR missing xz for %s\n' "$src" >> "$manifest"
                          continue
                      }
                      xz -d -c -- "$src" > "$dst" ;;
                .gz)  command -v gzip >/dev/null 2>&1 || {
                          printf 'ERROR missing gzip for %s\n' "$src" >> "$manifest"
                          continue
                      }
                      gzip -d -c -- "$src" > "$dst" ;;
            esac
            {
                printf 'source=%s\n' "$src"
                printf 'decoded=%s\n' "$dst"
                printf 'decoded_size=%s\n' "$(wc -c < "$dst" | tr -d ' ')"
                sha256sum -- "$dst"
            } >> "$manifest"
        done
    done
done

if [ "$found" -eq 0 ]; then
    printf 'NOT_FOUND: nenhum gpu_info Cyan Skillfish legivel nos diretorios padrao\n' \
        >> "$manifest"
fi

if command -v modinfo >/dev/null 2>&1; then
    modinfo amdgpu 2>/dev/null | grep -E '^(filename|firmware):.*cyan_skillfish' \
        > "$out/modinfo-amdgpu.txt" || true
fi

if command -v dmesg >/dev/null 2>&1; then
    dmesg 2>/dev/null | grep -Ei 'amdgpu|cyan.skillfish|gpu.info|shader.engine|compute.unit' \
        > "$out/dmesg-amdgpu-gpu-info.txt" || true
    grep -E 'SE [0-9]+, SH per SE [0-9]+, CU per SH [0-9]+, active_cu_number [0-9]+' \
        "$out/dmesg-amdgpu-gpu-info.txt" > "$out/topology-from-dmesg.txt" || true
fi

printf 'Coleta concluida: %s\n' "$out"
printf 'Leitura apenas; nenhum MMIO ou firmware instalado foi modificado.\n'
printf 'No Windows, confira topology-from-dmesg.txt e rode tools/fwinspect.py\n'
printf 'sobre o diretorio capturado caso um gpu_info tenha sido encontrado.\n'
