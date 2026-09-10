#include "../core/bc250_umd_private_contract.h"

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

#define CHECK(Expression) do { if (!(Expression)) { ExitProcess((unsigned long)__LINE__); return 1; } } while (0)
#else
#include <stdio.h>
#define CHECK(Expression) do { if (!(Expression)) return 1; } while (0)
#endif

/*
 * Trava do handshake DXGKQAITYPE_UMDRIVERPRIVATE.
 *
 * A corrida 2026-09-07 08:25 mediu 80 respostas STATUS_NOT_SUPPORTED para Type=0 e ZERO
 * chamadas em todas as entradas do adapter table da UMD. Este teste existe para o blob
 * nunca voltar a ser meia-boca: cabecalho auto-descrito, falha fechada em buffer curto e
 * recusa de blob incoerente.
 */

static void FillGoodFacts(BC250_UMD_PRIVATE_FACTS *facts)
{
    unsigned char *bytes = (unsigned char *)facts;
    unsigned i;
    for (i = 0u; i < (unsigned)sizeof(*facts); ++i) bytes[i] = 0u;

    facts->VendorId = 0x1002u;
    facts->DeviceId = 0x13FEu;
    facts->RevisionId = 0u;
    facts->SegmentCount = 2u;
    facts->VramSegmentId = 1u;
    facts->ApertureSegmentId = 2u;
    facts->VramSizeBytes = 256ull * 1024ull * 1024ull;
    facts->ApertureSizeBytes = 512ull * 1024ull * 1024ull;
    facts->GpuVaBits = 34u;   /* medido: [McdmQAI] GPUMMUCAPS: VaBits=34 */
    facts->PageSizeBytes = 4096u;
    facts->PageTableLevels = 2u;
    facts->NodeCount = 2u;
    facts->SdmaNodeOrdinal = 0u;
    facts->ComputeNodeOrdinal = 1u;
    facts->GateFlags = BC250_UMD_GATE_NO_3D_PIPELINE |
                       BC250_UMD_GATE_NO_NODE1_SUBMIT |
                       BC250_UMD_GATE_NO_IB_UNDER_VMID8 |
                       BC250_UMD_GATE_NO_SHADER_COMPILER |
                       BC250_UMD_GATE_CAPS_PROBE_ONLY;
}

static int TestRoundTrip(void)
{
    BC250_UMD_PRIVATE_FACTS facts;
    BC250_UMD_PRIVATE blob;
    uint32_t written = 0u;

    FillGoodFacts(&facts);
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_OK);
    CHECK(written == Bc250UmdPrivateRequiredBytes());
    CHECK(written == (uint32_t)sizeof(BC250_UMD_PRIVATE));

    /* O cabecalho auto-descrito e' o que permite falhar fechado do outro lado. */
    CHECK(blob.Magic == BC250_UMD_PRIVATE_MAGIC);
    CHECK(blob.Version == BC250_UMD_PRIVATE_VERSION);
    CHECK(blob.SizeBytes == (uint32_t)sizeof(BC250_UMD_PRIVATE));

    /* Os fatos atravessaram sem perda. */
    CHECK(blob.VendorId == 0x1002u && blob.DeviceId == 0x13FEu);
    CHECK(blob.VramSegmentId == 1u && blob.ApertureSegmentId == 2u);
    /* 34, nao 48: ver TestGpuVaGeometryIsPlausible e BC250_GPUVA_PT_VA_BITS. */
    CHECK(blob.GpuVaBits == 34u && blob.PageSizeBytes == 4096u);
    CHECK(blob.NodeCount == 2u && blob.ComputeNodeOrdinal == 1u);

    /* E o lado UMD aceita o que o lado KMD produziu. */
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) == BC250_UMD_PRIVATE_OK);
    return 0;
}

static int TestGatesAreDeclared(void)
{
    BC250_UMD_PRIVATE_FACTS facts;
    BC250_UMD_PRIVATE blob;
    uint32_t written = 0u;

    FillGoodFacts(&facts);
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_OK);

    /*
     * Enquanto nao existir pipeline 3D, compilador de shader nem submissao Node 1, o blob
     * TEM de dizer isso. A UMD nao pode inferir capacidade a partir de silencio -- foi
     * assim que o probe v1 acabou declarando suporte a um DDI inexistente.
     */
    CHECK((blob.GateFlags & BC250_UMD_GATE_NO_3D_PIPELINE) != 0u);
    CHECK((blob.GateFlags & BC250_UMD_GATE_NO_SHADER_COMPILER) != 0u);
    CHECK((blob.GateFlags & BC250_UMD_GATE_NO_NODE1_SUBMIT) != 0u);
    CHECK((blob.GateFlags & BC250_UMD_GATE_CAPS_PROBE_ONLY) != 0u);
    return 0;
}

static int TestShortBufferFailsClosed(void)
{
    BC250_UMD_PRIVATE_FACTS facts;
    unsigned char small[8];
    uint32_t written = 0xDEADBEEFu;
    unsigned i;

    for (i = 0u; i < (unsigned)sizeof(small); ++i) small[i] = 0xA5u;
    FillGoodFacts(&facts);

    CHECK(Bc250UmdPrivateWrite(&facts, small, (uint32_t)sizeof(small), &written) ==
        BC250_UMD_PRIVATE_BUFFER_TOO_SMALL);
    /* Nenhum byte escrito, e o contador zerado: nada de blob parcial. */
    CHECK(written == 0u);
    for (i = 0u; i < (unsigned)sizeof(small); ++i) {
        CHECK(small[i] == 0xA5u);
    }
    return 0;
}

static int TestValidateRejectsGarbage(void)
{
    BC250_UMD_PRIVATE blob;
    BC250_UMD_PRIVATE_FACTS facts;
    uint32_t written = 0u;

    FillGoodFacts(&facts);
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_OK);

    /* Magic errado: KMD de outro projeto, ou memoria nao inicializada. */
    blob.Magic = BC250_UMD_PRIVATE_MAGIC + 1ull;
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_BAD_MAGIC);
    blob.Magic = BC250_UMD_PRIVATE_MAGIC;

    /* Versao desconhecida: UMD velha contra KMD novo tem de falhar fechado. */
    blob.Version = BC250_UMD_PRIVATE_VERSION + 1u;
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_BAD_VERSION);
    blob.Version = BC250_UMD_PRIVATE_VERSION;

    /* SizeBytes mentindo sobre o proprio tamanho. */
    blob.SizeBytes = (uint32_t)sizeof(BC250_UMD_PRIVATE) - 4u;
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_BAD_SIZE);
    blob.SizeBytes = (uint32_t)sizeof(BC250_UMD_PRIVATE);

    /* Adaptador impossivel: ordinal fora do NodeCount. */
    blob.ComputeNodeOrdinal = blob.NodeCount;
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_INCOHERENT);

    /* Buffer curto na validacao tambem falha fechado. */
    CHECK(Bc250UmdPrivateValidate(&blob, 8u) == BC250_UMD_PRIVATE_BUFFER_TOO_SMALL);
    CHECK(Bc250UmdPrivateValidate(NULL, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_BAD_ARGUMENT);
    return 0;
}

/*
 * REGRESSÃO (07/09) -- a geometria de GPUVA tem de vir da MESMA fonte que o
 * GPUMMUCAPS, e 48 bits nao e' essa fonte.
 *
 * A primeira versao deste blob publicava BC250_ADAPTER_GPUVA_BITS (= 48) e
 * PageTableLevels fixo em 2. Mas o log da corrida 2026-09-07 11:08 mostra o que
 * o mesmo driver declara ao WDDM:
 *
 *     [McdmQAI] GPUMMUCAPS: VaBits=34, Levels=2
 *
 * 34 bits, coerente com a geometria de 2 niveis derivada em docs/03 (raiz de 13
 * bits + folha de 9 + 12 de offset) e com PAGE_TABLE_END_ADDR = 2^22 - 1.
 * Publicar 48 faria a UMD -- e, na rota B, o winsys do RADV -- dispor enderecos
 * fora do que o contexto VMID8 traduz. Era uma segunda fonte de verdade, o
 * mesmo defeito do A1, criada por mim.
 *
 * Este teste nao pode verificar de onde o KMD tira o numero (isso e' um gate
 * estatico na auditoria), mas trava o que o contrato aceita e recusa.
 */
static int TestGpuVaGeometryIsPlausible(void)
{
    BC250_UMD_PRIVATE_FACTS facts;
    BC250_UMD_PRIVATE blob;
    uint32_t written = 0u;

    /* A geometria medida no silicio atravessa o blob sem alteracao. */
    FillGoodFacts(&facts);
    facts.GpuVaBits = 34u;
    facts.PageTableLevels = 2u;
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_OK);
    CHECK(blob.GpuVaBits == 34u);
    CHECK(blob.PageTableLevels == 2u);
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) == BC250_UMD_PRIVATE_OK);

    /*
     * Geometria ausente e' recusada no produtor: melhor a UMD nao receber nada
     * do que receber numero inventado.
     */
    FillGoodFacts(&facts);
    facts.GpuVaBits = 0u;
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_INCOHERENT);
    CHECK(written == 0u);

    /* E o validador tambem recusa do outro lado. */
    FillGoodFacts(&facts);
    facts.GpuVaBits = 34u;
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_OK);
    blob.GpuVaBits = 0u;
    CHECK(Bc250UmdPrivateValidate(&blob, (uint32_t)sizeof(blob)) ==
        BC250_UMD_PRIVATE_INCOHERENT);
    return 0;
}

static int TestWriteRejectsIncoherentFacts(void)
{
    BC250_UMD_PRIVATE_FACTS facts;
    BC250_UMD_PRIVATE blob;
    uint32_t written = 0u;

    /* O produtor tambem nao pode publicar fato impossivel. */
    FillGoodFacts(&facts);
    facts.NodeCount = 0u;
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_INCOHERENT);
    CHECK(written == 0u);

    FillGoodFacts(&facts);
    facts.PageSizeBytes = 0u;
    CHECK(Bc250UmdPrivateWrite(&facts, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_INCOHERENT);

    FillGoodFacts(&facts);
    CHECK(Bc250UmdPrivateWrite(NULL, &blob, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_BAD_ARGUMENT);
    CHECK(Bc250UmdPrivateWrite(&facts, NULL, (uint32_t)sizeof(blob), &written) ==
        BC250_UMD_PRIVATE_BAD_ARGUMENT);
    return 0;
}

static int Bc250RunUmdPrivateContractTest(void)
{
    if (TestRoundTrip() != 0) return 1;
    if (TestGatesAreDeclared() != 0) return 1;
    if (TestShortBufferFailsClosed() != 0) return 1;
    if (TestValidateRejectsGarbage() != 0) return 1;
    if (TestGpuVaGeometryIsPlausible() != 0) return 1;
    if (TestWriteRejectsIncoherentFacts() != 0) return 1;
    return 0;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void)
{
    ExitProcess((unsigned long)Bc250RunUmdPrivateContractTest());
}
#else
int main(void)
{
    int failed = Bc250RunUmdPrivateContractTest();
    if (failed == 0) {
        printf("umd_private_contract_test: PASS\n");
    }
    return failed;
}
#endif
