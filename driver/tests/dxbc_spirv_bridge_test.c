#include "../core/bc250_dxbc_spirv_bridge.h"
#include <string.h>

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
    volatile unsigned char *d = (volatile unsigned char *)Destination;
    const volatile unsigned char *s = (const volatile unsigned char *)Source;
    size_t i;
    for (i = 0; i < Length; ++i) d[i] = s[i];
    return Destination;
}
#define CHECK(x) do { if (!(x)) return 0; } while (0)

typedef struct TEST_TRANSLATOR {
    uint32_t Words[5];
    int Result;
    int Frees;
    int SerializeCalls;
    int FreeDxbcCalls;
    uint8_t FakeDxbc[52];
} TEST_TRANSLATOR;

static int Compile(void *Context, const void *Dxbc, uint32_t DxbcBytes,
    const void **Spirv, uint32_t *SpirvBytes)
{
    TEST_TRANSLATOR *t = (TEST_TRANSLATOR *)Context;
    (void)Dxbc; (void)DxbcBytes;
    if (t->Result) return t->Result;
    *Spirv = t->Words; *SpirvBytes = sizeof(t->Words); return 0;
}
static void Free(void *Context, const void *Spirv)
{
    TEST_TRANSLATOR *t = (TEST_TRANSLATOR *)Context;
    (void)Spirv; t->Frees++;
}
static int Serialize(void *Context, size_t SectionCount,
    const BC250_DXBC_SECTION_DESC *Sections,
    const void **Dxbc, uint32_t *DxbcBytes)
{
    TEST_TRANSLATOR *t = (TEST_TRANSLATOR *)Context;
    (void)SectionCount; (void)Sections;
    t->SerializeCalls++;
    *Dxbc = t->FakeDxbc;
    *DxbcBytes = sizeof(t->FakeDxbc);
    return 0;
}
static void FreeDxbc(void *Context, const void *Dxbc)
{
    TEST_TRANSLATOR *t = (TEST_TRANSLATOR *)Context;
    (void)Dxbc; t->FreeDxbcCalls++;
}

static uint32_t BuildDxbc(uint8_t *b)
{
    BC250_DXBC_RAW_HEADER *h = (BC250_DXBC_RAW_HEADER *)b;
    BC250_DXBC_RAW_CHUNK_HEADER *c;
    uint32_t *u;
    memset(b, 0, 52);
    h->Magic=BC250_DXBC_MAGIC_DXBC; h->Version=1; h->TotalSize=52; h->ChunkCount=1;
    ((uint32_t *)(b + 32))[0]=36;
    c=(BC250_DXBC_RAW_CHUNK_HEADER *)(b+36); c->FourCC=BC250_DXBC_FOURCC_SHDR; c->ChunkSize=8;
    u=(uint32_t *)(b+44); u[0]=(1u<<16)|(4u<<4)|1u; u[1]=2; return 52;
}

static int Run(void)
{
    uint8_t dxbc[52]; BC250_DXBC_SPIRV_MODULE m; TEST_TRANSLATOR t;
    BC250_DXBC_SPIRV_TRANSLATOR api;
    uint32_t tokens[4];
    BC250_DXBC_SIGNATURE_ENTRY inSig;
    BC250_DXBC_SIGNATURE_ENTRY outSig;
    BC250_DXBC_STAGE_SIGNATURES sigs;

    BuildDxbc(dxbc); memset(&t,0,sizeof(t));
    BuildDxbc(t.FakeDxbc);
    t.Words[0]=BC250_SPIRV_MAGIC; t.Words[1]=0x00010300; t.Words[3]=2; t.Words[4]=0;
    api.Context=&t; api.Compile=Compile; api.Free=Free;
    api.SerializeDxbc=Serialize; api.FreeDxbc=FreeDxbc;

    CHECK(Bc250DxbcTranslateToSpirv(dxbc,sizeof(dxbc),&api,&m)==BC250_DXBC_SPIRV_OK);
    CHECK(m.Stage==BC250_DXBC_SHADER_VERTEX && m.SizeBytes==20 && t.Frees==0);
    Bc250DxbcReleaseSpirv(&m); CHECK(t.Frees==1 && m.Words==0);
    t.Words[0]=0; CHECK(Bc250DxbcTranslateToSpirv(dxbc,sizeof(dxbc),&api,&m)==BC250_DXBC_SPIRV_INVALID_OUTPUT);
    CHECK(t.Frees==2);
    dxbc[0]=0; CHECK(Bc250DxbcTranslateToSpirv(dxbc,sizeof(dxbc),&api,&m)==BC250_DXBC_SPIRV_INVALID_DXBC);

    /* Test Token Stream translation */
    tokens[0] = (1u << 16) | (4u << 4) | 1u; /* Vertex shader, SM 4.1 */
    tokens[1] = 4u;
    tokens[2] = 0x1234u;
    tokens[3] = 0x5678u;

    inSig.SystemValue = 6u; /* SV_VertexID */
    inSig.Register = 0u;
    inSig.Mask = 1u;

    outSig.SystemValue = 1u; /* SV_Position */
    outSig.Register = 0u;
    outSig.Mask = 0xFu;

    sigs.InputSignatures = &inSig;
    sigs.NumInputSignatures = 1u;
    sigs.OutputSignatures = &outSig;
    sigs.NumOutputSignatures = 1u;

    t.Words[0] = BC250_SPIRV_MAGIC;
    CHECK(Bc250DxbcTranslateTokenStreamToSpirv(tokens, &sigs, &api, &m) == BC250_DXBC_SPIRV_OK);
    CHECK(m.Stage == BC250_DXBC_SHADER_VERTEX && m.SizeBytes == 20);
    CHECK(t.SerializeCalls == 1 && t.FreeDxbcCalls == 1);
    Bc250DxbcReleaseSpirv(&m);

    return 1;
}
void mainCRTStartup(void) { ExitProcess(Run()?0u:1u); }
