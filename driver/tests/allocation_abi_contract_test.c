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

#define MEMBER_OFFSET(type, member) ((size_t)&(((type *)0)->member))

static int TestAbiStructureFrozen(void)
{
    /*
     * CONTRATO DE TAMANHO E ALINHAMENTO FIXO DA ABI (V1)
     * A estrutura DEVE possuir exatamente 40 bytes em x64.
     */
    CHECK(sizeof(BC250_ALLOCATION_PRIVATE_V1) == 40u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Magic) == 0u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Version) == 4u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, StructSize) == 8u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Flags) == 12u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, AllocationToken) == 16u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, ByteSize) == 24u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Alignment) == 32u);
    return 1;
}

static int TestAbiFormatSuccessAndNegative(void)
{
    BC250_ALLOCATION_PRIVATE_V1 formatted;
    BC250_ALLOCATION_PRIVATE_V1 parsed;

    /* Caso bem-sucedido canônico */
    memset(&formatted, 0xFF, sizeof(formatted));
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(formatted.Magic == BC250_ALLOCATION_PRIVATE_MAGIC);
    CHECK(formatted.Version == BC250_ALLOCATION_PRIVATE_VERSION);
    CHECK(formatted.StructSize == (uint32_t)sizeof(BC250_ALLOCATION_PRIVATE_V1));
    CHECK(formatted.Flags == 0u);
    CHECK(formatted.AllocationToken == 0x1000ull);
    CHECK(formatted.ByteSize == 65536ull);
    CHECK(formatted.Alignment == 4096ull);

    /* O formato gerado deve passar perfeitamente pelo parser */
    CHECK(Bc250AllocationAbiParse(&formatted, sizeof(formatted), &parsed) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(parsed.AllocationToken == 0x1000ull);
    CHECK(parsed.ByteSize == 65536ull);

    /* Casos negativos de formato */
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 0u, NULL) ==
        BC250_ALLOCATION_ABI_BAD_ARGUMENT);
    CHECK(Bc250AllocationAbiFormat(0ull, 65536ull, 4096ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 0ull, 4096ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 1u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 2048ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4u * 1024u * 1024u, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 3000ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65537ull, 4096ull, 0u, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    return 1;
}

static int TestAbiParseAllErrorCodes(void)
{
    BC250_ALLOCATION_PRIVATE_V1 valid;
    BC250_ALLOCATION_PRIVATE_V1 test;
    BC250_ALLOCATION_PRIVATE_V1 output;

    CHECK(Bc250AllocationAbiFormat(0xCAFEull, 131072ull, 65536ull, 0u, &valid) ==
        BC250_ALLOCATION_ABI_OK);

    /* BAD_ARGUMENT */
    CHECK(Bc250AllocationAbiParse(NULL, sizeof(valid), &output) ==
        BC250_ALLOCATION_ABI_BAD_ARGUMENT);
    CHECK(Bc250AllocationAbiParse(&valid, sizeof(valid), NULL) ==
        BC250_ALLOCATION_ABI_BAD_ARGUMENT);

    /* BAD_SIZE (tamanho do buffer recebido diferente de 40) */
    CHECK(Bc250AllocationAbiParse(&valid, 0u, &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiParse(&valid, sizeof(valid) - 1u, &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiParse(&valid, sizeof(valid) + 1u, &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);

    /* BAD_MAGIC */
    test = valid;
    test.Magic = 0u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_MAGIC);
    test.Magic = 0x32414342u; /* BCA2 */
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_MAGIC);

    /* BAD_VERSION */
    test = valid;
    test.Version = 0u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_VERSION);
    test.Version = 2u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_VERSION);
    test.Version = valid.Version;
    test.StructSize = 32u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_VERSION);
    test.StructSize = 48u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_VERSION);

    /* BAD_FLAGS */
    test = valid;
    test.Flags = 1u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    test.Flags = 0x80000000u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);

    /* BAD_SIZE (token ou bytesize zero no payload) */
    test = valid;
    test.AllocationToken = 0ull;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    test = valid;
    test.ByteSize = 0ull;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);

    /* BAD_ALIGNMENT */
    test = valid;
    test.Alignment = 2048ull; /* < 4096 */
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    test.Alignment = 4u * 1024u * 1024u; /* > 2MB */
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    test.Alignment = 5000ull; /* not power of 2 */
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    test.Alignment = 65536ull;
    test.ByteSize = 65537ull; /* not multiple of alignment */
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);

    return 1;
}

static int TestAbiTokenClassification(void)
{
    BC250_ALLOCATION_PRIVATE_V1 normalAlloc;
    BC250_ALLOCATION_PRIVATE_V1 primaryAlloc;
    uint64_t primaryToken;

    CHECK(Bc250AllocationAbiFormat(0x5555ull, 4096ull, 4096ull, 0u, &normalAlloc) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(!Bc250AllocationIsPrimary(&normalAlloc));

    primaryToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
        BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 42ull;
    CHECK(Bc250AllocationAbiFormat(primaryToken, 8192ull, 4096ull, 0u, &primaryAlloc) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(Bc250AllocationIsPrimary(&primaryAlloc));

    CHECK(!Bc250AllocationIsPrimary(NULL));
    return 1;
}

static int Run(void)
{
    CHECK(TestAbiStructureFrozen());
    CHECK(TestAbiFormatSuccessAndNegative());
    CHECK(TestAbiParseAllErrorCodes());
    CHECK(TestAbiTokenClassification());
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess(Run() ? 0ul : 1ul); }
#else
int main(void) { return Run() ? 0 : 1; }
#endif
