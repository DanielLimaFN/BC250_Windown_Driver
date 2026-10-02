#include "bc250_d3d_umd.h"
#include "bc250_umd_state_translate.h"
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
static volatile LONG g_Bc250UmdProofTriggered = 0;

/*
 * T4.4 (30/09) -- UMA sequencia de token por PROCESSO, nao por carga da DLL.
 *
 * MEDIDO na Corrida 183: o runtime carregou a bc250umd.dll 177 vezes dentro do
 * StartMenuExperienceHost.exe (pid 6992). Os tres contadores eram estaticos da
 * DLL e voltavam a 1 a cada carga; o token 0x524553001B50000D foi emitido 176
 * vezes enquanto alocacoes de cargas anteriores continuavam vivas no KMD. O KMD
 * procura a alocacao por token em DxgkDdiOpenAllocation e achava a ANTIGA:
 *   172x [ActiveOpenAlloc] RECUSADO: size/alignment mismatch rec=1638400 req=8192
 *        (a textura compartilhada 32x32 do menu nunca foi criada; E_INVALIDARG);
 *   2406x STATUS_OBJECT_NAME_COLLISION no device do dwm.exe (a alocacao
 *        "nova" resolvia para uma que o device ja tinha aberto).
 *
 * A sequencia agora e' unica e retomada: DllMain le BC250_UMD_TOKEN_SEQUENCE
 * do ambiente DO PROPRIO PROCESSO no attach e grava no detach por FreeLibrary.
 * O ambiente sobrevive ao descarregamento da DLL e nao e' visivel a outros
 * processos (o pid ja esta no token). Divida declarada: os 16 bits baixos
 * ainda dao a volta apos 65.536 alocacoes no mesmo processo.
 */
#define BC250_UMD_TOKEN_SEQUENCE_ENV "BC250_UMD_TOKEN_SEQUENCE"
static volatile LONG g_Bc250UmdTokenSequence = 0;

static ULONG Bc250UmdNextTokenSequence(void)
{
    ULONG sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdTokenSequence);
    if ((sequence & 0xFFFFu) == 0u) {
        sequence = (ULONG)InterlockedIncrement(&g_Bc250UmdTokenSequence);
    }
    return sequence;
}

static LONG Bc250UmdRestoreTokenSequence(void)
{
    char text[16];
    LONG value = 0;
    DWORD length = GetEnvironmentVariableA(BC250_UMD_TOKEN_SEQUENCE_ENV, text, (DWORD)sizeof(text));
    if (length > 0u && length < sizeof(text) && Bc250UmdTokenSequenceParse(text, &value)) {
        InterlockedExchange(&g_Bc250UmdTokenSequence, value);
        return value;
    }
    return 0;
}

static void Bc250UmdPersistTokenSequence(void)
{
    char text[16];
    if (Bc250UmdTokenSequenceFormat(InterlockedCompareExchange(&g_Bc250UmdTokenSequence, 0, 0),
            text, sizeof(text))) {
        (void)SetEnvironmentVariableA(BC250_UMD_TOKEN_SEQUENCE_ENV, text);
    }
}

static uint64_t Bc250UmdNextCommandToken(void)
{
    ULONG sequence = Bc250UmdNextTokenSequence();
    return ((uint64_t)0x434D4400u << 32) |
        ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
        (uint64_t)(sequence & 0xFFFFu);
}

static uint64_t Bc250UmdNextProofToken(void)
{
    ULONG sequence = Bc250UmdNextTokenSequence();
    return ((uint64_t)0x50524600u << 32) |
        ((uint64_t)(GetCurrentProcessId() & 0xFFFFu) << 16) |
        (uint64_t)(sequence & 0xFFFFu);
}

static uint64_t Bc250UmdNextResourceToken(BOOL isPrimary, BOOL isStaging)
{
    ULONG sequence = Bc250UmdNextTokenSequence();
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
    if (Reason == DLL_PROCESS_ATTACH) {
        LONG resumed;
        (void)DisableThreadLibraryCalls(Instance);
        resumed = Bc250UmdRestoreTokenSequence();
        Bc250UmdTrace("=== bc250umd.dll CARREGADA ===");
        Bc250UmdTrace("T4.4 TOKEN_SEQUENCE retomada em 0x%08lX (0 = primeira carga neste processo)",
            (unsigned long)resumed);
    } else if (Reason == DLL_PROCESS_DETACH && Reserved == NULL) {
        /* FreeLibrary: a proxima carga neste processo continua daqui. Com o
         * processo terminando (Reserved != NULL) nao ha proxima carga. */
        Bc250UmdPersistTokenSequence();
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
    InitializeSRWLock(&pDevice->DeviceLock);
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

    /* Initialize Vulkan Graphics Translation Bridge if kernel callbacks are operational */
    if (pCreateData->pKTCallbacks != NULL &&
        pCreateData->pKTCallbacks->pfnCreateContextVirtualCb != NULL &&
        Bc250UmdVkBridgeInit(&pDevice->VkBridge)) {
        pDevice->VkBridgeInitialized = TRUE;
        /* DX.49: todo flush fecha o render pass aberto por um clear. */
        pDevice->VkBridge.pRenderPassOpen = &pDevice->InRenderPass;
        if (Bc250UmdPipelineCacheInit(&pDevice->PipelineCache, pDevice)) {
            pDevice->PipelineCacheInitialized = TRUE;
        }
        Bc250UmdTrace("Bc250UmdDeviceInit: Vulkan Graphics Bridge e Pipeline Cache INICIALIZADOS com sucesso");
    } else {
        Bc250UmdTrace("Bc250UmdDeviceInit: Vulkan Graphics Bridge nao disponivel (fail-closed)");
    }

    Bc250UmdTrace("Bc250UmdDeviceInit: device=%p inicializado com sucesso (Interface=0x%08X, Version=0x%08X, Winsys=OK, VkBridge=%d)",
        (void*)pDevice, pDevice->Interface, pDevice->Version, (int)pDevice->VkBridgeInitialized);
    return S_OK;
}

/* SDMA GPU Blt implementation */
#include "bc250_umd_blt.inl"

VOID Bc250UmdDeviceTeardown(
    _Inout_ BC250_UMD_DEVICE* pDevice)
{
    UINT64 hangSpan;
    if (pDevice == NULL || pDevice->TeardownActive) {
        return;
    }

    hangSpan = Bc250WinsysHangSpan();
    Bc250WinsysHangEvent("DEVICE_TEARDOWN", "ENTER", pDevice, hangSpan,
        (UINT64)(ULONG_PTR)&pDevice->Winsys, 0u, S_OK);

    /* DX.82: Marca imediatamente como em teardown para que operacoes concorrentes retornem fail-closed */
    pDevice->TeardownActive = TRUE;
    pDevice->DeviceActive = FALSE;
    pDevice->VkBridge.DeviceActive = FALSE;

    /* DX.82: Bloqueia com lock exclusivo para que chamadas em andamento (WaitPaging, CreateResource) terminem */
    Bc250WinsysHangEvent("LOCK_EXCLUSIVE", "WAIT", pDevice, hangSpan, 0u, 0u, S_OK);
    AcquireSRWLockExclusive(&pDevice->DeviceLock);
    Bc250WinsysHangEvent("LOCK_EXCLUSIVE", "ACQUIRED", pDevice, hangSpan, 0u, 0u, S_OK);

    Bc250UmdTrace("Bc250UmdDeviceTeardown: device=%p iniciando teardown seguro", (void*)pDevice);

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

    if (pDevice->InRenderPass && pDevice->VkBridgeInitialized) {
        pDevice->VkBridge.vk.vkCmdEndRenderPass(pDevice->VkBridge.CommandBuffer);
        pDevice->InRenderPass = FALSE;
    }
    if (pDevice->PipelineCacheInitialized) {
        Bc250UmdPipelineCacheDestroy(&pDevice->PipelineCache);
        pDevice->PipelineCacheInitialized = FALSE;
    }
    if (pDevice->VkBridgeInitialized) {
        Bc250UmdVkBridgeShutdown(&pDevice->VkBridge);
        pDevice->VkBridgeInitialized = FALSE;
    }

    pDevice->NativeNode1Ready = FALSE;
    Bc250UmdTrace("Bc250UmdDeviceTeardown: device=%p concluido", (void*)pDevice);
    Bc250WinsysHangEvent("LOCK_EXCLUSIVE", "RELEASE", pDevice, hangSpan, 0u, 0u, S_OK);
    ReleaseSRWLockExclusive(&pDevice->DeviceLock);
    Bc250WinsysHangEvent("DEVICE_TEARDOWN", "LEAVE", pDevice, hangSpan, 0u, 0u, S_OK);
}

/* Vulkan Resource and Pipeline Cache translation implementations */
#include "bc250_umd_resource_vk.c"
#include "bc250_umd_pipeline_cache.c"
/*
 * DX.54 -- VELOCIDADE (Corrida 98R, P0 de tempo no log do dwm.exe): ~14-24 ms
 * por draw entre o upload do CB e "[VK_DESCRIPTOR] BOUND", onde so' ha a
 * criacao de view, sampler e de um VkDescriptorPool novo por draw. Ligado: um
 * pool por device, zerado depois de fence concluida, e samplers em cache. Cada
 * peca da DX.54 tem interruptor proprio para bisseccao.
 */
static const BOOL s_Bc250Dx54PersistentDescriptors = TRUE;
/* DX.54: inspecao do buffer linear no Present amostrada (ver o Present). */
static const BOOL s_Bc250Dx54SampledPresentInspection = TRUE;
#include "bc250_umd_descriptors.c"
#include "bc250_umd_state_translate.c"

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

    if (device->VkBridgeInitialized) {
        if (device->InRenderPass) {
            device->VkBridge.vk.vkCmdEndRenderPass(device->VkBridge.CommandBuffer);
            device->InRenderPass = FALSE;
        }
        (void)Bc250UmdVkFlushCommands(&device->VkBridge);
    }

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

/*
 * Menu Iniciar (Corrida 160, doc 516): um PS SM4.0 de 89 tokens falhou no
 * vkd3d-shader (status=3) 29 vezes, sem mensagem. Na falha, registra o
 * resultado e as mensagens do vkd3d (por thread, adaptador) e grava UMA vez por
 * shader distinto os tokens e as assinaturas em
 * %TEMP%\BC250_SHADER_FAIL_<pid>_<fnv32>.bin, para reproduzir a traducao offline.
 * Formato (uint32 LE): 'BCSF', versao 1, tipo (0 token stream, 1 DXBC),
 * bytes do codigo, n entradas ISGN, n entradas OSGN, codigo, entradas
 * D3D10DDIARG_SIGNATURE_ENTRY cruas (12 bytes cada). So' leitura do shader.
 */
static volatile LONG s_Bc250ShaderFailSeen[16];
static volatile LONG s_Bc250ShaderFailSeenCount;

static void Bc250UmdReportShaderFailure(
    _In_ CONST UINT* pShaderCode, UINT CodeBytes, UINT Kind,
    _In_opt_ CONST D3D10DDIARG_STAGE_IO_SIGNATURES* pSignatures)
{
    int vkd3dResult = 0;
    const char *messages = Bc250Vkd3dShaderAdapterLastMessages(&vkd3dResult);
    UINT hash = 2166136261u, i, numIn = 0, numOut = 0;
    const BYTE *bytes = (const BYTE *)pShaderCode;
    LONG slot, seen;
    char path[MAX_PATH];
    DWORD tempLen;
    HANDLE file;

    Bc250UmdTrace("SHADER_TRANSLATOR: vkd3d resultado=%d mensagens=[%s]",
        vkd3dResult, (messages && messages[0]) ? messages : "(nenhuma)");
    /* O tamanho vem do proprio token stream; um valor absurdo nao pode fazer
     * ler memoria alem do shader. Shaders reais do desktop tem poucos KB. */
    if (pShaderCode == NULL || CodeBytes == 0u || CodeBytes > (1u << 20)) {
        Bc250UmdTrace("SHADER_TRANSLATOR: shader recusado NAO gravado (tamanho %u fora de faixa)", CodeBytes);
        return;
    }
    for (i = 0; i < CodeBytes; ++i) { hash ^= bytes[i]; hash *= 16777619u; }
    seen = InterlockedCompareExchange(&s_Bc250ShaderFailSeenCount, 0, 0);
    for (slot = 0; slot < seen && slot < 16; ++slot) {
        if ((UINT)s_Bc250ShaderFailSeen[slot] == hash) return;
    }
    slot = InterlockedIncrement(&s_Bc250ShaderFailSeenCount) - 1;
    if (slot >= 16) return;
    s_Bc250ShaderFailSeen[slot] = (LONG)hash;

    tempLen = GetTempPathA((DWORD)sizeof(path), path);
    if (tempLen == 0u || tempLen > sizeof(path) - 48u) return;
    _snprintf_s(path + tempLen, sizeof(path) - tempLen, _TRUNCATE,
        "BC250_SHADER_FAIL_%lu_%08X.bin", GetCurrentProcessId(), hash);
    if (pSignatures != NULL) {
        numIn = pSignatures->pInputSignature ? pSignatures->NumInputSignatureEntries : 0u;
        numOut = pSignatures->pOutputSignature ? pSignatures->NumOutputSignatureEntries : 0u;
    }
    /*
     * SHADER.2 (Corrida 161): a DLL recarrega a cada device, a deduplicacao
     * acima zera, e CREATE_ALWAYS reescrevia o arquivo -- a ultima reescrita
     * (19:11:31) ficou no cache e a trava de 19:11:33 deixou 452 bytes ZERO.
     * CREATE_NEW preserva o primeiro arquivo, e FlushFileBuffers o leva ao disco.
     */
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS)
            Bc250UmdTrace("SHADER_TRANSLATOR: shader recusado ja gravado em %s (hash=%08X) -- mantido", path, hash);
        else
            Bc250UmdTrace("SHADER_TRANSLATOR: nao foi possivel gravar %s (erro=%lu)", path, err);
        return;
    }
    {
        UINT header[6];
        DWORD written = 0;
        header[0] = 0x46534342u; /* "BCSF" */
        header[1] = 1u;
        header[2] = Kind;
        header[3] = CodeBytes;
        header[4] = numIn;
        header[5] = numOut;
        (void)WriteFile(file, header, (DWORD)sizeof(header), &written, NULL);
        (void)WriteFile(file, pShaderCode, CodeBytes, &written, NULL);
        if (numIn) (void)WriteFile(file, pSignatures->pInputSignature,
            (DWORD)(numIn * sizeof(D3D10DDIARG_SIGNATURE_ENTRY)), &written, NULL);
        if (numOut) (void)WriteFile(file, pSignatures->pOutputSignature,
            (DWORD)(numOut * sizeof(D3D10DDIARG_SIGNATURE_ENTRY)), &written, NULL);
    }
    (void)FlushFileBuffers(file);
    CloseHandle(file);
    Bc250UmdTrace("SHADER_TRANSLATOR: shader recusado gravado em %s (hash=%08X, %u bytes, ISGN=%u OSGN=%u)",
        path, hash, CodeBytes, numIn, numOut);
}

static CONST BC250_DXBC_SPIRV_TRANSLATOR* Bc250UmdShaderTranslator(void)
{
    LONG cached = InterlockedCompareExchange(&s_Bc250ShaderTranslatorState, -1, -1);
    wchar_t path[MAX_PATH];

    if (cached == 1) return &s_Bc250ShaderTranslator;
    if (cached == 0) return NULL;

    if (!Bc250UmdShaderTranslatorReadPath(path, (DWORD)(sizeof(path) / sizeof(path[0])))) {
        wcscpy_s(path, sizeof(path)/sizeof(path[0]), L"C:\\BC250_LIB\\libvkd3d-shader-1.dll");
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            InterlockedExchange(&s_Bc250ShaderTranslatorState, 0);
            Bc250UmdTrace("SHADER_TRANSLATOR: desarmado (HKLM\\%s\\%s + %s) e fallback C:\\BC250_LIB ausente",
                BC250_D3D11_CAPS_PROBE_REG_KEY,
                BC250_D3D11_SHADER_TRANSLATOR_REG_NAME,
                BC250_D3D11_SHADER_TRANSLATOR_PATH_REG_NAME);
            return NULL;
        }
        Bc250UmdTrace("SHADER_TRANSLATOR: ativo via biblioteca padrao '%ls'", path);
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
            Bc250UmdReportShaderFailure(pShaderCode, rawHdr->TotalSize, 1u, pSignatures);
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
            Bc250UmdReportShaderFailure(pShaderCode,
                pShaderCode[1] * (UINT)sizeof(UINT), 0u, pSignatures);
            return;
        }
    }

    pShader->SpirvValid = TRUE;
    Bc250UmdTrace("SHADER_TRANSLATOR: %s -> SPIR-V %u bytes (estagio=%d). "
        "Traducao NAO e' pipeline: descriptors, render target, anel grafico e Draw seguem ausentes.",
        (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) ? "DXBC" : "TokenStream",
        pShader->Spirv.SizeBytes, (int)pShader->Spirv.Stage);
}

/*
 * I.1 (30/09, doc 545 §5): identidade do shader no log. FNV-1a 32 bits sobre o
 * stream de tokens inteiro (pShaderCode[1] = comprimento em dwords). Sem isso o
 * inventario so' distinguia shaders pelo numero de tokens. Comprimento fora de
 * faixa devolve 0 e o log diz Hash=00000000.
 */
static ULONG Bc250UmdShaderTokenHash(_In_opt_ CONST UINT* pShaderCode)
{
    ULONG hash = 2166136261u;
    UINT dwords;
    UINT i;
    if (pShaderCode == NULL) return 0u;
    /* Contêiner DXBC: [1..4] e' o digest; o tamanho total em bytes esta no dword 6. */
    dwords = (pShaderCode[0] == BC250_DXBC_MAGIC_DXBC) ? (pShaderCode[6] / 4u) : pShaderCode[1];
    if (dwords < 2u || dwords > (1u << 20)) return 0u;
    for (i = 0; i < dwords; ++i) {
        UINT value = pShaderCode[i];
        UINT b;
        for (b = 0; b < 4u; ++b) {
            hash ^= (value >> (8u * b)) & 0xFFu;
            hash *= 16777619u;
        }
    }
    return hash;
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

    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateShaderModule(device, shader);
    }

    Bc250UmdTrace("CreateVertexShader: shader=%p SM=%u.%u tokens=%u hRT=%p VkModule=%d Hash=%08lX",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle, (int)shader->VkModuleCreated,
        Bc250UmdShaderTokenHash(pShaderCode));
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

    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateShaderModule(device, shader);
    }

    Bc250UmdTrace("CreatePixelShader: shader=%p SM=%u.%u tokens=%u hRT=%p VkModule=%d Hash=%08lX",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle, (int)shader->VkModuleCreated,
        Bc250UmdShaderTokenHash(pShaderCode));
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

    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateShaderModule(device, shader);
    }

    Bc250UmdTrace("CreateGeometryShader: shader=%p SM=%u.%u tokens=%u hRT=%p VkModule=%d",
        (void*)shader, shader->MajorVersion, shader->MinorVersion,
        shader->OpcodeTokenCount, (void*)hRTShader.handle, (int)shader->VkModuleCreated);
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

    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateShaderModule(device, shader);
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

    Bc250UmdTrace("DestroyShader: shader=%p", (void*)shader);
    if (shader != NULL) {
        if (device != NULL && device->VkBridgeInitialized) {
            Bc250UmdVkDestroyShaderModule(device, shader);
        }
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

static DXGI_FORMAT Bc250D3dddiFormatToDxgiFormat(UINT Format)
{
    switch (Format) {
    case 21u: /* D3DDDIFMT_A8R8G8B8 */
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case 22u: /* D3DDDIFMT_X8R8G8B8 */
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case 28u: /* D3DDDIFMT_A8B8G8R8 */
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case 35u: /* D3DDDIFMT_A2B10G10R10 */
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case 23u: /* D3DDDIFMT_R5G6B5 */
        return DXGI_FORMAT_B5G6R5_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
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
        /* Formato desconhecido nao pode virar implicitamente RGBA8. */
        return 0u;
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

/* DX.59: consulta exata por token, sem alterar o resultado de pfnAllocateCb. */
static void Bc250UmdProbeAllocationCallback(
    BC250_UMD_DEVICE *device,
    UINT64 token,
    HRESULT allocateHr,
    UINT bindFlags,
    UINT miscFlags)
{
    static volatile LONG s_AllocationQueryCount = 0;
    BC250_ESCAPE_ALLOCATION_QUERY payload;
    D3DDDICB_ESCAPE args;
    HRESULT escapeHr;
    BOOL replyValid;
    LONG queryNumber = InterlockedIncrement(&s_AllocationQueryCount);
    /* A primeira amostra captura o controle; as seguintes cobrem a recusa
     * sem acrescentar centenas de escapes ao caminho do Explorer. */
    if (queryNumber > 16L && (queryNumber % 32L) != 0L)
        return;
    if (device == NULL || device->pAdapter == NULL ||
        device->pAdapter->hRTAdapter.handle == NULL ||
        device->Winsys.Callbacks == NULL ||
        device->Winsys.Callbacks->pfnEscapeCb == NULL) {
        Bc250UmdTrace("DX59_ALLOC_QUERY INDISPONIVEL: Token=0x%llX hr=0x%08X",
            (unsigned long long)token, (unsigned)allocateHr);
        return;
    }
    memset(&payload, 0, sizeof(payload));
    payload.Opcode = BC250_ESCAPE_OP_ALLOCATION_QUERY;
    payload.Version = BC250_ESCAPE_ALLOCATION_QUERY_VERSION;
    payload.Token = token;
    memset(&args, 0, sizeof(args));
    args.hDevice = device->Winsys.RuntimeDevice;
    args.pPrivateDriverData = &payload;
    args.PrivateDriverDataSize = (UINT)sizeof(payload);
    escapeHr = device->Winsys.Callbacks->pfnEscapeCb(
        device->pAdapter->hRTAdapter.handle, &args);
    replyValid = SUCCEEDED(escapeHr) &&
        payload.Opcode == BC250_ESCAPE_OP_ALLOCATION_QUERY &&
        payload.Version == BC250_ESCAPE_ALLOCATION_QUERY_VERSION &&
        payload.Token == token;
    Bc250UmdTrace("DX59_ALLOC_QUERY Token=0x%llX AllocateHr=0x%08X EscapeHr=0x%08X ReplyValid=%u "
        "Seen=%u KmdStatus=0x%08X Seq=%u Resource=%u Flags=0x%08X Priority=%u "
        "SegSet=0x%X Preferred=0x%X Align=%u Size=%llu hAlloc=%u hResource=%u "
        "Bind=0x%X Misc=0x%X OpenCalls=%u OpenStatus=0x%08X DestroyCalls=%u DestroyStatus=0x%08X",
        (unsigned long long)token, (unsigned)allocateHr, (unsigned)escapeHr,
        (unsigned)replyValid,
        payload.Seen, payload.KmdStatus, payload.Sequence,
        payload.FlagsResource, payload.AllocationFlags, payload.AllocationPriority,
        payload.WriteSegmentSet, payload.PreferredSegment, payload.Alignment,
        (unsigned long long)payload.SizeBytes, payload.KmdAllocationNonNull,
        payload.KmdResourceNonNull, bindFlags, miscFlags,
        payload.OpenCalls, payload.OpenLastStatus,
        payload.DestroyCalls, payload.DestroyLastStatus);
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
    UINT64 hangSpan = Bc250WinsysHangSpan();

    if (res == NULL || pCreate == NULL) {
        Bc250UmdTrace("CreateResource: argumento invalido (res=%p, create=%p)",
            (void*)res, (void*)pCreate);
        return;
    }

    if (device != NULL) {
        if (device->TeardownActive) {
            Bc250UmdTrace("CreateResource RECUSADO: device em teardown (res=%p)", (void*)res);
            return;
        }
        Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "WAIT", device, hangSpan,
            (UINT64)(ULONG_PTR)res, (UINT64)(ULONG_PTR)&device->Winsys, S_OK);
        AcquireSRWLockShared(&device->DeviceLock);
        Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "ACQUIRED", device, hangSpan,
            (UINT64)(ULONG_PTR)res, 0u, S_OK);
        if (device->TeardownActive) {
            Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "RELEASE", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, S_OK);
            ReleaseSRWLockShared(&device->DeviceLock);
            Bc250UmdTrace("CreateResource ABORTADO: device em teardown (res=%p)", (void*)res);
            return;
        }
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

    /*
     * F5.0 -- ESTREITAR A DERIVACAO DE PRIMARIEDADE (R9, R10).
     *
     * D3D10_DDI_BIND_PRESENT significa "pode ser apresentada", NAO "e' a primaria
     * de scanout". O Explorer.EXE aloca texturas/buffers de 1024x832 com BIND_PRESENT
     * para a barra de tarefas e papel de parede; se declararmos Flags.Primary=1 ao
     * kernel para elas, o dxgkrnl recusa com E_INVALIDARG (0x80070057) porque a
     * geometria diverge do modo ativo da fonte VidPn (2560x1080).
     *
     * Regra estrita: BIND_PRESENT NUNCA implica primaria por si so'.
     * Primaria exige pPrimaryDesc != NULL E geometria compativel com o modo da fonte.
     * Divergencia gera recusa nomeada; pPrimaryDesc ausente nunca e' primaria.
     */
    if (pCreate->pPrimaryDesc != NULL) {
        if (pCreate->pPrimaryDesc->ModeDesc.Width != 0u &&
            pCreate->pPrimaryDesc->ModeDesc.Height != 0u &&
            res->Width == pCreate->pPrimaryDesc->ModeDesc.Width &&
            res->Height == pCreate->pPrimaryDesc->ModeDesc.Height) {
            isPrimary = TRUE;
            alignment = 65536u;
            totalSize = (UINT)(((uint64_t)totalSize + (alignment - 1u)) & ~(alignment - 1u));
            res->TotalByteSize = totalSize;
            Bc250UmdTrace("PRIMARY_DESC VALIDO: res=%ux%u compativel com mode=%ux%u VidPnSourceId=%u -> Primary=1",
                res->Width, res->Height, pCreate->pPrimaryDesc->ModeDesc.Width, pCreate->pPrimaryDesc->ModeDesc.Height,
                (unsigned)pCreate->pPrimaryDesc->VidPnSourceId);
        } else {
            Bc250UmdTrace("PRIMARY_DESC INCOMPATIVEL RECUSADO: res=%ux%u != mode=%ux%u VidPnSourceId=%u -> Primary=0 (recusa nomeada)",
                res->Width, res->Height, pCreate->pPrimaryDesc->ModeDesc.Width, pCreate->pPrimaryDesc->ModeDesc.Height,
                (unsigned)pCreate->pPrimaryDesc->VidPnSourceId);
        }
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
        binding.VidPnSourceId = (pCreate->pPrimaryDesc != NULL) ? pCreate->pPrimaryDesc->VidPnSourceId : 0u;
        /* DX.89: recurso compartilhado (Explorer/DComp) -- ver bc250_wddm_winsys.h. */
        binding.Shared = (pCreate->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0u ? TRUE : FALSE;
        /*
         * M1 (doc 491): so' GPU via GPUVA. Fora: primaria, staging/dynamic (outra
         * classe de token), render target, depth, UAV, decoder, e qualquer MISC
         * que envolva outro processo ou o present -- SHARED (0x2),
         * DISCARD_ON_PRESENT (0x8), KEYEDMUTEX (0x100), GDI_COMPATIBLE (0x200),
         * REMOTE (0x400), RESTRICTED (0x800/0x1000), CROSS_ADAPTER (0x2000).
         * Esses sao os que o KMD pode enderecar por MC (composicao, Present,
         * scanout); os restantes -- texturas so' de leitura e buffers -- sao a
         * maior parte das alocacoes do Explorer (238 gradientes 1024x1 na 140).
         */
        binding.GpuVaOnly =
            (!isPrimary && !isStaging &&
             (pCreate->BindFlags & (D3D10_DDI_BIND_RENDER_TARGET |
                                    D3D10_DDI_BIND_DEPTH_STENCIL |
                                    D3D11_DDI_BIND_UNORDERED_ACCESS |
                                    D3D11_DDI_BIND_DECODER)) == 0u &&
             (pCreate->MiscFlags & (0x2u | 0x8u | 0x100u | 0x200u | 0x400u |
                                    0x800u | 0x1000u | 0x2000u)) == 0u) ? TRUE : FALSE;

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
        if (res->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D &&
            res->Width == 1024u && res->Height == 832u) {
            Bc250UmdProbeAllocationCallback(device, token,
                device->Winsys.LastCallbackHr, pCreate->BindFlags,
                pCreate->MiscFlags);
        }
        if (wsStatus == BC250_WINSYS_OK)
            wsStatus = Bc250WinsysMapAllocation(&device->Winsys, &res->Allocation, FALSE);
        if (wsStatus == BC250_WINSYS_OK)
            wsStatus = Bc250WinsysMakeResident(&device->Winsys, &res->Allocation);
        if (wsStatus == BC250_WINSYS_OK)
            wsStatus = Bc250WinsysWaitPaging(&device->Winsys, &res->Allocation);
        if (wsStatus != BC250_WINSYS_OK) {
            /* F3.3: the runtime will not DestroyResource after SetErrorCb.
             * Release the partially allocated resource here before reporting. */
            HRESULT failure = device->Winsys.LastCallbackHr;
            BC250_WINSYS_STATUS cleanup = Bc250WinsysDeallocate(&device->Winsys, &res->Allocation);
            Bc250UmdTrace("CreateResource RECUSADO: status=%s cleanup=%s; Created=FALSE",
                Bc250WinsysStatusName(wsStatus), Bc250WinsysStatusName(cleanup));
            /* DX.52: descreve o recurso recusado (laco do Explorer, Corridas 93..97). */
            Bc250UmdTrace("CreateResource RECUSADO desc: Dim=%u %ux%u Mips=%u Array=%u Fmt=%u Samples=%u "
                "Bind=0x%X Misc=0x%X Usage=%u CpuAccess=0x%X TotalSize=%llu RowPitch=%u InitData=%d hr=0x%08X",
                (UINT)pCreate->ResourceDimension,
                pCreate->pMipInfoList != NULL ? pCreate->pMipInfoList[0].TexelWidth : 0u,
                pCreate->pMipInfoList != NULL ? pCreate->pMipInfoList[0].TexelHeight : 0u,
                pCreate->MipLevels, pCreate->ArraySize, (UINT)pCreate->Format,
                pCreate->SampleDesc.Count, pCreate->BindFlags, pCreate->MiscFlags,
                (UINT)pCreate->Usage, pCreate->MapFlags,
                (unsigned long long)totalSize, res->RowPitch,
                pCreate->pInitialDataUP != NULL ? 1 : 0, (unsigned)failure);
            if (device->pUMCallbacks != NULL && device->pUMCallbacks->pfnSetErrorCb != NULL)
                device->pUMCallbacks->pfnSetErrorCb(device->hRTCoreLayer,
                    FAILED(failure) ? failure : E_FAIL);
            Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "RELEASE", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, failure);
            ReleaseSRWLockShared(&device->DeviceLock);
            return;
        }
        res->Created = TRUE;
        /* DX.82: Se o device entrou em teardown durante a paginacao, aborta com seguranca */
        if (device->TeardownActive || (device->VkBridgeInitialized && !device->VkBridge.DeviceActive)) {
            Bc250UmdTrace("CreateResource ABORTADO: device desmontado durante WaitPaging (res=%p)", (void*)res);
            Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "RELEASE", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, E_FAIL);
            ReleaseSRWLockShared(&device->DeviceLock);
            return;
        }
    } else {
        res->Created = TRUE;
    }

    /* Consumo obrigatorio de InitialDataUP (pInitialDataUP) quando fornecido pelo runtime D3D */
    if (pCreate->pInitialDataUP != NULL && pCreate->pInitialDataUP->pSysMem != NULL &&
        res->TotalByteSize > 0u) {
        if (res->pCpuShadow == NULL) {
            res->pCpuShadow = calloc(1, res->TotalByteSize);
        }
        if (res->pCpuShadow != NULL) {
            if (res->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
                UINT copyBytes = (res->Width > 0 && res->Width < res->TotalByteSize) ? res->Width : res->TotalByteSize;
                memcpy(res->pCpuShadow, pCreate->pInitialDataUP->pSysMem, copyBytes);
            } else if (res->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D && res->Height > 0u) {
                const UINT bpp = Bc250DxgiFormatBytesPerPixel(res->Format);
                const SIZE_T lineBytes = (SIZE_T)res->Width * bpp;
                if (bpp != 0u && pCreate->pInitialDataUP->SysMemPitch >= lineBytes &&
                    res->RowPitch >= lineBytes) {
                    for (UINT y = 0; y < res->Height; ++y) {
                        memcpy((BYTE*)res->pCpuShadow + (size_t)y * res->RowPitch,
                               (CONST BYTE*)pCreate->pInitialDataUP->pSysMem +
                                   (size_t)y * pCreate->pInitialDataUP->SysMemPitch,
                               lineBytes);
                    }
                } else {
                    Bc250UmdTrace("CreateResource: pInitialDataUP de textura RECUSADO "
                        "(Fmt=%u bpp=%u Width=%u SrcPitch=%u DstPitch=%u)",
                        (UINT)res->Format, bpp, res->Width,
                        pCreate->pInitialDataUP->SysMemPitch, res->RowPitch);
                }
            } else {
                memcpy(res->pCpuShadow, pCreate->pInitialDataUP->pSysMem, res->TotalByteSize);
            }
            if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
                wsStatus = Bc250WinsysUploadAllocation(&device->Winsys, &res->Allocation,
                    res->pCpuShadow, res->TotalByteSize);
                if (wsStatus == BC250_WINSYS_OK) {
                    res->WddmAllocationUploaded = TRUE;
                }
            }
            {
                UINT copyBytesReported = (res->ResourceDimension == D3D10DDIRESOURCE_BUFFER && res->Width > 0 && res->Width < res->TotalByteSize) ? res->Width : res->TotalByteSize;
                Bc250UmdTrace("CreateResource: pInitialDataUP consumido (%u bytes copiados para pCpuShadow)", copyBytesReported);
            }
        }
    }

    if (device != NULL && !device->TeardownActive && device->VkBridgeInitialized && device->VkBridge.DeviceActive) {
        (void)Bc250UmdVkCreateResource(device, res, pCreate);
    }

    Bc250UmdTrace("CreateResource: res=%p Dim=%u Fmt=%u Size=%u (%ux%u bpp=%u) GpuVa=0x%llX Primary=%d VkRes=%d Alias=%d "
        "Bind=0x%X Misc=0x%X Usage=%u Mips=%u Array=%u",
        (void*)res, res->ResourceDimension, res->Format, res->TotalByteSize,
        res->Width, res->Height, Bc250DxgiFormatBytesPerPixel(res->Format),
        (unsigned long long)res->Allocation.GpuVirtualAddress, res->IsPrimary,
        (int)res->VkResourceCreated, (int)res->VkMemAliased,
        (unsigned)pCreate->BindFlags, (unsigned)pCreate->MiscFlags, (unsigned)pCreate->Usage,
        (unsigned)pCreate->MipLevels, (unsigned)pCreate->ArraySize);

    Bc250WinsysHangEvent("CREATE_LOCK_SHARED", "RELEASE", device, hangSpan,
        (UINT64)(ULONG_PTR)res, 0u, S_OK);
    ReleaseSRWLockShared(&device->DeviceLock);
}

VOID APIENTRY Bc250Umd10OpenResource(
    D3D10DDI_HDEVICE hDevice,
    _In_ CONST D3D10DDIARG_OPENRESOURCE* pOpen,
    D3D10DDI_HRESOURCE hResource,
    D3D10DDI_HRTRESOURCE hRTResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    BC250_ALLOCATION_PRIVATE_V1 priv;
    BOOL hasPriv = FALSE;
    UNREFERENCED_PARAMETER(device);

    if (res == NULL || pOpen == NULL) {
        Bc250UmdTrace("OpenResource: argumento invalido");
        return;
    }

    memset(res, 0, sizeof(*res));
    res->hRTResource = hRTResource;
    res->hKMResource = pOpen->hKMResource;
    res->Created = TRUE;
    res->OpenedShared = TRUE; /* T4.6 */

    memset(&priv, 0, sizeof(priv));
    if (pOpen->pPrivateDriverData != NULL && pOpen->PrivateDriverDataSize > 0) {
        if (Bc250AllocationAbiParse(pOpen->pPrivateDriverData, pOpen->PrivateDriverDataSize, &priv) == BC250_ALLOCATION_ABI_OK) {
            hasPriv = TRUE;
        }
    }
    /*
     * T4.2: o elemento [0] e' lido pela struct v1. hAllocation, pPrivateDriverData
     * e PrivateDriverDataSize ocupam os MESMOS offsets nas duas versoes, entao a
     * leitura v1 e' correta em qualquer interface; so' GpuVirtualAddress (offset
     * 24) nao existe na v1. E a GPUVA do runtime NAO e' usada nem na v2: a VA de
     * quem criou vale no processo de quem criou. Igual ao Linux
     * (radv_amdgpu_winsys_bo_from_fd: va_range_alloc + AMDGPU_VA_OP_MAP) e ao
     * nosso proprio RADV WDDM2 (radv_wddm2_bo.c: MapGpuVirtualAddress sobre o
     * hAllocation aberto, ignorando alloc_info[0].GpuVirtualAddress).
     */
    if (pOpen->NumAllocations > 0 && pOpen->pOpenAllocationInfo != NULL) {
        const D3DDDI_OPENALLOCATIONINFO* pInfo = &pOpen->pOpenAllocationInfo[0];
        res->Allocation.KernelAllocation = pInfo->hAllocation;
        res->Allocation.GpuVirtualAddress = 0u;
        res->Allocation.Created = (pInfo->hAllocation != 0) ? TRUE : FALSE;
        if (!hasPriv && pInfo->pPrivateDriverData != NULL && pInfo->PrivateDriverDataSize > 0) {
            if (Bc250AllocationAbiParse(pInfo->pPrivateDriverData, pInfo->PrivateDriverDataSize, &priv) == BC250_ALLOCATION_ABI_OK) {
                hasPriv = TRUE;
            }
        }
    }
    Bc250UmdTrace("OpenResource T4.2: Interface=0x%08X array=%s NumAllocs=%u -- GPUVA do runtime IGNORADA; "
        "a VA sai do MapGpuVirtualAddress deste processo",
        (device != NULL) ? device->Interface : 0u,
        (device != NULL && Bc250UmdOpenAllocationArrayIsInfo2(device->Interface)) ?
            "OPENALLOCATIONINFO2" : "OPENALLOCATIONINFO(v1)",
        pOpen->NumAllocations);

    if (hasPriv) {
        res->Allocation.Token = priv.AllocationToken;
        res->Allocation.ByteSize = priv.ByteSize;
        if ((priv.Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) != 0u) {
            res->ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            res->Width = priv.Geometry.Width;
            res->Height = priv.Geometry.Height;
            res->RowPitch = priv.Geometry.Pitch;
            res->Format = Bc250D3dddiFormatToDxgiFormat(priv.Geometry.Format);
            if (res->Format == DXGI_FORMAT_UNKNOWN) {
                res->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            }
            res->TotalByteSize = (UINT)priv.ByteSize;
            res->Depth = 1u;
            res->MipLevels = 1u;
            res->ArraySize = 1u;
            res->SampleCount = 1u;
            Bc250UmdTrace("OpenResource: geometria extraida %ux%u pitch=%u fmt=%u (dxgi=%u) size=%llu token=0x%llX",
                res->Width, res->Height, res->RowPitch, priv.Geometry.Format, (unsigned)res->Format,
                (unsigned long long)priv.ByteSize, (unsigned long long)priv.AllocationToken);
        }
    }

    /*
     * Mapeia no processo que abriu e torna residente. Cada passo registra o
     * status: "sem VA" deixa de ser mudo, e o criterio da corrida distingue
     * mapeamento recusado de blt recusado. O tamanho sobe para a pagina de
     * 4 KiB: a alocacao do kernel e' granular em pagina e nunca menor que o
     * ByteSize publicado (medido: 3.407.872 publicado, 3.670.016 alocado).
     */
    if (res->Allocation.Created && device != NULL && device->WinsysInitialized) {
        BC250_WINSYS_STATUS mapStatus = BC250_WINSYS_BAD_ARGUMENT;
        BC250_WINSYS_STATUS residentStatus = BC250_WINSYS_BAD_ARGUMENT;
        BC250_WINSYS_STATUS waitStatus = BC250_WINSYS_BAD_ARGUMENT;
        res->Allocation.ByteSize = (res->Allocation.ByteSize + 0xFFFull) & ~0xFFFull;
        if (res->Allocation.ByteSize == 0u) {
            Bc250UmdTrace("OpenResource T4.2: MAPEAMENTO RECUSADO -- tamanho desconhecido "
                "(dados privados ausentes ou invalidos) hAlloc=0x%X",
                (unsigned)res->Allocation.KernelAllocation);
        } else {
            mapStatus = Bc250WinsysMapAllocation(&device->Winsys, &res->Allocation, FALSE);
            if (mapStatus == BC250_WINSYS_OK) {
                residentStatus = Bc250WinsysMakeResident(&device->Winsys, &res->Allocation);
                waitStatus = Bc250WinsysWaitPaging(&device->Winsys, &res->Allocation);
            }
            Bc250UmdTrace("OpenResource T4.2: Map=%d Resident=%d WaitPaging=%d hr=0x%08X hAlloc=0x%X Bytes=%llu VA=0x%llX",
                (int)mapStatus, (int)residentStatus, (int)waitStatus,
                (unsigned)device->Winsys.LastCallbackHr,
                (unsigned)res->Allocation.KernelAllocation,
                (unsigned long long)res->Allocation.ByteSize,
                (unsigned long long)res->Allocation.GpuVirtualAddress);
        }
    }

    Bc250UmdTrace("OpenResource: res=%p hKMResource=%u hAlloc=0x%X GpuVa=0x%llX NumAllocs=%u Geo=%d",
        (void*)res, (unsigned)pOpen->hKMResource.handle, (unsigned)res->Allocation.KernelAllocation,
        (unsigned long long)res->Allocation.GpuVirtualAddress, pOpen->NumAllocations,
        (hasPriv && (priv.Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY)) ? 1 : 0);
}

VOID APIENTRY Bc250Umd10DestroyResource(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;
    UINT64 hangSpan = Bc250WinsysHangSpan();

    Bc250UmdTrace("DestroyResource: res=%p", (void*)res);
    if (res != NULL) {
        if (device != NULL) {
            Bc250WinsysHangEvent("DESTROY_LOCK_SHARED", "WAIT", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, S_OK);
            AcquireSRWLockShared(&device->DeviceLock);
            Bc250WinsysHangEvent("DESTROY_LOCK_SHARED", "ACQUIRED", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, S_OK);
            if (!device->TeardownActive && device->VkBridgeInitialized && device->VkBridge.DeviceActive) {
                Bc250UmdVkDestroyResource(device, res);
            }
            if (device->WinsysInitialized && res->Allocation.Created) {
                (void)Bc250WinsysDeallocate(&device->Winsys, &res->Allocation);
            }
            Bc250WinsysHangEvent("DESTROY_LOCK_SHARED", "RELEASE", device, hangSpan,
                (UINT64)(ULONG_PTR)res, 0u, S_OK);
            ReleaseSRWLockShared(&device->DeviceLock);
        }
        if (res->pCpuShadow != NULL) {
            free(res->pCpuShadow);
            res->pCpuShadow = NULL;
        }
        memset(res, 0, sizeof(*res));
    }
}

/*
 * =========================================================================
 * SINCRONIA DE TEXTURA: shadow de CPU -> VkImage. Fonte unica.
 * =========================================================================
 * MEDIDO na Corrida 39 (19/09 16:20:14): o dwm.exe saiu com upload=0 enquanto
 * outros processos tiveram 21 uploads. A razao estava aqui: tanto
 * ResourceUpdateSubresourceUP quanto ResourceUnmap sincronizavam com Vulkan
 * SOMENTE quando ResourceDimension era BUFFER. Textura escrevia no shadow de
 * CPU e nunca chegava a VkImage -- e o descritor recusava, corretamente, uma
 * imagem sem conteudo.
 *
 * O DWM nao usa pInitialDataUP: cria a textura vazia e preenche depois. Por
 * isso cobrir so a criacao, como fez a faixa anterior, nao o alcancava.
 *
 * ESCOPO DECLARADO, herdado de Bc250UmdVkUploadImageData: subrecurso 0
 * (mip 0, camada 0). Subrecurso diferente e RECUSADO com motivo, nunca
 * aproximado -- carregar mip 0 e deixar o resto indefinido seria pior que nao
 * carregar, porque o descritor passaria a aceitar a imagem.
 *
 * A origem e o shadow INTEIRO com o pitch do proprio recurso, nao o pedaco que
 * a chamada trouxe: a atualizacao parcial ja foi aplicada ao shadow, entao
 * subir o shadow completo mantem a imagem coerente com o que o D3D enxerga.
 */
static void Bc250UmdSyncTextureToVkImage(
    BC250_UMD_DEVICE* device,
    BC250_UMD_RESOURCE* res,
    UINT Subresource,
    const char* origem)
{
    SIZE_T bytes;

    if (device == NULL || res == NULL) return;
    if (res->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D) return;
    ++res->TextureWriteDdiCount;
    if (!device->VkBridgeInitialized || !res->VkResourceCreated) return;
    if (res->VkImg == VK_NULL_HANDLE || res->pCpuShadow == NULL) return;


    if (Subresource != 0u) {
        Bc250UmdTrace("%s: textura NAO sincronizada -- Subresource=%u fora do escopo carregado "
            "(mip 0 / camada 0); a imagem segue sem este subrecurso", origem, Subresource);
        return;
    }
    if (res->RowPitch == 0u || res->Height == 0u) {
        Bc250UmdTrace("%s: textura NAO sincronizada -- RowPitch=%u Height=%u; sem geometria "
            "declarada o tamanho da origem seria adivinhado", origem, res->RowPitch, res->Height);
        return;
    }

    bytes = (SIZE_T)res->RowPitch * (SIZE_T)res->Height;
    if (res->TotalByteSize != 0u && bytes > (SIZE_T)res->TotalByteSize) {
        Bc250UmdTrace("%s: textura NAO sincronizada -- RowPitch*Height (%zu) excede o shadow (%u)",
            origem, bytes, res->TotalByteSize);
        return;
    }

    if (Bc250UmdVkUploadImageData(device, res, res->pCpuShadow, res->RowPitch, bytes)) {
        Bc250UmdTrace("%s: textura sincronizada com VkImage (%ux%u, %zu bytes, mip0/camada0)",
            origem, res->Width, res->Height, bytes);
    } else {
        Bc250UmdTrace("%s: sincronia de textura RECUSADA pelo upload; a imagem continua sem "
            "conteudo e o descritor vai recusa-la (fail-closed)", origem);
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
    UNREFERENCED_PARAMETER(Subresource);

    if (pMappedSubResource == NULL) return;
    memset(pMappedSubResource, 0, sizeof(*pMappedSubResource));

    if (res == NULL) return;

    /*
     * DX.77: se a aplicacao descarta o conteudo anterior do buffer (WRITE_DISCARD),
     * draws pendentes no command buffer precisam ser executados antes que a CPU
     * sobrescreva a memoria compartilhada. Chamadas com WRITE_NOOVERWRITE (como o DWM
     * faz para draws sequenciais na mesma moldura) garantem nao-sobreposicao e
     * NAO drenam, permitindo coalescencia de lote completa no command buffer.
     */
    if (device != NULL && device->VkBridgeInitialized && device->VkBridge.PendingDraws > 0 &&
        MapType == D3D10_DDI_MAP_WRITE_DISCARD) {
        (void)Bc250UmdVkDrainPending(&device->VkBridge, "ResourceMap DISCARD de buffer");
    }

    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    pMappedSubResource->pData = res->pCpuShadow;
    pMappedSubResource->RowPitch = res->RowPitch;
    pMappedSubResource->DepthPitch = res->SlicePitch;

    Bc250UmdTrace("ResourceMap: res=%p Sub=%u MapType=%u Flags=0x%x pData=%p Pitch=%u",
        (void*)res, Subresource, (unsigned)MapType, (unsigned)MapFlags, pMappedSubResource->pData, pMappedSubResource->RowPitch);
}

VOID APIENTRY Bc250Umd10ResourceUnmap(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRESOURCE hResource,
    UINT Subresource)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)hResource.pDrvPrivate;

    if (device != NULL && device->WinsysInitialized && res != NULL &&
        res->Allocation.Created && res->pCpuShadow != NULL && res->TotalByteSize > 0u) {
        BC250_WINSYS_STATUS wsStatus = Bc250WinsysUploadAllocation(&device->Winsys, &res->Allocation,
            res->pCpuShadow, res->TotalByteSize);
        if (wsStatus == BC250_WINSYS_OK) {
            res->WddmAllocationUploaded = TRUE;
        }
    }

    if (device != NULL && device->VkBridgeInitialized && res != NULL &&
        res->VkResourceCreated && res->VkMem != VK_NULL_HANDLE &&
        res->pCpuShadow != NULL && res->TotalByteSize > 0u &&
        res->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
        (void)Bc250UmdVkUploadBufferData(device, res, res->pCpuShadow, res->TotalByteSize);
        Bc250UmdTrace("ResourceUnmap: buffer sincronizado com VkBuffer (%u bytes)", res->TotalByteSize);
    }

    Bc250UmdSyncTextureToVkImage(device, res, Subresource, "ResourceUnmap");

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
    static volatile LONG s_Bc250MsaaQueryLogOrdinal = 0;
    LONG logOrdinal;
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Format);

    if (pNumQualityLevels == NULL) return;

    /* 1 sample supports 1 quality level (level 0). Higher sample counts: 0 until MSAA hardware resolve is validated */
    if (SampleCount <= 1u) {
        *pNumQualityLevels = 1u;
    } else {
        *pNumQualityLevels = 0u;
    }

    /* DX.69: o Explorer chamou esta query 550.800 vezes na Corrida 112.
     * Cada linha fazia ate dois WriteFile write-through. O retorno e' identico;
     * registrar as primeiras 16 e cada 4096a preserva atividade e contrato. */
    logOrdinal = InterlockedIncrement(&s_Bc250MsaaQueryLogOrdinal);
    if (logOrdinal <= 16 || (logOrdinal % 4096) == 0) {
        Bc250UmdTrace("[DX69_MSAA_SAMPLE] #%ld CheckMultisampleQualityLevels: Format=%u Samples=%u -> Levels=%u",
            logOrdinal, (UINT)Format, SampleCount, *pNumQualityLevels);
    }
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
    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateRtv(device, rtv, pCreate);
    }
    Bc250UmdTrace("CreateRenderTargetView: rtv=%p res=%p Dim=%u Fmt=%u Mip=%u Slice=%u Count=%u VkRtv=%d",
        (void*)rtv, (void*)res, (UINT)rtv->ResourceDimension, (UINT)rtv->Format,
        rtv->MipSlice, rtv->FirstArraySlice, rtv->ArraySize, (int)rtv->VkRtvCreated);
}

VOID APIENTRY Bc250Umd10DestroyRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RTV* rtv = (BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate;

    if (rtv == NULL) return;

    if (device != NULL) {
        if (device->VkBridgeInitialized) {
            Bc250UmdVkDestroyRtv(device, rtv);
        }
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
    if (device != NULL && device->VkBridgeInitialized) {
        (void)Bc250UmdVkCreateDsv(device, dsv, pCreate);
    }
    Bc250UmdTrace("CreateDepthStencilView: dsv=%p res=%p Dim=%u Fmt=%u Mip=%u Slice=%u Count=%u VkDsv=%d",
        (void*)dsv, (void*)res, (UINT)dsv->ResourceDimension, (UINT)dsv->Format,
        dsv->MipSlice, dsv->FirstArraySlice, dsv->ArraySize, (int)dsv->VkDsvCreated);
}

VOID APIENTRY Bc250Umd10DestroyDepthStencilView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_DSV* dsv = (BC250_UMD_DSV*)hDepthStencilView.pDrvPrivate;

    if (dsv == NULL) return;

    if (device != NULL) {
        if (device->VkBridgeInitialized) {
            Bc250UmdVkDestroyDsv(device, dsv);
        }
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

    {
        /* DX.23: qual recurso e' o RT0 -- sem isso nao se liga draw a superficie apresentada. */
        BC250_UMD_RTV *rtv0 = (count > 0) ? (BC250_UMD_RTV *)device->PipelineState.BoundRTVs[0].pDrvPrivate : NULL;
        Bc250UmdTrace("SetRenderTargets: NumViews=%u ClearSlots=%u DSV=%p RTV0=%p res=%p %ux%u",
            count, ClearSlots, hDepthStencilView.pDrvPrivate, (void *)rtv0,
            rtv0 ? (void *)rtv0->pResource : NULL,
            (rtv0 && rtv0->pResource) ? rtv0->pResource->Width : 0u,
            (rtv0 && rtv0->pResource) ? rtv0->pResource->Height : 0u);
    }
}

VOID APIENTRY Bc250Umd10ClearRenderTargetView(
    D3D10DDI_HDEVICE hDevice,
    D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    FLOAT pColorRGBA[4])
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    BC250_UMD_RTV* rtv = (BC250_UMD_RTV*)hRenderTargetView.pDrvPrivate;

    /* DX.28: o clear tambem vai para a imagem atual do recurso. Se a RTV nao
     * conseguir seguir, VkRtvCreated continua TRUE mas o clear cai no ramo de
     * recusa abaixo pela checagem explicita. */
    BOOL rtvCurrent = (device != NULL && device->VkBridgeInitialized && device->VkBridge.DeviceActive &&
        rtv != NULL && rtv->VkRtvCreated) ? Bc250UmdVkRtvFollowResource(device, rtv) : FALSE;

    if (rtvCurrent && rtv->VkPass != VK_NULL_HANDLE && rtv->VkFb != VK_NULL_HANDLE) {
        VkCommandBuffer cmd = Bc250UmdVkGetCommandBuffer(&device->VkBridge);
        if (cmd != VK_NULL_HANDLE) {
            VkClearValue clearVal;
            memset(&clearVal, 0, sizeof(clearVal));
            if (pColorRGBA != NULL) {
                clearVal.color.float32[0] = pColorRGBA[0];
                clearVal.color.float32[1] = pColorRGBA[1];
                clearVal.color.float32[2] = pColorRGBA[2];
                clearVal.color.float32[3] = pColorRGBA[3];
            } else {
                clearVal.color.float32[0] = 0.0f;
                clearVal.color.float32[1] = 0.0f;
                clearVal.color.float32[2] = 0.0f;
                clearVal.color.float32[3] = 1.0f;
            }

            /* Reiniciar RenderPass com clearValue explícito para acionar fast-clear nativo (loadOp=CLEAR) */
            if (device->InRenderPass) {
                device->VkBridge.vk.vkCmdEndRenderPass(cmd);
                device->InRenderPass = FALSE;
            }

            VkRenderPassBeginInfo rpBegin;
            memset(&rpBegin, 0, sizeof(rpBegin));
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = rtv->VkPass;
            rpBegin.framebuffer = rtv->VkFb;
            rpBegin.renderArea.offset.x = 0;
            rpBegin.renderArea.offset.y = 0;
            rpBegin.renderArea.extent.width = (rtv->pResource != NULL && rtv->pResource->Width > 0) ? rtv->pResource->Width : 1;
            rpBegin.renderArea.extent.height = (rtv->pResource != NULL && rtv->pResource->Height > 0) ? rtv->pResource->Height : 1;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearVal;

            device->VkBridge.vk.vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
            device->InRenderPass = TRUE;
            device->ActiveRenderPass = rtv->VkPass;
            device->ActiveFramebuffer = rtv->VkFb;
            if (rtv->pResource) rtv->pResource->VkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            Bc250UmdTrace("ClearRenderTargetView: EXECUTADO VULKAN FAST-CLEAR (rtv=%p res=%p %ux%u RGBA=[%.2f,%.2f,%.2f,%.2f])",
                (void*)rtv, (void*)rtv->pResource, rpBegin.renderArea.extent.width, rpBegin.renderArea.extent.height,
                clearVal.color.float32[0], clearVal.color.float32[1],
                clearVal.color.float32[2], clearVal.color.float32[3]);
            return;
        }
    }

    if (Bc250UmdDrawBltSubstitutionArmed()) {
        Bc250UmdTrace("ClearRenderTargetView: OMITIDO SOB REGIME EXPERIMENTAL DE SUBSTITUICAO -- "
            "sem backend de fill GPU ignorado sem SetErrorCb (shadow preservado; NAO e fill GPU)");
        return;
    }
    /* F3.1: COMPOSITE_BLT copies; it is not a fill backend. Keep the Map
     * shadow intact, but never pretend that changing it clears GPU memory.
     * A real fill needs its own proof. */
    Bc250UmdTrace("ClearRenderTargetView RECUSADO: sem backend de fill GPU; shadow preservado");
    if (device != NULL && device->pUMCallbacks != NULL &&
        device->pUMCallbacks->pfnSetErrorCb != NULL) {
        device->pUMCallbacks->pfnSetErrorCb(device->hRTCoreLayer, E_NOTIMPL);
    }
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
    for (UINT i = 0; i < NumBuffers && (StartSlot + i) < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT && i < 8; ++i) {
        Bc250UmdTrace("  VB Slot[%u]: Buf=%p Stride=%u Offset=%u",
            StartSlot + i,
            device->PipelineState.VertexBuffers[StartSlot + i].pDrvPrivate,
            device->PipelineState.VertexStrides[StartSlot + i],
            device->PipelineState.VertexOffsets[StartSlot + i]);
    }
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
        if (device != NULL && device->VkBridgeInitialized && res->VkResourceCreated &&
            res->VkMem != VK_NULL_HANDLE) {
            (void)Bc250UmdVkUploadBufferData(device, res, res->pCpuShadow, res->TotalByteSize);
            Bc250UmdTrace("DefaultConstantBufferUpdateSubresourceUP: sincronizado com VkBuffer (%u bytes)",
                res->TotalByteSize);
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
    BOOL copied = FALSE;
    UNREFERENCED_PARAMETER(DepthPitch);

    if (res == NULL || pSysMemBuffer == NULL) return;

    if (res->pCpuShadow == NULL && res->TotalByteSize > 0u) {
        res->pCpuShadow = calloc(1, res->TotalByteSize);
    }

    if (res->pCpuShadow != NULL) {
        if (res->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
            SIZE_T dstOffset = 0u;
            SIZE_T copyBytes = (res->Width > 0u && res->Width < res->TotalByteSize) ?
                (SIZE_T)res->Width : (SIZE_T)res->TotalByteSize;
            if (pDstBox != NULL) {
                if (pDstBox->left < 0 || pDstBox->right <= pDstBox->left ||
                    (UINT)pDstBox->right > res->Width || pDstBox->top != 0u ||
                    pDstBox->bottom > 1u || pDstBox->front != 0u ||
                    pDstBox->back > 1u) {
                    Bc250UmdTrace("ResourceUpdateSubresourceUP: buffer RECUSADO -- box invalido");
                    goto update_invalid;
                }
                dstOffset = pDstBox->left;
                copyBytes = (SIZE_T)(pDstBox->right - pDstBox->left);
            }
            if (dstOffset > res->TotalByteSize ||
                copyBytes > (SIZE_T)res->TotalByteSize - dstOffset) {
                Bc250UmdTrace("ResourceUpdateSubresourceUP: buffer RECUSADO -- copia excede shadow");
                goto update_invalid;
            }
            memcpy((BYTE*)res->pCpuShadow + dstOffset, pSysMemBuffer, copyBytes);
            copied = TRUE;
        } else if (res->ResourceDimension == D3D10DDIRESOURCE_TEXTURE2D) {
            const UINT bpp = Bc250DxgiFormatBytesPerPixel(res->Format);
            UINT left = 0u, top = 0u, width = res->Width, height = res->Height;
            SIZE_T lineBytes;
            SIZE_T dstOffset;
            SIZE_T dstRequired;
            CONST BYTE* src = (CONST BYTE*)pSysMemBuffer;
            BYTE* dst = (BYTE*)res->pCpuShadow;

            if (DstSubresource != 0u || bpp == 0u || width == 0u || height == 0u ||
                res->RowPitch == 0u) {
                Bc250UmdTrace("ResourceUpdateSubresourceUP: textura RECUSADA -- Sub=%u Fmt=%u "
                    "bpp=%u WxH=%ux%u DstPitch=%u", DstSubresource,
                    (UINT)res->Format, bpp, width, height, res->RowPitch);
                goto update_invalid;
            }
            if (pDstBox != NULL) {
                if (pDstBox->left < 0 || pDstBox->top < 0 ||
                    pDstBox->right <= pDstBox->left ||
                    pDstBox->bottom <= pDstBox->top ||
                    (UINT)pDstBox->right > res->Width ||
                    (UINT)pDstBox->bottom > res->Height ||
                    pDstBox->front != 0u || pDstBox->back != 1u) {
                    Bc250UmdTrace("ResourceUpdateSubresourceUP: textura RECUSADA -- box fora da geometria");
                    goto update_invalid;
                }
                left = pDstBox->left;
                top = pDstBox->top;
                width = pDstBox->right - pDstBox->left;
                height = pDstBox->bottom - pDstBox->top;
            }
            if ((SIZE_T)width > ((SIZE_T)-1) / bpp) goto update_invalid;
            lineBytes = (SIZE_T)width * bpp;
            if ((SIZE_T)RowPitch < lineBytes || (SIZE_T)res->RowPitch < lineBytes) {
                Bc250UmdTrace("ResourceUpdateSubresourceUP: textura RECUSADA -- linha=%zu "
                    "SrcPitch=%u DstPitch=%u", lineBytes, RowPitch, res->RowPitch);
                goto update_invalid;
            }
            dstOffset = (SIZE_T)top * res->RowPitch + (SIZE_T)left * bpp;
            dstRequired = dstOffset + (SIZE_T)(height - 1u) * res->RowPitch + lineBytes;
            if (dstRequired < dstOffset || dstRequired > (SIZE_T)res->TotalByteSize) {
                Bc250UmdTrace("ResourceUpdateSubresourceUP: textura RECUSADA -- destino exige %zu, shadow=%u",
                    dstRequired, res->TotalByteSize);
                goto update_invalid;
            }
            dst += dstOffset;
            for (UINT y = 0u; y < height; ++y) {
                memcpy(dst + (SIZE_T)y * res->RowPitch,
                    src + (SIZE_T)y * RowPitch, lineBytes);
            }
            copied = TRUE;
            Bc250UmdTrace("ResourceUpdateSubresourceUP: textura copiada linha-a-linha "
                "(%ux%u bpp=%u SrcPitch=%u DstPitch=%u)",
                width, height, bpp, RowPitch, res->RowPitch);
        } else {
            Bc250UmdTrace("ResourceUpdateSubresourceUP: dimensao %u RECUSADA",
                (UINT)res->ResourceDimension);
            goto update_invalid;
        }

        if (!copied) goto update_invalid;
        if (device != NULL && device->WinsysInitialized && res->Allocation.Created) {
            BC250_WINSYS_STATUS wsStatus = Bc250WinsysUploadAllocation(
                &device->Winsys, &res->Allocation, res->pCpuShadow, res->TotalByteSize);
            if (wsStatus == BC250_WINSYS_OK) {
                res->WddmAllocationUploaded = TRUE;
            } else {
                Bc250UmdTrace("ResourceUpdateSubresourceUP: WDDM allocation NAO sincronizada "
                    "(status=%s hr=0x%08X); Vulkan pode ter copia separada",
                    Bc250WinsysStatusName(wsStatus), (unsigned)device->Winsys.LastCallbackHr);
            }
        }
        if (device != NULL && device->VkBridgeInitialized && res->VkResourceCreated &&
            res->VkMem != VK_NULL_HANDLE && res->ResourceDimension == D3D10DDIRESOURCE_BUFFER) {
            UINT uploadBytes = (res->Width > 0 && res->Width < res->TotalByteSize) ? res->Width : res->TotalByteSize;
            (void)Bc250UmdVkUploadBufferData(device, res, res->pCpuShadow, uploadBytes);
            Bc250UmdTrace("ResourceUpdateSubresourceUP: buffer sincronizado com VkBuffer (%u bytes)",
                uploadBytes);
        }
    }

    if (copied) {
        Bc250UmdSyncTextureToVkImage(device, res, DstSubresource, "ResourceUpdateSubresourceUP");
    }

    Bc250UmdTrace("ResourceUpdateSubresourceUP: res=%p pitch=%u", (void*)res, RowPitch);
    return;

update_invalid:
    if (device != NULL && device->pUMCallbacks != NULL &&
        device->pUMCallbacks->pfnSetErrorCb != NULL) {
        device->pUMCallbacks->pfnSetErrorCb(device->hRTCoreLayer, E_INVALIDARG);
    }
}

static BOOL Bc250UmdVkCopyImageInternal(
    _Inout_ BC250_UMD_DEVICE* device,
    _Inout_ BC250_UMD_RESOURCE* dst,
    UINT dstX,
    UINT dstY,
    _In_ BC250_UMD_RESOURCE* src,
    _In_opt_ CONST D3D10_DDI_BOX* pSrcBox)
{
    if (device == NULL || !device->VkBridge.DeviceActive ||
        dst == NULL || src == NULL ||
        dst->VkImg == VK_NULL_HANDLE || src->VkImg == VK_NULL_HANDLE ||
        device->VkBridge.vk.vkCmdCopyImage == NULL ||
        device->VkBridge.vk.vkCmdPipelineBarrier == NULL) {
        return FALSE;
    }

    VkCommandBuffer cmdBuf = Bc250UmdVkGetCommandBuffer(&device->VkBridge);
    if (cmdBuf == VK_NULL_HANDLE) {
        return FALSE;
    }

    /* DX.78: se houver render pass aberto no command buffer, encerra antes de gravar barreiras e copia */
    if (device->InRenderPass) {
        device->VkBridge.vk.vkCmdEndRenderPass(cmdBuf);
        device->InRenderPass = FALSE;
    }

    /*
     * DX.64 (Corrida 106) -- A COPIA PASSA A RESPEITAR E ATUALIZAR O LAYOUT RASTREADO.
     *
     * MEDIDO: o dwm.exe criou uma textura 256x256 (SRV|RT), escreveu nela por
     * ResourceCopyRegion (esta funcao, Extent=50x26) e 120 ms depois o Draw a
     * amostrou -- recusado com SRV_LAYOUT_UNDEFINED_NUNCA_TRANSICIONADA, porque
     * esta funcao (a) supunha COLOR_ATTACHMENT_OPTIMAL como layout anterior das
     * duas imagens, qualquer que fosse o real, e (b) nunca gravava VkLayout.
     * O Draw recusado chama pfnSetErrorCb(E_NOTIMPL) e o DWM recriou o device
     * ~3 s depois, 2 de 2 vezes; as primarias da recriacao nao sobrevivem.
     *
     * Agora: layout anterior = o rastreado (UNDEFINED e' oldLayout sempre
     * valido e descarta so' conteudo que nunca existiu); barreiras cobrem a
     * imagem inteira, como em bc250_umd_descriptors.c; a origem volta ao seu
     * layout; o destino so' passa a declarar conteudo se a origem tinha.
     */
    const VkImageLayout srcOld = src->VkLayout;
    const VkImageLayout dstOld = dst->VkLayout;
    const BOOL srcHasContent =
        (srcOld == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
         srcOld == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ? TRUE : FALSE;
    const BOOL dstHadContent =
        (dstOld == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
         dstOld == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ? TRUE : FALSE;
    VkImageLayout srcFinal = srcHasContent ? srcOld : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageLayout dstFinal;

    if (src->VkImg == dst->VkImg) {
        /* Duas barreiras sobre a mesma imagem com layouts diferentes e' invalido. */
        Bc250UmdTrace("[VK_BRIDGE] ResourceCopyImage RECUSADO: origem e destino sao a mesma imagem (res=%p)",
            (void*)src);
        return FALSE;
    }
    if (!srcHasContent && !dstHadContent) {
        dstFinal = VK_IMAGE_LAYOUT_UNDEFINED;   /* nada foi escrito de verdade */
    } else if (dstOld == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
               (dst->MipLevels <= 1u && dst->ArraySize <= 1u)) {
        dstFinal = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else {
        /* Com mips/camadas, a copia escreve so' o subrecurso 0: mesma
         * semantica do upload, e o descritor recusa views alem dele. */
        dstFinal = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkImageMemoryBarrier barriers[2];
    memset(barriers, 0, sizeof(barriers));

    /* Source barrier */
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].oldLayout = srcOld;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = src->VkImg;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = src->MipLevels ? src->MipLevels : 1u;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = src->ArraySize ? src->ArraySize : 1u;

    /* Destination barrier */
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout = dstOld;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = dst->VkImg;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = dst->MipLevels ? dst->MipLevels : 1u;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = dst->ArraySize ? dst->ArraySize : 1u;

    device->VkBridge.vk.vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 2, barriers);

    /* 2. Regiao de copia */
    VkImageCopy region;
    memset(&region, 0, sizeof(region));
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;

    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;

    if (pSrcBox != NULL) {
        region.srcOffset.x = (int32_t)pSrcBox->left;
        region.srcOffset.y = (int32_t)pSrcBox->top;
        region.srcOffset.z = (int32_t)pSrcBox->front;
        region.dstOffset.x = (int32_t)dstX;
        region.dstOffset.y = (int32_t)dstY;
        region.dstOffset.z = 0;
        region.extent.width = (pSrcBox->right > pSrcBox->left) ? (pSrcBox->right - pSrcBox->left) : src->Width;
        region.extent.height = (pSrcBox->bottom > pSrcBox->top) ? (pSrcBox->bottom - pSrcBox->top) : src->Height;
        region.extent.depth = (pSrcBox->back > pSrcBox->front) ? (pSrcBox->back - pSrcBox->front) : 1;
    } else {
        region.srcOffset.x = 0;
        region.srcOffset.y = 0;
        region.srcOffset.z = 0;
        region.dstOffset.x = (int32_t)dstX;
        region.dstOffset.y = (int32_t)dstY;
        region.dstOffset.z = 0;
        region.extent.width = (dst->Width < src->Width) ? dst->Width : src->Width;
        region.extent.height = (dst->Height < src->Height) ? dst->Height : src->Height;
        region.extent.depth = 1;
    }

    device->VkBridge.vk.vkCmdCopyImage(cmdBuf,
        src->VkImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->VkImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    /* 3. Barreira de retorno: origem ao layout que tinha; destino ao layout
     * que passa a declarar (UNDEFINED rastreado vira COLOR_ATTACHMENT fisico,
     * e UNDEFINED continua oldLayout valido na proxima barreira). */
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = srcFinal;

    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
        VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = (dstFinal == VK_IMAGE_LAYOUT_UNDEFINED) ?
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : dstFinal;

    device->VkBridge.vk.vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 0, NULL, 2, barriers);

    /* DX.78: Coalescencia de copias de regioes de imagem (damage rects do DWM).
     * O comando de copia permanece gravado no command buffer sem flush sincrono
     * imediato, unificando copias de dano com o destile e a submissao ao hardware
     * por quadro. Flushes ocorrem apenas se a capacidade do lote for atingida. */
    device->VkBridge.PendingDraws++;
    const uint32_t batchPos = device->VkBridge.PendingDraws;
    const BOOL flushNow = (batchPos >= BC250_DX49_BATCH_MAX_DRAWS);
    if (flushNow) {
        if (Bc250UmdVkFlushCommands(&device->VkBridge) != VK_SUCCESS) {
            /* Sem prova de execucao, nada de conteudo declarado; UNDEFINED
             * e' oldLayout valido para qualquer estado fisico. */
            src->VkLayout = srcHasContent ? srcOld : VK_IMAGE_LAYOUT_UNDEFINED;
            dst->VkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Bc250UmdTrace("[VK_BRIDGE] ResourceCopyImage FALHOU no flush: src=%p dst=%p; destino sem conteudo declarado",
                (void*)src, (void*)dst);
            return FALSE;
        }
    }
    src->VkLayout = srcHasContent ? srcOld : VK_IMAGE_LAYOUT_UNDEFINED;
    dst->VkLayout = dstFinal;

    Bc250UmdTrace("[VK_BRIDGE] ResourceCopyImage Vk: src=%p dst=%p Extent=%ux%u DstXY=(%u,%u) "
        "LayoutSrc=%d->%d LayoutDst=%d->%d FlushAgora=%d Lote=%u",
        (void*)src, (void*)dst, region.extent.width, region.extent.height, dstX, dstY,
        (int)srcOld, (int)src->VkLayout, (int)dstOld, (int)dst->VkLayout,
        (int)flushNow, batchPos);

    return TRUE;
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

    /* Sincronizacao de imagens Vulkan para composicao DWM / texturas GPU */
    if (device != NULL && device->VkBridgeInitialized &&
        dst->VkImg != VK_NULL_HANDLE && src->VkImg != VK_NULL_HANDLE) {
        (void)Bc250UmdVkCopyImageInternal(device, dst, 0, 0, src, NULL);
    }

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

    /* Sincronizacao de sub-regioes de imagem Vulkan para composicao DWM */
    if (device != NULL && device->VkBridgeInitialized &&
        dst->VkImg != VK_NULL_HANDLE && src->VkImg != VK_NULL_HANDLE) {
        (void)Bc250UmdVkCopyImageInternal(device, dst, DstX, DstY, src, pSrcBox);
    }

    /*
     * ==================================================================
     * RECURSO NAO CRIADO NAO E ORIGEM NEM DESTINO DE COPIA.
     * ==================================================================
     * Ate 19/09 este caminho lia TotalByteSize e pCpuShadow sem perguntar se o
     * recurso chegou a existir, e passava o resultado direto a um memcpy.
     *
     * MEDIDO: bc250_d3d_umd_test.exe estourou dentro de memcpy com
     *   rcx = rdx = 0x00007ffd58bb5b40   (origem e destino iguais)
     *   r8  = 0x58bb34e0                 (1,4 GB)
     * O tamanho carrega o mesmo padrao alto do ponteiro: era um campo lido de
     * memoria nao inicializada. O teste declarava BC250_UMD_RESOURCE na pilha
     * sem inicializar e o entregava a ResourceResolveSubresource.
     *
     * O defeito NAO era do teste. Qualquer recurso cujo Created seja falso --
     * por lixo de pilha, por falha de criacao, por handle reaproveitado --
     * levava a um memcpy de comprimento arbitrario. A descoberta veio de
     * acrescentar um campo a struct, o que mudou QUAL lixo caia em
     * TotalByteSize; a mina ja estava armada, so nao tinha sido pisada.
     *
     * Agora a copia exige Created dos dois lados e confere que o tamanho cabe
     * no que foi de fato alocado. Recusa fala.
     */
    if (!dst->Created || !src->Created) {
        Bc250UmdTrace("ResourceCopyRegion RECUSADO: recurso nao criado (dstCreated=%d srcCreated=%d) -- "
            "TotalByteSize e pCpuShadow seriam lidos de memoria nao inicializada",
            (int)dst->Created, (int)src->Created);
        return;
    }

    if (dst->pCpuShadow == NULL && dst->TotalByteSize > 0u) {
        dst->pCpuShadow = calloc(1, dst->TotalByteSize);
    }

    if (dst->pCpuShadow != NULL && src->pCpuShadow != NULL) {
        if (pSrcBox == NULL) {
            UINT copySize = (dst->TotalByteSize < src->TotalByteSize) ? dst->TotalByteSize : src->TotalByteSize;
            if (copySize == 0u) {
                Bc250UmdTrace("ResourceCopyRegion RECUSADO: copySize=0 (dst=%u src=%u)",
                    dst->TotalByteSize, src->TotalByteSize);
                return;
            }
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
    if (Device->VkBridgeInitialized && Device->LastVkDrawFailureReason != NULL) {
        Bc250UmdTrace("%s: RECUSADO -- backend Vulkan motivo=%s (Rejected=%llu)",
            Operation, Device->LastVkDrawFailureReason, Device->RejectedDrawCount);
    } else {
    Bc250UmdTrace("%s: RECUSADO -- compilador/shader-state/draw PM4 ausente "
        "(Rejected=%llu)", Operation, Device->RejectedDrawCount);
    }
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

/*
 * DX.24 -- traducao do estado D3D11 que era IGNORADA EM SILENCIO.
 *
 * Corrida 69 (desfecho B): origem com conteudo, draws no alvo apresentado e
 * superficie preta. A leitura achou pontos em que o estado pedido pelo DWM nao
 * chegava ao Vulkan. Regra desta faixa: ou traduz exatamente, ou RECUSA o draw
 * com motivo nomeado -- nunca substitui por um valor "razoavel".
 */
/*
 * DX.24 (medicao para a Corrida 70): conteudo dos dados que o VS le, medido na
 * sombra de CPU. O VS do DWM multiplica a posicao pelo CB; CB zerado colapsa a
 * geometria e da preto sem erro nenhum. Amostra ate 4096 dwords por buffer.
 */
static void Bc250UmdTraceDrawData(_In_ BC250_UMD_DEVICE *d, UINT StartVertexLocation)
{
    BC250_UMD_RESOURCE *vscb = (BC250_UMD_RESOURCE *)d->PipelineState.VsConstantBuffers[0].pDrvPrivate;
    BC250_UMD_RESOURCE *pscb = (BC250_UMD_RESOURCE *)d->PipelineState.PsConstantBuffers[0].pDrvPrivate;
    BC250_UMD_RESOURCE *vb0 = (BC250_UMD_RESOURCE *)d->PipelineState.VertexBuffers[0].pDrvPrivate;
    uint32_t vsNz = 0, vsN = 0, psNz = 0, psN = 0, vbNz = 0, vbN = 0;
    float f[8] = {0};
    uint32_t v[8] = {0};
    if (vscb && vscb->pCpuShadow && vscb->Width >= 4u) {
        const uint32_t *w = (const uint32_t *)vscb->pCpuShadow;
        uint32_t n = vscb->Width / 4u, step = (n > 4096u) ? n / 4096u : 1u;
        for (uint32_t i = 0; i < n; i += step) { vsN++; if (w[i]) vsNz++; }
        /* so' leitura para o log -- nada e' escrito em recurso (portao C1) */
        for (uint32_t k = 0; k < 8u && k < n; ++k) { union { uint32_t u; float fl; } cv; cv.u = w[k]; f[k] = cv.fl; }
    }
    if (pscb && pscb->pCpuShadow && pscb->Width >= 4u) {
        const uint32_t *w = (const uint32_t *)pscb->pCpuShadow;
        uint32_t n = pscb->Width / 4u, step = (n > 4096u) ? n / 4096u : 1u;
        for (uint32_t i = 0; i < n; i += step) { psN++; if (w[i]) psNz++; }
    }
    if (vb0 && vb0->pCpuShadow && vb0->TotalByteSize >= 4u) {
        const uint8_t *base = (const uint8_t *)vb0->pCpuShadow;
        uint64_t off = (uint64_t)d->PipelineState.VertexOffsets[0] +
                       (uint64_t)StartVertexLocation * d->PipelineState.VertexStrides[0];
        uint32_t n = vb0->TotalByteSize / 4u, step = (n > 4096u) ? n / 4096u : 1u;
        const uint32_t *w = (const uint32_t *)base;
        for (uint32_t i = 0; i < n; i += step) { vbN++; if (w[i]) vbNz++; }
        if (off + 32u <= vb0->TotalByteSize && (off & 3u) == 0u) {
            const uint32_t *vw = (const uint32_t *)(base + off);
            for (uint32_t k = 0; k < 8u; ++k) v[k] = vw[k];
        }
    }
    Bc250UmdTrace("[DRAW_DATA] VSCB0=%p nz=%u/%u f0..7=[%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f] "
        "PSCB0=%p nz=%u/%u VB0=%p stride=%u off=%u nz=%u/%u v0=[%08X %08X %08X %08X %08X %08X %08X %08X]",
        (void *)vscb, vsNz, vsN, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
        (void *)pscb, psNz, psN, (void *)vb0, d->PipelineState.VertexStrides[0],
        d->PipelineState.VertexOffsets[0], vbNz, vbN, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
}

/*
 * DX.25 -- os vertices REALMENTE desenhados.
 *
 * Corrida 70: o [DRAW_DATA] amostrou o vertice do offset 0 do VB, que saiu zero,
 * mas os draws do DWM sao INDEXADOS: o vertice lido depende de StartIndexLocation,
 * BaseVertexLocation e do index buffer, e nada disso estava no log. Aqui: os
 * 6 primeiros indices (lidos da sombra do IB) e os 3 primeiros vertices que eles
 * apontam, com os 3 primeiros dwords como float (posicao no layout do DWM) e o
 * quarto em hex (cor R8G8B8A8 no offset 12). So' leitura.
 */
static void Bc250UmdTraceDrawVertices(_In_ BC250_UMD_DEVICE *d, BOOL Indexed, UINT Start, INT BaseVertex)
{
    BC250_UMD_RESOURCE *vb0 = (BC250_UMD_RESOURCE *)d->PipelineState.VertexBuffers[0].pDrvPrivate;
    BC250_UMD_RESOURCE *ib = (BC250_UMD_RESOURCE *)d->PipelineState.IndexBuffer.pDrvPrivate;
    UINT stride = d->PipelineState.VertexStrides[0];
    uint64_t vbOff = d->PipelineState.VertexOffsets[0];
    uint32_t idx[6] = {0};
    uint32_t nIdx = 0;
    char verts[3][96];
    const char *ibState = "N/A";

    if (Indexed) {
        uint32_t size = (d->PipelineState.IndexFormat == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
        ibState = "OK";
        if (ib == NULL || ib->pCpuShadow == NULL) {
            ibState = "IB_SEM_SOMBRA";
        } else {
            for (uint32_t k = 0; k < 6u; ++k) {
                uint64_t o = (uint64_t)d->PipelineState.IndexOffset + ((uint64_t)Start + k) * size;
                if (o + size > ib->TotalByteSize) { ibState = "IB_FORA_DO_BUFFER"; break; }
                idx[k] = (size == 2u) ? ((const uint16_t *)((const uint8_t *)ib->pCpuShadow + o))[0]
                                      : ((const uint32_t *)((const uint8_t *)ib->pCpuShadow + o))[0];
                nIdx++;
            }
        }
    } else {
        for (uint32_t k = 0; k < 6u; ++k) idx[k] = Start + k;
        nIdx = 6u;
    }

    for (uint32_t k = 0; k < 3u; ++k) {
        int64_t vi = (int64_t)idx[k] + (Indexed ? (int64_t)BaseVertex : 0);
        uint64_t o;
        if (k >= nIdx || vb0 == NULL || vb0->pCpuShadow == NULL || vi < 0) {
            _snprintf_s(verts[k], sizeof(verts[k]), _TRUNCATE, "v%u=INDISPONIVEL", k);
            continue;
        }
        o = vbOff + (uint64_t)vi * stride;
        if (o + 16u > vb0->TotalByteSize || (o & 3u) != 0u) {
            _snprintf_s(verts[k], sizeof(verts[k]), _TRUNCATE, "v%u(i=%lld)=FORA_DO_BUFFER(off=%llu)",
                k, (long long)vi, (unsigned long long)o);
            continue;
        }
        {
            const uint32_t *w = (const uint32_t *)((const uint8_t *)vb0->pCpuShadow + o);
            union { uint32_t u; float f; } x, y, z;
            x.u = w[0]; y.u = w[1]; z.u = w[2];
            _snprintf_s(verts[k], sizeof(verts[k]), _TRUNCATE, "v%u(i=%lld)=(%.2f,%.2f,%.2f) w3=%08X",
                k, (long long)vi, x.f, y.f, z.f, w[3]);
        }
    }
    Bc250UmdTrace("[DRAW_VERTS] Indexed=%d Start=%u Base=%d IB=%p fmt=%u off=%u %s idx=[%u %u %u %u %u %u] "
        "stride=%u vbOff=%llu %s %s %s",
        (int)Indexed, Start, BaseVertex, (void *)ib, (UINT)d->PipelineState.IndexFormat,
        d->PipelineState.IndexOffset, ibState, idx[0], idx[1], idx[2], idx[3], idx[4], idx[5],
        stride, (unsigned long long)vbOff, verts[0], verts[1], verts[2]);
}

/*
 * DX.25 -- o ALVO logo depois do draw.
 *
 * Separa "nada rasterizado" / "rasterizado em preto" / "desenhado e sobrescrito
 * depois": a grade do Present so' ve o fim do quadro. Instrumentacao de
 * laboratorio: copia o alvo inteiro (11 MB em 2560x1080) para o buffer linear e
 * roda a mesma grade. So' alvos grandes (>= 1024x512) e teto de 40 por device,
 * DECLARADO no log. Nada e' escrito no alvo.
 */
#define BC250_DRAW_READBACK_MAX 3u

/*
 * T4.5 (30/09) -- FONTE UNICA de "este alvo e' lido depois do draw".
 *
 * MEDIDO na Corrida 184: no StartMenuExperienceHost, 144 flushes para 143 draws
 * entre o 1o e o 2o Present de cada abertura do menu (~4,4 s), todos com
 * "Lote=1 Direto=0 FlushAgora=1". O flush do draw usava so'
 * DrawReadbackCount < 3, mas a leitura so' roda (e so' conta) em alvo
 * >= 1024x512; os alvos do menu sao 640x640, o contador ficava em 0 e TODO draw
 * esperava a GPU. As duas decisoes agora saem daqui.
 */
static BOOL Bc250UmdDrawReadbackTargetQualifies(_In_opt_ const BC250_UMD_RESOURCE *res)
{
    return (res != NULL && res->VkResourceCreated && res->VkImg != VK_NULL_HANDLE &&
        res->Width >= 1024u && res->Height >= 512u) ? TRUE : FALSE;
}

static void Bc250UmdReadbackDrawTarget(_Inout_ BC250_UMD_DEVICE *d, _In_opt_ BC250_UMD_RESOURCE *res)
{
    VkCommandBuffer cmd;
    VkImageMemoryBarrier imb;
    VkBufferMemoryBarrier bmb;
    VkBufferImageCopy region;
    uint32_t n;

    if (!Bc250UmdDrawReadbackTargetQualifies(res)) {
        return;
    }
    if (d->DrawReadbackCount >= BC250_DRAW_READBACK_MAX) {
        if (d->DrawReadbackCount == BC250_DRAW_READBACK_MAX) {
            Bc250UmdTrace("[DRAW_READBACK] TETO de %u leituras atingido neste device; draws seguintes NAO sao lidos",
                BC250_DRAW_READBACK_MAX);
            d->DrawReadbackCount++;
        }
        return;
    }
    n = ++d->DrawReadbackCount;
    if (!Bc250UmdVkEnsureLinearPresentBuf(&d->VkBridge, res->Width, res->Height) ||
        d->VkBridge.LinearPresentBuf == VK_NULL_HANDLE) {
        Bc250UmdTrace("[DRAW_READBACK] #%u PULADA: buffer linear indisponivel", n);
        return;
    }
    cmd = Bc250UmdVkGetCommandBuffer(&d->VkBridge);
    if (cmd == VK_NULL_HANDLE) {
        Bc250UmdTrace("[DRAW_READBACK] #%u PULADA: command buffer indisponivel", n);
        return;
    }

    memset(&imb, 0, sizeof(imb));
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = res->VkImg;
    imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imb.subresourceRange.levelCount = 1;
    imb.subresourceRange.layerCount = 1;
    d->VkBridge.vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &imb);

    memset(&region, 0, sizeof(region));
    region.bufferRowLength = res->Width;
    region.bufferImageHeight = res->Height;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = res->Width;
    region.imageExtent.height = res->Height;
    region.imageExtent.depth = 1;
    d->VkBridge.vk.vkCmdCopyImageToBuffer(cmd, res->VkImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        d->VkBridge.LinearPresentBuf, 1, &region);

    memset(&bmb, 0, sizeof(bmb));
    bmb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bmb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bmb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.buffer = d->VkBridge.LinearPresentBuf;
    bmb.size = (VkDeviceSize)res->Width * res->Height * 4u;
    d->VkBridge.vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bmb, 0, NULL);

    imb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    d->VkBridge.vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &imb);

    if (Bc250UmdVkFlushCommands(&d->VkBridge) != VK_SUCCESS) {
        Bc250UmdTrace("[DRAW_READBACK] #%u FALHOU: flush/fence da copia", n);
        return;
    }
    Bc250UmdTrace("[DRAW_READBACK] #%u VkDraws=%llu RT0=%p %ux%u -- a GRADE seguinte e' do ALVO logo apos este draw, nao do Present",
        n, d->NativeVkDrawCount, (void *)res, res->Width, res->Height);
    /*
     * 4b (doc 564): a copia pela GPU e o flush acima ficam IGUAIS; so' a
     * leitura da CPU pela BAR sai. A funcao devolve 0 e diz DESLIGADA no log.
     */
    (void)Bc250UmdVkInspectLinearPresentBuf(&d->VkBridge, res->Width, res->Height);
}

/*
 * DX.49: teto SUPERIOR dos bytes que um draw grava no anel -- todo CB ligado
 * (VS e PS, mesmo que a pipeline nao use), todo VB das ligacoes de entrada e o
 * IB, cada um arredondado ao alinhamento do anel. Decidido ANTES de gravar
 * qualquer comando: drenar no meio de um draw deixaria meio draw no lote.
 */
/*
 * DX.51 -- BISSECCAO: lote de draws DESLIGADO (Corridas 94..96 travaram).
 * Corrida 97: travou com o lote desligado -- o lote nao era a causa do
 * travamento, mas a 96 piscou com ele ligado.
 *
 * DX.54 -- RELIGADO, com o anel de 4 MB (antes 16 MB) e criado so' em device
 * que ja fez BC250_DX54_RING_MIN_DIRECT_DRAWS draws pelo caminho direto. H da
 * 96: o piscar era a ponte perdendo residencia (438 SpaceResolveAddress
 * status=9) com 16 MB mapeados por device, inclusive nos ~36 devices/min do
 * laco do Explorer, que fazem 1 draw cada e agora nunca criam anel.
 *
 * DX.55 -- DESLIGADO DE NOVO (bisseccao). Corrida 99: travamento duro em ~39 s
 * com o lote ligado, pool persistente e inspecao amostrada; a 98R, com o lote
 * desligado, rodou 5 min. Todas as corridas com lote ligado travaram (94, 95,
 * 96, 99). Uma variavel contra a 99: o pool persistente (DescUs 14 ms -> 32 us,
 * P0) e a inspecao amostrada ficam. Anel e limiar continuam no codigo.
 */
static const BOOL s_Bc250Dx51DrawBatchEnabled = FALSE;

static VkDeviceSize Bc250UmdDrawRingBytesNeeded(
    _In_ BC250_UMD_DEVICE *d,
    _In_ const BC250_PSO_KEY *key,
    BOOL Indexed)
{
    const VkDeviceSize align = BC250_DX49_DRAW_RING_ALIGN;
    VkDeviceSize total = 0;
    uint32_t i;

    for (i = 0; i < BC250_DESCRIPTOR_CB_COUNT && i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        BC250_UMD_RESOURCE *vs = (BC250_UMD_RESOURCE *)d->PipelineState.VsConstantBuffers[i].pDrvPrivate;
        BC250_UMD_RESOURCE *ps = (BC250_UMD_RESOURCE *)d->PipelineState.PsConstantBuffers[i].pDrvPrivate;
        if (vs != NULL && vs->pCpuShadow != NULL) total += (VkDeviceSize)vs->Width + align;
        if (ps != NULL && ps->pCpuShadow != NULL) total += (VkDeviceSize)ps->Width + align;
    }
    for (i = 0; i < key->NumInputBindings; ++i) {
        uint32_t slot = key->InputBindings[i].binding;
        BC250_UMD_RESOURCE *vb;
        if (slot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) continue;
        vb = (BC250_UMD_RESOURCE *)d->PipelineState.VertexBuffers[slot].pDrvPrivate;
        if (vb != NULL && vb->pCpuShadow != NULL && vb->TotalByteSize > 0u)
            total += (VkDeviceSize)vb->TotalByteSize + align;
    }
    if (Indexed) {
        BC250_UMD_RESOURCE *ib = (BC250_UMD_RESOURCE *)d->PipelineState.IndexBuffer.pDrvPrivate;
        if (ib != NULL && ib->pCpuShadow != NULL && ib->TotalByteSize > 0u)
            total += (VkDeviceSize)ib->TotalByteSize + align;
    }
    return total;
}

static BOOL Bc250UmdExecuteVkDraw(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    UINT VertexCount,
    UINT StartVertexLocation,
    UINT InstanceCount,
    UINT StartInstanceLocation,
    BOOL Indexed,
    INT BaseVertexLocation)
{
    BC250_UMD_RTV* rtv;
    BC250_PSO_KEY psoKey;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkCommandBuffer cmd;
    BC250_UMD_SHADER* vs = NULL;
    BC250_UMD_SHADER* ps = NULL;
    BC250_UMD_BLEND_STATE* blend = NULL;
    BC250_UMD_DEPTH_STENCIL_STATE* ds = NULL;
    BC250_UMD_RASTERIZER_STATE* rs = NULL;
    BC250_UMD_ELEMENT_LAYOUT* il = NULL;
    BOOL hasDescriptors = FALSE;
    BOOL directRef = FALSE;
    BOOL usedRing = FALSE;
    BOOL flushNow = FALSE;
    BOOL readbackActive = FALSE;
    uint32_t batchPosition = 0u;

    if (pDevice != NULL) pDevice->LastVkDrawFailureReason = "VULKAN_BRIDGE_INATIVA";
    if (pDevice == NULL || !pDevice->VkBridgeInitialized || !pDevice->VkBridge.DeviceActive) {
        return FALSE;
    }

    if (pDevice->PipelineState.NumBoundRTVs == 0 ||
        pDevice->PipelineState.BoundRTVs[0].pDrvPrivate == NULL) {
        pDevice->LastVkDrawFailureReason = "RTV_NAO_VINCULADO";
        Bc250UmdTrace("Bc250UmdExecuteVkDraw: RECUSADO -- nenhum RTV vinculado (NumBoundRTVs=%u)",
            pDevice->PipelineState.NumBoundRTVs);
        return FALSE;
    }

    rtv = (BC250_UMD_RTV*)pDevice->PipelineState.BoundRTVs[0].pDrvPrivate;
    /* DX.28: depois de RotateResourceIdentities a RTV tem de desenhar na
     * VkImage que o recurso carrega AGORA, nao na da criacao. */
    if (rtv != NULL && rtv->VkRtvCreated && !Bc250UmdVkRtvFollowResource(pDevice, rtv)) {
        pDevice->LastVkDrawFailureReason = "RTV_NAO_SEGUIU_ROTACAO";
        Bc250UmdTrace("Bc250UmdExecuteVkDraw: RECUSADO -- RTV nao conseguiu seguir a imagem do recurso apos rotacao (rtv=%p)", (void*)rtv);
        return FALSE;
    }
    if (rtv == NULL || !rtv->VkRtvCreated || rtv->VkPass == VK_NULL_HANDLE || rtv->VkFb == VK_NULL_HANDLE) {
        pDevice->LastVkDrawFailureReason = "RTV_VULKAN_INVALIDO";
        Bc250UmdTrace("Bc250UmdExecuteVkDraw: RECUSADO -- RTV invalido ou sem VkPass/VkFb (rtv=%p, VkRtvCreated=%d)",
            (void*)rtv, rtv ? (int)rtv->VkRtvCreated : 0);
        return FALSE;
    }

    memset(&psoKey, 0, sizeof(psoKey));
    psoKey.RenderPass = rtv->VkPass;
    psoKey.SampleCount = 1;

    if (pDevice->PipelineState.BoundVS.pDrvPrivate != NULL) {
        vs = (BC250_UMD_SHADER*)pDevice->PipelineState.BoundVS.pDrvPrivate;
        if (vs->VkModule == VK_NULL_HANDLE && vs->SpirvValid) {
            Bc250UmdVkCreateShaderModule(pDevice, vs);
        }
        psoKey.VsModule = vs->VkModule;
        psoKey.pVs = vs;
    }
    if (pDevice->PipelineState.BoundPS.pDrvPrivate != NULL) {
        ps = (BC250_UMD_SHADER*)pDevice->PipelineState.BoundPS.pDrvPrivate;
        if (ps->VkModule == VK_NULL_HANDLE && ps->SpirvValid) {
            Bc250UmdVkCreateShaderModule(pDevice, ps);
        }
        psoKey.PsModule = ps->VkModule;
        psoKey.pPs = ps;
    }

    switch (pDevice->PipelineState.Topology) {
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINESTRIP:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    default:
        psoKey.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    }

    if (pDevice->PipelineState.BoundRasterizerState.pDrvPrivate != NULL) {
        rs = (BC250_UMD_RASTERIZER_STATE*)pDevice->PipelineState.BoundRasterizerState.pDrvPrivate;
        psoKey.PolygonMode = (rs->Desc.FillMode == D3D10_DDI_FILL_WIREFRAME) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        if (rs->Desc.CullMode == D3D10_DDI_CULL_FRONT) {
            psoKey.CullMode = VK_CULL_MODE_FRONT_BIT;
        } else if (rs->Desc.CullMode == D3D10_DDI_CULL_BACK) {
            psoKey.CullMode = VK_CULL_MODE_BACK_BIT;
        } else {
            psoKey.CullMode = VK_CULL_MODE_NONE;
        }
        psoKey.FrontFace = rs->Desc.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    } else {
        psoKey.PolygonMode = VK_POLYGON_MODE_FILL;
        psoKey.CullMode = VK_CULL_MODE_NONE;
        psoKey.FrontFace = VK_FRONT_FACE_CLOCKWISE;
    }

    if (pDevice->PipelineState.BoundBlendState.pDrvPrivate != NULL) {
        const D3D10_DDI_RENDER_TARGET_BLEND_DESC1 *rt0;
        const char *why = NULL;
        BOOL okMap = TRUE;
        blend = (BC250_UMD_BLEND_STATE*)pDevice->PipelineState.BoundBlendState.pDrvPrivate;
        rt0 = &blend->Desc.RenderTarget[0];
        psoKey.BlendEnable = rt0->BlendEnable;
        psoKey.ColorWriteMask = rt0->RenderTargetWriteMask;
        psoKey.AlphaToCoverageEnable = blend->Desc.AlphaToCoverageEnable;
        psoKey.BlendFactorsExplicit = TRUE;
        if (rt0->BlendEnable) {
            okMap = Bc250UmdMapBlendFactor((UINT)rt0->SrcBlend, &psoKey.SrcColorBlendFactor, &why) &&
                    Bc250UmdMapBlendFactor((UINT)rt0->DestBlend, &psoKey.DstColorBlendFactor, &why) &&
                    Bc250UmdMapBlendOp((UINT)rt0->BlendOp, &psoKey.ColorBlendOp, &why) &&
                    Bc250UmdMapBlendFactor((UINT)rt0->SrcBlendAlpha, &psoKey.SrcAlphaBlendFactor, &why) &&
                    Bc250UmdMapBlendFactor((UINT)rt0->DestBlendAlpha, &psoKey.DstAlphaBlendFactor, &why) &&
                    Bc250UmdMapBlendOp((UINT)rt0->BlendOpAlpha, &psoKey.AlphaBlendOp, &why);
        } else {
            psoKey.SrcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            psoKey.DstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            psoKey.ColorBlendOp = VK_BLEND_OP_ADD;
            psoKey.SrcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            psoKey.DstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            psoKey.AlphaBlendOp = VK_BLEND_OP_ADD;
        }
        if (!okMap) {
            pDevice->LastVkDrawFailureReason = why;
            Bc250UmdTrace("Bc250UmdExecuteVkDraw: RECUSADO -- %s (Src=%u Dst=%u Op=%u SrcA=%u DstA=%u OpA=%u)",
                why, (UINT)rt0->SrcBlend, (UINT)rt0->DestBlend, (UINT)rt0->BlendOp,
                (UINT)rt0->SrcBlendAlpha, (UINT)rt0->DestBlendAlpha, (UINT)rt0->BlendOpAlpha);
            return FALSE;
        }
    } else {
        psoKey.BlendEnable = FALSE;
        psoKey.ColorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    if (pDevice->PipelineState.BoundDepthStencilState.pDrvPrivate != NULL) {
        ds = (BC250_UMD_DEPTH_STENCIL_STATE*)pDevice->PipelineState.BoundDepthStencilState.pDrvPrivate;
        psoKey.DepthTestEnable = ds->Desc.DepthEnable;
        psoKey.DepthWriteEnable = (ds->Desc.DepthWriteMask == D3D10_DDI_DEPTH_WRITE_MASK_ALL);
    }

    if (pDevice->PipelineState.InputLayout.pDrvPrivate != NULL) {
        il = (BC250_UMD_ELEMENT_LAYOUT*)pDevice->PipelineState.InputLayout.pDrvPrivate;
        if (il != NULL && il->NumElements > 0) {
            uint32_t vbPresent = 0;
            UINT failedElement = 0;
            const char *why;
            for (uint32_t s = 0; s < 16u && s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++s) {
                BC250_UMD_RESOURCE *vbs = (BC250_UMD_RESOURCE *)pDevice->PipelineState.VertexBuffers[s].pDrvPrivate;
                if (vbs != NULL && vbs->VkBuf != VK_NULL_HANDLE) vbPresent |= (1u << s);
            }
            /* DX.24: traducao pura em bc250_umd_state_translate.c (testada offline). */
            why = Bc250UmdTranslateInputLayout(il->Elements, il->NumElements,
                pDevice->PipelineState.VertexStrides, vbPresent,
                psoKey.InputAttributes, &psoKey.NumInputElements,
                psoKey.InputBindings, &psoKey.NumInputBindings, &failedElement);
            if (why != NULL) {
                pDevice->LastVkDrawFailureReason = why;
                Bc250UmdTrace("Bc250UmdExecuteVkDraw: RECUSADO -- %s (elementos=%u elemento=%u)",
                    why, il->NumElements, failedElement);
                return FALSE;
            }
        }
    }

    /*
     * DX.49 -- O DRAW SO' GRAVA. Ate a Corrida 93 havia um flush com espera
     * ANTES e outro DEPOIS de cada draw (425 flushes/min, ~100 ms de parede por
     * draw). A vida util que aquele flush protegia agora tem dono explicito:
     *  - VB/IB/CB vao para uma faixa propria do anel (Bc250UmdVkDrawRingPut);
     *  - conjuntos de descritores so' sao liberados na fence do lote;
     *  - quem destroi objeto (recurso, RTV, DSV, shader, pipeline despejada)
     *    drena antes (Bc250UmdVkDrainPending);
     *  - quem nao cabe no anel vai pelo caminho direto de antes, drenando
     *    antes e com flush ao fim do draw.
     * O render pass aberto por um clear e' fechado sem flush.
     */
    pDevice->DrawUsesRing = FALSE;
    if (pDevice->InRenderPass && pDevice->ActiveFramebuffer != rtv->VkFb) {
        pDevice->VkBridge.vk.vkCmdEndRenderPass(pDevice->VkBridge.CommandBuffer);
        pDevice->InRenderPass = FALSE;
    }
    {
        VkDeviceSize ringNeed = Bc250UmdDrawRingBytesNeeded(pDevice, &psoKey, Indexed);
        /* DX.51: desligado, o anel nem e' criado. DX.54: e nem nasce em device
         * de vida curta (menos de BC250_DX54_RING_MIN_DIRECT_DRAWS draws). */
        BOOL ringAllowed = s_Bc250Dx51DrawBatchEnabled &&
            pDevice->NativeVkDrawCount >= BC250_DX54_RING_MIN_DIRECT_DRAWS;
        VkDeviceSize ringFree = ringAllowed ? Bc250UmdVkDrawRingFree(&pDevice->VkBridge) : 0;
        if (ringAllowed &&
            pDevice->VkBridge.DrawRingMapped != NULL && ringNeed <= pDevice->VkBridge.DrawRingSize) {
            if (ringNeed > ringFree &&
                Bc250UmdVkDrainPending(&pDevice->VkBridge, "anel de draws sem espaco") != VK_SUCCESS) {
                pDevice->LastVkDrawFailureReason = "DRENO_DO_ANEL_FALHOU";
                return FALSE;
            }
            pDevice->DrawUsesRing = TRUE;
        } else if (pDevice->VkBridge.PendingDraws >= BC250_DX49_BATCH_MAX_DRAWS ||
                   (pDevice->VkBridge.PersistentPoolSets >= BC250_DX54_POOL_MAX_SETS - 2)) {
            /*
             * DX.77: no caminho direto, draws coalescem livremente no mesmo command buffer
             * ate o limite do lote (BC250_DX49_BATCH_MAX_DRAWS) ou ate quase esgotar os
             * conjuntos do pool persistente. O dreno incondicional anterior a cada draw
             * (PendingDraws > 0) destruia o lote e forcava ~8 flushes/quadro no DWM (~120 ms).
             * Agora, o flush ocorre de forma consolidada no Present (Bc250DxgiPrepareScanout).
             */
            if (Bc250UmdVkDrainPending(&pDevice->VkBridge, "teto do lote no caminho direto") != VK_SUCCESS) {
                pDevice->LastVkDrawFailureReason = "FLUSH_ANTES_DO_DRAW_FALHOU";
                return FALSE;
            }
        }
    }
    if (!Bc250UmdPipelineCacheGetOrCreate(&pDevice->PipelineCache, &psoKey, &pipeline, &layout)) {
        pDevice->LastVkDrawFailureReason = "PIPELINE_NAO_OBTIDA_OU_COMPILADA";
        Bc250UmdTrace("Bc250UmdExecuteVkDraw: falha ao obter/compilar VkPipeline");
        return FALSE;
    }

    cmd = Bc250UmdVkGetCommandBuffer(&pDevice->VkBridge);
    if (cmd == VK_NULL_HANDLE) {
        pDevice->LastVkDrawFailureReason = "COMMAND_BUFFER_AUSENTE";
        return FALSE;
    }

    /* DX.75: se a pipeline exige descritores (BindingCount > 0), o render pass aberto por
     * ClearRenderTargetView precisa ser fechado antes de Bc250UmdBindDrawDescriptors
     * para permitir barreiras de imagem se houver SRVs. Se BindingCount == 0, o render pass
     * permanece aberto e coalescido! */
    if (pDevice->InRenderPass) {
        uint32_t bCount = 0;
        for (uint32_t e = 0; e < BC250_PSO_CACHE_CAPACITY; ++e) {
            if (pDevice->PipelineCache.Entries[e].InUse &&
                pDevice->PipelineCache.Entries[e].PipelineLayout == layout) {
                bCount = pDevice->PipelineCache.Entries[e].BindingCount;
                break;
            }
        }
        if (bCount > 0) {
            pDevice->VkBridge.vk.vkCmdEndRenderPass(cmd);
            pDevice->InRenderPass = FALSE;
        }
    }

    /*
     * T4.3: superficies abertas sao carregadas ANTES do batch de descritores.
     * A carga drena o lote; o FlushCommands libera os batches e encerra a
     * gravacao, entao o command buffer e' obtido de novo depois (Corrida 182:
     * c0000005 em bc250umd.dll+0x8376). Com render pass aberto nao ha binding
     * de descritor (DX.75 acima), logo nao ha o que carregar.
     */
    if (!pDevice->InRenderPass) {
        Bc250UmdMaterializeDrawSrvs(pDevice, layout);
        cmd = Bc250UmdVkGetCommandBuffer(&pDevice->VkBridge);
        if (cmd == VK_NULL_HANDLE) {
            pDevice->LastVkDrawFailureReason = "COMMAND_BUFFER_AUSENTE_APOS_CARGA_DE_SRV";
            return FALSE;
        }
    }

    if (!Bc250UmdBindDrawDescriptors(pDevice, layout, cmd, &hasDescriptors)) return FALSE;

    if (!pDevice->InRenderPass || pDevice->ActiveFramebuffer != rtv->VkFb) {
        VkRenderPassBeginInfo rpBegin;
        VkClearValue clearVal;
        VkRenderPass passToUse = (rtv->VkPassLoad != VK_NULL_HANDLE) ? rtv->VkPassLoad : rtv->VkPass;

        if (pDevice->InRenderPass) {
            pDevice->VkBridge.vk.vkCmdEndRenderPass(cmd);
            pDevice->InRenderPass = FALSE;
        }
        memset(&clearVal, 0, sizeof(clearVal));

        memset(&rpBegin, 0, sizeof(rpBegin));
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = passToUse;
        rpBegin.framebuffer = rtv->VkFb;
        rpBegin.renderArea.offset.x = 0;
        rpBegin.renderArea.offset.y = 0;
        rpBegin.renderArea.extent.width = (rtv->pResource != NULL && rtv->pResource->Width > 0) ? rtv->pResource->Width : 1;
        rpBegin.renderArea.extent.height = (rtv->pResource != NULL && rtv->pResource->Height > 0) ? rtv->pResource->Height : 1;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearVal;

        pDevice->VkBridge.vk.vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        pDevice->InRenderPass = TRUE;
        pDevice->ActiveRenderPass = passToUse;
        pDevice->ActiveFramebuffer = rtv->VkFb;
    }

    if (pDevice->PipelineState.NumViewports > 0) {
        VkViewport vp;
        /*
         * DX.24: D3D tem y=+1 no TOPO do NDC; Vulkan tem y=+1 EMBAIXO. Altura
         * negativa (Vulkan 1.1 / maintenance1) reproduz o D3D, como o vkd3d faz.
         * Antes a imagem saia de cabeca para baixo e com o enrolamento invertido.
         */
        vp.x = pDevice->PipelineState.Viewports[0].TopLeftX;
        vp.y = pDevice->PipelineState.Viewports[0].TopLeftY + pDevice->PipelineState.Viewports[0].Height;
        vp.width = pDevice->PipelineState.Viewports[0].Width;
        vp.height = -pDevice->PipelineState.Viewports[0].Height;
        vp.minDepth = pDevice->PipelineState.Viewports[0].MinDepth;
        vp.maxDepth = pDevice->PipelineState.Viewports[0].MaxDepth;
        pDevice->VkBridge.vk.vkCmdSetViewport(cmd, 0, 1, &vp);
    } else {
        VkViewport vp;
        vp.x = 0.0f;
        vp.width = (float)((rtv->pResource != NULL) ? rtv->pResource->Width : 1920);
        vp.y = (float)((rtv->pResource != NULL) ? rtv->pResource->Height : 1080);
        vp.height = -vp.y;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        pDevice->VkBridge.vk.vkCmdSetViewport(cmd, 0, 1, &vp);
    }

    if (pDevice->PipelineState.NumScissorRects > 0) {
        VkRect2D sc;
        sc.offset.x = pDevice->PipelineState.ScissorRects[0].left;
        sc.offset.y = pDevice->PipelineState.ScissorRects[0].top;
        sc.extent.width = pDevice->PipelineState.ScissorRects[0].right - pDevice->PipelineState.ScissorRects[0].left;
        sc.extent.height = pDevice->PipelineState.ScissorRects[0].bottom - pDevice->PipelineState.ScissorRects[0].top;
        pDevice->VkBridge.vk.vkCmdSetScissor(cmd, 0, 1, &sc);
    } else {
        VkRect2D sc;
        sc.offset.x = 0;
        sc.offset.y = 0;
        sc.extent.width = (rtv->pResource != NULL) ? rtv->pResource->Width : 1920;
        sc.extent.height = (rtv->pResource != NULL) ? rtv->pResource->Height : 1080;
        pDevice->VkBridge.vk.vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    pDevice->VkBridge.vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    /* DX.24: o pipeline declara BLEND_CONSTANTS dinamico; o valor e' o do OMSetBlendState. */
    pDevice->VkBridge.vk.vkCmdSetBlendConstants(cmd, pDevice->PipelineState.BlendFactor);
    Bc250UmdTraceDrawData(pDevice, StartVertexLocation);
    Bc250UmdTraceDrawVertices(pDevice, Indexed, StartVertexLocation, BaseVertexLocation);

    for (uint32_t b = 0; b < psoKey.NumInputBindings; ++b) {
        uint32_t slot = psoKey.InputBindings[b].binding;
        if (slot < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT &&
            pDevice->PipelineState.VertexBuffers[slot].pDrvPrivate != NULL) {
            BC250_UMD_RESOURCE* vb = (BC250_UMD_RESOURCE*)pDevice->PipelineState.VertexBuffers[slot].pDrvPrivate;
            if (vb != NULL && vb->VkBuf != VK_NULL_HANDLE) {
                VkBuffer bindBuf = vb->VkBuf;
                VkDeviceSize offset = pDevice->PipelineState.VertexOffsets[slot];
                if (vb->pCpuShadow != NULL && vb->TotalByteSize > 0u && vb->VkMem != VK_NULL_HANDLE) {
                    UINT uploadBytes = (vb->Width > 0 && vb->Width < vb->TotalByteSize) ? vb->Width : vb->TotalByteSize;
                    if (pDevice->DrawUsesRing) {
                        VkDeviceSize ringOff = 0;
                        if (!Bc250UmdVkDrawRingPut(&pDevice->VkBridge, vb->pCpuShadow, uploadBytes, &bindBuf, &ringOff)) {
                            pDevice->LastVkDrawFailureReason = "VB_ANEL_SEM_ESPACO";
                            return FALSE;
                        }
                        offset += ringOff;
                    } else if (!Bc250UmdVkUploadBufferData(pDevice, vb, vb->pCpuShadow, uploadBytes)) {
                        return FALSE;
                    }
                } else {
                    /* Sem sombra: o draw le a memoria do proprio recurso, que um
                     * Unmap posterior reescreve. Nao pode ficar pendente. */
                    directRef = TRUE;
                }
                pDevice->VkBridge.vk.vkCmdBindVertexBuffers(cmd, slot, 1, &bindBuf, &offset);
            }
        }
        /* DX.24: slot sem buffer ja foi recusado antes do flush; nada e' substituido aqui. */
    }

    if (Indexed) {
        if (pDevice->PipelineState.IndexBuffer.pDrvPrivate != NULL) {
            BC250_UMD_RESOURCE* ib = (BC250_UMD_RESOURCE*)pDevice->PipelineState.IndexBuffer.pDrvPrivate;
            if (ib != NULL && ib->VkBuf != VK_NULL_HANDLE) {
                VkBuffer ibBuf = ib->VkBuf;
                VkDeviceSize ibOffset = pDevice->PipelineState.IndexOffset;
                if (ib->pCpuShadow != NULL && ib->TotalByteSize > 0u && ib->VkMem != VK_NULL_HANDLE) {
                    UINT uploadBytes = (ib->Width > 0 && ib->Width < ib->TotalByteSize) ? ib->Width : ib->TotalByteSize;
                    if (pDevice->DrawUsesRing) {
                        VkDeviceSize ringOff = 0;
                        if (!Bc250UmdVkDrawRingPut(&pDevice->VkBridge, ib->pCpuShadow, uploadBytes, &ibBuf, &ringOff)) {
                            pDevice->LastVkDrawFailureReason = "IB_ANEL_SEM_ESPACO";
                            return FALSE;
                        }
                        ibOffset += ringOff;
                    } else if (!Bc250UmdVkUploadBufferData(pDevice, ib, ib->pCpuShadow, uploadBytes)) {
                        return FALSE;
                    }
                } else {
                    directRef = TRUE;
                }
                VkIndexType idxType = (pDevice->PipelineState.IndexFormat == DXGI_FORMAT_R16_UINT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                pDevice->VkBridge.vk.vkCmdBindIndexBuffer(cmd, ibBuf, ibOffset, idxType);
            }
        }
        pDevice->VkBridge.vk.vkCmdDrawIndexed(
            cmd, VertexCount, (InstanceCount > 0 ? InstanceCount : 1),
            StartVertexLocation, BaseVertexLocation, StartInstanceLocation);
    } else {
        pDevice->VkBridge.vk.vkCmdDraw(
            cmd, VertexCount, (InstanceCount > 0 ? InstanceCount : 1),
            StartVertexLocation, StartInstanceLocation);
    }

    pDevice->VkBridge.vk.vkCmdEndRenderPass(cmd);
    pDevice->InRenderPass = FALSE;
    if (rtv->pResource) rtv->pResource->VkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (hasDescriptors) Bc250UmdRestoreDescriptorImages(&pDevice->VkBridge, cmd);
    pDevice->VkBridge.PendingDraws++;
    batchPosition = pDevice->VkBridge.PendingDraws;
    usedRing = pDevice->DrawUsesRing;
    pDevice->DrawUsesRing = FALSE;
    /* DX.76: o draw permanece gravado no command buffer sem flush sincrono
     * imediato se houver Present subsequente, unificando Clear+Draw+Destile
     * em UMA UNICA submissao ao hardware por quadro. Flushes ocorrem apenas
     * se directRef for true, no limite de lotes ou durante readback ativo. */
    readbackActive = (Bc250UmdDrawReadbackTargetQualifies(rtv->pResource) &&
        pDevice->DrawReadbackCount < BC250_DRAW_READBACK_MAX);
    flushNow = (directRef || readbackActive || batchPosition >= BC250_DX49_BATCH_MAX_DRAWS);
    if (flushNow) {
        if (Bc250UmdVkFlushCommands(&pDevice->VkBridge) != VK_SUCCESS) {
            pDevice->LastVkDrawFailureReason = "FLUSH_FENCE_DO_DRAW_FALHOU";
            return FALSE;
        }
    }
    pDevice->LastVkDrawFailureReason = NULL;
    pDevice->NativeVkDrawCount++;
    pDevice->PipelineState.DrawCount++;
    /* DX.23: nomeia o alvo do draw, para cruzar com hSurfaceToPresent (Corrida 68).
     * DX.49: o draw foi GRAVADO; so' ha fence concluida quando FlushAgora=1 ou
     * no proximo flush do lote ([DX49_BATCH] no FlushCommands). */
    Bc250UmdTrace("Bc250UmdExecuteVkDraw: DRAW GRAVADO NO LOTE; pixels NAO CONFERIDOS (VkDraws=%llu, Total=%llu, Verts=%u, Inst=%u, Indexed=%d) "
        "[DX49_BATCH] Lote=%u Anel=%d Direto=%d FlushAgora=%d RT0=%p %ux%u img=%p",
        pDevice->NativeVkDrawCount, pDevice->PipelineState.DrawCount, VertexCount, InstanceCount, (int)Indexed,
        batchPosition, (int)usedRing, (int)directRef, (int)flushNow,
        (void*)rtv->pResource, rtv->pResource ? rtv->pResource->Width : 0u,
        rtv->pResource ? rtv->pResource->Height : 0u,
        rtv->pResource ? (void*)rtv->pResource->VkImg : NULL);
    Bc250UmdReadbackDrawTarget(pDevice, rtv->pResource);
    return TRUE;
}

VOID APIENTRY Bc250Umd10Draw(
    D3D10DDI_HDEVICE hDevice,
    UINT VertexCount,
    UINT StartVertexLocation)
{
    BC250_UMD_DEVICE* device = (BC250_UMD_DEVICE*)hDevice.pDrvPrivate;
    if (device == NULL) return;
    if (Bc250UmdExecuteVkDraw(device, VertexCount, StartVertexLocation, 1, 0, FALSE, 0)) return;
    if (!device->VkBridgeInitialized && Bc250UmdSubstituteDrawWithBlt(device)) return;
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
    if (Bc250UmdExecuteVkDraw(device, IndexCount, StartIndexLocation, 1, 0, TRUE, BaseVertexLocation)) return;
    if (!device->VkBridgeInitialized && Bc250UmdSubstituteDrawWithBlt(device)) return;
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

    if (device == NULL) return;
    if (Bc250UmdExecuteVkDraw(device, VertexCountPerInstance, StartVertexLocation, InstanceCount, StartInstanceLocation, FALSE, 0)) return;
    if (!device->VkBridgeInitialized && Bc250UmdSubstituteDrawWithBlt(device)) return;
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

    if (device == NULL) return;
    if (Bc250UmdExecuteVkDraw(device, IndexCountPerInstance, StartIndexLocation, InstanceCount, StartInstanceLocation, TRUE, BaseVertexLocation)) return;
    if (!device->VkBridgeInitialized && Bc250UmdSubstituteDrawWithBlt(device)) return;
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

/* Real PM4 3D draw submission on Node 1 (Parede B).
 * Tool for KMD/UMD bringup and diagnostic validation of Node 1 GFX ring (CP_RB0).
 * Excluded from production D3D10/11 Draw DDIs (D1.1).
 * Builds CB_COLOR0, Scissor, IT_DRAW_INDEX_AUTO and flushes command buffer. */
BOOL Bc250UmdEmitPm4Draw(
    _Inout_ BC250_UMD_DEVICE* pDevice,
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    BOOL Indexed)
{
    BC250_UMD_COMMAND_BUFFER* cmd;
    BC250_UMD_RTV* rtv;
    BC250_UMD_RESOURCE* dst;
    BC250_PM4_STATUS pm4Status;
    uint32_t initialCursor;
    uint32_t drawInitiator;
    uint32_t compSwap;
    uint32_t fmtIndex;
    uint32_t scissorRight;
    uint32_t scissorBottom;
    uint32_t emittedDwords;
    HRESULT hr;

    UNREFERENCED_PARAMETER(StartIndexLocation);
    UNREFERENCED_PARAMETER(BaseVertexLocation);
    UNREFERENCED_PARAMETER(Indexed);

    /* Fail-closed: sem tunable explicito, draw PM4 e' recusado honestamente (Lei 4) */
    if (!Bc250UmdExperimentalPm4DrawArmed()) {
        return FALSE;
    }

    if (pDevice == NULL || !pDevice->NativeNode1Ready ||
        !pDevice->Winsys.ContextCreated ||
        pDevice->Winsys.NodeOrdinal != BC250_WINSYS_NODE_COMPUTE ||
        IndexCount == 0u) {
        return FALSE;
    }

    cmd = &pDevice->Node1CmdBuffer;
    if (!cmd->Initialized || cmd->pCpuVa == NULL || cmd->CapacityDwords == 0u) {
        return FALSE;
    }

    if (pDevice->PipelineState.NumBoundRTVs == 0 ||
        pDevice->PipelineState.BoundRTVs[0].pDrvPrivate == NULL) {
        return FALSE;
    }

    rtv = (BC250_UMD_RTV*)pDevice->PipelineState.BoundRTVs[0].pDrvPrivate;
    if (!rtv->Created || rtv->pResource == NULL) {
        return FALSE;
    }

    dst = rtv->pResource;
    if (!dst->Allocation.Created || !dst->Allocation.Resident ||
        dst->Allocation.GpuVirtualAddress == 0u ||
        dst->Width == 0u || dst->Height == 0u) {
        return FALSE;
    }

    /* Base address must be 256-byte aligned for GFX10 CB */
    if ((dst->Allocation.GpuVirtualAddress & 0xFFull) != 0u) {
        Bc250UmdTrace("EmitPm4Draw RECUSOU: BaseGpuVa 0x%llX nao alinhado a 256 bytes",
            (unsigned long long)dst->Allocation.GpuVirtualAddress);
        return FALSE;
    }

    /* Reserve space in command buffer (15 dwords minimum for CB + Scissor + Draw) */
    if (cmd->CursorDwords + 32u > cmd->CapacityDwords) {
        hr = Bc250UmdCommandBufferFlush(pDevice, cmd, BC250_WINSYS_NODE_COMPUTE);
        if (FAILED(hr)) {
            return FALSE;
        }
    }

    initialCursor = cmd->CursorDwords;

    /*
     * Format mapping for GFX10 Color Target.
     *
     * CLASSE H -- NAO DERIVADO. O valor 0x16 para COLOR_8_8_8_8 NAO tem fonte neste
     * repositorio: inc/linux_ref/gc_10_1_0_sh_mask.h traz a LARGURA do campo
     * (CB_COLOR0_INFO__FORMAT, [6:2]) mas NAO o enum de formato do CB, que vive em
     * gc_10_1_0_enum.h -- ausente aqui. O mesmo vale para compSwap (SWAP_STD /
     * SWAP_STD_REV). Ver docs/03, bloco "GFX10 3D", divida 1.
     *
     * Enquanto isso for H, o caminho fica atras do tunable D3D11ExperimentalPm4Draw,
     * que e' fail-closed. Nao citar estes dois valores como P1 em relatorio algum.
     */
    fmtIndex = 0x16u;
    switch (dst->Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        compSwap = 1u;
        break;
    default:
        compSwap = 0u;
        break;
    }

    /* 1. Emit CB_COLOR0 context registers */
    pm4Status = Bc250Pm4EmitColorTarget0(
        cmd->pCpuVa,
        cmd->CapacityDwords,
        &cmd->CursorDwords,
        dst->Allocation.GpuVirtualAddress,
        dst->Width,
        dst->Height,
        dst->RowPitch ? dst->RowPitch : (dst->Width * 4u),
        fmtIndex,
        compSwap);
    if (pm4Status != BC250_PM4_OK) {
        cmd->CursorDwords = initialCursor;
        return FALSE;
    }

    /* 2. Emit Window Scissor */
    scissorRight = dst->Width;
    scissorBottom = dst->Height;
    if (pDevice->PipelineState.NumScissorRects > 0 &&
        pDevice->PipelineState.ScissorRects[0].right > pDevice->PipelineState.ScissorRects[0].left &&
        pDevice->PipelineState.ScissorRects[0].bottom > pDevice->PipelineState.ScissorRects[0].top) {
        scissorRight = (uint32_t)pDevice->PipelineState.ScissorRects[0].right;
        scissorBottom = (uint32_t)pDevice->PipelineState.ScissorRects[0].bottom;
    } else if (pDevice->PipelineState.NumViewports > 0 &&
               pDevice->PipelineState.Viewports[0].Width > 0 &&
               pDevice->PipelineState.Viewports[0].Height > 0) {
        scissorRight = (uint32_t)(pDevice->PipelineState.Viewports[0].TopLeftX + pDevice->PipelineState.Viewports[0].Width);
        scissorBottom = (uint32_t)(pDevice->PipelineState.Viewports[0].TopLeftY + pDevice->PipelineState.Viewports[0].Height);
    }

    pm4Status = Bc250Pm4EmitWindowScissor(
        cmd->pCpuVa,
        cmd->CapacityDwords,
        &cmd->CursorDwords,
        0u, 0u, scissorRight, scissorBottom);
    if (pm4Status != BC250_PM4_OK) {
        cmd->CursorDwords = initialCursor;
        return FALSE;
    }

    /* 3. Map topology to PM4 primitive type and emit DRAW_INDEX_AUTO */
    switch (pDevice->PipelineState.Topology) {
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_POINTLIST:
        drawInitiator = BC250_PM4_DI_PT_POINTLIST;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_LINELIST:
        drawInitiator = BC250_PM4_DI_PT_LINELIST;
        break;
    case D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    default:
        drawInitiator = BC250_PM4_DI_PT_TRIANGLELIST;
        break;
    }

    pm4Status = Bc250Pm4EmitDrawIndexAuto(
        cmd->pCpuVa,
        cmd->CapacityDwords,
        &cmd->CursorDwords,
        IndexCount,
        drawInitiator);
    if (pm4Status != BC250_PM4_OK) {
        cmd->CursorDwords = initialCursor;
        return FALSE;
    }

    emittedDwords = cmd->CursorDwords - initialCursor;

    /* Flush command buffer to Node 1 via Winsys submit */
    hr = Bc250UmdCommandBufferFlush(pDevice, cmd, BC250_WINSYS_NODE_COMPUTE);
    if (FAILED(hr)) {
        cmd->CursorDwords = initialCursor;
        return FALSE;
    }

    pDevice->NativePm4DrawCount++;
    pDevice->PipelineState.DrawCount++;
    pDevice->PipelineState.TotalIndexCount += IndexCount;

    Bc250UmdTrace("EmitPm4Draw: IndexCount=%u Topology=%u ColorBase=0x%llX (%ux%u) PM4 Dwords=%u NativePm4DrawCount=%llu",
        IndexCount, drawInitiator,
        (unsigned long long)dst->Allocation.GpuVirtualAddress,
        dst->Width, dst->Height,
        emittedDwords,
        pDevice->NativePm4DrawCount);

    return TRUE;
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

    Bc250UmdTrace("CreateBlendState: state=%p AlphaToCoverage=%d IndBlend=%d "
        "RT0 En=%d Src=%u Dst=%u Op=%u SrcA=%u DstA=%u OpA=%u Mask=0x%X",
        (void*)state, pBlendDesc->AlphaToCoverageEnable, pBlendDesc->IndependentBlendEnable,
        (int)pBlendDesc->RenderTarget[0].BlendEnable,
        (unsigned)pBlendDesc->RenderTarget[0].SrcBlend, (unsigned)pBlendDesc->RenderTarget[0].DestBlend,
        (unsigned)pBlendDesc->RenderTarget[0].BlendOp, (unsigned)pBlendDesc->RenderTarget[0].SrcBlendAlpha,
        (unsigned)pBlendDesc->RenderTarget[0].DestBlendAlpha, (unsigned)pBlendDesc->RenderTarget[0].BlendOpAlpha,
        (unsigned)pBlendDesc->RenderTarget[0].RenderTargetWriteMask);
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

    Bc250UmdTrace("CreateSampler: sampler=%p Filter=%u AddressU=%u AddressV=%u AddressW=%u "
        "MaxAniso=%u Cmp=%u LodBias=%.2f MinLod=%.2f MaxLod=%.2f",
        (void*)sampler, (UINT)pSamplerDesc->Filter, (UINT)pSamplerDesc->AddressU,
        (UINT)pSamplerDesc->AddressV, (UINT)pSamplerDesc->AddressW,
        (UINT)pSamplerDesc->MaxAnisotropy, (UINT)pSamplerDesc->ComparisonFunc,
        (double)pSamplerDesc->MipLODBias, (double)pSamplerDesc->MinLOD, (double)pSamplerDesc->MaxLOD);
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
    for (UINT i = 0; i < layout->NumElements && i < 16; ++i) {
        Bc250UmdTrace("  Element[%u]: Slot=%u Fmt=%u Offset=%u Class=%u",
            i, layout->Elements[i].InputSlot, (UINT)layout->Elements[i].Format,
            layout->Elements[i].AlignedByteOffset, (UINT)layout->Elements[i].InputSlotClass);
    }
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

HRESULT Bc250UmdBridgeGpuBlt(
    _Inout_ BC250_UMD_DEVICE *d,
    uint64_t srcGpuVa,
    UINT srcPitch,
    uint64_t dstGpuVa,
    UINT dstPitch,
    UINT width,
    UINT height)
{
    BC250_ESCAPE_COMPOSITE_BLT payload;
    D3DDDICB_ESCAPE args;
    HRESULT hr;

    if (d == NULL || srcGpuVa == 0 || dstGpuVa == 0 || width == 0 || height == 0) {
        Bc250UmdTrace("BRIDGE GPU BLT RECUSADO: argumentos invalidos (srcVa=0x%llX dstVa=0x%llX %ux%u)",
            (unsigned long long)srcGpuVa, (unsigned long long)dstGpuVa, width, height);
        return E_INVALIDARG;
    }
    if (!d->WinsysInitialized || d->pAdapter == NULL ||
        d->pAdapter->hRTAdapter.handle == NULL ||
        d->Winsys.Callbacks == NULL ||
        d->Winsys.Callbacks->pfnEscapeCb == NULL) {
        Bc250UmdTrace("BRIDGE GPU BLT RECUSADO: contrato de escape ausente");
        return E_NOTIMPL;
    }
    if (d->VkBridgeInitialized &&
        Bc250UmdVkDrainPending(&d->VkBridge, "ponte Vulkan GPU Blt") != VK_SUCCESS) {
        Bc250UmdTrace("BRIDGE GPU BLT RECUSADO: dreno do lote Vulkan falhou");
        return E_FAIL;
    }

    memset(&payload, 0, sizeof(payload));
    payload.Opcode = BC250_ESCAPE_OP_COMPOSITE_BLT;
    payload.SourceGpuVa = srcGpuVa;
    payload.DestGpuVa = dstGpuVa;
    payload.Width = width;
    payload.Height = height;
    payload.SourcePitch = srcPitch;
    payload.DestPitch = dstPitch;
    payload.DestX = 0;
    payload.DestY = 0;

    memset(&args, 0, sizeof(args));
    args.hDevice = d->Winsys.RuntimeDevice;
    args.hContext = d->Winsys.PresentContextCreated ? d->Winsys.PresentContext : NULL;
    args.pPrivateDriverData = &payload;
    args.PrivateDriverDataSize = (UINT)sizeof(payload);
    args.Flags.HardwareAccess = 0u;

    Bc250UmdTrace("BRIDGE GPU BLT ENTER: %ux%u SrcVa=0x%llX DstVa=0x%llX Pitch=%u/%u",
        width, height,
        (unsigned long long)payload.SourceGpuVa,
        (unsigned long long)payload.DestGpuVa,
        payload.SourcePitch, payload.DestPitch);

    hr = d->Winsys.Callbacks->pfnEscapeCb(
        d->pAdapter->hRTAdapter.handle, &args);

    Bc250UmdTrace("BRIDGE GPU BLT LEAVE: hr=0x%08X ResultCode=0x%08X",
        (unsigned)hr, payload.ResultCode);

    if (FAILED(hr)) return hr;
    if (payload.ResultCode != BC250_PRESENT_BLT_STATUS_SUCCESS) return E_FAIL;
    return S_OK;
}

static HRESULT Bc250UmdBridgeLinearToWddm(
    _Inout_ BC250_UMD_DEVICE *d,
    uint64_t srcGpuVa,
    _In_ BC250_UMD_RESOURCE *dstRes)
{
    UINT pitch;

    if (d == NULL || dstRes == NULL || srcGpuVa == 0 ||
        dstRes->Allocation.GpuVirtualAddress == 0) {
        Bc250UmdTrace("BRIDGE VULKAN->WDDM RECUSADO: argumentos invalidos (srcVa=0x%llX dstVa=0x%llX)",
            (unsigned long long)srcGpuVa,
            dstRes ? (unsigned long long)dstRes->Allocation.GpuVirtualAddress : 0ULL);
        return E_INVALIDARG;
    }

    /* DX.49: o KMD le a origem fora da fila Vulkan; nada do lote pode faltar. */
    if (d->VkBridgeInitialized &&
        Bc250UmdVkDrainPending(&d->VkBridge, "ponte Vulkan->WDDM") != VK_SUCCESS) {
        Bc250UmdTrace("BRIDGE VULKAN->WDDM RECUSADO: dreno do lote Vulkan falhou");
        return E_FAIL;
    }

    pitch = dstRes->RowPitch ? dstRes->RowPitch : (dstRes->Width * 4);
    return Bc250UmdBridgeGpuBlt(d, srcGpuVa, dstRes->Width * 4,
        dstRes->Allocation.GpuVirtualAddress, pitch, dstRes->Width, dstRes->Height);
}

static BOOL Bc250UmdPresentVulkanDetileAndScanout(
    _Inout_ BC250_UMD_DEVICE* device,
    _In_opt_ BC250_UMD_RESOURCE* srcRes)
{
    BOOL vkRendered = FALSE;

    if (device == NULL || srcRes == NULL) {
        return FALSE;
    }

    /*
     * Verifica se houve renderizacao real na Vulkan Bridge:
     * Quer seja via Draw nativo (NativeVkDrawCount > 0) ou Fast-Clear (InRenderPass ativo).
     */
    if (device->VkBridgeInitialized && device->VkBridge.DeviceActive) {
        if (device->InRenderPass) {
            device->VkBridge.vk.vkCmdEndRenderPass(device->VkBridge.CommandBuffer);
            device->InRenderPass = FALSE;
            vkRendered = TRUE;
        } else if (device->NativeVkDrawCount > 0) {
            vkRendered = TRUE;
        }
    }

    /*
     * D1.2: Deteccao de divergencia entre renderizador e apresentador.
     * Se ambos os backends renderizaram, ou se o render ocorreu em PM4 enquanto
     * a Vulkan Bridge tambem declara render, recusa honestamente com motivo nomeado.
     */
    if (device->NativePm4DrawCount > 0 && (device->NativeVkDrawCount > 0 || vkRendered)) {
        Bc250UmdTrace("DXGI Present RECUSADO: [PRESENT RECUSADO: DIVERGENCIA_BACKEND_DRAW_VS_PRESENT] "
            "(NativeVkDrawCount=%llu, NativePm4DrawCount=%llu, InRenderPass=%d) - render e apresentacao divergem",
            device->NativeVkDrawCount, device->NativePm4DrawCount, (int)device->InRenderPass);
        return FALSE;
    }

    /*
     * DX.81: Se nenhum render novo foi detectado neste quadro (quadro inicial do DWM ou
     * sem draws pendentes), nao destila zeros e repassa a alocacao WDDM existente
     * para pfnPresentCb sem reprovar com E_FAIL (evitando o laco de crash/reinicio do DWM).
     */
     if (!vkRendered && device->NativePm4DrawCount == 0) {
         Bc250UmdTrace("[DX81_PASSTHROUGH] Nenhum render novo detectado (VkRendered=0, NativePm4DrawCount=0, NativeVkDrawCount=%llu) -> repassa para pfnPresentCb",
             device->NativeVkDrawCount);
         return TRUE;
     }

    /*
     * Rota 1: Renderizacao Vulkan ocorreu sobre srcRes->VkImg.
     * Destila a imagem Vulkan para a alocacao WDDM. O callback DXGI Present
     * entrega essa alocacao ao KMD; a UMD nao escreve no scanout fisico.
     */
    if (vkRendered && srcRes->VkResourceCreated &&
        srcRes->VkImg != VK_NULL_HANDLE && srcRes->Width > 0 && srcRes->Height > 0) {

        BOOL hasLinear = Bc250UmdVkEnsureLinearPresentBuf(&device->VkBridge, srcRes->Width, srcRes->Height);
        if (hasLinear && device->VkBridge.LinearPresentCreated && device->VkBridge.LinearPresentBuf != VK_NULL_HANDLE) {
            VkCommandBuffer cmdBuf = Bc250UmdVkGetCommandBuffer(&device->VkBridge);
            if (cmdBuf != VK_NULL_HANDLE) {
                if (device->InRenderPass) {
                    device->VkBridge.vk.vkCmdEndRenderPass(cmdBuf);
                    device->InRenderPass = FALSE;
                }
                /* 1. Barreira de Imagem: COLOR_ATTACHMENT -> TRANSFER_SRC */
                VkImageMemoryBarrier imb = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imb.image = srcRes->VkImg;
                imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imb.subresourceRange.baseMipLevel = 0;
                imb.subresourceRange.levelCount = 1;
                imb.subresourceRange.baseArrayLayer = 0;
                imb.subresourceRange.layerCount = 1;

                device->VkBridge.vk.vkCmdPipelineBarrier(cmdBuf,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, NULL, 0, NULL, 1, &imb);

                /* 2. Destiling por GPU via vkCmdCopyImageToBuffer */
                VkBufferImageCopy copyRegion = {0};
                copyRegion.bufferOffset = 0;
                copyRegion.bufferRowLength = srcRes->Width;
                copyRegion.bufferImageHeight = srcRes->Height;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = 0;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageOffset.x = 0;
                copyRegion.imageOffset.y = 0;
                copyRegion.imageOffset.z = 0;
                copyRegion.imageExtent.width = srcRes->Width;
                copyRegion.imageExtent.height = srcRes->Height;
                copyRegion.imageExtent.depth = 1;

                device->VkBridge.vk.vkCmdCopyImageToBuffer(cmdBuf,
                    srcRes->VkImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    device->VkBridge.LinearPresentBuf, 1, &copyRegion);

                /* 3. Barreira de Buffer e restaura layout da imagem para renderizacao subsequente */
                VkBufferMemoryBarrier bmb = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                bmb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                bmb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
                bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bmb.buffer = device->VkBridge.LinearPresentBuf;
                bmb.offset = 0;
                bmb.size = (VkDeviceSize)srcRes->Width * srcRes->Height * 4;

                device->VkBridge.vk.vkCmdPipelineBarrier(cmdBuf,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    0, 0, NULL, 1, &bmb, 0, NULL);

                /* 3b. Restaura layout da imagem de origem para COLOR_ATTACHMENT_OPTIMAL para quadros subsequentes */
                imb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                device->VkBridge.vk.vkCmdPipelineBarrier(cmdBuf,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0, 0, NULL, 0, NULL, 1, &imb);
            }

            /* 4. Envia comandos ao hardware e aguarda conclusao */
            VkResult vkFlushRes = Bc250UmdVkFlushCommands(&device->VkBridge);
            Bc250UmdTrace("DXGI Present: Vulkan Destile CommandBuffer flushed: res=%d", vkFlushRes);

            /*
             * 5. Inspecao de telemetria dos primeiros pixels e centro do buffer linear.
             * DX.54: a grade le a VRAM pela CPU e custava ~29 ms por Present (449
             * inspecoes na 98R). Amostrada: 16 primeiros Presents do device e 1 em
             * 64 depois. Fora da amostra o log diz NAO_AMOSTRADO, nunca um valor.
             */
            uint32_t nonZeroSamples = 0u;
            uint32_t presentIndex = ++device->PresentInspectCount;
            /* 4b (doc 564): sem leitura da CPU, o log diz DESLIGADA_4B, nunca AMOSTRA_ZERO. */
            BOOL inspectThis = BC250_UMD_CPU_INSPECTS_LINEAR_BUF &&
                (!s_Bc250Dx54SampledPresentInspection ||
                 presentIndex <= 16u || (presentIndex % 64u) == 0u);
            (void)presentIndex;
            if (inspectThis) {
                nonZeroSamples = Bc250UmdVkInspectLinearPresentBuf(&device->VkBridge, srcRes->Width, srcRes->Height);
            } else if (BC250_UMD_CPU_INSPECTS_LINEAR_BUF && presentIndex == 17u) {
                Bc250UmdTrace("[DX54_INSPECAO] grade do buffer linear passa a ser amostrada: 1 Present em 64 (NonZeroGrade=NAO_AMOSTRADO nos demais)");
            }

            /* 6. Ponte Vulkan -> WDDM: copia imagem linear para a alocacao WDDM oficial (srcRes->Allocation) */
            uint64_t linearGpuVa = 0;
            if (device->VkBridge.pfnGetMemoryGpuVa != NULL &&
                device->VkBridge.LinearPresentMem != VK_NULL_HANDLE) {
                linearGpuVa = device->VkBridge.pfnGetMemoryGpuVa(device->VkBridge.LinearPresentMem);
            }

            if (linearGpuVa == 0 || srcRes->Allocation.GpuVirtualAddress == 0) {
                Bc250UmdTrace("DXGI Present RECUSADO: ponte Vulkan->WDDM sem GPUVA de origem/destino");
                return FALSE;
            }
            {
                HRESULT bridgeHr = Bc250UmdBridgeLinearToWddm(device, linearGpuVa, srcRes);
                Bc250UmdTrace("DXGI Present: Bc250UmdBridgeLinearToWddm res=0x%08X (LinearVa=0x%llX -> DstVa=0x%llX %ux%u)",
                    (unsigned)bridgeHr, (unsigned long long)linearGpuVa,
                    (unsigned long long)srcRes->Allocation.GpuVirtualAddress,
                    srcRes->Width, srcRes->Height);
                if (FAILED(bridgeHr)) return FALSE;
            }
            Bc250UmdTrace("[DX71_WDDM_ONLY] imagem preparada para pfnPresentCb; scanout direto omitido (NonZeroGrade=%s)",
                !BC250_UMD_CPU_INSPECTS_LINEAR_BUF ? "DESLIGADA_4B" :
                inspectThis ? (nonZeroSamples ? "AMOSTRA_NAO_ZERO" : "AMOSTRA_ZERO") : "NAO_AMOSTRADO");
            return TRUE;
        } else {
            /* Contingencia caso linear buffer nao esteja disponivel */
            VkResult vkFlushRes = Bc250UmdVkFlushCommands(&device->VkBridge);
            Bc250UmdTrace("DXGI Present: Vulkan CommandBuffer flushed: res=%d", vkFlushRes);

            if (srcRes->Allocation.GpuVirtualAddress == 0 ||
                device->VkBridge.pfnGetMemoryGpuVa == NULL ||
                srcRes->VkMem == VK_NULL_HANDLE) {
                Bc250UmdTrace("DXGI Present RECUSADO: ponte RAW sem memoria/GPUVA WDDM");
                return FALSE;
            }
            {
                uint64_t rawGpuVa = device->VkBridge.pfnGetMemoryGpuVa(srcRes->VkMem);
                HRESULT bridgeHr;
                if (rawGpuVa == 0) {
                    Bc250UmdTrace("DXGI Present RECUSADO: ponte RAW sem GPUVA Vulkan");
                    return FALSE;
                }
                bridgeHr = Bc250UmdBridgeLinearToWddm(device, rawGpuVa, srcRes);
                Bc250UmdTrace("DXGI Present: Bc250UmdBridgeLinearToWddm (RAW) res=0x%08X", (unsigned)bridgeHr);
                if (FAILED(bridgeHr)) return FALSE;
            }
            Bc250UmdTrace("[DX71_WDDM_ONLY] imagem RAW preparada para pfnPresentCb; scanout direto omitido");
            return TRUE;
        }
    }
    /*
     * Rota 2: Renderizacao PM4 nativa ocorreu diretamente na alocacao WDDM (CP_RB0 Node 1 GFX).
     * NUNCA sobrescrever com zeros da Vulkan Bridge. A alocacao WDDM ja contem
     * o render e sera apresentada pelo callback DXGI Present.
     */
    else if (device->NativePm4DrawCount > 0 && srcRes->Allocation.GpuVirtualAddress != 0) {
        Bc250UmdTrace("[DX71_WDDM_ONLY] PM4 DRAW DETECTADO (NativePm4DrawCount=%llu) -> alocacao WDDM pronta para pfnPresentCb",
            device->NativePm4DrawCount);
        return TRUE;
    }
    else {
        Bc250UmdTrace("DXGI Present RECUSADO: [PRESENT RECUSADO: CONDICAO_SCANOUT_INVALIDA] (VkRendered=%d, Pm4Draws=%llu)",
            (int)vkRendered, device->NativePm4DrawCount);
        return FALSE;
    }
}

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
    /* T4.6: fronteira de quadro para a recopia de superficies abertas. */
    ++device->DxgiPresentCount;

    srcRes = (BC250_UMD_RESOURCE*)pPresentData->hSurfaceToPresent;
    dstRes = (BC250_UMD_RESOURCE*)pPresentData->hDstResource;

    Bc250UmdTrace("DXGI Present: hSurfaceToPresent=%p, hDstResource=%p, Flags=0x%08X, FlipInterval=%d",
        (void*)srcRes, (void*)dstRes,
        pPresentData->Flags.Value,
        (int)pPresentData->FlipInterval);

    if (!Bc250UmdPresentVulkanDetileAndScanout(device, srcRes)) {
        Bc250UmdTrace("DXGI Present RECUSADO: preparacao de scanout falhou ou divergiu -> E_FAIL");
        return E_FAIL;
    }

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

        LARGE_INTEGER qpcStart, qpcEnd, qpcFreq;
        ULONGLONG cbElapsedUs = 0ull;
        static volatile LONG s_UmdPresentCbOrdinal = 0;
        LONG cbOrdinal = InterlockedIncrement(&s_UmdPresentCbOrdinal);
        UINT64 hangSpan = Bc250WinsysHangSpan();

        if (QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0) {
            QueryPerformanceCounter(&qpcStart);
        } else {
            qpcStart.QuadPart = 0;
            qpcFreq.QuadPart = 1;
        }

        Bc250WinsysHangEvent("PRESENT_CB", "ENTER", device, hangSpan,
            (UINT64)(UINT32)presentCb.hSrcAllocation,
            (UINT64)(ULONG_PTR)presentCb.hContext, S_OK);
        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);
        Bc250WinsysHangEvent("PRESENT_CB", "LEAVE", device, hangSpan,
            (UINT64)(UINT32)presentCb.hSrcAllocation,
            (UINT64)(ULONG_PTR)presentCb.hContext, presentHr);

        QueryPerformanceCounter(&qpcEnd);
        if (qpcFreq.QuadPart > 0 && qpcEnd.QuadPart >= qpcStart.QuadPart) {
            cbElapsedUs = (ULONGLONG)((qpcEnd.QuadPart - qpcStart.QuadPart) * 1000000ull / (ULONGLONG)qpcFreq.QuadPart);
        }

        if (!srcIsPrimary || cbOrdinal <= 16L || (cbOrdinal % 32L) == 0L || FAILED(presentHr)) {
            Bc250UmdTrace("[DX74_PRESENT_UMD_CB] n=%ld StartQpc=%llu EndQpc=%llu ElapsedUs=%llu hr=0x%08X",
                cbOrdinal, (unsigned long long)qpcStart.QuadPart, (unsigned long long)qpcEnd.QuadPart,
                (unsigned long long)cbElapsedUs, (unsigned)presentHr);
        }

        Bc250UmdTrace("DXGI Present LEAVE cb: hr=0x%08X (hr!=0 significa que o "
            "dxgkrnl RECUSOU a apresentacao antes de chamar DxgkDdiPresent; "
            "compare com [Present #] no log do KMD)",
            (unsigned)presentHr);
        return presentHr;
    }
}

HRESULT APIENTRY Bc250DxgiPresent1(DXGI1_6_1_DDI_ARG_PRESENT* pPresentData)
{
    BC250_UMD_DEVICE* device;
    DXGIDDICB_PRESENT presentCb;
    BC250_UMD_RESOURCE* srcRes = NULL;
    BC250_UMD_RESOURCE* dstRes = NULL;

    if (pPresentData == NULL) {
        return E_INVALIDARG;
    }

    pPresentData->BackBufferMultiplicity = 1;

    device = (BC250_UMD_DEVICE*)pPresentData->hDevice;
    if (device == NULL) {
        return E_INVALIDARG;
    }
    /* T4.6: fronteira de quadro para a recopia de superficies abertas. */
    ++device->DxgiPresentCount;

    if (pPresentData->SurfacesToPresent > 0 && pPresentData->phSurfacesToPresent != NULL) {
        srcRes = (BC250_UMD_RESOURCE*)pPresentData->phSurfacesToPresent[0].hSurface;
    }
    dstRes = (BC250_UMD_RESOURCE*)pPresentData->hDstResource;

    Bc250UmdTrace("DXGI Present1: Surfaces=%u, hDstResource=%p, Flags=0x%08X, FlipInterval=%d, DirtyRects=%u",
        pPresentData->SurfacesToPresent, (void*)dstRes,
        pPresentData->Flags.Value,
        (int)pPresentData->FlipInterval,
        pPresentData->DirtyRects);

    if (!Bc250UmdPresentVulkanDetileAndScanout(device, srcRes)) {
        Bc250UmdTrace("DXGI Present1 RECUSADO: preparacao de scanout falhou ou divergiu -> E_FAIL");
        return E_FAIL;
    }

    if (device->pDXGICallbacks == NULL || device->pDXGICallbacks->pfnPresentCb == NULL) {
        Bc250UmdTrace("DXGI Present1: pDXGICallbacks ou pfnPresentCb ausente -> E_FAIL");
        return E_FAIL;
    }

    if (!device->Winsys.PresentContextCreated ||
        device->Winsys.PresentContext == NULL) {
        Bc250UmdTrace("DXGI Present1 RECUSADO: contexto de apresentacao ausente "
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

    {
        HRESULT presentHr;
        BOOL srcIsPrimary = (srcRes != NULL) &&
            Bc250WinsysAllocationIsPrimary(&srcRes->Allocation);

        Bc250UmdTrace("DXGI Present1 ENTER cb: hContext=%p PresentNode=%u WorkNode=%u "
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

        LARGE_INTEGER qpcStart, qpcEnd, qpcFreq;
        ULONGLONG cbElapsedUs = 0ull;
        static volatile LONG s_UmdPresent1CbOrdinal = 0;
        LONG cbOrdinal = InterlockedIncrement(&s_UmdPresent1CbOrdinal);
        UINT64 hangSpan = Bc250WinsysHangSpan();

        if (QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0) {
            QueryPerformanceCounter(&qpcStart);
        } else {
            qpcStart.QuadPart = 0;
            qpcFreq.QuadPart = 1;
        }

        Bc250WinsysHangEvent("PRESENT1_CB", "ENTER", device, hangSpan,
            (UINT64)(UINT32)presentCb.hSrcAllocation,
            (UINT64)(ULONG_PTR)presentCb.hContext, S_OK);
        presentHr = device->pDXGICallbacks->pfnPresentCb(
            device->hRTDevice.handle, &presentCb);
        Bc250WinsysHangEvent("PRESENT1_CB", "LEAVE", device, hangSpan,
            (UINT64)(UINT32)presentCb.hSrcAllocation,
            (UINT64)(ULONG_PTR)presentCb.hContext, presentHr);

        QueryPerformanceCounter(&qpcEnd);
        if (qpcFreq.QuadPart > 0 && qpcEnd.QuadPart >= qpcStart.QuadPart) {
            cbElapsedUs = (ULONGLONG)((qpcEnd.QuadPart - qpcStart.QuadPart) * 1000000ull / (ULONGLONG)qpcFreq.QuadPart);
        }

        if (!srcIsPrimary || cbOrdinal <= 16L || (cbOrdinal % 32L) == 0L || FAILED(presentHr)) {
            Bc250UmdTrace("[DX74_PRESENT1_UMD_CB] n=%ld StartQpc=%llu EndQpc=%llu ElapsedUs=%llu hr=0x%08X",
                cbOrdinal, (unsigned long long)qpcStart.QuadPart, (unsigned long long)qpcEnd.QuadPart,
                (unsigned long long)cbElapsedUs, (unsigned)presentHr);
        }

        Bc250UmdTrace("DXGI Present1 LEAVE cb: hr=0x%08X", (unsigned)presentHr);
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
    HRESULT result = S_OK;
    BC250_UMD_DEVICE* device;
    if (pResidencyData == NULL) return E_INVALIDARG;
    if (pResidencyData->Resources == 0) return S_OK;
    device = (BC250_UMD_DEVICE*)pResidencyData->hDevice;
    if (device == NULL || pResidencyData->pResources == NULL ||
        pResidencyData->pStatus == NULL) return E_INVALIDARG;
    if (device->pKTCallbacks == NULL || device->pKTCallbacks->pfnQueryResidencyCb == NULL) {
        Bc250UmdTrace("QueryResourceResidency RECUSADO: pfnQueryResidencyCb ausente");
        return E_NOTIMPL;
    }
    /* F3.3: DXGI_DDI_ARG_QUERYRESOURCERESIDENCY documents EVICTED_TO_DISK
     * as NONRESIDENT (DDI, not the API enum's disk wording). Never infer it
     * from our local Resident cache: query the OS. S_NOT_RESIDENT dominates
     * shared memory, which dominates S_OK. Unknown results are errors. */
    for (i = 0; i < pResidencyData->Resources; ++i) {
        BC250_UMD_RESOURCE* res = (BC250_UMD_RESOURCE*)pResidencyData->pResources[i];
        D3DDDICB_QUERYRESIDENCY args;
        D3DDDI_RESIDENCYSTATUS status = (D3DDDI_RESIDENCYSTATUS)0;
        HRESULT hr;
        if (res == NULL || !res->Created || res->Allocation.KernelAllocation == 0)
            return E_INVALIDARG;
        memset(&args, 0, sizeof(args));
        args.NumAllocations = 1u;
        args.HandleList = &res->Allocation.KernelAllocation;
        args.pResidencyStatus = &status;
        hr = device->pKTCallbacks->pfnQueryResidencyCb(device->hRTDevice.handle, &args);
        if (FAILED(hr)) {
            Bc250UmdTrace("QueryResourceResidency RECUSADO: callback hr=0x%08X", (unsigned)hr);
            return hr;
        }
        switch (status) {
        case D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY:
            pResidencyData->pStatus[i] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
            break;
        case D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY:
            pResidencyData->pStatus[i] = DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY;
            if (result == S_OK) result = S_RESIDENT_IN_SHARED_MEMORY;
            break;
        case D3DDDI_RESIDENCYSTATUS_NOTRESIDENT:
            pResidencyData->pStatus[i] = DXGI_DDI_RESIDENCY_EVICTED_TO_DISK;
            result = S_NOT_RESIDENT;
            break;
        default:
            Bc250UmdTrace("QueryResourceResidency RECUSADO: status desconhecido=%u", (unsigned)status);
            return E_UNEXPECTED;
        }
    }
    return result;
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

        /*
         * DX.28 -- O CONTEUDO RENDERIZADO TAMBEM E' IDENTIDADE.
         *
         * Ate a Corrida 73 so' Allocation e pCpuShadow giravam. Mas o que o DWM
         * desenhou mora na VkImage (e na VkDeviceMemory dela), que ficava no
         * recurso antigo: depois da rotacao, o "buffer anterior" que o DWM copia
         * para o novo back buffer era uma imagem com zeros ou com um quadro
         * velho. Medido na 73: grade 86400/86400 depois do draw e 0/86400 no
         * Present, logo apos os ResourceCopyRegion do buffer anterior -- os
         * quadrados do desktop que apareciam e sumiam. O grupo Vulkan gira
         * junto com o backing; a RTV segue a imagem em Bc250UmdVkRtvFollowResource.
         */
        {
            const VkBuffer savedBuf = first->VkBuf;
            const VkImage savedImg = first->VkImg;
            const VkDeviceMemory savedMem = first->VkMem;
            const VkDeviceSize savedMemSize = first->VkMemSize;
            const VkImageLayout savedLayout = first->VkLayout;
            const BOOL savedVkCreated = first->VkResourceCreated;
            const BOOL savedUploaded = first->WddmAllocationUploaded;
            /* Frente B: imagem e alocacao sao a MESMA memoria; o vinculo gira junto. */
            const BOOL savedAliased = first->VkMemAliased;

            for (i = 0; i + 1u < pRotateData->Resources; ++i) {
                BC250_UMD_RESOURCE* dst = (BC250_UMD_RESOURCE*)pRotateData->pResources[i];
                BC250_UMD_RESOURCE* src = (BC250_UMD_RESOURCE*)pRotateData->pResources[i + 1u];
                dst->Allocation = src->Allocation;
                dst->pCpuShadow = src->pCpuShadow;
                dst->VkBuf = src->VkBuf;
                dst->VkImg = src->VkImg;
                dst->VkMem = src->VkMem;
                dst->VkMemSize = src->VkMemSize;
                dst->VkLayout = src->VkLayout;
                dst->VkResourceCreated = src->VkResourceCreated;
                dst->WddmAllocationUploaded = src->WddmAllocationUploaded;
                dst->VkMemAliased = src->VkMemAliased;
            }

            {
                BC250_UMD_RESOURCE* last =
                    (BC250_UMD_RESOURCE*)pRotateData->pResources[pRotateData->Resources - 1u];
                last->Allocation = savedAllocation;
                last->pCpuShadow = savedShadow;
                last->VkBuf = savedBuf;
                last->VkImg = savedImg;
                last->VkMem = savedMem;
                last->VkMemSize = savedMemSize;
                last->VkLayout = savedLayout;
                last->VkResourceCreated = savedVkCreated;
                last->WddmAllocationUploaded = savedUploaded;
                last->VkMemAliased = savedAliased;
            }

            Bc250UmdTrace("DXGI RotateResourceIdentities LEAVE: %u recursos rotacionados; "
                "res[0] agora tem hAlloc=0x%x GpuVa=0x%llX (antes 0x%x) VkImg=%p (antes %p) -- DX.28: imagem Vulkan gira junto",
                pRotateData->Resources,
                (unsigned)first->Allocation.KernelAllocation,
                (unsigned long long)first->Allocation.GpuVirtualAddress,
                (unsigned)savedAllocation.KernelAllocation,
                (void*)first->VkImg, (void*)savedImg);
        }
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

HRESULT APIENTRY Bc250DxgiResolveSharedResource(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pResolveData)
{
    if (pResolveData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI ResolveSharedResource: hResource=%p -> DXGI_DDI_ERR_UNSUPPORTED",
        (void*)pResolveData->hResource);
    return DXGI_DDI_ERR_UNSUPPORTED;
}

HRESULT APIENTRY Bc250DxgiBlt1(DXGI_DDI_ARG_BLT1* pBlt1Data)
{
    DXGI_DDI_ARG_BLT blt;

    if (pBlt1Data == NULL) {
        return E_INVALIDARG;
    }

    if (pBlt1Data->Rotate != DXGI_DDI_MODE_ROTATION_IDENTITY &&
        pBlt1Data->Rotate != DXGI_DDI_MODE_ROTATION_UNSPECIFIED) {
        Bc250UmdTrace("DXGI Blt1 RECUSADO: Rotate=%d nao suportado (SupportSmoothRotation=FALSE)",
            (int)pBlt1Data->Rotate);
        return DXGI_DDI_ERR_UNSUPPORTED;
    }

    blt.hDevice = pBlt1Data->hDevice;
    blt.hDstResource = pBlt1Data->hDstResource;
    blt.DstSubresource = pBlt1Data->DstSubresource;
    blt.DstLeft = pBlt1Data->DstLeft;
    blt.DstTop = pBlt1Data->DstTop;
    blt.DstRight = pBlt1Data->DstRight;
    blt.DstBottom = pBlt1Data->DstBottom;
    blt.hSrcResource = pBlt1Data->hSrcResource;
    blt.SrcSubresource = pBlt1Data->SrcSubresource;
    blt.Flags = pBlt1Data->Flags;
    blt.Rotate = pBlt1Data->Rotate;

    return Bc250DxgiBlt(&blt);
}

HRESULT APIENTRY Bc250DxgiOfferResources1(DXGI_DDI_ARG_OFFERRESOURCES1* pOfferData)
{
    if (pOfferData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI OfferResources1: Resources=%u, Priority=%d, Flags=0x%x -> S_OK",
        pOfferData->Resources, (int)pOfferData->Priority, pOfferData->Flags);
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiReclaimResources(DXGI_DDI_ARG_RECLAIMRESOURCES* pReclaimData)
{
    UINT i;
    if (pReclaimData == NULL) {
        return E_INVALIDARG;
    }
    if (pReclaimData->pDiscarded != NULL) {
        for (i = 0; i < pReclaimData->Resources; ++i) {
            pReclaimData->pDiscarded[i] = FALSE;
        }
    }
    Bc250UmdTrace("DXGI ReclaimResources: Resources=%u -> S_OK", pReclaimData->Resources);
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiGetMultiplaneOverlayCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS* pCapsData)
{
    if (pCapsData == NULL) {
        return E_INVALIDARG;
    }
    pCapsData->MultiplaneOverlayCaps.MaxPlanes = 1;
    pCapsData->MultiplaneOverlayCaps.NumCapabilityGroups = 0;
    Bc250UmdTrace("DXGI GetMultiplaneOverlayCaps: MaxPlanes=1, NumCapabilityGroups=0 -> S_OK");
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiGetMultiplaneOverlayGroupCaps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS* pGroupCapsData)
{
    if (pGroupCapsData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI GetMultiplaneOverlayGroupCaps RECUSADO: sem grupos MPO");
    return DXGI_DDI_ERR_UNSUPPORTED;
}

HRESULT APIENTRY Bc250DxgiPresentMultiplaneOverlay(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMpoData)
{
    if (pPresentMpoData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI PresentMultiplaneOverlay RECUSADO: MPO nao suportado");
    return DXGI_DDI_ERR_UNSUPPORTED;
}

HRESULT APIENTRY Bc250DxgiCheckPresentDurationSupport(DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT* pDurationData)
{
    if (pDurationData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI CheckPresentDurationSupport RECUSADO: nao suportado");
    return DXGI_DDI_ERR_UNSUPPORTED;
}

HRESULT APIENTRY Bc250DxgiTrimResidencySet(DXGI_DDI_ARG_TRIMRESIDENCYSET* pTrimData)
{
    if (pTrimData == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI TrimResidencySet: NumBytesToTrim=%llu -> S_OK",
        (unsigned long long)pTrimData->NumBytesToTrim);
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiCheckMultiplaneOverlayColorSpaceSupport(DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT* pColorSpaceData)
{
    if (pColorSpaceData == NULL) {
        return E_INVALIDARG;
    }
    pColorSpaceData->Supported = FALSE;
    Bc250UmdTrace("DXGI CheckMultiplaneOverlayColorSpaceSupport: Supported=FALSE -> S_OK");
    return S_OK;
}

HRESULT APIENTRY Bc250DxgiPresentMultiplaneOverlay1(DXGI1_6_1_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMpo1Data)
{
    if (pPresentMpo1Data == NULL) {
        return E_INVALIDARG;
    }
    Bc250UmdTrace("DXGI PresentMultiplaneOverlay1 RECUSADO: MPO1 nao suportado");
    return DXGI_DDI_ERR_UNSUPPORTED;
}

HRESULT APIENTRY Bc250DxgiReclaimResources1(DXGI_DDI_ARG_RECLAIMRESOURCES1* pReclaimData)
{
    UINT i;
    if (pReclaimData == NULL) {
        return E_INVALIDARG;
    }
    if (pReclaimData->pResults != NULL) {
        for (i = 0; i < pReclaimData->Resources; ++i) {
            pReclaimData->pResults[i] = D3DDDI_RECLAIM_RESULT_OK;
        }
    }
    Bc250UmdTrace("DXGI ReclaimResources1: Resources=%u -> S_OK", pReclaimData->Resources);
    return S_OK;
}

VOID Bc250UmdPopulateDxgiBaseFuncs(_Out_ DXGI1_6_1_DDI_BASE_FUNCTIONS* pFuncs)
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
    pFuncs->pfnResolveSharedResource = Bc250DxgiResolveSharedResource;
    pFuncs->pfnBlt1 = Bc250DxgiBlt1;
    pFuncs->pfnOfferResources1 = Bc250DxgiOfferResources1;
    pFuncs->pfnReclaimResources = Bc250DxgiReclaimResources;
    pFuncs->pfnGetMultiplaneOverlayCaps = Bc250DxgiGetMultiplaneOverlayCaps;
    pFuncs->pfnGetMultiplaneOverlayGroupCaps = Bc250DxgiGetMultiplaneOverlayGroupCaps;
    pFuncs->pfnReserved1 = NULL;
    pFuncs->pfnPresentMultiplaneOverlay = Bc250DxgiPresentMultiplaneOverlay;
    pFuncs->pfnReserved2 = NULL;
    pFuncs->pfnPresent1 = Bc250DxgiPresent1;
    pFuncs->pfnCheckPresentDurationSupport = Bc250DxgiCheckPresentDurationSupport;
    pFuncs->pfnTrimResidencySet = Bc250DxgiTrimResidencySet;
    pFuncs->pfnCheckMultiplaneOverlayColorSpaceSupport = Bc250DxgiCheckMultiplaneOverlayColorSpaceSupport;
    pFuncs->pfnPresentMultiplaneOverlay1 = Bc250DxgiPresentMultiplaneOverlay1;
    pFuncs->pfnReclaimResources1 = Bc250DxgiReclaimResources1;
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
        Bc250UmdTrace("D3D10 CreateDevice: start Node1 nao inicializado (hr=0x%08X)", (unsigned)hr);
        if (!device->VkBridgeInitialized) {
            Bc250UmdDeviceTeardown(device);
            return hr;
        }
        Bc250UmdTrace("D3D10 CreateDevice: prosseguindo com aceleracao grafica via Vulkan Bridge");
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
    if (pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions6_1 != NULL) {
        Bc250UmdPopulateDxgiBaseFuncs(pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions6_1);
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

BOOL Bc250UmdExperimentalPm4DrawArmed(void)
{
    char envBuf[16];
    DWORD envLen;

    /*
     * A VARIAVEL DE AMBIENTE, QUANDO PRESENTE, E' AUTORITATIVA NOS DOIS
     * SENTIDOS -- inclusive para DESARMAR.
     *
     * MEDIDO EM 14/09: a versao anterior so' tratava o caso "==1" e, para
     * qualquer outro valor, CAIA no registro. A suite offline
     * (TestF5PmDrawEmissionOnNode1) apagava a variavel para exercitar o
     * fail-closed e passava a depender de HKLM -- que o bind das 11:43
     * alterou. Resultado: bc250_d3d_umd_test.c comecou a reprovar SEM
     * NENHUMA MUDANCA DE CODIGO, exatamente a classe que a faixa C1 ja tinha
     * encontrado e corrigido em outro ponto (docs/234).
     *
     * Com esta regra o teste fica HERMETICO: quem declara o ambiente manda, e
     * o registro so' decide quando ninguem declarou.
     */
    envLen = GetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME,
        envBuf, (DWORD)sizeof(envBuf));
    if (envLen > 0 && envLen < sizeof(envBuf)) {
        return (envBuf[0] == '1') ? TRUE : FALSE;
    }

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
            if (RegQueryValueExA(key, BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME, NULL, &type,
                    (LPBYTE)&value, &size) == ERROR_SUCCESS &&
                type == REG_DWORD && value != 0) {
                resolved = 1;
            }
            RegCloseKey(key);
        }

        InterlockedExchange(&s_state, resolved);
        Bc250UmdTrace("D3D11_EXPERIMENTAL_PM4_DRAW: %s (HKLM\\%s\\%s ou %s) -- experimental, requer anel GFX e shaders",
            resolved ? "ARMADO" : "desarmado",
            BC250_D3D11_CAPS_PROBE_REG_KEY, BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME,
            BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME);
        return (resolved != 0) ? TRUE : FALSE;
    }
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
