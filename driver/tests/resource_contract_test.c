#include "../core/bc250_resource_contract.h"

#include <stdio.h>
#include <string.h>

static int g_Failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FALHA linha %d: %s\n", __LINE__, #expr); \
        ++g_Failures; \
    } \
} while (0)

static const BC250_RESOLVED_BAR_RESOURCE *
FindBar(const BC250_RESOLVED_BAR_RESOURCE *r, size_t count, uint32_t index)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (r[i].Index == index) return &r[i];
    }
    return NULL;
}

static void TestCapturedLayout(void)
{
    const BC250_MEMORY_RESOURCE memory[] = {
        { UINT64_C(0xC0000000), UINT64_C(0x10200000) },
        { UINT64_C(0xD0000000), UINT64_C(0x00200000) },
        { UINT64_C(0xFE800000), UINT64_C(0x00080000) }
    };
    const BC250_PCI_BAR_RESOURCE bars[] = {
        { 0, 1, UINT64_C(0xC0000000) },
        { 2, 1, UINT64_C(0xD0000000) },
        { 5, 1, UINT64_C(0xFE800000) }
    };
    BC250_RESOLVED_BAR_RESOURCE out[3];
    const BC250_RESOLVED_BAR_RESOURCE *bar;
    size_t count = 0;
    BC250_RESOURCE_STATUS status;

    status = Bc250ResolveTranslatedBars(memory, 3, bars, 3, out, 3, &count);
    CHECK(status == BC250_RESOURCE_OK);
    CHECK(count == 3);

    bar = FindBar(out, count, 0);
    CHECK(bar != NULL && bar->Length == UINT64_C(0x10000000));
    CHECK(bar != NULL && bar->CappedByNextBar == 1);
    CHECK(bar != NULL && bar->DescriptorDepth == 1);

    bar = FindBar(out, count, 2);
    CHECK(bar != NULL && bar->Length == UINT64_C(0x00200000));
    CHECK(bar != NULL && bar->DescriptorDepth == 2);

    bar = FindBar(out, count, 5);
    CHECK(bar != NULL && bar->Length == UINT64_C(0x00080000));
}

static void TestOrderIndependent(void)
{
    const BC250_MEMORY_RESOURCE memory[] = {
        { UINT64_C(0xFE800000), UINT64_C(0x00080000) },
        { UINT64_C(0xD0000000), UINT64_C(0x00200000) },
        { UINT64_C(0xC0000000), UINT64_C(0x10200000) }
    };
    const BC250_PCI_BAR_RESOURCE bars[] = {
        { 5, 1, UINT64_C(0xFE800000) },
        { 0, 1, UINT64_C(0xC0000000) },
        { 2, 1, UINT64_C(0xD0000000) }
    };
    BC250_RESOLVED_BAR_RESOURCE out[3];
    size_t count = 0;

    CHECK(Bc250ResolveTranslatedBars(memory, 3, bars, 3, out, 3, &count) == BC250_RESOURCE_OK);
    CHECK(count == 3);
    CHECK(FindBar(out, count, 0)->Length == UINT64_C(0x10000000));
    CHECK(FindBar(out, count, 2)->Length == UINT64_C(0x00200000));
    CHECK(FindBar(out, count, 5)->Length == UINT64_C(0x00080000));
}

static void TestFailClosed(void)
{
    const BC250_MEMORY_RESOURCE valid[] = {
        { UINT64_C(0x10000000), UINT64_C(0x1000) }
    };
    const BC250_MEMORY_RESOURCE overflow[] = {
        { UINT64_MAX - 3u, UINT64_C(8) }
    };
    const BC250_MEMORY_RESOURCE ambiguous[] = {
        { UINT64_C(0x10000000), UINT64_C(0x1000) },
        { UINT64_C(0x10000000), UINT64_C(0x2000) }
    };
    const BC250_PCI_BAR_RESOURCE bar[] = {
        { 0, 1, UINT64_C(0x10000000) }
    };
    const BC250_PCI_BAR_RESOURCE duplicate[] = {
        { 0, 1, UINT64_C(0x10000000) },
        { 2, 1, UINT64_C(0x10000000) }
    };
    const BC250_PCI_BAR_RESOURCE absent[] = {
        { 0, 1, UINT64_C(0x20000000) }
    };
    BC250_RESOLVED_BAR_RESOURCE out[2];
    size_t count = 99;

    CHECK(Bc250ResolveTranslatedBars(overflow, 1, bar, 1, out, 2, &count) == BC250_RESOURCE_RANGE_OVERFLOW);
    CHECK(count == 0);
    CHECK(Bc250ResolveTranslatedBars(valid, 1, duplicate, 2, out, 2, &count) == BC250_RESOURCE_DUPLICATE_BAR);
    CHECK(Bc250ResolveTranslatedBars(ambiguous, 2, bar, 1, out, 2, &count) == BC250_RESOURCE_AMBIGUOUS_RANGE);
    CHECK(Bc250ResolveTranslatedBars(valid, 1, absent, 1, out, 2, &count) == BC250_RESOURCE_BAR_UNASSIGNED);
    CHECK(Bc250ResolveTranslatedBars(valid, 1, bar, 1, out, 0, &count) == BC250_RESOURCE_OUTPUT_TOO_SMALL);
    CHECK(Bc250ResolveTranslatedBars(NULL, 0, bar, 1, out, 2, &count) == BC250_RESOURCE_BAD_ARGUMENT);
}

int main(void)
{
    TestCapturedLayout();
    TestOrderIndependent();
    TestFailClosed();
    if (g_Failures != 0) {
        printf("RESOURCE CONTRACT: %d FALHAS\n", g_Failures);
        return 1;
    }
    printf("RESOURCE CONTRACT: OK (layout capturado + ordem + fail-closed)\n");
    printf("Veredito: NAO VALIDADO EM HARDWARE.\n");
    return 0;
}
