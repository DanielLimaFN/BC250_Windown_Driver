/*
 * bc250-vram-paging-stress.c -- Teste Sintetico Dirigido de Alocacao e Paginacao VidMm
 *
 * Objetivo (Tarefa 4D): substituir o roteiro manual de abrir/fechar o Menu Iniciar
 * por um teste controlado de estresse de memoria de video (D3D11 / VidMm).
 *
 * Aloca repetidamente texturas de 16 MB a 64 MB ate aproximar o teto da VRAM (256 MB),
 * forcando o VidMm a realizar desalocacoes, eviccoes para o Segmento 2 (System Memory)
 * e rajadas controladas de BuildPagingBuffer (FILL / FLUSH_TLB).
 *
 * Uso:
 *   bc250-vram-paging-stress.exe [max_mb=220] [cycles=100] [interval_ms=100]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define TEXTURE_DIM 2048 /* 2048 x 2048 x 4 bytes = 16 MB */
#define TEXTURE_SIZE_MB 16
#define MAX_SLOTS 64

static ID3D11Texture2D *g_textures[MAX_SLOTS] = {0};
static int g_activeCount = 0;

static void LogHeartbeat(FILE *f, const char *fmt, ...)
{
    va_list args;
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (f) fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (f) {
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fflush(f);
    }
}

int main(int argc, char **argv)
{
    int maxTargetMb = (argc > 1) ? atoi(argv[1]) : 220;
    int maxCycles   = (argc > 2) ? atoi(argv[2]) : 50;
    int intervalMs  = (argc > 3) ? atoi(argv[3]) : 100;
    int maxTextures = maxTargetMb / TEXTURE_SIZE_MB;
    int cycle = 0;
    FILE *logFile = NULL;
    HRESULT hr;

    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL featureLevel;
    D3D11_TEXTURE2D_DESC desc;

    if (maxTextures > MAX_SLOTS) maxTextures = MAX_SLOTS;
    if (maxTextures < 2) maxTextures = 2;

    fopen_s(&logFile, "bc250-paging-stress.log", "w");

    LogHeartbeat(logFile, "====================================================================\n");
    LogHeartbeat(logFile, "  AMD BC-250 -- ESTRESSE SINTETICO DE ALOCACAO E PAGINACAO VIDMM    \n");
    LogHeartbeat(logFile, "====================================================================\n");
    LogHeartbeat(logFile, "Config: Alvo=%d MB (%d texturas x %d MB) | Ciclos=%d | Intervalo=%d ms\n\n",
                 maxTargetMb, maxTextures, TEXTURE_SIZE_MB, maxCycles, intervalMs);

    LogHeartbeat(logFile, "[1/3] Criando D3D11 Device (Hardware WDDM)...\n");
    hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);

    if (FAILED(hr)) {
        LogHeartbeat(logFile, "[-] Falha ao criar dispositivo D3D11 (HRESULT: 0x%08X)\n", hr);
        if (logFile) fclose(logFile);
        return 1;
    }
    LogHeartbeat(logFile, "[+] Dispositivo D3D11 criado com sucesso (FeatureLevel: 0x%X)!\n\n", featureLevel);

    ZeroMemory(&desc, sizeof(desc));
    desc.Width = TEXTURE_DIM;
    desc.Height = TEXTURE_DIM;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    LogHeartbeat(logFile, "[2/3] Iniciando loop de ciclos de paginacao...\n");

    for (cycle = 1; cycle <= maxCycles; ++cycle) {
        int i;
        LogHeartbeat(logFile, "--- CICLO %d/%d ---\n", cycle, maxCycles);

        /* Fase A: Alocar ate a cota */
        for (i = 0; i < maxTextures; ++i) {
            if (g_textures[i] == NULL) {
                hr = device->lpVtbl->CreateTexture2D(device, &desc, NULL, &g_textures[i]);
                if (SUCCEEDED(hr)) {
                    g_activeCount++;
                } else {
                    LogHeartbeat(logFile, "  [!] CreateTexture2D falhou no slot %d (0x%08X)\n", i, hr);
                    break;
                }
            }
        }
        LogHeartbeat(logFile, "  [+] Fase A: %d texturas ativas (~%d MB em VRAM)\n",
                     g_activeCount, g_activeCount * TEXTURE_SIZE_MB);

        /* Flush para submeter as alocacoes ao VidMm */
        context->lpVtbl->Flush(context);
        Sleep(intervalMs);

        /* Fase B: Desalocar metade para criar churn de memoria */
        for (i = 0; i < maxTextures / 2; ++i) {
            if (g_textures[i] != NULL) {
                g_textures[i]->lpVtbl->Release(g_textures[i]);
                g_textures[i] = NULL;
                g_activeCount--;
            }
        }
        LogHeartbeat(logFile, "  [-] Fase B: Desalocadas %d texturas (Restam %d texturas, ~%d MB)\n",
                     maxTextures / 2, g_activeCount, g_activeCount * TEXTURE_SIZE_MB);

        context->lpVtbl->Flush(context);
        Sleep(intervalMs);
    }

    LogHeartbeat(logFile, "\n[3/3] Limpeza final...\n");
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (g_textures[i] != NULL) {
            g_textures[i]->lpVtbl->Release(g_textures[i]);
            g_textures[i] = NULL;
        }
    }

    if (context) context->lpVtbl->Release(context);
    if (device) device->lpVtbl->Release(device);

    LogHeartbeat(logFile, "[OK] Teste de estresse concluido com sucesso sem travamento da maquina!\n");
    if (logFile) fclose(logFile);
    return 0;
}
