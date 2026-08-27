#include "../core/bc250_allocation_abi_contract.h"

#ifdef BC250_KERNEL_MODE
__declspec(dllimport) void __stdcall ExitProcess(unsigned long ExitCode);
#pragma function(memset)
void * __cdecl memset(void *Destination, int Value, size_t Length)
{
    volatile unsigned char *bytes = (volatile unsigned char *)Destination;
    size_t i;
    for (i = 0; i < Length; ++i) bytes[i] = (unsigned char)Value;
    return Destination;
}
#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    const unsigned char *s = (const unsigned char *)Source;
    size_t i;
    for (i = 0; i < Length; ++i) d[i] = s[i];
    return Destination;
}
#define CHECK(x) do { if (!(x)) return 0; } while (0)
#else
#define CHECK(x) do { if (!(x)) return 1; } while (0)
#endif

static int Run(void)
{
    BC250_ALLOCATION_PRIVATE_V1 input;
    BC250_ALLOCATION_PRIVATE_V1 output;
    memset(&input, 0, sizeof(input));
    input.Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
    input.Version = BC250_ALLOCATION_PRIVATE_VERSION;
    input.StructSize = sizeof(input);
    input.AllocationToken = 0xA110u;
    input.ByteSize = 65536u;
    input.Alignment = 4096u;
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input), &output) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(output.ByteSize == input.ByteSize);
    input.AllocationToken = 0u;
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input), &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    input.AllocationToken = 0xA110u;
    input.Flags = 1u;
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input), &output) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    input.Flags = 0u;
    input.Alignment = 3000u;
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    input.Alignment = 4096u;
    input.ByteSize = 4097u;
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiParse(&input, sizeof(input) - 1u, &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess(Run() ? 0ul : 1ul); }
#else
int main(void) { return Run() ? 0 : 1; }
#endif
