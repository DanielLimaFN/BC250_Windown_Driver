#ifndef BC250_SMU_TELEMETRY_CONTRACT_H
#define BC250_SMU_TELEMETRY_CONTRACT_H

#include "../../inc/bc250_telemetry_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*BC250_SMU_READ32_FN)(void *Context, uint32_t ByteOffset);
typedef void (*BC250_SMU_WRITE32_FN)(void *Context, uint32_t ByteOffset, uint32_t Value);
typedef void (*BC250_SMU_STALL_US_FN)(void *Context, uint32_t Microseconds);

typedef struct BC250_SMU_TRANSPORT {
    BC250_SMU_READ32_FN Read32;
    BC250_SMU_WRITE32_FN Write32;
    BC250_SMU_STALL_US_FN StallUs;
    void *Context;
} BC250_SMU_TRANSPORT;

typedef enum BC250_SMU_STATUS {
    BC250_SMU_STATUS_OK = 0,
    BC250_SMU_STATUS_BAD_ARGUMENT,
    BC250_SMU_STATUS_TIMEOUT,
    BC250_SMU_STATUS_REJECTED,
    BC250_SMU_STATUS_BUSY,
    BC250_SMU_STATUS_HARDWARE_ERROR
} BC250_SMU_STATUS;

/* Envia mensagem generica ao SMU com argumento e aguarda resposta */
BC250_SMU_STATUS Bc250SmuSendMsgWithParam(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t MsgId,
    uint32_t Param,
    uint32_t *ResponseParam);

/* Handshake de Teste (TestMessage N -> N+1) */
BC250_SMU_STATUS Bc250SmuTestHandshake(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t TestValue,
    uint32_t *ReturnedValue);

/* Leitura de versoes */
BC250_SMU_STATUS Bc250SmuGetVersion(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *SmuVersion);

BC250_SMU_STATUS Bc250SmuGetDriverIfVersion(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *DriverIfVersion);

/* Leitura de frequencia e tensao GFX instantaneas */
BC250_SMU_STATUS Bc250SmuGetGfxFrequency(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *FrequencyMhz);

BC250_SMU_STATUS Bc250SmuGetGfxVid(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *VidMv);

/* Consulta de recursos DPM habilitados no SMU */
BC250_SMU_STATUS Bc250SmuGetEnabledFeatures(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *Features);

/* Requisicao de clock GFX (paridade amdgpu RequestGfxclk) */
BC250_SMU_STATUS Bc250SmuRequestGfxclk(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t FrequencyMhz);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SMU_TELEMETRY_CONTRACT_H */
