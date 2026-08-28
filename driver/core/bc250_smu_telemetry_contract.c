#include "bc250_smu_telemetry_contract.h"

#define BC250_SMU_TIMEOUT_US        5000u /* 5 ms max timeout */
#define BC250_SMU_POLL_INTERVAL_US  5u

static BC250_SMU_STATUS Bc250SmuWaitResponse(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *ResponseStatus)
{
    uint32_t elapsedUs = 0u;
    uint32_t resp = 0u;

    while (elapsedUs < BC250_SMU_TIMEOUT_US) {
        resp = Transport->Read32(Transport->Context, BC250_SMU_MMIO_C2PMSG_66);
        if (resp != 0u) {
            if (ResponseStatus != NULL) {
                *ResponseStatus = resp;
            }
            if (resp == BC250_SMU_RESP_OK) {
                return BC250_SMU_STATUS_OK;
            }
            if (resp == BC250_SMU_RESP_CMD_REJECT || resp == BC250_SMU_RESP_GUARD_REJECT) {
                return BC250_SMU_STATUS_REJECTED;
            }
            if (resp == BC250_SMU_RESP_BUSY) {
                return BC250_SMU_STATUS_BUSY;
            }
            return BC250_SMU_STATUS_HARDWARE_ERROR;
        }
        if (Transport->StallUs != NULL) {
            Transport->StallUs(Transport->Context, BC250_SMU_POLL_INTERVAL_US);
        }
        elapsedUs += BC250_SMU_POLL_INTERVAL_US;
    }

    return BC250_SMU_STATUS_TIMEOUT;
}

BC250_SMU_STATUS Bc250SmuSendMsgWithParam(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t MsgId,
    uint32_t Param,
    uint32_t *ResponseParam)
{
    BC250_SMU_STATUS status;
    uint32_t respStatus = 0u;

    if (Transport == NULL || Transport->Read32 == NULL || Transport->Write32 == NULL) {
        return BC250_SMU_STATUS_BAD_ARGUMENT;
    }

    /* 1. Limpa status anterior */
    Transport->Write32(Transport->Context, BC250_SMU_MMIO_C2PMSG_66, 0u);

    /* 2. Escreve argumento */
    Transport->Write32(Transport->Context, BC250_SMU_MMIO_C2PMSG_82, Param);

    /* 3. Dispara a mensagem */
    Transport->Write32(Transport->Context, BC250_SMU_MMIO_C2PMSG_67, MsgId);

    /* 4. Aguarda confirmacao do SMU */
    status = Bc250SmuWaitResponse(Transport, &respStatus);
    if (status != BC250_SMU_STATUS_OK) {
        return status;
    }

    /* 5. Le o resultado retornado no registrador de argumento */
    if (ResponseParam != NULL) {
        *ResponseParam = Transport->Read32(Transport->Context, BC250_SMU_MMIO_C2PMSG_82);
    }

    return BC250_SMU_STATUS_OK;
}

BC250_SMU_STATUS Bc250SmuTestHandshake(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t TestValue,
    uint32_t *ReturnedValue)
{
    uint32_t resp = 0u;
    BC250_SMU_STATUS status = Bc250SmuSendMsgWithParam(
        Transport, BC250_PPSMC_MSG_TestMessage, TestValue, &resp);

    if (status != BC250_SMU_STATUS_OK) {
        return status;
    }

    if (ReturnedValue != NULL) {
        *ReturnedValue = resp;
    }

    /* TestMessage deve retornar TestValue + 1 */
    if (resp != (TestValue + 1u)) {
        return BC250_SMU_STATUS_HARDWARE_ERROR;
    }

    return BC250_SMU_STATUS_OK;
}

BC250_SMU_STATUS Bc250SmuGetVersion(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *SmuVersion)
{
    return Bc250SmuSendMsgWithParam(
        Transport, BC250_PPSMC_MSG_GetSmuVersion, 0u, SmuVersion);
}

BC250_SMU_STATUS Bc250SmuGetDriverIfVersion(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *DriverIfVersion)
{
    return Bc250SmuSendMsgWithParam(
        Transport, BC250_PPSMC_MSG_GetDriverIfVersion, 0u, DriverIfVersion);
}

BC250_SMU_STATUS Bc250SmuGetGfxFrequency(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *FrequencyMhz)
{
    return Bc250SmuSendMsgWithParam(
        Transport, BC250_PPSMC_MSG_GetGfxFrequency, 0u, FrequencyMhz);
}

BC250_SMU_STATUS Bc250SmuGetGfxVid(
    const BC250_SMU_TRANSPORT *Transport,
    uint32_t *VidMv)
{
    return Bc250SmuSendMsgWithParam(
        Transport, BC250_PPSMC_MSG_GetGfxVid, 0u, VidMv);
}
