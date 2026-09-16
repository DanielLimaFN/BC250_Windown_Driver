/*
 * gfx_draw_initiator_test.c - Teste de Contrato de Draw Initiator e Disparo de Desenho PM4 (B.3.3)
 *
 * Valida o disparo de desenho de geometria sem indexacao (DRAW_INDEX_AUTO = 0x2D),
 * a configuracao de contagem de vertices e modos de inicializacao,
 * a rejeicao fail-closed de valores invalidos e a emissao do stream PM4 alinhado (4 DW / 16 B)
 * com suporte nativo a wrap-around circular.
 */

#include "../core/bc250_gfx_draw_initiator.h"

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

static int TestDrawInitiatorAutoIndex(void)
{
    uint32_t ring[32];
    uint32_t cursor = 0u;
    BC250_GFX_DRAW_PACKET packet;
    BC250_GFX_DRAW_STATUS st;

    memset(ring, 0xCC, sizeof(ring));
    memset(&packet, 0, sizeof(packet));

    st = Bc250GfxComputeDrawInitiator(3u, BC250_GFX_DRAW_INITIATOR_AUTO_INDEX, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);
    CHECK(packet.VertexCount == 3u);
    CHECK(packet.DrawInitiator == 0x00000002u);

    /* Emissao no anel */
    st = Bc250GfxEmitDrawInitiatorStream(ring, 32u, &cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);
    CHECK(cursor == BC250_GFX_DRAW_STREAM_TOTAL_DWORDS); /* exatamente 4 dwords / 16 bytes */

    /* Verificacao de integridade do stream */
    st = Bc250GfxVerifyDrawInitiatorStream(ring, 32u, cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);

    /* Injeção de corrupção: altera a contagem de vertices e confere detecção */
    ring[1] = 0x00000000u;
    st = Bc250GfxVerifyDrawInitiatorStream(ring, 32u, cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_STREAM_MISMATCH);

    return 1;
}

static int TestDrawInitiatorLegacyTriangleList(void)
{
    uint32_t ring[32];
    uint32_t cursor = 0u;
    BC250_GFX_DRAW_PACKET packet;
    BC250_GFX_DRAW_STATUS st;

    memset(ring, 0xAA, sizeof(ring));
    memset(&packet, 0, sizeof(packet));

    st = Bc250GfxComputeDrawInitiator(3u, BC250_GFX_DRAW_INITIATOR_TRIANGLELIST, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);
    CHECK(packet.VertexCount == 3u);
    CHECK(packet.DrawInitiator == 0x00000004u);

    st = Bc250GfxEmitDrawInitiatorStream(ring, 32u, &cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);
    CHECK(cursor == 4u);

    st = Bc250GfxVerifyDrawInitiatorStream(ring, 32u, cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);

    return 1;
}

static int TestDrawInitiatorOtherModes(void)
{
    static const uint32_t modes[] = {
        BC250_GFX_DRAW_INITIATOR_POINTLIST,
        BC250_GFX_DRAW_INITIATOR_RECTLIST
    };
    static const uint32_t counts[] = { 1u, 4u };
    uint32_t i;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        uint32_t ring[16];
        uint32_t cursor = 0u;
        BC250_GFX_DRAW_PACKET packet;
        BC250_GFX_DRAW_STATUS st;

        memset(ring, 0x55, sizeof(ring));
        st = Bc250GfxComputeDrawInitiator(counts[i], modes[i], &packet);
        CHECK(st == BC250_GFX_DRAW_OK);
        CHECK(packet.VertexCount == counts[i]);
        CHECK(packet.DrawInitiator == modes[i]);

        st = Bc250GfxEmitDrawInitiatorStream(ring, 16u, &cursor, &packet);
        CHECK(st == BC250_GFX_DRAW_OK);
        CHECK(cursor == 4u);

        st = Bc250GfxVerifyDrawInitiatorStream(ring, 16u, cursor, &packet);
        CHECK(st == BC250_GFX_DRAW_OK);
    }

    return 1;
}

static int TestDrawInitiatorFailClosed(void)
{
    uint32_t ring[16];
    uint32_t cursor = 0u;
    BC250_GFX_DRAW_PACKET packet;
    BC250_GFX_DRAW_STATUS st;

    /* Ponteiros nulos */
    st = Bc250GfxComputeDrawInitiator(3u, BC250_GFX_DRAW_INITIATOR_AUTO_INDEX, NULL);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxEmitDrawInitiatorStream(NULL, 16u, &cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxEmitDrawInitiatorStream(ring, 16u, NULL, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxEmitDrawInitiatorStream(ring, 16u, &cursor, NULL);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxVerifyDrawInitiatorStream(NULL, 16u, 4u, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxVerifyDrawInitiatorStream(ring, 16u, 4u, NULL);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxVerifyDrawInitiatorStream(ring, 15u, 4u, &packet); /* nao potencia de 2 */
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    /* Vertices = 0 */
    st = Bc250GfxComputeDrawInitiator(0u, BC250_GFX_DRAW_INITIATOR_AUTO_INDEX, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    /* Modos de DrawInitiator invalidos */
    st = Bc250GfxComputeDrawInitiator(3u, 0x00u, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxComputeDrawInitiator(3u, 0x03u, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxComputeDrawInitiator(3u, 0x05u, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    st = Bc250GfxComputeDrawInitiator(3u, 0xFFFFFFFFu, &packet);
    CHECK(st == BC250_GFX_DRAW_INVALID_ARGUMENT);

    return 1;
}

static int TestDrawInitiatorRingWrap(void)
{
    uint32_t ring[16];
    uint32_t cursor = 14u; /* 14 + 4 dwords = 18 (ultrapassa anel de 16) */
    BC250_GFX_DRAW_PACKET packet;
    BC250_GFX_DRAW_STATUS st;

    memset(ring, 0xEE, sizeof(ring));
    st = Bc250GfxComputeDrawInitiator(3u, BC250_GFX_DRAW_INITIATOR_AUTO_INDEX, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);

    st = Bc250GfxEmitDrawInitiatorStream(ring, 16u, &cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);
    CHECK(cursor == 18u);

    /* Verificacao com cursor circular */
    st = Bc250GfxVerifyDrawInitiatorStream(ring, 16u, cursor, &packet);
    CHECK(st == BC250_GFX_DRAW_OK);

    /* Nomes dos status */
    CHECK(Bc250GfxDrawStatusName(BC250_GFX_DRAW_OK) != NULL);
    CHECK(Bc250GfxDrawStatusName(BC250_GFX_DRAW_INVALID_ARGUMENT) != NULL);
    CHECK(Bc250GfxDrawStatusName(BC250_GFX_DRAW_RING_FULL) != NULL);
    CHECK(Bc250GfxDrawStatusName(BC250_GFX_DRAW_STREAM_MISMATCH) != NULL);

    return 1;
}

static int Bc250RunGfxDrawInitiatorTest(void)
{
    int rc;
    rc = TestDrawInitiatorAutoIndex();
    if (rc != 1) return rc;
    rc = TestDrawInitiatorLegacyTriangleList();
    if (rc != 1) return rc;
    rc = TestDrawInitiatorOtherModes();
    if (rc != 1) return rc;
    rc = TestDrawInitiatorFailClosed();
    if (rc != 1) return rc;
    rc = TestDrawInitiatorRingWrap();
    if (rc != 1) return rc;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    int rc = Bc250RunGfxDrawInitiatorTest();
    ExitProcess((rc == 1) ? 0ul : (unsigned long)rc);
}
#else
int main(void)
{
    int rc = Bc250RunGfxDrawInitiatorTest();
    return (rc == 1) ? 0 : rc;
}
#endif
