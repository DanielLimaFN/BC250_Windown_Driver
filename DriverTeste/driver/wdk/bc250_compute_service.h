#ifndef BC250_COMPUTE_SERVICE_H
#define BC250_COMPUTE_SERVICE_H

#if defined(_KERNEL_MODE) || defined(BC250_KERNEL_MODE) || defined(_NTDDK_)
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define BC250_COMPUTE_DEVICE_NAME        L"\\Device\\Bc250Compute"
#define BC250_COMPUTE_DOS_DEVICE_NAME    L"\\DosDevices\\Bc250Compute"

#define BC250_IOCTL_TYPE                 0x8250

#define IOCTL_BC250_GET_HARDWARE_INFO \
    CTL_CODE(BC250_IOCTL_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_SDMA_COPY \
    CTL_CODE(BC250_IOCTL_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_DISPATCH_COMPUTE \
    CTL_CODE(BC250_IOCTL_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_READ_MMIO_REG \
    CTL_CODE(BC250_IOCTL_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_WRITE_MMIO_REG \
    CTL_CODE(BC250_IOCTL_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_READ_SMN_REG \
    CTL_CODE(BC250_IOCTL_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_WRITE_SMN_REG \
    CTL_CODE(BC250_IOCTL_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_WAKEUP_GFX_CORE \
    CTL_CODE(BC250_IOCTL_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_ALLOC_VRAM \
    CTL_CODE(BC250_IOCTL_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_BC250_FREE_VRAM \
    CTL_CODE(BC250_IOCTL_TYPE, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _BC250_HARDWARE_INFO {
    ULONG  CpuCoreCount;
    ULONG  GpuCuCount;
    ULONG  GpuWgpCount;
    ULONG  GpuShaderEngineCount;
    ULONG  GpuShaderArrayPerSe;
    ULONG  CuMaskPerArray[4];
    ULONG  VramTotalMiB;
    ULONG  CpuVisibleMiB;
    ULONG  RebarSupportedMask;
    ULONG  RebarSelectedMiB;
    ULONG  MmioPhysicalBase;
    ULONG  MmioLength;
    ULONG  GfxoffState;
    ULONG  GpuClockMhz;
    ULONG  MemoryClockMhz;
    CHAR   SiliconName[32];
} BC250_HARDWARE_INFO, *PBC250_HARDWARE_INFO;

typedef struct _BC250_MMIO_REG_REQ {
    ULONG Offset;
    ULONG Value;
    ULONG Status;
} BC250_MMIO_REG_REQ, *PBC250_MMIO_REG_REQ;

typedef struct _BC250_SMN_REG_REQ {
    ULONG SmnAddress;
    ULONG Value;
    ULONG Status;
} BC250_SMN_REG_REQ, *PBC250_SMN_REG_REQ;

typedef struct _BC250_WAKEUP_REQ {
    ULONG InitialGfxoffState;
    ULONG FinalGfxoffState;
    ULONG SmuResponse;
    ULONG Success;
} BC250_WAKEUP_REQ, *PBC250_WAKEUP_REQ;

typedef struct _BC250_SDMA_COPY_REQ {
    ULONGLONG SourceGpuVa;
    ULONGLONG DestinationGpuVa;
    ULONG     ByteCount;
    ULONG     FenceValue;
    ULONG     WptrBefore;
    ULONG     WptrAfter;
    ULONG     RptrBefore;
    ULONG     RptrAfter;
    ULONG     FenceObserved;
    ULONG     ExecutionSuccess;
} BC250_SDMA_COPY_REQ, *PBC250_SDMA_COPY_REQ;

typedef struct _BC250_COMPUTE_DISPATCH_REQ {
    ULONG     ShaderCodeSize;
    ULONG     ShaderCode[256];
    ULONG     WorkgroupSizeX;
    ULONG     WorkgroupSizeY;
    ULONG     WorkgroupSizeZ;
    ULONG     GridDimX;
    ULONG     GridDimY;
    ULONG     GridDimZ;
    ULONG     FenceValue;
    ULONG     WptrBefore;
    ULONG     WptrAfter;
    ULONG     RptrBefore;
    ULONG     RptrAfter;
    ULONG     FenceObserved;
    ULONG     IsaValidationPassed;
    ULONG     ExecutionSuccess;
} BC250_COMPUTE_DISPATCH_REQ, *PBC250_COMPUTE_DISPATCH_REQ;

typedef struct _BC250_ALLOC_VRAM_REQ {
    ULONGLONG SizeBytes;
    ULONGLONG Alignment;
    ULONGLONG AllocatedGpuVa;
    ULONG     Success;
} BC250_ALLOC_VRAM_REQ, *PBC250_ALLOC_VRAM_REQ;

typedef struct _BC250_FREE_VRAM_REQ {
    ULONGLONG GpuVa;
    ULONGLONG SizeBytes;
    ULONG     Success;
} BC250_FREE_VRAM_REQ, *PBC250_FREE_VRAM_REQ;

#endif
