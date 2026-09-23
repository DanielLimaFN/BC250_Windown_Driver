#ifndef BC250_MMIO_TOPOLOGY_TRANSPORT_H
#define BC250_MMIO_TOPOLOGY_TRANSPORT_H

#include <ntddk.h>
#include <dispmprt.h>

#include "../core/bc250_resource_contract.h"
#include "../core/bc250_discovery_tmr_reader.h"
#include "../core/bc250_platform_register_snapshot.h"
#include "../core/bc250_gcvm_vmid8_transaction.h"
#include "../core/bc250_vmid_pool_contract.h"
#include "../core/bc250_gcvm_global_mmu.h"
#include "../core/bc250_psp_ring_transaction.h"
#include "../core/bc250_sdma_f32_transaction.h"
#include "../core/bc250_compute_mqd_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_MMIO_TOPOLOGY_SIGNATURE 0x4D543242u

typedef enum BC250_MMIO_TOPOLOGY_STATE {
    BC250_MMIO_TOPOLOGY_EMPTY = 0,
    BC250_MMIO_TOPOLOGY_MAPPED
} BC250_MMIO_TOPOLOGY_STATE;

typedef enum BC250_MMIO_TOPOLOGY_STATUS {
    BC250_MMIO_TOPOLOGY_OK = 0,
    BC250_MMIO_TOPOLOGY_BAD_ARGUMENT,
    BC250_MMIO_TOPOLOGY_WRONG_STATE,
    BC250_MMIO_TOPOLOGY_INTERFACE_TOO_SMALL,
    BC250_MMIO_TOPOLOGY_BAR_NOT_UNIQUE,
    BC250_MMIO_TOPOLOGY_BAR_TOO_SHORT,
    BC250_MMIO_TOPOLOGY_BAR_TOO_LARGE,
    BC250_MMIO_TOPOLOGY_PHYSICAL_RANGE_INVALID,
    BC250_MMIO_TOPOLOGY_MAP_FAILED,
    BC250_MMIO_TOPOLOGY_MAP_RETURNED_NULL,
    BC250_MMIO_TOPOLOGY_UNMAP_FAILED
} BC250_MMIO_TOPOLOGY_STATUS;

typedef struct BC250_MMIO_TOPOLOGY_CONTEXT {
    uint32_t Signature;
    uint32_t State;
    HANDLE DeviceHandle;
    DXGKCB_UNMAP_MEMORY UnmapMemory;
    PVOID VirtualBase;
    uint64_t PhysicalStart;
    uint64_t Length;
    uint32_t BarIndex;
    uint32_t ReadCount;
    uint32_t WriteCount;
    NTSTATUS LastCallbackStatus;
} BC250_MMIO_TOPOLOGY_CONTEXT;

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyInitialize(
    _Out_ BC250_MMIO_TOPOLOGY_CONTEXT *Context);

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyMapReadonly(
    _Inout_ BC250_MMIO_TOPOLOGY_CONTEXT *Context,
    _In_ const DXGKRNL_INTERFACE *DxgkInterface,
    _In_reads_(ResolvedBarCount)
        const BC250_RESOLVED_BAR_RESOURCE *ResolvedBars,
    _In_ size_t ResolvedBarCount,
    _In_ uint32_t MmioBarIndex);

uint32_t Bc250MmioTopologyRead32(
    _In_ void *Context,
    _In_ uint32_t ByteOffset,
    _Out_ uint32_t *Value);

uint32_t Bc250MmioTopologyWrite32(
    _In_ void *Context,
    _In_ uint32_t ByteOffset,
    _In_ uint32_t Value);

BC250_MMIO_TOPOLOGY_STATUS Bc250MmioTopologyUnmap(
    _Inout_ BC250_MMIO_TOPOLOGY_CONTEXT *Context);

const char *Bc250MmioTopologyStatusName(BC250_MMIO_TOPOLOGY_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
