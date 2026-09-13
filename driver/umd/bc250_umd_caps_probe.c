#include "bc250_umd_caps_probe.h"
#include <string.h>

BOOL Bc250UmdShouldPublishAdapter(BOOL NativeReady, BOOL ProbeArmed)
{
    return (NativeReady || ProbeArmed) ? TRUE : FALSE;
}

static BOOL Bc250UmdTokenIsHex128(const char* Token)
{
    UINT index;
    if (Token == NULL || strlen(Token) != BC250_NODE1_TRIPOD_TOKEN_CHARS) {
        return FALSE;
    }
    for (index = 0; index < BC250_NODE1_TRIPOD_TOKEN_CHARS; ++index) {
        char c = Token[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL Bc250UmdNode1TripodAuthorize(
    BOOL Armed,
    const char* ProcessImageName,
    const char* RegistryToken,
    const char* EnvironmentToken)
{
    if (!Armed || ProcessImageName == NULL ||
        _stricmp(ProcessImageName, BC250_NODE1_TRIPOD_HARNESS_EXE) != 0 ||
        !Bc250UmdTokenIsHex128(RegistryToken) ||
        !Bc250UmdTokenIsHex128(EnvironmentToken)) {
        return FALSE;
    }
    return (_stricmp(RegistryToken, EnvironmentToken) == 0) ? TRUE : FALSE;
}

BOOL Bc250UmdProbeAcceptsInterface(UINT Interface)
{
    /*
     * Allowlist de UM item. Nao e' conservadorismo: e' a unica versao cujo
     * alvo esta dimensionado (105 entradas de D3D10_1DDI_DEVICEFUNCS). Aceitar
     * qualquer outra seria declarar capacidade que nao existe -- e foi
     * exatamente esse erro que a corrida 23:45 mediu.
     */
    return (Interface == BC250_UMD_PROBE_IFACE_D3D10_1) ? TRUE : FALSE;
}

BOOL Bc250UmdProbeAcceptsInterface10_2(UINT Interface)
{
    /*
     * Somente D3D11_0_7. E' o DDI de D3D11 mais antigo que o runtime pede, e o
     * unico caminho medido para o pfnGetCaps existir. O minor 17 fica fora por
     * nao ter nome no WDK atual.
     */
    return (Interface == BC250_UMD_PROBE_IFACE_D3D11_0_7) ? TRUE : FALSE;
}

BOOL Bc250UmdProbeAcceptsInterface10(UINT Interface)
{
    /* A tabela de 3 entradas so' faz sentido para o DDI de D3D10.1. */
    return (Interface == BC250_UMD_PROBE_IFACE_D3D10_1) ? TRUE : FALSE;
}

BOOL Bc250Umd3DPipelineIsDeclared(BOOL NativeReady, BOOL LabPromotionArmed)
{
    if (NativeReady) return TRUE;
    return LabPromotionArmed ? TRUE : FALSE;
}

BOOL Bc250UmdDrawBltSubstitutionIsLawful(
    BOOL SubstitutionArmed,
    BOOL WinsysReady,
    BOOL SourceResident,
    BOOL DestResident,
    BOOL SameDimensions,
    BOOL SameFormat,
    BOOL ViewportCoversFullSurface,
    BOOL BlendEnabled)
{
    /*
     * Fail-closed por construcao: qualquer fato ausente recusa. O tunable vem
     * primeiro porque, desarmado, nem se avalia elegibilidade -- o default de
     * fabrica tem de ser identico ao comportamento anterior a C1.
     */
    if (!SubstitutionArmed) return FALSE;

    /*
     * Sem winsys nao existe caminho de GPU. Aceitar aqui reabriria a porta do
     * fallback de CPU, que e' exatamente o defeito que a C1 fechou.
     */
    if (!WinsysReady) return FALSE;

    if (!SourceResident || !DestResident) return FALSE;
    if (!SameDimensions || !SameFormat) return FALSE;
    if (!ViewportCoversFullSurface) return FALSE;

    /*
     * Nota sobre blend: sob substituicao armada declarada (arm-d3d11-draw-blt-substitution.ps1),
     * shader, vertex buffer, index buffer, topologia e blend sao IGNORADOS.
     * O SDMA executa a copia 1:1 de superficie diretamente em VRAM. O flag BlendEnabled
     * e' mantido na assinatura para telemetria/rastreio sem quebrar o contrato.
     */
    (void)BlendEnabled;

    return TRUE;
}

BOOL Bc250UmdCapsZeroIsTruthful(UINT CapsType)
{
    switch (CapsType) {
    case 130u: /* D3D11DDICAPS_3DPIPELINESUPPORT */
        /*
         * O payload e' um D3D11DDI_3DPIPELINELEVEL, e 0 == 3DPIPELINELEVEL_10_0.
         * Zerar aqui declararia um pipeline 3D que nao existe. Lei 4.
         */
        return FALSE;
    default:
        /* Conjuntos de flags: tudo zero == nada suportado == verdade. */
        return TRUE;
    }
}

const char* Bc250UmdCapsTypeName(UINT Type)
{
    /*
     * Valores de D3D10_2DDICAPS_TYPE lidos de
     * Windows Kits/10/Include/<ver>/um/d3d10umddi.h. Os tres primeiros sao os
     * que o runtime consulta ANTES de pedir o device -- se sairem do log como
     * "nao catalogado", a corrida do probe se perde.
     */
    switch (Type) {
    case 128u: return "D3D11DDICAPS_THREADING";
    case 129u: return "D3D11DDICAPS_SHADER";
    case 130u: return "D3D11DDICAPS_3DPIPELINESUPPORT";
    case 131u: return "D3D11_1DDICAPS_D3D11_OPTIONS";
    case 132u: return "D3D11_1DDICAPS_ARCHITECTURE_INFO";
    case 134u: return "D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT";
    case 136u: return "D3DWDDM1_3DDICAPS_D3D11_OPTIONS1";
    case 137u: return "D3DWDDM1_3DDICAPS_MARKER";
    case 143u: return "D3DWDDM2_0DDICAPS_D3D11_OPTIONS2";
    default:   return "(tipo nao catalogado)";
    }
}
