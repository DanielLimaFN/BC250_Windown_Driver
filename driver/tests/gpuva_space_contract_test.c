#include "../core/bc250_gpuva_space_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) { printf("FAIL line %d: %s\n", __LINE__, #Expression); return __LINE__; } } while (0)
#endif

static int Bc250RunGpuvaSpaceContractTest(void)
{
    static BC250_GPUVA_SPACE space;
    static BC250_GPUVA_SPACE replaceSpace;
    static BC250_GPUVA_SPACE scratchSpace;
    BC250_GPUVA_RANGE_RECORD r1, r2, r3, r4;
    uint64_t rangeId1 = 0, rangeId2 = 0, rangeId3 = 0, rangeId4 = 0;
    BC250_MCDM_HANDLE procA = (BC250_MCDM_HANDLE)0x1000;
    BC250_MCDM_HANDLE procB = (BC250_MCDM_HANDLE)0x2000;
    BC250_MCDM_HANDLE ctxA1 = (BC250_MCDM_HANDLE)0x1100;
    uint32_t resolvedSegment = 0u;
    uint64_t resolvedOffset = 0ull;

    /* 1. Inicialização */
    CHECK(Bc250GpuvaSpaceInitialize(&space, 0u) == BC250_GPUVA_OK);
    CHECK(space.AddressBits == BC250_GPUVA_PT_VA_BITS);
    CHECK(space.MaxAddress == (1ull << BC250_GPUVA_PT_VA_BITS));
    CHECK(space.PageTableEpoch == 1u);

    CHECK(Bc250GpuvaSpaceInitialize(&space, 65u) == BC250_GPUVA_OK);
    CHECK(space.AddressBits == BC250_GPUVA_PT_VA_BITS);

    CHECK(Bc250GpuvaSpaceInitialize(&space, 48u) == BC250_GPUVA_OK);
    CHECK(space.AddressBits == 48u);
    CHECK(space.MaxAddress == 0x0001000000000000ull);
    CHECK(space.PageTableEpoch == 1u);

    /* 2. Testes de inserção básica e validação de argumentos */
    /* Base zero ou tamanho zero */
    memset(&r1, 0, sizeof(r1));
    r1.BaseGpuVa = 0u;
    r1.Size = 4096u;
    r1.OwnerProcess = procA;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_BAD_ARGUMENT);

    r1.BaseGpuVa = 0x10000u;
    r1.Size = 0u;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_BAD_ARGUMENT);

    /* Desalinhamento (não múltiplo de 4KB na inserção) */
    r1.BaseGpuVa = 0x10001u;
    r1.Size = 4096u;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_MISALIGNED);

    r1.BaseGpuVa = 0x10000u;
    r1.Size = 4095u;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_MISALIGNED);

    /* Overflow de base + tamanho */
    r1.BaseGpuVa = 0xFFFFFFFFFFFFF000ull;
    r1.Size = 0x2000u;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_RANGE_OVERFLOW);

    /* Fora dos limites de 48 bits */
    r1.BaseGpuVa = 0x0001000000000000ull;
    r1.Size = 4096u;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_OUT_OF_BOUNDS);

    /* 3. Inserção de mapeamento válido: [0x10000, 0x14000) = 16KB para procA */
    memset(&r1, 0, sizeof(r1));
    r1.BaseGpuVa = 0x000010000000ull;
    r1.Size = 0x4000u; /* 16 KiB */
    r1.OwnerProcess = procA;
    r1.OwnerContext = ctxA1;
    r1.BackingSegmentId = 1u;
    r1.BackingSegmentOffset = 0x800000u;
    r1.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r1, &rangeId1) == BC250_GPUVA_OK);
    CHECK(rangeId1 != 0ull);
    CHECK(space.LiveCount == 1u);

    /* 4. Validação de cobertura integral em mapeamento único */
    /* Cobertura total exata */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010000000ull, 0x4000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OK);

    /* Subfaixa interna (ex: IB de 128 bytes no offset +256) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010000100ull, 128u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceResolveAddress(&space, procA, 0x000010000100ull,
        BC250_PTE_READABLE, &resolvedSegment, &resolvedOffset) == BC250_GPUVA_OK);
    CHECK(resolvedSegment == 1u && resolvedOffset == 0x800100u);

    /* 5. Inserção de segundo intervalo contíguo: [0x14000, 0x18000) = 16KB */
    memset(&r2, 0, sizeof(r2));
    r2.BaseGpuVa = 0x000010004000ull;
    r2.Size = 0x4000u; /* 16 KiB */
    r2.OwnerProcess = procA;
    r2.OwnerContext = ctxA1;
    r2.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE | BC250_PTE_EXECUTABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r2, &rangeId2) == BC250_GPUVA_OK);

    /* Validação que cruza a fronteira entre r1 e r2 sem gaps: [0x12000, 0x16000) */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010002000ull, 0x4000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OK);

    /* Cobertura de r1 + r2 inteiros: 32KB */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010000000ull, 0x8000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OK);

    /* 6. Detecção estrita de GAPs */
    /* Terceiro intervalo NÃO-contíguo (com gap de 4KB entre 0x18000 e 0x19000): [0x19000, 0x1D000) */
    memset(&r3, 0, sizeof(r3));
    r3.BaseGpuVa = 0x000010009000ull; /* Gap de 0x10008000 ate 0x10009000 */
    r3.Size = 0x4000u;
    r3.OwnerProcess = procA;
    r3.OwnerContext = ctxA1;
    r3.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r3, &rangeId3) == BC250_GPUVA_OK);

    /* Consulta que engloba o GAP deve falhar com BC250_GPUVA_GAP_DETECTED */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010007000ull, 0x3000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_GAP_DETECTED);

    /* Consulta em endereço completamente não mapeado */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000020000000ull, 0x1000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_NOT_FOUND);

    /* 7. Isolamento de Processos e Contextos */
    /* Consulta por outro processo procB na mesma faixa deve falhar */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procB, NULL,
        0x000010000000ull, 0x1000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OWNER_MISMATCH);

    /* Inserção para procB em mesma faixa virtual (isolamento de espaço de endereçamento) */
    memset(&r4, 0, sizeof(r4));
    r4.BaseGpuVa = 0x000010000000ull;
    r4.Size = 0x4000u;
    r4.OwnerProcess = procB;
    r4.BackingSegmentId = 1u;
    r4.BackingSegmentOffset = 0xC10000u;
    r4.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&space, &r4, &rangeId4) == BC250_GPUVA_OK);

    /* Agora procB é válido em seu próprio contexto */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procB, NULL,
        0x000010000000ull, 0x4000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceResolveAddress(&space, procB, 0x000010000100ull,
        BC250_PTE_READABLE, &resolvedSegment, &resolvedOffset) == BC250_GPUVA_OK);
    CHECK(resolvedSegment == 1u && resolvedOffset == 0xC10100u);
    CHECK(Bc250GpuvaSpaceResolveAddress(&space, (BC250_MCDM_HANDLE)0x3000,
        0x000010000100ull, BC250_PTE_READABLE,
        &resolvedSegment, &resolvedOffset) == BC250_GPUVA_OWNER_MISMATCH);

    /* 8. Rejeição por Permissões Insuficientes */
    /* r3 não tem permissão EXECUTABLE */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010009000ull, 0x1000u, BC250_PTE_EXECUTABLE, space.PageTableEpoch) == BC250_GPUVA_PERMISSION_DENIED);

    /* 9. Desmapeamento (Unmap) e Proteção contra Use-After-Unmap */
    CHECK(Bc250GpuvaSpaceRemoveRange(&space, procA, 0x000010000000ull, 0x4000u) == BC250_GPUVA_OK);
    /* Epoch avançou */
    CHECK(space.PageTableEpoch > 1u);

    /* Faixa que foi desmapeada por procA gera NOT_RESIDENT para procA */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010000000ull, 0x1000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_NOT_RESIDENT);

    /* Faixa completamente não mapeada para ninguém gera NOT_FOUND */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000050000000ull, 0x1000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_NOT_FOUND);

    /* 10. Invalidação por Teardown de Contexto / Objeto */
    CHECK(Bc250GpuvaSpaceInvalidateForObject(&space, procA, ctxA1, NULL) == BC250_GPUVA_OK);
    /* Todas as faixas de ctxA1 foram invalidadas */
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&space, procA, ctxA1,
        0x000010004000ull, 0x1000u, BC250_PTE_READABLE, space.PageTableEpoch) == BC250_GPUVA_IN_TEARDOWN);

    /* 11. Replace transacional: unmap parcial preserva esquerda e direita. */
    CHECK(Bc250GpuvaSpaceInitialize(&replaceSpace, 48u) == BC250_GPUVA_OK);
    memset(&r1, 0, sizeof(r1));
    r1.BaseGpuVa = 0x000030000000ull;
    r1.Size = 0x4000u;
    r1.OwnerProcess = procA;
    r1.OwnerContext = ctxA1;
    r1.BackingSegmentOffset = 0x800000u;
    r1.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
    CHECK(Bc250GpuvaSpaceInsertRange(&replaceSpace, &r1, &rangeId1) == BC250_GPUVA_OK);
    memset(&r2, 0, sizeof(r2));
    r2.BaseGpuVa = 0x000030001000ull;
    r2.Size = 0x1000u;
    r2.OwnerProcess = procA;
    CHECK(Bc250GpuvaSpaceReplaceRangeTransactional(
        &replaceSpace, &scratchSpace, &r2, 0u) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&replaceSpace, procA, ctxA1,
        0x000030000000ull, 0x1000u, BC250_PTE_READABLE,
        replaceSpace.PageTableEpoch) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&replaceSpace, procA, ctxA1,
        0x000030001000ull, 0x1000u, BC250_PTE_READABLE,
        replaceSpace.PageTableEpoch) != BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&replaceSpace, procA, ctxA1,
        0x000030002000ull, 0x2000u, BC250_PTE_READABLE,
        replaceSpace.PageTableEpoch) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceResolveAddress(&replaceSpace, procA,
        0x000030002800ull, BC250_PTE_READABLE,
        &resolvedSegment, &resolvedOffset) == BC250_GPUVA_OK);
    CHECK(resolvedOffset == 0x802800u);

    /* 12. Remap parcial só publica no sucesso e altera permissões. */
    r2.Permissions = BC250_PTE_READABLE | BC250_PTE_EXECUTABLE;
    r2.BackingSegmentOffset = 0x900000u;
    CHECK(Bc250GpuvaSpaceReplaceRangeTransactional(
        &replaceSpace, &scratchSpace, &r2, 1u) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&replaceSpace, procA, ctxA1,
        r2.BaseGpuVa, r2.Size, BC250_PTE_EXECUTABLE,
        replaceSpace.PageTableEpoch) == BC250_GPUVA_OK);
    CHECK(Bc250GpuvaSpaceResolveAddress(&replaceSpace, procA,
        r2.BaseGpuVa + 0x800u, BC250_PTE_EXECUTABLE,
        &resolvedSegment, &resolvedOffset) == BC250_GPUVA_OK);
    CHECK(resolvedOffset == 0x900800u);
    r2.Permissions = 0u;
    CHECK(Bc250GpuvaSpaceReplaceRangeTransactional(
        &replaceSpace, &scratchSpace, &r2, 1u) == BC250_GPUVA_PERMISSION_DENIED);
    CHECK(Bc250GpuvaSpaceValidateRangeCoverage(&replaceSpace, procA, ctxA1,
        r2.BaseGpuVa, r2.Size, BC250_PTE_EXECUTABLE,
        replaceSpace.PageTableEpoch) == BC250_GPUVA_OK);

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunGpuvaSpaceContractTest());
}
#else
int main(void)
{
    return Bc250RunGpuvaSpaceContractTest();
}
#endif
