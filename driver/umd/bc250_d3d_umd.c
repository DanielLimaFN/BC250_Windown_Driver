#include "bc250_d3d_umd.h"
#include "bc250_umd_caps_probe.h"
#include "bc250_vkd3d_shader_adapter.h"
#include "../core/bc250_umd_private_contract.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_escape_contract.h"
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
    va_list args;
    int messageLength;

    va_start(args, Format);
    messageLength = _vsnprintf_s(message, sizeof(message), _TRUNCATE, Format, args);
    va_end(args);
    if (messageLength < 0) {
        return;
    }

    /*
     * A formatacao e a escrita ficam em Bc250WinsysTraceRaw, FONTE UNICA
     * compartilhada com BC250_WINSYS_LOG. Antes da corrida 13/09 05:50 havia
     * duas instrumentacoes separadas: esta escrevia em arquivo, a da winsys
     * so' em stderr -- que o dwm.exe nao tem. O resultado foi uma corrida cujo
     * ponto de bloqueio era invisivel por construcao. Um arquivo por processo
     * evita que dxgiadaptercache.exe (SYSTEM) crie o arquivo com uma ACL que
     * depois impeca dwm.exe (DWM-*) de anexar.
     */
    Bc250WinsysTraceRaw(message);
}

/* Tokens de command buffer sao unicos por processo e nunca usam a classe
 * PRIMARY. O valor anterior era zero e Bc250WinsysAllocate o rejeitava por
 * contrato, portanto nenhum command buffer podia ser criado de verdade. */
static volatile LONG g_Bc250UmdCommandTokenSequence = 0;
static volatile LONG g_Bc250UmdProofTokenSequence = 0;
static volatile LONG g_Bc250UmdProofTriggered = 0;

static uint64_t Bc250UmdNextCommandToken(void)
{
    ULONG sequence = (ULONG)InterlockedIncrement(
        &g_Bc250UmdCommandTokenSequence);
    if (sequence == 0u) {
        sequence = (ULONG)InterlockedIncrement(
            &g_Bc250UmdCommandTokenSequence);
    }
    return ((uint64_t)0x434D4400u << 32) |
        ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
        (uint64_t)(sequence & 0xFFFFu);
}

static uint64_t Bc250UmdNextProofToken(void)
{
    ULONG sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdProofTokenSequence);
    if (sequence == 0u) {
        sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdProofTokenSequence);
    }
    return ((uint64_t)0x50524600u << 32) |
        ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
        (uint64_t)(sequence & 0xFFFFu);
}

static volatile LONG g_Bc250UmdResourceTokenSequence = 0;

static uint64_t Bc250UmdNextResourceToken(BOOL isPrimary, BOOL isStaging)
{
    ULONG sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdResourceTokenSequence);
    if (sequence == 0u) {
        sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdResourceTokenSequence);
    }
    if (isPrimary) {
        return ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) |
            (uint64_t)(sequence & 0xFFFFFFFFu);
    }
    if (isStaging) {
        return ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_STAGING << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) |
            ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
            (uint64_t)(sequence & 0xFFFFu);
    }
    return ((uint64_t)0x52455300u << 32) |
        ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
        (uint64_t)(sequence & 0xFFFFu);
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

SIZE_T APIENTRY Bc250Umd10CalcPrivateDeviceSize(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE* pCalcData)
{
    UNREFERENCED_PARAMETER(hAdapter);

    /*
     * P7_D3D11_CAPS_PROBE v3 (07/09) -- ponto cego fechado.
     *
     * MEDIDO na corrida 2026-09-07 08:05: a allowlist da v2 restaurou o
     * step-down (0x000B0011 recusado -> 0x000B000B recusado -> OpenAdapter10
     * com 0x000A0002 aceito com S_OK). E depois do S_OK o log ia direto para
     * "=== bc250umd.dll CARREGADA ===" outra vez, sem NENHUMA outra DDI.
     *
     * O problema e' que 2 das 3 entradas de D3D10DDI_ADAPTERFUNCS eram MUDAS:
     * so' CreateDevice registrava. Nao havia como distinguir "o runtime nao
     * chamou nada" de "o runtime chamou e nos nao registramos". Agora as tres
     * falam, e a proxima corrida diz qual das duas e'.
     */
    if (pCalcData != NULL) {
        Bc250UmdTrace("CalcPrivateDeviceSize: Interface=0x%08X, Version=0x%08X -> %u bytes",
            pCalcData->Interface, pCalcData->Version,
            (unsigned)sizeof(BC250_UMD_DEVICE));
    } else {
        Bc250UmdTrace("CalcPrivateDeviceSize: pCalcData=NULL -> %u bytes",
            (unsigned)sizeof(BC250_UMD_DEVICE));
    }
    return sizeof(BC250_UMD_DEVICE);
}

HRESULT Bc250UmdDeviceInit(
    _Out_ BC250_UMD_DEVICE* pDevice,
    _In_ BC250_UMD_ADAPTER* pAdapter,
    _In_ CONST D3D10DDIARG_CREATEDEVICE* pCreateData)
{
    BC250_WINSYS_STATUS wsStatus;

    if (pDevice == NULL || pAdapter == NULL || pCreateData == NULL) {
        return E_INVALIDARG;
    }
    if (!pAdapter->IsActive) {
        return E_INVALIDARG;
    }
    if (pCreateData->hRTDevice.handle == NULL || pCreateData->pKTCallbacks == NULL) {
        return E_INVALIDARG;
    }

    memset(pDevice, 0, sizeof(*pDevice));
    pDevice->hRTDevice = pCreateData->hRTDevice;
    pDevice->hRTCoreLayer = pCreateData->hRTCoreLayer;
    pDevice->pAdapter = pAdapter;
    pDevice->Interface = pCreateData->Interface;
    pDevice->Version = pCreateData->Version;
    pDevice->Flags = pCreateData->Flags;
    pDevice->pKTCallbacks = pCreateData->pKTCallbacks;
    pDevice->pUMCallbacks = pCreateData->pUMCallbacks;
    if (pCreateData->DXGIBaseDDI.pDXGIBaseCallbacks != NULL) {
        pDevice->pDXGICallbacks = pCreateData->DXGIBaseDDI.pDXGIBaseCallbacks;
    }

    /* Wire device callbacks to use the unified winsys */
    wsStatus = Bc250WinsysInitialize(
        &pDevice->Winsys,
        pCreateData->hRTDevice.handle,
        pCreateData->pKTCallbacks);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("Bc250UmdDeviceInit: Bc250WinsysInitialize falhou (status=%d %s)",
            (int)wsStatus, Bc250WinsysStatusName(wsStatus));
        return E_FAIL;
    }
    pDevice->WinsysInitialized = TRUE;
    pDevice->DeviceActive = TRUE;

    Bc250UmdTrace("Bc250UmdDeviceInit: device=%p inicializado com sucesso (Interface=0x%08X, Version=0x%08X, Winsys=OK)",
        (void*)pDevice, pDevice->Interface, pDevice->Version);
    return S_OK;
}

/* SDMA GPU Blt implementation */
#include "bc250_umd_blt.inl"

VOID Bc250UmdDeviceTeardown(
    _Inout_ BC250_UMD_DEVICE* pDevice)
{
    if (pDevice == NULL || !pDevice->DeviceActive) {
        return;
    }

    Bc250UmdTrace("Bc250UmdDeviceTeardown: device=%p iniciando teardown", (void*)pDevice);

    /*
     * Nao ha mais teardown de blt: a composicao passou a ser executada pelo
     * KMD via escape (13/09) e a UMD nao cria contexto, paging queue, command
     * buffer nem fence para isso.
     */

    if (pDevice->Node0CmdBuffer.Initialized) {
        Bc250UmdCommandBufferTeardown(pDevice, &pDevice->Node0CmdBuffer);
    }
    if (pDevice->Node1CmdBuffer.Initialized) {
        Bc250UmdCommandBufferTeardown(pDevice, &pDevice->Node1CmdBuffer);
    }

    if (pDevice->WinsysInitialized) {
        if (pDevice->Winsys.PagingQueueCreated) {
            (VOID)Bc250WinsysDestroyPagingQueue(&pDevice->Winsys);
        }
        if (pDevice->Winsys.ContextCreated) {
            (VOID)Bc250WinsysDestroyVirtualContext(&pDevice->Winsys);
        }
        /*
         * O contexto de apresentacao e' um objeto de kernel proprio: se nao for
         * destruido aqui, vaza a cada ciclo do laco de device-lost do dwm.exe --
         * que na corrida 17:18:35 fez 61 CreateDevice/DestroyDevice em ~3 min.
         */
        if (pDevice->Winsys.PresentContextCreated) {
            (VOID)Bc250WinsysDestroyPresentContext(&pDevice->Winsys);
        }
        pDevice->WinsysInitialized = FALSE;
    }

    pDevice->NativeNode1Ready = FALSE;
    pDevice->DeviceActive = FALSE;
    Bc250UmdTrace("Bc250UmdDeviceTeardown: device=%p concluido", (void*)pDevice);
}

VOID APIENTRY Bc250Umd10DestroyDevice(D3D10DDI_HDEVICE hDevice)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;

    Bc250UmdTrace("D3D10 DestroyDevice: pDrvPrivate=%p, device=%p",
        (void*)hDevice.pDrvPrivate, (void*)device);
    if (device != NULL) {
        Bc250UmdDeviceTeardown(device);
    }
}

VOID APIENTRY Bc250Umd10RelocateDeviceFuncs(
    D3D10DDI_HDEVICE hDevice,
    _In_ struct D3D10_1DDI_DEVICEFUNCS* pDeviceFuncs)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    Bc250UmdTrace("D3D10 RelocateDeviceFuncs: device=%p, pDeviceFuncs=%p",
        (void*)device, (void*)pDeviceFuncs);
}

VOID APIENTRY Bc250Umd10Flush(D3D10DDI_HDEVICE hDevice)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    Bc250UmdTrace("D3D10 Flush: device=%p", (void*)device);

    if (device->Node0CmdBuffer.Initialized && device->Node0CmdBuffer.CursorDwords > 0) {
        (VOID)Bc250UmdCommandBufferFlush(device, &device->Node0CmdBuffer, BC250_WINSYS_NODE_SDMA);
    }
    if (device->Node1CmdBuffer.Initialized && device->Node1CmdBuffer.CursorDwords > 0) {
        (VOID)Bc250UmdCommandBufferFlush(device, &device->Node1CmdBuffer, BC250_WINSYS_NODE_COMPUTE);
    }
}

/* =========================================================================
 * D3D10 Shader DDIs (Shader Model 4 / 5)
 * ========================================================================= */

SIZE_T APIENTRY Bc250Umd10CalcPrivateShaderSize(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_DXBC_CONTAINER container;
    BC250_DXBC_STATUS status;

    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pSignatures);

    if (pShaderCode == NULL) {
        Bc250UmdTrace("CalcPrivateShaderSize: pShaderCode=NULL -> 0");
        return 0;
    }

    /* Check whether this is a full DXBC container or direct bytecode chunk */
    if (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) {
        const BC250_DXBC_RAW_HEADER* rawHdr = (const BC250_DXBC_RAW_HEADER*)pShaderCode;
        status = Bc250DxbcParse(pShaderCode, rawHdr->TotalSize, &container);
        if (status != BC250_DXBC_OK) {
            Bc250UmdTrace("CalcPrivateShaderSize: DXBC container invalido (%s) -> 0",
                Bc250DxbcStatusName(status));
            return 0;
        }
        Bc250UmdTrace("CalcPrivateShaderSize: DXBC container OK, tipo=%s SM=%u.%u, %u tokens -> %u bytes",
            Bc250DxbcShaderTypeName(container.ShaderType),
            container.MajorVersion, container.MinorVersion,
            container.OpcodeTokenCount, (unsigned)sizeof(BC250_UMD_SHADER));
    } else {
        /* Direct shader token stream: [0] = version token, [1] = token count */
        UINT versionToken = pShaderCode[0];
        UINT typeField = (versionToken >> 16u) & 0xFFFFu;
        UINT major = (versionToken >> 4u) & 0xFu;
        UINT minor = versionToken & 0xFu;
        UINT tokenCount = pShaderCode[1];

        if (typeField > 5u || (major != 4u && major != 5u)) {
            Bc250UmdTrace("CalcPrivateShaderSize: token stream desconhecido (0x%08X) -> 0", versionToken);
            return 0;
        }
        Bc250UmdTrace("CalcPrivateShaderSize: direct stream tipo=%u SM=%u.%u, %u tokens -> %u bytes",
            typeField, major, minor, tokenCount, (unsigned)sizeof(BC250_UMD_SHADER));
    }

    return sizeof(BC250_UMD_SHADER);
}

/*
 * =========================================================================
 * C3 (12/09) -- tradutor DXBC -> SPIR-V, FECHADO POR DEFAULT.
 *
 * Resolucao unica por processo. Sem AS DUAS chaves de registro (flag em 1 e
 * caminho absoluto da DLL) nada e' carregado: Bc250Vkd3dShaderAdapterOpen nao e'
 * chamado e nenhuma DLL externa entra no processo. Isso importa porque a
 * bc250umd.dll carrega dentro do dwm.exe.
 *
 * Falhar aqui NUNCA pode derrubar a criacao do shader: o SPIR-V e' um adorno
 * default-off, e o driver segue exatamente como antes quando ele nao existe.
 * =========================================================================
 */
static BC250_VKD3D_SHADER_ADAPTER s_Bc250ShaderTranslatorAdapter;
static BC250_DXBC_SPIRV_TRANSLATOR s_Bc250ShaderTranslator;
static LONG s_Bc250ShaderTranslatorState = -1; /* -1 nao resolvido, 0 off, 1 on */

static BOOL Bc250UmdShaderTranslatorReadPath(_Out_writes_(Chars) wchar_t* Path, DWORD Chars)
{
    HKEY key = NULL;
    DWORD flag = 0;
    DWORD size = (DWORD)sizeof(flag);
    DWORD type = 0;
    BOOL armed = FALSE;

    Path[0] = L'\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    if (RegQueryValueExA(key, BC250_D3D11_SHADER_TRANSLATOR_REG_NAME, NULL, &type,
            (LPBYTE)&flag, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && flag == 1u) {
        char ansiPath[MAX_PATH];
        DWORD pathBytes = (DWORD)sizeof(ansiPath);
        DWORD pathType = 0;
        if (RegQueryValueExA(key, BC250_D3D11_SHADER_TRANSLATOR_PATH_REG_NAME, NULL,
                &pathType, (LPBYTE)ansiPath, &pathBytes) == ERROR_SUCCESS &&
            pathType == REG_SZ && pathBytes > 1u) {
            ansiPath[sizeof(ansiPath) - 1] = '\0';
            if (MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, Path, (int)Chars) > 0) {
                Path[Chars - 1u] = L'\0';
                armed = (Path[0] != L'\0');
            }
        }
    }
    RegCloseKey(key);
    return armed;
}

static CONST BC250_DXBC_SPIRV_TRANSLATOR* Bc250UmdShaderTranslator(void)
{
    LONG cached = InterlockedCompareExchange(&s_Bc250ShaderTranslatorState, -1, -1);
    wchar_t path[MAX_PATH];

    if (cached == 1) return &s_Bc250ShaderTranslator;
    if (cached == 0) return NULL;

    if (!Bc250UmdShaderTranslatorReadPath(path, (DWORD)(sizeof(path) / sizeof(path[0])))) {
        InterlockedExchange(&s_Bc250ShaderTranslatorState, 0);
        Bc250UmdTrace("SHADER_TRANSLATOR: desarmado (HKLM\\%s\\%s + %s)",
            BC250_D3D11_CAPS_PROBE_REG_KEY,
            BC250_D3D11_SHADER_TRANSLATOR_REG_NAME,
            BC250_D3D11_SHADER_TRANSLATOR_PATH_REG_NAME);
        return NULL;
    }
    if (!Bc250Vkd3dShaderAdapterOpen(path, &s_Bc250ShaderTranslatorAdapter)) {
        const wchar_t *fallbackPath = L"C:\\BC250_LIB\\libvkd3d-shader-1.dll";
        if (_wcsicmp(path, fallbackPath) != 0 &&
            Bc250Vkd3dShaderAdapterOpen(fallbackPath, &s_Bc250ShaderTranslatorAdapter)) {
            Bc250UmdTrace("SHADER_TRANSLATOR: carregado via fallback publico '%ls'", fallbackPath);
        }
    }
    if (!s_Bc250ShaderTranslatorAdapter.Module ||
        !Bc250Vkd3dShaderAdapterGetTranslator(&s_Bc250ShaderTranslatorAdapter,
                                              &s_Bc250ShaderTranslator)) {
        DWORD lastErr = GetLastError();
        Bc250Vkd3dShaderAdapterClose(&s_Bc250ShaderTranslatorAdapter);
        InterlockedExchange(&s_Bc250ShaderTranslatorState, 0);
        Bc250UmdTrace("SHADER_TRANSLATOR: ARMADO porem a DLL '%ls' nao abriu (err=%lu) -- fail-closed",
            path, lastErr);
        return NULL;
    }
    InterlockedExchange(&s_Bc250ShaderTranslatorState, 1);
    Bc250UmdTrace("SHADER_TRANSLATOR: ARMADO (%s %u.%u)",
        s_Bc250ShaderTranslatorAdapter.VersionText,
        s_Bc250ShaderTranslatorAdapter.VersionMajor,
        s_Bc250ShaderTranslatorAdapter.VersionMinor);
    return &s_Bc250ShaderTranslator;
}

/*
 * Traduz, se e somente se o tradutor estiver armado. O resultado e' registrado
 * com ownership no proprio shader; falha nao e' fatal e nao muda o desfecho da
 * criacao. Traduzir NAO significa poder desenhar.
 */
static VOID Bc250UmdAttachSpirvModule(
    BC250_UMD_SHADER* pShader,
    CONST UINT* pShaderCode,
    CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    CONST BC250_DXBC_SPIRV_TRANSLATOR* translator;
    BC250_DXBC_SPIRV_STATUS status;

    if (pShader == NULL || pShaderCode == NULL) return;
    translator = Bc250UmdShaderTranslator();
    if (translator == NULL) return;

    if (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) {
        const BC250_DXBC_RAW_HEADER* rawHdr = (const BC250_DXBC_RAW_HEADER*)pShaderCode;
        status = Bc250DxbcTranslateToSpirv(pShaderCode, rawHdr->TotalSize,
                                           translator, &pShader->Spirv);
        if (status != BC250_DXBC_SPIRV_OK) {
            Bc250UmdTrace("SHADER_TRANSLATOR: traducao DXBC falhou (status=%d) -- shader segue sem SPIR-V",
                (int)status);
            return;
        }
    } else {
        BC250_DXBC_STAGE_SIGNATURES stageSignatures;
        memset(&stageSignatures, 0, sizeof(stageSignatures));
        if (pSignatures != NULL) {
            stageSignatures.InputSignatures = (const BC250_DXBC_SIGNATURE_ENTRY*)pSignatures->pInputSignature;
            stageSignatures.NumInputSignatures = pSignatures->NumInputSignatureEntries;
            stageSignatures.OutputSignatures = (const BC250_DXBC_SIGNATURE_ENTRY*)pSignatures->pOutputSignature;
            stageSignatures.NumOutputSignatures = pSignatures->NumOutputSignatureEntries;
        }
        status = Bc250DxbcTranslateTokenStreamToSpirv(
            pShaderCode, &stageSignatures, translator, &pShader->Spirv);
        if (status != BC250_DXBC_SPIRV_OK) {
            Bc250UmdTrace("SHADER_TRANSLATOR: traducao TokenStream falhou (status=%d, tokens=%u) -- shader segue sem SPIR-V",
                (int)status, pShaderCode[1]);
            return;
        }
    }

    pShader->SpirvValid = TRUE;
    Bc250UmdTrace("SHADER_TRANSLATOR: %s -> SPIR-V %u bytes (estagio=%d). "
        "Traducao NAO e' pipeline: descriptors, render target, anel grafico e Draw seguem ausentes.",
        (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) ? "DXBC" : "TokenStream",
        pShader->Spirv.SizeBytes, (int)pShader->Spirv.Stage);
}

static BOOL Bc250UmdInitShaderInternal(
    BC250_UMD_SHADER* pShader,
    BC250_DXBC_SHADER_TYPE ExpectedType,
    CONST UINT* pShaderCode,
    D3D10DDI_HRTSHADER hRTShader,
    CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_DXBC_STATUS status;

    if (pShader == NULL || pShaderCode == NULL) {
        return FALSE;
    }

    memset(pShader, 0, sizeof(*pShader));
    pShader->hRTShader = hRTShader;
    pShader->Type = ExpectedType;

    if (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) {
        const BC250_DXBC_RAW_HEADER* rawHdr = (const BC250_DXBC_RAW_HEADER*)pShaderCode;
        status = Bc250DxbcParse(pShaderCode, rawHdr->TotalSize, &pShader->Container);
        if (status != BC250_DXBC_OK) {
            Bc250UmdTrace("Bc250UmdInitShaderInternal: parse falhou (%s)", Bc250DxbcStatusName(status));
            return FALSE;
        }
        pShader->MajorVersion = pShader->Container.MajorVersion;
        pShader->MinorVersion = pShader->Container.MinorVersion;
        pShader->TotalSizeBytes = pShader->Container.TotalSize;
        pShader->OpcodeTokenCount = pShader->Container.OpcodeTokenCount;
        pShader->pCodeTokens = pShader->Container.OpcodeTokens;
        pShader->IsParsed = TRUE;
    } else {
        UINT versionToken = pShaderCode[0];
        pShader->MajorVersion = (versionToken >> 4u) & 0xFu;
        pShader->MinorVersion = versionToken & 0xFu;
        if (pShaderCode[1] < 2u) return FALSE;
        pShader->OpcodeTokenCount = pShaderCode[1] - 2u;
        pShader->pCodeTokens = pShaderCode + 2u;
        pShader->TotalSizeBytes = pShaderCode[1] * (UINT)sizeof(UINT);
        pShader->IsParsed = TRUE;
    }

    /* C3: anexa o SPIR-V se -- e somente se -- o tradutor estiver armado. */
    Bc250UmdAttachSpirvModule(pShader, pShaderCode, pSignatures);

    return TRUE;
}

VOID APIENTRY Bc250Umd10CreateVertexShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SHADER* shader = (BC250_UMD_SHADER*)hShader.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (shader == NULL || pShaderCode == NULL) {
        Bc250UmdTrace("CreateVertexShader: argumento invalido (shader=%p, code=%p)",
            (void*)shader, (void*)pShaderCode);
        return;
    }

    if (!Bc250UmdInitShaderInternal(shader, BC250_DXBC_SHADER_VERTEX, pShaderCode, hRTShader, pSignatures)) {
        Bc250UmdTrace("CreateVertexShader: falha na inicializacao");
        return;
    }

    Bc250UmdTrace("CreateVertexShader: shader=%p SM=%u.%u tokens=%u hRT=%p",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle);
}

VOID APIENTRY Bc250Umd10CreatePixelShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SHADER* shader = (BC250_UMD_SHADER*)hShader.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (shader == NULL || pShaderCode == NULL) {
        Bc250UmdTrace("CreatePixelShader: argumento invalido (shader=%p, code=%p)",
            (void*)shader, (void*)pShaderCode);
        return;
    }

    if (!Bc250UmdInitShaderInternal(shader, BC250_DXBC_SHADER_PIXEL, pShaderCode, hRTShader, pSignatures)) {
        Bc250UmdTrace("CreatePixelShader: falha na inicializacao");
        return;
    }

    Bc250UmdTrace("CreatePixelShader: shader=%p SM=%u.%u tokens=%u hRT=%p",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle);
}

VOID APIENTRY Bc250Umd10CreateGeometryShader(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(pShaderCode[1]) CONST UINT* pShaderCode,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SHADER* shader = (BC250_UMD_SHADER*)hShader.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (shader == NULL || pShaderCode == NULL) {
        Bc250UmdTrace("CreateGeometryShader: argumento invalido (shader=%p, code=%p)",
            (void*)shader, (void*)pShaderCode);
        return;
    }

    if (!Bc250UmdInitShaderInternal(shader, BC250_DXBC_SHADER_GEOMETRY, pShaderCode, hRTShader, pSignatures)) {
        Bc250UmdTrace("CreateGeometryShader: falha na inicializacao");
        return;
    }

    Bc250UmdTrace("CreateGeometryShader: shader=%p SM=%u.%u tokens=%u hRT=%p",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle);
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreate,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCreate);
    UNREFERENCED_PARAMETER(pSignatures);
    return sizeof(BC250_UMD_SHADER);
}

VOID APIENTRY Bc250Umd10CreateGeometryShaderWithStreamOutput(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreate,
    D3D10DDI_HSHADER hShader,
    D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SHADER* shader = (BC250_UMD_SHADER*)hShader.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (shader == NULL || pCreate == NULL || pCreate->pShaderCode == NULL) {
        Bc250UmdTrace("CreateGeometryShaderWithSO: argumento invalido");
        return;
    }

    if (!Bc250UmdInitShaderInternal(shader, BC250_DXBC_SHADER_GEOMETRY, pCreate->pShaderCode, hRTShader, pSignatures)) {
        Bc250UmdTrace("CreateGeometryShaderWithSO: falha na inicializacao");
        return;
    }

    Bc250UmdTrace("CreateGeometryShaderWithSO: shader=%p SM=%u.%u tokens=%u hRT=%p",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle);
}

VOID APIENTRY Bc250Umd10DestroyShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SHADER* shader = (BC250_UMD_SHADER*)hShader.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    Bc250UmdTrace("DestroyShader: shader=%p", (void*)shader);
    if (shader != NULL) {
        /*
         * C3: o modulo SPIR-V tem ownership neste objeto. Liberar ANTES do
         * memset e' obrigatorio -- zerar a estrutura primeiro perderia o
         * ponteiro e vazaria o modulo dentro do processo hospedeiro, que no
         * caso do desktop e' o dwm.exe. Bc250DxbcReleaseSpirv zera o modulo e
         * e' idempotente.
         */
        if (shader->SpirvValid) {
            Bc250DxbcReleaseSpirv(&shader->Spirv);
            shader->SpirvValid = FALSE;
        }
        memset(shader, 0, sizeof(*shader));
    }
}

/* =========================================================================
 * D3D10 Resource DDIs
 * ========================================================================= */

/*
 * F2.1 (13/09) -- TRADUCAO DXGI -> D3DDDIFORMAT, NA UMD DE PROPOSITO.
 *
 * `DxgkDdiDescribeAllocation` devolve `D3DDDIFORMAT`, nao `DXGI_FORMAT`. Quem
 * conhece a semantica DXGI e' a UMD; o KMD apenas carrega o numero recebido pela
 * ABI de alocacao. Manter a traducao aqui evita que o kernel ganhe uma segunda
 * tabela de formatos, que divergiria com o tempo.
 *
 * Zero significa "nao sei traduzir" -- e a ABI recusa geometria com Format=0, de
 * modo que uma superficie de formato desconhecido nasce SEM geometria e e'
 * honestamente recusada em DescribeAllocation, em vez de descrita errado.
 *
 * Os valores de D3DDDIFMT vem de d3dukmdt.h. So' os formatos de scanout e de
 * superficie de composicao estao mapeados: e' o conjunto que o dwm.exe usa
 * (medido: Fmt=87 = DXGI_FORMAT_B8G8R8A8_UNORM) e o que o GOP herdado declara
 * (fmt=21 = D3DDDIFMT_A8R8G8B8).
 */
static UINT Bc250DxgiFormatToD3dddiFormat(DXGI_FORMAT Format)
{
    switch (Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return 21u; /* D3DDDIFMT_A8R8G8B8 */
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 22u; /* D3DDDIFMT_X8R8G8B8 */
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return 28u; /* D3DDDIFMT_A8B8G8R8 */
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return 35u; /* D3DDDIFMT_A2B10G10R10 */
    case DXGI_FORMAT_B5G6R5_UNORM:
        return 23u; /* D3DDDIFMT_R5G6B5 */
    default:
        return 0u;  /* desconhecido -- ver o bloco acima */
    }
}

static UINT Bc250DxgiFormatBytesPerPixel(DXGI_FORMAT Format)
{
    switch (Format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16u;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 12u;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return 8u;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return 4u;
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return 2u;
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 1u;
    default:
        return 4u;
    }
}

static UINT Bc250CalculateResourceByteSize(
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate,
    _Out_ UINT* pOutWidth,
    _Out_ UINT* pOutHeight,
    _Out_ UINT* pOutDepth,
    _Out_ UINT* pOutRowPitch,
    _Out_ UINT* pOutSlicePitch)
{
    UINT width = 1u;
    UINT height = 1u;
    UINT depth = 1u;
    UINT bpp = 4u;
    UINT rowPitch = 0u;
    UINT slicePitch = 0u;
    UINT totalSize = 0u;

    if (pCreate->pMipInfoList != NULL) {
        width = pCreate->pMipInfoList[0].PhysicalWidth ? pCreate->pMipInfoList[0].PhysicalWidth : pCreate->pMipInfoList[0].TexelWidth;
        height = pCreate->pMipInfoList[0].PhysicalHeight ? pCreate->pMipInfoList[0].PhysicalHeight : pCreate->pMipInfoList[0].TexelHeight;
        depth = pCreate->pMipInfoList[0].PhysicalDepth ? pCreate->pMipInfoList[0].PhysicalDepth : pCreate->pMipInfoList[0].TexelDepth;
    }
    if (width == 0u) width = 1u;
    if (height == 0u) height = 1u;
    if (depth == 0u) depth = 1u;

    bpp = Bc250DxgiFormatBytesPerPixel(pCreate->Format);

    if (pCreate->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
        totalSize = width;
        rowPitch = width;
        slicePitch = width;
    } else {
        /* AMD GFX10 256-byte linear row pitch alignment */
        rowPitch = ((width * bpp) + 255u) & ~255u;
        slicePitch = rowPitch * height;
        totalSize = slicePitch * depth;

        if (pCreate->ArraySize > 1u) {
            totalSize *= pCreate->ArraySize;
        }
    }

    /* Align total byte size to 4096 (page size) */
    totalSize = (totalSize + 4095u) & ~4095u;

    if (pOutWidth) *pOutWidth = width;
    if (pOutHeight) *pOutHeight = height;
    if (pOutDepth) *pOutDepth = depth;
    if (pOutRowPitch) *pOutRowPitch = rowPitch;
    if (pOutSlicePitch) *pOutSlicePitch = slicePitch;

    return totalSize;
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateResourceSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    if (pCreate == NULL) return 0;
    return sizeof(BC250_UMD_RESOURCE);
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateOpenedResourceSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_OPENRESOURCE* pOpen)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pOpen);
    return sizeof(BC250_UMD_RESOURCE);
}

VOID APIENTRY Bc250Umd10CreateResource(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERESOURCE* pCreate,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRTRESOURCE hRTResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    UINT width, height, depth, rowPitch, slicePitch, totalSize;
    uint64_t alignment = 4096u;
    uint64_t token;
    BOOL isPrimary = FALSE;
    BC250_WINSYS_STATUS wsStatus;

    if (res == NULL || pCreate == NULL) {
        Bc250UmdTrace("CreateResource: argumento invalido (res=%p, create=%p)",
            (void*)res, (void*)pCreate);
        return;
    }

    memset(res, 0, sizeof(*res));
    res->ResourceDimension = pCreate->ResourceDimension;
    res->Format = pCreate->Format;
    res->Usage = pCreate->Usage;
    res->BindFlags = pCreate->BindFlags;
    res->MapFlags = pCreate->MapFlags;
    res->MiscFlags = pCreate->MiscFlags;
    res->MipLevels = pCreate->MipLevels;
    res->ArraySize = pCreate->ArraySize;
    res->SampleCount = pCreate->SampleDesc.Count;
    res->hRTResource = hRTResource;

    totalSize = Bc250CalculateResourceByteSize(pCreate, &width, &height, &depth, &rowPitch, &slicePitch);
    res->Width = width;
    res->Height = height;
    res->Depth = depth;
    res->RowPitch = rowPitch;
    res->SlicePitch = slicePitch;
    res->TotalByteSize = totalSize;

    /*
     * F1.3 (13/09) -- O `pPrimaryDesc` NUNCA FOI LIDO, SO' TESTADO CONTRA NULL.
     *
     * Duas auditorias externas convergiram nisto, e a leitura confirma:
     * `DriverFlags` e `NO_SCANOUT` tinham ZERO ocorrencias em driver/umd. O
     * struct (dxgiddi.h:220) carrega `Flags` de ENTRADA -- em que
     * DXGI_DDI_PRIMARY_OPTIONAL (0x1) distingue back buffer de front/proxy -- e
     * `DriverFlags` de SAIDA, onde DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT (0x1)
     * e' o mecanismo DOCUMENTADO para o driver dizer ao runtime que apresenta por
     * COPIA, e nao por flip.
     *
     * Isso importa porque nos NAO PODEMOS fazer flip: o scanout e' o framebuffer
     * fixo herdado do UEFI GOP e escrita em registrador de DCN esta proibida no
     * escopo atual. Aceitar a cadeia de flip calados e' declarar um cap sem
     * backend -- exatamente o que a Lei 4 proibe.
     *
     * ESTA FAIXA E' SO' LEITURA E LOG: `DriverFlags` ainda NAO e' escrito, para
     * que a proxima corrida meca o que o runtime NOS ENTREGA antes de mudarmos
     * qualquer coisa. Escrever NO_SCANOUT e' o item 2.2 do plano.
     */
    if (pCreate->pPrimaryDesc != NULL) {
        /*
         * F2.2 -- OPT-OUT DO FLIP, SO' EM BACK BUFFER.
         *
         * `DXGI_DDI_PRIMARY_OPTIONAL` (dxgiddi.h:199) marca o back buffer; a
         * primaria de fato (front/proxy) chega SEM ele. Declarar NO_SCANOUT no
         * back buffer e' o mecanismo documentado para o runtime apresentar por
         * copia; declara-lo na front seria dizer que nem a primaria e' varrivel,
         * o que nao e' verdade -- o scanout do GOP existe e funciona.
         */
        if (Bc250UmdNoScanoutOptOutArmed() &&
            (pCreate->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_OPTIONAL) != 0u) {
            pCreate->pPrimaryDesc->DriverFlags |=
                DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT;
            Bc250UmdTrace("PRIMARY_DESC: NO_SCANOUT declarado no back buffer "
                "(DriverFlags=0x%08X) -- este driver apresenta por COPIA, nao por "
                "flip; o scanout e' o framebuffer fixo do GOP",
                pCreate->pPrimaryDesc->DriverFlags);
        }

        Bc250UmdTrace("PRIMARY_DESC recebido: Flags=0x%08X (OPTIONAL=%d NONPREROTATED=%d "
            "STEREO=%d INDIRECT=%d) VidPnSourceId=%u DriverFlags_in=0x%08X "
            "Mode=%ux%u Fmt=%u",
            pCreate->pPrimaryDesc->Flags,
            (pCreate->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_OPTIONAL) ? 1 : 0,
            (pCreate->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_NONPREROTATED) ? 1 : 0,
            (pCreate->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_STEREO) ? 1 : 0,
            (pCreate->pPrimaryDesc->Flags & DXGI_DDI_PRIMARY_INDIRECT) ? 1 : 0,
            (unsigned)pCreate->pPrimaryDesc->VidPnSourceId,
            pCreate->pPrimaryDesc->DriverFlags,
            pCreate->pPrimaryDesc->ModeDesc.Width,
            pCreate->pPrimaryDesc->ModeDesc.Height,
            (unsigned)pCreate->pPrimaryDesc->ModeDesc.Format);
    }

    if ((pCreate->BindFlags & D3D10_DDI_BIND_PRESENT) != 0u || pCreate->pPrimaryDesc != NULL) {
        isPrimary = TRUE;
        alignment = 65536u;
        totalSize = (UINT)(((uint64_t)totalSize + (alignment - 1u)) & ~(alignment - 1u));
        res->TotalByteSize = totalSize;
    }
    BOOL isStaging = (pCreate->Usage == D3D10_DDI_USAGE_STAGING ||
                      pCreate->Usage == D3D10_DDI_USAGE_DYNAMIC ||
                      (pCreate->MapFlags & (D3D10_DDI_MAP_READ | D3D10_DDI_MAP_WRITE)) != 0u);
    token = Bc250UmdNextResourceToken(isPrimary, isStaging);
    res->IsPrimary = isPrimary;
    res->Allocation.Token = token;

    /* If winsys is initialized, allocate physical/virtual VRAM */
    if (device != NULL && device->WinsysInitialized) {
        /*
         * 13/09 -- O VINCULO DE RECURSO PASSOU A EXISTIR. Ver o bloco de
         * proveniencia em bc250_wddm_winsys.h: ate a corrida 16:28:27 esta
         * chamada nao levava hRTResource, e por isso TODA superficie do dwm.exe
         * chegava ao KMD como "Non-resource allocation" e jamais como primaria.
         * O handle do runtime e' o mesmo que a DDI acabou de receber.
         */
        BC250_WINSYS_RESOURCE_BINDING binding;
        UINT d3dddiFormat = Bc250DxgiFormatToD3dddiFormat(res->Format);

        memset(&binding, 0, sizeof(binding));
        binding.RuntimeResource = (HANDLE)hRTResource.handle;
        binding.Primary = isPrimary;
        binding.VidPnSourceId = 0u;

        /*
         * F2.1/F2.5 -- a geometria viaja ate o KMD pela ABI de alocacao, e e' o
         * que permite responder DescribeAllocation e preencher
         * DXGK_ALLOCATIONINFO.Pitch. Fonte unica: os mesmos numeros ja
         * calculados por Bc250CalculateResourceByteSize.
         *
         * So' declaramos geometria para superficies 2D com formato que sabemos
         * traduzir. Buffer (Dim=1), formato desconhecido ou altura/pitch zerados
         * nascem SEM geometria -- e DescribeAllocation os recusa honestamente em
         * vez de inventar uma descricao.
         */
        if (d3dddiFormat != 0u && res->Width != 0u && res->Height != 0u &&
            res->RowPitch != 0u &&
            (uint64_t)res->RowPitch * (uint64_t)res->Height <= (uint64_t)totalSize) {
            binding.HasGeometry = TRUE;
            binding.Geometry.Width = res->Width;
            binding.Geometry.Height = res->Height;
            binding.Geometry.Pitch = res->RowPitch;
            binding.Geometry.Format = d3dddiFormat;
        } else {
            Bc250UmdTrace("CreateResource: SEM geometria declarada "
                "(%ux%u pitch=%u dxgiFmt=%u -> d3dddiFmt=%u size=%u). "
                "DescribeAllocation vai RECUSAR esta alocacao em vez de descreve-la.",
                res->Width, res->Height, res->RowPitch,
                (unsigned)res->Format, d3dddiFormat, totalSize);
        }
        wsStatus = Bc250WinsysAllocateForResource(&device->Winsys, token,
            totalSize, alignment, &binding, &res->Allocation);
        if (wsStatus == BC250_WINSYS_OK) {
            (void)Bc250WinsysMapAllocation(&device->Winsys, &res->Allocation, FALSE);
            (void)Bc250WinsysMakeResident(&device->Winsys, &res->Allocation);
            res->Created = TRUE;
        } else {
            Bc250UmdTrace("CreateResource: Bc250WinsysAllocate falhou (status=%d %s)",
                (int)wsStatus, Bc250WinsysStatusName(wsStatus));
        }
    } else {
        res->Created = TRUE;
    }

    Bc250UmdTrace("CreateResource: res=%p Dim=%u Fmt=%u Size=%u (%ux%u bpp=%u) GpuVa=0x%llX Primary=%d",
        (void*)res, res->ResourceDimension, res->Format, res->TotalByteSize,
        res->Width, res->Height, Bc250DxgiFormatBytesPerPixel(res->Format),
        (unsigned long long)res->Allocation.GpuVirtualAddress, res->IsPrimary);
}

VOID APIENTRY Bc250Umd10OpenResource(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_OPENRESOURCE* pOpen,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRTRESOURCE hRTResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (res == NULL || pOpen == NULL) {
        Bc250UmdTrace("OpenResource: argumento invalido");
        return;
    }

    memset(res, 0, sizeof(*res));
    res->hRTResource = hRTResource;
    res->hKMResource = pOpen->hKMResource;
    res->Created = TRUE;

    Bc250UmdTrace("OpenResource: res=%p hKMResource=%u NumAllocs=%u",
        (void*)res, (unsigned)pOpen->hKMResource.handle, pOpen->NumAllocations);
}

VOID APIENTRY Bc250Umd10DestroyResource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;

    Bc250UmdTrace("DestroyResource: res=%p", (void*)res);
    if (res != NULL) {
        if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
            (void)Bc250WinsysDeallocate(&device->Winsys, &res->Allocation);
        }
        if (res->pCpuShadow != NULL) {
            free(res->pCpuShadow);
            res->pCpuShadow = NULL;
        }
        memset(res, 0, sizeof(*res));
    }
}

VOID APIENTRY Bc250Umd10ResourceMap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource,
    D3D10_DDI_MAP MapType,
    UINT MapFlags,
    _Out_ D3D10DDI_MAPPED_SUBRESOURCE* pMappedSubResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(Subresource);
    UNREFERENCED_PARAMETER(MapType);
    UNREFERENCED_PARAMETER(MapFlags);

    if (pMappedSubResource == NULL) return;
    memset(pMappedSubResource, 0, sizeof(*pMappedSubResource));

    if (res == NULL) return;

    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    pMappedSubResource->pData = res->pCpuShadow;
    pMappedSubResource->RowPitch = res->RowPitch;
    pMappedSubResource->DepthPitch = res->SlicePitch;

    Bc250UmdTrace("ResourceMap: res=%p Sub=%u pData=%p Pitch=%u",
        (void*)res, Subresource, pMappedSubResource->pData, pMappedSubResource->RowPitch);
}

VOID APIENTRY Bc250Umd10ResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(Subresource);

    if (device != NULL && device->WinsysInitialized && res != NULL &&
        res->Allocation.Created && res->pCpuShadow != NULL && res->TotalByteSize > 0u) {
        (void)Bc250WinsysUploadAllocation(&device->Winsys, &res->Allocation,
            res->pCpuShadow, res->TotalByteSize);
    }

    Bc250UmdTrace("ResourceUnmap: res=%p Sub=%u", (void*)res, Subresource);
}

BOOL APIENTRY Bc250Umd10ResourceIsStagingBusy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hResource);
    return FALSE;
}

VOID APIENTRY Bc250Umd10CheckFormatSupport(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    _Out_ UINT* pFormatCaps)
{
    UNREFERENCED_PARAMETER(hDevice);

    if (pFormatCaps == NULL) return;
    *pFormatCaps = 0;

    switch (Format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE |
                       D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET |
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE;
        break;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
        *pFormatCaps = 0;
        break;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R16_UINT:
        *pFormatCaps = D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE;
        break;
    default:
        *pFormatCaps = 0;
        break;
    }

    Bc250UmdTrace("CheckFormatSupport: Format=%u -> Caps=0x%08X", (UINT)Format, *pFormatCaps);
}

VOID APIENTRY Bc250Umd10CheckMultisampleQualityLevels(
    D3D10DDI_HDEVICE hDevice,
    DXGI_FORMAT Format,
    UINT SampleCount,
    _Out_ UINT* pNumQualityLevels)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Format);

    if (pNumQualityLevels == NULL) return;

    /* 1 sample supports 1 quality level (level 0). Higher sample counts: 0 until MSAA hardware resolve is validated */
    if (SampleCount <= 1u) {
        *pNumQualityLevels = 1u;
    } else {
        *pNumQualityLevels = 0u;
    }

    Bc250UmdTrace("CheckMultisampleQualityLevels: Format=%u Samples=%u -> Levels=%u",
        (UINT)Format, SampleCount, *pNumQualityLevels);
}

/* D3D10 Render Target View DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateRenderTargetViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    if (pCreate == NULL) return 0;
    return sizeof(BC250_UMD_RTV);
}

VOID APIENTRY Bc250Umd10CreateRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATERENDERTARGETVIEW* pCreate,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    D3D10DDI_HRTRENDERTARGETVIEW hRTRenderTargetView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RTV* rtv = (BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate;
    BC250_UMD_RESOURCE* res;
    UNREFERENCED_PARAMETER(device);

    if (rtv == NULL || pCreate == NULL) {
        Bc250UmdTrace("CreateRenderTargetView: argumento invalido");
        return;
    }

    memset(rtv, 0, sizeof(*rtv));
    rtv->hResource = pCreate->hDrvResource;
    res = (BC250_UMD_RESOURCE*)pCreate->hDrvResource.pDrvPrivate;
    rtv->pResource = res;
    rtv->Format = (pCreate->Format != DXGI_FORMAT_UNKNOWN) ? pCreate->Format : (res ? res->Format : DXGI_FORMAT_UNKNOWN);
    rtv->ResourceDimension = pCreate->ResourceDimension;
    rtv->hRTRTV = hRTRenderTargetView;

    switch (pCreate->ResourceDimension) {
    case D3D10DDIRESOURCE_BUFFER:
        rtv->FirstArraySlice = pCreate->Buffer.FirstElement;
        rtv->ArraySize = pCreate->Buffer.NumElements;
        break;
    case D3D10DDIRESOURCE_TEXTURE1D:
        rtv->MipSlice = pCreate->Tex1D.MipSlice;
        rtv->FirstArraySlice = pCreate->Tex1D.FirstArraySlice;
        rtv->ArraySize = pCreate->Tex1D.ArraySize;
        break;
    case D3D10DDIRESOURCE_TEXTURE2D:
        rtv->MipSlice = pCreate->Tex2D.MipSlice;
        rtv->FirstArraySlice = pCreate->Tex2D.FirstArraySlice;
        rtv->ArraySize = pCreate->Tex2D.ArraySize;
        break;
    case D3D10DDIRESOURCE_TEXTURE3D:
        rtv->MipSlice = pCreate->Tex3D.MipSlice;
        rtv->FirstArraySlice = pCreate->Tex3D.FirstW;
        rtv->ArraySize = pCreate->Tex3D.WSize;
        break;
    case D3D10DDIRESOURCE_TEXTURECUBE:
        rtv->MipSlice = pCreate->TexCube.MipSlice;
        rtv->FirstArraySlice = pCreate->TexCube.FirstArraySlice;
        rtv->ArraySize = pCreate->TexCube.ArraySize;
        break;
    default:
        break;
    }

    rtv->Created = TRUE;
    Bc250UmdTrace("CreateRenderTargetView: rtv=%p res=%p Dim=%u Fmt=%u Mip=%u Slice=%u Count=%u",
        (void*)rtv, (void*)res, (UINT)rtv->ResourceDimension, (UINT)rtv->Format,
        rtv->MipSlice, rtv->FirstArraySlice, rtv->ArraySize);
}

VOID APIENTRY Bc250Umd10DestroyRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RTV* rtv = (BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate;

    if (rtv == NULL) return;

    if (device != NULL) {
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            if (device->PipelineState.BoundRTVs[i].pDrvPrivate == rtv) {
                device->PipelineState.BoundRTVs[i].pDrvPrivate = NULL;
            }
        }
    }

    rtv->Created = FALSE;
    Bc250UmdTrace("DestroyRenderTargetView: rtv=%p", (void*)rtv);
}

/* D3D10 Depth Stencil View DDIs */
SIZE_T APIENTRY Bc250Umd10CalcPrivateDepthStencilViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    if (pCreate == NULL) return 0;
    return sizeof(BC250_UMD_DSV);
}

VOID APIENTRY Bc250Umd10CreateDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILVIEW* pCreate,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    D3D10DDI_HRTDEPTHSTENCILVIEW hRTDepthStencilView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DSV* dsv = (BC250_UMD_DSV*)hDepthStencilView.pDrvPrivate;
    BC250_UMD_RESOURCE* res;
    UNREFERENCED_PARAMETER(device);

    if (dsv == NULL || pCreate == NULL) {
        Bc250UmdTrace("CreateDepthStencilView: argumento invalido");
        return;
    }

    memset(dsv, 0, sizeof(*dsv));
    dsv->hResource = pCreate->hDrvResource;
    res = (BC250_UMD_RESOURCE*)pCreate->hDrvResource.pDrvPrivate;
    dsv->pResource = res;
    dsv->Format = (pCreate->Format != DXGI_FORMAT_UNKNOWN) ? pCreate->Format : (res ? res->Format : DXGI_FORMAT_UNKNOWN);
    dsv->ResourceDimension = pCreate->ResourceDimension;
    dsv->hRTDSV = hRTDepthStencilView;

    switch (pCreate->ResourceDimension) {
    case D3D10DDIRESOURCE_TEXTURE1D:
        dsv->MipSlice = pCreate->Tex1D.MipSlice;
        dsv->FirstArraySlice = pCreate->Tex1D.FirstArraySlice;
        dsv->ArraySize = pCreate->Tex1D.ArraySize;
        break;
    case D3D10DDIRESOURCE_TEXTURE2D:
        dsv->MipSlice = pCreate->Tex2D.MipSlice;
        dsv->FirstArraySlice = pCreate->Tex2D.FirstArraySlice;
        dsv->ArraySize = pCreate->Tex2D.ArraySize;
        break;
    case D3D10DDIRESOURCE_TEXTURECUBE:
        dsv->MipSlice = pCreate->TexCube.MipSlice;
        dsv->FirstArraySlice = pCreate->TexCube.FirstArraySlice;
        dsv->ArraySize = pCreate->TexCube.ArraySize;
        break;
    default:
        break;
    }

    dsv->Created = TRUE;
    Bc250UmdTrace("CreateDepthStencilView: dsv=%p res=%p Dim=%u Fmt=%u Mip=%u Slice=%u Count=%u",
        (void*)dsv, (void*)res, (UINT)dsv->ResourceDimension, (UINT)dsv->Format,
        dsv->MipSlice, dsv->FirstArraySlice, dsv->ArraySize);
}

VOID APIENTRY Bc250Umd10DestroyDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DSV* dsv = (BC250_UMD_DSV*)hDepthStencilView.pDrvPrivate;

    if (dsv == NULL) return;

    if (device != NULL) {
        if (device->PipelineState.BoundDSV.pDrvPrivate == dsv) {
            device->PipelineState.BoundDSV.pDrvPrivate = NULL;
        }
    }

    dsv->Created = FALSE;
    Bc250UmdTrace("DestroyDepthStencilView: dsv=%p", (void*)dsv);
}

/* D3D10 Output Merger & Pipeline State DDIs */
VOID APIENTRY Bc250Umd10SetRenderTargets(
    D3D10DDI_HDEVICE hDevice,
    _In_reads_(NumViews) CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetView,
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _In_range_(0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT ClearSlots,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UINT count;
    UINT clearEnd;

    if (device == NULL) return;

    count = (NumViews <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) ? NumViews : D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    for (UINT i = 0; i < count; ++i) {
        device->PipelineState.BoundRTVs[i] = (phRenderTargetView != NULL) ? phRenderTargetView[i] : (D3D10DDI_HRENDERTARGETVIEW){NULL};
    }

    clearEnd = count + ClearSlots;
    if (clearEnd > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) {
        clearEnd = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    }
    for (UINT i = count; i < clearEnd; ++i) {
        device->PipelineState.BoundRTVs[i].pDrvPrivate = NULL;
    }

    device->PipelineState.NumBoundRTVs = count;
    device->PipelineState.BoundDSV = hDepthStencilView;

    Bc250UmdTrace("SetRenderTargets: NumViews=%u ClearSlots=%u DSV=%p",
        count, ClearSlots, hDepthStencilView.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10ClearRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    FLOAT pColorRGBA[4])
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RTV* rtv = (BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate;
    BC250_UMD_RESOURCE* res;
    UINT32 pixel = 0;
    BYTE a, r, g, b;

    UNREFERENCED_PARAMETER(device);

    if (rtv == NULL || pColorRGBA == NULL) return;
    res = rtv->pResource;
    if (res == NULL) return;

    /* Clamp 0.0 - 1.0 to 0 - 255 */
    r = (BYTE)((pColorRGBA[0] < 0.0f ? 0.0f : (pColorRGBA[0] > 1.0f ? 1.0f : pColorRGBA[0])) * 255.0f + 0.5f);
    g = (BYTE)((pColorRGBA[1] < 0.0f ? 0.0f : (pColorRGBA[1] > 1.0f ? 1.0f : pColorRGBA[1])) * 255.0f + 0.5f);
    b = (BYTE)((pColorRGBA[2] < 0.0f ? 0.0f : (pColorRGBA[2] > 1.0f ? 1.0f : pColorRGBA[2])) * 255.0f + 0.5f);
    a = (BYTE)((pColorRGBA[3] < 0.0f ? 0.0f : (pColorRGBA[3] > 1.0f ? 1.0f : pColorRGBA[3])) * 255.0f + 0.5f);

    switch (rtv->Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        pixel = ((UINT32)a << 24) | ((UINT32)r << 16) | ((UINT32)g << 8) | (UINT32)b;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    default:
        pixel = ((UINT32)a << 24) | ((UINT32)b << 16) | ((UINT32)g << 8) | (UINT32)r;
        break;
    }

    /* CPU Shadow Fill */
    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    if (res->pCpuShadow != NULL) {
        BYTE* dst = (BYTE*)res->pCpuShadow;
        UINT height = res->Height ? res->Height : 1u;
        UINT width = res->Width ? res->Width : 1u;
        UINT rowPitch = res->RowPitch ? res->RowPitch : (width * 4u);

        for (UINT y = 0; y < height; ++y) {
            UINT32* row = (UINT32*)(dst + (size_t)y * rowPitch);
            for (UINT x = 0; x < width; ++x) {
                row[x] = pixel;
            }
        }
    }

    Bc250UmdTrace("ClearRenderTargetView: rtv=%p res=%p color=(%.2f,%.2f,%.2f,%.2f) pixel=0x%08X",
        (void*)rtv, (void*)res, pColorRGBA[0], pColorRGBA[1], pColorRGBA[2], pColorRGBA[3], pixel);
}

VOID APIENTRY Bc250Umd10ClearDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    UINT ClearFlags,
    FLOAT Depth,
    UINT8 Stencil)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DSV* dsv = (BC250_UMD_DSV*)hDepthStencilView.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (dsv == NULL) return;

    Bc250UmdTrace("ClearDepthStencilView: dsv=%p res=%p Flags=0x%X Depth=%.3f Stencil=%u",
        (void*)dsv, (void*)dsv->pResource, ClearFlags, Depth, (UINT)Stencil);
}

VOID APIENTRY Bc250Umd10SetViewports(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE - NumViewports) UINT ClearViewports,
    _In_reads_(NumViewports) CONST D3D10_DDI_VIEWPORT* pViewports)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UINT count;
    UINT clearEnd;

    if (device == NULL) return;

    count = (NumViewports <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) ?
            NumViewports : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

    for (UINT i = 0; i < count; ++i) {
        if (pViewports != NULL) {
            device->PipelineState.Viewports[i] = pViewports[i];
        }
    }

    clearEnd = count + ClearViewports;
    if (clearEnd > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) {
        clearEnd = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    }
    for (UINT i = count; i < clearEnd; ++i) {
        memset(&device->PipelineState.Viewports[i], 0, sizeof(D3D10_DDI_VIEWPORT));
    }

    device->PipelineState.NumViewports = count;
    if (count > 0 && pViewports != NULL) {
        Bc250UmdTrace("SetViewports: NumViewports=%u [%.1f,%.1f..%.1f,%.1f]", count,
            pViewports[0].TopLeftX, pViewports[0].TopLeftY,
            pViewports[0].Width, pViewports[0].Height);
    } else {
        Bc250UmdTrace("SetViewports: NumViewports=%u ClearViewports=%u", count, ClearViewports);
    }
}

VOID APIENTRY Bc250Umd10SetScissorRects(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE - NumRects) UINT ClearRects,
    _In_reads_(NumRects) CONST D3D10_DDI_RECT* pRects)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UINT count;
    UINT clearEnd;

    if (device == NULL) return;

    count = (NumRects <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) ?
            NumRects : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

    for (UINT i = 0; i < count; ++i) {
        if (pRects != NULL) {
            device->PipelineState.ScissorRects[i] = pRects[i];
        }
    }

    clearEnd = count + ClearRects;
    if (clearEnd > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) {
        clearEnd = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    }
    for (UINT i = count; i < clearEnd; ++i) {
        memset(&device->PipelineState.ScissorRects[i], 0, sizeof(D3D10_DDI_RECT));
    }

    device->PipelineState.NumScissorRects = count;
    if (count > 0 && pRects != NULL) {
        Bc250UmdTrace("SetScissorRects: NumRects=%u [%d,%d..%d,%d]", count,
            pRects[0].left, pRects[0].top, pRects[0].right, pRects[0].bottom);
    } else {
        Bc250UmdTrace("SetScissorRects: NumRects=%u ClearRects=%u", count, ClearRects);
    }
}

/* D3D10 Input Assembly & Pipeline Binding DDIs */
VOID APIENTRY Bc250Umd10VsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.BoundVS = hShader;
    Bc250UmdTrace("VsSetShader: shader=%p", hShader.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10PsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.BoundPS = hShader;
    Bc250UmdTrace("PsSetShader: shader=%p", hShader.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10GsSetShader(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADER hShader)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.BoundGS = hShader;
    Bc250UmdTrace("GsSetShader: shader=%p", hShader.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10IaSetTopology(
    D3D10DDI_HDEVICE hDevice,
    D3D10_DDI_PRIMITIVE_TOPOLOGY Topology)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.Topology = Topology;
    Bc250UmdTrace("IaSetTopology: Topology=%u", (UINT)Topology);
}

VOID APIENTRY Bc250Umd10IaSetInputLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hInputLayout)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.InputLayout = hInputLayout;
    Bc250UmdTrace("IaSetInputLayout: layout=%p", hInputLayout.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10IaSetVertexBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers,
    _In_reads_(NumBuffers) CONST UINT* pStrides,
    _In_reads_(NumBuffers) CONST UINT* pOffsets)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
        device->PipelineState.VertexBuffers[StartSlot + i] = phBuffers ? phBuffers[i] : (D3D10DDI_HRESOURCE){NULL};
        device->PipelineState.VertexStrides[StartSlot + i] = pStrides ? pStrides[i] : 0;
        device->PipelineState.VertexOffsets[StartSlot + i] = pOffsets ? pOffsets[i] : 0;
    }
    Bc250UmdTrace("IaSetVertexBuffers: Start=%u Count=%u", StartSlot, NumBuffers);
}

VOID APIENTRY Bc250Umd10IaSetIndexBuffer(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hBuffer,
    DXGI_FORMAT Format,
    UINT Offset)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.IndexBuffer = hBuffer;
    device->PipelineState.IndexFormat = Format;
    device->PipelineState.IndexOffset = Offset;
    Bc250UmdTrace("IaSetIndexBuffer: Buffer=%p Format=%u Offset=%u", hBuffer.pDrvPrivate, (UINT)Format, Offset);
}

VOID APIENTRY Bc250Umd10VsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        device->PipelineState.VsConstantBuffers[StartSlot + i] = phBuffers ? phBuffers[i] : (D3D10DDI_HRESOURCE){NULL};
    }
    Bc250UmdTrace("VsSetConstantBuffers: Start=%u Count=%u", StartSlot, NumBuffers);
}

VOID APIENTRY Bc250Umd10PsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        device->PipelineState.PsConstantBuffers[StartSlot + i] = phBuffers ? phBuffers[i] : (D3D10DDI_HRESOURCE){NULL};
    }
    Bc250UmdTrace("PsSetConstantBuffers: Start=%u Count=%u", StartSlot, NumBuffers);
}

VOID APIENTRY Bc250Umd10GsSetConstantBuffers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phBuffers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        device->PipelineState.GsConstantBuffers[StartSlot + i] = phBuffers ? phBuffers[i] : (D3D10DDI_HRESOURCE){NULL};
    }
    Bc250UmdTrace("GsSetConstantBuffers: Start=%u Count=%u", StartSlot, NumBuffers);
}

/* D3D10 Resource Copy and Update DDIs */
VOID APIENTRY Bc250Umd10DefaultConstantBufferUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemBuffer,
    UINT RowPitch,
    UINT DepthPitch)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hDstResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(DstSubresource);
    UNREFERENCED_PARAMETER(DepthPitch);

    if (res == NULL || pSysMemBuffer == NULL) return;

    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    if (res->pCpuShadow != NULL) {
        UINT copyBytes = RowPitch ? RowPitch : res->TotalByteSize;
        if (pDstBox != NULL) {
            UINT boxBytes = (pDstBox->right > pDstBox->left) ? (pDstBox->right - pDstBox->left) : 0;
            if (boxBytes > 0 && boxBytes < copyBytes) {
                copyBytes = boxBytes;
            }
        }
        if (copyBytes > res->TotalByteSize) {
            copyBytes = res->TotalByteSize;
        }
        memcpy(res->pCpuShadow, pSysMemBuffer, copyBytes);
        if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
            (void)Bc250WinsysUploadAllocation(&device->Winsys, &res->Allocation,
                res->pCpuShadow, res->TotalByteSize);
        }
    }

    Bc250UmdTrace("DefaultConstantBufferUpdateSubresourceUP: res=%p bytes=%u",
        (void*)res, res->TotalByteSize);
}

VOID APIENTRY Bc250Umd10ResourceUpdateSubresourceUP(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pDstBox,
    _In_ CONST VOID* pSysMemBuffer,
    UINT RowPitch,
    UINT DepthPitch)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hDstResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(DstSubresource);
    UNREFERENCED_PARAMETER(DepthPitch);

    if (res == NULL || pSysMemBuffer == NULL) return;

    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    if (res->pCpuShadow != NULL) {
        if (pDstBox == NULL) {
            UINT copyBytes = res->TotalByteSize;
            memcpy(res->pCpuShadow, pSysMemBuffer, copyBytes);
        } else {
            UINT boxWidth = (pDstBox->right > pDstBox->left) ? (pDstBox->right - pDstBox->left) : 0;
            UINT boxHeight = (pDstBox->bottom > pDstBox->top) ? (pDstBox->bottom - pDstBox->top) : 1;
            UINT bpp = Bc250DxgiFormatBytesPerPixel(res->Format);
            if (bpp == 0 || res->ResourceDimension == D3D10DDIRESOURCE_BUFFER) bpp = 1;
            UINT lineBytes = boxWidth * bpp;
            BYTE* dst = (BYTE*)res->pCpuShadow + (size_t)pDstBox->top * res->RowPitch + (size_t)pDstBox->left * bpp;
            CONST BYTE* src = (CONST BYTE*)pSysMemBuffer;

            if (lineBytes == 0) lineBytes = RowPitch;
            for (UINT y = 0; y < boxHeight; ++y) {
                memcpy(dst + (size_t)y * res->RowPitch, src + (size_t)y * RowPitch, lineBytes);
            }
        }
        if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
            (void)Bc250WinsysUploadAllocation(&device->Winsys, &res->Allocation,
                res->pCpuShadow, res->TotalByteSize);
        }
    }

    Bc250UmdTrace("ResourceUpdateSubresourceUP: res=%p pitch=%u", (void*)res, RowPitch);
}

VOID APIENTRY Bc250Umd10ResourceCopy(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    D3D10DDI_HRESOURCE hSrcResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* dst = (BC250_UMD_RESOURCE*)hDstResource.pDrvPrivate;
    BC250_UMD_RESOURCE* src = (BC250_UMD_RESOURCE*)hSrcResource.pDrvPrivate;

    if (dst == NULL || src == NULL) return;

    /* Pipeline Blt GPU Real via SDMA quando recursos possuem alocacao em VRAM residente */
    if (device != NULL && dst->Allocation.Created && src->Allocation.Created &&
        dst->Allocation.Resident && src->Allocation.Resident &&
        dst->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
        src->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
        dst->Width == src->Width && dst->Height == src->Height &&
        dst->Format == src->Format) {
        DXGI_DDI_ARG_BLT bltArgs;
        memset(&bltArgs, 0, sizeof(bltArgs));
        bltArgs.hDevice = (DXGI_DDI_HDEVICE)device;
        bltArgs.hDstResource = (DXGI_DDI_HRESOURCE)dst;
        bltArgs.hSrcResource = (DXGI_DDI_HRESOURCE)src;
        bltArgs.DstLeft = 0;
        bltArgs.DstTop = 0;
        bltArgs.DstRight = dst->Width;
        bltArgs.DstBottom = dst->Height;
        bltArgs.Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY;
        (void)Bc250UmdBltGpu(device, dst, src, &bltArgs);
    }

    if (dst->pCpuShadow == NULL && dst->TotalByteSize > 0u) {
        dst->pCpuShadow = calloc(1, dst->TotalByteSize);
    }

    if (dst->pCpuShadow != NULL && src->pCpuShadow != NULL) {
        UINT copySize = (dst->TotalByteSize < src->TotalByteSize) ? dst->TotalByteSize : src->TotalByteSize;
        memcpy(dst->pCpuShadow, src->pCpuShadow, copySize);
        if (device != NULL && device->WinsysInitialized && dst->Allocation.Created) {
            (void)Bc250WinsysUploadAllocation(&device->Winsys, &dst->Allocation,
                dst->pCpuShadow, dst->TotalByteSize);
        }
    }

    Bc250UmdTrace("ResourceCopy: dst=%p src=%p", (void*)dst, (void*)src);
}

VOID APIENTRY Bc250Umd10ResourceCopyRegion(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    _In_opt_ CONST D3D10_DDI_BOX* pSrcBox)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* dst = (BC250_UMD_RESOURCE*)hDstResource.pDrvPrivate;
    BC250_UMD_RESOURCE* src = (BC250_UMD_RESOURCE*)hSrcResource.pDrvPrivate;
    UNREFERENCED_PARAMETER(DstSubresource);
    UNREFERENCED_PARAMETER(DstZ);
    UNREFERENCED_PARAMETER(SrcSubresource);

    if (dst == NULL || src == NULL) return;

    if (dst->pCpuShadow == NULL && dst->TotalByteSize > 0u) {
        dst->pCpuShadow = calloc(1, dst->TotalByteSize);
    }

    if (dst->pCpuShadow != NULL && src->pCpuShadow != NULL) {
        if (pSrcBox == NULL) {
            UINT copySize = (dst->TotalByteSize < src->TotalByteSize) ? dst->TotalByteSize : src->TotalByteSize;
            memcpy(dst->pCpuShadow, src->pCpuShadow, copySize);
        } else {
            UINT boxWidth = (pSrcBox->right > pSrcBox->left) ? (pSrcBox->right - pSrcBox->left) : 0;
            UINT boxHeight = (pSrcBox->bottom > pSrcBox->top) ? (pSrcBox->bottom - pSrcBox->top) : 1;
            UINT bpp = Bc250DxgiFormatBytesPerPixel(dst->Format);
            if (bpp == 0 || dst->ResourceDimension == D3D10DDIRESOURCE_BUFFER) bpp = 1;
            UINT lineBytes = boxWidth * bpp;
            BYTE* dstPtr = (BYTE*)dst->pCpuShadow + (size_t)DstY * dst->RowPitch + (size_t)DstX * bpp;
            CONST BYTE* srcPtr = (CONST BYTE*)src->pCpuShadow + (size_t)pSrcBox->top * src->RowPitch + (size_t)pSrcBox->left * bpp;

            for (UINT y = 0; y < boxHeight; ++y) {
                memcpy(dstPtr + (size_t)y * dst->RowPitch, srcPtr + (size_t)y * src->RowPitch, lineBytes);
            }
        }
        if (device != NULL && device->WinsysInitialized && dst->Allocation.Created) {
            (void)Bc250WinsysUploadAllocation(&device->Winsys, &dst->Allocation,
                dst->pCpuShadow, dst->TotalByteSize);
        }
    }

    Bc250UmdTrace("ResourceCopyRegion: dst=%p src=%p DstX=%u DstY=%u", (void*)dst, (void*)src, DstX, DstY);
}

VOID APIENTRY Bc250Umd10ResourceReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hResource);
}

VOID APIENTRY Bc250Umd10ShaderResourceViewReadAfterWriteHazard(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    D3D10DDI_HRESOURCE hResource)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hShaderResourceView);
    UNREFERENCED_PARAMETER(hResource);
}

/* D3D10 Draw Execution DDIs */
static VOID Bc250UmdRejectUnbackedDraw(
    _Inout_ BC250_UMD_DEVICE* Device,
    _In_z_ const char* Operation)
{
    if (Device == NULL) return;
    Device->RejectedDrawCount++;
    if (Bc250UmdDrawBltSubstitutionArmed()) {
        Bc250UmdTrace("%s: OMITIDO SOB REGIME EXPERIMENTAL DE SUBSTITUICAO -- "
            "draw sem backend de rasterizacao ignorado sem SetErrorCb "
            "(Rejected=%llu; NAO e rasterizacao 3D nativa)",
            Operation, Device->RejectedDrawCount);
        return;
    }
    Bc250UmdTrace("%s: RECUSADO -- compilador/shader-state/draw PM4 ausente "
        "(Rejected=%llu)", Operation, Device->RejectedDrawCount);
    if (Device->pUMCallbacks != NULL &&
        Device->pUMCallbacks->pfnSetErrorCb != NULL) {
        Device->pUMCallbacks->pfnSetErrorCb(Device->hRTCoreLayer, E_NOTIMPL);
    }
}

/*
 * C1 -- SUBSTITUICAO de Draw por blt 1:1 no SDMA. NAO e' rasterizacao.
 *
 * A versao anterior desta funcao compunha com memcpy de CPU entre os shadows e
 * declarava o draw atendido. Isso era fallback de software disfarcado de GPU
 * (Lei 4) e tornava infalsificavel qualquer pixel que aparecesse na tela. O
 * memcpy foi removido; o que sobra e' trabalho de GPU, fechado por default, e
 * a politica de elegibilidade vive em fonte unica testavel
 * (Bc250UmdDrawBltSubstitutionIsLawful).
 */
static BOOL Bc250UmdSubstituteDrawWithBlt(BC250_UMD_DEVICE* device)
{
    BC250_UMD_RTV* rtv;
    BC250_UMD_SRV* srv = NULL;
    BC250_UMD_RESOURCE* dst = NULL;
    BC250_UMD_RESOURCE* src = NULL;
    BC250_UMD_BLEND_STATE* blend;
    BOOL blendEnabled;
    DXGI_DDI_ARG_BLT bltArgs;
    HRESULT hrBlt;

    if (device == NULL) return FALSE;
    if (device->PipelineState.NumBoundRTVs == 0 || device->PipelineState.BoundRTVs[0].pDrvPrivate == NULL) {
        return FALSE;
    }

    rtv = (BC250_UMD_RTV*)device->PipelineState.BoundRTVs[0].pDrvPrivate;
    if (rtv == NULL || rtv->pResource == NULL) return FALSE;

    dst = rtv->pResource;
    if (dst->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D || dst->Width == 0 || dst->Height == 0) return FALSE;

    /*
     * Varre todos os slots de SRVs do Pixel Shader a procura da textura de origem.
     * O DWM pode vincular o SRV no slot 0 ou superior.
     */
    for (UINT s = 0; s < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++s) {
        if (device->PipelineState.PsShaderResources[s].pDrvPrivate != NULL) {
            BC250_UMD_SRV* cand = (BC250_UMD_SRV*)device->PipelineState.PsShaderResources[s].pDrvPrivate;
            if (cand != NULL && cand->pResource != NULL &&
                cand->pResource != dst &&
                cand->pResource->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
                cand->pResource->Width > 0 && cand->pResource->Height > 0) {
                srv = cand;
                src = cand->pResource;
                break;
            }
        }
    }

    if (src == NULL) {
        if (Bc250UmdDrawBltSubstitutionArmed()) {
            Bc250UmdTrace("DrawBltSubstitution REPROVOU: nenhum SRV de Texture2D valido nos slots PS (RTV dst=%p %ux%u)",
                (void*)dst, dst->Width, dst->Height);
        }
        return FALSE;
    }

    INT dstLeft = 0;
    INT dstTop = 0;
    INT dstRight = (INT)dst->Width;
    INT dstBottom = (INT)dst->Height;
    BOOL validGeometry = FALSE;
    BOOL viewportValid = FALSE;

    /*
     * Tres caminhos de geometria 1:1 licitos:
     * 1) ScissorRects declarado com dimensao exata do src (posicionamento de janela).
     * 2) Viewport declarada com dimensao exata do src.
     * 3) src com as mesmas dimensoes do dst (papel de parede / full-screen).
     */
    if (device->PipelineState.NumScissorRects > 0 &&
        device->PipelineState.ScissorRects[0].right > device->PipelineState.ScissorRects[0].left &&
        device->PipelineState.ScissorRects[0].bottom > device->PipelineState.ScissorRects[0].top) {
        const D3D10_DDI_RECT* sc = &device->PipelineState.ScissorRects[0];
        INT scW = sc->right - sc->left;
        INT scH = sc->bottom - sc->top;
        if (scW == (INT)src->Width && scH == (INT)src->Height &&
            sc->left >= 0 && sc->top >= 0 &&
            sc->right <= (INT)dst->Width && sc->bottom <= (INT)dst->Height) {
            dstLeft = sc->left;
            dstTop = sc->top;
            dstRight = sc->right;
            dstBottom = sc->bottom;
            viewportValid = TRUE;
        }
    }

    if (!viewportValid && device->PipelineState.NumViewports > 0) {
        const D3D10_DDI_VIEWPORT* vp = &device->PipelineState.Viewports[0];
        INT vpLeft = (INT)vp->TopLeftX;
        INT vpTop = (INT)vp->TopLeftY;
        INT vpRight = vpLeft + (INT)vp->Width;
        INT vpBottom = vpTop + (INT)vp->Height;

        if (vp->TopLeftX >= 0.0f && vp->TopLeftY >= 0.0f &&
            (UINT)vp->Width == src->Width && (UINT)vp->Height == src->Height &&
            vpRight <= (INT)dst->Width && vpBottom <= (INT)dst->Height) {
            dstLeft = vpLeft;
            dstTop = vpTop;
            dstRight = vpRight;
            dstBottom = vpBottom;
            viewportValid = TRUE;
        }
    }

    /*
     * 3) src cabe no dst (ex: src==dst em 2560x1080 full-screen, ou desktop 1920x1080
     *    em transicao de modo para monitor ultrawide 2560x1080): copia 1:1 posicionada
     *    em (0,0) ate os limites exatos do src.
     */
    if (!viewportValid && src->Width <= dst->Width && src->Height <= dst->Height) {
        dstLeft = 0;
        dstTop = 0;
        dstRight = (INT)src->Width;
        dstBottom = (INT)src->Height;
        viewportValid = TRUE;
    }

    if (dstLeft >= 0 && dstTop >= 0 &&
        dstRight <= (INT)dst->Width && dstBottom <= (INT)dst->Height &&
        dstRight > dstLeft && dstBottom > dstTop &&
        (UINT)(dstRight - dstLeft) == src->Width &&
        (UINT)(dstBottom - dstTop) == src->Height) {
        validGeometry = TRUE;
    }

    /*
     * "Blend" aqui significa: o output merger NAO e' escrita direta do pixel
     * de origem. Alpha-to-coverage, blend em qualquer RT ativo e write mask
     * parcial todos fazem o draw produzir resultado diferente de uma copia.
     */
    blendEnabled = FALSE;
    blend = (BC250_UMD_BLEND_STATE*)device->PipelineState.BoundBlendState.pDrvPrivate;
    if (blend != NULL && blend->Created) {
        UINT rt;
        UINT rtCount = blend->Desc.IndependentBlendEnable
            ? (UINT)D3D10_DDI_SIMULTANEOUS_RENDER_TARGET_COUNT
            : 1u;
        if (blend->Desc.AlphaToCoverageEnable) {
            blendEnabled = TRUE;
        }
        for (rt = 0; rt < rtCount; ++rt) {
            if (blend->Desc.RenderTarget[rt].BlendEnable) {
                blendEnabled = TRUE;
            }
        }
        if (blend->Desc.RenderTarget[0].RenderTargetWriteMask !=
            (UINT8)D3D10_DDI_COLOR_WRITE_ENABLE_ALL) {
            blendEnabled = TRUE;
        }
    }

    if (!Bc250UmdDrawBltSubstitutionIsLawful(
            Bc250UmdDrawBltSubstitutionArmed(),
            device->WinsysInitialized,
            (dst->Allocation.Created && src->Allocation.Created &&
             src->Allocation.Resident) ? TRUE : FALSE,
            dst->Allocation.Resident ? TRUE : FALSE,
            validGeometry,
            (dst->Format == src->Format) ? TRUE : FALSE,
            viewportValid,
            blendEnabled)) {
        if (Bc250UmdDrawBltSubstitutionArmed()) {
            Bc250UmdTrace("DrawBltSubstitution REPROVOU: armed=1 wsInit=%d srcAlloc=%d dstAlloc=%d srcRes=%d dstRes=%d geom=%d sameFmt=%d vpValid=%d blend=%d (src %ux%u fmt=%u, dst %ux%u fmt=%u at [%d,%d..%d,%d])",
                device->WinsysInitialized, src->Allocation.Created, dst->Allocation.Created,
                src->Allocation.Resident, dst->Allocation.Resident,
                validGeometry,
                (dst->Format == src->Format), viewportValid, blendEnabled,
                src->Width, src->Height, src->Format, dst->Width, dst->Height, dst->Format,
                dstLeft, dstTop, dstRight, dstBottom);
        }
        return FALSE;
    }

    memset(&bltArgs, 0, sizeof(bltArgs));
    bltArgs.hDevice = (DXGI_DDI_HDEVICE)device;
    bltArgs.hDstResource = (DXGI_DDI_HRESOURCE)dst;
    bltArgs.hSrcResource = (DXGI_DDI_HRESOURCE)src;
    bltArgs.DstLeft = dstLeft;
    bltArgs.DstTop = dstTop;
    bltArgs.DstRight = dstRight;
    bltArgs.DstBottom = dstBottom;
    bltArgs.Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY;
    hrBlt = Bc250UmdBltGpu(device, dst, src, &bltArgs);
    if (FAILED(hrBlt)) {
        if (Bc250UmdDrawBltSubstitutionArmed()) {
            Bc250UmdTrace("DrawBltSubstitution BltGpu falhou: hr=0x%08X src=%p dst=%p [%d,%d..%d,%d]",
                (unsigned)hrBlt, (void*)src, (void*)dst, dstLeft, dstTop, dstRight, dstBottom);
        }
        return FALSE;
    }

    device->SubstitutedDrawCount++;
    /*
     * SUBMETIDO, nao concluido: desde o anel de slots (13/09) a DDI nao espera
     * mais a fence do proprio blt. Afirmar conclusao aqui seria falso sucesso
     * -- a prova de execucao no silicio e' a do KMD (IB EXECUTADO, RPTR==WPTR,
     * fence sobre poison), nao esta linha.
     */
    Bc250UmdTrace("Draw SUBSTITUIDO por copia 1:1 SUBMETIDA ao SDMA -- NAO e' rasterizacao "
        "e NAO e' prova de conclusao "
        "(shader/VB/IB/topologia ignorados): src=%p dst=%p %ux%u at [%d,%d] (Substituidos=%llu)",
        (void*)src, (void*)dst, dst->Width, dst->Height, dstLeft, dstTop,
        device->SubstitutedDrawCount);
    return TRUE;
}

VOID APIENTRY Bc250Umd10Draw(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCount,
    UINT StartVertexLocation)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    UNREFERENCED_PARAMETER(VertexCount);
    UNREFERENCED_PARAMETER(StartVertexLocation);
    if (Bc250UmdSubstituteDrawWithBlt(device)) return;
    Bc250UmdRejectUnbackedDraw(device, "Draw");
}

VOID APIENTRY Bc250Umd10DrawIndexed(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    UNREFERENCED_PARAMETER(IndexCount);
    UNREFERENCED_PARAMETER(StartIndexLocation);
    UNREFERENCED_PARAMETER(BaseVertexLocation);
    if (Bc250UmdSubstituteDrawWithBlt(device)) return;
    Bc250UmdRejectUnbackedDraw(device, "DrawIndexed");
}

VOID APIENTRY Bc250Umd10DrawInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UNREFERENCED_PARAMETER(StartVertexLocation);
    UNREFERENCED_PARAMETER(StartInstanceLocation);

    if (device == NULL) return;
    UNREFERENCED_PARAMETER(VertexCountPerInstance);
    UNREFERENCED_PARAMETER(InstanceCount);
    if (Bc250UmdSubstituteDrawWithBlt(device)) return;
    Bc250UmdRejectUnbackedDraw(device, "DrawInstanced");
}

VOID APIENTRY Bc250Umd10DrawIndexedInstanced(
    D3D10DDI_HDEVICE hDevice,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UNREFERENCED_PARAMETER(StartIndexLocation);
    UNREFERENCED_PARAMETER(BaseVertexLocation);
    UNREFERENCED_PARAMETER(StartInstanceLocation);

    if (device == NULL) return;
    UNREFERENCED_PARAMETER(IndexCountPerInstance);
    UNREFERENCED_PARAMETER(InstanceCount);
    if (Bc250UmdSubstituteDrawWithBlt(device)) return;
    Bc250UmdRejectUnbackedDraw(device, "DrawIndexedInstanced");
}

VOID APIENTRY Bc250Umd10DrawAuto(
    D3D10DDI_HDEVICE hDevice)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    Bc250UmdRejectUnbackedDraw(device, "DrawAuto");
}

/* =========================================================================
 * UMD Internal Command Buffer Management Helpers
 * ========================================================================= */

HRESULT Bc250UmdCommandBufferInit(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd,
    UINT CapacityBytes)
{
    BC250_WINSYS_STATUS wsStatus;
    uint64_t allocationToken;
    if (pDevice == NULL || pCmd == NULL || CapacityBytes == 0) {
        return E_INVALIDARG;
    }
    memset(pCmd, 0, sizeof(*pCmd));
    if (!pDevice->WinsysInitialized) {
        return E_FAIL;
    }

    CapacityBytes = (CapacityBytes + 4095u) & ~4095u;
    allocationToken = Bc250UmdNextCommandToken();
    wsStatus = Bc250WinsysAllocate(&pDevice->Winsys, allocationToken,
        CapacityBytes, 4096, &pCmd->Allocation);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("CommandBufferInit: Allocate falhou (%s)", Bc250WinsysStatusName(wsStatus));
        return E_FAIL;
    }

    wsStatus = Bc250WinsysMapAllocation(&pDevice->Winsys, &pCmd->Allocation, TRUE);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("CommandBufferInit: MapAllocation falhou (%s)", Bc250WinsysStatusName(wsStatus));
        (VOID)Bc250WinsysDeallocate(&pDevice->Winsys, &pCmd->Allocation);
        return E_FAIL;
    }

    wsStatus = Bc250WinsysMakeResident(&pDevice->Winsys, &pCmd->Allocation);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("CommandBufferInit: MakeResident falhou (%s)",
            Bc250WinsysStatusName(wsStatus));
        (VOID)Bc250WinsysDeallocate(&pDevice->Winsys, &pCmd->Allocation);
        return E_FAIL;
    }

    pCmd->pCpuVa = (uint32_t*)calloc(1, CapacityBytes);
    if (pCmd->pCpuVa == NULL) {
        (VOID)Bc250WinsysDeallocate(&pDevice->Winsys, &pCmd->Allocation);
        return E_OUTOFMEMORY;
    }

    pCmd->CapacityDwords = CapacityBytes / sizeof(uint32_t);
    pCmd->CursorDwords = 0;
    pCmd->Initialized = TRUE;

    Bc250UmdTrace("CommandBufferInit: GpuVA=0x%llx Capacity=%u dwords",
        pCmd->Allocation.GpuVirtualAddress, pCmd->CapacityDwords);
    return S_OK;
}

VOID Bc250UmdCommandBufferTeardown(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd)
{
    if (pDevice == NULL || pCmd == NULL || !pCmd->Initialized) return;

    if (pCmd->pCpuVa != NULL) {
        free(pCmd->pCpuVa);
        pCmd->pCpuVa = NULL;
    }

    if (pDevice->WinsysInitialized && pCmd->Allocation.Created) {
        (VOID)Bc250WinsysDeallocate(&pDevice->Winsys, &pCmd->Allocation);
    }

    pCmd->CapacityDwords = 0;
    pCmd->CursorDwords = 0;
    pCmd->Initialized = FALSE;
    Bc250UmdTrace("CommandBufferTeardown: concluido");
}

HRESULT Bc250UmdCommandBufferFlush(
    _In_ BC250_UMD_DEVICE* pDevice,
    _Inout_ BC250_UMD_COMMAND_BUFFER* pCmd,
    UINT NodeOrdinal)
{
    BC250_WINSYS_STATUS wsStatus;
    UINT cmdLengthBytes;

    if (pDevice == NULL || pCmd == NULL || !pCmd->Initialized ||
        pCmd->pCpuVa == NULL || !pCmd->Allocation.Created ||
        !pCmd->Allocation.Resident ||
        NodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return E_INVALIDARG;
    }
    if (pCmd->CursorDwords == 0) {
        return S_OK;
    }

    cmdLengthBytes = pCmd->CursorDwords * (UINT)sizeof(uint32_t);

    wsStatus = Bc250WinsysWaitPaging(&pDevice->Winsys, &pCmd->Allocation);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("CommandBufferFlush: paging nao aposentou (Node=%u status=%s)",
            NodeOrdinal, Bc250WinsysStatusName(wsStatus));
        return E_FAIL;
    }

    wsStatus = Bc250WinsysUploadAllocation(
        &pDevice->Winsys,
        &pCmd->Allocation,
        pCmd->pCpuVa,
        cmdLengthBytes);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("CommandBufferFlush: upload falhou (Node=%u status=%s)",
            NodeOrdinal, Bc250WinsysStatusName(wsStatus));
        return E_FAIL;
    }

    wsStatus = Bc250WinsysSubmitGpuVaForNode(
        &pDevice->Winsys,
        NodeOrdinal,
        pCmd->Allocation.GpuVirtualAddress,
        cmdLengthBytes);

    Bc250UmdTrace("CommandBufferFlush: Node=%u bytes=%u status=%s",
        NodeOrdinal, cmdLengthBytes, Bc250WinsysStatusName(wsStatus));
    if (wsStatus != BC250_WINSYS_OK) {
        return E_FAIL;
    }
    pCmd->CursorDwords = 0;
    return S_OK;
}

HRESULT Bc250UmdBuildNode1ProofCommand(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    uint64_t DestinationGpuVa,
    _In_reads_(ValueCount) const uint32_t* Values,
    uint32_t ValueCount,
    uint64_t FenceGpuVa,
    uint32_t FenceValue)
{
    BC250_UMD_COMMAND_BUFFER* cmd;
    BC250_PM4_STATUS pm4Status;
    uint32_t initialCursor;
    uint32_t writeControl;

    if (pDevice == NULL || !pDevice->NativeNode1Ready ||
        !pDevice->Winsys.ContextCreated ||
        pDevice->Winsys.NodeOrdinal != BC250_WINSYS_NODE_COMPUTE ||
        DestinationGpuVa == 0u || FenceGpuVa == 0u || Values == NULL ||
        ValueCount == 0u) {
        return E_INVALIDARG;
    }

    cmd = &pDevice->Node1CmdBuffer;
    if (!cmd->Initialized || cmd->pCpuVa == NULL) {
        return E_FAIL;
    }

    initialCursor = cmd->CursorDwords;
    writeControl = BC250_PM4_WD_ENGINE_ME | BC250_PM4_WD_DST_MEM;
    pm4Status = Bc250Pm4EmitWriteData(cmd->pCpuVa,
        cmd->CapacityDwords, &cmd->CursorDwords, writeControl,
        DestinationGpuVa, Values, ValueCount);
    if (pm4Status != BC250_PM4_OK) {
        cmd->CursorDwords = initialCursor;
        return E_FAIL;
    }
    cmd->pCpuVa[initialCursor] |= BC250_PM4_SHADER_TYPE_COMPUTE;
    pm4Status = Bc250Pm4EmitReleaseMemEop32(cmd->pCpuVa,
        cmd->CapacityDwords, &cmd->CursorDwords, FenceGpuVa, FenceValue,
        BC250_PM4_RELEASE_MEM_INT_SEL_NONE);
    if (pm4Status != BC250_PM4_OK) {
        cmd->CursorDwords = initialCursor;
        return E_FAIL;
    }

    Bc250UmdTrace("NODE1_PROOF_BUILD: Dst=0x%llX Values=%u Fence=0x%llX/%08X Dwords=%u -- OFFLINE",
        (unsigned long long)DestinationGpuVa, ValueCount,
        (unsigned long long)FenceGpuVa, FenceValue,
        cmd->CursorDwords - initialCursor);
    return S_OK;
}

HRESULT Bc250UmdStartNode1Submission(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    UINT CommandBufferBytes)
{
    BC250_WINSYS_STATUS wsStatus;
    HRESULT hr;

    if (pDevice == NULL || !pDevice->DeviceActive ||
        !pDevice->WinsysInitialized || CommandBufferBytes == 0u) {
        return E_INVALIDARG;
    }
    if (pDevice->NativeNode1Ready || pDevice->Winsys.ContextCreated ||
        pDevice->Winsys.PagingQueueCreated ||
        pDevice->Node1CmdBuffer.Initialized) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }

    wsStatus = Bc250WinsysCreatePagingQueue(&pDevice->Winsys);
    if (wsStatus != BC250_WINSYS_OK) return E_FAIL;

    wsStatus = Bc250WinsysCreateVirtualContext(&pDevice->Winsys,
        BC250_WINSYS_NODE_COMPUTE);
    if (wsStatus != BC250_WINSYS_OK) {
        (VOID)Bc250WinsysDestroyPagingQueue(&pDevice->Winsys);
        return E_FAIL;
    }

    hr = Bc250UmdCommandBufferInit(pDevice, &pDevice->Node1CmdBuffer,
        CommandBufferBytes);
    if (FAILED(hr)) {
        (VOID)Bc250WinsysDestroyVirtualContext(&pDevice->Winsys);
        (VOID)Bc250WinsysDestroyPagingQueue(&pDevice->Winsys);
        return hr;
    }

    /*
     * CONTEXTO DE APRESENTACAO NO NO DE PAGINACAO (13/09, corrida 17:18:35).
     *
     * Ver a proveniencia completa em bc250_wddm_winsys.h. Em resumo: 84/84
     * pfnPresentCb devolveram E_FAIL com a primaria ja correta, e os unicos
     * contextos que o dxgkrnl usou para apresentar naquela corrida estao no
     * Node 0, enquanto todos os nossos estao no Node 1.
     *
     * Falha aqui NAO derruba o device: compute continua valendo sem
     * apresentacao. Mas o Present passa a RECUSAR com motivo nomeado, em vez de
     * cair de volta no contexto de trabalho -- cair de volta reproduziria o
     * estado que ja sabemos falhar e misturaria as duas variaveis, tornando o
     * proximo ensaio inconclusivo.
     */
    wsStatus = Bc250WinsysCreatePresentContext(&pDevice->Winsys,
        BC250_WINSYS_NODE_SDMA);
    if (wsStatus != BC250_WINSYS_OK) {
        Bc250UmdTrace("PRESENT_CONTEXT: criacao FALHOU no Node %u (status=%d %s) -- "
            "o Present vai RECUSAR com motivo nomeado; compute segue valido",
            BC250_WINSYS_NODE_SDMA, (int)wsStatus,
            Bc250WinsysStatusName(wsStatus));
    } else {
        Bc250UmdTrace("PRESENT_CONTEXT: Node=%u hContext=%p (contexto de trabalho "
            "segue no Node %u) -- NAO VALIDADO EM HARDWARE",
            pDevice->Winsys.PresentNodeOrdinal,
            pDevice->Winsys.PresentContext,
            BC250_WINSYS_NODE_COMPUTE);
    }

    pDevice->NativeNode1Ready = TRUE;
    Bc250UmdTrace("NODE1_SUBMISSION_READY: Context=%p GpuVA=0x%llX -- NAO VALIDADO EM HARDWARE",
        pDevice->Winsys.KernelContext,
        (unsigned long long)pDevice->Node1CmdBuffer.Allocation.GpuVirtualAddress);
    return S_OK;
}

HRESULT Bc250UmdSubmitNode1Proof(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    uint64_t DestinationGpuVa,
    _In_reads_(ValueCount) const uint32_t* Values,
    uint32_t ValueCount,
    uint64_t FenceGpuVa,
    uint32_t FenceValue)
{
    HRESULT hr = Bc250UmdBuildNode1ProofCommand(pDevice,
        DestinationGpuVa, Values, ValueCount, FenceGpuVa, FenceValue);
    if (FAILED(hr)) return hr;
    return Bc250UmdCommandBufferFlush(pDevice,
        &pDevice->Node1CmdBuffer, BC250_WINSYS_NODE_COMPUTE);
}

HRESULT Bc250UmdRunNode1ProofTransaction(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    _Out_ BC250_UMD_NODE1_PROOF_RESULT* pResult)
{
    BC250_WINSYS_ALLOCATION destination;
    BC250_WINSYS_MONITORED_FENCE fence;
    BC250_WINSYS_STATUS wsStatus;
    HRESULT hr = E_FAIL;
    BOOL fenceCleanup = TRUE;
    BOOL allocationCleanup = TRUE;
    uint32_t before[BC250_UMD_NODE1_PROOF_VALUE_COUNT];
    uint32_t index;

    if (pDevice == NULL || pResult == NULL || !pDevice->NativeNode1Ready ||
        !pDevice->Winsys.ContextCreated ||
        pDevice->Winsys.NodeOrdinal != BC250_WINSYS_NODE_COMPUTE) {
        return E_INVALIDARG;
    }

    memset(pResult, 0, sizeof(*pResult));
    memset(&destination, 0, sizeof(destination));
    memset(&fence, 0, sizeof(fence));
    for (index = 0; index < BC250_UMD_NODE1_PROOF_VALUE_COUNT; ++index) {
        pResult->Poison[index] = BC250_UMD_NODE1_PROOF_POISON_BASE + index;
        pResult->Expected[index] = 0xBC250C01u + index;
    }
    pResult->OperationHr = E_FAIL;

    wsStatus = Bc250WinsysAllocate(&pDevice->Winsys,
        Bc250UmdNextProofToken(), 4096u, 4096u, &destination);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    wsStatus = Bc250WinsysMapAllocation(&pDevice->Winsys, &destination, FALSE);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    wsStatus = Bc250WinsysMakeResident(&pDevice->Winsys, &destination);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    wsStatus = Bc250WinsysWaitPaging(&pDevice->Winsys, &destination);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;

    pResult->DestinationGpuVa = destination.GpuVirtualAddress;
    wsStatus = Bc250WinsysUploadAllocation(&pDevice->Winsys, &destination,
        pResult->Poison, sizeof(pResult->Poison));
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    memset(before, 0, sizeof(before));
    wsStatus = Bc250WinsysReadAllocation(&pDevice->Winsys, &destination,
        before, sizeof(before));
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    pResult->PoisonVerifiedBeforeSubmit =
        (memcmp(before, pResult->Poison, sizeof(before)) == 0);
    if (!pResult->PoisonVerifiedBeforeSubmit) goto cleanup;

    wsStatus = Bc250WinsysCreateMonitoredFence(&pDevice->Winsys,
        BC250_UMD_NODE1_PROOF_FENCE_POISON, &fence);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    pResult->FenceGpuVa = fence.GpuVirtualAddress;
    if (*fence.CpuVirtualAddress != BC250_UMD_NODE1_PROOF_FENCE_POISON) {
        goto cleanup;
    }

    hr = Bc250UmdSubmitNode1Proof(pDevice, destination.GpuVirtualAddress,
        pResult->Expected, BC250_UMD_NODE1_PROOF_VALUE_COUNT,
        fence.GpuVirtualAddress, BC250_UMD_NODE1_PROOF_FENCE_VALUE);
    if (FAILED(hr)) goto cleanup;
    pResult->Submitted = TRUE;

    wsStatus = Bc250WinsysWaitForFenceFromCpu(&pDevice->Winsys, &fence,
        BC250_UMD_NODE1_PROOF_FENCE_VALUE);
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    pResult->FenceObserved = *fence.CpuVirtualAddress;
    pResult->FenceMatched =
        (pResult->FenceObserved == BC250_UMD_NODE1_PROOF_FENCE_VALUE);
    if (!pResult->FenceMatched) goto cleanup;

    wsStatus = Bc250WinsysReadAllocation(&pDevice->Winsys, &destination,
        pResult->Observed, sizeof(pResult->Observed));
    if (wsStatus != BC250_WINSYS_OK) goto cleanup;
    pResult->DataMatched = (memcmp(pResult->Observed, pResult->Expected,
        sizeof(pResult->Expected)) == 0);
    if (!pResult->DataMatched) goto cleanup;
    hr = S_OK;

cleanup:
    pResult->OperationHr = hr;
    if (fence.Created) {
        fenceCleanup = (Bc250WinsysDestroyMonitoredFence(
            &pDevice->Winsys, &fence) == BC250_WINSYS_OK);
    }
    if (destination.Created) {
        allocationCleanup = (Bc250WinsysDeallocate(
            &pDevice->Winsys, &destination) == BC250_WINSYS_OK);
    }
    pResult->CleanupComplete = fenceCleanup && allocationCleanup;
    if (!pResult->CleanupComplete) hr = E_FAIL;
    if (FAILED(hr) || !pResult->PoisonVerifiedBeforeSubmit ||
        !pResult->Submitted || !pResult->FenceMatched ||
        !pResult->DataMatched) {
        pResult->OperationHr = E_FAIL;
        Bc250UmdTrace("NODE1_TRIPOD: FAIL-CLOSED poison=%d submit=%d fence=%d data=%d cleanup=%d -- NAO VALIDADO EM HARDWARE",
            pResult->PoisonVerifiedBeforeSubmit, pResult->Submitted,
            pResult->FenceMatched, pResult->DataMatched,
            pResult->CleanupComplete);
        return E_FAIL;
    }

    pResult->OperationHr = S_OK;
    Bc250UmdTrace("NODE1_TRIPOD: contrato completo por callbacks; prova fisica ainda exige RPTR/WPTR e evidencia arquivada -- NAO VALIDADO EM HARDWARE");
    return S_OK;
}

/* =========================================================================
 * D3D10 State Object DDIs (Blend, DepthStencil, Rasterizer, Sampler)
 * ========================================================================= */

SIZE_T APIENTRY Bc250Umd10_1CalcPrivateBlendStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1_DDI_BLEND_DESC* pBlendDesc)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pBlendDesc);
    return sizeof(BC250_UMD_BLEND_STATE);
}

VOID APIENTRY Bc250Umd10_1CreateBlendState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1_DDI_BLEND_DESC* pBlendDesc,
    D3D10DDI_HBLENDSTATE hBlendState,
    D3D10DDI_HRTBLENDSTATE hRTBlendState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_BLEND_STATE* state = (BC250_UMD_BLEND_STATE*)hBlendState.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (state == NULL || pBlendDesc == NULL) return;
    memset(state, 0, sizeof(*state));
    state->Desc = *pBlendDesc;
    state->hRTBlendState = hRTBlendState;
    state->Created = TRUE;

    Bc250UmdTrace("CreateBlendState: state=%p AlphaToCoverage=%d IndBlend=%d",
        (void*)state, pBlendDesc->AlphaToCoverageEnable, pBlendDesc->IndependentBlendEnable);
}

VOID APIENTRY Bc250Umd10DestroyBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_BLEND_STATE* state = (BC250_UMD_BLEND_STATE*)hBlendState.pDrvPrivate;

    if (device != NULL && device->PipelineState.BoundBlendState.pDrvPrivate == hBlendState.pDrvPrivate) {
        device->PipelineState.BoundBlendState.pDrvPrivate = NULL;
    }
    if (state != NULL) {
        state->Created = FALSE;
    }
    Bc250UmdTrace("DestroyBlendState: state=%p", (void*)state);
}

VOID APIENTRY Bc250Umd10SetBlendState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HBLENDSTATE hBlendState,
    CONST FLOAT pBlendFactor[4],
    UINT SampleMask)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    device->PipelineState.BoundBlendState = hBlendState;
    if (pBlendFactor != NULL) {
        device->PipelineState.BlendFactor[0] = pBlendFactor[0];
        device->PipelineState.BlendFactor[1] = pBlendFactor[1];
        device->PipelineState.BlendFactor[2] = pBlendFactor[2];
        device->PipelineState.BlendFactor[3] = pBlendFactor[3];
    } else {
        device->PipelineState.BlendFactor[0] = 1.0f;
        device->PipelineState.BlendFactor[1] = 1.0f;
        device->PipelineState.BlendFactor[2] = 1.0f;
        device->PipelineState.BlendFactor[3] = 1.0f;
    }
    device->PipelineState.SampleMask = SampleMask;
    Bc250UmdTrace("SetBlendState: state=%p SampleMask=0x%08X", hBlendState.pDrvPrivate, SampleMask);
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateDepthStencilStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pDepthStencilDesc);
    return sizeof(BC250_UMD_DEPTH_STENCIL_STATE);
}

VOID APIENTRY Bc250Umd10CreateDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DEPTH_STENCIL_STATE* state = (BC250_UMD_DEPTH_STENCIL_STATE*)hDepthStencilState.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (state == NULL || pDepthStencilDesc == NULL) return;
    memset(state, 0, sizeof(*state));
    state->Desc = *pDepthStencilDesc;
    state->hRTDepthStencilState = hRTDepthStencilState;
    state->Created = TRUE;

    Bc250UmdTrace("CreateDepthStencilState: state=%p DepthEnable=%d StencilEnable=%d",
        (void*)state, pDepthStencilDesc->DepthEnable, pDepthStencilDesc->StencilEnable);
}

VOID APIENTRY Bc250Umd10DestroyDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DEPTH_STENCIL_STATE* state = (BC250_UMD_DEPTH_STENCIL_STATE*)hDepthStencilState.pDrvPrivate;

    if (device != NULL && device->PipelineState.BoundDepthStencilState.pDrvPrivate == hDepthStencilState.pDrvPrivate) {
        device->PipelineState.BoundDepthStencilState.pDrvPrivate = NULL;
    }
    if (state != NULL) {
        state->Created = FALSE;
    }
    Bc250UmdTrace("DestroyDepthStencilState: state=%p", (void*)state);
}

VOID APIENTRY Bc250Umd10SetDepthStencilState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    UINT StencilRef)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    device->PipelineState.BoundDepthStencilState = hDepthStencilState;
    device->PipelineState.StencilRef = StencilRef;
    Bc250UmdTrace("SetDepthStencilState: state=%p StencilRef=%u", hDepthStencilState.pDrvPrivate, StencilRef);
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateRasterizerStateSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pRasterizerDesc);
    return sizeof(BC250_UMD_RASTERIZER_STATE);
}

VOID APIENTRY Bc250Umd10CreateRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_RASTERIZER_DESC* pRasterizerDesc,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RASTERIZER_STATE* state = (BC250_UMD_RASTERIZER_STATE*)hRasterizerState.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (state == NULL || pRasterizerDesc == NULL) return;
    memset(state, 0, sizeof(*state));
    state->Desc = *pRasterizerDesc;
    state->hRTRasterizerState = hRTRasterizerState;
    state->Created = TRUE;

    Bc250UmdTrace("CreateRasterizerState: state=%p FillMode=%u CullMode=%u Scissor=%d",
        (void*)state, (UINT)pRasterizerDesc->FillMode, (UINT)pRasterizerDesc->CullMode,
        pRasterizerDesc->ScissorEnable);
}

VOID APIENTRY Bc250Umd10DestroyRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RASTERIZER_STATE* state = (BC250_UMD_RASTERIZER_STATE*)hRasterizerState.pDrvPrivate;

    if (device != NULL && device->PipelineState.BoundRasterizerState.pDrvPrivate == hRasterizerState.pDrvPrivate) {
        device->PipelineState.BoundRasterizerState.pDrvPrivate = NULL;
    }
    if (state != NULL) {
        state->Created = FALSE;
    }
    Bc250UmdTrace("DestroyRasterizerState: state=%p", (void*)state);
}

VOID APIENTRY Bc250Umd10SetRasterizerState(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRASTERIZERSTATE hRasterizerState)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    device->PipelineState.BoundRasterizerState = hRasterizerState;
    Bc250UmdTrace("SetRasterizerState: state=%p", hRasterizerState.pDrvPrivate);
}

SIZE_T APIENTRY Bc250Umd10CalcPrivateSamplerSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_SAMPLER_DESC* pSamplerDesc)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pSamplerDesc);
    return sizeof(BC250_UMD_SAMPLER);
}

VOID APIENTRY Bc250Umd10CreateSampler(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_DDI_SAMPLER_DESC* pSamplerDesc,
    D3D10DDI_HSAMPLER hSampler,
    D3D10DDI_HRTSAMPLER hRTSampler)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SAMPLER* sampler = (BC250_UMD_SAMPLER*)hSampler.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (sampler == NULL || pSamplerDesc == NULL) return;
    memset(sampler, 0, sizeof(*sampler));
    sampler->Desc = *pSamplerDesc;
    sampler->hRTSampler = hRTSampler;
    sampler->Created = TRUE;

    Bc250UmdTrace("CreateSampler: sampler=%p Filter=%u AddressU=%u",
        (void*)sampler, (UINT)pSamplerDesc->Filter, (UINT)pSamplerDesc->AddressU);
}

VOID APIENTRY Bc250Umd10DestroySampler(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSAMPLER hSampler)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SAMPLER* sampler = (BC250_UMD_SAMPLER*)hSampler.pDrvPrivate;

    if (device != NULL) {
        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
            if (device->PipelineState.VsSamplers[i].pDrvPrivate == hSampler.pDrvPrivate) {
                device->PipelineState.VsSamplers[i].pDrvPrivate = NULL;
            }
            if (device->PipelineState.PsSamplers[i].pDrvPrivate == hSampler.pDrvPrivate) {
                device->PipelineState.PsSamplers[i].pDrvPrivate = NULL;
            }
            if (device->PipelineState.GsSamplers[i].pDrvPrivate == hSampler.pDrvPrivate) {
                device->PipelineState.GsSamplers[i].pDrvPrivate = NULL;
            }
        }
    }
    if (sampler != NULL) {
        sampler->Created = FALSE;
    }
    Bc250UmdTrace("DestroySampler: sampler=%p", (void*)sampler);
}

VOID APIENTRY Bc250Umd10VsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumSamplers && (Offset + i) < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
        device->PipelineState.VsSamplers[Offset + i] = phSamplers ? phSamplers[i] : (D3D10DDI_HSAMPLER){NULL};
    }
    Bc250UmdTrace("VsSetSamplers: Offset=%u Count=%u", Offset, NumSamplers);
}

VOID APIENTRY Bc250Umd10PsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumSamplers && (Offset + i) < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
        device->PipelineState.PsSamplers[Offset + i] = phSamplers ? phSamplers[i] : (D3D10DDI_HSAMPLER){NULL};
    }
    Bc250UmdTrace("PsSetSamplers: Offset=%u Count=%u", Offset, NumSamplers);
}

VOID APIENTRY Bc250Umd10GsSetSamplers(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - Offset) UINT NumSamplers,
    _In_reads_(NumSamplers) CONST D3D10DDI_HSAMPLER* phSamplers)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumSamplers && (Offset + i) < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
        device->PipelineState.GsSamplers[Offset + i] = phSamplers ? phSamplers[i] : (D3D10DDI_HSAMPLER){NULL};
    }
    Bc250UmdTrace("GsSetSamplers: Offset=%u Count=%u", Offset, NumSamplers);
}

/* =========================================================================
 * D3D10 Shader Resource View (SRV) DDIs
 * ========================================================================= */

SIZE_T APIENTRY Bc250Umd10_1CalcPrivateShaderResourceViewSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCreate);
    return sizeof(BC250_UMD_SRV);
}

VOID APIENTRY Bc250Umd10_1CreateShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10_1DDIARG_CREATESHADERRESOURCEVIEW* pCreate,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SRV* srv = (BC250_UMD_SRV*)hShaderResourceView.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (srv == NULL || pCreate == NULL) return;
    memset(srv, 0, sizeof(*srv));
    srv->Desc = *pCreate;
    srv->hRTSRV = hRTShaderResourceView;
    srv->pResource = (BC250_UMD_RESOURCE*)pCreate->hDrvResource.pDrvPrivate;
    srv->Created = TRUE;

    Bc250UmdTrace("CreateShaderResourceView: srv=%p res=%p Format=%u Dim=%u",
        (void*)srv, (void*)srv->pResource, (UINT)pCreate->Format, (UINT)pCreate->ResourceDimension);
}

VOID APIENTRY Bc250Umd10DestroyShaderResourceView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_SRV* srv = (BC250_UMD_SRV*)hShaderResourceView.pDrvPrivate;

    if (device != NULL) {
        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
            if (device->PipelineState.VsShaderResources[i].pDrvPrivate == hShaderResourceView.pDrvPrivate) {
                device->PipelineState.VsShaderResources[i].pDrvPrivate = NULL;
            }
            if (device->PipelineState.PsShaderResources[i].pDrvPrivate == hShaderResourceView.pDrvPrivate) {
                device->PipelineState.PsShaderResources[i].pDrvPrivate = NULL;
            }
            if (device->PipelineState.GsShaderResources[i].pDrvPrivate == hShaderResourceView.pDrvPrivate) {
                device->PipelineState.GsShaderResources[i].pDrvPrivate = NULL;
            }
        }
    }
    if (srv != NULL) {
        srv->Created = FALSE;
    }
    Bc250UmdTrace("DestroyShaderResourceView: srv=%p", (void*)srv);
}

VOID APIENTRY Bc250Umd10VsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumViews && (Offset + i) < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
        device->PipelineState.VsShaderResources[Offset + i] = phViews ? phViews[i] : (D3D10DDI_HSHADERRESOURCEVIEW){NULL};
    }
    Bc250UmdTrace("VsSetShaderResources: Offset=%u Count=%u", Offset, NumViews);
}

VOID APIENTRY Bc250Umd10PsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumViews && (Offset + i) < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
        device->PipelineState.PsShaderResources[Offset + i] = phViews ? phViews[i] : (D3D10DDI_HSHADERRESOURCEVIEW){NULL};
    }
    Bc250UmdTrace("PsSetShaderResources: Offset=%u Count=%u srv0=%p srv1=%p", Offset, NumViews,
        (NumViews > 0 && phViews) ? phViews[0].pDrvPrivate : NULL,
        (NumViews > 1 && phViews) ? phViews[1].pDrvPrivate : NULL);
}

VOID APIENTRY Bc250Umd10GsSetShaderResources(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT Offset,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - Offset) UINT NumViews,
    _In_reads_(NumViews) CONST D3D10DDI_HSHADERRESOURCEVIEW* phViews)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumViews && (Offset + i) < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
        device->PipelineState.GsShaderResources[Offset + i] = phViews ? phViews[i] : (D3D10DDI_HSHADERRESOURCEVIEW){NULL};
    }
    Bc250UmdTrace("GsSetShaderResources: Offset=%u Count=%u", Offset, NumViews);
}

/* =========================================================================
 * D3D10 Element Layout (Input Layout) DDIs
 * ========================================================================= */

SIZE_T APIENTRY Bc250Umd10CalcPrivateElementLayoutSize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEELEMENTLAYOUT* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCreate);
    return sizeof(BC250_UMD_ELEMENT_LAYOUT);
}

VOID APIENTRY Bc250Umd10CreateElementLayout(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEELEMENTLAYOUT* pCreate,
    D3D10DDI_HELEMENTLAYOUT hElementLayout,
    D3D10DDI_HRTELEMENTLAYOUT hRTElementLayout)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_ELEMENT_LAYOUT* layout = (BC250_UMD_ELEMENT_LAYOUT*)hElementLayout.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (layout == NULL || pCreate == NULL) return;
    memset(layout, 0, sizeof(*layout));
    layout->NumElements = (pCreate->NumElements > 32) ? 32 : pCreate->NumElements;
    if (pCreate->pVertexElements != NULL && layout->NumElements > 0) {
        memcpy(layout->Elements, pCreate->pVertexElements, layout->NumElements * sizeof(D3D10DDIARG_INPUT_ELEMENT_DESC));
    }
    layout->hRTElementLayout = hRTElementLayout;
    layout->Created = TRUE;

    Bc250UmdTrace("CreateElementLayout: layout=%p NumElements=%u", (void*)layout, layout->NumElements);
}

VOID APIENTRY Bc250Umd10DestroyElementLayout(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HELEMENTLAYOUT hElementLayout)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_ELEMENT_LAYOUT* layout = (BC250_UMD_ELEMENT_LAYOUT*)hElementLayout.pDrvPrivate;

    if (device != NULL && device->PipelineState.InputLayout.pDrvPrivate == hElementLayout.pDrvPrivate) {
        device->PipelineState.InputLayout.pDrvPrivate = NULL;
    }
    if (layout != NULL) {
        layout->Created = FALSE;
    }
    Bc250UmdTrace("DestroyElementLayout: layout=%p", (void*)layout);
}

/* =========================================================================
 * D3D10 Query & Predication DDIs
 * ========================================================================= */

SIZE_T APIENTRY Bc250Umd10CalcPrivateQuerySize(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEQUERY* pCreate)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCreate);
    return sizeof(BC250_UMD_QUERY);
}

VOID APIENTRY Bc250Umd10CreateQuery(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_CREATEQUERY* pCreate,
    D3D10DDI_HQUERY hQuery,
    D3D10DDI_HRTQUERY hRTQuery)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_QUERY* query = (BC250_UMD_QUERY*)hQuery.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);

    if (query == NULL || pCreate == NULL) return;
    memset(query, 0, sizeof(*query));
    query->QueryType = pCreate->Query;
    query->MiscFlags = pCreate->MiscFlags;
    query->hRTQuery = hRTQuery;
    query->IsActive = FALSE;
    query->HasData = FALSE;
    query->Created = TRUE;

    Bc250UmdTrace("CreateQuery: query=%p Type=%u Flags=0x%08X",
        (void*)query, (UINT)pCreate->Query, pCreate->MiscFlags);
}

VOID APIENTRY Bc250Umd10DestroyQuery(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_QUERY* query = (BC250_UMD_QUERY*)hQuery.pDrvPrivate;

    if (device != NULL && device->PipelineState.PredicateQuery.pDrvPrivate == hQuery.pDrvPrivate) {
        device->PipelineState.PredicateQuery.pDrvPrivate = NULL;
    }
    if (query != NULL) {
        query->Created = FALSE;
    }
    Bc250UmdTrace("DestroyQuery: query=%p", (void*)query);
}

VOID APIENTRY Bc250Umd10QueryBegin(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_QUERY* query = (BC250_UMD_QUERY*)hQuery.pDrvPrivate;

    if (query == NULL) return;
    query->IsActive = TRUE;
    query->HasData = FALSE;
    query->StartCounter = (device != NULL) ? device->PipelineState.DrawCount : 0;
    Bc250UmdTrace("QueryBegin: query=%p Type=%u StartCount=%llu",
        (void*)query, (UINT)query->QueryType, query->StartCounter);
}

VOID APIENTRY Bc250Umd10QueryEnd(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_QUERY* query = (BC250_UMD_QUERY*)hQuery.pDrvPrivate;

    if (query == NULL) return;
    query->IsActive = FALSE;
    query->HasData = TRUE;
    query->EndCounter = (device != NULL) ? device->PipelineState.DrawCount : 0;
    query->Data = query->EndCounter - query->StartCounter;
    Bc250UmdTrace("QueryEnd: query=%p Type=%u EndCount=%llu Data=%llu",
        (void*)query, (UINT)query->QueryType, query->EndCounter, query->Data);
}

VOID APIENTRY Bc250Umd10QueryGetData(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery,
    _Out_writes_bytes_all_opt_(DataSize) VOID* pData,
    UINT DataSize,
    UINT Flags)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_QUERY* query = (BC250_UMD_QUERY*)hQuery.pDrvPrivate;
    UNREFERENCED_PARAMETER(Flags);

    if (query == NULL || pData == NULL || DataSize == 0) return;

    memset(pData, 0, DataSize);

    switch (query->QueryType) {
    case D3D10DDI_QUERY_EVENT: {
        if (DataSize >= sizeof(BOOL)) {
            *(BOOL*)pData = TRUE;
        }
        break;
    }
    case D3D10DDI_QUERY_OCCLUSION:
    case D3D10DDI_QUERY_OCCLUSIONPREDICATE: {
        if (DataSize >= sizeof(UINT64)) {
            *(UINT64*)pData = query->Data;
        }
        break;
    }
    case D3D10DDI_QUERY_TIMESTAMP: {
        if (DataSize >= sizeof(UINT64)) {
            *(UINT64*)pData = query->EndCounter;
        }
        break;
    }
    case D3D10DDI_QUERY_TIMESTAMPDISJOINT: {
        if (DataSize >= sizeof(D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT)) {
            D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT* disjoint =
                (D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT*)pData;
            disjoint->Frequency = 1000000000ull; /* 1 GHz monotonic reference */
            disjoint->Disjoint = FALSE;
        }
        break;
    }
    case D3D10DDI_QUERY_PIPELINESTATS: {
        if (DataSize >= sizeof(D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS)) {
            D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS* stats =
                (D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS*)pData;
            if (device != NULL) {
                stats->IAVertices = device->PipelineState.TotalVertexCount;
                stats->IAPrimitives = device->PipelineState.TotalIndexCount;
                stats->VSInvocations = device->PipelineState.TotalVertexCount;
                stats->GSInvocations = 0;
                stats->GSPrimitives = 0;
                stats->CInvocations = device->PipelineState.DrawCount;
                stats->CPrimitives = device->PipelineState.DrawCount;
                stats->PSInvocations = device->PipelineState.TotalVertexCount;
            }
        }
        break;
    }
    case D3D10DDI_QUERY_STREAMOUTPUTSTATS: {
        if (DataSize >= sizeof(D3D10_DDI_QUERY_DATA_SO_STATISTICS)) {
            D3D10_DDI_QUERY_DATA_SO_STATISTICS* so =
                (D3D10_DDI_QUERY_DATA_SO_STATISTICS*)pData;
            so->NumPrimitivesWritten = 0;
            so->PrimitivesStorageNeeded = 0;
        }
        break;
    }
    default:
        break;
    }

    Bc250UmdTrace("QueryGetData: query=%p Type=%u DataSize=%u",
        (void*)query, (UINT)query->QueryType, DataSize);
}

VOID APIENTRY Bc250Umd10SetPredication(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HQUERY hQuery,
    BOOL PredicateValue)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    device->PipelineState.PredicateQuery = hQuery;
    device->PipelineState.PredicateValue = PredicateValue;
    Bc250UmdTrace("SetPredication: query=%p PredicateValue=%d", hQuery.pDrvPrivate, PredicateValue);
}

/* =========================================================================
 * D3D10 Stream Output, Mipmaps, Resolve, Counters, and Text Filter
 * ========================================================================= */

VOID APIENTRY Bc250Umd10SoSetTargets(
    D3D10DDI_HDEVICE hDevice,
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT ClearTargets,
    _In_reads_(NumBuffers) CONST D3D10DDI_HRESOURCE* phResources,
    _In_reads_(NumBuffers) CONST UINT* pOffsets)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;

    for (UINT i = 0; i < NumBuffers && i < D3D11_SO_BUFFER_SLOT_COUNT; ++i) {
        device->PipelineState.SoBuffers[i] = phResources ? phResources[i] : (D3D10DDI_HRESOURCE){NULL};
        device->PipelineState.SoOffsets[i] = pOffsets ? pOffsets[i] : 0;
    }
    for (UINT i = NumBuffers; i < (NumBuffers + ClearTargets) && i < D3D11_SO_BUFFER_SLOT_COUNT; ++i) {
        device->PipelineState.SoBuffers[i] = (D3D10DDI_HRESOURCE){NULL};
        device->PipelineState.SoOffsets[i] = 0;
    }

    Bc250UmdTrace("SoSetTargets: NumBuffers=%u ClearTargets=%u", NumBuffers, ClearTargets);
}

VOID APIENTRY Bc250Umd10GenMips(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    UNREFERENCED_PARAMETER(device);
    Bc250UmdTrace("GenMips: srv=%p", hShaderResourceView.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10ResourceResolveSubresource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hDstResource,
    UINT DstSubresource,
    D3D10DDI_HRESOURCE hSrcResource,
    UINT SrcSubresource,
    DXGI_FORMAT ResolveFormat)
{
    UNREFERENCED_PARAMETER(ResolveFormat);
    Bc250Umd10ResourceCopyRegion(hDevice, hDstResource, DstSubresource, 0, 0, 0,
        hSrcResource, SrcSubresource, NULL);
    Bc250UmdTrace("ResourceResolveSubresource: dst=%p src=%p",
        hDstResource.pDrvPrivate, hSrcResource.pDrvPrivate);
}

VOID APIENTRY Bc250Umd10CheckCounterInfo(
    D3D10DDI_HDEVICE hDevice,
    _Out_ D3D10DDI_COUNTER_INFO* pCounterInfo)
{
    UNREFERENCED_PARAMETER(hDevice);
    if (pCounterInfo == NULL) return;
    memset(pCounterInfo, 0, sizeof(*pCounterInfo));
    pCounterInfo->NumDetectableParallelUnits = 1;
    pCounterInfo->NumSimultaneousCounters = 0;
    pCounterInfo->LastDeviceDependentCounter = (D3D10DDI_QUERY)0;
    Bc250UmdTrace("CheckCounterInfo: reported 0 hw counters");
}

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
    _Inout_opt_ UINT* pDescriptionLength)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Query);
    if (pCounterType != NULL) *pCounterType = D3D10DDI_COUNTER_TYPE_FLOAT32;
    if (pActiveCounters != NULL) *pActiveCounters = 0;
    if (pNameLength != NULL) *pNameLength = 0;
    if (pUnitsLength != NULL) *pUnitsLength = 0;
    if (pDescriptionLength != NULL) *pDescriptionLength = 0;
    UNREFERENCED_PARAMETER(pName);
    UNREFERENCED_PARAMETER(pUnits);
    UNREFERENCED_PARAMETER(pDescription);
    Bc250UmdTrace("CheckCounter: Query=%u", (UINT)Query);
}

VOID APIENTRY Bc250Umd10SetTextFilterSize(
    D3D10DDI_HDEVICE hDevice,
    UINT Width,
    UINT Height)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    device->PipelineState.TextFilterWidth = Width;
    device->PipelineState.TextFilterHeight = Height;
    Bc250UmdTrace("SetTextFilterSize: Width=%u Height=%u", Width, Height);
}

VOID Bc250UmdPopulateDeviceFuncs10_1(_Out_ D3D10_1DDI_DEVICEFUNCS* pFuncs)
{
    if (pFuncs == NULL) return;
    memset(pFuncs, 0, sizeof(*pFuncs));

    /* Device lifecycle, relocate, flush */
    pFuncs->pfnDestroyDevice = Bc250Umd10DestroyDevice;
    pFuncs->pfnRelocateDeviceFuncs = Bc250Umd10RelocateDeviceFuncs;
    pFuncs->pfnFlush = Bc250Umd10Flush;

    /* Shaders */
    pFuncs->pfnCalcPrivateShaderSize = Bc250Umd10CalcPrivateShaderSize;
    pFuncs->pfnCreateVertexShader = Bc250Umd10CreateVertexShader;
    pFuncs->pfnCreateGeometryShader = Bc250Umd10CreateGeometryShader;
    pFuncs->pfnCreatePixelShader = Bc250Umd10CreatePixelShader;
    pFuncs->pfnCalcPrivateGeometryShaderWithStreamOutput = Bc250Umd10CalcPrivateGeometryShaderWithStreamOutput;
    pFuncs->pfnCreateGeometryShaderWithStreamOutput = Bc250Umd10CreateGeometryShaderWithStreamOutput;
    pFuncs->pfnDestroyShader = Bc250Umd10DestroyShader;

    /* Resources */
    pFuncs->pfnCalcPrivateResourceSize = Bc250Umd10CalcPrivateResourceSize;
    pFuncs->pfnCalcPrivateOpenedResourceSize = Bc250Umd10CalcPrivateOpenedResourceSize;
    pFuncs->pfnCreateResource = Bc250Umd10CreateResource;
    pFuncs->pfnOpenResource = Bc250Umd10OpenResource;
    pFuncs->pfnDestroyResource = Bc250Umd10DestroyResource;
    pFuncs->pfnResourceMap = Bc250Umd10ResourceMap;
    pFuncs->pfnResourceUnmap = Bc250Umd10ResourceUnmap;
    pFuncs->pfnResourceIsStagingBusy = Bc250Umd10ResourceIsStagingBusy;
    pFuncs->pfnDynamicIABufferMapNoOverwrite = Bc250Umd10ResourceMap;
    pFuncs->pfnDynamicIABufferUnmap = Bc250Umd10ResourceUnmap;
    pFuncs->pfnDynamicConstantBufferMapDiscard = Bc250Umd10ResourceMap;
    pFuncs->pfnDynamicIABufferMapDiscard = Bc250Umd10ResourceMap;
    pFuncs->pfnDynamicConstantBufferUnmap = Bc250Umd10ResourceUnmap;
    pFuncs->pfnDynamicResourceMapDiscard = Bc250Umd10ResourceMap;
    pFuncs->pfnDynamicResourceUnmap = Bc250Umd10ResourceUnmap;
    pFuncs->pfnStagingResourceMap = Bc250Umd10ResourceMap;
    pFuncs->pfnStagingResourceUnmap = Bc250Umd10ResourceUnmap;
    pFuncs->pfnCheckFormatSupport = Bc250Umd10CheckFormatSupport;
    pFuncs->pfnCheckMultisampleQualityLevels = Bc250Umd10CheckMultisampleQualityLevels;

    /* Render Target & Depth Stencil Views */
    pFuncs->pfnCalcPrivateRenderTargetViewSize = Bc250Umd10CalcPrivateRenderTargetViewSize;
    pFuncs->pfnCreateRenderTargetView = Bc250Umd10CreateRenderTargetView;
    pFuncs->pfnDestroyRenderTargetView = Bc250Umd10DestroyRenderTargetView;
    pFuncs->pfnCalcPrivateDepthStencilViewSize = Bc250Umd10CalcPrivateDepthStencilViewSize;
    pFuncs->pfnCreateDepthStencilView = Bc250Umd10CreateDepthStencilView;
    pFuncs->pfnDestroyDepthStencilView = Bc250Umd10DestroyDepthStencilView;
    pFuncs->pfnSetRenderTargets = Bc250Umd10SetRenderTargets;
    pFuncs->pfnClearRenderTargetView = Bc250Umd10ClearRenderTargetView;
    pFuncs->pfnClearDepthStencilView = Bc250Umd10ClearDepthStencilView;
    pFuncs->pfnSetViewports = Bc250Umd10SetViewports;
    pFuncs->pfnSetScissorRects = Bc250Umd10SetScissorRects;

    /* Draw, IA, Shaders, CBs, and Resource Copy DDIs */
    pFuncs->pfnDefaultConstantBufferUpdateSubresourceUP = Bc250Umd10DefaultConstantBufferUpdateSubresourceUP;
    pFuncs->pfnVsSetConstantBuffers = Bc250Umd10VsSetConstantBuffers;
    pFuncs->pfnVsSetShader = Bc250Umd10VsSetShader;
    pFuncs->pfnDrawIndexed = Bc250Umd10DrawIndexed;
    pFuncs->pfnDraw = Bc250Umd10Draw;
    pFuncs->pfnPsSetConstantBuffers = Bc250Umd10PsSetConstantBuffers;
    pFuncs->pfnPsSetShader = Bc250Umd10PsSetShader;
    pFuncs->pfnIaSetInputLayout = Bc250Umd10IaSetInputLayout;
    pFuncs->pfnIaSetVertexBuffers = Bc250Umd10IaSetVertexBuffers;
    pFuncs->pfnIaSetIndexBuffer = Bc250Umd10IaSetIndexBuffer;
    pFuncs->pfnDrawIndexedInstanced = Bc250Umd10DrawIndexedInstanced;
    pFuncs->pfnDrawInstanced = Bc250Umd10DrawInstanced;
    pFuncs->pfnGsSetConstantBuffers = Bc250Umd10GsSetConstantBuffers;
    pFuncs->pfnGsSetShader = Bc250Umd10GsSetShader;
    pFuncs->pfnIaSetTopology = Bc250Umd10IaSetTopology;
    pFuncs->pfnDrawAuto = Bc250Umd10DrawAuto;
    pFuncs->pfnResourceCopy = Bc250Umd10ResourceCopy;
    pFuncs->pfnResourceCopyRegion = Bc250Umd10ResourceCopyRegion;
    pFuncs->pfnResourceConvert = Bc250Umd10ResourceCopy;
    pFuncs->pfnResourceConvertRegion = Bc250Umd10ResourceCopyRegion;
    pFuncs->pfnResourceUpdateSubresourceUP = Bc250Umd10ResourceUpdateSubresourceUP;
    pFuncs->pfnResourceReadAfterWriteHazard = Bc250Umd10ResourceReadAfterWriteHazard;
    pFuncs->pfnShaderResourceViewReadAfterWriteHazard = Bc250Umd10ShaderResourceViewReadAfterWriteHazard;

    /* State Objects */
    pFuncs->pfnCalcPrivateBlendStateSize = Bc250Umd10_1CalcPrivateBlendStateSize;
    pFuncs->pfnCreateBlendState = Bc250Umd10_1CreateBlendState;
    pFuncs->pfnDestroyBlendState = Bc250Umd10DestroyBlendState;
    pFuncs->pfnSetBlendState = Bc250Umd10SetBlendState;

    pFuncs->pfnCalcPrivateDepthStencilStateSize = Bc250Umd10CalcPrivateDepthStencilStateSize;
    pFuncs->pfnCreateDepthStencilState = Bc250Umd10CreateDepthStencilState;
    pFuncs->pfnDestroyDepthStencilState = Bc250Umd10DestroyDepthStencilState;
    pFuncs->pfnSetDepthStencilState = Bc250Umd10SetDepthStencilState;

    pFuncs->pfnCalcPrivateRasterizerStateSize = Bc250Umd10CalcPrivateRasterizerStateSize;
    pFuncs->pfnCreateRasterizerState = Bc250Umd10CreateRasterizerState;
    pFuncs->pfnDestroyRasterizerState = Bc250Umd10DestroyRasterizerState;
    pFuncs->pfnSetRasterizerState = Bc250Umd10SetRasterizerState;

    pFuncs->pfnCalcPrivateSamplerSize = Bc250Umd10CalcPrivateSamplerSize;
    pFuncs->pfnCreateSampler = Bc250Umd10CreateSampler;
    pFuncs->pfnDestroySampler = Bc250Umd10DestroySampler;
    pFuncs->pfnVsSetSamplers = Bc250Umd10VsSetSamplers;
    pFuncs->pfnPsSetSamplers = Bc250Umd10PsSetSamplers;
    pFuncs->pfnGsSetSamplers = Bc250Umd10GsSetSamplers;

    /* Shader Resource Views */
    pFuncs->pfnCalcPrivateShaderResourceViewSize = Bc250Umd10_1CalcPrivateShaderResourceViewSize;
    pFuncs->pfnCreateShaderResourceView = Bc250Umd10_1CreateShaderResourceView;
    pFuncs->pfnDestroyShaderResourceView = Bc250Umd10DestroyShaderResourceView;
    pFuncs->pfnVsSetShaderResources = Bc250Umd10VsSetShaderResources;
    pFuncs->pfnPsSetShaderResources = Bc250Umd10PsSetShaderResources;
    pFuncs->pfnGsSetShaderResources = Bc250Umd10GsSetShaderResources;

    /* Element Layout */
    pFuncs->pfnCalcPrivateElementLayoutSize = Bc250Umd10CalcPrivateElementLayoutSize;
    pFuncs->pfnCreateElementLayout = Bc250Umd10CreateElementLayout;
    pFuncs->pfnDestroyElementLayout = Bc250Umd10DestroyElementLayout;

    /* Queries & Predication */
    pFuncs->pfnCalcPrivateQuerySize = Bc250Umd10CalcPrivateQuerySize;
    pFuncs->pfnCreateQuery = Bc250Umd10CreateQuery;
    pFuncs->pfnDestroyQuery = Bc250Umd10DestroyQuery;
    pFuncs->pfnQueryBegin = Bc250Umd10QueryBegin;
    pFuncs->pfnQueryEnd = Bc250Umd10QueryEnd;
    pFuncs->pfnQueryGetData = Bc250Umd10QueryGetData;
    pFuncs->pfnSetPredication = Bc250Umd10SetPredication;

    /* Stream Output, Mips, Resolve, Counters, and Text Filter */
    pFuncs->pfnSoSetTargets = Bc250Umd10SoSetTargets;
    pFuncs->pfnGenMips = Bc250Umd10GenMips;
    pFuncs->pfnResourceResolveSubresource = Bc250Umd10ResourceResolveSubresource;
    pFuncs->pfnCheckCounterInfo = Bc250Umd10CheckCounterInfo;
    pFuncs->pfnCheckCounter = Bc250Umd10CheckCounter;
    pFuncs->pfnSetTextFilterSize = Bc250Umd10SetTextFilterSize;
}

VOID Bc250UmdPopulateDeviceFuncs11(_Out_ D3D11DDI_DEVICEFUNCS* pFuncs)
{
    if (pFuncs == NULL) return;
    memset(pFuncs, 0, sizeof(*pFuncs));
    /* As primeiras 105 entradas sao identicas em layout a D3D10_1DDI_DEVICEFUNCS */
    Bc250UmdPopulateDeviceFuncs10_1((D3D10_1DDI_DEVICEFUNCS*)pFuncs);
}

/* =========================================================================
 * DXGI Base DDIs
 * ========================================================================= */

HRESULT APIENTRY Bc250DxgiPresent(DXGI_DDI_ARG_PRESENT* pPresentData)
{
    BC250_UMD_DEVICE* device;
    DXGIDDICB_PRESENT presentCb;
    BC250_UMD_RESOURCE* srcRes;
    BC250_UMD_RESOURCE* dstRes;

    if (pPresentData == NULL) {
        return E_INVALIDARG;
    }

    device = (BC250_UMD_DEVICE*)pPresentData->hDevice;
    if (device == NULL) {
        return E_INVALIDARG;
    }

    srcRes = (BC250_UMD_RESOURCE*)pPresentData->hSurfaceToPresent;
    dstRes = (BC250_UMD_RESOURCE*)pPresentData->hDstResource;

    Bc250UmdTrace("DXGI Present: hSurfaceToPresent=%p, hDstResource=%p, Flags=0x%08X, FlipInterval=%d",
        (void*)srcRes, (void*)dstRes,
        pPresentData->Flags.Value,
        (int)pPresentData->FlipInterval);

    if (device->pDXGICallbacks == NULL || device->pDXGICallbacks->pfnPresentCb == NULL) {
        Bc250UmdTrace("DXGI Present: pDXGICallbacks ou pfnPresentCb ausente -> E_FAIL");
        return E_FAIL;
    }

    /*
     * O Present vai pelo CONTEXTO DE APRESENTACAO (Node 0), nao pelo de trabalho
     * (Node 1). Proveniencia completa em bc250_wddm_winsys.h.
     *
     * RECUSA NOMEADA em vez de cair de volta no contexto de trabalho: o fallback
     * reproduziria o estado que a corrida 17:18:35 ja mediu falhando 84/84, e
     * misturaria as duas variaveis. Recusando, "E_FAIL de novo" significa sem
     * ambiguidade "apresentamos pelo Node 0 e ainda assim falhou".
     */
    if (!device->Winsys.PresentContextCreated ||
        device->Winsys.PresentContext == NULL) {
        Bc250UmdTrace("DXGI Present RECUSADO: contexto de apresentacao ausente "
            "(PresentContextCreated=%d). NAO ha fallback para o contexto de "
            "trabalho por decisao -- ver bc250_wddm_winsys.h",
            (int)device->Winsys.PresentContextCreated);
        return E_FAIL;
    }

    memset(&presentCb, 0, sizeof(presentCb));
    presentCb.pDXGIContext = pPresentData->pDXGIContext;
    presentCb.hContext = device->Winsys.PresentContext;

    if (srcRes != NULL && srcRes->Created) {
        presentCb.hSrcAllocation = srcRes->Allocation.KernelAllocation;
    }

    if (dstRes != NULL && dstRes->Created) {
        presentCb.hDstAllocation = dstRes->Allocation.KernelAllocation;
    }

    /*
     * I1 (13/09) -- O RETORNO DESTA CHAMADA NUNCA FOI REGISTRADO, E ERA O NUMERO
     * QUE DECIDIA A CORRIDA.
     *
     * Na corrida 16:28:27 o dwm.exe chamou Present 90 vezes e entrou em laco de
     * device-lost a ~1 Hz (Present -> destroi tudo -> DestroyDevice -> CloseAdapter
     * -> recarrega a DLL -> CreateDevice). Bc250DxgkDdiPresent nao apareceu UMA vez
     * no log do KMD. Com `return callback(...)` direto era impossivel distinguir
     * "o pfnPresentCb falhou" de "ele passou e o dxgkrnl nao nos chamou" -- a mesma
     * ambiguidade que a Lei 4 proibe.
     *
     * Agora a linha LEAVE carrega o hr, as duas alocacoes de kernel, o contexto e
     * as flags. Os dois desfechos informam.
     */
    {
        HRESULT presentHr;
        BOOL srcIsPrimary = (srcRes != NULL) &&
            Bc250WinsysAllocationIsPrimary(&srcRes->Allocation);

        Bc250UmdTrace("DXGI Present ENTER cb: hContext=%p PresentNode=%u WorkNode=%u "
            "hSrcAlloc=0x%x hDstAlloc=0x%x "
            "SrcPrimary=%d SrcDeclaredPrimary=%d SrcRTResource=%p Flags=0x%08X",
            (void*)presentCb.hContext,
            device->Winsys.PresentNodeOrdinal,
            device->Winsys.NodeOrdinal,
            (unsigned)presentCb.hSrcAllocation,
            (unsigned)presentCb.hDstAllocation,
            (int)srcIsPrimary,
            (srcRes != NULL) ? (int)srcRes->Allocation.DeclaredPrimary : -1,
            (srcRes != NULL) ? (void*)srcRes->Allocation.RuntimeResource : NULL,
            pPresentData->Flags.Value);

        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);

        Bc250UmdTrace("DXGI Present LEAVE cb: hr=0x%08X (hr!=0 significa que o "
            "dxgkrnl RECUSOU a apresentacao antes de chamar DxgkDdiPresent; "
            "compare com [Present #] no log do KMD)",
            (unsigned)presentHr);
        return presentHr;
    }
}

HRESULT APIENTRY Bc250DxgiGetGammaCaps(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS* pGammaData)
{
    if (pGammaData == NULL || pGammaData->pGammaCapabilities == NULL) {
        return E_INVALIDARG;
    }
    memset(pGammaData->pGammaCapabilities, 0, sizeof(*pGammaData->pGammaCapabilities));
    pGammaData->pGammaCapabilities->ScaleAndOffsetSupported = FALSE;
    pGammaData->pGammaCapabilities->MaxConvertedValue = 1.0f;
    pGammaData->pGammaCapabilities->MinConvertedValue = 0.0f;
    pGammaData->pGammaCapabilities->NumGammaControlPoints = 0;
    Bc250UmdTrace("DXGI GetGammaCaps: respondendo caps padrao");
    return S_OK;
}

/*
 * F0.2 (13/09) -- ESTA DDI DECLARAVA SUCESSO SEM BACKEND. VIOLACAO DIRETA DA LEI 4.
 *
 * Ate esta faixa o corpo era `log; return S_OK;`, e `pfnSetDisplayModeCb`
 * (d3dumddi.h:4533) nao aparecia em NENHUM lugar da UMD -- grep sobre driver/umd
 * devolvia zero. O DWM chama SetDisplayMode para dizer ao kernel QUAL primaria
 * deve ser varrida; como nunca repassavamos, o kernel seguia com a primaria do
 * boot. Medido na corrida 18:03:43: UM unico SetVidPnSourceAddress, o do GOP
 * (Addr=0xAAC000, hAlloc=0x08), e 96/96 Present com hr=0x80004005.
 *
 * AGRAVANTE, e e' o motivo de esta correcao vir ANTES das outras: o
 * bc250_d3d_umd_test EXIGIA o S_OK (`assert(hr == S_OK)` sobre um struct zerado).
 * O teste congelava a mentira e reprovaria qualquer tentativa de conserto. E' a
 * SEXTA ocorrencia da classe no projeto -- ver a lista em PLANO_PRESENT_E_FAIL.md.
 *
 * ESTADO ATUAL, DELIBERADO: recusa nomeada. O backend real (a chamada a
 * pfnSetDisplayModeCb com o hAllocation do recurso) e' o item 2.3 do plano e
 * ainda NAO existe. Falhar visivel e' melhor que suceder por reimplementacao --
 * mesmo precedente da faixa B3.
 */
HRESULT APIENTRY Bc250DxgiSetDisplayMode(DXGI_DDI_ARG_SETDISPLAYMODE* pDisplayModeData)
{
    BC250_UMD_DEVICE* device;
    BC250_UMD_RESOURCE* res;

    if (pDisplayModeData == NULL) {
        return E_INVALIDARG;
    }

    device = (BC250_UMD_DEVICE*)pDisplayModeData->hDevice;
    res = (BC250_UMD_RESOURCE*)pDisplayModeData->hResource;

    Bc250UmdTrace("DXGI SetDisplayMode ENTER: hDevice=%p hResource=%p SubResourceIndex=%u "
        "hAlloc=0x%x Primary=%d",
        (void*)device, (void*)res, pDisplayModeData->SubResourceIndex,
        (res != NULL) ? (unsigned)res->Allocation.KernelAllocation : 0u,
        (res != NULL) ? (int)res->Allocation.DeclaredPrimary : -1);

    /*
     * F2.3 -- o backend real. Fail-closed em cada pre-condicao, e NENHUMA delas
     * devolve sucesso: o ponto desta DDI e' informar ao kernel qual alocacao
     * passa a ser varrida, e nao ha' como faze-lo "parcialmente".
     */
    if (device == NULL || device->pKTCallbacks == NULL ||
        device->pKTCallbacks->pfnSetDisplayModeCb == NULL) {
        Bc250UmdTrace("DXGI SetDisplayMode RECUSADO: pfnSetDisplayModeCb ausente "
            "(device=%p callbacks=%p)", (void*)device,
            (device != NULL) ? (const void*)device->pKTCallbacks : NULL);
        return E_FAIL;
    }
    if (res == NULL || !res->Created ||
        res->Allocation.KernelAllocation == 0u) {
        Bc250UmdTrace("DXGI SetDisplayMode RECUSADO: recurso sem alocacao de kernel "
            "(res=%p Created=%d hAlloc=0x%x)",
            (void*)res, (res != NULL) ? (int)res->Created : -1,
            (res != NULL) ? (unsigned)res->Allocation.KernelAllocation : 0u);
        return E_FAIL;
    }
    /*
     * Recusar superficie NAO-PRIMARIA e' deliberado. O kernel so' pode varrer o
     * que ele aceitou como primaria; pedir scanout de outra coisa seria um
     * sucesso que o silicio nao honraria.
     */
    if (!res->Allocation.DeclaredPrimary) {
        Bc250UmdTrace("DXGI SetDisplayMode RECUSADO: recurso nao e' primaria declarada "
            "(hAlloc=0x%x Token=0x%llX)",
            (unsigned)res->Allocation.KernelAllocation,
            (unsigned long long)res->Allocation.Token);
        return E_FAIL;
    }

    {
        D3DDDICB_SETDISPLAYMODE args;
        HRESULT hr;

        memset(&args, 0, sizeof(args));
        args.hPrimaryAllocation = res->Allocation.KernelAllocation;

        hr = device->pKTCallbacks->pfnSetDisplayModeCb(
            device->hRTDevice.handle, &args);

        Bc250UmdTrace("DXGI SetDisplayMode LEAVE cb: hr=0x%08X hPrimaryAllocation=0x%x "
            "PrivateDriverFormatAttribute=%u",
            (unsigned)hr, (unsigned)args.hPrimaryAllocation,
            args.PrivateDriverFormatAttribute);
        return hr;
    }
}

HRESULT APIENTRY Bc250DxgiSetResourcePriority(DXGI_DDI_ARG_SETRESOURCEPRIORITY* pPriorityData)
{
    if (pPriorityData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI SetResourcePriority: hResource=%p Priority=%u",
        (void*)pPriorityData->hResource, pPriorityData->Priority);
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiQueryResourceResidency(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY* pResidencyData)
{
    SIZE_T i;
    if (pResidencyData == NULL) {
        return E_INVALIDARG;
    }
    if (pResidencyData->Resources > 0 &&
        (pResidencyData->pResources == NULL || pResidencyData->pStatus == NULL)) {
        return E_INVALIDARG;
    }

    for (i = 0; i < pResidencyData->Resources; ++i) {
        BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)pResidencyData->pResources[i];
        if (res != NULL && res->Created && res->Allocation.KernelAllocation != 0 && res->Allocation.Resident) {
            pResidencyData->pStatus[i] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
        } else {
            pResidencyData->pStatus[i] = DXGI_DDI_RESIDENCY_EVICTED_TO_DISK;
        }
    }
    Bc250UmdTrace("DXGI QueryResourceResidency: Resources=%Iu", pResidencyData->Resources);
    return S_OK;
}

/*
 * F0.2 (13/09) -- MESMA CLASSE DA SetDisplayMode: S_OK sem backend (Lei 4).
 *
 * O corpo era `log; return S_OK;` e nao rotacionava identidade nenhuma. No modelo
 * Flip o runtime chama esta DDI para girar front/back entre os recursos do array
 * (dxgiddi.h:97). Declarar sucesso sem girar significa dizer ao runtime que o
 * mapeamento recurso->backing mudou quando nada mudou.
 *
 * Hoje isto NAO e' a causa do E_FAIL -- a recusa do Present vem antes --, mas
 * seria o proximo travamento garantido assim que o Present passasse.
 *
 * O bc250_d3d_umd_test tambem EXIGIA o S_OK aqui (setima ocorrencia da classe).
 *
 * ESTADO ATUAL, DELIBERADO: recusa nomeada. A rotacao real e' o item 2.4 do
 * plano.
 */
HRESULT APIENTRY Bc250DxgiRotateResourceIdentities(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES* pRotateData)
{
    if (pRotateData == NULL) {
        return E_INVALIDARG;
    }

    Bc250UmdTrace("DXGI RotateResourceIdentities ENTER: hDevice=%p Resources=%u pResources=%p",
        (void*)pRotateData->hDevice, pRotateData->Resources,
        (const void*)pRotateData->pResources);

    if (pRotateData->Resources == 0u) {
        /* Rotacionar zero recursos e' no-op legitimo, nao mentira. */
        Bc250UmdTrace("DXGI RotateResourceIdentities LEAVE: Resources=0, no-op legitimo -> S_OK");
        return S_OK;
    }

    /*
     * F2.4 -- a rotacao real.
     *
     * Semantica do contrato (dxgiddi.h:100): "Array of Resources to rotate
     * identities; 0 <= 1, 1 <= 2, etc." -- o recurso i assume a identidade do
     * recurso i+1, e o ultimo assume a do primeiro.
     *
     * O que E' a identidade aqui: o BACKING. `Allocation` carrega o handle de
     * kernel, a GPUVA, o token, o hRTResource de origem e o estado de residencia;
     * `pCpuShadow` e' a copia de CPU associada aquele backing. Formato e
     * dimensoes NAO rotacionam -- numa cadeia de swap eles sao iguais por
     * construcao, e rotaciona-los mascararia uma cadeia heterogenea em vez de
     * denuncia-la.
     *
     * `Allocation.RuntimeResource` viaja JUNTO com o backing de proposito: a
     * desalocacao e' simetrica por recurso, e se o handle ficasse para tras a
     * alocacao seria devolvida com o hResource errado depois da primeira volta.
     */
    if (pRotateData->pResources == NULL) {
        Bc250UmdTrace("DXGI RotateResourceIdentities RECUSADO: Resources=%u com "
            "pResources NULL", pRotateData->Resources);
        return E_INVALIDARG;
    }

    {
        UINT i;
        BC250_UMD_RESOURCE* first;
        BC250_WINSYS_ALLOCATION savedAllocation;
        void* savedShadow;

        /* Toda a cadeia tem de ser valida ANTES de mover qualquer coisa. */
        for (i = 0; i < pRotateData->Resources; ++i) {
            BC250_UMD_RESOURCE* r = (BC250_UMD_RESOURCE*)pRotateData->pResources[i];
            if (r == NULL || !r->Created) {
                Bc250UmdTrace("DXGI RotateResourceIdentities RECUSADO: recurso[%u]=%p "
                    "invalido ou nao criado -- rotacao ABORTADA sem mover nada",
                    i, (void*)r);
                return E_INVALIDARG;
            }
        }

        first = (BC250_UMD_RESOURCE*)pRotateData->pResources[0];
        savedAllocation = first->Allocation;
        savedShadow = first->pCpuShadow;

        for (i = 0; i + 1u < pRotateData->Resources; ++i) {
            BC250_UMD_RESOURCE* dst = (BC250_UMD_RESOURCE*)pRotateData->pResources[i];
            BC250_UMD_RESOURCE* src = (BC250_UMD_RESOURCE*)pRotateData->pResources[i + 1u];
            dst->Allocation = src->Allocation;
            dst->pCpuShadow = src->pCpuShadow;
        }

        {
            BC250_UMD_RESOURCE* last =
                (BC250_UMD_RESOURCE*)pRotateData->pResources[pRotateData->Resources - 1u];
            last->Allocation = savedAllocation;
            last->pCpuShadow = savedShadow;
        }

        Bc250UmdTrace("DXGI RotateResourceIdentities LEAVE: %u recursos rotacionados; "
            "res[0] agora tem hAlloc=0x%x GpuVa=0x%llX (antes 0x%x)",
            pRotateData->Resources,
            (unsigned)first->Allocation.KernelAllocation,
            (unsigned long long)first->Allocation.GpuVirtualAddress,
            (unsigned)savedAllocation.KernelAllocation);
        return S_OK;
    }
}

HRESULT APIENTRY Bc250DxgiBlt(DXGI_DDI_ARG_BLT* pBltData)
{
    BC250_UMD_DEVICE* device;
    BC250_UMD_RESOURCE* dst;
    BC250_UMD_RESOURCE* src;

    if (pBltData == NULL) {
        return E_INVALIDARG;
    }
    device = (BC250_UMD_DEVICE*)pBltData->hDevice;
    dst = (BC250_UMD_RESOURCE*)pBltData->hDstResource;
    src = (BC250_UMD_RESOURCE*)pBltData->hSrcResource;

    Bc250UmdTrace("DXGI Blt: dst=%p (sub=%u) src=%p (sub=%u) rect=[%u,%u-%u,%u] flags=0x%08X rotate=%d",
        (void*)dst, pBltData->DstSubresource,
        (void*)src, pBltData->SrcSubresource,
        pBltData->DstLeft, pBltData->DstTop, pBltData->DstRight, pBltData->DstBottom,
        pBltData->Flags.Value, (int)pBltData->Rotate);

    if (device == NULL || dst == NULL || src == NULL || !dst->Created || !src->Created) {
        Bc250UmdTrace("DXGI Blt: parametros invalidos ou recursos nao criados -> E_INVALIDARG");
        return E_INVALIDARG;
    }

    return Bc250UmdBltGpu(device, dst, src, pBltData);
}

VOID Bc250UmdPopulateDxgiBaseFuncs(_Out_ DXGI_DDI_BASE_FUNCTIONS* pFuncs)
{
    if (pFuncs == NULL) return;
    memset(pFuncs, 0, sizeof(*pFuncs));
    pFuncs->pfnPresent = Bc250DxgiPresent;
    pFuncs->pfnGetGammaCaps = Bc250DxgiGetGammaCaps;
    pFuncs->pfnSetDisplayMode = Bc250DxgiSetDisplayMode;
    pFuncs->pfnSetResourcePriority = Bc250DxgiSetResourcePriority;
    pFuncs->pfnQueryResourceResidency = Bc250DxgiQueryResourceResidency;
    pFuncs->pfnRotateResourceIdentities = Bc250DxgiRotateResourceIdentities;
    pFuncs->pfnBlt = Bc250DxgiBlt;
}

HRESULT APIENTRY Bc250Umd10CreateDevice(
    D3D10DDI_HADAPTER hAdapter,
    _In_ D3D10DDIARG_CREATEDEVICE* pCreateData)
{
    BC250_UMD_ADAPTER* adapter = (BC250_UMD_ADAPTER*)hAdapter.pDrvPrivate;
    BC250_UMD_DEVICE* device = NULL;
    HRESULT hr;
    BOOL proofAuthorized;

    if (pCreateData == NULL) {
        Bc250UmdTrace("D3D10 CreateDevice: pCreateData=NULL -> E_INVALIDARG");
        return E_INVALIDARG;
    }

    if (adapter == NULL || !adapter->IsActive) {
        Bc250UmdTrace("D3D10 CreateDevice: adapter invalido ou inativo -> E_INVALIDARG");
        return E_INVALIDARG;
    }

    if (pCreateData->hDrvDevice.pDrvPrivate == NULL) {
        Bc250UmdTrace("D3D10 CreateDevice: hDrvDevice.pDrvPrivate=NULL -> E_INVALIDARG");
        return E_INVALIDARG;
    }

    device = (BC250_UMD_DEVICE*)pCreateData->hDrvDevice.pDrvPrivate;

    Bc250UmdTrace("D3D10 CreateDevice: INTERFACE ACORDADA=0x%08X, Version=0x%08X, "
        "hRTDevice=%p, Flags=0x%08X",
        pCreateData->Interface, pCreateData->Version,
        (void*)pCreateData->hRTDevice.handle, pCreateData->Flags);

    /* Initialize device foundation and wire unified winsys */
    hr = Bc250UmdDeviceInit(device, adapter, pCreateData);
    if (FAILED(hr)) {
        Bc250UmdTrace("D3D10 CreateDevice: Bc250UmdDeviceInit falhou -> hr=0x%08X", (unsigned)hr);
        return hr;
    }

    proofAuthorized = Bc250UmdNode1TripodAuthorized();
    if (proofAuthorized) {
        BC250_UMD_NODE1_PROOF_RESULT proof;
        if (InterlockedCompareExchange(&g_Bc250UmdProofTriggered, 1, 0) != 0) {
            Bc250UmdTrace("NODE1_TRIPOD_AUTHORIZED: disparo adicional recusado; single-shot por processo");
            Bc250UmdDeviceTeardown(device);
            return E_NOTIMPL;
        }
        hr = Bc250UmdStartNode1Submission(device, 64u * 1024u);
        if (SUCCEEDED(hr)) {
            hr = Bc250UmdRunNode1ProofTransaction(device, &proof);
            Bc250UmdTrace("NODE1_TRIPOD_RESULT: hr=0x%08X Dst=0x%llX FenceGpuVa=0x%llX FenceObs=0x%llX Poison=%d Submitted=%d Fence=%d Data=%d Cleanup=%d Values=%08X,%08X,%08X,%08X,%08X -- NAO VALIDADO EM HARDWARE",
                (unsigned)hr,
                (unsigned long long)proof.DestinationGpuVa,
                (unsigned long long)proof.FenceGpuVa,
                (unsigned long long)proof.FenceObserved,
                proof.PoisonVerifiedBeforeSubmit, proof.Submitted,
                proof.FenceMatched, proof.DataMatched, proof.CleanupComplete,
                proof.Observed[0], proof.Observed[1], proof.Observed[2],
                proof.Observed[3], proof.Observed[4]);
        } else {
            Bc250UmdTrace("NODE1_TRIPOD_RESULT: start Node1 falhou hr=0x%08X -- NAO VALIDADO EM HARDWARE",
                (unsigned)hr);
        }
        Bc250UmdDeviceTeardown(device);
        /* Instrumentacao nunca transforma o adapter em device D3D funcional. */
        return E_NOTIMPL;
    }

    /* Production gate: without native 3D pipeline on silicon or lab promotion, fail-closed without false capability advertisement */
    if (!Bc250Umd3DPipelineIsDeclared(
            BC250_D3D10_11_NATIVE_READY ? TRUE : FALSE,
            Bc250UmdD3D11LabPromotionEnabled())) {
        Bc250UmdTrace("D3D10 CreateDevice: BC250_D3D10_11_NATIVE_READY=0 -> E_NOTIMPL "
            "(fail-closed: sem pipeline 3D no silicio)");
        Bc250UmdDeviceTeardown(device);
        return E_NOTIMPL;
    }

    /* Este caminho so se torna alcancavel quando o gate de producao for
     * promovido. A promocao futura nao pode publicar um device sem contexto,
     * paging queue e command buffer executavel do Node 1. */
    hr = Bc250UmdStartNode1Submission(device, 64u * 1024u);
    if (FAILED(hr)) {
        Bc250UmdTrace("D3D10 CreateDevice: start Node1 falhou -> hr=0x%08X",
            (unsigned)hr);
        Bc250UmdDeviceTeardown(device);
        return hr;
    }

    /* Popula tabelas de funcoes para D3D10.1 ou D3D11 */
    if (pCreateData->Interface == D3D10_1_DDI_INTERFACE_VERSION) {
        if (pCreateData->p10_1DeviceFuncs != NULL) {
            Bc250UmdPopulateDeviceFuncs10_1(pCreateData->p10_1DeviceFuncs);
        }
    } else if (pCreateData->Interface == D3D11_0_7_DDI_INTERFACE_VERSION ||
               pCreateData->Interface == D3D11_0_DDI_INTERFACE_VERSION) {
        if (pCreateData->p11DeviceFuncs != NULL) {
            Bc250UmdPopulateDeviceFuncs11(pCreateData->p11DeviceFuncs);
        }
    } else {
        /* Fallback: popula p10_1DeviceFuncs */
        if (pCreateData->p10_1DeviceFuncs != NULL) {
            Bc250UmdPopulateDeviceFuncs10_1(pCreateData->p10_1DeviceFuncs);
        }
    }
    if (pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions != NULL) {
        Bc250UmdPopulateDxgiBaseFuncs(pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions);
    }

    return S_OK;
}

HRESULT APIENTRY Bc250Umd10CloseAdapter(D3D10DDI_HADAPTER hAdapter)
{
    BC250_UMD_ADAPTER* adapter = (BC250_UMD_ADAPTER*)hAdapter.pDrvPrivate;

    /*
     * P7_D3D11_CAPS_PROBE v3: a terceira entrada muda do adapter table.
     *
     * Se este log aparecer logo depois do S_OK do OpenAdapter10 e ANTES de uma
     * nova carga da DLL, o runtime desistiu de proposito e fechou o adapter --
     * e a desistencia esta entre CalcPrivateDeviceSize e CreateDevice. Se NAO
     * aparecer, a DLL esta sendo descarregada sem fechar o adapter, o que e'
     * outra historia.
     */
    Bc250UmdTrace("CloseAdapter: adapter=%p, Interface=0x%08X -> S_OK",
        (void*)adapter, (adapter != NULL) ? adapter->Interface : 0u);

    if (adapter != NULL) {
        adapter->IsActive = FALSE;
        adapter->PrivateBlobValid = FALSE;
        free(adapter);
    }
    return S_OK;
}

/*
 * P7_D3D11_CAPS_PROBE -- estado do probe.
 *
 * Lido do registro uma unica vez. Default 0: sem a chave armada, o
 * comportamento e' exatamente o de antes (E_NOTIMPL em OpenAdapter).
 */
/*
 * ITEM ZERO -- consumo do blob DXGKQAITYPE_UMDRIVERPRIVATE.
 *
 * MEDIDO na corrida 2026-09-07 08:25: o KMD recusava Type=0 com
 * STATUS_NOT_SUPPORTED 80 vezes, e nenhuma entrada do adapter table desta UMD
 * era chamada. O KMD passou a publicar um blob versionado nosso; aqui e' o
 * outro lado do handshake.
 *
 * O caminho e' pfnQueryAdapterInfoCb(hRTAdapter, {pPrivateDriverData, size}),
 * que o dxgkrnl encaminha para DxgkDdiQueryAdapterInfo com Type=0.
 *
 * FALHA FECHADO de proposito: blob invalido nao vira "segue mesmo assim". Se o
 * KMD e a UMD discordarem de versao ou tamanho, e' melhor recusar o adapter do
 * que operar sobre campos que nao existem -- foi a licao do probe v1, que
 * declarou suporte a um DDI inexistente e travou a negociacao inteira.
 */
static BOOL Bc250UmdFetchPrivateBlob(
    _In_ CONST D3D10DDIARG_OPENADAPTER* pOpenData,
    _Out_ BC250_UMD_PRIVATE* Blob)
{
    D3DDDICB_QUERYADAPTERINFO arg;
    BC250_UMD_PRIVATE_STATUS status;
    HRESULT hr;

    memset(Blob, 0, sizeof(*Blob));

    if (pOpenData->pAdapterCallbacks == NULL ||
        pOpenData->pAdapterCallbacks->pfnQueryAdapterInfoCb == NULL) {
        Bc250UmdTrace("UMDRIVERPRIVATE: pfnQueryAdapterInfoCb indisponivel");
        return FALSE;
    }

    memset(&arg, 0, sizeof(arg));
    arg.pPrivateDriverData = Blob;
    arg.PrivateDriverDataSize = (UINT)sizeof(*Blob);

    hr = pOpenData->pAdapterCallbacks->pfnQueryAdapterInfoCb(
        pOpenData->hRTAdapter.handle, &arg);
    if (FAILED(hr)) {
        Bc250UmdTrace("UMDRIVERPRIVATE: pfnQueryAdapterInfoCb -> hr=0x%08X (o KMD recusou)",
            (unsigned)hr);
        return FALSE;
    }

    status = Bc250UmdPrivateValidate(Blob, (uint32_t)sizeof(*Blob));
    if (status != BC250_UMD_PRIVATE_OK) {
        Bc250UmdTrace("UMDRIVERPRIVATE: blob recusado na validacao (status=%d)",
            (int)status);
        return FALSE;
    }

    Bc250UmdTrace("UMDRIVERPRIVATE: blob OK -- v%u, %u bytes, Vendor=0x%04X Device=0x%04X, "
        "NodeCount=%u (SDMA=%u, Compute=%u), GpuVaBits=%u, PageSize=%u, "
        "VramSegId=%u VramBytes=0x%llX, GateFlags=0x%08X",
        Blob->Version, Blob->SizeBytes, Blob->VendorId, Blob->DeviceId,
        Blob->NodeCount, Blob->SdmaNodeOrdinal, Blob->ComputeNodeOrdinal,
        Blob->GpuVaBits, Blob->PageSizeBytes,
        Blob->VramSegmentId, (unsigned long long)Blob->VramSizeBytes,
        Blob->GateFlags);

    /*
     * O blob declara o que NAO existe. Registrar aqui deixa explicito no log da
     * corrida por que o CreateDevice vai falhar fechado depois -- em vez de
     * parecer defeito.
     */
    if ((Blob->GateFlags & BC250_UMD_GATE_CAPS_PROBE_ONLY) != 0u) {
        Bc250UmdTrace("UMDRIVERPRIVATE: GateFlags declara CAPS_PROBE_ONLY -- "
            "CreateDevice falhara fechado por decisao, nao por defeito");
    }
    return TRUE;
}

BOOL Bc250UmdCapsProbeEnabled(void)
{
    static LONG s_state = -1; /* -1 = nao lido, 0 = off, 1 = on */
    LONG cached = InterlockedCompareExchange(&s_state, -1, -1);
    HKEY key = NULL;
    DWORD value = 0;
    DWORD size = (DWORD)sizeof(value);
    DWORD type = 0;
    LONG resolved = 0;

    if (cached >= 0) {
        return (cached != 0) ? TRUE : FALSE;
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, BC250_D3D11_CAPS_PROBE_REG_NAME, NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && value != 0) {
            resolved = 1;
        }
        RegCloseKey(key);
    }

    InterlockedExchange(&s_state, resolved);
    Bc250UmdTrace("CAPS_PROBE: %s (HKLM\\%s\\%s)",
        resolved ? "ARMADO" : "desarmado",
        BC250_D3D11_CAPS_PROBE_REG_KEY, BC250_D3D11_CAPS_PROBE_REG_NAME);
    return (resolved != 0) ? TRUE : FALSE;
}

BOOL Bc250UmdD3D11LabPromotionEnabled(void)
{
    static LONG s_state = -1; /* -1 = nao lido, 0 = off, 1 = on */
    LONG cached = InterlockedCompareExchange(&s_state, -1, -1);
    HKEY key = NULL;
    DWORD value = 0;
    DWORD size = (DWORD)sizeof(value);
    DWORD type = 0;
    LONG resolved = 0;

    if (cached >= 0) {
        return (cached != 0) ? TRUE : FALSE;
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, BC250_D3D11_LAB_PROMOTION_REG_NAME, NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && value != 0) {
            resolved = 1;
        }
        RegCloseKey(key);
    }

    InterlockedExchange(&s_state, resolved);
    Bc250UmdTrace("D3D11_LAB_PROMOTION: %s (HKLM\\%s\\%s)",
        resolved ? "ARMADO" : "desarmado",
        BC250_D3D11_CAPS_PROBE_REG_KEY, BC250_D3D11_LAB_PROMOTION_REG_NAME);
    return (resolved != 0) ? TRUE : FALSE;
}

/*
 * F2.2 -- ver a proveniencia completa em bc250_umd_caps_probe.h.
 * Desarmado por padrao DE PROPOSITO: armado, o DXGI troca o caminho inteiro para
 * Blt e a corrida deixaria de medir se as correcoes do caminho de Flip pagaram.
 */
BOOL Bc250UmdNoScanoutOptOutArmed(void)
{
    static LONG s_state = -1; /* -1 = nao lido, 0 = off, 1 = on */
    LONG cached = InterlockedCompareExchange(&s_state, -1, -1);
    HKEY key = NULL;
    DWORD value = 0;
    DWORD size = (DWORD)sizeof(value);
    DWORD type = 0;
    LONG resolved = 0;

    if (cached >= 0) {
        return (cached != 0) ? TRUE : FALSE;
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, BC250_D3D11_NO_SCANOUT_OPTOUT_REG_NAME, NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && value != 0) {
            resolved = 1;
        }
        RegCloseKey(key);
    }

    InterlockedExchange(&s_state, resolved);
    Bc250UmdTrace("D3D11_NO_SCANOUT_OPTOUT: %s (HKLM\\%s\\%s) -- armado faz o DXGI "
        "pedir Blt em vez de Flip",
        resolved ? "ARMADO" : "desarmado",
        BC250_D3D11_CAPS_PROBE_REG_KEY, BC250_D3D11_NO_SCANOUT_OPTOUT_REG_NAME);
    return (resolved != 0) ? TRUE : FALSE;
}

BOOL Bc250UmdDrawBltSubstitutionArmed(void)
{
    static LONG s_state = -1; /* -1 = nao lido, 0 = off, 1 = on */
    LONG cached = InterlockedCompareExchange(&s_state, -1, -1);
    HKEY key = NULL;
    DWORD value = 0;
    DWORD size = (DWORD)sizeof(value);
    DWORD type = 0;
    LONG resolved = 0;

    if (cached >= 0) {
        return (cached != 0) ? TRUE : FALSE;
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, BC250_D3D11_DRAW_BLT_SUBST_REG_NAME, NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && value != 0) {
            resolved = 1;
        }
        RegCloseKey(key);
    }

    InterlockedExchange(&s_state, resolved);
    Bc250UmdTrace("DRAW_BLT_SUBSTITUTION: %s (HKLM\\%s\\%s) -- armado NAO significa "
        "rasterizacao; e' copia 1:1 declarada",
        resolved ? "ARMADO" : "desarmado",
        BC250_D3D11_CAPS_PROBE_REG_KEY, BC250_D3D11_DRAW_BLT_SUBST_REG_NAME);
    return (resolved != 0) ? TRUE : FALSE;
}

BOOL Bc250UmdNode1TripodAuthorized(void)
{
    HKEY key = NULL;
    DWORD armed = 0;
    DWORD armedType = 0;
    DWORD armedSize = sizeof(armed);
    DWORD tokenType = 0;
    DWORD tokenSize = 0;
    char registryToken[BC250_NODE1_TRIPOD_TOKEN_CHARS + 1u];
    char environmentToken[BC250_NODE1_TRIPOD_TOKEN_CHARS + 1u];
    char imagePath[MAX_PATH];
    const char* imageName;
    DWORD environmentLength;
    BOOL authorized = FALSE;

    memset(registryToken, 0, sizeof(registryToken));
    memset(environmentToken, 0, sizeof(environmentToken));
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, BC250_D3D11_CAPS_PROBE_REG_KEY, 0,
            KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    if (RegQueryValueExA(key, BC250_NODE1_TRIPOD_REG_ARMED, NULL,
            &armedType, (LPBYTE)&armed, &armedSize) != ERROR_SUCCESS ||
        armedType != REG_DWORD || armed != 1u) {
        goto done;
    }
    tokenSize = (DWORD)sizeof(registryToken);
    if (RegQueryValueExA(key, BC250_NODE1_TRIPOD_REG_TOKEN, NULL,
            &tokenType, (LPBYTE)registryToken, &tokenSize) != ERROR_SUCCESS ||
        tokenType != REG_SZ || tokenSize == 0u ||
        registryToken[BC250_NODE1_TRIPOD_TOKEN_CHARS] != '\0') {
        goto done;
    }
    environmentLength = GetEnvironmentVariableA(BC250_NODE1_TRIPOD_ENV_TOKEN,
        environmentToken, (DWORD)sizeof(environmentToken));
    if (environmentLength != BC250_NODE1_TRIPOD_TOKEN_CHARS) goto done;
    if (GetModuleFileNameA(NULL, imagePath, ARRAYSIZE(imagePath)) == 0u) goto done;
    imageName = strrchr(imagePath, '\\');
    imageName = (imageName != NULL) ? imageName + 1 : imagePath;
    authorized = Bc250UmdNode1TripodAuthorize(TRUE, imageName,
        registryToken, environmentToken);

done:
    RegCloseKey(key);
    if (authorized) {
        Bc250UmdTrace("NODE1_TRIPOD_AUTHORIZED: processo e token conferidos; single-shot armado");
    }
    return authorized;
}

HRESULT APIENTRY Bc250Umd10GetSupportedVersions(
    D3D10DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions)
{
    static const UINT64 s_SupportedVersions[] = {
        D3D10_0_DDI_SUPPORTED,
        D3D10_1_DDI_SUPPORTED,
        D3D11_0_7_DDI_SUPPORTED,
        D3D11_0_DDI_SUPPORTED
    };
    UINT count = (UINT)(sizeof(s_SupportedVersions) / sizeof(s_SupportedVersions[0]));

    UNREFERENCED_PARAMETER(hAdapter);

    if (!puEntries) {
        return E_INVALIDARG;
    }

    if (!pSupportedDDIInterfaceVersions) {
        /* Primeira passada: o runtime só quer saber quantas versoes existem. */
        Bc250UmdTrace("GetSupportedVersions: sondagem de contagem -> %u", count);
        *puEntries = count;
        return S_OK;
    }

    if (*puEntries < count) {
        Bc250UmdTrace("GetSupportedVersions: buffer de %u < %u necessarias -> E_INVALIDARG",
            *puEntries, count);
        *puEntries = count;
        return E_INVALIDARG;
    }

    for (UINT i = 0; i < count; ++i) {
        pSupportedDDIInterfaceVersions[i] = s_SupportedVersions[i];
        /*
         * P7_D3D11_CAPS_PROBE: registrar o que declaramos suportar e' metade da
         * pergunta 1 do dimensionamento. A outra metade e' o que o runtime faz
         * depois disso -- se ele fecha em D3D10_1 ou desiste.
         */
        Bc250UmdTrace("GetSupportedVersions: declarado[%u]=0x%016llX",
            i, (unsigned long long)s_SupportedVersions[i]);
    }
    *puEntries = count;
    return S_OK;
}

HRESULT APIENTRY Bc250Umd10GetCaps(
    D3D10DDI_HADAPTER hAdapter,
    _In_ CONST D3D10_2DDIARG_GETCAPS* pCapsData)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (!pCapsData || !pCapsData->pData) {
        Bc250UmdTrace("GetCaps: argumento invalido -> E_INVALIDARG");
        return E_INVALIDARG;
    }

    /*
     * P7_D3D11_CAPS_PROBE: este log e' o produto do probe. Ele responde a
     * pergunta 2 do dimensionamento -- QUAIS caps o runtime consulta antes de
     * pedir o device, e com que tamanho de buffer.
     *
     * A resposta continua sendo zero para tudo. Zerar e' honesto: significa
     * "nao suportado". O que estava errado antes era zerar EM SILENCIO, sem
     * registrar o que havia sido perguntado -- e por isso nunca soubemos o que
     * o dwmcore valida.
     */
    /*
     * v4: zerar NAO e' resposta honesta para todo tipo de caps.
     *
     * D3D11DDICAPS_3DPIPELINESUPPORT carrega um D3D11DDI_3DPIPELINELEVEL onde
     * 0 == 3DPIPELINELEVEL_10_0. Zerar ali declararia um pipeline 3D nivel 10_0
     * que este driver nao tem -- cap falso, proibido pela Lei 4. Para esse tipo
     * respondemos FALHA; para conjuntos de flags, zero significa "nada
     * suportado", que e' a verdade.
     *
     * De todo modo, o produto do probe e' o LOG: qual tipo foi perguntado, com
     * que tamanho, em que ordem.
     */
    /*
     * D3D11DDICAPS_3DPIPELINESUPPORT (Type 130):
     * O runtime consulta os niveis de pipeline 3D suportados.
     * Retorna S_OK com 10_0/10_1 quando promovido em laboratorio (D3D11LabPromotion)
     * ou quando o compilador nativo estiver implementado (BC250_D3D10_11_NATIVE_READY).
     * Sem backend e sem promocao, retorna E_NOTIMPL fail-closed honesto.
     */
    if ((UINT)pCapsData->Type == 130u) {
        if (Bc250Umd3DPipelineIsDeclared(
                BC250_D3D10_11_NATIVE_READY ? TRUE : FALSE,
                Bc250UmdD3D11LabPromotionEnabled())) {
            if (pCapsData->DataSize >= sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS)) {
                D3D11DDI_3DPIPELINESUPPORT_CAPS* pCaps = (D3D11DDI_3DPIPELINESUPPORT_CAPS*)pCapsData->pData;
                pCaps->Caps = D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0) |
                              D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1);
                Bc250UmdTrace("GetCaps: Type=130 (D3D11DDICAPS_3DPIPELINESUPPORT) DataSize=%u -> Level 10_0 e 10_1 (Caps=0x%08X)",
                    pCapsData->DataSize, pCaps->Caps);
                return S_OK;
            } else {
                Bc250UmdTrace("GetCaps: Type=130 DataSize=%u < %u -> E_INVALIDARG",
                    pCapsData->DataSize, (UINT)sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS));
                return E_INVALIDARG;
            }
        } else {
            Bc250UmdTrace("GetCaps: Type=130 (D3D11DDICAPS_3DPIPELINESUPPORT) DataSize=%u -> E_NOTIMPL (backend nativo 3D ausente e lab promotion desarmada, fail-closed honesto)",
                pCapsData->DataSize);
            return E_NOTIMPL;
        }
    }

    if (!Bc250UmdCapsZeroIsTruthful((UINT)pCapsData->Type)) {
        Bc250UmdTrace("GetCaps: Type=%u (%s) DataSize=%u pInfo=%s "
            "-> E_NOTIMPL (zerar seria declarar capacidade inexistente)",
            (UINT)pCapsData->Type, Bc250UmdCapsTypeName((UINT)pCapsData->Type),
            pCapsData->DataSize, pCapsData->pInfo ? "presente" : "NULL");
        return E_NOTIMPL;
    }

    Bc250UmdTrace("GetCaps: Type=%u (%s) DataSize=%u pInfo=%s -> zerado (nada suportado, verdade)",
        (UINT)pCapsData->Type, Bc250UmdCapsTypeName((UINT)pCapsData->Type),
        pCapsData->DataSize, pCapsData->pInfo ? "presente" : "NULL");

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

    Bc250UmdTrace("OpenAdapter10_2: Interface=0x%08X, Version=0x%08X",
        pOpenData->Interface, pOpenData->Version);

    /*
     * Fail-closed: a UMD nativa ainda nao implementa o contrato D3D10/11.
     * Delegar para um rasterizador de software esconderia essa lacuna e
     * violaria o objetivo do projeto. O log distingue rejeicao honesta de
     * falha anterior ao carregamento da UMD.
     */
    /*
     * P7_D3D11_CAPS_PROBE: com o probe armado, seguimos adiante para publicar a
     * tabela de adapter e deixar o runtime consultar GetSupportedVersions e
     * GetCaps. pfnCreateDevice CONTINUA falhando fechado -- nao ha device, nao
     * ha render, nao ha promessa. O que muda e' que passamos a saber o que o
     * runtime pergunta antes de desistir.
     */
    /*
     * P7_D3D11_CAPS_PROBE v2 (07/09) -- allowlist de versao de DDI.
     *
     * A v1 aceitava qualquer Interface. Na corrida 2026-09-06 23:45 o runtime
     * pediu 0x000B0011, respondemos S_OK, e ele NAO chamou mais nada: zero
     * GetSupportedVersions, zero GetCaps, zero CreateDevice, e 66 ciclos de
     * carga/descarga da DLL dentro do dwm.exe. O OpenAdapter10 nunca foi
     * chamado -- aceitar o degrau mais alto IMPEDIU o runtime de descer a
     * escada ate o D3D10_1.
     *
     * Alem de inutilizar o probe, aquilo era declaracao de capacidade falsa,
     * proibida pela Lei 4. Agora recusamos o que nao pretendemos responder, e
     * a recusa e' o que permite a negociacao continuar.
     */
    if (Bc250UmdShouldPublishAdapter(BC250_D3D10_11_NATIVE_READY,
            Bc250UmdCapsProbeEnabled() || Bc250UmdD3D11LabPromotionEnabled() || Bc250UmdNode1TripodAuthorized()) &&
        !Bc250UmdProbeAcceptsInterface10_2(pOpenData->Interface)) {
        Bc250UmdTrace("OpenAdapter10_2: Interface=0x%08X fora da allowlist deste entry point "
            "-> E_NOTIMPL (o runtime desce a escada; alvo daqui e' 0x%08X)",
            pOpenData->Interface, BC250_UMD_PROBE_IFACE_D3D11_0_7);
        return E_NOTIMPL;
    }

    if (!Bc250UmdShouldPublishAdapter(BC250_D3D10_11_NATIVE_READY,
            Bc250UmdCapsProbeEnabled() || Bc250UmdD3D11LabPromotionEnabled() || Bc250UmdNode1TripodAuthorized())) {
        Bc250UmdTrace("OpenAdapter10_2: D3D10/11 nativo indisponivel -> E_NOTIMPL");
        return E_NOTIMPL;
    }

    BC250_UMD_PRIVATE blob;
    if (!Bc250UmdFetchPrivateBlob(pOpenData, &blob)) {
        Bc250UmdTrace("OpenAdapter: handshake UMDRIVERPRIVATE falhou -> E_NOTIMPL");
        return E_NOTIMPL;
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
    adapter->PrivateBlob = blob;
    adapter->PrivateBlobValid = TRUE;

    pOpenData->hAdapter.pDrvPrivate = adapter;
    /*
     * v5 (07/09) -- SUSPEITA DE DEFEITO NOSSO, e a mais forte ate agora.
     *
     * MEDIDO nas corridas 09:40 e 10:00: com o handshake UMDRIVERPRIVATE
     * funcionando, e com a tabela de 5 entradas publicada em 0x000B000B com
     * S_OK, o runtime chamou ZERO funcoes -- nem GetSupportedVersions, nem
     * GetCaps, nem CreateDevice, e nem CloseAdapter -- e recarregou a DLL 58
     * vezes. Nem o entry point nem o tamanho da tabela mudaram o desfecho.
     *
     * O header descreve o campo como "pAdapterFuncs_2 POINTS TO a
     * D3D10_2DDI_ADAPTERFUNCS structure", e a doc de inicializacao diz que a
     * tabela e' "returned IN the member". Isso e' a convencao de
     * CREATEDEVICE.pDeviceFuncs, que o header marca "in/out": o RUNTIME aloca a
     * struct e passa o ponteiro; o DRIVER preenche os membros dela.
     *
     * Se e' isso, nosso codigo estava errado: sobrescrevendo o PONTEIRO com a
     * nossa tabela estatica, o runtime continuava lendo a struct DELE, que
     * segue zerada -- tabela de ponteiros nulos. Isso explica exatamente o que
     * medimos: nenhuma funcao chamada, nem CloseAdapter (que tambem seria
     * nulo), e descarte imediato da DLL.
     *
     * Tratamos os dois casos e REGISTRAMOS qual era, para a corrida decidir.
     */
    if (pOpenData->pAdapterFuncs_2 != NULL) {
        Bc250UmdTrace("OpenAdapter10_2: runtime forneceu pAdapterFuncs_2=%p "
            "-> PREENCHENDO a struct dele (5 entradas)",
            (void*)pOpenData->pAdapterFuncs_2);
        *pOpenData->pAdapterFuncs_2 = g_Bc250UmdAdapterFuncs10_2;
    } else {
        Bc250UmdTrace("OpenAdapter10_2: pAdapterFuncs_2 veio NULL "
            "-> publicando ponteiro para a nossa tabela estatica");
        pOpenData->pAdapterFuncs_2 = (D3D10_2DDI_ADAPTERFUNCS*)&g_Bc250UmdAdapterFuncs10_2;
    }

    Bc250UmdTrace("OpenAdapter10_2: Interface=0x%08X, Version=0x%08X -> S_OK (BC250 Native)",
        pOpenData->Interface, pOpenData->Version);
    return S_OK;
}

HRESULT APIENTRY OpenAdapter10(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    BC250_UMD_ADAPTER* adapter = NULL;

    if (!pOpenData) {
        return E_INVALIDARG;
    }

    Bc250UmdTrace("OpenAdapter10: Interface=0x%08X, Version=0x%08X",
        pOpenData->Interface, pOpenData->Version);
    /* P7_D3D11_CAPS_PROBE: ver a nota em OpenAdapter10_2. */
    /*
     * P7_D3D11_CAPS_PROBE v2 (07/09) -- allowlist de versao de DDI.
     *
     * A v1 aceitava qualquer Interface. Na corrida 2026-09-06 23:45 o runtime
     * pediu 0x000B0011, respondemos S_OK, e ele NAO chamou mais nada: zero
     * GetSupportedVersions, zero GetCaps, zero CreateDevice, e 66 ciclos de
     * carga/descarga da DLL dentro do dwm.exe. O OpenAdapter10 nunca foi
     * chamado -- aceitar o degrau mais alto IMPEDIU o runtime de descer a
     * escada ate o D3D10_1.
     *
     * Alem de inutilizar o probe, aquilo era declaracao de capacidade falsa,
     * proibida pela Lei 4. Agora recusamos o que nao pretendemos responder, e
     * a recusa e' o que permite a negociacao continuar.
     */
    if (Bc250UmdShouldPublishAdapter(BC250_D3D10_11_NATIVE_READY,
            Bc250UmdCapsProbeEnabled() || Bc250UmdD3D11LabPromotionEnabled() || Bc250UmdNode1TripodAuthorized()) &&
        !Bc250UmdProbeAcceptsInterface10(pOpenData->Interface)) {
        Bc250UmdTrace("OpenAdapter10: Interface=0x%08X fora da allowlist deste entry point "
            "-> E_NOTIMPL (o runtime desce a escada; alvo daqui e' 0x%08X)",
            pOpenData->Interface, BC250_UMD_PROBE_IFACE_D3D10_1);
        return E_NOTIMPL;
    }

    if (!Bc250UmdShouldPublishAdapter(BC250_D3D10_11_NATIVE_READY,
            Bc250UmdCapsProbeEnabled() || Bc250UmdD3D11LabPromotionEnabled() || Bc250UmdNode1TripodAuthorized())) {
        Bc250UmdTrace("OpenAdapter10: D3D10 nativo indisponivel -> E_NOTIMPL");
        return E_NOTIMPL;
    }

    BC250_UMD_PRIVATE blob;
    if (!Bc250UmdFetchPrivateBlob(pOpenData, &blob)) {
        Bc250UmdTrace("OpenAdapter: handshake UMDRIVERPRIVATE falhou -> E_NOTIMPL");
        return E_NOTIMPL;
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
    adapter->PrivateBlob = blob;
    adapter->PrivateBlobValid = TRUE;

    pOpenData->hAdapter.pDrvPrivate = adapter;
    /* v5: ver a nota extensa em OpenAdapter10_2. Mesmo tratamento. */
    if (pOpenData->pAdapterFuncs != NULL) {
        Bc250UmdTrace("OpenAdapter10: runtime forneceu pAdapterFuncs=%p "
            "-> PREENCHENDO a struct dele (3 entradas)",
            (void*)pOpenData->pAdapterFuncs);
        *pOpenData->pAdapterFuncs = g_Bc250UmdAdapterFuncs10;
    } else {
        Bc250UmdTrace("OpenAdapter10: pAdapterFuncs veio NULL "
            "-> publicando ponteiro para a nossa tabela estatica");
        pOpenData->pAdapterFuncs = (D3D10DDI_ADAPTERFUNCS*)&g_Bc250UmdAdapterFuncs10;
    }

    Bc250UmdTrace("OpenAdapter10: Interface=0x%08X, Version=0x%08X -> S_OK",
        pOpenData->Interface, pOpenData->Version);
    return S_OK;
}

HRESULT APIENTRY OpenAdapter11(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData)
{
    if (!pOpenData) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("OpenAdapter11: Interface=0x%08X, Version=0x%08X",
        pOpenData->Interface, pOpenData->Version);
    Bc250UmdTrace("OpenAdapter11: D3D11 nativo indisponivel -> E_NOTIMPL");
    return E_NOTIMPL;
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
    if (!pOpenData) {
        return E_INVALIDARG;
    }

    /* Nao anunciar um adaptador cujo CreateDevice necessariamente falha. */
    Bc250UmdTrace("OpenAdapter12: D3D12 nao implementado nativamente -> E_NOTIMPL");
    return E_NOTIMPL;
}

HRESULT APIENTRY OpenAdapter(PVOID pOpenData)
{
    UNREFERENCED_PARAMETER(pOpenData);
    Bc250UmdTrace("OpenAdapter (D3D9 probe) -> E_NOTIMPL");
    return E_NOTIMPL;
}
