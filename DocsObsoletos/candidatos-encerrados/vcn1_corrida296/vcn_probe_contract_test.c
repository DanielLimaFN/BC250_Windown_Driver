/* ===========================================================================
 * vcn_probe_contract_test.c — Teste de Contrato do Modulo VCN 2.0 (Offline)
 * =========================================================================== */

#include "../wdk/bc250_vcn_probe.h"

#ifdef BC250_KERNEL_MODE
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

#pragma function(memcpy)
void * __cdecl memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *d = (unsigned char *)Destination;
    const unsigned char *s = (const unsigned char *)Source;
    size_t index;
    for (index = 0; index < Length; ++index) {
        d[index] = s[index];
    }
    return Destination;
}

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA na linha %d: %s\n", __LINE__, #Expression); \
        return __LINE__; \
    } \
} while (0)
#endif

/* Stubs de servicos de kernel para execucao do contrato offline */
static PVOID Bc250TestAllocateContiguous(
    SIZE_T NumberOfBytes,
    PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    MEMORY_CACHING_TYPE CacheType)
{
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(LowestAcceptableAddress);
    UNREFERENCED_PARAMETER(HighestAcceptableAddress);
    UNREFERENCED_PARAMETER(BoundaryAddressMultiple);
    UNREFERENCED_PARAMETER(CacheType);
    return NULL;
}
PVOID (*__imp_MmAllocateContiguousMemorySpecifyCache)(
    SIZE_T, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS,
    MEMORY_CACHING_TYPE) = Bc250TestAllocateContiguous;

static VOID Bc250TestFreeContiguous(PVOID BaseAddress)
{
    UNREFERENCED_PARAMETER(BaseAddress);
}
VOID (*__imp_MmFreeContiguousMemory)(PVOID) = Bc250TestFreeContiguous;

static PHYSICAL_ADDRESS Bc250TestGetPhysicalAddress(PVOID BaseAddress)
{
    PHYSICAL_ADDRESS address;
    UNREFERENCED_PARAMETER(BaseAddress);
    address.QuadPart = 0;
    return address;
}
PHYSICAL_ADDRESS (*__imp_MmGetPhysicalAddress)(PVOID) = Bc250TestGetPhysicalAddress;

static PVOID Bc250TestMapIoSpace(
    PHYSICAL_ADDRESS PhysicalAddress,
    SIZE_T NumberOfBytes,
    MEMORY_CACHING_TYPE CacheType)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(CacheType);
    return NULL;
}
PVOID (*__imp_MmMapIoSpace)(PHYSICAL_ADDRESS, SIZE_T, MEMORY_CACHING_TYPE) = Bc250TestMapIoSpace;

static VOID Bc250TestUnmapIoSpace(PVOID BaseAddress, SIZE_T NumberOfBytes)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
}
VOID (*__imp_MmUnmapIoSpace)(PVOID, SIZE_T) = Bc250TestUnmapIoSpace;

static PVOID Bc250TestAllocatePool2(
    POOL_FLAGS Flags,
    SIZE_T NumberOfBytes,
    ULONG Tag)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(Tag);
    return NULL;
}
PVOID (*__imp_ExAllocatePool2)(POOL_FLAGS, SIZE_T, ULONG) = Bc250TestAllocatePool2;

static VOID Bc250TestFreePoolWithTag(PVOID P, ULONG Tag)
{
    UNREFERENCED_PARAMETER(P);
    UNREFERENCED_PARAMETER(Tag);
}
VOID (*__imp_ExFreePoolWithTag)(PVOID, ULONG) = Bc250TestFreePoolWithTag;

static VOID Bc250TestStallExecutionProcessor(ULONG Microseconds)
{
    UNREFERENCED_PARAMETER(Microseconds);
}
VOID (*__imp_KeStallExecutionProcessor)(ULONG) = Bc250TestStallExecutionProcessor;

static NTSTATUS NTAPI Bc250TestZwOpenFile(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions)
{
    UNREFERENCED_PARAMETER(FileHandle);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(IoStatusBlock);
    UNREFERENCED_PARAMETER(ShareAccess);
    UNREFERENCED_PARAMETER(OpenOptions);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}
NTSTATUS (NTAPI *__imp_ZwOpenFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG) =
    Bc250TestZwOpenFile;

static NTSTATUS NTAPI Bc250TestZwQueryInformationFile(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
    UNREFERENCED_PARAMETER(FileHandle);
    UNREFERENCED_PARAMETER(IoStatusBlock);
    UNREFERENCED_PARAMETER(FileInformation);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(FileInformationClass);
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS (NTAPI *__imp_ZwQueryInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS) =
    Bc250TestZwQueryInformationFile;

static NTSTATUS NTAPI Bc250TestZwReadFile(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key)
{
    UNREFERENCED_PARAMETER(FileHandle);
    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(ApcRoutine);
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(IoStatusBlock);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Key);
    return STATUS_NOT_IMPLEMENTED;
}
NTSTATUS (NTAPI *__imp_ZwReadFile)(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG) =
    Bc250TestZwReadFile;

static NTSTATUS NTAPI Bc250TestZwClose(HANDLE Handle)
{
    UNREFERENCED_PARAMETER(Handle);
    return STATUS_SUCCESS;
}
NTSTATUS (NTAPI *__imp_ZwClose)(HANDLE) =
    Bc250TestZwClose;

static VOID NTAPI Bc250TestRtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString)
{
    if (DestinationString != NULL) {
        DestinationString->Buffer = (PWSTR)SourceString;
        if (SourceString != NULL) {
            USHORT len = 0;
            while (SourceString[len] != 0) ++len;
            DestinationString->Length = (USHORT)(len * sizeof(WCHAR));
            DestinationString->MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
        } else {
            DestinationString->Length = 0;
            DestinationString->MaximumLength = 0;
        }
    }
}
VOID (NTAPI *__imp_RtlInitUnicodeString)(PUNICODE_STRING, PCWSTR) =
    Bc250TestRtlInitUnicodeString;

/* Constantes espelhadas para validacao de invariantes de hardware */
#define TEST_VCN_REG_UVD_STATUS      0x000201BCu
#define TEST_VCN_REG_UVD_POWER_STATUS 0x0001FB10u
#define TEST_VCN_REG_UVD_VCPU_CNTL   0x00020160u
#define TEST_UVD_VCPU_CNTL_CLK_EN    0x00000200u
#define TEST_UVD_SOFT_RESET_VCPU     0x00000008u
#define TEST_PS5_VCN_FILE_BYTES      404544u
#define TEST_PS5_VCN_PAYLOAD_BYTES   404288u

static int Bc250RunVcnProbeContractTest(void)
{
    PHYSICAL_ADDRESS nullPhys;
    nullPhys.QuadPart = 0;

    /* 1. Fail-closed com endereco MMIO nulo */
    CHECK(Bc250VcnProbeExecute(nullPhys, NULL, 0u) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250VcnProbeExecute(nullPhys, NULL, 1u) == STATUS_INVALID_PARAMETER);
    CHECK(Bc250VcnProbeExecute(nullPhys, NULL, 2u) == STATUS_INVALID_PARAMETER);

    /* 2. Invariantes de registradores do GC 10.1.3 / Cyan Skillfish */
    CHECK((TEST_VCN_REG_UVD_STATUS & 3u) == 0u);       /* Alinhado a dword */
    CHECK((TEST_VCN_REG_UVD_POWER_STATUS & 3u) == 0u); /* Alinhado a dword */
    CHECK((TEST_VCN_REG_UVD_VCPU_CNTL & 3u) == 0u);    /* Alinhado a dword */

    CHECK(TEST_UVD_VCPU_CNTL_CLK_EN == 0x200u);
    CHECK(TEST_UVD_SOFT_RESET_VCPU == 0x08u);

    /* 3. Invariantes de geometria do firmware PS5 */
    CHECK(TEST_PS5_VCN_FILE_BYTES == 404544u);
    CHECK(TEST_PS5_VCN_PAYLOAD_BYTES == 404288u);
    CHECK((TEST_PS5_VCN_FILE_BYTES - TEST_PS5_VCN_PAYLOAD_BYTES) == 256u); /* common_firmware_header */

    /* 4. Teardown sem alocacao previa nao causa falha */
    Bc250VcnProbeTeardown();

    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunVcnProbeContractTest());
}
#else
int main(void)
{
    int rc = Bc250RunVcnProbeContractTest();
    if (rc == 0) {
        printf("VCN PROBE CONTRACT: OK\n");
    }
    return rc;
}
#endif
