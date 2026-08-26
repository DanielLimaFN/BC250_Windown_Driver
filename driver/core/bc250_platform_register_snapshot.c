#include "bc250_platform_register_snapshot.h"

#include <string.h>

static const uint32_t g_Bc250PlatformRegisterOffsets[
    BC250_PLATFORM_REGISTER_COUNT] = {
    BC250_REG_GCVM_PT_BASE_LO, BC250_REG_GCVM_PT_BASE_HI,
    BC250_REG_GCVM_PT_START_LO, BC250_REG_GCVM_PT_START_HI,
    BC250_REG_GCVM_PT_END_LO, BC250_REG_GCVM_PT_END_HI,
    BC250_REG_CP_RB0_BASE, BC250_REG_CP_RB0_BASE_HI,
    BC250_REG_CP_RB0_CNTL, BC250_REG_SDMA0_RB_BASE,
    BC250_REG_SDMA0_RB_BASE_HI, BC250_REG_SDMA0_RB_CNTL,
    BC250_REG_CP_RB0_RPTR, BC250_REG_CP_RB0_RPTR_ADDR_LO,
    BC250_REG_CP_RB0_RPTR_ADDR_HI, BC250_REG_CP_RB0_WPTR,
    BC250_REG_CP_RB0_WPTR_HI, BC250_REG_SDMA0_RB_RPTR,
    BC250_REG_SDMA0_RB_RPTR_HI, BC250_REG_SDMA0_RB_WPTR,
    BC250_REG_SDMA0_RB_WPTR_HI, BC250_REG_SDMA0_RB_RPTR_ADDR_LO,
    BC250_REG_SDMA0_RB_RPTR_ADDR_HI, BC250_REG_PSP_C2PMSG_35,
    BC250_REG_PSP_C2PMSG_36, BC250_REG_PSP_C2PMSG_37,
    BC250_REG_PSP_C2PMSG_81, BC250_REG_PSP_C2PMSG_64,
    BC250_REG_PSP_C2PMSG_67, BC250_REG_PSP_C2PMSG_69,
    BC250_REG_PSP_C2PMSG_70, BC250_REG_PSP_C2PMSG_71
};

BC250_PLATFORM_SNAPSHOT_STATUS Bc250CapturePlatformRegistersReadonly(
    const BC250_VRAM_TOPOLOGY_READER_INTERFACE *Interface,
    uint32_t SnapshotGeneration,
    BC250_PLATFORM_REGISTER_SNAPSHOT *Snapshot)
{
    uint32_t values[2][BC250_PLATFORM_REGISTER_COUNT];
    uint32_t pass;
    uint32_t index;

    if (Snapshot != NULL) memset(Snapshot, 0, sizeof(*Snapshot));
    if (Interface == NULL || Interface->Read32 == NULL || Snapshot == NULL ||
        SnapshotGeneration == 0u) return BC250_PLATFORM_SNAPSHOT_BAD_ARGUMENT;
    if ((Interface->IdentityFlags & BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) !=
        BC250_TOPOLOGY_READER_IDENTITY_REQUIRED) {
        return BC250_PLATFORM_SNAPSHOT_MISSING_IDENTITY;
    }
    for (pass = 0u; pass < 2u; ++pass) {
        for (index = 0u; index < BC250_PLATFORM_REGISTER_COUNT; ++index) {
            if (Interface->Read32(Interface->Context,
                    g_Bc250PlatformRegisterOffsets[index],
                    &values[pass][index]) == 0u) {
                return BC250_PLATFORM_SNAPSHOT_CALLBACK_FAILED;
            }
        }
    }
    for (index = 0u; index < BC250_PLATFORM_REGISTER_COUNT; ++index) {
        if (values[0][index] != values[1][index]) {
            return BC250_PLATFORM_SNAPSHOT_UNSTABLE;
        }
    }
    Snapshot->SnapshotGeneration = SnapshotGeneration;
    Snapshot->ReadCount = 2u * BC250_PLATFORM_REGISTER_COUNT;
    Snapshot->StablePasses = 2u;
    Snapshot->GcvmPtBaseLo = values[0][0];
    Snapshot->GcvmPtBaseHi = values[0][1];
    Snapshot->GcvmPtStartLo = values[0][2];
    Snapshot->GcvmPtStartHi = values[0][3];
    Snapshot->GcvmPtEndLo = values[0][4];
    Snapshot->GcvmPtEndHi = values[0][5];
    Snapshot->CpRb0Base = values[0][6];
    Snapshot->CpRb0BaseHi = values[0][7];
    Snapshot->CpRb0Cntl = values[0][8];
    Snapshot->Sdma0RbBase = values[0][9];
    Snapshot->Sdma0RbBaseHi = values[0][10];
    Snapshot->Sdma0RbCntl = values[0][11];
    Snapshot->CpRb0Rptr = values[0][12];
    Snapshot->CpRb0RptrAddrLo = values[0][13];
    Snapshot->CpRb0RptrAddrHi = values[0][14];
    Snapshot->CpRb0Wptr = values[0][15];
    Snapshot->CpRb0WptrHi = values[0][16];
    Snapshot->Sdma0RbRptr = values[0][17];
    Snapshot->Sdma0RbRptrHi = values[0][18];
    Snapshot->Sdma0RbWptr = values[0][19];
    Snapshot->Sdma0RbWptrHi = values[0][20];
    Snapshot->Sdma0RbRptrAddrLo = values[0][21];
    Snapshot->Sdma0RbRptrAddrHi = values[0][22];
    Snapshot->PspC2pmsg35 = values[0][23];
    Snapshot->PspC2pmsg36 = values[0][24];
    Snapshot->PspC2pmsg37 = values[0][25];
    Snapshot->PspC2pmsg81 = values[0][26];
    Snapshot->PspC2pmsg64 = values[0][27];
    Snapshot->PspC2pmsg67 = values[0][28];
    Snapshot->PspC2pmsg69 = values[0][29];
    Snapshot->PspC2pmsg70 = values[0][30];
    Snapshot->PspC2pmsg71 = values[0][31];
    return BC250_PLATFORM_SNAPSHOT_OK;
}
