#include "bc250_allocation_abi_contract.h"

#include <string.h>

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
    if (PrivateDataSize != sizeof(candidate)) {
        return BC250_ALLOCATION_ABI_BAD_SIZE;
    }
    memcpy(&candidate, PrivateData, sizeof(candidate));
    if (candidate.Magic != BC250_ALLOCATION_PRIVATE_MAGIC) {
        return BC250_ALLOCATION_ABI_BAD_MAGIC;
    }
    if (candidate.Version != BC250_ALLOCATION_PRIVATE_VERSION ||
        candidate.StructSize != sizeof(candidate)) {
        return BC250_ALLOCATION_ABI_BAD_VERSION;
    }
    if ((candidate.Flags & ~BC250_ALLOCATION_SUPPORTED_FLAGS) != 0u ||
        candidate.AllocationToken == 0u ||
        candidate.ByteSize == 0u) {
        return ((candidate.Flags & ~BC250_ALLOCATION_SUPPORTED_FLAGS) != 0u) ?
            BC250_ALLOCATION_ABI_BAD_FLAGS : BC250_ALLOCATION_ABI_BAD_SIZE;
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
    return 0;
}

