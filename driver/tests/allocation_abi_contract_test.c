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
    volatile unsigned char *d = (volatile unsigned char *)Destination;
    const volatile unsigned char *s = (const volatile unsigned char *)Source;
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
     * CONTRATO DE TAMANHO E ALINHAMENTO FIXO DA ABI.
     *
     * V2 (F2, 13/09): a estrutura cresceu de 40 para 56 bytes ao receber
     * BC250_ALLOCATION_GEOMETRY. Os sete offsets originais continuam TRAVADOS --
     * a geometria foi acrescentada no FIM de proposito, para que qualquer
     * reordenacao acidental reprove aqui e nao no silicio.
     *
     * A UMD e o KMD sao empacotados juntos e o parse exige Version E StructSize
     * exatos, entao nao existe janela de blob V1 chegando a um KMD V2: a
     * divergencia falha fechado em BAD_VERSION.
     */
    CHECK(sizeof(BC250_ALLOCATION_PRIVATE_V1) == 56u);
    CHECK(BC250_ALLOCATION_PRIVATE_VERSION == 2u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Magic) == 0u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Version) == 4u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, StructSize) == 8u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Flags) == 12u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, AllocationToken) == 16u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, ByteSize) == 24u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Alignment) == 32u);
    CHECK(MEMBER_OFFSET(BC250_ALLOCATION_PRIVATE_V1, Geometry) == 40u);
    CHECK(sizeof(BC250_ALLOCATION_GEOMETRY) == 16u);
    return 1;
}

static int TestAbiFormatSuccessAndNegative(void)
{
    BC250_ALLOCATION_PRIVATE_V1 formatted;
    BC250_ALLOCATION_PRIVATE_V1 parsed;

    /* Caso bem-sucedido canônico */
    memset(&formatted, 0xFF, sizeof(formatted));
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 0u, NULL, &formatted) ==
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
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 0u, NULL, NULL) ==
        BC250_ALLOCATION_ABI_BAD_ARGUMENT);
    CHECK(Bc250AllocationAbiFormat(0ull, 65536ull, 4096ull, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 0ull, 4096ull, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_SIZE);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 2u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull, 0x80000000u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4096ull,
        BC250_ALLOCATION_FLAG_CPU_VISIBLE, NULL, &formatted) == BC250_ALLOCATION_ABI_OK);
    CHECK(formatted.Flags == BC250_ALLOCATION_FLAG_CPU_VISIBLE);
    CHECK(Bc250AllocationRequiresCpuAccess(&formatted));
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 2048ull, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 4u * 1024u * 1024u, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65536ull, 3000ull, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);
    CHECK(Bc250AllocationAbiFormat(0x1000ull, 65537ull, 4096ull, 0u, NULL, &formatted) ==
        BC250_ALLOCATION_ABI_BAD_ALIGNMENT);

    /*
     * GEOMETRIA (F2.1). Ela existe para que DxgkDdiDescribeAllocation possa
     * responder com a geometria REAL da superficie em vez de NOT_SUPPORTED.
     * Geometria incoerente tem de falhar fechado: aceita-la produziria uma
     * descricao plausivel e errada, cujo sintoma seria imagem torta no monitor
     * -- a classe do stride skew de 10/09.
     */
    {
        BC250_ALLOCATION_GEOMETRY geo;
        BC250_ALLOCATION_PRIVATE_V1 geoBlob;

        /* Caso canonico: 2560x1080 pitch 10240, o backbuffer do dwm.exe. */
        geo.Width = 2560u; geo.Height = 1080u; geo.Pitch = 10240u; geo.Format = 21u;
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 11075584ull, 65536ull, 0u,
            &geo, &geoBlob) == BC250_ALLOCATION_ABI_OK);
        CHECK((geoBlob.Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) != 0u);
        CHECK(Bc250AllocationHasGeometry(&geoBlob));
        CHECK(geoBlob.Geometry.Pitch == 10240u);
        /* Sobrevive ao round-trip pelo parse, que e' o caminho do KMD. */
        CHECK(Bc250AllocationAbiParse(&geoBlob, (uint32_t)sizeof(geoBlob), &parsed) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationHasGeometry(&parsed));
        CHECK(parsed.Geometry.Width == 2560u && parsed.Geometry.Height == 1080u);
        CHECK(parsed.Geometry.Format == 21u);

        /* Sem geometria: caso legitimo de command buffer/scratch. */
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 4096ull, 4096ull, 0u,
            NULL, &geoBlob) == BC250_ALLOCATION_ABI_OK);
        CHECK((geoBlob.Flags & BC250_ALLOCATION_FLAG_HAS_GEOMETRY) == 0u);
        CHECK(!Bc250AllocationHasGeometry(&geoBlob));

        /* Nao cabe no ByteSize -- o invariante que amarra geometria a alocacao. */
        geo.Width = 2560u; geo.Height = 1080u; geo.Pitch = 10240u; geo.Format = 21u;
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 65536ull, 65536ull, 0u,
            &geo, &geoBlob) == BC250_ALLOCATION_ABI_BAD_GEOMETRY);

        /* Campo zerado em qualquer posicao recusa. */
        geo.Width = 0u; geo.Height = 1080u; geo.Pitch = 10240u; geo.Format = 21u;
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 11075584ull, 65536ull, 0u,
            &geo, &geoBlob) == BC250_ALLOCATION_ABI_BAD_GEOMETRY);
        geo.Width = 2560u; geo.Format = 0u;
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 11075584ull, 65536ull, 0u,
            &geo, &geoBlob) == BC250_ALLOCATION_ABI_BAD_GEOMETRY);

        /* Pitch menor que a largura e' incoerente mesmo cabendo no ByteSize. */
        geo.Width = 2560u; geo.Height = 2u; geo.Pitch = 100u; geo.Format = 21u;
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 11075584ull, 65536ull, 0u,
            &geo, &geoBlob) == BC250_ALLOCATION_ABI_BAD_GEOMETRY);

        /*
         * A flag NAO pode ser declarada pelo chamador: ela e' DERIVADA do
         * ponteiro. Aceita-la permitiria um blob com HAS_GEOMETRY e conteudo
         * zerado -- descricao vazia apresentada como valida.
         */
        CHECK(Bc250AllocationAbiFormat(0x2000ull, 11075584ull, 65536ull,
            BC250_ALLOCATION_FLAG_HAS_GEOMETRY, NULL, &geoBlob) ==
            BC250_ALLOCATION_ABI_BAD_FLAGS);
    }
    return 1;
}

static int TestAbiParseAllErrorCodes(void)
{
    BC250_ALLOCATION_PRIVATE_V1 valid;
    BC250_ALLOCATION_PRIVATE_V1 test;
    BC250_ALLOCATION_PRIVATE_V1 output;

    CHECK(Bc250AllocationAbiFormat(0xCAFEull, 131072ull, 65536ull, 0u, NULL, &valid) ==
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
    /* F2: a versao 2 passou a ser a VALIDA; o caso de versao futura virou 3. */
    test.Version = 3u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_VERSION);
    test.Version = 1u; /* versao antiga, agora recusada */
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
    /*
     * F2: o bit 0x2 deixou de ser desconhecido -- virou
     * BC250_ALLOCATION_FLAG_HAS_GEOMETRY. Declara-lo com geometria zerada nao e'
     * mais BAD_FLAGS e sim BAD_GEOMETRY, que e' a recusa CERTA: o blob afirma
     * carregar geometria e nao carrega. Este caso migrou de categoria, nao
     * desapareceu -- se ele voltasse a ser aceito, uma descricao vazia passaria
     * a ser apresentada ao dxgkrnl como valida.
     */
    test.Flags = BC250_ALLOCATION_FLAG_HAS_GEOMETRY;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_GEOMETRY);
    test.Flags = 0x80000000u;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);

    /* Valid flag CPU_VISIBLE */
    test = valid;
    test.Flags = BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    CHECK(Bc250AllocationAbiParse(&test, sizeof(test), &output) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(output.Flags == BC250_ALLOCATION_FLAG_CPU_VISIBLE);
    CHECK(Bc250AllocationRequiresCpuAccess(&output));

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

    /* V1 (40 bytes legados do Mesa RADV) */
    {
        struct {
            uint32_t Magic;
            uint32_t Version;
            uint32_t StructSize;
            uint32_t Flags;
            uint64_t AllocationToken;
            uint64_t ByteSize;
            uint64_t Alignment;
        } v1;
        v1.Magic = BC250_ALLOCATION_PRIVATE_MAGIC;
        v1.Version = 1u;
        v1.StructSize = 40u;
        v1.Flags = 0u;
        v1.AllocationToken = 0xCAFEull;
        v1.ByteSize = 65536ull;
        v1.Alignment = 4096ull;

        CHECK(Bc250AllocationAbiParse(&v1, (uint32_t)sizeof(v1), &output) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(output.AllocationToken == 0xCAFEull);
        CHECK(output.ByteSize == 65536ull);
        CHECK(output.Alignment == 4096ull);
        CHECK(!Bc250AllocationHasGeometry(&output));

        /* Caso negativo V1: versao errada */
        v1.Version = 2u;
        CHECK(Bc250AllocationAbiParse(&v1, (uint32_t)sizeof(v1), &output) ==
            BC250_ALLOCATION_ABI_BAD_VERSION);
        v1.Version = 1u;

        /* Caso negativo V1: StructSize errado */
        v1.StructSize = 48u;
        CHECK(Bc250AllocationAbiParse(&v1, (uint32_t)sizeof(v1), &output) ==
            BC250_ALLOCATION_ABI_BAD_VERSION);
    }

    return 1;
}

static int TestAbiTokenClassification(void)
{
    BC250_ALLOCATION_PRIVATE_V1 normalAlloc;
    BC250_ALLOCATION_PRIVATE_V1 primaryAlloc;
    uint64_t primaryToken;

    CHECK(Bc250AllocationAbiFormat(0x5555ull, 4096ull, 4096ull, 0u, NULL, &normalAlloc) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(!Bc250AllocationIsPrimary(&normalAlloc));

    primaryToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY <<
        BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 42ull;
    CHECK(Bc250AllocationAbiFormat(primaryToken, 8192ull, 4096ull, 0u, NULL, &primaryAlloc) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(Bc250AllocationIsPrimary(&primaryAlloc));

    CHECK(!Bc250AllocationIsPrimary(NULL));
    CHECK(!Bc250AllocationRequiresCpuAccess(&normalAlloc));
    CHECK(Bc250AllocationRequiresCpuAccess(&primaryAlloc));
    CHECK(!Bc250AllocationRequiresCpuAccess(NULL));
    {
        BC250_ALLOCATION_PRIVATE_V1 stagingAlloc;
        uint64_t stagingToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_STAGING <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 42ull;
        CHECK(Bc250AllocationAbiFormat(stagingToken, 4096ull, 4096ull, 0u, NULL, &stagingAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&stagingAlloc));
        CHECK(!Bc250AllocationIsPrimary(&stagingAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 shadowAlloc;
        uint64_t shadowToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_SHADOW <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 43ull;
        CHECK(Bc250AllocationAbiFormat(shadowToken, 4096ull, 4096ull, 0u, NULL, &shadowAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&shadowAlloc));
        CHECK(!Bc250AllocationIsPrimary(&shadowAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 gdiAlloc;
        uint64_t gdiToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_GDI <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 44ull;
        CHECK(Bc250AllocationAbiFormat(gdiToken, 4096ull, 4096ull, 0u, NULL, &gdiAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&gdiAlloc));
        CHECK(!Bc250AllocationIsPrimary(&gdiAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 cpuVisAlloc;
        uint64_t cpuVisToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_CPU_VISIBLE <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 45ull;
        CHECK(Bc250AllocationAbiFormat(cpuVisToken, 4096ull, 4096ull, 0u, NULL, &cpuVisAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&cpuVisAlloc));
        CHECK(!Bc250AllocationIsPrimary(&cpuVisAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 cmdAlloc;
        uint64_t cmdToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_COMMAND <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 46ull;
        CHECK(Bc250AllocationAbiFormat(cmdToken, 4096ull, 4096ull, 0u, NULL, &cmdAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&cmdAlloc));
        CHECK(!Bc250AllocationIsPrimary(&cmdAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 proofAlloc;
        uint64_t proofToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PROOF <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 47ull;
        CHECK(Bc250AllocationAbiFormat(proofToken, 4096ull, 4096ull, 0u, NULL, &proofAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&proofAlloc));
        CHECK(!Bc250AllocationIsPrimary(&proofAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 flagAlloc;
        CHECK(Bc250AllocationAbiFormat(0x7777ull, 4096ull, 4096ull,
            BC250_ALLOCATION_FLAG_CPU_VISIBLE, NULL, &flagAlloc) == BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&flagAlloc));
        CHECK(!Bc250AllocationIsPrimary(&flagAlloc));
    }
    {
        BC250_ALLOCATION_PRIVATE_V1 resourceAlloc;
        uint64_t resToken = ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_RESOURCE <<
            BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 48ull;
        /* DX.89: RESOURCE sem flag (recurso compartilhado) NAO exige CPU... */
        CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull, 0u, NULL, &resourceAlloc) ==
            BC250_ALLOCATION_ABI_OK);
        CHECK(!Bc250AllocationRequiresCpuAccess(&resourceAlloc));
        CHECK(!Bc250AllocationIsPrimary(&resourceAlloc));
        /* ...e RESOURCE com a flag (recurso comum) continua exigindo. */
        CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull,
            BC250_ALLOCATION_FLAG_CPU_VISIBLE, NULL, &resourceAlloc) == BC250_ALLOCATION_ABI_OK);
        CHECK(Bc250AllocationRequiresCpuAccess(&resourceAlloc));
    }
    return 1;
}

/*
 * M1 (doc 491): GPU_VA_ONLY vale so' para RESOURCE, nos dois lados do
 * contrato, e e' o unico caminho para o segmento de sistema.
 */
static int TestAbiGpuVaOnlySystemSegment(void)
{
    const uint64_t resToken =
        ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_RESOURCE << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 7u;
    const uint64_t primToken =
        ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_PRIMARY << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 7u;
    const uint64_t cmdToken =
        ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_COMMAND << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 7u;
    BC250_ALLOCATION_PRIVATE_V1 blob;
    BC250_ALLOCATION_PRIVATE_V1 parsed;
    const uint32_t both = BC250_ALLOCATION_FLAG_CPU_VISIBLE | BC250_ALLOCATION_FLAG_GPU_VA_ONLY;

    /* RESOURCE com a flag: aceito, e o unico que pode usar o segmento de sistema. */
    CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull, both, NULL, &blob) ==
        BC250_ALLOCATION_ABI_OK);
    CHECK(Bc250AllocationAbiParse(&blob, sizeof(blob), &parsed) == BC250_ALLOCATION_ABI_OK);
    CHECK(Bc250AllocationMayUseSystemSegment(&parsed));
    CHECK(Bc250AllocationRequiresCpuAccess(&parsed));
    /* RESOURCE sem a flag: segmento local somente. */
    CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull,
        BC250_ALLOCATION_FLAG_CPU_VISIBLE, NULL, &blob) == BC250_ALLOCATION_ABI_OK);
    CHECK(!Bc250AllocationMayUseSystemSegment(&blob));
    /* Primaria e comando com a flag: RECUSADOS na formatacao... */
    CHECK(Bc250AllocationAbiFormat(primToken, 4096ull, 4096ull, both, NULL, &blob) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(cmdToken, 4096ull, 4096ull, both, NULL, &blob) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    /* ...e no parse, se alguem forjar o blob. */
    CHECK(Bc250AllocationAbiFormat(primToken, 4096ull, 4096ull,
        BC250_ALLOCATION_FLAG_CPU_VISIBLE, NULL, &blob) == BC250_ALLOCATION_ABI_OK);
    blob.Flags |= BC250_ALLOCATION_FLAG_GPU_VA_ONLY;
    CHECK(Bc250AllocationAbiParse(&blob, sizeof(blob), &parsed) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(!Bc250AllocationMayUseSystemSegment(&blob));
    CHECK(!Bc250AllocationMayUseSystemSegment(NULL));

    /* M2 (doc 548): segmento 3 so' para alocacao da RADV (token-ponteiro, fora
     * das classes da UMD) SEM CPU. Cada exclusao recusa sozinha. */
    memset(&blob, 0, sizeof(blob));
    blob.AllocationToken = 0x0000023400001000ull;
    blob.Flags = 0u;
    CHECK(Bc250AllocationMayUseHiddenSegment(&blob));
    blob.Flags = BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    CHECK(!Bc250AllocationMayUseHiddenSegment(&blob));
    blob.Flags = BC250_ALLOCATION_FLAG_GPU_VA_ONLY;
    CHECK(!Bc250AllocationMayUseHiddenSegment(&blob));
    blob.Flags = 0u;
    blob.AllocationToken = resToken;
    CHECK(!Bc250AllocationMayUseHiddenSegment(&blob));
    blob.AllocationToken = primToken;
    CHECK(!Bc250AllocationMayUseHiddenSegment(&blob));
    blob.AllocationToken = cmdToken;
    CHECK(!Bc250AllocationMayUseHiddenSegment(&blob));
    CHECK(!Bc250AllocationMayUseHiddenSegment(NULL));

    /*
     * M2.1 (Corrida 196): abertura no conjunto suportado so' para alocacao da
     * RADV publicada com CPU. Classe da UMD e alocacao sem CPU ficam fora.
     */
    blob.Flags = BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    blob.AllocationToken = 0x0000023400001000ull;
    CHECK(Bc250AllocationLockNeedsApertureFallback(&blob, 1));
    CHECK(!Bc250AllocationLockNeedsApertureFallback(&blob, 0));
    blob.Flags = 0u;
    CHECK(Bc250AllocationLockNeedsApertureFallback(&blob, 1));
    blob.AllocationToken = resToken;
    CHECK(!Bc250AllocationLockNeedsApertureFallback(&blob, 1));
    blob.AllocationToken = primToken;
    CHECK(!Bc250AllocationLockNeedsApertureFallback(&blob, 1));
    blob.AllocationToken = cmdToken;
    CHECK(!Bc250AllocationLockNeedsApertureFallback(&blob, 1));
    CHECK(!Bc250AllocationLockNeedsApertureFallback(NULL, 1));
    return 1;
}

/*
 * M1b.1 (doc 554): dominio e comando da RADV. As flags so' valem em token da
 * RADV; a politica e' a fonte unica de onde a alocacao pode morar.
 */
static int TestAbiRadvDomainAndCommand(void)
{
    const uint64_t radvToken = 0x0000023400001000ull;
    const uint64_t resToken =
        ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_RESOURCE << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 7u;
    const uint64_t cmdToken =
        ((uint64_t)BC250_ALLOCATION_TOKEN_CLASS_COMMAND << BC250_ALLOCATION_TOKEN_CLASS_SHIFT) | 7u;
    BC250_ALLOCATION_PRIVATE_V1 blob;
    BC250_ALLOCATION_PRIVATE_V1 parsed;
    BC250_ALLOCATION_SEGMENT_POLICY policy;

    /* Formatacao e parse aceitam as flags em token da RADV... */
    CHECK(Bc250AllocationAbiFormat(radvToken, 8192ull, 4096ull,
        BC250_ALLOCATION_FLAG_CPU_VISIBLE | BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT |
        BC250_ALLOCATION_FLAG_RADV_COMMAND, NULL, &blob) == BC250_ALLOCATION_ABI_OK);
    CHECK(Bc250AllocationAbiParse(&blob, sizeof(blob), &parsed) == BC250_ALLOCATION_ABI_OK);
    /* ...e recusam em classe da UMD, na formatacao e no blob forjado. */
    CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull,
        BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT, NULL, &blob) == BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(cmdToken, 4096ull, 4096ull,
        BC250_ALLOCATION_FLAG_RADV_COMMAND, NULL, &blob) == BC250_ALLOCATION_ABI_BAD_FLAGS);
    CHECK(Bc250AllocationAbiFormat(resToken, 4096ull, 4096ull, 0u, NULL, &blob) ==
        BC250_ALLOCATION_ABI_OK);
    blob.Flags |= BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT;
    CHECK(Bc250AllocationAbiParse(&blob, sizeof(blob), &parsed) == BC250_ALLOCATION_ABI_BAD_FLAGS);
    /* Bit seguinte continua desconhecido: o contrato nao virou peneira. */
    CHECK(Bc250AllocationAbiFormat(radvToken, 4096ull, 4096ull, 0x20u, NULL, &blob) ==
        BC250_ALLOCATION_ABI_BAD_FLAGS);

    memset(&blob, 0, sizeof(blob));
    blob.AllocationToken = radvToken;
    /* GTT: segmento 2 primeiro, com ou sem CPU. */
    blob.Flags = BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT | BC250_ALLOCATION_FLAG_CPU_VISIBLE;
    CHECK(Bc250AllocationRadvSystemSegmentPolicy(&blob, 1, &policy));
    CHECK(policy.SegmentSet == 0x3u && policy.Preferred0 == 2u && policy.Preferred1 == 1u);
    /* VRAM sem CPU: transborda, local primeiro. */
    blob.Flags = 0u;
    CHECK(Bc250AllocationRadvSystemSegmentPolicy(&blob, 0, &policy));
    CHECK(policy.SegmentSet == 0x3u && policy.Preferred0 == 1u && policy.Preferred1 == 2u);
    /* VRAM com CPU (buffer linear do Present): fica no 1. */
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(&blob, 1, &policy));
    CHECK(policy.SegmentSet == 0u);
    /* Comando: fica no 1 mesmo pedido em GTT (DX.43 le o DW0 pela VRAM). */
    blob.Flags = BC250_ALLOCATION_FLAG_RADV_DOMAIN_GTT | BC250_ALLOCATION_FLAG_RADV_COMMAND;
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(&blob, 1, &policy));
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(&blob, 0, &policy));
    /* Classe da UMD: nunca, nem sem CPU. */
    blob.Flags = 0u;
    blob.AllocationToken = resToken;
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(&blob, 0, &policy));
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(NULL, 0, &policy));
    blob.AllocationToken = radvToken;
    CHECK(!Bc250AllocationRadvSystemSegmentPolicy(&blob, 0, NULL));
    return 1;
}

static int Run(void)
{
    CHECK(TestAbiStructureFrozen());
    CHECK(TestAbiFormatSuccessAndNegative());
    CHECK(TestAbiParseAllErrorCodes());
    CHECK(TestAbiTokenClassification());
    CHECK(TestAbiGpuVaOnlySystemSegment());
    CHECK(TestAbiRadvDomainAndCommand());
    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess(Run() ? 0ul : 1ul); }
#else
int main(void) { return Run() ? 0 : 1; }
#endif
