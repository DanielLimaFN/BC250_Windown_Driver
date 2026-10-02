#include "bc250_umd_vulkan_bridge.h"
#include "bc250_wddm_winsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include <fcntl.h>

/*
 * M1b.1 (doc 554): liga junto com BC250_M1B_RADV_MEMORY (RADV) e
 * BC250_M1B_RADV_SYSTEM_SEGMENT_ENABLED (KMD). Sozinho nao muda segmento.
 */
#ifndef BC250_M1B_PRESENT_DEVICE_LOCAL
#define BC250_M1B_PRESENT_DEVICE_LOCAL 0
#endif

static void Bc250VkTrace(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    Bc250WinsysTraceRaw(buf);
}

#define LOAD_VK_INST(Name) do { \
    pBridge->vk.Name = (PFN_##Name)getInstanceProcAddr(pBridge->Instance, #Name); \
    if (pBridge->vk.Name == NULL) { \
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Funcao de instancia ausente: %s", #Name); \
        return FALSE; \
    } \
} while (0)

#define LOAD_VK_DEV(Name) do { \
    pBridge->vk.Name = (PFN_##Name)pBridge->vk.vkGetDeviceProcAddr(pBridge->Device, #Name); \
    if (pBridge->vk.Name == NULL) { \
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Funcao de dispositivo ausente: %s", #Name); \
        return FALSE; \
    } \
} while (0)

static HMODULE Bc250LocateAndLoadIcd(void)
{
    char path[MAX_PATH];
    HMODULE mod = NULL;
    DWORD len = 0;

    Bc250VkTrace("[VK_BRIDGE] Localizando ICD Vulkan (vulkan_radeon.dll)...");

    /* 1. Check registry override if configured */
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\BC250\\Umd", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = sizeof(path);
        if (RegQueryValueExA(key, "IcdPath", NULL, &type, (LPBYTE)path, &size) == ERROR_SUCCESS && type == REG_SZ) {
            Bc250VkTrace("[VK_BRIDGE] Tentando IcdPath do registro: '%s'", path);
            mod = LoadLibraryExA(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (mod != NULL) {
                Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado via registro: %p", mod);
                RegCloseKey(key);
                return mod;
            } else {
                Bc250VkTrace("[VK_BRIDGE] AVISO: Falha ao carregar IcdPath do registro (err=%lu)", GetLastError());
            }
        }
        RegCloseKey(key);
    }

    /* 2. Check directory of current module (bc250umd.dll) */
    HMODULE currentModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)Bc250LocateAndLoadIcd, &currentModule);
    if (currentModule != NULL) {
        len = GetModuleFileNameA(currentModule, path, ARRAYSIZE(path));
        if (len > 0 && len < ARRAYSIZE(path)) {
            char *lastSlash = strrchr(path, '\\');
            if (lastSlash != NULL) {
                *(lastSlash + 1) = '\0';
                strcat_s(path, sizeof(path), "vulkan_radeon.dll");
                Bc250VkTrace("[VK_BRIDGE] Tentando caminho local do modulo: '%s'", path);
                mod = LoadLibraryExA(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (mod != NULL) {
                    Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado do diretorio local: %p", mod);
                    return mod;
                }
            }
        }
    }

    /* 3. Check development default build path */
    const char *devPath = "C:\\Users\\Daniel\\Desktop\\bc250-win\\bc250-win\\mesa-radv-wddm\\build\\src\\amd\\vulkan\\vulkan_radeon.dll";
    Bc250VkTrace("[VK_BRIDGE] Tentando caminho de desenvolvimento: '%s'", devPath);
    mod = LoadLibraryExA(devPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado do caminho de desenvolvimento: %p", mod);
        return mod;
    }

    /* 3b. Check C:\BC250_LIB system installation directory */
    const char *libPath = "C:\\BC250_LIB\\vulkan_radeon.dll";
    Bc250VkTrace("[VK_BRIDGE] Tentando caminho C:\\BC250_LIB: '%s'", libPath);
    mod = LoadLibraryExA(libPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado de C:\\BC250_LIB: %p", mod);
        return mod;
    }

    /* 4. Fallback to standard DLL search */
    Bc250VkTrace("[VK_BRIDGE] Tentando busca padrao no sistema para 'vulkan_radeon.dll'...");
    mod = LoadLibraryA("vulkan_radeon.dll");
    if (mod != NULL) {
        Bc250VkTrace("[VK_BRIDGE] SUCESSO: ICD carregado da busca padrao: %p", mod);
        return mod;
    }

    Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Nao foi possivel carregar vulkan_radeon.dll por nenhum caminho! (LastErr=%lu)", GetLastError());
    return NULL;
}

/*
 * I.1 (30/09, doc 545 §4.2 e §5) -- o stderr do processo vai para um arquivo.
 *
 * O winsys WDDM2 da RADV so' escreve o motivo das falhas em fprintf(stderr):
 * 528 vkCreateDevice -3 na Pesquisa e 1.124 vkAllocateMemory -2 no Explorer
 * sairam sem causa porque dwm.exe e os apps nao tem console. A RADV e esta UMD
 * usam o mesmo UCRT (api-ms-win-crt-stdio), entao redirecionar o stderr aqui
 * alcanca os fprintf da RADV sem recompila-la. Uma vez por processo, so' quando
 * o ICD foi carregado (as suites offline nao chegam aqui e mantem o stderr).
 * Primeiro C:\Windows\Temp (coletado pelo binder); senao o %TEMP% do processo
 * (AppContainer). Nome BC250_UMD_LOG_<pid>-RADV.txt: a coleta atual ja' o pega.
 */
/*
 * I.1b (30/09, Corrida 187): a primeira versao usava freopen_s, que abre SEM
 * compartilhamento; o binder nao conseguiu copiar o -RADV com o dwm.exe vivo,
 * abortou e a corrida virou rollback sem ensaio. Agora o arquivo e' aberto por
 * CreateFileA com FILE_SHARE_READ|WRITE|DELETE e ligado ao stderr: o stderr
 * ganha um descritor valido (freopen_s para NUL, necessario em processo sem
 * console) e _dup2 aponta esse descritor para o handle compartilhado.
 */
static BOOL Bc250UmdVkRedirectStderrTo(_In_z_ const char *path)
{
    HANDLE h;
    int fd;
    FILE *nul = NULL;

    h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    fd = _open_osfhandle((intptr_t)h, _O_APPEND | _O_TEXT);
    if (fd < 0) { CloseHandle(h); return FALSE; }
    if (_fileno(stderr) < 0 && freopen_s(&nul, "NUL", "w", stderr) != 0) { _close(fd); return FALSE; }
    /* I.1c: _dup2 com descritor invalido dispara o manipulador de parametro
     * invalido do CRT, que por padrao encerra o processo. */
    if (_fileno(stderr) < 0 || _dup2(fd, _fileno(stderr)) != 0) { _close(fd); return FALSE; }
    _close(fd);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    return TRUE;
}

/*
 * I.1c (30/09, Corrida 188): o redirecionamento DERRUBOU SearchApp,
 * ShellExperienceHost e TextInputHost (falha em ucrtbase.dll; o log da UMD de
 * cada um termina em "ICD carregado", passo imediatamente anterior, e o -RADV
 * ficou vazio). Os tres sao AppContainer; o teste offline da I.1b so' cobriu
 * processo sem console comum. Agora: (1) AppContainer nao e' redirecionado --
 * e, se o tipo do processo nao puder ser lido, tambem nao (fail-closed);
 * (2) o redirecionamento roda com manipulador de parametro invalido da thread,
 * que faz o CRT devolver erro em vez de encerrar o processo.
 */
static void __cdecl Bc250UmdVkIgnoreInvalidParameter(const wchar_t *Expression, const wchar_t *Function,
    const wchar_t *File, unsigned int Line, uintptr_t Reserved)
{
    (void)Expression; (void)Function; (void)File; (void)Line; (void)Reserved;
}

static BOOL Bc250UmdVkProcessIsAppContainer(void)
{
    HANDLE token = NULL;
    DWORD isAppContainer = 1u;
    DWORD returned = 0u;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return TRUE;
    if (!GetTokenInformation(token, TokenIsAppContainer, &isAppContainer, sizeof(isAppContainer), &returned)) {
        isAppContainer = 1u;
    }
    CloseHandle(token);
    return (isAppContainer != 0u) ? TRUE : FALSE;
}

static void Bc250UmdVkRedirectRadvStderr(void)
{
    static volatile LONG s_redirected = 0;
    char path[MAX_PATH];
    char tempDir[MAX_PATH];
    DWORD len;

    _invalid_parameter_handler previous;
    BOOL done = FALSE;

    if (InterlockedCompareExchange(&s_redirected, 1, 0) != 0) return;
    if (Bc250UmdVkProcessIsAppContainer()) {
        Bc250VkTrace("[VK_BRIDGE] I.1c stderr da RADV NAO redirecionado: processo AppContainer "
            "(o redirecionamento derrubou 3 processos do shell na Corrida 188)");
        return;
    }
    previous = _set_thread_local_invalid_parameter_handler(Bc250UmdVkIgnoreInvalidParameter);
    if (_snprintf_s(path, sizeof(path), _TRUNCATE, "C:\\Windows\\Temp\\BC250_UMD_LOG_%lu-RADV.txt",
            GetCurrentProcessId()) > 0 &&
        Bc250UmdVkRedirectStderrTo(path)) {
        done = TRUE;
    } else {
        len = GetTempPathA((DWORD)sizeof(tempDir), tempDir);
        if (len > 0u && len < sizeof(tempDir) &&
            _snprintf_s(path, sizeof(path), _TRUNCATE, "%sBC250_UMD_LOG_%lu-RADV.txt",
                tempDir, GetCurrentProcessId()) > 0 &&
            Bc250UmdVkRedirectStderrTo(path)) {
            done = TRUE;
        }
    }
    (void)_set_thread_local_invalid_parameter_handler(previous);
    if (done) {
        Bc250VkTrace("[VK_BRIDGE] I.1 stderr da RADV -> %s", path);
        return;
    }
    Bc250VkTrace("[VK_BRIDGE] I.1 stderr da RADV NAO redirecionado (GetLastError=%lu); motivos da RADV seguem invisiveis",
        GetLastError());
}

static BOOL Bc250UmdVkBridgeInitInternal(_Out_ BC250_UMD_VK_BRIDGE *pBridge)
{
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeInitInternal: iniciando ponte Vulkan RADV...");

    /*
     * Faixa DX.20: desativa DCC no RADV. E' a VARIAVEL ISOLADA da Corrida 67,
     * nao uma causa provada.
     *
     * CORRECAO DE REGISTRO (DX.21, 22/09): o comentario anterior atribuia o
     * fault Cid=10 ao "Texture Cache Pipe (TCP_UTCL0)". FALSO. Em GC 10.1.x o
     * gmc_v10_0 usa gfxhub_v2_0, cuja tabela de clientes da Cid=8 "TCP",
     * Cid=9 "SQC (inst)" e Cid=10 "SQC (data)" (P1, gfxhub_v2_0.c upstream,
     * sha256 BF032415...). O fault em 0x2431000 e' LEITURA ESCALAR, e o SQC nao
     * le metadados de DCC. A hipotese do DCC nao explica o endereco; o despejo
     * [VK_PIPELINE_IR] existe para nomear a instrucao que o produz.
     *
     * O RADV no Windows le a opcao por GetEnvironmentVariableA
     * (src/util/os_misc.c:217 do fork), entao SetEnvironmentVariableA alcanca.
     */
    SetEnvironmentVariableA("RADV_DEBUG", "nodcc");
    Bc250VkTrace("[VK_BRIDGE] RADV_DEBUG='nodcc' definido antes do ICD (variavel isolada da Corrida 67; causa NAO provada)");

    pBridge->hIcdModule = Bc250LocateAndLoadIcd();
    if (pBridge->hIcdModule == NULL) {
        Bc250VkTrace("[VK_BRIDGE] RECUSA: modulo ICD nao carregado");
        return FALSE;
    }
    Bc250UmdVkRedirectRadvStderr();

    PFN_vk_icdNegotiateLoaderICDInterfaceVersion negotiate =
        (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(void *)
        GetProcAddress(pBridge->hIcdModule, "vk_icdNegotiateLoaderICDInterfaceVersion");
    PFN_vk_icdGetInstanceProcAddr getInstanceProcAddr =
        (PFN_vk_icdGetInstanceProcAddr)(void *)
        GetProcAddress(pBridge->hIcdModule, "vk_icdGetInstanceProcAddr");

    if (negotiate == NULL || getInstanceProcAddr == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: Exportacoes de loader ICD ausentes (negotiate=%p, getInstance=%p)",
            (void*)negotiate, (void*)getInstanceProcAddr);
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    uint32_t icdVersion = CURRENT_LOADER_ICD_INTERFACE_VERSION;
    VkResult negRes = negotiate(&icdVersion);
    Bc250VkTrace("[VK_BRIDGE] vk_icdNegotiateLoaderICDInterfaceVersion: res=%d, versao negociada=%u",
        negRes, icdVersion);
    if (negRes != VK_SUCCESS || icdVersion < 2u) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: versao negociada insuficiente (< 2) ou falha");
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    pBridge->vk.vkCreateInstance = (PFN_vkCreateInstance)
        getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (pBridge->vk.vkCreateInstance == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: vkCreateInstance nao encontrado via getInstanceProcAddr");
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "BC250_UMD_D3D11_TRANSLATOR";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "BC250_D3D11_TO_VK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instInfo.pApplicationInfo = &appInfo;

    VkResult res = pBridge->vk.vkCreateInstance(&instInfo, NULL, &pBridge->Instance);
    Bc250VkTrace("[VK_BRIDGE] vkCreateInstance: res=%d, Instance=%p", res, (void*)pBridge->Instance);
    if (res != VK_SUCCESS || pBridge->Instance == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: falha ao criar VkInstance (res=%d)", res);
        FreeLibrary(pBridge->hIcdModule);
        pBridge->hIcdModule = NULL;
        return FALSE;
    }

    LOAD_VK_INST(vkDestroyInstance);
    LOAD_VK_INST(vkEnumeratePhysicalDevices);
    LOAD_VK_INST(vkGetPhysicalDeviceProperties);
    LOAD_VK_INST(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_VK_INST(vkGetPhysicalDeviceMemoryProperties);
    LOAD_VK_INST(vkGetPhysicalDeviceFormatProperties);
    LOAD_VK_INST(vkCreateDevice);
    LOAD_VK_INST(vkGetDeviceProcAddr);

    uint32_t physCount = 0;
    res = pBridge->vk.vkEnumeratePhysicalDevices(pBridge->Instance, &physCount, NULL);
    Bc250VkTrace("[VK_BRIDGE] vkEnumeratePhysicalDevices: res=%d, physCount=%u", res, physCount);
    if (res != VK_SUCCESS || physCount == 0) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma GPU fisica encontrada via Vulkan ICD");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkPhysicalDevice *physList = (VkPhysicalDevice *)calloc(physCount, sizeof(VkPhysicalDevice));
    if (physList == NULL) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: falha de memoria ao alocar lista de GPUs");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkEnumeratePhysicalDevices(pBridge->Instance, &physCount, physList);
    pBridge->PhysicalDevice = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < physCount; ++i) {
        VkPhysicalDeviceProperties props;
        pBridge->vk.vkGetPhysicalDeviceProperties(physList[i], &props);
        Bc250VkTrace("[VK_BRIDGE] GPU [%u]: Vendor=0x%04X, Device=0x%04X, Nome='%s', DriverVer=0x%08X",
            i, props.vendorID, props.deviceID, props.deviceName, props.driverVersion);
        if (props.vendorID == BC250_VENDOR_ID && props.deviceID == BC250_DEVICE_ID) {
            pBridge->PhysicalDevice = physList[i];
            Bc250VkTrace("[VK_BRIDGE] -> SELECIONADO: Adaptador BC-250 (1002:13FE) no indice %u", i);
            break;
        }
    }
    free(physList);

    if (pBridge->PhysicalDevice == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: GPU BC-250 (1002:13FE) nao encontrada entre as %u GPUs!", physCount);
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    uint32_t queueCount = 0;
    pBridge->vk.vkGetPhysicalDeviceQueueFamilyProperties(pBridge->PhysicalDevice, &queueCount, NULL);
    Bc250VkTrace("[VK_BRIDGE] Familias de filas: count=%u", queueCount);
    if (queueCount == 0) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma familia de fila encontrada na GPU");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkQueueFamilyProperties *queues = (VkQueueFamilyProperties *)calloc(queueCount, sizeof(VkQueueFamilyProperties));
    if (queues == NULL) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkGetPhysicalDeviceQueueFamilyProperties(pBridge->PhysicalDevice, &queueCount, queues);
    pBridge->GraphicsQueueFamily = UINT32_MAX;
    for (uint32_t q = 0; q < queueCount; ++q) {
        Bc250VkTrace("[VK_BRIDGE] Fila [%u]: Flags=0x%08X (Graphics=%d, Compute=%d, Transfer=%d) Count=%u",
            q, queues[q].queueFlags,
            (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? 1 : 0,
            (queues[q].queueFlags & VK_QUEUE_COMPUTE_BIT) ? 1 : 0,
            (queues[q].queueFlags & VK_QUEUE_TRANSFER_BIT) ? 1 : 0,
            queues[q].queueCount);
        if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && queues[q].queueCount > 0) {
            pBridge->GraphicsQueueFamily = q;
            Bc250VkTrace("[VK_BRIDGE] -> Fila grafica selecionada: index=%u", q);
            break;
        }
    }
    free(queues);

    if (pBridge->GraphicsQueueFamily == UINT32_MAX) {
        Bc250VkTrace("[VK_BRIDGE] ERRO: nenhuma fila com suporte GRAPHICS encontrada");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    pBridge->vk.vkGetPhysicalDeviceMemoryProperties(pBridge->PhysicalDevice, &pBridge->MemProperties);
    Bc250VkTrace("[VK_BRIDGE] Memoria GPU: %u tipos de memoria, %u heaps",
        pBridge->MemProperties.memoryTypeCount, pBridge->MemProperties.memoryHeapCount);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = pBridge->GraphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    /*
     * Faixa DX.21: VK_KHR_pipeline_executable_properties, SOMENTE para ler o
     * programa final do ACO de cada pipeline e grava-lo no log da UMD.
     *
     * MEDIDO na Corrida 66: os dois faults sao do SQC -- Cid=10 "SQC (data)" em
     * 0x2431000 (= VA da imagem >> 8) e Cid=9 "SQC (inst)" em 0x200040000
     * (gfxhub_v2_0.c, P1). Leitura escalar com endereco igual ao word0 de um T#
     * so' se nomeia olhando o s_load/s_buffer_load que a produz; sem o programa
     * isso seria H para sempre.
     *
     * Se a criacao com a extensao falhar, o device e' criado exatamente como
     * antes e a ausencia fica declarada -- a instrumentacao nunca decide se o
     * device existe.
     */
    static const char *const kIrExtensions[] = {VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME};
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR irFeature =
        {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
    irFeature.pipelineExecutableInfo = VK_TRUE;
    deviceCreateInfo.pNext = &irFeature;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = kIrExtensions;

    res = pBridge->vk.vkCreateDevice(pBridge->PhysicalDevice, &deviceCreateInfo, NULL, &pBridge->Device);
    if (res == VK_SUCCESS && pBridge->Device != VK_NULL_HANDLE) {
        pBridge->PipelineIrCaptureEnabled = TRUE;
    } else {
        Bc250VkTrace("[VK_PIPELINE_IR] INDISPONIVEL: vkCreateDevice com %s devolveu res=%d; "
            "device recriado SEM a extensao, programas de shader NAO serao despejados",
            VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME, res);
        pBridge->Device = VK_NULL_HANDLE;
        deviceCreateInfo.pNext = NULL;
        deviceCreateInfo.enabledExtensionCount = 0;
        deviceCreateInfo.ppEnabledExtensionNames = NULL;
        res = pBridge->vk.vkCreateDevice(pBridge->PhysicalDevice, &deviceCreateInfo, NULL, &pBridge->Device);
    }
    Bc250VkTrace("[VK_BRIDGE] vkCreateDevice: res=%d, Device=%p", res, (void*)pBridge->Device);
    if (res != VK_SUCCESS || pBridge->Device == VK_NULL_HANDLE) {
        Bc250VkTrace("[VK_BRIDGE] ERRO CRITICO: vkCreateDevice falhou (res=%d)", res);
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    LOAD_VK_DEV(vkDestroyDevice);
    LOAD_VK_DEV(vkGetDeviceQueue);
    LOAD_VK_DEV(vkCreateBuffer);
    LOAD_VK_DEV(vkDestroyBuffer);
    LOAD_VK_DEV(vkGetBufferMemoryRequirements);
    LOAD_VK_DEV(vkCreateImage);
    LOAD_VK_DEV(vkDestroyImage);
    LOAD_VK_DEV(vkGetImageMemoryRequirements);
    LOAD_VK_DEV(vkGetImageSubresourceLayout);
    LOAD_VK_DEV(vkCreateImageView);
    LOAD_VK_DEV(vkDestroyImageView);
    LOAD_VK_DEV(vkCreateBufferView);
    LOAD_VK_DEV(vkDestroyBufferView);
    LOAD_VK_DEV(vkCreateSampler);
    LOAD_VK_DEV(vkDestroySampler);
    LOAD_VK_DEV(vkAllocateMemory);
    LOAD_VK_DEV(vkFreeMemory);
    LOAD_VK_DEV(vkBindBufferMemory);
    LOAD_VK_DEV(vkBindImageMemory);
    LOAD_VK_DEV(vkMapMemory);
    LOAD_VK_DEV(vkUnmapMemory);
    LOAD_VK_DEV(vkInvalidateMappedMemoryRanges);
    LOAD_VK_DEV(vkFlushMappedMemoryRanges);
    LOAD_VK_DEV(vkCreateRenderPass);
    LOAD_VK_DEV(vkDestroyRenderPass);
    LOAD_VK_DEV(vkCreateFramebuffer);
    LOAD_VK_DEV(vkDestroyFramebuffer);
    LOAD_VK_DEV(vkCreateDescriptorSetLayout);
    LOAD_VK_DEV(vkDestroyDescriptorSetLayout);
    LOAD_VK_DEV(vkCreateDescriptorPool);
    LOAD_VK_DEV(vkDestroyDescriptorPool);
    LOAD_VK_DEV(vkResetDescriptorPool);
    LOAD_VK_DEV(vkAllocateDescriptorSets);
    LOAD_VK_DEV(vkUpdateDescriptorSets);
    LOAD_VK_DEV(vkCreatePipelineLayout);
    LOAD_VK_DEV(vkDestroyPipelineLayout);
    LOAD_VK_DEV(vkCreateShaderModule);
    LOAD_VK_DEV(vkDestroyShaderModule);
    LOAD_VK_DEV(vkCreateGraphicsPipelines);
    LOAD_VK_DEV(vkDestroyPipeline);
    LOAD_VK_DEV(vkCreateCommandPool);
    LOAD_VK_DEV(vkDestroyCommandPool);
    LOAD_VK_DEV(vkAllocateCommandBuffers);
    LOAD_VK_DEV(vkBeginCommandBuffer);
    LOAD_VK_DEV(vkEndCommandBuffer);
    LOAD_VK_DEV(vkCmdBeginRenderPass);
    LOAD_VK_DEV(vkCmdEndRenderPass);
    LOAD_VK_DEV(vkCmdBindPipeline);
    LOAD_VK_DEV(vkCmdBindVertexBuffers);
    LOAD_VK_DEV(vkCmdBindIndexBuffer);
    LOAD_VK_DEV(vkCmdBindDescriptorSets);
    LOAD_VK_DEV(vkCmdSetViewport);
    LOAD_VK_DEV(vkCmdSetScissor);
    LOAD_VK_DEV(vkCmdSetBlendConstants);
    LOAD_VK_DEV(vkCmdPushConstants);
    LOAD_VK_DEV(vkCmdDraw);
    LOAD_VK_DEV(vkCmdDrawIndexed);
    LOAD_VK_DEV(vkCmdPipelineBarrier);
    LOAD_VK_DEV(vkCmdCopyBufferToImage);
    LOAD_VK_DEV(vkCmdCopyImageToBuffer);
    LOAD_VK_DEV(vkCmdCopyImage);
    LOAD_VK_DEV(vkCmdCopyBuffer);
    LOAD_VK_DEV(vkCmdClearAttachments);
    LOAD_VK_DEV(vkCmdClearColorImage);
    LOAD_VK_DEV(vkCreateFence);
    LOAD_VK_DEV(vkDestroyFence);
    LOAD_VK_DEV(vkResetFences);
    LOAD_VK_DEV(vkResetCommandBuffer);
    LOAD_VK_DEV(vkQueueSubmit);
    LOAD_VK_DEV(vkWaitForFences);
    LOAD_VK_DEV(vkDeviceWaitIdle);

    if (pBridge->PipelineIrCaptureEnabled) {
        pBridge->pfnGetPipelineExecutableProperties = (PFN_vkGetPipelineExecutablePropertiesKHR)
            pBridge->vk.vkGetDeviceProcAddr(pBridge->Device, "vkGetPipelineExecutablePropertiesKHR");
        pBridge->pfnGetPipelineExecutableInternalRepresentations = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)
            pBridge->vk.vkGetDeviceProcAddr(pBridge->Device, "vkGetPipelineExecutableInternalRepresentationsKHR");
        if (pBridge->pfnGetPipelineExecutableProperties == NULL ||
            pBridge->pfnGetPipelineExecutableInternalRepresentations == NULL) {
            pBridge->PipelineIrCaptureEnabled = FALSE;
        }
    }
    Bc250VkTrace("[VK_PIPELINE_IR] captura %s (props=%p reps=%p)",
        pBridge->PipelineIrCaptureEnabled ? "ARMADA" : "DESARMADA",
        (void*)pBridge->pfnGetPipelineExecutableProperties,
        (void*)pBridge->pfnGetPipelineExecutableInternalRepresentations);

    pBridge->vk.vkGetDeviceQueue(pBridge->Device, pBridge->GraphicsQueueFamily, 0, &pBridge->GraphicsQueue);
    Bc250VkTrace("[VK_BRIDGE] vkGetDeviceQueue: GraphicsQueue=%p", (void*)pBridge->GraphicsQueue);

    VkCommandPoolCreateInfo cmdPoolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = pBridge->GraphicsQueueFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    res = pBridge->vk.vkCreateCommandPool(pBridge->Device, &cmdPoolInfo, NULL, &pBridge->CommandPool);
    Bc250VkTrace("[VK_BRIDGE] vkCreateCommandPool: res=%d, CommandPool=%p", res, (void*)pBridge->CommandPool);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkCommandBufferAllocateInfo cmdAlloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = pBridge->CommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    res = pBridge->vk.vkAllocateCommandBuffers(pBridge->Device, &cmdAlloc, &pBridge->CommandBuffer);
    Bc250VkTrace("[VK_BRIDGE] vkAllocateCommandBuffers: res=%d, CommandBuffer=%p", res, (void*)pBridge->CommandBuffer);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    res = pBridge->vk.vkCreateFence(pBridge->Device, &fenceInfo, NULL, &pBridge->Fence);
    Bc250VkTrace("[VK_BRIDGE] vkCreateFence: res=%d, Fence=%p", res, (void*)pBridge->Fence);
    if (res != VK_SUCCESS) {
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }

    /* Opcional: checar presenca de radv_bc250_present_scanout exportado pelo ICD */
    pBridge->pfnPresentScanout = (PFN_radv_bc250_present_scanout)(void*)
        GetProcAddress(pBridge->hIcdModule, "radv_bc250_present_scanout");
    Bc250VkTrace("[VK_BRIDGE] radv_bc250_present_scanout: %s (%p)",
        pBridge->pfnPresentScanout ? "PRESENTE" : "AUSENTE",
        (void*)pBridge->pfnPresentScanout);

    /* Checar presenca de radv_bc250_get_memory_gpu_va exportado pelo ICD */
    pBridge->pfnGetMemoryGpuVa = (PFN_radv_bc250_get_memory_gpu_va)(void*)
        GetProcAddress(pBridge->hIcdModule, "radv_bc250_get_memory_gpu_va");
    Bc250VkTrace("[VK_BRIDGE] radv_bc250_get_memory_gpu_va: %s (%p)",
        pBridge->pfnGetMemoryGpuVa ? "PRESENTE" : "AUSENTE",
        (void*)pBridge->pfnGetMemoryGpuVa);

    pBridge->Initialized = TRUE;
    pBridge->DeviceActive = TRUE;
    pBridge->RecordingActive = FALSE;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeInitInternal: CONCLUIDO COM SUCESSO!");
    return TRUE;
}

#define BC250_PIPELINE_IR_MAX_PIPELINES 32u
#define BC250_PIPELINE_IR_MAX_EXECUTABLES 8u
#define BC250_PIPELINE_IR_MAX_REPRESENTATIONS 8u
#define BC250_PIPELINE_IR_MAX_BYTES (96u * 1024u)

/*
 * Grava no log da UMD o programa final de cada estagio do pipeline, linha a
 * linha, com o rotulo [VK_PIPELINE_IR] P<n> E<m>. Preferencia: "Assembly"
 * (sem LLVM o RADV entrega ali o aco_print_program pos-RA, com cada s_load /
 * s_buffer_load e os SGPRs de origem); se ausente, "ACO IR". NIR fica de fora
 * por volume. Teto por processo e por representacao, com truncamento
 * DECLARADO -- nunca silencioso.
 */
VOID Bc250UmdVkDumpPipelineIr(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkPipeline pipeline,
    const void *vsModuleTag, const void *psModuleTag)
{
    VkPipelineInfoKHR pipelineInfo = {VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    VkPipelineExecutablePropertiesKHR props[BC250_PIPELINE_IR_MAX_EXECUTABLES];
    uint32_t execCount = BC250_PIPELINE_IR_MAX_EXECUTABLES;
    uint32_t pipelineIndex;
    VkResult res;

    if (pBridge == NULL || pipeline == VK_NULL_HANDLE || !pBridge->PipelineIrCaptureEnabled) {
        return;
    }
    if (pBridge->PipelineIrDumpCount >= BC250_PIPELINE_IR_MAX_PIPELINES) {
        if (pBridge->PipelineIrDumpCount == BC250_PIPELINE_IR_MAX_PIPELINES) {
            Bc250VkTrace("[VK_PIPELINE_IR] TETO de %u pipelines atingido neste processo; os seguintes NAO serao despejados",
                BC250_PIPELINE_IR_MAX_PIPELINES);
            pBridge->PipelineIrDumpCount++;
        }
        return;
    }
    pipelineIndex = pBridge->PipelineIrDumpCount++;

    pipelineInfo.pipeline = pipeline;
    for (uint32_t i = 0; i < BC250_PIPELINE_IR_MAX_EXECUTABLES; ++i) {
        memset(&props[i], 0, sizeof(props[i]));
        props[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
    }
    res = pBridge->pfnGetPipelineExecutableProperties(pBridge->Device, &pipelineInfo, &execCount, props);
    Bc250VkTrace("[VK_PIPELINE_IR] P%u BEGIN pipeline=%p VS=%p PS=%p executaveis=%u res=%d",
        pipelineIndex, (void*)pipeline, vsModuleTag, psModuleTag, execCount, res);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
        return;
    }

    for (uint32_t e = 0; e < execCount && e < BC250_PIPELINE_IR_MAX_EXECUTABLES; ++e) {
        VkPipelineExecutableInfoKHR execInfo = {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        VkPipelineExecutableInternalRepresentationKHR reps[BC250_PIPELINE_IR_MAX_REPRESENTATIONS];
        uint32_t repCount = BC250_PIPELINE_IR_MAX_REPRESENTATIONS;
        int chosen = -1;
        char *text;

        execInfo.pipeline = pipeline;
        execInfo.executableIndex = e;
        memset(reps, 0, sizeof(reps));
        for (uint32_t r = 0; r < BC250_PIPELINE_IR_MAX_REPRESENTATIONS; ++r) {
            reps[r].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR;
        }
        /* 1a passada: nomes e tamanhos (pData NULL). */
        res = pBridge->pfnGetPipelineExecutableInternalRepresentations(pBridge->Device, &execInfo, &repCount, reps);
        Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u stages=0x%08X nome='%s' subgroup=%u representacoes=%u res=%d",
            pipelineIndex, e, props[e].stages, props[e].name, props[e].subgroupSize, repCount, res);
        if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
            continue;
        }
        for (uint32_t r = 0; r < repCount && r < BC250_PIPELINE_IR_MAX_REPRESENTATIONS; ++r) {
            if (reps[r].isText && strcmp(reps[r].name, "Assembly") == 0) { chosen = (int)r; break; }
        }
        if (chosen < 0) {
            for (uint32_t r = 0; r < repCount && r < BC250_PIPELINE_IR_MAX_REPRESENTATIONS; ++r) {
                if (reps[r].isText && strcmp(reps[r].name, "ACO IR") == 0) { chosen = (int)r; break; }
            }
        }
        if (chosen < 0 || reps[chosen].dataSize == 0) {
            Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u SEM representacao textual 'Assembly' nem 'ACO IR'", pipelineIndex, e);
            continue;
        }
        {
            size_t fullSize = reps[chosen].dataSize;
            size_t allocSize = fullSize < BC250_PIPELINE_IR_MAX_BYTES ? fullSize : BC250_PIPELINE_IR_MAX_BYTES;
            text = (char *)calloc(1, allocSize + 1);
            if (text == NULL) {
                Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u sem memoria para %zu bytes", pipelineIndex, e, allocSize);
                continue;
            }
            /* 2a passada: o texto. dataSize menor que o total devolve VK_INCOMPLETE e texto truncado. */
            repCount = BC250_PIPELINE_IR_MAX_REPRESENTATIONS;
            for (uint32_t r = 0; r < BC250_PIPELINE_IR_MAX_REPRESENTATIONS; ++r) {
                reps[r].pData = NULL;
            }
            reps[chosen].dataSize = allocSize;
            reps[chosen].pData = text;
            for (uint32_t r = 0; r < BC250_PIPELINE_IR_MAX_REPRESENTATIONS; ++r) {
                if ((int)r != chosen) reps[r].dataSize = 0;
            }
            res = pBridge->pfnGetPipelineExecutableInternalRepresentations(pBridge->Device, &execInfo, &repCount, reps);
            text[allocSize] = '\0';
            Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u '%s' %zu bytes%s res=%d -- inicio",
                pipelineIndex, e, reps[chosen].name, fullSize,
                fullSize > allocSize ? " (TRUNCADO NO TETO)" : "", res);
            {
                char *cursor = text;
                uint32_t lineNo = 0;
                while (*cursor != '\0') {
                    char *end = strchr(cursor, '\n');
                    if (end != NULL) *end = '\0';
                    if (end > cursor && end[-1] == '\r') end[-1] = '\0';
                    Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u %04u| %.600s", pipelineIndex, e, lineNo++, cursor);
                    if (end == NULL) break;
                    cursor = end + 1;
                }
                Bc250VkTrace("[VK_PIPELINE_IR] P%u E%u fim (%u linhas)", pipelineIndex, e, lineNo);
            }
            free(text);
        }
    }
    Bc250VkTrace("[VK_PIPELINE_IR] P%u END", pipelineIndex);
}

VOID Bc250UmdVkReleaseDescriptorBatches(BC250_UMD_VK_BRIDGE *pBridge)
{
    while (pBridge->DescriptorBatches) {
        BC250_VK_DESCRIPTOR_BATCH *batch = pBridge->DescriptorBatches;
        pBridge->DescriptorBatches = batch->Next;
        if (batch->Pool) pBridge->vk.vkDestroyDescriptorPool(pBridge->Device, batch->Pool, NULL);
        for (uint32_t i = 0; i < 316; ++i) {
            if (batch->Views[i]) pBridge->vk.vkDestroyImageView(pBridge->Device, batch->Views[i], NULL);
            if (batch->BufferViews[i]) pBridge->vk.vkDestroyBufferView(pBridge->Device, batch->BufferViews[i], NULL);
            if (batch->Samplers[i]) pBridge->vk.vkDestroySampler(pBridge->Device, batch->Samplers[i], NULL);
        }
        free(batch);
    }
}

VkDescriptorSet Bc250UmdVkAllocPersistentSet(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult res;

    if (pBridge == NULL || pBridge->PersistentPoolFailed || layout == VK_NULL_HANDLE ||
        pBridge->vk.vkResetDescriptorPool == NULL) return VK_NULL_HANDLE;
    if (pBridge->PersistentPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize sizes[4] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BC250_DX54_POOL_UNIFORM_BUFFERS},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BC250_DX54_POOL_SAMPLED_IMAGES},
            {VK_DESCRIPTOR_TYPE_SAMPLER, BC250_DX54_POOL_SAMPLERS},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, BC250_DX54_POOL_TEXEL_BUFFERS}};
        VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = BC250_DX54_POOL_MAX_SETS;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = sizes;
        res = pBridge->vk.vkCreateDescriptorPool(pBridge->Device, &poolInfo, NULL, &pBridge->PersistentPool);
        if (res != VK_SUCCESS) {
            pBridge->PersistentPool = VK_NULL_HANDLE;
            pBridge->PersistentPoolFailed = TRUE;
            Bc250VkTrace("[DX54_DESC] POOL PERSISTENTE RECUSADO: vkCreateDescriptorPool res=%d; draws seguem com pool por draw", res);
            return VK_NULL_HANDLE;
        }
        Bc250VkTrace("[DX54_DESC] POOL PERSISTENTE criado: %u conjuntos, %u CB, %u SRV, %u samplers, %u SRV de buffer (pool=%p)",
            BC250_DX54_POOL_MAX_SETS, BC250_DX54_POOL_UNIFORM_BUFFERS, BC250_DX54_POOL_SAMPLED_IMAGES,
            BC250_DX54_POOL_SAMPLERS, BC250_DX54_POOL_TEXEL_BUFFERS, (void *)pBridge->PersistentPool);
    }
    if (pBridge->PersistentPoolSets >= BC250_DX54_POOL_MAX_SETS) return VK_NULL_HANDLE;
    allocInfo.descriptorPool = pBridge->PersistentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    res = pBridge->vk.vkAllocateDescriptorSets(pBridge->Device, &allocInfo, &set);
    if (res != VK_SUCCESS) return VK_NULL_HANDLE;
    pBridge->PersistentPoolSets++;
    return set;
}

VkSampler Bc250UmdVkCachedSampler(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_ const VkSamplerCreateInfo *pInfo)
{
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t i;

    if (pBridge == NULL || pInfo == NULL || pInfo->pNext != NULL) return VK_NULL_HANDLE;
    for (i = 0; i < pBridge->SamplerCacheCount; ++i)
        if (memcmp(&pBridge->SamplerCache[i].Info, pInfo, sizeof(*pInfo)) == 0)
            return pBridge->SamplerCache[i].Sampler;
    if (pBridge->SamplerCacheCount >= BC250_DX54_SAMPLER_CACHE_CAPACITY) return VK_NULL_HANDLE;
    if (pBridge->vk.vkCreateSampler(pBridge->Device, pInfo, NULL, &sampler) != VK_SUCCESS) return VK_NULL_HANDLE;
    pBridge->SamplerCache[pBridge->SamplerCacheCount].Info = *pInfo;
    pBridge->SamplerCache[pBridge->SamplerCacheCount].Sampler = sampler;
    pBridge->SamplerCacheCount++;
    return sampler;
}

VOID Bc250UmdVkBridgeShutdown(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    UINT64 hangSpan;
    if (pBridge == NULL) return;
    hangSpan = Bc250WinsysHangSpan();
    Bc250WinsysHangEvent("VK_SHUTDOWN", "ENTER", pBridge, hangSpan,
        (UINT64)(uintptr_t)pBridge->Device, 0u, S_OK);
    pBridge->DeviceActive = FALSE;
    pBridge->RecordingActive = FALSE;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeShutdown ENTER: DeviceActive=%d", pBridge->DeviceActive);

    if (pBridge->Device != VK_NULL_HANDLE) {
        if (pBridge->vk.vkDeviceWaitIdle != NULL) {
            VkResult idleResult;
            Bc250WinsysHangEvent("VK_WAIT_IDLE", "ENTER", pBridge, hangSpan,
                (UINT64)(uintptr_t)pBridge->Device, 0u, S_OK);
            idleResult = pBridge->vk.vkDeviceWaitIdle(pBridge->Device);
            Bc250WinsysHangEvent("VK_WAIT_IDLE", "LEAVE", pBridge, hangSpan,
                (UINT64)(uintptr_t)pBridge->Device, 0u, (HRESULT)idleResult);
        }
        Bc250UmdVkReleaseDescriptorBatches(pBridge);
        /* DX.54: pool persistente e cache de samplers vivem ate aqui. */
        if (pBridge->PersistentPool != VK_NULL_HANDLE && pBridge->vk.vkDestroyDescriptorPool != NULL)
            pBridge->vk.vkDestroyDescriptorPool(pBridge->Device, pBridge->PersistentPool, NULL);
        pBridge->PersistentPool = VK_NULL_HANDLE;
        pBridge->PersistentPoolSets = 0;
        for (uint32_t s = 0; s < pBridge->SamplerCacheCount; ++s)
            if (pBridge->SamplerCache[s].Sampler != VK_NULL_HANDLE && pBridge->vk.vkDestroySampler != NULL)
                pBridge->vk.vkDestroySampler(pBridge->Device, pBridge->SamplerCache[s].Sampler, NULL);
        pBridge->SamplerCacheCount = 0;
        /* DX.49: anel de draws e staging persistente vivem ate aqui. */
        if (pBridge->DrawRingBuf != VK_NULL_HANDLE && pBridge->vk.vkDestroyBuffer != NULL) {
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->DrawRingBuf, NULL);
            pBridge->DrawRingBuf = VK_NULL_HANDLE;
        }
        if (pBridge->DrawRingMem != VK_NULL_HANDLE && pBridge->vk.vkFreeMemory != NULL) {
            pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->DrawRingMem, NULL);
            pBridge->DrawRingMem = VK_NULL_HANDLE;
        }
        pBridge->DrawRingMapped = NULL;
        pBridge->DrawRingSize = 0;
        pBridge->DrawRingHead = 0;
        if (pBridge->UploadStagingBuf != VK_NULL_HANDLE && pBridge->vk.vkDestroyBuffer != NULL) {
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->UploadStagingBuf, NULL);
            pBridge->UploadStagingBuf = VK_NULL_HANDLE;
        }
        if (pBridge->UploadStagingMem != VK_NULL_HANDLE && pBridge->vk.vkFreeMemory != NULL) {
            pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->UploadStagingMem, NULL);
            pBridge->UploadStagingMem = VK_NULL_HANDLE;
        }
        pBridge->UploadStagingMapped = NULL;
        pBridge->UploadStagingSize = 0;
        pBridge->UploadStagingHead = 0;
        if (pBridge->LinearPresentBuf != VK_NULL_HANDLE && pBridge->vk.vkDestroyBuffer != NULL) {
            pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
            pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        }
        if (pBridge->LinearPresentMem != VK_NULL_HANDLE && pBridge->vk.vkFreeMemory != NULL) {
            pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
            pBridge->LinearPresentMem = VK_NULL_HANDLE;
        }
        pBridge->LinearPresentCreated = FALSE;
        pBridge->LinearPresentSize = 0;

        if (pBridge->Fence != VK_NULL_HANDLE && pBridge->vk.vkDestroyFence != NULL) {
            pBridge->vk.vkDestroyFence(pBridge->Device, pBridge->Fence, NULL);
            pBridge->Fence = VK_NULL_HANDLE;
        }
        if (pBridge->CommandPool != VK_NULL_HANDLE && pBridge->vk.vkDestroyCommandPool != NULL) {
            pBridge->vk.vkDestroyCommandPool(pBridge->Device, pBridge->CommandPool, NULL);
            pBridge->CommandPool = VK_NULL_HANDLE;
        }
        if (pBridge->vk.vkDestroyDevice != NULL) {
            Bc250WinsysHangEvent("VK_DESTROY_DEVICE", "ENTER", pBridge, hangSpan,
                (UINT64)(uintptr_t)pBridge->Device, 0u, S_OK);
            pBridge->vk.vkDestroyDevice(pBridge->Device, NULL);
            Bc250WinsysHangEvent("VK_DESTROY_DEVICE", "LEAVE", pBridge, hangSpan,
                (UINT64)(uintptr_t)pBridge->Device, 0u, S_OK);
            pBridge->Device = VK_NULL_HANDLE;
        }
    }

    if (pBridge->Instance != VK_NULL_HANDLE) {
        if (pBridge->vk.vkDestroyInstance != NULL) {
            pBridge->vk.vkDestroyInstance(pBridge->Instance, NULL);
            pBridge->Instance = VK_NULL_HANDLE;
        }
    }

    if (pBridge->hIcdModule != NULL) {
        Bc250WinsysHangEvent("VK_FREE_LIBRARY", "ENTER", pBridge, hangSpan,
            (UINT64)(uintptr_t)pBridge->hIcdModule, 0u, S_OK);
        FreeLibrary(pBridge->hIcdModule);
        Bc250WinsysHangEvent("VK_FREE_LIBRARY", "LEAVE", pBridge, hangSpan,
            (UINT64)(uintptr_t)pBridge->hIcdModule, 0u, S_OK);
        pBridge->hIcdModule = NULL;
    }

    pBridge->pfnPresentScanout = NULL;
    pBridge->pfnGetMemoryGpuVa = NULL;
    pBridge->Initialized = FALSE;
    pBridge->DeviceActive = FALSE;
    pBridge->RecordingActive = FALSE;
    Bc250VkTrace("[VK_BRIDGE] Bc250UmdVkBridgeShutdown LEAVE: Desmontado com sucesso");
    Bc250WinsysHangEvent("VK_SHUTDOWN", "LEAVE", pBridge, hangSpan, 0u, 0u, S_OK);
}

BOOL Bc250UmdVkBridgeInit(_Out_ BC250_UMD_VK_BRIDGE *pBridge)
{
    char envBuf[16];
    if (pBridge == NULL) return FALSE;
    memset(pBridge, 0, sizeof(*pBridge));

    if (GetEnvironmentVariableA("BC250_UMD_DISABLE_VK_BRIDGE", envBuf, sizeof(envBuf)) > 0 &&
        envBuf[0] == '1') {
        Bc250VkTrace("[VK_BRIDGE] Desabilitado via variavel de ambiente BC250_UMD_DISABLE_VK_BRIDGE=1");
        return FALSE;
    }

    __try {
        return Bc250UmdVkBridgeInitInternal(pBridge);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Bc250VkTrace("[VK_BRIDGE] EXCECAO SEH CAPTURADA em Bc250UmdVkBridgeInit! Executando shutdown seguro.");
        Bc250UmdVkBridgeShutdown(pBridge);
        return FALSE;
    }
}

uint32_t Bc250UmdVkFindMemoryType(
    _In_ const BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    if (pBridge == NULL) return UINT32_MAX;
    for (uint32_t i = 0; i < pBridge->MemProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0u &&
            (pBridge->MemProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkCommandBuffer Bc250UmdVkGetCommandBuffer(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->SubmissionFailed) return VK_NULL_HANDLE;

    if (!pBridge->RecordingActive) {
        if (pBridge->vk.vkResetCommandBuffer(pBridge->CommandBuffer, 0) != VK_SUCCESS) {
            pBridge->SubmissionFailed = TRUE;
            return VK_NULL_HANDLE;
        }
        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (pBridge->vk.vkBeginCommandBuffer(pBridge->CommandBuffer, &beginInfo) == VK_SUCCESS) {
            pBridge->RecordingActive = TRUE;
            Bc250VkTrace("[VK_BRIDGE] BeginCommandBuffer: gravacao iniciada (cmd=%p)", (void*)pBridge->CommandBuffer);
        } else {
            Bc250VkTrace("[VK_BRIDGE] ERRO: vkBeginCommandBuffer falhou");
            return VK_NULL_HANDLE;
        }
    }
    return pBridge->CommandBuffer;
}

static uint64_t Bc250VkNowMicros(void)
{
    LARGE_INTEGER now, freq;
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return 0u;
    /* Dividir antes de multiplicar: now*1e6 estoura int64 depois de ~10 dias a 10 MHz. */
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000ull +
           (uint64_t)((now.QuadPart % freq.QuadPart) * 1000000ll / freq.QuadPart);
}

VkResult Bc250UmdVkFlushCommands(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    uint64_t waitStart, waitMicros, flushStart, submitStart, submitMicros, endMicros;
    uint32_t drawsInBatch;
    VkDeviceSize ringUsed;

    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->SubmissionFailed) return VK_ERROR_DEVICE_LOST;
    if (!pBridge->RecordingActive) return VK_SUCCESS;

    flushStart = Bc250VkNowMicros();
    drawsInBatch = pBridge->PendingDraws;
    ringUsed = pBridge->DrawRingHead;
    Bc250VkTrace("[VK_BRIDGE] FlushCommands ENTER: gravacao ativa, finalizando (cmd=%p) DrawsNoLote=%u RingUsado=%llu...",
        (void*)pBridge->CommandBuffer, drawsInBatch, (unsigned long long)ringUsed);

    /* DX.49: um clear deixa o render pass aberto; fecha-lo aqui torna todo
     * flush seguro, inclusive o dreno chamado de dentro de uma destruicao. */
    if (pBridge->pRenderPassOpen != NULL && *pBridge->pRenderPassOpen) {
        pBridge->vk.vkCmdEndRenderPass(pBridge->CommandBuffer);
        *pBridge->pRenderPassOpen = FALSE;
    }

    VkResult res = pBridge->vk.vkEndCommandBuffer(pBridge->CommandBuffer);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkEndCommandBuffer falhou: res=%d", res);
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    res = pBridge->vk.vkResetFences(pBridge->Device, 1, &pBridge->Fence);
    if (res != VK_SUCCESS) {
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &pBridge->CommandBuffer;

    /* DX.54: EsperaUs sozinho enganava -- na 98R o flush levava ~14 ms com
     * EsperaUs=190. Fim do CB e vkQueueSubmit passam a ter tempo proprio. */
    submitStart = Bc250VkNowMicros();
    endMicros = submitStart - flushStart;
    res = pBridge->vk.vkQueueSubmit(pBridge->GraphicsQueue, 1, &submitInfo, pBridge->Fence);
    submitMicros = Bc250VkNowMicros() - submitStart;
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkQueueSubmit falhou: res=%d", res);
        pBridge->SubmissionFailed = TRUE;
        return res;
    }

    waitStart = Bc250VkNowMicros();
    res = pBridge->vk.vkWaitForFences(pBridge->Device, 1, &pBridge->Fence, VK_TRUE, 2000000000ull);
    waitMicros = Bc250VkNowMicros() - waitStart;
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: vkWaitForFences retorno nao-sucesso: res=%d (TIMEOUT ou ERRO) DrawsNoLote=%u "
            "-- os draws deste lote foram gravados e NAO tem conclusao provada", res, drawsInBatch);
        pBridge->SubmissionFailed = TRUE;
    } else {
        Bc250UmdVkReleaseDescriptorBatches(pBridge);
        /* DX.54: conjuntos do pool persistente so' voltam com a fence concluida. */
        if (pBridge->PersistentPool != VK_NULL_HANDLE && pBridge->PersistentPoolSets != 0u) {
            if (pBridge->vk.vkResetDescriptorPool(pBridge->Device, pBridge->PersistentPool, 0) == VK_SUCCESS) {
                pBridge->PersistentPoolSets = 0;
                pBridge->PersistentPoolResets++;
            }
        }
        /* So' com a fence concluida o anel e o staging podem voltar ao inicio. */
        pBridge->DrawRingHead = 0;
        pBridge->UploadStagingHead = 0;
        pBridge->UploadCoalescedInBatch = 0;
        pBridge->BatchFlushCount++;
        pBridge->BatchDrawsFlushed += drawsInBatch;
        Bc250VkTrace("[VK_BRIDGE] FlushCommands: Execucao e Fence em hardware CONCLUIDOS com SUCESSO! "
            "[DX49_BATCH] DrawsNoLote=%u RingUsado=%llu EsperaUs=%llu Flushes=%llu DrawsTotais=%llu "
            "[DX54_TEMPO] FimCbUs=%llu SubmitUs=%llu DescritoresNoLoteUs=%llu PoolResets=%llu PoolsPorDraw=%llu",
            drawsInBatch, (unsigned long long)ringUsed, (unsigned long long)waitMicros,
            (unsigned long long)pBridge->BatchFlushCount, (unsigned long long)pBridge->BatchDrawsFlushed,
            (unsigned long long)endMicros, (unsigned long long)submitMicros,
            (unsigned long long)pBridge->DescriptorMicrosInBatch,
            (unsigned long long)pBridge->PersistentPoolResets,
            (unsigned long long)pBridge->DescriptorFallbackPools);
    }
    pBridge->PendingDraws = 0;
    pBridge->DescriptorMicrosInBatch = 0;
    pBridge->RecordingActive = FALSE;
    return res;
}

VkResult Bc250UmdVkDrainPending(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_z_ const char *reason)
{
    if (pBridge == NULL) return VK_ERROR_DEVICE_LOST;
    if (!pBridge->RecordingActive) return VK_SUCCESS;
    Bc250VkTrace("[DX49_BATCH] DRENO antes de %s: DrawsPendentes=%u", reason, pBridge->PendingDraws);
    return Bc250UmdVkFlushCommands(pBridge);
}

static BOOL Bc250UmdVkDrawRingEnsure(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    VkBufferCreateInfo bufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements req;
    uint32_t typeIndex;
    void *mapped = NULL;

    if (pBridge->DrawRingMapped != NULL) return TRUE;
    if (pBridge->DrawRingFailed || pBridge->Device == VK_NULL_HANDLE ||
        pBridge->vk.vkCreateBuffer == NULL || pBridge->vk.vkAllocateMemory == NULL ||
        pBridge->vk.vkBindBufferMemory == NULL || pBridge->vk.vkMapMemory == NULL ||
        pBridge->vk.vkGetBufferMemoryRequirements == NULL) {
        pBridge->DrawRingFailed = TRUE;
        return FALSE;
    }
    bufInfo.size = BC250_DX49_DRAW_RING_BYTES;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (pBridge->vk.vkCreateBuffer(pBridge->Device, &bufInfo, NULL, &pBridge->DrawRingBuf) != VK_SUCCESS) {
        pBridge->DrawRingBuf = VK_NULL_HANDLE;
        goto fail;
    }
    pBridge->vk.vkGetBufferMemoryRequirements(pBridge->Device, pBridge->DrawRingBuf, &req);
    typeIndex = Bc250UmdVkFindMemoryType(pBridge, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (typeIndex == UINT32_MAX) goto fail;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (pBridge->vk.vkAllocateMemory(pBridge->Device, &allocInfo, NULL, &pBridge->DrawRingMem) != VK_SUCCESS) {
        pBridge->DrawRingMem = VK_NULL_HANDLE;
        goto fail;
    }
    if (pBridge->vk.vkBindBufferMemory(pBridge->Device, pBridge->DrawRingBuf, pBridge->DrawRingMem, 0) != VK_SUCCESS ||
        pBridge->vk.vkMapMemory(pBridge->Device, pBridge->DrawRingMem, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS ||
        mapped == NULL) {
        goto fail;
    }
    pBridge->DrawRingMapped = (uint8_t *)mapped;
    pBridge->DrawRingSize = BC250_DX49_DRAW_RING_BYTES;
    pBridge->DrawRingHead = 0;
    Bc250VkTrace("[DX49_BATCH] ANEL de dados por draw criado: %u bytes host-visible/coerente, mapa persistente (buf=%p)",
        BC250_DX49_DRAW_RING_BYTES, (void *)pBridge->DrawRingBuf);
    return TRUE;
fail:
    Bc250VkTrace("[DX49_BATCH] ANEL RECUSADO: criacao/mapa falhou; draws seguem pelo caminho direto com flush por draw");
    if (pBridge->DrawRingMem != VK_NULL_HANDLE && pBridge->vk.vkFreeMemory != NULL)
        pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->DrawRingMem, NULL);
    if (pBridge->DrawRingBuf != VK_NULL_HANDLE && pBridge->vk.vkDestroyBuffer != NULL)
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->DrawRingBuf, NULL);
    pBridge->DrawRingMem = VK_NULL_HANDLE;
    pBridge->DrawRingBuf = VK_NULL_HANDLE;
    pBridge->DrawRingFailed = TRUE;
    return FALSE;
}

VkDeviceSize Bc250UmdVkDrawRingFree(_Inout_ BC250_UMD_VK_BRIDGE *pBridge)
{
    if (pBridge == NULL || !Bc250UmdVkDrawRingEnsure(pBridge)) return 0;
    return (pBridge->DrawRingHead >= pBridge->DrawRingSize) ? 0 : pBridge->DrawRingSize - pBridge->DrawRingHead;
}

BOOL Bc250UmdVkDrawRingPut(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, _In_reads_bytes_(Bytes) const void *pData,
    VkDeviceSize Bytes, _Out_ VkBuffer *pBuffer, _Out_ VkDeviceSize *pOffset)
{
    VkDeviceSize start;
    *pBuffer = VK_NULL_HANDLE;
    *pOffset = 0;
    if (pBridge == NULL || pData == NULL || Bytes == 0 || !Bc250UmdVkDrawRingEnsure(pBridge)) return FALSE;
    start = (pBridge->DrawRingHead + (BC250_DX49_DRAW_RING_ALIGN - 1u)) & ~(VkDeviceSize)(BC250_DX49_DRAW_RING_ALIGN - 1u);
    if (start > pBridge->DrawRingSize || Bytes > pBridge->DrawRingSize - start) return FALSE;
    memcpy(pBridge->DrawRingMapped + start, pData, (size_t)Bytes);
    pBridge->DrawRingHead = start + Bytes;
    *pBuffer = pBridge->DrawRingBuf;
    *pOffset = start;
    return TRUE;
}

BOOL Bc250UmdVkEnsureUploadStaging(_Inout_ BC250_UMD_VK_BRIDGE *pBridge, VkDeviceSize Bytes,
    _Out_ BOOL *pGrew, _Out_ uint64_t *pAllocMicros)
{
    VkBufferCreateInfo bufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements req;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size;
    uint32_t typeIndex;
    void *mapped = NULL;
    uint64_t t0;

    *pGrew = FALSE;
    *pAllocMicros = 0u;
    if (pBridge == NULL || Bytes == 0) return FALSE;
    if (pBridge->UploadStagingMapped != NULL && pBridge->UploadStagingSize >= Bytes) return TRUE;

    /* Crescer libera o anterior: so' e' seguro sem gravacao pendente que o
     * referencie. O upload sempre espera a propria fence, mas o dreno fecha
     * a porta para qualquer outro caminho. */
    if (pBridge->UploadStagingMapped != NULL &&
        Bc250UmdVkDrainPending(pBridge, "crescer o staging persistente") != VK_SUCCESS) {
        return FALSE;
    }
    t0 = Bc250VkNowMicros();
    size = Bytes;
    if (size < (VkDeviceSize)(4u * 1024u * 1024u)) size = (VkDeviceSize)(4u * 1024u * 1024u);
    size = (size + 0xFFFFu) & ~(VkDeviceSize)0xFFFFu;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (pBridge->vk.vkCreateBuffer(pBridge->Device, &bufInfo, NULL, &buf) != VK_SUCCESS) return FALSE;
    pBridge->vk.vkGetBufferMemoryRequirements(pBridge->Device, buf, &req);
    typeIndex = Bc250UmdVkFindMemoryType(pBridge, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (typeIndex == UINT32_MAX) goto fail;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (pBridge->vk.vkAllocateMemory(pBridge->Device, &allocInfo, NULL, &mem) != VK_SUCCESS) {
        mem = VK_NULL_HANDLE;
        goto fail;
    }
    if (pBridge->vk.vkBindBufferMemory(pBridge->Device, buf, mem, 0) != VK_SUCCESS ||
        pBridge->vk.vkMapMemory(pBridge->Device, mem, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS ||
        mapped == NULL) {
        goto fail;
    }
    if (pBridge->UploadStagingMem != VK_NULL_HANDLE) pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->UploadStagingMem, NULL);
    if (pBridge->UploadStagingBuf != VK_NULL_HANDLE) pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->UploadStagingBuf, NULL);
    pBridge->UploadStagingBuf = buf;
    pBridge->UploadStagingMem = mem;
    pBridge->UploadStagingMapped = mapped;
    pBridge->UploadStagingSize = size;
    pBridge->UploadStagingHead = 0;
    pBridge->UploadStagingGrowCount++;
    *pGrew = TRUE;
    *pAllocMicros = Bc250VkNowMicros() - t0;
    Bc250VkTrace("[DX49_STAGING] staging persistente %s: %llu bytes (pedido %llu) AlocUs=%llu Crescimentos=%u",
        pBridge->UploadStagingGrowCount == 1u ? "CRIADO" : "CRESCEU",
        (unsigned long long)size, (unsigned long long)Bytes,
        (unsigned long long)*pAllocMicros, pBridge->UploadStagingGrowCount);
    return TRUE;
fail:
    Bc250VkTrace("[DX49_STAGING] RECUSADO: criacao do staging de %llu bytes falhou", (unsigned long long)size);
    if (mem != VK_NULL_HANDLE) pBridge->vk.vkFreeMemory(pBridge->Device, mem, NULL);
    if (buf != VK_NULL_HANDLE) pBridge->vk.vkDestroyBuffer(pBridge->Device, buf, NULL);
    return FALSE;
}

VkFormat Bc250UmdVkFormatFromDxgi(uint32_t dxgiFormat)
{
    VkFormat strict = Bc250UmdVkFormatFromDxgiStrict(dxgiFormat);
    /*
     * Criacao de RECURSO mantem o fallback historico para BGRA8 (formato que o
     * DWM usa na cadeia); atributo de VERTICE usa a versao estrita e recusa.
     */
    return (strict != VK_FORMAT_UNDEFINED) ? strict : VK_FORMAT_B8G8R8A8_UNORM;
}

VkComponentMapping Bc250UmdVkSrvComponentMapping(uint32_t dxgiFormat)
{
    VkComponentMapping m;
    m.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    m.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    m.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    m.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    switch (dxgiFormat) {
    case 88: /* DXGI_FORMAT_B8G8R8X8_UNORM: o D3D devolve alpha = 1 */
    case 93: /* DXGI_FORMAT_B8G8R8X8_UNORM_SRGB */
        m.a = VK_COMPONENT_SWIZZLE_ONE;
        break;
    case 65: /* DXGI_FORMAT_A8_UNORM: o D3D devolve (0,0,0,a); a imagem Vulkan e' R8 */
        m.r = VK_COMPONENT_SWIZZLE_ZERO;
        m.g = VK_COMPONENT_SWIZZLE_ZERO;
        m.b = VK_COMPONENT_SWIZZLE_ZERO;
        m.a = VK_COMPONENT_SWIZZLE_R;
        break;
    default:
        break;
    }
    return m;
}

VkFormat Bc250UmdVkFormatFromDxgiStrict(uint32_t dxgiFormat)
{
    switch (dxgiFormat) {
    case 2:  /* DXGI_FORMAT_R32G32B32A32_FLOAT */
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 3:  /* DXGI_FORMAT_R32G32B32A32_UINT */
        return VK_FORMAT_R32G32B32A32_UINT;
    case 4:  /* DXGI_FORMAT_R32G32B32A32_SINT */
        return VK_FORMAT_R32G32B32A32_SINT;
    case 6:  /* DXGI_FORMAT_R32G32B32_FLOAT */
        return VK_FORMAT_R32G32B32_SFLOAT;
    case 7:  /* DXGI_FORMAT_R32G32B32_UINT */
        return VK_FORMAT_R32G32B32_UINT;
    case 8:  /* DXGI_FORMAT_R32G32B32_SINT */
        return VK_FORMAT_R32G32B32_SINT;
    case 10: /* DXGI_FORMAT_R16G16B16A16_FLOAT */
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 11: /* DXGI_FORMAT_R16G16B16A16_UNORM */
        return VK_FORMAT_R16G16B16A16_UNORM;
    case 12: /* DXGI_FORMAT_R16G16B16A16_UINT */
        return VK_FORMAT_R16G16B16A16_UINT;
    case 13: /* DXGI_FORMAT_R16G16B16A16_SNORM */
        return VK_FORMAT_R16G16B16A16_SNORM;
    case 14: /* DXGI_FORMAT_R16G16B16A16_SINT */
        return VK_FORMAT_R16G16B16A16_SINT;
    case 16: /* DXGI_FORMAT_R32G32_FLOAT */
        return VK_FORMAT_R32G32_SFLOAT;
    case 17: /* DXGI_FORMAT_R32G32_UINT */
        return VK_FORMAT_R32G32_UINT;
    case 18: /* DXGI_FORMAT_R32G32_SINT */
        return VK_FORMAT_R32G32_SINT;
    case 27: /* DXGI_FORMAT_R8G8B8A8_TYPELESS */
    case 28: /* DXGI_FORMAT_R8G8B8A8_UNORM */
        return VK_FORMAT_R8G8B8A8_UNORM;
    case 29: /* DXGI_FORMAT_R8G8B8A8_UNORM_SRGB -- DX.24: antes caia em UNORM (gama errada) */
        return VK_FORMAT_R8G8B8A8_SRGB;
    case 30: /* DXGI_FORMAT_R8G8B8A8_UINT */
        return VK_FORMAT_R8G8B8A8_UINT;
    case 31: /* DXGI_FORMAT_R8G8B8A8_SNORM */
        return VK_FORMAT_R8G8B8A8_SNORM;
    case 32: /* DXGI_FORMAT_R8G8B8A8_SINT */
        return VK_FORMAT_R8G8B8A8_SINT;
    case 34: /* DXGI_FORMAT_R16G16_FLOAT */
        return VK_FORMAT_R16G16_SFLOAT;
    case 35: /* DXGI_FORMAT_R16G16_UNORM */
        return VK_FORMAT_R16G16_UNORM;
    case 36: /* DXGI_FORMAT_R16G16_UINT */
        return VK_FORMAT_R16G16_UINT;
    case 37: /* DXGI_FORMAT_R16G16_SNORM */
        return VK_FORMAT_R16G16_SNORM;
    case 38: /* DXGI_FORMAT_R16G16_SINT */
        return VK_FORMAT_R16G16_SINT;
    case 40: /* DXGI_FORMAT_D32_FLOAT */
    case 41: /* DXGI_FORMAT_R32_FLOAT */
        return VK_FORMAT_R32_SFLOAT;
    case 42: /* DXGI_FORMAT_R32_UINT */
        return VK_FORMAT_R32_UINT;
    case 43: /* DXGI_FORMAT_R32_SINT */
        return VK_FORMAT_R32_SINT;
    case 45: /* DXGI_FORMAT_D24_UNORM_S8_UINT */
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case 49: /* DXGI_FORMAT_R8G8_UNORM */
        return VK_FORMAT_R8G8_UNORM;
    case 50: /* DXGI_FORMAT_R8G8_UINT */
        return VK_FORMAT_R8G8_UINT;
    case 51: /* DXGI_FORMAT_R8G8_SNORM */
        return VK_FORMAT_R8G8_SNORM;
    case 52: /* DXGI_FORMAT_R8G8_SINT */
        return VK_FORMAT_R8G8_SINT;
    case 54: /* DXGI_FORMAT_R16_FLOAT */
        return VK_FORMAT_R16_SFLOAT;
    case 56: /* DXGI_FORMAT_R16_UNORM */
        return VK_FORMAT_R16_UNORM;
    case 57: /* DXGI_FORMAT_R16_UINT */
        return VK_FORMAT_R16_UINT;
    case 58: /* DXGI_FORMAT_R16_SNORM */
        return VK_FORMAT_R16_SNORM;
    case 59: /* DXGI_FORMAT_R16_SINT */
        return VK_FORMAT_R16_SINT;
    case 61: /* DXGI_FORMAT_R8_UNORM */
        return VK_FORMAT_R8_UNORM;
    case 62: /* DXGI_FORMAT_R8_UINT */
        return VK_FORMAT_R8_UINT;
    case 63: /* DXGI_FORMAT_R8_SNORM */
        return VK_FORMAT_R8_SNORM;
    case 64: /* DXGI_FORMAT_R8_SINT */
        return VK_FORMAT_R8_SINT;
    case 65: /* DXGI_FORMAT_A8_UNORM */
        return VK_FORMAT_R8_UNORM;
    case 85: /* DXGI_FORMAT_B5G6R5_UNORM */
        return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case 86: /* DXGI_FORMAT_B5G5R5A1_UNORM */
        return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
    /*
     * DX.24: os comentarios anteriores estavam TROCADOS (88 rotulado como SRGB,
     * 90/91 como X8). Valores de dxgiformat.h: 87 B8G8R8A8_UNORM,
     * 88 B8G8R8X8_UNORM, 90 B8G8R8A8_TYPELESS, 91 B8G8R8A8_UNORM_SRGB,
     * 92 B8G8R8X8_TYPELESS, 93 B8G8R8X8_UNORM_SRGB. O alpha=1 dos X8 sai do
     * swizzle da view de amostragem (Bc250UmdVkSrvComponentMapping).
     */
    case 87: /* DXGI_FORMAT_B8G8R8A8_UNORM */
    case 88: /* DXGI_FORMAT_B8G8R8X8_UNORM */
    case 90: /* DXGI_FORMAT_B8G8R8A8_TYPELESS */
    case 92: /* DXGI_FORMAT_B8G8R8X8_TYPELESS */
        return VK_FORMAT_B8G8R8A8_UNORM;
    case 91: /* DXGI_FORMAT_B8G8R8A8_UNORM_SRGB */
    case 93: /* DXGI_FORMAT_B8G8R8X8_UNORM_SRGB */
        return VK_FORMAT_B8G8R8A8_SRGB;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

BOOL Bc250UmdVkEnsureLinearPresentBuf(
    _Inout_ BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t width,
    uint32_t height)
{
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->Device == VK_NULL_HANDLE) {
        return FALSE;
    }

    VkDeviceSize requiredSize = (VkDeviceSize)width * height * 4;
    if (requiredSize == 0) requiredSize = (VkDeviceSize)2560 * 1080 * 4;

    if (pBridge->LinearPresentCreated && pBridge->LinearPresentBuf != VK_NULL_HANDLE) {
        if (pBridge->LinearPresentSize >= requiredSize) {
            return TRUE;
        }
        Bc250VkTrace("[VK_BRIDGE] Redimensionando LinearPresentBuf: atual=%llu, necessario=%llu",
            (unsigned long long)pBridge->LinearPresentSize, (unsigned long long)requiredSize);
        /* DX.49: gravacao pendente pode referenciar o buffer antigo. */
        if (Bc250UmdVkDrainPending(pBridge, "redimensionar LinearPresentBuf") != VK_SUCCESS) {
            return FALSE;
        }
        if (pBridge->vk.vkDeviceWaitIdle != NULL) {
            pBridge->vk.vkDeviceWaitIdle(pBridge->Device);
        }
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
        pBridge->LinearPresentMem = VK_NULL_HANDLE;
        pBridge->LinearPresentCreated = FALSE;
    }

    VkBufferCreateInfo linearBufInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    linearBufInfo.size = requiredSize;
    linearBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    linearBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = pBridge->vk.vkCreateBuffer(pBridge->Device, &linearBufInfo, NULL, &pBridge->LinearPresentBuf);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkCreateBuffer falhou para LinearPresentBuf: %d", res);
        return FALSE;
    }

    VkMemoryRequirements memReqs = {0};
    pBridge->vk.vkGetBufferMemoryRequirements(pBridge->Device, pBridge->LinearPresentBuf, &memReqs);

    /*
     * ESCOLHA DE MEMORIA -- e por que o rebaixamento nao pode mais ser silencioso.
     *
     * Medido na corrida 2026-09-18 13:12: o buffer saiu com memType=2 e a
     * inspecao de pixel falhou 128 vezes com "vkMapMemory retornou -5"
     * (VK_ERROR_MEMORY_MAP_FAILED). A mensagem culpava o mapeamento, mas a causa
     * estava AQUI: a primeira opcao (HOST_VISIBLE|HOST_COHERENT) nao foi
     * atendida e a cadeia caiu para DEVICE_LOCAL, que nao e' mapeavel pela CPU.
     * O resultado pratico foi perder a Perna 3 do tripe -- a unica que confere
     * bytes -- sem que nada no log dissesse que a decisao de alocacao tinha sido
     * rebaixada.
     *
     * A cadeia de fallback CONTINUA, de proposito: recusar a alocacao mataria o
     * caminho de apresentacao inteiro, o que e' pior do que apresentar sem poder
     * inspecionar. O que muda e' que o nivel escolhido passa a ser declarado, e
     * a perda de inspecionabilidade e' anunciada no momento em que acontece.
     */
    uint32_t memType = UINT32_MAX;
    const char *memTier = "HOST_VISIBLE|HOST_COHERENT";
    BOOL memHostVisible = TRUE;
#if BC250_M1B_PRESENT_DEVICE_LOCAL
    /*
     * M1b.1 (doc 554, bloqueio 3 do doc 496): com a RADV declarada APU, o
     * primeiro tipo HOST_VISIBLE|HOST_COHERENT tende a ser GTT, que o KMD
     * passa a poder pôr no segmento 2 -- e a copia para o scanout e' por MC,
     * so' no segmento 1. O tipo local e mapeavel vem primeiro; se nao houver,
     * a cadeia antiga segue e o nivel sai declarado no log.
     */
    memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType != UINT32_MAX) {
        memTier = "DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT (M1b: fica no segmento 1)";
    }
#endif
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(
            pBridge, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        memTier = "HOST_VISIBLE (sem HOST_COHERENT)";
    }
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        memTier = "DEVICE_LOCAL -- NAO MAPEAVEL PELA CPU";
        memHostVisible = FALSE;
    }
    if (memType == UINT32_MAX) {
        memType = Bc250UmdVkFindMemoryType(pBridge, memReqs.memoryTypeBits, 0);
        memTier = "QUALQUER TIPO -- inspecionabilidade DESCONHECIDA";
        memHostVisible = FALSE;
    }

    VkMemoryAllocateInfo memAlloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = (memType != UINT32_MAX) ? memType : 0;

    res = pBridge->vk.vkAllocateMemory(pBridge->Device, &memAlloc, NULL, &pBridge->LinearPresentMem);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkAllocateMemory falhou para LinearPresentMem: %d", res);
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        return FALSE;
    }

    res = pBridge->vk.vkBindBufferMemory(pBridge->Device, pBridge->LinearPresentBuf, pBridge->LinearPresentMem, 0);
    if (res != VK_SUCCESS) {
        Bc250VkTrace("[VK_BRIDGE] AVISO: vkBindBufferMemory falhou para LinearPresentBuf: %d", res);
        pBridge->vk.vkFreeMemory(pBridge->Device, pBridge->LinearPresentMem, NULL);
        pBridge->LinearPresentMem = VK_NULL_HANDLE;
        pBridge->vk.vkDestroyBuffer(pBridge->Device, pBridge->LinearPresentBuf, NULL);
        pBridge->LinearPresentBuf = VK_NULL_HANDLE;
        return FALSE;
    }

    pBridge->LinearPresentSize = memReqs.size;
    pBridge->LinearPresentCreated = TRUE;
    Bc250VkTrace("[VK_BRIDGE] LinearPresentBuf LAZY criado com sucesso (%llu bytes, mem=%p, memType=%u, nivel=%s)",
        (unsigned long long)memReqs.size, (void*)pBridge->LinearPresentMem,
        memAlloc.memoryTypeIndex, memTier);
    if (!memHostVisible) {
        Bc250VkTrace("[VK_BRIDGE] AVISO LinearPresentBuf: memoria NAO e' host-visible (nivel=%s). "
                     "A INSPECAO LINEAR_BUF vai falhar com VK_ERROR_MEMORY_MAP_FAILED (-5) por DECISAO DE ALOCACAO, "
                     "nao por defeito de mapeamento. A Perna 3 do tripe fica sem medicao nesta sessao.",
            memTier);
    }
    return TRUE;
}

uint32_t Bc250UmdVkInspectLinearPresentBuf(
    _Inout_ BC250_UMD_VK_BRIDGE *pBridge,
    uint32_t width,
    uint32_t height)
{
#if !BC250_UMD_CPU_INSPECTS_LINEAR_BUF
    {
        static volatile LONG s_skipped = 0;
        LONG n = InterlockedIncrement(&s_skipped);
        (void)pBridge;
        if (n <= 4 || (n % 256) == 0) {
            Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF DESLIGADA (4b): a CPU nao le a VRAM pela BAR (%ux%u, ocorrencia #%ld)",
                width, height, n);
        }
        return 0;
    }
#else
    uint32_t nonZeroSamples = 0;
    if (pBridge == NULL || !pBridge->DeviceActive || pBridge->LinearPresentMem == VK_NULL_HANDLE ||
        pBridge->vk.vkMapMemory == NULL || pBridge->vk.vkUnmapMemory == NULL) {
        return 0;
    }

    void *mapped = NULL;
    VkDeviceSize checkSize = (VkDeviceSize)width * height * 4;
    if (checkSize == 0 || checkSize > pBridge->LinearPresentSize) checkSize = pBridge->LinearPresentSize;

    VkResult res = pBridge->vk.vkMapMemory(pBridge->Device, pBridge->LinearPresentMem, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res == VK_SUCCESS && mapped != NULL) {
        if (pBridge->vk.vkInvalidateMappedMemoryRanges != NULL) {
            VkMappedMemoryRange range;
            memset(&range, 0, sizeof(range));
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = pBridge->LinearPresentMem;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            pBridge->vk.vkInvalidateMappedMemoryRanges(pBridge->Device, 1, &range);
        }
        const uint32_t *pWords = (const uint32_t *)mapped;
        uint32_t centerIdx = (height > 0 && width > 0) ? ((height / 2) * width + (width / 2)) : 0;
        uint32_t s0 = pWords[0];
        uint32_t s1 = (checkSize >= 8) ? pWords[1] : 0;
        uint32_t s2 = (checkSize >= 12) ? pWords[2] : 0;
        uint32_t s3 = (checkSize >= 16) ? pWords[3] : 0;
        uint32_t sc = (centerIdx < (checkSize / 4)) ? pWords[centerIdx] : 0;
        nonZeroSamples = (s0 != 0) + (s1 != 0) + (s2 != 0) + (s3 != 0) + (sc != 0);

        Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF: Px0..3=[0x%08X 0x%08X 0x%08X 0x%08X] Center(%u,%u)=0x%08X NonZeroSamples=%u/5",
            s0, s1, s2, s3, width / 2, height / 2, sc, nonZeroSamples);

        /*
         * DX.23 -- GRADE sobre a superficie inteira.
         *
         * MEDIDO na Corrida 68: os 16 Presents aceitos do DWM sairam com
         * NonZeroSamples=0/5, e a conclusao "tudo preto" nao se sustenta: as 5
         * amostras sao 4 pixels do canto superior esquerdo e o centro, e o draw
         * daquele quadro tinha scissor 0,1040..2560,1080 -- as 40 linhas de baixo
         * (barra de tarefas), onde nenhuma amostra caia. Aqui: uma linha a cada
         * 4 e um pixel a cada 8, com caixa envolvente dos nao-nulos e a faixa
         * das 40 linhas inferiores contada a parte. O valor devolvido passa a
         * ser o da grade.
         */
        if (width > 0 && height > 0 && (VkDeviceSize)width * height * 4u <= checkSize) {
            uint32_t gridTotal = 0u, gridNonZero = 0u, bottomTotal = 0u, bottomNonZero = 0u;
            uint32_t minX = UINT32_MAX, minY = UINT32_MAX, maxX = 0u, maxY = 0u;
            uint32_t firstValue = 0u, firstX = 0u, firstY = 0u;
            const uint32_t bottomStart = (height > 40u) ? (height - 40u) : 0u;
            for (uint32_t y = 0; y < height; y += 4u) {
                const uint32_t *row = pWords + (size_t)y * width;
                for (uint32_t x = 0; x < width; x += 8u) {
                    uint32_t v = row[x];
                    gridTotal++;
                    if (y >= bottomStart) bottomTotal++;
                    if (v != 0u) {
                        if (gridNonZero == 0u) { firstValue = v; firstX = x; firstY = y; }
                        gridNonZero++;
                        if (y >= bottomStart) bottomNonZero++;
                        if (x < minX) minX = x;
                        if (y < minY) minY = y;
                        if (x > maxX) maxX = x;
                        if (y > maxY) maxY = y;
                    }
                }
            }
            if (gridNonZero > 0u) {
                Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF GRADE: NonZero=%u/%u Caixa=(%u,%u)-(%u,%u) "
                    "Faixa40Inferior=%u/%u Primeiro=0x%08X@(%u,%u)",
                    gridNonZero, gridTotal, minX, minY, maxX, maxY, bottomNonZero, bottomTotal,
                    firstValue, firstX, firstY);
            } else {
                Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF GRADE: NonZero=0/%u -- superficie inteira preta "
                    "na grade (1 linha em 4, 1 pixel em 8; Faixa40Inferior=0/%u)", gridTotal, bottomTotal);
            }
            nonZeroSamples = gridNonZero;
        } else {
            Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF GRADE: PULADA -- %ux%u nao cabe no buffer mapeado (%llu bytes)",
                width, height, (unsigned long long)checkSize);
        }

        pBridge->vk.vkUnmapMemory(pBridge->Device, pBridge->LinearPresentMem);
    } else {
        Bc250VkTrace("[VK_BRIDGE] INSPECAO LINEAR_BUF: vkMapMemory retornou %d (memoria nao host-visible ou ocupada)", res);
    }
    return nonZeroSamples;
#endif
}

