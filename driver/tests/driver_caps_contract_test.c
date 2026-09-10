#include "../core/bc250_driver_caps_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 0; } } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

/*
 * A1 -- trava da tabela de caps por tipo de driver.
 *
 * Este teste existe porque a divergencia entre os dois handlers de
 * DXGK_DRIVERCAPS passou meses sem ser notada: nenhum teste comparava os dois
 * caminhos, e a exigencia da Microsoft nunca esteve codificada. Cada assercao
 * abaixo corresponde a uma linha de
 * referencias/windows-driver-docs/.../display/wddm-driver-and-feature-caps.md.
 *
 * O teste NAO prova comportamento em hardware. Ele prova que o valor declarado
 * e' o valor decidido, e que ninguem muda um cap sem passar por aqui.
 */

static int TestFullGraphicsPolicy(void)
{
    BC250_DRIVER_CAPS_POLICY policy;

    CHECK(Bc250GetDriverCapsPolicy(
        BC250_WDDM_DRIVER_FULL_GRAPHICS, &policy) == BC250_DRIVER_CAPS_OK);

    /* M: PnP start/stop de non-VGA. Era o campo que o handler WDDM zerava. */
    CHECK(policy.SupportNonVGA == 1u);

    /* M condicional -- dispensado por nao existir modo rotacionado. */
    CHECK(policy.SupportSmoothRotation == 0u);

    /*
     * M sem backend -- DIVIDA DECLARADA. Se algum dia isto virar 1, tem de vir
     * junto com o flip real do endereco de scanout (P9), senao e' cap fake.
     */
    CHECK(policy.SupportDirectFlip == 0u);

    /* M para Full Graphics. */
    CHECK(policy.SupportPerEngineTDR == 1u);
    CHECK(policy.FlipOnVSyncMmIo == 1u);

    /* Exatamente uma divida declarada neste perfil: DirectFlip. */
    CHECK(Bc250GetDriverCapsDeclaredDebtCount(
        BC250_WDDM_DRIVER_FULL_GRAPHICS) == 1u);
    return 0;
}

static int TestDisplayOnlyPolicy(void)
{
    BC250_DRIVER_CAPS_POLICY policy;

    CHECK(Bc250GetDriverCapsPolicy(
        BC250_WDDM_DRIVER_DISPLAY_ONLY, &policy) == BC250_DRIVER_CAPS_OK);

    /* M tambem no perfil Display Only -- e o KMDOD ja declarava. */
    CHECK(policy.SupportNonVGA == 1u);
    CHECK(policy.SupportSmoothRotation == 0u);

    /* NA no perfil Display Only. */
    CHECK(policy.SupportDirectFlip == 0u);
    CHECK(policy.SupportPerEngineTDR == 0u);
    CHECK(policy.FlipOnVSyncMmIo == 0u);

    CHECK(Bc250GetDriverCapsDeclaredDebtCount(
        BC250_WDDM_DRIVER_DISPLAY_ONLY) == 0u);
    return 0;
}

static int TestBothPathsAgreeOnNonVga(void)
{
    BC250_DRIVER_CAPS_POLICY full;
    BC250_DRIVER_CAPS_POLICY dod;

    CHECK(Bc250GetDriverCapsPolicy(
        BC250_WDDM_DRIVER_FULL_GRAPHICS, &full) == BC250_DRIVER_CAPS_OK);
    CHECK(Bc250GetDriverCapsPolicy(
        BC250_WDDM_DRIVER_DISPLAY_ONLY, &dod) == BC250_DRIVER_CAPS_OK);

    /*
     * O defeito original em uma assercao: os dois perfis divergiam em
     * SupportNonVGA. A exigencia e' M nos dois, logo tem de bater.
     */
    CHECK(full.SupportNonVGA == dod.SupportNonVGA);
    CHECK(full.SupportSmoothRotation == dod.SupportSmoothRotation);
    return 0;
}

static int TestBadArguments(void)
{
    BC250_DRIVER_CAPS_POLICY policy;

    CHECK(Bc250GetDriverCapsPolicy(
        BC250_WDDM_DRIVER_FULL_GRAPHICS, NULL) ==
        BC250_DRIVER_CAPS_BAD_ARGUMENT);
    CHECK(Bc250GetDriverCapsPolicy(
        (BC250_WDDM_DRIVER_TYPE)99, &policy) ==
        BC250_DRIVER_CAPS_INVALID_TYPE);

    /* Tipo invalido nao deve deixar campo ligado por acidente. */
    CHECK(policy.SupportNonVGA == 0u);
    CHECK(policy.SupportPerEngineTDR == 0u);
    CHECK(policy.FlipOnVSyncMmIo == 0u);
    return 0;
}

static int Bc250RunDriverCapsContractTest(void)
{
    if (TestFullGraphicsPolicy() != 0) return 1;
    if (TestDisplayOnlyPolicy() != 0) return 1;
    if (TestBothPathsAgreeOnNonVga() != 0) return 1;
    if (TestBadArguments() != 0) return 1;
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunDriverCapsContractTest());
}
#else
int main(void)
{
    int failed = Bc250RunDriverCapsContractTest();
    if (failed == 0) {
        printf("driver_caps_contract_test: PASS\n");
    }
    return failed;
}
#endif
