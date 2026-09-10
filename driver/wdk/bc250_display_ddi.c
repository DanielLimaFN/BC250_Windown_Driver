#include "bc250_display_ddi.h"
#include "bc250_driver_caps_contract.h"
#include "bc250_blt_proof_contract.h"
#include "bc250_probe_telemetry.h"
#include "bc250_sdma_present.h"

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
                if (*SegmentId == 1u && *PhysicalAddress > 0ull) {
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
        PhysicalAddress == NULL || SegmentId == NULL || Present->Flags.Blt == 0u) {
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
        PhysicalAddress == NULL || SegmentId == NULL || Present->Flags.Blt == 0u) {
        return FALSE;
    }

    return Bc250DisplayResolveAllocationSlot(
        Context, &Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX],
        PhysicalAddress, SegmentId);
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
    if (g_Bc250DisplayState.FrameBufferVirtual == NULL) return FALSE;
    if (Samples == 0u || ByteCount < sizeof(ULONG)) return FALSE;
    if (ByteOffset > g_Bc250DisplayState.FrameBufferSize) return FALSE;
    if (ByteCount > g_Bc250DisplayState.FrameBufferSize - ByteOffset) return FALSE;

    base = (CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual +
        (SIZE_T)ByteOffset;
    stride = (ByteCount - (ULONG)sizeof(ULONG)) / Samples;
    stride &= ~3u;
    if (stride == 0u) stride = (ULONG)sizeof(ULONG);

    for (i = 0u; i < Samples; ++i) {
        ULONG offset = i * stride;
        ULONG value;
        if (offset > ByteCount - sizeof(ULONG)) break;
        value = *(CONST ULONG *)(base + offset);
        if (value != 0u) ++nonZero;
        hash ^= value;
    }

    *NonZeroCount = nonZero;
    *XorHash = hash;
    return TRUE;
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
    for (index = 0u; index < ByteCount; ++index) {
        if (base[(SIZE_T)SourceOffset + index] !=
            base[(SIZE_T)DestinationOffset + index]) {
            *FirstMismatchOffset = index;
            return FALSE;
        }
    }
    return TRUE;
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
void Bc250DisplayLog(const char *Format, ...)
{
    char buffer[512];
    va_list args;
    int len;

    va_start(args, Format);
    len = _vsnprintf(buffer, sizeof(buffer) - 1, Format, args);
    va_end(args);
    if (len <= 0) return;
    buffer[len] = '\0';

    DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, "[BC250] %s", buffer);

    /*
     * Operacoes de paginacao (VidMm DxgkDdiBuildPagingBuffer e rotinas associadas)
     * rodam em PASSIVE_LEVEL na paging thread do sistema, mas NUNCA podem realizar
     * I/O sincrono de arquivo em disco. Fazer ZwCreateFile/ZwFlushBuffersFile na
     * paging thread causa deadlock circular imediato com NTFS/Cache Manager/Storport.
     * Quando g_Bc250PagingActiveCount > 0, o DbgPrintEx acima permanece ativo para
     * debuggers e ETW, mas o I/O em disco e' suprimido com seguranca.
     */
    if (KeGetCurrentIrql() == PASSIVE_LEVEL &&
        InterlockedCompareExchange(&g_Bc250PagingActiveCount, 0, 0) == 0) {
        UNICODE_STRING logFileName = RTL_CONSTANT_STRING(L"\\DosDevices\\C:\\BC250_DISPLAY_LOG.txt");
        OBJECT_ATTRIBUTES objAttr;
        IO_STATUS_BLOCK ioStatus;
        HANDLE fileHandle = NULL;

        InitializeObjectAttributes(&objAttr, &logFileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(ZwCreateFile(&fileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
            (void)ZwWriteFile(fileHandle, NULL, NULL, NULL, &ioStatus, buffer, (ULONG)len, NULL, NULL);
            (void)ZwFlushBuffersFile(fileHandle, &ioStatus);
            ZwClose(fileHandle);
        }
    }
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

    RtlZeroMemory(SignalInfo, sizeof(*SignalInfo));
    horizontalTotal = (ULONGLONG)g_Bc250DisplayState.CurrentMode.Width +
        BC250_NATIVE_HBLANK;
    verticalTotal = (ULONGLONG)g_Bc250DisplayState.CurrentMode.Height +
        BC250_NATIVE_VBLANK;
    refreshNumerator = BC250_NATIVE_REFRESH_NUM;
    refreshDenominator = BC250_NATIVE_REFRESH_DEN;
    pixelRate = BC250_NATIVE_PIXEL_RATE;

    SignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    SignalInfo->TotalSize.cx = (UINT)horizontalTotal;
    SignalInfo->TotalSize.cy = (UINT)verticalTotal;
    SignalInfo->ActiveSize.cx =
        g_Bc250DisplayState.CurrentMode.Width;
    SignalInfo->ActiveSize.cy =
        g_Bc250DisplayState.CurrentMode.Height;
    SignalInfo->VSyncFreq.Numerator = (UINT)refreshNumerator;
    SignalInfo->VSyncFreq.Denominator = (UINT)refreshDenominator;
    SignalInfo->HSyncFreq.Numerator = BC250_NATIVE_HSYNC_NUM;
    SignalInfo->HSyncFreq.Denominator = BC250_NATIVE_HSYNC_DEN;
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
        if (state->PrimaryAddressValid && state->PrimaryAddress.QuadPart != 0) {
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
        ULONG currentFrame = *g_Bc250DisplayState.OtgFrameCountRegister;
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
        ULONG fbBytes = 256u * 1024u * 1024u; /* 256 MB cobre 100% do BAR 0 fisico de VRAM */
        g_Bc250DisplayState.FrameBufferSize = fbBytes;
        g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
            g_Bc250DisplayState.FrameBufferPhysical,
            fbBytes,
            PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            fbBytes = 128u * 1024u * 1024u;
            g_Bc250DisplayState.FrameBufferSize = fbBytes;
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                g_Bc250DisplayState.FrameBufferPhysical,
                fbBytes,
                PAGE_READWRITE | PAGE_WRITECOMBINE);
        }
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            fbBytes = g_Bc250DisplayState.CurrentMode.Pitch * g_Bc250DisplayState.CurrentMode.Height;
            g_Bc250DisplayState.FrameBufferSize = fbBytes;
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                g_Bc250DisplayState.FrameBufferPhysical,
                fbBytes,
                PAGE_READWRITE | PAGE_WRITECOMBINE);
        }
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpaceEx(
                g_Bc250DisplayState.FrameBufferPhysical,
                g_Bc250DisplayState.FrameBufferSize,
                PAGE_READWRITE | PAGE_NOCACHE);
        }
        if (g_Bc250DisplayState.FrameBufferVirtual == NULL) {
            g_Bc250DisplayState.FrameBufferVirtual = MmMapIoSpace(
                g_Bc250DisplayState.FrameBufferPhysical,
                g_Bc250DisplayState.FrameBufferSize,
                MmWriteCombined);
        }
        Bc250DisplayLog("[DisplayInit] FrameBuffer mapping: Phys=0x%llX, Size=%u, Virt=%p\n",
            (unsigned long long)g_Bc250DisplayState.FrameBufferPhysical.QuadPart,
            g_Bc250DisplayState.FrameBufferSize,
            g_Bc250DisplayState.FrameBufferVirtual);
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
    UNREFERENCED_PARAMETER(HardwareUid);
    UNREFERENCED_PARAMETER(ActionType);

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
    if (Mode == NULL) {
        return FALSE;
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
    g_Bc250DisplayState.PrimaryAddress = pSetVidPnSourceAddress->PrimaryAddress;
    g_Bc250DisplayState.PrimarySegment = pSetVidPnSourceAddress->PrimarySegment;
    g_Bc250DisplayState.hPrimarySourceAllocation =
        pSetVidPnSourceAddress->hAllocation;
    g_Bc250DisplayState.PrimaryAddressValid = TRUE;
    Bc250DisplayLog("=== SetVidPnSourceAddress (SourceId=%u, Segment=%u, Addr=0x%I64X, hAlloc=%p, ContextCount=%u, Flags=0x%08X) ===\n",
        pSetVidPnSourceAddress->VidPnSourceId,
        pSetVidPnSourceAddress->PrimarySegment,
        pSetVidPnSourceAddress->PrimaryAddress.QuadPart,
        pSetVidPnSourceAddress->hAllocation,
        pSetVidPnSourceAddress->ContextCount,
        *(CONST ULONG *)&pSetVidPnSourceAddress->Flags);

    return STATUS_SUCCESS;
}

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
        BC250_NATIVE_REFRESH_NUM,
        BC250_NATIVE_REFRESH_DEN,
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

    if (hContext == NULL) return STATUS_NOT_SUPPORTED;
    if (pPresent == NULL) return STATUS_INVALID_PARAMETER;

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
         * Resolucao da superficie de origem para Blt:
         * 1. pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX] com PhysicalAddress/SegmentId
         *    (Slot 1 / DWM sob WDDM 2.0+).  E' onde o Windows realmente compoe
         *    a imagem da tela (ex: GPUVA 0xAC0000 -> VRAM 0xC10000).
         *    Possui autoridade PRIMEIRA quando resolvida com sucesso.
         *
         * O Primary de SetVidPnSourceAddress fica apenas em telemetria. A corrida
         * 12:23 provou que copiar esse buffer pode exibir dados que nao sao o
         * desktop; sem Slot 1 resolvido pelo snapshot, Present falha fechado.
         */
        UINT32 srcSegmentId = 0;
        BOOLEAN vaResolved = FALSE;
        uint64_t presentPhysical = 0;
        UINT32 presentSegmentId = 0;
        BOOLEAN presentResolved;

        presentResolved = Bc250DisplayResolvePresentSource(
            hContext, pPresent, &presentPhysical, &presentSegmentId);
        if (presentResolved && presentSegmentId == 1u) {
            srcPhysical = presentPhysical;
            srcSegmentId = presentSegmentId;
            vaResolved = TRUE;
        }

        if (vaResolved) {
            Bc250DisplayLog("[Present #%d] Source=%s -> Physical VRAM 0x%llX (Seg=%u); PresentSlot: resolved=%u Phys=0x%llX Seg=%u\n",
                count, (srcPhysical == presentPhysical && presentResolved) ? "PRESENT_SLOT" : "PRIMARY",
                srcPhysical, srcSegmentId,
                presentResolved ? 1u : 0u, presentPhysical, presentSegmentId);
        } else {
            Bc250DisplayLog("[Present #%d] REJECT source contract: Blt=%u, PrimaryValid=%u, PrimarySeg=%u, SlotHAlloc=%p, SlotSeg=%u, SlotWord=0x%llX\n",
                count, isBlt ? 1u : 0u,
                g_Bc250DisplayState.PrimaryAddressValid ? 1u : 0u,
                g_Bc250DisplayState.PrimarySegment,
                pPresent->pAllocationInfo != NULL ?
                    pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation : NULL,
                pPresent->pAllocationInfo != NULL ?
                    (UINT32)pPresent->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].SegmentId : 0u,
                srcVa);
            srcPhysical = 0ull;
        }

        /* Amostragem e telemetria extensiva solicitada */
        if (count <= 10 || (count % 60) == 0) {
            CONST ULONG *dstDwords = (CONST ULONG *)g_Bc250DisplayState.FrameBufferVirtual;
            CONST ULONG *primDwords = NULL;
            CONST ULONG *vaDwords = NULL;
            ULONG subRectCenterSample = 0;

            if (srcPhysical != 0 && (srcPhysical + 16) <= g_Bc250DisplayState.FrameBufferSize) {
                primDwords = (CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcPhysical);
            }
            if (srcVa != 0 && (srcVa + 16) <= g_Bc250DisplayState.FrameBufferSize) {
                vaDwords = (CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcVa);
            }

            if (pPresent->SubRectCnt > 0 && pPresent->pDstSubRects != NULL && srcPhysical != 0) {
                LONG midX = (pPresent->pDstSubRects[0].left + pPresent->pDstSubRects[0].right) / 2;
                LONG midY = (pPresent->pDstSubRects[0].top + pPresent->pDstSubRects[0].bottom) / 2;
                if (midX >= 0 && midY >= 0 &&
                    (ULONG)midX < g_Bc250DisplayState.CurrentMode.Width &&
                    (ULONG)midY < g_Bc250DisplayState.CurrentMode.Height) {
                    ULONG sampleByteOffset = (ULONG)midY * g_Bc250DisplayState.CurrentMode.Pitch + (ULONG)midX * 4u;
                    if ((srcPhysical + sampleByteOffset + 4) <= g_Bc250DisplayState.FrameBufferSize) {
                        subRectCenterSample = *(CONST ULONG *)((CONST UCHAR *)g_Bc250DisplayState.FrameBufferVirtual + srcPhysical + sampleByteOffset);
                    }
                }
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
                Bc250DisplayLog("[Present #%d] SubRect[0]=(%d,%d)-(%d,%d), CenterPixel=0x%08X\n",
                    count,
                    pPresent->pDstSubRects[0].left, pPresent->pDstSubRects[0].top,
                    pPresent->pDstSubRects[0].right, pPresent->pDstSubRects[0].bottom,
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
                    Bc250DisplaySampleSurface(presentPhysical, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread SLOT_SRC  @ 0x%llX: NonZero=%u/256, Xor=%08X (Seg=%u)\n",
                        count, presentPhysical, nz, hx, presentSegmentId);
                    if (nz > 0) {
                        Bc250DisplayLog("[Present #%d] *** DESKTOP REAL DETECTADO NA SUPERFICIE *** NonZero=%u/256, Hash=0x%08X (Phys=0x%llX)\n",
                            count, nz, hx, presentPhysical);
                    } else {
                        Bc250DisplayLog("[Present #%d] AVISO: Superficie de origem 0x%llX esta com todos os pixels em zero (NonZero=0/256)\n",
                            count, presentPhysical);
                    }
                }
                if (dstResolved &&
                    Bc250DisplaySampleSurface(dstPhysical, scanoutBytes, 256u, &nz, &hx)) {
                    Bc250DisplayLog("[Present #%d] Spread SLOT_DST  @ 0x%llX: NonZero=%u/256, Xor=%08X (Seg=%u)\n",
                        count, dstPhysical, nz, hx, dstSegmentId);
                    if (nz > 0) {
                        Bc250DisplayLog("[Present #%d] *** DESKTOP REAL DETECTADO EM SLOT_DST *** NonZero=%u/256, Hash=0x%08X (Phys=0x%llX)\n",
                            count, nz, hx, dstPhysical);
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
         * SEGURANCA FAIL-CLOSED CONTRA TELA PRETA (Lei 6):
         * Se a superficie de origem contem 100% de zeros (ex: logo apos PAGING_FILL
         * ou antes de o DWM compor o primeiro quadro real), NAO podemos copiar zeros
         * para o scanout do UEFI GOP. Copiar zeros apaga a imagem da tela e gera tela
         * preta permanente. Mantemos o scanout GOP intacto e completamos a submissao
         * com sucesso para que o escalonador do Windows continue operando sem TDR.
         */
        {
            ULONG srcNz = 0u, srcHx = 0u;
            BOOLEAN srcOk = Bc250DisplaySampleSurface(srcPhysical, scanoutBytes, 256u, &srcNz, &srcHx);
            UINT64 effectiveSrcPhys = srcPhysical;

            /* Se Slot 1 estiver zerado, verificar se Slot 2 (Destination/SharedPrimary) contem o desktop */
            if (srcOk && srcNz == 0u) {
                UINT64 dstPhysical = 0;
                UINT32 dstSegmentId = 0;
                if (Bc250DisplayResolvePresentDestination(hContext, pPresent, &dstPhysical, &dstSegmentId) &&
                    dstSegmentId == 1u && dstPhysical > 0ull) {
                    ULONG dstNz = 0u, dstHx = 0u;
                    if (Bc250DisplaySampleSurface(dstPhysical, scanoutBytes, 256u, &dstNz, &dstHx) && dstNz > 0u) {
                        effectiveSrcPhys = dstPhysical;
                        srcNz = dstNz;
                        srcHx = dstHx;
                        Bc250DisplayLog("[Present #%d] REDIRECIONAMENTO DE ORIGEM: Slot 1 zerado, usando Slot 2 (0x%llX) com %u/256 pixels ativos!\n",
                            count, dstPhysical, dstNz);
                    }
                }
            }

            if (effectiveSrcPhys != 0 && scanoutBytes > 0 &&
                effectiveSrcPhys <= g_Bc250DisplayState.FrameBufferSize &&
                scanoutBytes <= g_Bc250DisplayState.FrameBufferSize - effectiveSrcPhys) {
                BOOLEAN sdmaDone = FALSE;
                uint64_t srcGpu = g_Bc250SdmaPresent.VramMcStart + effectiveSrcPhys;
                uint64_t dstScanoutGpu = g_Bc250SdmaPresent.ScanoutGpuAddress;

                if (g_Bc250SdmaPresent.HardwareEnabled && dstScanoutGpu != 0 && srcGpu != dstScanoutGpu) {
                    sdmaDone = Bc250SdmaPresentBlt(srcGpu, dstScanoutGpu, scanoutBytes);
                }

                if (sdmaDone &&
                    (count == 1 || count == 5 || count == 10 || count == 25 || count == 45)) {
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
                            /*
                             * A9 (06/09) -- ESTA linha dizia PASS.
                             *
                             * MEDIDO na corrida 2026-09-06 22:42: 63 de 63
                             * amostras com NonZero=0/256 e ainda assim PASS. A
                             * comparacao passava porque origem e destino eram
                             * ambos zero -- ou seja, ela declarava sucesso
                             * exatamente no caso em que a tela estava apagada.
                             * Rotulo de falso sucesso, proibido pela Lei 4.
                             *
                             * Copia fiel de um frame em branco e' INCONCLUSIVA:
                             * nao prova caminho de pixel, e tambem nao e' falha
                             * (o DWM pode legitimamente apresentar preto). Nao
                             * desligamos o SDMA -- apenas nos recusamos a
                             * certificar, e contamos a sequencia de frames em
                             * branco, que e' o sintoma da tela preta.
                             */
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
                        g_Bc250SdmaPresent.HardwareEnabled = FALSE;
                        sdmaDone = FALSE;
                        Bc250DisplayLog("[Present #%d] BYTE_PROOF FAIL: Source=0x%llX Dest=0x0 Bytes=%u FirstMismatch=0x%llX; SDMA disabled\n",
                            count, effectiveSrcPhys, scanoutBytes, mismatchOffset);
                    }
                }

                if (count <= 10 || (count % 60) == 0) {
                    Bc250DisplayLog("[Present #%d] Blt %u bytes (src 0x%llX -> scanout 0x0): SDMA=%s\n",
                        count, scanoutBytes, effectiveSrcPhys, sdmaDone ? "YES" : "FAILED_CLOSED");
                }
                presentStatus = sdmaDone ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
            } else if (vaResolved) {
                Bc250DisplayLog("[Present #%d] REJECT source bounds: Phys=0x%llX, Bytes=%u, Aperture=%u\n",
                    count, effectiveSrcPhys, scanoutBytes, g_Bc250DisplayState.FrameBufferSize);
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
        BOOLEAN sdmaFullDone = FALSE;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        PHYSICAL_ADDRESS srcPhys = MmGetPhysicalAddress((PVOID)pPresentDisplayOnly->pSource);
        if (srcPhys.QuadPart != 0 && g_Bc250SdmaPresent.HardwareEnabled) {
            sdmaFullDone = Bc250SdmaPresentBlt(
                (uint64_t)srcPhys.QuadPart,
                g_Bc250SdmaPresent.ScanoutGpuAddress,
                height * destinationPitch);
        }
#endif
        if (!sdmaFullDone) {
            LONG y;
            for (y = 0; y < (LONG)height; ++y) {
                PUCHAR dst = frameBuffer + ((SIZE_T)y * destinationPitch);
                CONST UCHAR *src =
                    (CONST UCHAR *)pPresentDisplayOnly->pSource +
                    ((SIZE_T)y * pPresentDisplayOnly->Pitch);
                RtlCopyMemory(dst, src, (SIZE_T)width * 4u);
            }
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
                BOOLEAN sdmaRowDone = FALSE;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
                PHYSICAL_ADDRESS rowPhys = MmGetPhysicalAddress((PVOID)src);
                if (rowPhys.QuadPart != 0 && g_Bc250SdmaPresent.HardwareEnabled) {
                    uint64_t dstGpu = g_Bc250SdmaPresent.ScanoutGpuAddress +
                        ((SIZE_T)y * destinationPitch) +
                        ((SIZE_T)clipped.left * 4u);
                    sdmaRowDone = Bc250SdmaPresentBlt(
                        (uint64_t)rowPhys.QuadPart,
                        dstGpu,
                        rowBytes);
                }
#endif
                if (!sdmaRowDone) {
                    RtlCopyMemory(
                        dst, src, (SIZE_T)rowBytes);
                }
            }
        }
    } else if (pPresentDisplayOnly->NumMoves == 0) {
        BOOLEAN sdmaDone = FALSE;
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
        PHYSICAL_ADDRESS srcPhys = MmGetPhysicalAddress((PVOID)pPresentDisplayOnly->pSource);
        if (srcPhys.QuadPart != 0 && g_Bc250SdmaPresent.HardwareEnabled) {
            sdmaDone = Bc250SdmaPresentBlt(
                (uint64_t)srcPhys.QuadPart,
                g_Bc250SdmaPresent.ScanoutGpuAddress,
                height * destinationPitch);
        }
#endif
        if (!sdmaDone) {
            LONG y;
            for (y = 0; y < (LONG)height; y++) {
                PUCHAR dst = frameBuffer + ((SIZE_T)y * destinationPitch);
                CONST UCHAR *src = (CONST UCHAR *)pPresentDisplayOnly->pSource +
                    ((SIZE_T)y * pPresentDisplayOnly->Pitch);
                RtlCopyMemory(dst, src, (SIZE_T)width * 4u);
            }
        }
    }

    if (presentCount == 1 || (presentCount % 60) == 0) {
        Bc250DisplayLog("[PresentDisplayOnly #%d] SDMA Blt Status: Completed=%ld, Submitted=%ld, Fallbacks=%ld\n",
            presentCount,
            g_Bc250SdmaPresent.BltCompletedCount,
            g_Bc250SdmaPresent.BltSubmittedCount,
            g_Bc250SdmaPresent.FallbackCpuCount);
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
