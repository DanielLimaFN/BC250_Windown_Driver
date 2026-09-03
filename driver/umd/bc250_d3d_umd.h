#ifndef BC250_D3D_UMD_H
#define BC250_D3D_UMD_H

#pragma warning(push)
#pragma warning(disable: 4201) /* nameless struct/union in SDK headers */
#include <windows.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>
#pragma warning(pop)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _BC250_UMD_ADAPTER {
    D3D10DDI_HRTADAPTER hRTAdapter;
    CONST D3DDDI_ADAPTERCALLBACKS* pCallbacks;
    UINT Interface;
    UINT Version;
    BOOL IsActive;
} BC250_UMD_ADAPTER;

typedef struct _BC250_UMD_DEVICE {
    D3D10DDI_HRTDEVICE hRTDevice;
    BC250_UMD_ADAPTER* pAdapter;
    UINT Interface;
    UINT Version;
} BC250_UMD_DEVICE;

/* D3D10 / D3D11 Adapter Entry Points */
HRESULT APIENTRY OpenAdapter10(
    _Inout_ D3D10DDIARG_OPENADAPTER* pOpenData);

HRESULT APIENTRY OpenAdapter10_2(
    _Inout_ D3D10DDIARG_OPENADAPTER* pOpenData);

HRESULT APIENTRY OpenAdapter11(
    _Inout_ D3D10DDIARG_OPENADAPTER* pOpenData);

/* D3D12 Adapter Entry Points */
HRESULT APIENTRY OpenAdapter12(
    _Inout_ D3D12DDIARG_OPENADAPTER* pOpenData);

#ifdef __cplusplus
}
#endif

#endif /* BC250_D3D_UMD_H */
