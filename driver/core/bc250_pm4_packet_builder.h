#ifndef BC250_PM4_PACKET_BUILDER_H
#define BC250_PM4_PACKET_BUILDER_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
#endif
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_PM4_TYPE_3 3u
#define BC250_PM4_SHADER_TYPE_COMPUTE (1u << 1)

#define BC250_PM4_TYPE3_HEADER(Opcode, Count) \
    (((uint32_t)BC250_PM4_TYPE_3 << 30) | \
     ((((uint32_t)(Count) - 1u) & 0x3FFFu) << 16) | \
     (((uint32_t)(Opcode) & 0xFFu) << 8))

#define BC250_PM4_IT_NOP                 0x10u
#define BC250_PM4_IT_SET_BASE            0x11u
#define BC250_PM4_IT_DISPATCH_DIRECT     0x15u
#define BC250_PM4_IT_DISPATCH_INDIRECT   0x16u
#define BC250_PM4_IT_DRAW_INDEX_AUTO     0x2Du
#define BC250_PM4_IT_WRITE_DATA          0x37u
#define BC250_PM4_IT_WAIT_REG_MEM        0x3Cu
#define BC250_PM4_IT_INDIRECT_BUFFER     0x3Fu
#define BC250_PM4_IT_COPY_DATA           0x40u
#define BC250_PM4_IT_EVENT_WRITE         0x46u
#define BC250_PM4_IT_EVENT_WRITE_EOP     0x47u
#define BC250_PM4_IT_RELEASE_MEM         0x49u
#define BC250_PM4_IT_SET_CONFIG_REG      0x68u
#define BC250_PM4_IT_SET_CONTEXT_REG     0x69u
#define BC250_PM4_IT_SET_SH_REG          0x76u
#define BC250_PM4_IT_SET_UCONFIG_REG     0x79u

#define BC250_PM4_CONFIG_REG_BASE  0x2000u
#define BC250_PM4_CONTEXT_REG_BASE 0xA000u
#define BC250_PM4_SH_REG_BASE      0x2C00u
#define BC250_PM4_UCONFIG_REG_BASE 0xC000u

#define BC250_PM4_WD_ENGINE_ME  (0u << 30)
#define BC250_PM4_WD_ENGINE_PFP (1u << 30)
#define BC250_PM4_WD_DST_REG    (0u << 8)
#define BC250_PM4_WD_DST_MEM    (5u << 8)
#define BC250_PM4_WD_CONFIRM    (1u << 20)

#define BC250_PM4_WAIT_ALWAYS 0u
#define BC250_PM4_WAIT_LT     1u
#define BC250_PM4_WAIT_LE     2u
#define BC250_PM4_WAIT_EQ     3u
#define BC250_PM4_WAIT_NE     4u
#define BC250_PM4_WAIT_GE     5u
#define BC250_PM4_WAIT_GT     6u

#define BC250_PM4_EVENT_BOTTOM_OF_PIPE       0x3Au
#define BC250_PM4_EVENT_CACHE_FLUSH_AND_INV_TS 0x14u

/*
 * Forma exata de RELEASE_MEM aprovada no silicio no candidato 3.43 (fence
 * 0xBC250D15 gravada pela GPU).  Os dois primeiros dwords sao literais
 * conferidos byte a byte no ensaio fisico e NAO devem ser recalculados:
 *   0xC0064900 = TYPE3(RELEASE_MEM, 8 dwords)
 *   0x06603514 = CACHE_FLUSH_AND_INV_TS_EVENT | EVENT_INDEX(5) | GCR GL2
 * Somente DATA_SEL/INT_SEL do terceiro dword sao parametrizados.
 */
#define BC250_PM4_RELEASE_MEM_HEADER_PROVEN  0xC0064900u
#define BC250_PM4_RELEASE_MEM_EVENT_PROVEN   0x06603514u
#define BC250_PM4_RELEASE_MEM_DATA_SEL_32    (1u << 29)
#define BC250_PM4_RELEASE_MEM_INT_SEL_NONE   (0u << 24)
#define BC250_PM4_RELEASE_MEM_INT_SEL_WRITE_CONFIRM (2u << 24)
#define BC250_PM4_DWORDS_RELEASE_MEM         8u
#define BC250_PM4_DWORDS_INDIRECT_BUFFER     4u
#define BC250_PM4_IB_VALID_BIT               (1u << 23)
#define BC250_PM4_IB_VMID_SHIFT              24u
#define BC250_PM4_IB_SIZE_MASK               0x000FFFFFu
#define BC250_PM4_PACKET2_NOP                0x80000000u
#define BC250_PM4_EVENT_CS_PARTIAL_FLUSH     0x40u
#define BC250_PM4_EVENT_EOP                  0x46u

#define BC250_PM4_DI_PT_POINTLIST     0x01u
#define BC250_PM4_DI_PT_LINELIST      0x02u
#define BC250_PM4_DI_PT_TRIANGLELIST  0x04u
#define BC250_PM4_DI_PT_RECTLIST      0x11u

/* GFX10 Context Registers for 3D Draw / Rasterization (Parede B) */
#define BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL (BC250_PM4_CONTEXT_REG_BASE + 0x0081u)
#define BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_BR (BC250_PM4_CONTEXT_REG_BASE + 0x0082u)
#define BC250_PM4_REG_CB_TARGET_MASK          (BC250_PM4_CONTEXT_REG_BASE + 0x008Eu)
#define BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_TL (BC250_PM4_CONTEXT_REG_BASE + 0x0094u)
#define BC250_PM4_REG_PA_SC_VPORT_SCISSOR_0_BR (BC250_PM4_CONTEXT_REG_BASE + 0x0095u)
#define BC250_PM4_REG_CB_COLOR0_BASE          (BC250_PM4_CONTEXT_REG_BASE + 0x0318u)
#define BC250_PM4_REG_CB_COLOR0_PITCH         (BC250_PM4_CONTEXT_REG_BASE + 0x0319u)
#define BC250_PM4_REG_CB_COLOR0_SLICE         (BC250_PM4_CONTEXT_REG_BASE + 0x031Au)
#define BC250_PM4_REG_CB_COLOR0_VIEW          (BC250_PM4_CONTEXT_REG_BASE + 0x031Bu)
#define BC250_PM4_REG_CB_COLOR0_INFO          (BC250_PM4_CONTEXT_REG_BASE + 0x031Cu)
#define BC250_PM4_REG_CB_COLOR0_ATTRIB        (BC250_PM4_CONTEXT_REG_BASE + 0x031Du)

typedef enum BC250_PM4_STATUS {
    BC250_PM4_OK = 0,
    BC250_PM4_BAD_ARGUMENT,
    BC250_PM4_RING_FULL,
    BC250_PM4_MISALIGNED,
    BC250_PM4_PAYLOAD_TOO_LARGE,
    BC250_PM4_OUT_OF_BOUNDS
} BC250_PM4_STATUS;

BC250_PM4_STATUS Bc250Pm4EmitDrawIndexAuto(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t IndexCount,
    uint32_t DrawInitiator);

BC250_PM4_STATUS Bc250Pm4EmitColorTarget0(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t BaseGpuVa,
    uint32_t Width,
    uint32_t Height,
    uint32_t PitchBytes,
    uint32_t FormatIndex,
    uint32_t CompSwap);

BC250_PM4_STATUS Bc250Pm4EmitWindowScissor(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Left,
    uint32_t Top,
    uint32_t Right,
    uint32_t Bottom);

BC250_PM4_STATUS Bc250Pm4EmitNop(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t ExtraCount);

BC250_PM4_STATUS Bc250Pm4EmitWriteData(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Control,
    uint64_t DstAddress,
    const uint32_t *Data,
    uint32_t DwordCount);

BC250_PM4_STATUS Bc250Pm4EmitSetShReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count);

BC250_PM4_STATUS Bc250Pm4EmitSetContextReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count);

BC250_PM4_STATUS Bc250Pm4EmitSetUconfigReg(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t RegDwordOffset,
    const uint32_t *Values,
    uint32_t Count);

BC250_PM4_STATUS Bc250Pm4EmitWaitRegMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t Control,
    uint64_t Address,
    uint32_t RefValue,
    uint32_t Mask,
    uint32_t PollIntervalUs);

BC250_PM4_STATUS Bc250Pm4EmitIndirectBuffer(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t IbAddress,
    uint32_t SizeInDwords,
    uint32_t Vmid);

BC250_PM4_STATUS Bc250Pm4EmitReleaseMem(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t EventType,
    uint32_t EventFlags,
    uint64_t FenceAddress,
    uint64_t FenceSequence);

/*
 * RELEASE_MEM de 32 bits na forma provada em silicio.  IntSel deve ser
 * BC250_PM4_RELEASE_MEM_INT_SEL_NONE (conclusao por polling) ou
 * BC250_PM4_RELEASE_MEM_INT_SEL_WRITE_CONFIRM (conclusao por interrupcao).
 */
BC250_PM4_STATUS Bc250Pm4EmitReleaseMemEop32(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint64_t FenceAddress,
    uint32_t FenceValue,
    uint32_t IntSel);

BC250_PM4_STATUS Bc250Pm4EmitEventWrite(
    uint32_t *RingBuffer,
    uint32_t RingDwords,
    uint32_t *CursorDwords,
    uint32_t EventType);

BC250_PM4_STATUS Bc250Pm4RingComputeAvailableSpace(
    uint32_t RingDwords,
    uint32_t RptrDwords,
    uint32_t WptrDwords,
    uint32_t *AvailableDwords);

BC250_PM4_STATUS Bc250Pm4RingAdvanceWptr(
    uint32_t RingDwords,
    uint32_t CurrentWptrDwords,
    uint32_t EmittedDwords,
    uint32_t *NewWptrDwords);

const char *Bc250Pm4StatusName(BC250_PM4_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
