#include "../wdk/bc250_display_ddi.h"
#include "../wdk/bc250_active_miniport.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = (unsigned char)Value;
    return Destination;
}

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *destination = (unsigned char *)Destination;
    const unsigned char *source = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) destination[index] = source[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)

static int Bc250RunDisplayVidPnTest(void)
{
    DRIVER_INITIALIZATION_DATA initData;
    DXGK_CHILD_DESCRIPTOR childDescriptor;
    DXGK_CHILD_STATUS childStatus;
    DXGKARG_ISSUPPORTEDVIDPN isSupported;
    DXGKARG_PRESENT presentArg;

    memset(&initData, 0, sizeof(initData));
    memset(&childDescriptor, 0, sizeof(childDescriptor));
    memset(&childStatus, 0, sizeof(childStatus));
    memset(&isSupported, 0, sizeof(isSupported));
    memset(&presentArg, 0, sizeof(presentArg));

    /* 1. Test DDI Population */
    CHECK(Bc250DisplayPopulateCallbacks(&initData) == STATUS_SUCCESS);
    CHECK(initData.DxgkDdiQueryChildRelations == Bc250DxgkDdiQueryChildRelations);
    CHECK(initData.DxgkDdiQueryChildStatus == Bc250DxgkDdiQueryChildStatus);
    CHECK(initData.DxgkDdiIsSupportedVidPn == Bc250DxgkDdiIsSupportedVidPn);
    CHECK(initData.DxgkDdiCommitVidPn == Bc250DxgkDdiCommitVidPn);
    CHECK(initData.DxgkDdiPresent == Bc250DxgkDdiPresent);

    /* 2. Test QueryChildRelations */
    CHECK(Bc250DxgkDdiQueryChildRelations(NULL, &childDescriptor, sizeof(childDescriptor)) == STATUS_SUCCESS);
    CHECK(childDescriptor.ChildDeviceType == TypeVideoOutput);
    CHECK(childDescriptor.ChildUid == 1);
    CHECK(childDescriptor.ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected);

    /* 3. Test QueryChildStatus */
    childStatus.Type = StatusConnection;
    CHECK(Bc250DxgkDdiQueryChildStatus(NULL, &childStatus, FALSE) == STATUS_SUCCESS);
    CHECK(childStatus.HotPlug.Connected == TRUE);

    /* 4. Test IsSupportedVidPn */
    CHECK(Bc250DxgkDdiIsSupportedVidPn(NULL, &isSupported) == STATUS_SUCCESS);
    CHECK(isSupported.IsVidPnSupported == TRUE);

    /* 5. Test Present */
    CHECK(Bc250DxgkDdiPresent(NULL, &presentArg) == STATUS_SUCCESS);

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunDisplayVidPnTest() ? 0ul : 1ul);
}
