#ifndef BC250_VRAM_CPU_PATH_H
#define BC250_VRAM_CPU_PATH_H

/*
 * P1b (doc 602): caminho da CPU ate a VRAM, como o amdgpu numa APU.
 *
 * Upstream (P1, inc/linux_ref/PROVENANCE_P1B_HDP_APU.txt, linux v6.6):
 *  - amdgpu_drv.c: 0x1002:0x13FE = CHIP_CYAN_SKILLFISH | AMD_IS_APU;
 *  - gmc_v10_0_mc_init: em APU x86_64 (sem passthrough)
 *      aper_base = gfxhub get_mc_fb_offset() = GCMC_VM_FB_OFFSET << 24,
 *    isto e', a CPU acessa a VRAM pelo endereco fisico na DRAM, NAO pela BAR
 *    PCI (e mapeia com ioremap_wc);
 *  - amdgpu_device_flush_hdp / amdgpu_device_invalidate_hdp retornam cedo em
 *    APU: sem a BAR no caminho nao ha HDP a esvaziar.
 *
 * Fonte unica: todo mapeamento de VRAM pela CPU no regime passa por
 * Bc250VramCpuTranslate, e o HDP so' roda quando Bc250VramCpuHdpNeeded().
 * Enquanto a P1b nao for armada (ou se o preflight recusar), a traducao e'
 * identidade e o HDP continua -- o caminho antigo pela BAR, inteiro.
 */
#define BC250_P1B_VRAM_CPU_PELA_DRAM 1

__declspec(selectany) volatile LONG g_Bc250VramCpuViaDram = 0;
__declspec(selectany) ULONGLONG g_Bc250VramCpuBarStart = 0ull;
__declspec(selectany) ULONGLONG g_Bc250VramCpuBarBytes = 0ull;
__declspec(selectany) ULONGLONG g_Bc250VramCpuDramBase = 0ull;
__declspec(selectany) volatile LONG64 g_Bc250VramCpuHdpSkipped = 0;

static __forceinline ULONGLONG Bc250VramCpuTranslate(ULONGLONG BarPhysical)
{
    if (g_Bc250VramCpuViaDram != 0 &&
        BarPhysical >= g_Bc250VramCpuBarStart &&
        BarPhysical - g_Bc250VramCpuBarStart < g_Bc250VramCpuBarBytes) {
        return g_Bc250VramCpuDramBase + (BarPhysical - g_Bc250VramCpuBarStart);
    }
    return BarPhysical;
}

static __forceinline BOOLEAN Bc250VramCpuHdpNeeded(void)
{
    if (g_Bc250VramCpuViaDram != 0) {
        InterlockedIncrement64(&g_Bc250VramCpuHdpSkipped);
        return FALSE;
    }
    return TRUE;
}

/*
 * P1b.2 (doc 604): DxgkCbMapMemory so' mapeia faixas dos recursos do adaptador
 * (as BARs). Na Corrida 225 ele RECUSOU o endereco da DRAM e o PfpDirectLoad
 * derrubou o StartDevice (status=6 MAP_FAILED, Codigo 43). Fonte unica de
 * mapeamento de VRAM pela CPU: com a P1b armada e o endereco traduzido,
 * MmMapIoSpaceEx na DRAM (como o ioremap do aper_base no amdgpu); senao, o
 * DxgkCbMapMemory de sempre pela BAR. O unmap decide pela tabela de mapeamentos
 * feitos pela DRAM, nao pelo estado da P1b, que pode mudar entre um e outro
 * (o teardown do display a desarma antes dos demais teardowns).
 */
/*
 * C3 (doc 674): UM so' tipo de memoria para a VRAM vista pela CPU, como o
 * amdgpu em APU (aper_base = get_mc_fb_offset, mapeado write-combined: TTM
 * ttm_write_combined / ioremap_wc). O VidMm ja' mapeia o segmento 1 pela DRAM
 * desde a P1b (Bc250ActiveQueryRuntimeCaps traduz CpuPhysicalStart) e usa WC;
 * o FrameBufferVirtual cobre a abertura inteira em WC; mas os mapeamentos
 * diretos do KMD na DRAM (anel/CSB do GFX, tabela da GART, bloco do MEC, MQD,
 * pagina do walker) eram UC -- a mesma pagina fisica com dois tipos. Com a
 * chave em 1, todo mapeamento traduzido para a DRAM sai WC. Toda escrita do
 * KMD que a GPU le precisa de barreira antes do kick (KeMemoryBarrier/mfence
 * esvazia os buffers WC). Chave em 0 volta ao caminho da 253.
 */
#ifndef BC250_C3_VRAM_DO_WINDOWS_PELA_DRAM
#define BC250_C3_VRAM_DO_WINDOWS_PELA_DRAM 1
#endif
__declspec(selectany) volatile LONG g_Bc250C3DramWcMaps = 0;

/* Tipo do mapeamento de um endereco ja traduzido: WC na DRAM com a C3, senao o pedido. */
static __forceinline BOOLEAN Bc250VramCpuUseWc(ULONGLONG Translated, ULONGLONG BarPhysical)
{
    return (BC250_C3_VRAM_DO_WINDOWS_PELA_DRAM && Translated != BarPhysical) ? TRUE : FALSE;
}

/*
 * C3: os MmMapIoSpace diretos de VRAM do KMD (anel/CSB do GFX, tabela da
 * GART, pagina do walker) passam por aqui com o endereco ja traduzido.
 */
static __inline PVOID Bc250VramCpuMapTranslated(
    _In_ PHYSICAL_ADDRESS Translated,
    _In_ ULONGLONG BarPhysical,
    _In_ SIZE_T Bytes)
{
    if (Bc250VramCpuUseWc((ULONGLONG)Translated.QuadPart, BarPhysical)) {
        PVOID va = MmMapIoSpaceEx(Translated, Bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (va != NULL) InterlockedIncrement(&g_Bc250C3DramWcMaps);
        return va;
    }
    return MmMapIoSpace(Translated, Bytes, MmNonCached);
}

#define BC250_VRAM_CPU_DRAM_MAP_SLOTS 64u

__declspec(selectany) PVOID volatile g_Bc250VramCpuDramMapVa[BC250_VRAM_CPU_DRAM_MAP_SLOTS] = { 0 };
__declspec(selectany) SIZE_T g_Bc250VramCpuDramMapBytes[BC250_VRAM_CPU_DRAM_MAP_SLOTS] = { 0 };
__declspec(selectany) volatile LONG g_Bc250VramCpuDramMaps = 0;

static __inline NTSTATUS Bc250VramCpuMapMemory(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_ ULONGLONG BarPhysical,
    _In_ ULONG Bytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _Out_ PVOID *VirtualAddress)
{
    PHYSICAL_ADDRESS physical;
    ULONGLONG translated = Bc250VramCpuTranslate(BarPhysical);
    PVOID va;
    ULONG protect;
    ULONG slot;

    *VirtualAddress = NULL;
    if (translated == BarPhysical) {
        physical.QuadPart = (LONGLONG)BarPhysical;
        return DxgkInterface->DxgkCbMapMemory(DxgkInterface->DeviceHandle,
            physical, Bytes, FALSE, FALSE, CacheType, VirtualAddress);
    }

    protect = PAGE_READWRITE;
    if (Bc250VramCpuUseWc(translated, BarPhysical)) {
        protect |= PAGE_WRITECOMBINE; /* C3: um so' tipo por pagina fisica */
    } else if (CacheType == MmNonCached) {
        protect |= PAGE_NOCACHE;
    } else if (CacheType == MmWriteCombined) {
        protect |= PAGE_WRITECOMBINE;
    }
    physical.QuadPart = (LONGLONG)translated;
    va = MmMapIoSpaceEx(physical, Bytes, protect);
    if (va == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if ((protect & PAGE_WRITECOMBINE) != 0u) InterlockedIncrement(&g_Bc250C3DramWcMaps);
    for (slot = 0u; slot < BC250_VRAM_CPU_DRAM_MAP_SLOTS; ++slot) {
        if (InterlockedCompareExchangePointer(
                (PVOID volatile *)&g_Bc250VramCpuDramMapVa[slot], va, NULL) == NULL) {
            g_Bc250VramCpuDramMapBytes[slot] = Bytes;
            InterlockedIncrement(&g_Bc250VramCpuDramMaps);
            *VirtualAddress = va;
            return STATUS_SUCCESS;
        }
    }
    /* Tabela cheia: falha fechada, sem mapeamento orfao. */
    MmUnmapIoSpace(va, Bytes);
    return STATUS_INSUFFICIENT_RESOURCES;
}

static __inline NTSTATUS Bc250VramCpuUnmapMemory(
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_ PVOID VirtualAddress)
{
    ULONG slot;

    if (VirtualAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    for (slot = 0u; slot < BC250_VRAM_CPU_DRAM_MAP_SLOTS; ++slot) {
        if (g_Bc250VramCpuDramMapVa[slot] == VirtualAddress) {
            SIZE_T bytes = g_Bc250VramCpuDramMapBytes[slot];
            MmUnmapIoSpace(VirtualAddress, bytes);
            g_Bc250VramCpuDramMapBytes[slot] = 0u;
            InterlockedExchangePointer(
                (PVOID volatile *)&g_Bc250VramCpuDramMapVa[slot], NULL);
            InterlockedDecrement(&g_Bc250VramCpuDramMaps);
            return STATUS_SUCCESS;
        }
    }
    return DxgkInterface->DxgkCbUnmapMemory(DxgkInterface->DeviceHandle,
        VirtualAddress);
}

#endif
