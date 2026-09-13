#include "../core/bc250_dxbc_parser.h"

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
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #x); return 0; } } while (0)
#endif

/*
 * Construct a valid synthetic DXBC container containing:
 * - Header (32 bytes)
 * - 5 chunk offset table entries (20 bytes)
 * - Chunk 0: RDEF (8-byte header + 16 bytes dummy payload = 24 bytes)
 * - Chunk 1: ISGN (8-byte header + 16 bytes dummy payload = 24 bytes)
 * - Chunk 2: OSGN (8-byte header + 16 bytes dummy payload = 24 bytes)
 * - Chunk 3: SHDR (8-byte header + 8-byte shader header + 56 bytes opcodes = 72 bytes)
 * - Chunk 4: STAT (8-byte header + 16 bytes dummy payload = 24 bytes)
 * Total Size = 32 + 20 + 24 + 24 + 24 + 72 + 24 = 220 bytes.
 */

static int BuildValidDxbcContainer(uint8_t *Buffer, uint32_t BufferSize, uint32_t *OutTotalSize)
{
    uint32_t offset;
    uint32_t offsetRdef, offsetIsgn, offsetOsgn, offsetShdr, offsetStat;
    BC250_DXBC_RAW_HEADER *hdr;
    uint32_t *chunkOffsets;
    BC250_DXBC_RAW_CHUNK_HEADER *chunkHdr;
    uint32_t *shaderData;
    uint32_t opWords[14];

    if (BufferSize < 220u) return 0;
    memset(Buffer, 0, BufferSize);

    /* Chunk offsets layout */
    offsetRdef = 32u + 5u * 4u; /* 52 */
    offsetIsgn = offsetRdef + 24u; /* 76 */
    offsetOsgn = offsetIsgn + 24u; /* 100 */
    offsetShdr = offsetOsgn + 24u; /* 124 */
    offsetStat = offsetShdr + 72u; /* 196 */
    *OutTotalSize = offsetStat + 24u; /* 220 */

    /* DXBC header */
    hdr = (BC250_DXBC_RAW_HEADER *)Buffer;
    hdr->Magic = BC250_DXBC_MAGIC_DXBC;
    hdr->Checksum[0] = 0x12345678u;
    hdr->Checksum[1] = 0x9ABCDEF0u;
    hdr->Checksum[2] = 0x0FEDCBA9u;
    hdr->Checksum[3] = 0x87654321u;
    hdr->Version = 1u;
    hdr->TotalSize = *OutTotalSize;
    hdr->ChunkCount = 5u;

    /* Chunk offset table */
    chunkOffsets = (uint32_t *)(Buffer + 32u);
    chunkOffsets[0] = offsetRdef;
    chunkOffsets[1] = offsetIsgn;
    chunkOffsets[2] = offsetOsgn;
    chunkOffsets[3] = offsetShdr;
    chunkOffsets[4] = offsetStat;

    /* Chunk 0: RDEF */
    chunkHdr = (BC250_DXBC_RAW_CHUNK_HEADER *)(Buffer + offsetRdef);
    chunkHdr->FourCC = BC250_DXBC_FOURCC_RDEF;
    chunkHdr->ChunkSize = 16u;

    /* Chunk 1: ISGN */
    chunkHdr = (BC250_DXBC_RAW_CHUNK_HEADER *)(Buffer + offsetIsgn);
    chunkHdr->FourCC = BC250_DXBC_FOURCC_ISGN;
    chunkHdr->ChunkSize = 16u;

    /* Chunk 2: OSGN */
    chunkHdr = (BC250_DXBC_RAW_CHUNK_HEADER *)(Buffer + offsetOsgn);
    chunkHdr->FourCC = BC250_DXBC_FOURCC_OSGN;
    chunkHdr->ChunkSize = 16u;

    /* Chunk 3: SHDR (Pixel Shader SM 4.1) */
    chunkHdr = (BC250_DXBC_RAW_CHUNK_HEADER *)(Buffer + offsetShdr);
    chunkHdr->FourCC = BC250_DXBC_FOURCC_SHDR;
    chunkHdr->ChunkSize = 64u; /* 8-byte shader header + 56 bytes opcode stream (14 dwords) */

    shaderData = (uint32_t *)(Buffer + offsetShdr + 8u);
    shaderData[0] = (0u << 16u) | (4u << 4u) | 1u; /* Pixel Shader, Major 4, Minor 1 */
    shaderData[1] = 16u; /* total program length: 2 header + 14 opcode DWORDs */

    /*
     * Instruction stream (14 DWORDs total):
     * 1. MOV: opcode 54, length 3 DWORDs -> token = (3 << 24) | 54
     * 2. ADD: opcode 0,  length 4 DWORDs -> token = (4 << 24) | 0
     * 3. CUSTOMDATA: opcode 116, token = (0 << 24) | 116, length word = 4, 4 payload words (total 6 words)
     * 4. RET: opcode 62, length 1 DWORD  -> token = (1 << 24) | 62
     * Sum = 3 + 4 + 6 + 1 = 14 DWORDs.
     */
    opWords[0] = (3u << 24u) | 54u;
    opWords[1] = 0x00100012u; /* dst operand mock */
    opWords[2] = 0x00200046u; /* src operand mock */

    opWords[3] = (4u << 24u) | 0u;
    opWords[4] = 0x00100012u;
    opWords[5] = 0x00200046u;
    opWords[6] = 0x00200086u;

    opWords[7] = (0u << 24u) | BC250_DXBC_OPCODE_CUSTOMDATA;
    opWords[8] = 4u; /* Custom data payload length */
    opWords[9] = 0x11111111u;
    opWords[10] = 0x22222222u;
    opWords[11] = 0x33333333u;
    opWords[12] = 0x44444444u;

    opWords[13] = (1u << 24u) | 62u; /* RET */

    for (offset = 0; offset < 14u; offset++) {
        shaderData[2u + offset] = opWords[offset];
    }

    /* Chunk 4: STAT */
    chunkHdr = (BC250_DXBC_RAW_CHUNK_HEADER *)(Buffer + offsetStat);
    chunkHdr->FourCC = BC250_DXBC_FOURCC_STAT;
    chunkHdr->ChunkSize = 16u;

    return 1;
}

static int RunTests(void)
{
    uint8_t buffer[512];
    uint32_t totalSize = 0;
    BC250_DXBC_CONTAINER container;
    BC250_DXBC_STATUS status;
    const BC250_DXBC_CHUNK *chunk;
    BC250_DXBC_INSTRUCTION_ITERATOR iter;
    BC250_DXBC_INSTRUCTION inst;

    CHECK(BuildValidDxbcContainer(buffer, sizeof(buffer), &totalSize) == 1);
    CHECK(totalSize == 220u);

    /* Test 1: Valid container parsing */
    status = Bc250DxbcParse(buffer, totalSize, &container);
    CHECK(status == BC250_DXBC_OK);
    CHECK(container.TotalSize == 220u);
    CHECK(container.Version == 1u);
    CHECK(container.ChunkCount == 5u);

    /* Fast lookup pointers */
    CHECK(container.RdefChunk != NULL);
    CHECK(container.RdefChunk->FourCC == BC250_DXBC_FOURCC_RDEF);
    CHECK(container.RdefChunk->DataSize == 16u);

    CHECK(container.IsgnChunk != NULL);
    CHECK(container.IsgnChunk->FourCC == BC250_DXBC_FOURCC_ISGN);

    CHECK(container.OsgnChunk != NULL);
    CHECK(container.OsgnChunk->FourCC == BC250_DXBC_FOURCC_OSGN);

    CHECK(container.ShaderChunk != NULL);
    CHECK(container.ShaderChunk->FourCC == BC250_DXBC_FOURCC_SHDR);
    CHECK(container.ShaderChunk->DataSize == 64u);

    CHECK(container.StatChunk != NULL);
    CHECK(container.StatChunk->FourCC == BC250_DXBC_FOURCC_STAT);

    /* Find chunk helper */
    chunk = Bc250DxbcFindChunk(&container, BC250_DXBC_FOURCC_SHDR);
    CHECK(chunk == container.ShaderChunk);
    chunk = Bc250DxbcFindChunk(&container, BC250_DXBC_FOURCC('U','N','K','N'));
    CHECK(chunk == NULL);

    /* Decoded shader header */
    CHECK(container.ShaderType == BC250_DXBC_SHADER_PIXEL);
    CHECK(container.MajorVersion == 4u);
    CHECK(container.MinorVersion == 1u);
    CHECK(container.OpcodeTokenCount == 14u);
    CHECK(container.OpcodeTokens != NULL);

    /* Test 2: Instruction iteration */
    Bc250DxbcInitInstructionIterator(&container, &iter);

    /* Instruction 1: MOV */
    CHECK(Bc250DxbcNextInstruction(&iter, &inst) == TRUE);
    CHECK(inst.Opcode == 54u);
    CHECK(inst.LengthDwords == 3u);
    CHECK(inst.Tokens != NULL);

    /* Instruction 2: ADD */
    CHECK(Bc250DxbcNextInstruction(&iter, &inst) == TRUE);
    CHECK(inst.Opcode == 0u);
    CHECK(inst.LengthDwords == 4u);

    /* Instruction 3: CUSTOMDATA */
    CHECK(Bc250DxbcNextInstruction(&iter, &inst) == TRUE);
    CHECK(inst.Opcode == BC250_DXBC_OPCODE_CUSTOMDATA);
    CHECK(inst.LengthDwords == 6u);

    /* Instruction 4: RET */
    CHECK(Bc250DxbcNextInstruction(&iter, &inst) == TRUE);
    CHECK(inst.Opcode == 62u);
    CHECK(inst.LengthDwords == 1u);

    /* Instruction 5: End of stream -> cleanly terminates */
    CHECK(Bc250DxbcNextInstruction(&iter, &inst) == FALSE);

    /* Test 3: Vertex Shader type decode */
    {
        uint32_t *shaderWords = (uint32_t *)(buffer + container.ShaderChunk->ChunkOffset + 8u);
        shaderWords[0] = (1u << 16u) | (5u << 4u) | 0u; /* Vertex Shader SM 5.0 */
        status = Bc250DxbcParse(buffer, totalSize, &container);
        CHECK(status == BC250_DXBC_OK);
        CHECK(container.ShaderType == BC250_DXBC_SHADER_VERTEX);
        CHECK(container.MajorVersion == 5u);
        CHECK(container.MinorVersion == 0u);
        /* restore */
        shaderWords[0] = (0u << 16u) | (4u << 4u) | 1u;
    }

    /* Test 4: Fail-closed negative cases */

    /* NULL parameters */
    CHECK(Bc250DxbcParse(NULL, totalSize, &container) == BC250_DXBC_BAD_ARGUMENT);
    CHECK(Bc250DxbcParse(buffer, totalSize, NULL) == BC250_DXBC_BAD_ARGUMENT);

    /* Buffer too small for DXBC header */
    CHECK(Bc250DxbcParse(buffer, 31u, &container) == BC250_DXBC_BUFFER_TOO_SMALL);

    /* Invalid Magic */
    {
        BC250_DXBC_RAW_HEADER *h = (BC250_DXBC_RAW_HEADER *)buffer;
        uint32_t savedMagic = h->Magic;
        h->Magic = 0xDEADBEEFu;
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_INVALID_MAGIC);
        h->Magic = savedMagic;
    }

    /* TotalSize exceeding BufferSize */
    CHECK(Bc250DxbcParse(buffer, totalSize - 1u, &container) == BC250_DXBC_BUFFER_TOO_SMALL);

    /* TotalSize smaller than header */
    {
        BC250_DXBC_RAW_HEADER *h = (BC250_DXBC_RAW_HEADER *)buffer;
        uint32_t savedTotal = h->TotalSize;
        h->TotalSize = 16u;
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_INVALID_TOTAL_SIZE);
        h->TotalSize = savedTotal;
    }

    /* ChunkCount exceeds maximum (32) */
    {
        BC250_DXBC_RAW_HEADER *h = (BC250_DXBC_RAW_HEADER *)buffer;
        uint32_t savedChunks = h->ChunkCount;
        h->ChunkCount = 33u;
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_INVALID_CHUNK_COUNT);
        h->ChunkCount = savedChunks;
    }

    /* Chunk offset out of bounds */
    {
        uint32_t *offsets = (uint32_t *)(buffer + 32u);
        uint32_t savedOffset = offsets[0];
        offsets[0] = totalSize + 10u;
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_CHUNK_OFFSET_OUT_OF_BOUNDS);
        offsets[0] = savedOffset;
    }

    /* Chunk data extends beyond TotalSize */
    {
        uint32_t *offsets = (uint32_t *)(buffer + 32u);
        BC250_DXBC_RAW_CHUNK_HEADER *chk = (BC250_DXBC_RAW_CHUNK_HEADER *)(buffer + offsets[0]);
        uint32_t savedSize = chk->ChunkSize;
        chk->ChunkSize = totalSize;
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_CHUNK_DATA_OUT_OF_BOUNDS);
        chk->ChunkSize = savedSize;
    }

    /* Shader token count exceeds chunk data size */
    {
        uint32_t *offsets = (uint32_t *)(buffer + 32u);
        uint32_t *shaderWords = (uint32_t *)(buffer + offsets[3] + 8u);
        uint32_t savedTokens = shaderWords[1];
        shaderWords[1] = 1000u; /* Way larger than chunk size */
        CHECK(Bc250DxbcParse(buffer, totalSize, &container) == BC250_DXBC_INVALID_SHADER_HEADER);
        shaderWords[1] = savedTokens;
    }

    /* Instruction iterator fail-closed on malformed stream */
    {
        uint32_t badStream[4];
        BC250_DXBC_CONTAINER badCont;
        memset(&badCont, 0, sizeof(badCont));
        badCont.OpcodeTokens = badStream;
        badCont.OpcodeTokenCount = 4u;

        /* Case A: Opcode with length 0 */
        badStream[0] = (0u << 24u) | 54u; /* length = 0 */
        Bc250DxbcInitInstructionIterator(&badCont, &iter);
        CHECK(Bc250DxbcNextInstruction(&iter, &inst) == FALSE);

        /* Case B: Opcode with length exceeding stream */
        badStream[0] = (10u << 24u) | 54u; /* length = 10 > 4 */
        Bc250DxbcInitInstructionIterator(&badCont, &iter);
        CHECK(Bc250DxbcNextInstruction(&iter, &inst) == FALSE);

        /* Case C: CUSTOMDATA exceeding stream */
        badStream[0] = (0u << 24u) | BC250_DXBC_OPCODE_CUSTOMDATA;
        badStream[1] = 5u; /* total len = 2 + 5 = 7 > 4 */
        Bc250DxbcInitInstructionIterator(&badCont, &iter);
        CHECK(Bc250DxbcNextInstruction(&iter, &inst) == FALSE);
    }

    /* Status name helper */
    CHECK(Bc250DxbcStatusName(BC250_DXBC_OK) != NULL);
    CHECK(Bc250DxbcStatusName(BC250_DXBC_INVALID_MAGIC) != NULL);
    CHECK(Bc250DxbcShaderTypeName(BC250_DXBC_SHADER_PIXEL) != NULL);
    CHECK(Bc250DxbcShaderTypeName(BC250_DXBC_SHADER_VERTEX) != NULL);

    return 1;
}

#ifdef BC250_KERNEL_MODE
void __stdcall mainCRTStartup(void) { ExitProcess(RunTests() ? 0ul : 1ul); }
#else
int main(void)
{
    int pass = RunTests();
    if (pass) {
        printf("DXBC PARSER CONTRACT TEST: PASS\n");
        return 0;
    } else {
        printf("DXBC PARSER CONTRACT TEST: FAIL\n");
        return 1;
    }
}
#endif
