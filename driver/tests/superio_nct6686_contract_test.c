#include "../core/bc250_superio_nct6686_contract.h"

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
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

static uint8_t g_superIoRegs[256];
static uint8_t g_hwmBank1[256];
static uint8_t g_hwmBank2[256];
static uint8_t g_hwmBank3[256];
static uint8_t g_currentBank = 1;
static uint8_t g_superIoIdx = 0;
static uint8_t g_hwmIdx = 0;

static uint8_t MockIsaInByte(void *Context, uint16_t Port)
{
    (void)Context;
    if (Port == 0x002F) {
        return g_superIoRegs[g_superIoIdx];
    }
    if (Port == 0x0A26) {
        if (g_currentBank == 1) return g_hwmBank1[g_hwmIdx];
        if (g_currentBank == 2) return g_hwmBank2[g_hwmIdx];
        if (g_currentBank == 3) return g_hwmBank3[g_hwmIdx];
    }
    return 0xFFu;
}

static void MockIsaOutByte(void *Context, uint16_t Port, uint8_t Value)
{
    (void)Context;
    if (Port == 0x002E) {
        g_superIoIdx = Value;
    } else if (Port == 0x002F) {
        g_superIoRegs[g_superIoIdx] = Value;
    } else if (Port == 0x0A25) {
        g_hwmIdx = Value;
    } else if (Port == 0x0A26) {
        if (g_hwmIdx == 0xFF) {
            g_currentBank = Value;
        } else {
            if (g_currentBank == 1) g_hwmBank1[g_hwmIdx] = Value;
            if (g_currentBank == 2) g_hwmBank2[g_hwmIdx] = Value;
            if (g_currentBank == 3) g_hwmBank3[g_hwmIdx] = Value;
        }
    }
}

static int Bc250RunSuperIoNct6686Test(void)
{
    BC250_ISA_PORT_TRANSPORT transport;
    BC250_SUPERIO_NCT6686_METRICS metrics;
    uint16_t chipId = 0u;
    uint16_t baseAddr = 0u;

    transport.InByte = MockIsaInByte;
    transport.OutByte = MockIsaOutByte;
    transport.Context = NULL;

    /* Setup SuperIO registers */
    g_superIoRegs[0x20] = 0xC4; /* Chip ID Hi */
    g_superIoRegs[0x21] = 0x51; /* Chip ID Lo (NCT6686D) */
    g_superIoRegs[0x60] = 0x0A; /* Base Hi */
    g_superIoRegs[0x61] = 0x20; /* Base Lo */

    /* Setup Bank 1: Temperatures */
    g_hwmBank1[0x50] = 45; /* CPU Temp 45 C */
    g_hwmBank1[0x51] = 38; /* System Temp 38 C */
    g_hwmBank1[0x52] = 58; /* VRM MOS Temp 58 C */

    /* Setup Bank 2: Voltages */
    g_hwmBank2[0x10] = 125; /* Vcore = 125 * 8 = 1000 mV */
    g_hwmBank2[0x11] = 125; /* +12V = 125 * 96 = 12000 mV */
    g_hwmBank2[0x12] = 125; /* +5V = 125 * 40 = 5000 mV */
    g_hwmBank2[0x13] = 138; /* +3.3V = 138 * 24 = 3312 mV */
    g_hwmBank2[0x14] = 120; /* SoC = 120 * 8 = 960 mV */

    /* Setup Bank 3: Fans */
    g_hwmBank3[0x10] = 0x05; /* Fan 1 RPM Hi */
    g_hwmBank3[0x11] = 0xDC; /* Fan 1 RPM Lo (1500 RPM) */
    g_hwmBank3[0x30] = 128;  /* Fan 1 PWM = 128 (50%) */

    /* 1. Detect */
    CHECK(Bc250Nct6686Detect(&transport, &chipId, &baseAddr) == BC250_SUPERIO_STATUS_OK);
    CHECK(chipId == 0xC451u);
    CHECK(baseAddr == 0x0A20u);

    /* 2. Read Sensors */
    CHECK(Bc250Nct6686ReadSensors(&transport, baseAddr, &metrics) == BC250_SUPERIO_STATUS_OK);
    CHECK(metrics.Valid == 1u);
    CHECK(metrics.TempCpuC == 45);
    CHECK(metrics.TempSystemC == 38);
    CHECK(metrics.TempVrmMosC == 58);
    CHECK(metrics.VoltageVcoreMv == 1000u);
    CHECK(metrics.Voltage12VMv == 12000u);
    CHECK(metrics.Voltage5VMv == 5000u);
    CHECK(metrics.Voltage33VMv == 3312u);
    CHECK(metrics.VoltageSocMv == 960u);
    CHECK(metrics.FanRpm[0] == 1500u);
    CHECK(metrics.FanPwm[0] == 128u);

    /* 3. Set Fan PWM */
    CHECK(Bc250Nct6686SetFanPwm(&transport, baseAddr, 0u, 200u) == BC250_SUPERIO_STATUS_OK);
    CHECK(g_hwmBank3[0x30] == 200u);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunSuperIoNct6686Test() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunSuperIoNct6686Test() ? 0 : 1;
}
#endif
