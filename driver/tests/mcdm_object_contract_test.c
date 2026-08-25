#include "../core/bc250_mcdm_object_contract.h"

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
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

static BC250_MCDM_OBJECT_REGISTRY g_registry;

static int Bc250RunMcdmObjectContractTest(void)
{
    BC250_MCDM_HANDLE processA = NULL, processB = NULL;
    BC250_MCDM_HANDLE deviceA = NULL, deviceB = NULL;
    BC250_MCDM_HANDLE contextA = NULL, allocationA = NULL, openA = NULL;
    BC250_MCDM_HANDLE staleContext;
    BC250_MCDM_OBJECT_RECORD record;
    uint32_t destroyed;
    uint32_t index;

    CHECK(Bc250McdmObjectRegistryInitialize(&g_registry) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateProcess(&g_registry, (void *)0xA001, &processA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateProcess(&g_registry, (void *)0xB001, &processB) == BC250_MCDM_OBJECT_OK);
    CHECK(processA != NULL && processB != NULL && processA != processB);
    CHECK(Bc250McdmObjectCreateProcess(&g_registry, (void *)0xA001, &contextA) == BC250_MCDM_OBJECT_DUPLICATE_OWNER);
    CHECK(contextA == NULL);

    CHECK(Bc250McdmObjectCreateChild(&g_registry, BC250_MCDM_OBJECT_DEVICE,
        processA, processA, &deviceA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&g_registry, BC250_MCDM_OBJECT_DEVICE,
        processB, processB, &deviceB) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateChild(&g_registry, BC250_MCDM_OBJECT_CONTEXT,
        deviceA, processA, &contextA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateAdapterAllocation(&g_registry,
        (void *)0xA110, 0xA110u, 65536u, 4096u, &allocationA) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectCreateAdapterAllocation(&g_registry,
        (void *)0xA111, 0xA110u, 65536u, 4096u, &openA) ==
        BC250_MCDM_OBJECT_DUPLICATE_OWNER);
    CHECK(openA == NULL);

    CHECK(Bc250McdmObjectLookup(&g_registry, contextA,
        BC250_MCDM_OBJECT_CONTEXT, processA, &record) == BC250_MCDM_OBJECT_OK);
    CHECK(record.Generation != 0u && record.Parent == deviceA);
    CHECK(Bc250McdmObjectLookup(&g_registry, allocationA,
        BC250_MCDM_OBJECT_ALLOCATION, NULL, &record) == BC250_MCDM_OBJECT_OK);
    CHECK(record.OwnerProcess == NULL && record.Parent == NULL &&
        record.AllocationToken == 0xA110u);
    CHECK(record.AllocationBytes == 65536u &&
        record.AllocationAlignment == 4096u);
    CHECK(Bc250McdmObjectLookup(&g_registry, contextA,
        BC250_MCDM_OBJECT_ALLOCATION, processA, &record) == BC250_MCDM_OBJECT_TYPE_MISMATCH);
    CHECK(Bc250McdmObjectLookup(&g_registry, contextA,
        BC250_MCDM_OBJECT_CONTEXT, processB, &record) == BC250_MCDM_OBJECT_OWNER_MISMATCH);
    CHECK(Bc250McdmObjectLookup(&g_registry, (void *)0x1234,
        BC250_MCDM_OBJECT_CONTEXT, processA, &record) == BC250_MCDM_OBJECT_INVALID_HANDLE);
    CHECK(Bc250McdmObjectCreateChild(&g_registry, BC250_MCDM_OBJECT_CONTEXT,
        deviceA, processB, &staleContext) == BC250_MCDM_OBJECT_OWNER_MISMATCH);

    CHECK(Bc250McdmObjectFindAllocationByToken(&g_registry, 0xA110u,
        &openA) == BC250_MCDM_OBJECT_OK && openA == allocationA);
    CHECK(Bc250McdmObjectOpenAllocation(&g_registry, deviceA, allocationA,
        (void *)0xD311u, &openA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectOpenAllocation(&g_registry, deviceA, allocationA,
        (void *)0xD312u, &staleContext) ==
        BC250_MCDM_OBJECT_DUPLICATE_OWNER);
    CHECK(staleContext == NULL);
    CHECK(Bc250McdmObjectLookup(&g_registry, openA,
        BC250_MCDM_OBJECT_OPEN_ALLOCATION, processA, &record) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(record.Parent == deviceA && record.ReferencedObject == allocationA);
    CHECK(Bc250McdmObjectDestroy(&g_registry, allocationA,
        BC250_MCDM_OBJECT_ALLOCATION, NULL) == BC250_MCDM_OBJECT_CHILDREN_LIVE);
    CHECK(Bc250McdmObjectDestroy(&g_registry, openA,
        BC250_MCDM_OBJECT_OPEN_ALLOCATION, processA) == BC250_MCDM_OBJECT_OK);

    CHECK(Bc250McdmObjectDestroy(&g_registry, deviceA,
        BC250_MCDM_OBJECT_DEVICE, processA) == BC250_MCDM_OBJECT_CHILDREN_LIVE);
    staleContext = contextA;
    CHECK(Bc250McdmObjectDestroy(&g_registry, contextA,
        BC250_MCDM_OBJECT_CONTEXT, processA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectDestroy(&g_registry, staleContext,
        BC250_MCDM_OBJECT_CONTEXT, processA) == BC250_MCDM_OBJECT_STALE_HANDLE);
    CHECK(Bc250McdmObjectDestroy(&g_registry, allocationA,
        BC250_MCDM_OBJECT_ALLOCATION, NULL) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectDestroy(&g_registry, deviceA,
        BC250_MCDM_OBJECT_DEVICE, processA) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectDestroy(&g_registry, processA,
        BC250_MCDM_OBJECT_PROCESS, processA) == BC250_MCDM_OBJECT_OK);

    /* Exhaustion never reuses tombstones, so stale pointer aliases are avoided. */
    for (index = g_registry.NextSlot; index < BC250_MCDM_MAX_OBJECTS; ++index) {
        BC250_MCDM_HANDLE handle;
        const void *owner = (const void *)(ULONG_PTR)(0x10000u + index);
        CHECK(Bc250McdmObjectCreateProcess(&g_registry, owner, &handle) == BC250_MCDM_OBJECT_OK);
    }
    CHECK(Bc250McdmObjectCreateProcess(&g_registry, (void *)0xFFFF,
        &contextA) == BC250_MCDM_OBJECT_NO_SPACE);
    CHECK(contextA == NULL);

    destroyed = 0u;
    CHECK(Bc250McdmObjectRegistryTeardown(&g_registry, &destroyed) == BC250_MCDM_OBJECT_OK);
    CHECK(destroyed != 0u && g_registry.LiveCount == 0u);
    destroyed = 1u;
    CHECK(Bc250McdmObjectRegistryTeardown(&g_registry, &destroyed) == BC250_MCDM_OBJECT_OK);
    CHECK(destroyed == 0u);
    CHECK(Bc250McdmObjectLookup(&g_registry, deviceB,
        BC250_MCDM_OBJECT_DEVICE, processB, &record) == BC250_MCDM_OBJECT_STALE_HANDLE);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunMcdmObjectContractTest() ? 0ul : 1ul);
}
#else
int main(void)
{
    return Bc250RunMcdmObjectContractTest() ? 0 : 1;
}
#endif
