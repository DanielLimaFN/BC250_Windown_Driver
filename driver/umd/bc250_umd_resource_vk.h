#ifndef BC250_UMD_RESOURCE_VK_H
#define BC250_UMD_RESOURCE_VK_H

#include "bc250_umd_vulkan_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _BC250_UMD_DEVICE;
struct _BC250_UMD_RESOURCE;
struct _BC250_UMD_RTV;
struct _BC250_UMD_DSV;

/* Resource Lifecycle */
BOOL Bc250UmdVkCreateResource(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate);

VOID Bc250UmdVkDestroyResource(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes);

/* Render Target View Lifecycle */
BOOL Bc250UmdVkCreateRtv(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RTV* pRtv,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate);

VOID Bc250UmdVkDestroyRtv(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RTV* pRtv);

/* Depth Stencil View Lifecycle */
BOOL Bc250UmdVkCreateDsv(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_DSV* pDsv,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate);

VOID Bc250UmdVkDestroyDsv(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_DSV* pDsv);

/* Buffer upload helper */
BOOL Bc250UmdVkUploadImageData(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes,
    _In_reads_bytes_(ByteSize) CONST VOID* pData,
    UINT RowPitch,
    SIZE_T ByteSize);

/* Contrato puro/testavel da geometria VkBufferImageCopy. */
BOOL Bc250UmdVkComputeImageUploadLayout(
    DXGI_FORMAT Format,
    UINT Width,
    UINT Height,
    UINT RowPitch,
    SIZE_T ByteSize,
    UINT TotalByteSize,
    _Out_ UINT* pBufferRowLength,
    _Out_ SIZE_T* pRequiredBytes);

BOOL Bc250UmdVkUploadBufferData(
    _Inout_ struct _BC250_UMD_DEVICE* pDevice,
    _Inout_ struct _BC250_UMD_RESOURCE* pRes,
    _In_reads_bytes_(ByteSize) CONST VOID* pData,
    SIZE_T ByteSize);

#ifdef __cplusplus
}
#endif

#endif /* BC250_UMD_RESOURCE_VK_H */
