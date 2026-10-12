/*
 * bc250-processo-novo.c -- T3 (doc 691): gatilho reprodutivel de "processo novo".
 *
 * Por que existe: nas Corridas 253-262 a trava dura veio ate ~3 s depois de um
 * processo NOVO comecar a usar a GPU (menu, desktop, estresse), mesmo sem
 * submissao ao GFX (doc 691 sec. 5). O estresse de 500 MB media o caminho de
 * falta de memoria (50 E_OUTOFMEMORY por processo), nao churn. Esta ferramenta
 * abre um processo filho novo a cada ciclo, que cria um device D3D11 na BC-250,
 * aloca texturas DENTRO do orcamento, libera e sai. Assim a dispersao (3-600 s)
 * vira uma taxa: travas por 100 processos novos.
 *
 * Log: uma linha por evento, gravada com FILE_FLAG_WRITE_THROUGH e
 * FlushFileBuffers, para a ultima linha sobreviver a trava dura. Toda linha
 * leva data, hora, sessao e pid: um log velho nunca se confunde com o da
 * corrida (o erro da Corrida 262, doc 689).
 *
 * Uso (pai):
 *   bc250-processo-novo.exe [--ciclos 120] [--intervalo-ms 5000]
 *       [--texturas 4] [--mb 8] [--segurar-ms 500] [--clear]
 *       [--timeout-filho-s 60] [--log C:\BC250_PROCESSO_NOVO.log]
 *       [--qualquer-adaptador]
 * O filho e' o mesmo exe com --filho; nao chame direto.
 *
 * Codigos de saida: 0 ok; 1 falha D3D11 no filho; 2 adaptador nao e' a BC-250;
 * 3 filho nao terminou no prazo (o pai para de abrir processos); 4 uso errado.
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu
#define MAX_TEXTURAS 32

typedef struct {
    int filho;
    int ciclos;
    int intervaloMs;
    int texturas;
    int mb;
    int segurarMs;
    int clear;
    int timeoutFilhoS;
    int qualquerAdaptador;
    int ciclo;
    wchar_t log[MAX_PATH];
    wchar_t sessao[64];
} OPCOES;

static HANDLE g_log = INVALID_HANDLE_VALUE;
static OPCOES g_op;

static void LogLinha(const char *fmt, ...)
{
    char corpo[1024];
    char linha[1400];
    SYSTEMTIME st;
    LARGE_INTEGER qpc;
    va_list args;
    DWORD escritos = 0;
    int n;

    va_start(args, fmt);
    _vsnprintf_s(corpo, sizeof(corpo), _TRUNCATE, fmt, args);
    va_end(args);

    GetLocalTime(&st);
    QueryPerformanceCounter(&qpc);
    n = _snprintf_s(linha, sizeof(linha), _TRUNCATE,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u qpc=%lld sessao=%ls pid=%lu papel=%s %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, (long long)qpc.QuadPart, g_op.sessao,
        GetCurrentProcessId(), g_op.filho ? "FILHO" : "PAI", corpo);
    if (n < 0) n = (int)strlen(linha);

    fputs(linha, stdout);
    fflush(stdout);
    if (g_log != INVALID_HANDLE_VALUE) {
        WriteFile(g_log, linha, (DWORD)n, &escritos, NULL);
        FlushFileBuffers(g_log);
    }
}

static int AbrirLog(void)
{
    g_log = CreateFileW(g_op.log, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (g_log == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERRO: nao abri o log %ls (erro %lu). Rode elevado ou use --log.\n",
            g_op.log, GetLastError());
        return 0;
    }
    return 1;
}

static int LerInt(int argc, wchar_t **argv, int *i, int *destino)
{
    if (*i + 1 >= argc) return 0;
    *destino = _wtoi(argv[++(*i)]);
    return 1;
}

static int LerArgs(int argc, wchar_t **argv)
{
    int i;
    memset(&g_op, 0, sizeof(g_op));
    g_op.ciclos = 120;
    g_op.intervaloMs = 5000;
    g_op.texturas = 4;
    g_op.mb = 8;
    g_op.segurarMs = 500;
    g_op.timeoutFilhoS = 60;
    wcscpy_s(g_op.log, MAX_PATH, L"C:\\BC250_PROCESSO_NOVO.log");

    for (i = 1; i < argc; ++i) {
        const wchar_t *a = argv[i];
        int ok = 1;
        if (wcscmp(a, L"--filho") == 0) g_op.filho = 1;
        else if (wcscmp(a, L"--clear") == 0) g_op.clear = 1;
        else if (wcscmp(a, L"--qualquer-adaptador") == 0) g_op.qualquerAdaptador = 1;
        else if (wcscmp(a, L"--ciclos") == 0) ok = LerInt(argc, argv, &i, &g_op.ciclos);
        else if (wcscmp(a, L"--intervalo-ms") == 0) ok = LerInt(argc, argv, &i, &g_op.intervaloMs);
        else if (wcscmp(a, L"--texturas") == 0) ok = LerInt(argc, argv, &i, &g_op.texturas);
        else if (wcscmp(a, L"--mb") == 0) ok = LerInt(argc, argv, &i, &g_op.mb);
        else if (wcscmp(a, L"--segurar-ms") == 0) ok = LerInt(argc, argv, &i, &g_op.segurarMs);
        else if (wcscmp(a, L"--timeout-filho-s") == 0) ok = LerInt(argc, argv, &i, &g_op.timeoutFilhoS);
        else if (wcscmp(a, L"--ciclo") == 0) ok = LerInt(argc, argv, &i, &g_op.ciclo);
        else if (wcscmp(a, L"--log") == 0 && i + 1 < argc) wcscpy_s(g_op.log, MAX_PATH, argv[++i]);
        else if (wcscmp(a, L"--sessao") == 0 && i + 1 < argc) wcscpy_s(g_op.sessao, 64, argv[++i]);
        else ok = 0;
        if (!ok) {
            fprintf(stderr, "argumento invalido: %ls\n", a);
            return 0;
        }
    }
    if (g_op.ciclos < 1 || g_op.intervaloMs < 0 || g_op.texturas < 1 ||
        g_op.texturas > MAX_TEXTURAS || g_op.mb < 1 || g_op.mb > 64 ||
        g_op.segurarMs < 0 || g_op.timeoutFilhoS < 5) {
        fprintf(stderr, "valores fora da faixa (texturas 1..%d, mb 1..64, timeout >= 5 s)\n",
            MAX_TEXTURAS);
        return 0;
    }
    return 1;
}

static void LogMemoria(IDXGIAdapter *adapter, const char *quando)
{
    IDXGIAdapter3 *a3 = NULL;
    DXGI_QUERY_VIDEO_MEMORY_INFO local, naoLocal;
    if (FAILED(IDXGIAdapter_QueryInterface(adapter, &IID_IDXGIAdapter3, (void **)&a3))) {
        LogLinha("MEMORIA %s: IDXGIAdapter3 indisponivel", quando);
        return;
    }
    memset(&local, 0, sizeof(local));
    memset(&naoLocal, 0, sizeof(naoLocal));
    IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
    IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &naoLocal);
    LogLinha("MEMORIA %s: local uso=%llu MB orcamento=%llu MB | nao-local uso=%llu MB orcamento=%llu MB",
        quando,
        (unsigned long long)(local.CurrentUsage >> 20), (unsigned long long)(local.Budget >> 20),
        (unsigned long long)(naoLocal.CurrentUsage >> 20), (unsigned long long)(naoLocal.Budget >> 20));
    IDXGIAdapter3_Release(a3);
}

static int RodarFilho(void)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGIDevice *dxgiDevice = NULL;
    IDXGIAdapter *adapter = NULL;
    ID3D11Texture2D *tex[MAX_TEXTURAS];
    D3D11_TEXTURE2D_DESC desc;
    DXGI_ADAPTER_DESC ad;
    D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
    HRESULT hr;
    int i, criadas = 0, falhas = 0, codigo = 0;

    memset(tex, 0, sizeof(tex));
    LogLinha("INICIO ciclo=%d texturas=%d mb=%d segurar_ms=%d clear=%d",
        g_op.ciclo, g_op.texturas, g_op.mb, g_op.segurarMs, g_op.clear);

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr)) {
        LogLinha("FALHA D3D11CreateDevice hr=0x%08lX", (unsigned long)hr);
        return 1;
    }
    LogLinha("DEVICE criado FeatureLevel=0x%X", (unsigned)fl);

    hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgiDevice);
    if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgiDevice, &adapter);
    if (FAILED(hr) || adapter == NULL || FAILED(IDXGIAdapter_GetDesc(adapter, &ad))) {
        LogLinha("FALHA ao identificar o adaptador hr=0x%08lX", (unsigned long)hr);
        codigo = 1;
        goto fim;
    }
    LogLinha("ADAPTADOR %04X:%04X \"%ls\"", ad.VendorId, ad.DeviceId, ad.Description);
    if ((ad.VendorId != BC250_VENDOR_ID || ad.DeviceId != BC250_DEVICE_ID) &&
        !g_op.qualquerAdaptador) {
        LogLinha("ADAPTADOR NAO E' A BC-250 (1002:13FE): o processo nao exercita o nosso driver");
        codigo = 2;
        goto fim;
    }
    LogMemoria(adapter, "antes");

    memset(&desc, 0, sizeof(desc));
    desc.Width = 1024;
    desc.Height = (UINT)g_op.mb * 256u; /* 1024 x (mb*256) x 4 bytes = mb MiB */
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    for (i = 0; i < g_op.texturas; ++i) {
        hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &tex[i]);
        if (FAILED(hr)) {
            ++falhas;
            LogLinha("TEXTURA %d FALHOU hr=0x%08lX (para de alocar: nao exercitar o caminho de OOM)",
                i + 1, (unsigned long)hr);
            tex[i] = NULL;
            break;
        }
        ++criadas;
        if (g_op.clear) {
            ID3D11RenderTargetView *rtv = NULL;
            static const FLOAT cor[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
            hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex[i], NULL, &rtv);
            if (SUCCEEDED(hr)) {
                ID3D11DeviceContext_ClearRenderTargetView(context, rtv, cor);
                ID3D11RenderTargetView_Release(rtv);
            } else {
                LogLinha("RTV da textura %d FALHOU hr=0x%08lX", i + 1, (unsigned long)hr);
            }
        }
    }
    ID3D11DeviceContext_Flush(context);
    LogLinha("ALOCADAS %d/%d texturas de %d MB (falhas=%d)", criadas, g_op.texturas, g_op.mb, falhas);
    LogMemoria(adapter, "depois");

    if (g_op.segurarMs > 0) Sleep((DWORD)g_op.segurarMs);

    for (i = 0; i < MAX_TEXTURAS; ++i) {
        if (tex[i] != NULL) ID3D11Texture2D_Release(tex[i]);
    }
    ID3D11DeviceContext_Flush(context);
    if (falhas != 0) codigo = 1;

fim:
    if (adapter) IDXGIAdapter_Release(adapter);
    if (dxgiDevice) IDXGIDevice_Release(dxgiDevice);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    LogLinha("FIM ciclo=%d codigo=%d", g_op.ciclo, codigo);
    return codigo;
}

static int RodarPai(void)
{
    wchar_t exe[MAX_PATH];
    wchar_t cmd[2048];
    int c, ok = 0, falhas = 0, adaptadorErrado = 0;
    ULONGLONG ligadoMs = GetTickCount64();

    if (GetModuleFileNameW(NULL, exe, MAX_PATH) == 0) {
        LogLinha("FALHA GetModuleFileNameW erro=%lu", GetLastError());
        return 4;
    }
    LogLinha("SESSAO INICIO ciclos=%d intervalo_ms=%d texturas=%d mb=%d segurar_ms=%d clear=%d timeout_filho_s=%d qualquer_adaptador=%d maquina_ligada_ha_s=%llu log=%ls",
        g_op.ciclos, g_op.intervaloMs, g_op.texturas, g_op.mb, g_op.segurarMs,
        g_op.clear, g_op.timeoutFilhoS, g_op.qualquerAdaptador,
        (unsigned long long)(ligadoMs / 1000ull), g_op.log);

    for (c = 1; c <= g_op.ciclos; ++c) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        DWORD espera, codigo = 0;
        ULONGLONG t0;

        swprintf_s(cmd, 2048,
            L"\"%ls\" --filho --sessao %ls --ciclo %d --texturas %d --mb %d --segurar-ms %d --log \"%ls\"%ls%ls",
            exe, g_op.sessao, c, g_op.texturas, g_op.mb, g_op.segurarMs, g_op.log,
            g_op.clear ? L" --clear" : L"",
            g_op.qualquerAdaptador ? L" --qualquer-adaptador" : L"");
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        LogLinha("CICLO %d/%d abrindo filho", c, g_op.ciclos);
        t0 = GetTickCount64();
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            LogLinha("CICLO %d FALHA CreateProcessW erro=%lu", c, GetLastError());
            ++falhas;
            continue;
        }
        espera = WaitForSingleObject(pi.hProcess, (DWORD)g_op.timeoutFilhoS * 1000u);
        if (espera != WAIT_OBJECT_0) {
            LogLinha("CICLO %d FILHO pid=%lu NAO TERMINOU em %d s: paro de abrir processos (o filho fica vivo para analise)",
                c, pi.dwProcessId, g_op.timeoutFilhoS);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            LogLinha("SESSAO FIM ciclos_feitos=%d ok=%d falhas=%d adaptador_errado=%d motivo=FILHO_PRESO",
                c - 1, ok, falhas, adaptadorErrado);
            return 3;
        }
        GetExitCodeProcess(pi.hProcess, &codigo);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        LogLinha("CICLO %d filho pid=%lu saiu codigo=%lu em %llu ms",
            c, pi.dwProcessId, codigo, (unsigned long long)(GetTickCount64() - t0));
        if (codigo == 0) ++ok;
        else if (codigo == 2) ++adaptadorErrado;
        else ++falhas;
        if (codigo == 2) {
            LogLinha("SESSAO FIM ciclos_feitos=%d ok=%d falhas=%d adaptador_errado=%d motivo=ADAPTADOR_NAO_E_BC250",
                c, ok, falhas, adaptadorErrado);
            return 2;
        }
        if (c < g_op.ciclos && g_op.intervaloMs > 0) Sleep((DWORD)g_op.intervaloMs);
    }
    LogLinha("SESSAO FIM ciclos_feitos=%d ok=%d falhas=%d adaptador_errado=%d motivo=COMPLETO",
        g_op.ciclos, ok, falhas, adaptadorErrado);
    return falhas == 0 ? 0 : 1;
}

int wmain(int argc, wchar_t **argv)
{
    int r;
    if (!LerArgs(argc, argv)) return 4;
    if (!g_op.filho) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        swprintf_s(g_op.sessao, 64, L"%04u%02u%02u-%02u%02u%02u-%lu",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            GetCurrentProcessId());
    } else if (g_op.sessao[0] == L'\0') {
        fprintf(stderr, "--filho exige --sessao\n");
        return 4;
    }
    if (!AbrirLog()) return 4;
    r = g_op.filho ? RodarFilho() : RodarPai();
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    return r;
}
