#ifndef BC250_CPU_SMU_CONTRACT_H
#define BC250_CPU_SMU_CONTRACT_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stdint.h>
#endif

#define BC250_CPU_MIN_FREQ_MHZ          3000u /* 3.0 GHz */
#define BC250_CPU_MAX_FREQ_MHZ          4200u /* 4.2 GHz */
#define BC250_CPU_DEFAULT_FREQ_MHZ      3500u /* 3.5 GHz (Base PS5/Oberon) */
#define BC250_CPU_SAFE_BOOST_FREQ_MHZ   4000u /* 4.0 GHz (Comprovado no silicio pela comunidade) */

#define BC250_CPU_MIN_TEMP_LIMIT_C      65u
#define BC250_CPU_MAX_TEMP_LIMIT_C      95u
#define BC250_CPU_DEFAULT_TEMP_LIMIT_C  80u

/* Enderecos fisicos e comandos de SMU Mailbox (Q3 / Q0) validados */
#define BC250_SMU_Q3_CMD_REG            0x03B10A20u
#define BC250_SMU_Q3_RSP_REG            0x03B10A80u
#define BC250_SMU_Q3_ARG_REG            0x03B10A88u

#define BC250_SMU_MSG_SCALE_F_VID_CURVE 0x50u /* Q3: Curva de voltagem/frequencia */
#define BC250_SMU_MSG_SET_CPU_MAX_TEMP  0x8Bu /* Q3: Limite termico CPU em C */
#define BC250_SMU_MSG_SET_GPU_MAX_TEMP  0x8Cu /* Q3: Limite termico GPU em C */
#define BC250_SMU_MSG_SET_MAX_CPU_BOOST 0x8Fu /* Q3: Frequencia maxima de boost da CPU em MHz */

typedef enum BC250_CPU_SMU_STATUS {
    BC250_CPU_SMU_OK = 0,
    BC250_CPU_SMU_ERROR_NULL_POINTER,
    BC250_CPU_SMU_ERROR_FREQ_TOO_LOW,
    BC250_CPU_SMU_ERROR_FREQ_TOO_HIGH,
    BC250_CPU_SMU_ERROR_TEMP_OUT_OF_RANGE,
    BC250_CPU_SMU_ERROR_UNSUPPORTED_MSG
} BC250_CPU_SMU_STATUS;

typedef struct BC250_CPU_SMU_PACKET {
    uint32_t QueueCmdReg;
    uint32_t QueueRspReg;
    uint32_t QueueArgReg;
    uint32_t MessageId;
    uint32_t Argument;
} BC250_CPU_SMU_PACKET;

#ifdef __cplusplus
extern "C" {
#endif

BC250_CPU_SMU_STATUS Bc250ValidateCpuBoostFrequency(uint32_t FreqMhz);
BC250_CPU_SMU_STATUS Bc250ValidateCpuTempLimit(uint32_t TempC);

BC250_CPU_SMU_STATUS Bc250BuildCpuBoostMessage(
    uint32_t FreqMhz,
    BC250_CPU_SMU_PACKET *OutPacket);

BC250_CPU_SMU_STATUS Bc250BuildCpuTempLimitMessage(
    uint32_t TempC,
    BC250_CPU_SMU_PACKET *OutPacket);

#ifdef __cplusplus
}
#endif

#endif /* BC250_CPU_SMU_CONTRACT_H */
