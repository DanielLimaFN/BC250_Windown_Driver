/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    wddm.h

Abstract:
    Header definitions for the Native AMD BC-250 WDDM Kernel-Mode Display-Only Driver (KMDOD).

Environment:
    Kernel mode only (WDDM 1.2+ Display-Only Miniport)

--*/

#ifndef _BC250_WDDM_H_
#define _BC250_WDDM_H_

#if !defined(_WINDOWS_) && !defined(_INC_WINDOWS) && !defined(_USER_MODE)
#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
#endif
#include <emmintrin.h>

#define BC250_WDDM_DRIVER_TAG                           'DWcB'

/* Hardware IDs */
#define AMD_VENDOR_ID                                   0x1002
#define AMDBC250_DEVICE_ID                              0x13FE

/* Um bloco base de EDID tem 128 bytes; nao publicamos blocos de extensao. */
#define BC250_EDID_BLOCK_SIZE                           128u

/* Canonical BAR definitions for BC-250 */
#define BC250_BAR5_MMIO_SIZE                            0x00080000ULL /* 512 KB */
#define BC250_BAR0_VRAM_SIZE                            0x10000000ULL /* 256 MB */
#define BC250_VRAM_GPU_BASE                             0xF400000000ULL

/* Escape IOCTLs for User-Mode Driver (RADV / Vulkan UMD Interface) */
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

#define BC250_ESCAPE_SDMA_COPY              0xBD04u
#define BC250_ESCAPE_SDMA_BENCHMARK         0xBD05u

typedef struct _BC250_WDDM_ESCAPE_SDMA_COPY {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG SrcGpuAddress;
    ULONGLONG DstGpuAddress;
    ULONG     SizeBytes;
    ULONG     FenceValue;
    ULONG     DurationUs;
    ULONG     BandwidthMBs;
} BC250_WDDM_ESCAPE_SDMA_COPY;

typedef struct _BC250_WDDM_ESCAPE_SDMA_BENCHMARK {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     Iterations;
    ULONG     BlockSizeBytes;
    ULONG     AvgBandwidthMBs;
    ULONG     PeakBandwidthMBs;
} BC250_WDDM_ESCAPE_SDMA_BENCHMARK;

/* Compute & GPGPU Escapes (Phase 5) */
#define BC250_ESCAPE_CMD_QUERY_COMPUTE_CAPS 0xBD06u
#define BC250_ESCAPE_CMD_COMPUTE_DISPATCH   0xBD07u
#define BC250_ESCAPE_CMD_VRAM_WRITE         0xBD08u
#define BC250_ESCAPE_CMD_VRAM_READ          0xBD09u

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_CAPS {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     NumCu;              /* 24 CUs */
    ULONG     NumWgp;             /* 12 WGPs */
    ULONG     NumSimdPerCu;       /* 2 SIMD32 per CU (RDNA1) */
    ULONG     NumTotalShaders;    /* 1536 Stream Processors */
    ULONG     GpuClockMhz;        /* 1500 MHz */
    ULONG     GfxArch;            /* 1010 (GFX10.1 / Navi12) */
    ULONG     MaxWorkgroupSize;   /* 1024 */
    ULONG     WaveSize;           /* 32 */
    ULONG     LdsSizeBytes;       /* 64 KB per WGP */
    ULONGLONG VramTotalBytes;     /* Abertura total de VRAM */
    ULONGLONG ComputeVramBase;    /* Base segura de VRAM para compute (0xF401000000) */
    ULONG     ComputeVramSize;    /* Tamanho da staging area de compute (16 MB) */
    ULONG     DispatchCount;      /* Total de dispatches efetuados */
} BC250_WDDM_ESCAPE_COMPUTE_CAPS;

typedef struct _BC250_WDDM_ESCAPE_COMPUTE_DISPATCH {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG ShaderGpuAddress;   /* Endereço GPU do Shader na VRAM */
    ULONG     DimX;               /* Grid X (em workgroups) */
    ULONG     DimY;               /* Grid Y */
    ULONG     DimZ;               /* Grid Z */
    ULONG     ThreadsPerGroupX;   /* Threads por Group X (default 32) */
    ULONG     ThreadsPerGroupY;   /* Threads por Group Y (default 1) */
    ULONG     ThreadsPerGroupZ;   /* Threads por Group Z (default 1) */
    ULONG     UserData[8];        /* Registradores COMPUTE_USER_DATA_0..7 */
    ULONG     UserDataCount;      /* Quantidade de user data (0..8) */
    ULONG     FenceValue;         /* Valor da cerca gerada/esperada */
    ULONG     DurationUs;         /* Tempo de execução medido na GPU */
    ULONG     GrbmStatusBefore;   /* Readback de GRBM_STATUS antes */
    ULONG     GrbmStatusAfter;    /* Readback de GRBM_STATUS depois */
} BC250_WDDM_ESCAPE_COMPUTE_DISPATCH;

typedef struct _BC250_WDDM_ESCAPE_VRAM_RW {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONGLONG VramOffset;         /* Offset relativo a FbVirtualBase (ex: 0x01000000) */
    ULONG     ByteCount;          /* Quantidade de bytes (max 4096 por chamada) */
    ULONG     Data[1024];         /* Buffer de dados */
} BC250_WDDM_ESCAPE_VRAM_RW;

/* Telemetry & Thermal Escapes */
#define BC250_ESCAPE_CMD_QUERY_TELEMETRY    0xBD10u

typedef struct _BC250_WDDM_ESCAPE_TELEMETRY {
    BC250_WDDM_ESCAPE_HEADER Header;
    ULONG     GfxTemperature;     /* GPU Core / Edge temp em miligraus Celsius (ex: 55000 = 55.0 C) */
    ULONG     GfxTemperatureC;    /* GPU Core / Edge temp em graus Celsius (ex: 55 C) */
    ULONG     SocTemperatureC;    /* SoC / Northbridge temp em graus Celsius */
    ULONG     EdgeTemperatureC;   /* Edge temp em graus Celsius */
    ULONG     HotspotTemperatureC;/* Hotspot / Junction temp em graus Celsius */
    ULONG     GfxVoltageMv;       /* Tensão VDDGFX em mV (ex: 850 mV) */
    ULONG     SocVoltageMv;       /* Tensão VDDNB / SoC em mV */
    ULONG     GfxClockMhz;        /* Clock atual GFXCLK em MHz (ex: 1500 MHz) */
    ULONG     SocClockMhz;        /* Clock SOCCLK em MHz */
    ULONG     MemClockMhz;        /* Clock MEMCLK em MHz */
    ULONG     PowerMilliwatts;    /* Potência PPT em mW (ex: 45000 = 45.0 W) */
    ULONG     PowerWatts;         /* Potência PPT em W (ex: 45 W) */
    ULONG     SmuStatus;          /* Código de status retornado pelo SMU */
    ULONG     Flags;              /* 1 = SMU Metrics Table, 2 = Fallback probe */
} BC250_WDDM_ESCAPE_TELEMETRY;

#if !defined(_WINDOWS_) && !defined(_INC_WINDOWS) && !defined(_USER_MODE)
/* Native KMDOD Device Extension */
typedef struct _BC250_WDDM_DEVICE {
    PDEVICE_OBJECT              DeviceObject;
    PDEVICE_OBJECT              PhysicalDeviceObject;
    DXGKRNL_INTERFACE           DxgkInterface;

    /* MMIO (BAR5) */
    PHYSICAL_ADDRESS            MmioPhysicalBase;
    PVOID                       MmioVirtualBase;
    SIZE_T                      MmioSize;

    /* Abertura de VRAM (BAR0) -- base do aperture, offset 0 da VRAM.
       O anel PM4 vive aqui, em GfxRingVramOffset. */
    PHYSICAL_ADDRESS            FbPhysicalBase;
    PVOID                       FbVirtualBase;
    SIZE_T                      FbSize;

    /* Framebuffer de scanout herdado do UEFI GOP. NAO e necessariamente o
       offset 0 do BAR0: manter separado evita escrever pixels no lugar
       errado e evita mapear alem do fim do BAR0. */
    PHYSICAL_ADDRESS            DisplayPhysicalBase;
    PVOID                       DisplayVirtualBase;
    SIZE_T                      DisplaySize;
    BOOLEAN                     DisplayInsideVram;  /* TRUE: aponta dentro de FbVirtualBase */

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

    /* Retangulo de destino efetivo no scanout nativo. Em modo centralizado
       vale ActiveWidth/ActiveHeight; em modo esticado vale o scanout inteiro.
       Os mapas nearest-neighbor traduzem coluna/linha de destino para
       coluna/linha de origem e sao validos apenas quando ScaleActive. */
    ULONG                       DestWidth;
    ULONG                       DestHeight;
    PULONG                      ScaleXMap;
    PULONG                      ScaleYMap;
    BOOLEAN                     ScaleActive;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING  Scaling;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    BOOLEAN                     PostDisplayAcquired;
    BOOLEAN                     HardwareInitialized;
    BOOLEAN                     SkipHwBringup;      /* ajuste de registro */
    BOOLEAN                     EnableDangerousEscapes;
    BOOLEAN                     DeviceStarted;
    BOOLEAN                     SurpriseRemoved;
    BOOLEAN                     SourceVisible;
    BOOLEAN                     VidPnCommitted;
    BOOLEAN                     ForceFullPresent;
    DEVICE_POWER_STATE          AdapterPowerState;
    DEVICE_POWER_STATE          MonitorPowerState;

    /* Contadores do caminho de apresentacao. Gravar no registro a cada
       frame seria fatal (60 Hz x transacao de registro), entao contamos em
       memoria e publicamos so na primeira chamada e a cada 256. */
    ULONG                       PresentCount;
    ULONG                       PresentFirstSeq;

    /* Telemetria granular do bring-up.
       Cada etapa grava seu proprio NTSTATUS/readback: um unico
       HwBringupStatus agregado nao prova nada sobre as etapas seguintes. */
    NTSTATUS                    HwStatusSmuGfxOff;
    NTSTATUS                    HwBringupStatus;
    NTSTATUS                    HwStatusPspRing;
    NTSTATUS                    HwStatusTmr;
    NTSTATUS                    HwStatusFw[7];      /* SDMA0, RLC, CE, PFP, ME, MEC, MEC2 */

    /* Resposta CRUA do PSP por carga de firmware, indexada por PSP_FW_*
       (ME=1, PFP=2, CE=3, MEC=4, MEC2=5, RLC_G=8, SDMA0=9), nao pela ordem de
       chamada. HwStatusFw guarda apenas um NTSTATUS generico; sem o codigo do
       proprio PSP nao ha como saber se a recusa foi de assinatura, de tamanho
       ou de tipo de firmware nao suportado.
       Seen distingue "PSP respondeu" de "nunca chegamos a submeter". */
    ULONG                       HwPspFwResp[16];
    ULONG                       HwPspFwSession[16];
    ULONG                       HwPspFwSeen[16];

    /* Readback do mailbox do SMU logo apos a escrita. Distingue "aperture SMN
       quebrada" de "SMU nao responde" -- ver BcSmuSendMsg. */
    ULONG                       HwSmuCmdReadback;
    ULONG                       HwSmuArgReadback;
    ULONG                       HwSmuRspAtSubmit;

    /* Handshake do mailbox via PPSMC_MSG_TestMessage (0x01): resposta deve ser
       1 e o argumento deve voltar como enviado+1. Prova o caminho sem
       depender de adivinhar IDs de mensagem. */
    ULONG                       HwSmuTestQ0Status;
    ULONG                       HwSmuTestQ0Rsp;
    ULONG                       HwSmuTestQ0Arg;
    ULONG                       HwSmuTestQ2Status;
    ULONG                       HwSmuTestQ2Rsp;
    ULONG                       HwSmuTestQ2Arg;
    ULONG                       HwSmuVersionRsp;
    ULONG                       HwSmuVersion;
    ULONG                       HwPspTosReg;        /* PSP_C2PMSG_64 antes da criacao do anel */
    ULONG                       HwSmuRawResponse;   /* valor cru do RSP do SMU */
    ULONG                       HwFbOffsetReg;      /* GCMC_VM_FB_OFFSET */
    ULONG                       HwL2CntlReadback;   /* GCVM_L2_CNTL apos escrita */
    ULONG                       HwRbCntlReadback;   /* CP_RB0_CNTL apos escrita */
    ULONG                       HwMeCntlReadback;   /* CP_ME_CNTL apos unhalt */
    ULONG                       HwRptrAfterInit;    /* CP_RB0_RPTR apos init do anel */

    /* CP PM4 Command Ring State */
    ULONG                       GfxWptr;
    ULONG                       GfxRingVramOffset;
    ULONG                       GfxRingSize;

    /* SDMA0 Hardware Copy Engine State */
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

    /* O PSP continua fazendo DMA para o GPCOM ring enquanto ele estiver
       registrado. Essas paginas precisam sobreviver ate Bc250HwShutdown. */
    PVOID                       PspRingVa;
    PVOID                       PspCmdVa;
    PVOID                       PspFenceVa;
    PHYSICAL_ADDRESS            PspRingPa;
    PHYSICAL_ADDRESS            PspCmdPa;
    PHYSICAL_ADDRESS            PspFencePa;
    ULONG                       PspRingWptr;
    ULONG                       PspFenceValue;
    BOOLEAN                     PspRingActive;

    /* VSync sintetico.
       O dxgkrnl exige notificacao de DXGK_INTERRUPT_DISPLAYONLY_VSYNC para
       fazer o motor de apresentacao avancar. Nao temos interrupcao de
       hardware conectada, entao um temporizador periodico gera o evento. */
    KTIMER                      VsyncTimer;
    KDPC                        VsyncDpc;
    BOOLEAN                     VsyncEnabled;
    BOOLEAN                     VsyncRequested;
    BOOLEAN                     VsyncTimerInitialized;
    ULONG                       VsyncCount;
    ULONG                       InterruptTypeMask;  /* tipos pedidos por ControlInterrupt */
    LARGE_INTEGER               VsyncLastQpc;
    LARGE_INTEGER               VsyncQpcFrequency;

    /* Mutex for synchronization */
    /* EDID sintetico servido por DxgkDdiQueryDeviceDescriptor.
       Sem descritor o Windows registra o filho como "Generic Non-PnP Monitor"
       com BAD_EDID e usa um mode set default minimo, estrangulando a lista de
       resolucoes independentemente de quantos modos o driver publique no
       VidPN. Montado em StartDevice, quando o tamanho real do scanout do GOP
       ja e conhecido. */
    UCHAR                       Edid[BC250_EDID_BLOCK_SIZE];
    BOOLEAN                     EdidValid;

    /* Compute & GPGPU (Phase 5) */
    ULONG                       ComputeDispatchCount;
    ULONG                       ComputeFenceSequence;

    /* SMU Telemetry & Thermal */
    PVOID                       SmuMetricsVa;
    PHYSICAL_ADDRESS            SmuMetricsPa;
    BC250_WDDM_ESCAPE_TELEMETRY LastTelemetry;

    FAST_MUTEX                  DeviceMutex;
} BC250_WDDM_DEVICE, *PBC250_WDDM_DEVICE;

/* Hardware Bring-up & Submission Prototypes */
NTSTATUS Bc250HwBringup(_In_ PBC250_WDDM_DEVICE Dev);
VOID     Bc250HwShutdown(_In_ PBC250_WDDM_DEVICE Dev);
NTSTATUS Bc250HwSubmitPm4(_In_ PBC250_WDDM_DEVICE Dev, _In_ const ULONG *Data, _In_ ULONG DwordCount, _Out_ PULONG RptrBefore, _Out_ PULONG RptrAfter);
VOID     BcGcvmTlbFlush(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Vmid);

/* Telemetry & Thermal Prototypes */
NTSTATUS Bc250HwQueryTelemetry(_In_ PBC250_WDDM_DEVICE Dev, _Out_ BC250_WDDM_ESCAPE_TELEMETRY *Telem);

/* SDMA0 Hardware Copy Engine Prototypes */
NTSTATUS Bc250SdmaRingInit(_In_ PBC250_WDDM_DEVICE Dev);
VOID     Bc250SdmaRingShutdown(_In_ PBC250_WDDM_DEVICE Dev);
NTSTATUS Bc250SdmaSubmitCopy(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG SrcGpuAddr, _In_ ULONGLONG DstGpuAddr, _In_ ULONG Bytes, _Out_opt_ PULONG DurationUs);
NTSTATUS Bc250SdmaSubmitFill(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG DstGpuAddr, _In_ ULONG Bytes, _In_ ULONG Pattern);
NTSTATUS Bc250SdmaRunBenchmark(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONG Iterations, _In_ ULONG BlockSizeBytes, _Out_ PULONG AvgBandwidthMBs, _Out_ PULONG PeakBandwidthMBs);

/* Compute & GPGPU Engine Prototypes (Phase 5) */
NTSTATUS Bc250HwSubmitComputeDispatch(_In_ PBC250_WDDM_DEVICE Dev, _Inout_ BC250_WDDM_ESCAPE_COMPUTE_DISPATCH *Dispatch);
NTSTATUS Bc250HwVramWrite(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG Offset, _In_ const VOID *Buffer, _In_ ULONG Bytes);
NTSTATUS Bc250HwVramRead(_In_ PBC250_WDDM_DEVICE Dev, _In_ ULONGLONG Offset, _Out_ VOID *Buffer, _In_ ULONG Bytes);
#endif /* Kernel Mode Only */

/* ===========================================================================
   WDDM 2.0+ & Mesa RADV Winsys Private Query Structures
   =========================================================================== */
#define BC250_WDDM2_NUM_NODES       2
#define BC250_NODE_COMPUTE          0
#define BC250_NODE_SDMA_COPY        1

#define BC250_NUM_SEGMENTS          2
#define BC250_SEGMENT_VRAM_LOCAL    0
#define BC250_SEGMENT_SYSTEM_MEMORY 1

#pragma pack(push, 1)
#pragma push_macro("va_start")
#pragma push_macro("va_end")
#undef va_start
#undef va_end

typedef struct _BC250_UMD_PRIVATE_HEADER {
    ULONG flags[3];
    ULONG vendor_id;
    ULONG unknown0;
    ULONG pci_id;
    ULONG unknown1[9];
    USHORT registry_path[100];
    ULONG padding[490];
    ULONG type;
    ULONG something;
    ULONG unknown2[42];
    ULONG size;
} BC250_UMD_PRIVATE_HEADER;

typedef struct _BC250_UMD_BASIC_PROPERTIES {
    BC250_UMD_PRIVATE_HEADER header;
    ULONG unk0[11];
    ULONG device_id;
    ULONG family_id;
    ULONG chip_external_rev;
    ULONG subsystem_id;
    ULONG unk1[6];
    ULONG vram_type;
    ULONG vram_bit_width;
    ULONG unk2[43];
    CHAR umd_registry_path[256];
    ULONG unk3[18];
    ULONG num_rb_pipes;
    ULONG num_enabled_rb_pipes;
    ULONG enabled_rb_pipes_mask;
    ULONG enabled_rb_pipes_mask_hi;
    ULONG unk4[8];
} BC250_UMD_BASIC_PROPERTIES;

typedef struct _BC250_UMD_GPU_PROPERTIES {
    BC250_UMD_PRIVATE_HEADER header;
    ULONG unk0[550];
    ULONG num_engines;
    CHAR engines[64];
    ULONG unk1[5];
    ULONG me_fw_version;
    ULONG mec_fw_version;
    ULONG pfp_fw_version;
    ULONG unk3[65];
    ULONG device_id;
    ULONG family_id;
    ULONG chip_external_rev;
    ULONG chip_rev;
    ULONG gfx_engine_id;
    ULONG unk4[4];
    ULONG clock_crystal_freq_hz;
    ULONG unk5;
    ULONG unk6;
    ULONGLONG vram_vis_size;
    ULONGLONG vram_inv_size;
    ULONG unk7[112];
    ULONG gb_addr_cfg;
    ULONG unk8[21];
    ULONG num_shader_visible_vgprs;
    ULONG wave_front_size;
    ULONG num_shader_engines;
    ULONG num_active_rb_per_se;
    ULONG num_shader_arrays_per_engine;
    ULONG unk9[2];
    ULONG num_tcc_blocks;
    ULONG max_gs_waves_per_vgt;
    ULONG unk10;
    ULONG gs_vgt_table_depth;
    ULONG gs_prim_buffer_depth;
    ULONG unk11[23];
    ULONG num_cu_per_wgp;
    ULONG unk12[7];
    USHORT wgp_bitmap[6][2];
    USHORT wgp_ao_bitmap[6][2];
    ULONG unk13[10];
    ULONG num_sqc_per_wgp;
    ULONG sqc_inst_cache_size;
    ULONG sqc_data_cache_size;
    ULONG gl1c_cache_size;
    ULONG unk14;
    ULONG gl2c_cache_size;
    ULONG unk15[4];
    ULONG mall_size_mb;
    ULONG unk16[1824];
    ULONGLONG va_start;
    ULONGLONG va_end;
    ULONG unk17[256];
    ULONG large_page;
    ULONG unk18[220];
    struct {
        ULONG start;
        ULONG size;
    } ib_alignments[8];
    ULONG unk19[525];
} BC250_UMD_GPU_PROPERTIES;
#pragma pop_macro("va_end")
#pragma pop_macro("va_start")
#pragma pack(pop)

#endif /* _BC250_WDDM_H_ */
