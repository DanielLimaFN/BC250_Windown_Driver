#ifndef BC250_SUPERIO_NCT6686_CONTRACT_H
#define BC250_SUPERIO_NCT6686_CONTRACT_H

#include "../../inc/bc250_telemetry_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*BC250_ISA_INBYTE_FN)(void *Context, uint16_t Port);
typedef void (*BC250_ISA_OUTBYTE_FN)(void *Context, uint16_t Port, uint8_t Value);

typedef struct BC250_ISA_PORT_TRANSPORT {
    BC250_ISA_INBYTE_FN InByte;
    BC250_ISA_OUTBYTE_FN OutByte;
    void *Context;
} BC250_ISA_PORT_TRANSPORT;

typedef enum BC250_SUPERIO_STATUS {
    BC250_SUPERIO_STATUS_OK = 0,
    BC250_SUPERIO_STATUS_BAD_ARGUMENT,
    BC250_SUPERIO_STATUS_CHIP_NOT_FOUND,
    BC250_SUPERIO_STATUS_PORT_ERROR
} BC250_SUPERIO_STATUS;

/* Detecta a presenca do Nuvoton NCT6686D e le seu endereco base */
BC250_SUPERIO_STATUS Bc250Nct6686Detect(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t *ChipId,
    uint16_t *BaseAddress);

/* Coleta leituras de sensores (temperaturas de VRM/CPU/Sistema, tensoes e ventoinhas) */
BC250_SUPERIO_STATUS Bc250Nct6686ReadSensors(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t BaseAddress,
    BC250_SUPERIO_NCT6686_METRICS *Metrics);

/* Ajusta o PWM de uma ventoinha especifica (0 a 255) */
BC250_SUPERIO_STATUS Bc250Nct6686SetFanPwm(
    const BC250_ISA_PORT_TRANSPORT *Transport,
    uint16_t BaseAddress,
    uint8_t FanIndex,
    uint8_t PwmValue);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SUPERIO_NCT6686_CONTRACT_H */
