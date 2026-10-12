/* ===========================================================================
 * dcn_probe_contract_test.c — Testes Unitários das Baterias A, B e C do DCN 2.0.1
 * =========================================================================== */

#include "../core/bc250_dcn_probe_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) {
        bytes[index] = (unsigned char)Value;
    }
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    const unsigned char *s = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) {
        d[index] = s[index];
    }
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return __LINE__; \
    } \
} while (0)
#endif

/* ---------------------------------------------------------------------------
 * Teste 1: Bateria A (Inspeção de Scanout e Timings OTG0/HUBP0 do GOP)
 * --------------------------------------------------------------------------- */
static int TestBatteryA(void)
{
    BC250_DCN_BATTERY_A_RESULT result;
    bool ok;

    /* Parâmetros físicos medidos no silício do BC-250 sob GOP (2560x1080@56.13Hz) */
    uint32_t otgControl = 1u;                        /* Bit 0 = OTG habilitado */
    uint32_t otgStatus = 0u;
    uint32_t otgPosition = (540u << 16) | 1280u;     /* Linha 540, Pixel 1280 */
    uint32_t otgFrameCount = 12345u;
    uint32_t otgHTotal = 2783u;                      /* Registrador armazena Total - 1 (2784 - 1) */
    uint32_t otgHBlank = (2783u << 16) | 2560u;      /* HBlankStart=2560, HBlankEnd=2783 */
    uint32_t otgVTotal = 1110u;                      /* Registrador armazena Total - 1 (1111 - 1) */
    uint32_t otgVBlank = (1110u << 16) | 1080u;      /* VBlankStart=1080, VBlankEnd=1110 */
    uint32_t hubpViewportDim = (1080u << 16) | 2560u;/* 2560 x 1080 */
    uint32_t hubpSurfacePitch = 2560u;
    uint32_t hubpSurfaceAddrLo = 0x00AB0000u;
    uint32_t hubpSurfaceAddrHi = 0u;
    uint32_t hubpSurfaceInUseLo = 0x00AB0000u;
    uint32_t hubpSurfaceInUseHi = 0u;

    /* 1. Decodificação com valores válidos */
    ok = Bc250DcnDecodeBatteryA(
        otgControl, otgStatus, otgPosition, otgFrameCount,
        otgHTotal, otgHBlank, otgVTotal, otgVBlank,
        hubpViewportDim, hubpSurfacePitch,
        hubpSurfaceAddrLo, hubpSurfaceAddrHi,
        hubpSurfaceInUseLo, hubpSurfaceInUseHi,
        &result);

    CHECK(ok);
    CHECK(result.Valid);
    CHECK(result.ViewportWidth == 2560u);
    CHECK(result.ViewportHeight == 1080u);
    CHECK(result.SurfacePitch == 2560u);
    CHECK(result.HTotal == 2784u);
    CHECK(result.VTotal == 1111u);
    CHECK(result.HBlankStart == 2560u);
    CHECK(result.HBlankEnd == 2783u);
    CHECK(result.VBlankStart == 1080u);
    CHECK(result.VBlankEnd == 1110u);
    CHECK(result.PrimarySurfaceAddress == 0x00AB0000ull);
    CHECK(result.SurfaceInUseAddress == 0x00AB0000ull);
    CHECK(result.OtgEnabled);
    CHECK(result.FrameCount == 12345u);
    CHECK(result.CurrentPixel == 1280u);
    CHECK(result.CurrentLine == 540u);
    /* 173.611.437 * 100 / (2784 * 1111) = 5613 (56.13 Hz medido) */
    CHECK(result.CalculatedRefreshHz100 == 5613u);

    /* 2. Caso negativo: ponteiro nulo */
    ok = Bc250DcnDecodeBatteryA(
        otgControl, otgStatus, otgPosition, otgFrameCount,
        otgHTotal, otgHBlank, otgVTotal, otgVBlank,
        hubpViewportDim, hubpSurfacePitch,
        hubpSurfaceAddrLo, hubpSurfaceAddrHi,
        hubpSurfaceInUseLo, hubpSurfaceInUseHi,
        NULL);
    CHECK(!ok);

    /* 3. Caso negativo: viewport zerada */
    ok = Bc250DcnDecodeBatteryA(
        otgControl, otgStatus, otgPosition, otgFrameCount,
        otgHTotal, otgHBlank, otgVTotal, otgVBlank,
        0u, 0u, 0u, 0u, 0u, 0u,
        &result);
    CHECK(!ok);
    CHECK(!result.Valid);

    return 0;
}

/* ---------------------------------------------------------------------------
 * Teste 2: Bateria B (Link DPCD via AUX)
 * --------------------------------------------------------------------------- */
static int TestBatteryB(void)
{
    BC250_DCN_BATTERY_B_RESULT result;
    uint8_t dpcd[0x205];
    bool ok;

    memset(dpcd, 0, sizeof(dpcd));
    dpcd[0] = 0x14;        /* DPCD 1.4 */
    dpcd[1] = 0x14;        /* HBR2 (5.40 Gbps) */
    dpcd[2] = 0x84;        /* Enhanced Framing + 4 lanes */
    dpcd[0x202] = 0x77;    /* CR & EQ Done lanes 0 e 1 */

    /* 1. Decodificação válida */
    ok = Bc250DcnDecodeBatteryB(dpcd, sizeof(dpcd), 0x00000010u, &result);
    CHECK(ok);
    CHECK(result.Valid);
    CHECK(result.DpcdMajorRev == 1u);
    CHECK(result.DpcdMinorRev == 4u);
    CHECK(result.MaxLinkRateCode == 0x14u);
    CHECK(result.MaxLinkRateMbps == 5400u);
    CHECK(result.MaxLaneCount == 4u);
    CHECK(result.EnhancedFramingSupported);
    CHECK(result.CrDoneLane0);
    CHECK(result.EqDoneLane0);
    CHECK(result.CrDoneLane1);
    CHECK(result.EqDoneLane1);
    CHECK(result.AuxStatusRaw == 0x00000010u);

    /* 2. Caso negativo: buffer nulo ou curto */
    ok = Bc250DcnDecodeBatteryB(NULL, 0u, 0u, &result);
    CHECK(!ok);
    CHECK(!result.Valid);

    ok = Bc250DcnDecodeBatteryB(dpcd, 2u, 0u, &result);
    CHECK(!ok);

    return 0;
}

/* ---------------------------------------------------------------------------
 * Teste 3: Bateria C (Leitura e Decodificação do Bloco 0 do EDID Real)
 * --------------------------------------------------------------------------- */
static int TestBatteryC(void)
{
    BC250_DCN_BATTERY_C_RESULT result;
    uint8_t edid[128];
    uint32_t sum;
    size_t i;
    bool ok;

    memset(edid, 0, sizeof(edid));

    /* Cabeçalho padrão EDID */
    edid[0] = 0x00;
    edid[1] = 0xFF;
    edid[2] = 0xFF;
    edid[3] = 0xFF;
    edid[4] = 0xFF;
    edid[5] = 0xFF;
    edid[6] = 0xFF;
    edid[7] = 0x00;

    /* ID Fabricante: 'GSM' -> (7 << 10) | (19 << 5) | 13 = 0x1E6D */
    edid[8] = 0x1E;
    edid[9] = 0x6D;

    /* Código do Produto: 0x015B */
    edid[10] = 0x5B;
    edid[11] = 0x01;

    /* Número de Série: 0x12345678 */
    edid[12] = 0x78;
    edid[13] = 0x56;
    edid[14] = 0x34;
    edid[15] = 0x12;

    /* Semana 12 de 2021 (1990 + 31) */
    edid[16] = 12;
    edid[17] = 31;

    /* Versão 1.4 */
    edid[18] = 1;
    edid[19] = 4;

    /* DTD 1 (Bytes 54..71): 2560x1080@60Hz, Pixel Clock 185.00 MHz */
    /* Pixel clock em 10 kHz = 18500 = 0x4844 */
    edid[54] = 0x44;
    edid[55] = 0x48;
    /* HActive = 2560 (0x0A00), HBlank = 400 (0x0190) */
    edid[56] = 0x00;
    edid[57] = 0x90;
    edid[58] = 0xA1; /* (0x0A << 4) | 0x01 */
    /* VActive = 1080 (0x0438), VBlank = 45 (0x002D) */
    edid[59] = 0x38;
    edid[60] = 0x2D;
    edid[61] = 0x40; /* (0x04 << 4) | 0x00 */

    /* Descritor de Nome do Monitor (Bytes 72..89, Tag 0xFC) */
    edid[72] = 0x00;
    edid[73] = 0x00;
    edid[74] = 0x00;
    edid[75] = 0xFC;
    edid[76] = 0x00;
    /* "LG ULTRAWIDE\n" */
    edid[77]  = 'L';
    edid[78]  = 'G';
    edid[79]  = ' ';
    edid[80]  = 'U';
    edid[81]  = 'L';
    edid[82]  = 'T';
    edid[83]  = 'R';
    edid[84]  = 'A';
    edid[85]  = 'W';
    edid[86]  = 'I';
    edid[87]  = 'D';
    edid[88]  = 'E';
    edid[89]  = '\n';

    /* Cálculo de Checksum para que sum % 256 == 0 */
    sum = 0;
    for (i = 0; i < 127; ++i) {
        sum += edid[i];
    }
    edid[127] = (uint8_t)((256u - (sum & 0xFFu)) & 0xFFu);

    /* 1. Decodificação Válida */
    ok = Bc250DcnDecodeBatteryC(edid, sizeof(edid), &result);
    CHECK(ok);
    CHECK(result.Valid);
    CHECK(result.HeaderValid);
    CHECK(result.ChecksumValid);
    CHECK(result.ManufacturerId[0] == 'G');
    CHECK(result.ManufacturerId[1] == 'S');
    CHECK(result.ManufacturerId[2] == 'M');
    CHECK(result.ManufacturerId[3] == '\0');
    CHECK(result.ProductCode == 0x015Bu);
    CHECK(result.SerialNumber == 0x12345678u);
    CHECK(result.ManufactureWeek == 12u);
    CHECK(result.ManufactureYear == 2021u);
    CHECK(result.PreferredWidth == 2560u);
    CHECK(result.PreferredHeight == 1080u);
    CHECK(result.PreferredPixelClockKhz == 185000u);
    /* Verifica string do monitor */
    CHECK(result.MonitorName[0] == 'L');
    CHECK(result.MonitorName[1] == 'G');
    CHECK(result.MonitorName[2] == ' ');
    CHECK(result.MonitorName[3] == 'U');
    CHECK(result.MonitorName[11] == 'E');
    CHECK(result.MonitorName[12] == '\0');

    /* 2. Caso negativo: cabeçalho corrompido */
    edid[0] = 0xAA;
    ok = Bc250DcnDecodeBatteryC(edid, sizeof(edid), &result);
    CHECK(!ok);
    CHECK(!result.Valid);
    edid[0] = 0x00;

    /* 3. Caso negativo: checksum corrompido */
    edid[127] ^= 0x01;
    ok = Bc250DcnDecodeBatteryC(edid, sizeof(edid), &result);
    CHECK(!ok);
    CHECK(!result.Valid);

    return 0;
}

static int Bc250RunDcnProbeContractTest(void)
{
    int rc;
    rc = TestBatteryA();
    if (rc != 0) return rc;
    rc = TestBatteryB();
    if (rc != 0) return rc;
    rc = TestBatteryC();
    if (rc != 0) return rc;
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunDcnProbeContractTest());
}
#else
int main(void)
{
    return Bc250RunDcnProbeContractTest();
}
#endif
