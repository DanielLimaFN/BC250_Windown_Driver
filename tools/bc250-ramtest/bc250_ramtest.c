/*
 * bc250_ramtest.c (doc 595) -- teste de RAM do sistema em modo usuario.
 *
 * Na BC-250 a RAM do Windows e a VRAM sao a MESMA GDDR6. As travas duras
 * (11 seguidas, GPU ociosa, maquina morta entre duas observacoes) sao
 * compativeis com instabilidade de memoria. Este teste aloca uma fatia grande
 * da RAM, escreve padroes com todas as CPUs ao mesmo tempo (para estressar o
 * controlador como a carga real) e confere cada palavra.
 *
 * Padroes por passada: endereco-no-endereco, inverso do endereco, 0x00/0xFF,
 * xadrez 0x55/0xAA, um-bit-andando e pseudoaleatorio (xorshift, semente por
 * passada). Cada erro registra endereco, esperado, lido e bits trocados.
 *
 * Uso: bc250_ramtest.exe [--mb N] [--passes N] [--threads N] [--out arquivo]
 *   --mb      MB a testar (padrao: 70% da RAM livre, teto 12288)
 *   --passes  passadas completas (padrao 2)
 *   --threads threads (padrao: todas as CPUs logicas)
 *   --out     relatorio (padrao: bc250_ramtest_<data>.txt na pasta atual)
 *   --selftest troca 1 bit de proposito na 1a passada (prova que o teste pega erro;
 *             o veredito TEM de sair FALHA com exatamente 1 erro)
 * Saida: 0 = sem erro; 1 = erros de memoria; 2 = falha de alocacao/uso.
 * So' usa memoria do proprio processo; nao toca em driver nem hardware.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_THREADS 64
#define MAX_REPORTED_ERRORS 64

typedef struct {
    volatile uint64_t *base;
    size_t words;
    unsigned pattern;
    uint64_t seed;
    uint64_t errors;
    uint64_t firstAddr[8];
    uint64_t firstExp[8];
    uint64_t firstGot[8];
    unsigned firstCount;
    uint64_t bitFlips[64];
} SLICE;

static const char *g_patternNames[] = {
    "endereco", "inverso-do-endereco", "zeros", "uns", "xadrez-55", "xadrez-AA",
    "bit-andando", "aleatorio"
};
#define PATTERN_COUNT 8u

static uint64_t Xorshift(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    *s = x;
    return x;
}

static uint64_t Expected(unsigned pattern, uint64_t index, uint64_t addr, uint64_t *rng)
{
    switch (pattern) {
    case 0: return addr;
    case 1: return ~addr;
    case 2: return 0ull;
    case 3: return ~0ull;
    case 4: return 0x5555555555555555ull;
    case 5: return 0xAAAAAAAAAAAAAAAAull;
    case 6: return 1ull << (index & 63u);
    default: return Xorshift(rng);
    }
}

static DWORD WINAPI WriteSlice(LPVOID p)
{
    SLICE *s = (SLICE *)p;
    uint64_t rng = s->seed;
    size_t i;
    for (i = 0; i < s->words; ++i) {
        s->base[i] = Expected(s->pattern, i, (uint64_t)(uintptr_t)&s->base[i], &rng);
    }
    return 0;
}

static DWORD WINAPI VerifySlice(LPVOID p)
{
    SLICE *s = (SLICE *)p;
    uint64_t rng = s->seed;
    size_t i;
    for (i = 0; i < s->words; ++i) {
        uint64_t exp = Expected(s->pattern, i, (uint64_t)(uintptr_t)&s->base[i], &rng);
        uint64_t got = s->base[i];
        if (got != exp) {
            uint64_t diff = got ^ exp;
            unsigned b;
            s->errors++;
            for (b = 0; b < 64u; ++b) if (diff & (1ull << b)) s->bitFlips[b]++;
            if (s->firstCount < 8u) {
                s->firstAddr[s->firstCount] = (uint64_t)(uintptr_t)&s->base[i];
                s->firstExp[s->firstCount] = exp;
                s->firstGot[s->firstCount] = got;
                s->firstCount++;
            }
        }
    }
    return 0;
}

static void RunParallel(LPTHREAD_START_ROUTINE fn, SLICE *slices, unsigned n)
{
    HANDLE h[MAX_THREADS];
    unsigned t;
    for (t = 0; t < n; ++t) {
        h[t] = CreateThread(NULL, 0, fn, &slices[t], 0, NULL);
        if (h[t] == NULL) { fn(&slices[t]); }
    }
    for (t = 0; t < n; ++t) {
        if (h[t] != NULL) { WaitForSingleObject(h[t], INFINITE); CloseHandle(h[t]); }
    }
}

int main(int argc, char **argv)
{
    MEMORYSTATUSEX ms;
    SYSTEM_INFO si;
    unsigned long long mb = 0, passes = 2;
    unsigned threads = 0, t, pass, pat;
    const char *outPath = NULL;
    char defaultOut[MAX_PATH];
    FILE *out;
    size_t bytes, words, per;
    volatile uint64_t *buf;
    SLICE slices[MAX_THREADS];
    uint64_t totalErrors = 0, flips[64];
    unsigned reported = 0;
    LARGE_INTEGER freq, t0, t1;
    SYSTEMTIME now;
    int i;
    int selftest = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--mb") && i + 1 < argc) mb = _strtoui64(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--passes") && i + 1 < argc) passes = _strtoui64(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else { fprintf(stderr, "uso: bc250_ramtest.exe [--mb N] [--passes N] [--threads N] [--out arquivo]\n"); return 2; }
    }
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    GetSystemInfo(&si);
    if (threads == 0 || threads > MAX_THREADS) threads = si.dwNumberOfProcessors > MAX_THREADS ? MAX_THREADS : si.dwNumberOfProcessors;
    if (mb == 0) {
        mb = (ms.ullAvailPhys / (1024ull * 1024ull)) * 70ull / 100ull;
        if (mb > 12288ull) mb = 12288ull;
    }
    if (passes == 0) passes = 1;
    GetLocalTime(&now);
    if (outPath == NULL) {
        sprintf_s(defaultOut, sizeof(defaultOut), "bc250_ramtest_%04u-%02u-%02u_%02u-%02u-%02u.txt",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        outPath = defaultOut;
    }
    if (fopen_s(&out, outPath, "w") != 0 || out == NULL) { fprintf(stderr, "nao abri %s\n", outPath); return 2; }

    bytes = (size_t)(mb * 1024ull * 1024ull);
    buf = (volatile uint64_t *)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buf == NULL) {
        fprintf(out, "FALHA: VirtualAlloc de %llu MB (erro %lu)\n", mb, GetLastError());
        fclose(out);
        fprintf(stderr, "FALHA de alocacao de %llu MB\n", mb);
        return 2;
    }
    words = bytes / sizeof(uint64_t);
    per = words / threads;
    memset(flips, 0, sizeof(flips));

    fprintf(out, "bc250_ramtest (doc 595) %04u-%02u-%02u %02u:%02u:%02u\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    fprintf(out, "RAM total=%llu MB livre=%llu MB testados=%llu MB threads=%u passadas=%llu%s\n",
        ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20, mb, threads, passes,
        selftest ? " SELFTEST(1 bit injetado)" : "");
    printf("Testando %llu MB com %u threads, %llu passada(s)...\n", mb, threads, passes);
    QueryPerformanceFrequency(&freq);

    for (pass = 0; pass < passes; ++pass) {
        for (pat = 0; pat < PATTERN_COUNT; ++pat) {
            uint64_t patErrors = 0;
            double secs;
            for (t = 0; t < threads; ++t) {
                memset(&slices[t], 0, sizeof(slices[t]));
                slices[t].base = buf + (size_t)t * per;
                slices[t].words = (t == threads - 1u) ? words - (size_t)t * per : per;
                slices[t].pattern = pat;
                slices[t].seed = 0x9E3779B97F4A7C15ull ^ ((uint64_t)(pass + 1) << 32) ^ (uint64_t)(t + 1);
            }
            QueryPerformanceCounter(&t0);
            RunParallel(WriteSlice, slices, threads);
            if (selftest && pass == 0u && pat == 0u && slices[0].words > 12345u) {
                slices[0].base[12345] ^= 0x8ull; /* falha injetada de proposito */
            }
            RunParallel(VerifySlice, slices, threads);
            QueryPerformanceCounter(&t1);
            secs = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
            for (t = 0; t < threads; ++t) {
                unsigned k, b;
                patErrors += slices[t].errors;
                for (b = 0; b < 64u; ++b) flips[b] += slices[t].bitFlips[b];
                for (k = 0; k < slices[t].firstCount && reported < MAX_REPORTED_ERRORS; ++k, ++reported) {
                    fprintf(out, "  ERRO passada=%u padrao=%s addr=0x%016llX esperado=0x%016llX lido=0x%016llX xor=0x%016llX\n",
                        pass + 1, g_patternNames[pat],
                        (unsigned long long)slices[t].firstAddr[k],
                        (unsigned long long)slices[t].firstExp[k],
                        (unsigned long long)slices[t].firstGot[k],
                        (unsigned long long)(slices[t].firstExp[k] ^ slices[t].firstGot[k]));
                }
            }
            totalErrors += patErrors;
            fprintf(out, "passada %u padrao %-20s erros=%llu  %.1f s  %.0f MB/s (escrita+leitura)\n",
                pass + 1, g_patternNames[pat], (unsigned long long)patErrors, secs,
                secs > 0.0 ? (2.0 * (double)mb) / secs : 0.0);
            printf("  passada %u %-20s erros=%llu (%.1f s)\n", pass + 1, g_patternNames[pat],
                (unsigned long long)patErrors, secs);
            fflush(out);
        }
    }
    if (totalErrors != 0) {
        unsigned b;
        fprintf(out, "bits trocados por posicao:");
        for (b = 0; b < 64u; ++b) if (flips[b]) fprintf(out, " b%u=%llu", b, (unsigned long long)flips[b]);
        fprintf(out, "\n");
    }
    fprintf(out, "VEREDITO: %s (erros=%llu em %llu MB x %llu passada(s))\n",
        totalErrors ? "FALHA -- ERROS DE MEMORIA" : "SEM ERROS no escopo testado",
        (unsigned long long)totalErrors, mb, passes);
    fclose(out);
    VirtualFree((LPVOID)buf, 0, MEM_RELEASE);
    printf("VEREDITO: %s (erros=%llu). Relatorio: %s\n",
        totalErrors ? "FALHA -- ERROS DE MEMORIA" : "SEM ERROS no escopo testado",
        (unsigned long long)totalErrors, outPath);
    return totalErrors ? 1 : 0;
}
