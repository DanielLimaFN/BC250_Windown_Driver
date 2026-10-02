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


    /*
     * C3.12 (12/09) -- OCUPACAO POR ESTADO.
     *
     * MEDIDO na corrida 16:31: PublishStatus=3 (BC250_GPUVA_NO_SPACE) com
     * Attempts=1 Busy=0 -- o publicador pegou o lock de primeira e a falha veio
     * de DENTRO, por exaustao do teto antigo de 256 slots. Isso REFUTOU a hipotese da C3.8
     * (contencao leitor/publicador).
     *
     * O que NO_SPACE sozinho nao diz: 256 faixas legitimamente em uso, ou slots
     * presos em TEARDOWN e nunca reciclados? Bc250GpuvaSpaceInsertRange so'
     * reaproveita FREE e UNMAPPED, entao TEARDOWN acumula. Reusable espelha
     * EXATAMENTE esse criterio -- se Reusable==0 com Teardown>0, e' vazamento,
     * nao capacidade, e o conserto e' outro.
     */
    {
        static BC250_GPUVA_SPACE occSpace;
        BC250_GPUVA_SPACE_OCCUPANCY occ;

        CHECK(Bc250GpuvaSpaceInitialize(&occSpace, 34u) == BC250_GPUVA_OK);
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(&occSpace, &occ) == BC250_GPUVA_OK);
        CHECK(occ.Capacity == BC250_GPUVA_SPACE_MAX_RANGES);
        CHECK(occ.Free == BC250_GPUVA_SPACE_MAX_RANGES);
        CHECK(occ.Mapped == 0u && occ.Teardown == 0u && occ.Unmapped == 0u);
        CHECK(occ.Reusable == BC250_GPUVA_SPACE_MAX_RANGES);

        /* Um slot de cada estado nao-livre: as contagens tem de seguir. */
        occSpace.Ranges[0].State = BC250_GPUVA_RANGE_MAPPED;
        occSpace.Ranges[1].State = BC250_GPUVA_RANGE_UNMAPPED;
        occSpace.Ranges[2].State = BC250_GPUVA_RANGE_TEARDOWN;
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(&occSpace, &occ) == BC250_GPUVA_OK);
        CHECK(occ.Mapped == 1u);
        CHECK(occ.Unmapped == 1u);
        CHECK(occ.Teardown == 1u);
        CHECK(occ.Free == BC250_GPUVA_SPACE_MAX_RANGES - 3u);
        /* Reusable = Free + Unmapped, e NAO inclui Teardown. */
        CHECK(occ.Reusable == occ.Free + occ.Unmapped);
        CHECK(occ.Reusable == BC250_GPUVA_SPACE_MAX_RANGES - 2u);

        /*
         * O CASO QUE DISTINGUE AS DUAS CAUSAS: espaco inteiro em TEARDOWN.
         * Capacidade "cheia", nada reaproveitavel, e ZERO faixas em uso.
         * Se a corrida mostrar isto, o defeito e' reciclagem, nao tamanho.
         */
        {
            uint32_t k;
            for (k = 0u; k < BC250_GPUVA_SPACE_MAX_RANGES; ++k) {
                occSpace.Ranges[k].State = BC250_GPUVA_RANGE_TEARDOWN;
            }
            CHECK(Bc250GpuvaSpaceDescribeOccupancy(&occSpace, &occ) == BC250_GPUVA_OK);
            CHECK(occ.Teardown == BC250_GPUVA_SPACE_MAX_RANGES);
            CHECK(occ.Reusable == 0u);
            CHECK(occ.Mapped == 0u);
        }

        /* Argumentos degenerados falham fechado e zeram a saida. */
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(NULL, &occ) == BC250_GPUVA_BAD_ARGUMENT);
        CHECK(occ.Mapped == 0u && occ.Teardown == 0u && occ.Reusable == 0u);
        CHECK(Bc250GpuvaSpaceDescribeOccupancy(&occSpace, NULL) == BC250_GPUVA_BAD_ARGUMENT);
    }

    /*
     * DISCRIMINADOR DE ENDERECO DE FAULT (20/09) -- replay da Corrida 46.
     *
     * O registrador devolveu LOGICAL_PAGE_ADDR=0x2431 e o header vendorizado
     * nao declara o tamanho da pagina. As duas conversoes defensaveis caem em
     * lugares opostos, e este caso monta EXATAMENTE a vizinhanca medida no
     * silicio para provar que o discriminador separa as duas:
     *
     *   << 12 -> 0x2431000    (36 MB)   -- nada mapeado la
     *   << 20 -> 0x243100000  (9,05 GB) -- entre 0x242F00000 e 0x243340000,
     *                                      que sao alocacoes REAIS da winsys
     *                                      observadas no log da UMD.
     */
    {
        static BC250_GPUVA_SPACE faultSpace;
        BC250_GPUVA_RANGE_RECORD r;
        BC250_GPUVA_ADDRESS_SITE site;
        uint64_t faultRangeId = 0ull;
        const uint64_t page = 0x2431ull;

        CHECK(Bc250GpuvaSpaceInitialize(&faultSpace, 34u) == BC250_GPUVA_OK);

        /* Duas faixas vivas, com os endereços medidos na Corrida 46. */
        memset(&r, 0, sizeof(r));
        r.State = BC250_GPUVA_RANGE_MAPPED;
        r.BaseGpuVa = 0x242F00000ull;
        r.Size = 0x100000ull;                    /* termina em 0x243000000 */
        r.EndGpuVa = r.BaseGpuVa + r.Size;
        r.OwnerProcess = (BC250_MCDM_HANDLE)0x7777;
        r.Permissions = BC250_PTE_READABLE;
        r.IsResident = 1u;
        CHECK(Bc250GpuvaSpaceInsertRange(&faultSpace, &r, &faultRangeId) == BC250_GPUVA_OK);

        memset(&r, 0, sizeof(r));
        r.State = BC250_GPUVA_RANGE_MAPPED;
        r.BaseGpuVa = 0x243340000ull;
        r.Size = 0x10000ull;
        r.EndGpuVa = r.BaseGpuVa + r.Size;
        r.OwnerProcess = (BC250_MCDM_HANDLE)0x7777;
        r.Permissions = BC250_PTE_READABLE;
        r.IsResident = 1u;
        CHECK(Bc250GpuvaSpaceInsertRange(&faultSpace, &r, &faultRangeId) == BC250_GPUVA_OK);

        /* Candidato << 12: tem de cair FORA, e longe de tudo. */
        memset(&site, 0, sizeof(site));
        CHECK(Bc250GpuvaSpaceDescribeAddress(&faultSpace, page << 12, &site) == BC250_GPUVA_OK);
        CHECK(site.Found == 0u);
        CHECK(site.LiveRangesScanned == 2u);
        /* Esta abaixo de TUDO: nao ha faixa viva abaixo dele. */
        CHECK(site.HasBelow == 0u);
        CHECK(site.HasAbove == 1u);
        CHECK(site.NearestAboveBase == 0x242F00000ull);

        /* Candidato << 20: tem de cair FORA tambem, mas ENTRE as duas faixas. */
        memset(&site, 0, sizeof(site));
        CHECK(Bc250GpuvaSpaceDescribeAddress(&faultSpace, page << 20, &site) == BC250_GPUVA_OK);
        CHECK(site.Found == 0u);
        CHECK(site.HasBelow == 1u);
        CHECK(site.NearestBelowEnd == 0x243000000ull);
        CHECK(site.DistanceAboveEnd == (0x243100000ull - 0x243000000ull));
        CHECK(site.HasAbove == 1u);
        CHECK(site.NearestAboveBase == 0x243340000ull);

        /*
         * CONTRAPONTO, para as assercoes acima nao virarem carimbo: se a faixa
         * de baixo for grande o bastante para conter o candidato << 20, ele tem
         * de ser reportado DENTRO, com o offset exato.
         */
        memset(&r, 0, sizeof(r));
        r.State = BC250_GPUVA_RANGE_MAPPED;
        r.BaseGpuVa = 0x243050000ull;
        r.Size = 0x200000ull;                    /* cobre 0x243100000 */
        r.EndGpuVa = r.BaseGpuVa + r.Size;
        r.BackingSegmentId = 2u;                 /* GART / memoria de sistema */
        r.BackingSegmentOffset = 0x12340000ull;
        r.OwnerProcess = (BC250_MCDM_HANDLE)0x7777;
        r.Permissions = BC250_PTE_READABLE;
        r.IsResident = 1u;
        CHECK(Bc250GpuvaSpaceInsertRange(&faultSpace, &r, &faultRangeId) == BC250_GPUVA_OK);

        memset(&site, 0, sizeof(site));
        CHECK(Bc250GpuvaSpaceDescribeAddress(&faultSpace, page << 20, &site) == BC250_GPUVA_OK);
        CHECK(site.Found == 1u);
        CHECK(site.BaseGpuVa == 0x243050000ull);
        CHECK(site.OffsetInRange == (0x243100000ull - 0x243050000ull));
        CHECK(site.OwnerProcess == (BC250_MCDM_HANDLE)0x7777);
        /*
         * O segmento de backing tem de ser PROPAGADO. Sem ele, a Corrida 47
         * nomeou a alocacao que falha e nao disse se ela vive na VRAM local
         * (Seg 1, ~243 MB, limitada pela janela BAR) ou na GART em memoria de
         * sistema (Seg 2, 2 GB) -- que exigem consertos opostos.
         */
        CHECK(site.BackingSegmentId == 2u);
        CHECK(site.BackingSegmentOffset == 0x12340000ull);

        /*
         * Faixa NAO mapeada nao pode contar como acerto -- seria falso positivo
         * capaz de INVERTER o veredito do discriminador.
         */
        faultSpace.Ranges[2].State = BC250_GPUVA_RANGE_TEARDOWN;
        memset(&site, 0, sizeof(site));
        CHECK(Bc250GpuvaSpaceDescribeAddress(&faultSpace, page << 20, &site) == BC250_GPUVA_OK);
        CHECK(site.Found == 0u);
    }

    /*
     * DX.11 -- replay exato da Corrida 56. Tres faults, em VMIDs diferentes,
     * trouxeram AddrPagina=0x200040. <<12 cai no inicio de uma faixa viva de
     * 0x40000 bytes; <<20 excede o teto traduzivel de 34 bits. O token e' o
     * nome imutavel que precisa sobreviver ate o diagnostico post-mortem.
     */
    {
        static BC250_GPUVA_SPACE targetSpace;
        BC250_GPUVA_RANGE_RECORD target;
        BC250_GPUVA_ADDRESS_SITE site;
        uint64_t targetRangeId = 0ull;
        const uint64_t page = 0x200040ull;

        CHECK(Bc250GpuvaSpaceInitialize(&targetSpace, 34u) == BC250_GPUVA_OK);
        memset(&target, 0, sizeof(target));
        target.State = BC250_GPUVA_RANGE_MAPPED;
        target.BaseGpuVa = page << 12;
        target.Size = 0x40000ull;
        target.EndGpuVa = target.BaseGpuVa + target.Size;
        target.OwnerProcess = (BC250_MCDM_HANDLE)0x1C;
        target.Allocation = (BC250_MCDM_HANDLE)0xAE;
        target.AllocationToken = 0x52455300000000AEull;
        target.BackingSegmentId = 1u;
        target.BackingSegmentOffset = 0x5B00000ull;
        target.Permissions = BC250_PTE_READABLE;
        target.IsResident = 1u;
        CHECK(Bc250GpuvaSpaceInsertRange(&targetSpace, &target,
            &targetRangeId) == BC250_GPUVA_OK);

        memset(&site, 0, sizeof(site));
        CHECK(Bc250GpuvaSpaceDescribeAddress(&targetSpace, page << 12,
            &site) == BC250_GPUVA_OK);
        CHECK(site.Found == 1u);
        CHECK(site.OffsetInRange == 0ull);
        CHECK(site.Allocation == (BC250_MCDM_HANDLE)0xAE);
        CHECK(site.AllocationToken == 0x52455300000000AEull);
        CHECK(site.BackingSegmentId == 1u);
        CHECK(site.BackingSegmentOffset == 0x5B00000ull);
        CHECK((page << 20) >= BC250_GPUVA_TRANSLATABLE_LIMIT);
    }
    return 0;
}

/*
 * FPS.1 (28/09): propriedade da reconstrucao otimizada. Centenas de faixas de
 * tres donos e trezentas substituicoes aleatorias (mapear e desmapear), cada
 * uma conferida contra um modelo de referencia simples: ordenacao de
 * SortedIndices, nenhuma sobreposicao do mesmo dono, e ResolveAddress
 * devolvendo exatamente o offset do modelo nas bordas de cada intervalo.
 * Sem esta prova, "mesma semantica" seria afirmacao sem teste.
 */
typedef struct {
    uint32_t Owner;
    uint64_t Base;
    uint64_t End;
    uint64_t Offset;
} FPS1_MODEL_RANGE;

static uint32_t g_Fps1Seed = 0x1234567u;
static uint32_t Fps1Rand(void)
{
    g_Fps1Seed = g_Fps1Seed * 1103515245u + 12345u;
    return g_Fps1Seed >> 8;
}

static FPS1_MODEL_RANGE g_Fps1Model[1100];
static FPS1_MODEL_RANGE g_Fps1Next[1100];
static uint32_t g_Fps1Count;

static void Fps1Copy(FPS1_MODEL_RANGE *D, const FPS1_MODEL_RANGE *S)
{
    volatile uint64_t *d = (volatile uint64_t *)&D->Base;
    D->Owner = S->Owner;
    d[0] = S->Base; d[1] = S->End; d[2] = S->Offset;
}

static int Fps1ModelApply(uint32_t Owner, uint64_t Base, uint64_t End,
    uint32_t Map, uint64_t Offset)
{
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < g_Fps1Count; ++i) {
        const FPS1_MODEL_RANGE *r = &g_Fps1Model[i];
        if (r->Owner != Owner || r->End <= Base || End <= r->Base) {
            Fps1Copy(&g_Fps1Next[n++], r);
            continue;
        }
        if (r->Base < Base) {
            Fps1Copy(&g_Fps1Next[n], r);
            g_Fps1Next[n++].End = Base;
        }
        if (r->End > End) {
            Fps1Copy(&g_Fps1Next[n], r);
            g_Fps1Next[n].Offset = r->Offset + (End - r->Base);
            g_Fps1Next[n++].Base = End;
        }
    }
    if (Map) {
        g_Fps1Next[n].Owner = Owner; g_Fps1Next[n].Base = Base;
        g_Fps1Next[n].End = End; g_Fps1Next[n].Offset = Offset;
        ++n;
    }
    if (n > 1024u) return 0;
    for (i = 0u; i < n; ++i) Fps1Copy(&g_Fps1Model[i], &g_Fps1Next[i]);
    g_Fps1Count = n;
    return 1;
}

static int Bc250RunGpuvaRebuildPropertyTest(void)
{
    static BC250_GPUVA_SPACE space;
    static BC250_GPUVA_SPACE scratch;
    const BC250_MCDM_HANDLE owners[3] = {
        (BC250_MCDM_HANDLE)0xA1u, (BC250_MCDM_HANDLE)0xB2u, (BC250_MCDM_HANDLE)0xC3u };
    uint32_t op;
    CHECK(Bc250GpuvaSpaceInitialize(&space, 40u) == BC250_GPUVA_OK);
    g_Fps1Count = 0u;
    for (op = 0u; op < 700u; ++op) {
        BC250_GPUVA_RANGE_RECORD param;
        uint32_t owner = Fps1Rand() % 3u;
        uint64_t base = 0x100000000ull + (uint64_t)(Fps1Rand() % 4096u) * 0x1000u;
        uint64_t size = (uint64_t)(1u + Fps1Rand() % 16u) * 0x1000u;
        uint32_t map = (op < 400u) ? 1u : (Fps1Rand() % 3u != 0u);
        uint64_t offset = (uint64_t)(Fps1Rand() % 60000u) * 0x1000u;
        BC250_GPUVA_STATUS status;
        uint32_t i;
        memset(&param, 0, sizeof(param));
        param.BaseGpuVa = base;
        param.Size = size;
        param.OwnerProcess = owners[owner];
        param.BackingSegmentId = 1u;
        param.BackingSegmentOffset = offset;
        param.Permissions = BC250_PTE_READABLE | BC250_PTE_WRITEABLE;
        status = Bc250GpuvaSpaceReplaceRangeTransactional(&space, &scratch, &param, map);
        if (status == BC250_GPUVA_OK) {
            CHECK(Fps1ModelApply(owner, base, base + size, map, offset));
        } else {
            CHECK(status == BC250_GPUVA_NO_SPACE);
        }
        /* Ordenacao. */
        for (i = 1u; i < space.SortedCount; ++i) {
            CHECK(space.Ranges[space.SortedIndices[i - 1u]].BaseGpuVa <=
                  space.Ranges[space.SortedIndices[i]].BaseGpuVa);
        }
        /* Contagem de faixas vivas igual ao modelo. */
        CHECK(space.LiveCount == g_Fps1Count);
        /* Bordas de cada intervalo do modelo resolvem com o offset exato. */
        for (i = 0u; i < g_Fps1Count; ++i) {
            uint32_t seg = 0u;
            uint64_t off = 0u;
            const FPS1_MODEL_RANGE *r = &g_Fps1Model[i];
            CHECK(Bc250GpuvaSpaceResolveAddress(&space, owners[r->Owner], r->Base,
                BC250_PTE_READABLE, &seg, &off) == BC250_GPUVA_OK);
            CHECK(off == r->Offset);
            CHECK(Bc250GpuvaSpaceResolveAddress(&space, owners[r->Owner], r->End - 0x1000u,
                BC250_PTE_READABLE, &seg, &off) == BC250_GPUVA_OK);
            CHECK(off == r->Offset + (r->End - 0x1000u - r->Base));
        }
    }
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    unsigned long code = (unsigned long)Bc250RunGpuvaSpaceContractTest();
    if (code == 0ul) code = (unsigned long)Bc250RunGpuvaRebuildPropertyTest();
    ExitProcess(code);
}
#else
int main(void)
{
    int code = Bc250RunGpuvaSpaceContractTest();
    if (code == 0) code = Bc250RunGpuvaRebuildPropertyTest();
    return code;
}
#endif
