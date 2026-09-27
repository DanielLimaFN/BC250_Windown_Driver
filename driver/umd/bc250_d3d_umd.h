#ifndef BC250_D3D_UMD_H
#define BC250_D3D_UMD_H

#pragma warning(push)
#pragma warning(disable: 4201) /* nameless struct/union in SDK headers */
#include <windows.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>
#pragma warning(pop)

/* d3d9.h MAKE_D3DSTATUS(2165/2166), also required by the DXGI DDI. */
#ifndef S_NOT_RESIDENT
#define S_NOT_RESIDENT ((HRESULT)0x08760875L)
#define S_RESIDENT_IN_SHARED_MEMORY ((HRESULT)0x08760876L)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Production gate: this may become 1 only when CreateDevice publishes a
 * complete native table backed by real VidMm allocations and GPU retirement. */
#define BC250_D3D10_11_NATIVE_READY 0

/*
 * P7_D3D11_CAPS_PROBE (06/09) -- instrumentacao, nao funcionalidade.
 *
 * MOTIVO: o dump do dwm.exe da corrida 2026-09-06 22:42 provou que o DWM exige
 * device D3D11 e fail-fasta com DXGI_ERROR_UNSUPPORTED em
 * dwmcore!CD3DDevice::CreateD3D11Device. O dimensionamento em
 * docs/173_DIMENSIONAMENTO_D3D11_PARA_DWM.md mostrou que o piso e' o DDI
 * D3D10_1 com 105 entradas de device -- obra grande. Antes de comecar essa
 * obra, tres perguntas continuam sem resposta e NAO se resolvem por leitura:
 *
 *   1. o dwmcore aceita um device de feature level 10_1?
 *   2. quais combinacoes de caps ele valida antes de criar o device?
 *   3. qual header corresponde ao Interface 0x000B0011 (minor 17) no 19041?
 *
 * O probe responde as tres em UMA corrida: OpenAdapter passa a devolver uma
 * tabela de adapter valida, pfnGetSupportedVersions e pfnGetCaps respondem e
 * REGISTRAM o que foi perguntado, e pfnCreateDevice continua falhando fechado.
 *
 * O probe NAO desenha nada, NAO promete render e NAO e' fallback de software.
 * Ele so' faz o runtime falar. Default DESLIGADO: exige armar explicitamente.
 */
/* Declaracoes puras do probe: driver/umd/bc250_umd_caps_probe.h */
#include "bc250_umd_caps_probe.h"

#include "bc250_wddm_winsys.h"
#include "../core/bc250_umd_private_contract.h"
#include "../core/bc250_dxbc_parser.h"
#include "../core/bc250_dxbc_spirv_bridge.h"
#include <dxgiddi.h>
#include "bc250_umd_vulkan_bridge.h"
#include "bc250_umd_resource_vk.h"
#include "bc250_umd_pipeline_cache.h"

/* Shader object representation */
typedef struct _BC250_UMD_SHADER {
    BC250_DXBC_SHADER_TYPE Type;
    UINT MajorVersion;
    UINT MinorVersion;
    UINT TotalSizeBytes;
    UINT OpcodeTokenCount;
    BOOL IsParsed;
    BC250_DXBC_CONTAINER Container;
    CONST UINT *pCodeTokens;
    D3D10DDI_HRTSHADER hRTShader;
    /*
     * C3 (12/09): modulo SPIR-V traduzido, com ownership EXPLICITO por shader.
     * SpirvValid so' vai a TRUE depois que Bc250DxbcTranslateToSpirv devolve OK,
     * e Bc250Umd10DestroyShader e' obrigado a chamar Bc250DxbcReleaseSpirv antes
     * de zerar a estrutura -- senao cada shader do DWM vaza o modulo dentro do
     * compositor. Fechado por default: sem tradutor armado, SpirvValid e' FALSE
     * e nada muda no comportamento do driver.
     *
     * Ter o SPIR-V NAO autoriza anunciar pipeline D3D: faltam descriptors,
     * pipeline grafico, render target e Draw -- e o anel grafico (PM4 de DRAW)
     * nao existe em nenhuma linha deste repositorio.
     */
    BOOL SpirvValid;
    BC250_DXBC_SPIRV_MODULE Spirv;
    VkShaderModule VkModule;
    BOOL VkModuleCreated;
    uint32_t NumBindings;
    VkDescriptorSetLayoutBinding Bindings[158];
    VkDescriptorSetLayout DescriptorSetLayout;
} BC250_UMD_SHADER;

/* Resource object representation */
typedef struct _BC250_UMD_RESOURCE {
    D3D10DDIRESOURCE_TYPE ResourceDimension;
    DXGI_FORMAT Format;
    UINT Usage;
    UINT BindFlags;
    UINT MapFlags;
    UINT MiscFlags;
    UINT MipLevels;
    UINT ArraySize;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT SampleCount;
    UINT RowPitch;
    UINT SlicePitch;
    UINT TotalByteSize;
    BOOL IsPrimary;
    BOOL Created;
    BC250_WINSYS_ALLOCATION Allocation;
    D3D10DDI_HRTRESOURCE hRTResource;
    D3D10DDI_HKMRESOURCE hKMResource;
    void *pCpuShadow;
    /* Vulkan objects */
    VkBuffer VkBuf;
    VkImage VkImg;
    VkDeviceMemory VkMem;
    VkDeviceSize VkMemSize;
    VkImageLayout VkLayout;
    /* Numero de DDIs de escrita de textura que chegaram a fonte unica de
     * sincronia (UpdateSubresourceUP ou Unmap). Diagnostico por recurso; nao
     * afirma que o upload Vulkan concluiu. */
    UINT TextureWriteDdiCount;
    BOOL VkResourceCreated;
    BOOL WddmAllocationUploaded;
} BC250_UMD_RESOURCE;

/* Maximum simultaneous render targets and viewports */
#ifndef D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT
#define D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT 8
#endif

#ifndef D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
#define D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE 16
#endif

#ifndef D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
#define D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT 32
#endif

#ifndef D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
#define D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT 14
#endif

#ifndef D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
#define D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT 16
#endif

#ifndef D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
#define D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT 128
#endif

#ifndef D3D11_SO_BUFFER_SLOT_COUNT
#define D3D11_SO_BUFFER_SLOT_COUNT 4
#endif

/* Render Target View representation */
typedef struct _BC250_UMD_RTV {
    D3D10DDI_HRESOURCE hResource;
    BC250_UMD_RESOURCE* pResource;
    DXGI_FORMAT Format;
    D3D10DDIRESOURCE_TYPE ResourceDimension;
    D3D10DDI_HRTRENDERTARGETVIEW hRTRTV;
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
    BOOL Created;
    /* Vulkan objects */
    VkImageView VkView;
    VkRenderPass VkPass;
    VkRenderPass VkPassLoad;
    VkFramebuffer VkFb;
    BOOL VkRtvCreated;
    /* DX.28: imagem para a qual VkView/VkFb foram criados. Se a rotacao de
     * identidades trocar pResource->VkImg, a RTV tem de seguir o recurso
     * (Bc250UmdVkRtvFollowResource) em vez de desenhar na imagem antiga. */
    VkImage VkImageAtCreation;
#define BC250_RTV_MAX_CACHED_FBS 8u
    struct {
        VkImage Image;
        VkImageView View;
        VkFramebuffer Fb;
    } FbCache[BC250_RTV_MAX_CACHED_FBS];
    uint32_t FbCacheCount;
    uint32_t FollowHitCount;
    uint32_t FollowMissCount;
} BC250_UMD_RTV;

/* Depth Stencil View representation */
typedef struct _BC250_UMD_DSV {
    D3D10DDI_HRESOURCE hResource;
    BC250_UMD_RESOURCE* pResource;
    DXGI_FORMAT Format;
    D3D10DDIRESOURCE_TYPE ResourceDimension;
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDSV;
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
    BOOL Created;
    /* Vulkan objects */
    VkImageView VkView;
    BOOL VkDsvCreated;
} BC250_UMD_DSV;

/* Blend state object representation */
typedef struct _BC250_UMD_BLEND_STATE {
    D3D10_1_DDI_BLEND_DESC Desc;
    D3D10DDI_HRTBLENDSTATE hRTBlendState;
    BOOL Created;
} BC250_UMD_BLEND_STATE;

/* Depth stencil state object representation */
typedef struct _BC250_UMD_DEPTH_STENCIL_STATE {
    D3D10_DDI_DEPTH_STENCIL_DESC Desc;
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState;
    BOOL Created;
} BC250_UMD_DEPTH_STENCIL_STATE;

/* Rasterizer state object representation */
typedef struct _BC250_UMD_RASTERIZER_STATE {
    D3D10_DDI_RASTERIZER_DESC Desc;
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState;
    BOOL Created;
} BC250_UMD_RASTERIZER_STATE;

/* Sampler state object representation */
typedef struct _BC250_UMD_SAMPLER {
    D3D10_DDI_SAMPLER_DESC Desc;
    D3D10DDI_HRTSAMPLER hRTSampler;
    BOOL Created;
} BC250_UMD_SAMPLER;

/* Input Element Layout representation */
typedef struct _BC250_UMD_ELEMENT_LAYOUT {
    UINT NumElements;
    D3D10DDIARG_INPUT_ELEMENT_DESC Elements[32];
    D3D10DDI_HRTELEMENTLAYOUT hRTElementLayout;
    BOOL Created;
} BC250_UMD_ELEMENT_LAYOUT;

/* Shader Resource View (SRV) representation */
typedef struct _BC250_UMD_SRV {
    D3D10_1DDIARG_CREATESHADERRESOURCEVIEW Desc;
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTSRV;
    BC250_UMD_RESOURCE* pResource;
    BOOL Created;
} BC250_UMD_SRV;

/* Query representation */
typedef struct _BC250_UMD_QUERY {
    D3D10DDI_QUERY QueryType;
    UINT MiscFlags;
    D3D10DDI_HRTQUERY hRTQuery;
    BOOL IsActive;
    BOOL HasData;
    UINT64 StartCounter;
    UINT64 EndCounter;
    UINT64 Data;
    BOOL Created;
} BC250_UMD_QUERY;

/* UMD internal command buffer for packet emission */
typedef struct _BC250_UMD_COMMAND_BUFFER {
    BC250_WINSYS_ALLOCATION Allocation;
    uint32_t* pCpuVa;
    uint32_t CapacityDwords;
    uint32_t CursorDwords;
    BOOL Initialized;
} BC250_UMD_COMMAND_BUFFER;

/*
 * Slots do anel de command buffer do blt. Quatro basta para o DWM: ele emite
 * poucos draws por quadro, e a espera de reciclagem so' alcanca trabalho
 * submetido quatro blts atras -- na pratica ja concluido.
 */
/* (o anel de slots do blt saiu em 13/09 junto com a submissao de contexto) */

#define BC250_UMD_NODE1_PROOF_VALUE_COUNT 5u
#define BC250_UMD_NODE1_PROOF_POISON_BASE 0x43DAFFF0u
#define BC250_UMD_NODE1_PROOF_FENCE_POISON 0x43DAFFFEull
#define BC250_UMD_NODE1_PROOF_FENCE_VALUE 0xBC250F01u

typedef struct _BC250_UMD_NODE1_PROOF_RESULT {
    uint64_t DestinationGpuVa;
    uint64_t FenceGpuVa;
    uint64_t FenceObserved;
    uint32_t Poison[BC250_UMD_NODE1_PROOF_VALUE_COUNT];
    uint32_t Expected[BC250_UMD_NODE1_PROOF_VALUE_COUNT];
    uint32_t Observed[BC250_UMD_NODE1_PROOF_VALUE_COUNT];
    BOOL PoisonVerifiedBeforeSubmit;
    BOOL Submitted;
    BOOL FenceMatched;
    BOOL DataMatched;
    BOOL CleanupComplete;
    HRESULT OperationHr;
} BC250_UMD_NODE1_PROOF_RESULT;

/* Context pipeline & Output Merger state */
typedef struct _BC250_UMD_PIPELINE_STATE {
    /* Output Merger */
    UINT NumBoundRTVs;
    D3D10DDI_HRENDERTARGETVIEW BoundRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    D3D10DDI_HDEPTHSTENCILVIEW BoundDSV;
    UINT NumViewports;
    D3D10_DDI_VIEWPORT Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT NumScissorRects;
    D3D10_DDI_RECT ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D10DDI_HBLENDSTATE BoundBlendState;
    FLOAT BlendFactor[4];
    UINT SampleMask;
    D3D10DDI_HDEPTHSTENCILSTATE BoundDepthStencilState;
    UINT StencilRef;

    /* Rasterizer */
    D3D10DDI_HRASTERIZERSTATE BoundRasterizerState;

    /* Input Assembly */
    D3D10_DDI_PRIMITIVE_TOPOLOGY Topology;
    D3D10DDI_HELEMENTLAYOUT InputLayout;
    D3D10DDI_HRESOURCE VertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT VertexStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT VertexOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    D3D10DDI_HRESOURCE IndexBuffer;
    DXGI_FORMAT IndexFormat;
    UINT IndexOffset;

    /* Shader bindings */
    D3D10DDI_HSHADER BoundVS;
    D3D10DDI_HSHADER BoundPS;
    D3D10DDI_HSHADER BoundGS;

    /* Constant buffer bindings */
    D3D10DDI_HRESOURCE VsConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    D3D10DDI_HRESOURCE PsConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    D3D10DDI_HRESOURCE GsConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

    /* Sampler bindings */
    D3D10DDI_HSAMPLER VsSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    D3D10DDI_HSAMPLER PsSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    D3D10DDI_HSAMPLER GsSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];

    /* Shader Resource View bindings */
    D3D10DDI_HSHADERRESOURCEVIEW VsShaderResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    D3D10DDI_HSHADERRESOURCEVIEW PsShaderResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    D3D10DDI_HSHADERRESOURCEVIEW GsShaderResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];

    /* Stream Output bindings */
    D3D10DDI_HRESOURCE SoBuffers[D3D11_SO_BUFFER_SLOT_COUNT];
    UINT SoOffsets[D3D11_SO_BUFFER_SLOT_COUNT];

    /* Predication */
    D3D10DDI_HQUERY PredicateQuery;
    BOOL PredicateValue;

    /* Text Filter */
    UINT TextFilterWidth;
    UINT TextFilterHeight;

    /* Draw telemetry */
    UINT64 DrawCount;
    UINT64 TotalVertexCount;
    UINT64 TotalIndexCount;
} BC250_UMD_PIPELINE_STATE;

/* TRUE somente quando o probe foi armado no registro. Lido uma vez e cacheado. */
BOOL Bc250UmdCapsProbeEnabled(void);
BOOL Bc250UmdNode1TripodAuthorized(void);

typedef struct _BC250_UMD_ADAPTER {
    D3D10DDI_HRTADAPTER hRTAdapter;
    CONST D3DDDI_ADAPTERCALLBACKS* pCallbacks;
    UINT Interface;
    UINT Version;
    BOOL IsActive;
    BC250_UMD_PRIVATE PrivateBlob;
    BOOL PrivateBlobValid;
} BC250_UMD_ADAPTER;

typedef struct _BC250_UMD_DEVICE {
    D3D10DDI_HRTDEVICE hRTDevice;
    D3D10DDI_HRTCORELAYER hRTCoreLayer;
    BC250_UMD_ADAPTER* pAdapter;
    UINT Interface;
    UINT Version;
    UINT Flags;
    CONST D3DDDI_DEVICECALLBACKS* pKTCallbacks;
    CONST D3D10DDI_CORELAYER_DEVICECALLBACKS* pUMCallbacks;
    CONST DXGI_DDI_BASE_CALLBACKS* pDXGICallbacks;
    BC250_WINSYS_CONTEXT Winsys;
    BOOL WinsysInitialized;
    /*
     * A composicao e' executada PELO KMD via escape desde 13/09. A UMD nao
     * cria contexto, paging queue, command buffer nem fence para o blt --
     * submissao de contexto no no de paginacao produziu TDR em 5 de 5
     * tentativas na corrida 08:12. Resta o contador de sequencia.
     */
    uint32_t BltSequence;
    BOOL BltPending;
    BC250_UMD_COMMAND_BUFFER Node0CmdBuffer;
    BC250_UMD_COMMAND_BUFFER Node1CmdBuffer;
    BC250_UMD_PIPELINE_STATE PipelineState;
    UINT64 RejectedDrawCount;
    /* Draws atendidos por SUBSTITUICAO (blt 1:1 no SDMA), nunca por
     * rasterizacao. Contagem separada de proposito: misturar com DrawCount
     * tornaria a prova de pixel infalsificavel. Ver C1 em caps_probe.h. */
    UINT64 SubstitutedDrawCount;
    /* Draws atendidos por emissao real PM4 no Node 1 (Parede B).
     * Contagem separada de SubstitutedDrawCount e RejectedDrawCount. */
    UINT64 NativePm4DrawCount;
    BOOL NativeNode1Ready;
    BOOL DeviceActive;
    BOOL TeardownActive;
    /* DX.82: Sincronizacao de ciclo de vida do device contra criacao concorrente de recursos */
    SRWLOCK DeviceLock;

    /* Vulkan Graphics Translation Backend */
    BC250_UMD_VK_BRIDGE VkBridge;
    BOOL VkBridgeInitialized;
    BC250_UMD_PIPELINE_CACHE PipelineCache;
    BOOL PipelineCacheInitialized;
    BOOL InRenderPass;
    /* DX.49: TRUE enquanto o draw corrente grava VB/IB/CB no anel do bridge
     * (lote); FALSE = caminho direto, que forca flush ao fim do draw. */
    BOOL DrawUsesRing;
    VkRenderPass ActiveRenderPass;
    VkFramebuffer ActiveFramebuffer;
    UINT64 NativeVkDrawCount;
    /* DX.54: Presents do device, para amostrar a inspecao do buffer linear. */
    UINT32 PresentInspectCount;
    UINT32 DrawReadbackCount; /* DX.25: leituras do alvo logo apos o draw (teto por device) */
    /* Motivo exato da ultima tentativa Vulkan. Aponta somente para literais
     * estaticos; Bc250UmdRejectUnbackedDraw o publica em vez do diagnostico
     * historico e falso de "PM4 ausente". */
    const char* LastVkDrawFailureReason;
} BC250_UMD_DEVICE;

/* Device foundation lifecycle helpers */
HRESULT Bc250UmdDeviceInit(
    _Out_ BC250_UMD_DEVICE* pDevice,
    _In_ BC250_UMD_ADAPTER* pAdapter,
    _In_ CONST D3D10DDIARG_CREATEDEVICE* pCreateData);

VOID Bc250UmdDeviceTeardown(
    _Inout_ BC250_UMD_DEVICE* pDevice);

/* D3D10.1 Device DDI entry points */
VOID APIENTRY Bc250Umd10DestroyDevice(
    D3D10DDI_HDEVICE hDevice);

VOID APIENTRY Bc250Umd10RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    _In_ struct D3D10_1DDI_DEVICEFUNCS* pDeviceFuncs);

VOID APIENTRY Bc250Umd10Flush(
    D3D10DDI_HDEVICE hDevice);

/* D3D10 Shader DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateShaderSize(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

VOID APIENTRY Bc250Umd10CreateVertexShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

VOID APIENTRY Bc250Umd10CreatePixelShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

VOID APIENTRY Bc250Umd10CreateGeometryShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

SIZE_T APIENTRY Bc250Umd10CalcPrivateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreate,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

VOID APIENTRY Bc250Umd10CreateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreate,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures);

VOID APIENTRY Bc250Umd10DestroyShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader);

/* D3D10 Resource DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateResourceSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate);

SIZE_T APIENTRY Bc250Umd10CalcPrivateOpenedResourceSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_OPENRESOURCE* pOpen);

VOID APIENTRY Bc250Umd10CreateResource(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRTRESOURCE hRTResource);

VOID APIENTRY Bc250Umd10OpenResource(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_OPENRESOURCE* pOpen,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRTRESOURCE hRTResource);

VOID APIENTRY Bc250Umd10DestroyResource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource);

VOID APIENTRY Bc250Umd10ResourceMap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource,
    D3D10_DDI_MAP MapType,
    UINT MapFlags,
    _Out_ D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource);

VOID APIENTRY Bc250Umd10ResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource);

BOOL APIENTRY Bc250Umd10ResourceIsStagingBusy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource);

VOID APIENTRY Bc250Umd10CheckFormatSupport(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    _Out_ UINT* pFormatCaps);

VOID APIENTRY Bc250Umd10CheckMultisampleQualityLevels(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    UINT SampleCount,
    _Out_ UINT* pNumQualityLevels);

/* D3D10 Render Target View DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateRenderTargetViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate);

VOID APIENTRY Bc250Umd10CreateRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    D3D10DDI_HRTRENDERTARGETVIEW hRTRenderTargetView);

VOID APIENTRY Bc250Umd10DestroyRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView);

/* D3D10 Depth Stencil View DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateDepthStencilViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate);

VOID APIENTRY Bc250Umd10CreateDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView);

VOID APIENTRY Bc250Umd10DestroyDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView);

/* D3D10 Output Merger & Pipeline State DDIs */
VOID APIENTRY Bc250Umd10SetRenderTargets(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(NumViews) CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetView,
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT ClearSlots,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView);

VOID APIENTRY Bc250Umd10ClearRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    FLOAT pColorRGBA[4]);

VOID APIENTRY Bc250Umd10ClearDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    UINT ClearFlags,
    FLOAT Depth,
    UINT8 Stencil);

VOID APIENTRY Bc250Umd10SetViewports(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE - NumViewports) UINT ClearViewports,
    _In_reads_(NumViewports) CONST D3D10_DDI_VIEWPORT* pViewports);

VOID APIENTRY Bc250Umd10SetScissorRects(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE - NumRects) UINT ClearRects,
    _In_reads_(NumRects) CONST D3D10_DDI_RECT* pRects);

/* D3D10 Input Assembly & Pipeline Binding DDIs */
VOID APIENTRY Bc250Umd10VsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader);

VOID APIENTRY Bc250Umd10PsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader);

VOID APIENTRY Bc250Umd10GsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader);

VOID APIENTRY Bc250Umd10IaSetTopology(
    D3D10DDI_HDEVICE hDevice,
    D3D10_DDI_PRIMITIVE_TOPOLOGY Topology);

VOID APIENTRY Bc250Umd10IaSetInputLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hInputLayout);

VOID APIENTRY Bc250Umd10IaSetVertexBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers,
    _In_reads_(NumBuffers) CONST UINT* pStrides,
    _In_reads_(NumBuffers) CONST UINT* pOffsets);

VOID APIENTRY Bc250Umd10IaSetIndexBuffer(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBuffer,
    DXGI_FORMAT Format,
    UINT Offset);

VOID APIENTRY Bc250Umd10VsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers);

VOID APIENTRY Bc250Umd10PsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers);

VOID APIENTRY Bc250Umd10GsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers);

/* D3D10 Resource Copy and Update DDIs */
VOID APIENTRY Bc250Umd10DefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemBuffer,
    UINT RowPitch,
    UINT DepthPitch);

VOID APIENTRY Bc250Umd10ResourceUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemBuffer,
    UINT RowPitch,
    UINT DepthPitch);

VOID APIENTRY Bc250Umd10ResourceCopy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    D3D10DDI_HRESOURCE hSrcResource);

VOID APIENTRY Bc250Umd10ResourceCopyRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pSrcBox);

VOID APIENTRY Bc250Umd10ResourceReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource);

VOID APIENTRY Bc250Umd10ShaderResourceViewReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    D3D10DDI_HRESOURCE hResource);

/* D3D10 Draw Execution DDIs */
VOID APIENTRY Bc250Umd10Draw(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCount,
    UINT StartVertexLocation);

VOID APIENTRY Bc250Umd10DrawIndexed(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation);

VOID APIENTRY Bc250Umd10DrawInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation);

VOID APIENTRY Bc250Umd10DrawIndexedInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation);

VOID APIENTRY Bc250Umd10DrawAuto(
    D3D10DDI_HDEVICE hDevice);

/* D3D10 State Object DDIs (Blend, DepthStencil, Rasterizer, Sampler) */
SIZE_T APIENTRY Bc250Umd10_1CalcPrivateBlendStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1_DDI_BLEND_DESC* pBlendDesc);

VOID APIENTRY Bc250Umd10_1CreateBlendState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1_DDI_BLEND_DESC* pBlendDesc,
    D3D10DDI_HBLENDSTATE hBlendState,
    D3D10DDI_HRTBLENDSTATE hRTBlendState);

VOID APIENTRY Bc250Umd10DestroyBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState);

VOID APIENTRY Bc250Umd10SetBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState,
    CONST FLOAT pBlendFactor[4],
    UINT SampleMask);

SIZE_T APIENTRY Bc250Umd10CalcPrivateDepthStencilStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc);

VOID APIENTRY Bc250Umd10CreateDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState);

VOID APIENTRY Bc250Umd10DestroyDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState);

VOID APIENTRY Bc250Umd10SetDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    UINT StencilRef);

SIZE_T APIENTRY Bc250Umd10CalcPrivateRasterizerStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc);

VOID APIENTRY Bc250Umd10CreateRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState);

VOID APIENTRY Bc250Umd10DestroyRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState);

VOID APIENTRY Bc250Umd10SetRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState);

SIZE_T APIENTRY Bc250Umd10CalcPrivateSamplerSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_SAMPLER_DESC* pSamplerDesc);

VOID APIENTRY Bc250Umd10CreateSampler(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_SAMPLER_DESC* pSamplerDesc,
    D3D10DDI_HSAMPLER hSampler,
    D3D10DDI_HRTSAMPLER hRTSampler);

VOID APIENTRY Bc250Umd10DestroySampler(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSAMPLER hSampler);

VOID APIENTRY Bc250Umd10VsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers);

VOID APIENTRY Bc250Umd10PsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers);

VOID APIENTRY Bc250Umd10GsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers);

/* D3D10 Shader Resource View (SRV) DDIs */
SIZE_T APIENTRY Bc250Umd10_1CalcPrivateShaderResourceViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreate);

VOID APIENTRY Bc250Umd10_1CreateShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreate,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView);

VOID APIENTRY Bc250Umd10DestroyShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView);

VOID APIENTRY Bc250Umd10VsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews);

VOID APIENTRY Bc250Umd10PsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews);

VOID APIENTRY Bc250Umd10GsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews);

/* D3D10 Element Layout (Input Layout) DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateElementLayoutSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEELEMENTLAYOUT* pCreate);

VOID APIENTRY Bc250Umd10CreateElementLayout(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEELEMENTLAYOUT* pCreate,
    D3D10DDI_HELEMENTLAYOUT hElementLayout,
    D3D10DDI_HRTELEMENTLAYOUT hRTElementLayout);

VOID APIENTRY Bc250Umd10DestroyElementLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hElementLayout);

/* D3D10 Query & Predication DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateQuerySize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEQUERY* pCreate);

VOID APIENTRY Bc250Umd10CreateQuery(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEQUERY* pCreate,
    D3D10DDI_HQUERY hQuery,
    D3D10DDI_HRTQUERY hRTQuery);

VOID APIENTRY Bc250Umd10DestroyQuery(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery);

VOID APIENTRY Bc250Umd10QueryBegin(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery);

VOID APIENTRY Bc250Umd10QueryEnd(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery);

VOID APIENTRY Bc250Umd10QueryGetData(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery,
    _Out_writes_bytes_all_opt_(DataSize) VOID* pData,
    UINT DataSize,
    UINT Flags);

VOID APIENTRY Bc250Umd10SetPredication(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery,
    BOOL PredicateValue);

/* D3D10 Stream Output, Mipmaps, Resolve, Counters, and Text Filter */
VOID APIENTRY Bc250Umd10SoSetTargets(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT ClearTargets,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phResources,
    _In_reads_(NumBuffers) CONST UINT* pOffsets);

VOID APIENTRY Bc250Umd10GenMips(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView);

VOID APIENTRY Bc250Umd10ResourceResolveSubresource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    DXGI_FORMAT ResolveFormat);

VOID APIENTRY Bc250Umd10CheckCounterInfo(
    D3D10DDI_HDEVICE hDevice,
    _Out_ D3D10DDI_COUNTER_INFO* pCounterInfo);

VOID APIENTRY Bc250Umd10CheckCounter(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_QUERY Query,
    _Out_ D3D10DDI_COUNTER_TYPE* pCounterType,
    _Out_ UINT* pActiveCounters,
    _Out_writes_to_opt_(*pNameLength, *pNameLength) LPSTR pName,
    _Inout_opt_ UINT* pNameLength,
    _Out_writes_to_opt_(*pUnitsLength, *pUnitsLength) LPSTR pUnits,
    _Inout_opt_ UINT* pUnitsLength,
    _Out_writes_to_opt_(*pDescriptionLength, *pDescriptionLength) LPSTR pDescription,
    _Inout_opt_ UINT* pDescriptionLength);

VOID APIENTRY Bc250Umd10SetTextFilterSize(
    D3D10DDI_HDEVICE hDevice,
    UINT Width,
    UINT Height);

/* UMD Command Buffer Management Helpers */
HRESULT Bc250UmdCommandBufferInit(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd,
    UINT CapacityBytes);

VOID Bc250UmdCommandBufferTeardown(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd);

HRESULT Bc250UmdCommandBufferFlush(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd,
    UINT NodeOrdinal);

/* Offline-verifiable first Node 1 payload. It only builds PM4; physical
 * execution must still close the silicon tripod and archive evidence. */
HRESULT Bc250UmdBuildNode1ProofCommand(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    uint64_t DestinationGpuVa,
    _In_reads_(ValueCount) const uint32_t* Values,
    uint32_t ValueCount,
    uint64_t FenceGpuVa,
    uint32_t FenceValue);

HRESULT Bc250UmdStartNode1Submission(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    UINT CommandBufferBytes);

HRESULT Bc250UmdSubmitNode1Proof(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    uint64_t DestinationGpuVa,
    _In_reads_(ValueCount) const uint32_t* Values,
    uint32_t ValueCount,
    uint64_t FenceGpuVa,
    uint32_t FenceValue);

/* Full fail-closed proof transaction over runtime callbacks. It returns S_OK
 * only when poison was observed before submit, the exact GPU fence retired,
 * all five values match byte-for-byte, and teardown completed. */
HRESULT Bc250UmdRunNode1ProofTransaction(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Out_ BC250_UMD_NODE1_PROOF_RESULT* pResult);

/* Real PM4 3D draw submission on Node 1 (Parede B).
 * Builds CB_COLOR0, Scissor, IT_DRAW_INDEX_AUTO and flushes command buffer. */
BOOL Bc250UmdEmitPm4Draw(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    BOOL Indexed);

/* Device DDI table population helpers */
VOID Bc250UmdPopulateDeviceFuncs10_1(_Out_ D3D10_1DDI_DEVICEFUNCS* pFuncs);
VOID Bc250UmdPopulateDeviceFuncs11(_Out_ D3D11DDI_DEVICEFUNCS* pFuncs);
VOID Bc250UmdPopulateDxgiBaseFuncs(_Out_ DXGI1_6_1_DDI_BASE_FUNCTIONS* pFuncs);

HRESULT APIENTRY Bc250Umd10GetCaps(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10_2DDIARG_GETCAPS* pCapsData);

HRESULT APIENTRY Bc250Umd10GetSupportedVersions(
    D3D10DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions);

/* DXGI Base DDI entry points (DXGI 1.0 - 1.6) */
HRESULT APIENTRY Bc250DxgiPresent(DXGI_DDI_ARG_PRESENT* pPresentData);
HRESULT APIENTRY Bc250DxgiGetGammaCaps(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS* pGammaData);
HRESULT APIENTRY Bc250DxgiSetDisplayMode(DXGI_DDI_ARG_SETDISPLAYMODE* pDisplayModeData);
HRESULT APIENTRY Bc250DxgiSetResourcePriority(DXGI_DDI_ARG_SETRESOURCEPRIORITY* pPriorityData);
HRESULT APIENTRY Bc250DxgiQueryResourceResidency(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY* pResidencyData);
HRESULT APIENTRY Bc250DxgiRotateResourceIdentities(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES* pRotateData);
HRESULT APIENTRY Bc250DxgiBlt(DXGI_DDI_ARG_BLT* pBltData);
HRESULT APIENTRY Bc250DxgiResolveSharedResource(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pResolveData);
HRESULT APIENTRY Bc250DxgiBlt1(DXGI_DDI_ARG_BLT1* pBlt1Data);
HRESULT APIENTRY Bc250DxgiOfferResources1(DXGI_DDI_ARG_OFFERRESOURCES1* pOfferData);
HRESULT APIENTRY Bc250DxgiReclaimResources(DXGI_DDI_ARG_RECLAIMRESOURCES* pReclaimData);
HRESULT APIENTRY Bc250DxgiGetMultiplaneOverlayCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS* pCapsData);
HRESULT APIENTRY Bc250DxgiGetMultiplaneOverlayGroupCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS* pGroupCapsData);
HRESULT APIENTRY Bc250DxgiPresentMultiplaneOverlay(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMpoData);
HRESULT APIENTRY Bc250DxgiPresent1(DXGI1_6_1_DDI_ARG_PRESENT* pPresent1Data);
HRESULT APIENTRY Bc250DxgiCheckPresentDurationSupport(DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT* pDurationData);
HRESULT APIENTRY Bc250DxgiTrimResidencySet(DXGI_DDI_ARG_TRIMRESIDENCYSET* pTrimData);
HRESULT APIENTRY Bc250DxgiCheckMultiplaneOverlayColorSpaceSupport(DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT* pColorSpaceData);
HRESULT APIENTRY Bc250DxgiPresentMultiplaneOverlay1(DXGI1_6_1_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMpo1Data);
HRESULT APIENTRY Bc250DxgiReclaimResources1(DXGI_DDI_ARG_RECLAIMRESOURCES1* pReclaimData);

/* D3D10 / D3D10_2 Adapter Caps */
HRESULT APIENTRY Bc250Umd10GetCaps(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10_2DDIARG_GETCAPS* pCapsData);

/* D3D10 Adapter DDI entry points */
SIZE_T APIENTRY Bc250Umd10CalcPrivateDeviceSize(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE* pCalcData);

HRESULT APIENTRY Bc250Umd10CreateDevice(
    D3D10DDI_HADAPTER hAdapter,
    _In_ D3D10DDIARG_CREATEDEVICE* pCreateData);

HRESULT APIENTRY Bc250Umd10CloseAdapter(
    D3D10DDI_HADAPTER hAdapter);

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
