#include "bc250_display_ddi.h"
#include "bc250_driver_caps_contract.h"
#include "bc250_blt_proof_contract.h"
#include "bc250_probe_telemetry.h"
#include "bc250_sdma_present.h"
#include "bc250_mmio_breadcrumb.h"
#include "bc250_vram_cpu_path.h"
#include "bc250_dcn_flip_contract.h"
#include "bc250_dcn_ext_contract.h"

/* T3.1: a janela WC termina exatamente na pagina de migalhas, a primeira
 * estrutura privada UC; se uma das duas mudar, o build quebra. */
C_ASSERT(BC250_VRAM_WC_WINDOW_BYTES == BC250_CRUMB_VRAM_OFFSET);

BC250_DISPLAY_STATE g_Bc250DisplayState = { 0 };

/*
 * Nao-zero quando o carregamento corrente NAO anuncia children de video
 * (perfil RENDER_ONLY_LAB).  Vive fora de BC250_DISPLAY_STATE porque
 * Bc250DisplayInit zera aquela estrutura por completo.
 */
static volatile LONG g_Bc250DisplayChildEnumerationDisabled = 0;

void Bc250DisplaySetChildEnumeration(_In_ BOOLEAN Enabled)
{
    InterlockedExchange(
        &g_Bc250DisplayChildEnumerationDisabled, Enabled ? 0 : 1);
}

_Check_return_
BOOLEAN Bc250DisplayGetChildEnumeration(void)
{
    return InterlockedCompareExchange(
        &g_Bc250DisplayChildEnumerationDisabled, 0, 0) == 0 ? TRUE : FALSE;
}

static BC250_DISPLAY_GPUVA_RESOLVER g_Bc250DisplayGpuVaResolver = NULL;

void Bc250DisplayInstallGpuVaResolver(
    _In_opt_ BC250_DISPLAY_GPUVA_RESOLVER Resolver)
{
    g_Bc250DisplayGpuVaResolver = Resolver;
}

static BC250_DISPLAY_ALLOCATION_GEOMETRY_RESOLVER g_Bc250DisplayGeometryResolver = NULL;

void Bc250DisplayInstallAllocationGeometryResolver(
    _In_opt_ BC250_DISPLAY_ALLOCATION_GEOMETRY_RESOLVER Resolver)
{
    g_Bc250DisplayGeometryResolver = Resolver;
}

#define BC250_DISPLAY_MAX_GPUVA_RANGES 64
#define BC250_DISPLAY_MAX_CONTEXT_MAP 16

typedef struct BC250_DISPLAY_GPUVA_RANGE {
    HANDLE ProcessHandle;
    UINT64 GpuVaStart;
    UINT64 GpuVaEnd;
    UINT64 PhysicalStart;
    UINT32 SegmentId;
    UINT32 Valid;
} BC250_DISPLAY_GPUVA_RANGE;

typedef struct BC250_DISPLAY_CONTEXT_MAP {
    HANDLE ContextHandle;
    HANDLE ProcessHandle;
    UINT32 Valid;
} BC250_DISPLAY_CONTEXT_MAP;

static BC250_DISPLAY_GPUVA_RANGE g_Bc250DisplayGpuVaRanges[BC250_DISPLAY_MAX_GPUVA_RANGES];
static BC250_DISPLAY_CONTEXT_MAP g_Bc250DisplayContextMap[BC250_DISPLAY_MAX_CONTEXT_MAP];
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static KSPIN_LOCK g_Bc250DisplayGpuVaLock;
static BOOLEAN g_Bc250DisplayGpuVaLockInit = FALSE;
#endif

void Bc250DisplayRegisterContextProcess(
    _In_ HANDLE ContextHandle,
    _In_opt_ HANDLE ProcessHandle)
{
    ULONG i;
    ULONG emptySlot = BC250_DISPLAY_MAX_CONTEXT_MAP;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        KeInitializeSpinLock(&g_Bc250DisplayGpuVaLock);
        g_Bc250DisplayGpuVaLockInit = TRUE;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif

    for (i = 0; i < BC250_DISPLAY_MAX_CONTEXT_MAP; ++i) {
        if (g_Bc250DisplayContextMap[i].Valid &&
            g_Bc250DisplayContextMap[i].ContextHandle == ContextHandle) {
            g_Bc250DisplayContextMap[i].ProcessHandle = ProcessHandle;
            emptySlot = i;
            break;
        } else if (!g_Bc250DisplayContextMap[i].Valid && emptySlot == BC250_DISPLAY_MAX_CONTEXT_MAP) {
            emptySlot = i;
        }
    }

    if (emptySlot < BC250_DISPLAY_MAX_CONTEXT_MAP) {
        g_Bc250DisplayContextMap[emptySlot].ContextHandle = ContextHandle;
        g_Bc250DisplayContextMap[emptySlot].ProcessHandle = ProcessHandle;
        g_Bc250DisplayContextMap[emptySlot].Valid = 1;
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
}

void Bc250DisplayUnregisterContextProcess(
    _In_ HANDLE ContextHandle)
{
    ULONG i;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        return;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif

    for (i = 0; i < BC250_DISPLAY_MAX_CONTEXT_MAP; ++i) {
        if (g_Bc250DisplayContextMap[i].Valid &&
            g_Bc250DisplayContextMap[i].ContextHandle == ContextHandle) {
            g_Bc250DisplayContextMap[i].Valid = 0;
            g_Bc250DisplayContextMap[i].ContextHandle = NULL;
            g_Bc250DisplayContextMap[i].ProcessHandle = NULL;
            break;
        }
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
}

HANDLE Bc250DisplayGetContextProcess(
    _In_ HANDLE ContextHandle)
{
    ULONG i;
    HANDLE proc = NULL;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        return NULL;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif

    for (i = 0; i < BC250_DISPLAY_MAX_CONTEXT_MAP; ++i) {
        if (g_Bc250DisplayContextMap[i].Valid &&
            g_Bc250DisplayContextMap[i].ContextHandle == ContextHandle) {
            proc = g_Bc250DisplayContextMap[i].ProcessHandle;
            break;
        }
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
    return proc;
}

void Bc250DisplayClearGpuVaMappings(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        KeInitializeSpinLock(&g_Bc250DisplayGpuVaLock);
        g_Bc250DisplayGpuVaLockInit = TRUE;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif
    memset(g_Bc250DisplayGpuVaRanges, 0, sizeof(g_Bc250DisplayGpuVaRanges));
    memset(g_Bc250DisplayContextMap, 0, sizeof(g_Bc250DisplayContextMap));
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
}

void Bc250DisplayRecordGpuVaMapping(
    _In_opt_ HANDLE ProcessHandle,
    _In_ UINT64 GpuVa,
    _In_ UINT64 PhysicalAddress,
    _In_ UINT64 ByteSize,
    _In_ UINT32 SegmentId)
{
    ULONG i;
    ULONG emptySlot = BC250_DISPLAY_MAX_GPUVA_RANGES;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        KeInitializeSpinLock(&g_Bc250DisplayGpuVaLock);
        g_Bc250DisplayGpuVaLockInit = TRUE;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif

    if (ByteSize == 0 || SegmentId == 0) {
        /* Invalidation request: invalidate any overlapping range */
        for (i = 0; i < BC250_DISPLAY_MAX_GPUVA_RANGES; ++i) {
            if (g_Bc250DisplayGpuVaRanges[i].Valid &&
                (ProcessHandle == NULL || g_Bc250DisplayGpuVaRanges[i].ProcessHandle == ProcessHandle ||
                 g_Bc250DisplayGpuVaRanges[i].ProcessHandle == NULL) &&
                GpuVa >= g_Bc250DisplayGpuVaRanges[i].GpuVaStart &&
                GpuVa < g_Bc250DisplayGpuVaRanges[i].GpuVaEnd) {
                g_Bc250DisplayGpuVaRanges[i].Valid = 0;
            }
        }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
        return;
    }

    /* Check if this chunk can be appended to an existing contiguous range */
    for (i = 0; i < BC250_DISPLAY_MAX_GPUVA_RANGES; ++i) {
        if (g_Bc250DisplayGpuVaRanges[i].Valid &&
            g_Bc250DisplayGpuVaRanges[i].SegmentId == SegmentId &&
            (ProcessHandle == NULL || g_Bc250DisplayGpuVaRanges[i].ProcessHandle == ProcessHandle ||
             g_Bc250DisplayGpuVaRanges[i].ProcessHandle == NULL)) {
            /* Case 1: Contiguous append: existing end == new start, and physical is also contiguous */
            if (g_Bc250DisplayGpuVaRanges[i].GpuVaEnd == GpuVa &&
                (g_Bc250DisplayGpuVaRanges[i].PhysicalStart +
                 (g_Bc250DisplayGpuVaRanges[i].GpuVaEnd - g_Bc250DisplayGpuVaRanges[i].GpuVaStart)) == PhysicalAddress) {
                g_Bc250DisplayGpuVaRanges[i].GpuVaEnd += ByteSize;
                if (g_Bc250DisplayGpuVaRanges[i].ProcessHandle == NULL && ProcessHandle != NULL) {
                    g_Bc250DisplayGpuVaRanges[i].ProcessHandle = ProcessHandle;
                }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
                KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
                return;
            }
            /* Case 2: Exact replacement / overlap at the same start */
            if (g_Bc250DisplayGpuVaRanges[i].GpuVaStart == GpuVa) {
                g_Bc250DisplayGpuVaRanges[i].GpuVaEnd = GpuVa + ByteSize;
                g_Bc250DisplayGpuVaRanges[i].PhysicalStart = PhysicalAddress;
                g_Bc250DisplayGpuVaRanges[i].SegmentId = SegmentId;
                if (ProcessHandle != NULL) {
                    g_Bc250DisplayGpuVaRanges[i].ProcessHandle = ProcessHandle;
                }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
                KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
                return;
            }
        } else if (!g_Bc250DisplayGpuVaRanges[i].Valid && emptySlot == BC250_DISPLAY_MAX_GPUVA_RANGES) {
            emptySlot = i;
        }
    }

    /* If no contiguous extension, use an empty slot */
    if (emptySlot < BC250_DISPLAY_MAX_GPUVA_RANGES) {
        g_Bc250DisplayGpuVaRanges[emptySlot].ProcessHandle = ProcessHandle;
        g_Bc250DisplayGpuVaRanges[emptySlot].GpuVaStart = GpuVa;
        g_Bc250DisplayGpuVaRanges[emptySlot].GpuVaEnd = GpuVa + ByteSize;
        g_Bc250DisplayGpuVaRanges[emptySlot].PhysicalStart = PhysicalAddress;
        g_Bc250DisplayGpuVaRanges[emptySlot].SegmentId = SegmentId;
        g_Bc250DisplayGpuVaRanges[emptySlot].Valid = 1;
    } else {
        Bc250DisplayLog("[DisplayGpuVa] WARNING: GPUVA range table full (%u entries), dropped VA=0x%llX\n",
            BC250_DISPLAY_MAX_GPUVA_RANGES, GpuVa);
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
}

BOOLEAN Bc250DisplayLookupPhysicalAddress(
    _In_opt_ HANDLE ProcessHandle,
    _In_ UINT64 GpuVa,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId)
{
    ULONG i;
    BOOLEAN found = FALSE;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KIRQL oldIrql;
    if (!g_Bc250DisplayGpuVaLockInit) {
        return FALSE;
    }
    KeAcquireSpinLock(&g_Bc250DisplayGpuVaLock, &oldIrql);
#endif

    for (i = 0; i < BC250_DISPLAY_MAX_GPUVA_RANGES; ++i) {
        if (g_Bc250DisplayGpuVaRanges[i].Valid &&
            (ProcessHandle == NULL || g_Bc250DisplayGpuVaRanges[i].ProcessHandle == ProcessHandle ||
             g_Bc250DisplayGpuVaRanges[i].ProcessHandle == NULL) &&
            GpuVa >= g_Bc250DisplayGpuVaRanges[i].GpuVaStart &&
            GpuVa < g_Bc250DisplayGpuVaRanges[i].GpuVaEnd) {
            UINT64 offset = GpuVa - g_Bc250DisplayGpuVaRanges[i].GpuVaStart;
            if (PhysicalAddress != NULL) {
                *PhysicalAddress = g_Bc250DisplayGpuVaRanges[i].PhysicalStart + offset;
            }
            if (SegmentId != NULL) {
                *SegmentId = g_Bc250DisplayGpuVaRanges[i].SegmentId;
            }
            found = TRUE;
            break;
        }
    }

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeReleaseSpinLock(&g_Bc250DisplayGpuVaLock, oldIrql);
#endif
    return found;
}

/*
 * UNIAO pAllocationList / pAllocationInfo em DXGKARG_PRESENT
 *
 * A partir de WDDM 2.0 com modelo GpuMmu -- que e o nosso caso
 * (DriverCaps GpuMmu=1, WDDMVersion=0x2000) -- o dxgkrnl preenche
 * pAllocationInfo (DXGK_PRESENTALLOCATIONINFO, 32 bytes por entrada), NAO
 * pAllocationList (DXGK_ALLOCATIONLIST, 24 bytes por entrada). Sao membros da
 * MESMA uniao, entao ler o membro errado nao falha: le lixo com passo errado.
 *
 * PROVA (corrida 04/09 18:10, docs/evidence/2026-09-04_18-10-08-.../bc250_display_log.txt):
 *   [Present #1] Slot0: hDev=0000000000000000, Seg=0,  VA=0x0,      Phys=0x0
 *   [Present #1] Slot1: hDev=0000000000000000, Seg=16, VA=0xAC0000, Phys=0xAC0000
 *   [Present #1] Slot2: hDev=0000000000000000, Seg=0,  VA=0xFFFFC909A9906BC0, ...
 *   [Present #1] REJECT source contract: SlotHAlloc=0000000000000000, SlotSeg=16
 *
 * Lendo com passo de 24 bytes sobre um array de 32:
 *   Slot1.hDev  (offset 24) cai em info[0].SegmentId/PhysicalAdapterIndex  -> 0
 *   Slot1.Seg   (offset 32) cai no dword baixo de info[1].hDeviceSpecific  -> 16 (lixo)
 *   Slot1.VA    (offset 40) cai em info[1].AllocationVirtualAddress        -> 0xAC0000 (certo por acaso)
 *   Slot2.hDev  (offset 48) cai em info[1].PhysicalAddress / alem do array -> ponteiro de kernel
 *
 * Os tres sintomas so tem explicacao com o passo de 32; nenhum tem com o de 24.
 *
 * Lendo a estrutura certa, o SO nos entrega PhysicalAddress e SegmentId da
 * origem prontos. Deixa de ser necessario inferir o endereco resolvendo a GPUVA
 * no nosso snapshot -- que era exatamente o passo que falhava com
 * BC250_GPUVA_NOT_FOUND (status=9) para VA=0xAC0000.
 */
static BOOLEAN Bc250DisplayResolveAllocationSlot(
    _In_ HANDLE Context,
    _In_ const DXGK_PRESENTALLOCATIONINFO *AllocInfo,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId)
{
    if (PhysicalAddress != NULL) *PhysicalAddress = 0ull;
    if (SegmentId != NULL) *SegmentId = 0u;
    if (AllocInfo == NULL || PhysicalAddress == NULL || SegmentId == NULL) {
        return FALSE;
    }

    /*
     * 0. Endereco fisico entregue pelo proprio VidMm. E a fonte de maior
     *    autoridade que existe: nao e inferencia nossa. Segmento 1 e a VRAM
     *    local; qualquer outro segmento nao e blitavel para o scanout GOP.
     */
    if (AllocInfo->SegmentId == 1u && AllocInfo->PhysicalAddress.QuadPart > 0) {
        *PhysicalAddress = (UINT64)AllocInfo->PhysicalAddress.QuadPart;
        *SegmentId = AllocInfo->SegmentId;
        return TRUE;
    }

    /*
     * 1. Tentativa por VirtualAddress (DWM sob WDDM 2.0+).
     * Sob enderecamento virtual, hDeviceSpecificAllocation e' NULL e o DWM
     * entrega a superficie desenhada via VirtualAddress (ex: 0xAC0000).
     */
    if (AllocInfo->AllocationVirtualAddress != 0ull) {
        /*
         * A fonte autoritativa e' somente o snapshot publicado pelo journal
         * depois do ACK do FlushTlb. O cache paralelo de PTEs nao participa
         * do caminho de producao: ele podia expor uma atualizacao ainda nao
         * comitada ou devolver backing antigo depois de um remapeamento.
         */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        /* Protecao contra BugCheck 0x0A: ExAcquirePushLock nao pode ser chamado em DISPATCH_LEVEL */
        if (KeGetCurrentIrql() <= APC_LEVEL)
#endif
        {
            if (g_Bc250DisplayGpuVaResolver != NULL &&
                g_Bc250DisplayGpuVaResolver(Context, (UINT64)AllocInfo->AllocationVirtualAddress, PhysicalAddress, SegmentId)) {
                if (*SegmentId == 1u) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

BOOLEAN Bc250DisplayResolvePresentSource(
    _In_ HANDLE Context,
    _In_ const DXGKARG_PRESENT *Present,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId)
{
    if (PhysicalAddress != NULL) *PhysicalAddress = 0ull;
    if (SegmentId != NULL) *SegmentId = 0u;
    if (Context == NULL || Present == NULL || Present->pAllocationInfo == NULL ||
        PhysicalAddress == NULL || SegmentId == NULL ||
        (Present->Flags.Blt == 0u && Present->Flags.Flip == 0u)) {
        return FALSE;
    }

    return Bc250DisplayResolveAllocationSlot(
        Context, &Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX],
        PhysicalAddress, SegmentId);
}

BOOLEAN Bc250DisplayResolvePresentDestination(
    _In_ HANDLE Context,
    _In_ const DXGKARG_PRESENT *Present,
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId)
{
    if (PhysicalAddress != NULL) *PhysicalAddress = 0ull;
    if (SegmentId != NULL) *SegmentId = 0u;
    if (Context == NULL || Present == NULL || Present->pAllocationInfo == NULL ||
        PhysicalAddress == NULL || SegmentId == NULL ||
        (Present->Flags.Blt == 0u && Present->Flags.Flip == 0u)) {
        return FALSE;
    }

    return Bc250DisplayResolveAllocationSlot(
        Context, &Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX],
        PhysicalAddress, SegmentId);
}

/*
 * DX.63 (Corrida 104) -- PRESENT BLT VAI PARA O DESTINO QUE O DXGKRNL ENTREGA.
 *
 * MEDIDO (P0): os 60 presents Blt do harness (SrcRect=DstRect=1280x720) traziam
 * destino resolvido (SLOT_DST @ 0x4ECC000, a superficie de redirecionamento
 * do DWM), e o KMD o ignorava: copiava direto para o scanout numa posicao
 * CENTRALIZADA inventada (640,180). Eram dois escritores do scanout ao mesmo
 * tempo -- o flip do DWM e este blt --, e os BYTE_PROOF FAIL dos flips #10
 * (FirstMismatch=0x1C2A00 = linha 180, x=640, o canto da janela) e #45 (dentro
 * da mesma area) sao a conferencia do flip pegando o blt concorrente.
 *
 * Contrato (DXGKARG_PRESENT, Flags.Blt): copiar SrcRect da origem para
 * DstRect do DESTINO. O scanout so' recebe o que o flip do DWM compoe.
 * Sem geometria de origem/destino, formato 32 bpp igual, retangulos do mesmo
 * tamanho e dentro das duas superficies, o present e' RECUSADO -- nunca cai
 * de volta no scanout.
 */
static const char *Bc250DisplayBltToDestination(
    _In_ HANDLE Context,
    _In_ const DXGKARG_PRESENT *Present,
    _In_ UINT64 SourceVramOffset,
    _Out_ BOOLEAN *Copied)
{
    UINT32 srcW = 0u, srcH = 0u, srcPitch = 0u, srcFmt = 0u;
    UINT32 dstW = 0u, dstH = 0u, dstPitch = 0u, dstFmt = 0u;
    UINT64 dstSegOffset = 0ull, dstVram = 0ull;
    UINT32 dstSeg = 0u;
    LONG w, h;
    uint64_t srcGpu, dstGpu;

    *Copied = FALSE;
    if (g_Bc250DisplayGeometryResolver == NULL) return "SEM_RESOLVEDOR_DE_GEOMETRIA";
    if (Present->pAllocationInfo == NULL) return "SEM_ALLOCATIONINFO";
    if (!g_Bc250DisplayGeometryResolver(
            Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation,
            &srcW, &srcH, &srcPitch, &srcFmt)) {
        return "GEOMETRIA_DA_ORIGEM_DESCONHECIDA";
    }
    if (!g_Bc250DisplayGeometryResolver(
            Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation,
            &dstW, &dstH, &dstPitch, &dstFmt)) {
        return "GEOMETRIA_DO_DESTINO_DESCONHECIDA";
    }
    /*
     * D3DDDIFMT_A8R8G8B8 = 21, D3DDDIFMT_X8R8G8B8 = 22: 4 bytes por pixel na
     * mesma ordem de bytes; copiar um para o outro e' copia byte a byte.
     * Qualquer outro formato, em qualquer lado, e' recusado.
     */
    if ((srcFmt != 21u && srcFmt != 22u) || (dstFmt != 21u && dstFmt != 22u)) {
        return "FORMATO_DIVERGENTE_OU_NAO_32BPP";
    }
    if (srcPitch < srcW * 4u || dstPitch < dstW * 4u) return "PITCH_MENOR_QUE_A_LINHA";
    w = Present->SrcRect.right - Present->SrcRect.left;
    h = Present->SrcRect.bottom - Present->SrcRect.top;
    if (w <= 0 || h <= 0 ||
        w != Present->DstRect.right - Present->DstRect.left ||
        h != Present->DstRect.bottom - Present->DstRect.top) {
        return "RETANGULOS_VAZIOS_OU_COM_ESCALA";
    }
    if (Present->SrcRect.left < 0 || Present->SrcRect.top < 0 ||
        (ULONG)Present->SrcRect.right > srcW || (ULONG)Present->SrcRect.bottom > srcH ||
        Present->DstRect.left < 0 || Present->DstRect.top < 0 ||
        (ULONG)Present->DstRect.right > dstW || (ULONG)Present->DstRect.bottom > dstH) {
        return "RETANGULO_FORA_DA_SUPERFICIE";
    }
    if ((UINT64)srcPitch * srcH > g_Bc250DisplayState.FrameBufferSize ||
        SourceVramOffset > g_Bc250DisplayState.FrameBufferSize - (UINT64)srcPitch * srcH) {
        return "ORIGEM_FORA_DA_VRAM";
    }
    if (!Bc250DisplayResolvePresentDestination(Context, Present, &dstSegOffset, &dstSeg)) {
        return "DESTINO_NAO_RESOLVIDO";
    }
    if (!Bc250SdmaPresentSegmentOffsetToVram(dstSeg, dstSegOffset,
            (UINT64)dstPitch * dstH, &dstVram)) {
        return "DESTINO_FORA_DO_SEGMENTO";
    }
    if (dstVram < (UINT64)g_Bc250DisplayState.CurrentMode.Pitch *
            g_Bc250DisplayState.CurrentMode.Height) {
        /* O destino de um Blt nunca e' o scanout do GOP (offset 0). */
        return "DESTINO_SOBREPOE_O_SCANOUT";
    }
    srcGpu = g_Bc250SdmaPresent.VramMcStart + SourceVramOffset +
        (uint64_t)(ULONG)Present->SrcRect.top * srcPitch +
        (uint64_t)(ULONG)Present->SrcRect.left * 4ull;
    dstGpu = g_Bc250SdmaPresent.VramMcStart + dstVram;
    *Copied = Bc250SdmaPresentBltRect(srcGpu, srcPitch, dstGpu, dstPitch,
        (ULONG)Present->DstRect.left, (ULONG)Present->DstRect.top,
        (ULONG)w, (ULONG)h);
    return *Copied ? "OK" : "SDMA_FALHOU";
}

BOOLEAN Bc250DisplayResolvePrimarySource(
    _Out_ UINT64 *PhysicalAddress,
    _Out_ UINT32 *SegmentId)
{
    if (PhysicalAddress != NULL) *PhysicalAddress = 0ull;
    if (SegmentId != NULL) *SegmentId = 0u;
    if (PhysicalAddress == NULL || SegmentId == NULL) return FALSE;
    if (!g_Bc250DisplayState.PrimaryAddressValid) return FALSE;
    if (g_Bc250DisplayState.PrimaryAddress.QuadPart <= 0) return FALSE;
    /*
     * O driver declara dois segmentos (QUERYSEGMENT4): 1 = VRAM local,
     * 2 = abertura GART.  Somente o segmento 1 e' alcancavel pelo blt de
     * scanout; qualquer outro valor falha fechado em vez de virar offset.
     */
    if (g_Bc250DisplayState.PrimarySegment != 1u) return FALSE;
    *PhysicalAddress = (UINT64)g_Bc250DisplayState.PrimaryAddress.QuadPart;
    *SegmentId = g_Bc250DisplayState.PrimarySegment;
    return TRUE;
}

BOOLEAN Bc250DisplaySampleSurface(
    _In_ UINT64 ByteOffset,
    _In_ ULONG ByteCount,
    _In_ ULONG Samples,
    _Out_ ULONG *NonZeroCount,
    _Out_ ULONG *XorHash)
{
    CONST UCHAR *base;
    ULONG stride;
    ULONG i;
    ULONG nonZero = 0u;
    ULONG hash = 0u;

    if (NonZeroCount != NULL) *NonZeroCount = 0u;
    if (XorHash != NULL) *XorHash = 0u;
    if (NonZeroCount == NULL || XorHash == NULL) return FALSE;
#if BC250_P1_SILENCIO_DA_BAR && defined(BC250_ENABLE_DXGK_REGISTRATION)
    /* P1 (doc 594): sem leitura de VRAM pela CPU para prova amostral. So' na
     * imagem do driver: a suite offline continua exercitando a logica. Os
     * chamadores ja tratam "nao amostrado" (SAMPLE_UNAVAILABLE): a copia do
     * Present continua; perdem-se so' os atalhos que dependem de amostra. */
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(ByteCount);
    UNREFERENCED_PARAMETER(Samples);
    UNREFERENCED_PARAMETER(base);
    UNREFERENCED_PARAMETER(stride);
    UNREFERENCED_PARAMETER(i);
    UNREFERENCED_PARAMETER(nonZero);
    UNREFERENCED_PARAMETER(hash);
    return FALSE;
#else
    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) return FALSE;
    if (Samples == 0u || ByteCount < sizeof(ULONG)) return FALSE;
    if (ByteOffset > g_Bc250DisplayState.FrameBufferSize) return FALSE;
    if (ByteCount > g_Bc250DisplayState.FrameBufferSize - ByteOffset) return FALSE;

    base = (CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
        (SIZE_T)ByteOffset;
    stride = (ByteCount - (ULONG)sizeof(ULONG)) / Samples;
    stride &= ~3u;
    if (stride == 0u) stride = (ULONG)sizeof(ULONG);

    {
    ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_SAMPLE, (ULONG)ByteOffset);
    for (i = 0u; i < Samples; ++i) {
        ULONG offset = i * stride;
        ULONG value;
        if (offset > ByteCount - sizeof(ULONG)) break;
        value = *(CONST ULONG *)(base + offset);
        if (value != 0u) ++nonZero;
        hash ^= value;
    }
    Bc250CrumbAfter(crumb);
    }

    *NonZeroCount = nonZero;
    *XorHash = hash;
    return TRUE;
#endif
}

BOOLEAN Bc250DisplayCompareSurfaceBytes(
    _In_ UINT64 SourceOffset,
    _In_ UINT64 DestinationOffset,
    _In_ ULONG ByteCount,
    _Out_ UINT64 *FirstMismatchOffset)
{
    volatile CONST UCHAR *base;
    ULONG index;

    if (FirstMismatchOffset != NULL) *FirstMismatchOffset = ~(UINT64)0;
    if (FirstMismatchOffset == NULL || ByteCount == 0u ||
        g_Bc250DisplayState.FrameBufferVirtual == NULL) return FALSE;
    if (SourceOffset > g_Bc250DisplayState.FrameBufferSize ||
        ByteCount > g_Bc250DisplayState.FrameBufferSize - SourceOffset ||
        DestinationOffset > g_Bc250DisplayState.FrameBufferSize ||
        ByteCount > g_Bc250DisplayState.FrameBufferSize - DestinationOffset) {
        return FALSE;
    }

    base = (volatile CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual;
    {
    ULONG crumb = Bc250CrumbBefore(BC250_CRUMB_SITE_VRAM_COMPARE, (ULONG)SourceOffset);
    for (index = 0u; index < ByteCount; ++index) {
        if (base[(SIZE_T)SourceOffset + index] !=
            base[(SIZE_T)DestinationOffset + index]) {
            *FirstMismatchOffset = index;
            Bc250CrumbAfter(crumb);
            return FALSE;
        }
    }
    Bc250CrumbAfter(crumb);
    }
    return TRUE;
}

/*
 * DX.31: a mudanca de primaria do WDDM nao e' um flip fisico neste perfil.
 * O GOP continua lendo o mesmo scanout; a nova primaria precisa ser copiada
 * por SDMA. A Corrida 76 registrou buffers de Flip nao-nulos sem a prova de
 * dados que o caminho DxgkDdiPresent possui. Toda chamada agora deixa uma
 * trilha correlacionavel, inclusive a recusa e o quadro em branco. Esta DDI
 * pode bloquear o scheduler em nivel de interrupcao: somente 256 amostras
 * pareadas, NUNCA a comparacao integral de 11 MB do caminho DxgkDdiPresent.
 */
static volatile LONG s_Bc250PrimaryCopySequence = 0;

/*
 * DX.34: retangulos sujos do Present Blt. Teto pequeno de proposito: cada
 * retangulo e' uma submissao SDMA sincrona; lista maior cai na copia de tela
 * cheia de antes, com log. Aceita so' Src == Dst == tela cheia, para que (x,y)
 * na origem e no scanout sejam o mesmo ponto.
 */
#define BC250_DIRTY_RECT_MAX 16u
static volatile LONG s_Bc250DirtyRectLogCount = 0;

/*
 * DX.49 -- RETANGULO VAZIO = SUPERFICIE INTEIRA.
 *
 * Corrida 93 (P0): as 17 recusas vieram com SubRectCnt 1..3 e, nas amostradas,
 * "DstRect=(0,0)-(0,0), SrcRect=(0,0)-(0,0)". No Present de flip o dxgkrnl nao
 * preenche Src/Dst; a exigencia de Src == Dst == tela cheia recusava TODOS e
 * caia na copia de 11 MB. Retangulo vazio nos DOIS lados e' aceito como
 * superficie inteira; qualquer outro Src/Dst continua exigindo tela cheia.
 * A recusa agora nomeia a condicao, em vez de uma lista de quatro possiveis.
 */
/*
 * DX.50 -- BISSECCAO: dirty rects DESLIGADOS.
 *
 * Corridas 94 e 95 (P0): travamento duro nas duas, e defeito visual "depois de
 * um tempo" na 95, logo que os dirty rects passaram a ser aceitos (384 copias
 * SDMA por retangulo na 95; zero em todas as corridas anteriores). Com isto em
 * FALSE todo Present volta a copia de tela cheia da Corrida 93; a validacao
 * acima continua compilada e testada, e a recusa sai no log com
 * motivo=DX50_BISSECCAO_DESLIGADO. Uma variavel: o lote de draws, o staging e
 * o trace da DX.49/49b continuam ligados.
 */
static const BOOLEAN s_Bc250Dx50DirtyRectsEnabled = FALSE;

static BOOLEAN Bc250DisplayRectIsEmpty(_In_ CONST RECT *Rect)
{
    return Rect->left == 0 && Rect->top == 0 && Rect->right == 0 && Rect->bottom == 0;
}

static BOOLEAN Bc250DisplayRectIsFull(_In_ CONST RECT *Rect, ULONG Width, ULONG Height)
{
    return Rect->left == 0 && Rect->top == 0 &&
        Rect->right == (LONG)Width && Rect->bottom == (LONG)Height;
}

static BOOLEAN Bc250DisplayDirtyRectsUsable(
    _In_ CONST DXGKARG_PRESENT *Present,
    _Out_ UINT *Count,
    _Out_ CONST char **Reason,
    _Out_ UINT *BadIndex)
{
    ULONG w = g_Bc250DisplayState.CurrentMode.Width;
    ULONG h = g_Bc250DisplayState.CurrentMode.Height;
    BOOLEAN bothEmpty;
    UINT i;

    *Count = 0u;
    *BadIndex = 0u;
    *Reason = "OK";
    if (Present->SubRectCnt == 0u) { *Reason = "LISTA_VAZIA"; return FALSE; }
    if (Present->SubRectCnt > BC250_DIRTY_RECT_MAX) { *Reason = "LISTA_ACIMA_DO_TETO"; return FALSE; }
    if (Present->pDstSubRects == NULL) { *Reason = "pDstSubRects_NULL"; return FALSE; }
    if (w == 0u || h == 0u || g_Bc250DisplayState.CurrentMode.Pitch < w * 4u) {
        *Reason = "MODO_SEM_GEOMETRIA"; return FALSE;
    }
    bothEmpty = Bc250DisplayRectIsEmpty(&Present->SrcRect) && Bc250DisplayRectIsEmpty(&Present->DstRect);
    if (!bothEmpty &&
        (!Bc250DisplayRectIsFull(&Present->SrcRect, w, h) || !Bc250DisplayRectIsFull(&Present->DstRect, w, h))) {
        *Reason = "SRC_DST_NEM_VAZIOS_NEM_TELA_CHEIA"; return FALSE;
    }
    for (i = 0u; i < Present->SubRectCnt; ++i) {
        CONST RECT *rc = &Present->pDstSubRects[i];
        if (rc->left < 0 || rc->top < 0 || rc->right <= rc->left ||
            rc->bottom <= rc->top || (ULONG)rc->right > w || (ULONG)rc->bottom > h) {
            *Reason = "RETANGULO_FORA_DA_TELA_OU_DEGENERADO";
            *BadIndex = i;
            return FALSE;
        }
    }
    *Count = Present->SubRectCnt;
    return TRUE;
}

/* Prova amostral dos retangulos: centro e 4 cantos de cada um, origem x scanout. */
static VOID Bc250DisplayDirtyRectProof(
    _In_ CONST DXGKARG_PRESENT *Present,
    _In_ UINT Count,
    _In_ UINT64 SourceOffset,
    _Out_ ULONG *Match,
    _Out_ ULONG *Probes,
    _Out_ ULONG *SourceNonZero)
{
    CONST UCHAR *fb = (CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual;
    ULONG pitch = g_Bc250DisplayState.CurrentMode.Pitch;
    UINT64 scanoutOffset = g_Bc250SdmaPresent.ScanoutGpuAddress -
        g_Bc250SdmaPresent.VramMcStart;
    UINT i, p;

    *Match = 0u; *Probes = 0u; *SourceNonZero = 0u;
    if (fb == NULL) return;
    for (i = 0u; i < Count; ++i) {
        CONST RECT *rc = &Present->pDstSubRects[i];
        LONG pts[5][2] = {
            { (rc->left + rc->right) / 2, (rc->top + rc->bottom) / 2 },
            { rc->left, rc->top }, { rc->right - 1, rc->top },
            { rc->left, rc->bottom - 1 }, { rc->right - 1, rc->bottom - 1 } };
        for (p = 0u; p < 5u; ++p) {
            UINT64 rel = (UINT64)(ULONG)pts[p][1] * pitch + (UINT64)(ULONG)pts[p][0] * 4ull;
            ULONG s, d;
            if (SourceOffset + rel + 4ull > g_Bc250DisplayState.FrameBufferSize ||
                scanoutOffset + rel + 4ull > g_Bc250DisplayState.FrameBufferSize) {
                continue;
            }
            s = *(CONST volatile ULONG *)(fb + SourceOffset + rel);
            d = *(CONST volatile ULONG *)(fb + scanoutOffset + rel);
            ++*Probes;
            if (s != 0u) ++*SourceNonZero;
            if (s == d) ++*Match;
        }
    }
}

/* 256 comparacoes pareadas origem x scanout pelo mapa de CPU. Devolve os pares lidos. */
static ULONG Bc250DisplayPairedSampleCompare(
    _In_ UINT64 SourceOffset,
    _In_ UINT64 ScanoutOffset,
    _In_ ULONG Bytes,
    _Out_ ULONG *MismatchCount,
    _Out_ UINT64 *FirstMismatch)
{
    volatile CONST UCHAR *base = (volatile CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual;
    ULONG stride = ((Bytes - sizeof(ULONG)) / 256u) & ~3u;
    ULONG index;
    *MismatchCount = 0u;
    *FirstMismatch = ~(UINT64)0;
    if (stride == 0u) stride = sizeof(ULONG);
    for (index = 0u; index < 256u; ++index) {
        ULONG offset = index * stride;
        ULONG srcWord, dstWord;
        if (offset > Bytes - sizeof(ULONG)) break;
        srcWord = *(volatile CONST ULONG *)(base + SourceOffset + offset);
        dstWord = *(volatile CONST ULONG *)(base + ScanoutOffset + offset);
        if (srcWord != dstWord) {
            if (*FirstMismatch == ~(UINT64)0) *FirstMismatch = offset;
            ++*MismatchCount;
        }
    }
    return index;
}

/*
 * DCN.1 (doc 710, P9 aberta pelo Daniel em 12/10/2026): flip pelo HUBP0.
 *
 * D2 (so' leitura): no StartDevice, le o HUBP0/OTG0 como o GOP deixou e decide
 *   se o pipeline serve para flip sem reprogramar formato, pitch, viewport,
 *   timing, clock ou PHY (Bc250DcnFlipEvaluateGop).
 * D3 (escrita): a cada SetVidPnSourceAddress/MPO3 aponta o HUBP0 para a
 *   primaria (FLIP_CONTROL, endereco alto, endereco baixo, readback), como
 *   hubp2_program_surface_flip_and_addr, em vez de copiar 11 MB pelo SDMA0. O
 *   Present de flip deixa de copiar para o scanout do GOP.
 * D4 (so' leitura): no DPC de VSYNC le FLIP_CONTROL/EARLIEST_INUSE; o VSYNC so'
 *   reporta a primaria nova depois do latch (hubp2_is_flip_pending). O VSYNC
 *   continua pelo contador de quadros do OTG0 com poll de 4 ms: a interrupcao
 *   do DCN pelo IH NAO esta nesta etapa.
 * Teardown (Lei 5): o HUBP0 volta ao endereco e ao FLIP_CONTROL do GOP antes de
 *   o BasicDisplay assumir.
 * Chave DcnFlip do INF (padrao 0). Com 0 nada disto escreve registrador.
 */
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
typedef struct BC250_DCN_FLIP_RUNTIME {
    volatile UCHAR *Window;
    BC250_DCN_GOP_STATE Gop;
    BC250_DCN_FLIP_PLAN Plan;
    ULONG Refuse;
    BOOLEAN GopRead;
    volatile LONG Enabled;
    volatile LONG Armed;
    volatile LONG OnUserSurface;
    volatile LONG64 RequestedDcn;
    volatile LONG64 RequestedSeg;
    volatile LONG64 DisplayedSeg;
    volatile LONG64 RequestQpc;
    volatile LONG Flips;
    volatile LONG Latched;
    volatile LONG ResolvedSeq;  /* ultimo flip com latch visto (D4) */
    volatile LONG Superseded;   /* trocados por um mais novo antes do latch */
    /* Medicao da Corrida 270 (doc 711): so' contadores. */
    volatile LONG RouteSetAddress;
    volatile LONG RouteMpo1;
    volatile LONG RouteMpo3;
    volatile LONG RouteImmediate;
    volatile LONG NotSegment1;  /* primaria fora do segmento 1: copia, sem flip */
    volatile LONG RequestFrame; /* OTG0 FRAME_COUNT no pedido */
    volatile LONG LatchFrames[4];
    volatile LONG LatchUs[4];
    ULONG ArmFrame;
    LONG ArmHwVsync;
    LONG ArmVsync;
    LONGLONG ArmQpc;
    volatile LONG Refused;
    volatile LONG ReadbackFail;
    volatile LONG CopiesSkipped;
    volatile LONG MaxLatchUs;
} BC250_DCN_FLIP_RUNTIME;
static BC250_DCN_FLIP_RUNTIME s_Bc250Dcn;
/* DCN.2 (doc 713), definidas mais abaixo neste bloco. */
static VOID Bc250Dcn2MeasureTiming(void);
static VOID Bc250Dcn2LogSummary(_In_ const char *Why);
static ULONG Bc250Dcn2KeysArmed(void);

static ULONG Bc250DcnRead(ULONG AbsOffset)
{
    return *(volatile ULONG *)(s_Bc250Dcn.Window + (AbsOffset - BC250_DCN_FLIP_WINDOW_BASE));
}

static VOID Bc250DcnWrite(ULONG AbsOffset, ULONG Value)
{
    *(volatile ULONG *)(s_Bc250Dcn.Window + (AbsOffset - BC250_DCN_FLIP_WINDOW_BASE)) = Value;
}

/* D2: mapeia a janela e fotografa o estado do GOP. So' leitura. */
static VOID Bc250DcnFlipReadGop(_In_ PHYSICAL_ADDRESS MmioPhysical)
{
    PHYSICAL_ADDRESS window;
    if (MmioPhysical.QuadPart == 0 || s_Bc250Dcn.Window != NULL) return;
    window.QuadPart = MmioPhysical.QuadPart + BC250_DCN_FLIP_WINDOW_BASE;
    s_Bc250Dcn.Window = (volatile UCHAR *)MmMapIoSpace(window, BC250_DCN_FLIP_WINDOW_BYTES, MmNonCached);
    if (s_Bc250Dcn.Window == NULL) {
        Bc250DisplayLog("[DCN1_D2] AVISO: falha ao mapear a janela do DCN (0x%llX, 0x%X bytes); flip pelo HUBP indisponivel\n",
            (unsigned long long)window.QuadPart, BC250_DCN_FLIP_WINDOW_BYTES);
        return;
    }
    s_Bc250Dcn.Gop.SurfaceConfig = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBP0_SURFACE_CONFIG);
    s_Bc250Dcn.Gop.ViewportDim = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBP0_PRI_VIEWPORT_DIM);
    s_Bc250Dcn.Gop.SurfacePitch = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_SURFACE_PITCH);
    s_Bc250Dcn.Gop.PrimaryAddrLo = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR);
    s_Bc250Dcn.Gop.PrimaryAddrHi = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI);
    s_Bc250Dcn.Gop.SurfaceControl = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_SURFACE_CONTROL);
    s_Bc250Dcn.Gop.FlipControl = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL);
    s_Bc250Dcn.Gop.OtgControl = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_CONTROL);
    s_Bc250Dcn.Gop.MasterUpdateLock = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_MASTER_UPDATE_LOCK);
    s_Bc250Dcn.GopRead = TRUE;
    Bc250DisplayLog("[DCN1_D2] HUBP0/OTG0 como o GOP deixou (so' leitura): SURFACE_CONFIG=0x%08X VIEWPORT=0x%08X PITCH=0x%08X ADDR=0x%04X:%08X SURFACE_CONTROL=0x%08X FLIP_CONTROL=0x%08X EARLIEST_INUSE=0x%04X:%08X OTG_CONTROL=0x%08X MASTER_UPDATE_LOCK=0x%08X FRAME=%u\n",
        s_Bc250Dcn.Gop.SurfaceConfig, s_Bc250Dcn.Gop.ViewportDim, s_Bc250Dcn.Gop.SurfacePitch,
        s_Bc250Dcn.Gop.PrimaryAddrHi & 0xFFFFu, s_Bc250Dcn.Gop.PrimaryAddrLo,
        s_Bc250Dcn.Gop.SurfaceControl, s_Bc250Dcn.Gop.FlipControl,
        Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE_HI) & 0xFFFFu,
        Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE),
        s_Bc250Dcn.Gop.OtgControl, s_Bc250Dcn.Gop.MasterUpdateLock,
        Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK);
    Bc250Dcn2MeasureTiming(); /* DCN.2 item 1, so' leitura */
}

/* D0/D2: liga o flip so' com a chave do INF E o pipeline do GOP elegivel. */
void Bc250DisplaySetDcnFlipEnabled(ULONG Enabled)
{
    InterlockedExchange(&s_Bc250Dcn.Enabled, Enabled != 0u ? 1 : 0);
    InterlockedExchange(&s_Bc250Dcn.Armed, 0);
    if (Enabled == 0u) {
        Bc250DisplayLog("[DCN1_FLIP] chave DcnFlip=0: apresentacao pela copia do SDMA0 no scanout do GOP (como antes); nenhuma escrita no DCN\n");
        return;
    }
    if (!s_Bc250Dcn.GopRead) {
        Bc250DisplayLog("[DCN1_FLIP] RECUSADO: estado do GOP nao lido (janela do DCN sem mapa); copia do SDMA0 mantida\n");
        return;
    }
    s_Bc250Dcn.Refuse = Bc250DcnFlipEvaluateGop(&s_Bc250Dcn.Gop,
        g_Bc250DisplayState.CurrentMode.Width, g_Bc250DisplayState.CurrentMode.Height,
        g_Bc250DisplayState.CurrentMode.Pitch, &s_Bc250Dcn.Plan);
    if (s_Bc250Dcn.Refuse != 0u) {
        Bc250DisplayLog("[DCN1_FLIP] RECUSADO: pipeline do GOP nao serve para flip (recusas=0x%04X: 1=OTG desligado 2=viewport 4=pitch 8=DCC 10=lock de superficie 20=master lock 40=endereco do GOP 80=bloco morto 100=modo) Modo=%ux%u pitch=%u Viewport=%ux%u PitchPx=%u; copia do SDMA0 mantida\n",
            s_Bc250Dcn.Refuse,
            g_Bc250DisplayState.CurrentMode.Width, g_Bc250DisplayState.CurrentMode.Height,
            g_Bc250DisplayState.CurrentMode.Pitch,
            s_Bc250Dcn.Plan.Width, s_Bc250Dcn.Plan.Height, s_Bc250Dcn.Plan.PitchPixels);
        return;
    }
    InterlockedExchange(&s_Bc250Dcn.Armed, 1);
    s_Bc250Dcn.ArmFrame = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK;
    s_Bc250Dcn.ArmHwVsync = InterlockedCompareExchange(&g_Bc250DisplayState.HardwareVsyncCount, 0, 0);
    s_Bc250Dcn.ArmVsync = InterlockedCompareExchange(&g_Bc250DisplayState.VsyncCount, 0, 0);
    s_Bc250Dcn.ArmQpc = KeQueryPerformanceCounter(NULL).QuadPart;
    Bc250DisplayLog("[DCN1_FLIP] ARMADO: flip pelo HUBP0 sobre o pipeline do GOP (endereco do GOP 0x%llX, %ux%u, pitch %u px, formato %u, FLIP_CONTROL base 0x%08X); Present de flip sem copia ao scanout; VSYNC so' depois do latch\n",
        (unsigned long long)s_Bc250Dcn.Plan.GopAddress, s_Bc250Dcn.Plan.Width, s_Bc250Dcn.Plan.Height,
        s_Bc250Dcn.Plan.PitchPixels, s_Bc250Dcn.Plan.PixelFormat, s_Bc250Dcn.Plan.BaseFlipControl);
}

ULONG Bc250DisplayDcnFlipArmed(void)
{
    return (ULONG)InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0);
}

/* D3: aponta o HUBP0 para a primaria em SourceVramOffset. FALSE = nao feito
 * (o chamador usa a copia do SDMA0 como antes). */
static BOOLEAN Bc250DcnFlipProgram(
    _In_ const char *Route, _In_ UINT64 SourceVramOffset,
    _In_ PHYSICAL_ADDRESS SegmentAddress, _In_ BOOLEAN Immediate)
{
    UINT64 address = 0ull;
    UINT64 scanoutOffset;
    ULONG bytes;
    ULONG flipCtl, hiRb, loRb, frame;
    LONG n;

    if (InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0) == 0 || s_Bc250Dcn.Window == NULL)
        return FALSE;
    bytes = g_Bc250DisplayState.CurrentMode.Pitch * g_Bc250DisplayState.CurrentMode.Height;
    scanoutOffset = (g_Bc250SdmaPresent.ScanoutGpuAddress >= g_Bc250SdmaPresent.VramMcStart) ?
        g_Bc250SdmaPresent.ScanoutGpuAddress - g_Bc250SdmaPresent.VramMcStart : ~(UINT64)0;
    if (scanoutOffset == ~(UINT64)0 ||
        !Bc250DcnFlipAddressForVramOffset(s_Bc250Dcn.Plan.GopAddress, scanoutOffset,
            SourceVramOffset, bytes, g_Bc250DisplayState.FrameBufferSize, &address)) {
        LONG r = InterlockedIncrement(&s_Bc250Dcn.Refused);
        if (r <= 16 || (r % 256) == 0) {
            Bc250DisplayLog("[DCN1_FLIP] RECUSA #%ld Route=%s VramOff=0x%llX Bytes=%u ScanoutOff=0x%llX VramMapBytes=%u: endereco fora da VRAM mapeada ou desalinhado; copia do SDMA0 neste flip\n",
                r, Route, (unsigned long long)SourceVramOffset, bytes,
                (unsigned long long)scanoutOffset, g_Bc250DisplayState.FrameBufferSize);
        }
        return FALSE;
    }
    flipCtl = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL);
    if ((flipCtl & BC250_DCN_SURFACE_UPDATE_LOCK_MASK) != 0u) {
        LONG r = InterlockedIncrement(&s_Bc250Dcn.Refused);
        if (r <= 16 || (r % 256) == 0) {
            Bc250DisplayLog("[DCN1_FLIP] RECUSA #%ld Route=%s: SURFACE_UPDATE_LOCK ligado (FLIP_CONTROL=0x%08X); copia do SDMA0 neste flip\n",
                r, Route, flipCtl);
        }
        return FALSE;
    }
    Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL,
        Bc250DcnFlipControlValue(flipCtl, Immediate ? 1 : 0));
    /* Alto primeiro, baixo por ultimo: a escrita do baixo dispara o latch. */
    Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI,
        (ULONG)(address >> 32) & BC250_DCN_PRIMARY_ADDR_HIGH_MASK);
    Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR, (ULONG)address);
    hiRb = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI) & BC250_DCN_PRIMARY_ADDR_HIGH_MASK;
    loRb = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR);
    if ((((UINT64)hiRb << 32) | loRb) != address) {
        LONG f = InterlockedIncrement(&s_Bc250Dcn.ReadbackFail);
        Bc250DisplayLog("[DCN1_FLIP] READBACK FALHOU #%ld Route=%s pedido=0x%llX lido=0x%04X:%08X; HUBP0 devolvido ao GOP e flip desarmado (falha fechada)\n",
            f, Route, (unsigned long long)address, hiRb, loRb);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL, s_Bc250Dcn.Plan.BaseFlipControl);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI,
            (ULONG)(s_Bc250Dcn.Plan.GopAddress >> 32) & BC250_DCN_PRIMARY_ADDR_HIGH_MASK);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR, (ULONG)s_Bc250Dcn.Plan.GopAddress);
        InterlockedExchange(&s_Bc250Dcn.OnUserSurface, 0);
        InterlockedExchange(&s_Bc250Dcn.Armed, 0);
        return FALSE;
    }
    frame = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK;
    InterlockedExchange(&s_Bc250Dcn.RequestFrame, (LONG)frame);
    if (Route[0] == 'S') InterlockedIncrement(&s_Bc250Dcn.RouteSetAddress);
    else if (Route[0] == 'M' && Route[3] == '1') InterlockedIncrement(&s_Bc250Dcn.RouteMpo1);
    else if (Route[0] == 'M' && Route[3] == '3') InterlockedIncrement(&s_Bc250Dcn.RouteMpo3);
    if (Immediate) InterlockedIncrement(&s_Bc250Dcn.RouteImmediate);
    InterlockedExchange64(&s_Bc250Dcn.RequestQpc, KeQueryPerformanceCounter(NULL).QuadPart);
    InterlockedExchange64(&s_Bc250Dcn.RequestedDcn, (LONG64)address);
    InterlockedExchange64(&s_Bc250Dcn.RequestedSeg, SegmentAddress.QuadPart);
    InterlockedExchange(&s_Bc250Dcn.OnUserSurface, address != s_Bc250Dcn.Plan.GopAddress ? 1 : 0);
    n = InterlockedIncrement(&s_Bc250Dcn.Flips);
    if (n <= 16 || (n % 256) == 0) {
        Bc250DisplayLog("[DCN1_FLIP] #%ld Route=%s VramOff=0x%llX -> HUBP0 0x%llX (readback OK) Tipo=%s FLIP_CONTROL 0x%08X->0x%08X Frame=%u Latchados=%ld Recusas=%ld CopiasOmitidas=%ld\n",
            n, Route, (unsigned long long)SourceVramOffset, (unsigned long long)address,
            Immediate ? "IMEDIATO" : "VSYNC", flipCtl,
            Bc250DcnFlipControlValue(flipCtl, Immediate ? 1 : 0),
            frame,
            (long)s_Bc250Dcn.Latched, (long)s_Bc250Dcn.Refused, (long)s_Bc250Dcn.CopiesSkipped);
    }
    return TRUE;
}

/* Corrida 270 (doc 711): resumo das rotas, baldes do latch e VSYNC contra os
 * quadros do OTG desde o arme. So' leitura do contador de quadros. */
static VOID Bc250DcnFlipLogSummary(_In_ const char *Why)
{
    LARGE_INTEGER freq;
    LONGLONG now = KeQueryPerformanceCounter(&freq).QuadPart;
    UINT64 elapsedUs = (freq.QuadPart > 0 && now > s_Bc250Dcn.ArmQpc) ?
        (UINT64)(((now - s_Bc250Dcn.ArmQpc) * 1000000LL) / freq.QuadPart) : 0ull;
    ULONG frames = Bc250DcnFrameDelta(s_Bc250Dcn.ArmFrame,
        Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK);
    LONG hw = InterlockedCompareExchange(&g_Bc250DisplayState.HardwareVsyncCount, 0, 0) - s_Bc250Dcn.ArmHwVsync;
    LONG vs = InterlockedCompareExchange(&g_Bc250DisplayState.VsyncCount, 0, 0) - s_Bc250Dcn.ArmVsync;
    ULONG mhz = Bc250DcnRefreshMilliHz(frames, elapsedUs);
    Bc250DisplayLog("[DCN1_RESUMO] %s: %llu ms armado | rotas SET_ADDRESS=%ld MPO1=%ld MPO3=%ld imediatos=%ld fora_do_seg1=%ld | "
        "latch em quadros 0=%ld 1=%ld 2=%ld 3+=%ld | latch em ms <16,7=%ld <33,4=%ld <50=%ld 50+=%ld | "
        "OTG %lu quadros = %lu.%03lu Hz; VSYNC do OTG visto %ld (perdidos %ld); VSYNC notificados %ld\n",
        Why, (unsigned long long)(elapsedUs / 1000ull),
        (long)s_Bc250Dcn.RouteSetAddress, (long)s_Bc250Dcn.RouteMpo1, (long)s_Bc250Dcn.RouteMpo3,
        (long)s_Bc250Dcn.RouteImmediate, (long)s_Bc250Dcn.NotSegment1,
        (long)s_Bc250Dcn.LatchFrames[0], (long)s_Bc250Dcn.LatchFrames[1],
        (long)s_Bc250Dcn.LatchFrames[2], (long)s_Bc250Dcn.LatchFrames[3],
        (long)s_Bc250Dcn.LatchUs[0], (long)s_Bc250Dcn.LatchUs[1],
        (long)s_Bc250Dcn.LatchUs[2], (long)s_Bc250Dcn.LatchUs[3],
        (unsigned long)frames, (unsigned long)(mhz / 1000u), (unsigned long)(mhz % 1000u),
        (long)hw, (long)((LONG)frames - hw), (long)vs);
    if (Bc250Dcn2KeysArmed() != 0u) Bc250Dcn2LogSummary(Why);
}

/* D4: chamado no DPC de VSYNC; marca o flip como exibido so' depois do latch. */
static VOID Bc250DcnFlipPollLatch(void)
{
    LONG flipsBefore;
    LONG resolved;
    LONG skipped;
    UINT64 requested;
    LONG64 requestedSeg;
    ULONG flipCtl, lo, hi;

    if (InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0) == 0 || s_Bc250Dcn.Window == NULL)
        return;
    flipsBefore = InterlockedCompareExchange(&s_Bc250Dcn.Flips, 0, 0);
    if (flipsBefore == 0) return;
    /* Pela sequencia, nao pelo segmento: flip repetido para o endereco ja
     * exibido tambem precisa ser resolvido (Corrida 269, doc 711). */
    resolved = InterlockedCompareExchange(&s_Bc250Dcn.ResolvedSeq, 0, 0);
    if (resolved >= flipsBefore) return;
    requestedSeg = InterlockedCompareExchange64(&s_Bc250Dcn.RequestedSeg, 0, 0);
    requested = (UINT64)InterlockedCompareExchange64(&s_Bc250Dcn.RequestedDcn, 0, 0);
    flipCtl = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL);
    lo = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE);
    hi = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_EARLIEST_INUSE_HI);
    if (InterlockedCompareExchange(&s_Bc250Dcn.Flips, 0, 0) != flipsBefore) return; /* flip novo no meio */
    if (Bc250DcnFlipIsPending(flipCtl, lo, hi, requested)) return;
    if (InterlockedCompareExchange(&s_Bc250Dcn.ResolvedSeq, flipsBefore, resolved) != resolved) return;
    skipped = (LONG)Bc250DcnFlipSupersededBetween((uint32_t)resolved, (uint32_t)flipsBefore);
    if (skipped != 0) InterlockedAdd(&s_Bc250Dcn.Superseded, skipped);
    InterlockedExchange64(&s_Bc250Dcn.DisplayedSeg, requestedSeg);
    {
        LARGE_INTEGER freq;
        LONGLONG now = KeQueryPerformanceCounter(&freq).QuadPart;
        LONG us = (freq.QuadPart > 0) ?
            (LONG)(((now - InterlockedCompareExchange64(&s_Bc250Dcn.RequestQpc, 0, 0)) * 1000000LL) / freq.QuadPart) : 0;
        ULONG frames = Bc250DcnFrameDelta((uint32_t)InterlockedCompareExchange(&s_Bc250Dcn.RequestFrame, 0, 0),
            g_Bc250DisplayState.LastOtgFrameCount);
        LONG l = InterlockedIncrement(&s_Bc250Dcn.Latched);
        if (us > s_Bc250Dcn.MaxLatchUs) InterlockedExchange(&s_Bc250Dcn.MaxLatchUs, us);
        InterlockedIncrement(&s_Bc250Dcn.LatchFrames[Bc250DcnLatchFrameBucket(frames)]);
        InterlockedIncrement(&s_Bc250Dcn.LatchUs[Bc250DcnLatchUsBucket(us > 0 ? (uint32_t)us : 0u)]);
        if (l <= 16 || (l % 256) == 0) {
            Bc250DisplayLog("[DCN1_LATCH] #%ld HUBP0 0x%llX latchado (EARLIEST_INUSE=0x%04X:%08X, FLIP_PENDING=0) em %ld us depois do pedido; VSYNC passa a reportar Seg 0x%llX (max %ld us) flip=%ld substituidos=%ld\n",
                l, (unsigned long long)requested, hi & 0xFFFFu, lo, us,
                (unsigned long long)requestedSeg, (long)s_Bc250Dcn.MaxLatchUs,
                (long)flipsBefore, (long)s_Bc250Dcn.Superseded);
        }
        {
            /* Corrida 270 (doc 712): por contagem o resumo nunca saiu numa trava
             * precoce. Agora a cada 256 quadros do OTG (~4 s). */
            static ULONG s_LastSummaryFrame = 0;
            ULONG nowFrame = g_Bc250DisplayState.LastOtgFrameCount & BC250_DCN_FRAME_COUNT_MASK;
            if (Bc250DcnFrameDelta(s_LastSummaryFrame, nowFrame) >= 256u) {
                s_LastSummaryFrame = nowFrame;
                Bc250DcnFlipLogSummary("PERIODICO");
            }
        }
    }
}

/* D4: endereco de segmento que o VSYNC deve reportar (0 = sem flip latchado). */
static LONG64 Bc250DcnFlipDisplayedSegment(void)
{
    if (InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0) == 0) return 0;
    return InterlockedCompareExchange64(&s_Bc250Dcn.DisplayedSeg, 0, 0);
}

/* Lei 5: devolve o HUBP0 ao scanout do GOP antes do BasicDisplay. */
static VOID Bc250DcnFlipRestoreGop(_In_ const char *Why)
{
    ULONG hiRb = 0u, loRb = 0u;
    if (s_Bc250Dcn.Window == NULL) return;
    InterlockedExchange(&s_Bc250Dcn.Armed, 0);
    if (s_Bc250Dcn.GopRead && InterlockedCompareExchange(&s_Bc250Dcn.Flips, 0, 0) != 0) {
        LONG flips = InterlockedCompareExchange(&s_Bc250Dcn.Flips, 0, 0);
        LONG latched = InterlockedCompareExchange(&s_Bc250Dcn.Latched, 0, 0);
        LONG superseded = InterlockedCompareExchange(&s_Bc250Dcn.Superseded, 0, 0);
        LONG pending = flips - InterlockedCompareExchange(&s_Bc250Dcn.ResolvedSeq, 0, 0);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_FLIP_CONTROL, s_Bc250Dcn.Gop.FlipControl);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI, s_Bc250Dcn.Gop.PrimaryAddrHi);
        Bc250DcnWrite(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR, s_Bc250Dcn.Gop.PrimaryAddrLo);
        hiRb = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR_HI);
        loRb = Bc250DcnRead(BC250_DCN_FLIP_REG_HUBPREQ0_PRIMARY_ADDR);
        InterlockedExchange(&s_Bc250Dcn.OnUserSurface, 0);
        Bc250DcnFlipLogSummary(Why);
        Bc250DisplayLog("[DCN1_TEARDOWN] %s: HUBP0 devolvido ao GOP 0x%04X:%08X (readback 0x%04X:%08X %s) FLIP_CONTROL=0x%08X | flips=%ld latchados=%ld substituidos=%ld pendentes=%ld conta=%s recusas=%ld readback_falhou=%ld copias_omitidas=%ld latch_max=%ld us\n",
            Why, s_Bc250Dcn.Gop.PrimaryAddrHi & 0xFFFFu, s_Bc250Dcn.Gop.PrimaryAddrLo,
            hiRb & 0xFFFFu, loRb,
            ((hiRb & 0xFFFFu) == (s_Bc250Dcn.Gop.PrimaryAddrHi & 0xFFFFu) && loRb == s_Bc250Dcn.Gop.PrimaryAddrLo) ? "OK" : "DIVERGE",
            s_Bc250Dcn.Gop.FlipControl,
            (long)flips, (long)latched, (long)superseded, (long)pending,
            (latched + superseded + pending == flips) ? "FECHA" : "DIVERGE",
            (long)s_Bc250Dcn.Refused,
            (long)s_Bc250Dcn.ReadbackFail, (long)s_Bc250Dcn.CopiesSkipped, (long)s_Bc250Dcn.MaxLatchUs);
    }
}

static VOID Bc250DcnFlipUnmap(void)
{
    if (s_Bc250Dcn.Window != NULL) {
        MmUnmapIoSpace((PVOID)s_Bc250Dcn.Window, BC250_DCN_FLIP_WINDOW_BYTES);
        s_Bc250Dcn.Window = NULL;
    }
    s_Bc250Dcn.GopRead = FALSE;
}

/* =========================================================================
 * DCN.2 (doc 713, P9 ampliada pelo Daniel em 12/10/2026): timing real do OTG
 * no VidPn, VSYNC e flip pela interrupcao do DCN, CRC do OTG, cursor por
 * hardware e o lado seguro do conector (EDID real por AUX, HPD so' leitura,
 * apagar/acender o OTG). Cada item tem um bit na chave DcnExt (INF, padrao 0)
 * e falha fechado sozinho. Offsets e sequencias: bc250_dcn_ext_contract.h.
 * ========================================================================= */
/* Janela de CPU da VRAM (256 MB, doc 200): limite para o cursor. */
#define BC250_DCN2_CPU_WINDOW_BYTES 0x10000000ull

typedef struct BC250_DCN2_CRC_BUFFER {
    LONG64 Seg;
    ULONG Crc;
    LONG Samples;
    LONG Changes;
} BC250_DCN2_CRC_BUFFER;

typedef struct BC250_DCN2_RUNTIME {
    ULONG Keys;
    /* 1. timing */
    BC250_DCN_TIMING Timing;
    ULONG TimingRefuse;
    BOOLEAN TimingMeasured;
    BOOLEAN TimingApplied;
    /* 6. EDID / HPD / AUX */
    UCHAR RealEdid[256];
    ULONG RealEdidBytes;
    LONG RealEdidDtd;
    BOOLEAN RealEdidPublished;
    ULONG HpdStatus[BC250_DCN2_HPD_COUNT];
    ULONG AuxChannel;
    ULONG AuxTransactions;
    ULONG AuxFailures;
    UCHAR Dpcd[16];
    BOOLEAN DpcdRead;
    /* 2. interrupcao */
    volatile LONG IrqArmed;
    ULONG SavedGlobalSync;
    ULONG SavedFlipInt;
    volatile LONG64 IrqVstartup;
    volatile LONG64 IrqFlip;
    volatile LONG IrqPending;
    volatile LONG64 IrqLastQpc;
    volatile LONG IrqVsyncNotified;
    volatile LONG IrqCoalesced;
    volatile LONG IrqFallbackTicks;
    /* 3. CRC */
    volatile LONG CrcArmed;
    ULONG SavedCrcCntl;
    ULONG SavedCrcWin[4];
    ULONG LastCrc;
    LONG CrcSamples;
    LONG CrcChanges;
    LONG CrcStableRun;
    LONG CrcStableMax;
    LONG64 CrcLastSeg;
    LONG CrcSegFrames;
    BC250_DCN2_CRC_BUFFER CrcBuf[4];
    /* 4. cursor */
    UINT64 CursorVramOffset;
    PVOID CursorCpu;
    UINT64 CursorDcnAddress[2];
    BOOLEAN CursorVramReady;
    BOOLEAN CursorSaved;
    ULONG CursorSlot;
    ULONG CursorWidth;
    ULONG CursorHeight;
    BOOLEAN CursorHasShape;
    ULONG SavedCursorControl;
    ULONG SavedCursorSettings;
    ULONG SavedCursorAddr;
    ULONG SavedCursorAddrHi;
    ULONG SavedCursorSize;
    ULONG SavedCnvcControl;
    ULONG SavedCnvcScaleBias;
    LONG CursorShapes;
    LONG CursorShapeRefused;
    LONG CursorMoves;
    LONG CursorHidden;
    /* 6. DPMS */
    ULONG SavedBlankControl;
    BOOLEAN BlankSaved;
    LONG Blanks;
    LONG Unblanks;
} BC250_DCN2_RUNTIME;
static BC250_DCN2_RUNTIME s_Bc250Dcn2;
static uint32_t s_Bc250Dcn2CursorScratch[BC250_DCN_CURSOR_MAX * BC250_DCN_CURSOR_MAX];

static BOOLEAN Bc250Dcn2Key(ULONG Bit)
{
    return (s_Bc250Dcn2.Keys & Bit) != 0u && s_Bc250Dcn.Window != NULL && s_Bc250Dcn.GopRead;
}

/* ---- 1. Timing real (so' leitura; chamado no D2, PASSIVE_LEVEL) ---------- */
static BOOLEAN Bc250Dcn2ReadScanPair(ULONG *Frame, ULONG *Vert)
{
    ULONG k;
    for (k = 0u; k < 8u; ++k) {
        ULONG f1 = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK;
        ULONG v = Bc250DcnRead(BC250_DCN2_REG_OTG0_STATUS_POSITION) & BC250_DCN2_VERT_COUNT_MASK;
        ULONG f2 = Bc250DcnRead(BC250_DCN_FLIP_REG_OTG0_STATUS_FRAME_COUNT) & BC250_DCN_FRAME_COUNT_MASK;
        if (f1 == f2) { *Frame = f1; *Vert = v; return TRUE; }
    }
    return FALSE;
}

static VOID Bc250Dcn2MeasureTiming(void)
{
    ULONG f1 = 0u, v1 = 0u, f2 = 0u, v2 = 0u;
    LARGE_INTEGER freq, t1, t2, delay;
    UINT64 elapsed100ns;
    ULONG mhz = 0u;
    if (s_Bc250Dcn.Window == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    s_Bc250Dcn2.TimingRefuse = Bc250DcnTimingDecode(
        Bc250DcnRead(BC250_DCN2_REG_OTG0_H_TOTAL), Bc250DcnRead(BC250_DCN2_REG_OTG0_H_BLANK_START_END),
        Bc250DcnRead(BC250_DCN2_REG_OTG0_H_SYNC_A), Bc250DcnRead(BC250_DCN2_REG_OTG0_H_SYNC_A_CNTL),
        Bc250DcnRead(BC250_DCN2_REG_OTG0_V_TOTAL), Bc250DcnRead(BC250_DCN2_REG_OTG0_V_BLANK_START_END),
        Bc250DcnRead(BC250_DCN2_REG_OTG0_V_SYNC_A), Bc250DcnRead(BC250_DCN2_REG_OTG0_V_SYNC_A_CNTL),
        g_Bc250DisplayState.CurrentMode.Width, g_Bc250DisplayState.CurrentMode.Height,
        &s_Bc250Dcn2.Timing);
    /* Medida: linhas varridas em ~200 ms (quadros * VTotal + delta vertical). */
    if (Bc250Dcn2ReadScanPair(&f1, &v1)) {
        t1 = KeQueryPerformanceCounter(&freq);
        delay.QuadPart = -2000000LL;
        (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        if (Bc250Dcn2ReadScanPair(&f2, &v2)) {
            t2 = KeQueryPerformanceCounter(NULL);
            elapsed100ns = (freq.QuadPart > 0) ?
                (UINT64)(((t2.QuadPart - t1.QuadPart) * 10000000LL) / freq.QuadPart) : 0ull;
            mhz = Bc250DcnRefreshFromScan(Bc250DcnFrameDelta(f1, f2), (int32_t)v2 - (int32_t)v1,
                s_Bc250Dcn2.Timing.VTotal, elapsed100ns);
        }
    }
    s_Bc250Dcn2.TimingRefuse |= Bc250DcnTimingApplyRefresh(&s_Bc250Dcn2.Timing, mhz);
    s_Bc250Dcn2.TimingMeasured = (s_Bc250Dcn2.TimingRefuse == 0u) ? TRUE : FALSE;
    Bc250DisplayLog("[DCN2_TIMING] OTG0 lido: ativo %ux%u total %ux%u porch/sync H %u/%u V %u/%u pol H%c V%c | medido %lu.%03lu Hz, clock de pixel %llu Hz | recusa=0x%02X (1=total 2=ativo!=GOP 4=sync 8=refresh) | constante antiga: 56,130 Hz, 173611437 Hz\n",
        s_Bc250Dcn2.Timing.HActive, s_Bc250Dcn2.Timing.VActive,
        s_Bc250Dcn2.Timing.HTotal, s_Bc250Dcn2.Timing.VTotal,
        s_Bc250Dcn2.Timing.HFrontPorch, s_Bc250Dcn2.Timing.HSyncWidth,
        s_Bc250Dcn2.Timing.VFrontPorch, s_Bc250Dcn2.Timing.VSyncWidth,
        s_Bc250Dcn2.Timing.HSyncPositive ? '+' : '-', s_Bc250Dcn2.Timing.VSyncPositive ? '+' : '-',
        (unsigned long)(mhz / 1000u), (unsigned long)(mhz % 1000u),
        (unsigned long long)s_Bc250Dcn2.Timing.PixelRateHz, s_Bc250Dcn2.TimingRefuse);
}

/* ---- 6. AUX por software (dce_aux.c), PASSIVE_LEVEL ----------------------- */
static BOOLEAN Bc250Dcn2AuxWait(ULONG Ch, ULONG Mask, ULONG Want, ULONG MaxUs, ULONG *Last)
{
    ULONG us, v = 0u;
    for (us = 0u; us <= MaxUs; us += 10u) {
        v = Bc250DcnRead(BC250_DCN2_REG_AUX_SW_STATUS(Ch));
        if ((v & Mask) == Want) { if (Last) *Last = v; return TRUE; }
        KeStallExecutionProcessor(10u);
    }
    if (Last) *Last = v;
    return FALSE;
}

static BOOLEAN Bc250Dcn2AuxAcquire(ULONG Ch, ULONG *SavedControl)
{
    ULONG arb = Bc250DcnRead(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch));
    ULONG ctl, k;
    if (((arb & BC250_DCN2_AUX_REG_RW_CNTL_STATUS_MASK) >> BC250_DCN2_AUX_REG_RW_CNTL_STATUS_SHIFT) ==
        BC250_DCN2_DMCU_CAN_ACCESS_AUX) return FALSE;
    ctl = Bc250DcnRead(BC250_DCN2_REG_AUX_CONTROL(Ch));
    *SavedControl = ctl;
    if ((ctl & BC250_DCN2_AUX_EN) == 0u) {
        /* acquire_engine: AUX_EN com AUX_RESET, espera RESET_DONE, solta o reset. */
        ULONG on = Bc250DcnField(ctl, BC250_DCN2_AUX_EN | 0x10u, BC250_DCN2_AUX_EN | 0x10u);
        Bc250DcnWrite(BC250_DCN2_REG_AUX_CONTROL(Ch), on);
        for (k = 0u; k < 11u && (Bc250DcnRead(BC250_DCN2_REG_AUX_CONTROL(Ch)) & 0x20u) == 0u; ++k)
            KeStallExecutionProcessor(1u);
        Bc250DcnWrite(BC250_DCN2_REG_AUX_CONTROL(Ch), Bc250DcnField(on, 0x10u, 0u));
        for (k = 0u; k < 11u && (Bc250DcnRead(BC250_DCN2_REG_AUX_CONTROL(Ch)) & 0x20u) != 0u; ++k)
            KeStallExecutionProcessor(1u);
    }
    Bc250DcnWrite(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch),
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch)),
            BC250_DCN2_AUX_SW_USE_AUX_REG_REQ, BC250_DCN2_AUX_SW_USE_AUX_REG_REQ));
    arb = Bc250DcnRead(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch));
    return (((arb & BC250_DCN2_AUX_REG_RW_CNTL_STATUS_MASK) >> BC250_DCN2_AUX_REG_RW_CNTL_STATUS_SHIFT) ==
        BC250_DCN2_SW_CAN_ACCESS_AUX) ? TRUE : FALSE;
}

static VOID Bc250Dcn2AuxRelease(ULONG Ch, ULONG SavedControl)
{
    ULONG arb = Bc250DcnRead(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch));
    arb = Bc250DcnField(arb, BC250_DCN2_AUX_SW_DONE_USING_AUX_REG, BC250_DCN2_AUX_SW_DONE_USING_AUX_REG);
    arb = Bc250DcnField(arb, BC250_DCN2_AUX_SW_USE_AUX_REG_REQ, 0u);
    Bc250DcnWrite(BC250_DCN2_REG_AUX_ARB_CONTROL(Ch), arb);
    /* Lei 5: o AUX volta como o GOP deixou. */
    Bc250DcnWrite(BC250_DCN2_REG_AUX_CONTROL(Ch), SavedControl);
}

static VOID Bc250Dcn2AuxPutByte(ULONG Ch, ULONG *Shadow, ULONG Byte)
{
    *Shadow = Bc250DcnField(*Shadow, BC250_DCN2_AUX_SW_DATA_MASK, Byte << BC250_DCN2_AUX_SW_DATA_SHIFT);
    Bc250DcnWrite(BC250_DCN2_REG_AUX_SW_DATA(Ch), *Shadow);
}

/* Uma transacao: devolve o codigo de resposta (BC250_AUX_REPLY_*) ou INVALID. */
static ULONG Bc250Dcn2AuxXfer(ULONG Ch, ULONG Action, ULONG Address,
    _In_reads_opt_(WrLen) const UCHAR *Wr, ULONG WrLen, ULONG RdLen,
    _Out_writes_opt_(RdLen) UCHAR *Rd, ULONG *RdGot)
{
    UCHAR hdr[4];
    ULONG hdrLen, len, data, status = 0u, replied, i, k;
    BOOLEAN isWrite = (Action == BC250_AUX_ACTION_I2C_WRITE || Action == BC250_AUX_ACTION_I2C_WRITE_MOT) ? TRUE : FALSE;
    if (RdGot) *RdGot = 0u;
    s_Bc250Dcn2.AuxTransactions++;
    hdrLen = Bc250DcnAuxComposeHeader(Action, Address, isWrite ? WrLen : RdLen, hdr);
    len = hdrLen + (isWrite ? WrLen : 0u);
    Bc250DcnWrite(BC250_DCN2_REG_AUX_INTERRUPT_CONTROL(Ch),
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_AUX_INTERRUPT_CONTROL(Ch)),
            BC250_DCN2_AUX_SW_DONE_ACK, BC250_DCN2_AUX_SW_DONE_ACK));
    (void)Bc250Dcn2AuxWait(Ch, BC250_DCN2_AUX_SW_DONE, 0u, 2400u, NULL);
    Bc250DcnWrite(BC250_DCN2_REG_AUX_SW_CONTROL(Ch),
        Bc250DcnField(Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_AUX_SW_CONTROL(Ch)),
            BC250_DCN2_AUX_SW_START_DELAY_MASK, 0u),
            BC250_DCN2_AUX_SW_WR_BYTES_MASK, len << BC250_DCN2_AUX_SW_WR_BYTES_SHIFT));
    /* indice 0 sem autoincremento no 1o byte; depois autoincremento */
    data = Bc250DcnRead(BC250_DCN2_REG_AUX_SW_DATA(Ch));
    data = Bc250DcnField(data, BC250_DCN2_AUX_SW_INDEX_MASK | BC250_DCN2_AUX_SW_DATA_RW |
        BC250_DCN2_AUX_SW_AUTOINC_DISABLE, BC250_DCN2_AUX_SW_AUTOINC_DISABLE);
    Bc250Dcn2AuxPutByte(Ch, &data, hdr[0]);
    data = Bc250DcnField(data, BC250_DCN2_AUX_SW_AUTOINC_DISABLE, 0u);
    Bc250Dcn2AuxPutByte(Ch, &data, hdr[1]);
    Bc250Dcn2AuxPutByte(Ch, &data, hdr[2]);
    if (hdrLen == 4u) Bc250Dcn2AuxPutByte(Ch, &data, hdr[3]);
    if (isWrite && Wr != NULL) {
        for (i = 0u; i < WrLen; ++i) Bc250Dcn2AuxPutByte(Ch, &data, Wr[i]);
    }
    Bc250DcnWrite(BC250_DCN2_REG_AUX_SW_CONTROL(Ch),
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_AUX_SW_CONTROL(Ch)), BC250_DCN2_AUX_SW_GO, BC250_DCN2_AUX_SW_GO));
    if (!Bc250Dcn2AuxWait(Ch, BC250_DCN2_AUX_SW_DONE, BC250_DCN2_AUX_SW_DONE, 14400u, &status) ||
        (status & (BC250_DCN2_AUX_SW_HPD_DISCON | BC250_DCN2_AUX_SW_RX_TIMEOUT |
            BC250_DCN2_AUX_SW_RX_TIMEOUT_STATE_MASK | BC250_DCN2_AUX_SW_RX_INVALID_STOP |
            BC250_DCN2_AUX_SW_RX_RECV_NO_DET | BC250_DCN2_AUX_SW_RX_RECV_INVALID_H |
            BC250_DCN2_AUX_SW_RX_RECV_INVALID_L)) != 0u) {
        s_Bc250Dcn2.AuxFailures++;
        return BC250_AUX_REPLY_INVALID;
    }
    replied = (status & BC250_DCN2_AUX_SW_REPLY_BYTE_COUNT_MASK) >> BC250_DCN2_AUX_SW_REPLY_BYTE_COUNT_SHIFT;
    if (replied == 0u) { s_Bc250Dcn2.AuxFailures++; return BC250_AUX_REPLY_INVALID; }
    data = Bc250DcnRead(BC250_DCN2_REG_AUX_SW_DATA(Ch));
    data = Bc250DcnField(data, BC250_DCN2_AUX_SW_INDEX_MASK | BC250_DCN2_AUX_SW_AUTOINC_DISABLE |
        BC250_DCN2_AUX_SW_DATA_RW, BC250_DCN2_AUX_SW_AUTOINC_DISABLE | BC250_DCN2_AUX_SW_DATA_RW);
    Bc250DcnWrite(BC250_DCN2_REG_AUX_SW_DATA(Ch), data);
    k = Bc250DcnAuxDecodeReply(((Bc250DcnRead(BC250_DCN2_REG_AUX_SW_DATA(Ch)) &
        BC250_DCN2_AUX_SW_DATA_MASK) >> BC250_DCN2_AUX_SW_DATA_SHIFT) >> 4);
    if (k != BC250_AUX_REPLY_ACK) return k;
    for (i = 0u; i + 1u < replied && i < RdLen && Rd != NULL; ++i) {
        Rd[i] = (UCHAR)((Bc250DcnRead(BC250_DCN2_REG_AUX_SW_DATA(Ch)) & BC250_DCN2_AUX_SW_DATA_MASK) >>
            BC250_DCN2_AUX_SW_DATA_SHIFT);
    }
    if (RdGot) *RdGot = i;
    return BC250_AUX_REPLY_ACK;
}

static ULONG Bc250Dcn2AuxRetry(ULONG Ch, ULONG Action, ULONG Address, const UCHAR *Wr, ULONG WrLen,
    ULONG RdLen, UCHAR *Rd, ULONG *RdGot)
{
    ULONG r = BC250_AUX_REPLY_INVALID, k;
    for (k = 0u; k < 7u; ++k) {   /* AUX_DEFER_RETRY_COUNTER = 6 */
        r = Bc250Dcn2AuxXfer(Ch, Action, Address, Wr, WrLen, RdLen, Rd, RdGot);
        if (r != BC250_AUX_REPLY_DEFER && r != BC250_AUX_REPLY_I2C_DEFER) break;
        KeStallExecutionProcessor(500u);
    }
    return r;
}

/* EDID por I2C sobre AUX (0x50): escrita MOT do offset 0, leituras MOT de 16
 * bytes, parada sem MOT. So' leitura do monitor. */
static VOID Bc250Dcn2ReadRealEdid(void)
{
    ULONG ch, saved = 0u, got, r, off, blocks = 1u, i;
    UCHAR zero = 0u;
    BOOLEAN acquired;
    for (i = 0u; i < BC250_DCN2_HPD_COUNT; ++i) {
        s_Bc250Dcn2.HpdStatus[i] = Bc250DcnRead(BC250_DCN2_REG_HPD_INT_STATUS(i));
    }
    ch = ((s_Bc250Dcn2.HpdStatus[0] & BC250_DCN2_HPD_SENSE) == 0u &&
        (s_Bc250Dcn2.HpdStatus[1] & BC250_DCN2_HPD_SENSE) != 0u) ? 1u : 0u;
    s_Bc250Dcn2.AuxChannel = ch;
    Bc250DisplayLog("[DCN2_HPD] HPD0 STATUS=0x%08X (sense=%u) HPD1 STATUS=0x%08X (sense=%u) -> AUX%u (so' leitura)\n",
        s_Bc250Dcn2.HpdStatus[0], (s_Bc250Dcn2.HpdStatus[0] & BC250_DCN2_HPD_SENSE) ? 1u : 0u,
        s_Bc250Dcn2.HpdStatus[1], (s_Bc250Dcn2.HpdStatus[1] & BC250_DCN2_HPD_SENSE) ? 1u : 0u, ch);
    acquired = Bc250Dcn2AuxAcquire(ch, &saved);
    if (!acquired) {
        Bc250DisplayLog("[DCN2_EDID] RECUSADO: AUX%u nao cedido ao software (ARB=0x%08X CONTROL=0x%08X); EDID sintetizado mantido\n",
            ch, Bc250DcnRead(BC250_DCN2_REG_AUX_ARB_CONTROL(ch)), Bc250DcnRead(BC250_DCN2_REG_AUX_CONTROL(ch)));
        Bc250Dcn2AuxRelease(ch, saved);
        return;
    }
    r = Bc250Dcn2AuxRetry(ch, BC250_AUX_ACTION_DP_READ, 0x00000u, NULL, 0u, 16u, s_Bc250Dcn2.Dpcd, &got);
    s_Bc250Dcn2.DpcdRead = (r == BC250_AUX_REPLY_ACK && got == 16u) ? TRUE : FALSE;
    Bc250DisplayLog("[DCN2_AUX] DPCD 0x000 x16 resposta=%lu bytes=%lu: REV=0x%02X MAX_LINK_RATE=0x%02X MAX_LANES=0x%02X\n",
        r, got, s_Bc250Dcn2.Dpcd[0], s_Bc250Dcn2.Dpcd[1], s_Bc250Dcn2.Dpcd[2] & 0x1Fu);
    r = Bc250Dcn2AuxRetry(ch, BC250_AUX_ACTION_I2C_WRITE_MOT, BC250_EDID_I2C_ADDRESS, &zero, 1u, 0u, NULL, NULL);
    for (off = 0u; r == BC250_AUX_REPLY_ACK && off < blocks * 128u && off < sizeof(s_Bc250Dcn2.RealEdid); off += 16u) {
        r = Bc250Dcn2AuxRetry(ch, BC250_AUX_ACTION_I2C_READ_MOT, BC250_EDID_I2C_ADDRESS, NULL, 0u, 16u,
            &s_Bc250Dcn2.RealEdid[off], &got);
        if (r == BC250_AUX_REPLY_ACK && got != 16u) r = BC250_AUX_REPLY_INVALID;
        if (off == 112u && r == BC250_AUX_REPLY_ACK && s_Bc250Dcn2.RealEdid[126] != 0u) blocks = 2u;
    }
    (void)Bc250Dcn2AuxRetry(ch, BC250_AUX_ACTION_I2C_READ, BC250_EDID_I2C_ADDRESS, NULL, 0u, 0u, NULL, NULL);
    Bc250Dcn2AuxRelease(ch, saved);
    s_Bc250Dcn2.RealEdidBytes = (r == BC250_AUX_REPLY_ACK) ? off : 0u;
    s_Bc250Dcn2.RealEdidDtd = (s_Bc250Dcn2.RealEdidBytes >= 128u) ?
        Bc250DcnEdidFindTiming(s_Bc250Dcn2.RealEdid, &s_Bc250Dcn2.Timing) : -1;
    Bc250DisplayLog("[DCN2_EDID] AUX%u: %lu bytes lidos (resposta=%lu, blocos=%lu, transacoes=%lu, falhas=%lu) checksum=%s fabricante=%02X%02X produto=%02X%02X DTD_igual_ao_OTG=%ld\n",
        ch, s_Bc250Dcn2.RealEdidBytes, r, blocks, s_Bc250Dcn2.AuxTransactions, s_Bc250Dcn2.AuxFailures,
        (s_Bc250Dcn2.RealEdidBytes >= 128u && Bc250DcnEdidBlockValid(s_Bc250Dcn2.RealEdid)) ? "OK" : "FALHOU",
        s_Bc250Dcn2.RealEdid[8], s_Bc250Dcn2.RealEdid[9], s_Bc250Dcn2.RealEdid[11], s_Bc250Dcn2.RealEdid[10],
        (long)s_Bc250Dcn2.RealEdidDtd);
    for (i = 0u; i + 16u <= s_Bc250Dcn2.RealEdidBytes; i += 16u) {
        const UCHAR *e = &s_Bc250Dcn2.RealEdid[i];
        Bc250DisplayLog("[DCN2_EDID %03lu] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            i, e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7], e[8], e[9], e[10], e[11], e[12], e[13], e[14], e[15]);
    }
}

/* ---- 2. Interrupcao do DCN (VBLANK1 = OTG0 VSTARTUP; PFLIP1 = HUBP0) ------ */
static VOID Bc250Dcn2IrqArm(void)
{
    ULONG g = Bc250DcnRead(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS);
    ULONG f = Bc250DcnRead(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT);
    s_Bc250Dcn2.SavedGlobalSync = g;
    s_Bc250Dcn2.SavedFlipInt = f;
    /* dal_irq_service_set: ack, depois enable (Lei 6, so' o bit do enable). */
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS,
        Bc250DcnField(g, BC250_DCN2_VSTARTUP_EVENT_CLEAR | BC250_DCN2_VSTARTUP_INT_EN,
            BC250_DCN2_VSTARTUP_EVENT_CLEAR | BC250_DCN2_VSTARTUP_INT_EN));
    Bc250DcnWrite(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT,
        Bc250DcnField(f, BC250_DCN2_SURFACE_FLIP_CLEAR | BC250_DCN2_SURFACE_FLIP_INT_MASK,
            BC250_DCN2_SURFACE_FLIP_CLEAR | BC250_DCN2_SURFACE_FLIP_INT_MASK));
    InterlockedExchange(&s_Bc250Dcn2.IrqArmed, 1);
    Bc250DisplayLog("[DCN2_IRQ] ARMADO: OTG0 VSTARTUP (IH cliente 0x%02X fonte 0x%02X) e flip do HUBP0 (fonte 0x%02X); GLOBAL_SYNC 0x%08X->0x%08X FLIP_INT 0x%08X->0x%08X; o timer de 4 ms fica como rede\n",
        BC250_IH_CLIENTID_DCE, BC250_IH_SRCID_DCN_OTG0_VSTARTUP, BC250_IH_SRCID_DCN_HUBP0_FLIP,
        g, Bc250DcnRead(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS),
        f, Bc250DcnRead(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT));
}

static VOID Bc250Dcn2IrqDisarm(void)
{
    if (InterlockedExchange(&s_Bc250Dcn2.IrqArmed, 0) == 0 || s_Bc250Dcn.Window == NULL) return;
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS),
            BC250_DCN2_VSTARTUP_INT_EN | BC250_DCN2_VSTARTUP_EVENT_CLEAR,
            (s_Bc250Dcn2.SavedGlobalSync & BC250_DCN2_VSTARTUP_INT_EN) | BC250_DCN2_VSTARTUP_EVENT_CLEAR));
    Bc250DcnWrite(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT),
            BC250_DCN2_SURFACE_FLIP_INT_MASK | BC250_DCN2_SURFACE_FLIP_CLEAR,
            (s_Bc250Dcn2.SavedFlipInt & BC250_DCN2_SURFACE_FLIP_INT_MASK) | BC250_DCN2_SURFACE_FLIP_CLEAR));
}

/* Chamado do ISR do IH (DIRQL) para o cliente DCE. So' ack e enfileira o DPC. */
BOOLEAN Bc250DisplayDcnIsr(ULONG SrcId)
{
    if (InterlockedCompareExchange(&s_Bc250Dcn2.IrqArmed, 0, 0) == 0 || s_Bc250Dcn.Window == NULL)
        return FALSE;
    if (SrcId == BC250_IH_SRCID_DCN_OTG0_VSTARTUP) {
        Bc250DcnWrite(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS,
            Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_OTG0_GLOBAL_SYNC_STATUS),
                BC250_DCN2_VSTARTUP_EVENT_CLEAR, BC250_DCN2_VSTARTUP_EVENT_CLEAR));
        InterlockedIncrement64(&s_Bc250Dcn2.IrqVstartup);
        InterlockedIncrement(&s_Bc250Dcn2.IrqPending);
        InterlockedExchange64(&s_Bc250Dcn2.IrqLastQpc, KeQueryPerformanceCounter(NULL).QuadPart);
    } else if (SrcId == BC250_IH_SRCID_DCN_HUBP0_FLIP) {
        Bc250DcnWrite(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT,
            Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_HUBPREQ0_FLIP_INTERRUPT),
                BC250_DCN2_SURFACE_FLIP_CLEAR, BC250_DCN2_SURFACE_FLIP_CLEAR));
        InterlockedIncrement64(&s_Bc250Dcn2.IrqFlip);
    } else {
        return FALSE;
    }
    if (g_Bc250DisplayState.VsyncTimerInitialized) {
        (void)KeInsertQueueDpc(&g_Bc250DisplayState.VsyncDpc, NULL, NULL);
    }
    return TRUE;
}

/* No DPC de VSYNC: com a interrupcao viva (<50 ms), so' ela gera VSYNC; o
 * timer de 4 ms vira rede. Devolve 1 = notificar, 0 = nao, -1 = sem IRQ. */
static LONG Bc250Dcn2VsyncFromIrq(ULONG CurrentFrame)
{
    LARGE_INTEGER freq;
    LONGLONG now, last;
    LONG pending;
    if (InterlockedCompareExchange(&s_Bc250Dcn2.IrqArmed, 0, 0) == 0) return -1;
    now = KeQueryPerformanceCounter(&freq).QuadPart;
    last = InterlockedCompareExchange64(&s_Bc250Dcn2.IrqLastQpc, 0, 0);
    if (last == 0 || freq.QuadPart <= 0 || (now - last) > freq.QuadPart / 20) {
        InterlockedIncrement(&s_Bc250Dcn2.IrqFallbackTicks);
        return -1;
    }
    pending = InterlockedExchange(&s_Bc250Dcn2.IrqPending, 0);
    if (pending <= 0) return 0;
    if (pending > 1) InterlockedAdd(&s_Bc250Dcn2.IrqCoalesced, pending - 1);
    g_Bc250DisplayState.LastOtgFrameCount = CurrentFrame;
    InterlockedIncrement(&g_Bc250DisplayState.HardwareVsyncCount);
    InterlockedIncrement(&s_Bc250Dcn2.IrqVsyncNotified);
    return 1;
}

/* ---- 3. CRC do OTG ------------------------------------------------------- */
static VOID Bc250Dcn2CrcArm(void)
{
    s_Bc250Dcn2.SavedCrcCntl = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC_CNTL);
    s_Bc250Dcn2.SavedCrcWin[0] = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_X);
    s_Bc250Dcn2.SavedCrcWin[1] = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_Y);
    s_Bc250Dcn2.SavedCrcWin[2] = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_X);
    s_Bc250Dcn2.SavedCrcWin[3] = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_Y);
    /* optc1_configure_crc: zera, janelas, depois CONT_EN/SELECT/EN. */
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC_CNTL, 0u);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_X, Bc250DcnCrcWindowX(g_Bc250DisplayState.CurrentMode.Width));
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_Y, Bc250DcnCrcWindowY(g_Bc250DisplayState.CurrentMode.Height));
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_X, Bc250DcnCrcWindowX(g_Bc250DisplayState.CurrentMode.Width));
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_Y, Bc250DcnCrcWindowY(g_Bc250DisplayState.CurrentMode.Height));
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC_CNTL, Bc250DcnCrcCntlEnable(0u));
    InterlockedExchange(&s_Bc250Dcn2.CrcArmed, 1);
    Bc250DisplayLog("[DCN2_CRC] ARMADO: CRC0 continuo do OTG0 no quadro ativo inteiro (CNTL 0x%08X->0x%08X); NAO e' prova byte a byte (sem gabarito do algoritmo): mede se o quadro exibido muda\n",
        s_Bc250Dcn2.SavedCrcCntl, Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC_CNTL));
}

static VOID Bc250Dcn2CrcDisarm(void)
{
    if (InterlockedExchange(&s_Bc250Dcn2.CrcArmed, 0) == 0 || s_Bc250Dcn.Window == NULL) return;
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC_CNTL, 0u);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_X, s_Bc250Dcn2.SavedCrcWin[0]);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWA_Y, s_Bc250Dcn2.SavedCrcWin[1]);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_X, s_Bc250Dcn2.SavedCrcWin[2]);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC0_WINDOWB_Y, s_Bc250Dcn2.SavedCrcWin[3]);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_CRC_CNTL, s_Bc250Dcn2.SavedCrcCntl);
}

/* Uma amostra por quadro novo (DPC de VSYNC). */
static VOID Bc250Dcn2CrcSample(void)
{
    ULONG rg, b, crc, k;
    LONG64 seg;
    if (InterlockedCompareExchange(&s_Bc250Dcn2.CrcArmed, 0, 0) == 0 || s_Bc250Dcn.Window == NULL) return;
    rg = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_DATA_RG);
    b = Bc250DcnRead(BC250_DCN2_REG_OTG0_CRC0_DATA_B) & BC250_DCN2_CRC_B_MASK;
    crc = rg ^ (b * 0x9E37u);
    s_Bc250Dcn2.CrcSamples++;
    if (s_Bc250Dcn2.CrcSamples > 1 && crc != s_Bc250Dcn2.LastCrc) {
        s_Bc250Dcn2.CrcChanges++;
        s_Bc250Dcn2.CrcStableRun = 0;
    } else if (++s_Bc250Dcn2.CrcStableRun > s_Bc250Dcn2.CrcStableMax) {
        s_Bc250Dcn2.CrcStableMax = s_Bc250Dcn2.CrcStableRun;
    }
    s_Bc250Dcn2.LastCrc = crc;
    /* Por buffer exibido: so' depois de 2 quadros parados no mesmo buffer. */
    seg = InterlockedCompareExchange64(&s_Bc250Dcn.DisplayedSeg, 0, 0);
    if (seg != s_Bc250Dcn2.CrcLastSeg) { s_Bc250Dcn2.CrcLastSeg = seg; s_Bc250Dcn2.CrcSegFrames = 0; return; }
    if (++s_Bc250Dcn2.CrcSegFrames < 2 || seg == 0) return;
    for (k = 0u; k < 4u; ++k) {
        if (s_Bc250Dcn2.CrcBuf[k].Samples != 0 && s_Bc250Dcn2.CrcBuf[k].Seg == seg) break;
    }
    if (k == 4u) {
        for (k = 0u; k < 4u && s_Bc250Dcn2.CrcBuf[k].Samples != 0; ++k) { }
        if (k == 4u) return;
        s_Bc250Dcn2.CrcBuf[k].Seg = seg;
        s_Bc250Dcn2.CrcBuf[k].Crc = crc;
    } else if (s_Bc250Dcn2.CrcBuf[k].Crc != crc) {
        s_Bc250Dcn2.CrcBuf[k].Changes++;
        s_Bc250Dcn2.CrcBuf[k].Crc = crc;
    }
    s_Bc250Dcn2.CrcBuf[k].Samples++;
}

/* ---- 4. Cursor por hardware (HUBP0 + DPP0) -------------------------------- */
VOID Bc250DisplaySetDcnCursorVram(UINT64 VramOffset, UINT64 Bytes)
{
    UINT64 a0 = 0ull, a1 = 0ull;
    if (s_Bc250Dcn2.CursorCpu != NULL) return;
    UINT64 scanoutOffset = (g_Bc250SdmaPresent.ScanoutGpuAddress >= g_Bc250SdmaPresent.VramMcStart) ?
        g_Bc250SdmaPresent.ScanoutGpuAddress - g_Bc250SdmaPresent.VramMcStart : ~(UINT64)0;
    s_Bc250Dcn2.CursorVramReady = FALSE;
    if (Bytes < 2ull * BC250_DCN_CURSOR_BYTES || scanoutOffset == ~(UINT64)0 || !s_Bc250Dcn.GopRead ||
        !Bc250DcnFlipAddressForVramOffset(s_Bc250Dcn.Plan.GopAddress, scanoutOffset, VramOffset,
            BC250_DCN_CURSOR_BYTES, BC250_DCN2_CPU_WINDOW_BYTES, &a0) ||
        !Bc250DcnFlipAddressForVramOffset(s_Bc250Dcn.Plan.GopAddress, scanoutOffset,
            VramOffset + BC250_DCN_CURSOR_BYTES, BC250_DCN_CURSOR_BYTES,
            BC250_DCN2_CPU_WINDOW_BYTES, &a1) ||
        g_Bc250DisplayState.FrameBufferPhysical.QuadPart == 0 ||
        !NT_SUCCESS(Bc250VramCpuMapMemory(&g_Bc250DisplayState.DxgkInterface,
            (ULONGLONG)g_Bc250DisplayState.FrameBufferPhysical.QuadPart + VramOffset,
            (ULONG)(2u * BC250_DCN_CURSOR_BYTES), MmWriteCombined, &s_Bc250Dcn2.CursorCpu)) ||
        s_Bc250Dcn2.CursorCpu == NULL) {
        s_Bc250Dcn2.CursorCpu = NULL;
        Bc250DisplayLog("[DCN2_CURSOR] RECUSADO: buffer de cursor VRAM 0x%llX (%llu bytes) fora da janela ou GOP nao lido; o Windows mantem o cursor por software\n",
            (unsigned long long)VramOffset, (unsigned long long)Bytes);
        return;
    }
    s_Bc250Dcn2.CursorVramOffset = VramOffset;
    s_Bc250Dcn2.CursorDcnAddress[0] = a0;
    s_Bc250Dcn2.CursorDcnAddress[1] = a1;
    s_Bc250Dcn2.CursorVramReady = TRUE;
    Bc250DisplayLog("[DCN2_CURSOR] buffer: VRAM 0x%llX, dois slots de %u bytes no DCN em 0x%llX e 0x%llX\n",
        (unsigned long long)VramOffset, BC250_DCN_CURSOR_BYTES, (unsigned long long)a0, (unsigned long long)a1);
}

ULONG Bc250DisplayDcnCursorReady(void)
{
    return (Bc250Dcn2Key(BC250_DCN2_KEY_CURSOR) && s_Bc250Dcn2.CursorVramReady) ? 1u : 0u;
}

static VOID Bc250Dcn2CursorSave(void)
{
    if (s_Bc250Dcn2.CursorSaved) return;
    s_Bc250Dcn2.SavedCursorControl = Bc250DcnRead(BC250_DCN2_REG_CURSOR0_CONTROL);
    s_Bc250Dcn2.SavedCursorSettings = Bc250DcnRead(BC250_DCN2_REG_HUBPREQ0_CURSOR_SETTINGS);
    s_Bc250Dcn2.SavedCursorAddr = Bc250DcnRead(BC250_DCN2_REG_CURSOR0_SURFACE_ADDRESS);
    s_Bc250Dcn2.SavedCursorAddrHi = Bc250DcnRead(BC250_DCN2_REG_CURSOR0_SURFACE_ADDR_HIGH);
    s_Bc250Dcn2.SavedCursorSize = Bc250DcnRead(BC250_DCN2_REG_CURSOR0_SIZE);
    s_Bc250Dcn2.SavedCnvcControl = Bc250DcnRead(BC250_DCN2_REG_CNVC_CUR0_CONTROL);
    s_Bc250Dcn2.SavedCnvcScaleBias = Bc250DcnRead(BC250_DCN2_REG_CNVC_CUR0_FP_SCALE_BIAS);
    s_Bc250Dcn2.CursorSaved = TRUE;
}

static NTSTATUS Bc250Dcn2CursorSetShape(CONST DXGKARG_SETPOINTERSHAPE *Shape)
{
    ULONG slot, i, hiAddr, loAddr, ctl, cnvc;
    volatile ULONG *dst;
    LONG n;
    if (!Bc250DisplayDcnCursorReady()) return STATUS_NOT_SUPPORTED;
    /* So' cor (ARGB) nesta rodada: o WDK nao deixa claro se Height do
     * monocromatico conta as duas mascaras, e errar ali leria alem do buffer. */
    if (Shape->Flags.Value != BC250_DCN_POINTER_COLOR || Shape->pPixels == NULL ||
        !Bc250DcnCursorConvert(Shape->Flags.Value, Shape->Width, Shape->Height, Shape->Pitch,
            (const uint8_t *)Shape->pPixels, s_Bc250Dcn2CursorScratch)) {
        n = InterlockedIncrement(&s_Bc250Dcn2.CursorShapeRefused);
        if (n <= 8) {
            Bc250DisplayLog("[DCN2_CURSOR] forma RECUSADA #%ld: Flags=0x%X %ux%u pitch=%u (so' cor ARGB ate 64x64 nesta rodada); o Windows usa o cursor por software\n",
                n, Shape->Flags.Value, Shape->Width, Shape->Height, Shape->Pitch);
        }
        return STATUS_NOT_SUPPORTED;
    }
    Bc250Dcn2CursorSave();
    slot = s_Bc250Dcn2.CursorSlot ^ 1u;
    dst = (volatile ULONG *)((PUCHAR)s_Bc250Dcn2.CursorCpu + (SIZE_T)slot * BC250_DCN_CURSOR_BYTES);
    for (i = 0u; i < BC250_DCN_CURSOR_MAX * BC250_DCN_CURSOR_MAX; ++i) dst[i] = s_Bc250Dcn2CursorScratch[i];
    KeMemoryBarrier();
    s_Bc250Dcn2.CursorWidth = Shape->Width;
    s_Bc250Dcn2.CursorHeight = Shape->Height;
    hiAddr = (ULONG)(s_Bc250Dcn2.CursorDcnAddress[slot] >> 32) & BC250_DCN_PRIMARY_ADDR_HIGH_MASK;
    loAddr = (ULONG)s_Bc250Dcn2.CursorDcnAddress[slot];
    /* hubp2_cursor_set_attributes: endereco, tamanho, controle, settings. */
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SURFACE_ADDR_HIGH, hiAddr);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SURFACE_ADDRESS, loAddr);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SIZE,
        ((BC250_DCN_CURSOR_MAX & 0x1FFu) << BC250_DCN2_CURSOR_WIDTH_SHIFT) | (BC250_DCN_CURSOR_MAX & BC250_DCN2_CURSOR_HEIGHT_MASK));
    ctl = Bc250DcnRead(BC250_DCN2_REG_CURSOR0_CONTROL);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_CONTROL,
        Bc250DcnCursorControl(ctl, BC250_DCN_CURSOR_MAX, (ctl & BC250_DCN2_CURSOR_ENABLE) ? 1u : 0u));
    Bc250DcnWrite(BC250_DCN2_REG_HUBPREQ0_CURSOR_SETTINGS,
        Bc250DcnField(0u, BC250_DCN2_CURSOR0_CHUNK_HDL_ADJUST_MASK, 3u << BC250_DCN2_CURSOR0_CHUNK_HDL_ADJUST_SHIFT));
    /* dpp2_set_cursor_attributes + escala 1.0 (dcn10_set_cursor_sdr_white_level). */
    cnvc = Bc250DcnRead(BC250_DCN2_REG_CNVC_CUR0_CONTROL);
    cnvc = Bc250DcnField(cnvc, BC250_DCN2_CUR0_MODE_MASK, BC250_DCN_CURSOR_MODE_UN_PREMULT << BC250_DCN2_CUR0_MODE_SHIFT);
    cnvc = Bc250DcnField(cnvc, BC250_DCN2_CUR0_EXPANSION_MODE | BC250_DCN2_CUR0_ROM_EN, 0u);
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_CONTROL, cnvc);
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_FP_SCALE_BIAS, BC250_DCN2_CUR0_FP_SCALE_ONE);
    s_Bc250Dcn2.CursorSlot = slot;
    s_Bc250Dcn2.CursorHasShape = TRUE;
    n = InterlockedIncrement(&s_Bc250Dcn2.CursorShapes);
    if (n <= 8 || (n % 64) == 0) {
        Bc250DisplayLog("[DCN2_CURSOR] forma #%ld cor %ux%u hot=(%u,%u) slot=%u DCN=0x%04X:%08X (readback 0x%04X:%08X)\n",
            n, Shape->Width, s_Bc250Dcn2.CursorHeight,
            Shape->XHot, Shape->YHot, slot, hiAddr, loAddr,
            Bc250DcnRead(BC250_DCN2_REG_CURSOR0_SURFACE_ADDR_HIGH) & 0xFFFFu,
            Bc250DcnRead(BC250_DCN2_REG_CURSOR0_SURFACE_ADDRESS));
    }
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250Dcn2CursorSetPosition(CONST DXGKARG_SETPOINTERPOSITION *Pos)
{
    BC250_DCN_CURSOR_POS p;
    LONG n;
    if (!Bc250DisplayDcnCursorReady() || !s_Bc250Dcn2.CursorHasShape) return STATUS_NOT_SUPPORTED;
    Bc250DcnCursorPosition(Pos->X, Pos->Y, Pos->Flags.Visible, BC250_DCN_CURSOR_MAX, BC250_DCN_CURSOR_MAX,
        g_Bc250DisplayState.CurrentMode.Width, g_Bc250DisplayState.CurrentMode.Height, &p);
    /* hubp2_cursor_set_position + dpp1_set_cursor_position */
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_CONTROL,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_CURSOR0_CONTROL), BC250_DCN2_CURSOR_ENABLE,
            p.Enable ? BC250_DCN2_CURSOR_ENABLE : 0u));
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_POSITION, p.PositionReg);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_HOT_SPOT, p.HotSpotReg);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_DST_OFFSET, 0u);
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_CONTROL,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_CNVC_CUR0_CONTROL), BC250_DCN2_CUR0_ENABLE,
            p.Enable ? BC250_DCN2_CUR0_ENABLE : 0u));
    n = InterlockedIncrement(&s_Bc250Dcn2.CursorMoves);
    if (!p.Enable) InterlockedIncrement(&s_Bc250Dcn2.CursorHidden);
    if (n <= 4 || (n % 4096) == 0) {
        Bc250DisplayLog("[DCN2_CURSOR] posicao #%ld (%d,%d) visivel=%u -> enable=%u POS=0x%08X HOT=0x%08X\n",
            n, Pos->X, Pos->Y, Pos->Flags.Visible, p.Enable, p.PositionReg, p.HotSpotReg);
    }
    return STATUS_SUCCESS;
}

static VOID Bc250Dcn2CursorRestore(void)
{
    if (!s_Bc250Dcn2.CursorSaved || s_Bc250Dcn.Window == NULL) return;
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_CONTROL,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_CURSOR0_CONTROL), BC250_DCN2_CURSOR_ENABLE, 0u));
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_CONTROL,
        Bc250DcnField(Bc250DcnRead(BC250_DCN2_REG_CNVC_CUR0_CONTROL), BC250_DCN2_CUR0_ENABLE, 0u));
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SURFACE_ADDR_HIGH, s_Bc250Dcn2.SavedCursorAddrHi);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SURFACE_ADDRESS, s_Bc250Dcn2.SavedCursorAddr);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_SIZE, s_Bc250Dcn2.SavedCursorSize);
    Bc250DcnWrite(BC250_DCN2_REG_HUBPREQ0_CURSOR_SETTINGS, s_Bc250Dcn2.SavedCursorSettings);
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_FP_SCALE_BIAS, s_Bc250Dcn2.SavedCnvcScaleBias);
    Bc250DcnWrite(BC250_DCN2_REG_CNVC_CUR0_CONTROL, s_Bc250Dcn2.SavedCnvcControl);
    Bc250DcnWrite(BC250_DCN2_REG_CURSOR0_CONTROL, s_Bc250Dcn2.SavedCursorControl);
    s_Bc250Dcn2.CursorSaved = FALSE;
    s_Bc250Dcn2.CursorHasShape = FALSE;
}

static VOID Bc250Dcn2CursorUnmap(void)
{
    if (s_Bc250Dcn2.CursorCpu != NULL) {
        (void)Bc250VramCpuUnmapMemory(&g_Bc250DisplayState.DxgkInterface, s_Bc250Dcn2.CursorCpu);
        s_Bc250Dcn2.CursorCpu = NULL;
    }
    s_Bc250Dcn2.CursorVramReady = FALSE;
}

/* ---- 6. DPMS: apagar/acender o OTG (optc1_blank_crtc/unblank_crtc) -------- */
static VOID Bc250Dcn2Blank(BOOLEAN Blank)
{
    ULONG v;
    if (!Bc250Dcn2Key(BC250_DCN2_KEY_DPMS)) return;
    v = Bc250DcnRead(BC250_DCN2_REG_OTG0_BLANK_CONTROL);
    if (!s_Bc250Dcn2.BlankSaved) { s_Bc250Dcn2.SavedBlankControl = v; s_Bc250Dcn2.BlankSaved = TRUE; }
    v = Bc250DcnField(v, BC250_DCN2_BLANK_DATA_EN | BC250_DCN2_BLANK_DE_MODE,
        Blank ? BC250_DCN2_BLANK_DATA_EN : 0u);
    Bc250DcnWrite(BC250_DCN2_REG_OTG0_BLANK_CONTROL, v);
    if (Blank) InterlockedIncrement(&s_Bc250Dcn2.Blanks); else InterlockedIncrement(&s_Bc250Dcn2.Unblanks);
    Bc250DisplayLog("[DCN2_DPMS] monitor %s: OTG_BLANK_CONTROL 0x%08X (estado atual do blank=%u)\n",
        Blank ? "D3 -> OTG apagado" : "D0 -> OTG aceso", v,
        Bc250DcnRead(BC250_DCN2_REG_OTG0_BLANK_CONTROL) & BC250_DCN2_BLANK_CURRENT_STATE);
}

/* ---- arme e teardown ------------------------------------------------------ */
static VOID Bc250Dcn2LogSummary(_In_ const char *Why)
{
    ULONG k;
    Bc250DisplayLog("[DCN2_RESUMO] %s: chaves=0x%02lX | IRQ vstartup=%lld flip=%lld vsync_por_irq=%ld juntadas=%ld ticks_sem_irq=%ld | CRC amostras=%ld mudancas=%ld maior_sequencia_estavel=%ld | cursor formas=%ld recusadas=%ld movimentos=%ld ocultos=%ld | DPMS apagar=%ld acender=%ld | EDID real %lu bytes publicado=%u\n",
        Why, s_Bc250Dcn2.Keys,
        (long long)s_Bc250Dcn2.IrqVstartup, (long long)s_Bc250Dcn2.IrqFlip,
        (long)s_Bc250Dcn2.IrqVsyncNotified, (long)s_Bc250Dcn2.IrqCoalesced, (long)s_Bc250Dcn2.IrqFallbackTicks,
        (long)s_Bc250Dcn2.CrcSamples, (long)s_Bc250Dcn2.CrcChanges, (long)s_Bc250Dcn2.CrcStableMax,
        (long)s_Bc250Dcn2.CursorShapes, (long)s_Bc250Dcn2.CursorShapeRefused,
        (long)s_Bc250Dcn2.CursorMoves, (long)s_Bc250Dcn2.CursorHidden,
        (long)s_Bc250Dcn2.Blanks, (long)s_Bc250Dcn2.Unblanks,
        s_Bc250Dcn2.RealEdidBytes, s_Bc250Dcn2.RealEdidPublished ? 1u : 0u);
    for (k = 0u; k < 4u; ++k) {
        if (s_Bc250Dcn2.CrcBuf[k].Samples == 0) continue;
        Bc250DisplayLog("[DCN2_CRC] buffer Seg 0x%llX: CRC 0x%08X, %ld amostras paradas, %ld trocas de conteudo\n",
            (unsigned long long)s_Bc250Dcn2.CrcBuf[k].Seg, s_Bc250Dcn2.CrcBuf[k].Crc,
            (long)s_Bc250Dcn2.CrcBuf[k].Samples, (long)s_Bc250Dcn2.CrcBuf[k].Changes);
    }
}

static VOID Bc250Dcn2Teardown(_In_ const char *Why)
{
    if (s_Bc250Dcn.Window == NULL || s_Bc250Dcn2.Keys == 0u) return;
    Bc250Dcn2LogSummary(Why);
    Bc250Dcn2IrqDisarm();
    Bc250Dcn2CrcDisarm();
    Bc250Dcn2CursorRestore();
    Bc250Dcn2CursorUnmap();
    if (s_Bc250Dcn2.BlankSaved) {
        Bc250DcnWrite(BC250_DCN2_REG_OTG0_BLANK_CONTROL, s_Bc250Dcn2.SavedBlankControl);
        s_Bc250Dcn2.BlankSaved = FALSE;
    }
    Bc250DisplayLog("[DCN2_TEARDOWN] %s: interrupcao, CRC, cursor e blank devolvidos como o GOP deixou\n", Why);
    s_Bc250Dcn2.Keys = 0u;
}

static VOID Bc250BuildEdid(void);

VOID Bc250DisplaySetDcnExt(ULONG Keys)
{
    s_Bc250Dcn2.Keys = Keys & BC250_DCN2_KEY_ALL;
    Bc250DisplayLog("[DCN2] chave DcnExt=0x%02lX (1=IRQ 2=CRC 4=cursor 8=EDID real 0x10=DPMS 0x20=timing real) GOP lido=%u\n",
        s_Bc250Dcn2.Keys, s_Bc250Dcn.GopRead ? 1u : 0u);
    if (s_Bc250Dcn2.Keys == 0u || s_Bc250Dcn.Window == NULL || !s_Bc250Dcn.GopRead) {
        s_Bc250Dcn2.Keys = 0u;
        return;
    }
    if (Bc250Dcn2Key(BC250_DCN2_KEY_TIMING)) {
        if (s_Bc250Dcn2.TimingMeasured) {
            s_Bc250Dcn2.TimingApplied = TRUE;
            g_Bc250DisplayState.CurrentMode.RefreshRateNumerator = s_Bc250Dcn2.Timing.RefreshMilliHz;
            g_Bc250DisplayState.CurrentMode.RefreshRateDenominator = 1000u;
            Bc250BuildEdid();
            Bc250DisplayLog("[DCN2_TIMING] APLICADO ao VidPn e ao EDID sintetizado: %lu/1000 Hz, clock %llu Hz\n",
                s_Bc250Dcn2.Timing.RefreshMilliHz, (unsigned long long)s_Bc250Dcn2.Timing.PixelRateHz);
        } else {
            Bc250DisplayLog("[DCN2_TIMING] RECUSADO (recusa=0x%02X): VidPn segue com a constante de 56,13 Hz\n",
                s_Bc250Dcn2.TimingRefuse);
        }
    }
    if (Bc250Dcn2Key(BC250_DCN2_KEY_EDID)) {
        Bc250Dcn2ReadRealEdid();
        if (s_Bc250Dcn2.RealEdidBytes >= 128u && Bc250DcnEdidBlockValid(s_Bc250Dcn2.RealEdid) &&
            s_Bc250Dcn2.RealEdidDtd == 0 && s_Bc250Dcn2.TimingApplied) {
            s_Bc250Dcn2.RealEdidPublished = TRUE;
            Bc250DisplayLog("[DCN2_EDID] PUBLICADO ao Windows: o timing preferido do monitor (DTD %ld) e' o que o OTG varre\n",
                (long)s_Bc250Dcn2.RealEdidDtd);
        } else {
            Bc250DisplayLog("[DCN2_EDID] NAO publicado (bytes=%lu dtd=%ld timing=%u): o Windows segue com o modo recomendado pelo driver\n",
                s_Bc250Dcn2.RealEdidBytes, (long)s_Bc250Dcn2.RealEdidDtd, s_Bc250Dcn2.TimingApplied ? 1u : 0u);
        }
    }
    if (Bc250Dcn2Key(BC250_DCN2_KEY_IRQ)) Bc250Dcn2IrqArm();
    if (Bc250Dcn2Key(BC250_DCN2_KEY_CRC)) Bc250Dcn2CrcArm();
    if (Bc250Dcn2Key(BC250_DCN2_KEY_CURSOR)) {
        Bc250DisplayLog("[DCN2_CURSOR] chave ligada; caps de ponteiro so' com o buffer de VRAM reservado (pronto=%u)\n",
            s_Bc250Dcn2.CursorVramReady ? 1u : 0u);
    }
}

/* Timing nativo para o VidPn: o medido quando aplicado, senao a constante. */
static VOID Bc250NativeTiming(_Out_ BC250_DCN_TIMING *T)
{
    if (s_Bc250Dcn2.TimingApplied) { *T = s_Bc250Dcn2.Timing; return; }
    RtlZeroMemory(T, sizeof(*T));
}

static BOOLEAN Bc250DcnRealEdid(_Outptr_result_maybenull_ const UCHAR **Edid, _Out_ ULONG *Bytes)
{
    *Edid = s_Bc250Dcn2.RealEdidPublished ? s_Bc250Dcn2.RealEdid : NULL;
    *Bytes = s_Bc250Dcn2.RealEdidPublished ? s_Bc250Dcn2.RealEdidBytes : 0u;
    return s_Bc250Dcn2.RealEdidPublished;
}

static ULONG Bc250Dcn2KeysArmed(void) { return s_Bc250Dcn2.Keys; }
ULONG Bc250DisplayDcnExtKeys(void) { return s_Bc250Dcn2.Keys; }
#else
void Bc250DisplaySetDcnFlipEnabled(ULONG Enabled) { UNREFERENCED_PARAMETER(Enabled); }
VOID Bc250DisplaySetDcnExt(ULONG Keys) { UNREFERENCED_PARAMETER(Keys); }
ULONG Bc250DisplayDcnExtKeys(void) { return 0u; }
VOID Bc250DisplaySetDcnCursorVram(UINT64 VramOffset, UINT64 Bytes) { UNREFERENCED_PARAMETER(VramOffset); UNREFERENCED_PARAMETER(Bytes); }
ULONG Bc250DisplayDcnCursorReady(void) { return 0u; }
BOOLEAN Bc250DisplayDcnIsr(ULONG SrcId) { UNREFERENCED_PARAMETER(SrcId); return FALSE; }
static NTSTATUS Bc250Dcn2CursorSetShape(CONST DXGKARG_SETPOINTERSHAPE *Shape) { UNREFERENCED_PARAMETER(Shape); return STATUS_NOT_SUPPORTED; }
static NTSTATUS Bc250Dcn2CursorSetPosition(CONST DXGKARG_SETPOINTERPOSITION *Pos) { UNREFERENCED_PARAMETER(Pos); return STATUS_NOT_SUPPORTED; }
static VOID Bc250Dcn2Blank(BOOLEAN Blank) { UNREFERENCED_PARAMETER(Blank); }
static VOID Bc250NativeTiming(_Out_ BC250_DCN_TIMING *T) { RtlZeroMemory(T, sizeof(*T)); }
static BOOLEAN Bc250DcnRealEdid(_Outptr_result_maybenull_ const UCHAR **Edid, _Out_ ULONG *Bytes) { *Edid = NULL; *Bytes = 0u; return FALSE; }
ULONG Bc250DisplayDcnFlipArmed(void) { return 0u; }
static BOOLEAN Bc250DcnFlipProgram(
    _In_ const char *Route, _In_ UINT64 SourceVramOffset,
    _In_ PHYSICAL_ADDRESS SegmentAddress, _In_ BOOLEAN Immediate)
{
    UNREFERENCED_PARAMETER(Route); UNREFERENCED_PARAMETER(SourceVramOffset);
    UNREFERENCED_PARAMETER(SegmentAddress); UNREFERENCED_PARAMETER(Immediate);
    return FALSE;
}
#endif

static NTSTATUS Bc250DisplayCopyPrimaryToGop(
    _In_ const char *Route,
    _In_ UINT32 SegmentId,
    _In_ UINT64 SourceOffset,
    _In_ BOOLEAN ModeChangeOnly)
{
    LONG sequence = InterlockedIncrement(&s_Bc250PrimaryCopySequence);
    UINT64 scanoutOffset;
    UINT64 sourceMc;
    ULONG bytes;
    ULONG srcNz = 0u, srcHash = 0u, beforeNz = 0u, beforeHash = 0u;
    ULONG afterNz = 0u, afterHash = 0u;
    UINT64 firstMismatch = ~(UINT64)0;
    ULONG mismatchCount = 0u;
    ULONG sampleIndex;
    BOOLEAN copied;
    BOOLEAN srcSampled;
    BOOLEAN beforeSampled;
    BOOLEAN afterSampled;

    if (SegmentId != 1u || !g_Bc250SdmaPresent.Initialized ||
        !g_Bc250SdmaPresent.HardwareEnabled ||
        g_Bc250SdmaPresent.ScanoutGpuAddress < g_Bc250SdmaPresent.VramMcStart ||
        g_Bc250DisplayState.FrameBufferVirtual == NULL) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] REJECT Route=%s Seg=%u SrcOff=0x%llX Init=%u Hw=%u ScanoutMc=0x%llX VramMc=0x%llX Map=%p Status=DEVICE_NOT_READY\n",
            sequence, Route, SegmentId, SourceOffset,
            g_Bc250SdmaPresent.Initialized ? 1u : 0u,
            g_Bc250SdmaPresent.HardwareEnabled ? 1u : 0u,
            g_Bc250SdmaPresent.ScanoutGpuAddress,
            g_Bc250SdmaPresent.VramMcStart,
            g_Bc250DisplayState.FrameBufferVirtual);
        return STATUS_DEVICE_NOT_READY;
    }

    scanoutOffset = g_Bc250SdmaPresent.ScanoutGpuAddress -
        g_Bc250SdmaPresent.VramMcStart;
    bytes = g_Bc250DisplayState.CurrentMode.Pitch *
        g_Bc250DisplayState.CurrentMode.Height;
    if (bytes < sizeof(ULONG) || SourceOffset > ~(UINT64)0 -
            g_Bc250SdmaPresent.VramMcStart) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] REJECT Route=%s Seg=%u SrcOff=0x%llX DstOff=0x%llX Bytes=%u MapBytes=%u Status=INVALID_PARAMETER\n",
            sequence, Route, SegmentId, SourceOffset, scanoutOffset,
            bytes, g_Bc250DisplayState.FrameBufferSize);
        return STATUS_INVALID_PARAMETER;
    }

    sourceMc = g_Bc250SdmaPresent.VramMcStart + SourceOffset;
    srcSampled = Bc250DisplaySampleSurface(SourceOffset, bytes, 256u,
        &srcNz, &srcHash);
    beforeSampled = Bc250DisplaySampleSurface(scanoutOffset, bytes, 256u,
        &beforeNz, &beforeHash);
    if (!srcSampled || !beforeSampled) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] SAMPLE_UNAVAILABLE Route=%s SrcSample=%u DstBeforeSample=%u; CPU BAR may cover less than local VRAM; SDMA still attempted\n",
            sequence, Route, srcSampled ? 1u : 0u,
            beforeSampled ? 1u : 0u);
    }

    Bc250DisplayLog("[PRIMARY_COPY #%ld] BEGIN Route=%s Seg=%u SrcOff=0x%llX SrcMc=0x%llX DstOff=0x%llX DstMc=0x%llX Bytes=%u Pitch=%u SrcSample=%u SrcNZ=%u/256 SrcHash=%08X DstBeforeSample=%u DstBeforeNZ=%u/256 DstBeforeHash=%08X\n",
        sequence, Route, SegmentId, SourceOffset, sourceMc, scanoutOffset,
        g_Bc250SdmaPresent.ScanoutGpuAddress, bytes,
        g_Bc250DisplayState.CurrentMode.Pitch,
        srcSampled ? 1u : 0u, srcNz, srcHash,
        beforeSampled ? 1u : 0u, beforeNz, beforeHash);

    if (SourceOffset == scanoutOffset) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] ALIAS: source is inherited GOP scanout; no SDMA copy, bytes already at destination; visibility on monitor NOT PROVEN\n",
            sequence);
        return STATUS_SUCCESS;
    }

    /*
     * DX.33 -- MODE CHANGE NAO APAGA O SCANOUT COM PRIMARIA VAZIA.
     *
     * Corrida 78 (P0 amostral): as 16 PRIMARY_COPY da rota SET_ADDRESS vieram
     * todas com Flags=0x1 (ModeChange, d3dkmddi.h) e origem 0/256 -- a
     * primaria do GOP 0xAAC000 e as primarias novas do DWM, que ninguem
     * desenhou ainda. No modelo Blt o Present escreve DIRETO no scanout, entao
     * copiar essa primaria apagava o quadro valido: 86/256 (o 1280x720 do
     * harness) -> 0/256 em #5-#8, e 256/256 -> 0/256 em #12. Na 77, 8 de 8
     * SET_ADDRESS tambem tinham origem zero.
     *
     * Pula SO' se as duas condicoes valem: (1) o dxgkrnl declarou ModeChange
     * sem FlipImmediate/FlipOnNextVSync -- troca de modo, nao flip; (2) uma
     * amostragem de 1024 dwords da origem e' INTEIRAMENTE zero. Origem com
     * qualquer amostra nao-nula copia como antes; flips (MPO1/MPO3) nao passam
     * por aqui. Risco declarado: uma primaria com conteudo esparso que caia
     * fora das 1024 amostras nao seria copiada ate o proximo Present/flip.
     */
    if (ModeChangeOnly && srcSampled) {
        ULONG denseNz = 0u, denseHash = 0u;
        if (Bc250DisplaySampleSurface(SourceOffset, bytes, 1024u,
                &denseNz, &denseHash) && denseNz == 0u) {
            Bc250DisplayLog("[PRIMARY_COPY #%ld] MODECHANGE_BLANK_SKIP Route=%s SrcOff=0x%llX SrcNZ=0/1024: ModeChange sem flip para primaria sem conteudo amostrado; scanout PRESERVADO (DstNZ=%u/256 DstHash=%08X) Status=SUCCESS\n",
                sequence, Route, SourceOffset, beforeNz, beforeHash);
            return STATUS_SUCCESS;
        }
    }

    /*
     * DX.32 -- NAO REPETIR A COPIA QUE O PRESENT ACABOU DE FAZER.
     *
     * Corrida 77 (P0): as 22 PRIMARY_COPY nao-nulas do MPO3 copiavam a MESMA
     * origem que o DxgkDdiPresent imediatamente anterior ja tinha copiado por
     * SDMA (Present #1 e #4 de 0x153C000; #10 e #12 de 0x266C000), e as 256
     * amostras do destino ja batiam com a origem antes da copia em 22 de 22.
     * Era uma segunda copia sincrona de ~11 MB numa DDI que a Microsoft
     * documenta como chamada em nivel de interrupcao, bloqueando o scheduler.
     *
     * Pula SO' se as DUAS condicoes valem: (1) a fonte unica em
     * bc250_sdma_present.c diz que a ultima escrita no scanout foi a copia
     * INTEGRAL desta mesma origem, sem escrita posterior; (2) as 256 amostras
     * pareadas conferem AGORA, com origem nao-nula. Qualquer uma falhando, a
     * copia acontece como antes. Nao e' prova integral de bytes; e' a mesma
     * classe de prova amostral do END, e o rotulo diz isso.
     */
    if (srcSampled && beforeSampled && srcNz != 0u) {
        LONG holdSeq = 0;
        if (Bc250SdmaPresentScanoutHoldsFullCopyOf(sourceMc, bytes, &holdSeq)) {
            ULONG preMismatch = 0u;
            UINT64 preFirst = ~(UINT64)0;
            ULONG prePairs = Bc250DisplayPairedSampleCompare(SourceOffset, scanoutOffset,
                bytes, &preMismatch, &preFirst);
            if (preMismatch == 0u && prePairs == 256u) {
                Bc250DisplayLog("[PRIMARY_COPY #%ld] REDUNDANT_SKIP Route=%s SrcOff=0x%llX: ultima escrita no scanout (#%ld) foi a copia INTEGRAL desta origem e %u/256 amostras pareadas conferem; SDMA NAO repetida Proof=SAMPLED_MATCH_NONZERO_NOT_FULL_BYTE_PROOF Status=SUCCESS\n",
                    sequence, Route, SourceOffset, holdSeq, prePairs);
                return STATUS_SUCCESS;
            }
            Bc250DisplayLog("[PRIMARY_COPY #%ld] REDUNDANT_CANDIDATE_REJEITADO Route=%s: marca de copia integral presente, mas %u/%u pares divergem (primeiro 0x%llX); copia SDMA executada\n",
                sequence, Route, preMismatch, prePairs, preFirst);
        }
    }

    copied = Bc250SdmaPresentBlt(sourceMc,
        g_Bc250SdmaPresent.ScanoutGpuAddress, bytes);
    afterSampled = Bc250DisplaySampleSurface(scanoutOffset, bytes, 256u,
        &afterNz, &afterHash);
    if (!copied) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] FAIL Route=%s Sdma=0 DstAfterSample=%u DstAfterNZ=%u/256 DstAfterHash=%08X Status=GPU_EXCEPTION; no successful primary switch\n",
            sequence, Route, afterSampled ? 1u : 0u,
            afterNz, afterHash);
        return STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;
    }

    if (!srcSampled || !afterSampled) {
        Bc250DisplayLog("[PRIMARY_COPY #%ld] END Route=%s Sdma=YES FenceAndRptr=YES Proof=DATA_UNOBSERVED_CPU_MAP SrcSample=%u DstAfterSample=%u Visibility=NOT_PROVEN Status=SUCCESS_ENGINE_ONLY\n",
            sequence, Route, srcSampled ? 1u : 0u,
            afterSampled ? 1u : 0u);
        return STATUS_SUCCESS;
    }

    sampleIndex = Bc250DisplayPairedSampleCompare(SourceOffset, scanoutOffset,
        bytes, &mismatchCount, &firstMismatch);
    if (mismatchCount != 0u) {
        /* A origem pode ser reescrita pelo produtor entre a fence da SDMA
         * e esta leitura de CPU. A divergencia perde a prova de dados, mas
         * nao refuta a conclusao fisica da engine nem justifica device-lost. */
        Bc250DisplayLog("[PRIMARY_COPY #%ld] SAMPLE_MISMATCH_INCONCLUSIVE Route=%s Count=%u/256 FirstOffset=0x%llX SrcNZ=%u/256 DstNZ=%u/256 Sdma=YES Status=SUCCESS_ENGINE_ONLY\n",
            sequence, Route, mismatchCount, firstMismatch, srcNz, afterNz);
    }
    Bc250DisplayLog("[PRIMARY_COPY #%ld] END Route=%s Sdma=YES FenceAndRptr=YES SrcNZ=%u/256 SrcHash=%08X DstAfterNZ=%u/256 DstAfterHash=%08X PairedSamples=%u Mismatch=%u Proof=%s Visibility=NOT_PROVEN Status=SUCCESS\n",
        sequence, Route, srcNz, srcHash, afterNz, afterHash,
        sampleIndex, mismatchCount,
        mismatchCount != 0u ? "SAMPLE_MISMATCH_INCONCLUSIVE" :
            (afterNz == 0u ? "MATCH_BLANK_INCONCLUSIVE" :
                "SAMPLED_MATCH_NONZERO_NOT_FULL_BYTE_PROOF"));
    return STATUS_SUCCESS;
}

/*
 * DCN.1: rota unica de comutacao de primaria. Com o flip armado, o HUBP0
 * aponta para a primaria e a copia do SDMA0 nao acontece; recusado ou
 * desarmado, a copia de antes (Bc250DisplayCopyPrimaryToGop).
 */
static NTSTATUS Bc250DisplaySwitchPrimary(
    _In_ const char *Route,
    _In_ UINT32 SegmentId,
    _In_ PHYSICAL_ADDRESS SegmentAddress,
    _In_ BOOLEAN ModeChangeOnly,
    _In_ BOOLEAN Immediate)
{
    if (SegmentId == 1u &&
        Bc250DcnFlipProgram(Route, (UINT64)SegmentAddress.QuadPart, SegmentAddress, Immediate)) {
        return STATUS_SUCCESS;
    }
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (SegmentId != 1u && InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0) != 0) {
        LONG o = InterlockedIncrement(&s_Bc250Dcn.NotSegment1);
        if (o <= 8 || (o % 256) == 0) {
            Bc250DisplayLog("[DCN1_FLIP] FORA_DO_SEG1 #%ld Route=%s Segment=%u Addr=0x%llX: primaria fora do segmento 1, copia do SDMA0\n",
                o, Route, SegmentId, (unsigned long long)SegmentAddress.QuadPart);
        }
    }
    /* Flip recusado com o HUBP0 numa primaria do DWM: a copia abaixo vai ao
     * scanout do GOP, que ninguem estaria exibindo. Volta ao GOP e desarma
     * (falha fechada): o resto da corrida segue no modelo de copia. */
    if (InterlockedCompareExchange(&s_Bc250Dcn.Armed, 0, 0) != 0 &&
        InterlockedCompareExchange(&s_Bc250Dcn.OnUserSurface, 0, 0) != 0) {
        Bc250DcnFlipRestoreGop("FLIP_RECUSADO_VOLTA_A_COPIA");
    }
#endif
    return Bc250DisplayCopyPrimaryToGop(Route, SegmentId,
        (UINT64)SegmentAddress.QuadPart, ModeChangeOnly);
}

static volatile LONG g_Bc250DisplaySyntheticEdidEnabled = 0; /* Default: 0 (disabled) para forçar o DMM a usar DxgkDdiRecommendMonitorModes */

void Bc250DisplaySetSyntheticEdidEnabled(BOOLEAN Enabled)
{
    InterlockedExchange(&g_Bc250DisplaySyntheticEdidEnabled, Enabled ? 1 : 0);
    g_Bc250DisplayState.EdidValid = Enabled;
    Bc250DisplayLog("[DisplayConfig] Synthetic EDID %s\n", Enabled ? "ENABLED" : "DISABLED (using DxgkDdiRecommendMonitorModes)");
}

_Check_return_
BOOLEAN Bc250DisplayGetSyntheticEdidEnabled(void)
{
    return InterlockedCompareExchange(&g_Bc250DisplaySyntheticEdidEnabled, 0, 0) != 0;
}

/*
 * Timings físicos reais ancorados na medição de hardware do OTG
 * (docs/evidence/2026-09-02_21-40-32-.../dcn_inventory.json):
 * mmOTG0_OTG_H_TOTAL: 2783 (+1 = 2784) -> HBLANK = 224
 * mmOTG0_OTG_V_TOTAL: 1110 (+1 = 1111) -> VBLANK = 31
 * Ancoragem: VSyncFreq = 5613 / 100 Hz (56.13 Hz)
 * HSyncFreq = vTotal * VSyncFreq = 1111 * 56.13 = 62360.43 Hz (6236043 / 100)
 * PixelRate = hTotal * HSyncFreq = 2784 * 62360.43 = 173,611,437 Hz
 */
#define BC250_NATIVE_HBLANK         224u
#define BC250_NATIVE_VBLANK         31u
#define BC250_NATIVE_REFRESH_NUM    5613u
#define BC250_NATIVE_REFRESH_DEN    100u
#define BC250_NATIVE_HSYNC_NUM      6236043u
#define BC250_NATIVE_HSYNC_DEN      100u
#define BC250_NATIVE_PIXEL_RATE     173611437ULL

#include <stdarg.h>

#if defined(__cplusplus)
extern "C" {
#endif
int __cdecl _vsnprintf(char *, size_t, const char *, va_list);
NTSYSAPI NTSTATUS NTAPI ZwFlushBuffersFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock);
#if defined(__cplusplus)
}
#endif

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
#define BC250_PAGING_LOG_RING_SIZE (128u * 1024u)
/* FB.1b (doc 607): globais, NAO static -- o PDB privado so traz simbolos
 * globais, e o extrator (tools/extrair-anel-do-dump.ps1) le o anel do dump
 * pelo nome. Corrida 227: com static o cdb nao achou nenhum dos tres. */
char g_Bc250PagingLogRing[BC250_PAGING_LOG_RING_SIZE];
ULONG g_Bc250PagingLogHead = 0u;
ULONG g_Bc250PagingLogTail = 0u;
static KSPIN_LOCK g_Bc250PagingLogLock;
static volatile LONG g_Bc250PagingLogLockInitOnce = 0;

/*
 * C3.16 (12/09) -- AMOSTRAGEM DO LOG DE ALTO VOLUME.
 *
 * A corrida 18:45 produziu 1.179.761 linhas / 100 MB em cerca de cinco
 * minutos enquanto a tela parecia congelada. O KMD continuou vivo (8.895
 * submits aposentados e 38.090 flushes publicados), portanto o proprio log
 * fazia parte do experimento: cada linha normal ainda passava por DbgPrintEx
 * e, em PASSIVE_LEVEL, por abrir/escrever/flushar/fechar o arquivo.
 *
 * Preservamos integralmente falhas e vereditos. Apenas as classes repetitivas
 * de sucesso sao emitidas nas primeiras 16 ocorrencias e depois a cada 1024.
 * Os contadores atomicos continuam exatos mesmo quando a linha e suprimida.
 */
/*
 * I3 (13/09) -- A CLASSE 12 ESTAVA CEGANDO O CICLO DE VIDA DE DEVICE E CONTEXTO.
 *
 * [McdmQAI], [McdmCreateDevice] e [McdmCreateContext] compartilhavam UM contador.
 * O [McdmQAI] sai em rajada no bind (17 linhas medidas na corrida 16:28:27) e
 * consome sozinho as 16 primeiras ocorrencias da classe; dali em diante so' passa
 * 1 em 1024. Resultado medido: o dwm.exe criou e destruiu 90 devices na corrida e
 * o log do KMD nao registrou NENHUM [McdmCreateDevice] nem [McdmCreateContext].
 *
 * As tres classes agora tem contadores independentes (12, 13 e 14). E' a MESMA
 * classe de defeito do teto de escape corrigido em 13/09 11:32: teto global que
 * nao alcanca -- aqui, teto compartilhado que outra tag esgota.
 *
 * CORRIDA 17:18:35 -- A MESMA FALHA ESTAVA NA CLASSE 9 E EU NAO A TINHA VISTO.
 * `[CreateAllocation]`, `[ActiveOpenAlloc]`, `[McdmCreateAllocation]` e
 * `[McdmCloseAllocation]` dividiam um contador: 45 linhas da classe contra o
 * orcamento de 16 primeiras. Por isso "Primary token confirmed (UMD" nao saiu
 * NENHUMA vez mesmo com a primaria de UMD funcionando -- na mesma corrida o
 * SetVidPnSourceAddress foi para hAlloc=0xA8, alocacao criada pela UMD, o que
 * o dxgkrnl so' faz com uma primaria aceita. A ausencia era artefato do logger,
 * nao medicao, e custou a conclusividade do criterio.
 *
 * Cada tag da classe 9 passa a ter contador proprio (9, 15, 16 e 17).
 */
#define BC250_VERBOSE_LOG_CLASS_COUNT 18u
#define BC250_VERBOSE_LOG_FIRST       16L
#define BC250_VERBOSE_LOG_PERIOD      1024L
static volatile LONG g_Bc250VerboseLogOrdinal[BC250_VERBOSE_LOG_CLASS_COUNT];

static BOOLEAN Bc250LogTextStartsWith(
    _In_ const char *Text,
    _In_ const char *Prefix)
{
    while (*Prefix != '\0') {
        if (*Text++ != *Prefix++) return FALSE;
    }
    return TRUE;
}

static BOOLEAN Bc250LogTextContains(
    _In_ const char *Text,
    _In_ const char *Needle)
{
    const char *candidate;
    if (*Needle == '\0') return TRUE;
    for (candidate = Text; *candidate != '\0'; ++candidate) {
        const char *left = candidate;
        const char *right = Needle;
        while (*left != '\0' && *right != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        if (*right == '\0') return TRUE;
    }
    return FALSE;
}

static BOOLEAN Bc250ShouldEmitVerboseLog(
    _In_ const char *Text,
    _Out_ ULONG *ClassId,
    _Out_ LONG *Ordinal)
{
    ULONG classId = 0u;
    LONG ordinal;

    *ClassId = 0u;
    *Ordinal = 0L;

    /* Falha, recusa e discriminante nunca sao amostrados. */
    if (Bc250LogTextContains(Text, "NO_SPACE") ||
        Bc250LogTextContains(Text, "FAIL") ||
        Bc250LogTextContains(Text, "FALHA") ||
        Bc250LogTextContains(Text, "RECUSA") ||
        Bc250LogTextContains(Text, "TIMEOUT") ||
        Bc250LogTextContains(Text, "FAULT") ||
        Bc250LogTextContains(Text, "DIVERGENCIA") ||
        Bc250LogTextContains(Text, "OCUPACAO") ||
        /*
         * COMPOSICAO sai da amostragem (Corrida 45, 20/09). A linha
         * PUBLICACAO_COMPOSICAO foi adicionada na faixa D3 para dizer se a
         * falha de publicacao de GPUVA e' de ORDEM ou de CAPACIDADE, e saiu
         * ZERO vezes em 10 PUBLICACAO_FALHA daquela corrida -- a string estava
         * no SYS assinado, mas a isencao do amostrador casa por SUBSTRING
         * ("FALHA", "OCUPACAO", "PASS"...) e o nome escolhido nao continha
         * nenhum token isento. Caiu na classe 4 e foi amostrada fora,
         * exatamente na medicao que a faixa existia para obter.
         */
        Bc250LogTextContains(Text, "COMPOSICAO") ||
        /*
         * Corrida 57: o fault nao latchou, mas o UpdatePageTable que cobre
         * 0x200040000 aconteceu uma unica vez. A identidade/permissao desse
         * alvo nao pode ser amostrada fora justamente por ser rara.
         */
        Bc250LogTextContains(Text, "DX_TARGET_200040") ||
        /* DX.36: mesmo motivo para o alvo da escrita sem permissao (80/81). */
        Bc250LogTextContains(Text, "DX_TARGET_2401A1") ||
        /* DX.43: uma linha por troca de VMID -- e' a variavel da Corrida 86. */
        Bc250LogTextContains(Text, "DX43_VMID_WARMUP") ||
        /* DX.44: a DDI de preempcao tem teto proprio no emissor (Corrida 87). */
        Bc250LogTextContains(Text, "PREEMPT_COMMAND") ||
        /* DX.47: conclusao e espera da preempcao (teto proprio no emissor). */
        Bc250LogTextContains(Text, "PREEMPT_DONE") ||
        /* DX.48: ciclo de vida do adaptador e o QAI Type=29 (ultima linha dos
         * travamentos 88/92) nunca podem ser amostrados fora. */
        Bc250LogTextContains(Text, "DX48_LIFECYCLE") ||
        /* DX.52: recusa de CreateAllocation (laco do Explorer, Corridas 93..97). */
        Bc250LogTextContains(Text, "CREATE_ALLOC_RECUSADO") ||
        (Bc250LogTextStartsWith(Text, "[McdmQAI]") && Bc250LogTextContains(Text, "Type=29,")) ||
        Bc250LogTextContains(Text, "PREEMPT_WAIT") ||
        /* DX.45: reset silencioso do CP (Corrida 88); teto de 16 no emissor. */
        Bc250LogTextContains(Text, "CP_RESET_DETECTED") ||
        /*
         * DX.12: a mascara de VMIDs invalidados e a MEDICAO da Corrida 59.
         * A linha comeca com "[FLUSH_POOL_VMIDS]", que cairia na classe 5
         * ("[FlushMappedGfxhubTlb] " nao, mas o ramo final devolve TRUE para
         * tag desconhecida) -- o risco real e' alguem futuramente lhe dar uma
         * classe. A isencao explicita e' a armadilha 16 ja paga: PUBLICACAO_
         * COMPOSICAO saiu ZERO vezes em 10 falhas porque o nome escolhido nao
         * continha token isento. O teto desta linha e' proprio, no emissor.
         */
        Bc250LogTextContains(Text, "FLUSH_POOL_VMIDS") ||
        /*
         * DX.13: as tres linhas da escrita de diretorio de paginas. Todas sao
         * raras POR CONSTRUCAO -- PDE_WRITE so sai quando a atualizacao cobre
         * o indice da arena, e as outras duas so saem quando algo anormal
         * acontece. Rara e critica e exatamente o perfil que a armadilha 16
         * queimou uma vez: PUBLICACAO_COMPOSICAO saiu ZERO vezes em 10 falhas
         * por nao ter token isento. PDE_TRIPWIRE ja carrega FALHA, mas nao
         * dependa disso -- o nome pode mudar e a isencao nao pode depender de
         * uma palavra no meio do texto.
         */
        Bc250LogTextContains(Text, "PDE_WRITE") ||
        Bc250LogTextContains(Text, "PDE_ARENA_CONFRONTO") ||
        Bc250LogTextContains(Text, "PDE_TRIPWIRE") ||
        Bc250LogTextContains(Text, "PDE_LARGEPAGE") ||
        Bc250LogTextContains(Text, "REJECT") ||
        Bc250LogTextContains(Text, "ERROR") ||
        Bc250LogTextContains(Text, "FAILED") ||
        Bc250LogTextContains(Text, "PASS") ||
        Bc250LogTextContains(Text, "PROVADO") ||
        Bc250LogTextContains(Text, "VEREDITO") ||
        /*
         * CTX_PROBE e CONTEXT SWITCH saem da amostragem (13/09): sao as
         * linhas que dizem se a root do VMID do contexto foi instalada
         * antes da submissao, e na corrida 08:12 sobraram 3 CTX_PROBE em
         * ~958 submissoes -- justamente o dado que faltava. Sao raras por
         * natureza (uma por troca de contexto), nao um diluvio.
         */
        /* DX.91: uma linha por copia/preenchimento/descarte de paginacao. */
        Bc250LogTextContains(Text, "DX91_PAGING_MOVE") ||
        Bc250LogTextContains(Text, "CTX_PROBE") ||
        Bc250LogTextContains(Text, "CONTEXT SWITCH") ||
        Bc250LogTextContains(Text, "IB EXECUTADO") ||
        Bc250LogTextContains(Text, "IB_ENGINE_2LEGS") ||
        Bc250LogTextContains(Text, "TDR_") ||
        Bc250LogTextContains(Text, "DEVICE_REMOVED")) {
        return TRUE;
    }

    if (Bc250LogTextStartsWith(Text, "[McdmBuildPagingBuffer]")) {
        classId = 1u;
    } else if (Bc250LogTextStartsWith(Text, "[UpdatePT] ") ||
               Bc250LogTextStartsWith(Text, "[UpdatePT Lvl=")) {
        classId = 2u;
    } else if (Bc250LogTextStartsWith(Text, "[ActiveUpdatePT] ")) {
        classId = 3u;
    } else if (Bc250LogTextStartsWith(Text, "[ActiveFlushTlb]")) {
        classId = 4u;
    } else if (Bc250LogTextStartsWith(Text, "[FlushMappedGfxhubTlb] ")) {
        classId = 5u;
    } else if (Bc250LogTextStartsWith(Text, "[PT_WALK ") ||
               Bc250LogTextStartsWith(Text, "[PT_RANGE ") ||
               Bc250LogTextStartsWith(Text, "[PT_READ] ")) {
        classId = 6u;
    } else if (Bc250LogTextStartsWith(Text, "[SubmitVirtual Node0 SDMA]")) {
        classId = 7u;
    } else if (Bc250LogTextStartsWith(Text, "[TransferVirtual]") ||
               Bc250LogTextStartsWith(Text, "[FillVirtual]")) {
        classId = 8u;
    } else if (Bc250LogTextStartsWith(Text, "[ActiveOpenAlloc]")) {
        classId = 9u;
    } else if (Bc250LogTextStartsWith(Text, "[CreateAllocation]")) {
        classId = 15u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmCreateAllocation]")) {
        classId = 16u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmCloseAllocation]")) {
        classId = 17u;
    } else if (Bc250LogTextStartsWith(Text, "[SdmaRetire]")) {
        classId = 10u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmSubmitCommandVirtual]")) {
        classId = 11u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmQAI]")) {
        classId = 12u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmCreateDevice]")) {
        classId = 13u;
    } else if (Bc250LogTextStartsWith(Text, "[McdmCreateContext]")) {
        classId = 14u;
    } else {
        return TRUE;
    }

    ordinal = InterlockedIncrement(&g_Bc250VerboseLogOrdinal[classId]);
    *ClassId = classId;
    *Ordinal = ordinal;
    return ordinal <= BC250_VERBOSE_LOG_FIRST ||
        (ordinal % BC250_VERBOSE_LOG_PERIOD) == 0L;
}

/* DX.88: dono unico do arquivo de log; ver o bloco em Bc250DisplayLog. */
static volatile LONG g_Bc250LogFileOwner = 0;

static VOID Bc250LogRingAppend(_In_reads_(Length) const char *Text, _In_ int Length)
{
    KIRQL irql;
    int i;
    KeAcquireSpinLock(&g_Bc250PagingLogLock, &irql);
    for (i = 0; i < Length; ++i) {
        ULONG next = (g_Bc250PagingLogHead + 1u) % BC250_PAGING_LOG_RING_SIZE;
        if (next == g_Bc250PagingLogTail) {
            g_Bc250PagingLogTail = (g_Bc250PagingLogTail + 1u) % BC250_PAGING_LOG_RING_SIZE;
        }
        g_Bc250PagingLogRing[g_Bc250PagingLogHead] = Text[i];
        g_Bc250PagingLogHead = next;
    }
    KeReleaseSpinLock(&g_Bc250PagingLogLock, irql);
}

/* So' a thread dona do arquivo (g_Bc250LogFileOwner) chama isto. */
static VOID Bc250LogRingDrainToFile(_In_ HANDLE FileHandle)
{
    char drainBuf[4096];
    ULONG drainLen;
    IO_STATUS_BLOCK ioStatus;
    do {
        KIRQL irql;
        drainLen = 0u;
        KeAcquireSpinLock(&g_Bc250PagingLogLock, &irql);
        while (g_Bc250PagingLogTail != g_Bc250PagingLogHead && drainLen < sizeof(drainBuf)) {
            drainBuf[drainLen++] = g_Bc250PagingLogRing[g_Bc250PagingLogTail];
            g_Bc250PagingLogTail = (g_Bc250PagingLogTail + 1u) % BC250_PAGING_LOG_RING_SIZE;
        }
        KeReleaseSpinLock(&g_Bc250PagingLogLock, irql);
        if (drainLen > 0u) {
            (void)ZwWriteFile(FileHandle, NULL, NULL, NULL, &ioStatus, drainBuf, drainLen, NULL, NULL);
        }
    } while (drainLen == sizeof(drainBuf));
}

void Bc250DisplayLog(const char *Format, ...)
{
    char buffer[512];
    va_list args;
    int len;
    ULONG sampleClass = 0u;
    LONG sampleOrdinal = 0L;

    if (InterlockedCompareExchange(&g_Bc250PagingLogLockInitOnce, 1, 0) == 0) {
        KeInitializeSpinLock(&g_Bc250PagingLogLock);
    }

    va_start(args, Format);
    len = _vsnprintf(buffer, sizeof(buffer) - 1, Format, args);
    va_end(args);
    if (len <= 0) return;
    buffer[len] = '\0';

    if (!Bc250ShouldEmitVerboseLog(buffer, &sampleClass, &sampleOrdinal)) {
        return;
    }

    /*
     * DX.86 -- SEM DbgPrintEx NO CAMINHO DO LOG.
     *
     * Corrida 132 (P0): com DEBUG/DEBUGPORT=NET no boot, 536 de 10.615 FLUSH_TLB
     * duraram exatamente ~510 ms ou ~1.030 ms, o VidMm passou 96% da janela em
     * PagingPreparation (ETW) e o Present do DWM ficou 88-116 s por chamada.
     * Cada linha emitida passava por DbgPrintEx em DPFLTR_ERROR_LEVEL, que
     * nenhum filtro barra, e com depurador de kernel habilitado isso vai ao
     * transporte KD. O arquivo C:\BC250_DISPLAY_LOG.txt e' a evidencia; o
     * espelho em DbgPrint so' custava tempo e mudava o experimento.
     */

    /*
     * Operacoes de paginacao (VidMm DxgkDdiBuildPagingBuffer e rotinas associadas)
     * rodam em PASSIVE_LEVEL na paging thread do sistema, mas NUNCA podem realizar
     * I/O sincrono de arquivo em disco. Fazer ZwCreateFile/ZwFlushBuffersFile na
     * paging thread causa deadlock circular imediato com NTFS/Cache Manager/Storport.
     * Quando g_Bc250PagingActiveCount > 0 ou IRQL > PASSIVE_LEVEL, armazenamos as
     * mensagens no buffer circular em memoria RAM (zero disco, zero deadlock).
     * Quando g_Bc250PagingActiveCount == 0 em PASSIVE_LEVEL, o buffer e' descarregado
     * em lote para C:\BC250_DISPLAY_LOG.txt.
     */
    /*
     * DX.88 -- UM SO' DONO DO ARQUIVO POR VEZ.
     *
     * Corrida 136 (P0): uma linha "BC250_HANG ... ENTER" saiu com outra linha
     * enfiada no meio do qpc, e o parser leu um FLUSH_TLB de 78,7 s que durou
     * 36 ms. Mecanismo (P1): varias threads podiam estar neste caminho ao mesmo
     * tempo, cada uma drenando o anel em blocos de 4 KB; os pedacos de uma linha
     * partida na divisa de dois blocos iam para o arquivo por threads diferentes,
     * com outra linha gravada entre eles. Agora so' uma thread e' dona do
     * arquivo (flag atomico, sem bloquear ninguem): quem o encontra ocupado poe
     * a linha no anel e segue; a dona drena o anel inteiro antes e depois da
     * propria linha. Assim toda escrita no arquivo sai de uma unica sequencia.
     */
    if (KeGetCurrentIrql() > PASSIVE_LEVEL ||
        KeAreApcsDisabled() ||
        InterlockedCompareExchange(&g_Bc250PagingActiveCount, 0, 0) > 0 ||
        InterlockedCompareExchange(&g_Bc250LogFileOwner, 1, 0) != 0) {
        Bc250LogRingAppend(buffer, len);
        return;
    }

    {
        UNICODE_STRING logFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\BC250_DISPLAY_LOG.txt");
        OBJECT_ATTRIBUTES objAttr;
        IO_STATUS_BLOCK ioStatus;
        HANDLE fileHandle = NULL;

        InitializeObjectAttributes(&objAttr, &logFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(ZwCreateFile(&fileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
            /* Drena logs acumulados em memoria antes do buffer corrente */
            Bc250LogRingDrainToFile(fileHandle);
            (void)ZwWriteFile(fileHandle, NULL, NULL, NULL, &ioStatus, buffer, (ULONG)len, NULL, NULL);
            /* Linhas que entraram no anel enquanto esta thread era a dona. */
            Bc250LogRingDrainToFile(fileHandle);
            (void)ZwFlushBuffersFile(fileHandle, &ioStatus);
            ZwClose(fileHandle);
        } else {
            /* Arquivo indisponivel: a linha espera no anel em vez de sumir. */
            Bc250LogRingAppend(buffer, len);
        }
        InterlockedExchange(&g_Bc250LogFileOwner, 0);
    }
}

/*
 * LOG.1 (doc 585): DriverEntry, AddDevice e StartDevice rodam com APCs
 * desligadas, entao TODAS as linhas deles vao para o anel em RAM e so' chegam
 * ao arquivo quando alguem escreve uma linha num contexto normal. Na Corrida
 * 213 o StartDevice falhou, o dispositivo foi removido e nenhum contexto
 * normal veio: zero linhas no arquivo, e uma corrida inteira sem diagnostico.
 * A thread do rastro duravel ja roda em PASSIVE com APCs ligadas a cada
 * 100 ms e ja faz I/O de arquivo durante a paginacao; ela passa a drenar o
 * anel, com o mesmo dono unico do arquivo (DX.88) e o mesmo caminho de escrita.
 */
void Bc250DisplayLogDrainFromWorker(void)
{
    UNICODE_STRING logFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\BC250_DISPLAY_LOG.txt");
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE fileHandle = NULL;
    BOOLEAN pending;
    KIRQL irql;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || KeAreApcsDisabled()) {
        return;
    }
    if (InterlockedCompareExchange(&g_Bc250PagingLogLockInitOnce, 1, 0) == 0) {
        KeInitializeSpinLock(&g_Bc250PagingLogLock);
    }
    KeAcquireSpinLock(&g_Bc250PagingLogLock, &irql);
    pending = (g_Bc250PagingLogTail != g_Bc250PagingLogHead) ? TRUE : FALSE;
    KeReleaseSpinLock(&g_Bc250PagingLogLock, irql);
    if (!pending) {
        return;
    }
    if (InterlockedCompareExchange(&g_Bc250LogFileOwner, 1, 0) != 0) {
        return;
    }
    InitializeObjectAttributes(&objAttr, &logFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwCreateFile(&fileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
        Bc250LogRingDrainToFile(fileHandle);
        (void)ZwFlushBuffersFile(fileHandle, &ioStatus);
        ZwClose(fileHandle);
    }
    InterlockedExchange(&g_Bc250LogFileOwner, 0);
}

void Bc250DisplayLogSamplingSummary(void)
{
    Bc250DisplayLog("[LOG_SAMPLING_SUMMARY] BuildPaging=%ld UpdatePT=%ld ActiveUpdatePT=%ld Flush=%ld FlushMapped=%ld PtWalk=%ld SubmitNode0=%ld PagingOps=%ld Alloc=%ld Retire=%ld SubmitDdi=%ld MiscDdi=%ld First=%ld Period=%ld\n",
        g_Bc250VerboseLogOrdinal[1],
        g_Bc250VerboseLogOrdinal[2],
        g_Bc250VerboseLogOrdinal[3],
        g_Bc250VerboseLogOrdinal[4],
        g_Bc250VerboseLogOrdinal[5],
        g_Bc250VerboseLogOrdinal[6],
        g_Bc250VerboseLogOrdinal[7],
        g_Bc250VerboseLogOrdinal[8],
        g_Bc250VerboseLogOrdinal[9],
        g_Bc250VerboseLogOrdinal[10],
        g_Bc250VerboseLogOrdinal[11],
        g_Bc250VerboseLogOrdinal[12],
        BC250_VERBOSE_LOG_FIRST,
        BC250_VERBOSE_LOG_PERIOD);
}
#endif

static VOID Bc250RecordDisplayProbePassive(
    _In_ BC250_PROBE_FIELD Field,
    _In_ ULONG Value)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
        Bc250ProbeRecord(Field, Value);
    }
#else
    UNREFERENCED_PARAMETER(Field);
    UNREFERENCED_PARAMETER(Value);
#endif
}

static BOOLEAN Bc250CanRecordDisplayProbePassive(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
#else
    return TRUE;
#endif
}

static VOID Bc250RecordVsyncProofAfterSuccessfulPresent(void)
{
    LONG vsyncCount = InterlockedCompareExchange(
        &g_Bc250DisplayState.VsyncCount, 0, 0);

    if (vsyncCount > 0 && Bc250CanRecordDisplayProbePassive() &&
        InterlockedCompareExchange(
            &g_Bc250DisplayState.VsyncProofRecorded, 1, 0) == 0) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_VSYNC_COUNT, (ULONG)vsyncCount);
    }
}

static VOID Bc250FillNativeSignalInfo(
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *SignalInfo)
{
    ULONGLONG horizontalTotal;
    ULONGLONG verticalTotal;
    ULONGLONG refreshNumerator;
    ULONGLONG refreshDenominator;
    ULONGLONG pixelRate;

    BC250_DCN_TIMING native;
    UINT hsyncNumerator = BC250_NATIVE_HSYNC_NUM;
    UINT hsyncDenominator = BC250_NATIVE_HSYNC_DEN;

    RtlZeroMemory(SignalInfo, sizeof(*SignalInfo));
    horizontalTotal = (ULONGLONG)g_Bc250DisplayState.CurrentMode.Width +
        BC250_NATIVE_HBLANK;
    verticalTotal = (ULONGLONG)g_Bc250DisplayState.CurrentMode.Height +
        BC250_NATIVE_VBLANK;
    refreshNumerator = BC250_NATIVE_REFRESH_NUM;
    refreshDenominator = BC250_NATIVE_REFRESH_DEN;
    pixelRate = BC250_NATIVE_PIXEL_RATE;
    /* DCN.2 item 1 (doc 713): o timing que o OTG0 varre, medido no D2. */
    Bc250NativeTiming(&native);
    if (native.RefreshMilliHz != 0u) {
        horizontalTotal = native.HTotal;
        verticalTotal = native.VTotal;
        refreshNumerator = native.RefreshMilliHz;
        refreshDenominator = 1000u;
        pixelRate = native.PixelRateHz;
        hsyncNumerator = native.RefreshMilliHz * native.VTotal;
        hsyncDenominator = 1000u;
    }

    SignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    SignalInfo->TotalSize.cx = (UINT)horizontalTotal;
    SignalInfo->TotalSize.cy = (UINT)verticalTotal;
    SignalInfo->ActiveSize.cx =
        g_Bc250DisplayState.CurrentMode.Width;
    SignalInfo->ActiveSize.cy =
        g_Bc250DisplayState.CurrentMode.Height;
    SignalInfo->VSyncFreq.Numerator = (UINT)refreshNumerator;
    SignalInfo->VSyncFreq.Denominator = (UINT)refreshDenominator;
    SignalInfo->HSyncFreq.Numerator = hsyncNumerator;
    SignalInfo->HSyncFreq.Denominator = hsyncDenominator;
    SignalInfo->PixelRate = pixelRate;
    SignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    Bc250DisplayLog("[SignalInfo] Active=%ux%u, Total=%ux%u, VSync=%u/%u Hz, HSync=%u/%u Hz, PixelRate=%I64u\n",
        SignalInfo->ActiveSize.cx, SignalInfo->ActiveSize.cy,
        SignalInfo->TotalSize.cx, SignalInfo->TotalSize.cy,
        SignalInfo->VSyncFreq.Numerator, SignalInfo->VSyncFreq.Denominator,
        SignalInfo->HSyncFreq.Numerator, SignalInfo->HSyncFreq.Denominator,
        SignalInfo->PixelRate);
}

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static BOOLEAN Bc250SynchronizedVsyncCallback(
    _In_opt_ PVOID SynchronizeContext)
{
    BC250_DISPLAY_STATE *state = (BC250_DISPLAY_STATE *)SynchronizeContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
    LONG interruptType;

    if (state == NULL ||
        InterlockedCompareExchange(&state->DisplayStarted, 1, 1) == 0 ||
        InterlockedCompareExchange(&state->PowerStateD0, 1, 1) == 0 ||
        InterlockedCompareExchange(&state->VsyncEnabled, 1, 1) == 0 ||
        state->DxgkInterface.DxgkCbNotifyInterrupt == NULL) {
        return FALSE;
    }

    interruptType = InterlockedCompareExchange(&state->ActiveInterruptType, 0, 0);
    RtlZeroMemory(&data, sizeof(data));

    if (interruptType == (LONG)DXGK_INTERRUPT_CRTC_VSYNC ||
        interruptType == (LONG)DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY) {
        data.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
        data.CrtcVsync.VidPnTargetId = 1;
        if (Bc250DcnFlipDisplayedSegment() != 0) {
            /* DCN.1 D4: so' a primaria que o HUBP0 ja latchou. */
            data.CrtcVsync.PhysicalAddress.QuadPart = Bc250DcnFlipDisplayedSegment();
        } else if (state->PrimaryAddressValid && state->PrimaryAddress.QuadPart != 0) {
            data.CrtcVsync.PhysicalAddress = state->PrimaryAddress;
        } else {
            data.CrtcVsync.PhysicalAddress = state->FrameBufferPhysical;
        }
        state->DxgkInterface.DxgkCbNotifyInterrupt(
            state->DxgkInterface.DeviceHandle, &data);
    } else {
        data.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_VSYNC;
        data.DisplayOnlyVsync.VidPnTargetId = 1;
        state->DxgkInterface.DxgkCbNotifyInterrupt(
            state->DxgkInterface.DeviceHandle, &data);
    }

    {
        LONG vCount = InterlockedIncrement(&state->VsyncCount);
        if (vCount == 1 || (vCount % 60) == 0) {
            Bc250DisplayLog("[Vsync #%ld] Type=%ld, TargetId=1, PhysAddr=0x%llX, OTG_Frame=%u\n",
                vCount, interruptType,
                (unsigned long long)(data.InterruptType == DXGK_INTERRUPT_CRTC_VSYNC ?
                    data.CrtcVsync.PhysicalAddress.QuadPart : 0),
                state->LastOtgFrameCount);
        }
    }
    return TRUE;
}

static VOID Bc250VsyncDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    BOOLEAN notified = FALSE;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedCompareExchange(
            &g_Bc250DisplayState.DisplayStarted, 1, 1) == 0 ||
        InterlockedCompareExchange(
            &g_Bc250DisplayState.PowerStateD0, 1, 1) == 0 ||
        InterlockedCompareExchange(
            &g_Bc250DisplayState.VsyncEnabled, 1, 1) == 0 ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyDpc == NULL) {
        return;
    }

    if (g_Bc250DisplayState.OtgVsyncActive &&
        g_Bc250DisplayState.OtgFrameCountRegister != NULL) {
        static ULONG s_StallTicks = 0;
        ULONG otgCrumb = Bc250CrumbBefore(BC250_CRUMB_SITE_OTG_READ, 0x14030u);
        ULONG currentFrame = *g_Bc250DisplayState.OtgFrameCountRegister;
        LONG fromIrq;
        Bc250CrumbAfter(otgCrumb);
        /* DCN.2 item 2 (doc 713): com a interrupcao do OTG0 viva, so' ela
         * gera VSYNC; o timer de 4 ms vira rede (fromIrq = -1). */
        fromIrq = Bc250Dcn2VsyncFromIrq(currentFrame);
        if (fromIrq == 0) {
            return;
        }
        if (fromIrq < 0) {
            if (currentFrame == g_Bc250DisplayState.LastOtgFrameCount) {
                /* Quadro de hardware ainda nao avancou; se atrasar mais de 32ms (8 ticks de 4ms), libera Vsync por tempo */
                if (++s_StallTicks < 8u) {
                    return;
                }
            }
            s_StallTicks = 0u;
            g_Bc250DisplayState.LastOtgFrameCount = currentFrame;
            InterlockedIncrement(&g_Bc250DisplayState.HardwareVsyncCount);
        }
    }
    Bc250DcnFlipPollLatch(); /* DCN.1 D4 */
    Bc250Dcn2CrcSample();    /* DCN.2 item 3 */

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbSynchronizeExecution(
        g_Bc250DisplayState.DxgkInterface.DeviceHandle,
        Bc250SynchronizedVsyncCallback,
        &g_Bc250DisplayState,
        0,
        &notified);
    if (NT_SUCCESS(status) && notified) {
        g_Bc250DisplayState.DxgkInterface.DxgkCbNotifyDpc(
            g_Bc250DisplayState.DxgkInterface.DeviceHandle);
    }
}

static VOID Bc250SetVsyncState(BOOLEAN Enable)
{
    InterlockedExchange(
        &g_Bc250DisplayState.VsyncRequested, Enable ? 1 : 0);
    if (!g_Bc250DisplayState.VsyncTimerInitialized) {
        return;
    }

    if (Enable &&
        InterlockedCompareExchange(
            &g_Bc250DisplayState.DisplayStarted, 1, 1) != 0 &&
        InterlockedCompareExchange(
            &g_Bc250DisplayState.PowerStateD0, 1, 1) != 0) {
        LARGE_INTEGER due;
        LONG periodMs = g_Bc250DisplayState.OtgVsyncActive ? 4 : 17;
        due.QuadPart = -(LONGLONG)periodMs * 10000LL;
        InterlockedExchange(&g_Bc250DisplayState.VsyncEnabled, 1);
        KeSetTimerEx(&g_Bc250DisplayState.VsyncTimer, due, periodMs, &g_Bc250DisplayState.VsyncDpc);
    } else {
        InterlockedExchange(&g_Bc250DisplayState.VsyncEnabled, 0);
        KeCancelTimer(&g_Bc250DisplayState.VsyncTimer);
    }
}
#else
static VOID Bc250SetVsyncState(BOOLEAN Enable) { UNREFERENCED_PARAMETER(Enable); }
#endif

static BOOLEAN Bc250ClipRectToMode(
    _In_ CONST RECT *Input,
    _Out_ RECT *Clipped)
{
    LONG width = (LONG)g_Bc250DisplayState.CurrentMode.Width;
    LONG height = (LONG)g_Bc250DisplayState.CurrentMode.Height;

    if (Input == NULL || Clipped == NULL || width <= 0 || height <= 0) {
        return FALSE;
    }

    Clipped->left = (Input->left < 0) ? 0 : Input->left;
    Clipped->top = (Input->top < 0) ? 0 : Input->top;
    Clipped->right = (Input->right > width) ? width : Input->right;
    Clipped->bottom = (Input->bottom > height) ? height : Input->bottom;
    return Clipped->right > Clipped->left &&
           Clipped->bottom > Clipped->top;
}

static BOOLEAN Bc250AcquirePresentAccess(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    if (!g_Bc250DisplayState.PresentRundownInitialized) {
        return FALSE;
    }
    if (!ExAcquireRundownProtection(&g_Bc250DisplayState.PresentRundown)) {
        return FALSE;
    }
#endif
    if (g_Bc250DisplayState.DisplayStarted == 0 ||
        g_Bc250DisplayState.PowerStateD0 == 0 ||
        g_Bc250DisplayState.FrameBufferVirtual == NULL) {
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        ExReleaseRundownProtection(&g_Bc250DisplayState.PresentRundown);
#endif
        return FALSE;
    }
    return TRUE;
}

static VOID Bc250ReleasePresentAccess(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    ExReleaseRundownProtection(&g_Bc250DisplayState.PresentRundown);
#endif
}

static void Bc250BuildEdid(void)
{
    UCHAR *e = g_Bc250DisplayState.Edid;
    ULONG hActive = g_Bc250DisplayState.CurrentMode.Width;
    ULONG vActive = g_Bc250DisplayState.CurrentMode.Height;
    ULONG hTotal, vTotal, clock10kHz, hSizeMm, vSizeMm;
    ULONG sum = 0, i;
    UCHAR *d;
    static CONST UCHAR stdTimings[16] = {
        0xD1, 0xC0,   /* 1920x1080 @60, 16:9  */
        0xA9, 0x40,   /* 1600x900  @60, 16:9  */
        0x95, 0x00,   /* 1440x900  @60, 16:10 */
        0x90, 0x40,   /* 1400x1050 @60, 4:3   */
        0x81, 0x80,   /* 1280x1024 @60, 5:4   */
        0x81, 0x40,   /* 1280x960  @60, 4:3   */
        0x81, 0x00,   /* 1280x800  @60, 16:10 */
        0x81, 0xC0    /* 1280x720  @60, 16:9  */
    };

    g_Bc250DisplayState.EdidValid = FALSE;
    RtlZeroMemory(e, 128);

    if (hActive == 0 || vActive == 0) return;

    hTotal = hActive + BC250_NATIVE_HBLANK;
    vTotal = vActive + BC250_NATIVE_VBLANK;
    clock10kHz = (ULONG)((BC250_NATIVE_PIXEL_RATE + 5000ULL) / 10000ULL);
    if (clock10kHz > 0xFFFF) clock10kHz = 0xFFFF;

    hSizeMm = (hActive * 254u) / 960u;
    vSizeMm = (vActive * 254u) / 960u;
    if (hSizeMm > 4095u) hSizeMm = 4095u;
    if (vSizeMm > 4095u) vSizeMm = 4095u;

    /* Header */
    e[0] = 0x00;
    for (i = 1; i <= 6; i++) e[i] = 0xFF;
    e[7] = 0x00;

    /* Vendor "BCX" */
    e[8] = 0x08; e[9] = 0x78;
    /* Product 0x13FE */
    e[10] = 0xFE; e[11] = 0x13;
    /* Serial 1 */
    e[12] = 0x01; e[13] = 0x00; e[14] = 0x00; e[15] = 0x00;
    e[16] = 1; e[17] = 36; e[18] = 1; e[19] = 3;

    /* Basic params */
    e[20] = 0x80;
    e[21] = (UCHAR)(hSizeMm / 10u);
    e[22] = (UCHAR)(vSizeMm / 10u);
    e[23] = 120;
    e[24] = 0x02;

    /* Chromaticity */
    e[25] = 0xEE; e[26] = 0x91; e[27] = 0xA3; e[28] = 0x54; e[29] = 0x4C;
    e[30] = 0x99; e[31] = 0x26; e[32] = 0x0F; e[33] = 0x50; e[34] = 0x54;

    /* Established timings */
    e[35] = 0x21; e[36] = 0x08; e[37] = 0x00;

    /* Standard timings */
    RtlCopyMemory(&e[38], stdTimings, sizeof(stdTimings));

    /* Detailed timing descriptor 1: native mode */
    d = &e[54];
    {
        BC250_DCN_TIMING native;
        Bc250NativeTiming(&native);
        if (native.RefreshMilliHz != 0u) {
            /* DCN.2 item 1 (doc 713): DTD do timing que o OTG0 varre. */
            Bc250DcnEncodeDtd(&native, hSizeMm, vSizeMm, d);
            clock10kHz = (ULONG)((native.PixelRateHz + 5000ULL) / 10000ULL);
            goto Bc250EdidDtdDone;
        }
    }
    d[0] = (UCHAR)(clock10kHz & 0xFF);
    d[1] = (UCHAR)((clock10kHz >> 8) & 0xFF);
    d[2] = (UCHAR)(hActive & 0xFF);
    d[3] = (UCHAR)(BC250_NATIVE_HBLANK & 0xFF);
    d[4] = (UCHAR)((((hActive >> 8) & 0x0F) << 4) |
        ((BC250_NATIVE_HBLANK >> 8) & 0x0F));
    d[5] = (UCHAR)(vActive & 0xFF);
    d[6] = (UCHAR)(BC250_NATIVE_VBLANK & 0xFF);
    d[7] = (UCHAR)((((vActive >> 8) & 0x0F) << 4) |
        ((BC250_NATIVE_VBLANK >> 8) & 0x0F));
    d[8] = 48; d[9] = 32; d[10] = (UCHAR)((3u << 4) | 5u); d[11] = 0;
    d[12] = (UCHAR)(hSizeMm & 0xFF);
    d[13] = (UCHAR)(vSizeMm & 0xFF);
    d[14] = (UCHAR)((((hSizeMm >> 8) & 0x0F) << 4) | ((vSizeMm >> 8) & 0x0F));
    d[15] = 0; d[16] = 0; d[17] = 0x1E;
Bc250EdidDtdDone:

    /* Detailed descriptor 2: range limits */
    d = &e[72];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0xFD; d[4] = 0;
    d[5] = 50; d[6] = 61; d[7] = 20; d[8] = 90; d[9] = 19; d[10] = 0; d[11] = 0x0A;
    for (i = 12; i < 18; i++) d[i] = 0x20;

    /* Detailed descriptor 3: monitor name */
    d = &e[90];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0xFC; d[4] = 0;
    d[5] = 'B'; d[6] = 'C'; d[7] = '-'; d[8] = '2'; d[9] = '5'; d[10] = '0';
    d[11] = ' '; d[12] = 'G'; d[13] = 'O'; d[14] = 'P'; d[15] = 0x0A; d[16] = 0x20; d[17] = 0x20;

    /* Detailed descriptor 4: dummy */
    d = &e[108];
    d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0x10; d[4] = 0;

    /* Checksum */
    e[126] = 0;
    for (i = 0; i < 127; i++) sum += e[i];
    e[127] = (UCHAR)((0x100u - (sum & 0xFFu)) & 0xFFu);

    Bc250DisplayLog("[EDID] Synthesized %u bytes, Checksum=0x%02X (sum=0x%X)\n",
        128, e[127], sum);
    Bc250DisplayLog("[EDID 00-15] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7], e[8], e[9], e[10], e[11], e[12], e[13], e[14], e[15]);
    Bc250DisplayLog("[EDID DTD1] PixelClock10kHz=%u, HActive=%u, HBlank=%u, VActive=%u, VBlank=%u\n",
        clock10kHz, hActive, BC250_NATIVE_HBLANK, vActive, BC250_NATIVE_VBLANK);

    g_Bc250DisplayState.EdidValid = TRUE;
}

NTSTATUS Bc250DisplayInit(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_ const DXGK_DISPLAY_INFORMATION *PostDisplayInfo)
{
    RtlZeroMemory(&g_Bc250DisplayState, sizeof(g_Bc250DisplayState));
    if (DxgkInterface == NULL || PostDisplayInfo == NULL ||
        PostDisplayInfo->PhysicAddress.QuadPart == 0 ||
        PostDisplayInfo->Width == 0 || PostDisplayInfo->Height == 0 ||
        PostDisplayInfo->Width > (MAXULONG / 4u) ||
        PostDisplayInfo->Pitch < (PostDisplayInfo->Width * 4u) ||
        PostDisplayInfo->Height > (MAXULONG / PostDisplayInfo->Pitch)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(&g_Bc250DisplayState.DxgkInterface,
                  DxgkInterface, sizeof(DXGKRNL_INTERFACE));
    RtlCopyMemory(&g_Bc250DisplayState.PostDisplayInfo,
                  PostDisplayInfo, sizeof(DXGK_DISPLAY_INFORMATION));
    g_Bc250DisplayState.DisplayAcquired = 1;
    g_Bc250DisplayState.FrameBufferPhysical = PostDisplayInfo->PhysicAddress;
    g_Bc250DisplayState.CurrentMode.Width = PostDisplayInfo->Width;
    g_Bc250DisplayState.CurrentMode.Height = PostDisplayInfo->Height;
    g_Bc250DisplayState.CurrentMode.Pitch = PostDisplayInfo->Pitch;
    g_Bc250DisplayState.CurrentMode.Format = D3DDDIFMT_A8R8G8B8;
    g_Bc250DisplayState.CurrentMode.RefreshRateNumerator = BC250_NATIVE_REFRESH_NUM;
    g_Bc250DisplayState.CurrentMode.RefreshRateDenominator = BC250_NATIVE_REFRESH_DEN;
    g_Bc250DisplayState.ModeCount = 1;
    g_Bc250DisplayState.SourceVisible = 1;

    Bc250BuildEdid();
    Bc250DisplayLog("[DisplayInit] Acquired GOP: %ux%u, Pitch=%u, Phys=0x%llX, DeviceHandle=0x%p\n",
        PostDisplayInfo->Width, PostDisplayInfo->Height, PostDisplayInfo->Pitch,
        PostDisplayInfo->PhysicAddress.QuadPart, g_Bc250DisplayState.DxgkInterface.DeviceHandle);

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    KeInitializeSpinLock(&g_Bc250DisplayGpuVaLock);
    g_Bc250DisplayGpuVaLockInit = TRUE;
    Bc250DisplayClearGpuVaMappings();
    KeInitializeTimer(&g_Bc250DisplayState.VsyncTimer);
    KeInitializeDpc(&g_Bc250DisplayState.VsyncDpc, Bc250VsyncDpcRoutine, NULL);
    ExInitializeRundownProtection(&g_Bc250DisplayState.PresentRundown);
    g_Bc250DisplayState.PresentRundownInitialized = TRUE;
    g_Bc250DisplayState.VsyncTimerInitialized = TRUE;
    InterlockedExchange(&g_Bc250DisplayState.PowerStateD0, 1);

    if (g_Bc250DisplayState.FrameBufferPhysical.QuadPart != 0) {
        /*
         * T3.1 (28/09, doc 520): a janela WC para no inicio da faixa privada
         * (../core/bc250_vram_cache_policy.h). Antes ela cobria os 256 MB da
         * BAR 0, e anel do SDMA, anel grafico, MQD e migalhas eram mapeados
         * UC POR CIMA dela -- mesma pagina fisica com dois tipos de memoria,
         * o que a Intel e a AMD declaram nao suportado. Tambem saiu o
         * fallback PAGE_NOCACHE: ele aliasaria as alocacoes WC do VidMm.
         * Quem le a faixa privada pela janela ja confere FrameBufferSize e
         * passa a recusar (falha fechada) em vez de ler por um alias.
         */
        ULONG fbBytes = (ULONG)BC250_VRAM_WC_WINDOW_BYTES;
        /* P1b: a janela WC enxerga a VRAM pela DRAM quando armada (aper_base em APU). */
        PHYSICAL_ADDRESS fbCpuPhysical;
        fbCpuPhysical.QuadPart = (LONGLONG)Bc250VramCpuTranslate(
            (ULONGLONG)g_Bc250DisplayState.FrameBufferPhysical.QuadPart);
        g_Bc250DisplayState.FrameBufferSize = fbBytes;
        g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
            fbCpuPhysical,
            fbBytes,
            PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            fbBytes = 128u * 1024u * 1024u;
            g_Bc250DisplayState.FrameBufferSize = fbBytes;
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                fbCpuPhysical,
                fbBytes,
                PAGE_READWRITE | PAGE_WRITECOMBINE);
        }
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            fbBytes = g_Bc250DisplayState.CurrentMode.Pitch * g_Bc250DisplayState.CurrentMode.Height;
            g_Bc250DisplayState.FrameBufferSize = fbBytes;
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                fbCpuPhysical,
                fbBytes,
                PAGE_READWRITE | PAGE_WRITECOMBINE);
        }
        Bc250DisplayLog("[DisplayInit] FrameBuffer mapping: Phys=0x%llX, Size=%u, Virt=%p\n",
            (unsigned long long)g_Bc250DisplayState.FrameBufferPhysical.QuadPart,
            g_Bc250DisplayState.FrameBufferSize,
            g_Bc250DisplayState.FrameBufferVirtual);
        Bc250DisplayLog("[T31_CACHE_MAPA] WC [0x0,0x%X) scanout+segmento do VidMm | faixa privada UC comeca em 0x%llX -- sem sobreposicao WC/UC\n",
            g_Bc250DisplayState.FrameBufferSize,
            (unsigned long long)BC250_VRAM_WC_WINDOW_BYTES);
    }
    Bc250ProbeRecord(
        BC250_PROBE_DMM_DISPLAY_MAPPED,
        (g_Bc250DisplayState.FrameBufferVirtual != NULL) ? 1 : 0);
    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 1);
#endif
    return STATUS_SUCCESS;
}

void Bc250DisplaySetMmioBase(_In_ PHYSICAL_ADDRESS MmioPhysical)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    g_Bc250DisplayState.MmioPhysical = MmioPhysical;
    if (MmioPhysical.QuadPart != 0) {
        PHYSICAL_ADDRESS otgRegPhysical;
        otgRegPhysical.QuadPart = MmioPhysical.QuadPart + 0x14030;
        g_Bc250DisplayState.OtgFrameCountRegister = (volatile ULONG *)MmMapIoSpace(
            otgRegPhysical,
            sizeof(ULONG),
            MmNonCached);
        if (g_Bc250DisplayState.OtgFrameCountRegister != NULL) {
            g_Bc250DisplayState.LastOtgFrameCount = *g_Bc250DisplayState.OtgFrameCountRegister;
            g_Bc250DisplayState.OtgVsyncActive = TRUE;
            Bc250DisplayLog("[DisplayInit] OTG VSync linked to BAR5 at 0x%llX (Offset 0x14030), InitialFrameCount=%u\n",
                MmioPhysical.QuadPart, g_Bc250DisplayState.LastOtgFrameCount);
        } else {
            Bc250DisplayLog("[DisplayInit] WARNING: Failed to map OTG frame counter at 0x%llX\n",
                otgRegPhysical.QuadPart);
        }
        Bc250DcnFlipReadGop(MmioPhysical); /* DCN.1 D2, so' leitura */
    }
#else
    UNREFERENCED_PARAMETER(MmioPhysical);
#endif
}

void Bc250DisplayTeardown(void)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 0);
    Bc250SetVsyncState(FALSE);
    if (g_Bc250DisplayState.VsyncTimerInitialized) {
        KeCancelTimer(&g_Bc250DisplayState.VsyncTimer);
        KeFlushQueuedDpcs();
    }
    if (g_Bc250DisplayState.PresentRundownInitialized) {
        ExWaitForRundownProtectionRelease(&g_Bc250DisplayState.PresentRundown);
        g_Bc250DisplayState.PresentRundownInitialized = FALSE;
    }
    Bc250Dcn2Teardown("TEARDOWN");      /* DCN.2, Lei 5 */
    Bc250DcnFlipRestoreGop("TEARDOWN"); /* DCN.1, Lei 5 */
    Bc250DcnFlipUnmap();
    if (g_Bc250DisplayState.OtgFrameCountRegister != NULL) {
        MmUnmapIoSpace((PVOID)g_Bc250DisplayState.OtgFrameCountRegister, sizeof(ULONG));
        g_Bc250DisplayState.OtgFrameCountRegister = NULL;
        g_Bc250DisplayState.OtgVsyncActive = FALSE;
    }
    if (g_Bc250DisplayState.FrameBufferVirtual != NULL) {
        MmUnmapIoSpace(g_Bc250DisplayState.FrameBufferVirtual, g_Bc250DisplayState.FrameBufferSize);
        g_Bc250DisplayState.FrameBufferVirtual = NULL;
        g_Bc250DisplayState.FrameBufferSize = 0;
    }
    Bc250DisplayClearGpuVaMappings();
    Bc250SdmaPresentTeardown();
    /* P1b: o proximo StartDevice decide de novo o caminho da CPU ate a VRAM. */
    if (g_Bc250VramCpuViaDram != 0) {
        Bc250DisplayLog("[P1B_VRAM_DRAM] TEARDOWN: desarmada (HDP omitido %lld vezes)\n",
            (long long)g_Bc250VramCpuHdpSkipped);
    }
    InterlockedExchange(&g_Bc250VramCpuViaDram, 0);
#else
    Bc250DisplayClearGpuVaMappings();
#endif
}

static NTSTATUS Bc250AddSingleSourceMode(
    _In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pSourceModeSetInterface,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hSourceModeSet,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    D3DKMDT_VIDPN_SOURCE_MODE *pSourceModeInfo = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(SourceId);

    if (pSourceModeSetInterface == NULL || hSourceModeSet == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pSourceModeSetInterface->pfnCreateNewModeInfo(
        hSourceModeSet, &pSourceModeInfo);
    if (!NT_SUCCESS(status) || pSourceModeInfo == NULL) {
        return status;
    }

    pSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
    pSourceModeInfo->Format.Graphics.VisibleRegionSize.cx = g_Bc250DisplayState.CurrentMode.Width;
    pSourceModeInfo->Format.Graphics.VisibleRegionSize.cy = g_Bc250DisplayState.CurrentMode.Height;
    pSourceModeInfo->Format.Graphics.PrimSurfSize = pSourceModeInfo->Format.Graphics.VisibleRegionSize;
    pSourceModeInfo->Format.Graphics.Stride = g_Bc250DisplayState.CurrentMode.Pitch;
    pSourceModeInfo->Format.Graphics.PixelFormat = g_Bc250DisplayState.CurrentMode.Format;
    pSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
    pSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

    status = pSourceModeSetInterface->pfnAddMode(hSourceModeSet, pSourceModeInfo);
    if (!NT_SUCCESS(status)) {
        (void)pSourceModeSetInterface->pfnReleaseModeInfo(hSourceModeSet, pSourceModeInfo);
        return status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS Bc250AddSingleTargetMode(
    _In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pTargetModeSetInterface,
    _In_ D3DKMDT_HVIDPNTARGETMODESET hTargetModeSet,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    D3DKMDT_VIDPN_TARGET_MODE *pTargetModeInfo = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(TargetId);

    if (pTargetModeSetInterface == NULL || hTargetModeSet == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    status = pTargetModeSetInterface->pfnCreateNewModeInfo(
        hTargetModeSet, &pTargetModeInfo);
    if (!NT_SUCCESS(status) || pTargetModeInfo == NULL) {
        return status;
    }

    Bc250FillNativeSignalInfo(&pTargetModeInfo->VideoSignalInfo);
    pTargetModeInfo->Preference = D3DKMDT_MP_PREFERRED;

    status = pTargetModeSetInterface->pfnAddMode(hTargetModeSet, pTargetModeInfo);
    if (!NT_SUCCESS(status)) {
        (void)pTargetModeSetInterface->pfnReleaseModeInfo(hTargetModeSet, pTargetModeInfo);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryChildRelations(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_RELATIONS_CALLED, ChildRelationsSize);

    if (ChildRelations == NULL || ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        Bc250ProbeRecordFirstFailure(STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);

    Bc250DisplayLog("[ChildRelations] Called. Size=%u, ChildEnumEnabled=%d\n",
        ChildRelationsSize, (int)Bc250DisplayGetChildEnumeration());

    if (!Bc250DisplayGetChildEnumeration()) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_V5_CHILD_ENUMERATION_ENABLED, 0u);
        return STATUS_SUCCESS;
    }
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_V5_CHILD_ENUMERATION_ENABLED, 1u);

    ChildRelations[0].ChildDeviceType = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_DISPLAYPORT_EXTERNAL;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    ChildRelations[0].AcpiUid = 0;
    ChildRelations[0].ChildUid = 1;

    Bc250DisplayLog("[ChildRelations] Returned DP ChildUid=1\n");
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryChildStatus(
    CONST PVOID MiniportDeviceContext,
    PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED, (ULONG)ChildStatus->Type);

    if (ChildStatus->Type == StatusConnection) {
        ChildStatus->HotPlug.Connected =
            Bc250DisplayGetChildEnumeration() ? TRUE : FALSE;
        Bc250DisplayLog("[ChildStatus] StatusConnection: ChildUid=%u, Connected=%d\n",
            ChildStatus->ChildUid, (int)ChildStatus->HotPlug.Connected);
        return STATUS_SUCCESS;
    }

    if (ChildStatus->Type == StatusRotation) {
        ChildStatus->Rotation.Angle = 0;
        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryDeviceDescriptor(
    CONST PVOID MiniportDeviceContext,
    ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    ULONG available, copy;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    Bc250ProbeRecord(BC250_PROBE_DMM_DESCRIPTOR_CALLED, ChildUid);

    if (DeviceDescriptor == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[QueryDeviceDescriptor] ChildUid=%u, Offset=%u, Len=%u\n",
        ChildUid, DeviceDescriptor->DescriptorOffset, DeviceDescriptor->DescriptorLength);

    {
        /* DCN.2 item 6 (doc 713): o EDID real lido por AUX, so' quando o DTD
         * dele e' o timing que o OTG varre. */
        const UCHAR *real = NULL;
        ULONG realBytes = 0u;
        if (ChildUid == 1 && Bc250DcnRealEdid(&real, &realBytes) && real != NULL) {
            if (DeviceDescriptor->DescriptorOffset >= realBytes ||
                DeviceDescriptor->DescriptorBuffer == NULL ||
                DeviceDescriptor->DescriptorLength == 0) {
                return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
            }
            available = realBytes - DeviceDescriptor->DescriptorOffset;
            copy = (DeviceDescriptor->DescriptorLength < available)
                 ? DeviceDescriptor->DescriptorLength : available;
            RtlCopyMemory(DeviceDescriptor->DescriptorBuffer,
                &real[DeviceDescriptor->DescriptorOffset], copy);
            DeviceDescriptor->DescriptorLength = copy;
            Bc250DisplayLog("[QueryDeviceDescriptor] EDID REAL (DCN.2): Offset=%u Len=%u de %lu\n",
                DeviceDescriptor->DescriptorOffset, copy, realBytes);
            return STATUS_SUCCESS;
        }
    }

    if (ChildUid != 1 || !Bc250DisplayGetSyntheticEdidEnabled() || !g_Bc250DisplayState.EdidValid) {
        Bc250DisplayLog("[QueryDeviceDescriptor] ChildUid=%u, SyntheticEdid=%d, EdidValid=%d -> STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA (forcing DxgkDdiRecommendMonitorModes)\n",
            ChildUid, (int)Bc250DisplayGetSyntheticEdidEnabled(), (int)g_Bc250DisplayState.EdidValid);
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    if (DeviceDescriptor->DescriptorOffset >= 128 ||
        DeviceDescriptor->DescriptorBuffer == NULL ||
        DeviceDescriptor->DescriptorLength == 0) {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }

    available = 128 - DeviceDescriptor->DescriptorOffset;
    copy = (DeviceDescriptor->DescriptorLength < available)
         ? DeviceDescriptor->DescriptorLength : available;

    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer,
                  &g_Bc250DisplayState.Edid[DeviceDescriptor->DescriptorOffset],
                  copy);
    DeviceDescriptor->DescriptorLength = copy;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiGetChildContainerId(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_CHILD_CONTAINER_ID ContainerId)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(ContainerId);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryInterface(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    BC250_V5_QI_RECORD record;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE);

    if (QueryInterface != NULL && QueryInterface->InterfaceType != NULL) {
        CONST GUID *guid = QueryInterface->InterfaceType;
        record.Data1 = guid->Data1;
        record.Data2 = guid->Data2;
        record.Data3 = guid->Data3;
        RtlCopyMemory(record.Data4, guid->Data4, sizeof(record.Data4));
        record.Version = QueryInterface->Version;
        record.Size = QueryInterface->Size;
        record.Status = (ULONG)STATUS_NOT_SUPPORTED;
        record.Ordinal = 0;
        Bc250ProbeRecordQueryInterface(&record);
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryAdapterInfoDisplay(
    CONST HANDLE hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250ProbeRecord(BC250_PROBE_DMM_QUERY_ADAPTER_TYPE, (ULONG)pQueryAdapterInfo->Type);
    Bc250ProbeRecord(BC250_PROBE_DMM_QAI_OUTSIZE, pQueryAdapterInfo->OutputDataSize);
    Bc250DisplayLog("[QueryAdapterInfoDisplay] Type=%u, OutSize=%u\n",
        (UINT)pQueryAdapterInfo->Type, pQueryAdapterInfo->OutputDataSize);

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_DRIVERCAPS) {
        DXGK_DRIVERCAPS *caps;
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < 32) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        caps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(caps, pQueryAdapterInfo->OutputDataSize);

        caps->HighestAcceptableAddress.QuadPart = -1;
        caps->WDDMVersion = DXGKDDI_WDDMv1_2;

        /*
         * A1 (06/09): mesma fonte de verdade do caminho WDDM. Os valores aqui
         * eram escritos a mao e divergiram do outro handler; agora os dois
         * perfis saem da tabela em driver/core, que cita a exigencia da
         * Microsoft campo a campo.
         */
        {
            BC250_DRIVER_CAPS_POLICY capsPolicy;
            if (Bc250GetDriverCapsPolicy(BC250_WDDM_DRIVER_DISPLAY_ONLY,
                    &capsPolicy) != BC250_DRIVER_CAPS_OK) {
                Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS,
                    (ULONG)STATUS_DEVICE_CONFIGURATION_ERROR);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            caps->SupportNonVGA =
                (capsPolicy.SupportNonVGA != 0u) ? TRUE : FALSE;
            caps->SupportSmoothRotation =
                (capsPolicy.SupportSmoothRotation != 0u) ? TRUE : FALSE;
        }
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PHYSICALADAPTERCAPS) {
        DXGK_PHYSICALADAPTERCAPS *physCaps;
        if (pQueryAdapterInfo->pOutputData == NULL ||
            pQueryAdapterInfo->OutputDataSize < 16) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        physCaps = (DXGK_PHYSICALADAPTERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(physCaps, pQueryAdapterInfo->OutputDataSize);
        physCaps->NumExecutionNodes = 0;
        physCaps->PagingNodeIndex = 0;
        physCaps->Flags.Value = 0;
        physCaps->DxgkPhysicalAdapterHandle = (HANDLE)hAdapter;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_NUMPOWERCOMPONENTS) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    if (pQueryAdapterInfo->Type == DXGKQAITYPE_PREFERREDGPUNODE) {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(UINT)) {
            Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_BUFFER_TOO_SMALL);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)pQueryAdapterInfo->pOutputData = 0;
        Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, 0);
        return STATUS_SUCCESS;
    }

    Bc250ProbeRecord(BC250_PROBE_DMM_QAI_STATUS, (ULONG)STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPowerState(
    CONST PVOID MiniportDeviceContext,
    ULONG HardwareUid,
    DEVICE_POWER_STATE DevicePowerState,
    POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ActionType);

    /* DCN.2 item 6 (doc 713): o filho 1 e' o monitor; D3 apaga o OTG, D0 acende. */
    if (HardwareUid == 1u &&
        (DevicePowerState == PowerDeviceD0 || DevicePowerState == PowerDeviceD3)) {
        Bc250Dcn2Blank(DevicePowerState == PowerDeviceD3 ? TRUE : FALSE);
    }

    if (DevicePowerState == PowerDeviceD0 ||
        DevicePowerState == PowerDeviceD1 ||
        DevicePowerState == PowerDeviceD2 ||
        DevicePowerState == PowerDeviceD3) {
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        if (DevicePowerState == PowerDeviceD0) {
            InterlockedExchange(&g_Bc250DisplayState.PowerStateD0, 1);
            if (InterlockedCompareExchange(
                    &g_Bc250DisplayState.VsyncRequested, 1, 1) != 0) {
                Bc250SetVsyncState(TRUE);
            }
        } else {
            InterlockedExchange(&g_Bc250DisplayState.PowerStateD0, 0);
            InterlockedExchange(&g_Bc250DisplayState.VsyncEnabled, 0);
            if (g_Bc250DisplayState.VsyncTimerInitialized) {
                KeCancelTimer(&g_Bc250DisplayState.VsyncTimer);
            }
        }
#endif
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS APIENTRY Bc250DxgkDdiNotifyAcpiEvent(
    CONST PVOID MiniportDeviceContext,
    DXGK_EVENT_TYPE EventType,
    ULONG Event,
    PVOID Argument,
    PULONG AcpiFlags)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(Argument);
    if (AcpiFlags != NULL) {
        *AcpiFlags = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPalette(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_SETPALETTE *pSetPalette)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pSetPalette);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiGetScanLine(
    CONST PVOID MiniportDeviceContext,
    DXGKARG_GETSCANLINE *pGetScanLine)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    if (pGetScanLine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pGetScanLine->InVerticalBlank = TRUE;
    pGetScanLine->ScanLine = 0;
    return STATUS_SUCCESS;
}

VOID APIENTRY Bc250DxgkDdiControlEtwLogging(
    BOOLEAN Enable,
    ULONG Flags,
    UCHAR Level)
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Level);
}

NTSTATUS APIENTRY Bc250DxgkDdiCollectDbgInfo(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_COLLECTDBGINFO *pCollectDbgInfo)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(pCollectDbgInfo);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiNotifySurpriseRemoval(
    PVOID MiniportDeviceContext,
    DXGK_SURPRISE_REMOVAL_TYPE RemovalType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(RemovalType);
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    InterlockedExchange(&g_Bc250DisplayState.DisplayStarted, 0);
    Bc250SetVsyncState(FALSE);
#endif
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250VidPnPathSupported(
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_ BOOLEAN RequirePinnedTransform)
{
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING scaling;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION rotation;

    if (Path == NULL || Path->VidPnSourceId != 0 ||
        (Path->VidPnTargetId != 0 && Path->VidPnTargetId != 1)) {
        return FALSE;
    }
    scaling = Path->ContentTransformation.Scaling;
    rotation = Path->ContentTransformation.Rotation;
    if (RequirePinnedTransform) {
        return scaling == D3DKMDT_VPPS_IDENTITY &&
               rotation == D3DKMDT_VPPR_IDENTITY;
    }
    return (scaling == D3DKMDT_VPPS_IDENTITY ||
            scaling == D3DKMDT_VPPS_UNPINNED ||
            scaling == D3DKMDT_VPPS_NOTSPECIFIED ||
            scaling == D3DKMDT_VPPS_UNINITIALIZED) &&
           (rotation == D3DKMDT_VPPR_IDENTITY ||
            rotation == D3DKMDT_VPPR_UNPINNED ||
            rotation == D3DKMDT_VPPR_NOTSPECIFIED ||
            rotation == D3DKMDT_VPPR_UNINITIALIZED);
}

static BOOLEAN Bc250SourceModeMatchesGop(
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    return Mode != NULL && Mode->Type == D3DKMDT_RMT_GRAPHICS &&
        Mode->Format.Graphics.PrimSurfSize.cx ==
            g_Bc250DisplayState.CurrentMode.Width &&
        Mode->Format.Graphics.PrimSurfSize.cy ==
            g_Bc250DisplayState.CurrentMode.Height &&
        Mode->Format.Graphics.VisibleRegionSize.cx ==
            g_Bc250DisplayState.CurrentMode.Width &&
        Mode->Format.Graphics.VisibleRegionSize.cy ==
            g_Bc250DisplayState.CurrentMode.Height &&
        Mode->Format.Graphics.Stride >=
            (g_Bc250DisplayState.CurrentMode.Width * 4u) &&
        (Mode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8 ||
         Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8);
}

static BOOLEAN Bc250TargetModeMatchesGop(
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    BC250_DCN_TIMING native;
    if (Mode == NULL) {
        return FALSE;
    }
    /* DCN.2 item 1 (doc 713): com o timing medido aplicado, um modo-alvo com
     * refresh conhecido e diferente (>1%) do que o OTG varre nao e' o do GOP. */
    Bc250NativeTiming(&native);
    if (native.RefreshMilliHz != 0u && Mode->VideoSignalInfo.VSyncFreq.Denominator != 0u &&
        Mode->VideoSignalInfo.VSyncFreq.Numerator != 0u) {
        ULONGLONG mhz = ((ULONGLONG)Mode->VideoSignalInfo.VSyncFreq.Numerator * 1000ULL) /
            Mode->VideoSignalInfo.VSyncFreq.Denominator;
        ULONGLONG want = native.RefreshMilliHz;
        if (mhz + want / 100ULL < want || mhz > want + want / 100ULL) {
            return FALSE;
        }
    }
    return Mode->VideoSignalInfo.ActiveSize.cx == g_Bc250DisplayState.CurrentMode.Width &&
           Mode->VideoSignalInfo.ActiveSize.cy == g_Bc250DisplayState.CurrentMode.Height &&
           (Mode->VideoSignalInfo.ScanLineOrdering == D3DDDI_VSSLO_PROGRESSIVE ||
            Mode->VideoSignalInfo.ScanLineOrdering == D3DDDI_VSSLO_UNINITIALIZED ||
            Mode->VideoSignalInfo.ScanLineOrdering == D3DDDI_VSSLO_OTHER);
}

static NTSTATUS Bc250ValidateVidPn(
    _In_ D3DKMDT_HVIDPN VidPn,
    _In_ BOOLEAN RequirePinnedModes,
    _Out_ BOOLEAN *Supported,
    _Out_opt_ BOOLEAN *HasPath)
{
    CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *sourceInterface = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *targetInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY topology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET sourceSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET targetSet = 0;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *path = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *sourceMode = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *targetMode = NULL;
    SIZE_T pathCount = 0;
    NTSTATUS status = STATUS_SUCCESS;

    if (Supported == NULL) return STATUS_INVALID_PARAMETER;
    *Supported = FALSE;
    if (HasPath != NULL) *HasPath = FALSE;
    if (VidPn == 0) {
        if (!RequirePinnedModes) *Supported = TRUE;
        Bc250DisplayLog("[VidPn] VidPn=0 -> Supported=%d\n", *Supported);
        return STATUS_SUCCESS;
    }
    if (g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        Bc250DisplayLog("[VidPn] DxgkCbQueryVidPnInterface is NULL\n");
        return STATUS_DEVICE_NOT_READY;
    }

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface(
        VidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnInterface);
    if (!NT_SUCCESS(status) || vidPnInterface == NULL) {
        Bc250DisplayLog("[VidPn] QueryVidPnInterface failed 0x%08X\n", status);
        goto Exit;
    }
    status = vidPnInterface->pfnGetTopology(
        VidPn, &topology, &topologyInterface);
    if (!NT_SUCCESS(status) || topologyInterface == NULL) {
        Bc250DisplayLog("[VidPn] GetTopology failed 0x%08X\n", status);
        goto Exit;
    }
    status = topologyInterface->pfnGetNumPaths(topology, &pathCount);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[VidPn] GetNumPaths failed 0x%08X\n", status);
        goto Exit;
    }
    if (pathCount == 0) {
        if (HasPath != NULL) *HasPath = FALSE;
        *Supported = TRUE;
        status = STATUS_SUCCESS;
        Bc250DisplayLog("[VidPn] pathCount=0 -> Supported=TRUE (empty VidPN transition)\n");
        goto Exit;
    }
    if (pathCount != 1) {
        status = STATUS_SUCCESS;
        Bc250DisplayLog("[VidPn] pathCount=%u != 1 -> Supported=FALSE\n", (ULONG)pathCount);
        goto Exit;
    }

    status = topologyInterface->pfnAcquireFirstPathInfo(topology, &path);
    if (!NT_SUCCESS(status) || path == NULL) {
        Bc250DisplayLog("[VidPn] AcquireFirstPathInfo failed 0x%08X\n", status);
        goto Exit;
    }
    Bc250DisplayLog("[VidPn] Path: Src=%u, Tgt=%u, Scale=%u, Rot=%u, ReqPinned=%d\n",
        path->VidPnSourceId, path->VidPnTargetId,
        path->ContentTransformation.Scaling,
        path->ContentTransformation.Rotation, RequirePinnedModes);

    if (!Bc250VidPnPathSupported(path, RequirePinnedModes)) {
        status = STATUS_SUCCESS;
        Bc250DisplayLog("[VidPn] Path NOT supported -> Supported=FALSE\n");
        goto Exit;
    }
    if (HasPath != NULL) *HasPath = TRUE;

    status = vidPnInterface->pfnAcquireSourceModeSet(
        VidPn, path->VidPnSourceId, &sourceSet, &sourceInterface);
    if (!NT_SUCCESS(status) || sourceInterface == NULL) {
        Bc250DisplayLog("[VidPn] AcquireSourceModeSet failed 0x%08X\n", status);
        goto Exit;
    }
    status = sourceInterface->pfnAcquirePinnedModeInfo(sourceSet, &sourceMode);
    if (status == STATUS_GRAPHICS_MODE_NOT_PINNED ||
        (NT_SUCCESS(status) && sourceMode == NULL)) {
        if (RequirePinnedModes) status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        else status = STATUS_SUCCESS;
        if (!RequirePinnedModes) *Supported = TRUE;
        Bc250DisplayLog("[VidPn] Source unpinned -> Supported=%d\n", *Supported);
        goto Exit;
    }
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[VidPn] AcquirePinnedModeInfo (src) failed 0x%08X\n", status);
        goto Exit;
    }
    Bc250DisplayLog("[VidPn] Pinned SourceMode: %ux%u, fmt=%u, stride=%u\n",
        sourceMode->Format.Graphics.VisibleRegionSize.cx,
        sourceMode->Format.Graphics.VisibleRegionSize.cy,
        sourceMode->Format.Graphics.PixelFormat,
        sourceMode->Format.Graphics.Stride);

    if (!Bc250SourceModeMatchesGop(sourceMode)) {
        status = STATUS_SUCCESS;
        Bc250DisplayLog("[VidPn] SourceMode does NOT match GOP -> Supported=FALSE\n");
        goto Exit;
    }

    status = vidPnInterface->pfnAcquireTargetModeSet(
        VidPn, path->VidPnTargetId, &targetSet, &targetInterface);
    if (!NT_SUCCESS(status) || targetInterface == NULL) {
        Bc250DisplayLog("[VidPn] AcquireTargetModeSet failed 0x%08X\n", status);
        goto Exit;
    }
    status = targetInterface->pfnAcquirePinnedModeInfo(targetSet, &targetMode);
    if (status == STATUS_GRAPHICS_MODE_NOT_PINNED ||
        (NT_SUCCESS(status) && targetMode == NULL)) {
        if (RequirePinnedModes) status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        else status = STATUS_SUCCESS;
        if (!RequirePinnedModes) *Supported = TRUE;
        Bc250DisplayLog("[VidPn] Target unpinned -> Supported=%d\n", *Supported);
        goto Exit;
    }
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[VidPn] AcquirePinnedModeInfo (tgt) failed 0x%08X\n", status);
        goto Exit;
    }
    Bc250DisplayLog("[VidPn] Pinned TargetMode: %ux%u, scanline=%u\n",
        targetMode->VideoSignalInfo.ActiveSize.cx,
        targetMode->VideoSignalInfo.ActiveSize.cy,
        targetMode->VideoSignalInfo.ScanLineOrdering);

    if (Bc250TargetModeMatchesGop(targetMode)) {
        *Supported = TRUE;
        Bc250DisplayLog("[VidPn] TargetMode matches GOP -> Supported=TRUE!\n");
    } else {
        status = STATUS_SUCCESS;
        Bc250DisplayLog("[VidPn] TargetMode does NOT match GOP -> Supported=FALSE\n");
    }

Exit:
    if (targetMode != NULL && targetInterface != NULL && targetSet != 0) {
        (void)targetInterface->pfnReleaseModeInfo(targetSet, targetMode);
    }
    if (targetSet != 0 && vidPnInterface != NULL) {
        (void)vidPnInterface->pfnReleaseTargetModeSet(VidPn, targetSet);
    }
    if (sourceMode != NULL && sourceInterface != NULL && sourceSet != 0) {
        (void)sourceInterface->pfnReleaseModeInfo(sourceSet, sourceMode);
    }
    if (sourceSet != 0 && vidPnInterface != NULL) {
        (void)vidPnInterface->pfnReleaseSourceModeSet(VidPn, sourceSet);
    }
    if (path != NULL && topologyInterface != NULL && topology != 0) {
        (void)topologyInterface->pfnReleasePathInfo(topology, path);
    }
    return status;
}

NTSTATUS APIENTRY Bc250DxgkDdiIsSupportedVidPn(
    CONST HANDLE hAdapter,
    DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn)
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(hAdapter);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_VIDPN_QUERY_CALLED, 1u);
    if (pIsSupportedVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = Bc250ValidateVidPn(
        pIsSupportedVidPn->hDesiredVidPn,
        FALSE,
        &pIsSupportedVidPn->IsVidPnSupported,
        NULL);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_IS_SUPPORTED_COUNT,
        (ULONG)InterlockedIncrement(
            &g_Bc250DisplayState.IsSupportedVidPnCount));
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_IS_SUPPORTED_STATUS, (ULONG)status);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_IS_SUPPORTED_RESULT,
        pIsSupportedVidPn->IsVidPnSupported ? 1u : 0u);
    Bc250DisplayLog("=== IsSupportedVidPn (Call #%u): Supported=%d, status=0x%08X ===\n",
        g_Bc250DisplayState.IsSupportedVidPnCount,
        pIsSupportedVidPn->IsVidPnSupported, status);
    return status;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendFunctionalVidPn(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn)
{
    CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY topology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
    SIZE_T numPaths = 0;
    D3DKMDT_HVIDPNSOURCEMODESET sourceModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *sourceModeInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *sourceMode = NULL;
    D3DKMDT_HVIDPNTARGETMODESET targetModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *targetModeInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *targetMode = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);
    if (pRecommendFunctionalVidPn == NULL ||
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn == 0 ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DisplayLog("[RecommendFunctionalVidPn] called, Reason=%u\n",
        pRecommendFunctionalVidPn->RequestReason);

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &vidPnInterface);
    if (!NT_SUCCESS(status) || vidPnInterface == NULL) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] QueryVidPnInterface failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = vidPnInterface->pfnGetTopology(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        &topology,
        &topologyInterface);
    if (!NT_SUCCESS(status) || topologyInterface == NULL) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] GetTopology failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = topologyInterface->pfnGetNumPaths(topology, &numPaths);
    if (!NT_SUCCESS(status)) return status;
    if (numPaths == 0) {
        D3DKMDT_VIDPN_PRESENT_PATH *newPath = NULL;
        status = topologyInterface->pfnCreateNewPathInfo(topology, &newPath);
        if (!NT_SUCCESS(status) || newPath == NULL) {
            Bc250DisplayLog("[RecommendFunctionalVidPn] CreateNewPathInfo failed 0x%08X\n", status);
            return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
        }
        RtlZeroMemory(newPath, sizeof(*newPath));
        newPath->VidPnSourceId = 0;
        newPath->VidPnTargetId = 1;
        newPath->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;
        newPath->Content = D3DKMDT_VPPC_NOTSPECIFIED;
        newPath->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
        newPath->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
        newPath->ContentTransformation.ScalingSupport.Identity = 1;
        newPath->ContentTransformation.RotationSupport.Identity = 1;
        newPath->ContentTransformation.RotationSupport.Offset0 = 1;

        status = topologyInterface->pfnAddPath(topology, newPath);
        if (!NT_SUCCESS(status)) {
            Bc250DisplayLog("[RecommendFunctionalVidPn] AddPath failed 0x%08X\n", status);
            (void)topologyInterface->pfnReleasePathInfo(topology, newPath);
            return status;
        }
    }

    status = vidPnInterface->pfnCreateNewSourceModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        0,
        &sourceModeSet,
        &sourceModeInterface);
    if (!NT_SUCCESS(status) || sourceModeInterface == NULL) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] CreateNewSourceModeSet failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = Bc250AddSingleSourceMode(sourceModeInterface, sourceModeSet, 0);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] AddSingleSourceMode failed 0x%08X\n", status);
        (void)vidPnInterface->pfnReleaseSourceModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, sourceModeSet);
        return status;
    }

    status = sourceModeInterface->pfnAcquireFirstModeInfo(sourceModeSet, &sourceMode);
    if (NT_SUCCESS(status) && sourceMode != NULL) {
        (void)sourceModeInterface->pfnPinMode(sourceModeSet, sourceMode->Id);
        (void)sourceModeInterface->pfnReleaseModeInfo(sourceModeSet, sourceMode);
    }

    status = vidPnInterface->pfnAssignSourceModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        0,
        sourceModeSet);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] AssignSourceModeSet failed 0x%08X\n", status);
        (void)vidPnInterface->pfnReleaseSourceModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, sourceModeSet);
        return status;
    }

    status = vidPnInterface->pfnCreateNewTargetModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        1,
        &targetModeSet,
        &targetModeInterface);
    if (!NT_SUCCESS(status) || targetModeInterface == NULL) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] CreateNewTargetModeSet failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = Bc250AddSingleTargetMode(targetModeInterface, targetModeSet, 1);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] AddSingleTargetMode failed 0x%08X\n", status);
        (void)vidPnInterface->pfnReleaseTargetModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, targetModeSet);
        return status;
    }

    status = targetModeInterface->pfnAcquireFirstModeInfo(targetModeSet, &targetMode);
    if (NT_SUCCESS(status) && targetMode != NULL) {
        (void)targetModeInterface->pfnPinMode(targetModeSet, targetMode->Id);
        (void)targetModeInterface->pfnReleaseModeInfo(targetModeSet, targetMode);
    }

    status = vidPnInterface->pfnAssignTargetModeSet(
        pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn,
        1,
        targetModeSet);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[RecommendFunctionalVidPn] AssignTargetModeSet failed 0x%08X\n", status);
        (void)vidPnInterface->pfnReleaseTargetModeSet(
            pRecommendFunctionalVidPn->hRecommendedFunctionalVidPn, targetModeSet);
        return status;
    }

    Bc250DisplayLog("=== RecommendFunctionalVidPn SUCCESS: 2560x1080@60Hz pinned ===\n");
    return STATUS_SUCCESS;
}

static BOOLEAN Bc250CofuncPivotMatchesPath(
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *Arguments,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_ D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE PivotType)
{
    if (Arguments == NULL || Path == NULL ||
        Arguments->EnumPivotType != PivotType) {
        return FALSE;
    }
    if (PivotType == D3DKMDT_EPT_VIDPNSOURCE) {
        return Arguments->EnumPivot.VidPnSourceId == Path->VidPnSourceId;
    }
    if (PivotType == D3DKMDT_EPT_VIDPNTARGET) {
        return Arguments->EnumPivot.VidPnTargetId == Path->VidPnTargetId;
    }
    return Arguments->EnumPivot.VidPnSourceId == Path->VidPnSourceId &&
           Arguments->EnumPivot.VidPnTargetId == Path->VidPnTargetId;
}

static NTSTATUS Bc250PublishCofuncSourceMode(
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DKMDT_HVIDPN VidPn)
{
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *modeInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *pinnedMode = NULL;
    D3DKMDT_HVIDPNSOURCEMODESET modeSet = 0;
    NTSTATUS status;

    status = VidPnInterface->pfnAcquireSourceModeSet(
        VidPn, 0, &modeSet, &modeInterface);
    if (NT_SUCCESS(status) && modeInterface != NULL && modeSet != 0) {
        CONST D3DKMDT_VIDPN_SOURCE_MODE *firstMode = NULL;
        status = modeInterface->pfnAcquirePinnedModeInfo(modeSet, &pinnedMode);
        if (status == STATUS_GRAPHICS_MODE_NOT_PINNED) {
            pinnedMode = NULL;
        } else if (NT_SUCCESS(status) && pinnedMode != NULL) {
            status = Bc250SourceModeMatchesGop(pinnedMode) ?
                STATUS_SUCCESS : STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
            (void)modeInterface->pfnReleaseModeInfo(modeSet, pinnedMode);
            (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
            return status;
        } else if (!NT_SUCCESS(status)) {
            (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
            Bc250DisplayLog("[CofuncSourceMode] AcquirePinnedModeInfo failed 0x%08X\n", status);
            return status;
        }

        /* Se o ModeSet já contiver o modo unpinned, mantemos sem reatribuir para convergir a DMM */
        status = modeInterface->pfnAcquireFirstModeInfo(modeSet, &firstMode);
        if (NT_SUCCESS(status) && firstMode != NULL) {
            (void)modeInterface->pfnReleaseModeInfo(modeSet, firstMode);
            (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
            Bc250DisplayLog("[CofuncSourceMode] ModeSet already populated -> SUCCESS\n");
            return STATUS_SUCCESS;
        }

        (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
        modeSet = 0;
        modeInterface = NULL;
    }

    status = VidPnInterface->pfnCreateNewSourceModeSet(
        VidPn, 0, &modeSet, &modeInterface);
    if (!NT_SUCCESS(status) || modeInterface == NULL || modeSet == 0) {
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }
    status = Bc250AddSingleSourceMode(modeInterface, modeSet, 0);
    if (!NT_SUCCESS(status)) {
        (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
        return status;
    }
    status = VidPnInterface->pfnAssignSourceModeSet(VidPn, 0, modeSet);
    if (!NT_SUCCESS(status)) {
        (void)VidPnInterface->pfnReleaseSourceModeSet(VidPn, modeSet);
        return status;
    }
    /* Successful assignment transfers ownership to the VidPN manager. */
    modeSet = 0;
    Bc250DisplayLog("[CofuncSourceMode] Created & Assigned new SourceModeSet -> SUCCESS\n");
    return STATUS_SUCCESS;
}

static NTSTATUS Bc250PublishCofuncTargetMode(
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DKMDT_HVIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *modeInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *pinnedMode = NULL;
    D3DKMDT_HVIDPNTARGETMODESET modeSet = 0;
    NTSTATUS status;

    status = VidPnInterface->pfnAcquireTargetModeSet(
        VidPn, TargetId, &modeSet, &modeInterface);
    if (NT_SUCCESS(status) && modeInterface != NULL && modeSet != 0) {
        CONST D3DKMDT_VIDPN_TARGET_MODE *firstMode = NULL;
        status = modeInterface->pfnAcquirePinnedModeInfo(modeSet, &pinnedMode);
        if (status == STATUS_GRAPHICS_MODE_NOT_PINNED) {
            pinnedMode = NULL;
        } else if (NT_SUCCESS(status) && pinnedMode != NULL) {
            status = Bc250TargetModeMatchesGop(pinnedMode) ?
                STATUS_SUCCESS : STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
            (void)modeInterface->pfnReleaseModeInfo(modeSet, pinnedMode);
            (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
            return status;
        } else if (!NT_SUCCESS(status)) {
            (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
            Bc250DisplayLog("[CofuncTargetMode] AcquirePinnedModeInfo failed 0x%08X\n", status);
            return status;
        }

        /* Se o ModeSet já contiver o modo unpinned, mantemos sem reatribuir para convergir a DMM */
        status = modeInterface->pfnAcquireFirstModeInfo(modeSet, &firstMode);
        if (NT_SUCCESS(status) && firstMode != NULL) {
            (void)modeInterface->pfnReleaseModeInfo(modeSet, firstMode);
            (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
            Bc250DisplayLog("[CofuncTargetMode] ModeSet already populated -> SUCCESS\n");
            return STATUS_SUCCESS;
        }

        (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
        modeSet = 0;
        modeInterface = NULL;
    }

    status = VidPnInterface->pfnCreateNewTargetModeSet(
        VidPn, TargetId, &modeSet, &modeInterface);
    if (!NT_SUCCESS(status) || modeInterface == NULL || modeSet == 0) {
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }
    status = Bc250AddSingleTargetMode(modeInterface, modeSet, TargetId);
    if (!NT_SUCCESS(status)) {
        (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
        return status;
    }
    status = VidPnInterface->pfnAssignTargetModeSet(VidPn, TargetId, modeSet);
    if (!NT_SUCCESS(status)) {
        (void)VidPnInterface->pfnReleaseTargetModeSet(VidPn, modeSet);
        return status;
    }
    /* Successful assignment transfers ownership to the VidPN manager. */
    modeSet = 0;

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiEnumVidPnCofuncModality(
    CONST HANDLE hAdapter,
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality)
{
    CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *path = NULL;
    D3DKMDT_VIDPN_PRESENT_PATH supportedPath;
    D3DKMDT_HVIDPNTOPOLOGY topology = 0;
    SIZE_T pathCount = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);
    Bc250DisplayLog("=== EnumVidPnCofuncModality (PivotType=%u, PivotSrc=%u, PivotTgt=%u) ===\n",
        pEnumCofuncModality ? pEnumCofuncModality->EnumPivotType : 999,
        pEnumCofuncModality ? pEnumCofuncModality->EnumPivot.VidPnSourceId : 999,
        pEnumCofuncModality ? pEnumCofuncModality->EnumPivot.VidPnTargetId : 999);

    if (pEnumCofuncModality == NULL ||
        pEnumCofuncModality->hConstrainingVidPn == 0 ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface == NULL ||
        pEnumCofuncModality->EnumPivotType < D3DKMDT_EPT_VIDPNSOURCE ||
        pEnumCofuncModality->EnumPivotType > D3DKMDT_EPT_NOPIVOT) {
        return STATUS_INVALID_PARAMETER;
    }

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface(
        pEnumCofuncModality->hConstrainingVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &vidPnInterface);
    if (!NT_SUCCESS(status)) return status;
    if (vidPnInterface == NULL) return STATUS_DEVICE_CONFIGURATION_ERROR;

    status = vidPnInterface->pfnGetTopology(
        pEnumCofuncModality->hConstrainingVidPn,
        &topology,
        &topologyInterface);
    if (!NT_SUCCESS(status)) return status;
    if (topologyInterface == NULL || topology == 0) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    status = topologyInterface->pfnGetNumPaths(topology, &pathCount);
    if (!NT_SUCCESS(status)) return status;
    if (pathCount == 0) {
        D3DKMDT_VIDPN_PRESENT_PATH *newPath = NULL;
        status = topologyInterface->pfnCreateNewPathInfo(topology, &newPath);
        if (NT_SUCCESS(status) && newPath != NULL) {
            RtlZeroMemory(newPath, sizeof(*newPath));
            newPath->VidPnSourceId = 0;
            newPath->VidPnTargetId = 1;
            newPath->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;
            newPath->Content = D3DKMDT_VPPC_NOTSPECIFIED;
            newPath->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
            newPath->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
            newPath->ContentTransformation.ScalingSupport.Identity = 1;
            newPath->ContentTransformation.RotationSupport.Identity = 1;
            newPath->ContentTransformation.RotationSupport.Offset0 = 1;

            status = topologyInterface->pfnAddPath(topology, newPath);
            if (!NT_SUCCESS(status)) {
                Bc250DisplayLog("[EnumVidPnCofuncModality] AddPath failed 0x%08X\n", status);
                (void)topologyInterface->pfnReleasePathInfo(topology, newPath);
                return status;
            }
            pathCount = 1;
            Bc250DisplayLog("=== EnumVidPnCofuncModality: Added Path 0->1 to empty topology ===\n");
        } else {
            return STATUS_SUCCESS;
        }
    }
    if (pathCount != 1) return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;

    status = topologyInterface->pfnAcquireFirstPathInfo(topology, &path);
    if (!NT_SUCCESS(status)) return status;
    if (path == NULL) return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!Bc250VidPnPathSupported(path, FALSE)) {
        status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        goto Exit;
    }

    supportedPath = *path;

    /*
     * Display v1 por heranca de GOP: a BC-250 so suporta Identity tanto para
     * scaling quanto para rotacao (sem DCN ativo no caminho critico).
     *
     * HOTFIX BUGCHECK 0x113 (0x1C):
     * A estrutura path recebida da DMM pode conter lixo ou bits de offsets
     * independentes (Offset90/180/270 = bits 0xE0). Se esses bits chegarem a
     * UpdatePathSupportInfo em caminho unico nao-clone, o dxgkrnl dispara
     * VIDEO_DXGKRNL_FATAL_ERROR (0x113, subcode 0x1C).
     *
     * A DMM, entretanto, proibe alterar o campo que corresponde ao pivo atual.
     * Sanitizamos cada campo somente quando ele nao e' o pivo desta chamada.
     * Assim preservamos o hotfix do 0x113 em campos mutaveis sem violar o
     * contrato de EnumVidPnCofuncModality.
     */
    if (!Bc250CofuncPivotMatchesPath(
            pEnumCofuncModality, path, D3DKMDT_EPT_SCALING)) {
        RtlZeroMemory(&supportedPath.ContentTransformation.ScalingSupport,
            sizeof(supportedPath.ContentTransformation.ScalingSupport));
        supportedPath.ContentTransformation.ScalingSupport.Identity = 1;
    }
    if (!Bc250CofuncPivotMatchesPath(
            pEnumCofuncModality, path, D3DKMDT_EPT_ROTATION)) {
        RtlZeroMemory(&supportedPath.ContentTransformation.RotationSupport,
            sizeof(supportedPath.ContentTransformation.RotationSupport));
        supportedPath.ContentTransformation.RotationSupport.Identity = 1;
        supportedPath.ContentTransformation.RotationSupport.Offset0 = 1;
    }
    status = topologyInterface->pfnUpdatePathSupportInfo(
        topology, &supportedPath);
    if (!NT_SUCCESS(status)) goto Exit;

    if (!Bc250CofuncPivotMatchesPath(
            pEnumCofuncModality, path, D3DKMDT_EPT_VIDPNSOURCE)) {
        status = Bc250PublishCofuncSourceMode(
            vidPnInterface, pEnumCofuncModality->hConstrainingVidPn);
        if (!NT_SUCCESS(status)) goto Exit;
    }
    if (!Bc250CofuncPivotMatchesPath(
            pEnumCofuncModality, path, D3DKMDT_EPT_VIDPNTARGET)) {
        status = Bc250PublishCofuncTargetMode(
            vidPnInterface, pEnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId);
    }

Exit:
    (void)topologyInterface->pfnReleasePathInfo(topology, path);
    Bc250DisplayLog("  EnumVidPnCofuncModality status=0x%08X\n", status);
    return status;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddress(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pSetVidPnSourceAddress->VidPnSourceId != 0) {
        return STATUS_NOT_SUPPORTED;
    }
    Bc250DisplayLog("=== SetVidPnSourceAddress (SourceId=%u, Segment=%u, Addr=0x%I64X, hAlloc=%p, ContextCount=%u, Flags=0x%08X) ===\n",
        pSetVidPnSourceAddress->VidPnSourceId,
        pSetVidPnSourceAddress->PrimarySegment,
        pSetVidPnSourceAddress->PrimaryAddress.QuadPart,
        pSetVidPnSourceAddress->hAllocation,
        pSetVidPnSourceAddress->ContextCount,
        *(CONST ULONG *)&pSetVidPnSourceAddress->Flags);

    if (pSetVidPnSourceAddress->PrimarySegment == 1u) {
        NTSTATUS copyStatus = Bc250DisplaySwitchPrimary("SET_ADDRESS",
            pSetVidPnSourceAddress->PrimarySegment,
            pSetVidPnSourceAddress->PrimaryAddress,
            (pSetVidPnSourceAddress->Flags.ModeChange != 0u &&
             pSetVidPnSourceAddress->Flags.FlipImmediate == 0u &&
             pSetVidPnSourceAddress->Flags.FlipOnNextVSync == 0u) ? TRUE : FALSE,
            pSetVidPnSourceAddress->Flags.FlipImmediate != 0u ? TRUE : FALSE);
        if (!NT_SUCCESS(copyStatus)) return copyStatus;
    } else if (pSetVidPnSourceAddress->PrimarySegment != 0u) {
        Bc250DisplayLog("[PRIMARY_COPY] REJECT Route=SET_ADDRESS Seg=%u Status=NOT_SUPPORTED\n",
            pSetVidPnSourceAddress->PrimarySegment);
        return STATUS_NOT_SUPPORTED;
    }
    g_Bc250DisplayState.PrimaryAddress = pSetVidPnSourceAddress->PrimaryAddress;
    g_Bc250DisplayState.PrimarySegment = pSetVidPnSourceAddress->PrimarySegment;
    g_Bc250DisplayState.hPrimarySourceAllocation = pSetVidPnSourceAddress->hAllocation;
    g_Bc250DisplayState.PrimaryAddressValid =
        (pSetVidPnSourceAddress->PrimarySegment == 1u) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY pSetVidPnSourceAddressWithMultiPlaneOverlay)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId != 0) {
        return STATUS_NOT_SUPPORTED;
    }

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount == 0) {
        Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay (PlaneCount=0, MPO disabled, SourceId=%u) ===\n",
            pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId);
        return STATUS_SUCCESS;
    }

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->pPlanes != NULL) {
        CONST DXGK_MULTIPLANE_OVERLAY_PLANE *pPlane0 = &pSetVidPnSourceAddressWithMultiPlaneOverlay->pPlanes[0];
        if (pPlane0->AllocationSegment != 0) {
            Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay (SourceId=%u, Seg=%u, Addr=0x%I64X, hAlloc=%p, PlaneCount=%u) ===\n",
                pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId,
                pPlane0->AllocationSegment,
                pPlane0->AllocationAddress.QuadPart,
                pPlane0->hAllocation,
                pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount);
            if (pPlane0->AllocationSegment == 1u) {
                NTSTATUS copyStatus = Bc250DisplaySwitchPrimary("MPO1",
                    pPlane0->AllocationSegment,
                    pPlane0->AllocationAddress, FALSE, FALSE);
                if (!NT_SUCCESS(copyStatus)) return copyStatus;
            } else {
                Bc250DisplayLog("[PRIMARY_COPY] REJECT Route=MPO1 Seg=%u Status=NOT_SUPPORTED\n",
                    pPlane0->AllocationSegment);
                return STATUS_NOT_SUPPORTED;
            }
            g_Bc250DisplayState.PrimaryAddress = pPlane0->AllocationAddress;
            g_Bc250DisplayState.PrimarySegment = pPlane0->AllocationSegment;
            g_Bc250DisplayState.hPrimarySourceAllocation = pPlane0->hAllocation;
            g_Bc250DisplayState.PrimaryAddressValid = TRUE;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCheckMultiPlaneOverlaySupport(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT pCheckMultiPlaneOverlaySupport)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pCheckMultiPlaneOverlaySupport == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pCheckMultiPlaneOverlaySupport->ReturnInfo.Value = 0;
    pCheckMultiPlaneOverlaySupport->Supported = FALSE;
    Bc250DisplayLog("=== CheckMultiPlaneOverlaySupport (Planes=%u, Supported=0 [DISABLED]) ===\n",
        pCheckMultiPlaneOverlaySupport->PlaneCount);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY2 pSetVidPnSourceAddressWithMultiPlaneOverlay)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId != 0) {
        return STATUS_NOT_SUPPORTED;
    }

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount == 0) {
        Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay2 (PlaneCount=0, MPO disabled, SourceId=%u) ===\n",
            pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId);
        return STATUS_SUCCESS;
    }

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->pPlanes != NULL) {
        CONST DXGK_MULTIPLANE_OVERLAY_PLANE2 *pPlane0 = &pSetVidPnSourceAddressWithMultiPlaneOverlay->pPlanes[0];
        if (pPlane0->AllocationSegment != 0) {
            g_Bc250DisplayState.PrimaryAddress = pPlane0->AllocationAddress;
            g_Bc250DisplayState.PrimarySegment = pPlane0->AllocationSegment;
            g_Bc250DisplayState.hPrimarySourceAllocation = pPlane0->hAllocation;
            g_Bc250DisplayState.PrimaryAddressValid = TRUE;
            Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay2 (SourceId=%u, Seg=%u, Addr=0x%I64X, hAlloc=%p, PlaneCount=%u) ===\n",
                pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId,
                pPlane0->AllocationSegment,
                pPlane0->AllocationAddress.QuadPart,
                pPlane0->hAllocation,
                pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCheckMultiPlaneOverlaySupport2(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT2 pCheckMultiPlaneOverlaySupport)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pCheckMultiPlaneOverlaySupport == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pCheckMultiPlaneOverlaySupport->ReturnInfo.Value = 0;
    pCheckMultiPlaneOverlaySupport->Supported = FALSE;
    Bc250DisplayLog("=== CheckMultiPlaneOverlaySupport2 (Planes=%u, Supported=0 [DISABLED]) ===\n",
        pCheckMultiPlaneOverlaySupport->PlaneCount);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 pSetVidPnSourceAddressWithMultiPlaneOverlay)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId != 0) {
        return STATUS_NOT_SUPPORTED;
    }

    pSetVidPnSourceAddressWithMultiPlaneOverlay->OutputFlags.PrePresentNeeded = 0;

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount == 0) {
        Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay3 (PlaneCount=0, MPO disabled, SourceId=%u) ===\n",
            pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId);
        return STATUS_SUCCESS;
    }

    if (pSetVidPnSourceAddressWithMultiPlaneOverlay->ppPlanes != NULL &&
        pSetVidPnSourceAddressWithMultiPlaneOverlay->ppPlanes[0] != NULL) {
        DXGK_MULTIPLANE_OVERLAY_PLANE3 *pPlane0 = pSetVidPnSourceAddressWithMultiPlaneOverlay->ppPlanes[0];
        pPlane0->OutputFlags.Value = 0;
        if (pPlane0->ContextCount > 0 && pPlane0->ppContextData != NULL && pPlane0->ppContextData[0] != NULL) {
            DXGK_PRIMARYCONTEXTDATA *pCtx = pPlane0->ppContextData[0];
            if (pCtx->SegmentId != 0) {
                Bc250DisplayLog("=== SetVidPnSourceAddressWithMultiPlaneOverlay3 (SourceId=%u, Seg=%u, Addr=0x%I64X, hAlloc=%p, PlaneCount=%u) ===\n",
                    pSetVidPnSourceAddressWithMultiPlaneOverlay->VidPnSourceId,
                    (UINT)pCtx->SegmentId,
                    pCtx->SegmentAddress.QuadPart,
                    pCtx->hAllocation,
                    pSetVidPnSourceAddressWithMultiPlaneOverlay->PlaneCount);
                if (pCtx->SegmentId == 1u) {
                    NTSTATUS copyStatus = Bc250DisplaySwitchPrimary("MPO3",
                        (UINT32)pCtx->SegmentId,
                        pCtx->SegmentAddress, FALSE, FALSE);
                    if (!NT_SUCCESS(copyStatus)) {
                        /* MPO3 documenta INVALID_PARAMETER para flip que nao
                         * pode ser processado. O motivo preciso fica no log. */
                        Bc250DisplayLog("[PRIMARY_COPY] MPO3 FAIL_CLOSED InternalStatus=0x%08X ReturnStatus=INVALID_PARAMETER\n",
                            (ULONG)copyStatus);
                        return STATUS_INVALID_PARAMETER;
                    }
                } else {
                    Bc250DisplayLog("[PRIMARY_COPY] REJECT Route=MPO3 Seg=%u Status=NOT_SUPPORTED\n",
                        (UINT)pCtx->SegmentId);
                    return STATUS_NOT_SUPPORTED;
                }
                g_Bc250DisplayState.PrimaryAddress = pCtx->SegmentAddress;
                g_Bc250DisplayState.PrimarySegment = (UINT)pCtx->SegmentId;
                g_Bc250DisplayState.hPrimarySourceAllocation = pCtx->hAllocation;
                g_Bc250DisplayState.PrimaryAddressValid = TRUE;
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCheckMultiPlaneOverlaySupport3(
    IN_CONST_HANDLE hAdapter,
    IN_OUT_PDXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 pCheckMultiPlaneOverlaySupport)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pCheckMultiPlaneOverlaySupport == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    pCheckMultiPlaneOverlaySupport->ReturnInfo.Value = 0;
    pCheckMultiPlaneOverlaySupport->Supported = FALSE;
    Bc250DisplayLog("=== CheckMultiPlaneOverlaySupport3 (Planes=%u, Supported=0 [DISABLED]) ===\n",
        pCheckMultiPlaneOverlaySupport->PlaneCount);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiPostMultiPlaneOverlayPresent(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_POSTMULTIPLANEOVERLAYPRESENT pPostPresent)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pPostPresent == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("=== PostMultiPlaneOverlayPresent (TargetId=%u, Layer=%u, PresentId=%I64u) ===\n",
        pPostPresent->VidPnTargetId,
        pPostPresent->LayerIndex,
        pPostPresent->PresentID);
    return STATUS_SUCCESS;
}
#endif

NTSTATUS APIENTRY Bc250DxgkDdiSetVidPnSourceVisibility(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetVidPnSourceVisibility == NULL ||
        (pSetVidPnSourceVisibility->VidPnSourceId != 0 &&
         pSetVidPnSourceVisibility->VidPnSourceId != D3DDDI_ID_ALL)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pSetVidPnSourceVisibility->Visible) {
        InterlockedExchange(&g_Bc250DisplayState.SourceVisible, 1);
        InterlockedExchange(&g_Bc250DisplayState.ForceFullPresent, 1);
    } else {
        Bc250DisplayLog("=== SetVidPnSourceVisibility (Visible=0 recebido, scanout GOP preservado) ===\n");
    }
    Bc250DisplayLog("=== SetVidPnSourceVisibility (Visible=%d) ===\n",
        pSetVidPnSourceVisibility->Visible);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiCommitVidPn(
    CONST HANDLE hAdapter,
    CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    BOOLEAN supported = FALSE;
    BOOLEAN hasPath = FALSE;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(hAdapter);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_COMMIT_COUNT,
        (ULONG)InterlockedIncrement(&g_Bc250DisplayState.CommitVidPnCount));
    Bc250DisplayLog("=== CommitVidPn Entered: AffectedSource=%u, Started=%d, Flags=0x%08X (PowerOff=%u, PowerTrans=%u), hAlloc=%p ===\n",
        pCommitVidPn ? pCommitVidPn->AffectedVidPnSourceId : 999,
        g_Bc250DisplayState.DisplayStarted,
        pCommitVidPn ? *(ULONG *)&pCommitVidPn->Flags : 0,
        pCommitVidPn ? pCommitVidPn->Flags.PathPoweredOff : 0,
        pCommitVidPn ? pCommitVidPn->Flags.PathPowerTransition : 0,
        pCommitVidPn ? pCommitVidPn->hPrimaryAllocation : NULL);
    if (pCommitVidPn == NULL ||
        g_Bc250DisplayState.DisplayStarted == 0) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS,
            (ULONG)STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (pCommitVidPn->AffectedVidPnSourceId != 0 &&
        pCommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS,
            (ULONG)STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    if (pCommitVidPn->Flags.PathPoweredOff || pCommitVidPn->Flags.PathPowerTransition) {
        InterlockedExchange(&g_Bc250DisplayState.SourceVisible, 0);
        InterlockedExchange(&g_Bc250DisplayState.VidPnCommitted, 0);
        Bc250DisplayLog("=== CommitVidPn: empty VidPN power transition (PowerOff=%u, PowerTrans=%u) -> SUCCESS ===\n",
            pCommitVidPn->Flags.PathPoweredOff,
            pCommitVidPn->Flags.PathPowerTransition);
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS, (ULONG)STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    status = Bc250ValidateVidPn(
        pCommitVidPn->hFunctionalVidPn,
        TRUE,
        &supported,
        &hasPath);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_COMMIT_HAS_PATH, hasPath ? 1u : 0u);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_COMMIT_SUPPORTED, supported ? 1u : 0u);
    if (!NT_SUCCESS(status)) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS, (ULONG)status);
        return status;
    }
    if (!hasPath) {
        InterlockedExchange(&g_Bc250DisplayState.VidPnCommitted, 0);
        Bc250DisplayLog("=== CommitVidPn: empty VidPN transition -> SUCCESS (preserving GOP scanout) ===\n");
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS, (ULONG)STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }
    if (!supported) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_COMMIT_STATUS,
            (ULONG)STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED);
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    InterlockedExchange(&g_Bc250DisplayState.VidPnCommitted, 1);
    InterlockedExchange(&g_Bc250DisplayState.ForceFullPresent, 1);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_COMMIT_STATUS, (ULONG)STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE hAdapter,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pUpdateActiveVidPnPresentPath == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DisplayState.DisplayStarted == 0 ||
        g_Bc250DisplayState.VidPnCommitted == 0) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (!Bc250VidPnPathSupported(
            &pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo, TRUE)) {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendMonitorModes(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes)
{
    D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);

    if (pRecommendMonitorModes == NULL ||
        pRecommendMonitorModes->pMonitorSourceModeSetInterface == NULL ||
        pRecommendMonitorModes->hMonitorSourceModeSet == 0) {
        Bc250DisplayLog("[RecommendMonitorModes] INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DisplayLog("=== DxgkDdiRecommendMonitorModes: TargetId=%u, hSet=%p ===\n",
        pRecommendMonitorModes->VideoPresentTargetId,
        pRecommendMonitorModes->hMonitorSourceModeSet);

    status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        &pMonitorSourceMode);
    if (!NT_SUCCESS(status) || pMonitorSourceMode == NULL) {
        Bc250DisplayLog("[RecommendMonitorModes] CreateNewModeInfo failed 0x%08X\n", status);
        return status;
    }

    Bc250FillNativeSignalInfo(&pMonitorSourceMode->VideoSignalInfo);
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;

    status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        pMonitorSourceMode);
    if (!NT_SUCCESS(status)) {
        Bc250DisplayLog("[RecommendMonitorModes] AddMode failed 0x%08X\n", status);
        (void)pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(
            pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        return status;
    }

    Bc250DisplayLog("=== DxgkDdiRecommendMonitorModes SUCCESS: Added %ux%u @ %u/%u Hz to Target %u ===\n",
        g_Bc250DisplayState.CurrentMode.Width,
        g_Bc250DisplayState.CurrentMode.Height,
        g_Bc250DisplayState.CurrentMode.RefreshRateNumerator,
        g_Bc250DisplayState.CurrentMode.RefreshRateDenominator,
        pRecommendMonitorModes->VideoPresentTargetId);

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiRecommendVidPnTopology(
    CONST HANDLE hAdapter,
    CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *pRecommendVidPnTopology)
{
    CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY topology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
    D3DKMDT_VIDPN_PRESENT_PATH *newPath = NULL;
    SIZE_T numPaths = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(hAdapter);
    if (pRecommendVidPnTopology == NULL ||
        pRecommendVidPnTopology->hVidPn == 0 ||
        g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DisplayLog("[RecommendVidPnTopology] called, Reason=%u\n",
        pRecommendVidPnTopology->RequestReason);

    status = g_Bc250DisplayState.DxgkInterface.DxgkCbQueryVidPnInterface(
        pRecommendVidPnTopology->hVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &vidPnInterface);
    if (!NT_SUCCESS(status) || vidPnInterface == NULL) {
        Bc250DisplayLog("[RecommendVidPnTopology] QueryVidPnInterface failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = vidPnInterface->pfnGetTopology(
        pRecommendVidPnTopology->hVidPn,
        &topology,
        &topologyInterface);
    if (!NT_SUCCESS(status) || topologyInterface == NULL) {
        Bc250DisplayLog("[RecommendVidPnTopology] GetTopology failed 0x%08X\n", status);
        return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR : status;
    }

    status = topologyInterface->pfnGetNumPaths(topology, &numPaths);
    if (NT_SUCCESS(status) && numPaths == 0) {
        status = topologyInterface->pfnCreateNewPathInfo(topology, &newPath);
        if (NT_SUCCESS(status) && newPath != NULL) {
            RtlZeroMemory(newPath, sizeof(*newPath));
            newPath->VidPnSourceId = 0;
            newPath->VidPnTargetId = 1;
            newPath->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;
            newPath->Content = D3DKMDT_VPPC_NOTSPECIFIED;
            newPath->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
            newPath->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
            newPath->ContentTransformation.ScalingSupport.Identity = 1;
            newPath->ContentTransformation.RotationSupport.Identity = 1;
            newPath->ContentTransformation.RotationSupport.Offset0 = 1;

            status = topologyInterface->pfnAddPath(topology, newPath);
            if (!NT_SUCCESS(status)) {
                Bc250DisplayLog("[RecommendVidPnTopology] AddPath failed 0x%08X\n", status);
                (void)topologyInterface->pfnReleasePathInfo(topology, newPath);
                return status;
            }
            Bc250DisplayLog("=== RecommendVidPnTopology SUCCESS: Path 0->1 added ===\n");
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiPresent(
    CONST HANDLE hContext,
    DXGKARG_PRESENT *pPresent)
{
    static LONG s_PresentCount = 0;
    static LONG s_PresentByteProofState = 0;
    LONG count;
    NTSTATUS presentStatus = STATUS_NOT_SUPPORTED;
    LARGE_INTEGER presentTimerFrequency;
    LARGE_INTEGER presentTimerStart = KeQueryPerformanceCounter(
        &presentTimerFrequency);

    /*
     * I2 (13/09) -- ESTA DDI TINHA TRES SAIDAS MUDAS, E POR ISSO "ZERO LINHAS DE
     * PRESENT" ERA AMBIGUO POR CONSTRUCAO.
     *
     * Na corrida 16:28:27 nao houve UMA linha [Present #N] em 5,4 MB de log,
     * enquanto o dwm.exe emitia 90 Present pela UMD. Com as guardas mudas nao dava
     * para separar tres hipoteses: (a) o dxgkrnl nunca chamou, (b) chamou com
     * hContext NULL e devolvemos NOT_SUPPORTED calados, (c) chamou mas
     * FrameBufferVirtual era NULL e o corpo inteiro foi pulado sem log.
     *
     * A entrada agora e' INCONDICIONAL e anterior a qualquer guarda, entao a
     * ausencia da linha passa a ser conclusiva: significa que a DDI nao foi
     * chamada. Amostrada por contador PROPRIO (8 primeiras + 1 em 256) para nao
     * afogar o log a 60 Hz -- o teto global nao alcanca esta classe.
     */
    {
        static volatile LONG s_PresentEnterOrdinal = 0;
        LONG enterOrdinal = InterlockedIncrement(&s_PresentEnterOrdinal);
        if (enterOrdinal <= 8L || (enterOrdinal % 256L) == 0L) {
            Bc250DisplayLog("[DdiPresent] ENTER #%ld: hContext=%p pPresent=%p FrameBufferVirtual=%p\n",
                enterOrdinal, hContext, (PVOID)pPresent,
                g_Bc250DisplayState.FrameBufferVirtual);
        }
    }

    if (hContext == NULL) {
        Bc250DisplayLog("[DdiPresent] RECUSADO: hContext NULL -> STATUS_NOT_SUPPORTED\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (pPresent == NULL) {
        Bc250DisplayLog("[DdiPresent] RECUSADO: pPresent NULL -> STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }
    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
        Bc250DisplayLog("[DdiPresent] RECUSADO: FrameBufferVirtual NULL -- scanout do GOP nao mapeado, "
            "o corpo de apresentacao inteiro seria pulado sem log\n");
    }

    /*
     * Liveness do SDMA a 60 Hz: drena submissoes de paginacao/transferencia concluidas
     * no silicio para garantir que o dxgkrnl e o scheduler recebam os ACKs sem retardo.
     */
    Bc250ActiveSdmaRetirePendingOnAllAdapters(FALSE);

    count = InterlockedIncrement(&s_PresentCount);

    /*
     * Apresentacao no scanout do UEFI GOP herdado (Fase P5 do Roadmap).
     * O monitor esta fisicamente conectado no scanout GOP (0xC0000000, offset 0 em VRAM).
     */
    if (g_Bc250DisplayState.FrameBufferVirtual != NULL) {
        ULONG numSrc = pPresent->NumSrcAllocations;
        ULONG numDst = pPresent->NumDstAllocations;
        ULONG allocLimit = DXGK_PRESENT_MAX_INDEX + 1u;
        uint64_t srcVa = 0;
        uint64_t srcPhysical = 0;
        ULONG scanoutBytes = g_Bc250DisplayState.CurrentMode.Pitch *
                             g_Bc250DisplayState.CurrentMode.Height;
        BOOLEAN isBlt = (pPresent->Flags.Blt != 0);
        BOOLEAN isFlip = (pPresent->Flags.Flip != 0);

        if (pPresent->pAllocationInfo != NULL) {
            srcVa = (uint64_t)
                pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].AllocationVirtualAddress;
        }

        /*
         * Identificacao da superficie de origem.
         *
         * Ordem de autoridade (corrida 09:04 mostrou por que):
         *
        /*
         * Resolucao da superficie de origem para Blt e Flip:
         * 1. pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX] com PhysicalAddress/SegmentId
         *    (Slot 1 / DWM sob WDDM 2.0+).  E' onde o Windows realmente compoe
         *    a imagem da tela (ex: GPUVA 0xAC0000 -> VRAM 0xC10000).
         *    Possui autoridade PRIMEIRA quando resolvida com sucesso.
         *
         * 2. Em apresentacoes de Flip (Flags.Flip != 0), caso o Slot 1 nao resolva
         *    mas SetVidPnSourceAddress tenha comutado uma primaria valida no Segmento 1,
         *    o buffer da primaria e' a origem autoritativa da exibicao.
         *
         * Sem Slot 1 resolvido nem primaria autoritativa, Present falha fechado.
         */
        UINT32 srcSegmentId = 0;
        BOOLEAN vaResolved = FALSE;
        uint64_t presentPhysical = 0;
        UINT32 presentSegmentId = 0;
        BOOLEAN presentResolved;

        presentResolved = Bc250DisplayResolvePresentSource(
            hContext, pPresent, &presentPhysical, &presentSegmentId);
        if (presentResolved && presentSegmentId == 1u) {
            /*
             * C3.6 (12/09) -- DEFEITO CORRIGIDO, medido na corrida 13:11.
             *
             * Bc250DisplayResolvePresentSource devolve um OFFSET DE SEGMENTO, e
             * este caminho o usava DIRETO como indice de
             * g_Bc250DisplayState.FrameBufferVirtual -- que e' um mapeamento de
             * CPU comecando no byte ZERO da VRAM. O segmento 1 comeca em
             * LocalSegmentBaseOffset (0xA8C000), entao toda amostragem e o byte
             * proof liam 11.059.200 bytes antes da superficie real.
             *
             * Consequencia medida: `SrcPhysSample @ 0x15D0000` leu zeros, o
             * BYTE_PROOF reprovou um quadro CORRETO, o driver desligou o SDMA e
             * a submissao seguinte virou completion nao notificado -> BugCheck
             * 0x119 Arg1=2 Arg2=0xC0000185.
             *
             * E' o MESMO defeito que a C3.4 corrigiu no caminho de escape. Dois
             * caminhos, uma aritmetica: agora os dois saem de
             * Bc250SdmaPresentSegmentOffsetToVram. Falha fechada: sem geometria
             * publicada ou faixa fora do segmento, a origem e' RECUSADA em vez
             * de virar um endereco inventado.
             */
            UINT64 srcVramOffset = 0ull;
            if (Bc250SdmaPresentSegmentOffsetToVram(presentSegmentId,
                    presentPhysical, (UINT64)scanoutBytes, &srcVramOffset)) {
                srcPhysical = srcVramOffset;
                srcSegmentId = presentSegmentId;
                vaResolved = TRUE;
            } else {
                Bc250DisplayLog("[Present #%d] RECUSA conversao de segmento: SegOff=0x%llX Seg=%u Bytes=%u "
                    "(geometria publicada=%d SegBase=0x%llX SegBytes=0x%llX)\n",
                    count, (unsigned long long)presentPhysical, presentSegmentId,
                    scanoutBytes,
                    (int)g_Bc250SdmaPresent.SegmentGeometryReady,
                    (unsigned long long)g_Bc250SdmaPresent.LocalSegmentBaseOffset,
                    (unsigned long long)g_Bc250SdmaPresent.LocalSegmentBytes);
            }
        }

        /*
         * Fallback autoritativo para Flip: caso o Slot 1 nao tenha resolvido pelo
         * snapshot de GPUVA, mas a comutacao do scanout pelo SetVidPnSourceAddress
         * tenha publicado uma primaria valida no Segmento 1.
         */
        if (!vaResolved && isFlip && g_Bc250DisplayState.PrimaryAddressValid &&
            g_Bc250DisplayState.PrimarySegment == 1u) {
            UINT64 primaryOffset = 0ull;
            UINT32 primarySeg = 0u;
            if (Bc250DisplayResolvePrimarySource(&primaryOffset, &primarySeg)) {
                UINT64 srcVramOffset = 0ull;
                if (Bc250SdmaPresentSegmentOffsetToVram(primarySeg,
                        primaryOffset, (UINT64)scanoutBytes, &srcVramOffset)) {
                    srcPhysical = srcVramOffset;
                    srcSegmentId = primarySeg;
                    vaResolved = TRUE;
                    Bc250DisplayLog("[Present #%d] Source=PRIMARY_FALLBACK SegOff=0x%llX Seg=%u SegBase=0x%llX -> VramOff=0x%llX\n",
                        count, (unsigned long long)primaryOffset, primarySeg,
                        (unsigned long long)g_Bc250SdmaPresent.LocalSegmentBaseOffset,
                        (unsigned long long)srcPhysical);
                }
            }
        }

        if (vaResolved) {
            /*
             * C3.6: o log passa a mostrar os TRES numeros -- offset de segmento,
             * base do segmento e offset de VRAM resultante. Antes so' saia um
             * "Physical VRAM 0x%llX" que na verdade era offset de segmento, e foi
             * essa ambiguidade que escondeu o defeito por varias corridas.
             */
            Bc250DisplayLog("[Present #%d] Source=PRESENT_SLOT SegOff=0x%llX Seg=%u SegBase=0x%llX -> VramOff=0x%llX (resolved=%u, Flip=%u, Blt=%u)\n",
                count, presentPhysical, presentSegmentId,
                (unsigned long long)g_Bc250SdmaPresent.LocalSegmentBaseOffset,
                srcPhysical, presentResolved ? 1u : 0u, isFlip ? 1u : 0u, isBlt ? 1u : 0u);
        } else {
            Bc250DisplayLog("[Present #%d] REJECT source contract: Blt=%u, Flip=%u, PrimaryValid=%u, PrimarySeg=%u, SlotHAlloc=%p, SlotSeg=%u, SlotWord=0x%llX\n",
                count, isBlt ? 1u : 0u, isFlip ? 1u : 0u,
                g_Bc250DisplayState.PrimaryAddressValid ? 1u : 0u,
                g_Bc250DisplayState.PrimarySegment,
                pPresent->pAllocationInfo != NULL ?
                    pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation : NULL,
                pPresent->pAllocationInfo != NULL ?
                    (UINT32)pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].SegmentId : 0u,
                srcVa);
            srcPhysical = 0ull;
        }

        /*
         * Amostragem de sub-retangulo (cursor / apresentacao parcial):
         * Calculada em todo quadro para que o gate de supressao de tela preta
         * saiba se ha conteudo valido mesmo quando os 256 pontos esparsos forem zero.
         *
         * Robustez (multi-retangulo e multi-ponto):
         * 1. Varre ate 32 sub-retangulos (SubRectCnt), nao apenas o primeiro.
         *    DWM rotineiramente entrega SubRectCnt > 1 no movimento de cursor (rect 0 =
         *    area anterior apagada com zeros; rect 1 = nova posicao com o cursor).
         * 2. Amostra multiplos pontos por sub-retangulo (centro e 4 vertices/bordas),
         *    pois cursores transparentes ou com centro vazado podem ter amostra
         *    central zero e pixels nao-nulos no restante do sub-retangulo.
         */
        ULONG subRectCenterSample = 0u;
        UINT activeSubRectIndex = 0u;
        if (pPresent->SubRectCnt > 0 && pPresent->pDstSubRects != NULL && srcPhysical != 0 &&
            g_Bc250DisplayState.FrameBufferVirtual != NULL &&
            g_Bc250DisplayState.CurrentMode.Pitch >= 4u) {
            UINT maxSubRects = (pPresent->SubRectCnt > 32u) ? 32u : pPresent->SubRectCnt;
            for (UINT r = 0u; r < maxSubRects; ++r) {
                CONST RECT* rc = &pPresent->pDstSubRects[r];
                if (rc->left < 0 || rc->top < 0 || rc->right <= rc->left || rc->bottom <= rc->top) {
                    continue;
                }
                if ((ULONG)rc->right > g_Bc250DisplayState.CurrentMode.Width ||
                    (ULONG)rc->bottom > g_Bc250DisplayState.CurrentMode.Height) {
                    continue;
                }

                LONG midX = (rc->left + rc->right) / 2;
                LONG midY = (rc->top + rc->bottom) / 2;
                LONG probePoints[5][2] = {
                    { midX, midY },
                    { rc->left, rc->top },
                    { rc->right - 1, rc->top },
                    { rc->left, rc->bottom - 1 },
                    { rc->right - 1, rc->bottom - 1 }
                };

                for (int p = 0; p < 5; ++p) {
                    LONG px = probePoints[p][0];
                    LONG py = probePoints[p][1];
                    if (px >= 0 && py >= 0 &&
                        (ULONG)px < g_Bc250DisplayState.CurrentMode.Width &&
                        (ULONG)py < g_Bc250DisplayState.CurrentMode.Height) {
                        ULONG sampleByteOffset = (ULONG)py * g_Bc250DisplayState.CurrentMode.Pitch + (ULONG)px * 4u;
                        if ((srcPhysical + sampleByteOffset + 4u) <= g_Bc250DisplayState.FrameBufferSize) {
                            ULONG pixelVal = *(CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcPhysical + sampleByteOffset);
                            if (pixelVal != 0u) {
                                subRectCenterSample = pixelVal;
                                activeSubRectIndex = r;
                                break;
                            }
                        }
                    }
                }
                if (subRectCenterSample != 0u) {
                    break;
                }
            }
        }

        /* Amostragem e telemetria extensiva solicitada */
        if (count <= 10 || (count % 60) == 0) {
            CONST ULONG *dstDwords = (CONST ULONG *)g_Bc250DisplayState.FrameBufferVirtual;
            CONST ULONG *primDwords = NULL;
            CONST ULONG *vaDwords = NULL;

            if (srcPhysical != 0 && (srcPhysical + 16) <= g_Bc250DisplayState.FrameBufferSize) {
                primDwords = (CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcPhysical);
            }
            if (srcVa != 0 && (srcVa + 16) <= g_Bc250DisplayState.FrameBufferSize) {
                vaDwords = (CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcVa);
            }

            Bc250DisplayLog("[Present #%d] Blt=%d, hCtx=%p, DmaSize=%u, DmaVA=0x%llX, NumSrc=%u, NumDst=%u, Flags=0x%08X\n",
                count, isBlt ? 1 : 0, hContext, pPresent->DmaSize,
                (unsigned long long)pPresent->DmaBufferGpuVirtualAddress,
                numSrc, numDst, pPresent->Flags.Value);

            if (pPresent->pAllocationInfo != NULL) {
                for (ULONG slot = 0; slot < allocLimit; ++slot) {
                    Bc250DisplayLog("[Present #%d] Info%u: hDev=%p, Seg=%u, PhysAdapter=%u, VA=0x%llX, Phys=0x%llX\n",
                        count, slot,
                        pPresent->pAllocationInfo[slot].hDeviceSpecificAllocation,
                        (UINT32)pPresent->pAllocationInfo[slot].SegmentId,
                        (UINT32)pPresent->pAllocationInfo[slot].PhysicalAdapterIndex,
                        (unsigned long long)pPresent->pAllocationInfo[slot].AllocationVirtualAddress,
                        (unsigned long long)pPresent->pAllocationInfo[slot].PhysicalAddress.QuadPart);
                }
            }

            Bc250DisplayLog("[Present #%d] DstRect=(%d,%d)-(%d,%d), SrcRect=(%d,%d)-(%d,%d), SubRects=%u\n",
                count,
                pPresent->DstRect.left, pPresent->DstRect.top, pPresent->DstRect.right, pPresent->DstRect.bottom,
                pPresent->SrcRect.left, pPresent->SrcRect.top, pPresent->SrcRect.right, pPresent->SrcRect.bottom,
                pPresent->SubRectCnt);

            if (pPresent->SubRectCnt > 0 && pPresent->pDstSubRects != NULL) {
                Bc250DisplayLog("[Present #%d] SubRect[%u]=(%d,%d)-(%d,%d), CenterPixel=0x%08X\n",
                    count,
                    activeSubRectIndex,
                    pPresent->pDstSubRects[activeSubRectIndex].left, pPresent->pDstSubRects[activeSubRectIndex].top,
                    pPresent->pDstSubRects[activeSubRectIndex].right, pPresent->pDstSubRects[activeSubRectIndex].bottom,
                    subRectCenterSample);
            }

            if (primDwords != NULL) {
                Bc250DisplayLog("[Present #%d] SrcPhysSample @ 0x%llX: %08X %08X %08X %08X\n",
                    count, srcPhysical, primDwords[0], primDwords[1], primDwords[2], primDwords[3]);
            }
            if (vaDwords != NULL) {
                Bc250DisplayLog("[Present #%d] SrcVaOffsetSample @ 0x%llX: %08X %08X %08X %08X\n",
                    count, srcVa, vaDwords[0], vaDwords[1], vaDwords[2], vaDwords[3]);
            }
            if (dstDwords != NULL) {
                Bc250DisplayLog("[Present #%d] ScanoutSample @ 0x0: %08X %08X %08X %08X\n",
                    count, dstDwords[0], dstDwords[1], dstDwords[2], dstDwords[3]);
            }

            /*
             * Amostragem distribuida dos tres candidatos, antes de qualquer
             * escrita no scanout.  Quatro DWORDs no inicio de um buffer nao
             * distinguem "superficie zerada" de "primeiros pixels pretos" --
             * essa foi a fragilidade central do diagnostico da corrida 09:04.
             * 256 pontos igualmente espacados + XOR acumulado resolvem isso.
             */
            {
                ULONG nz = 0u, hx = 0u;
                UINT64 primaryOffset = 0;
                UINT32 primarySeg = 0;
                UINT64 dstPhysical = 0;
                UINT32 dstSegmentId = 0;
                BOOLEAN dstResolved = Bc250DisplayResolvePresentDestination(
                    hContext, pPresent, &dstPhysical, &dstSegmentId);

                if (Bc250DisplaySampleSurface(0ull, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread SCANOUT   @ 0x%llX: NonZero=%u/256, Xor=%08X\n",
                        count, 0ull, nz, hx);
                }
                if (Bc250DisplayResolvePrimarySource(&primaryOffset, &primarySeg) &&
                    Bc250DisplaySampleSurface(primaryOffset, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread PRIMARY   @ 0x%llX: NonZero=%u/256, Xor=%08X (Seg=%u, hAlloc=%p)\n",
                        count, primaryOffset, nz, hx, primarySeg,
                        g_Bc250DisplayState.hPrimarySourceAllocation);
                }
                if (presentResolved &&
                    Bc250DisplaySampleSurface(srcPhysical, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread SLOT_SRC  @ 0x%llX (SegOff=0x%llX, Seg=%u): NonZero=%u/256, Xor=%08X\n",
                        count, srcPhysical, presentPhysical, presentSegmentId, nz, hx);
                    if (nz > 0) {
                        Bc250DisplayLog("[Present #%d] *** DESKTOP REAL DETECTADO NA SUPERFICIE *** NonZero=%u/256, Hash=0x%08X (Phys=0x%llX)\n",
                            count, nz, hx, srcPhysical);
                    } else {
                        Bc250DisplayLog("[Present #%d] AVISO: Superficie de origem 0x%llX esta com todos os pixels em zero (NonZero=0/256)\n",
                            count, srcPhysical);
                    }
                }
                if (dstResolved) {
                    UINT64 dstVramOffset = 0ull;
                    if (Bc250SdmaPresentSegmentOffsetToVram(dstSegmentId, dstPhysical, (UINT64)scanoutBytes, &dstVramOffset)) {
                        if (Bc250DisplaySampleSurface(dstVramOffset, scanoutBytes, 256u, &nz, &hx)) {
                            Bc250DisplayLog("[Present #%d] Spread SLOT_DST  @ 0x%llX (SegOff=0x%llX Seg=%u): NonZero=%u/256, Xor=%08X\n",
                                count, dstVramOffset, dstPhysical, dstSegmentId, nz, hx);
                        }
                    }
                }
                if (srcVa != 0 &&
                    Bc250DisplaySampleSurface(srcVa, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread SLOT_WORD @ 0x%llX: NonZero=%u/256, Xor=%08X (union PhysicalAddress/VirtualAddress)\n",
                        count, srcVa, nz, hx);
                }
            }
        }

        /*
         * Execucao do Blt para o scanout do UEFI GOP (offset 0):
         * Se temos a superficie primaria fisica (srcPhysical), copiamos para o scanout.
         *
         * F3.2: sample only for diagnostics. Black is a valid frame and must
         * replace the previous frame through the same GPU copy path.
         */
        {
            BOOLEAN isSubWindow = FALSE;
            ULONG presentW = g_Bc250DisplayState.CurrentMode.Width;
            ULONG presentH = g_Bc250DisplayState.CurrentMode.Height;
            LONG rectW = pPresent->SrcRect.right - pPresent->SrcRect.left;
            LONG rectH = pPresent->SrcRect.bottom - pPresent->SrcRect.top;

            if (rectW > 0 && rectH > 0 &&
                ((ULONG)rectW < presentW || (ULONG)rectH < presentH)) {
                isSubWindow = TRUE;
                presentW = (ULONG)rectW;
                presentH = (ULONG)rectH;
            }

            ULONG requiredSrcBytes = isSubWindow ? (presentW * presentH * 4u) : scanoutBytes;
            ULONG srcNz = 0u, srcHx = 0u;
            (void)Bc250DisplaySampleSurface(srcPhysical, requiredSrcBytes, 256u, &srcNz, &srcHx);
            UINT64 effectiveSrcPhys = srcPhysical;

            if (effectiveSrcPhys != 0ull && (count <= 10 || (count % 60) == 0)) {
                Bc250DisplayLog("[Present #%d] ROTEAMENTO AO SCANOUT: VramOff=0x%llX (NonZero=%u/256 Hash=0x%08X, Dim=%ux%u%s)\n",
                    count, effectiveSrcPhys, srcNz, srcHx, presentW, presentH,
                    isSubWindow ? " SUBWINDOW" : "");
            }

            static int s_TileProbeCount = 0;
            if (s_TileProbeCount < 3 && srcNz >= 64u && g_Bc250DisplayState.FrameBufferVirtual != NULL && effectiveSrcPhys != 0 &&
                effectiveSrcPhys + 10248u <= g_Bc250DisplayState.FrameBufferSize) {
                volatile uint32_t *p = (volatile uint32_t *)((char *)g_Bc250DisplayState.FrameBufferVirtual + effectiveSrcPhys);
                Bc250DisplayLog("[Present #%d TILE_PROBE (Content NonZero=%u/256)] Off=0: %08X %08X %08X %08X | Off+64: %08X %08X | Off+10240: %08X %08X\n",
                    count, srcNz, p[0], p[1], p[2], p[3], p[16], p[17], p[2560], p[2561]);
                s_TileProbeCount++;
            }

            if (effectiveSrcPhys != 0 && requiredSrcBytes > 0 &&
                effectiveSrcPhys <= g_Bc250DisplayState.FrameBufferSize &&
                requiredSrcBytes <= g_Bc250DisplayState.FrameBufferSize - effectiveSrcPhys) {
                BOOLEAN sdmaDone = FALSE;
                BOOLEAN dcnNoCopy = FALSE; /* DCN.1: flip pelo HUBP0, nenhuma copia */
                BOOLEAN bltToDestination = (pPresent->Flags.Blt != 0u) ? TRUE : FALSE;
                const char *bltDestinationVerdict = NULL;
                BOOLEAN dirtyRectsUsed = FALSE;
                UINT dirtyRectCount = 0u;
                CONST char *dirtyRefuseReason = s_Bc250Dx50DirtyRectsEnabled ?
                    "NAO_AVALIADO" : "DX50_BISSECCAO_DESLIGADO";
                UINT dirtyRefuseIndex = 0u;
                ULONG dirtyRectBytes = 0u;
                uint64_t srcGpu = g_Bc250SdmaPresent.VramMcStart + effectiveSrcPhys;
                uint64_t dstScanoutGpu = g_Bc250SdmaPresent.ScanoutGpuAddress;

                /* F3.2: black is valid content. Sampling is evidence only,
                 * never permission to suppress a requested GPU copy. */
                {
                    if (srcNz == 0u && subRectCenterSample != 0u && (count <= 10 || (count % 60) == 0)) {
                        Bc250DisplayLog("[Present #%d] Blt acionado por sub-retangulo parcial nao-nulo (NonZero=0/256, SubRect=0x%08X @ 0x%llX)\n",
                            count, subRectCenterSample, effectiveSrcPhys);
                    }
                    /* DX.63: a supressao de quadro em branco protege o SCANOUT; um
                     * Blt nao escreve o scanout, e preto e' conteudo valido. */
                    BOOLEAN skipBlankUnderEscape = (!bltToDestination &&
                        srcNz == 0u && subRectCenterSample == 0u &&
                        (g_Bc250SdmaPresent.EscapeNonZeroBltCount > 0 || g_Bc250SdmaPresent.EscapeEnteredCount > 0 || count <= 5));
                    if (bltToDestination) {
                        bltDestinationVerdict = g_Bc250SdmaPresent.HardwareEnabled ?
                            Bc250DisplayBltToDestination(hContext, pPresent,
                                effectiveSrcPhys, &sdmaDone) : "SDMA_DESABILITADO";
                        if (!sdmaDone) {
                            Bc250DisplayLog("[Present #%d] BLT_DESTINO RECUSADO: motivo=%s hSrc=%p hDst=%p Src=(%ld,%ld)-(%ld,%ld) Dst=(%ld,%ld)-(%ld,%ld); scanout NAO tocado\n",
                                count, bltDestinationVerdict,
                                pPresent->pAllocationInfo != NULL ?
                                    pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation : NULL,
                                pPresent->pAllocationInfo != NULL ?
                                    pPresent->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation : NULL,
                                pPresent->SrcRect.left, pPresent->SrcRect.top,
                                pPresent->SrcRect.right, pPresent->SrcRect.bottom,
                                pPresent->DstRect.left, pPresent->DstRect.top,
                                pPresent->DstRect.right, pPresent->DstRect.bottom);
                        } else if (count <= 10 || (count % 60) == 0) {
                            Bc250DisplayLog("[Present #%d] BLT_DESTINO OK: Src=(%ld,%ld)-(%ld,%ld) -> Dst=(%ld,%ld)-(%ld,%ld) na alocacao de destino; scanout NAO tocado\n",
                                count,
                                pPresent->SrcRect.left, pPresent->SrcRect.top,
                                pPresent->SrcRect.right, pPresent->SrcRect.bottom,
                                pPresent->DstRect.left, pPresent->DstRect.top,
                                pPresent->DstRect.right, pPresent->DstRect.bottom);
                        }
                    } else if (isFlip && !isSubWindow && Bc250DisplayDcnFlipArmed() != 0u) {
                        /*
                         * DCN.1 D3: o scanout passa a ser a propria primaria; o
                         * SetVidPnSourceAddress/MPO3 aponta o HUBP0 para ela. A
                         * copia de 11 MB pelo SDMA0 deixa de existir neste caminho.
                         */
                        sdmaDone = TRUE;
                        dcnNoCopy = TRUE;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
                        {
                            LONG skipped = InterlockedIncrement(&s_Bc250Dcn.CopiesSkipped);
                            if (skipped <= 8 || (skipped % 512) == 0) {
                                Bc250DisplayLog("[Present #%d] DCN1_SEM_COPIA #%ld: flip pelo HUBP0, nenhuma escrita no scanout do GOP (VramOff=0x%llX)\n",
                                    count, skipped, (unsigned long long)effectiveSrcPhys);
                            }
                        }
#endif
                    } else if (skipBlankUnderEscape) {
                        sdmaDone = TRUE;
                        if (count <= 10 || (count % 60) == 0) {
                            Bc250DisplayLog("[Present #%d] SCANOUT ESCAPE ATIVO: DWM em branco suprimido, scanout mantido para aplicacao (EscapeNonZero=%ld EscapeTotal=%ld InitPreserve=%d)\n",
                                count, (long)g_Bc250SdmaPresent.EscapeNonZeroBltCount, (long)g_Bc250SdmaPresent.EscapeEnteredCount, (count <= 5));
                        }
                    } else if (g_Bc250SdmaPresent.HardwareEnabled && dstScanoutGpu != 0 && srcGpu != dstScanoutGpu) {
                        if (isSubWindow) {
                            ULONG srcPitch = presentW * 4u;
                            ULONG dstPitch = g_Bc250DisplayState.CurrentMode.Pitch;
                            ULONG dstX = (g_Bc250DisplayState.CurrentMode.Width > presentW) ?
                                         ((g_Bc250DisplayState.CurrentMode.Width - presentW) / 2u) : 0u;
                            ULONG dstY = (g_Bc250DisplayState.CurrentMode.Height > presentH) ?
                                         ((g_Bc250DisplayState.CurrentMode.Height - presentH) / 2u) : 0u;
                            sdmaDone = Bc250SdmaPresentBltRect(
                                srcGpu, srcPitch,
                                dstScanoutGpu, dstPitch,
                                dstX, dstY,
                                presentW, presentH);
                        } else if (s_Bc250Dx50DirtyRectsEnabled &&
                                   Bc250DisplayDirtyRectsUsable(pPresent, &dirtyRectCount,
                                       &dirtyRefuseReason, &dirtyRefuseIndex)) {
                            /*
                             * DX.34 -- HONRAR pDstSubRects (contrato do DXGKARG_PRESENT).
                             *
                             * Corrida 79 (P0): Present #8 veio com SubRects=1 do tamanho de
                             * um cursor e a superficie inteira 0/256; o KMD copiava os
                             * 11.059.200 bytes e o Present #9 ja amostrava o scanout 0/256
                             * -- tela preta com o cursor sozinho. So' os retangulos sujos
                             * sao conteudo novo; o resto do scanout e' o quadro anterior.
                             * Cada retangulo vai por COPY_LINEAR_SUB_WINDOW (o caminho do
                             * SUBWINDOW que pos o 1280x720 da Corrida 75 no monitor), com a
                             * origem no mesmo (x,y) da superficie de tela cheia.
                             */
                            ULONG pitch = g_Bc250DisplayState.CurrentMode.Pitch;
                            UINT r;
                            dirtyRectsUsed = TRUE;
                            sdmaDone = TRUE;
                            for (r = 0u; r < dirtyRectCount && sdmaDone; ++r) {
                                CONST RECT *rc = &pPresent->pDstSubRects[r];
                                uint64_t rectSrc = srcGpu + (uint64_t)(ULONG)rc->top * pitch +
                                    (uint64_t)(ULONG)rc->left * 4ull;
                                sdmaDone = Bc250SdmaPresentBltRect(
                                    rectSrc, pitch, dstScanoutGpu, pitch,
                                    (ULONG)rc->left, (ULONG)rc->top,
                                    (ULONG)(rc->right - rc->left),
                                    (ULONG)(rc->bottom - rc->top));
                                dirtyRectBytes += (ULONG)(rc->right - rc->left) *
                                    (ULONG)(rc->bottom - rc->top) * 4u;
                            }
                            {
                                LONG dirtyLog = InterlockedIncrement(&s_Bc250DirtyRectLogCount);
                                if (dirtyLog <= 16 || (dirtyLog % 128) == 0) {
                                    ULONG match = 0u, probes = 0u, srcNonZero = 0u;
                                    if (sdmaDone) {
                                        Bc250DisplayDirtyRectProof(pPresent, dirtyRectCount,
                                            effectiveSrcPhys, &match, &probes, &srcNonZero);
                                    }
                                    Bc250DisplayLog("[Present #%d] DIRTY_RECTS n=%u Bytes=%u rect0=(%ld,%ld)-(%ld,%ld) SDMA=%s DIRTY_RECT_PROOF Match=%u/%u SrcNZ=%u -- so' os retangulos sujos foram ao scanout (ocorrencia #%ld)\n",
                                        count, dirtyRectCount, dirtyRectBytes,
                                        pPresent->pDstSubRects[0].left, pPresent->pDstSubRects[0].top,
                                        pPresent->pDstSubRects[0].right, pPresent->pDstSubRects[0].bottom,
                                        sdmaDone ? "YES" : "FAILED_CLOSED",
                                        match, probes, srcNonZero, dirtyLog);
                                }
                            }
                        } else {
                            if (pPresent->SubRectCnt > 0u) {
                                LONG fullLog = InterlockedIncrement(&s_Bc250DirtyRectLogCount);
                                if (fullLog <= 16 || (fullLog % 128) == 0) {
                                    CONST RECT *bad = (pPresent->pDstSubRects != NULL &&
                                        dirtyRefuseIndex < pPresent->SubRectCnt) ?
                                        &pPresent->pDstSubRects[dirtyRefuseIndex] : NULL;
                                    Bc250DisplayLog("[Present #%d] DIRTY_RECTS_RECUSADOS SubRectCnt=%u motivo=%s Src=(%ld,%ld)-(%ld,%ld) Dst=(%ld,%ld)-(%ld,%ld) rect[%u]=(%ld,%ld)-(%ld,%ld) teto=%u; copia de tela cheia (ocorrencia #%ld)\n",
                                        count, pPresent->SubRectCnt, dirtyRefuseReason,
                                        pPresent->SrcRect.left, pPresent->SrcRect.top,
                                        pPresent->SrcRect.right, pPresent->SrcRect.bottom,
                                        pPresent->DstRect.left, pPresent->DstRect.top,
                                        pPresent->DstRect.right, pPresent->DstRect.bottom,
                                        dirtyRefuseIndex,
                                        bad ? bad->left : 0L, bad ? bad->top : 0L,
                                        bad ? bad->right : 0L, bad ? bad->bottom : 0L,
                                        BC250_DIRTY_RECT_MAX, fullLog);
                                }
                            }
                            sdmaDone = Bc250SdmaPresentBlt(srcGpu, dstScanoutGpu, scanoutBytes);
                        }
                    }

                    if (sdmaDone && !skipBlankUnderEscape &&
                        (count == 1 || count == 5 || count == 10 || count == 25 || count == 45)) {
                        if (bltToDestination) {
                            /* DX.63: o Blt nao escreveu o scanout; comparar a origem
                             * com o scanout reprovaria um present correto. */
                            Bc250DisplayLog("[Present #%d] BYTE_PROOF SKIPPED (BLT_DESTINO): copia para a alocacao de destino, scanout nao participa\n",
                                count);
                        } else if (isSubWindow) {
                            Bc250DisplayLog("[Present #%d] BYTE_PROOF SKIPPED (SUBWINDOW %ux%u no scanout %ux%u): Blt 2D retangular conferido via engine\n",
                                count, presentW, presentH,
                                g_Bc250DisplayState.CurrentMode.Width,
                                g_Bc250DisplayState.CurrentMode.Height);
                        } else if (dirtyRectsUsed) {
                            /* DX.34: so' os retangulos sujos foram copiados; comparar a
                             * superficie inteira reprovaria um present correto. A prova
                             * amostral por retangulo sai na linha DIRTY_RECTS. */
                            Bc250DisplayLog("[Present #%d] BYTE_PROOF SKIPPED (DIRTY_RECTS n=%u Bytes=%u): prova amostral por retangulo na linha DIRTY_RECTS\n",
                                count, dirtyRectCount, dirtyRectBytes);
                        } else if (BC250_P1_SILENCIO_DA_BAR) {
                            /* P1 (doc 594): a comparacao integral le ~11 MB da VRAM pela BAR. */
                            Bc250DisplayLog("[Present #%d] BYTE_PROOF SKIPPED (P1 silencio da BAR): sem leitura de VRAM pela CPU; prova so' da engine (RPTR==WPTR e fence)\n",
                                count);
                        } else {
                            UINT64 mismatchOffset = ~(UINT64)0;
                            ULONG postNz = 0u, postHx = 0u;
                            BOOLEAN bytesMatch = Bc250DisplayCompareSurfaceBytes(
                                effectiveSrcPhys, 0ull, scanoutBytes, &mismatchOffset);
                            BC250_BLT_PROOF_VERDICT proofVerdict;

                            if (bytesMatch) {
                                (void)Bc250DisplaySampleSurface(0ull, scanoutBytes, 256u, &postNz, &postHx);
                            }
                            /*
                             * A9/A10: a classificacao vem de driver/core, e o teste
                             * blt_proof_contract_test trava a semantica. O driver nao
                             * decide sozinho o que e' PASS.
                             */
                            proofVerdict = Bc250ClassifyBltProof(
                                bytesMatch ? 1u : 0u, (uint32_t)postNz,
                                bytesMatch ? 256u : 0u);

                            if (!Bc250BltProofRequiresFailClosed(proofVerdict)) {
                                if (!Bc250BltProofAllowsTripodClaim(proofVerdict)) {
                                    LONG streak = InterlockedIncrement(
                                        &g_Bc250SdmaPresent.BltBlankStreak);
                                    InterlockedExchange(&s_PresentByteProofState, 0);
                                    InterlockedExchange(
                                        &g_Bc250SdmaPresent.BltDataProofState,
                                        BC250_BLT_DATA_PROOF_BLANK);
                                    Bc250DisplayLog("[Present #%d] BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO): Source=0x%llX Dest=0x0 Bytes=%u copiados fielmente, mas PostScanout NonZero=0/256 Hash=0x%08X. Zero contra zero NAO prova caminho de pixel. BlankStreak=%ld\n",
                                        count, effectiveSrcPhys, scanoutBytes, postHx, streak);
                                } else {
                                    InterlockedExchange(&s_PresentByteProofState, 2);
                                    InterlockedExchange(&g_Bc250SdmaPresent.BltBlankStreak, 0);
                                    InterlockedExchange(
                                        &g_Bc250SdmaPresent.BltDataProofState,
                                        BC250_BLT_DATA_PROOF_OK);
                                    Bc250DisplayLog("[Present #%d] BYTE_PROOF PASS (dados conferidos, frame com conteudo): Source=0x%llX Dest=0x0 Bytes=%u (PostScanout: NonZero=%u/256, Hash=0x%08X)\n",
                                        count, effectiveSrcPhys, scanoutBytes, postNz, postHx);
                                }
                            } else {
                                InterlockedExchange(&s_PresentByteProofState, -1);
                                InterlockedExchange(
                                    &g_Bc250SdmaPresent.BltDataProofState,
                                    BC250_BLT_DATA_PROOF_MISMATCH);
                                sdmaDone = FALSE;
                                /*
                                 * C3.7 (12/09) -- A REACAO DEIXOU DE SER PIOR QUE A FALHA.
                                 *
                                 * Aqui havia `g_Bc250SdmaPresent.HardwareEnabled = FALSE`.
                                 * Medido na corrida 13:11: um byte proof reprovado
                                 * desligava o SDMA GLOBALMENTE, e a partir dai TODA
                                 * submissao WDDM caia em `SubmitIndirectAsync recusado:
                                 * HwEnabled=0` -> `IB NAO EXECUTADO; completion NAO
                                 * notificado` -> o VidMm derrubou a maquina com
                                 * BugCheck 0x119 Arg1=2 Arg2=0xC0000185, sete linhas de
                                 * log depois.
                                 *
                                 * E a reprovacao era FALSA: o byte proof lia o endereco
                                 * errado por falta de LocalSegmentBaseOffset (C3.6).
                                 *
                                 * Falhar fechado continua certo -- este present NAO
                                 * declara sucesso e `sdmaDone` fica FALSE. O que nao pode
                                 * e' um quadro reprovado condenar o motor de submissao do
                                 * adaptador inteiro. Quem desabilita a engine e' o
                                 * caminho de TDR, que tem quiesce e restart com prova.
                                 */
                                Bc250DisplayLog("[Present #%d] BYTE_PROOF FAIL: Source=0x%llX Dest=0x0 Bytes=%u FirstMismatch=0x%llX; present reprovado (engine PRESERVADA)\n",
                                    count, effectiveSrcPhys, scanoutBytes, mismatchOffset);
                            }
                        }
                    }

                    if (!bltToDestination && dcnNoCopy && (count <= 10 || (count % 60) == 0)) {
                        /* Corrida 270 (doc 712): sem isto a linha abaixo dizia
                         * "Blt ... SDMA=YES" para um Present que nao copiou nada. */
                        Bc250DisplayLog("[Present #%d] SEM COPIA ao scanout do GOP (flip pelo HUBP0, DCN.1); src 0x%llX\n",
                            count, effectiveSrcPhys);
                    } else if (!bltToDestination && (count <= 10 || (count % 60) == 0)) {
                        if (isSubWindow) {
                            Bc250DisplayLog("[Present #%d] Blt 2D %ux%u srcPitch=%u (src 0x%llX -> scanout 0x0 DstXY=%u,%u): SDMA=%s\n",
                                count, presentW, presentH, presentW * 4u,
                                effectiveSrcPhys,
                                (g_Bc250DisplayState.CurrentMode.Width > presentW) ? ((g_Bc250DisplayState.CurrentMode.Width - presentW) / 2u) : 0u,
                                (g_Bc250DisplayState.CurrentMode.Height > presentH) ? ((g_Bc250DisplayState.CurrentMode.Height - presentH) / 2u) : 0u,
                                sdmaDone ? "YES" : "FAILED_CLOSED");
                        } else if (dirtyRectsUsed) {
                            Bc250DisplayLog("[Present #%d] Blt DIRTY_RECTS n=%u %u bytes (src 0x%llX -> scanout 0x0): SDMA=%s\n",
                                count, dirtyRectCount, dirtyRectBytes, effectiveSrcPhys, sdmaDone ? "YES" : "FAILED_CLOSED");
                        } else {
                            Bc250DisplayLog("[Present #%d] Blt %u bytes (src 0x%llX -> scanout 0x0): SDMA=%s\n",
                                count, scanoutBytes, effectiveSrcPhys, sdmaDone ? "YES" : "FAILED_CLOSED");
                        }
                    }
                    /* F3.2: the old unconditional SUCCESS concealed failed DMA.
                     * Do NOT return the historical STATUS_IO_DEVICE_ERROR (0x10E).
                     * WDK DXGKDDI_PRESENT documents GPU_EXCEPTION_ON_DEVICE for
                     * DMA errors, placing this context in the lost state. This
                     * failure route is NOT VALIDATED ON HARDWARE; no guarantee
                     * against BugCheck is inferred from the documented status.
                     * No NOP/completion is generated for a failed transfer. */
                    presentStatus = Bc250PresentCopySucceeded(sdmaDone,
                        dstScanoutGpu != 0 && srcGpu == dstScanoutGpu) ?
                        STATUS_SUCCESS : STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;
                    if (!NT_SUCCESS(presentStatus)) {
                        Bc250DisplayLog("[Present #%d] RECUSADO: copia GPU nao concluida; status=0x%08X, sem NOP de sucesso\n",
                            count, (ULONG)presentStatus);
                    }
                }
            } else if (vaResolved) {
                Bc250DisplayLog("[Present #%d] REJECT source bounds: Phys=0x%llX, Bytes=%u, Aperture=%u\n",
                    count, effectiveSrcPhys, requiredSrcBytes, g_Bc250DisplayState.FrameBufferSize);
                presentStatus = STATUS_INVALID_PARAMETER;
            }
        }
}

    /*
     * Emite pacote SDMA no DMA buffer para que o GPU Scheduler do Windows
     * tenha um pacote de submissao valido para despachar e avancar a fence.
     */
    if (NT_SUCCESS(presentStatus) && pPresent->pDmaBuffer != NULL &&
        pPresent->DmaSize >= sizeof(uint32_t)) {
        uint32_t *dma = (uint32_t *)pPresent->pDmaBuffer;
        *dma = 0u; /* SDMA NOP */
        pPresent->pDmaBuffer = (PVOID)(dma + 1);
    }

    Bc250ActiveSdmaRetirePendingOnAllAdapters(FALSE);

    /*
     * DX.72: tempo INTERNO desta DDI, comparavel ao intervalo ENTER/LEAVE do
     * pfnPresentCb na UMD. O harness windowed usa Blt: guardar todos os seus
     * quadros; os Flip do DWM ficam amostrados para limitar I/O no caminho
     * quente. A leitura do contador precede o log para nao incluir seu flush.
     * A diferenca callback - DDI inclui o runtime/scheduler, sem atribuir
     * automaticamente toda a diferenca a uma unica camada.
     */
    {
        LARGE_INTEGER presentTimerEnd = KeQueryPerformanceCounter(NULL);
        LONGLONG elapsedTicks = presentTimerEnd.QuadPart -
            presentTimerStart.QuadPart;
        ULONGLONG elapsedUs = 0ull;
        if (elapsedTicks > 0 && presentTimerFrequency.QuadPart > 0) {
            elapsedUs = ((ULONGLONG)elapsedTicks * 1000000ull) /
                (ULONGLONG)presentTimerFrequency.QuadPart;
        }
        if (pPresent->Flags.Blt || count <= 16L || (count % 32L) == 0L ||
            !NT_SUCCESS(presentStatus)) {
            Bc250DisplayLog("[DX74_PRESENT_KMD_TEMPO] n=%ld hContext=%p Blt=%u Flip=%u Status=0x%08X StartQpc=%llu EndQpc=%llu ElapsedUs=%llu\n",
                count, hContext, (unsigned)pPresent->Flags.Blt,
                (unsigned)pPresent->Flags.Flip, (unsigned)presentStatus,
                (unsigned long long)presentTimerStart.QuadPart,
                (unsigned long long)presentTimerEnd.QuadPart,
                (unsigned long long)elapsedUs);
        }
    }

    return presentStatus;
}

NTSTATUS APIENTRY Bc250DxgkDdiControlInterrupt(
    CONST HANDLE hAdapter,
    CONST DXGK_INTERRUPT_TYPE InterruptType,
    BOOLEAN EnableInterrupt)
{
    UNREFERENCED_PARAMETER(hAdapter);

    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_CONTROL_INTERRUPT_COUNT,
        (ULONG)InterlockedIncrement(
            &g_Bc250DisplayState.ControlInterruptCount));
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_CONTROL_INTERRUPT_TYPE, (ULONG)InterruptType);
    Bc250RecordDisplayProbePassive(
        BC250_PROBE_DMM_CONTROL_INTERRUPT_ENABLE,
        EnableInterrupt ? 1u : 0u);

    if (InterruptType == DXGK_INTERRUPT_DISPLAYONLY_VSYNC ||
        InterruptType == DXGK_INTERRUPT_CRTC_VSYNC ||
        InterruptType == DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY) {
        InterlockedExchange(&g_Bc250DisplayState.ActiveInterruptType, (LONG)InterruptType);
        Bc250SetVsyncState(EnableInterrupt);
        Bc250DisplayLog("[ControlInterrupt] VSYNC Enable=%d (Type=%u)\n",
            (int)EnableInterrupt, (ULONG)InterruptType);
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DxgkDdiControlInterrupt2(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGKARG_CONTROLINTERRUPT2 InterruptControl)
{
    BOOLEAN enable = (InterruptControl.InterruptState == DXGK_INTERRUPT_ENABLE);
    return Bc250DxgkDdiControlInterrupt(
        hAdapter,
        InterruptControl.InterruptType,
        enable);
}

NTSTATUS APIENTRY Bc250DxgkDdiPresentDisplayOnly(
    CONST PVOID MiniportDeviceContext,
    CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly)
{
    ULONG i;
    LONG presentCount;
    BOOLEAN firstPresent;
    ULONG width = g_Bc250DisplayState.CurrentMode.Width;
    ULONG height = g_Bc250DisplayState.CurrentMode.Height;
    ULONG destinationPitch = g_Bc250DisplayState.CurrentMode.Pitch;
    PUCHAR frameBuffer;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (pPresentDisplayOnly == NULL || pPresentDisplayOnly->pSource == NULL ||
        pPresentDisplayOnly->VidPnSourceId != 0 ||
        pPresentDisplayOnly->BytesPerPixel != 4 ||
        pPresentDisplayOnly->Pitch <= 0 ||
        width == 0 || height == 0 || width > (MAXLONG / 4u) ||
        (ULONG)pPresentDisplayOnly->Pitch < width * 4u ||
        (pPresentDisplayOnly->NumMoves != 0 && pPresentDisplayOnly->pMoves == NULL) ||
        (pPresentDisplayOnly->NumDirtyRects != 0 &&
         pPresentDisplayOnly->pDirtyRect == NULL) ||
        pPresentDisplayOnly->Flags.Rotate != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    presentCount = InterlockedIncrement(&g_Bc250DisplayState.PresentCount);
    firstPresent = (presentCount == 1);
    if (firstPresent) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_PRESENT_ENTERED, 1u);
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_PRESENT_SOURCE_PITCH,
            (ULONG)pPresentDisplayOnly->Pitch);
        Bc250DisplayLog("=== PresentDisplayOnly First Call: Pitch=%d, Visible=%d, Committed=%d ===\n",
            pPresentDisplayOnly->Pitch,
            g_Bc250DisplayState.SourceVisible,
            g_Bc250DisplayState.VidPnCommitted);
    }

    if (g_Bc250DisplayState.SourceVisible == 0) {
        if (firstPresent) {
            Bc250RecordDisplayProbePassive(
                BC250_PROBE_DMM_PRESENT_STATUS, (ULONG)STATUS_SUCCESS);
        }
        Bc250RecordVsyncProofAfterSuccessfulPresent();
        return STATUS_SUCCESS;
    }
    if (g_Bc250DisplayState.VidPnCommitted == 0) {
        if (firstPresent) {
            Bc250RecordDisplayProbePassive(
                BC250_PROBE_DMM_PRESENT_STATUS,
                (ULONG)STATUS_DEVICE_NOT_READY);
        }
        return STATUS_DEVICE_NOT_READY;
    }
    if (!Bc250AcquirePresentAccess()) {
        if (firstPresent) {
            Bc250RecordDisplayProbePassive(
                BC250_PROBE_DMM_PRESENT_STATUS,
                (ULONG)STATUS_DEVICE_NOT_READY);
        }
        return STATUS_DEVICE_NOT_READY;
    }
    frameBuffer = (PUCHAR)g_Bc250DisplayState.FrameBufferVirtual;

    if (InterlockedExchange(
            &g_Bc250DisplayState.ForceFullPresent, 0) != 0) {
        LONG y;
        for (y = 0; y < (LONG)height; ++y) {
            PUCHAR dst = frameBuffer + ((SIZE_T)y * destinationPitch);
            CONST UCHAR *src =
                (CONST UCHAR *)pPresentDisplayOnly->pSource +
                ((SIZE_T)y * pPresentDisplayOnly->Pitch);
            RtlCopyMemory(dst, src, (SIZE_T)width * 4u);
        }
        KeMemoryBarrier();
        Bc250ReleasePresentAccess();
        if (firstPresent) {
            Bc250RecordDisplayProbePassive(
                BC250_PROBE_DMM_PRESENT_STATUS, (ULONG)STATUS_SUCCESS);
        }
        return STATUS_SUCCESS;
    }

    for (i = 0; i < pPresentDisplayOnly->NumMoves; ++i) {
        RECT clipped;
        LONG y;

        if (!Bc250ClipRectToMode(
                &pPresentDisplayOnly->pMoves[i].DestRect, &clipped)) {
            continue;
        }
        for (y = clipped.top; y < clipped.bottom; ++y) {
            PUCHAR destinationRow = frameBuffer +
                ((SIZE_T)y * destinationPitch) +
                ((SIZE_T)clipped.left * 4u);
            CONST UCHAR *sourceRow =
                (CONST UCHAR *)pPresentDisplayOnly->pSource +
                ((SIZE_T)y * pPresentDisplayOnly->Pitch) +
                ((SIZE_T)clipped.left * 4u);
            RtlCopyMemory(destinationRow, sourceRow,
                (SIZE_T)(clipped.right - clipped.left) * 4u);
        }
    }

    if (pPresentDisplayOnly->NumDirtyRects > 0) {
        for (i = 0; i < pPresentDisplayOnly->NumDirtyRects; i++) {
            RECT clipped;
            LONG y;

            if (!Bc250ClipRectToMode(
                    &pPresentDisplayOnly->pDirtyRect[i], &clipped)) {
                continue;
            }

            for (y = clipped.top; y < clipped.bottom; y++) {
                PUCHAR dst = frameBuffer +
                    ((SIZE_T)y * destinationPitch) +
                    ((SIZE_T)clipped.left * 4u);
                CONST UCHAR *src = (CONST UCHAR *)pPresentDisplayOnly->pSource +
                    ((SIZE_T)y * pPresentDisplayOnly->Pitch) +
                    ((SIZE_T)clipped.left * 4u);
                ULONG rowBytes = (ULONG)(clipped.right - clipped.left) * 4u;
                RtlCopyMemory(dst, src, (SIZE_T)rowBytes);
            }
        }
    } else if (pPresentDisplayOnly->NumMoves == 0) {
        LONG y;
        for (y = 0; y < (LONG)height; y++) {
            PUCHAR dst = frameBuffer + ((SIZE_T)y * destinationPitch);
            CONST UCHAR *src = (CONST UCHAR *)pPresentDisplayOnly->pSource +
                ((SIZE_T)y * pPresentDisplayOnly->Pitch);
            RtlCopyMemory(dst, src, (SIZE_T)width * 4u);
        }
    }

    if (presentCount == 1 || (presentCount % 60) == 0) {
        Bc250DisplayLog("[PresentDisplayOnly #%d] SDMA Blt Status: Completed=%ld, Submitted=%ld, Fallbacks=%ld, HostCpuCopies=%ld\n",
            presentCount,
            g_Bc250SdmaPresent.BltCompletedCount,
            g_Bc250SdmaPresent.BltSubmittedCount,
            g_Bc250SdmaPresent.FallbackCpuCount,
            g_Bc250SdmaPresent.HostCpuCopyCount);
    }

    KeMemoryBarrier();
    Bc250ReleasePresentAccess();
    if (firstPresent) {
        Bc250RecordDisplayProbePassive(
            BC250_PROBE_DMM_PRESENT_STATUS, (ULONG)STATUS_SUCCESS);
    }
    Bc250RecordVsyncProofAfterSuccessfulPresent();
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership(
    CONST PVOID MiniportDeviceContext,
    CONST D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);

    if (DisplayInfo == NULL) return STATUS_INVALID_PARAMETER;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    /* DCN.1 (Lei 5): o BasicDisplay recebe o scanout do GOP, entao o HUBP0
     * tem de voltar a ele antes da passagem de posse. */
    Bc250Dcn2Teardown("POST_DISPLAY_OWNERSHIP");
    Bc250DcnFlipRestoreGop("POST_DISPLAY_OWNERSHIP");
#endif

    if (g_Bc250DisplayState.DisplayAcquired != 0u) {
        RtlCopyMemory(DisplayInfo, &g_Bc250DisplayState.PostDisplayInfo, sizeof(DXGK_DISPLAY_INFORMATION));
        DisplayInfo->TargetId = 0;
    } else {
        DisplayInfo->Width = g_Bc250DisplayState.CurrentMode.Width;
        DisplayInfo->Height = g_Bc250DisplayState.CurrentMode.Height;
        DisplayInfo->Pitch = g_Bc250DisplayState.CurrentMode.Pitch;
        DisplayInfo->ColorFormat = D3DDDIFMT_A8R8G8B8;
        DisplayInfo->PhysicAddress = g_Bc250DisplayState.FrameBufferPhysical;
        DisplayInfo->TargetId = 0;
        DisplayInfo->AcpiId = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSystemDisplayEnable(
    CONST PVOID MiniportDeviceContext,
    CONST D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    UINT *Width,
    UINT *Height,
    D3DDDIFORMAT *ColorFormat)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (Width == NULL || Height == NULL || ColorFormat == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Width = g_Bc250DisplayState.CurrentMode.Width;
    *Height = g_Bc250DisplayState.CurrentMode.Height;
    *ColorFormat = D3DDDIFMT_A8R8G8B8;
    return STATUS_SUCCESS;
}

VOID APIENTRY Bc250DxgkDdiSystemDisplayWrite(
    CONST PVOID MiniportDeviceContext,
    PVOID Source,
    UINT SourceWidth,
    UINT SourceHeight,
    UINT SourceStride,
    UINT PositionX,
    UINT PositionY)
{
    UINT y;
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (Source == NULL || g_Bc250DisplayState.FrameBufferVirtual == NULL ||
        g_Bc250DisplayState.DisplayStarted == 0 ||
        SourceWidth == 0 || SourceHeight == 0 ||
        PositionX >= g_Bc250DisplayState.CurrentMode.Width ||
        PositionY >= g_Bc250DisplayState.CurrentMode.Height ||
        SourceWidth > (MAXULONG / 4u) ||
        SourceStride < SourceWidth * 4u) {
        return;
    }

    for (y = 0; y < SourceHeight; y++) {
        if (y >= g_Bc250DisplayState.CurrentMode.Height - PositionY) break;
        {
            PUCHAR dst = (PUCHAR)g_Bc250DisplayState.FrameBufferVirtual +
                ((SIZE_T)(PositionY + y) * g_Bc250DisplayState.CurrentMode.Pitch) +
                ((SIZE_T)PositionX * 4);
            CONST UCHAR *src = (CONST UCHAR *)Source + ((SIZE_T)y * SourceStride);
            UINT copyWidth = SourceWidth;
            if (copyWidth > g_Bc250DisplayState.CurrentMode.Width - PositionX) {
                copyWidth = g_Bc250DisplayState.CurrentMode.Width - PositionX;
            }
            RtlCopyMemory(dst, src, (SIZE_T)copyWidth * 4u);
        }
    }
    KeMemoryBarrier();
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPointerPosition(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetPointerPosition == NULL ||
        pSetPointerPosition->VidPnSourceId != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    /* DCN.2 item 4 (doc 713): cursor por hardware no HUBP0/DPP0. */
    if (Bc250DisplayDcnCursorReady() != 0u) {
        return Bc250Dcn2CursorSetPosition(pSetPointerPosition);
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetPointerShape(
    CONST HANDLE hAdapter,
    CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetPointerShape == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    /* DCN.2 item 4 (doc 713): forma recusada -> o Windows desenha por software. */
    if (Bc250DisplayDcnCursorReady() != 0u) {
        return Bc250Dcn2CursorSetShape(pSetPointerShape);
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiQueryVidPnHWCapability(
    CONST HANDLE hAdapter,
    DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pVidPnHWCaps == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&pVidPnHWCaps->VidPnHWCaps, sizeof(pVidPnHWCaps->VidPnHWCaps));
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetDisplayPrivateDriverFormat(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT pSetDisplayPrivateDriverFormat)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetDisplayPrivateDriverFormat);
    return STATUS_SUCCESS;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
NTSTATUS APIENTRY Bc250DxgkDdiSetTargetAdjustedColorimetry(
    IN_CONST_HANDLE hAdapter,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    IN DXGK_COLORIMETRY AdjustedColorimetry)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(AdjustedColorimetry);
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED, (ULONG)TargetId);
    Bc250DisplayLog("[SetTargetAdjustedColorimetry] TargetId=%u\n", TargetId);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetTargetContentType(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETTARGETCONTENTTYPE pSetTargetContentTypeArg)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetTargetContentTypeArg == NULL) return STATUS_INVALID_PARAMETER;
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED,
        (ULONG)pSetTargetContentTypeArg->TargetId);
    Bc250DisplayLog("[SetTargetContentType] TargetId=%u, ContentType=%u\n",
        pSetTargetContentTypeArg->TargetId,
        pSetTargetContentTypeArg->ContentType);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DxgkDdiSetTargetGamma(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_PDXGKARG_SETTARGETGAMMA pSetTargetGammaArg)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetTargetGammaArg == NULL) return STATUS_INVALID_PARAMETER;
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED,
        (ULONG)pSetTargetGammaArg->TargetId);
    Bc250DisplayLog("[SetTargetGamma] TargetId=%u\n", pSetTargetGammaArg->TargetId);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
NTSTATUS APIENTRY Bc250DxgkDdiSetTargetAdjustedColorimetry2(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_SETTARGETADJUSTEDCOLORIMETRY2 pArgSetTargetAdjustedColorimetry)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pArgSetTargetAdjustedColorimetry == NULL) return STATUS_INVALID_PARAMETER;
    Bc250ProbeRecord(BC250_PROBE_DMM_CHILD_STATUS_CALLED,
        (ULONG)pArgSetTargetAdjustedColorimetry->TargetId);
    Bc250DisplayLog("[SetTargetAdjustedColorimetry2] TargetId=%u, SdrWhiteLevel=%u\n",
        pArgSetTargetAdjustedColorimetry->TargetId,
        pArgSetTargetAdjustedColorimetry->SdrWhiteLevel);
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
NTSTATUS APIENTRY Bc250DxgkDdiUpdateMonitorLinkInfo(
    IN_CONST_HANDLE hAdapter,
    INOUT_PDXGKARG_UPDATEMONITORLINKINFO pUpdateMonitorLinkInfoArg)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pUpdateMonitorLinkInfoArg == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    Bc250DisplayLog("[UpdateMonitorLinkInfo] TargetId=%u\n",
        pUpdateMonitorLinkInfoArg->VideoPresentTargetId);
    RtlZeroMemory(&pUpdateMonitorLinkInfoArg->MonitorLinkInfo,
        sizeof(pUpdateMonitorLinkInfoArg->MonitorLinkInfo));
    pUpdateMonitorLinkInfoArg->MonitorLinkInfo.Capabilities.Stereo = 0;
    pUpdateMonitorLinkInfoArg->MonitorLinkInfo.Capabilities.WideColorSpace = 0;
    pUpdateMonitorLinkInfoArg->MonitorLinkInfo.Capabilities.HighColorSpace = 0;
    return STATUS_SUCCESS;
}
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
NTSTATUS APIENTRY Bc250DxgkDdiQueryConnectionChange(
    IN_CONST_HANDLE hAdapter,
    IN_PDXGKARG_QUERYCONNECTIONCHANGE pQueryConnectionChange)
{
    if (hAdapter == NULL || pQueryConnectionChange == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_ALREADY_COMPLETE;
}
#endif

NTSTATUS Bc250DisplayPopulateBaseOnlyCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData)
{
    if (InitializationData == NULL) return STATUS_INVALID_PARAMETER;

    /*
     * Enumeracao de children: obrigatoria na DRIVER_INITIALIZATION_DATA de
     * qualquer miniport, inclusive um sem display.  As tres DDIs continuam
     * anunciadas e respondem de forma coerente com zero children quando
     * Bc250DisplaySetChildEnumeration(FALSE) foi chamada.
     */
    InitializationData->DxgkDdiQueryChildRelations = Bc250DxgkDdiQueryChildRelations;
    InitializationData->DxgkDdiQueryChildStatus = Bc250DxgkDdiQueryChildStatus;
    InitializationData->DxgkDdiQueryDeviceDescriptor = Bc250DxgkDdiQueryDeviceDescriptor;

    /* Base do adaptador, independente de haver saida de video. */
    InitializationData->DxgkDdiSetPowerState = Bc250DxgkDdiSetPowerState;
    InitializationData->DxgkDdiQueryInterface = Bc250DxgkDdiQueryInterface;
    InitializationData->DxgkDdiControlEtwLogging = Bc250DxgkDdiControlEtwLogging;
    InitializationData->DxgkDdiNotifyAcpiEvent = Bc250DxgkDdiNotifyAcpiEvent;
    InitializationData->DxgkDdiNotifySurpriseRemoval = Bc250DxgkDdiNotifySurpriseRemoval;

    /*
     * ControlInterrupt/ControlInterrupt2 servem tambem aos tipos de render.
     * Continuam fail-closed: apenas DXGK_INTERRUPT_DISPLAYONLY_VSYNC devolve
     * sucesso, e num perfil sem display esse tipo nunca sera' pedido.
     */
    InitializationData->DxgkDdiControlInterrupt = Bc250DxgkDdiControlInterrupt;
    InitializationData->DxgkDdiControlInterrupt2 = Bc250DxgkDdiControlInterrupt2;

    return STATUS_SUCCESS;
}

NTSTATUS Bc250DisplayPopulateCallbacks(
    _Inout_ DRIVER_INITIALIZATION_DATA *InitializationData)
{
    NTSTATUS status = Bc250DisplayPopulateBaseOnlyCallbacks(InitializationData);
    if (!NT_SUCCESS(status)) return status;

    InitializationData->DxgkDdiIsSupportedVidPn = Bc250DxgkDdiIsSupportedVidPn;
    InitializationData->DxgkDdiRecommendFunctionalVidPn = Bc250DxgkDdiRecommendFunctionalVidPn;
    InitializationData->DxgkDdiEnumVidPnCofuncModality = Bc250DxgkDdiEnumVidPnCofuncModality;
    InitializationData->DxgkDdiSetVidPnSourceAddress = Bc250DxgkDdiSetVidPnSourceAddress;
    InitializationData->DxgkDdiSetVidPnSourceVisibility = Bc250DxgkDdiSetVidPnSourceVisibility;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    InitializationData->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay = Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay;
    InitializationData->DxgkDdiCheckMultiPlaneOverlaySupport = Bc250DxgkDdiCheckMultiPlaneOverlaySupport;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    InitializationData->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2 = Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
    InitializationData->DxgkDdiCheckMultiPlaneOverlaySupport2 = Bc250DxgkDdiCheckMultiPlaneOverlaySupport2;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    InitializationData->DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3 = Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3;
    InitializationData->DxgkDdiCheckMultiPlaneOverlaySupport3 = Bc250DxgkDdiCheckMultiPlaneOverlaySupport3;
    InitializationData->DxgkDdiPostMultiPlaneOverlayPresent = Bc250DxgkDdiPostMultiPlaneOverlayPresent;
#endif
    InitializationData->DxgkDdiCommitVidPn = Bc250DxgkDdiCommitVidPn;
    InitializationData->DxgkDdiUpdateActiveVidPnPresentPath = Bc250DxgkDdiUpdateActiveVidPnPresentPath;
    InitializationData->DxgkDdiRecommendMonitorModes = Bc250DxgkDdiRecommendMonitorModes;
    InitializationData->DxgkDdiRecommendVidPnTopology = Bc250DxgkDdiRecommendVidPnTopology;
    InitializationData->DxgkDdiPresent = Bc250DxgkDdiPresent;
    InitializationData->DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Bc250DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    InitializationData->DxgkDdiSystemDisplayEnable = Bc250DxgkDdiSystemDisplayEnable;
    InitializationData->DxgkDdiSystemDisplayWrite = Bc250DxgkDdiSystemDisplayWrite;
    InitializationData->DxgkDdiSetPointerPosition = Bc250DxgkDdiSetPointerPosition;
    InitializationData->DxgkDdiSetPointerShape = Bc250DxgkDdiSetPointerShape;
    InitializationData->DxgkDdiQueryVidPnHWCapability = Bc250DxgkDdiQueryVidPnHWCapability;
    InitializationData->DxgkDdiGetScanLine = Bc250DxgkDdiGetScanLine;
    InitializationData->DxgkDdiGetChildContainerId = Bc250DxgkDdiGetChildContainerId;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
    InitializationData->DxgkDdiUpdateMonitorLinkInfo = Bc250DxgkDdiUpdateMonitorLinkInfo;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    InitializationData->DxgkDdiQueryConnectionChange = Bc250DxgkDdiQueryConnectionChange;
    InitializationData->DxgkDdiSetTargetAdjustedColorimetry = Bc250DxgkDdiSetTargetAdjustedColorimetry;
    InitializationData->DxgkDdiSetTargetContentType = Bc250DxgkDdiSetTargetContentType;
    InitializationData->DxgkDdiSetTargetGamma = Bc250DxgkDdiSetTargetGamma;
#endif
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
    InitializationData->DxgkDdiSetTargetAdjustedColorimetry2 = Bc250DxgkDdiSetTargetAdjustedColorimetry2;
#endif

    /*
     * ==== DIVIDA TECNICA DELIBERADA E TEMPORARIA — isolamento do Gate 1.5 ===
     *
     * As duas DDIs abaixo sao falsos sucessos conhecidos: ignoram os
     * argumentos e devolvem STATUS_SUCCESS sem efeito. O candidato 3.75 as
     * retirou, e essa retirada foi correta em si — mas foi embarcada NO MESMO
     * candidato que estreou o perfil RenderOnlyLab, junto com um bind de
     * bring-up fisico.  Quando aquela corrida terminou em tela preta e
     * Codigo 39, nao era possivel atribuir a falha a um eixo.
     *
     * Elas voltam aqui para que o 3.76 tenha EXATAMENTE a tabela KMDOD da
     * baseline conhecida, e o unico eixo novo seja o perfil render-only.
     *
     * A remocao volta num candidato proprio, depois que o Gate 1.5 fechar.
     * tools/audit-ddi-noop-callbacks.py carrega a excecao correspondente,
     * nomeada e datada, e reprova se ela for esquecida la' dentro.
     */
    InitializationData->DxgkDdiSetDisplayPrivateDriverFormat = Bc250DxgkDdiSetDisplayPrivateDriverFormat;
    InitializationData->DxgkDdiSetPalette = Bc250DxgkDdiSetPalette;

    /*
     * Texto original da retirada, preservado para o candidato de limpeza:
     *
     * DxgkDdiSetDisplayPrivateDriverFormat NAO e' anunciada.
     *
     * A implementacao existente ignora os dois argumentos e devolve
     * STATUS_SUCCESS sem preencher saida nem alterar estado: um falso
     * positivo puro.  A DDI e' opcional no contrato selecionado — nao ha'
     * formato privado de display para negociar enquanto nao existir
     * allocation primaria propria — entao a alternativa correta e' nao
     * anuncia-la, e nao devolver erro numa DDI que prometemos atender.
     *
     * O simbolo permanece compilado e testado para que a reintroducao, se um
     * dia houver contrato real, seja uma linha aqui e nao uma reescrita.
     *
     * DxgkDdiSetPalette sai pelo mesmo motivo, com uma justificativa mais
     * simples: e' exigida apenas de drivers com modos palettizados, e este
     * miniport expoe somente D3DDDIFMT_A8R8G8B8.
     *
     * DxgkDdiCollectDbgInfo tambem nao entra aqui: a versao com semantica
     * real, Bc250McdmCollectDbgInfo, ja' e' instalada por
     * Bc250McdmPopulateRequiredCallbacks.  Sobrescreve-la pelo stub local
     * apagaria a unica implementacao que valida o buffer.
     */

    return STATUS_SUCCESS;
}
