/*
 * gfx_primitive_assembler_test.c - Teste de Contrato de Primitive Assembler e Topologia PM4 (B.3.2)
 *
 * Valida a configuracao de topologia de triângulos (DI_PT_TRILIST = 0x04) e contagem de instancias,
 * a rejeicao fail-closed de valores invalidos e a emissao do stream PM4 alinhado (8 DW)
 * com suporte nativo a wrap-around circular.
 */

#include "../core/bc250_gfx_primitive_assembler.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    unsigned char *bytes = (unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <string.h>
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#endif

static int TestPrimitiveAssemblerTriList(void)
{
    uint32_t ring[64];
    uint32_t cursor = 0u;
    BC250_GFX_PRIMITIVE_PACKET packet;
    BC250_GFX_PRIM_STATUS st;

    memset(ring, 0xCC, sizeof(ring));
    memset(&packet, 0, sizeof(packet));

    st = Bc250GfxComputePrimitiveAssembler(BC250_GFX_PRIM_TRILIST, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);
    CHECK(packet.Topology == 0x04u);
    CHECK(packet.NumInstances == 1u);

    /* Emissao no anel */
    st = Bc250GfxEmitPrimitiveAssemblerStream(ring, 64u, &cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);
    CHECK(cursor == BC250_GFX_PRIM_STREAM_TOTAL_DWORDS); /* exatamente 8 dwords */

    /* Verificacao de integridade do stream */
    st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 64u, cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);

    /* Injeção de corrupção: altera o valor de topologia e confere detecção */
    ring[2] = 0x00000000u;
    st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 64u, cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_STREAM_MISMATCH);

    return 1;
}

static int TestPrimitiveAssemblerOtherTopologies(void)
{
    static const uint32_t topologies[] = {
        BC250_GFX_PRIM_POINTLIST,
        BC250_GFX_PRIM_LINELIST,
        BC250_GFX_PRIM_LINESTRIP,
        BC250_GFX_PRIM_TRISTRIP,
        BC250_GFX_PRIM_RECTLIST
    };
    uint32_t i;

    for (i = 0; i < sizeof(topologies) / sizeof(topologies[0]); ++i) {
        uint32_t ring[32];
        uint32_t cursor = 0u;
        BC250_GFX_PRIMITIVE_PACKET packet;
        BC250_GFX_PRIM_STATUS st;

        memset(ring, 0xAA, sizeof(ring));
        st = Bc250GfxComputePrimitiveAssembler(topologies[i], 2u + i, &packet);
        CHECK(st == BC250_GFX_PRIM_OK);
        CHECK(packet.Topology == topologies[i]);
        CHECK(packet.NumInstances == 2u + i);

        st = Bc250GfxEmitPrimitiveAssemblerStream(ring, 32u, &cursor, &packet);
        CHECK(st == BC250_GFX_PRIM_OK);
        CHECK(cursor == 8u);

        st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 32u, cursor, &packet);
        CHECK(st == BC250_GFX_PRIM_OK);
    }

    return 1;
}

static int TestPrimitiveAssemblerFailClosed(void)
{
    uint32_t ring[16];
    uint32_t cursor = 0u;
    BC250_GFX_PRIMITIVE_PACKET packet;
    BC250_GFX_PRIM_STATUS st;

    /* Ponteiros nulos */
    st = Bc250GfxComputePrimitiveAssembler(BC250_GFX_PRIM_TRILIST, 1u, NULL);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxEmitPrimitiveAssemblerStream(NULL, 16u, &cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxEmitPrimitiveAssemblerStream(ring, 16u, NULL, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxEmitPrimitiveAssemblerStream(ring, 16u, &cursor, NULL);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxVerifyPrimitiveAssemblerStream(NULL, 16u, 8u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 16u, 8u, NULL);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 15u, 8u, &packet); /* nao potencia de 2 */
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    /* Instancias = 0 */
    st = Bc250GfxComputePrimitiveAssembler(BC250_GFX_PRIM_TRILIST, 0u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    /* Topologias invalidas */
    st = Bc250GfxComputePrimitiveAssembler(0x00u, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxComputePrimitiveAssembler(0x06u, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxComputePrimitiveAssembler(0x10u, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    st = Bc250GfxComputePrimitiveAssembler(0xFFFFFFFFu, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_INVALID_ARGUMENT);

    return 1;
}

static int TestPrimitiveAssemblerRingWrap(void)
{
    uint32_t ring[16];
    uint32_t cursor = 12u; /* 12 + 8 dwords = 20 (ultrapassa anel de 16) */
    BC250_GFX_PRIMITIVE_PACKET packet;
    BC250_GFX_PRIM_STATUS st;

    memset(ring, 0xEE, sizeof(ring));
    st = Bc250GfxComputePrimitiveAssembler(BC250_GFX_PRIM_TRILIST, 1u, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);

    st = Bc250GfxEmitPrimitiveAssemblerStream(ring, 16u, &cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);
    CHECK(cursor == 20u);

    /* Verificacao com cursor circular */
    st = Bc250GfxVerifyPrimitiveAssemblerStream(ring, 16u, cursor, &packet);
    CHECK(st == BC250_GFX_PRIM_OK);

    /* Nomes dos status */
    CHECK(Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_OK) != NULL);
    CHECK(Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_INVALID_ARGUMENT) != NULL);
    CHECK(Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_RING_FULL) != NULL);
    CHECK(Bc250GfxPrimitiveStatusName(BC250_GFX_PRIM_STREAM_MISMATCH) != NULL);

    return 1;
}

static int Bc250RunGfxPrimitiveAssemblerTest(void)
{
    int rc;
    rc = TestPrimitiveAssemblerTriList();
    if (rc != 1) return rc;
    rc = TestPrimitiveAssemblerOtherTopologies();
    if (rc != 1) return rc;
    rc = TestPrimitiveAssemblerFailClosed();
    if (rc != 1) return rc;
    rc = TestPrimitiveAssemblerRingWrap();
    if (rc != 1) return rc;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    int rc = Bc250RunGfxPrimitiveAssemblerTest();
    ExitProcess((rc == 1) ? 0ul : (unsigned long)rc);
}
#else
int main(void)
{
    int rc = Bc250RunGfxPrimitiveAssemblerTest();
    return (rc == 1) ? 0 : rc;
}
#endif
