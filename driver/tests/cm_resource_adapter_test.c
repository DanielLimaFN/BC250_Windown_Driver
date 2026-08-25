#include "../wdk/bc250_cm_resource_adapter.h"

__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);

#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t index;
    for (index = 0; index < Length; ++index) {
        bytes[index] = (unsigned char)Value;
    }
    return Destination;
}

static void Bc250TestZero(void *Buffer, size_t Length)
{
    unsigned char *bytes = (unsigned char *)Buffer;
    size_t index;
    for (index = 0; index < Length; ++index) bytes[index] = 0;
}

static CM_FULL_RESOURCE_DESCRIPTOR *
Bc250TestAppendFull(unsigned char **Cursor, ULONG DescriptorCount)
{
    CM_FULL_RESOURCE_DESCRIPTOR *full =
        (CM_FULL_RESOURCE_DESCRIPTOR *)(void *)*Cursor;
    Bc250TestZero(full,
        FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                     PartialResourceList.PartialDescriptors) +
        (size_t)DescriptorCount * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
    full->PartialResourceList.Version = 1;
    full->PartialResourceList.Revision = 1;
    full->PartialResourceList.Count = DescriptorCount;
    *Cursor = (unsigned char *)&full->PartialResourceList.PartialDescriptors[
        DescriptorCount];
    return full;
}

static int Bc250RunCmResourceAdapterTest(void)
{
    __declspec(align(8)) unsigned char blob[1024];
    CM_RESOURCE_LIST *list = (CM_RESOURCE_LIST *)(void *)blob;
    CM_FULL_RESOURCE_DESCRIPTOR *full0, *full1;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *descriptor;
    BC250_MEMORY_RESOURCE memory[8];
    BC250_PCI_BAR_RESOURCE bars[3];
    BC250_RESOLVED_BAR_RESOURCE resolved[3];
    BC250_RESOURCE_STATUS contractStatus;
    BC250_CM_RESOURCE_STATUS status;
    size_t count = 0;
    unsigned char *cursor;

    Bc250TestZero(blob, sizeof(blob));
    list->Count = 2;
    cursor = (unsigned char *)&list->List[0];

    full0 = Bc250TestAppendFull(&cursor, 2);
    descriptor = &full0->PartialResourceList.PartialDescriptors[0];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xC0000000ll;
    descriptor->u.Memory.Length = 0x10200000u;
    full0->PartialResourceList.PartialDescriptors[1].Type =
        CmResourceTypeInterrupt;

    full1 = Bc250TestAppendFull(&cursor, 3);
    descriptor = &full1->PartialResourceList.PartialDescriptors[0];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xD0000000ll;
    descriptor->u.Memory.Length = 0x00200000u;
    descriptor = &full1->PartialResourceList.PartialDescriptors[1];
    descriptor->Type = CmResourceTypeMemory;
    descriptor->u.Memory.Start.QuadPart = 0xFE800000ll;
    descriptor->u.Memory.Length = 0x00080000u;
    descriptor = &full1->PartialResourceList.PartialDescriptors[2];
    descriptor->Type = CmResourceTypeMemoryLarge;
    descriptor->Flags = CM_RESOURCE_MEMORY_LARGE_48;
    descriptor->u.Memory48.Start.QuadPart = 0x0000010000000000ll;
    descriptor->u.Memory48.Length48 = 2u;

    status = Bc250CollectTranslatedMemoryResources(
        list, memory, RTL_NUMBER_OF(memory), &count);
    if (status != BC250_CM_RESOURCE_OK || count != 4 ||
        memory[0].Start != 0xC0000000ull ||
        memory[0].Length != 0x10200000ull ||
        memory[3].Start != 0x0000010000000000ull ||
        memory[3].Length != 0x0000000000020000ull) {
        return 0;
    }

    Bc250TestZero(bars, sizeof(bars));
    bars[0].Index = 5;
    bars[0].Present = 1;
    bars[0].Base = 0xFE800000ull;
    bars[1].Index = 0;
    bars[1].Present = 1;
    bars[1].Base = 0xC0000000ull;
    bars[2].Index = 2;
    bars[2].Present = 1;
    bars[2].Base = 0xD0000000ull;
    status = Bc250ResolveCmTranslatedBars(
        list, bars, RTL_NUMBER_OF(bars), resolved, RTL_NUMBER_OF(resolved),
        &count, &contractStatus);
    if (status != BC250_CM_RESOURCE_OK ||
        contractStatus != BC250_RESOURCE_OK || count != 3 ||
        resolved[0].Length != 0x00080000ull ||
        resolved[1].Length != 0x10000000ull ||
        resolved[2].Length != 0x00200000ull) {
        return 0;
    }

    status = Bc250CollectTranslatedMemoryResources(list, memory, 2, &count);
    if (status != BC250_CM_RESOURCE_OUTPUT_TOO_SMALL || count != 0) return 0;

    full0->PartialResourceList.PartialDescriptors[1].Type =
        CmResourceTypeDeviceSpecific;
    status = Bc250CollectTranslatedMemoryResources(
        list, memory, RTL_NUMBER_OF(memory), &count);
    if (status != BC250_CM_RESOURCE_UNSUPPORTED_LAYOUT || count != 0) return 0;
    full0->PartialResourceList.PartialDescriptors[1].Type =
        CmResourceTypeInterrupt;

    descriptor = &full1->PartialResourceList.PartialDescriptors[2];
    descriptor->Flags = 0;
    status = Bc250CollectTranslatedMemoryResources(
        list, memory, RTL_NUMBER_OF(memory), &count);
    if (status != BC250_CM_RESOURCE_BAD_MEMORY_DESCRIPTOR || count != 0) return 0;

    return 1;
}

void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunCmResourceAdapterTest() ? 0ul : 1ul);
}
