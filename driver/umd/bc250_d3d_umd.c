#include "bc250_d3d_umd.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* =========================================================================
 * Tracer da UMD (instrumentacao 3.82 — nao altera nenhum retorno)
 *
 * Motivo: nas corridas 12:46, 13:32 e 14:25 sob RenderNode=1 o dxgkrnl
 * entregou topologia vazia ao CommitVidPn com hPrimaryAllocation=NULL, e
 * DxgkDdiCreateAllocation nunca foi chamada. A primary allocation de um
 * VidPnSource WDDM nasce na UMD, pelo runtime D3D — logo precisamos saber,
 * com medicao e nao com hipotese, se esta DLL e' sequer carregada, por qual
 * processo, e em que ponto exato o runtime desiste.
 *
 * O arquivo fica em C:\Windows\Temp porque a raiz de C: nao e' gravavel pela
 * conta virtual do dwm.exe. Sobrevive a tela preta e ao reboot.
 * ========================================================================= */

static void Bc250UmdTrace(_Printf_format_string_ const char *Format, ...)
{
    char message[512];
    char line[768];
    char imagePath[MAX_PATH];
    const char *imageName = "?";
    va_list args;
    int messageLength;
    int lineLength;
    HANDLE file;
    DWORD written = 0;
    SYSTEMTIME now;

    va_start(args, Format);
    messageLength = _vsnprintf_s(message, sizeof(message), _TRUNCATE, Format, args);
    va_end(args);
    if (messageLength < 0) {
        return;
    }

    if (GetModuleFileNameA(NULL, imagePath, (DWORD)sizeof(imagePath)) != 0) {
        const char *slash = strrchr(imagePath, '\\');
        imageName = (slash != NULL) ? (slash + 1) : imagePath;
    }

    GetLocalTime(&now);
    lineLength = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "%02u:%02u:%02u.%03u [pid=%lu %s] %s\r\n",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), imageName, message);
    if (lineLength < 0) {
        return;
    }

    OutputDebugStringA(line);

    file = CreateFileA("C:\\Windows\\Temp\\BC250_UMD_LOG.txt",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        file = CreateFileA("C:\\BC250_UMD_LOG.txt",
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    (void)WriteFile(file, line, (DWORD)lineLength, &written, NULL);
    (void)CloseHandle(file);
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason == DLL_PROCESS_ATTACH) {
        (void)DisableThreadLibraryCalls(Instance);
        Bc250UmdTrace("=== bc250umd.dll CARREGADA ===");
    }
    return TRUE;
}

/* =========================================================================
 * D3D10 / D3D11 Adapter Functions
 * ========================================================================= */

static SIZE_T APIENTRY Bc250Umd10CalcPrivateDeviceSize(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE* pCalcData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCalcData);
    return sizeof(BC250_UMD_DEVICE);
}

static HRESULT APIENTRY Bc250Umd10CreateDevice(
    D3D10DDI_HADAPTER hAdapter,
    _In_ D3D10DDIARG_CREATEDEVICE* pCreateData)
{
    UNREFERENCED_PARAMETER(hAdapter);

    /*
     * Ponto exato onde o runtime D3D desiste hoje. Se este log aparecer numa
     * corrida com RenderNode=1, esta' PROVADO que o runtime chegou ate' aqui e
     * que o E_NOTIMPL e' o bloqueio; se NAO aparecer, o bloqueio e' anterior e
     * a UMD esta' inocente.
     */
    if (pCreateData != NULL) {
        Bc250UmdTrace("D3D10 CreateDevice: Interface=0x%08X, Version=0x%08X -> E_NOTIMPL",
            pCreateData->Interface, pCreateData->Version);
    } else {
        Bc250UmdTrace("D3D10 CreateDevice: pCreateData=NULL -> E_NOTIMPL");
    }

    /* Fail-closed: 3D rasterizer pipeline not implemented in this UMD probe.
     * Direct3D/DXGI will report DXGI_ERROR_UNSUPPORTED instead of crashing. */
    return E_NOTIMPL;
}

static HRESULT APIENTRY Bc250Umd10CloseAdapter(D3D10DDI_HADAPTER hAdapter)
{
    BC250_UMD_ADAPTER* adapter = (BC250_UMD_ADAPTER*)hAdapter.pDrvPrivate;
    if (adapter != NULL) {
        adapter->IsActive = FALSE;
        free(adapter);
    }
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd10GetSupportedVersions(
    D3D10DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions)
{
    static const UINT64 s_SupportedVersions[] = {
        D3D10_0_DDI_SUPPORTED,
        D3D10_1_DDI_SUPPORTED,
        D3D11_0_DDI_SUPPORTED,
        D3D11_1_DDI_SUPPORTED
    };
    UINT count = (UINT)(sizeof(s_SupportedVersions) / sizeof(s_SupportedVersions[0]));

    UNREFERENCED_PARAMETER(hAdapter);

    if (!puEntries) {
        return E_INVALIDARG;
    }

    if (!pSupportedDDIInterfaceVersions) {
        *puEntries = count;
        return S_OK;
    }

    if (*puEntries < count) {
        *puEntries = count;
        return E_INVALIDARG;
    }

    for (UINT i = 0; i < count; ++i) {
        pSupportedDDIInterfaceVersions[i] = s_SupportedVersions[i];
    }
    *puEntries = count;
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd10GetCaps(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10_2DDIARG_GETCAPS* pCapsData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (!pCapsData || !pCapsData->pData) {
        return E_INVALIDARG;
    }

    memset(pCapsData->pData, 0, pCapsData->DataSize);
    return S_OK;
}

static CONST D3D10_2DDI_ADAPTERFUNCS g_Bc250UmdAdapterFuncs10_2 = {
    Bc250Umd10CalcPrivateDeviceSize,
    Bc250Umd10CreateDevice,
    Bc250Umd10CloseAdapter,
    Bc250Umd10GetSupportedVersions,
    Bc250Umd10GetCaps
};

static CONST D3D10DDI_ADAPTERFUNCS g_Bc250UmdAdapterFuncs10 = {
    Bc250Umd10CalcPrivateDeviceSize,
    Bc250Umd10CreateDevice,
    Bc250Umd10CloseAdapter
};

HRESULT APIENTRY OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    BC250_UMD_ADAPTER* adapter = NULL;

    if (!pOpenData) {
        return E_INVALIDARG;
    }

    adapter = (BC250_UMD_ADAPTER*)calloc(1, sizeof(BC250_UMD_ADAPTER));
    if (!adapter) {
        return E_OUTOFMEMORY;
    }

    adapter->hRTAdapter = pOpenData->hRTAdapter;
    adapter->pCallbacks = pOpenData->pAdapterCallbacks;
    adapter->Interface = pOpenData->Interface;
    adapter->Version = pOpenData->Version;
    adapter->IsActive = TRUE;

    pOpenData->hAdapter.pDrvPrivate = adapter;
    pOpenData->pAdapterFuncs_2 = (D3D10_2DDI_ADAPTERFUNCS*)&g_Bc250UmdAdapterFuncs10_2;

    Bc250UmdTrace("OpenAdapter10_2: Interface=0x%08X, Version=0x%08X -> S_OK",
        pOpenData->Interface, pOpenData->Version);
    return S_OK;
}

HRESULT APIENTRY OpenAdapter10(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    BC250_UMD_ADAPTER* adapter = NULL;

    if (!pOpenData) {
        return E_INVALIDARG;
    }

    adapter = (BC250_UMD_ADAPTER*)calloc(1, sizeof(BC250_UMD_ADAPTER));
    if (!adapter) {
        return E_OUTOFMEMORY;
    }

    adapter->hRTAdapter = pOpenData->hRTAdapter;
    adapter->pCallbacks = pOpenData->pAdapterCallbacks;
    adapter->Interface = pOpenData->Interface;
    adapter->Version = pOpenData->Version;
    adapter->IsActive = TRUE;

    pOpenData->hAdapter.pDrvPrivate = adapter;
    pOpenData->pAdapterFuncs = (D3D10DDI_ADAPTERFUNCS*)&g_Bc250UmdAdapterFuncs10;

    Bc250UmdTrace("OpenAdapter10: Interface=0x%08X, Version=0x%08X -> S_OK",
        pOpenData->Interface, pOpenData->Version);
    return S_OK;
}

HRESULT APIENTRY OpenAdapter11(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    Bc250UmdTrace("OpenAdapter11: delegando para OpenAdapter10_2");
    return OpenAdapter10_2(pOpenData);
}

/* =========================================================================
 * D3D12 Adapter Functions
 * ========================================================================= */

static SIZE_T APIENTRY Bc250Umd12CalcPrivateDeviceSize(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE* pCalcData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCalcData);
    return sizeof(BC250_UMD_DEVICE);
}

static HRESULT APIENTRY Bc250Umd12CreateDevice(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_CREATEDEVICE_0003* pCreateData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateData);
    Bc250UmdTrace("D3D12 CreateDevice -> E_NOTIMPL");
    return E_NOTIMPL;
}

static HRESULT APIENTRY Bc250Umd12CloseAdapter(D3D12DDI_HADAPTER hAdapter)
{
    BC250_UMD_ADAPTER* adapter = (BC250_UMD_ADAPTER*)hAdapter.pDrvPrivate;
    if (adapter != NULL) {
        adapter->IsActive = FALSE;
        free(adapter);
    }
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd12GetSupportedVersions(
    D3D12DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions)
{
    static const UINT64 s_SupportedVersions12[] = {
        D3D12DDI_SUPPORTED_0020
    };
    UINT count = (UINT)(sizeof(s_SupportedVersions12) / sizeof(s_SupportedVersions12[0]));

    UNREFERENCED_PARAMETER(hAdapter);
    if (!puEntries) {
        return E_INVALIDARG;
    }
    if (!pSupportedDDIInterfaceVersions) {
        *puEntries = count;
        return S_OK;
    }
    if (*puEntries < count) {
        *puEntries = count;
        return E_INVALIDARG;
    }
    for (UINT i = 0; i < count; ++i) {
        pSupportedDDIInterfaceVersions[i] = s_SupportedVersions12[i];
    }
    *puEntries = count;
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd12GetCaps(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_GETCAPS* pCapsData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (!pCapsData || !pCapsData->pData) {
        return E_INVALIDARG;
    }
    memset(pCapsData->pData, 0, pCapsData->DataSize);
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd12GetOptionalDDITables(
    D3D12DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) D3D12DDI_TABLE_REQUEST* pTableRequests)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (puEntries) {
        *puEntries = 0;
    }
    UNREFERENCED_PARAMETER(pTableRequests);
    return S_OK;
}

static HRESULT APIENTRY Bc250Umd12FillDDITable(
    D3D12DDI_HADAPTER hAdapter,
    D3D12DDI_TABLE_TYPE TableType,
    _Inout_updates_bytes_(TableLength) VOID* pTable,
    SIZE_T TableLength,
    UINT AdditionalParameter,
    _Inout_ D3D12DDI_HRTTABLE hRTTable)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(TableType);
    UNREFERENCED_PARAMETER(pTable);
    UNREFERENCED_PARAMETER(TableLength);
    UNREFERENCED_PARAMETER(AdditionalParameter);
    UNREFERENCED_PARAMETER(hRTTable);
    return E_NOTIMPL;
}

static VOID APIENTRY Bc250Umd12DestroyDevice(D3D12DDI_HDEVICE hDevice)
{
    UNREFERENCED_PARAMETER(hDevice);
}

static CONST D3D12DDI_ADAPTERFUNCS g_Bc250UmdAdapterFuncs12 = {
    Bc250Umd12CalcPrivateDeviceSize,
    Bc250Umd12CreateDevice,
    Bc250Umd12CloseAdapter,
    Bc250Umd12GetSupportedVersions,
    Bc250Umd12GetCaps,
    Bc250Umd12GetOptionalDDITables,
    Bc250Umd12FillDDITable,
    Bc250Umd12DestroyDevice
};

HRESULT APIENTRY OpenAdapter12(_Inout_ D3D12DDIARG_OPENADAPTER* pOpenData)
{
    BC250_UMD_ADAPTER* adapter = NULL;

    if (!pOpenData) {
        return E_INVALIDARG;
    }

    adapter = (BC250_UMD_ADAPTER*)calloc(1, sizeof(BC250_UMD_ADAPTER));
    if (!adapter) {
        return E_OUTOFMEMORY;
    }

    adapter->hRTAdapter = pOpenData->hRTAdapter;
    adapter->pCallbacks = pOpenData->pAdapterCallbacks;
    adapter->IsActive = TRUE;

    pOpenData->hAdapter.pDrvPrivate = adapter;
    pOpenData->pAdapterFuncs = (D3D12DDI_ADAPTERFUNCS*)&g_Bc250UmdAdapterFuncs12;

    Bc250UmdTrace("OpenAdapter12 -> S_OK");
    return S_OK;
}

HRESULT APIENTRY OpenAdapter(PVOID pOpenData)
{
    UNREFERENCED_PARAMETER(pOpenData);
    Bc250UmdTrace("OpenAdapter (D3D9 probe) -> E_NOTIMPL");
    return E_NOTIMPL;
}

