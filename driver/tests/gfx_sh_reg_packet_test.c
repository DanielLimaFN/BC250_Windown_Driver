#include "../core/bc250_gfx_sh_reg_packet.h"

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

static int Bc250RunGfxShRegPacketTest(void)
{
    uint32_t ring[64];
    uint32_t smallRing[8];
    uint32_t cursor = 0u;
    BC250_GFX_SHADER_REGS vsRegs;
    BC250_GFX_SHADER_REGS psRegs;
    BC250_GFX_SHADER_REGS badRegs;
    BC250_GFX_SH_REG_EMISSION_EVIDENCE ev;
    uint32_t i;

    for (i = 0; i < 64u; ++i) ring[i] = 0xCCCCCCCCu;
    for (i = 0; i < 8u; ++i) smallRing[i] = 0xCCCCCCCCu;

    /* Registradores de teste configurados com valores tipicos */
    vsRegs.PgmLo = 0x0000A8C0u; /* Endereco 0x00A8C000 >> 8 */
    vsRegs.PgmHi = 0x00000000u;
    vsRegs.Rsrc1 = 0x00AF0000u; /* FLOAT_MODE=0xF0, DX10_CLAMP, IEEE_MODE */
    vsRegs.Rsrc2 = 0x00000000u;

    psRegs.PgmLo = 0x0000A8C2u; /* Endereco 0x00A8C200 >> 8 */
    psRegs.PgmHi = 0x00000000u;
    psRegs.Rsrc1 = 0x02AF0000u; /* PS com MEM_ORDERED no bit 25 */
    psRegs.Rsrc2 = 0x00000000u;

    /* 1. Teste de Emissao Pipeline Completa (Caminho Feliz: 12 dwords) */
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_OK);
    CHECK(cursor == BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS);
    CHECK(ev.VsEmittedDwords == BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE);
    CHECK(ev.PsEmittedDwords == BC250_GFX_PM4_SHADER_DWORDS_PER_STAGE);
    CHECK(ev.TotalEmittedDwords == BC250_GFX_PM4_PIPELINE_SHADER_TOTAL_DWORDS);
    CHECK(ev.VsHeaderEmitted == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK(ev.PsHeaderEmitted == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK(ev.EmissionSuccessful == 1u);

    /* Verificacao direta dos 6 dwords do VS */
    CHECK(ring[0] == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK((ring[0] & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) == 0u);
    CHECK(ring[1] == BC250_GFX_SH_PGM_LO_VS);
    CHECK(ring[2] == vsRegs.PgmLo);
    CHECK(ring[3] == vsRegs.PgmHi);
    CHECK(ring[4] == vsRegs.Rsrc1);
    CHECK(ring[5] == vsRegs.Rsrc2);

    /* Verificacao direta dos 6 dwords do PS */
    CHECK(ring[6] == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK((ring[6] & BC250_GFX_SH_REG_COMPUTE_BIT_FORBIDDEN) == 0u);
    CHECK(ring[7] == BC250_GFX_SH_PGM_LO_PS);
    CHECK(ring[8] == psRegs.PgmLo);
    CHECK(ring[9] == psRegs.PgmHi);
    CHECK(ring[10] == psRegs.Rsrc1);
    CHECK(ring[11] == psRegs.Rsrc2);

    /* 2. Verificacao de Stream Integral */
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_OK);

    /* 3. Teste de Emissao Individual de Estagios */
    cursor = 20u;
    CHECK(Bc250GfxShRegEmitVs(ring, 64u, &cursor, &vsRegs) == BC250_GFX_SH_REG_OK);
    CHECK(cursor == 26u);
    CHECK(ring[20] == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK(ring[21] == BC250_GFX_SH_PGM_LO_VS);
    CHECK(ring[22] == vsRegs.PgmLo);

    cursor = 30u;
    CHECK(Bc250GfxShRegEmitPs(ring, 64u, &cursor, &psRegs) == BC250_GFX_SH_REG_OK);
    CHECK(cursor == 36u);
    CHECK(ring[30] == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
    CHECK(ring[31] == BC250_GFX_SH_PGM_LO_PS);
    CHECK(ring[32] == psRegs.PgmLo);

    /* 4. Caso Negativo: Ring Cheio / Espaco Insuficiente */
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(smallRing, 8u, &cursor, &vsRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_RING_FULL);
    CHECK(cursor == 0u); /* Nao move cursor na falha */

    {
        uint32_t tinyRing[4] = {0};
        CHECK(Bc250GfxShRegEmitVs(tinyRing, 4u, &cursor, &vsRegs) ==
              BC250_GFX_SH_REG_RING_FULL);
        CHECK(cursor == 0u);
    }

    /* 4b. Verificacao de Wrap-around Circular (Reuso de Bc250Pm4EmitSetShReg) */
    {
        uint32_t wrapRing[64];
        for (i = 0; i < 64u; ++i) wrapRing[i] = 0xCCCCCCCCu;
        cursor = 60u; /* Emite 6 dwords comecando em 60 num ring de 64: da a volta para indices 0..1 */
        CHECK(Bc250GfxShRegEmitVs(wrapRing, 64u, &cursor, &vsRegs) == BC250_GFX_SH_REG_OK);
        CHECK(cursor == 66u);
        CHECK(wrapRing[60] == BC250_GFX_PM4_SET_SH_REG_HEADER_4DW);
        CHECK(wrapRing[61] == BC250_GFX_SH_PGM_LO_VS);
        CHECK(wrapRing[62] == vsRegs.PgmLo);
        CHECK(wrapRing[63] == vsRegs.PgmHi);
        CHECK(wrapRing[0] == vsRegs.Rsrc1);
        CHECK(wrapRing[1] == vsRegs.Rsrc2);
    }

    /* 5. Caso de Registradores: PgmLo==0 e' Recusado, Rsrc1==0 e' Aceito */
    badRegs = vsRegs;
    badRegs.PgmLo = 0u;
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &badRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_INVALID_REGS);
    CHECK(Bc250GfxShRegEmitVs(ring, 64u, &cursor, &badRegs) ==
          BC250_GFX_SH_REG_INVALID_REGS);

    /* Rsrc1 == 0 e' permitido arquiteturalmente para shaders minimos */
    badRegs = vsRegs;
    badRegs.Rsrc1 = 0u;
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &badRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_OK);
    CHECK(ring[4] == 0u);

    badRegs = psRegs;
    badRegs.PgmLo = 0u;
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &badRegs, &ev) ==
          BC250_GFX_SH_REG_INVALID_REGS);
    CHECK(Bc250GfxShRegEmitPs(ring, 64u, &cursor, &badRegs) ==
          BC250_GFX_SH_REG_INVALID_REGS);

    /* 6. Caso Negativo: Argumentos Nulos */
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(NULL, 64u, &cursor, &vsRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_BAD_ARGUMENT);
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, NULL, &vsRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_BAD_ARGUMENT);
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, NULL, &psRegs, &ev) ==
          BC250_GFX_SH_REG_BAD_ARGUMENT);
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, NULL, &ev) ==
          BC250_GFX_SH_REG_BAD_ARGUMENT);
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &psRegs, NULL) ==
          BC250_GFX_SH_REG_BAD_ARGUMENT);

    /* 7. Caso Negativo: Deteccao de Corrupcao em Stream */
    /* Reemite a sequencia valida no ring antes dos testes de corrupcao */
    cursor = 0u;
    CHECK(Bc250GfxShRegEmitPipeline(ring, 64u, &cursor, &vsRegs, &psRegs, &ev) ==
          BC250_GFX_SH_REG_OK);

    /* DwordCount invalido */
    CHECK(Bc250GfxShRegVerifyStream(ring, 11u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);

    /* Corrupcao de header com bit compute */
    ring[0] = 0xC0047602u;
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);
    ring[0] = BC250_GFX_PM4_SET_SH_REG_HEADER_4DW;

    /* Corrupcao de offset de registrador VS */
    ring[1] = 0x00000040u;
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);
    ring[1] = BC250_GFX_SH_PGM_LO_VS;

    /* Corrupcao de valor PgmLo do VS */
    ring[2] = 0xDEADBEEFu;
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);
    ring[2] = vsRegs.PgmLo;

    /* Corrupcao de offset de registrador PS */
    ring[7] = 0x00000000u;
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);
    ring[7] = BC250_GFX_SH_PGM_LO_PS;

    /* Corrupcao de valor Rsrc1 do PS */
    ring[10] = 0x12345678u;
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_STREAM_MISMATCH);
    ring[10] = psRegs.Rsrc1;

    /* Revalidacao de retorno integro */
    CHECK(Bc250GfxShRegVerifyStream(ring, 12u, &vsRegs, &psRegs) ==
          BC250_GFX_SH_REG_OK);

    /* 8. Status Name */
    CHECK(Bc250GfxShRegStatusName(BC250_GFX_SH_REG_OK) != NULL);
    CHECK(Bc250GfxShRegStatusName(BC250_GFX_SH_REG_RING_FULL) != NULL);
    CHECK(Bc250GfxShRegStatusName(BC250_GFX_SH_REG_STREAM_MISMATCH) != NULL);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    int rc = Bc250RunGfxShRegPacketTest();
    ExitProcess((rc == 1) ? 0ul : (unsigned long)rc);
}
#else
int main(void)
{
    int rc = Bc250RunGfxShRegPacketTest();
    return (rc == 1) ? 0 : rc;
}
#endif
