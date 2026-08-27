#include "../core/bc250_gcvm_sdma_vmid_transaction.h"
#include "../core/bc250_sdma_packet_builder.h"

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

#define CHECK(x) do { if (!(x)) { ExitProcess((unsigned long)__LINE__); return 0; } } while (0)
#else
#include <stdio.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#endif

static unsigned long g_failureLine;

/* Fixture: ring + PTEs + destino. Tudo na memoria do usuario. */
typedef struct FIXTURE {
    uint64_t Ptes[16];         /* ate 16 paginas = 64 KiB */
    uint32_t Ring[BC250_GSVT_RING_DWORDS];
    uint8_t  Destination[16 * 4096];
} FIXTURE;

static FIXTURE g_fixture;

/* Padrao customizado para teste de injetor. */
static uint32_t CustomPattern(uint64_t PageIndex, uint32_t ByteOffset, void *Context)
{
    uint32_t seed = (uint32_t)(uintptr_t)Context;
    (void)PageIndex;
    return (uint32_t)((seed + ByteOffset) & 0xFFu);
}

/* Caso 1: happy path com 4 paginas, padrao default. */
static int Case_HappyFourPages(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    uint32_t i;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x80000000000ull;
    req.PageCount = 4u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x45FF10000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    req.FillPattern = NULL;
    req.FillContext = NULL;
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_OK);
    CHECK(ev.PageCount == 4u);
    CHECK(ev.VmId == BC250_GSVT_VMID);
    CHECK(ev.PageTablePhysical == 0x45FF10000ull);
    CHECK(ev.VaStart == 0x80000000000ull);
    CHECK(ev.TranslationFaults == 0u);
    CHECK(ev.PatternMismatch == 0u);
    CHECK(ev.OfflineContractReady == 1u);
    CHECK(ev.TotalRingDwords ==
        (BC250_SDMA_DWORDS_NOP(0u) + BC250_SDMA_DWORDS_COPY_LINEAR +
         BC250_SDMA_DWORDS_FENCE));
    /* Traducao esperada: o stub codifica cada PTE apontando para
     * PhysicalStart + i*4K. Como PhysicalStart == PageTablePhysical,
     * a primeira PA decodificada low-32 e 0x45FF10000. */
    if (ev.FirstTranslatedPaLow != 0x45FF10000u) {
        ExitProcess((unsigned long)(0xFFFF0000u | ev.FirstTranslatedPaLow));
    }
    CHECK(ev.FirstTranslatedPaLow == 0x45FF10000u);
    CHECK(ev.LastTranslatedPaLow == 0x45FF10000u +
        3u * (uint32_t)BC250_PAGE_SIZE_4K);
    /* Destino foi zerado pelo rollback obrigatorio. */
    for (i = 0u; i < req.PageCount * (uint32_t)BC250_PAGE_SIZE_4K; ++i) {
        CHECK(f->Destination[i] == 0u);
    }
    return 1;
}

/* Caso 2: 1 pagina apenas (caminho minimo). */
static int Case_SinglePage(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x90000000000ull;
    req.PageCount = 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0xDEADBEEF000ull; /* 4 KiB aligned */
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_OK);
    CHECK(ev.PageCount == 1u);
    CHECK(ev.FirstTranslatedPaLow == 0xDEADBEEF000u);
    CHECK(ev.LastTranslatedPaLow == 0xDEADBEEF000u);
    return 1;
}

/* Caso 3: maximo de paginas permitido - com PteBuffer insuficiente. */
static int Case_MaxPages(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000000ull;
    req.PageCount = BC250_GSVT_MAX_PAGE_COUNT; /* 1024 */
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x100000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u; /* FALHA: pouco espaco para 1024 */
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    /* Validate checa PteBufferDwords antes de Destination: BAD_ARGUMENT. */
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_BAD_ARGUMENT);
    return 1;
}

/* Caso 4: VA desalinhado -> bad_argument. */
static int Case_VaMisaligned(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000001ull; /* nao 4 KiB aligned */
    req.PageCount = 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x1000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_BAD_ARGUMENT);
    return 1;
}

/* Caso 5: PageTablePhysical desalinhado -> bad_argument. */
static int Case_PtPhysMisaligned(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000000ull;
    req.PageCount = 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x1001ull; /* nao 4 KiB aligned */
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_BAD_ARGUMENT);
    return 1;
}

/* Caso 6: zero paginas -> plan_invalid. */
static int Case_ZeroPages(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000000ull;
    req.PageCount = 0u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x1000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_PLAN_INVALID);
    return 1;
}

/* Caso 7: ring pequeno -> ring_overflow. */
static int Case_RingTooSmall(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000000ull;
    req.PageCount = 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x1000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = 8u; /* propositalmente insuficiente */
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_RING_OVERFLOW);
    return 1;
}

/* Caso 8: destino NULL -> bad_argument. */
static int Case_NullDestination(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x100000000ull;
    req.PageCount = 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x1000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = NULL;
    req.Destination.Capacity = 0u;
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_BAD_ARGUMENT);
    return 1;
}

/* Caso 9: padrao customizado via FillPattern. */
static int Case_CustomPattern(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    uint32_t seed = 0xCAFEu;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x200000000ull;
    req.PageCount = 2u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x200000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    req.FillPattern = CustomPattern;
    req.FillContext = (void *)(uintptr_t)seed;
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_OK);
    CHECK(ev.OfflineContractReady == 1u);
    return 1;
}

/* Caso 10: mais paginas do que o maximo -> plan_invalid. */
static int Case_OverMaxPages(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x300000000ull;
    req.PageCount = BC250_GSVT_MAX_PAGE_COUNT + 1u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x300000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_PLAN_INVALID);
    return 1;
}

/* Caso 11: BufferBytes menor que o necessario -> bad_argument
 * (PteBufferDwords insuficiente). */
static int Case_PteBufferTooSmall(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x400000000ull;
    req.PageCount = 4u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x400000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 2u; /* precisa 4 */
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = (uint32_t)sizeof(f->Destination);
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) == BC250_GSVT_BAD_ARGUMENT);
    return 1;
}

/* Caso 12: destino com capacidade insuficiente -> destination_too_small. */
static int Case_DestinationTooSmall(void)
{
    FIXTURE *f = &g_fixture;
    BC250_GSVT_REQUEST req;
    BC250_GSVT_EVIDENCE ev;
    memset(f, 0, sizeof(*f));
    memset(&req, 0, sizeof(req));
    req.VaStart = 0x500000000ull;
    req.PageCount = 4u;
    req.PteFlags = BC250_PTE_RW_DEFAULT;
    req.PageTablePhysical = 0x500000ull;
    req.PteBuffer = f->Ptes;
    req.PteBufferDwords = 16u;
    req.RingBuffer = f->Ring;
    req.RingDwords = BC250_GSVT_RING_DWORDS;
    req.Destination.Bytes = f->Destination;
    req.Destination.Capacity = 4096u; /* 1 pagina so */
    CHECK(Bc250RunGcvmSdmaVmidContract(&req, &ev) ==
        BC250_GSVT_DESTINATION_TOO_SMALL);
    return 1;
}

static int Run(void)
{
    CHECK(Case_HappyFourPages());
    CHECK(Case_SinglePage());
    CHECK(Case_MaxPages());
    CHECK(Case_VaMisaligned());
    CHECK(Case_PtPhysMisaligned());
    CHECK(Case_ZeroPages());
    CHECK(Case_RingTooSmall());
    CHECK(Case_NullDestination());
    CHECK(Case_CustomPattern());
    CHECK(Case_OverMaxPages());
    CHECK(Case_PteBufferTooSmall());
    CHECK(Case_DestinationTooSmall());
    return 1;
}

void mainCRTStartup(void)
{
    ExitProcess(Run() ? 0u : g_failureLine);
}
