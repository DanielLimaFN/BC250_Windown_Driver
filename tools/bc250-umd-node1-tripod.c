#define COBJMACROS
#include <windows.h>
#pragma warning(push)
#pragma warning(disable: 4201)
#include <d3d10_1.h>
#include <dxgi.h>
#pragma warning(pop)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC250_VENDOR_ID 0x1002u
#define BC250_DEVICE_ID 0x13FEu
#define TOKEN_CHARS 32u

static int TokenPresent(void)
{
    char token[TOKEN_CHARS + 1u];
    DWORD length = GetEnvironmentVariableA(
        "BC250_UMD_NODE1_TRIPOD_TOKEN", token, ARRAYSIZE(token));
    return length == TOKEN_CHARS;
}

static int ReadOwnUmdVerdict(char* path, size_t pathBytes)
{
    HANDLE file;
    DWORD size;
    DWORD read = 0;
    char* text;
    int passed = 0;

    if (_snprintf_s(path, pathBytes, _TRUNCATE,
            "C:\\Windows\\Temp\\BC250_UMD_LOG_%lu.txt",
            GetCurrentProcessId()) < 0) return 0;
    file = CreateFileA(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0u || size > (4u * 1024u * 1024u)) {
        CloseHandle(file);
        return 0;
    }
    text = (char*)calloc((size_t)size + 1u, 1u);
    if (text != NULL && ReadFile(file, text, size, &read, NULL) && read == size) {
        passed = strstr(text, "NODE1_TRIPOD_RESULT: hr=0x00000000") != NULL &&
            strstr(text, "Poison=1 Submitted=1 Fence=1 Data=1 Cleanup=1") != NULL;
    }
    free(text);
    CloseHandle(file);
    return passed;
}

int main(void)
{
    IDXGIFactory1* factory = NULL;
    IDXGIAdapter1* selected = NULL;
    ID3D10Device1* device = NULL;
    HRESULT hr;
    UINT index;
    char logPath[MAX_PATH];

    if (!TokenPresent()) {
        fprintf(stderr, "preflight: token de corrida ausente ou invalido\n");
        return 2;
    }
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateDXGIFactory1 falhou: 0x%08X\n", (unsigned)hr);
        return 3;
    }
    for (index = 0u; ; ++index) {
        IDXGIAdapter1* adapter = NULL;
        DXGI_ADAPTER_DESC1 desc;
        hr = IDXGIFactory1_EnumAdapters1(factory, index, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) goto cleanup;
        memset(&desc, 0, sizeof(desc));
        hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
        if (SUCCEEDED(hr) && desc.VendorId == BC250_VENDOR_ID &&
            desc.DeviceId == BC250_DEVICE_ID) {
            selected = adapter;
            break;
        }
        IDXGIAdapter1_Release(adapter);
    }
    if (selected == NULL) {
        fprintf(stderr, "preflight: BC-250 1002:13FE nao encontrada pelo DXGI\n");
        hr = E_FAIL;
        goto cleanup;
    }

    (void)_snprintf_s(logPath, sizeof(logPath), _TRUNCATE,
        "C:\\Windows\\Temp\\BC250_UMD_LOG_%lu.txt", GetCurrentProcessId());
    (void)DeleteFileA(logPath);
    hr = D3D10CreateDevice1((IDXGIAdapter*)selected, D3D10_DRIVER_TYPE_HARDWARE,
        NULL, 0u, D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &device);
    if (SUCCEEDED(hr)) {
        fprintf(stderr, "FALHA DE SEGURANCA: CreateDevice retornou sucesso no modo de prova\n");
        hr = E_UNEXPECTED;
        goto cleanup;
    }

    Sleep(100u);
    if (!ReadOwnUmdVerdict(logPath, sizeof(logPath))) {
        fprintf(stderr, "tripod UMD nao fechou; log=%s CreateDevice=0x%08X\n",
            logPath, (unsigned)hr);
        hr = E_FAIL;
        goto cleanup;
    }
    printf("B7 UMD NODE1 CALLBACK LEGS: PASS; KMD RPTR/WPTR AINDA EXIGIDO\n");
    printf("umd_log=%s\n", logPath);
    hr = S_OK;

cleanup:
    if (device != NULL) ID3D10Device1_Release(device);
    if (selected != NULL) IDXGIAdapter1_Release(selected);
    if (factory != NULL) IDXGIFactory1_Release(factory);
    return SUCCEEDED(hr) ? 0 : 1;
}
