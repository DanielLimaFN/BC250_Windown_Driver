/* bc250dbg.h — estado interno do driver de instrumentacao.
 *
 * Driver legacy (nao-PnP): ele NAO assume o dispositivo PCI. O adaptador segue
 * com o Basic Display / KMDOD; aqui apenas mapeamos a janela de registradores
 * para observacao. E isso que permite instrumentar mesmo com o driver grafico
 * quebrado, desinstalado ou em loop de TDR.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BC250DBG_H
#define BC250DBG_H

#include <ntddk.h>
#include <ntstrsafe.h>
#include <stdarg.h>

#include "../inc/bc250dbg_abi.h"

#define BC250DBG_POOL_TAG  'gbcB'   /* "Bcbg" */

/* ------------------------------------------------------------------------ */
/* Estado global do driver                                                   */
/* ------------------------------------------------------------------------ */

typedef struct _BC250DBG_DEVICE {
    PDEVICE_OBJECT      DeviceObject;

    /* --- PCI ------------------------------------------------------------ */
    BOOLEAN             PciFound;
    ULONG               PciBus, PciDev, PciFn;
    USHORT              VendorId, DeviceId;
    UCHAR               RevisionId;
    BC250DBG_BAR_INFO   Bar[BC250DBG_MAX_BARS];

    /* --- BAR5 mapeado --------------------------------------------------- */
    PHYSICAL_ADDRESS    MmioPhysical;
    ULONG               MmioLength;
    volatile UCHAR     *Mmio;          /* NULL se nao mapeado               */

    /* --- BAR0 (abertura de VRAM) ----------------------------------------- */
    PHYSICAL_ADDRESS    VramPhysical;
    ULONG               VramLength;
    volatile UCHAR     *Vram;          /* NULL se nao mapeado               */

    /* --- Serializacao ---------------------------------------------------- */
    /* SmnLock protege o par indice/dado (0x38/0x3C): o acesso e indexado e
     * duas threads intercaladas leem o registrador da outra. */
    KSPIN_LOCK          SmnLock;
    /* SmuMutex serializa a sequencia completa do mailbox. KMUTEX, e nao
     * FAST_MUTEX, porque essa sequencia dorme por ate um segundo: com fast
     * mutex ficariamos em APC_LEVEL com APCs bloqueadas todo esse tempo. */
    KMUTEX              SmuMutex;
    /* ScriptMutex garante um script por vez (mesma razao). */
    KMUTEX              ScriptMutex;

    /* --- Modelo de seguranca --------------------------------------------- */
    volatile LONG       ArmLevel;      /* BC250DBG_ARM_*                    */

    /* --- Log ring -------------------------------------------------------- */
    PVOID               LogBuffer;     /* header + entradas, non-paged      */
    ULONG               LogTotalBytes;
    ULONG               LogEntryCount; /* potencia de dois                  */
    ULONG               LogEntryMask;
    PMDL                LogMdl;
    BC250DBG_LOG_HEADER *LogHeader;
    BC250DBG_LOG_ENTRY  *LogEntries;
    volatile LONG       LogLevelMask;
    volatile LONG       LogSubsysMask;
    LARGE_INTEGER       QpcFrequency;

    /* --- Watchdog termico ------------------------------------------------ */
    PKTHREAD            ThermalThread;
    KEVENT              ThermalStopEvent;
    volatile LONG       ThermalStopping;
    volatile LONG       ThermalTripped;
    volatile LONG       ThermalMilliC;
    volatile LONG       ThermalTrusted;
    volatile LONG       ThermalLimitC;
    ULONG               ThermalSource;      /* 0=edge 1=junction 2=mem      */
    LONG                ThermalScaleNum;
    LONG                ThermalScaleDen;
    LONG                ThermalOffsetMilliC;
    ULONG               RawEdge, RawJunction, RawMem, FanRpm, FanPwm;

    /* --- SDMA Ring Buffer ----------------------------------------------- */
    PVOID               SdmaRingBuffer;     /* non-paged contiguous DMA buffer   */
    PHYSICAL_ADDRESS    SdmaRingPhysical;
    ULONG               SdmaRingSize;       /* em bytes (4096)                   */
    ULONG               SdmaWptr;           /* byte offset dentro do ring        */
    ULONG               SdmaRingPlacement;  /* BC250DBG_SDMA_RING_*              */
    ULONG               SdmaRingVramOffset; /* offset na VRAM quando em VRAM     */
    ULONGLONG           SdmaRingGpuAddr;    /* endereco que foi para RB_BASE     */

    /* --- PSP (MP0) mailbox GPCOM ------------------------------------------ */
    PVOID               PspRingVa;
    PHYSICAL_ADDRESS    PspRingPa;
    PVOID               PspCmdVa;
    PHYSICAL_ADDRESS    PspCmdPa;
    PVOID               PspFenceVa;
    PHYSICAL_ADDRESS    PspFencePa;
    ULONG               PspRingWptr;       /* em dwords                         */
    ULONG               PspFenceValue;
    BOOLEAN             PspRingCreated;

    /* --- GFX Ring Buffer (CP PM4 Ring 0) --------------------------------- */
    PVOID               GfxRingBuffer;     /* Host System RAM DMA buffer (se alocado) */
    PHYSICAL_ADDRESS    GfxRingPa;
    ULONG               GfxRingVramOffset;
    ULONGLONG           GfxRingGpuAddr;
    ULONG               GfxRingSize;
    ULONG               GfxWptr;           /* em dwords                         */
    BOOLEAN             GfxRingInitialized;

    /* --- Aborto de script ------------------------------------------------ */
    volatile LONG       ScriptAbort;
} BC250DBG_DEVICE, *PBC250DBG_DEVICE;

extern BC250DBG_DEVICE g_Dev;

/* Contexto por handle aberto (FsContext do FILE_OBJECT). */
typedef struct _BC250DBG_FILE_CONTEXT {
    PVOID       LogUserVa;      /* mapeamento do ring neste processo        */
    PEPROCESS   LogProcess;
} BC250DBG_FILE_CONTEXT, *PBC250DBG_FILE_CONTEXT;

/* ------------------------------------------------------------------------ */
/* log.c                                                                     */
/* ------------------------------------------------------------------------ */

NTSTATUS BcLogInit(ULONG EntryCount);
VOID     BcLogFree(VOID);

VOID BcLogEvent(ULONG Level, ULONG Subsys, ULONG EventId,
                UINT64 A0, UINT64 A1, UINT64 A2, UINT64 A3);
/* Somente para caminhos frios (init, erro): formata no kernel. */
VOID BcLogText(ULONG Level, ULONG Subsys, PCSTR Format, ...);

NTSTATUS BcLogMapToUser(PBC250DBG_FILE_CONTEXT Ctx, BC250DBG_LOG_MAP_OUT *Out);
VOID     BcLogUnmapFromUser(PBC250DBG_FILE_CONTEXT Ctx);

/* ------------------------------------------------------------------------ */
/* hw.c                                                                      */
/* ------------------------------------------------------------------------ */

NTSTATUS BcPciScan(VOID);
ULONG    BcPciConfigRead(ULONG Bus, ULONG Dev, ULONG Fn, ULONG Offset);
NTSTATUS BcMmioMap(VOID);
NTSTATUS BcVramMap(VOID);
VOID     BcMmioUnmap(VOID);
VOID     BcVramUnmap(VOID);

/* Acesso cru — sem checagem de armamento. Uso interno. */
ULONG    BcRegReadRaw(ULONG Offset);
VOID     BcRegWriteRaw(ULONG Offset, ULONG Value);
ULONG    BcVramReadRaw(ULONG Offset);
VOID     BcVramWriteRaw(ULONG Offset, ULONG Value);
ULONG    BcSmnReadRaw(ULONG Address);
VOID     BcSmnWriteRaw(ULONG Address, ULONG Value);

/* Acesso com politica: checa faixa, armamento e emite log. */
ULONG    BcRegRead(ULONG Offset, ULONG *Status);
ULONG    BcRegWrite(ULONG Offset, ULONG Value, ULONG *Status);
ULONG    BcSmnRead(ULONG Address, ULONG *Status);
ULONG    BcSmnWrite(ULONG Address, ULONG Value, ULONG *Status);

/* Classificacao de risco das mensagens do SMU. */
#define BC250_SMU_RISK_SAFE     0
#define BC250_SMU_RISK_MEDIUM   1
#define BC250_SMU_RISK_HIGH     2
ULONG    BcSmuRisk(ULONG Queue, ULONG Msg);
NTSTATUS BcSmuMsg(ULONG Queue, ULONG Msg, ULONG Arg, ULONG ArgHi,
                  ULONG TimeoutMs, ULONG *SmuStatus, ULONG *Result);
VOID     BcHwCollectInfo(BC250DBG_INFO *Info);

/* SDMA Ring Buffer */
NTSTATUS BcSdmaInit(ULONG RingSizeBytes, ULONG Placement, ULONG VramOffset,
                    ULONGLONG *OutGpuAddr);
VOID     BcSdmaFree(VOID);
NTSTATUS BcSdmaF32Reset(ULONG *OutPcBefore, ULONG *OutPcAfter, ULONG *OutMoved);
NTSTATUS BcSdmaExecOp(BC250DBG_SDMA_OP_IO *Io);
VOID     BcHdpReadCacheInvalidate(VOID);
NTSTATUS BcVramScan(BC250DBG_VRAM_SCAN_IO *Io);
NTSTATUS BcVramFillPattern(BC250DBG_VRAM_PATTERN_IO *Io);
NTSTATUS BcVramVerifyPattern(BC250DBG_VRAM_PATTERN_IO *Io);
NTSTATUS BcSdmaRaw(BC250DBG_SDMA_RAW_IO *Io);

/* PSP (MP0) mailbox GPCOM */
VOID     BcPspReadStatus(BC250DBG_PSP_STATUS_IO *Out);
NTSTATUS BcPspRingCreate(ULONGLONG *OutRingPa, ULONG *OutBefore, ULONG *OutAfter);
NTSTATUS BcPspSetupTmr(ULONG TmrSizeBytes, ULONG VramOffset, BC250DBG_PSP_SETUP_TMR_IO *Out);
NTSTATUS BcPspLoadIpFw(ULONG FwType, const ULONG *Dwords, ULONG Count,
                       BC250DBG_PSP_LOAD_FW_IO *Out);
VOID     BcPspFree(VOID);
NTSTATUS BcSdmaSubmit(const ULONG *Dwords, ULONG Count, ULONG *RptrBefore, ULONG *WptrAfter, ULONG *RptrAfter);
NTSTATUS BcSdmaStatus(ULONG *Rptr, ULONG *Wptr, ULONG *RbCntl, ULONG *StatusReg,
                      PHYSICAL_ADDRESS *RbBase, ULONG *RptrWb, ULONG *WptrShadow);
NTSTATUS BcSdmaLoadFirmware(const ULONG *Dwords, ULONG Count, ULONG *OutVersion, ULONG *OutChecksum);

/* GCVM / GART / VMID 0 */
VOID     BcGcvmStatus(BC250DBG_GCVM_STATUS_IO *Out);
NTSTATUS BcGcvmInit(BC250DBG_GCVM_INIT_IO *Io);

/* GFX Ring Buffer (CP PM4 Ring 0) */
NTSTATUS BcGfxInit(BC250DBG_GFX_INIT_IO *Io);
NTSTATUS BcGfxSubmit(BC250DBG_GFX_SUBMIT_IO *Io);
VOID     BcGfxStatus(BC250DBG_GFX_STATUS_IO *Out);
VOID     BcGfxFree(VOID);


/* ------------------------------------------------------------------------ */
/* thermal.c                                                                 */
/* ------------------------------------------------------------------------ */

NTSTATUS BcThermalStart(VOID);
VOID     BcThermalStop(VOID);
VOID     BcThermalSample(VOID);
VOID     BcThermalGet(BC250DBG_THERMAL *Out);
NTSTATUS BcThermalSet(const BC250DBG_THERMAL_SET *In);
/* TRUE se e seguro seguir: sensor confiavel e abaixo do limite, ou sensor
 * nao confiavel (nesse caso quem decide e o nivel de armamento). */
BOOLEAN  BcThermalOk(ULONG LimitOverrideC);

/* ------------------------------------------------------------------------ */
/* script.c                                                                  */
/* ------------------------------------------------------------------------ */

NTSTATUS BcScriptRun(const BC250DBG_SCRIPT_IN *In, const BC250DBG_OP *Ops,
                     BC250DBG_SCRIPT_OUT *Out, BC250DBG_OP_RESULT *Results,
                     ULONG MaxResults);

#endif /* BC250DBG_H */

