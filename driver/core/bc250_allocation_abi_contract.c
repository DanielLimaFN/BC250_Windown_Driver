#include "bc250_allocation_abi_contract.h"

#include <string.h>

static int Bc250AllocationTokenIsUmdClass(uint64_t AllocationToken);

/*
 * Coerencia da geometria. Usada na formatacao E no parse, para que o KMD nunca
 * aceite um blob que a UMD nao produziria -- fonte unica da regra.
 *
 * A checagem forte e' ByteSize >= Pitch * Height: e' a unica que amarra a
 * geometria ao tamanho realmente alocado. Sem ela, um Pitch errado viraria uma
 * descricao plausivel e o erro so' apareceria como imagem torta no monitor --
 * exatamente a classe de defeito do stride skew de 10/09.
 */
static int Bc250AllocationGeometryIsCoherent(
    const BC250_ALLOCATION_GEOMETRY *Geometry,
    uint64_t ByteSize)
{
    uint64_t needed;

    if (Geometry == NULL) return 0;
    if (Geometry->Width == 0u || Geometry->Height == 0u ||
        Geometry->Pitch == 0u || Geometry->Format == 0u) {
        return 0;
    }
    /* Pitch tem de comportar a largura: 1 byte por pixel e' o piso absoluto. */
    if ((uint64_t)Geometry->Pitch < (uint64_t)Geometry->Width) {
        return 0;
    }
    needed = (uint64_t)Geometry->Pitch * (uint64_t)Geometry->Height;
    if (needed == 0u || needed > ByteSize) {
        return 0;
    }
    return 1;
}

int Bc250AllocationHasGeometry(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    if (Request == NULL) return 0;
    if ((Request->Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) == 0u) return 0;
    return Bc250AllocationGeometryIsCoherent(&Request->Geometry, Request->ByteSize);
}

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiFormat(
    uint64_t AllocationToken,
    uint64_t ByteSize,
    uint64_t Alignment,
    uint32_t Flags,
    const BC250_ALLOCATION_GEOMETRY *Geometry,
    BC250_ALLOCATION_PRIVATE_V1 *Output)
{
    if (Output != NULL) {
        memset(Output, 0, sizeof(*Output));
    }
    if (Output == NULL) {
        return BC250_ALLOCATION_ABI_BAD_ARGUMENT;
    }
    if (AllocationToken == 0u || ByteSize == 0u) {
        return BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    if ((Flags & ~BC250_ALLOCATION_SUPPORTED_FLAGS) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    /*
     * A flag e' DERIVADA do ponteiro, nunca aceita do chamador: declarar
     * HAS_GEOMETRY sem geometria produziria um blob que o parse aceita e cujo
     * conteudo e' zero -- uma descricao vazia apresentada como valida.
     */
    if ((Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    /* M1: GPU_VA_ONLY so' na classe RESOURCE (ver o cabecalho). */
    if ((Flags & BC250_ALLOCATION_FLAG_GPU_VA_ONLY) != 0u &&
        BC250_ALLOCATION_TOKEN_CLASS(AllocationToken) !=
            BC250_ALLOCATION_TOKEN_CLASS_RESOURCE) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    /* M1b.1: flags da RADV so' em token da RADV (ver o cabecalho). */
    if ((Flags & BC250_ALLOCATION_RADV_ONLY_FLAGS) != 0u &&
        Bc250AllocationTokenIsUmdClass(AllocationToken)) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    if (Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (Alignment & (Alignment - 1u)) != 0u ||
        (ByteSize & (Alignment - 1u)) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_ALIGNMENT;
    }
    if (Geometry != NULL &&
        !Bc250AllocationGeometryIsCoherent(Geometry, ByteSize)) {
        return BC250_ALLOCATION_ABI_BAD_GEOMETRY;
    }
    Output->Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
    Output->Version = BC250_ALLOCATION_PRIVATE_VERSION;
    Output->StructSize = (uint32_t)sizeof(*Output);
    Output->Flags = Flags;
    Output->AllocationToken = AllocationToken;
    Output->ByteSize = ByteSize;
    Output->Alignment = Alignment;
    if (Geometry != NULL) {
        Output->Geometry = *Geometry;
        Output->Flags |= BC250_ALLOCATION_FLAG_HAS_GEOMETRY;
    }
    return BC250_ALLOCATION_ABI_OK;
}

BC250_ALLOCATION_ABI_STATUS Bc250AllocationAbiParse(
    const void *PrivateData,
    uint32_t PrivateDataSize,
    BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    BC250_ALLOCATION_PRIVATE_V1 candidate;

    if (Request != NULL) memset(Request, 0, sizeof(*Request));
    if (PrivateData == NULL || Request == NULL) {
        return BC250_ALLOCATION_ABI_BAD_ARGUMENT;
    }
    if (PrivateDataSize != sizeof(candidate) && PrivateDataSize != BC250_ALLOCATION_V1_SIZE) {
        return BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    memset(&candidate, 0, sizeof(candidate));
    if (PrivateDataSize == BC250_ALLOCATION_V1_SIZE) {
        memcpy(&candidate, PrivateData, BC250_ALLOCATION_V1_SIZE);
        if (candidate.Version != BC250_ALLOCATION_PRIVATE_VERSION_1 ||
            candidate.StructSize != BC250_ALLOCATION_V1_SIZE) {
            return BC250_ALLOCATION_ABI_BAD_VERSION;
        }
    } else {
        memcpy(&candidate, PrivateData, sizeof(candidate));
        if (candidate.Version != BC250_ALLOCATION_PRIVATE_VERSION_2 ||
            candidate.StructSize != sizeof(candidate)) {
            return BC250_ALLOCATION_ABI_BAD_VERSION;
        }
    }
    if (candidate.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_ALLOCATION_ABI_BAD_MAGIC;
    }
    if ((candidate.Flags & ~BC250_ALLOCATION_SUPPORTED_FLAGS) != 0u ||
        candidate.AllocationToken == 0u ||
        candidate.ByteSize == 0u) {
        return ((candidate.Flags & ~BC250_ALLOCATION_SUPPORTED_FLAGS) != 0u) ?
            BC250_ALLOCATION_ABI_BAD_FLAGS : BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    /* M1: mesma regra da formatacao -- GPU_VA_ONLY fora de RESOURCE e' recusada. */
    if ((candidate.Flags & BC250_ALLOCATION_FLAG_GPU_VA_ONLY) != 0u &&
        BC250_ALLOCATION_TOKEN_CLASS(candidate.AllocationToken) !=
            BC250_ALLOCATION_TOKEN_CLASS_RESOURCE) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    /* M1b.1: mesma regra da formatacao -- flag da RADV em classe da UMD e' recusada. */
    if ((candidate.Flags & BC250_ALLOCATION_RADV_ONLY_FLAGS) != 0u &&
        Bc250AllocationTokenIsUmdClass(candidate.AllocationToken)) {
        return BC250_ALLOCATION_ABI_BAD_FLAGS;
    }
    if (candidate.Alignment < BC250_ALLOCATION_MIN_ALIGNMENT ||
        candidate.Alignment > BC250_ALLOCATION_MAX_ALIGNMENT ||
        (candidate.Alignment & (candidate.Alignment - 1u)) != 0u ||
        (candidate.ByteSize & (candidate.Alignment - 1u)) != 0u) {
        return BC250_ALLOCATION_ABI_BAD_ALIGNMENT;
    }
    /*
     * Mesma regra de coerencia da formatacao. Um blob que declare geometria e
     * traga campos incoerentes e' RECUSADO aqui, e nao aceito para virar uma
     * descricao errada em DescribeAllocation depois.
     */
    if ((candidate.Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) != 0u &&
        !Bc250AllocationGeometryIsCoherent(&candidate.Geometry, candidate.ByteSize)) {
        return BC250_ALLOCATION_ABI_BAD_GEOMETRY;
    }
    *Request = candidate;
    return BC250_ALLOCATION_ABI_OK;
}

int Bc250AllocationIsPrimary(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    return Request != NULL &&
        BC250_ALLOCATION_TOKEN_CLASS(Request->AllocationToken) ==
            BC250_ALLOCATION_TOKEN_CLASS_PRIMARY;
}

int Bc250AllocationRequiresCpuAccess(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    uint32_t tokenClass;
    if (Request == NULL) return 0;
    if ((Request->Flags & BC250_ALLOCATION_FLAG_CPU_VISIBLE) != 0u) {
        return 1;
    }
    tokenClass = BC250_ALLOCATION_TOKEN_CLASS(Request->AllocationToken);
    if (tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PRIMARY ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_SHADOW ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_STAGING ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_GDI ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_COMMAND ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PROOF) {
        return 1;
    }
    /*
     * DX.89: a classe RESOURCE deixou de exigir CPU pelo token; quem decide e'
     * a flag BC250_ALLOCATION_FLAG_CPU_VISIBLE, tratada acima. A UMD continua
     * mandando a flag para recursos comuns e a omite para recursos
     * compartilhados (MISC_SHARED). Na Corrida 138 a UMD ja' omitia a flag e o
     * KMD devolveu CpuVisible=1 mesmo assim, por esta regra de classe.
     */
    return 0;
}

int Bc250AllocationMayUseSystemSegment(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    return Request != NULL &&
        (Request->Flags & BC250_ALLOCATION_FLAG_GPU_VA_ONLY) != 0u &&
        BC250_ALLOCATION_TOKEN_CLASS(Request->AllocationToken) ==
            BC250_ALLOCATION_TOKEN_CLASS_RESOURCE;
}


static int Bc250AllocationTokenIsUmdClass(uint64_t AllocationToken)
{
    const uint32_t tokenClass = BC250_ALLOCATION_TOKEN_CLASS(AllocationToken);
    return tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PRIMARY ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_SHADOW ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_STAGING ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_GDI ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_COMMAND ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_PROOF ||
        tokenClass == BC250_ALLOCATION_TOKEN_CLASS_RESOURCE;
}

int Bc250AllocationMayUseHiddenSegment(
    const BC250_ALLOCATION_PRIVATE_V1 *Request)
{
    if (Request == NULL) return 0;
    if ((Request->Flags & (BC250_ALLOCATION_FLAG_CPU_VISIBLE |
                           BC250_ALLOCATION_FLAG_GPU_VA_ONLY)) != 0u) {
        return 0;
    }
    return Bc250AllocationTokenIsUmdClass(Request->AllocationToken) ? 0 : 1;
}

int Bc250AllocationLockNeedsApertureFallback(
    const BC250_ALLOCATION_PRIVATE_V1 *Request,
    int CpuVisible)
{
    if (Request == NULL || !CpuVisible) return 0;
    return Bc250AllocationTokenIsUmdClass(Request->AllocationToken) ? 0 : 1;
}

int Bc250AllocationRadvSystemSegmentPolicy(
    const BC250_ALLOCATION_PRIVATE_V1 *Request,
    int CpuVisible,
    BC250_ALLOCATION_SEGMENT_POLICY *Out)
{
    if (Out != NULL) memset(Out, 0, sizeof(*Out));
    if (Request == NULL || Out == NULL) return 0;
    if (Bc250AllocationTokenIsUmdClass(Request->AllocationToken)) return 0;
    if ((Request->Flags & BC250_ALLOCATION_FLAG_RADV_COMMAND) != 0u) return 0;
    if ((Request->Flags & BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT) != 0u) {
        Out->SegmentSet = 0x3u;
        Out->Preferred0 = 2u;
        Out->Preferred1 = 1u;
        return 1;
    }
    if (CpuVisible) return 0;
    Out->SegmentSet = 0x3u;
    Out->Preferred0 = 1u;
    Out->Preferred1 = 2u;
    return 1;
}
