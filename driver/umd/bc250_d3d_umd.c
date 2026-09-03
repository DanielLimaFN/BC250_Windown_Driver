#include "bc250_d3d_umd.h"
#include <stdlib.h>
#include <string.h>

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
    UNREFERENCED_PARAMETER(pCreateData);

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

    return S_OK;
}

HRESULT APIENTRY OpenAdapter11(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
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

    return S_OK;
}
