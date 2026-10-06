#ifndef BC250_PLATFORM_REGISTER_SNAPSHOT_H
#define BC250_PLATFORM_REGISTER_SNAPSHOT_H

#include "bc250_vram_topology_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_REG_GCVM_PT_BASE_LO    0x0000A3ACu
#define BC250_REG_GCVM_PT_BASE_HI    0x0000A3B0u
#define BC250_REG_GCVM_PT_START_LO   0x0000A42Cu
#define BC250_REG_GCVM_PT_START_HI   0x0000A430u
#define BC250_REG_GCVM_PT_END_LO     0x0000A4ACu
#define BC250_REG_GCVM_PT_END_HI     0x0000A4B0u
#define BC250_REG_CP_RB0_BASE        0x0000C100u
#define BC250_REG_CP_RB0_CNTL        0x0000C104u
#define BC250_REG_CP_RB0_BASE_HI     0x0000C2C4u
#define BC250_REG_CP_RB0_RPTR        0x00008700u
#define BC250_REG_CP_RB0_RPTR_ADDR_LO 0x0000C10Cu
#define BC250_REG_CP_RB0_RPTR_ADDR_HI 0x0000C110u
#define BC250_REG_CP_RB0_WPTR        0x0000C150u
#define BC250_REG_CP_RB0_WPTR_HI     0x0000C154u
/*
 * EOP do CP (doc 618), P1: mmCP_INT_CNTL_RING0 = 0x1e0a, BASE_IDX 0
 * (gc_10_1_0_offset.h), GC_BASE seg0 = 0x1260 (cyan_skillfish_ip_offset.h)
 * -> (0x1260 + 0x1E0A) * 4 = 0xC1A8. TIME_STAMP_INT_ENABLE = bit 26, mascara
 * 0x04000000 (gc_10_1_0_sh_mask.h). Escritor unico: gfx_v10_0_set_gfx_eop_
 * interrupt_state (me 0, pipe 0) no amdgpu; aqui Bc250ActiveCpEopIrqArm/Disarm.
 */
#define BC250_REG_CP_INT_CNTL_RING0  0x0000C1A8u
#define BC250_CP_INT_CNTL_RING0_TIME_STAMP_INT_ENABLE 0x04000000u
#define BC250_REG_SDMA0_RB_CNTL      0x00004B80u
#define BC250_REG_SDMA0_RB_BASE      0x00004B84u
#define BC250_REG_SDMA0_RB_BASE_HI   0x00004B88u
#define BC250_REG_SDMA0_RB_RPTR      0x00004B8Cu
#define BC250_REG_SDMA0_RB_RPTR_HI   0x00004B90u
#define BC250_REG_SDMA0_RB_WPTR      0x00004B94u
#define BC250_REG_SDMA0_RB_WPTR_HI   0x00004B98u
#define BC250_REG_SDMA0_RB_RPTR_ADDR_HI 0x00004BA0u
#define BC250_REG_SDMA0_RB_RPTR_ADDR_LO 0x00004BA4u
#define BC250_REG_PSP_C2PMSG_35      0x0005818Cu
#define BC250_REG_PSP_C2PMSG_36      0x00058190u
#define BC250_REG_PSP_C2PMSG_37      0x00058194u
#define BC250_REG_PSP_C2PMSG_81      0x00058244u
#define BC250_REG_PSP_C2PMSG_64      0x00058200u
#define BC250_REG_PSP_C2PMSG_67      0x0005820Cu
#define BC250_REG_PSP_C2PMSG_69      0x00058214u
#define BC250_REG_PSP_C2PMSG_70      0x00058218u
#define BC250_REG_PSP_C2PMSG_71      0x0005821Cu
#define BC250_PLATFORM_REGISTER_COUNT 32u

typedef enum BC250_PLATFORM_SNAPSHOT_STATUS {
    BC250_PLATFORM_SNAPSHOT_OK = 0,
    BC250_PLATFORM_SNAPSHOT_BAD_ARGUMENT,
    BC250_PLATFORM_SNAPSHOT_MISSING_IDENTITY,
    BC250_PLATFORM_SNAPSHOT_CALLBACK_FAILED,
    BC250_PLATFORM_SNAPSHOT_UNSTABLE
} BC250_PLATFORM_SNAPSHOT_STATUS;

typedef struct BC250_PLATFORM_REGISTER_SNAPSHOT {
    uint32_t SnapshotGeneration;
    uint32_t ReadCount;
    uint32_t StablePasses;
    uint32_t GcvmPtBaseLo;
    uint32_t GcvmPtBaseHi;
    uint32_t GcvmPtStartLo;
    uint32_t GcvmPtStartHi;
    uint32_t GcvmPtEndLo;
    uint32_t GcvmPtEndHi;
    uint32_t CpRb0Base;
    uint32_t CpRb0BaseHi;
    uint32_t CpRb0Cntl;
    uint32_t CpRb0Rptr;
    uint32_t CpRb0RptrAddrLo;
    uint32_t CpRb0RptrAddrHi;
    uint32_t CpRb0Wptr;
    uint32_t CpRb0WptrHi;
    uint32_t Sdma0RbBase;
    uint32_t Sdma0RbBaseHi;
    uint32_t Sdma0RbCntl;
    uint32_t Sdma0RbRptr;
    uint32_t Sdma0RbRptrHi;
    uint32_t Sdma0RbWptr;
    uint32_t Sdma0RbWptrHi;
    uint32_t Sdma0RbRptrAddrLo;
    uint32_t Sdma0RbRptrAddrHi;
    uint32_t PspC2pmsg35;
    uint32_t PspC2pmsg36;
    uint32_t PspC2pmsg37;
    uint32_t PspC2pmsg81;
    uint32_t PspC2pmsg64;
    uint32_t PspC2pmsg67;
    uint32_t PspC2pmsg69;
    uint32_t PspC2pmsg70;
    uint32_t PspC2pmsg71;
} BC250_PLATFORM_REGISTER_SNAPSHOT;

BC250_PLATFORM_SNAPSHOT_STATUS Bc250CapturePlatformRegistersReadonly(
    const BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface,
    uint32_t SnapshotGeneration,
    BC250_PLATFORM_REGISTER_SNAPSHOT *Snapshot);

#ifdef __cplusplus
}
#endif

#endif
