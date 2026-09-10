#include "bc250_active_miniport.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "bc250_sdma_present.h"

#define BC250_SDMA_WORK_PAGE_BYTES 4096u
#define BC250_SDMA_WORK_BYTES (4u * BC250_SDMA_WORK_PAGE_BYTES)
#define BC250_SDMA_LAYOUT_BYTES (5u * BC250_SDMA_WORK_PAGE_BYTES)
#define BC250_SDMA_LEGACY_RING_OFFSET 0x0FF00000ull
#define BC250_SDMA_LEGACY_WORK_OFFSET 0x0FF01000ull
#define BC250_SDMA_RING_DWORDS 1024u
#define BC250_SDMA_WORK_DWORDS (BC250_SDMA_WORK_BYTES / sizeof(uint32_t))
#define BC250_SDMA_RPTR_OFFSET 0u
#define BC250_SDMA_WPTR_OFFSET 0x1000u
#define BC250_SDMA_FENCE_OFFSET 0x2000u
#define BC250_SDMA_DEST_OFFSET 0x3000u
#define BC250_SDMA_FENCE_POISON 0x43DAFFFEu
#define BC250_SDMA_FENCE_VALUE 0xBC250123u
#define BC250_SDMA_RING_POOL_TAG 'rSdB'

#define R_SEM_TIMER 0x04A04u
#define R_CLK_CTRL 0x049ECu
#define R_CNTL 0x049F0u
#define R_CHICKEN 0x049F4u
#define R_GB_ADDR 0x049F8u
#define R_GB_ADDR_READ 0x049FCu
#define R_STATUS 0x04A14u
#define R_STATUS1 0x04A18u
#define R_STATUS2 0x04A60u
#define R_UTCL1_RD_STATUS 0x04A78u
#define R_UTCL1_WR_STATUS 0x04A7Cu
#define R_UTCL1_RD_XNACK0 0x04A8Cu
#define R_UTCL1_RD_XNACK1 0x04A90u
#define R_UTCL1_WR_XNACK0 0x04A94u
#define R_UTCL1_WR_XNACK1 0x04A98u
#define R_UTCL1_TIMEOUT 0x04A9Cu
#define R_STATUS3 0x04AB0u
#define R_ERROR_LOG 0x04AC0u
#define R_F32_CNTL 0x04A28u
#define R_UTCL1_CNTL 0x04A70u
#define R_UTCL1_PAGE 0x04AA0u
#define R_RB_CNTL 0x04B80u
#define R_RB_BASE 0x04B84u
#define R_RB_BASE_HI 0x04B88u
#define R_RB_RPTR 0x04B8Cu
#define R_RB_RPTR_HI 0x04B90u
#define R_RB_WPTR 0x04B94u
#define R_RB_WPTR_HI 0x04B98u
#define R_WPTR_POLL_CNTL 0x04B9Cu
#define R_RPTR_ADDR_HI 0x04BA0u
#define R_RPTR_ADDR_LO 0x04BA4u
#define R_IB_CNTL 0x04BA8u
#define R_DOORBELL 0x04BC8u
#define R_WPTR_POLL_ADDR_HI 0x04C48u
#define R_WPTR_POLL_ADDR_LO 0x04C4Cu
#define R_MINOR_PTR 0x04C54u
#define R_HDP_INVALIDATE 0x03FC4u
#define R_HDP_MISC 0x03FCCu
#define R_HDP_FLUSH 0x04A80u

#define F_RB_ENABLE 0x00000001u
#define M_RB_SIZE 0x0000003Eu
#define F_RPTR_WB 0x00001000u
#define F_UTC_L1 0x00000002u
#define F_MIDCMD 0x00000020u
#define F_AUTO_CTXSW 0x00040000u
#define F_DOORBELL_ENABLE 0x10000000u
#define F_IB_ENABLE 0x00000001u
#define F_MINOR_PTR 0x00000001u
#define F_POLL_ENABLE 0x00000004u
#define F_F32_HALT 0x00000001u

/*
 * Este modulo e' o caminho provado em silicio (Item 63).  bc250_sdma_present.c
 * reimplementa a mesma sequencia para o blt de apresentacao e, ate a corrida
 * 09:04, tinha divergido em cinco offsets/mascaras sem que nada acusasse.
 * As afirmacoes abaixo travam as duas copias em tempo de compilacao: qualquer
 * divergencia futura passa a ser erro de build, nao surpresa no silicio.
 *
 * Nenhum comportamento deste modulo muda -- sao apenas asserts.
 */
C_ASSERT(R_UTCL1_CNTL == BC250_SDMA0_REG_UTCL1_CNTL);
C_ASSERT(R_UTCL1_PAGE == BC250_SDMA0_REG_UTCL1_PAGE);
C_ASSERT(R_HDP_INVALIDATE == BC250_SDMA0_REG_HDP_INVALIDATE);
C_ASSERT(R_HDP_MISC == BC250_SDMA0_REG_HDP_MISC);
C_ASSERT(R_HDP_FLUSH == BC250_SDMA0_REG_HDP_FLUSH);
C_ASSERT(R_STATUS1 == BC250_SDMA0_REG_STATUS1);
C_ASSERT(R_ERROR_LOG == BC250_SDMA0_REG_ERROR_LOG);
C_ASSERT(R_RB_CNTL == BC250_SDMA0_REG_RB_CNTL);
C_ASSERT(R_RB_BASE == BC250_SDMA0_REG_RB_BASE);
C_ASSERT(R_DOORBELL == BC250_SDMA0_REG_DOORBELL);
C_ASSERT(R_MINOR_PTR == BC250_SDMA0_REG_MINOR_PTR);
C_ASSERT(R_WPTR_POLL_CNTL == BC250_SDMA0_REG_WPTR_POLL_CNTL);
/*
 * A janela mapeada por bc250_sdma_present.c precisa cobrir tanto o bloco HDP
 * quanto o do SDMA0.
 */
C_ASSERT(R_HDP_INVALIDATE >= BC250_SDMA0_MMIO_WINDOW_START);
C_ASSERT(R_MINOR_PTR <
    BC250_SDMA0_MMIO_WINDOW_START + BC250_SDMA0_MMIO_WINDOW_BYTES);

static const uint32_t g_Bc250SdmaRegisters[] = {
    R_SEM_TIMER, R_CNTL, R_CHICKEN, R_GB_ADDR, R_GB_ADDR_READ,
    R_F32_CNTL, R_UTCL1_CNTL, R_UTCL1_PAGE, R_CLK_CTRL,
    R_RB_CNTL, R_RB_BASE, R_RB_BASE_HI, R_RB_RPTR, R_RB_RPTR_HI,
    R_RB_WPTR, R_RB_WPTR_HI, R_WPTR_POLL_CNTL, R_RPTR_ADDR_HI,
    R_RPTR_ADDR_LO, R_IB_CNTL, R_DOORBELL, R_MINOR_PTR,
    R_WPTR_POLL_ADDR_HI, R_WPTR_POLL_ADDR_LO, R_HDP_INVALIDATE,
    R_HDP_MISC, R_HDP_FLUSH
};

#define BC250_SDMA_REGISTER_COUNT \
    (sizeof(g_Bc250SdmaRegisters) / sizeof(g_Bc250SdmaRegisters[0]))

#if defined(BC250_ENABLE_DXGK_REGISTRATION)
static uint32_t Bc250SdmaRead(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t *Value)
{
    return Bc250MmioTopologyRead32(
        &Context->Core.MmioTransport, Offset, Value);
}

static uint32_t Bc250SdmaWrite(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset, uint32_t Value)
{
    return Bc250MmioTopologyWrite32(
        &Context->Core.MmioTransport, Offset, Value);
}

static uint32_t Bc250SdmaGolden(
    BC250_DXGK_ACTIVE_CONTEXT *Context, uint32_t Offset,
    uint32_t Mask, uint32_t OrValue)
{
    uint32_t value;
    if (!Bc250SdmaRead(Context, Offset, &value)) return 0u;
    value = (value & ~Mask) | (OrValue & Mask);
    return Bc250SdmaWrite(Context, Offset, value);
}

static uint32_t Bc250SdmaHdp(
    BC250_DXGK_ACTIVE_CONTEXT *Context)
{
    uint32_t misc, value;
    if (!Bc250SdmaRead(Context, R_HDP_MISC, &misc) ||
        !Bc250SdmaWrite(Context, R_HDP_FLUSH, 0u) ||
        !Bc250SdmaRead(Context, R_HDP_FLUSH, &value) ||
        !Bc250SdmaWrite(Context, R_HDP_INVALIDATE, 1u) ||
        !Bc250SdmaRead(Context, R_HDP_INVALIDATE, &value) ||
        !Bc250SdmaWrite(Context, R_HDP_MISC, misc | 1u) ||
        !Bc250SdmaRead(Context, R_HDP_MISC, &value) ||
        !Bc250SdmaWrite(Context, R_HDP_MISC, misc & ~1u) ||
        !Bc250SdmaRead(Context, R_HDP_MISC, &value)) return 0u;
    return 1u;
}

static void Bc250SdmaCopyFromDevice(
    uint32_t *Destination, volatile uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        Destination[i] = READ_REGISTER_ULONG((volatile ULONG *)&Source[i]);
}

static void Bc250SdmaCopyToDevice(
    volatile uint32_t *Destination, const uint32_t *Source, uint32_t Dwords)
{
    uint32_t i;
    for (i = 0u; i < Dwords; ++i)
        WRITE_REGISTER_ULONG((volatile ULONG *)&Destination[i], Source[i]);
    KeMemoryBarrier();
}

static uint32_t Bc250SdmaDeviceRead(volatile uint32_t *Address)
{
    return READ_REGISTER_ULONG((volatile ULONG *)Address);
}

static void Bc250SdmaDeviceWrite(volatile uint32_t *Address, uint32_t Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)Address, Value);
}
#endif

BC250_SDMA_RING_WORK_STATUS Bc250ActiveRunSdmaRingWork(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SDMA_RING_WORK_EVIDENCE *Evidence)
{
#if defined(BC250_ENABLE_DXGK_REGISTRATION)
    BC250_VRAM_ALLOCATOR *allocator;
    const BC250_VRAM_ADDRESS_SPACE *addressSpace;
    BC250_VRAM_TOKEN ringToken = {0};
    BC250_VRAM_REGION ringRegion, workRegion;
    BC250_VRAM_STATUS vramStatus;
    const BC250_RESOLVED_BAR_RESOURCE *bar = NULL;
    PHYSICAL_ADDRESS physical;
    volatile uint32_t *ring = NULL, *work = NULL;
    uint32_t *ringBackup = NULL, *workBackup = NULL;
    uint32_t snapshot[BC250_SDMA_REGISTER_COUNT];
    static const uint32_t writeData[4] = {
        0x11223344u, 0x55667788u, 0xA5A5A5A5u, 0xBC250125u
    };
    uint32_t packets[32] = {0};
    uint32_t cursor = 0u, value, rbCntl, i, j, matches = 0u;
    uint32_t ringAllocated = 0u;
    uint32_t snapshotValid = 0u, ringMapped = 0u, workMapped = 0u;
    uint32_t memoryPrepared = 0u, registersTouched = 0u;
    NTSTATUS ntStatus;
    BC250_SDMA_RING_WORK_STATUS status = BC250_SDMA_RING_WORK_BAD_ARGUMENT;

    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    if (Context == NULL || Evidence == NULL ||
        Context->Core.MmioTransport.State != BC250_MMIO_TOPOLOGY_MAPPED)
        return status;
    allocator = &Context->Core.StartDevice.Owned.Bootstrap.Allocator;
    addressSpace = &Context->Core.TopologyEvidence.AddressSpace;
    if (addressSpace->SnapshotGeneration == 0u ||
        addressSpace->VramMcStart == 0u) return status;
    vramStatus = Bc250VramReserveFixed(allocator,
            BC250_SDMA_LEGACY_RING_OFFSET, BC250_SDMA_LAYOUT_BYTES,
            BC250_SDMA_WORK_PAGE_BYTES,
            BC250_VRAM_ROLE_RING, BC250_VRAM_ALLOC_CPU_VISIBLE,
            &ringToken);
    Evidence->FixedReservationStatus = (uint32_t)vramStatus;
    if (vramStatus == BC250_VRAM_OK) {
        Evidence->AllocationMode = 1u;
    } else {
        vramStatus = Bc250VramAllocate(allocator, BC250_SDMA_LAYOUT_BYTES,
            BC250_SDMA_WORK_PAGE_BYTES, BC250_VRAM_ROLE_RING,
            BC250_VRAM_ALLOC_TOP_DOWN | BC250_VRAM_ALLOC_CPU_VISIBLE,
            &ringToken);
        if (vramStatus != BC250_VRAM_OK)
            return BC250_SDMA_RING_WORK_ALLOCATION_FAILED;
        Evidence->AllocationMode = 2u;
    }
    ringAllocated = 1u;
    if (Bc250VramGetRegion(allocator, ringToken, &ringRegion) != BC250_VRAM_OK ||
        ringRegion.Length != BC250_SDMA_LAYOUT_BYTES) {
        status = BC250_SDMA_RING_WORK_REGION_FAILED;
        goto Cleanup;
    }
    workRegion = ringRegion;
    workRegion.Offset += BC250_SDMA_WORK_PAGE_BYTES;
    workRegion.Length = BC250_SDMA_WORK_BYTES;
    for (i = 0u; i < Context->Core.ResolvedBarCount; ++i) {
        if (Context->Core.ResolvedBars[i].Index ==
            BC250_PCI_VRAM_APERTURE_BAR_INDEX) {
            bar = &Context->Core.ResolvedBars[i];
            ++matches;
        }
    }
    if (matches != 1u || bar == NULL ||
        bar->Length < BC250_SDMA_WORK_PAGE_BYTES ||
        ringRegion.Offset > bar->Length - BC250_SDMA_WORK_PAGE_BYTES ||
        workRegion.Offset > bar->Length - BC250_SDMA_WORK_BYTES ||
        addressSpace->VramMcStart > MAXULONGLONG - ringRegion.Offset ||
        addressSpace->VramMcStart > MAXULONGLONG - workRegion.Offset) {
        status = BC250_SDMA_RING_WORK_APERTURE_INVALID;
        goto Cleanup;
    }
    Evidence->RingOffset = ringRegion.Offset;
    Evidence->WorkOffset = workRegion.Offset;
    Evidence->RingMcAddress = addressSpace->VramMcStart + ringRegion.Offset;
    Evidence->WorkMcAddress = addressSpace->VramMcStart + workRegion.Offset;
    physical.QuadPart = (LONGLONG)(bar->Start + ringRegion.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle, physical,
        BC250_SDMA_WORK_PAGE_BYTES, FALSE, FALSE, MmNonCached, (PVOID *)&ring);
    if (!NT_SUCCESS(ntStatus) || ring == NULL) {
        status = BC250_SDMA_RING_WORK_MAP_FAILED;
        goto Cleanup;
    }
    ringMapped = 1u;
    physical.QuadPart = (LONGLONG)(bar->Start + workRegion.Offset);
    ntStatus = Context->Core.DxgkInterface.DxgkCbMapMemory(
        Context->Core.DxgkInterface.DeviceHandle, physical,
        BC250_SDMA_WORK_BYTES, FALSE, FALSE, MmNonCached, (PVOID *)&work);
    if (!NT_SUCCESS(ntStatus) || work == NULL) {
        status = BC250_SDMA_RING_WORK_MAP_FAILED;
        goto Cleanup;
    }
    workMapped = 1u;
    ringBackup = (uint32_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        BC250_SDMA_WORK_PAGE_BYTES, BC250_SDMA_RING_POOL_TAG);
    workBackup = (uint32_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        BC250_SDMA_WORK_BYTES, BC250_SDMA_RING_POOL_TAG);
    if (ringBackup == NULL || workBackup == NULL) {
        status = BC250_SDMA_RING_WORK_ALLOCATION_FAILED;
        goto Cleanup;
    }
    Bc250SdmaCopyFromDevice(ringBackup, ring, BC250_SDMA_RING_DWORDS);
    Bc250SdmaCopyFromDevice(workBackup, work, BC250_SDMA_WORK_DWORDS);
    for (i = 0u; i < BC250_SDMA_REGISTER_COUNT; ++i) {
        if (!Bc250SdmaRead(Context, g_Bc250SdmaRegisters[i], &snapshot[i])) {
            status = BC250_SDMA_RING_WORK_MMIO_FAILED;
            goto Cleanup;
        }
    }
    snapshotValid = 1u;
    if ((snapshot[9] & F_RB_ENABLE) != 0u) {
        status = BC250_SDMA_RING_WORK_INHERITED_ACTIVE;
        goto Cleanup;
    }
    /* Recuperacao do latch EX_IDLE_POLL_TIMER_EXPIRE (bit 11 de STATUS_REG).
     *
     * Correlacao medida em 129 ciclos de 29/08: 3 falhas do SDMA, TODAS com o
     * latch aceso na entrada, e ZERO falhas sem ele. Condicao necessaria, nao
     * suficiente (2 das 5 ocorrencias sobreviveram).
     *
     * A simetria com o CP e' direta: la', o que destravou o motor foi um reset
     * antes do bring-up. Aqui, um HALT/UNHALT controlado do F32 antes de
     * programar o anel. Fica atras de SdmaWakeOnExIdle (default 0) e so' dispara
     * quando o latch esta aceso — nao muda nada nos outros ciclos. */
    /* Des-gating de clock ANTES de qualquer sondagem.
     *
     * Medido em 29/08 20:43: com EX_IDLE_POLL_TIMER_EXPIRE aceso na entrada, o
     * bit sobrevive ao HALT/UNHALT do F32 E a 10 ms de espera
     * (ExIdleClearPolls=10000 esgotados). Nao e' nivel transitorio nem estado do
     * microengine: e' persistente e vem de fora dele.
     *
     * O suspeito imediato e' clock gating. Este estagio ja' escreve
     * CLK_CTRL |= 0xFF000000, mas so' no meio da programacao do anel — depois de
     * ja' termos sondado e tentado acordar um motor possivelmente sem clock.
     * Aqui o mesmo write acontece primeiro. Atras de SdmaEarlyClkUngate. */
    if (Context->SdmaEarlyClkUngate != 0u) {
        uint32_t clk = 0u;
        if (Bc250SdmaRead(Context, R_CLK_CTRL, &clk)) {
            Evidence->ClkCtrlAtEntry = clk;
            (void)Bc250SdmaWrite(Context, R_CLK_CTRL, clk | 0xFF000000u);
            KeStallExecutionProcessor(50u);
            Evidence->EarlyClkUngateApplied = 1u;
        }
    }

    (void)Bc250SdmaRead(Context, R_STATUS, &Evidence->StatusAtEntry);
    Evidence->ExIdleAtEntry =
        (Evidence->StatusAtEntry & 0x00000800u) ? 1u : 0u;
    if (Evidence->ExIdleAtEntry != 0u && Context->SdmaWakeOnExIdle != 0u) {
        uint32_t f32 = 0u;
        if (Bc250SdmaRead(Context, R_F32_CNTL, &f32)) {
            (void)Bc250SdmaWrite(Context, R_F32_CNTL, f32 | F_F32_HALT);
            KeStallExecutionProcessor(50u);
            (void)Bc250SdmaWrite(Context, R_F32_CNTL, f32 & ~F_F32_HALT);
            KeStallExecutionProcessor(50u);
            Evidence->WakeApplied = 1u;
        }
        (void)Bc250SdmaRead(Context, R_STATUS, &Evidence->StatusAfterWake);

        /* Medido em 29/08 20:29: o HALT/UNHALT do F32 NAO limpa o bit
         * (wake aplicado=1, latch depois=1) e o ciclo falhou mesmo assim. Entao
         * ou o bit e' um latch que so' outro mecanismo apaga, ou e' um NIVEL que
         * reflete uma condicao em curso — e nivel se resolve esperando.
         *
         * Aqui espera-se ate' ~10 ms pelo bit cair, registrando quantos polls
         * foram precisos. Se cair: e' nivel transitorio e o remedio e' esperar.
         * Se nao cair em 10 ms: e' persistente e a origem esta fora do
         * microengine (clock/power/SMU), nao em algo que este estagio controle. */
        for (Evidence->ExIdleClearPolls = 0u;
             Evidence->ExIdleClearPolls < 10000u;
             ++Evidence->ExIdleClearPolls) {
            uint32_t st = 0u;
            if (Bc250SdmaRead(Context, R_STATUS, &st) &&
                (st & 0x00000800u) == 0u) {
                Evidence->StatusAfterWake = st;
                break;
            }
            KeStallExecutionProcessor(1u);
        }
        Evidence->ExIdleAfterWake =
            (Evidence->StatusAfterWake & 0x00000800u) ? 1u : 0u;
    }

    RtlZeroMemory(packets, sizeof(packets));
    if (Bc250SdmaEmitWriteLinear(packets, 32u, &cursor,
            Evidence->WorkMcAddress + BC250_SDMA_DEST_OFFSET,
            writeData, 4u) != BC250_SDMA_OK ||
        Bc250SdmaEmitGcr(packets, 32u, &cursor) != BC250_SDMA_OK ||
        Bc250SdmaEmitFence(packets, 32u, &cursor,
            Evidence->WorkMcAddress + BC250_SDMA_FENCE_OFFSET,
            BC250_SDMA_FENCE_VALUE) != BC250_SDMA_OK) {
        status = BC250_SDMA_RING_WORK_PACKET_FAILED;
        goto Cleanup;
    }
    Evidence->WptrPublished = cursor * sizeof(uint32_t);
    Evidence->FencePoison = BC250_SDMA_FENCE_POISON;
    Evidence->FenceExpected = BC250_SDMA_FENCE_VALUE;
    for (i = 0u; i < BC250_SDMA_RING_DWORDS; ++i)
        Bc250SdmaDeviceWrite(&ring[i], 0u);
    for (i = 0u; i < cursor; ++i)
        Bc250SdmaDeviceWrite(&ring[i], packets[i]);
    for (i = 0u; i < 4u; ++i)
        Bc250SdmaDeviceWrite(&work[BC250_SDMA_DEST_OFFSET / 4u + i],
            0xDEADC000u + i);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_RPTR_OFFSET / 4u], 0u);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_RPTR_OFFSET / 4u + 1u], 0u);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_WPTR_OFFSET / 4u], 0u);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_WPTR_OFFSET / 4u + 1u], 0u);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_FENCE_OFFSET / 4u],
        BC250_SDMA_FENCE_POISON);
    KeMemoryBarrier();
    memoryPrepared = 1u;
    registersTouched = 1u;
    if (!Bc250SdmaHdp(Context)) {
        status = BC250_SDMA_RING_WORK_MMIO_FAILED;
        goto Restore;
    }
    Evidence->RingHeaderReadback = Bc250SdmaDeviceRead(&ring[0]);
    Evidence->RptrBefore = Bc250SdmaDeviceRead(
        &work[BC250_SDMA_RPTR_OFFSET / 4u]);
    if (!Bc250SdmaWrite(Context, R_SEM_TIMER, 0u) ||
        !Bc250SdmaRead(Context, R_RB_CNTL, &rbCntl)) {
        status = BC250_SDMA_RING_WORK_MMIO_FAILED;
        goto Restore;
    }
    rbCntl = (rbCntl & ~(M_RB_SIZE | F_RB_ENABLE | F_RPTR_WB)) |
        (10u << 1) | F_RPTR_WB;
    if (!Bc250SdmaWrite(Context, R_RB_CNTL, rbCntl & ~F_RB_ENABLE) ||
        !Bc250SdmaWrite(Context, R_RB_RPTR, 0u) ||
        !Bc250SdmaWrite(Context, R_RB_RPTR_HI, 0u) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR, 0u) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR_HI, 0u) ||
        !Bc250SdmaWrite(Context, R_WPTR_POLL_ADDR_LO,
            (uint32_t)(Evidence->WorkMcAddress + BC250_SDMA_WPTR_OFFSET)) ||
        !Bc250SdmaWrite(Context, R_WPTR_POLL_ADDR_HI,
            (uint32_t)((Evidence->WorkMcAddress + BC250_SDMA_WPTR_OFFSET) >> 32)) ||
        !Bc250SdmaWrite(Context, R_WPTR_POLL_CNTL, 0x00403000u | F_POLL_ENABLE) ||
        !Bc250SdmaWrite(Context, R_RPTR_ADDR_HI,
            (uint32_t)((Evidence->WorkMcAddress + BC250_SDMA_RPTR_OFFSET) >> 32)) ||
        !Bc250SdmaWrite(Context, R_RPTR_ADDR_LO,
            (uint32_t)(Evidence->WorkMcAddress + BC250_SDMA_RPTR_OFFSET) &
                0xFFFFFFFCu) ||
        !Bc250SdmaGolden(Context, R_CHICKEN, 0xFFBF1F0Fu, 0x03AB0107u) ||
        !Bc250SdmaGolden(Context, R_GB_ADDR, 0x001877FFu, 0x00000044u) ||
        !Bc250SdmaGolden(Context, R_GB_ADDR_READ, 0x001877FFu, 0x00000044u) ||
        !Bc250SdmaWrite(Context, R_RB_BASE,
            (uint32_t)(Evidence->RingMcAddress >> 8)) ||
        !Bc250SdmaWrite(Context, R_RB_BASE_HI,
            (uint32_t)(Evidence->RingMcAddress >> 40)) ||
        !Bc250SdmaWrite(Context, R_MINOR_PTR, F_MINOR_PTR) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR, 0u) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR_HI, 0u) ||
        !Bc250SdmaWrite(Context, R_MINOR_PTR, 0u) ||
        !Bc250SdmaRead(Context, R_DOORBELL, &value) ||
        !Bc250SdmaWrite(Context, R_DOORBELL, value & ~F_DOORBELL_ENABLE) ||
        !Bc250SdmaRead(Context, R_CLK_CTRL, &value) ||
        !Bc250SdmaWrite(Context, R_CLK_CTRL, value | 0xFF000000u) ||
        !Bc250SdmaRead(Context, R_CNTL, &Evidence->CntlBefore) ||
        !Bc250SdmaWrite(Context, R_CNTL,
            (Evidence->CntlApplied =
                (Context->SdmaAutoCtxSw != 0u)
                    ? ((Evidence->CntlBefore | F_UTC_L1 | F_AUTO_CTXSW) & ~F_MIDCMD)
                    : ((Evidence->CntlBefore | F_UTC_L1) & ~(F_AUTO_CTXSW | F_MIDCMD)))) ||
        !Bc250SdmaRead(Context, R_UTCL1_CNTL, &value) ||
        !Bc250SdmaWrite(Context, R_UTCL1_CNTL,
            (value & ~(0x00000E00u | 0x0000003Eu)) |
            (3u << 9) | (9u << 1)) ||
        !Bc250SdmaGolden(Context, R_UTCL1_PAGE, 0x00FFF000u, 0x004C5C00u) ||
        !Bc250SdmaRead(Context, R_F32_CNTL, &value) ||
        !Bc250SdmaWrite(Context, R_F32_CNTL, value & ~F_F32_HALT) ||
        !Bc250SdmaWrite(Context, R_RB_CNTL, rbCntl | F_RB_ENABLE) ||
        !Bc250SdmaRead(Context, R_IB_CNTL, &value) ||
        !Bc250SdmaWrite(Context, R_IB_CNTL, value | F_IB_ENABLE)) {
        status = BC250_SDMA_RING_WORK_MMIO_FAILED;
        goto Restore;
    }
    if (!Bc250SdmaRead(Context, R_RB_BASE, &value) ||
        value != (uint32_t)(Evidence->RingMcAddress >> 8) ||
        !Bc250SdmaRead(Context, R_RB_CNTL, &value) ||
        (value & (F_RB_ENABLE | M_RB_SIZE | F_RPTR_WB)) !=
            ((rbCntl | F_RB_ENABLE) &
                (F_RB_ENABLE | M_RB_SIZE | F_RPTR_WB))) {
        status = BC250_SDMA_RING_WORK_READBACK_FAILED;
        goto Restore;
    }
    Evidence->RbCntlApplied = value;
    if (Bc250SdmaRead(Context, R_WPTR_POLL_CNTL, &value))
        Evidence->WptrPollCntlApplied = value;
    (void)Bc250SdmaRead(Context, R_STATUS, &Evidence->StatusBefore);
    /* Bit 11 de SDMA0_STATUS_REG = EX_IDLE_POLL_TIMER_EXPIRE. No soak de 40
     * ciclos de 29/08 18:56 ele separou os dois grupos sem excecao: limpo nos
     * 37 ciclos bons (STATUS_before=0x46DEE557), aceso nos 3 que deram timeout
     * (0x46DEED57). E' estado herdado, lido antes de qualquer escrita nossa. */
    Evidence->ExIdlePollExpiredBefore =
        (Evidence->StatusBefore & 0x00000800u) ? 1u : 0u;
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_WPTR_OFFSET / 4u],
        Evidence->WptrPublished);
    Bc250SdmaDeviceWrite(&work[BC250_SDMA_WPTR_OFFSET / 4u + 1u], 0u);
    (void)Bc250SdmaDeviceRead(&ring[0]);
    KeMemoryBarrier();
    KeStallExecutionProcessor(50u);
    if (!Bc250SdmaHdp(Context) ||
        !Bc250SdmaWrite(Context, R_MINOR_PTR, F_MINOR_PTR) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR, Evidence->WptrPublished) ||
        !Bc250SdmaWrite(Context, R_RB_WPTR_HI, 0u) ||
        !Bc250SdmaWrite(Context, R_MINOR_PTR, 0u)) {
        status = BC250_SDMA_RING_WORK_MMIO_FAILED;
        goto Restore;
    }
    (void)Bc250SdmaRead(Context, R_RB_WPTR,
        &Evidence->WptrRegisterAfterKick);
    Evidence->WptrShadowAfterKick = Bc250SdmaDeviceRead(
        &work[BC250_SDMA_WPTR_OFFSET / 4u]);
    (void)Bc250SdmaRead(Context, R_STATUS, &Evidence->StatusKick);
    if (Bc250SdmaRead(Context, R_STATUS2, &value))
        Evidence->PcKick = (value >> 2) & 0x3FFFu;
    /* Snapshot ANTES do poll. ERROR_LOG e os XNACK da UTCL1 sao latches: em
     * 29/08 as duas falhas distintas trouxeram valores identicos
     * (ErrorLog=0xF, RdXnack0=0x0045FA00, WrXnack0=1), o que nao distingue
     * "falhou agora" de "falhou uma vez em algum ciclo anterior". Sem o par
     * antes/depois nao se pode atribuir a falta de traducao a este ciclo. */
    (void)Bc250SdmaRead(Context, R_ERROR_LOG, &Evidence->ErrorLogBefore);
    (void)Bc250SdmaRead(Context, R_UTCL1_RD_XNACK0,
        &Evidence->Utcl1ReadXnack0Before);
    (void)Bc250SdmaRead(Context, R_UTCL1_WR_XNACK0,
        &Evidence->Utcl1WriteXnack0Before);
    (void)Bc250SdmaRead(Context, R_UTCL1_TIMEOUT,
        &Evidence->Utcl1TimeoutBefore);

    for (i = 1u; i <= 1000u; ++i) {
        (void)Bc250SdmaHdp(Context);
        Evidence->RptrAfter = Bc250SdmaDeviceRead(
            &work[BC250_SDMA_RPTR_OFFSET / 4u]);
        (void)Bc250SdmaRead(Context, R_RB_RPTR,
            &Evidence->RptrRegisterAfter);
        Evidence->FenceObserved = Bc250SdmaDeviceRead(
            &work[BC250_SDMA_FENCE_OFFSET / 4u]);
        Evidence->Polls = i;
        if (Evidence->RptrRegisterAfter == Evidence->WptrPublished &&
            Evidence->FenceObserved == Evidence->FenceExpected) break;
    }
    (void)Bc250SdmaHdp(Context);
    Evidence->RptrAfter = Bc250SdmaDeviceRead(&work[BC250_SDMA_RPTR_OFFSET / 4u]);
    (void)Bc250SdmaRead(Context, R_STATUS, &Evidence->StatusAfter);
    (void)Bc250SdmaRead(Context, R_RB_RPTR, &Evidence->RptrRegisterAfter);
    (void)Bc250SdmaRead(Context, R_STATUS1, &Evidence->Status1After);
    (void)Bc250SdmaRead(Context, R_STATUS2, &Evidence->Status2After);
    (void)Bc250SdmaRead(Context, R_STATUS3, &Evidence->Status3After);
    (void)Bc250SdmaRead(Context, R_ERROR_LOG, &Evidence->ErrorLogAfter);
    (void)Bc250SdmaRead(Context, R_UTCL1_RD_STATUS,
        &Evidence->Utcl1ReadStatusAfter);
    (void)Bc250SdmaRead(Context, R_UTCL1_WR_STATUS,
        &Evidence->Utcl1WriteStatusAfter);
    (void)Bc250SdmaRead(Context, R_UTCL1_RD_XNACK0,
        &Evidence->Utcl1ReadXnack0After);
    (void)Bc250SdmaRead(Context, R_UTCL1_RD_XNACK1,
        &Evidence->Utcl1ReadXnack1After);
    (void)Bc250SdmaRead(Context, R_UTCL1_WR_XNACK0,
        &Evidence->Utcl1WriteXnack0After);
    (void)Bc250SdmaRead(Context, R_UTCL1_WR_XNACK1,
        &Evidence->Utcl1WriteXnack1After);
    (void)Bc250SdmaRead(Context, R_UTCL1_TIMEOUT,
        &Evidence->Utcl1TimeoutAfter);
    if (Evidence->RptrRegisterAfter != Evidence->WptrPublished) {
        /* Nao altera o comportamento — so' o nome do desfecho. Sair mais cedo
         * quando o latch esta aceso seria errado: 2 das 3 ocorrencias mais
         * recentes sobreviveram, entao abortar transformaria ciclos bons em
         * falhas. Aqui apenas se distingue a causa provavel no relatorio. */
        status = (Evidence->ExIdleAtEntry != 0u)
            ? BC250_SDMA_RING_WORK_EX_IDLE_BLOCKED
            : BC250_SDMA_RING_WORK_TIMEOUT;
        goto Restore;
    }
    if (Evidence->FenceObserved != Evidence->FenceExpected ||
        Evidence->FenceObserved == Evidence->FencePoison) {
        status = BC250_SDMA_RING_WORK_FENCE_FAILED;
        goto Restore;
    }
    (void)Bc250SdmaHdp(Context);
    Evidence->ComparedDwords = 4u;
    for (i = 0u; i < Evidence->ComparedDwords; ++i) {
        if (Bc250SdmaDeviceRead(&work[BC250_SDMA_DEST_OFFSET / 4u + i]) !=
            writeData[i])
            ++Evidence->MismatchCount;
    }
    if (Evidence->MismatchCount != 0u) {
        status = BC250_SDMA_RING_WORK_DATA_FAILED;
        goto Restore;
    }
    status = BC250_SDMA_RING_WORK_OK;

Restore:
    if (registersTouched != 0u && snapshotValid != 0u) {
        (void)Bc250SdmaRead(Context, R_RB_CNTL, &value);
        (void)Bc250SdmaWrite(Context, R_RB_CNTL, value & ~F_RB_ENABLE);
        (void)Bc250SdmaWrite(Context, R_WPTR_POLL_CNTL, 0u);
        (void)Bc250SdmaRead(Context, R_F32_CNTL, &value);
        (void)Bc250SdmaWrite(Context, R_F32_CNTL, value | F_F32_HALT);
        KeStallExecutionProcessor(50u);
        for (i = 0u; i < BC250_SDMA_REGISTER_COUNT; ++i) {
            if (g_Bc250SdmaRegisters[i] == R_RB_CNTL ||
                g_Bc250SdmaRegisters[i] == R_RB_WPTR ||
                g_Bc250SdmaRegisters[i] == R_RB_WPTR_HI ||
                g_Bc250SdmaRegisters[i] == R_MINOR_PTR) continue;
            (void)Bc250SdmaWrite(Context, g_Bc250SdmaRegisters[i], snapshot[i]);
        }
        (void)Bc250SdmaWrite(Context, R_MINOR_PTR, F_MINOR_PTR);
        (void)Bc250SdmaWrite(Context, R_RB_WPTR, snapshot[14]);
        (void)Bc250SdmaWrite(Context, R_RB_WPTR_HI, snapshot[15]);
        (void)Bc250SdmaWrite(Context, R_MINOR_PTR, snapshot[21]);
        (void)Bc250SdmaWrite(Context, R_RB_CNTL, snapshot[9]);
        Evidence->RegisterRestorePassed = 1u;
        for (i = 0u; i < BC250_SDMA_REGISTER_COUNT; ++i) {
            if (!Bc250SdmaRead(Context, g_Bc250SdmaRegisters[i], &value) ||
                value != snapshot[i]) {
                Evidence->RegisterRestorePassed = 0u;
                break;
            }
        }
    }
    if (memoryPrepared != 0u && ringBackup != NULL && workBackup != NULL) {
        Bc250SdmaCopyToDevice(ring, ringBackup, BC250_SDMA_RING_DWORDS);
        Bc250SdmaCopyToDevice(work, workBackup, BC250_SDMA_WORK_DWORDS);
        (void)Bc250SdmaHdp(Context);
        for (j = 0u; j < 5u; ++j) KeStallExecutionProcessor(1000u);
        (void)Bc250SdmaWrite(Context, R_HDP_INVALIDATE, snapshot[24]);
        (void)Bc250SdmaWrite(Context, R_HDP_MISC, snapshot[25]);
        (void)Bc250SdmaWrite(Context, R_HDP_FLUSH, snapshot[26]);
        Evidence->MemoryRestorePassed = 1u;
        for (i = 0u; i < BC250_SDMA_RING_DWORDS; ++i) {
            if (Bc250SdmaDeviceRead(&ring[i]) != ringBackup[i]) {
                Evidence->MemoryRestorePassed = 0u;
                break;
            }
        }
        if (Evidence->MemoryRestorePassed != 0u) {
            for (i = 0u; i < BC250_SDMA_WORK_DWORDS; ++i) {
                if (Bc250SdmaDeviceRead(&work[i]) != workBackup[i]) {
                    Evidence->MemoryRestorePassed = 0u;
                    break;
                }
            }
        }
        for (i = 24u; i < BC250_SDMA_REGISTER_COUNT; ++i) {
            if (!Bc250SdmaRead(Context, g_Bc250SdmaRegisters[i], &value) ||
                value != snapshot[i]) {
                Evidence->RegisterRestorePassed = 0u;
                break;
            }
        }
    }
    if ((registersTouched != 0u && Evidence->RegisterRestorePassed == 0u) ||
        (memoryPrepared != 0u && Evidence->MemoryRestorePassed == 0u))
        status = BC250_SDMA_RING_WORK_RESTORE_FAILED;
    if (status == BC250_SDMA_RING_WORK_OK)
        Evidence->PhysicalReady = 1u;

Cleanup:
    if (workBackup != NULL) ExFreePoolWithTag(workBackup, BC250_SDMA_RING_POOL_TAG);
    if (ringBackup != NULL) ExFreePoolWithTag(ringBackup, BC250_SDMA_RING_POOL_TAG);
    if (workMapped != 0u && !NT_SUCCESS(
            Context->Core.DxgkInterface.DxgkCbUnmapMemory(
                Context->Core.DxgkInterface.DeviceHandle, (PVOID)work)) &&
        status == BC250_SDMA_RING_WORK_OK)
        status = BC250_SDMA_RING_WORK_UNMAP_FAILED;
    if (ringMapped != 0u && !NT_SUCCESS(
            Context->Core.DxgkInterface.DxgkCbUnmapMemory(
                Context->Core.DxgkInterface.DeviceHandle, (PVOID)ring)) &&
        status == BC250_SDMA_RING_WORK_OK)
        status = BC250_SDMA_RING_WORK_UNMAP_FAILED;
    if (ringAllocated != 0u &&
        Bc250VramRelease(allocator, ringToken) != BC250_VRAM_OK &&
        status == BC250_SDMA_RING_WORK_OK)
        status = BC250_SDMA_RING_WORK_RELEASE_FAILED;
    if (status != BC250_SDMA_RING_WORK_OK) Evidence->PhysicalReady = 0u;
    return status;
#else
    UNREFERENCED_PARAMETER(Context);
    if (Evidence != NULL) RtlZeroMemory(Evidence, sizeof(*Evidence));
    return BC250_SDMA_RING_WORK_BAD_ARGUMENT;
#endif
}
