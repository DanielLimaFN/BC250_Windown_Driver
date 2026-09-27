#include "bc250_driver_caps_contract.h"

/*
 * A1 (06/09). Ver o header para a proveniencia campo a campo. Aqui so a tabela.
 *
 * A tabela e' deliberadamente literal: nada e' derivado em runtime, para que a
 * auditoria e o teste offline possam comparar valor a valor com a tabela de
 * referencias/windows-driver-docs/.../display/wddm-driver-and-feature-caps.md.
 */

BC250_DRIVER_CAPS_STATUS Bc250GetDriverCapsPolicy(
    BC250_WDDM_DRIVER_TYPE DriverType,
    BC250_DRIVER_CAPS_POLICY *Policy)
{
    if (Policy == NULL) {
        return BC250_DRIVER_CAPS_BAD_ARGUMENT;
    }

    Policy->SupportNonVGA = 0u;
    Policy->SupportSmoothRotation = 0u;
    Policy->SupportDirectFlip = 0u;
    Policy->SupportPerEngineTDR = 0u;
    Policy->FlipOnVSyncMmIo = 0u;

    switch (DriverType) {
    case BC250_WDDM_DRIVER_DISPLAY_ONLY:
        /* PnP start/stop de non-VGA: M tambem para Display Only. */
        Policy->SupportNonVGA = 1u;
        /* Sem modo rotacionado -> ver justificativa no header. */
        Policy->SupportSmoothRotation = 0u;
        /* DirectFlip e' NA para Display Only. */
        Policy->SupportDirectFlip = 0u;
        /* TDR per-engine e' NA para Display Only. */
        Policy->SupportPerEngineTDR = 0u;
        /* FlipOnVSyncMmIo e' NA para Display Only. */
        Policy->FlipOnVSyncMmIo = 0u;
        return BC250_DRIVER_CAPS_OK;

    case BC250_WDDM_DRIVER_FULL_GRAPHICS:
        Policy->SupportNonVGA = 1u;
        Policy->SupportSmoothRotation = 0u;
        Policy->SupportDirectFlip = 0u;
        Policy->SupportPerEngineTDR = 1u;
        Policy->FlipOnVSyncMmIo = 1u;
        return BC250_DRIVER_CAPS_OK;

    default:
        return BC250_DRIVER_CAPS_INVALID_TYPE;
    }
}

uint32_t Bc250GetDriverCapsDeclaredDebtCount(BC250_WDDM_DRIVER_TYPE DriverType)
{
    switch (DriverType) {
    case BC250_WDDM_DRIVER_DISPLAY_ONLY:
        /*
         * Nenhuma divida: SupportSmoothRotation esta dispensado por nao haver
         * modo rotacionado, e DirectFlip/TDR/Flip sao NA neste perfil.
         */
        return 0u;

    case BC250_WDDM_DRIVER_FULL_GRAPHICS:
        /*
         * Uma divida declarada: SupportDirectFlip. A documentacao marca
         * Mandatory e nos nao temos backend (exigiria escrita de scanout no
         * DCN, proibida fora da P9).
         */
        return 1u;

    default:
        return 0u;
    }
}
