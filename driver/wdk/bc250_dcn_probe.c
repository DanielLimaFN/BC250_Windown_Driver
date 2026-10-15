/* ===========================================================================
 * bc250_dcn_probe.c — Módulo de Sonda DCN 2.0.1 em Modo Kernel (WDK)
 *
 * Target: AMD BC-250 APU (1002:13FE, GC 10.1.3 / Cyan Skillfish 2, DCN 2.0.1)
 *
 * Executa as Baterias A, B e C durante o StartDevice do KMD.
 * Todas as operações são puramente não-intrusivas (somente leitura e DP AUX).
 * =========================================================================== */

#include "bc250_dcn_probe.h"
#include "bc250_display_ddi.h"

#define BC250_DCN_MAP_SIZE 0x20000u /* 128 KiB cobre OTG0, HUBP0 e AUX0 */

/*
 * Corrida 251 (doc 664), item 3: foto do clock gating por bloco e refresh
 * real, so' leitura, uma vez no fim do bring-up. Tabela gerada de
 * tools/linux/bc250-l1b-regs.txt (gerar-lista-regs-l1b.py), offsets em bytes
 * na BAR de registradores = (BASE[seg] + mm) * 4 dos headers de inc/linux_ref.
 */
typedef struct BC250_CG_PHOTO_ENTRY { const char *Name; uint32_t Offset; } BC250_CG_PHOTO_ENTRY;
static const BC250_CG_PHOTO_ENTRY g_Bc250CgPhoto[] = {
    { "IH_CLK_CTRL", 0x045ECu }, /* osssys_5_0_0_offset.h mmIH_CLK_CTRL=0x00db BASE_IDX=0 */
    { "SDMA0_POWER_CNTL", 0x049E8u }, /* gc_10_1_0_offset.h mmSDMA0_POWER_CNTL=0x001a BASE_IDX=0 */
    { "SDMA0_CLK_CTRL", 0x049ECu }, /* gc_10_1_0_offset.h mmSDMA0_CLK_CTRL=0x001b BASE_IDX=0 */
    { "SDMA1_POWER_CNTL", 0x061E8u }, /* gc_10_1_0_offset.h mmSDMA1_POWER_CNTL=0x061a BASE_IDX=0 */
    { "SDMA1_CLK_CTRL", 0x061ECu }, /* gc_10_1_0_offset.h mmSDMA1_CLK_CTRL=0x061b BASE_IDX=0 */
    { "GC_ATC_L2_CGTT_CLK_CTRL", 0x0A030u }, /* gc_10_1_0_offset.h mmGC_ATC_L2_CGTT_CLK_CTRL=0x15ac BASE_IDX=0 */
    { "GCVM_L2_CGTT_CLK_CTRL", 0x0A17Cu }, /* gc_10_1_0_offset.h mmGCVM_L2_CGTT_CLK_CTRL=0x15ff BASE_IDX=0 */
    { "RLC_PG_CNTL", 0x3B10Cu }, /* gc_10_1_0_offset.h mmRLC_PG_CNTL=0x4c43 BASE_IDX=1 */
    { "RLC_CGTT_MGCG_OVERRIDE", 0x3B120u }, /* gc_10_1_0_offset.h mmRLC_CGTT_MGCG_OVERRIDE=0x4c48 BASE_IDX=1 */
    { "RLC_CGCG_CGLS_CTRL", 0x3B124u }, /* gc_10_1_0_offset.h mmRLC_CGCG_CGLS_CTRL=0x4c49 BASE_IDX=1 */
    { "RLC_CGCG_RAMP_CTRL", 0x3B128u }, /* gc_10_1_0_offset.h mmRLC_CGCG_RAMP_CTRL=0x4c4a BASE_IDX=1 */
    { "CGTT_SPI_CGTSSM_CLK_CTRL", 0x3C034u }, /* gc_10_1_0_offset.h mmCGTT_SPI_CGTSSM_CLK_CTRL=0x500d BASE_IDX=1 */
    { "CGTT_SPI_PS_CLK_CTRL", 0x3C1F4u }, /* gc_10_1_0_offset.h mmCGTT_SPI_PS_CLK_CTRL=0x507d BASE_IDX=1 */
    { "CGTT_SPIS_CLK_CTRL", 0x3C1F8u }, /* gc_10_1_0_offset.h mmCGTT_SPIS_CLK_CTRL=0x507e BASE_IDX=1 */
    { "CGTT_SPI_CLK_CTRL", 0x3C200u }, /* gc_10_1_0_offset.h mmCGTT_SPI_CLK_CTRL=0x5080 BASE_IDX=1 */
    { "CGTT_PC_CLK_CTRL", 0x3C204u }, /* gc_10_1_0_offset.h mmCGTT_PC_CLK_CTRL=0x5081 BASE_IDX=1 */
    { "CGTT_BCI_CLK_CTRL", 0x3C208u }, /* gc_10_1_0_offset.h mmCGTT_BCI_CLK_CTRL=0x5082 BASE_IDX=1 */
    { "CGTT_VGT_CLK_CTRL", 0x3C210u }, /* gc_10_1_0_offset.h mmCGTT_VGT_CLK_CTRL=0x5084 BASE_IDX=1 */
    { "CGTT_IA_CLK_CTRL", 0x3C214u }, /* gc_10_1_0_offset.h mmCGTT_IA_CLK_CTRL=0x5085 BASE_IDX=1 */
    { "CGTT_WD_CLK_CTRL", 0x3C218u }, /* gc_10_1_0_offset.h mmCGTT_WD_CLK_CTRL=0x5086 BASE_IDX=1 */
    { "CGTT_GS_NGG_CLK_CTRL", 0x3C21Cu }, /* gc_10_1_0_offset.h mmCGTT_GS_NGG_CLK_CTRL=0x5087 BASE_IDX=1 */
    { "CGTT_PA_CLK_CTRL", 0x3C220u }, /* gc_10_1_0_offset.h mmCGTT_PA_CLK_CTRL=0x5088 BASE_IDX=1 */
    { "CGTT_SC_CLK_CTRL0", 0x3C224u }, /* gc_10_1_0_offset.h mmCGTT_SC_CLK_CTRL0=0x5089 BASE_IDX=1 */
    { "CGTT_SC_CLK_CTRL1", 0x3C228u }, /* gc_10_1_0_offset.h mmCGTT_SC_CLK_CTRL1=0x508a BASE_IDX=1 */
    { "CGTT_SC_CLK_CTRL2", 0x3C22Cu }, /* gc_10_1_0_offset.h mmCGTT_SC_CLK_CTRL2=0x508b BASE_IDX=1 */
    { "CGTT_SQ_CLK_CTRL", 0x3C230u }, /* gc_10_1_0_offset.h mmCGTT_SQ_CLK_CTRL=0x508c BASE_IDX=1 */
    { "CGTT_SQG_CLK_CTRL", 0x3C234u }, /* gc_10_1_0_offset.h mmCGTT_SQG_CLK_CTRL=0x508d BASE_IDX=1 */
    { "SQ_LDS_CLK_CTRL", 0x3C240u }, /* bc250_golden_settings_contract.c */
    { "CGTT_SX_CLK_CTRL0", 0x3C250u }, /* gc_10_1_0_offset.h mmCGTT_SX_CLK_CTRL0=0x5094 BASE_IDX=1 */
    { "CGTT_SX_CLK_CTRL1", 0x3C254u }, /* gc_10_1_0_offset.h mmCGTT_SX_CLK_CTRL1=0x5095 BASE_IDX=1 */
    { "CGTT_SX_CLK_CTRL2", 0x3C258u }, /* gc_10_1_0_offset.h mmCGTT_SX_CLK_CTRL2=0x5096 BASE_IDX=1 */
    { "CGTT_SX_CLK_CTRL3", 0x3C25Cu }, /* gc_10_1_0_offset.h mmCGTT_SX_CLK_CTRL3=0x5097 BASE_IDX=1 */
    { "CGTT_SX_CLK_CTRL4", 0x3C260u }, /* gc_10_1_0_offset.h mmCGTT_SX_CLK_CTRL4=0x5098 BASE_IDX=1 */
    { "TD_CGTT_CTRL", 0x3C270u }, /* gc_10_1_0_offset.h mmTD_CGTT_CTRL=0x509c BASE_IDX=1 */
    { "TA_CGTT_CTRL", 0x3C274u }, /* gc_10_1_0_offset.h mmTA_CGTT_CTRL=0x509d BASE_IDX=1 */
    { "CGTT_TCPI_CLK_CTRL", 0x3C278u }, /* gc_10_1_0_offset.h mmCGTT_TCPI_CLK_CTRL=0x509e BASE_IDX=1 */
    { "CGTT_TCI_CLK_CTRL", 0x3C27Cu }, /* gc_10_1_0_offset.h mmCGTT_TCI_CLK_CTRL=0x509f BASE_IDX=1 */
    { "CGTT_GDS_CLK_CTRL", 0x3C280u }, /* gc_10_1_0_offset.h mmCGTT_GDS_CLK_CTRL=0x50a0 BASE_IDX=1 */
    { "DB_CGTT_CLK_CTRL_0", 0x3C290u }, /* gc_10_1_0_offset.h mmDB_CGTT_CLK_CTRL_0=0x50a4 BASE_IDX=1 */
    { "CB_CGTT_SCLK_CTRL", 0x3C2A0u }, /* gc_10_1_0_offset.h mmCB_CGTT_SCLK_CTRL=0x50a8 BASE_IDX=1 */
    { "GL2C_CGTT_SCLK_CTRL", 0x3C2B0u }, /* gc_10_1_0_offset.h mmGL2C_CGTT_SCLK_CTRL=0x50ac BASE_IDX=1 */
    { "GL2A_CGTT_SCLK_CTRL", 0x3C2B4u }, /* gc_10_1_0_offset.h mmGL2A_CGTT_SCLK_CTRL=0x50ad BASE_IDX=1 */
    { "GL2A_CGTT_SCLK_CTRL_1", 0x3C2B8u }, /* gc_10_1_0_offset.h mmGL2A_CGTT_SCLK_CTRL_1=0x50ae BASE_IDX=1 */
    { "CGTT_CP_CLK_CTRL", 0x3C2C0u }, /* gc_10_1_0_offset.h mmCGTT_CP_CLK_CTRL=0x50b0 BASE_IDX=1 */
    { "CGTT_CPF_CLK_CTRL", 0x3C2C4u }, /* gc_10_1_0_offset.h mmCGTT_CPF_CLK_CTRL=0x50b1 BASE_IDX=1 */
    { "CGTT_CPC_CLK_CTRL", 0x3C2C8u }, /* gc_10_1_0_offset.h mmCGTT_CPC_CLK_CTRL=0x50b2 BASE_IDX=1 */
    { "CGTT_RLC_CLK_CTRL", 0x3C2D4u }, /* gc_10_1_0_offset.h mmCGTT_RLC_CLK_CTRL=0x50b5 BASE_IDX=1 */
    { "RMI_CGTT_SCLK_CTRL", 0x3C300u }, /* gc_10_1_0_offset.h mmRMI_CGTT_SCLK_CTRL=0x50c0 BASE_IDX=1 */
    { "CGTT_TCPF_CLK_CTRL", 0x3C304u }, /* gc_10_1_0_offset.h mmCGTT_TCPF_CLK_CTRL=0x50c1 BASE_IDX=1 */
    { "GCR_CGTT_SCLK_CTRL", 0x3C308u }, /* gc_10_1_0_offset.h mmGCR_CGTT_SCLK_CTRL=0x50c2 BASE_IDX=1 */
    { "UTCL1_CGTT_CLK_CTRL", 0x3C30Cu }, /* gc_10_1_0_offset.h mmUTCL1_CGTT_CLK_CTRL=0x50c3 BASE_IDX=1 */
    { "GCEA_CGTT_CLK_CTRL", 0x3C310u }, /* gc_10_1_0_offset.h mmGCEA_CGTT_CLK_CTRL=0x50c4 BASE_IDX=1 */
    { "SE_CAC_CGTT_CLK_CTRL", 0x3C340u }, /* gc_10_1_0_offset.h mmSE_CAC_CGTT_CLK_CTRL=0x50d0 BASE_IDX=1 */
    { "GC_CAC_CGTT_CLK_CTRL", 0x3C360u }, /* gc_10_1_0_offset.h mmGC_CAC_CGTT_CLK_CTRL=0x50d8 BASE_IDX=1 */
    { "CGTT_GL1C_CLK_CTRL", 0x3C3B0u }, /* gc_10_1_0_offset.h mmCGTT_GL1C_CLK_CTRL=0x50ec BASE_IDX=1 */
    { "CGTT_CHC_CLK_CTRL", 0x3C3B8u }, /* gc_10_1_0_offset.h mmCGTT_CHC_CLK_CTRL=0x50ee BASE_IDX=1 */
    { "CGTT_CHCG_CLK_CTRL", 0x3C3BCu }, /* gc_10_1_0_offset.h mmCGTT_CHCG_CLK_CTRL=0x50ef BASE_IDX=1 */
    { "CGTT_GL1A_CLK_CTRL", 0x3C3C0u }, /* gc_10_1_0_offset.h mmCGTT_GL1A_CLK_CTRL=0x50f0 BASE_IDX=1 */
    { "CGTT_CHA_CLK_CTRL", 0x3C3C4u }, /* gc_10_1_0_offset.h mmCGTT_CHA_CLK_CTRL=0x50f1 BASE_IDX=1 */
    { "GUS_CGTT_CLK_CTRL", 0x3C3D0u }, /* gc_10_1_0_offset.h mmGUS_CGTT_CLK_CTRL=0x50f4 BASE_IDX=1 */
    { "CGTT_PH_CLK_CTRL0", 0x3C3E0u }, /* gc_10_1_0_offset.h mmCGTT_PH_CLK_CTRL0=0x50f8 BASE_IDX=1 */
    { "CGTT_PH_CLK_CTRL1", 0x3C3E4u }, /* gc_10_1_0_offset.h mmCGTT_PH_CLK_CTRL1=0x50f9 BASE_IDX=1 */
    { "CGTT_PH_CLK_CTRL2", 0x3C3E8u }, /* gc_10_1_0_offset.h mmCGTT_PH_CLK_CTRL2=0x50fa BASE_IDX=1 */
    { "CGTT_PH_CLK_CTRL3", 0x3C3ECu }, /* gc_10_1_0_offset.h mmCGTT_PH_CLK_CTRL3=0x50fb BASE_IDX=1 */
    { "GCUTCL2_CGTT_CLK_CTRL", 0x3EB70u }, /* gc_10_1_0_offset.h mmGCUTCL2_CGTT_CLK_CTRL=0x5adc BASE_IDX=1 */
    { "DAGB0_RD_CGTT_CLK_CTRL", 0x68060u }, /* mmhub_2_0_0_offset.h mmDAGB0_RD_CGTT_CLK_CTRL=0x0018 BASE_IDX=0 */
    { "DAGB0_WR_CGTT_CLK_CTRL", 0x68124u }, /* mmhub_2_0_0_offset.h mmDAGB0_WR_CGTT_CLK_CTRL=0x0049 BASE_IDX=0 */
    { "MMEA0_CGTT_CLK_CTRL", 0x68860u }, /* mmhub_2_0_0_offset.h mmMMEA0_CGTT_CLK_CTRL=0x0218 BASE_IDX=0 */
    { "MM_ATC_L2_MISC_CG", 0x69928u }, /* mmhub_2_0_0_offset.h mmMM_ATC_L2_MISC_CG=0x064a BASE_IDX=0 */
};
#define BC250_CG_PHOTO_MAP_SIZE 0x6A000u

/* Primeiro ponto do refresh real: quadro do OTG0 e QPC lidos na Bateria A. */
static volatile LONG s_Bc250RefreshT0Valid = 0;
static uint32_t s_Bc250RefreshT0Frame = 0u;
static LONGLONG s_Bc250RefreshT0Qpc = 0;

/*
 * DCN.AUX0 (doc 658, Corrida 249): as transacoes AUX das Baterias B e C
 * escreviam AUX_SW_DATA/AUX_CNTL em 0x4800-0x480C, offsets de um header
 * "legado" sem procedencia no catalogo (Lei 7). O bloco AUX do DCN 2.0.1,
 * derivado como os registradores do OTG que ja lemos (DMU_BASE seg2 0x34C0 +
 * mm, dio_dc_dp_aux0: AUX_CONTROL 0x1F50 .. AUX_SW_STATUS 0x1F54), fica em
 * 0x15040-0x15050; 0x4800 e' outro bloco (dword 0x1200). Alem disso a
 * sequencia de escrita usa o layout do DCE (GO no AUX_CONTROL), nao o do
 * DCN (GO no AUX_SW_CONTROL). Ate isso ser conferido no upstream, B e C
 * leem o estado do bloco AUX real (sem escrita, sem ler a FIFO de dados,
 * que avanca o indice) e as transacoes ficam desligadas. 1 = transacoes
 * antigas (escrita em 0x4800), so' por decisao explicita.
 */
#define BC250_DCN_PROBE_AUX_TRANSFERS 0
#define BC250_DCN_AUX0_DCN_CONTROL     0x00015040u /* (0x34C0 + 0x1F50) * 4 */
#define BC250_DCN_AUX0_DCN_SW_CONTROL  0x00015044u /* (0x34C0 + 0x1F51) * 4 */
#define BC250_DCN_AUX0_DCN_ARB_CONTROL 0x00015048u /* (0x34C0 + 0x1F52) * 4 */
#define BC250_DCN_AUX0_DCN_INT_CONTROL 0x0001504Cu /* (0x34C0 + 0x1F53) * 4 */
#define BC250_DCN_AUX0_DCN_SW_STATUS   0x00015050u /* (0x34C0 + 0x1F54) * 4 */

static inline uint32_t DcnRead(volatile UCHAR *Base, uint32_t Offset)
{
    return *(volatile uint32_t *)(Base + Offset);
}

static inline void DcnWrite(volatile UCHAR *Base, uint32_t Offset, uint32_t Value)
{
    *(volatile uint32_t *)(Base + Offset) = Value;
}

#if BC250_DCN_PROBE_AUX_TRANSFERS
/*
 * Execução segura e com timeout limitado de transação no canal DP AUX0.
 * Zero risco de hang: número de tentativas estritamente limitado com stall.
 */
static bool Bc250DcnAuxTransfer(
    volatile UCHAR *Base,
    uint8_t Command,
    uint32_t Address,
    uint8_t *Buffer,
    uint32_t Length,
    _Out_ uint32_t *OutStatus)
{
    uint32_t status = 0;
    uint32_t poll;
    uint32_t val;
    uint32_t repliedCount;

    if (OutStatus != NULL) {
        *OutStatus = 0;
    }
    if (Base == NULL || Buffer == NULL || Length == 0 || Length > 16) {
        return false;
    }

    /* 1. Prepara comando e endereço na FIFO AUX_SW_DATA */
    /* Byte 0: Command (4-bit) | Address[19:16] */
    val = ((uint32_t)(Command & 0x0Fu) << 4) | ((Address >> 16) & 0x0Fu);
    /* Index=0, RW=0 (write to SW buffer), AutoIncDisable=1 (bit 8) */
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA, val | 0x0100u);

    /* Byte 1: Address[15:8], AutoIncDisable=0 */
    val = (Address >> 8) & 0xFFu;
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA, val);

    /* Byte 2: Address[7:0] */
    val = Address & 0xFFu;
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA, val);

    /* Byte 3: Length - 1 */
    val = (Length - 1u) & 0x0Fu;
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA, val);

    /* 2. Dispara a transação no AUX_SW_CONTROL: StartDelay=0, WrBytes=4, GO=1 (bit 0) */
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_CNTL, (4u << 8) | 1u);

    /* 3. Aguarda conclusão (AUX_SW_DONE) de forma estritamente limitada */
    for (poll = 0; poll < 100; ++poll) {
        status = DcnRead(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_STATUS);
        if ((status & 0x00010000u) != 0 || (status & 1u) != 0) {
            break;
        }
        KeStallExecutionProcessor(10); /* 10 us */
    }

    if (OutStatus != NULL) {
        *OutStatus = status;
    }

    /* Se não completou em até 1 ms ou erro */
    if (poll >= 100) {
        return false;
    }

    /* 4. Lê os bytes respondidos da FIFO */
    /* Configura leitura: Index=0, RW=1 (bit 9), AutoIncDisable=1 (bit 8) */
    DcnWrite(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA, 0x0300u);

    /* Lê reply status (bits [7:4] == 0 indica ACK) */
    val = DcnRead(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA) & 0xFFu;
    if ((val >> 4) != 0) {
        return false;
    }

    repliedCount = (status >> 8) & 0x1Fu;
    if (repliedCount > Length) {
        repliedCount = Length;
    }

    for (val = 0; val < repliedCount; ++val) {
        Buffer[val] = (uint8_t)(DcnRead(Base, BC250_DCN_REG_DP_AUX0_AUX_SW_DATA) & 0xFFu);
    }

    return (repliedCount > 0);
}
#endif /* BC250_DCN_PROBE_AUX_TRANSFERS */

NTSTATUS Bc250DcnProbeExecute(_In_ PHYSICAL_ADDRESS MmioPhysical)
{
    volatile UCHAR *dcnBase;
    BC250_DCN_BATTERY_A_RESULT batA;
    BC250_DCN_BATTERY_B_RESULT batB;
    BC250_DCN_BATTERY_C_RESULT batC;
    uint32_t auxStatusRaw = 0;
    uint8_t dpcdBuffer[16];
    uint8_t edidBuffer[128];
    bool dpcdOk = false;
    bool edidOk = false;
    size_t chunk;

    if (MmioPhysical.QuadPart == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Mapeamento temporário e seguro da janela DCN na BAR5 */
    dcnBase = (volatile UCHAR *)MmMapIoSpace(MmioPhysical, BC250_DCN_MAP_SIZE, MmNonCached);
    if (dcnBase == NULL) {
        Bc250DisplayLog("[DCN_PROBE] AVISO: Falha ao mapear janela DCN (BAR5 0x%llX, 128 KiB)\n",
            (unsigned long long)MmioPhysical.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Bc250DisplayLog("[DCN_PROBE] Iniciando Baterias A, B e C do DCN 2.0.1 (BAR5 0x%llX)...\n",
        (unsigned long long)MmioPhysical.QuadPart);

    /* =======================================================================
     * BATERIA A: Inspeção do Scanout e Timings Físicos do OTG0/HUBP0 (GOP)
     * ======================================================================= */
    {
        uint32_t otgControl = DcnRead(dcnBase, BC250_DCN_REG_OTG0_CONTROL);
        uint32_t otgStatus = DcnRead(dcnBase, BC250_DCN_REG_OTG0_STATUS);
        uint32_t otgPosition = DcnRead(dcnBase, BC250_DCN_REG_OTG0_STATUS_POSITION);
        uint32_t otgFrameCount = DcnRead(dcnBase, BC250_DCN_REG_OTG0_STATUS_FRAME_COUNT);
        s_Bc250RefreshT0Qpc = KeQueryPerformanceCounter(NULL).QuadPart;
        s_Bc250RefreshT0Frame = otgFrameCount;
        InterlockedExchange(&s_Bc250RefreshT0Valid, 1);
        uint32_t otgHTotal = DcnRead(dcnBase, BC250_DCN_REG_OTG0_H_TOTAL);
        uint32_t otgHBlank = DcnRead(dcnBase, BC250_DCN_REG_OTG0_H_BLANK_START_END);
        uint32_t otgVTotal = DcnRead(dcnBase, BC250_DCN_REG_OTG0_V_TOTAL);
        uint32_t otgVBlank = DcnRead(dcnBase, BC250_DCN_REG_OTG0_V_BLANK_START_END);
        uint32_t hubpViewport = DcnRead(dcnBase, BC250_DCN_REG_HUBP0_DCSURF_PRI_VIEWPORT_DIM);
        uint32_t hubpPitch = DcnRead(dcnBase, BC250_DCN_REG_HUBPREQ0_DCSURF_SURFACE_PITCH);
        uint32_t hubpAddrLo = DcnRead(dcnBase, BC250_DCN_REG_HUBPREQ0_PRIMARY_SURFACE_ADDR);
        uint32_t hubpAddrHi = DcnRead(dcnBase, BC250_DCN_REG_HUBPREQ0_PRIMARY_SURFACE_HI);
        uint32_t hubpInUseLo = DcnRead(dcnBase, BC250_DCN_REG_HUBPREQ0_DCSURF_SURFACE_INUSE);
        uint32_t hubpInUseHi = DcnRead(dcnBase, BC250_DCN_REG_HUBPREQ0_SURFACE_INUSE_HI);

        Bc250DcnDecodeBatteryA(
            otgControl, otgStatus, otgPosition, otgFrameCount,
            otgHTotal, otgHBlank, otgVTotal, otgVBlank,
            hubpViewport, hubpPitch,
            hubpAddrLo, hubpAddrHi, hubpInUseLo, hubpInUseHi,
            &batA);

        Bc250DisplayLog("[DCN_BATTERY_A] Scanout & OTG Timings (GOP Takeover):\n"
            "  Status=%s, OtgEnabled=%u, FrameCount=%u\n"
            "  Viewport=%ux%u, Pitch=%u\n"
            "  OTG Timings: HTotal=%u, VTotal=%u | HBlank=[%u..%u], VBlank=[%u..%u]\n"
            "  Current Scan: Line %u, Pixel %u\n"
            "  Surface Physical Addr: Base=0x%llX, ActiveInUse=0x%llX\n"
            "  Derived PixelRate=%llu Hz, Refresh=%u.%02u Hz\n",
            batA.Valid ? "VALID" : "INVALID",
            batA.OtgEnabled ? 1u : 0u,
            batA.FrameCount,
            batA.ViewportWidth, batA.ViewportHeight,
            batA.SurfacePitch,
            batA.HTotal, batA.VTotal,
            batA.HBlankStart, batA.HBlankEnd,
            batA.VBlankStart, batA.VBlankEnd,
            batA.CurrentLine, batA.CurrentPixel,
            (unsigned long long)batA.PrimarySurfaceAddress,
            (unsigned long long)batA.SurfaceInUseAddress,
            (unsigned long long)batA.CalculatedPixelRateHz,
            batA.CalculatedRefreshHz100 / 100u,
            batA.CalculatedRefreshHz100 % 100u);
    }

    /* =======================================================================
     * BATERIA B: Inspeção de Link DisplayPort via DPCD por canal AUX
     * ======================================================================= */
    RtlZeroMemory(dpcdBuffer, sizeof(dpcdBuffer));
#if !BC250_DCN_PROBE_AUX_TRANSFERS
    {
        uint32_t auxCtl = DcnRead(dcnBase, BC250_DCN_AUX0_DCN_CONTROL);
        uint32_t auxSwCtl = DcnRead(dcnBase, BC250_DCN_AUX0_DCN_SW_CONTROL);
        uint32_t auxArb = DcnRead(dcnBase, BC250_DCN_AUX0_DCN_ARB_CONTROL);
        uint32_t auxInt = DcnRead(dcnBase, BC250_DCN_AUX0_DCN_INT_CONTROL);
        uint32_t auxSt = DcnRead(dcnBase, BC250_DCN_AUX0_DCN_SW_STATUS);
        UNREFERENCED_PARAMETER(dpcdOk);
        UNREFERENCED_PARAMETER(edidOk);
        UNREFERENCED_PARAMETER(chunk);
        UNREFERENCED_PARAMETER(batB);
        UNREFERENCED_PARAMETER(batC);
        UNREFERENCED_PARAMETER(edidBuffer);
        UNREFERENCED_PARAMETER(auxStatusRaw);
        Bc250DisplayLog("[DCN_BATTERY_B] AUX0 do DCN (BAR5 0x15040.., somente leitura): AUX_CONTROL=0x%08X (AUX_EN=%u) AUX_SW_CONTROL=0x%08X AUX_ARB_CONTROL=0x%08X AUX_INTERRUPT_CONTROL=0x%08X AUX_SW_STATUS=0x%08X | %s\n",
            auxCtl, auxCtl & 1u, auxSwCtl, auxArb, auxInt, auxSt,
            (auxCtl == 0xFFFFFFFFu) ? "bloco morto (0xFFFFFFFF)" : "bloco responde");
        Bc250DisplayLog("[DCN_BATTERY_C] EDID por I2C-over-AUX NAO tentado nesta corrida: transacoes AUX desligadas (BC250_DCN_PROBE_AUX_TRANSFERS=0, doc 658); a Bateria B diz se o bloco AUX real responde\n");
        MmUnmapIoSpace((PVOID)dcnBase, BC250_DCN_MAP_SIZE);
        Bc250DisplayLog("[DCN_PROBE] Bateria A lida; B e C em modo somente leitura.\n");
        return STATUS_SUCCESS;
    }
#else
    auxStatusRaw = DcnRead(dcnBase, BC250_DCN_REG_DP_AUX0_AUX_SW_STATUS);

    /* Comando 0x9 = Native DP AUX Read nos primeiros 16 bytes de DPCD (0x00000) */
    dpcdOk = Bc250DcnAuxTransfer(dcnBase, 0x09u, 0x00000u, dpcdBuffer, sizeof(dpcdBuffer), &auxStatusRaw);
    if (dpcdOk && Bc250DcnDecodeBatteryB(dpcdBuffer, sizeof(dpcdBuffer), auxStatusRaw, &batB)) {
        Bc250DisplayLog("[DCN_BATTERY_B] DPCD Link Status (DP AUX Native):\n"
            "  DPCD Rev: %u.%u | MaxLinkRate: %u Mbps (Code 0x%02X)\n"
            "  MaxLanes: %u | EnhancedFraming: %s\n"
            "  Status Raw: 0x%08X\n",
            batB.DpcdMajorRev, batB.DpcdMinorRev,
            batB.MaxLinkRateMbps, batB.MaxLinkRateCode,
            batB.MaxLaneCount,
            batB.EnhancedFramingSupported ? "YES" : "NO",
            batB.AuxStatusRaw);
    } else {
        Bc250DisplayLog("[DCN_BATTERY_B] AUX Native Read status=0x%08X (Non-fatal, GOP display link ativo)\n",
            auxStatusRaw);
    }

    /* =======================================================================
     * BATERIA C: Leitura de EDID Físico Real via I2C-over-AUX (0x50)
     * ======================================================================= */
    RtlZeroMemory(edidBuffer, sizeof(edidBuffer));
    edidOk = true;

    /* Leitura em 8 blocos de 16 bytes para respeitar limite do buffer AUX */
    for (chunk = 0; chunk < 8; ++chunk) {
        uint32_t addr = (uint32_t)(chunk * 16u);
        /* I2C Read no endereço 0x50 */
        if (!Bc250DcnAuxTransfer(dcnBase, 0x01u, 0x50u, &edidBuffer[addr], 16u, &auxStatusRaw)) {
            edidOk = false;
            break;
        }
    }

    if (edidOk && Bc250DcnDecodeBatteryC(edidBuffer, sizeof(edidBuffer), &batC)) {
        Bc250DisplayLog("[DCN_BATTERY_C] EDID Real via I2C-over-AUX (0x50):\n"
            "  Monitor: \"%s\" | Fabricante: %s | Modelo: 0x%04X | Serial: 0x%08X\n"
            "  Fabricacao: Sem %u / %u | Extensoes: %u\n"
            "  DTD 1: %ux%u @ %u.%02u Hz (PixelClock: %u kHz)\n",
            batC.MonitorName, batC.ManufacturerId, batC.ProductCode, batC.SerialNumber,
            batC.ManufactureWeek, batC.ManufactureYear, batC.ExtensionBlockCount,
            batC.PreferredWidth, batC.PreferredHeight,
            batC.PreferredRefreshHz100 / 100u, batC.PreferredRefreshHz100 % 100u,
            batC.PreferredPixelClockKhz);
    } else {
        Bc250DisplayLog("[DCN_BATTERY_C] EDID I2C-over-AUX nao disponivel (status=0x%08X). GOP EDID ativo.\n",
            auxStatusRaw);
    }

    /* Desmapeia a janela DCN */
    MmUnmapIoSpace((PVOID)dcnBase, BC250_DCN_MAP_SIZE);
    Bc250DisplayLog("[DCN_PROBE] Baterias A, B e C concluidas com seguranca.\n");

    return STATUS_SUCCESS;
#endif /* !BC250_DCN_PROBE_AUX_TRANSFERS */
}

/*
 * Corrida 251 (doc 664): foto do clock gating por bloco e refresh real.
 * Somente leitura. O refresh e' medido entre o quadro do OTG0 lido na Bateria
 * A e o lido aqui, contra o QPC; se o intervalo for menor que 1 s, espera ate
 * 1 s com KeDelayExecutionThread (PASSIVE_LEVEL, fora de submissao), sem
 * nenhum registrador escrito.
 */
NTSTATUS Bc250BootProbeCgAndRefresh(_In_ PHYSICAL_ADDRESS MmioPhysical, _In_ BOOLEAN DoCgPhoto)
{
    volatile UCHAR *base;
    LARGE_INTEGER freq;
    LONGLONG qpc1;
    uint32_t frame1;
    uint32_t i;
    const SIZE_T mapBytes = DoCgPhoto ? (SIZE_T)BC250_CG_PHOTO_MAP_SIZE : (SIZE_T)BC250_DCN_MAP_SIZE;

    if (MmioPhysical.QuadPart == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    base = (volatile UCHAR *)MmMapIoSpace(MmioPhysical, mapBytes, MmNonCached);
    if (base == NULL) {
        Bc250DisplayLog("[CG_FOTO] AVISO: falha ao mapear 0x%X bytes da BAR5 0x%llX; foto e refresh nao medidos\n",
            (uint32_t)mapBytes, (unsigned long long)MmioPhysical.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!DoCgPhoto) {
        Bc250DisplayLog("[CG_FOTO] DESLIGADA (doc 665): a leitura dos 69 registradores e' a suspeita da trava da Corrida 251; so' o refresh real e' medido\n");
    } else {
    Bc250DisplayLog("[CG_FOTO] inicio: %u registradores de clock gating, so' leitura, depois de RLC/G.1/SDMA/CP/IH\n",
        (uint32_t)(sizeof(g_Bc250CgPhoto) / sizeof(g_Bc250CgPhoto[0])));
    for (i = 0u; i < (uint32_t)(sizeof(g_Bc250CgPhoto) / sizeof(g_Bc250CgPhoto[0])); ++i) {
        Bc250DisplayLog("[CG_FOTO] %s @0x%05X = 0x%08X\n",
            g_Bc250CgPhoto[i].Name, g_Bc250CgPhoto[i].Offset,
            DcnRead(base, g_Bc250CgPhoto[i].Offset));
    }
    }

    freq.QuadPart = 0;
    qpc1 = KeQueryPerformanceCounter(&freq).QuadPart;
    if (InterlockedCompareExchange(&s_Bc250RefreshT0Valid, 0, 0) == 0 || freq.QuadPart <= 0) {
        Bc250DisplayLog("[REFRESH_REAL] NAO MEDIDO: sem o primeiro ponto da Bateria A\n");
    } else {
        LONGLONG dt = qpc1 - s_Bc250RefreshT0Qpc;
        /* 251b (doc 665): sem espera; o intervalo da Bateria A ate aqui ja e' de segundos. */
        frame1 = DcnRead(base, BC250_DCN_REG_OTG0_STATUS_FRAME_COUNT);
        {
            uint32_t frames = frame1 - s_Bc250RefreshT0Frame;
            ULONGLONG us = (ULONGLONG)((dt * 1000000LL) / freq.QuadPart);
            ULONGLONG mhz = (us != 0ull) ? ((ULONGLONG)frames * 1000000000ull) / us : 0ull;
            Bc250DisplayLog("[REFRESH_REAL] quadros %u -> %u (%u) em %llu us -> %llu.%03llu Hz medidos; o EDID sintetizado declara 56,130 Hz\n",
                s_Bc250RefreshT0Frame, frame1, frames, us, mhz / 1000ull, mhz % 1000ull);
        }
    }
    MmUnmapIoSpace((PVOID)base, mapBytes);
    return STATUS_SUCCESS;
}
