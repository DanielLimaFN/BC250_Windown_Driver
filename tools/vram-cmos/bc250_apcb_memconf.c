#include "bc250_apcb_memconf.h"

static bc250_u16 Bc250ApcbLe16(const bc250_u8 *p)
{
    return (bc250_u16)(p[0] | ((bc250_u16)p[1] << 8));
}

static bc250_u32 Bc250ApcbLe32(const bc250_u8 *p)
{
    return (bc250_u32)p[0] | ((bc250_u32)p[1] << 8) |
        ((bc250_u32)p[2] << 16) | ((bc250_u32)p[3] << 24);
}

bc250_u16 Bc250ApcbChecksum(const bc250_u8 *Bytes, unsigned ByteCount)
{
    bc250_u16 sum = 0u;
    unsigned i;
    if (Bytes == 0 || ByteCount < BC250_APCB_BYTES) return 0u;
    for (i = BC250_APCB_CHECKSUM_FIRST; i < BC250_APCB_BYTES; ++i) {
        sum = (bc250_u16)(sum + Bytes[i]);
    }
    return sum;
}

BC250_APCB_STATUS Bc250ApcbDecode(const bc250_u8 *Bytes, unsigned ByteCount,
                                  BC250_APCB_MEMCONF *Out)
{
    if (Bytes == 0 || Out == 0 || ByteCount < BC250_APCB_BYTES) {
        return BC250_APCB_BAD_ARGUMENT;
    }
    Out->Signature = Bc250ApcbLe32(Bytes + 0x00);
    Out->Checksum = Bc250ApcbLe16(Bytes + 0x04);
    Out->ClockSpeed = Bc250ApcbLe16(Bytes + 0x06);
    Out->tCL = Bytes[0x08];
    Out->tRAS = Bytes[0x09];
    Out->tRCDRD = Bytes[0x0A];
    Out->tRCDWR = Bytes[0x0B];
    Out->tRCAb = Bytes[0x0C];
    Out->tRCPb = Bytes[0x0D];
    Out->tRPAb = Bytes[0x0E];
    Out->tRPPb = Bytes[0x0F];
    Out->tRRDS = Bytes[0x10];
    Out->tRRDL = Bytes[0x11];
    Out->tRTP = Bytes[0x12];
    Out->tFAW = Bytes[0x13];
    Out->tREF = Bc250ApcbLe16(Bytes + 0x14);
    Out->RFCPb = Bc250ApcbLe16(Bytes + 0x16);
    Out->tRFC = Bc250ApcbLe16(Bytes + 0x18);
    Out->UmaSizeMb = Bc250ApcbLe16(Bytes + 0x1A);
    return BC250_APCB_OK;
}

int Bc250ApcbIsTrustworthy(const bc250_u8 *Bytes, unsigned ByteCount)
{
    BC250_APCB_MEMCONF conf;
    if (Bc250ApcbDecode(Bytes, ByteCount, &conf) != BC250_APCB_OK) return 0;
    if (conf.Signature != BC250_APCB_SIG_ABL &&
        conf.Signature != BC250_APCB_SIG_LINUX_TOOL) {
        return 0;
    }
    return conf.Checksum == Bc250ApcbChecksum(Bytes, ByteCount);
}

const char *Bc250ApcbSignatureName(bc250_u32 Signature)
{
    switch (Signature) {
    case BC250_APCB_SIG_ABL: return "ABL_SIGNATURE (aplicado pelo firmware)";
    case BC250_APCB_SIG_LINUX_TOOL: return "LINUX_TOOL_SIGNATURE (pedido pendente: aplica no proximo boot frio)";
    case BC250_APCB_SIG_CMOS_BAD: return "CMOS_BAD_ERROR_CODE";
    case BC250_APCB_SIG_WATCHDOG: return "WATCH_DOG_TIMER_FIRED";
    case BC250_APCB_SIG_CHECKSUM_ERR: return "CHECKSUM_ERROR_CODE";
    case BC250_APCB_SIG_SIGNATURE_ERR: return "SIGNATURE_ERROR_CODE";
    default: return "DESCONHECIDA";
    }
}

const char *Bc250ApcbStatusName(BC250_APCB_STATUS Status)
{
    switch (Status) {
    case BC250_APCB_OK: return "ok";
    case BC250_APCB_BAD_ARGUMENT: return "argumento_invalido";
    case BC250_APCB_UNKNOWN_SIGNATURE: return "bloco_atual_nao_confiavel";
    case BC250_APCB_BAD_CHECKSUM: return "checksum_atual_errado";
    case BC250_APCB_UMA_OUT_OF_RANGE: return "uma_fora_da_faixa_256_8192";
    case BC250_APCB_UMA_NOT_ALIGNED: return "uma_fora_do_passo_256";
    case BC250_APCB_UMA_UNCHANGED: return "uma_igual_ao_atual";
    default: return "desconhecido";
    }
}

BC250_APCB_STATUS Bc250ApcbPlanUmaWrite(const bc250_u8 *Current,
                                        unsigned ByteCount,
                                        bc250_u32 NewUmaMb,
                                        bc250_u8 *Planned)
{
    BC250_APCB_MEMCONF conf;
    bc250_u16 sum;
    unsigned i;

    if (Current == 0 || Planned == 0 || ByteCount < BC250_APCB_BYTES) {
        return BC250_APCB_BAD_ARGUMENT;
    }
    (void)Bc250ApcbDecode(Current, ByteCount, &conf);
    if (conf.Signature != BC250_APCB_SIG_ABL &&
        conf.Signature != BC250_APCB_SIG_LINUX_TOOL) {
        return BC250_APCB_UNKNOWN_SIGNATURE;
    }
    if (conf.Checksum != Bc250ApcbChecksum(Current, ByteCount)) {
        return BC250_APCB_BAD_CHECKSUM;
    }
    if (NewUmaMb < BC250_APCB_UMA_MIN_MB || NewUmaMb > BC250_APCB_UMA_MAX_MB) {
        return BC250_APCB_UMA_OUT_OF_RANGE;
    }
    if ((NewUmaMb % BC250_APCB_UMA_STEP_MB) != 0u) {
        return BC250_APCB_UMA_NOT_ALIGNED;
    }
    if (NewUmaMb == conf.UmaSizeMb) {
        return BC250_APCB_UMA_UNCHANGED;
    }

    for (i = 0; i < BC250_APCB_BYTES; ++i) Planned[i] = Current[i];
    Planned[0x00] = (bc250_u8)(BC250_APCB_SIG_LINUX_TOOL & 0xFFu);
    Planned[0x01] = (bc250_u8)((BC250_APCB_SIG_LINUX_TOOL >> 8) & 0xFFu);
    Planned[0x02] = (bc250_u8)((BC250_APCB_SIG_LINUX_TOOL >> 16) & 0xFFu);
    Planned[0x03] = (bc250_u8)((BC250_APCB_SIG_LINUX_TOOL >> 24) & 0xFFu);
    Planned[0x1A] = (bc250_u8)(NewUmaMb & 0xFFu);
    Planned[0x1B] = (bc250_u8)((NewUmaMb >> 8) & 0xFFu);
    sum = Bc250ApcbChecksum(Planned, BC250_APCB_BYTES);
    Planned[0x04] = (bc250_u8)(sum & 0xFFu);
    Planned[0x05] = (bc250_u8)((sum >> 8) & 0xFFu);
    return BC250_APCB_OK;
}
