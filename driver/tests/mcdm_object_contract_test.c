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

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) { printf("FAIL line %d: %s\n", __LINE__, #Expression); return __LINE__; } } while (0)
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
    CHECK(Bc250McdmObjectSetRootPageTable(&g_registry, contextA,
        1u, 0x0000000012345000ull, 512u, 512u) == BC250_MCDM_OBJECT_OK);
    CHECK(Bc250McdmObjectLookup(&g_registry, processA,
        BC250_MCDM_OBJECT_PROCESS, processA, &record) ==
        BC250_MCDM_OBJECT_OK);
    CHECK(record.RootPageTableSegmentId == 1u &&
        record.RootPageTableAddress == 0x0000000012345000ull &&
        record.RootPageTableEntries == 512u &&
        record.RootPageTableSetCount == 1u);
    CHECK(Bc250McdmObjectSetRootPageTable(&g_registry, contextA,
        1u, 0x0000000012345001ull, 512u, 512u) ==
        BC250_MCDM_OBJECT_BAD_ARGUMENT);
    CHECK(Bc250McdmObjectSetRootPageTable(&g_registry, contextA,
        0u, 0u, 0u, 512u) == BC250_MCDM_OBJECT_OK);
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

    /* Testes de Contexto Seguro e Visão de Submissão */
    {
        static BC250_MCDM_OBJECT_REGISTRY s_registry2;
        BC250_MCDM_HANDLE proc = NULL;
        BC250_MCDM_HANDLE dev = NULL;
        BC250_MCDM_HANDLE ctx = NULL;
        BC250_CONTEXT_SUBMISSION_VIEW view;
        const void *adapter = (const void *)0xC001;
        const void *wrongAdapter = (const void *)0xC002;

        CHECK(Bc250McdmObjectRegistryInitialize(&s_registry2) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectCreateProcess(&s_registry2, (void *)0x1111, &proc) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmObjectCreateChild(&s_registry2, BC250_MCDM_OBJECT_DEVICE, proc, proc, &dev) == BC250_MCDM_OBJECT_OK);
        
        /* NodeOrdinal 1 (Compute MEC) */
        CHECK(Bc250McdmObjectCreateContext(&s_registry2, adapter, dev, proc, 1u, 0u, &ctx) == BC250_MCDM_OBJECT_OK);
        CHECK(ctx != NULL);

        /* Falha antes de configurar Root Page Table */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_NO_ROOT_PAGE_TABLE);

        /* Configurar Root Page Table válida */
        CHECK(Bc250McdmObjectSetRootPageTable(&s_registry2, ctx, 1u, 0x0000000020000000ull, 1024u, 1024u) ==
            BC250_MCDM_OBJECT_OK);

        /* Aquisição com parâmetros corretos */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_OK);
        CHECK(view.ContextHandle == ctx);
        CHECK(view.ParentDevice == dev);
        CHECK(view.OwnerProcess == proc);
        CHECK(view.OwnerAdapter == adapter);
        CHECK(view.NodeOrdinal == 1u);
        CHECK(view.RootPageTableAddress == 0x0000000020000000ull);
        CHECK(view.RootPageTableEntries == 1024u);

        /* Rejeição por NodeOrdinal incorreto (SDMA Node 0) */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, adapter, 0u, &view) ==
            BC250_MCDM_OBJECT_NODE_MISMATCH);

        /* Rejeição por adaptador incorreto */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, wrongAdapter, 1u, &view) ==
            BC250_MCDM_OBJECT_OWNER_MISMATCH);

        /* Rejeição por handle nulo ou inválido */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, NULL, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_BAD_ARGUMENT);
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, (void *)0x9999, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_INVALID_HANDLE);

        /* Tentativa de destruição enquanto InFlightSubmissions > 0 */
        CHECK(Bc250McdmObjectDestroy(&s_registry2, ctx, BC250_MCDM_OBJECT_CONTEXT, proc) ==
            BC250_MCDM_OBJECT_IN_FLIGHT_SUBMISSIONS);

        /* Liberação da visão */
        CHECK(Bc250McdmContextReleaseSubmissionView(&s_registry2, ctx) == BC250_MCDM_OBJECT_OK);

        /* Agora a destruição deve ter sucesso */
        CHECK(Bc250McdmObjectDestroy(&s_registry2, ctx, BC250_MCDM_OBJECT_CONTEXT, proc) ==
            BC250_MCDM_OBJECT_OK);

        /* Tentativa de adquirir contexto morto falha com stale handle */
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_STALE_HANDLE);

        /* Teardown invalida aquisições */
        CHECK(Bc250McdmObjectRegistryTeardown(&s_registry2, &destroyed) == BC250_MCDM_OBJECT_OK);
        CHECK(Bc250McdmContextAcquireSubmissionView(&s_registry2, ctx, adapter, 1u, &view) ==
            BC250_MCDM_OBJECT_IN_TEARDOWN);
    }

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunMcdmObjectContractTest());
}
#else
int main(void)
{
    return Bc250RunMcdmObjectContractTest();
}
#endif
