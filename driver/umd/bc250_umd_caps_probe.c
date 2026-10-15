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

BOOL Bc250UmdOpenAllocationArrayIsInfo2(UINT Interface)
{
    /* D3DWDDM2_0_DDI_MINOR_VERSION = 17 (d3d10umddi.h, WDK 10.0.28000). */
    return ((Interface >> 16) == 11u && (Interface & 0xFFFFu) >= 17u) ? TRUE : FALSE;
}

BOOL Bc250UmdTokenSequenceParse(const char* Text, LONG* Value)
{
    ULONG value = 0u;
    size_t digits = 0u;
    if (Value != NULL) *Value = 0;
    if (Text == NULL || Value == NULL) return FALSE;
    for (; Text[digits] != '\0'; ++digits) {
        char c = Text[digits];
        ULONG nibble;
        if (digits >= 8u) return FALSE;
        if (c >= '0' && c <= '9') nibble = (ULONG)(c - '0');
        else if (c >= 'A' && c <= 'F') nibble = (ULONG)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') nibble = (ULONG)(c - 'a' + 10);
        else return FALSE;
        value = (value << 4) | nibble;
    }
    if (digits == 0u) return FALSE;
    *Value = (LONG)value;
    return TRUE;
}

BOOL Bc250UmdTokenSequenceFormat(LONG Value, char* Text, size_t Size)
{
    static const char hex[] = "0123456789ABCDEF";
    ULONG value = (ULONG)Value;
    int i;
    if (Text == NULL || Size < 9u) return FALSE;
    for (i = 7; i >= 0; --i) {
        Text[i] = hex[value & 0xFu];
        value >>= 4;
    }
    Text[8] = '\0';
    return TRUE;
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

/* Frente B (doc 547) -- ver bc250_umd_caps_probe.h. */
const char* Bc250UmdFrenteBPreCheck(BOOL AllocCreated, UINT64 GpuVa, UINT64 AllocBytes,
    UINT MipLevels, UINT ArraySize, UINT SampleCount, BOOL DepthStencil, BOOL RenderTarget, BOOL OpenedSurface,
    BOOL PresentableTarget, UINT Width, UINT Height, UINT RowPitch)
{
    if (!AllocCreated || GpuVa == 0u || AllocBytes < 4096u) return "sem alocacao WDDM mapeada";
    if (MipLevels > 1u || ArraySize > 1u || SampleCount > 1u) return "mips/array/msaa (linear so' 1x1x1)";
    if (DepthStencil) return "depth-stencil nao tem layout linear";
    /* Corrida 190: com alias nos alvos de render do DWM, os buffers do swapchain
     * perderam o conteudo fora do que era redesenhado (6 de 61 Presents cheios,
     * contra 28 de 29 na 188). Mecanismo ainda H; render target volta a ter
     * memoria propria ate ele ser medido. */
    if (RenderTarget && !PresentableTarget) return "render target fica com memoria propria (Corrida 190)";
    /* Corridas 191 e 192: o MESMO IB (Fnv64 4092CF3C8621F809, shaders D86B7E93/59C609E5)
     * travou a GPU duas vezes lendo uma textura propria 256x80 com Alias=1. Mecanismo H;
     * o alias fica so' nas superficies abertas de outro processo, onde o ganho foi medido. */
    /* ZC.1 (doc 749): o render target apresentavel e' do proprio processo e
     * entra pelo alias; a regra das 191/192 segue para textura propria. */
    if (!OpenedSurface && !PresentableTarget) return "textura propria fica com memoria propria (Corridas 191/192)";
    if (Width == 0u || Height == 0u || RowPitch == 0u) return "geometria incompleta";
    if ((UINT64)RowPitch * (UINT64)Height > AllocBytes) return "pitch*altura maior que a alocacao";
    return NULL;
}

const char* Bc250UmdFrenteBLayoutCheck(UINT64 GpuVa, UINT64 AllocBytes, UINT RowPitch,
    UINT64 LayoutOffset, UINT64 LayoutRowPitch, UINT64 ReqSize, UINT64 ReqAlignment)
{
    const UINT64 pageBytes = AllocBytes & ~0xFFFull;
    if (LayoutOffset != 0u) return "imagem nao comeca no inicio da memoria";
    if (LayoutRowPitch != (UINT64)RowPitch) return "pitch da RADV difere do pitch da alocacao";
    if (ReqSize == 0u || ReqSize > pageBytes) return "imagem maior que a alocacao";
    if (ReqAlignment != 0u && (GpuVa % ReqAlignment) != 0u) return "GPUVA fora do alinhamento exigido";
    return NULL;
}