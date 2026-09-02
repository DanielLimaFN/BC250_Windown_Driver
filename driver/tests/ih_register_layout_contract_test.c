#include "../core/bc250_ih_register_layout_contract.h"

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
#define CHECK(Expression) do { if (!(Expression)) return 0; } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { \
    if (!(Expression)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #Expression); \
        return 1; \
    } \
} while (0)
#endif

static int Bc250RunIhRegisterLayoutContractTest(void)
{
    BC250_IH_REGISTER_LAYOUT layout;
    BC250_IH_REGISTER_LAYOUT legacy3800;
    uint32_t inferredBase;

    CHECK(Bc250ComposeOsssys5IhRegisterLayout(5u, 0u, 0x10A0u,
          &layout) == BC250_IH_REGISTER_LAYOUT_OK);
    CHECK(layout.IhRbCntlBytes == 0x4480u);
    CHECK(layout.IhRbBaseBytes == 0x4484u);
    CHECK(layout.IhRbBaseHiBytes == 0x4488u);
    CHECK(layout.IhRbRptrBytes == 0x448Cu);
    CHECK(layout.IhRbWptrBytes == 0x4490u);
    CHECK(layout.IhRbWptrAddrHiBytes == 0x4494u);
    CHECK(layout.IhRbWptrAddrLoBytes == 0x4498u);
    CHECK(layout.IhDoorbellRptrBytes == 0x449Cu);
    CHECK(layout.IhCntlBytes == 0x4580u);
    CHECK(layout.IhCntl2Bytes == 0x4584u);
    CHECK(layout.IhStatusBytes == 0x4588u);
    CHECK(Bc250ClassifyOsssys5IhByteLayout(&layout, &inferredBase) ==
          BC250_IH_REGISTER_LAYOUT_OK);
    CHECK(inferredBase == 0x10A0u);

    memset(&legacy3800, 0, sizeof(legacy3800));
    legacy3800.IhRbBaseBytes = 0x3800u;
    legacy3800.IhRbBaseHiBytes = 0x3804u;
    legacy3800.IhRbCntlBytes = 0x3808u;
    legacy3800.IhRbRptrBytes = 0x3810u;
    legacy3800.IhRbWptrBytes = 0x3814u;
    legacy3800.IhDoorbellRptrBytes = 0x3818u;
    legacy3800.IhCntlBytes = 0x3820u;
    legacy3800.IhCntl2Bytes = 0x3824u;
    legacy3800.IhStatusBytes = 0x3830u;
    CHECK(Bc250ClassifyOsssys5IhByteLayout(&legacy3800, &inferredBase) ==
          BC250_IH_REGISTER_LAYOUT_MISMATCH);

    layout.IhRbCntlBytes = 0x4482u;
    CHECK(Bc250ClassifyOsssys5IhByteLayout(&layout, &inferredBase) ==
          BC250_IH_REGISTER_LAYOUT_BASE_UNALIGNED);
    CHECK(Bc250ComposeOsssys5IhRegisterLayout(6u, 0u, 0x10A0u,
          &layout) == BC250_IH_REGISTER_LAYOUT_UNSUPPORTED_VERSION);
    CHECK(Bc250ComposeOsssys5IhRegisterLayout(5u, 0u, 0x3FFFFFFFu,
          &layout) == BC250_IH_REGISTER_LAYOUT_OVERFLOW);
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess(Bc250RunIhRegisterLayoutContractTest() ? 1ul : 0ul);
}
#else
int main(void)
{
    int result = Bc250RunIhRegisterLayoutContractTest();
    if (result == 0) {
        printf("IH REGISTER LAYOUT: OK (OSSSYS 5.0 composition, offline)\n");
        printf("0x4480 family is structurally coherent; 0x3800 is rejected.\n");
        printf("No catalog promotion and no MMIO access were performed.\n");
        printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    }
    return result;
}
#endif
