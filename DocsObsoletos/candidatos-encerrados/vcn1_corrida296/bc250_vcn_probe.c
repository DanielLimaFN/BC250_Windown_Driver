/* ===========================================================================
 * bc250_vcn_probe.c — Modulo de Sonda e Ativacao do VCN 2.0 (Video Core Next)
 *
 * Target: AMD BC-250 APU (1002:13FE, GC 10.1.3 / Cyan Skillfish 2 / Oberon)
 * Microcodigo: ps5_vcn.bin (VCN 2.0.3, proveniente dos patches do PS5 Linux)
 *
 * Procedencia dos registradores (Lei 7):
 *   Bases de IP: inc/linux_ref/cyan_skillfish_ip_offset.h
 *     UVD0_BASE__INST0_SEG0 = 0x00007800 (dwords) -> 0x1E000 (bytes)
 *     UVD0_BASE__INST0_SEG1 = 0x00007E00 (dwords) -> 0x1F800 (bytes)
 *   Offsets de bloco: inc/linux_ref/vcn/vcn_2_0_0_offset.h
 *   Sequencia de boot: referencias/ps5-linux-patches/linux.patch (vcn_v2_0_start, vcn_v2_0_ps5_reset)
 * =========================================================================== */

#include "bc250_vcn_probe.h"
#include "bc250_display_ddi.h"
#include "bc250_active_miniport.h"
#include "bc250_vram_cpu_path.h"

#define BC250_VCN_MMIO_MAP_SIZE 0x25000u /* Cobre SEG0 (0x1E000) e SEG1 (0x1F800..0x22000) */

/* Registradores do Segmento 0 (base 0x1E000 em bytes) */
#define BC250_VCN_REG_JPEG_SYS_INT_STATUS          0x0001E708u /* (0x7800 + 0x01c2) * 4 */

/* Registradores do Segmento 1 (base 0x1F800 em bytes) */
#define BC250_VCN_REG_UVD_POWER_STATUS             0x0001FB10u /* (0x7E00 + 0x00c4) * 4 */
#define BC250_VCN_REG_UVD_SCRATCH2                 0x0001FB58u /* (0x7E00 + 0x00d6) * 4 */
#define BC250_VCN_REG_UVD_SOFT_RESET2              0x0001FF98u /* (0x7E00 + 0x01e6) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET0       0x00020108u /* (0x7E00 + 0x0242) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_SIZE0         0x0002010Cu /* (0x7E00 + 0x0243) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET1       0x00020110u /* (0x7E00 + 0x0244) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_SIZE1         0x00020114u /* (0x7E00 + 0x0245) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET2       0x00020118u /* (0x7E00 + 0x0246) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CACHE_SIZE2         0x0002011Cu /* (0x7E00 + 0x0247) * 4 */
#define BC250_VCN_REG_UVD_VCPU_CNTL                0x00020160u /* (0x7E00 + 0x0258) * 4 */
#define BC250_VCN_REG_UVD_SOFT_RESET               0x00020180u /* (0x7E00 + 0x0260) * 4 */
#define BC250_VCN_REG_UVD_RBC_RB_RPTR              0x00020190u /* (0x7E00 + 0x0264) * 4 */
#define BC250_VCN_REG_UVD_RBC_RB_WPTR              0x00020194u /* (0x7E00 + 0x0265) * 4 */
#define BC250_VCN_REG_UVD_RBC_RB_CNTL              0x000201A4u /* (0x7E00 + 0x0269) * 4 */
#define BC250_VCN_REG_UVD_STATUS                   0x000201BCu /* (0x7E00 + 0x026f) * 4 */
#define BC250_VCN_REG_UVD_RB_RPTR                  0x00020FA4u /* (0x7E00 + 0x05e9) * 4 */
#define BC250_VCN_REG_UVD_RB_WPTR                  0x00020FA8u /* (0x7E00 + 0x05ea) * 4 */
#define BC250_VCN_REG_UVD_RB_RPTR2                 0x00020FB4u /* (0x7E00 + 0x05ed) * 4 */
#define BC250_VCN_REG_UVD_RB_WPTR2                 0x00020FB8u /* (0x7E00 + 0x05ee) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE_BAR_HIGH  0x00021078u /* (0x7E00 + 0x061e) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE_BAR_LOW   0x0002107Cu /* (0x7E00 + 0x061f) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE1_BAR_HIGH 0x00021080u /* (0x7E00 + 0x0620) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE1_BAR_LOW  0x00021084u /* (0x7E00 + 0x0621) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE2_BAR_HIGH 0x00021088u /* (0x7E00 + 0x0622) * 4 */
#define BC250_VCN_REG_UVD_LMI_VCPU_CACHE2_BAR_LOW  0x0002108Cu /* (0x7E00 + 0x0623) * 4 */

/* Mascaras canonicas */
#define BC250_UVD_VCPU_CNTL_CLK_EN_MASK            0x00000200u
#define BC250_UVD_SOFT_RESET_VCPU_MASK             0x00000008u
#define BC250_UVD_RBC_RB_CNTL_NO_FETCH_MASK        0x00010000u
#define BC250_UVD_RBC_RB_CNTL_RPTR_WR_EN_MASK      0x10000000u

/* Geometria do Firmware VCN do PS5 */
#define BC250_PS5_VCN_FILE_BYTES                   404544u
#define BC250_PS5_VCN_PAYLOAD_OFFSET               256u
#define BC250_PS5_VCN_PAYLOAD_BYTES                404288u
#define BC250_PS5_VCN_STACK_SIZE                   65536u   /* 64 KiB */
#define BC250_PS5_VCN_CONTEXT_SIZE                 262144u  /* 256 KiB */
#define BC250_PS5_VCN_TOTAL_BUFFER_BYTES           1048576u /* 1 MiB alinhado */
#define BC250_VCN_POOL_TAG                         'nCvB'

static PVOID g_VcnContiguousBuffer = NULL;

static inline uint32_t VcnMmioRead(volatile UCHAR *Base, uint32_t Offset)
{
    return *(volatile uint32_t *)(Base + Offset);
}

static inline void VcnMmioWrite(volatile UCHAR *Base, uint32_t Offset, uint32_t Value)
{
    *(volatile uint32_t *)(Base + Offset) = Value;
}

static inline void VcnMmioRmw(volatile UCHAR *Base, uint32_t Offset, uint32_t Mask, uint32_t Bits)
{
    uint32_t cur = VcnMmioRead(Base, Offset);
    VcnMmioWrite(Base, Offset, (cur & ~Mask) | (Bits & Mask));
}

static NTSTATUS Bc250VcnLoadFirmware(
    _Out_ PVOID *FirmwareBuffer,
    _Out_ uint32_t *PayloadBytes)
{
    UNICODE_STRING uPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE handle = NULL;
    FILE_STANDARD_INFORMATION fsi;
    NTSTATUS st;
    PVOID buf = NULL;
    LARGE_INTEGER byteOffset;

    if (FirmwareBuffer == NULL || PayloadBytes == NULL) return STATUS_INVALID_PARAMETER;
    *FirmwareBuffer = NULL;
    *PayloadBytes = 0u;

    RtlInitUnicodeString(&uPath, L"\\SystemRoot\\System32\\drivers\\ps5_vcn.bin");
    InitializeObjectAttributes(&oa, &uPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    st = ZwOpenFile(&handle, FILE_GENERIC_READ | SYNCHRONIZE, &oa, &iosb,
                    FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(st)) {
        Bc250DisplayLog("[VCN_PROBE] Firmware ps5_vcn.bin nao encontrado em System32\\drivers (st=0x%08X)\n", st);
        return st;
    }

    st = ZwQueryInformationFile(handle, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
    if (!NT_SUCCESS(st) || fsi.EndOfFile.QuadPart < (LONGLONG)BC250_PS5_VCN_FILE_BYTES) {
        Bc250DisplayLog("[VCN_PROBE] Tamanho invalido de ps5_vcn.bin: %lld bytes (esperado >= %u)\n",
            fsi.EndOfFile.QuadPart, BC250_PS5_VCN_FILE_BYTES);
        ZwClose(handle);
        return STATUS_FILE_INVALID;
    }

    buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, BC250_PS5_VCN_FILE_BYTES, BC250_VCN_POOL_TAG);
    if (buf == NULL) {
        ZwClose(handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    byteOffset.QuadPart = 0;
    st = ZwReadFile(handle, NULL, NULL, NULL, &iosb, buf, BC250_PS5_VCN_FILE_BYTES, &byteOffset, NULL);
    ZwClose(handle);
    if (!NT_SUCCESS(st) || iosb.Information < BC250_PS5_VCN_FILE_BYTES) {
        Bc250DisplayLog("[VCN_PROBE] Falha ao ler ps5_vcn.bin (st=0x%08X, lidos=%llu)\n",
            st, (unsigned long long)iosb.Information);
        ExFreePoolWithTag(buf, BC250_VCN_POOL_TAG);
        return STATUS_UNSUCCESSFUL;
    }

    *FirmwareBuffer = buf;
    *PayloadBytes = BC250_PS5_VCN_PAYLOAD_BYTES;
    return STATUS_SUCCESS;
}

NTSTATUS Bc250VcnProbeExecute(
    _In_ PHYSICAL_ADDRESS MmioPhysical,
    _In_opt_ void *Context,
    _In_ ULONG Mode)
{
    volatile UCHAR *mmio = NULL;
    uint32_t statusInitial, powerStatus, vcpuCntl;
    PVOID fwFile = NULL;
    uint32_t fwBytes = 0u;
    NTSTATUS st;
    BC250_DXGK_ACTIVE_CONTEXT *active = (BC250_DXGK_ACTIVE_CONTEXT *)Context;
    uint64_t vcnBufferMc = 0ull;
    PVOID vcnBufferCpu = NULL;
    BC250_VRAM_TOKEN vramToken;
    BOOLEAN isVramAlloc = FALSE;
    uint32_t i;
    uint32_t fwSizeAligned;

    RtlZeroMemory(&vramToken, sizeof(vramToken));

    if (MmioPhysical.QuadPart == 0) return STATUS_INVALID_PARAMETER;

    /*
     * ACHADO DE SILICIO (Corrida 296, doc 757):
     * Na APU BC-250 (Cyan Skillfish 2 / Oberon), o bloco VCN 2.0 vem sob
     * Power Gating e Clock Gating completos pelo SMU vindo do UEFI GOP.
     * Qualquer leitura MMIO direta (VcnMmioRead) nos registradores de UVD/VCN
     * sem previo ungate pelo SMU (smu_set_power_gate / AMD_PG_STATE_UNGATE)
     * causa TARGET ABORT no Data Fabric / PCIe (RESET_REASON bit 30: a parity error occurred)
     * e reinicio instantaneo da maquina.
     * O modulo permanece bloqueado ate a integracao de mensagens ao SMU.
     */
    if (Mode != 0x554E4754u) {
        Bc250DisplayLog("[VCN_PROBE] BLOQUEADO: VCN requer ungate de clock/energia via SMU antes de MMIO (Corrida 296 / bit 30)\n");
        return STATUS_SUCCESS;
    }

    mmio = (volatile UCHAR *)MmMapIoSpace(MmioPhysical, BC250_VCN_MMIO_MAP_SIZE, MmNonCached);
    if (mmio == NULL) {
        Bc250DisplayLog("[VCN_PROBE] Falha ao mapear MMIO para sonda VCN\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Leitura passiva inicial */
    statusInitial = VcnMmioRead(mmio, BC250_VCN_REG_UVD_STATUS);
    powerStatus   = VcnMmioRead(mmio, BC250_VCN_REG_UVD_POWER_STATUS);
    vcpuCntl      = VcnMmioRead(mmio, BC250_VCN_REG_UVD_VCPU_CNTL);

    Bc250DisplayLog("[VCN_PROBE] ESTADO INICIAL: STATUS=0x%08X, POWER_STATUS=0x%08X, VCPU_CNTL=0x%08X (Mode=%u)\n",
        statusInitial, powerStatus, vcpuCntl, Mode);

    if (Mode < 2u) {
        MmUnmapIoSpace((PVOID)mmio, BC250_VCN_MMIO_MAP_SIZE);
        return STATUS_SUCCESS;
    }

    /* Modo 2: Sequencia de ativacao do VCN do PS5 */
    st = Bc250VcnLoadFirmware(&fwFile, &fwBytes);
    if (!NT_SUCCESS(st) || fwFile == NULL) {
        Bc250DisplayLog("[VCN_PROBE] Ativacao abortada: sem binario de firmware valido\n");
        MmUnmapIoSpace((PVOID)mmio, BC250_VCN_MMIO_MAP_SIZE);
        return STATUS_SUCCESS;
    }

    /* Tenta alocar na VRAM GDDR6 se o alocador do adaptador estiver pronto */
    if (active != NULL && active->Signature == BC250_ACTIVE_SIGNATURE &&
        active->GlobalMmuAllocator.TotalBytes > 0ull) {
        const BC250_RESOLVED_BAR_RESOURCE *vramBar = NULL;
        uint32_t bIdx;
        for (bIdx = 0; bIdx < active->Core.ResolvedBarCount; ++bIdx) {
            if (active->Core.ResolvedBars[bIdx].Index == BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
                vramBar = &active->Core.ResolvedBars[bIdx];
                break;
            }
        }

        if (vramBar != NULL) {
            BC250_VRAM_REGION region;
            BC250_VRAM_STATUS vrStatus = Bc250VramAllocate(
                &active->GlobalMmuAllocator,
                BC250_PS5_VCN_TOTAL_BUFFER_BYTES,
                65536ull,
                BC250_VRAM_ROLE_FIRMWARE,
                BC250_VRAM_ALLOC_CPU_VISIBLE | BC250_VRAM_ALLOC_TOP_DOWN,
                &vramToken);
            if (vrStatus == BC250_VRAM_OK &&
                Bc250VramGetRegion(&active->GlobalMmuAllocator, vramToken, &region) == BC250_VRAM_OK) {
                PHYSICAL_ADDRESS vramPhys;
                uint64_t vramMcBase = (active->Core.TopologyEvidence.AddressSpace.VramMcStart != 0ull)
                    ? active->Core.TopologyEvidence.AddressSpace.VramMcStart
                    : 0x000000F400000000ull;
                vramPhys.QuadPart = (LONGLONG)Bc250VramCpuTranslate(vramBar->Start + region.Offset);
                vcnBufferCpu = MmMapIoSpace(vramPhys, BC250_PS5_VCN_TOTAL_BUFFER_BYTES, MmNonCached);
                if (vcnBufferCpu != NULL) {
                    vcnBufferMc = vramMcBase + region.Offset;
                    isVramAlloc = TRUE;
                    Bc250DisplayLog("[VCN_PROBE] Buffer alocado na VRAM fisica: MC=0x%llX (Offset=0x%llX)\n",
                        (unsigned long long)vcnBufferMc, (unsigned long long)region.Offset);
                }
            }
        }
    }

    /* Fallback para memoria contigua do sistema se VRAM falhar */
    if (vcnBufferCpu == NULL) {
        PHYSICAL_ADDRESS low = {0}, high, bound = {0};
        high.QuadPart = 0x0000000FFFFFFFFFull;
        g_VcnContiguousBuffer = MmAllocateContiguousMemorySpecifyCache(
            BC250_PS5_VCN_TOTAL_BUFFER_BYTES, low, high, bound, MmNonCached);
        if (g_VcnContiguousBuffer != NULL) {
            PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(g_VcnContiguousBuffer);
            vcnBufferCpu = g_VcnContiguousBuffer;
            vcnBufferMc = (uint64_t)pa.QuadPart;
            Bc250DisplayLog("[VCN_PROBE] Buffer alocado na DRAM contigua do sistema: PA=0x%llX\n",
                (unsigned long long)vcnBufferMc);
        }
    }

    if (vcnBufferCpu == NULL) {
        Bc250DisplayLog("[VCN_PROBE] Falha ao alocar buffer para o VCN\n");
        ExFreePoolWithTag(fwFile, BC250_VCN_POOL_TAG);
        MmUnmapIoSpace((PVOID)mmio, BC250_VCN_MMIO_MAP_SIZE);
        return STATUS_SUCCESS;
    }

    /* Copia o payload do firmware para o buffer (pulando o header de 256 bytes) */
    RtlZeroMemory(vcnBufferCpu, BC250_PS5_VCN_TOTAL_BUFFER_BYTES);
    RtlCopyMemory(vcnBufferCpu, (const UCHAR *)fwFile + BC250_PS5_VCN_PAYLOAD_OFFSET, fwBytes);
    ExFreePoolWithTag(fwFile, BC250_VCN_POOL_TAG);
    fwFile = NULL;
    KeMemoryBarrier();

    /* =======================================================================
     * Sequencia de ungate e reset do PS5 Linux (vcn_v2_0_start / vcn_v2_0_ps5_reset)
     * ======================================================================= */
    Bc250DisplayLog("[VCN_PROBE] Aplicando sequencia de boot do PS5 Linux no hardware...\n");

    /* 1. Ungate de energia */
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_POWER_STATUS,
        VcnMmioRead(mmio, BC250_VCN_REG_UVD_POWER_STATUS) & 2u);

    /* 2. Soft reset na VCPU */
    VcnMmioRmw(mmio, BC250_VCN_REG_UVD_SOFT_RESET,
        BC250_UVD_SOFT_RESET_VCPU_MASK, BC250_UVD_SOFT_RESET_VCPU_MASK);

    /* 3. Reset estendido canonico do PS5 (vcn_v2_0_ps5_reset) */
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_SOFT_RESET, 0x0003FFFFu);
    KeStallExecutionProcessor(20u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_SOFT_RESET2, 1u);
    KeStallExecutionProcessor(10u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_SOFT_RESET2, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_SOFT_RESET, BC250_UVD_SOFT_RESET_VCPU_MASK);
    KeStallExecutionProcessor(20u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_SCRATCH2, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_STATUS, 0u);

    /* 4. Reset e inicializacao dos aneis de comando */
    VcnMmioRmw(mmio, BC250_VCN_REG_UVD_RBC_RB_CNTL,
        BC250_UVD_RBC_RB_CNTL_NO_FETCH_MASK | BC250_UVD_RBC_RB_CNTL_RPTR_WR_EN_MASK,
        BC250_UVD_RBC_RB_CNTL_NO_FETCH_MASK | BC250_UVD_RBC_RB_CNTL_RPTR_WR_EN_MASK);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RBC_RB_RPTR, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RBC_RB_WPTR, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RB_RPTR, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RB_WPTR, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RB_RPTR2, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_RB_WPTR2, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_JPEG_SYS_INT_STATUS, 0u);

    /* 5. Configuracao dos caches de instrucoes da VCPU */
    fwSizeAligned = (fwBytes + 4095u) & ~4095u;

    /* Cache 0: Firmware */
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE_BAR_LOW, (uint32_t)(vcnBufferMc & 0xFFFFFFFFull));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE_BAR_HIGH, (uint32_t)(vcnBufferMc >> 32));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET0, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_SIZE0, fwSizeAligned);

    /* Cache 1: Stack da VCPU */
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE1_BAR_LOW, (uint32_t)((vcnBufferMc + fwSizeAligned) & 0xFFFFFFFFull));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE1_BAR_HIGH, (uint32_t)((vcnBufferMc + fwSizeAligned) >> 32));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET1, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_SIZE1, BC250_PS5_VCN_STACK_SIZE);

    /* Cache 2: Contexto da VCPU */
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE2_BAR_LOW, (uint32_t)((vcnBufferMc + fwSizeAligned + BC250_PS5_VCN_STACK_SIZE) & 0xFFFFFFFFull));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_LMI_VCPU_CACHE2_BAR_HIGH, (uint32_t)((vcnBufferMc + fwSizeAligned + BC250_PS5_VCN_STACK_SIZE) >> 32));
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_OFFSET2, 0u);
    VcnMmioWrite(mmio, BC250_VCN_REG_UVD_VCPU_CACHE_SIZE2, BC250_PS5_VCN_CONTEXT_SIZE);

    /* 6. Desassere o soft reset da VCPU */
    VcnMmioRmw(mmio, BC250_VCN_REG_UVD_SOFT_RESET, BC250_UVD_SOFT_RESET_VCPU_MASK, 0u);

    /* 7. Habilita o clock da VCPU */
    VcnMmioRmw(mmio, BC250_VCN_REG_UVD_VCPU_CNTL,
        BC250_UVD_VCPU_CNTL_CLK_EN_MASK, BC250_UVD_VCPU_CNTL_CLK_EN_MASK);

    /* 8. Polling do status de prontidao (bit 1 / 0x2 em mmUVD_STATUS) */
    {
        uint32_t bootStatus = 0u;
        BOOLEAN bootOk = FALSE;
        for (i = 0u; i < 100u; ++i) {
            bootStatus = VcnMmioRead(mmio, BC250_VCN_REG_UVD_STATUS);
            if ((bootStatus & 2u) != 0u) {
                bootOk = TRUE;
                break;
            }
            KeStallExecutionProcessor(10000u); /* 10 ms por iteracao, teto 1 s */
        }

        if (bootOk) {
            Bc250DisplayLog("[VCN_PROBE] *** SUCESSO TOTAL: Firmware VCN do PS5 ACORDOU NO SILICIO! *** (STATUS=0x%08X, voltas=%u)\n",
                bootStatus, i);
        } else {
            Bc250DisplayLog("[VCN_PROBE] TIMEOUT: Firmware VCN nao sinalizou prontidao no prazo de 1s (STATUS=0x%08X, voltas=%u)\n",
                bootStatus, i);
        }
    }

    /* Teardown limpo do mapeamento temporario */
    if (isVramAlloc && vcnBufferCpu != NULL) {
        MmUnmapIoSpace(vcnBufferCpu, BC250_PS5_VCN_TOTAL_BUFFER_BYTES);
    }
    MmUnmapIoSpace((PVOID)mmio, BC250_VCN_MMIO_MAP_SIZE);
    return STATUS_SUCCESS;
}

void Bc250VcnProbeTeardown(void)
{
    if (g_VcnContiguousBuffer != NULL) {
        MmFreeContiguousMemory(g_VcnContiguousBuffer);
        g_VcnContiguousBuffer = NULL;
    }
}
