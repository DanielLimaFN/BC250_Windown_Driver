#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_paging_proof_contract.h"

#include <string.h>

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

#define TEST_RING_DWORDS 128u

static uint32_t g_ring[TEST_RING_DWORDS];

static int Bc250RunSdmaPacketBuilderTest(void)
{
    uint32_t cursor = 0;
    uint32_t rptr = 0;
    uint32_t wptr = 0;
    uint32_t available = 0;
    uint32_t i;

    memset(g_ring, 0, sizeof(g_ring));

    /* 1. Test COPY_LINEAR emission */
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x0000000001000000ull, 0x0000000002000000ull, 4096u) == BC250_SDMA_OK);
    CHECK(cursor == 7u);
    CHECK(g_ring[0] == 0x00000001u); /* Opcode 1, Subop 0 */
    CHECK(g_ring[1] == 4095u);       /* ByteCount - 1 */
    CHECK(g_ring[2] == 0x00000000u); /* Endian/reserved */
    CHECK(g_ring[3] == 0x01000000u); /* Src Low */
    CHECK(g_ring[4] == 0x00000000u); /* Src High */
    CHECK(g_ring[5] == 0x02000000u); /* Dst Low */
    CHECK(g_ring[6] == 0x00000000u); /* Dst High */

    /* 1b. Caso medido no DWM em 13/09: 1280x1040, pitches 5120 -> 10240. */
    cursor = 0u;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitCopyLinearSubWindow32(g_ring, TEST_RING_DWORDS, &cursor,
        0x000000F401610000ull, 5120u,
        0x000000F4000F0000ull, 10240u,
        0u, 0u, 1280u, 1040u) == BC250_SDMA_OK);
    CHECK(cursor == BC250_SDMA_DWORDS_COPY_LINEAR_SUB_WIND);
    CHECK(g_ring[0] == 0x40000401u); /* OP=1, SUBOP=4, elementsize=DWORD */
    CHECK(g_ring[1] == 0x01610000u && g_ring[2] == 0x000000F4u);
    CHECK(g_ring[3] == 0u);
    CHECK(g_ring[4] == ((1280u - 1u) << 13));
    CHECK(g_ring[5] == (1280u * 1040u) - 1u);
    CHECK(g_ring[6] == 0x000F0000u && g_ring[7] == 0x000000F4u);
    CHECK(g_ring[8] == 0u);
    CHECK(g_ring[9] == ((2560u - 1u) << 13));
    CHECK(g_ring[10] == (2560u * 1040u) - 1u);
    CHECK(g_ring[11] == ((1280u - 1u) | ((1040u - 1u) << 16)));
    CHECK(g_ring[12] == 0u);
    cursor = 0u;
    CHECK(Bc250SdmaEmitCopyLinearSubWindow32(g_ring, TEST_RING_DWORDS, &cursor,
        0x1000ull, 5119u, 0x2000ull, 10240u, 0u, 0u, 1280u, 1040u) ==
        BC250_SDMA_MISALIGNED);

    /* Restaura o preambulo linear usado pelos testes encadeados abaixo. */
    cursor = 0u;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitCopyLinear(g_ring, TEST_RING_DWORDS, &cursor,
        0x0000000001000000ull, 0x0000000002000000ull, 4096u) ==
        BC250_SDMA_OK);

    /* 2. Test FENCE emission */
    CHECK(Bc250SdmaEmitFence(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x000000000FF00000ull, 0xBC250001u) == BC250_SDMA_OK);
    CHECK(cursor == 11u);
    CHECK(g_ring[7] == 0x00030005u); /* Opcode 5, MTYPE UC */
    CHECK(g_ring[8] == 0x0FF00000u); /* Fence Low */
    CHECK(g_ring[9] == 0x00000000u); /* Fence High */
    CHECK(g_ring[10] == 0xBC250001u); /* Sequence */
    CHECK(Bc250SdmaEmitGcr(
        g_ring, TEST_RING_DWORDS, &cursor) == BC250_SDMA_OK);
    CHECK(cursor == 16u);
    CHECK(g_ring[11] == 17u);
    CHECK(g_ring[12] == 0u);
    CHECK(g_ring[13] == 0xC3A10000u);
    CHECK(g_ring[14] == 0u && g_ring[15] == 0u);

    /* Test FENCE64 emission (SDMA 5.0 monitored fence: WRITE64 + MTYPE_UC) */
    {
        uint32_t f64Cursor = 0;
        uint32_t f64Ring[16] = {0};
        CHECK(Bc250SdmaEmitFence64(
            f64Ring, 16u, &f64Cursor,
            0x0000000450001000ull, 0x12345678AABBCCDDull) == BC250_SDMA_OK);
        CHECK(f64Cursor == 5u);
        CHECK(f64Ring[0] == 0x80030005u); /* Opcode 5, MTYPE UC, WRITE64 */
        CHECK(f64Ring[1] == 0x50001000u); /* Fence Low */
        CHECK(f64Ring[2] == 0x00000004u); /* Fence High */
        CHECK(f64Ring[3] == 0xAABBCCDDu); /* Value Low */
        CHECK(f64Ring[4] == 0x12345678u); /* Value High */

        /* Misalignment check */
        CHECK(Bc250SdmaEmitFence64(
            f64Ring, 16u, &f64Cursor,
            0x0000000450001001ull, 0x12345678AABBCCDDull) == BC250_SDMA_MISALIGNED);
    }

    cursor = 0u;
    memset(g_ring, 0, sizeof(g_ring));
    {
        const uint32_t data[4] = {0x11223344u,0x55667788u,0xA5A5A5A5u,0xBC250125u};
        CHECK(Bc250SdmaEmitWriteLinear(g_ring, TEST_RING_DWORDS, &cursor,
            0x000000F40FF04000ull, data, 4u) == BC250_SDMA_OK);
        CHECK(cursor == 8u);
        CHECK(g_ring[0] == 2u && g_ring[1] == 0x0FF04000u &&
            g_ring[2] == 0x000000F4u && g_ring[3] == 3u);
        CHECK(g_ring[4] == data[0] && g_ring[7] == data[3]);
    }

    /* 3. Test CONST_FILL emission */
    cursor = 0;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitConstFill(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x0000000003000000ull, 0x5A5A5A5Au, 4096u) == BC250_SDMA_OK);
    CHECK(cursor == 5u);
    CHECK(g_ring[0] == 0x8000000Bu); /* Opcode 11, Size 2 (DWORD) in bits 30-31 */
    CHECK(g_ring[1] == 0x03000000u); /* Dst Low */
    CHECK(g_ring[2] == 0x00000000u); /* Dst High */
    CHECK(g_ring[3] == 0x5A5A5A5Au); /* Fill value */
    CHECK(g_ring[4] == 4095u);       /* ByteCount - 1 */

    /* 4. Test NOP, TRAP, POLL_REGMEM emission */
    cursor = 0;
    memset(g_ring, 0, sizeof(g_ring));
    CHECK(Bc250SdmaEmitNop(g_ring, TEST_RING_DWORDS, &cursor, 2u) == BC250_SDMA_OK);
    CHECK(cursor == 3u);
    CHECK(g_ring[0] == 0x00020000u); /* NOP with 2 extra dwords */
    CHECK(g_ring[1] == 0);
    CHECK(g_ring[2] == 0);

    CHECK(Bc250SdmaEmitTrap(g_ring, TEST_RING_DWORDS, &cursor, 0x42u) == BC250_SDMA_OK);
    CHECK(cursor == 5u);
    CHECK(g_ring[3] == 0x00000006u); /* Opcode 6 */
    CHECK(g_ring[4] == 0x42u);

    CHECK(Bc250SdmaEmitPollRegMem(
        g_ring, TEST_RING_DWORDS, &cursor,
        0x000000000FF04000ull, 0x1u, 0x1u, 0u, 1u, 10u, 100u) == BC250_SDMA_OK);
    CHECK(cursor == 11u);

    /* 5. Test Ring space calculation and wraparound */
    rptr = 0;
    wptr = 0;
    CHECK(Bc250SdmaRingComputeAvailableSpace(
        TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
    CHECK(available == TEST_RING_DWORDS - 1u);

    /* Advance WPTR by 11 dwords */
    CHECK(Bc250SdmaRingAdvanceWptr(
        TEST_RING_DWORDS, wptr, 11u, &wptr) == BC250_SDMA_OK);
    CHECK(wptr == 11u);
    CHECK(Bc250SdmaRingComputeAvailableSpace(
        TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
    CHECK(available == TEST_RING_DWORDS - 1u - 11u);

    /* Simulate 12 sequential submits with wraparound */
    cursor = 0;
    rptr = 0;
    wptr = 0;
    for (i = 0; i < 12; ++i) {
        uint32_t startCursor = cursor;
        CHECK(Bc250SdmaRingComputeAvailableSpace(
            TEST_RING_DWORDS, rptr, wptr, &available) == BC250_SDMA_OK);
        CHECK(available >= 11u);

        CHECK(Bc250SdmaEmitCopyLinear(
            g_ring, TEST_RING_DWORDS, &cursor,
            0x01000000ull, 0x02000000ull + (uint64_t)(i * 4096u), 4096u) == BC250_SDMA_OK);
        CHECK(Bc250SdmaEmitFence(
            g_ring, TEST_RING_DWORDS, &cursor,
            0x0FF00000ull, 0xBC250000u + i) == BC250_SDMA_OK);
        CHECK(cursor - startCursor == 11u);

        CHECK(Bc250SdmaRingAdvanceWptr(
            TEST_RING_DWORDS, wptr, 11u, &wptr) == BC250_SDMA_OK);
        /* Simulate GPU consuming ring immediately (RPTR = WPTR) */
        rptr = wptr;
    }

    /* 6. Test Error cases */
    CHECK(Bc250SdmaEmitCopyLinear(
        NULL, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 4096u) == BC250_SDMA_BAD_ARGUMENT);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, 100u /* not power of 2 */, &cursor, 0x1000ull, 0x2000ull, 4096u) == BC250_SDMA_BAD_ARGUMENT);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 0u) == BC250_SDMA_PAYLOAD_TOO_LARGE);
    CHECK(Bc250SdmaEmitCopyLinear(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1000ull, 0x2000ull, 0x00400001u) == BC250_SDMA_PAYLOAD_TOO_LARGE);

    CHECK(Bc250SdmaEmitConstFill(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1001ull /* misaligned */, 0xAAu, 4096u) == BC250_SDMA_MISALIGNED);
    CHECK(Bc250SdmaEmitFence(
        g_ring, TEST_RING_DWORDS, &cursor, 0x1002ull /* misaligned */, 0x1u) == BC250_SDMA_MISALIGNED);

    /*
     * 6b. LAYOUT BYTE A BYTE DO INDIRECT (07/09).
     *
     * O pre-pad ja estava travado abaixo, mas os SEIS dwords do pacote nao
     * estavam -- e e' o conteudo deles que a engine consome. O doc 167 lista
     * como pergunta aberta 6 exatamente "o formato do nosso INDIRECT esta
     * correto para SDMA 5.0?". Este bloco congela a resposta que temos, para
     * que o pacote nao possa mudar em silencio.
     *
     * Layout esperado de SDMA_PKT_INDIRECT (SDMA 5.0):
     *   [0] header      = OP_INDIRECT | (vmid << 16)
     *   [1] base_lo     = IbAddress[31:0] com os 5 bits baixos zerados
     *   [2] base_hi     = IbAddress[63:32]
     *   [3] ib_size     = tamanho em DWORDS
     *   [4] csa_lo      = CsaAddress[31:0]
     *   [5] csa_hi      = CsaAddress[63:32]
     */
    {
        uint32_t base = 0u;
        cursor = 0u;
        for (i = 0u; i < 16u; ++i) {
            g_ring[i] = 0xDEADBEEFu;
        }
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x0000000146602000ull, 8u, 0ull, 8u) == BC250_SDMA_OK);
        CHECK(cursor == BC250_SDMA_DWORDS_INDIRECT);

        CHECK(g_ring[base + 0u] == (BC250_SDMA_OP_INDIRECT | (8u << 16)));
        CHECK(g_ring[base + 1u] == 0x46602000u);   /* alinhado a 32 B */
        CHECK(g_ring[base + 2u] == 0x00000001u);
        CHECK(g_ring[base + 3u] == 8u);            /* dwords, nao bytes */
        CHECK(g_ring[base + 4u] == 0u);            /* CSA=0: ver nota abaixo */
        CHECK(g_ring[base + 5u] == 0u);

        /*
         * CSA = 0 e' DELIBERADO, nao esquecimento. O upstream so' devolve um
         * CSA nao nulo quando a preempcao no meio do comando esta habilitada
         * (MCBP); com ela desligada -- que e' o nosso caso desde que
         * F_CNTL_MIDCMD saiu do CNTL -- o endereco correto e' zero.
         * Classificacao: H (recordado do upstream; nao ha copia local do
         * amdgpu neste repositorio para conferir).
         */

        /* Os 5 bits baixos do base_lo sao sempre zerados pelo builder. */
        cursor = 0u;
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x0000000146602020ull, 8u, 0ull, 8u) == BC250_SDMA_OK);
        CHECK(g_ring[1] == 0x46602020u);

        /* VMID entra no bit 16, e 15 e' o maximo aceito. */
        cursor = 0u;
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1000ull, 4u, 0ull, 15u) == BC250_SDMA_OK);
        CHECK(g_ring[0] == (BC250_SDMA_OP_INDIRECT | (15u << 16)));

        /* Recusas: VMID fora de faixa, tamanho zero, tamanho absurdo. */
        cursor = 0u;
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1000ull, 4u, 0ull, 16u) == BC250_SDMA_BAD_ARGUMENT);
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1000ull, 0u, 0ull, 8u) == BC250_SDMA_BAD_ARGUMENT);
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1000ull, 0x100000u, 0ull, 8u) == BC250_SDMA_BAD_ARGUMENT);

        /* IB desalinhada e CSA desalinhado falham fechado. */
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1001ull, 4u, 0ull, 8u) == BC250_SDMA_MISALIGNED);
        CHECK(Bc250SdmaEmitIndirect(g_ring, TEST_RING_DWORDS, &cursor,
            0x1000ull, 4u, 0x2001ull, 8u) == BC250_SDMA_MISALIGNED);
    }

    /* 7. SDMA 5.0: INDIRECT de 6 DW deve terminar em fronteira de 8 DW. */
    CHECK(Bc250SdmaIndirectPrePadDwords(0u) == 2u);
    CHECK(Bc250SdmaIndirectPrePadDwords(2u) == 0u);
    CHECK(Bc250SdmaIndirectPrePadDwords(13u) == 5u);
    CHECK(Bc250SdmaIndirectPrePadDwords(17u) == 1u);
    for (i = 0u; i < 32u; ++i) {
        uint32_t pad = Bc250SdmaIndirectPrePadDwords(i);
        CHECK(pad < BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS);
        CHECK(((i + pad + BC250_SDMA_DWORDS_INDIRECT) & 7u) == 0u);
    }

    /* 8. O tamanho publicado da propria IB tambem preserva o proximo VA. */
    CHECK(Bc250SdmaIbSizePadDwords(15u) == 1u);
    CHECK(Bc250SdmaIbSizePadDwords(16u) == 0u);
    CHECK(Bc250SdmaIbSizePadDwords(21u) == 3u);
    for (i = 0u; i < 32u; ++i) {
        uint32_t pad = Bc250SdmaIbSizePadDwords(i);
        CHECK(pad < BC250_SDMA_IB_PACKET_ALIGNMENT_DWORDS);
        CHECK(((i + pad) & 7u) == 0u);
    }

    {
        uint32_t words[8] = {0x8000000bu, 0x23f000u, 0u, 0u, 4095u, 0u, 0u, 0u};
        BC250_PAGING_PROOF proof;
        CHECK(Bc250PagingDecodeProof(words, 8u, 0x1ce865cc0ull, &proof));
        CHECK(Bc250PagingProofMatches(&proof, 0x1ce865cc0ull, 32u));
        /* Measured #99: another 32-byte IB in the SAME page must never borrow poison. */
        CHECK(!Bc250PagingProofMatches(&proof, 0x1ce865c80ull, 32u));
        CHECK(!Bc250PagingProofMatches(&proof, 0x1ce865cc0ull, 64u));
        CHECK(!Bc250PagingDecodeProof(words, 4u, 0x1ce865cc0ull, &proof));
        words[0] = 1u;
        CHECK(!Bc250PagingDecodeProof(words, 8u, 0x1ce865cc0ull, &proof));
    }

    /* Test COPY_LINEAR proof decoding */
    {
        uint32_t copyWords[8] = {
            0x00000001u,            /* Opcode 1, Subop 0 */
            4095u,                  /* ByteCount - 1 (4096 bytes) */
            0u,                     /* Endian */
            0x01000000u, 0u,        /* Src VA: 0x0000000001000000 */
            0x02000000u, 0u,        /* Dst VA: 0x0000000002000000 */
            0u                      /* Padding NOP */
        };
        BC250_PAGING_PROOF proof;
        CHECK(Bc250PagingDecodeProof(copyWords, 8u, 0x1ce865cc0ull, &proof));
        CHECK(proof.Opcode == BC250_PAGING_PROOF_OP_COPY);
        CHECK(proof.Source == 0x01000000ull);
        CHECK(proof.Destination == 0x02000000ull);
        CHECK(proof.Bytes == 4096u);
        CHECK(Bc250PagingProofMatches(&proof, 0x1ce865cc0ull, 32u));

        /* Misaligned source address should fail decode */
        copyWords[3] = 0x01000001u;
        CHECK(!Bc250PagingDecodeProof(copyWords, 8u, 0x1ce865cc0ull, &proof));
        copyWords[3] = 0x01000000u;

        /* Misaligned byte count should fail decode */
        copyWords[1] = 4094u; /* 4095 bytes */
        CHECK(!Bc250PagingDecodeProof(copyWords, 8u, 0x1ce865cc0ull, &proof));
    }
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSdmaPacketBuilderTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSdmaPacketBuilderTest() ? 0 : 1;
}
#endif
