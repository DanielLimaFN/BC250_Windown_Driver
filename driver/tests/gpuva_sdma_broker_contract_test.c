#include "../core/bc250_gpuva_sdma_broker_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

static int Bc250RunGpuvaSdmaBrokerContractTest(void)
{
    static BC250_MCDM_OBJECT_REGISTRY objects;
    static BC250_GPUVA_SDMA_BROKER broker;
    BC250_MCDM_HANDLE processA = NULL, processB = NULL;
    BC250_MCDM_HANDLE deviceA = NULL, deviceB = NULL;
    BC250_MCDM_HANDLE contextA = NULL, contextB = NULL;
    BC250_MCDM_HANDLE allocationA = NULL, allocationB = NULL;
    BC250_MCDM_HANDLE openA = NULL, openB = NULL;
    uint64_t sourceLease = 0ull, destinationLease = 0ull;

    CHECK(Bc250McdmObjectRegistryInitialize(&objects) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250GpuvaSdmaBrokerInitialize(&broker, 0x100000000ull) ==
        BC250_GPUVA_SDMA_BROKER_OK);
    CHECK(Bc250McdmObjectCreateProcess(&objects, (void *)0xA001, &processA) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateProcess(&objects, (void *)0xB001, &processB) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_DEVICE,
        processA, processA, &deviceA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_DEVICE,
        processB, processB, &deviceB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_CONTEXT,
        deviceA, processA, &contextA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&objects, BC250_MCDM_OBJECT_CONTEXT,
        deviceB, processB, &contextB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateAdapterAllocation(&objects, (void *)0xA110,
        0xA110u, 65536u, 4096u, &allocationA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateAdapterAllocation(&objects, (void *)0xB110,
        0xB110u, 65536u, 4096u, &allocationB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectOpenAllocation(&objects, deviceA, allocationA,
        (void *)0xA120, &openA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectOpenAllocation(&objects, deviceB, allocationB,
        (void *)0xB120, &openB) == BC250_MCDM_OBJECT_OK);

    /* Leases are impossible before the MCDM process has a root page table. */
    CHECK(Bc250GpuvaSdmaBrokerIssueLease(&broker, &objects, processA, openA,
        0x100000ull, 4096u, &sourceLease) ==
        BC250_GPUVA_SDMA_BROKER_MAPPING_NOT_READY);
    CHECK(sourceLease == 0ull);
    CHECK(Bc250McdmObjectSetRootPageTable(&objects, contextA, 1u,
        0x12345000ull, 512u, 512u) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectSetRootPageTable(&objects, contextB, 1u,
        0x22345000ull, 512u, 512u) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250GpuvaSdmaBrokerIssueLease(&broker, &objects, processA, openA,
        0x100000ull, 4096u, &sourceLease) == BC250_GPUVA_SDMA_BROKER_OK);
    CHECK(Bc250GpuvaSdmaBrokerIssueLease(&broker, &objects, processB, openB,
        0x200000ull, 4096u, &destinationLease) == BC250_GPUVA_SDMA_BROKER_OK);
    CHECK(sourceLease != 0ull && destinationLease != 0ull &&
        sourceLease != destinationLease);
    CHECK(Bc250GpuvaSdmaBrokerValidatePair(&broker, sourceLease,
        destinationLease, 0x300000ull, 4096u) == BC250_GPUVA_SDMA_BROKER_OK);
    CHECK(Bc250GpuvaSdmaBrokerValidatePair(&broker, sourceLease,
        sourceLease, 0x300000ull, 4096u) ==
        BC250_GPUVA_SDMA_BROKER_PAIR_NOT_ISOLATED);
    CHECK(Bc250GpuvaSdmaBrokerRevokeLease(&broker, sourceLease) ==
        BC250_GPUVA_SDMA_BROKER_OK);
    CHECK(Bc250GpuvaSdmaBrokerValidatePair(&broker, sourceLease,
        destinationLease, 0x300000ull, 4096u) ==
        BC250_GPUVA_SDMA_BROKER_STALE_LEASE);
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunGpuvaSdmaBrokerContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunGpuvaSdmaBrokerContractTest() ? 0 : 1;
}
#endif
