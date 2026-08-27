/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    wddm.h

Abstract:
    Header definitions for the Native AMD BC-250 Full WDDM 2.0+ Kernel-Mode Miniport Driver.
    Supports Display + 3D/Compute/Copy Engine Topology directly communicating with silicon.

Environment:
    Kernel mode only (WDDM 2.0+)

--*/

#ifndef _BC250_WDDM_H_
#define _BC250_WDDM_H_

#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
#include <emmintrin.h>

#define BC250_WDDM_DRIVER_TAG                           'DW3B'

/* Hardware IDs */
#define AMD_VENDOR_ID                                   0x1002
#define AMDBC250_DEVICE_ID                              0x13FE

/* EDID Block Size (128 bytes base block) */
#define BC250_EDID_BLOCK_SIZE                           128u

/* Canonical BAR definitions for BC-250 */
#define BC250_BAR5_MMIO_SIZE                            0x00080000ULL /* 512 KB */
#define BC250_BAR0_VRAM_SIZE                            0x10000000ULL /* 256 MB minimum aperture */
#define BC250_VRAM_GPU_BASE                             0xF400000000ULL

/* WDDM 2.0 Engine Nodes (3 Hardware Execution Engines) */
#define BC250_ENGINE_NODE_3D                            0u
#define BC250_ENGINE_NODE_COMPUTE                       1u
#define BC250_ENGINE_NODE_SDMA                          2u
#define BC250_ENGINE_NODE_COUNT                         3u

/* WDDM 2.0 Memory Segments */
#define BC250_SEGMENT_VRAM                              1u
#define BC250_SEGMENT_GART                              2u
#define BC250_SEGMENT_COUNT                             2u

/* D3DKMT Private Escape Commands */
#define BC250_ESCAPE_CMD_SUBMIT_PM4                     0x01
#define BC250_ESCAPE_CMD_READ_REG                       0x02
#define BC250_ESCAPE_CMD_WRITE_REG                      0x03
#define BC250_ESCAPE_CMD_QUERY_INFO                     0x04

typedef struct _BC250_WDDM_ESCAPE_HEADER {
    ULONG CommandId;
    ULONG Status;
    ULONG InSize;
    ULONG OutSize;
} BC250_WDDM_ESCAPE_HEADER;

typedef struct _BC250_WDDM_ESCAPE_SUBMIT {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG DwordCount;
    ULONG RptrBefore;
    ULONG RptrAfter;
    ULONG Data[256];
} BC250_WDDM_ESCAPE_SUBMIT;

typedef struct _BC250_WDDM_ESCAPE_REG {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG Offset;
    ULONG Value;
} BC250_WDDM_ESCAPE_REG;

typedef struct _BC250_WDDM_ESCAPE_INFO {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG DeviceId;
    ULONG HardwareInitialized;
    ULONG GfxWptr;
    ULONG GfxRptr;
    ULONG GrbmStatus;
    ULONG CpStat;
    ULONGLONG VramGpuBase;
    ULONGLONG RingGpuBase;
    ULONG SdmaInitialized;
    ULONG SdmaWptr;
    ULONG SdmaRptr;
    ULONG SdmaBandwidthMBs;
    ULONG SdmaCopyCount;
} BC250_WDDM_ESCAPE_INFO;

/* SDMA Hardware DMA Copy Escapes */
#define BC250_ESCAPE_SDMA_COPY                          0xBD04u
#define BC250_ESCAPE_SDMA_BENCHMARK                     0xBD05u

typedef struct _BC250_WDDM_ESCAPE_SDMA_COPY {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG SrcGpuAddress;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     FenceValue;          /* valor ESPERADO da fence */
    ULONG     DurationUs;
    ULONG     BandwidthMBs;
    /* --- Evidencia fisica (Fase 0, item 0.5). ---
       O loop de conclusao aceitava RPTR==WPTR OU fence. Isso confunde
       "o motor consumiu o frame" com "o motor executou a copia": e' possivel
       o RPTR alcancar o WPTR e a copia nunca acontecer. Estes campos separam
       as duas coisas. */
    ULONG     FenceObserved;       /* valor REALMENTE lido da fence em VRAM */
    ULONGLONG FenceGpuAddress;
    ULONG     RptrBefore;
    ULONG     RptrAfter;
    ULONG     WptrTarget;
    ULONG     FenceWritten;        /* 1 = a GPU escreveu a fence esperada */
    ULONG     CompletedByRptrOnly; /* 1 = RPTR alcancou WPTR mas a fence NAO */
} BC250_WDDM_ESCAPE_SDMA_COPY;

typedef struct _BC250_WDDM_ESCAPE_SDMA_BENCHMARK {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     Iterations;
    ULONG     BlockSizeBytes;
    ULONG     AvgBandwidthMBs;
    ULONG     PeakBandwidthMBs;
} BC250_WDDM_ESCAPE_SDMA_BENCHMARK;

/* SDMA CONST_FILL — escape de DIAGNOSTICO (Fase 0).
   Existe para separar duas hipoteses quando a copia falha:
     - se FILL escreve, o motor executa pacotes de movimentacao de dados e o
       problema esta na LEITURA da origem pelo COPY;
     - se FILL nao escreve, o motor so' executa a FENCE.
   O padrao e' gerado pela propria GPU, sem depender de dado escrito pelo host. */
#define BC250_ESCAPE_SDMA_FILL                          0xBD0Cu

typedef struct _BC250_WDDM_ESCAPE_SDMA_FILL {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     Pattern;
} BC250_WDDM_ESCAPE_SDMA_FILL;

/* Topologia do shader array na Cyan Skillfish (GC 10.1.3): 2 SE x 2 SA.
   Usadas apenas para converter WGP/SA em totais — a quantidade de WGP ativos
   por SA e' SEMPRE lida do silicio, nunca fixada. */
#define BC250_SHADER_ENGINE_COUNT                       2u
#define BC250_SA_PER_SE                                 2u
#define BC250_SHADER_ARRAY_COUNT                        (BC250_SHADER_ENGINE_COUNT * BC250_SA_PER_SE)
#define BC250_WGP_PER_SA_MAX                            5u   /* 10 CU por SA */
#define BC250_WGP_PER_SA_STOCK                          3u   /* baseline 24 CU */

/* Versao do schema das estruturas de caps. Incrementar sempre que campos
   forem adicionados, para que consumidores detectem drivers antigos. */
#define BC250_CAPS_SCHEMA_VERSION                       2u

/* GFXOFF: nao existe na Cyan Skillfish.
   Os defines SMN_GFXOFF_* foram REMOVIDOS porque colidiam com o mailbox do
   SMU (0x03B10528 == SMU_Q2_CMD, 0x03B10998 == SMU_Q2_ARG). Ver o comentario
   completo em wddm_hw.c. Nao reintroduzir. */

/* Compute / GPGPU Escapes */
#define BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS             0xBD06u
#define BC250_ESCAPE_CMD_COMPUTE_DISPATCH               0xBD07u
#define BC250_ESCAPE_CMD_VRAM_WRITE                     0xBD08u
#define BC250_ESCAPE_CMD_VRAM_READ                      0xBD09u
#define BC250_ESCAPE_CMD_SUBMIT_IB                      0xBD0Bu

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_CAPS {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     NumCu;
    ULONG     NumWgp;
    ULONG     NumSimdPerCu;
    ULONG     NumTotalShaders;
    ULONG     GpuClockMhz;
    ULONG     GfxArch;
    ULONG     MaxWorkgroupSize;
    ULONG     WaveSize;
    ULONG     LdsSizeBytes;
    ULONGLONG VramTotalBytes;
    ULONGLONG ComputeVramBase;
    ULONG     ComputeVramSize;
    ULONG     DispatchCount;
    /* --- Procedencia dos numeros acima (Fase 0 e Fase 1). ---
       SchemaVersion != 0 marca um KMD que realmente preenche estes campos.
       Zero = driver antigo; consumidores devem tratar como DESCONHECIDO.
       CuCountSource / VramSizeSource: 0 = DETECTADO no silicio, 1 = ASSUMIDO. */
    ULONG     SchemaVersion;
    ULONG     CuCountSource;
    ULONG     VramSizeSource;
    ULONG     WgpMaskRaw;          /* Valor cru de SPI_PG_ENABLE_STATIC_WGP_MASK */
    ULONG     FirmwareReady;       /* 1 somente se todos os microcodigos do CP foram aceitos */
    /* Fase 1: GFXOFF, CPU cores e ReBAR */
    ULONG     GfxOffDisabled;   /* SEMPRE 0: GFXOFF nao se aplica a este ASIC */
    ULONG     GfxOffApplicable; /* 0 = nao aplicavel neste ASIC */
    ULONG     CpuCoresActive;      /* Detectado por KeQueryActiveProcessorCountEx */
    ULONG     ReBarActive;         /* 1 se BAR0 == config space, 0 se restrito */
    ULONGLONG Bar0SizeBytes;       /* Tamanho real da BAR0 */
    ULONGLONG GttSizeBytes;        /* Memoria de sistema GTT (0, ASSUMIDO) */
    ULONG     GttSizeSource;       /* 1 = ASSUMIDO */
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_DISPATCH {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG ShaderGpuAddress;
    ULONG     DimX;
    ULONG     DimY;
    ULONG     DimZ;
    ULONG     ThreadsPerGroupX;
    ULONG     ThreadsPerGroupY;
    ULONG     ThreadsPerGroupZ;
    ULONG     UserData[8];
    ULONG     UserDataCount;
    ULONG     FenceValue;          /* Valor ESPERADO da fence (escrito pela CPU como poison ~valor) */
    ULONG     DurationUs;
    ULONG     GrbmStatusBefore;
    ULONG     GrbmStatusAfter;
    /* --- Evidencia fisica (Fase 0). Nenhum destes campos e' fabricado. --- */
    ULONG     FenceObserved;       /* Valor REALMENTE lido da memoria apos o dispatch */
    ULONGLONG FenceGpuAddress;     /* Endereco GPU da fence */
    ULONG     ExecutionProven;     /* 1 somente se a GPU escreveu a fence esperada */
    ULONG     GrbmChanged;         /* 1 se GRBM_STATUS mudou durante a carga */
} BC250_WDDM_ESCAPE_COMPUTE_DISPATCH;

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;
    ULONG     ByteCount;
    ULONG     Data[1024];
} BC250_WDDM_ESCAPE_VRAM_RW;

typedef struct _BC250_WDDM_ESCAPE_SUBMIT_IB {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG IbGpuAddress;
    ULONG     IbDwordCount;
    ULONG     VmId;
    ULONGLONG FenceGpuAddress;
    ULONG     FenceValue;
    ULONG     GrbmStatusBefore;
    ULONG     GrbmStatusAfter;
    ULONG     RptrBefore;
    ULONG     RptrAfter;
    ULONG     DurationUs;
} BC250_WDDM_ESCAPE_SUBMIT_IB;

/* SMU Telemetry & Thermal Escape (0xBD10) */
#define BC250_ESCAPE_CMD_QUERY_TELEMETRY                0xBD10u

typedef struct _BC250_WDDM_ESCAPE_TELEMETRY {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     GfxTemperature;
    ULONG     GfxTemperatureC;
    ULONG     SocTemperatureC;
    ULONG     EdgeTemperatureC;
    ULONG     HotspotTemperatureC;
    ULONG     GfxVoltageMv;
    ULONG     SocVoltageMv;
    ULONG     GfxClockMhz;
    ULONG     SocClockMhz;
    ULONG     MemClockMhz;
    ULONG     PowerMilliwatts;
    ULONG     PowerWatts;
    ULONG     SmuStatus;
    ULONG     Flags;
} BC250_WDDM_ESCAPE_TELEMETRY;

/* Low-Level SMN / SMU Escapes */
#define BC250_ESCAPE_SMN_READ                           0xBD20u
#define BC250_ESCAPE_SMN_WRITE                          0xBD21u
#define BC250_ESCAPE_SMU_SEND_MSG                       0xBD22u
#define BC250_ESCAPE_SET_POWER_PROFILE                  0xBD23u

typedef struct _BC250_WDDM_ESCAPE_SMN_RW {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     Address;
    ULONG     Value;
} BC250_WDDM_ESCAPE_SMN_RW;

typedef struct _BC250_WDDM_ESCAPE_SMU_MSG {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     QueueId;
    ULONG     MsgId;
    ULONG     Param;
    ULONG     SmuStatus;
    ULONG     ArgReadback;
} BC250_WDDM_ESCAPE_SMU_MSG;

typedef struct _BC250_WDDM_ESCAPE_POWER_PROFILE {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     TargetFreqMhz;   /* 400 - 2350 MHz, or 0 for Unforce */
    ULONG     TargetVoltageMv; /* 700 - 1125 mV, or 0 for Unforce */
    ULONG     PerfProfile;     /* 1 (Eco/Idle) or 3 (Performance/IF) */
    ULONG     MaxTemperatureC; /* 60 - 85 C, default 85 */
    ULONG     Flags;           /* 0 = Apply Profile, 1 = Unforce/Restore Default */
    ULONG     ResultStatus;    /* Out: 0 = OK, error code */
    ULONG     CurrentFreqMhz;  /* Out: Readback frequency */
    ULONG     CurrentVid;      /* Out: Readback VID */
    ULONG     CurrentVoltageMv;/* Out: Readback Voltage in mV */
} BC250_WDDM_ESCAPE_POWER_PROFILE;

/* Low-Level PSP Raw Injection Escape */
#define BC250_ESCAPE_PSP_RAW_CMD                        0xBD30u

typedef struct _BC250_WDDM_ESCAPE_PSP_RAW {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     Opcode;
    ULONG     SubOp;
    ULONG     BufSizeBytes;
    ULONG     Data[128];
    ULONG     RespStatus;
    ULONG     RespSession;
    ULONG     FenceValue;
    ULONG     DurationUs;
} BC250_WDDM_ESCAPE_PSP_RAW;

/* Real-Time Studio / Dashboard Escapes */
#define BC250_ESCAPE_QUERY_RING_STATUS                  0xBD40u
#define BC250_ESCAPE_QUERY_MEMORY_MAP                   0xBD41u
#define BC250_ESCAPE_QUERY_EVENT_LOG                    0xBD42u

typedef struct _BC250_WDDM_ESCAPE_RING_STATUS {
    BC250_WDDM_ESCAPE_HEADER Header;
    /* CP PM4 Ring 0 */
    ULONGLONG CpRingGpuBase;
    ULONG     CpRingSizeBytes;
    ULONG     CpRingWptr;
    ULONG     CpRingRptr;
    ULONG     CpRingOccupancyBytes;
    ULONG     CpRingSubmitCount;
    /* SDMA0 Copy Ring */
    ULONGLONG SdmaRingGpuBase;
    ULONG     SdmaRingSizeBytes;
    ULONG     SdmaRingWptr;
    ULONG     SdmaRingRptr;
    ULONG     SdmaRingOccupancyBytes;
    ULONG     SdmaCopyCount;
    ULONG     SdmaBandwidthMBs;
    /* PSP GPCOM Ring */
    ULONGLONG PspRingGpuBase;
    ULONG     PspRingSizeBytes;
    ULONG     PspRingWptr;
    ULONG     PspFenceValue;
    ULONG     PspCmdCount;
    /* Engine Status */
    ULONG     GrbmStatus;
    ULONG     CpStat;
    LARGE_INTEGER TimestampQpc;
    LARGE_INTEGER QpcFrequency;
} BC250_WDDM_ESCAPE_RING_STATUS;

typedef struct _BC250_WDDM_ESCAPE_MEMORY_MAP {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG TotalPhysicalBytes;   /* 16 GB GDDR6 */
    ULONGLONG Bar0PhysicalBase;     /* e.g. 0xF400000000 */
    ULONGLONG Bar0ApertureBytes;    /* 256 MB */
    /* Segments / Carveouts */
    ULONGLONG GopFbBase;            /* 0x00000000 */
    ULONG     GopFbSizeBytes;       /* ~10 MB */
    ULONGLONG StagingComputeBase;   /* 0x01000000 */
    ULONG     StagingComputeBytes;  /* 224 MB */
    ULONGLONG TmrBase;              /* 0x0F800000 */
    ULONG     TmrSizeBytes;         /* 4 MB */
    ULONGLONG SdmaRingBase;         /* 0x0FD00000 */
    ULONG     SdmaRingSizeBytes;    /* 16 KB */
    ULONGLONG CpRingBase;           /* 0x0FE00000 */
    ULONG     CpRingSizeBytes;      /* 16 KB */
    ULONGLONG DynamicPoolBase;      /* 0x10000000 */
    ULONGLONG DynamicPoolBytes;     /* ~15.75 GB */
    ULONG     ScreenWidth;
    ULONG     ScreenHeight;
    ULONG     ScreenPitch;
} BC250_WDDM_ESCAPE_MEMORY_MAP;

#define BC250_EVENT_LOG_MAX 32

typedef struct _BC250_EVENT_ENTRY {
    ULONG         EventId;
    ULONG         Param1;
    ULONG         Param2;
    LARGE_INTEGER Timestamp;
} BC250_EVENT_ENTRY;

typedef struct _BC250_WDDM_ESCAPE_EVENT_LOG {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG             TotalEventsCount;
    ULONG             EntryCount;
    ULONG             PresentCount;
    ULONG             VsyncCount;
    ULONG             IbSubmitCount;
    ULONG             ComputeDispatchCount;
    ULONG             SdmaCopyCount;
    BC250_EVENT_ENTRY Entries[BC250_EVENT_LOG_MAX];
} BC250_WDDM_ESCAPE_EVENT_LOG;

/* WDDM 2.0 Process, Device, Context and Allocation Objects */
typedef struct _BC250_WDDM_PROCESS {
    HANDLE                  hProcess;
    D3DGPU_VIRTUAL_ADDRESS  RootPageTableAddress;
    ULONG                   ProcessFlags;
    LIST_ENTRY              ListEntry;
} BC250_WDDM_PROCESS, *PBC250_WDDM_PROCESS;

typedef struct _BC250_WDDM_DEVICE_CTX {
    HANDLE                  hDevice;
    PBC250_WDDM_PROCESS     Process;
    ULONG                   DeviceFlags;
    LIST_ENTRY              ListEntry;
} BC250_WDDM_DEVICE_CTX, *PBC250_WDDM_DEVICE_CTX;

typedef struct _BC250_WDDM_CONTEXT {
    HANDLE                  hContext;
    PBC250_WDDM_DEVICE_CTX  Device;
    UINT                    NodeOrdinal;
    UINT                    EngineAffinity;
    ULONG                   ContextFlags;
    ULONG                   LastSubmittedFence;
} BC250_WDDM_CONTEXT, *PBC250_WDDM_CONTEXT;

typedef struct _BC250_WDDM_ALLOCATION {
    HANDLE                  hAllocation;
    UINT                    SegmentId;
    PHYSICAL_ADDRESS        PhysicalAddress;
    D3DGPU_VIRTUAL_ADDRESS  GpuVirtualAddress;
    SIZE_T                  SizeBytes;
    ULONG                   AlignmentBytes;
    BOOLEAN                 CpuVisible;
    BOOLEAN                 Primary;
} BC250_WDDM_ALLOCATION, *PBC250_WDDM_ALLOCATION;

/* Master Device Extension Struct */
typedef struct _BC250_WDDM_DEVICE {
    PDEVICE_OBJECT              PhysicalDeviceObject;
    DXGKRNL_INTERFACE           DxgkInterface;

    /* MMIO Register Space (BAR5) */
    PHYSICAL_ADDRESS            MmioPhysicalBase;
    PVOID                       MmioVirtualBase;
    SIZE_T                      MmioSize;

    /* VRAM Aperture (BAR0) - Base of GDDR6 */
    PHYSICAL_ADDRESS            FbPhysicalBase;
    PVOID                       FbVirtualBase;
    SIZE_T                      FbSize;

    /* Framebuffer inherited from UEFI GOP scanout */
    PHYSICAL_ADDRESS            DisplayPhysicalBase;
    PVOID                       DisplayVirtualBase;
    SIZE_T                      DisplaySize;
    BOOLEAN                     DisplayInsideVram;

    /* Display Mode / Post Display Ownership */
    ULONG                       ScreenWidth;
    ULONG                       ScreenHeight;
    ULONG                       ScreenPitch;
    D3DDDIFORMAT                ScreenFormat;
    ULONG                       ActiveWidth;
    ULONG                       ActiveHeight;
    ULONG                       ActivePitch;
    ULONG                       OffsetX;
    ULONG                       OffsetY;

    /* Effective Destination Rectangle / Nearest-Neighbor Scaling */
    ULONG                       DestWidth;
    ULONG                       DestHeight;
    PULONG                      ScaleXMap;
    PULONG                      ScaleYMap;
    BOOLEAN                     ScaleActive;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING  Scaling;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    BOOLEAN                     PostDisplayAcquired;
    BOOLEAN                     HardwareInitialized;
    BOOLEAN                     SkipHwBringup;
    BOOLEAN                     EnableDangerousEscapes;
    BOOLEAN                     DeviceStarted;
    BOOLEAN                     SurpriseRemoved;
    BOOLEAN                     SourceVisible;
    BOOLEAN                     VidPnCommitted;
    BOOLEAN                     ForceFullPresent;
    DEVICE_POWER_STATE          AdapterPowerState;
    DEVICE_POWER_STATE          MonitorPowerState;

    /* Presentation Path Counters */
    ULONG                       PresentCount;
    ULONG                       PresentFirstSeq;

    /* Silicon Bring-up Granular Telemetry */
    NTSTATUS                    HwStatusSmuGfxOff;
    NTSTATUS                    HwBringupStatus;
    NTSTATUS                    HwStatusPspRing;
    NTSTATUS                    HwStatusTmr;
    NTSTATUS                    HwStatusFw[7];      /* SDMA0, RLC, CE, PFP, ME, MEC, MEC2 */

    /* Raw PSP Firmware Response Status */
    ULONG                       HwPspFwResp[16];
    ULONG                       HwPspFwSession[16];
    ULONG                       HwPspFwSeen[16];

    /* SMU Mailbox Readback */
    ULONG                       HwSmuCmdReadback;
    ULONG                       HwSmuArgReadback;
    ULONG                       HwSmuRspAtSubmit;

    /* SMU Mailbox Handshake */
    ULONG                       HwSmuTestQ0Status;
    ULONG                       HwSmuTestQ0Rsp;
    ULONG                       HwSmuTestQ0Arg;
    ULONG                       HwSmuTestQ2Status;
    ULONG                       HwSmuTestQ2Rsp;
    ULONG                       HwSmuTestQ2Arg;
    ULONG                       HwSmuVersionRsp;
    ULONG                       HwSmuVersion;
    ULONG                       HwPspTosReg;
    ULONG                       HwSmuRawResponse;
    ULONG                       HwFbOffsetReg;
    ULONG                       HwL2CntlReadback;
    ULONG                       HwRbCntlReadback;
    ULONG                       HwMeCntlReadback;
    ULONG                       HwRptrAfterInit;

    /* Dynamic Compute Units (CU) & WGP Topology (24 CUs stock -> up to 40 CUs unlocked) */
    ULONG                       HwCuUnlockMode;     /* 0 = Auto-detect, 1 = Force 24 CU stock, 2 = Unlock 40 CU */
    ULONG                       HwWgpMask;          /* Bitmask of active WGPs per array (e.g. 0x07 for 24 CUs, 0x0F for 32 CUs, 0x1F for 40 CUs) */
    ULONG                       HwNumCu;            /* Active Compute Units (e.g. 24 or 40) */
    ULONG                       HwNumWgp;           /* Active Workgroup Processors (e.g. 12 or 20) */
    ULONG                       HwNumShaders;       /* Active Stream Processors / ALUs (e.g. 1536 or 2560) */
    ULONG                       HwActiveCuMask[4];  /* Active CU mask per Shader Engine */
    BOOLEAN                     HwCuUnlockSuccess;  /* TRUE if 40 CU unlock succeeded */

    /* CP PM4 Command Ring State (VRAM Offset 0x0FE00000) */
    ULONG                       GfxWptr;
    ULONG                       GfxRingVramOffset;
    ULONG                       GfxRingSize;

    /* SDMA0 Hardware Copy Engine State (VRAM Offset 0x0FD00000) */
    BOOLEAN                     SdmaInitialized;
    NTSTATUS                    HwStatusSdmaRing;
    ULONG                       HwSdmaRbCntlReadback;
    ULONG                       SdmaWptr;
    ULONG                       SdmaFenceSequence;
    ULONG                       SdmaRingVramOffset;
    ULONG                       SdmaRingSize;
    ULONGLONG                   SdmaRingGpuBase;
    PVOID                       SdmaRingVirtualBase;
    ULONG                       SdmaRptrVramOffset;
    ULONG                       SdmaFenceVramOffset;
    ULONG                       SdmaBandwidthMBs;
    ULONG                       SdmaCopyCount;
    FAST_MUTEX                  SdmaLock;

    /* PSP GPCOM Ring State (Contiguous Host RAM DMA) */
    PVOID                       PspRingVa;
    PVOID                       PspCmdVa;
    PVOID                       PspFenceVa;
    PHYSICAL_ADDRESS            PspRingPa;
    PHYSICAL_ADDRESS            PspCmdPa;
    PHYSICAL_ADDRESS            PspFencePa;
    ULONG                       PspRingWptr;
    ULONG                       PspFenceValue;
    BOOLEAN                     PspRingActive;

    /* Synthetic VSync Generator (DPC Timer) */
    KTIMER                      VsyncTimer;
    KDPC                        VsyncDpc;
    BOOLEAN                     VsyncEnabled;
    BOOLEAN                     VsyncRequested;
    BOOLEAN                     VsyncTimerInitialized;
    ULONG                       VsyncCount;
    ULONG                       InterruptTypeMask;
    LARGE_INTEGER               VsyncLastQpc;
    LARGE_INTEGER               VsyncQpcFrequency;

    /* Synthetic EDID */
    UCHAR                       Edid[BC250_EDID_BLOCK_SIZE];
    BOOLEAN                     EdidValid;

    /* Compute & GPGPU State */
    ULONG                       ComputeDispatchCount;
    ULONG                       ComputeFenceSequence;
    ULONG                       ComputeFenceVramOffset; /* fence fisica do compute em VRAM */
    ULONG                       ComputeFailNoFence;     /* dispatches sem fence da GPU */

    /* Procedencia da configuracao detectada (Fase 0) */
    ULONG                       HwCuCountSource;    /* 0 = detectado, 1 = assumido */
    ULONG                       HwSpiPgRaw;         /* leitura crua de SPI_PG_ENABLE_STATIC_WGP_MASK */
    ULONG                       HwCcArrayRaw;       /* leitura crua de CC_GC_SHADER_ARRAY_CONFIG */
    ULONG                       HwWgpSourcesDisagree; /* 1 = CC e SPI_PG discordam */
    ULONG                       HwGrbmStatusAtInit; /* GRBM_STATUS logo apos o bring-up */
    ULONG                       HwCpStatAtInit;     /* CP_STAT logo apos o bring-up */
    BOOLEAN                     HwFirmwareReady;    /* TRUE so' se TODOS os microcodigos do CP foram aceitos */

    /* Fase 1: GFXOFF State */
    /* Sondas cruas do bloco 0x03B1xxxx, mantidas apenas como diagnostico.
       NAO sao estado de GFXOFF: esse bloco e' o mailbox do SMU. */
    ULONG                       HwSmnProbeQ2Cmd;
    ULONG                       HwSmnProbeQ2Arg;

    /* Fase 1: Chaves Fail-Closed */
    ULONG                       HwUnhaltCp;         /* 0 = manter CP halted por padrao */
    ULONG                       HwInitMaxStep;      /* 0 = sem limite, N = para apos o passo N */
    ULONG                       HwInitFirmware;     /* 1 = carregar firmware, 0 = pular */
    ULONG                       HwInitGfxRing;      /* 1 = inicializar CP ring, 0 = pular */
    ULONG                       HwInitSdmaRing;     /* 1 = inicializar SDMA ring, 0 = pular */

    /* Fase 1: Deteccao de CPU e ReBAR */
    ULONG                       HwCpuCoresActive;   /* Cores detectados */
    ULONGLONG                   HwBar0Size;         /* Tamanho BAR0 detectado */
    ULONG                       HwReBarActive;      /* 1 se ReBAR ativo */

    /* SMU Telemetry & Thermal Buffer */
    PVOID                       SmuMetricsVa;
    PHYSICAL_ADDRESS            SmuMetricsPa;
    BC250_WDDM_ESCAPE_TELEMETRY LastTelemetry;

    /* Event Log Circular Buffer & Counters */
    BC250_EVENT_ENTRY           EventLog[BC250_EVENT_LOG_MAX];
    ULONG                       EventLogHead;
    ULONG                       EventLogTotal;
    ULONG                       IbSubmitCount;
    ULONG                       PspCmdCount;

    /* Process & Context Lists */
    LIST_ENTRY                  ProcessListHead;
    LIST_ENTRY                  DeviceListHead;
    ULONG                       ActiveProcessCount;
    ULONG                       ActiveContextCount;

    FAST_MUTEX                  DeviceMutex;
} BC250_WDDM_DEVICE, *PBC250_WDDM_DEVICE;

/* Register Defines for Escapes and Status
   Derivados por SOC15: BAR5_byte = (segment_base + mmREG) * 4,
   segment_base = 0x1260 (BASE_IDX 0) ou 0xA000 (BASE_IDX 1).
   Fonte: gc_10_1_0_offset.h. NAO editar a mao — derivar. */
#ifndef A_CP_RB0_RPTR
#define A_CP_RB0_RPTR                                   0x08700u  /* mm 0x0F60 idx0 */
#endif
#ifndef A_GRBM_STATUS
/* CORRIGIDO na Fase 0: era 0x03040 (lia 0x00000000 constante).
   O offset real e' 0x08010 — comprovado lendo 0x00003028 no silicio. */
#define A_GRBM_STATUS                                   0x08010u  /* mm 0x0DA4 idx0 */
#endif
#ifndef A_CP_STAT
/* CORRIGIDO na Fase 0: era 0x08684 (lia fill pattern 0xDEF4DEF4). */
#define A_CP_STAT                                       0x08680u  /* mm 0x0F40 idx0 */
#endif
#ifndef A_SDMA0_GFX_RB_RPTR
#define A_SDMA0_GFX_RB_RPTR                             0x04B8Cu
#endif

/* Hardware Register Access */
static __inline ULONG
BcRegRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG RegOffset)
{
    if (!Dev || !Dev->MmioVirtualBase) return 0;
    return *(volatile ULONG *)((PUCHAR)Dev->MmioVirtualBase + (RegOffset & 0x7FFFC));
}

static __inline VOID
BcRegWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG RegOffset, _In_ ULONG Value)
{
    if (!Dev || !Dev->MmioVirtualBase) return;
    *(volatile ULONG *)((PUCHAR)Dev->MmioVirtualBase + (RegOffset & 0x7FFFC)) = Value;
}

/* Hardware Bring-up & Submission Prototypes */
NTSTATUS Bc250HwBringup(_In_ PBC250_WDDM_DEVICE Dev);
VOID     Bc250HwShutdown(_In_ PBC250_WDDM_DEVICE Dev);
NTSTATUS Bc250HwSubmitPm4(_In_ PBC250_WDDM_DEVICE Dev, _In_ const ULONG *Data, _In_ ULONG DwordCount, _Out_ PULONG RptrBefore, _Out_ PULONG RptrAfter);
NTSTATUS Bc250HwSubmitIndirectBuffer(_In_ PBC250_WDDM_DEVICE Dev, _Inout_ BC250_WDDM_ESCAPE_SUBMIT_IB *IbSubmit);

/* Telemetry & Thermal Prototypes */
NTSTATUS Bc250HwQueryTelemetry(_In_ PBC250_WDDM_DEVICE Dev, _Out_ BC250_WDDM_ESCAPE_TELEMETRY *Telem);
NTSTATUS Bc250HwSetPowerProfile(_In_ PBC250_WDDM_DEVICE Dev, _Inout_ BC250_WDDM_ESCAPE_POWER_PROFILE *Profile);

/* SDMA0 Hardware Copy Engine Prototypes */
NTSTATUS Bc250SdmaRingInit(_In_ PBC250_WDDM_DEVICE Dev);
VOID     Bc250SdmaRingShutdown(_In_ PBC250_WDDM_DEVICE Dev);
NTSTATUS Bc250SdmaSubmitCopy(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG SrcGpuAddr, _In_ ULONGLONG DstGpuAddr, _In_ ULONG Bytes, _Out_opt_ PULONG DurationUs, _Out_opt_ PULONG FenceValue, _Out_opt_ BC250_WDDM_ESCAPE_SDMA_COPY *Evidence);
NTSTATUS Bc250SdmaSubmitFill(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG DstGpuAddr, _In_ ULONG Bytes, _In_ ULONG Pattern);
NTSTATUS Bc250SdmaRunBenchmark(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Iterations, _In_ ULONG BlockSizeBytes, _Out_ PULONG AvgBandwidthMBs, _Out_ PULONG PeakBandwidthMBs);

/* Compute & GPGPU Engine Prototypes */
/* Compute & GPGPU Engine Prototypes */
NTSTATUS Bc250ComputeDispatch(_In_ PBC250_WDDM_DEVICE Dev, _Inout_ BC250_WDDM_ESCAPE_COMPUTE_DISPATCH *Dispatch);
NTSTATUS Bc250HwSubmitComputeDispatch(_In_ PBC250_WDDM_DEVICE Dev, _Inout_ BC250_WDDM_ESCAPE_COMPUTE_DISPATCH *Dispatch);
NTSTATUS Bc250HwVramWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG Offset, _In_ const VOID *Buffer, _In_ ULONG Bytes);
NTSTATUS Bc250HwVramRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG Offset, _Out_ VOID *Buffer, _In_ ULONG Bytes);

/* Event Logging Helper */
VOID     Bc250LogEvent(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG EventId, _In_ ULONG Param1, _In_ ULONG Param2);

/* Low-Level SMN / SMU & PSP Prototypes */
ULONG    Bc250HwSmnRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG SmnAddr);
VOID     Bc250HwSmnWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG SmnAddr, _In_ ULONG Value);
NTSTATUS Bc250HwSmuSendRawMsg(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG QueueId, _In_ ULONG MsgId, _In_ ULONG Param, _Out_ PULONG SmuStatus, _Out_ PULONG ArgReadback);
NTSTATUS Bc250HwPspRawSubmit(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Opcode, _In_ ULONG SubOp, _In_ ULONG BufSizeBytes, _Inout_updates_bytes_(BufSizeBytes) PULONG Data, _Out_ PULONG RespStatus, _Out_ PULONG RespSession, _Out_ PULONG FenceValue, _Out_ PULONG DurationUs);

/* Real-Time Studio Query Prototypes */
VOID     Bc250HwQueryRingStatus(_In_ PBC250_WDDM_DEVICE Dev, _Out_ BC250_WDDM_ESCAPE_RING_STATUS *Rings);
VOID     Bc250HwQueryMemoryMap(_In_ PBC250_WDDM_DEVICE Dev, _Out_ BC250_WDDM_ESCAPE_MEMORY_MAP *Map);
VOID     Bc250HwQueryEventLog(_In_ PBC250_WDDM_DEVICE Dev, _Out_ BC250_WDDM_ESCAPE_EVENT_LOG *Log);

#endif /* _BC250_WDDM_H_ */
