#include "bc250_umd_private_contract.h"

uint32_t Bc250UmdPrivateRequiredBytes(void)
{
    return (uint32_t)sizeof(BC250_UMD_PRIVATE);
}

BC250_UMD_PRIVATE_STATUS Bc250UmdPrivateWrite(
    const BC250_UMD_PRIVATE_FACTS *Facts,
    void *Buffer,
    uint32_t BufferBytes,
    uint32_t *BytesWritten)
{
    BC250_UMD_PRIVATE blob;
    unsigned char *dst;
    const unsigned char *src;
    uint32_t index;

    if (BytesWritten != NULL) {
        *BytesWritten = 0u;
    }
    if (Facts == NULL || Buffer == NULL || BytesWritten == NULL) {
        return BC250_UMD_PRIVATE_BAD_ARGUMENT;
    }

    /*
     * Falha fechado ANTES de escrever qualquer byte. Escrever parcial deixaria a
     * UMD com meio blob que passa no magic e mente no resto.
     */
    if (BufferBytes < (uint32_t)sizeof(BC250_UMD_PRIVATE)) {
        return BC250_UMD_PRIVATE_BUFFER_TOO_SMALL;
    }

    /* Nao publicamos fato impossivel: sem node nao existe adaptador utilizavel. */
    if (Facts->NodeCount == 0u || Facts->PageSizeBytes == 0u ||
        Facts->GpuVaBits == 0u) {
        return BC250_UMD_PRIVATE_INCOHERENT;
    }

    /* Zera tudo primeiro: nenhum byte de padding vaza do kernel para o usuario. */
    dst = (unsigned char *)&blob;
    for (index = 0u; index < (uint32_t)sizeof(blob); ++index) {
        dst[index] = 0u;
    }

    blob.Magic = BC250_UMD_PRIVATE_MAGIC;
    blob.Version = BC250_UMD_PRIVATE_VERSION;
    blob.SizeBytes = (uint32_t)sizeof(BC250_UMD_PRIVATE);

    blob.VendorId = Facts->VendorId;
    blob.DeviceId = Facts->DeviceId;
    blob.RevisionId = Facts->RevisionId;

    blob.SegmentCount = Facts->SegmentCount;
    blob.VramSegmentId = Facts->VramSegmentId;
    blob.ApertureSegmentId = Facts->ApertureSegmentId;
    blob.VramSizeBytes = Facts->VramSizeBytes;
    blob.ApertureSizeBytes = Facts->ApertureSizeBytes;

    blob.GpuVaBits = Facts->GpuVaBits;
    blob.PageSizeBytes = Facts->PageSizeBytes;
    blob.PageTableLevels = Facts->PageTableLevels;

    blob.NodeCount = Facts->NodeCount;
    blob.SdmaNodeOrdinal = Facts->SdmaNodeOrdinal;
    blob.ComputeNodeOrdinal = Facts->ComputeNodeOrdinal;

    blob.GateFlags = Facts->GateFlags;

    src = (const unsigned char *)&blob;
    dst = (unsigned char *)Buffer;
    for (index = 0u; index < (uint32_t)sizeof(blob); ++index) {
        dst[index] = src[index];
    }

    *BytesWritten = (uint32_t)sizeof(BC250_UMD_PRIVATE);
    return BC250_UMD_PRIVATE_OK;
}

BC250_UMD_PRIVATE_STATUS Bc250UmdPrivateValidate(
    const void *Buffer,
    uint32_t BufferBytes)
{
    BC250_UMD_PRIVATE blob;
    unsigned char *dst;
    const unsigned char *src;
    uint32_t index;

    if (Buffer == NULL) {
        return BC250_UMD_PRIVATE_BAD_ARGUMENT;
    }
    if (BufferBytes < (uint32_t)sizeof(BC250_UMD_PRIVATE)) {
        return BC250_UMD_PRIVATE_BUFFER_TOO_SMALL;
    }

    /* Copia antes de olhar: o buffer pode vir de fora e nao ser alinhado. */
    src = (const unsigned char *)Buffer;
    dst = (unsigned char *)&blob;
    for (index = 0u; index < (uint32_t)sizeof(blob); ++index) {
        dst[index] = src[index];
    }

    if (blob.Magic != BC250_UMD_PRIVATE_MAGIC) {
        return BC250_UMD_PRIVATE_BAD_MAGIC;
    }
    if (blob.Version != BC250_UMD_PRIVATE_VERSION) {
        return BC250_UMD_PRIVATE_BAD_VERSION;
    }
    if (blob.SizeBytes != (uint32_t)sizeof(BC250_UMD_PRIVATE)) {
        return BC250_UMD_PRIVATE_BAD_SIZE;
    }

    /*
     * Coerencia minima. A UMD nao deve seguir com um blob que passa no cabecalho
     * e descreve um adaptador impossivel.
     */
    if (blob.NodeCount == 0u || blob.PageSizeBytes == 0u || blob.GpuVaBits == 0u) {
        return BC250_UMD_PRIVATE_INCOHERENT;
    }
    if (blob.SdmaNodeOrdinal >= blob.NodeCount ||
        blob.ComputeNodeOrdinal >= blob.NodeCount) {
        return BC250_UMD_PRIVATE_INCOHERENT;
    }

    return BC250_UMD_PRIVATE_OK;
}
