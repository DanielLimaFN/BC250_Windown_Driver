/*
 * gfx_viewport_scissor_test.c - Teste de Contrato de Viewport, Scissor e Contexto PM4 (B.3.1)
 *
 * Valida os calculos matematicos de escala e deslocamento NDC -> Screen,
 * a rejeicao fail-closed de entradas invalidas e a emissao dos 4 pacotes SET_CONTEXT_REG (20 DW).
 */

#include "../core/bc250_gfx_viewport_scissor.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
int _fltused = 0x9875;

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

static float DwordToFloat(uint32_t dw)
{
    union {
        uint32_t u;
        float f;
    } cvt;
    cvt.u = dw;
    return cvt.f;
}

static int TestViewportScissorMath(void)
{
    BC250_GFX_VIEWPORT_DIMS dims;
    BC250_GFX_SCISSOR_RECT scissor;
    BC250_GFX_VIEWPORT_SCISSOR_PACKET packet;
    BC250_GFX_VPORT_STATUS st;

    memset(&dims, 0, sizeof(dims));
    memset(&scissor, 0, sizeof(scissor));
    memset(&packet, 0, sizeof(packet));

    dims.X = 0.0f;
    dims.Y = 0.0f;
    dims.Width = 1920.0f;
    dims.Height = 1080.0f;
    dims.MinDepth = 0.0f;
    dims.MaxDepth = 1.0f;

    scissor.Left = 0u;
    scissor.Top = 0u;
    scissor.Right = 1920u;
    scissor.Bottom = 1080u;

    st = Bc250GfxComputeViewportScissor(&dims, &scissor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);

    /* Verificacao das escalas e offsets matematicos */
    CHECK(DwordToFloat(packet.VportXScale) == 960.0f);
    CHECK(DwordToFloat(packet.VportXOffset) == 960.0f);
    CHECK(DwordToFloat(packet.VportYScale) == -540.0f);
    CHECK(DwordToFloat(packet.VportYOffset) == 540.0f);
    CHECK(DwordToFloat(packet.VportZScale) == 1.0f);
    CHECK(DwordToFloat(packet.VportZOffset) == 0.0f);

    /* Registradores de controle */
    CHECK(packet.SuScModeCntl == 0x00000000u);
    CHECK(packet.VteCntl == 0x0000043Fu);

    /* Scissor de janela */
    CHECK(packet.WindowScissorTl == (0u | 0x80000000u));
    CHECK(packet.WindowScissorBr == (1920u | (1080u << 16)));

    /* Scissor de viewport 0 */
    CHECK(packet.VportScissor0Tl == (0u | 0x80000000u));
    CHECK(packet.VportScissor0Br == (1920u | (1080u << 16)));

    return 1;
}

static int TestViewportScissorFailClosed(void)
{
    BC250_GFX_VIEWPORT_DIMS dims;
    BC250_GFX_SCISSOR_RECT scissor;
    BC250_GFX_VIEWPORT_SCISSOR_PACKET packet;
    uint32_t nanBits = 0x7FC00000u;
    uint32_t infBits = 0x7F800000u;

    memset(&dims, 0, sizeof(dims));
    memset(&scissor, 0, sizeof(scissor));
    memset(&packet, 0, sizeof(packet));

    dims.Width = 1920.0f;
    dims.Height = 1080.0f;
    dims.MaxDepth = 1.0f;
    scissor.Right = 1920u;
    scissor.Bottom = 1080u;

    /* 1. Argumentos nulos */
    CHECK(Bc250GfxComputeViewportScissor(NULL, &scissor, &packet) == BC250_GFX_VPORT_INVALID_ARGUMENT);
    CHECK(Bc250GfxComputeViewportScissor(&dims, NULL, &packet) == BC250_GFX_VPORT_INVALID_ARGUMENT);
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, NULL) == BC250_GFX_VPORT_INVALID_ARGUMENT);

    /* 2. Rejeicao de NaN */
    dims.Width = DwordToFloat(nanBits);
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_INVALID_ARGUMENT);
    dims.Width = 1920.0f;

    /* 3. Rejeicao de Infinito */
    dims.Height = DwordToFloat(infBits);
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_INVALID_ARGUMENT);
    dims.Height = 1080.0f;

    /* 4. Dimensoes nulas ou negativas */
    dims.Width = 0.0f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.Width = -100.0f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.Width = 1920.0f;

    dims.Height = -50.0f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.Height = 1080.0f;

    /* 5. Dimensoes alem do maximo de hardware (16384) */
    dims.Width = 16385.0f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.Width = 1920.0f;

    /* 6. Faixa de profundidade invalida */
    dims.MinDepth = -0.1f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.MinDepth = 0.0f;

    dims.MaxDepth = 1.1f;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.MaxDepth = 1.0f;

    dims.MinDepth = 0.8f;
    dims.MaxDepth = 0.2f; /* Min > Max */
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    dims.MinDepth = 0.0f;
    dims.MaxDepth = 1.0f;

    /* 7. Retangulo de Scissor invertido */
    scissor.Left = 500u;
    scissor.Right = 100u;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);
    scissor.Left = 0u;
    scissor.Right = 1920u;

    scissor.Top = 800u;
    scissor.Bottom = 200u;
    CHECK(Bc250GfxComputeViewportScissor(&dims, &scissor, &packet) == BC250_GFX_VPORT_OUT_OF_BOUNDS);

    return 1;
}

static int TestViewportScissorEmitAndVerify(void)
{
    uint32_t ring[64];
    uint32_t cursor = 0u;
    BC250_GFX_VIEWPORT_DIMS dims;
    BC250_GFX_SCISSOR_RECT scissor;
    BC250_GFX_VIEWPORT_SCISSOR_PACKET packet;
    BC250_GFX_VPORT_STATUS st;

    memset(ring, 0xCC, sizeof(ring));
    dims.X = 100.0f;
    dims.Y = 50.0f;
    dims.Width = 1280.0f;
    dims.Height = 720.0f;
    dims.MinDepth = 0.0f;
    dims.MaxDepth = 1.0f;
    scissor.Left = 100u;
    scissor.Top = 50u;
    scissor.Right = 1380u;
    scissor.Bottom = 770u;

    st = Bc250GfxComputeViewportScissor(&dims, &scissor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);

    /* Emissao no anel */
    st = Bc250GfxEmitViewportScissorStream(ring, 64u, &cursor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);
    CHECK(cursor == BC250_GFX_VPORT_STREAM_TOTAL_DWORDS); /* exatamente 20 dwords */

    /* Verificacao de integridade do stream */
    st = Bc250GfxVerifyViewportScissorStream(ring, 64u, cursor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);

    /* Injeção de corrupção: altera um dword e confere detecção */
    ring[cursor - 3u] ^= 0x00000001u;
    st = Bc250GfxVerifyViewportScissorStream(ring, 64u, cursor, &packet);
    CHECK(st == BC250_GFX_VPORT_STREAM_MISMATCH);

    return 1;
}

static int TestViewportScissorRingWrap(void)
{
    uint32_t ring[32];
    uint32_t cursor = 24u; /* 24 + 20 dwords = 44 (ultrapassa anel de 32) */
    BC250_GFX_VIEWPORT_DIMS dims;
    BC250_GFX_SCISSOR_RECT scissor;
    BC250_GFX_VIEWPORT_SCISSOR_PACKET packet;
    BC250_GFX_VPORT_STATUS st;

    memset(ring, 0xAA, sizeof(ring));
    dims.X = 0.0f;
    dims.Y = 0.0f;
    dims.Width = 2560.0f;
    dims.Height = 1080.0f;
    dims.MinDepth = 0.0f;
    dims.MaxDepth = 1.0f;
    scissor.Left = 0u;
    scissor.Top = 0u;
    scissor.Right = 2560u;
    scissor.Bottom = 1080u;

    st = Bc250GfxComputeViewportScissor(&dims, &scissor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);

    st = Bc250GfxEmitViewportScissorStream(ring, 32u, &cursor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);
    CHECK(cursor == 44u);

    /* Verificacao com cursor circular */
    st = Bc250GfxVerifyViewportScissorStream(ring, 32u, cursor, &packet);
    CHECK(st == BC250_GFX_VPORT_OK);

    /* Status Name */
    CHECK(Bc250GfxVportStatusName(BC250_GFX_VPORT_OK) != NULL);
    CHECK(Bc250GfxVportStatusName(BC250_GFX_VPORT_INVALID_ARGUMENT) != NULL);
    CHECK(Bc250GfxVportStatusName(BC250_GFX_VPORT_OUT_OF_BOUNDS) != NULL);
    CHECK(Bc250GfxVportStatusName(BC250_GFX_VPORT_RING_FULL) != NULL);
    CHECK(Bc250GfxVportStatusName(BC250_GFX_VPORT_STREAM_MISMATCH) != NULL);

    return 1;
}

static int Bc250RunGfxViewportScissorTest(void)
{
    int rc;
    rc = TestViewportScissorMath();
    if (rc != 1) return rc;
    rc = TestViewportScissorFailClosed();
    if (rc != 1) return rc;
    rc = TestViewportScissorEmitAndVerify();
    if (rc != 1) return rc;
    rc = TestViewportScissorRingWrap();
    if (rc != 1) return rc;
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    int rc = Bc250RunGfxViewportScissorTest();
    ExitProcess((rc == 1) ? 0ul : (unsigned long)rc);
}
#else
int main(void)
{
    int rc = Bc250RunGfxViewportScissorTest();
    return (rc == 1) ? 0 : rc;
}
#endif
