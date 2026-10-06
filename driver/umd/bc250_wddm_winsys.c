#include "bc250_wddm_winsys.h"

#include <stdio.h>
#include <string.h>

/*
 * Sink unico de trace da UMD.
 *
 * Corrida 13/09 05:50: o log arquivado nao tinha UMA linha da winsys, porque
 * BC250_WINSYS_LOG so' escrevia em OutputDebugStringA e stderr -- e o dwm.exe
 * nao tem stderr. Sabiamos que o compositor congelava entre o blt de tela
 * cheia e a submissao seguinte, e nao qual callback bloqueava. Um criterio de
 * corrida foi escrito sobre linhas que a instrumentacao nunca poderia produzir.
 *
 * Este sink e' o MESMO usado por Bc250UmdTrace (bc250_d3d_umd.c), para que
 * exista uma unica fonte de formato e um unico arquivo por processo. Fica em
 * C:\Windows\Temp porque a raiz de C: nao e' gravavel pela conta virtual do
 * dwm.exe; sobrevive a tela preta e ao reboot.
 */
/*
 * DX.49 -- SINK LEVE. Corrida 93 (P0): o dwm.exe escreveu ~10 mil linhas por
 * minuto, e cada uma fazia GetModuleFileNameA + GetTempPathA + CreateFileA com
 * FILE_FLAG_WRITE_THROUGH + WriteFile + FlushFileBuffers + CloseHandle, em ate
 * dois arquivos. Agora os caminhos e os handles sao resolvidos UMA vez por
 * processo e cada linha e' um WriteFile sem write-through.
 *
 * DX.49b -- DURABILIDADE DE VOLTA. Corrida 94 (P0): travamento duro e NENHUM
 * BC250_UMD_LOG_* sobreviveu -- sem write-through, as linhas ficaram no cache
 * do sistema e morreram com a maquina, justamente na corrida em que o log do
 * DWM era a evidencia que faltava. O handle continua aberto UMA vez, mas com
 * FILE_FLAG_WRITE_THROUGH: cada WriteFile vai ao disco. O que saiu de cada
 * linha foi so' CreateFileA + FlushFileBuffers + CloseHandle. A descarga de
 * metadados (tamanho do arquivo) acontece na abertura e, depois, no maximo a
 * cada BC250_TRACE_FLUSH_MS, fora de Bc250WinsysTraceRaw.
 */
#define BC250_TRACE_FLUSH_MS 250u
typedef struct BC250_TRACE_SINK_STATE {
    INIT_ONCE Once;
    HANDLE File[2];
    char ImageName[MAX_PATH];
} BC250_TRACE_SINK_STATE;

static BC250_TRACE_SINK_STATE g_Bc250TraceSink = { INIT_ONCE_STATIC_INIT, { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE }, { 0 } };

static HANDLE Bc250WinsysTraceOpen(const char *Path)
{
    return CreateFileA(Path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
}

static volatile LONG64 g_Bc250TraceLastFlushMs;

/* DX.70: apenas linhas rotineiras de sucesso; falhas, Present e tempos de
 * FlushCommands continuam integrais e write-through. */
static volatile LONG g_Bc250RoutineTraceOrdinal[8];
static volatile LONG64 g_Bc250HangSpanSequence;
static volatile LONG64 g_Bc250HangEventSequence;
static INIT_ONCE g_Bc250HangTraceOnce = INIT_ONCE_STATIC_INIT;
static BOOL g_Bc250HangTraceEnabled;

static BOOL CALLBACK Bc250WinsysHangTraceInit(PINIT_ONCE Once, PVOID Parameter, PVOID *Context)
{
    DWORD value = 0u;
    DWORD bytes = sizeof(value);
    (void)Once; (void)Parameter; (void)Context;
    g_Bc250HangTraceEnabled = RegGetValueA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\BC250\\Umd", "HangTrace", RRF_RT_REG_DWORD,
        NULL, &value, &bytes) == ERROR_SUCCESS && value == 1u;
    /* Processo de teste pode exercitar o contrato sem elevacao. */
    if (!g_Bc250HangTraceEnabled) {
        char override[2] = { 0, 0 };
        g_Bc250HangTraceEnabled = GetEnvironmentVariableA(
            "BC250_HANG_TRACE", override, sizeof(override)) == 1u && override[0] == '1';
    }
    return TRUE;
}

UINT64 Bc250WinsysHangSpan(void)
{
    return (UINT64)InterlockedIncrement64(&g_Bc250HangSpanSequence);
}

void Bc250WinsysHangEvent(const char *Operation, const char *Phase,
    const void *Owner, UINT64 Span, UINT64 A, UINT64 B, HRESULT Result)
{
    char message[384];
    LARGE_INTEGER qpc;
    LONG64 sequence;
    if (Operation == NULL || Phase == NULL) return;
    (void)InitOnceExecuteOnce(&g_Bc250HangTraceOnce, Bc250WinsysHangTraceInit, NULL, NULL);
    if (!g_Bc250HangTraceEnabled) return;
    QueryPerformanceCounter(&qpc);
    sequence = InterlockedIncrement64(&g_Bc250HangEventSequence);
    if (_snprintf_s(message, sizeof(message), _TRUNCATE,
            "BC250_HANG v=1 seq=%llu span=%llu tid=%lu qpc=%llu op=%s phase=%s owner=%p a=0x%llX b=0x%llX hr=0x%08X",
            (unsigned long long)sequence, (unsigned long long)Span,
            GetCurrentThreadId(), (unsigned long long)qpc.QuadPart,
            Operation, Phase, Owner, (unsigned long long)A,
            (unsigned long long)B, (unsigned)Result) >= 0) {
        Bc250WinsysTraceRaw(message);
    }
}

static unsigned Bc250WinsysRoutineTraceClass(const char *message)
{
    if (strncmp(message, "[VK_RES] UploadBufferData SUCESSO:", 34) == 0) return 1u;
    if (strncmp(message, "[WDDM_WINSYS] Upload ENTER:", 27) == 0) return 2u;
    if (strncmp(message, "[WDDM_WINSYS] Upload LEAVE: hr=0x00000000", 41) == 0) return 3u;
    if (strncmp(message, "[VK_BRIDGE] BeginCommandBuffer: gravacao iniciada", 49) == 0) return 4u;
    if (strncmp(message, "ResourceMap: res=", 17) == 0) return 5u;
    if (strncmp(message, "ResourceUnmap: buffer sincronizado", 34) == 0) return 6u;
    if (strncmp(message, "ResourceUnmap: res=", 19) == 0) return 7u;
    return 0u;
}

/* Metadados (tamanho do arquivo) ao disco no maximo a cada BC250_TRACE_FLUSH_MS.
 * Os dados de cada linha ja foram ao disco pelo write-through. */
static void Bc250WinsysTraceFlushMetadataIfDue(void)
{
    LONG64 now = (LONG64)GetTickCount64();
    LONG64 last = g_Bc250TraceLastFlushMs;
    unsigned i;
    if (now - last < (LONG64)BC250_TRACE_FLUSH_MS) return;
    if (InterlockedCompareExchange64(&g_Bc250TraceLastFlushMs, now, last) != last) return;
    for (i = 0; i < 2u; ++i) {
        if (g_Bc250TraceSink.File[i] != INVALID_HANDLE_VALUE && g_Bc250TraceSink.File[i] != NULL) {
            (void)FlushFileBuffers(g_Bc250TraceSink.File[i]);
        }
    }
}

static BOOL CALLBACK Bc250WinsysTraceInit(PINIT_ONCE Once, PVOID Parameter, PVOID *Context)
{
    char imagePath[MAX_PATH];
    char logPath[MAX_PATH];
    char sharedLogPath[MAX_PATH];
    char tempPath[MAX_PATH];
    DWORD tempLen;
    (void)Once; (void)Parameter; (void)Context;

    strcpy_s(g_Bc250TraceSink.ImageName, sizeof(g_Bc250TraceSink.ImageName), "?");
    if (GetModuleFileNameA(NULL, imagePath, (DWORD)sizeof(imagePath)) != 0) {
        const char *slash = strrchr(imagePath, '\\');
        strcpy_s(g_Bc250TraceSink.ImageName, sizeof(g_Bc250TraceSink.ImageName),
            (slash != NULL) ? (slash + 1) : imagePath);
    }

    /* Destino por processo no %TEMP% do usuario. */
    logPath[0] = '\0';
    tempLen = GetTempPathA(MAX_PATH, tempPath);
    if (tempLen > 0 && tempLen < MAX_PATH &&
        _snprintf_s(logPath, sizeof(logPath), _TRUNCATE,
            "%sBC250_UMD_LOG_%lu.txt", tempPath, GetCurrentProcessId()) >= 0) {
        g_Bc250TraceSink.File[0] = Bc250WinsysTraceOpen(logPath);
    } else {
        logPath[0] = '\0';
    }

    /*
     * Destino compartilhado do sistema para DWM e auditoria.
     *
     * Corrida 40 (P0): dwm.exe e dxgiadaptercache.exe produziram uma copia
     * adjacente identica de praticamente toda linha, enquanto Explorer.EXE e
     * o harness nao. Se %%TEMP%% resolver para C:\Windows\Temp, os dois
     * destinos seriam O MESMO arquivo. Compare os caminhos resolvidos; quando
     * sao diferentes, ambos continuam existindo por seus contratos distintos.
     */
    if (_snprintf_s(sharedLogPath, sizeof(sharedLogPath), _TRUNCATE,
            "C:\\Windows\\Temp\\BC250_UMD_LOG_%lu.txt",
            GetCurrentProcessId()) >= 0) {
        if (logPath[0] == '\0' || _stricmp(logPath, sharedLogPath) != 0) {
            g_Bc250TraceSink.File[1] = Bc250WinsysTraceOpen(sharedLogPath);
        }
    }
    /* O arquivo precisa existir no disco antes da primeira linha. */
    g_Bc250TraceLastFlushMs = 0;
    Bc250WinsysTraceFlushMetadataIfDue();
    return TRUE;
}

void Bc250WinsysTraceRaw(const char *Message)
{
    char line[768];
    int lineLength;
    DWORD written = 0;
    SYSTEMTIME now;
    unsigned i;
    unsigned routineClass;
    LONG routineOrdinal = 0;

    if (Message == NULL) {
        return;
    }
    routineClass = Bc250WinsysRoutineTraceClass(Message);
    if (routineClass != 0u) {
        routineOrdinal = InterlockedIncrement(&g_Bc250RoutineTraceOrdinal[routineClass]);
        if (routineOrdinal > 16 && (routineOrdinal % 64) != 0) return;
    }
    (void)InitOnceExecuteOnce(&g_Bc250TraceSink.Once, Bc250WinsysTraceInit, NULL, NULL);

    GetLocalTime(&now);
    if (routineClass != 0u) {
        lineLength = _snprintf_s(line, sizeof(line), _TRUNCATE,
            "%02u:%02u:%02u.%03u [pid=%lu %s] [DX70_SAMPLE c=%u n=%ld] %s\r\n",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            GetCurrentProcessId(), g_Bc250TraceSink.ImageName,
            routineClass, routineOrdinal, Message);
    } else {
        lineLength = _snprintf_s(line, sizeof(line), _TRUNCATE,
            "%02u:%02u:%02u.%03u [pid=%lu %s] %s\r\n",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            GetCurrentProcessId(), g_Bc250TraceSink.ImageName, Message);
    }
    if (lineLength < 0) {
        return;
    }

    OutputDebugStringA(line);

    /* stderr: capturado pelo harness de teste e pelas suites offline. */
    fputs(line, stderr);

    /* Um WriteFile por destino; FILE_APPEND_DATA torna cada linha atomica
     * entre threads. O handle e' write-through (DX.49b): a linha vai ao disco. */
    for (i = 0; i < 2u; ++i) {
        if (g_Bc250TraceSink.File[i] != INVALID_HANDLE_VALUE && g_Bc250TraceSink.File[i] != NULL) {
            (void)WriteFile(g_Bc250TraceSink.File[i], line, (DWORD)lineLength, &written, NULL);
        }
    }
    Bc250WinsysTraceFlushMetadataIfDue();
}

/*
 * O prefixo [WDDM_WINSYS] permanece para que as linhas da winsys continuem
 * separaveis das da UMD dentro do mesmo arquivo. stderr fica para as suites
 * offline, que rodam em console.
 */
#define BC250_WINSYS_LOG(fmt, ...) do { \
    char _log_buf[256]; \
    int _len = _snprintf_s(_log_buf, sizeof(_log_buf), _TRUNCATE, \
        "[WDDM_WINSYS] " fmt, ##__VA_ARGS__); \
    if (_len > 0) { \
        Bc250WinsysTraceRaw(_log_buf); \
    } \
} while (0)

static BC250_WINSYS_STATUS Bc250WinsysRememberFailure(
    BC250_WINSYS_CONTEXT *Context,
    HRESULT Hr)
{
    if (Context != NULL) Context->LastCallbackHr = Hr;
    return BC250_WINSYS_CALLBACK_FAILED;
}

/*
 * DX.85 -- ESPERA DE FENCE PELA CPU: LIMITADA E FAIL-CLOSED.
 *
 * A DX.84 registrava um evento assincrono, esperava 1000 ms e, no TIMEOUT,
 * devolvia BC250_WINSYS_OK ("continuando sem travar"). Isso e' sucesso sem o
 * trabalho (Lei 4): WaitPaging OK autoriza a UMD a usar uma alocacao cuja
 * paginacao nao terminou, e WaitForFenceFromCpu OK autoriza a reescrita de
 * slots do anel de staging que a GPU ainda pode estar lendo. Alem disso a
 * trava que o timeout "contornava" era a da propria DX.83 na thread de
 * paginacao do KMD (ver Bc250McdmBuildPagingBuffer) -- sintoma, nao causa.
 *
 * Agora: o evento assincrono continua (a thread nunca fica presa sem saida no
 * dxgkrnl), mas o timeout vira FALHA nomeada. O teto e' maior que o TdrDelay
 * de 2 s, para que uma GPU realmente travada produza TDR antes da nossa
 * desistencia. Sem evento (CreateEventW falhou) a espera e' a sincrona
 * original do contrato.
 */
#define BC250_WINSYS_CPU_WAIT_TIMEOUT_MS 5000u
#define BC250_WINSYS_HR_WAIT_TIMEOUT ((HRESULT)0x80070102L) /* HRESULT_FROM_WIN32(WAIT_TIMEOUT) */

static HRESULT Bc250WinsysBoundedCpuWait(
    BC250_WINSYS_CONTEXT *Context,
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *Args,
    const char *Label,
    BOOL *TimedOut)
{
    HANDLE waitEvent;
    HRESULT hr;
    DWORD waitResult;

    *TimedOut = FALSE;
    waitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Args->hAsyncEvent = waitEvent;
    hr = Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb(
        Context->RuntimeDevice, Args);
    if (waitEvent == NULL) {
        return hr;
    }
    if (SUCCEEDED(hr)) {
        waitResult = WaitForSingleObject(waitEvent,
            BC250_WINSYS_CPU_WAIT_TIMEOUT_MS);
        if (waitResult == WAIT_TIMEOUT) {
            *TimedOut = TRUE;
            hr = BC250_WINSYS_HR_WAIT_TIMEOUT;
            BC250_WINSYS_LOG("%s TIMEOUT (%ums): fence NAO chegou -- FALHA fechada, sem continuar",
                Label, BC250_WINSYS_CPU_WAIT_TIMEOUT_MS);
        } else if (waitResult != WAIT_OBJECT_0) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            if (SUCCEEDED(hr)) hr = E_FAIL;
            BC250_WINSYS_LOG("%s WaitForSingleObject FALHOU: result=%lu hr=0x%08X",
                Label, (unsigned long)waitResult, (unsigned)hr);
        }
    }
    CloseHandle(waitEvent);
    return hr;
}

BC250_WINSYS_STATUS Bc250WinsysInitialize(
    BC250_WINSYS_CONTEXT *Context,
    HANDLE RuntimeDevice,
    const D3DDDI_DEVICECALLBACKS *Callbacks)
{
    if (Context == NULL || RuntimeDevice == NULL || Callbacks == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    memset(Context, 0, sizeof(*Context));
    Context->RuntimeDevice = RuntimeDevice;
    Context->Callbacks = Callbacks;
    Context->LastCallbackHr = S_OK;
    Context->Initialized = TRUE;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreateVirtualContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal)
{
    D3DDDICB_CREATECONTEXTVIRTUAL args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        NodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->ContextCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreateContextVirtualCb == NULL ||
        Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NodeOrdinal = NodeOrdinal;
    hr = Context->Callbacks->pfnCreateContextVirtualCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hContext == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->KernelContext = args.hContext;
    Context->NodeOrdinal = NodeOrdinal;
    Context->ContextCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyVirtualContext(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_DESTROYCONTEXT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->ContextCreated) return BC250_WINSYS_OK;
    if (Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->KernelContext;
    hr = Context->Callbacks->pfnDestroyContextCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->KernelContext = NULL;
    Context->NodeOrdinal = 0u;
    Context->ContextCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreatePresentContext(
    BC250_WINSYS_CONTEXT *Context,
    UINT NodeOrdinal)
{
    D3DDDICB_CREATECONTEXTVIRTUAL args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        NodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->PresentContextCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreateContextVirtualCb == NULL ||
        Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NodeOrdinal = NodeOrdinal;
    BC250_WINSYS_LOG("CreatePresentContext ENTER: Node=%u", NodeOrdinal);
    hr = Context->Callbacks->pfnCreateContextVirtualCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("CreatePresentContext LEAVE: hr=0x%08X hContext=%p Node=%u",
        (unsigned)hr, (void*)args.hContext, NodeOrdinal);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hContext == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->PresentContext = args.hContext;
    Context->PresentNodeOrdinal = NodeOrdinal;
    Context->PresentContextCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyPresentContext(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_DESTROYCONTEXT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->PresentContextCreated) return BC250_WINSYS_OK;
    if (Context->Callbacks->pfnDestroyContextCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->PresentContext;
    BC250_WINSYS_LOG("DestroyPresentContext ENTER: hContext=%p",
        (void*)Context->PresentContext);
    hr = Context->Callbacks->pfnDestroyContextCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("DestroyPresentContext LEAVE: hr=0x%08X", (unsigned)hr);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->PresentContext = NULL;
    Context->PresentNodeOrdinal = 0u;
    Context->PresentContextCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysCreatePagingQueue(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDICB_CREATEPAGINGQUEUE args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->PagingQueueCreated) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnCreatePagingQueueCb == NULL ||
        Context->Callbacks->pfnDestroyPagingQueueCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    args.PhysicalAdapterIndex = 0u;
    hr = Context->Callbacks->pfnCreatePagingQueueCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hPagingQueue == 0u || args.hSyncObject == 0u ||
        args.FenceValueCPUVirtualAddress == NULL) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Context->PagingQueue = args.hPagingQueue;
    Context->PagingSyncObject = args.hSyncObject;
    Context->PagingFenceCpuVa =
        (volatile UINT64 *)args.FenceValueCPUVirtualAddress;
    Context->PagingQueueCreated = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyPagingQueue(
    BC250_WINSYS_CONTEXT *Context)
{
    D3DDDI_DESTROYPAGINGQUEUE args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->PagingQueueCreated) return BC250_WINSYS_OK;
    if (Context->ActiveGpuVaMappings != 0u) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (Context->Callbacks->pfnDestroyPagingQueueCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    hr = Context->Callbacks->pfnDestroyPagingQueueCb(
        Context->RuntimeDevice, &args);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->PagingQueue = 0u;
    Context->PagingSyncObject = 0u;
    Context->PagingFenceCpuVa = NULL;
    Context->PagingQueueCreated = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysAllocate(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    return Bc250WinsysAllocateForResource(Context, Token, ByteSize, Alignment,
        NULL, Allocation);
}

BC250_WINSYS_STATUS Bc250WinsysAllocateForResource(
    BC250_WINSYS_CONTEXT *Context,
    uint64_t Token,
    uint64_t ByteSize,
    uint64_t Alignment,
    const BC250_WINSYS_RESOURCE_BINDING *Binding,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    BC250_ALLOCATION_PRIVATE_V1 privateData;
    D3DDDI_ALLOCATIONINFO2 info;
    D3DDDICB_ALLOCATE args;
    HRESULT hr;
    BOOL bindPrimary = FALSE;
    BOOL tokenPrimary;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        Token == 0u || ByteSize == 0u ||
        Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (Alignment & (Alignment - 1u)) != 0u ||
        (ByteSize & (Alignment - 1u)) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }

    /*
     * FONTE UNICA DE "ISTO E' PRIMARIA": a classe do token. O vinculo de recurso
     * nao pode DISCORDAR dela -- se discordasse, voltariamos a ter duas fontes de
     * verdade sobre primariedade, que e' exatamente a classe de defeito que esta
     * faixa corrige. Divergencia falha FECHADO.
     */
    tokenPrimary = (BC250_ALLOCATION_TOKEN_CLASS(Token) ==
                    BC250_ALLOCATION_TOKEN_CLASS_PRIMARY) ? TRUE : FALSE;
    if (Binding != NULL) {
        bindPrimary = Binding->Primary ? TRUE : FALSE;
        if (bindPrimary != tokenPrimary) {
            BC250_WINSYS_LOG("Allocate RECUSADO: divergencia de primariedade "
                "(token=%d binding=%d Token=0x%llX) -- duas fontes de verdade",
                (int)tokenPrimary, (int)bindPrimary,
                (unsigned long long)Token);
            return BC250_WINSYS_BAD_ARGUMENT;
        }
        /*
         * Primaria SEM handle de recurso do runtime e' o defeito medido em
         * 16:28:27: o dxgkrnl nao liga Flags.Resource, o KMD classifica como
         * nao-primaria e o FLIP do dwm.exe nao tem como ser honrado. Recusar e'
         * melhor que alocar uma "primaria" que o kernel nunca vera como tal.
         */
        if (bindPrimary && Binding->RuntimeResource == NULL) {
            BC250_WINSYS_LOG("Allocate RECUSADO: primaria sem hRTResource -- o "
                "dxgkrnl nao ligaria Flags.Resource e o KMD a trataria como "
                "nao-primaria (Token=0x%llX)", (unsigned long long)Token);
            return BC250_WINSYS_BAD_ARGUMENT;
        }
    } else if (tokenPrimary) {
        BC250_WINSYS_LOG("Allocate RECUSADO: token PRIMARY sem vinculo de recurso "
            "(Token=0x%llX) -- use Bc250WinsysAllocateForResource",
            (unsigned long long)Token);
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Created) return BC250_WINSYS_STATE_CONFLICT;
    if (Context->Callbacks->pfnAllocateCb == NULL ||
        (Context->Callbacks->pfnDeallocate2Cb == NULL &&
         Context->Callbacks->pfnDeallocateCb == NULL)) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    BC250_ALLOCATION_ABI_STATUS abiStatus;
    uint32_t flags = 0u;
    uint32_t tokenClass = BC250_ALLOCATION_TOKEN_CLASS(Token);
    /*
     * DX.89 (hipotese a do doc 476): recurso compartilhado NAO pede CPU_VISIBLE.
     * Medido nas Corridas 133-137: 100% das alocacoes compartilhadas sao
     * recusadas com E_INVALIDARG depois de o KMD devolver sucesso, e o VidMm
     * nunca cria o objeto (ETW da 137). Uma variavel por vez: so' esta muda.
     */
    BOOL sharedResource = (Binding != NULL && Binding->Shared) ? TRUE : FALSE;
    if (tokenClass == BC250_ALLOCATION_TOKEN_CLASS_STAGING ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_COMMAND ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PROOF ||
        (tokenClass == BC250_ALLOCATION_TOKEN_CLASS_RESOURCE && !sharedResource)) {
        flags |= BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    }
    if (sharedResource) {
        BC250_WINSYS_LOG("Allocate SHARED: recurso compartilhado sem CPU_VISIBLE "
            "(DX.89 hipotese a) Token=0x%llX TokenClass=%u Flags=0x%X",
            (unsigned long long)Token, (unsigned)tokenClass, (unsigned)flags);
    }
    /*
     * M1 (doc 491): so' RESOURCE nao compartilhado e nao primario. A regra de
     * classe tambem e' verificada no contrato (formatacao e parse), entao um
     * erro aqui vira recusa, nao alocacao no segmento errado.
     */
    if (Binding != NULL && Binding->GpuVaOnly && !Binding->Primary &&
        !sharedResource && tokenClass == BC250_ALLOCATION_TOKEN_CLASS_RESOURCE) {
        static volatile LONG s_gpuVaOnlyCount = 0;
        LONG count = InterlockedIncrement(&s_gpuVaOnlyCount);
        flags |= BC250_ALLOCATION_FLAG_GPU_VA_ONLY;
        if (count <= 8 || (count % 256) == 0) {
            BC250_WINSYS_LOG("Allocate M1 GPU_VA_ONLY #%ld: Token=0x%llX Size=%llu "
                "Flags=0x%X (pode morar no segmento de sistema)",
                count, (unsigned long long)Token,
                (unsigned long long)ByteSize, (unsigned)flags);
        }
    }
    const BC250_ALLOCATION_GEOMETRY *geometry = NULL;
    if (Binding != NULL && Binding->HasGeometry) {
        geometry = &Binding->Geometry;
    }
    abiStatus = Bc250AllocationAbiFormat(Token, ByteSize, Alignment, flags,
        geometry, &privateData);
    if (abiStatus == BC250_ALLOCATION_ABI_BAD_GEOMETRY) {
        /*
         * Geometria incoerente e' RECUSADA aqui, antes do callback. Deixar
         * passar produziria uma alocacao cuja descricao ao dxgkrnl seria falsa
         * -- e o sintoma apareceria como imagem torta, nao como erro.
         */
        BC250_WINSYS_LOG("Allocate RECUSADO: geometria incoerente "
            "(%ux%u pitch=%u fmt=%u contra ByteSize=%llu)",
            Binding->Geometry.Width, Binding->Geometry.Height,
            Binding->Geometry.Pitch, Binding->Geometry.Format,
            (unsigned long long)ByteSize);
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (abiStatus != BC250_ALLOCATION_ABI_OK ||
        privateData.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }

    memset(&info, 0, sizeof(info));
    info.pPrivateDriverData = &privateData;
    info.PrivateDriverDataSize = (UINT)sizeof(privateData);
    if (bindPrimary) {
        /*
         * d3dukmdt.h:554 e :549. Sem estes dois campos o kernel nao tem como
         * saber que a superficie e' candidata a scanout, por mais que o nosso
         * token privado diga que e'.
         */
        info.Flags.Primary = 1u;
        info.VidPnSourceId = (D3DDDI_VIDEO_PRESENT_SOURCE_ID)Binding->VidPnSourceId;
    }

    memset(&args, 0, sizeof(args));
    args.NumAllocations = 1u;
    args.pAllocationInfo2 = &info;
    if (Binding != NULL) {
        /* d3dumddi.h:3457 -- e' isto que faz o dxgkrnl ligar Flags.Resource. */
        args.hResource = Binding->RuntimeResource;
    }
    BC250_WINSYS_LOG("Allocate ENTER: Token=0x%llX Size=%llu Align=%llu "
        "hRTResource=%p Primary=%d VidPnSourceId=%u",
        (unsigned long long)Token, (unsigned long long)ByteSize,
        (unsigned long long)Alignment, (void*)args.hResource,
        (int)bindPrimary, (unsigned)info.VidPnSourceId);
    hr = Context->Callbacks->pfnAllocateCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("Allocate LEAVE: hr=0x%08X hAlloc=0x%x hKMResource=0x%x",
        (unsigned)hr, (unsigned)info.hAllocation, (unsigned)args.hKMResource);
    if (FAILED(hr)) {
        /* DX.52: a recusa diz O QUE foi pedido (laco do Explorer, Corridas 93..97). */
        BC250_WINSYS_LOG("Allocate RECUSADO pelo runtime/KMD: hr=0x%08X TokenClass=%u Flags=0x%X "
            "Geometria=%d (%ux%u pitch=%u fmt=%u) PrivSize=%u hRTResource=%p -- ver [CREATE_ALLOC_RECUSADO] no KMD",
            (unsigned)hr, (unsigned)tokenClass, (unsigned)flags, geometry != NULL ? 1 : 0,
            geometry != NULL ? geometry->Width : 0u, geometry != NULL ? geometry->Height : 0u,
            geometry != NULL ? geometry->Pitch : 0u, geometry != NULL ? geometry->Format : 0u,
            (unsigned)info.PrivateDriverDataSize, (void*)args.hResource);
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (info.hAllocation == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memset(Allocation, 0, sizeof(*Allocation));
    Allocation->KernelAllocation = info.hAllocation;
    Allocation->GpuVirtualAddress = info.GpuVirtualAddress;
    Allocation->Token = Token;
    Allocation->ByteSize = ByteSize;
    Allocation->Alignment = Alignment;
    Allocation->RuntimeResource = (Binding != NULL) ? Binding->RuntimeResource : NULL;
    Allocation->KernelResource = args.hKMResource;
    Allocation->DeclaredPrimary = bindPrimary;
    Allocation->Created = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDeallocate(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Allocation->Created) return BC250_WINSYS_OK;

    /*
     * No WDDM 2.0+, pfnDeallocate2Cb / pfnDeallocateCb realiza a eviccao
     * atomica e o desregistro seguro no kernel. Evitar a chamada explicita a
     * pfnEvictCb imediatamente antes da desalocacao para nao introduzir corridas
     * no gerenciador de memoria do kernel (dxgmms2 / VIDMM_RECYCLE_HEAP_MGR).
     */
    /* Preserve residency state if cleanup fails; retry must not add a reference. */
    if (Allocation->OwnsGpuVaMapping) {
        D3DDDICB_FREEGPUVIRTUALADDRESS freeArgs;
        BC250_WINSYS_STATUS waitStatus;
        if (Context->Callbacks->pfnFreeGpuVirtualAddressCb == NULL) {
            return BC250_WINSYS_MISSING_CALLBACK;
        }
        waitStatus = Bc250WinsysWaitPaging(Context, Allocation);
        if (waitStatus != BC250_WINSYS_OK) return waitStatus;
        memset(&freeArgs, 0, sizeof(freeArgs));
        freeArgs.BaseAddress = Allocation->GpuVirtualAddress;
        freeArgs.Size = Allocation->ByteSize;
        hr = Context->Callbacks->pfnFreeGpuVirtualAddressCb(
            Context->RuntimeDevice, &freeArgs);
        if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
        Allocation->OwnsGpuVaMapping = FALSE;
        Allocation->GpuVirtualAddress = 0u;
        Allocation->PagingFenceValue = 0u;
        Context->ActiveGpuVaMappings--;
    }

    /*
     * SIMETRIA COM A CRIACAO (13/09): alocacao criada com hResource TEM de ser
     * devolvida com hResource. d3dumddi.h:3472 -- "NumAllocations: ignored if
     * hResource != NULL" --, ou seja o runtime libera o recurso inteiro. Passar
     * so' a lista de handles deixaria o objeto de recurso do kernel orfao.
     */
    if (Context->Callbacks->pfnDeallocate2Cb != NULL) {
        D3DDDICB_DEALLOCATE2 args2;
        memset(&args2, 0, sizeof(args2));
        if (Allocation->RuntimeResource != NULL) {
            args2.hResource = Allocation->RuntimeResource;
        } else {
            args2.NumAllocations = 1u;
            args2.HandleList = &Allocation->KernelAllocation;
        }
        hr = Context->Callbacks->pfnDeallocate2Cb(
            Context->RuntimeDevice, &args2);
    } else if (Context->Callbacks->pfnDeallocateCb != NULL) {
        D3DDDICB_DEALLOCATE args;
        memset(&args, 0, sizeof(args));
        if (Allocation->RuntimeResource != NULL) {
            args.hResource = Allocation->RuntimeResource;
        } else {
            args.NumAllocations = 1u;
            args.HandleList = &Allocation->KernelAllocation;
        }
        hr = Context->Callbacks->pfnDeallocateCb(Context->RuntimeDevice, &args);
    } else {
        return BC250_WINSYS_MISSING_CALLBACK;
    }
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    memset(Allocation, 0, sizeof(*Allocation));
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysMapAllocation(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation,
    BOOL Executable)
{
    D3DDDI_MAPGPUVIRTUALADDRESS args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Allocation->ByteSize == 0u ||
        (Allocation->ByteSize & 0xFFFu) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->GpuVirtualAddress != 0u) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (!Context->PagingQueueCreated ||
        Context->Callbacks->pfnMapGpuVirtualAddressCb == NULL ||
        Context->Callbacks->pfnFreeGpuVirtualAddressCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    args.MinimumAddress = 0x10000u;
    args.MaximumAddress = BC250_ADAPTER_MAX_GPUVA_ADDRESS;
    args.hAllocation = Allocation->KernelAllocation;
    args.SizeInPages = Allocation->ByteSize >> 12;
    args.Protection.Write = 1u;
    args.Protection.Execute = Executable ? 1u : 0u;
    BC250_WINSYS_LOG("MapAllocation ENTER: hAlloc=0x%x Min=0x%llX Max=0x%llX Pages=%llu Exec=%d",
        (unsigned)Allocation->KernelAllocation, (unsigned long long)args.MinimumAddress,
        (unsigned long long)args.MaximumAddress, (unsigned long long)args.SizeInPages, (int)Executable);
    hr = Context->Callbacks->pfnMapGpuVirtualAddressCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("MapAllocation LEAVE: hr=0x%08X VA=0x%llX PagingFence=%llu",
        (unsigned)hr, (unsigned long long)args.VirtualAddress, (unsigned long long)args.PagingFenceValue);
    if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (args.VirtualAddress == 0u || args.PagingFenceValue == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    Allocation->GpuVirtualAddress = args.VirtualAddress;
    Allocation->PagingFenceValue = args.PagingFenceValue;
    Allocation->OwnsGpuVaMapping = TRUE;
    Context->ActiveGpuVaMappings++;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysWaitPaging(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU args;
    D3DKMT_HANDLE objectHandle;
    UINT64 fenceValue;
    UINT64 currentFence = 0ull;
    UINT64 hangSpan;
    HRESULT hr;
    BOOL timedOut = FALSE;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->PagingFenceValue == 0u) {
        return BC250_WINSYS_OK;
    }
    if (!Context->PagingQueueCreated ||
        Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }
    if (Context->PagingFenceCpuVa != NULL) {
        currentFence = *Context->PagingFenceCpuVa;
        if (currentFence >= Allocation->PagingFenceValue) {
            BC250_WINSYS_LOG("WaitPaging SKIP (already reached): TargetFence=%llu CurrentFence=%llu",
                (unsigned long long)Allocation->PagingFenceValue, (unsigned long long)currentFence);
            return BC250_WINSYS_OK;
        }
    }

    objectHandle = Context->PagingSyncObject;
    fenceValue = Allocation->PagingFenceValue;
    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &objectHandle;
    args.FenceValueArray = &fenceValue;

    BC250_WINSYS_LOG("WaitPaging ENTER: TargetFence=%llu CurrentFence=%llu SyncObj=0x%x",
        (unsigned long long)fenceValue, (unsigned long long)currentFence, (unsigned)objectHandle);
    hangSpan = Bc250WinsysHangSpan();
    Bc250WinsysHangEvent("WAIT_PAGING", "ENTER", Context, hangSpan,
        fenceValue, ((UINT64)(UINT32)objectHandle << 32) | (UINT32)Allocation->KernelAllocation,
        S_OK);
    hr = Bc250WinsysBoundedCpuWait(Context, &args, "WaitPaging", &timedOut);
    if (timedOut) {
        Bc250WinsysHangEvent("WAIT_PAGING", "TIMEOUT", Context, hangSpan,
            fenceValue, currentFence, hr);
    }

    if (Context->PagingFenceCpuVa != NULL) {
        currentFence = *Context->PagingFenceCpuVa;
    }
    BC250_WINSYS_LOG("WaitPaging LEAVE: hr=0x%08X TargetFence=%llu CurrentFence=%llu",
        (unsigned)hr, (unsigned long long)fenceValue, (unsigned long long)currentFence);
    Bc250WinsysHangEvent("WAIT_PAGING", "LEAVE", Context, hangSpan,
        fenceValue, currentFence, hr);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVa(
    BC250_WINSYS_CONTEXT *Context,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength)
{
    D3DDDICB_SUBMITCOMMAND args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized ||
        !Context->ContextCreated || CommandGpuVa == 0u ||
        CommandLength == 0u || (CommandLength & 3u) != 0u) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSubmitCommandCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Commands = CommandGpuVa;
    args.CommandLength = CommandLength;
    args.BroadcastContextCount = 1u;
    args.BroadcastContext[0] = Context->KernelContext;
    /*
     * ENTER/LEAVE aqui porque pfnSubmitCommandCb BLOQUEIA quando a fila do
     * contexto esta cheia, e esta era a unica funcao da winsys sem trace.
     * Na corrida 13/09 05:50 o compositor congelou entre o blt de tela cheia
     * e a submissao seguinte, e nao havia como saber se foi aqui.
     */
    BC250_WINSYS_LOG("SubmitGpuVa ENTER: hCtx=0x%p Gpuva=0x%llX Bytes=%u Node=%u",
        Context->KernelContext, (unsigned long long)CommandGpuVa,
        CommandLength, Context->NodeOrdinal);
    hr = Context->Callbacks->pfnSubmitCommandCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SubmitGpuVa LEAVE: hr=0x%08X Bytes=%u", (unsigned)hr, CommandLength);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSubmitGpuVaForNode(
    BC250_WINSYS_CONTEXT *Context,
    UINT ExpectedNodeOrdinal,
    D3DGPU_VIRTUAL_ADDRESS CommandGpuVa,
    UINT CommandLength)
{
    if (Context == NULL || !Context->Initialized ||
        ExpectedNodeOrdinal > BC250_WINSYS_NODE_COMPUTE) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Context->ContextCreated ||
        Context->NodeOrdinal != ExpectedNodeOrdinal) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    return Bc250WinsysSubmitGpuVa(Context, CommandGpuVa, CommandLength);
}

static unsigned long long Bc250WinsysElapsedMicroseconds(
    const LARGE_INTEGER *Start, const LARGE_INTEGER *End)
{
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        End->QuadPart < Start->QuadPart) {
        return 0ull;
    }
    return (unsigned long long)(((End->QuadPart - Start->QuadPart) * 1000000LL) /
        frequency.QuadPart);
}

BC250_WINSYS_STATUS Bc250WinsysUploadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    const void *Source,
    UINT ByteCount)
{
    D3DDDICB_LOCK lockArgs;
    D3DDDICB_UNLOCK unlockArgs;
    D3DKMT_HANDLE allocationHandle;
    HRESULT hr;
    LARGE_INTEGER lockStart;
    LARGE_INTEGER lockEnd;
    unsigned long long lockUs;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Source == NULL || ByteCount == 0u ||
        (uint64_t)ByteCount > Allocation->ByteSize) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnLockCb == NULL ||
        Context->Callbacks->pfnUnlockCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&lockArgs, 0, sizeof(lockArgs));
    lockArgs.hAllocation = Allocation->KernelAllocation;
    lockArgs.Flags.WriteOnly = 1u;
    lockArgs.Flags.LockEntire = 1u;
    BC250_WINSYS_LOG("Upload ENTER: hAlloc=0x%x Bytes=%u",
        (unsigned)Allocation->KernelAllocation, ByteCount);
    /* I.LK (doc 571): quanto o Lock esperou. Lock lento = o VidMm trazendo a
     * alocacao de volta da RAM; e' o que separa "menu lento" de "menu travado". */
    QueryPerformanceCounter(&lockStart);
    hr = Context->Callbacks->pfnLockCb(Context->RuntimeDevice, &lockArgs);
    QueryPerformanceCounter(&lockEnd);
    lockUs = Bc250WinsysElapsedMicroseconds(&lockStart, &lockEnd);
    if (FAILED(hr)) {
        BC250_WINSYS_LOG("Upload LOCK falhou: hr=0x%08X LockUs=%llu", (unsigned)hr,
            (unsigned long long)lockUs);
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (lockArgs.pData == NULL) {
        allocationHandle = lockArgs.hAllocation;
        memset(&unlockArgs, 0, sizeof(unlockArgs));
        unlockArgs.NumAllocations = 1u;
        unlockArgs.phAllocations = &allocationHandle;
        (void)Context->Callbacks->pfnUnlockCb(
            Context->RuntimeDevice, &unlockArgs);
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memcpy(lockArgs.pData, Source, ByteCount);

    allocationHandle = lockArgs.hAllocation;
    memset(&unlockArgs, 0, sizeof(unlockArgs));
    unlockArgs.NumAllocations = 1u;
    unlockArgs.phAllocations = &allocationHandle;
    hr = Context->Callbacks->pfnUnlockCb(Context->RuntimeDevice, &unlockArgs);
    BC250_WINSYS_LOG("Upload LEAVE: hr=0x%08X Bytes=%u LockUs=%llu%s",
        (unsigned)hr, ByteCount, (unsigned long long)lockUs,
        lockUs >= 50000ull ? " LOCK_LENTO" : "");
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysReadAllocation(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_ALLOCATION *Allocation,
    void *Destination,
    UINT ByteCount)
{
    D3DDDICB_LOCK lockArgs;
    D3DDDICB_UNLOCK unlockArgs;
    D3DKMT_HANDLE allocationHandle;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created || Destination == NULL || ByteCount == 0u ||
        (uint64_t)ByteCount > Allocation->ByteSize) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnLockCb == NULL ||
        Context->Callbacks->pfnUnlockCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&lockArgs, 0, sizeof(lockArgs));
    lockArgs.hAllocation = Allocation->KernelAllocation;
    lockArgs.Flags.LockEntire = 1u;
    lockArgs.Flags.ReadOnly = 1u;
    BC250_WINSYS_LOG("Read ENTER: hAlloc=0x%x Bytes=%u",
        (unsigned)Allocation->KernelAllocation, ByteCount);
    hr = Context->Callbacks->pfnLockCb(Context->RuntimeDevice, &lockArgs);
    if (FAILED(hr)) {
        BC250_WINSYS_LOG("Read LOCK falhou: hr=0x%08X", (unsigned)hr);
        return Bc250WinsysRememberFailure(Context, hr);
    }
    if (lockArgs.pData == NULL) {
        allocationHandle = lockArgs.hAllocation;
        memset(&unlockArgs, 0, sizeof(unlockArgs));
        unlockArgs.NumAllocations = 1u;
        unlockArgs.phAllocations = &allocationHandle;
        (void)Context->Callbacks->pfnUnlockCb(
            Context->RuntimeDevice, &unlockArgs);
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memcpy(Destination, lockArgs.pData, ByteCount);
    allocationHandle = lockArgs.hAllocation;
    memset(&unlockArgs, 0, sizeof(unlockArgs));
    unlockArgs.NumAllocations = 1u;
    unlockArgs.phAllocations = &allocationHandle;
    hr = Context->Callbacks->pfnUnlockCb(Context->RuntimeDevice, &unlockArgs);
    BC250_WINSYS_LOG("Read LEAVE: hr=0x%08X Bytes=%u",
        (unsigned)hr, ByteCount);
    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysMakeResident(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDI_MAKERESIDENT args;
    HRESULT hr;
    BC250_WINSYS_STATUS waitStatus;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Allocation->Resident) {
        return BC250_WINSYS_OK;
    }
    if (Allocation->ResidencyPending) goto complete_residency;
    if (!Context->PagingQueueCreated || Context->Callbacks->pfnMakeResidentCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hPagingQueue = Context->PagingQueue;
    args.NumAllocations = 1u;
    args.AllocationList = &Allocation->KernelAllocation;
    args.Flags.CantTrimFurther = 1u;

    BC250_WINSYS_LOG("MakeResident ENTER: hAlloc=0x%x hPagingQueue=0x%x",
        (unsigned)Allocation->KernelAllocation, (unsigned)args.hPagingQueue);
    hr = Context->Callbacks->pfnMakeResidentCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("MakeResident LEAVE: hr=0x%08X PagingFence=%llu",
        (unsigned)hr, (unsigned long long)args.PagingFenceValue);

    if (FAILED(hr) && hr != (HRESULT)0x8000000AL /* E_PENDING / STATUS_PENDING */) {
        return Bc250WinsysRememberFailure(Context, hr);
    }

    /* F3.3: E_PENDING accepts a residency reference; it does not complete it.
     * Keep a pending flag across wait failures to avoid adding a second
     * reference on retry. The existing API now returns OK only after the
     * paging fence, preserving callers which immediately consume Resident. */
    if (args.PagingFenceValue > Allocation->PagingFenceValue)
        Allocation->PagingFenceValue = args.PagingFenceValue;
    Allocation->ResidencyPending = TRUE;
    if (hr == E_PENDING && args.PagingFenceValue == 0u) {
        Allocation->ResidencyRequestInvalid = TRUE;
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }
complete_residency:
    if (Allocation->ResidencyRequestInvalid)
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    waitStatus = Bc250WinsysWaitPaging(Context, Allocation);
    if (waitStatus != BC250_WINSYS_OK) return waitStatus;
    Allocation->ResidencyPending = FALSE;
    Allocation->Resident = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysEvict(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_ALLOCATION *Allocation)
{
    D3DDDICB_EVICT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Allocation == NULL ||
        !Allocation->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Allocation->Resident && !Allocation->ResidencyPending) {
        return BC250_WINSYS_OK;
    }
    if (Context->Callbacks->pfnEvictCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.NumAllocations = 1u;
    args.AllocationList = &Allocation->KernelAllocation;

    BC250_WINSYS_LOG("Evict ENTER: hAlloc=0x%x", (unsigned)Allocation->KernelAllocation);
    hr = Context->Callbacks->pfnEvictCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("Evict LEAVE: hr=0x%08X NumBytesToTrim=%llu",
        (unsigned)hr, (unsigned long long)args.NumBytesToTrim);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Allocation->Resident = FALSE;
    Allocation->ResidencyPending = FALSE;
    Allocation->ResidencyRequestInvalid = FALSE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BOOL Bc250WinsysAllocationIsPrimary(
    const BC250_WINSYS_ALLOCATION *Allocation)
{
    if (Allocation == NULL || !Allocation->Created) {
        return FALSE;
    }
    return (BC250_ALLOCATION_TOKEN_CLASS(Allocation->Token) == BC250_ALLOCATION_TOKEN_CLASS_PRIMARY);
}

BC250_WINSYS_STATUS Bc250WinsysCreateMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    UINT64 InitialValue,
    BC250_WINSYS_MONITORED_FENCE *Fence)
{
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Fence->Created) {
        return BC250_WINSYS_STATE_CONFLICT;
    }
    if (Context->Callbacks->pfnCreateSynchronizationObject2Cb == NULL ||
        Context->Callbacks->pfnDestroySynchronizationObjectCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.Info.Type = D3DDDI_MONITORED_FENCE;
    args.Info.MonitoredFence.InitialFenceValue = InitialValue;

    BC250_WINSYS_LOG("CreateMonitoredFence ENTER: InitialValue=%llu", (unsigned long long)InitialValue);
    hr = Context->Callbacks->pfnCreateSynchronizationObject2Cb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("CreateMonitoredFence LEAVE: hr=0x%08X hSync=0x%x CpuVa=%p GpuVa=0x%llX",
        (unsigned)hr, (unsigned)args.hSyncObject,
        (void*)args.Info.MonitoredFence.FenceValueCPUVirtualAddress,
        (unsigned long long)args.Info.MonitoredFence.FenceValueGPUVirtualAddress);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);
    if (args.hSyncObject == 0u ||
        args.Info.MonitoredFence.FenceValueCPUVirtualAddress == NULL ||
        args.Info.MonitoredFence.FenceValueGPUVirtualAddress == 0u) {
        Context->LastCallbackHr = E_UNEXPECTED;
        return BC250_WINSYS_BAD_CALLBACK_OUTPUT;
    }

    memset(Fence, 0, sizeof(*Fence));
    Fence->SyncObject = args.hSyncObject;
    Fence->CpuVirtualAddress = (volatile UINT64*)args.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    Fence->GpuVirtualAddress = args.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    Fence->InitialValue = InitialValue;
    Fence->Created = TRUE;
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysDestroyMonitoredFence(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence)
{
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (!Fence->Created) {
        return BC250_WINSYS_OK;
    }
    if (Context->Callbacks->pfnDestroySynchronizationObjectCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hSyncObject = Fence->SyncObject;

    BC250_WINSYS_LOG("DestroyMonitoredFence ENTER: hSync=0x%x", (unsigned)Fence->SyncObject);
    hr = Context->Callbacks->pfnDestroySynchronizationObjectCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("DestroyMonitoredFence LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    memset(Fence, 0, sizeof(*Fence));
    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysSignalFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue)
{
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || Fence == NULL || !Fence->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSignalSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.FenceValueArray = &FenceValue;

    BC250_WINSYS_LOG("SignalFenceFromCpu ENTER: hSync=0x%x Value=%llu",
        (unsigned)Fence->SyncObject, (unsigned long long)FenceValue);
    hr = Context->Callbacks->pfnSignalSynchronizationObjectFromCpuCb(Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SignalFenceFromCpu LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

/*
 * Sinalizacao de fence monitorada PELO CONTRATO WDDM.
 *
 * Corrida 12/09 22:57 (BugCheck 0x116, 5 TDRs): o blt da UMD escrevia o valor
 * da fence monitorada com um pacote SDMA montado por ela mesma, e o
 * D3DDDICB_SUBMITCOMMAND saia sem qualquer objeto de sincronizacao. O dxgkrnl
 * nunca ficava sabendo do sinal, entao pfnWaitForSynchronizationObjectFromCpuCb
 * nunca era satisfeito e o VidSch declarava hang com a engine OCIOSA e DRENADA
 * (RPTR==WPTR, STATUS1=0x3FF em todos os 5 quiesces).
 *
 * O caminho correto e' este: o runtime enfileira o sinal no MESMO contexto,
 * depois do comando, e o KMD materializa a escrita em
 * Bc250McdmBuildSignalMonitoredFence -- que ja emite Bc250SdmaEmitFence64,
 * largura correta para a fence de 64 bits do WDDM.
 *
 * Ordenacao: o dxgkrnl serializa os pacotes do contexto, logo o sinal so'
 * executa depois do command buffer submetido imediatamente antes.
 */
BC250_WINSYS_STATUS Bc250WinsysSignalMonitoredFenceFromGpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 FenceValue)
{
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU args;
    HRESULT hr;

    if (Context == NULL || !Context->Initialized || !Context->ContextCreated ||
        Fence == NULL || !Fence->Created || FenceValue == 0ull) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnSignalSynchronizationObjectFromGpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    memset(&args, 0, sizeof(args));
    args.hContext = Context->KernelContext;
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.MonitoredFenceValueArray = &FenceValue;

    BC250_WINSYS_LOG("SignalMonitoredFenceFromGpu ENTER: hCtx=0x%x hSync=0x%x Value=%llu",
        (unsigned)(ULONG_PTR)Context->KernelContext, (unsigned)Fence->SyncObject,
        (unsigned long long)FenceValue);
    hr = Context->Callbacks->pfnSignalSynchronizationObjectFromGpuCb(
        Context->RuntimeDevice, &args);
    BC250_WINSYS_LOG("SignalMonitoredFenceFromGpu LEAVE: hr=0x%08X", (unsigned)hr);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

BC250_WINSYS_STATUS Bc250WinsysWaitForFenceFromCpu(
    BC250_WINSYS_CONTEXT *Context,
    const BC250_WINSYS_MONITORED_FENCE *Fence,
    UINT64 TargetFenceValue)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU args;
    UINT64 currentFence = 0ull;
    HRESULT hr;
    BOOL timedOut = FALSE;

    if (Context == NULL || !Context->Initialized || Fence == NULL || !Fence->Created) {
        return BC250_WINSYS_BAD_ARGUMENT;
    }
    if (Context->Callbacks->pfnWaitForSynchronizationObjectFromCpuCb == NULL) {
        return BC250_WINSYS_MISSING_CALLBACK;
    }

    if (Fence->CpuVirtualAddress != NULL) {
        currentFence = *Fence->CpuVirtualAddress;
        if (currentFence >= TargetFenceValue) {
            BC250_WINSYS_LOG("WaitForFenceFromCpu SKIP (already reached): TargetFence=%llu CurrentFence=%llu",
                (unsigned long long)TargetFenceValue, (unsigned long long)currentFence);
            return BC250_WINSYS_OK;
        }
    }

    memset(&args, 0, sizeof(args));
    args.ObjectCount = 1u;
    args.ObjectHandleArray = &Fence->SyncObject;
    args.FenceValueArray = &TargetFenceValue;

    BC250_WINSYS_LOG("WaitForFenceFromCpu ENTER: TargetFence=%llu CurrentFence=%llu SyncObj=0x%x",
        (unsigned long long)TargetFenceValue, (unsigned long long)currentFence, (unsigned)Fence->SyncObject);
    hr = Bc250WinsysBoundedCpuWait(Context, &args, "WaitForFenceFromCpu", &timedOut);

    if (Fence->CpuVirtualAddress != NULL) {
        currentFence = *Fence->CpuVirtualAddress;
    }
    BC250_WINSYS_LOG("WaitForFenceFromCpu LEAVE: hr=0x%08X TargetFence=%llu CurrentFence=%llu",
        (unsigned)hr, (unsigned long long)TargetFenceValue, (unsigned long long)currentFence);

    if (FAILED(hr)) return Bc250WinsysRememberFailure(Context, hr);

    Context->LastCallbackHr = S_OK;
    return BC250_WINSYS_OK;
}

const char *Bc250WinsysStatusName(BC250_WINSYS_STATUS Status)
{
    switch (Status) {
    case BC250_WINSYS_OK: return "OK";
    case BC250_WINSYS_BAD_ARGUMENT: return "BAD_ARGUMENT";
    case BC250_WINSYS_MISSING_CALLBACK: return "MISSING_CALLBACK";
    case BC250_WINSYS_STATE_CONFLICT: return "STATE_CONFLICT";
    case BC250_WINSYS_CALLBACK_FAILED: return "CALLBACK_FAILED";
    case BC250_WINSYS_BAD_CALLBACK_OUTPUT: return "BAD_CALLBACK_OUTPUT";
    default: return "UNKNOWN";
    }
}
