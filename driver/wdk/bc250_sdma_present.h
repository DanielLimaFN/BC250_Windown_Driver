#ifndef BC250_SDMA_PRESENT_H
#define BC250_SDMA_PRESENT_H

#include <ntddk.h>
#include <dispmprt.h>
#include "../core/bc250_sdma_packet_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_SDMA_LEGACY_RING_OFFSET     0x0FF00000ull
#define BC250_SDMA_VRAM_MC_BASE           0x000450000000ull

#define BC250_SDMA_PRESENT_RING_DWORDS    1024u
#define BC250_SDMA_PRESENT_RING_BYTES     (BC250_SDMA_PRESENT_RING_DWORDS * sizeof(uint32_t))
#define BC250_SDMA_PRESENT_WORK_BYTES     4096u
#define BC250_SDMA_PRESENT_BUFFER_TOTAL   (BC250_SDMA_PRESENT_RING_BYTES + BC250_SDMA_PRESENT_WORK_BYTES)

#define BC250_SDMA_PRESENT_RPTR_OFFSET    0x000u
#define BC250_SDMA_PRESENT_DEST_OFFSET    0x100u
#define BC250_SDMA_PRESENT_WPTR_OFFSET    0x400u
#define BC250_SDMA_PRESENT_FENCE_OFFSET   0x800u
#define BC250_SDMA_PRESENT_FENCE_POISON   0x43DAFFFEu
#define BC250_SDMA_PRESENT_FENCE_INIT     0x1000u

/* Offsets de registradores SDMA0 relativos à base de BAR5 */
#define BC250_SDMA0_REG_BASE_OFFSET       0x04000u
#define BC250_SDMA0_REG_CLK_CTRL          0x049ECu
#define BC250_SDMA0_REG_CNTL              0x049F0u
#define BC250_SDMA0_REG_CHICKEN           0x049F4u
#define BC250_SDMA0_REG_GB_ADDR           0x049F8u
#define BC250_SDMA0_REG_GB_ADDR_READ      0x049FCu
#define BC250_SDMA0_REG_SEM_TIMER         0x04A04u
#define BC250_SDMA0_REG_STATUS            0x04A14u
#define BC250_SDMA0_REG_F32_CNTL          0x04A28u
#define BC250_SDMA0_REG_HDP_FLUSH         0x04A80u
#define BC250_SDMA0_REG_UTCL1_CNTL        0x04AE8u
#define BC250_SDMA0_REG_UTCL1_PAGE        0x04AECu
#define BC250_SDMA0_REG_RB_CNTL           0x04B80u
#define BC250_SDMA0_REG_RB_BASE           0x04B84u
#define BC250_SDMA0_REG_RB_BASE_HI        0x04B88u
#define BC250_SDMA0_REG_RB_RPTR           0x04B8Cu
#define BC250_SDMA0_REG_RB_RPTR_HI        0x04B90u
#define BC250_SDMA0_REG_RB_WPTR           0x04B94u
#define BC250_SDMA0_REG_RB_WPTR_HI        0x04B98u
#define BC250_SDMA0_REG_WPTR_POLL_CNTL    0x04B9Cu
#define BC250_SDMA0_REG_RPTR_ADDR_HI      0x04BA0u
#define BC250_SDMA0_REG_RPTR_ADDR_LO      0x04BA4u
#define BC250_SDMA0_REG_IB_CNTL           0x04BA8u
#define BC250_SDMA0_REG_DOORBELL          0x04BC8u
#define BC250_SDMA0_REG_WPTR_POLL_ADDR_HI 0x04C48u
#define BC250_SDMA0_REG_WPTR_POLL_ADDR_LO 0x04C4Cu
#define BC250_SDMA0_REG_MINOR_PTR         0x04C54u

typedef struct BC250_SDMA_PRESENT_CONTEXT {
    BOOLEAN Initialized;
    BOOLEAN HardwareEnabled;
    BOOLEAN TripodValidated;

    PHYSICAL_ADDRESS MmioBase;
    volatile UCHAR *MmioVirtual;
    ULONG MmioLength;

    PHYSICAL_ADDRESS VramBase;
    volatile UCHAR *VramVirtual;
    ULONG VramLength;

    volatile uint32_t *Ring;
    uint64_t RingGpuAddress;

    volatile uint32_t *Work;
    uint64_t WorkGpuAddress;

    /* Estado de sincronização e fence */
    uint32_t CurrentCursor;
    volatile LONG NextFenceSequence;
    volatile LONG BltSubmittedCount;
    volatile LONG BltCompletedCount;
    volatile LONG FallbackCpuCount;

    /* Métricas de prova do tripé */
    ULONG TripodPolls;
    uint32_t TripodFenceObserved;
    uint32_t TripodMismatches;

    /* GOP Scanout MC address */
    uint64_t ScanoutGpuAddress;
} BC250_SDMA_PRESENT_CONTEXT;

extern BC250_SDMA_PRESENT_CONTEXT g_Bc250SdmaPresent;

NTSTATUS Bc250SdmaPresentInit(
    _In_ PHYSICAL_ADDRESS MmioBar5Physical,
    _In_ PHYSICAL_ADDRESS VramBar0Physical,
    _In_ uint64_t ScanoutGpuMcAddress);

BOOLEAN Bc250SdmaPresentBlt(
    _In_ uint64_t SourceGpuAddress,
    _In_ uint64_t DestGpuAddress,
    _In_ ULONG ByteCount);

void Bc250SdmaPresentTeardown(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SDMA_PRESENT_H */
