#ifndef BC250_ACTIVE_MINIPORT_H
#define BC250_ACTIVE_MINIPORT_H

#include <ntddk.h>
#include <dispmprt.h>

#include "bc250_dxgk_readonly_ddi.h"
#include "bc250_readonly_miniport.h"
#include "bc250_display_ddi.h"
#include "bc250_post_display_reservation.h"
#include "bc250_vram_probe_transaction.h"
#include "bc250_gcvm_backing_transaction.h"
#include "../core/bc250_mcdm_object_contract.h"
#include "../core/bc250_runtime_inventory.h"
#include "../core/bc250_gfxhub_tlb_transaction.h"
#include "../core/bc250_gcvm_vmid8_transaction.h"
#include "../core/bc250_psp_ring_transaction.h"
#include "../core/bc250_psp_load_fw_transaction.h"
#include "../core/bc250_sdma_f32_transaction.h"
#include "../core/bc250_vram_allocator.h"
#include "../core/bc250_compute_mqd_contract.h"
#include "../core/bc250_gpuva_sdma_broker_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_ACTIVE_SIGNATURE 0x4D434431u /* "MCD1" */
#define BC250_ACTIVE_POOL_TAG  'ADCB'

typedef enum BC250_SDMA_RING_WORK_STATUS {
    BC250_SDMA_RING_WORK_OK = 0,
    BC250_SDMA_RING_WORK_BAD_ARGUMENT,
    BC250_SDMA_RING_WORK_ALLOCATION_FAILED,
    BC250_SDMA_RING_WORK_REGION_FAILED,
    BC250_SDMA_RING_WORK_APERTURE_INVALID,
    BC250_SDMA_RING_WORK_MAP_FAILED,
    BC250_SDMA_RING_WORK_INHERITED_ACTIVE,
    BC250_SDMA_RING_WORK_PACKET_FAILED,
    BC250_SDMA_RING_WORK_MMIO_FAILED,
    BC250_SDMA_RING_WORK_READBACK_FAILED,
    BC250_SDMA_RING_WORK_TIMEOUT,
    BC250_SDMA_RING_WORK_FENCE_FAILED,
    BC250_SDMA_RING_WORK_DATA_FAILED,
    BC250_SDMA_RING_WORK_RESTORE_FAILED,
    BC250_SDMA_RING_WORK_UNMAP_FAILED,
    BC250_SDMA_RING_WORK_RELEASE_FAILED
} BC250_SDMA_RING_WORK_STATUS;

typedef struct BC250_SDMA_RING_WORK_EVIDENCE {
    uint32_t FixedReservationStatus;
    uint32_t AllocationMode;
    uint64_t RingMcAddress;
    uint64_t WorkMcAddress;
    uint64_t RingOffset;
    uint64_t WorkOffset;
    uint32_t RptrBefore;
    uint32_t RptrAfter;
    uint32_t WptrPublished;
    uint32_t WptrRegisterAfterKick;
    uint32_t WptrShadowAfterKick;
    uint32_t RbCntlApplied;
    uint32_t RingHeaderReadback;
    uint32_t RptrRegisterAfter;
    uint32_t WptrPollCntlApplied;
    uint32_t FencePoison;
    uint32_t FenceExpected;
    uint32_t FenceObserved;
    uint32_t Polls;
    uint32_t ComparedDwords;
    uint32_t MismatchCount;
    uint32_t StatusBefore;
    uint32_t StatusKick;
    uint32_t StatusAfter;
    uint32_t PcKick;
    uint32_t Status1After;
    uint32_t Status2After;
    uint32_t Status3After;
    uint32_t ErrorLogAfter;
    uint32_t Utcl1ReadStatusAfter;
    uint32_t Utcl1WriteStatusAfter;
    uint32_t Utcl1ReadXnack0After;
    uint32_t Utcl1ReadXnack1After;
    uint32_t Utcl1WriteXnack0After;
    uint32_t Utcl1WriteXnack1After;
    uint32_t Utcl1TimeoutAfter;
    uint32_t RegisterRestorePassed;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
} BC250_SDMA_RING_WORK_EVIDENCE;

typedef struct BC250_RLC_LOAD_EVIDENCE {
    uint32_t CntlBefore;
    uint32_t GpmStatBefore;
    uint32_t CntlAfter;
    uint32_t GpmStatAfter;
    uint32_t ExecRomBefore;
    uint32_t ExecRomAfter;
    uint32_t EnableF32After;
    uint32_t CntlRestorePassed;
    uint32_t AlreadyActive;
    uint32_t PhysicalReady;
} BC250_RLC_LOAD_EVIDENCE;

typedef enum BC250_PFP_DIRECT_LOAD_STATUS {
    BC250_PFP_DIRECT_LOAD_OK = 0,
    BC250_PFP_DIRECT_LOAD_BAD_ARGUMENT,
    BC250_PFP_DIRECT_LOAD_FIRMWARE_FAILED,
    BC250_PFP_DIRECT_LOAD_ALLOCATION_FAILED,
    BC250_PFP_DIRECT_LOAD_REGION_FAILED,
    BC250_PFP_DIRECT_LOAD_APERTURE_INVALID,
    BC250_PFP_DIRECT_LOAD_MAP_FAILED,
    BC250_PFP_DIRECT_LOAD_MMIO_FAILED,
    BC250_PFP_DIRECT_LOAD_INHERITED_ACTIVE,
    BC250_PFP_DIRECT_LOAD_STAGING_FAILED,
    BC250_PFP_DIRECT_LOAD_PC_STATIC,
    BC250_PFP_DIRECT_LOAD_RESTORE_FAILED,
    BC250_PFP_DIRECT_LOAD_UNMAP_FAILED,
    BC250_PFP_DIRECT_LOAD_RELEASE_FAILED,
    BC250_PFP_DIRECT_LOAD_PM4_NOT_CONSUMED
} BC250_PFP_DIRECT_LOAD_STATUS;

typedef struct BC250_PFP_DIRECT_LOAD_EVIDENCE {
    uint64_t FirmwareMcAddress;
    uint64_t FirmwareOffset;
    uint32_t FirmwareBytes;
    uint32_t JumpTableDwords;
    uint32_t UcodeVersion;
    uint32_t StagedDwords;
    uint32_t StagingMismatches;
    uint32_t CpMeCntlBefore;
    uint32_t CpMeCntlApplied;
    uint32_t CpMeCntlAfter;
    uint32_t RlcCntlBefore;
    uint32_t RlcCntlApplied;
    uint32_t PfpPcBefore;
    uint32_t PfpPcFirst;
    uint32_t PfpPcLast;
    uint32_t PfpPcTransitions;
    uint32_t PfpPcSamples;
    uint32_t MePcBefore;
    uint32_t MePcLast;
    uint32_t CePcBefore;
    uint32_t CePcLast;
    uint32_t FaultStatusBefore;
    uint32_t FaultStatusAfter;
    uint32_t FaultAddressLow;
    uint32_t FaultAddressHigh;
    uint32_t WriteOnlyPortsProgrammed;
    uint32_t Rb0ActiveBefore;
    uint32_t Rb0ActiveApplied;
    uint32_t Rb0ActiveAfter;
    uint32_t Rb0RptrBefore;
    uint32_t Rb0RptrAfter;
    uint32_t Rb0WptrApplied;
    uint32_t Rb0FenceBefore;
    uint32_t Rb0FenceAfter;
    uint32_t RegisterRestorePassed;
    uint32_t MemoryRestorePassed;
    uint32_t PhysicalReady;
    /* Mascaras PFP/ME/CE/CPC (bits 0..3) das operacoes de I-Cache que o
     * hardware confirmou. 0x0F = os quatro engines confirmaram. */
    uint32_t IcacheInvalidateEntryMask;
    uint32_t IcacheInvalidateUnhaltMask;
    uint32_t IcachePrimeUnhaltMask;
} BC250_PFP_DIRECT_LOAD_EVIDENCE;

typedef struct BC250_DXGK_ACTIVE_CONTEXT {
    uint32_t Signature;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BC250_DXGK_READONLY_CONTEXT Core;
    BC250_DISPLAY_STATE Display;
    BC250_POST_DISPLAY_STATUS PostDisplayStatus;
    NTSTATUS PostDisplayCallbackStatus;
    BC250_INHERITED_VRAM_RESERVATION PostDisplayReservation;
    BC250_POST_DISPLAY_EVIDENCE PostDisplayEvidence;
    BC250_RUNTIME_INVENTORY_STATUS RuntimeInventoryStatus;
    BC250_RUNTIME_INVENTORY_INPUT RuntimeInventoryInput;
    BC250_RUNTIME_INVENTORY_REPORT RuntimeInventoryReport;
    BC250_DXGK_READONLY_INPUT SealedRuntime;
    NTSTATUS RuntimeStageStatus;
    uint32_t SecondPreflightStage;
    BC250_VRAM_PROBE_STATUS VramProbeStatus;
    BC250_VRAM_PROBE_EVIDENCE VramProbeEvidence;
    BC250_TLB_TRANSACTION_STATUS TlbTransactionStatus;
    BC250_TLB_TRANSACTION_EVIDENCE TlbTransactionEvidence;
    BC250_GCVM_BACKING_STATUS GcvmBackingStatus;
    BC250_GCVM_BACKING_EVIDENCE GcvmBackingEvidence;
    BC250_GCVM_VMID8_STATUS GcvmVmid8Status;
    BC250_GCVM_VMID8_EVIDENCE GcvmVmid8Evidence;
    BC250_PSP_RING_STATUS PspRingStatus;
    BC250_PSP_RING_EVIDENCE PspRingEvidence;
    PVOID DeferredPspRingVirtual;
    BC250_PSP_LOAD_FW_STATUS PspLoadFwStatus;
    BC250_PSP_LOAD_FW_EVIDENCE PspLoadFwEvidence;
    BC250_PSP_LOAD_FW_STATUS PspRlcLoadStatus;
    BC250_PSP_LOAD_FW_EVIDENCE PspRlcLoadEvidence;
    BC250_RLC_LOAD_EVIDENCE RlcLoadEvidence;
    BC250_PFP_DIRECT_LOAD_STATUS PfpDirectLoadStatus;
    BC250_PFP_DIRECT_LOAD_EVIDENCE PfpDirectLoadEvidence;
    BC250_HQD_PREFLIGHT_STATUS HqdPreflightStatus;
    BC250_HQD_READONLY_SNAPSHOT HqdPreflightSnapshot;
    uint32_t HqdPreflightReadCount;
    BC250_HQD_SELECTOR_STATUS MecHqdSelectorStatus;
    BC250_HQD_SELECTOR_EVIDENCE MecHqdSelectorEvidence;
    BC250_HQD_BANK_PROBE_STATUS MecHqdBankProbeStatus;
    BC250_HQD_BANK_PROBE_EVIDENCE MecHqdBankProbeEvidence;
    BC250_HQD_PROGRAM_STATUS HqdProgramStatus;
    BC250_HQD_PROGRAM_EVIDENCE HqdProgramEvidence;
    BC250_HQD_WPTR_DIAGNOSTIC_STATUS HqdWptrDiagStatus;
    BC250_HQD_WPTR_DIAGNOSTIC_EVIDENCE HqdWptrDiagEvidence;
    BC250_HQD_ACTIVE_STATUS HqdActiveStatus;
    BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE HqdActiveEvidence;
    BC250_SDMA_F32_STATUS SdmaF32Status;
    BC250_SDMA_F32_EVIDENCE SdmaF32Evidence;
    BC250_SDMA_RING_WORK_STATUS SdmaRingWorkStatus;
    BC250_SDMA_RING_WORK_EVIDENCE SdmaRingWorkEvidence;
    PVOID DeferredPspLoadRing;
    PVOID DeferredPspLoadCommand;
    PVOID DeferredPspLoadFence;
    PVOID DeferredPspLoadFirmware;
    EX_PUSH_LOCK ObjectLock;
    BC250_MCDM_OBJECT_REGISTRY Objects;
    /* In-memory authority only; it never performs MMIO or submission. */
    BC250_GPUVA_SDMA_BROKER GpuvaSdmaBroker;
    volatile LONG GpuvaSdmaBrokerReady;
    DXGK_CONTEXTINFO ContextAbi;
    volatile LONG ContextAbiReady;
    volatile LONG AllocationAbiReady;
    volatile LONG RootPageTableAbiReady;
    uint64_t AllocationSegmentBytes;
    uint32_t AllocationSegmentSet;
    uint32_t RootPageTableMaxEntries;
    volatile LONG IsActive;
    uint32_t SubmissionsCount;
    uint32_t ResetsCount;
} BC250_DXGK_ACTIVE_CONTEXT;

NTSTATUS Bc250BuildActiveInitializationData(
    DRIVER_INITIALIZATION_DATA *InitializationData);

NTSTATUS APIENTRY Bc250DxgkDdiAddDeviceActive(
    CONST PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID *MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiStartDeviceActive(
    CONST PVOID MiniportDeviceContext,
    PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface,
    PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren);

NTSTATUS APIENTRY Bc250DxgkDdiStopDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS APIENTRY Bc250DxgkDdiRemoveDeviceActive(
    CONST PVOID MiniportDeviceContext);

NTSTATUS Bc250ActiveCreateProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    const void *ExternalOwner,
    BC250_MCDM_HANDLE *Handle);

NTSTATUS Bc250ActiveDestroyProcessObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE Handle);

NTSTATUS Bc250ActiveCreateDeviceObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ProcessHandle,
    BC250_MCDM_HANDLE *DeviceHandle);

NTSTATUS Bc250ActiveDestroyDeviceObject(
    BC250_MCDM_HANDLE DeviceHandle);

NTSTATUS Bc250ActiveCreateContextObject(
    BC250_MCDM_HANDLE DeviceHandle,
    UINT NodeOrdinal,
    BC250_MCDM_HANDLE *ContextHandle,
    DXGK_CONTEXTINFO *ContextInfo);

NTSTATUS Bc250ActiveDestroyContextObject(
    BC250_MCDM_HANDLE ContextHandle);

NTSTATUS Bc250ActiveSetRootPageTableObject(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE ContextHandle,
    uint32_t SegmentId,
    uint64_t Address,
    uint32_t NumEntries,
    uint32_t MaxEntries);

NTSTATUS Bc250ActiveIssueGpuvaSdmaLease(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_MCDM_HANDLE OwnerProcess,
    BC250_MCDM_HANDLE OpenAllocation,
    uint64_t GpuVa,
    uint64_t ByteCount,
    uint64_t *LeaseId);

NTSTATUS Bc250ActiveValidateGpuvaSdmaLeasePair(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    uint64_t SourceLeaseId,
    uint64_t DestinationLeaseId,
    uint64_t FenceGpuVa,
    uint64_t ByteCount);

BC250_SDMA_RING_WORK_STATUS Bc250ActiveRunSdmaRingWork(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_SDMA_RING_WORK_EVIDENCE *Evidence);

BC250_PFP_DIRECT_LOAD_STATUS Bc250ActiveRunPfpDirectLoad(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_PFP_DIRECT_LOAD_EVIDENCE *Evidence);

BC250_HQD_PROGRAM_STATUS Bc250ActiveRunInactiveHqdProgramTransaction(
    BC250_DXGK_ACTIVE_CONTEXT *Context,
    BC250_HQD_PROGRAM_EVIDENCE *Evidence);

#ifdef __cplusplus
}
#endif

#endif
