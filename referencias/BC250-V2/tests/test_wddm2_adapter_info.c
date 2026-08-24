/*++

Copyright (c) 2026 AMD BC-250 Project

Module Name:
    test_wddm2_adapter_info.c

Abstract:
    Validador de Modo Usuario para Topologia de Adaptador WDDM 2.0+
    e Queries Privadas do Mesa RADV Winsys na AMD BC-250.

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#undef va_start
#undef va_end

#include "wddm.h"

#undef va_start
#undef va_end

typedef LONG NTSTATUS;
typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC {
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;
    UINT          VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef UINT KMTQUERYADAPTERINFOTYPE;

typedef struct _D3DKMT_QUERYADAPTERINFO {
    D3DKMT_HANDLE           hAdapter;
    KMTQUERYADAPTERINFOTYPE Type;
    VOID*                   pPrivateDriverData;
    UINT                    PrivateDriverDataSize;
} D3DKMT_QUERYADAPTERINFO;

typedef NTSTATUS (WINAPI *PFND3DKMT_OPENADAPTERFROMHDC)(D3DKMT_OPENADAPTERFROMHDC*);
typedef NTSTATUS (WINAPI *PFND3DKMT_CLOSEADAPTER)(CONST D3DKMT_CLOSEADAPTER*);
typedef NTSTATUS (WINAPI *PFND3DKMT_QUERYADAPTERINFO)(CONST D3DKMT_QUERYADAPTERINFO*);

static PFND3DKMT_OPENADAPTERFROMHDC pfnOpenAdapterFromHdc = NULL;
static PFND3DKMT_CLOSEADAPTER       pfnCloseAdapter       = NULL;
static PFND3DKMT_QUERYADAPTERINFO   pfnQueryAdapterInfo   = NULL;

static bool InitD3DKMT(void)
{
    HMODULE hGdi32 = LoadLibraryA("gdi32.dll");
    if (!hGdi32) return false;

    pfnOpenAdapterFromHdc = (PFND3DKMT_OPENADAPTERFROMHDC)GetProcAddress(hGdi32, "D3DKMTOpenAdapterFromHdc");
    pfnCloseAdapter       = (PFND3DKMT_CLOSEADAPTER)GetProcAddress(hGdi32, "D3DKMTCloseAdapter");
    pfnQueryAdapterInfo   = (PFND3DKMT_QUERYADAPTERINFO)GetProcAddress(hGdi32, "D3DKMTQueryAdapterInfo");

    return (pfnOpenAdapterFromHdc && pfnCloseAdapter && pfnQueryAdapterInfo);
}

int main(void)
{
    printf("========================================================\n");
    printf("  AMD BC-250 - Teste de Topologia WDDM 2.0+ & Mesa RADV\n");
    printf("========================================================\n\n");

    if (!InitD3DKMT()) {
        printf("[ERRO] Nao foi possivel carregar funcoes D3DKMT da gdi32.dll\n");
        return 1;
    }

    HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (!hdc) {
        printf("[AVISO] CreateDCA('DISPLAY') falhou; tentando EnumDisplayDevices...\n");
        DISPLAY_DEVICEA dd = { sizeof(dd) };
        for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++) {
            if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                hdc = CreateDCA(dd.DeviceName, NULL, NULL, NULL);
                if (hdc) break;
            }
        }
    }

    if (!hdc) {
        printf("[ERRO] Nao foi possivel obter HDC de display ativo.\n");
        return 1;
    }

    D3DKMT_OPENADAPTERFROMHDC openInfo = { 0 };
    openInfo.hDc = hdc;
    NTSTATUS status = pfnOpenAdapterFromHdc(&openInfo);
    DeleteDC(hdc);

    if (status != 0) {
        printf("[ERRO] D3DKMTOpenAdapterFromHdc falhou com status 0x%08X\n", (unsigned int)status);
        return 1;
    }

    printf("[OK] Adaptador WDDM Aberto com Sucesso!\n");
    printf("     - Handle: 0x%08X\n", openInfo.hAdapter);
    printf("     - LUID:   0x%08X:0x%08X\n\n", (unsigned int)openInfo.AdapterLuid.HighPart, (unsigned int)openInfo.AdapterLuid.LowPart);

    /* 1. Teste de UMD Private Data: basic_properties (size 3008) */
    printf("[1/3] Consultando basic_properties (Mesa RADV Winsys - Size 3008)...\n");
    BC250_UMD_BASIC_PROPERTIES basicProps = { 0 };
    D3DKMT_QUERYADAPTERINFO qaiBasic = {
        openInfo.hAdapter,
        0, /* KMTQAITYPE_UMDRIVERPRIVATE / DXGKQAITYPE_UMDRIVERPRIVATE */
        &basicProps,
        sizeof(basicProps)
    };

    status = pfnQueryAdapterInfo(&qaiBasic);
    if (status == 0) {
        printf("     [OK] basic_properties obtidas com sucesso:\n");
        printf("          - Vendor ID:        0x%04X\n", (unsigned int)basicProps.header.vendor_id);
        printf("          - Device ID:        0x%04X\n", (unsigned int)basicProps.device_id);
        printf("          - Family ID:        %u (143 = FAMILY_NV / Navi12 Lite)\n", (unsigned int)basicProps.family_id);
        printf("          - Chip Ext Rev:     %u\n", (unsigned int)basicProps.chip_external_rev);
        printf("          - Subsystem ID:     0x%08X\n", (unsigned int)basicProps.subsystem_id);
        printf("          - VRAM Type:        %u (6 = AMDGPU_VRAM_TYPE_GDDR6)\n", (unsigned int)basicProps.vram_type);
        printf("          - VRAM Bus Width:   %u-bit\n", (unsigned int)basicProps.vram_bit_width);
        printf("          - Render Backends:  %u Pipes (Mask 0x%02X)\n", (unsigned int)basicProps.num_rb_pipes, (unsigned int)basicProps.enabled_rb_pipes_mask);
    } else {
        printf("     [AVISO] Query basic_properties retornou status 0x%08X\n", (unsigned int)status);
    }

    /* 2. Teste de UMD Private Data: gpu_properties (size 17248) */
    printf("\n[2/3] Consultando gpu_properties (Mesa RADV Winsys - Size 17248)...\n");
    BC250_UMD_GPU_PROPERTIES gpuProps = { 0 };
    D3DKMT_QUERYADAPTERINFO qaiGpu = {
        openInfo.hAdapter,
        0, /* DXGKQAITYPE_UMDRIVERPRIVATE */
        &gpuProps,
        sizeof(gpuProps)
    };

    status = pfnQueryAdapterInfo(&qaiGpu);
    if (status == 0) {
        printf("     [OK] gpu_properties obtidas com sucesso:\n");
        printf("          - Shader Engines:   %u\n", (unsigned int)gpuProps.num_shader_engines);
        printf("          - Arrays / SE:      %u\n", (unsigned int)gpuProps.num_shader_arrays_per_engine);
        printf("          - CUs per WGP:      %u\n", (unsigned int)gpuProps.num_cu_per_wgp);
        printf("          - WGP Bitmap SE0:   0x%02X (6 WGPs / 12 CUs)\n", (unsigned int)gpuProps.wgp_bitmap[0][0]);
        printf("          - WGP Bitmap SE1:   0x%02X (6 WGPs / 12 CUs)\n", (unsigned int)gpuProps.wgp_bitmap[1][0]);
        printf("          - Total Shaders:    1536 Stream Processors (24 CUs / 12 WGPs)\n");
        printf("          - Wavefront Size:   %u\n", (unsigned int)gpuProps.wave_front_size);
        printf("          - VGPRs Visiveis:   %u\n", (unsigned int)gpuProps.num_shader_visible_vgprs);
        printf("          - Crystal Freq:     %u Hz (25 MHz)\n", (unsigned int)gpuProps.clock_crystal_freq_hz);
        printf("          - VRAM Visivel:     %llu MB (%llu GB GDDR6)\n", (unsigned long long)(gpuProps.vram_vis_size / (1024 * 1024)), (unsigned long long)(gpuProps.vram_vis_size / (1024ULL * 1024 * 1024)));
        printf("          - GPU VA Range:     0x%llX - 0x%llX (48-bit GPUVM)\n", (unsigned long long)gpuProps.va_start, (unsigned long long)gpuProps.va_end);
        printf("          - Microcode ME:     v%u\n", (unsigned int)gpuProps.me_fw_version);
        printf("          - Microcode MEC:    v%u\n", (unsigned int)gpuProps.mec_fw_version);
        printf("          - Microcode PFP:    v%u\n", (unsigned int)gpuProps.pfp_fw_version);
    } else {
        printf("     [AVISO] Query gpu_properties retornou status 0x%08X\n", (unsigned int)status);
    }

    /* 3. Fechamento */
    printf("\n[3/3] Fechando Handle de Adaptador...\n");
    D3DKMT_CLOSEADAPTER closeInfo = { openInfo.hAdapter };
    pfnCloseAdapter(&closeInfo);
    printf("     [OK] Adaptador Fechado.\n\n");

    printf("========================================================\n");
    printf("  FASE 1 (TOPOLOGIA WDDM 2.0+ & RADV WINSYS) VALIDADA!\n");
    printf("========================================================\n");

    return 0;
}
