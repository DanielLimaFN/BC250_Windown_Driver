#include <windows.h>
#include <stdio.h>
#include "../driver/wdk/bc250_compute_service.h"

static int PrintGateResult(const BC250_GPUVA_SDMA_TWO_CONTEXT_REQ *request, DWORD error)
{
    printf("BC250 GPUVA+SDMA TWO-CONTEXT GATE\n");
    printf("  win32 error:              %lu\n", error);
    printf("  gate/backend:             %lu / %lu\n", request->GateStatus, request->BackendState);
    printf("  preflight/isolation:      %lu / %lu\n", request->PreflightPassed, request->MappingIsolationPassed);
    printf("  execution/fence/data:     %lu / %lu / mismatches=%lu\n",
           request->ExecutionAttempted, request->PhysicalFenceObserved, request->DataMismatches);
    printf("  rollback/hardware proof:  %lu / %lu\n",
           request->RollbackPerformed, request->HardwareValidated);
    printf("  RPTR/WPTR:                %llu/%llu -> %llu/%llu\n",
           request->RptrBefore, request->WptrBefore, request->RptrAfter, request->WptrAfter);
    return request->HardwareValidated == 1u ? 0 : 2;
}

int main(int argc, char **argv)
{
    BC250_GPUVA_SDMA_TWO_CONTEXT_REQ request;
    HANDLE device;
    DWORD returned = 0;
    BOOL execute = argc == 2 && lstrcmpA(argv[1], "--execute") == 0;

    ZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Version = BC250_GPUVA_SDMA_TWO_CONTEXT_VERSION;
    request.Flags = execute ? BC250_GPUVA_SDMA_FLAG_EXECUTE : BC250_GPUVA_SDMA_FLAG_DRY_RUN;
    request.ByteCount = 4096u;
    /* Test-only opaque lease values. A live backend must issue them itself. */
    request.SourceLeaseId = 1ull;
    request.DestinationLeaseId = 2ull;
    request.SourceGpuVa = 0x0000000000100000ull;
    request.DestinationGpuVa = 0x0000000000200000ull;
    request.FenceGpuVa = 0x0000000000300000ull;
    request.FenceExpected = 0xBC250001u;
    request.SourcePoison = 0xA5A55A5Au;
    request.DestinationPoison = 0xDEADC0DEu;

    device = CreateFileW(L"\\\\.\\Bc250Compute", GENERIC_READ | GENERIC_WRITE, 0,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        printf("Bc250Compute unavailable: %lu\n", GetLastError());
        return 1;
    }

    if (!DeviceIoControl(device, IOCTL_BC250_GPUVA_SDMA_TWO_CONTEXT,
                         &request, sizeof(request), &request, sizeof(request), &returned, NULL)) {
        DWORD error = GetLastError();
        CloseHandle(device);
        return PrintGateResult(&request, error);
    }
    CloseHandle(device);
    return PrintGateResult(&request, ERROR_SUCCESS);
}
