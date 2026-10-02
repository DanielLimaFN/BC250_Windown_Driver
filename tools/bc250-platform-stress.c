/*
 * bc250-platform-stress.c -- Frente W1 do PLANO_DESKTOP_ACELERADO.md (30/09/2026)
 *
 * Pergunta unica: a plataforma (controlador de GDDR6, fonte, temperatura) trava ou
 * corrompe dados SEM o nosso driver? A BC-250 e' APU: a CPU usa o mesmo controlador
 * de memoria que para durante a trava dura. Rodar com o BasicDisplay ativo.
 *
 * O que faz: N threads (padrao = processadores logicos), cada uma com um bloco de
 * memoria; em voltas, escreve um padrao que depende do endereco e da volta, le de
 * volta e confere palavra por palavra, e faz copias grandes entre metades do bloco.
 * Batimento a cada 2 s num arquivo com FlushFileBuffers: se a maquina travar, a
 * ultima linha diz a hora e o progresso.
 *
 * Uso: bc250-platform-stress.exe [minutos=45] [MB_total=8192] [threads=0(auto)] [pasta_saida=.]
 * Saida: 0 = sem erro de dados no tempo pedido; 3 = erro de dados; outros = falha de setup.
 * Nao toca em driver, registrador nem configuracao. Leitura/escrita so' na propria memoria.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    UINT64 *mem;
    SIZE_T words;
    volatile LONG64 passes;
    volatile LONG64 bytesMoved;
    volatile LONG64 errors;
    UINT64 firstBadIndex;
    UINT64 firstBadExpected;
    UINT64 firstBadGot;
    int id;
} WORKER;

static volatile LONG g_stop = 0;

static UINT64 Pattern(UINT64 index, UINT64 pass, int id)
{
    UINT64 x = index * 0x9E3779B97F4A7C15ull ^ (pass << 32) ^ (UINT64)id * 0xD1B54A32D192ED03ull;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32;
    return x;
}

static DWORD WINAPI WorkerMain(LPVOID param)
{
    WORKER *w = (WORKER *)param;
    UINT64 pass = 0;
    SIZE_T half = w->words / 2u;
    while (!g_stop) {
        SIZE_T i;
        for (i = 0; i < w->words; ++i) w->mem[i] = Pattern(i, pass, w->id);
        for (i = 0; i < w->words; ++i) {
            UINT64 e = Pattern(i, pass, w->id);
            if (w->mem[i] != e) {
                if (InterlockedIncrement64(&w->errors) == 1) {
                    w->firstBadIndex = i; w->firstBadExpected = e; w->firstBadGot = w->mem[i];
                }
            }
        }
        /* copia grande: metade de cima <- metade de baixo, e confere */
        memcpy(w->mem + half, w->mem, half * sizeof(UINT64));
        for (i = 0; i < half; ++i) {
            UINT64 e = Pattern(i, pass, w->id);
            if (w->mem[half + i] != e) {
                if (InterlockedIncrement64(&w->errors) == 1) {
                    w->firstBadIndex = half + i; w->firstBadExpected = e; w->firstBadGot = w->mem[half + i];
                }
            }
        }
        InterlockedAdd64(&w->bytesMoved, (LONG64)(w->words * 8u * 2u + half * 8u * 3u));
        InterlockedIncrement64(&w->passes);
        ++pass;
    }
    return 0;
}

static void Beat(HANDLE f, const char *text)
{
    DWORD wr;
    WriteFile(f, text, (DWORD)strlen(text), &wr, NULL);
    FlushFileBuffers(f);
    fputs(text, stdout);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int minutes = argc > 1 ? atoi(argv[1]) : 45;
    UINT64 totalMb = argc > 2 ? (UINT64)_strtoui64(argv[2], NULL, 10) : 8192u;
    int threads = argc > 3 ? atoi(argv[3]) : 0;
    const char *outDir = argc > 4 ? argv[4] : ".";
    SYSTEM_INFO si;
    WORKER *workers;
    HANDLE *handles;
    char path[MAX_PATH], line[512];
    HANDLE log;
    SYSTEMTIME st;
    ULONGLONG start, now;
    LONG64 lastBytes = 0;
    int i, exitCode = 0;

    GetSystemInfo(&si);
    if (threads <= 0) threads = (int)si.dwNumberOfProcessors;
    if (minutes <= 0 || totalMb < 64u || threads <= 0 || threads > 64) {
        fprintf(stderr, "Parametros invalidos\n");
        return 2;
    }
    GetLocalTime(&st);
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\bc250-platform-stress_%04u-%02u-%02u_%02u-%02u-%02u.txt",
        outDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (log == INVALID_HANDLE_VALUE) { fprintf(stderr, "Nao abri %s\n", path); return 4; }

    workers = (WORKER *)calloc((size_t)threads, sizeof(WORKER));
    handles = (HANDLE *)calloc((size_t)threads, sizeof(HANDLE));
    if (!workers || !handles) return 5;

    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "INICIO %04u-%02u-%02u %02u:%02u:%02u minutos=%d MB_total=%llu threads=%d (W1: plataforma sem o nosso driver)\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, minutes, totalMb, threads);
    Beat(log, line);

    for (i = 0; i < threads; ++i) {
        SIZE_T bytes = (SIZE_T)((totalMb * 1024u * 1024u) / (UINT64)threads) & ~(SIZE_T)0xFFFF;
        workers[i].id = i;
        workers[i].words = bytes / 8u;
        workers[i].mem = (UINT64 *)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!workers[i].mem) {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "FALHA: VirtualAlloc de %llu bytes na thread %d (erro %lu). Use menos MB.\r\n",
                (UINT64)bytes, i, GetLastError());
            Beat(log, line);
            return 6;
        }
    }
    for (i = 0; i < threads; ++i) handles[i] = CreateThread(NULL, 0, WorkerMain, &workers[i], 0, NULL);

    start = GetTickCount64();
    for (;;) {
        LONG64 passes = 0, bytes = 0, errors = 0;
        Sleep(2000);
        now = GetTickCount64();
        for (i = 0; i < threads; ++i) { passes += workers[i].passes; bytes += workers[i].bytesMoved; errors += workers[i].errors; }
        GetLocalTime(&st);
        _snprintf_s(line, sizeof(line), _TRUNCATE,
            "%02u:%02u:%02u t=%llus voltas=%lld GB/s=%.1f erros=%lld\r\n",
            st.wHour, st.wMinute, st.wSecond, (now - start) / 1000u, passes,
            (double)(bytes - lastBytes) / 2.0 / 1e9, errors);
        lastBytes = bytes;
        Beat(log, line);
        if (errors > 0 && exitCode == 0) {
            for (i = 0; i < threads; ++i) {
                if (workers[i].errors > 0) {
                    _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "ERRO DE DADOS thread=%d indice=%llu esperado=%016llX lido=%016llX\r\n",
                        i, workers[i].firstBadIndex, workers[i].firstBadExpected, workers[i].firstBadGot);
                    Beat(log, line);
                }
            }
            exitCode = 3;
        }
        if (now - start >= (ULONGLONG)minutes * 60000u) break;
    }
    InterlockedExchange(&g_stop, 1);
    WaitForMultipleObjects((DWORD)threads, handles, TRUE, 60000);
    _snprintf_s(line, sizeof(line), _TRUNCATE, "FIM veredito=%s (codigo %d). Sem trava no tempo pedido.\r\n",
        exitCode == 0 ? "SEM_ERRO_DE_DADOS" : "ERRO_DE_DADOS", exitCode);
    Beat(log, line);
    CloseHandle(log);
    return exitCode;
}
