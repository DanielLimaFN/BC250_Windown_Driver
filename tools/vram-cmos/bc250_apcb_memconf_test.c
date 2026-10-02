#include <stdio.h>
#include <string.h>
#include "bc250_apcb_memconf.h"

#define CHECK(x) do { if (!(x)) { printf("FALHOU linha %d: %s\n", __LINE__, #x); return 0; } } while (0)

/* Dumps REAIS da CMOS da BC-250 do Daniel (referencias/linux/histo.txt, 01/10/2026). */
static const bc250_u8 kDump512[BC250_APCB_BYTES] = {
    0x24, 0x41, 0x42, 0x4C, 0xFA, 0x03, 0xD6, 0x06, 0x18, 0x34, 0x1B, 0x13, 0x4E, 0x00, 0x1A, 0x00,
    0x08, 0x08, 0x02, 0x20, 0xF7, 0x26, 0xD2, 0x00, 0x18, 0x01, 0x00, 0x02
};
static const bc250_u8 kDump2048[BC250_APCB_BYTES] = {
    0x24, 0x41, 0x42, 0x4C, 0x00, 0x04, 0xD6, 0x06, 0x18, 0x34, 0x1B, 0x13, 0x4E, 0x00, 0x1A, 0x00,
    0x08, 0x08, 0x02, 0x20, 0xF7, 0x26, 0xD2, 0x00, 0x18, 0x01, 0x00, 0x08
};

static int TestDecodeRealDumps(void)
{
    BC250_APCB_MEMCONF c;
    CHECK(Bc250ApcbDecode(kDump512, BC250_APCB_BYTES, &c) == BC250_APCB_OK);
    CHECK(c.Signature == BC250_APCB_SIG_ABL);
    CHECK(c.Checksum == 0x03FAu);
    CHECK(c.ClockSpeed == 1750u);
    CHECK(c.tCL == 24u && c.tRAS == 52u && c.tFAW == 32u);
    CHECK(c.tREF == 9975u && c.RFCPb == 210u && c.tRFC == 280u);
    CHECK(c.UmaSizeMb == 512u);
    CHECK(Bc250ApcbChecksum(kDump512, BC250_APCB_BYTES) == 0x03FAu);
    CHECK(Bc250ApcbIsTrustworthy(kDump512, BC250_APCB_BYTES));

    CHECK(Bc250ApcbDecode(kDump2048, BC250_APCB_BYTES, &c) == BC250_APCB_OK);
    CHECK(c.UmaSizeMb == 2048u && c.Checksum == 0x0400u);
    CHECK(Bc250ApcbIsTrustworthy(kDump2048, BC250_APCB_BYTES));
    return 1;
}

/* O plano de 512 -> 2048 tem de ser exatamente o que a ferramenta Linux gravou e a ABL aceitou. */
static int TestPlanMatchesWhatTheFirmwareAccepted(void)
{
    bc250_u8 planned[BC250_APCB_BYTES];
    BC250_APCB_MEMCONF c;
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 2048u, planned) == BC250_APCB_OK);
    CHECK(Bc250ApcbDecode(planned, BC250_APCB_BYTES, &c) == BC250_APCB_OK);
    CHECK(c.Signature == BC250_APCB_SIG_LINUX_TOOL);
    CHECK(c.Checksum == 0x0400u);
    CHECK(c.UmaSizeMb == 2048u);
    CHECK(Bc250ApcbIsTrustworthy(planned, BC250_APCB_BYTES));
    /* tirando a assinatura (que a ABL troca no boot), e' byte a byte o dump pos-boot */
    CHECK(memcmp(planned + 4, kDump2048 + 4, BC250_APCB_BYTES - 4) == 0);
    /* timings intactos */
    CHECK(memcmp(planned + 6, kDump512 + 6, 0x1A - 6) == 0);
    return 1;
}

static int TestRefusals(void)
{
    bc250_u8 planned[BC250_APCB_BYTES];
    bc250_u8 bad[BC250_APCB_BYTES];

    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 128u, planned) == BC250_APCB_UMA_OUT_OF_RANGE);
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 0u, planned) == BC250_APCB_UMA_OUT_OF_RANGE);
    /* 256 = o BAR: piso desde a decisao de 01/10 (doc 551) */
    CHECK(Bc250ApcbPlanUmaWrite(kDump2048, BC250_APCB_BYTES, 256u, planned) == BC250_APCB_OK);
    CHECK(planned[0x1A] == 0x00u && planned[0x1B] == 0x01u);
    CHECK(Bc250ApcbIsTrustworthy(planned, BC250_APCB_BYTES));
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 8448u, planned) == BC250_APCB_UMA_OUT_OF_RANGE);
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 1000u, planned) == BC250_APCB_UMA_NOT_ALIGNED);
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 512u, planned) == BC250_APCB_UMA_UNCHANGED);
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES, 8192u, planned) == BC250_APCB_OK);
    CHECK(Bc250ApcbPlanUmaWrite(kDump2048, BC250_APCB_BYTES, 4096u, planned) == BC250_APCB_OK);
    CHECK(Bc250ApcbPlanUmaWrite(0, BC250_APCB_BYTES, 2048u, planned) == BC250_APCB_BAD_ARGUMENT);
    CHECK(Bc250ApcbPlanUmaWrite(kDump512, BC250_APCB_BYTES - 1, 2048u, planned) == BC250_APCB_BAD_ARGUMENT);

    /* checksum errado: recusa, nao "conserta" */
    memcpy(bad, kDump512, sizeof(bad));
    bad[0x08] ^= 0x01u;
    CHECK(!Bc250ApcbIsTrustworthy(bad, BC250_APCB_BYTES));
    CHECK(Bc250ApcbPlanUmaWrite(bad, BC250_APCB_BYTES, 2048u, planned) == BC250_APCB_BAD_CHECKSUM);

    /* assinatura de erro do firmware: recusa */
    memcpy(bad, kDump512, sizeof(bad));
    bad[0] = 0x43u; bad[1] = 0x48u; bad[2] = 0x4Bu; bad[3] = 0x45u; /* CHECKSUM_ERROR_CODE */
    CHECK(Bc250ApcbPlanUmaWrite(bad, BC250_APCB_BYTES, 2048u, planned) == BC250_APCB_UNKNOWN_SIGNATURE);
    CHECK(strcmp(Bc250ApcbSignatureName(0x454B4843u), "CHECKSUM_ERROR_CODE") == 0);
    return 1;
}

int main(void)
{
    if (!TestDecodeRealDumps()) return 1;
    if (!TestPlanMatchesWhatTheFirmwareAccepted()) return 1;
    if (!TestRefusals()) return 1;
    printf("bc250_apcb_memconf_test: PASS (dumps reais 512/2048, plano = gravacao aceita pela ABL, 10 recusas)\n");
    return 0;
}
