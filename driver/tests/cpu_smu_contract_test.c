#include <stdio.h>
#include <stdlib.h>
#include "../core/bc250_cpu_smu_contract.h"

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

int main(void)
{
    BC250_CPU_SMU_PACKET packet;
    BC250_CPU_SMU_STATUS status;

    printf("=== BC-250 CPU SMU CONTRACT TEST ===\n");

    /* 1. Test limits on boost frequency */
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(2999u) == BC250_CPU_SMU_ERROR_FREQ_TOO_LOW);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(0u) == BC250_CPU_SMU_ERROR_FREQ_TOO_LOW);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(4201u) == BC250_CPU_SMU_ERROR_FREQ_TOO_HIGH);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(5000u) == BC250_CPU_SMU_ERROR_FREQ_TOO_HIGH);

    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(3000u) == BC250_CPU_SMU_OK);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(3500u) == BC250_CPU_SMU_OK);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(4000u) == BC250_CPU_SMU_OK);
    ASSERT_TRUE(Bc250ValidateCpuBoostFrequency(4200u) == BC250_CPU_SMU_OK);

    /* 2. Test packet build for boost frequency */
    ASSERT_TRUE(Bc250BuildCpuBoostMessage(3500u, NULL) == BC250_CPU_SMU_ERROR_NULL_POINTER);
    ASSERT_TRUE(Bc250BuildCpuBoostMessage(2500u, &packet) == BC250_CPU_SMU_ERROR_FREQ_TOO_LOW);

    status = Bc250BuildCpuBoostMessage(4000u, &packet);
    ASSERT_TRUE(status == BC250_CPU_SMU_OK);
    ASSERT_TRUE(packet.QueueCmdReg == 0x03B10A20u);
    ASSERT_TRUE(packet.QueueRspReg == 0x03B10A80u);
    ASSERT_TRUE(packet.QueueArgReg == 0x03B10A88u);
    ASSERT_TRUE(packet.MessageId   == 0x8Fu);
    ASSERT_TRUE(packet.Argument    == 4000u);

    status = Bc250BuildCpuBoostMessage(4200u, &packet);
    ASSERT_TRUE(status == BC250_CPU_SMU_OK);
    ASSERT_TRUE(packet.Argument    == 4200u);

    /* 3. Test thermal limits */
    ASSERT_TRUE(Bc250ValidateCpuTempLimit(60u) == BC250_CPU_SMU_ERROR_TEMP_OUT_OF_RANGE);
    ASSERT_TRUE(Bc250ValidateCpuTempLimit(100u) == BC250_CPU_SMU_ERROR_TEMP_OUT_OF_RANGE);
    ASSERT_TRUE(Bc250ValidateCpuTempLimit(80u) == BC250_CPU_SMU_OK);

    status = Bc250BuildCpuTempLimitMessage(80u, &packet);
    ASSERT_TRUE(status == BC250_CPU_SMU_OK);
    ASSERT_TRUE(packet.MessageId == 0x8Bu);
    ASSERT_TRUE(packet.Argument  == 80u);

    printf("[PASS] All CPU SMU contract tests passed successfully (3.0 GHz - 4.2 GHz range verified).\n");
    return 0;
}
