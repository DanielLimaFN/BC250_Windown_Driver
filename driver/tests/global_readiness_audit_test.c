#include "../wdk/bc250_mmio_topology_transport.h"
#include "../core/bc250_vram_topology_contract.h"
#include "../core/bc250_discovery_tmr_reader.h"
#include "../core/bc250_gcvm_contract.h"
#include "../core/bc250_staging_lease.h"
#include "../core/bc250_sdma_packet_builder.h"
#include "../core/bc250_pm4_packet_builder.h"
#include "../core/bc250_golden_settings_contract.h"
#include "../core/bc250_compute_mqd_contract.h"
#include "../core/bc250_context_dispatcher_contract.h"
#include "../core/bc250_ih_contract.h"
#include "../core/bc250_paging_contract.h"
#include "../core/bc250_adapter_info_contract.h"
#include "../core/bc250_segment_contract.h"
#include "../core/bc250_tdr_recovery_contract.h"
#include "../core/bc250_submit_command_contract.h"
#include "../core/bc250_preemption_contract.h"
#include "../core/bc250_gpu_clock_contract.h"
#include "../core/bc250_power_contract.h"

#include <string.h>

#ifdef BC250_KERNEL_MODE
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
    volatile unsigned char *dst = (volatile unsigned char *)Destination;
    const volatile unsigned char *src = (const volatile unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) dst[index] = src[index];
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
/* Falha devolve 0 porque main() faz `return Run() ? 0 : 1`: devolver 1
 * aqui fazia a falha virar SUCESSO no build offline. Medido em 14/09
 * quebrando o emissor de proposito -- suite offline saiu EXIT=0 e a
 * kernel EXIT=1. Travado por audit_offline_check_is_not_inverted. */
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#endif

#define AUDIT_RING_DWORDS 256u

static uint32_t g_auditRing[AUDIT_RING_DWORDS];

static int Bc250RunGlobalReadinessAuditTest(void)
{
    /* 1. MMIO Topology & PnP Resources */
    {
        BC250_MMIO_TOPOLOGY_CONTEXT mmioCtx;
        CHECK(Bc250MmioTopologyInitialize(&mmioCtx) == BC250_MMIO_TOPOLOGY_OK);
        CHECK(mmioCtx.Signature == BC250_MMIO_TOPOLOGY_SIGNATURE);
    }

    /* 2. VRAM Topology & Carveouts */
    {
        BC250_VRAM_TOPOLOGY_SNAPSHOT snapshot;
        BC250_VRAM_ADDRESS_SPACE addrSpace;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.RccConfigMemsizeMiB = 16384u;
        snapshot.GcmcVmFbLocationBase = 0u;
        snapshot.GcmcVmFbLocationTop = 1023u;
        snapshot.GcmcVmFbOffset = 0u;
        snapshot.CpuVisibleBytes = 0x10000000ull;
        snapshot.EvidenceFlags = BC250_TOPOLOGY_EVIDENCE_REQUIRED;
        snapshot.SnapshotGeneration = 1u;
        CHECK(Bc250DecodeVramTopology(&snapshot, &addrSpace) == BC250_VRAM_TOPOLOGY_OK);
        CHECK(addrSpace.TotalBytes == 16ull * 1024ull * 1024ull * 1024ull);
    }

    /* 3. GCVM Translation */
    {
        uint64_t pte = 0;
        CHECK(Bc250GcvmEncodePte(
            0x0000000100000000ull,
            BC250_PTE_RW_DEFAULT,
            &pte) == BC250_GCVM_OK);
        CHECK((pte & BC250_PTE_VALID) != 0);
    }

    /* 4. SDMA Packet Builder */
    {
        uint32_t cursor = 0;
        memset(g_auditRing, 0, sizeof(g_auditRing));
        CHECK(Bc250SdmaEmitFence(
            g_auditRing, AUDIT_RING_DWORDS, &cursor, 0x0000000020000000ull, 100u) == BC250_SDMA_OK);
        CHECK(cursor == 4u);
    }

    /* 5. PM4 Packet Builder & Indirect Buffer */
    {
        uint32_t cursor = 0;
        memset(g_auditRing, 0, sizeof(g_auditRing));
        CHECK(Bc250Pm4EmitIndirectBuffer(
            g_auditRing, AUDIT_RING_DWORDS, &cursor, 0x0000000030000000ull, 64u, 0u) == BC250_PM4_OK);
        CHECK(cursor == 4u);
        CHECK(g_auditRing[0] == 0xC0023F00u);
    }

    /* 6. Golden Settings RMW */
    {
        uint32_t count = 0;
        const BC250_GOLDEN_ENTRY *table = Bc250GoldenSettingsGetTable(&count);
        CHECK(table != NULL);
        CHECK(count == BC250_GOLDEN_SETTINGS_COUNT);
    }

    /* 7. Compute MQD */
    {
        BC250_COMPUTE_MQD_PROPERTIES mqdProps;
        BC250_GFX10_COMPUTE_MQD mqd;
        memset(&mqdProps, 0, sizeof(mqdProps));
        memset(&mqd, 0, sizeof(mqd));
        mqdProps.MqdGpuAddress = 0x0000080000000000ull;
        mqdProps.HqdBaseGpuAddress = 0x0000080000001000ull;
        mqdProps.QueueSizeDwords = 1024u;
        mqdProps.EopGpuAddress = 0x0000080000002000ull;
        mqdProps.EopSizeBytes = 4096u;
        CHECK(Bc250ComputeMqdInit(&mqdProps, &mqd) == BC250_MQD_OK);
        CHECK(mqd.Header == BC250_MQD_HEADER_MAGIC);
    }

    /* 8. Context Dispatcher */
    {
        BC250_GPU_CONTEXT gpuCtx;
        memset(&gpuCtx, 0, sizeof(gpuCtx));
        CHECK(Bc250ContextCreate(
            &gpuCtx, 1u, BC250_ENGINE_TYPE_SDMA, 0u, 0x0000080000000000ull, 128u) == BC250_DISPATCH_OK);
        CHECK(gpuCtx.IsActive == 1u);
    }

    /* 9. Interrupt Handler */
    {
        BC250_IH_STATE ihState;
        BC250_IH_RING_PROPERTIES ihProps;
        memset(&ihState, 0, sizeof(ihState));
        memset(&ihProps, 0, sizeof(ihProps));
        ihProps.RingGpuAddress = 0x0000080000000000ull;
        ihProps.RingSizeDwords = 256u;
        CHECK(Bc250IhInit(&ihState, &ihProps) == BC250_IH_OK);
    }

    /* 10. Paging Buffer */
    {
        uint32_t cursor = 0;
        BC250_PAGING_TRANSFER_PARAMS transferParams;
        memset(g_auditRing, 0, sizeof(g_auditRing));
        memset(&transferParams, 0, sizeof(transferParams));
        transferParams.SourceGpuAddress = 0x0000000001000000ull;
        transferParams.DestinationGpuAddress = 0x0000000002000000ull;
        transferParams.TransferSizeBytes = 4096u;
        CHECK(Bc250PagingBuildTransfer(
            &transferParams, g_auditRing, AUDIT_RING_DWORDS, &cursor) == BC250_PAGING_OK);
        CHECK(cursor == 7u);
    }

    /* 11. Adapter Info */
    {
        BC250_ADAPTER_CAPABILITIES caps;
        CHECK(Bc250GetAdapterCapabilities(&caps) == BC250_ADAPTER_INFO_OK);
        CHECK(caps.NodeCount == 2u);
    }

    /* 12. Memory Segments */
    {
        BC250_SEGMENT_CONFIG segConfig;
        CHECK(Bc250BuildSegmentConfig(
            0x0000000400000000ull, 0x00000003E0000000ull,
            0x0000000010000000ull, 0x0000000020000000ull,
            &segConfig) == BC250_SEGMENT_OK);
        CHECK(segConfig.SegmentCount == 2u);
    }

    /* 13. TDR Recovery */
    {
        uint32_t isHung = 0;
        CHECK(Bc250TdrDetectHang(0u, 10u, 30u, 25u, 30u, &isHung) == BC250_TDR_OK);
        CHECK(isHung == 0u);
    }

    /* 14. Submit Command */
    {
        BC250_SUBMISSION_REQUEST submitReq;
        BC250_SUBMISSION_RESULT submitRes;
        uint32_t cursor = 0;
        memset(&submitReq, 0, sizeof(submitReq));
        submitReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
        submitReq.DmaBufferGpuVa = 0x00000000A0000000ull;
        submitReq.DmaBufferSizeBytes = 512u;
        submitReq.FenceGpuVa = 0x00000000B0000000ull;
        submitReq.FenceValue = 888ull;
        memset(g_auditRing, 0, sizeof(g_auditRing));
        CHECK(Bc250SubmitCommand(
            &submitReq, g_auditRing, AUDIT_RING_DWORDS, &cursor, &submitRes) == BC250_SUBMISSION_OK);
        CHECK(submitRes.UpdatedWptr == cursor);
    }

    /* 15. Preemption */
    {
        BC250_PREEMPTION_REQUEST preemptReq;
        BC250_PREEMPTION_RESULT preemptRes;
        uint32_t cursor = 0;
        memset(&preemptReq, 0, sizeof(preemptReq));
        preemptReq.NodeIndex = BC250_ADAPTER_NODE_COMPUTE;
        preemptReq.PreemptionFenceGpuVa = 0x00000000C0000000ull;
        preemptReq.PreemptionFenceId = 999ull;
        memset(g_auditRing, 0, sizeof(g_auditRing));
        CHECK(Bc250PreemptCommand(
            &preemptReq, g_auditRing, AUDIT_RING_DWORDS, &cursor, &preemptRes) == BC250_PREEMPTION_OK);
        CHECK(preemptRes.IsSuccessful == 1u);
    }

    /* 16. GPU Clock Calibration */
    {
        BC250_GPU_CLOCK_CALIBRATION calib;
        CHECK(Bc250CalibrateGpuClock(
            BC250_ADAPTER_NODE_COMPUTE, 0u, 0x1234u, 0x0u, 1000ull, 1000ull, &calib) == BC250_GPU_CLOCK_OK);
        CHECK(calib.IsValid == 1u);
    }

    /* 17. Power Management */
    {
        BC250_POWER_CONTEXT powerCtx;
        CHECK(Bc250PowerInitialize(&powerCtx) == BC250_POWER_OK);
        CHECK(Bc250PowerSetState(&powerCtx, BC250_POWER_STATE_D3, 0u) == BC250_POWER_OK);
        CHECK(Bc250PowerSetState(&powerCtx, BC250_POWER_STATE_D0, 0u) == BC250_POWER_OK);
        CHECK(powerCtx.CurrentPowerState == BC250_POWER_STATE_D0);
    }

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGlobalReadinessAuditTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGlobalReadinessAuditTest() ? 0 : 1;
}
#endif
