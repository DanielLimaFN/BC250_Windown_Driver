#include "bc250_gfx_shader_staging_contract.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

/*
 * =========================================================================
 * B.2.3 -- CONTRATO DE STAGING E ALOCACAO DE SHADERS NA VRAM
 *
 * Implementacao estrita, deterministica e fail-closed.
 * NADA AQUI FOI VALIDADO EM HARDWARE.
 * =========================================================================
 */

static void Bc250StagingZeroMemory(void *Destination, uint32_t Length)
{
    unsigned char *bytes;
    uint32_t i;

    if (Destination == NULL || Length == 0u) {
        return;
    }
    bytes = (unsigned char *)Destination;
    for (i = 0u; i < Length; ++i) {
        bytes[i] = 0u;
    }
}

static void Bc250StagingMemoryBarrier(const BC250_GFX_SHADER_STAGING_TRANSPORT *Transport)
{
    if (Transport != NULL && Transport->MemoryBarrier != NULL) {
        Transport->MemoryBarrier(Transport->Context);
    } else {
#if defined(_MSC_VER)
        _ReadWriteBarrier();
#endif
    }
}

static int Bc250StagingRangesOverlap(uint64_t AStart, uint64_t ABytes,
                                     uint64_t BStart, uint64_t BBytes)
{
    if (ABytes == 0ull || BBytes == 0ull) {
        return 0;
    }
    return (AStart < (BStart + BBytes)) && (BStart < (AStart + ABytes));
}

static void Bc250StagingWipeMemory(
    const BC250_GFX_SHADER_STAGING_TRANSPORT *Transport,
    void *VirtualAddress,
    uint32_t DwordCount)
{
    uint32_t i;
    if (VirtualAddress == NULL || DwordCount == 0u) {
        return;
    }
    if (Transport != NULL && Transport->Write32 != NULL) {
        for (i = 0u; i < DwordCount; ++i) {
            Transport->Write32(Transport->Context, VirtualAddress, i, 0u);
        }
    } else {
        volatile uint32_t *dst = (volatile uint32_t *)VirtualAddress;
        for (i = 0u; i < DwordCount; ++i) {
            dst[i] = 0u;
        }
    }
}

const char *Bc250GfxShaderStagingStatusName(BC250_GFX_SHADER_STAGING_STATUS Status)
{
    switch (Status) {
    case BC250_GFX_SHADER_STAGING_OK:
        return "BC250_GFX_SHADER_STAGING_OK";
    case BC250_GFX_SHADER_STAGING_BAD_ARGUMENT:
        return "BC250_GFX_SHADER_STAGING_BAD_ARGUMENT";
    case BC250_GFX_SHADER_STAGING_MISALIGNED:
        return "BC250_GFX_SHADER_STAGING_MISALIGNED";
    case BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL:
        return "BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL";
    case BC250_GFX_SHADER_STAGING_OVERLAP:
        return "BC250_GFX_SHADER_STAGING_OVERLAP";
    case BC250_GFX_SHADER_STAGING_READBACK_FAILED:
        return "BC250_GFX_SHADER_STAGING_READBACK_FAILED";
    case BC250_GFX_SHADER_STAGING_MANIFEST_INVALID:
        return "BC250_GFX_SHADER_STAGING_MANIFEST_INVALID";
    case BC250_GFX_SHADER_STAGING_CONTRACT_FAILED:
        return "BC250_GFX_SHADER_STAGING_CONTRACT_FAILED";
    case BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW:
        return "BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW";
    case BC250_GFX_SHADER_STAGING_RESERVED_COLLISION:
        return "BC250_GFX_SHADER_STAGING_RESERVED_COLLISION";
    default:
        return "BC250_GFX_SHADER_STAGING_UNKNOWN";
    }
}

BC250_GFX_SHADER_STAGING_STATUS Bc250GfxShaderStagePipeline(
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *VsManifest,
    const BC250_GFX_OBSERVABLE_STAGE_MANIFEST *PsManifest,
    const BC250_GFX_SHADER_STAGING_BUFFER *VsBuffer,
    const BC250_GFX_SHADER_STAGING_BUFFER *PsBuffer,
    const BC250_GFX_SHADER_STAGING_WINDOW *Window,
    const BC250_GFX_SHADER_STAGING_TRANSPORT *Transport,
    BC250_GFX_SHADER_PLAN *VsPlanOut,
    BC250_GFX_SHADER_PLAN *PsPlanOut,
    BC250_GFX_SHADER_STAGING_EVIDENCE *EvidenceOut)
{
    uint32_t vsRequiredBytes;
    uint32_t psRequiredBytes;
    uint64_t vsGpuStart, vsGpuEnd;
    uint64_t psGpuStart, psGpuEnd;
    uintptr_t vsVirtStart, vsVirtEnd;
    uintptr_t psVirtStart, psVirtEnd;
    uint64_t winBase;
    uint64_t winBytes;
    uint64_t scanoutBytes;
    uint32_t i;

    /* 1. Falha fechada: zera saidas imediatamente */
    if (VsPlanOut != NULL) {
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
    }
    if (PsPlanOut != NULL) {
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
    }
    if (EvidenceOut != NULL) {
        Bc250StagingZeroMemory(EvidenceOut, (uint32_t)sizeof(*EvidenceOut));
    }

    /* 2. Validacao de ponteiros nulos */
    if (VsManifest == NULL || PsManifest == NULL ||
        VsBuffer == NULL || PsBuffer == NULL ||
        VsPlanOut == NULL || PsPlanOut == NULL ||
        EvidenceOut == NULL) {
        return BC250_GFX_SHADER_STAGING_BAD_ARGUMENT;
    }

    if (VsBuffer->VirtualAddress == NULL || PsBuffer->VirtualAddress == NULL) {
        return BC250_GFX_SHADER_STAGING_BAD_ARGUMENT;
    }

    /* 3. Validacao dos manifestos (B.2.2) */
    if (Bc250ValidateGfxObservableManifest(VsManifest) == 0 ||
        Bc250ValidateGfxObservableManifest(PsManifest) == 0) {
        return BC250_GFX_SHADER_STAGING_MANIFEST_INVALID;
    }

    /* 4. Alinhamento estrito a 256 bytes (Lei 7 / RDNA 1 SPI_SHADER_PGM_LO_*) */
    if ((VsBuffer->GpuAddress & (BC250_GFX_SHADER_STAGING_ALIGNMENT - 1ull)) != 0ull ||
        (PsBuffer->GpuAddress & (BC250_GFX_SHADER_STAGING_ALIGNMENT - 1ull)) != 0ull) {
        return BC250_GFX_SHADER_STAGING_MISALIGNED;
    }

    /* 5. Limites de tamanho de buffer */
    vsRequiredBytes = VsManifest->ShaderDwordCount * 4u;
    psRequiredBytes = PsManifest->ShaderDwordCount * 4u;
    if (VsBuffer->BufferSizeBytes < vsRequiredBytes ||
        PsBuffer->BufferSizeBytes < psRequiredBytes) {
        return BC250_GFX_SHADER_STAGING_BUFFER_TOO_SMALL;
    }

    /* 6. Delimitacao de janela de VRAM e zonas reservadas (D2) */
    winBase = (Window != NULL) ? Window->VramWindowBase : BC250_GFX_VRAM_WINDOW_DEFAULT_BASE;
    winBytes = (Window != NULL) ? Window->VramWindowBytes : BC250_GFX_VRAM_WINDOW_DEFAULT_BYTES;
    scanoutBytes = (Window != NULL) ? Window->ReservedScanoutBytes : BC250_GFX_VRAM_SCANOUT_RESERVED_BYTES;

    /* 6a. Colisao contra a reserva de scanout herdado GOP [0, scanoutBytes) */
    if (VsBuffer->GpuAddress < scanoutBytes || PsBuffer->GpuAddress < scanoutBytes) {
        return BC250_GFX_SHADER_STAGING_RESERVED_COLLISION;
    }

    /* 6b. Delimitacao estrita dentro da janela de VRAM alocavel */
    if (VsBuffer->GpuAddress < winBase ||
        (VsBuffer->GpuAddress + (uint64_t)vsRequiredBytes) > (winBase + winBytes) ||
        PsBuffer->GpuAddress < winBase ||
        (PsBuffer->GpuAddress + (uint64_t)psRequiredBytes) > (winBase + winBytes)) {
        return BC250_GFX_SHADER_STAGING_OUT_OF_WINDOW;
    }

    /* 6c. Colisao contra zonas reservadas declaradas (anel grafico e dummy page) */
    if (Window != NULL) {
        if (Window->ReservedRingGpuVa != 0ull && Window->ReservedRingBytes != 0u) {
            if (Bc250StagingRangesOverlap(VsBuffer->GpuAddress, vsRequiredBytes,
                                         Window->ReservedRingGpuVa, Window->ReservedRingBytes) ||
                Bc250StagingRangesOverlap(PsBuffer->GpuAddress, psRequiredBytes,
                                         Window->ReservedRingGpuVa, Window->ReservedRingBytes)) {
                return BC250_GFX_SHADER_STAGING_RESERVED_COLLISION;
            }
        }
        if (Window->ReservedDummyPageGpuVa != 0ull && Window->ReservedDummyPageBytes != 0u) {
            if (Bc250StagingRangesOverlap(VsBuffer->GpuAddress, vsRequiredBytes,
                                         Window->ReservedDummyPageGpuVa, Window->ReservedDummyPageBytes) ||
                Bc250StagingRangesOverlap(PsBuffer->GpuAddress, psRequiredBytes,
                                         Window->ReservedDummyPageGpuVa, Window->ReservedDummyPageBytes)) {
                return BC250_GFX_SHADER_STAGING_RESERVED_COLLISION;
            }
        }
    }

    /* 7. Ausencia de sobreposicao (Zero Overlap) */
    /* 7a. Sobreposicao no espaco GPUVA */
    vsGpuStart = VsBuffer->GpuAddress;
    vsGpuEnd = vsGpuStart + (uint64_t)VsBuffer->BufferSizeBytes;
    psGpuStart = PsBuffer->GpuAddress;
    psGpuEnd = psGpuStart + (uint64_t)PsBuffer->BufferSizeBytes;

    if (vsGpuStart < psGpuEnd && psGpuStart < vsGpuEnd) {
        return BC250_GFX_SHADER_STAGING_OVERLAP;
    }

    /* 7b. Sobreposicao no espaco virtual da CPU */
    vsVirtStart = (uintptr_t)VsBuffer->VirtualAddress;
    vsVirtEnd = vsVirtStart + (uintptr_t)VsBuffer->BufferSizeBytes;
    psVirtStart = (uintptr_t)PsBuffer->VirtualAddress;
    psVirtEnd = psVirtStart + (uintptr_t)PsBuffer->BufferSizeBytes;

    if (vsVirtStart < psVirtEnd && psVirtStart < vsVirtEnd) {
        return BC250_GFX_SHADER_STAGING_OVERLAP;
    }

    /* 8. Upload com suporte a transporte de injecao (Lei 4 / D1) */
    if (Transport != NULL && Transport->Write32 != NULL) {
        for (i = 0u; i < VsManifest->ShaderDwordCount; ++i) {
            Transport->Write32(Transport->Context, VsBuffer->VirtualAddress, i, VsManifest->ShaderDwords[i]);
        }
        for (i = 0u; i < PsManifest->ShaderDwordCount; ++i) {
            Transport->Write32(Transport->Context, PsBuffer->VirtualAddress, i, PsManifest->ShaderDwords[i]);
        }
    } else {
        volatile uint32_t *vsDst = (volatile uint32_t *)VsBuffer->VirtualAddress;
        volatile uint32_t *psDst = (volatile uint32_t *)PsBuffer->VirtualAddress;
        for (i = 0u; i < VsManifest->ShaderDwordCount; ++i) {
            vsDst[i] = VsManifest->ShaderDwords[i];
        }
        for (i = 0u; i < PsManifest->ShaderDwordCount; ++i) {
            psDst[i] = PsManifest->ShaderDwords[i];
        }
    }
    EvidenceOut->VsDwordsWritten = VsManifest->ShaderDwordCount;
    EvidenceOut->PsDwordsWritten = PsManifest->ShaderDwordCount;

    /* 9. Barreira de memoria antes do readback (D1) */
    Bc250StagingMemoryBarrier(Transport);

    /* 10. Prova de gravacao por leitura (Readback Proof -- Lei 2 / D1) */
    /* 10a. Readback do Vertex Shader */
    for (i = 0u; i < VsManifest->ShaderDwordCount; ++i) {
        uint32_t val = 0u;
        uint32_t ok = 1u;
        if (Transport != NULL && Transport->Read32 != NULL) {
            ok = Transport->Read32(Transport->Context, VsBuffer->VirtualAddress, i, &val);
        } else {
            val = ((volatile uint32_t *)VsBuffer->VirtualAddress)[i];
        }
        if (ok == 0u || val != VsManifest->ShaderDwords[i]) {
            EvidenceOut->VsReadbackMismatches++;
        } else {
            EvidenceOut->VsReadbackMatches++;
        }
    }
    if (EvidenceOut->VsReadbackMismatches != 0u) {
        /* Limpa a memoria de destino ao falhar (D3) */
        Bc250StagingWipeMemory(Transport, VsBuffer->VirtualAddress, VsManifest->ShaderDwordCount);
        Bc250StagingWipeMemory(Transport, PsBuffer->VirtualAddress, PsManifest->ShaderDwordCount);
        EvidenceOut->MemoryClearedOnFailure = 1u;
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
        return BC250_GFX_SHADER_STAGING_READBACK_FAILED;
    }

    /* 10b. Readback do Pixel Shader */
    for (i = 0u; i < PsManifest->ShaderDwordCount; ++i) {
        uint32_t val = 0u;
        uint32_t ok = 1u;
        if (Transport != NULL && Transport->Read32 != NULL) {
            ok = Transport->Read32(Transport->Context, PsBuffer->VirtualAddress, i, &val);
        } else {
            val = ((volatile uint32_t *)PsBuffer->VirtualAddress)[i];
        }
        if (ok == 0u || val != PsManifest->ShaderDwords[i]) {
            EvidenceOut->PsReadbackMismatches++;
        } else {
            EvidenceOut->PsReadbackMatches++;
        }
    }
    if (EvidenceOut->PsReadbackMismatches != 0u) {
        /* Limpa a memoria de destino ao falhar (D3) */
        Bc250StagingWipeMemory(Transport, VsBuffer->VirtualAddress, VsManifest->ShaderDwordCount);
        Bc250StagingWipeMemory(Transport, PsBuffer->VirtualAddress, PsManifest->ShaderDwordCount);
        EvidenceOut->MemoryClearedOnFailure = 1u;
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
        return BC250_GFX_SHADER_STAGING_READBACK_FAILED;
    }

    /* 11. Integracao com o contrato de shader e calculo de registradores (B.2.1) */
    if (Bc250PopulateGfxShaderPlanFromObservable(VsManifest, VsBuffer->GpuAddress, VsPlanOut) == 0 ||
        Bc250PopulateGfxShaderPlanFromObservable(PsManifest, PsBuffer->GpuAddress, PsPlanOut) == 0) {
        Bc250StagingWipeMemory(Transport, VsBuffer->VirtualAddress, VsManifest->ShaderDwordCount);
        Bc250StagingWipeMemory(Transport, PsBuffer->VirtualAddress, PsManifest->ShaderDwordCount);
        EvidenceOut->MemoryClearedOnFailure = 1u;
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
        return BC250_GFX_SHADER_STAGING_CONTRACT_FAILED;
    }

    if (Bc250GfxShaderValidateBlob(VsManifest->ShaderDwords, VsManifest->ShaderDwordCount, VsPlanOut) != BC250_GFX_SHADER_OK ||
        Bc250GfxShaderValidateBlob(PsManifest->ShaderDwords, PsManifest->ShaderDwordCount, PsPlanOut) != BC250_GFX_SHADER_OK) {
        Bc250StagingWipeMemory(Transport, VsBuffer->VirtualAddress, VsManifest->ShaderDwordCount);
        Bc250StagingWipeMemory(Transport, PsBuffer->VirtualAddress, PsManifest->ShaderDwordCount);
        EvidenceOut->MemoryClearedOnFailure = 1u;
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
        return BC250_GFX_SHADER_STAGING_CONTRACT_FAILED;
    }

    if (Bc250GfxShaderComputeRegs(VsPlanOut, &EvidenceOut->VsRegs) != BC250_GFX_SHADER_OK ||
        Bc250GfxShaderComputeRegs(PsPlanOut, &EvidenceOut->PsRegs) != BC250_GFX_SHADER_OK) {
        Bc250StagingWipeMemory(Transport, VsBuffer->VirtualAddress, VsManifest->ShaderDwordCount);
        Bc250StagingWipeMemory(Transport, PsBuffer->VirtualAddress, PsManifest->ShaderDwordCount);
        EvidenceOut->MemoryClearedOnFailure = 1u;
        Bc250StagingZeroMemory(VsPlanOut, (uint32_t)sizeof(*VsPlanOut));
        Bc250StagingZeroMemory(PsPlanOut, (uint32_t)sizeof(*PsPlanOut));
        return BC250_GFX_SHADER_STAGING_CONTRACT_FAILED;
    }

    EvidenceOut->StagingCompleted = 1u;
    return BC250_GFX_SHADER_STAGING_OK;
}
