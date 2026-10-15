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


/*
 * P3.0 (doc 609): foto SO' DE LEITURA do MMHUB. O IH (bloco OSS) e' cliente do
 * MMHUB (mmhub_v2_0.c, tabela de clientes [15] = "OSS"), e o amdgpu liga a GART
 * nos dois hubs (gmc_v10_0_gart_enable). Antes de escrever qualquer registrador
 * do MMHUB, medir o que o firmware deixou. MMHUB_BASE seg0 = 0x1A000
 * (cyan_skillfish_ip_offset.h); offsets de mmhub_2_0_0_offset.h, BASE_IDX 0;
 * bytes = (0x1A000 + reg) * 4. Estes offsets entram SO' na lista de leitura.
 */
#define BC250_MMHUB_REG_VM_L2_CNTL                     0x00069A00u /* 0x680 */
#define BC250_MMHUB_REG_VM_L2_PROT_FAULT_STATUS        0x00069A30u /* 0x68C */
#define BC250_MMHUB_REG_VM_L2_PROT_FAULT_ADDR_LO32     0x00069A34u /* 0x68D */
#define BC250_MMHUB_REG_VM_CONTEXT0_CNTL               0x00069B00u /* 0x6C0 */
#define BC250_MMHUB_REG_VM_CONTEXT0_PT_BASE_LO32       0x00069CACu /* 0x72B */
#define BC250_MMHUB_REG_VM_CONTEXT0_PT_BASE_HI32       0x00069CB0u /* 0x72C */
#define BC250_MMHUB_REG_VM_CONTEXT0_PT_START_LO32      0x00069D2Cu /* 0x74B */
#define BC250_MMHUB_REG_VM_CONTEXT0_PT_END_LO32        0x00069DACu /* 0x76B */
#define BC250_MMHUB_REG_MC_VM_FB_OFFSET                0x0006A15Cu /* 0x857 */
#define BC250_MMHUB_REG_MC_VM_SYS_APERTURE_DEFAULT_LSB 0x0006A160u /* 0x858 */
#define BC250_MMHUB_REG_MC_VM_FB_LOCATION_BASE         0x0006A1B0u /* 0x86C */
#define BC250_MMHUB_REG_MC_VM_FB_LOCATION_TOP          0x0006A1B4u /* 0x86D */
#define BC250_MMHUB_REG_MC_VM_AGP_TOP                  0x0006A1B8u /* 0x86E */
#define BC250_MMHUB_REG_MC_VM_AGP_BOT                  0x0006A1BCu /* 0x86F */
#define BC250_MMHUB_REG_MC_VM_SYS_APERTURE_LOW         0x0006A1C4u /* 0x871 */
#define BC250_MMHUB_REG_MC_VM_SYS_APERTURE_HIGH        0x0006A1C8u /* 0x872 */
#define BC250_MMHUB_REG_MC_VM_MX_L1_TLB_CNTL           0x0006A1CCu /* 0x873 */
#define BC250_MMHUB_P30_READ_COUNT 17u

#endif
