#include "bc250_superio_nct6686_contract.h"
#include <string.h>

#define NCT6686_ENTER_KEY               0x87u
#define NCT6686_EXIT_KEY                0xAAu
#define NCT6686_REG_LOGICAL_DEVICE      0x07u
#define NCT6686_REG_CHIP_ID_HI          0x20u
#define NCT6686_REG_CHIP_ID_LO          0x21u
#define NCT6686_REG_BASE_HI             0x60u
#define NCT6686_REG_BASE_LO             0x61u
#define NCT6686_LDN_HWM                 0x0Bu

static uint8_t SuperIoReadReg(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t IndexPort,
    uint16_t DataPort,
    uint8_t Reg)
{
    Transport->OutByte(Transport->Context, IndexPort, Reg);
    return Transport->InByte(Transport->Context, DataPort);
}

static void SuperIoWriteReg(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t IndexPort,
    uint16_t DataPort,
    uint8_t Reg,
    uint8_t Val)
{
    Transport->OutByte(Transport->Context, IndexPort, Reg);
    Transport->OutByte(Transport->Context, DataPort, Val);
}

BC250_SUPERIO_STATUS Bc250Nct6686Detect(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t *ChipId,
    uint16_t *BaseAddress)
{
    uint16_t ports[2][2] = { { 0x002Eu, 0x002Fu }, { 0x004Eu, 0x004Fu } };
    uint32_t p;

    if (Transport == NULL || Transport->InByte == NULL || Transport->OutByte == NULL) {
        return BC250_SUPERIO_STATUS_BAD_ARGUMENT;
    }

    for (p = 0u; p < 2u; ++p) {
        uint16_t idxPort = ports[p][0];
        uint16_t datPort = ports[p][1];
        uint8_t idHi, idLo;
        uint16_t id;

        /* 1. Entra no espaco de configuracao */
        Transport->OutByte(Transport->Context, idxPort, NCT6686_ENTER_KEY);
        Transport->OutByte(Transport->Context, idxPort, NCT6686_ENTER_KEY);

        /* 2. Le Chip ID */
        idHi = SuperIoReadReg(Transport, idxPort, datPort, NCT6686_REG_CHIP_ID_HI);
        idLo = SuperIoReadReg(Transport, idxPort, datPort, NCT6686_REG_CHIP_ID_LO);
        id = (uint16_t)((idHi << 8) | idLo);

        /* NCT6686D ID: 0xC451, 0xC452 ou 0xD44x */
        if ((id & 0xFFF0u) == 0xC450u || (id & 0xFF00u) == 0xD400u) {
            uint8_t baseHi, baseLo;
            uint16_t base;

            /* Seleciona Logical Device 0x0B (Hardware Monitor) */
            SuperIoWriteReg(Transport, idxPort, datPort, NCT6686_REG_LOGICAL_DEVICE, NCT6686_LDN_HWM);
            baseHi = SuperIoReadReg(Transport, idxPort, datPort, NCT6686_REG_BASE_HI);
            baseLo = SuperIoReadReg(Transport, idxPort, datPort, NCT6686_REG_BASE_LO);
            base = (uint16_t)((baseHi << 8) | baseLo);

            if (base == 0u || base == 0xFFFFu) {
                base = BC250_NCT6686_DEFAULT_BASE_ADDR;
            }

            /* Sai do espaco de configuracao */
            Transport->OutByte(Transport->Context, idxPort, NCT6686_EXIT_KEY);

            if (ChipId != NULL) *ChipId = id;
            if (BaseAddress != NULL) *BaseAddress = base;
            return BC250_SUPERIO_STATUS_OK;
        }

        /* Sai do espaco de configuracao */
        Transport->OutByte(Transport->Context, idxPort, NCT6686_EXIT_KEY);
    }

    return BC250_SUPERIO_STATUS_CHIP_NOT_FOUND;
}

BC250_SUPERIO_STATUS Bc250Nct6686ReadSensors(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t BaseAddress,
    BC250_SUPERIO_NCT6686_METRICS *Metrics)
{
    uint16_t hwmIdx;
    uint16_t hwmDat;
    uint8_t fanIdx;

    if (Transport == NULL || Transport->InByte == NULL || Transport->OutByte == NULL || Metrics == NULL) {
        return BC250_SUPERIO_STATUS_BAD_ARGUMENT;
    }
    if (BaseAddress == 0u) {
        BaseAddress = BC250_NCT6686_DEFAULT_BASE_ADDR;
    }

    memset(Metrics, 0, sizeof(*Metrics));
    hwmIdx = BaseAddress + 5u;
    hwmDat = BaseAddress + 6u;

    Metrics->BaseAddress = BaseAddress;
    Metrics->FanCount = BC250_NCT6686_MAX_FAN_COUNT;

    /* Leitura de Temperaturas (Banco 1) */
    SuperIoWriteReg(Transport, hwmIdx, hwmDat, 0xFFu, 0x01u); /* Seleciona Banco 1 */
    Metrics->TempCpuC = (int16_t)(int8_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x50u);
    Metrics->TempSystemC = (int16_t)(int8_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x51u);
    Metrics->TempVrmMosC = (int16_t)(int8_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x52u);

    /* Leitura de Tensoes (Banco 2) */
    SuperIoWriteReg(Transport, hwmIdx, hwmDat, 0xFFu, 0x02u); /* Seleciona Banco 2 */
    Metrics->VoltageVcoreMv = (uint16_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x10u) * 8u;
    Metrics->Voltage12VMv = (uint16_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x11u) * 96u;
    Metrics->Voltage5VMv = (uint16_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x12u) * 40u;
    Metrics->Voltage33VMv = (uint16_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x13u) * 24u;
    Metrics->VoltageSocMv = (uint16_t)SuperIoReadReg(Transport, hwmIdx, hwmDat, 0x14u) * 8u;

    /* Leitura de Fan RPM e PWM (Banco 3) */
    SuperIoWriteReg(Transport, hwmIdx, hwmDat, 0xFFu, 0x03u); /* Seleciona Banco 3 */
    for (fanIdx = 0u; fanIdx < BC250_NCT6686_MAX_FAN_COUNT; ++fanIdx) {
        uint8_t rpmHi = SuperIoReadReg(Transport, hwmIdx, hwmDat, (uint8_t)(0x10u + fanIdx * 2u));
        uint8_t rpmLo = SuperIoReadReg(Transport, hwmIdx, hwmDat, (uint8_t)(0x11u + fanIdx * 2u));
        uint16_t rpm = (uint16_t)((rpmHi << 8) | rpmLo);
        Metrics->FanRpm[fanIdx] = (rpm != 0xFFFFu) ? rpm : 0u;
        Metrics->FanPwm[fanIdx] = SuperIoReadReg(Transport, hwmIdx, hwmDat, (uint8_t)(0x30u + fanIdx));
    }

    Metrics->Valid = 1u;
    return BC250_SUPERIO_STATUS_OK;
}

BC250_SUPERIO_STATUS Bc250Nct6686SetFanPwm(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t BaseAddress,
    uint8_t FanIndex,
    uint8_t PwmValue)
{
    uint16_t hwmIdx;
    uint16_t hwmDat;

    if (Transport == NULL || Transport->InByte == NULL || Transport->OutByte == NULL ||
        FanIndex >= BC250_NCT6686_MAX_FAN_COUNT) {
        return BC250_SUPERIO_STATUS_BAD_ARGUMENT;
    }
    if (BaseAddress == 0u) {
        BaseAddress = BC250_NCT6686_DEFAULT_BASE_ADDR;
    }

    hwmIdx = BaseAddress + 5u;
    hwmDat = BaseAddress + 6u;

    /* Seleciona Banco 3 (Controle de Fan PWM) */
    SuperIoWriteReg(Transport, hwmIdx, hwmDat, 0xFFu, 0x03u);
    SuperIoWriteReg(Transport, hwmIdx, hwmDat, (uint8_t)(0x30u + FanIndex), PwmValue);

    return BC250_SUPERIO_STATUS_OK;
}
