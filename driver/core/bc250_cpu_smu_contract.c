#include "bc250_cpu_smu_contract.h"

BC250_CPU_SMU_STATUS Bc250ValidateCpuBoostFrequency(uint32_t FreqMhz)
{
    if (FreqMhz < BC250_CPU_MIN_FREQ_MHZ) {
        return BC250_CPU_SMU_ERROR_FREQ_TOO_LOW;
    }
    if (FreqMhz > BC250_CPU_MAX_FREQ_MHZ) {
        return BC250_CPU_SMU_ERROR_FREQ_TOO_HIGH;
    }
    return BC250_CPU_SMU_OK;
}

BC250_CPU_SMU_STATUS Bc250ValidateCpuTempLimit(uint32_t TempC)
{
    if (TempC < BC250_CPU_MIN_TEMP_LIMIT_C || TempC > BC250_CPU_MAX_TEMP_LIMIT_C) {
        return BC250_CPU_SMU_ERROR_TEMP_OUT_OF_RANGE;
    }
    return BC250_CPU_SMU_OK;
}

BC250_CPU_SMU_STATUS Bc250BuildCpuBoostMessage(
    uint32_t FreqMhz,
    BC250_CPU_SMU_PACKET *OutPacket)
{
    BC250_CPU_SMU_STATUS status;

    if (OutPacket == 0) {
        return BC250_CPU_SMU_ERROR_NULL_POINTER;
    }

    status = Bc250ValidateCpuBoostFrequency(FreqMhz);
    if (status != BC250_CPU_SMU_OK) {
        return status;
    }

    OutPacket->QueueCmdReg = BC250_SMU_Q3_CMD_REG;
    OutPacket->QueueRspReg = BC250_SMU_Q3_RSP_REG;
    OutPacket->QueueArgReg = BC250_SMU_Q3_ARG_REG;
    OutPacket->MessageId   = BC250_SMU_MSG_SET_MAX_CPU_BOOST;
    OutPacket->Argument    = FreqMhz;

    return BC250_CPU_SMU_OK;
}

BC250_CPU_SMU_STATUS Bc250BuildCpuTempLimitMessage(
    uint32_t TempC,
    BC250_CPU_SMU_PACKET *OutPacket)
{
    BC250_CPU_SMU_STATUS status;

    if (OutPacket == 0) {
        return BC250_CPU_SMU_ERROR_NULL_POINTER;
    }

    status = Bc250ValidateCpuTempLimit(TempC);
    if (status != BC250_CPU_SMU_OK) {
        return status;
    }

    OutPacket->QueueCmdReg = BC250_SMU_Q3_CMD_REG;
    OutPacket->QueueRspReg = BC250_SMU_Q3_RSP_REG;
    OutPacket->QueueArgReg = BC250_SMU_Q3_ARG_REG;
    OutPacket->MessageId   = BC250_SMU_MSG_SET_CPU_MAX_TEMP;
    OutPacket->Argument    = TempC;

    return BC250_CPU_SMU_OK;
}
