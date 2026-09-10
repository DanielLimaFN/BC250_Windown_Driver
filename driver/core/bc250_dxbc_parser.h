#ifndef BC250_DXBC_PARSER_H
#define BC250_DXBC_PARSER_H

#ifdef BC250_KERNEL_MODE
#include <ntddk.h>
#ifndef BC250_FIXED_WIDTH_TYPES_DEFINED
#define BC250_FIXED_WIDTH_TYPES_DEFINED
typedef UINT64 uint64_t;
typedef UINT32 uint32_t;
typedef UINT16 uint16_t;
typedef UINT8  uint8_t;
#endif
#ifndef BOOL
typedef int BOOL;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#else
#include <stdint.h>
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_DXBC_FOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
    ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define BC250_DXBC_MAGIC_DXBC   BC250_DXBC_FOURCC('D','X','B','C') /* 0x43425844 */
#define BC250_DXBC_FOURCC_RDEF  BC250_DXBC_FOURCC('R','D','E','F') /* 0x46454452 */
#define BC250_DXBC_FOURCC_ISGN  BC250_DXBC_FOURCC('I','S','G','N') /* 0x4E475349 */
#define BC250_DXBC_FOURCC_OSGN  BC250_DXBC_FOURCC('O','S','G','N') /* 0x4E47534F */
#define BC250_DXBC_FOURCC_SHDR  BC250_DXBC_FOURCC('S','H','D','R') /* 0x52444853 */
#define BC250_DXBC_FOURCC_SHEX  BC250_DXBC_FOURCC('S','H','E','X') /* 0x58454853 */
#define BC250_DXBC_FOURCC_STAT  BC250_DXBC_FOURCC('S','T','A','T') /* 0x54415453 */

#define BC250_DXBC_MAX_CHUNKS 32u
#define BC250_DXBC_HEADER_SIZE 32u
#define BC250_DXBC_CHUNK_HEADER_SIZE 8u

typedef enum BC250_DXBC_SHADER_TYPE {
    BC250_DXBC_SHADER_UNKNOWN = -1,
    BC250_DXBC_SHADER_PIXEL = 0,
    BC250_DXBC_SHADER_VERTEX = 1,
    BC250_DXBC_SHADER_GEOMETRY = 2,
    BC250_DXBC_SHADER_HULL = 3,
    BC250_DXBC_SHADER_DOMAIN = 4,
    BC250_DXBC_SHADER_COMPUTE = 5
} BC250_DXBC_SHADER_TYPE;

typedef enum BC250_DXBC_STATUS {
    BC250_DXBC_OK = 0,
    BC250_DXBC_BAD_ARGUMENT,
    BC250_DXBC_BUFFER_TOO_SMALL,
    BC250_DXBC_INVALID_MAGIC,
    BC250_DXBC_INVALID_TOTAL_SIZE,
    BC250_DXBC_INVALID_CHUNK_COUNT,
    BC250_DXBC_CHUNK_OFFSET_OUT_OF_BOUNDS,
    BC250_DXBC_CHUNK_DATA_OUT_OF_BOUNDS,
    BC250_DXBC_CHUNK_NOT_FOUND,
    BC250_DXBC_INVALID_SHADER_HEADER,
    BC250_DXBC_UNKNOWN_SHADER_TYPE
} BC250_DXBC_STATUS;

#pragma pack(push, 1)
typedef struct BC250_DXBC_RAW_HEADER {
    uint32_t Magic;          /* "DXBC" = 0x43425844 */
    uint32_t Checksum[4];    /* 128-bit hash */
    uint32_t Version;        /* 1 */
    uint32_t TotalSize;      /* Total byte size including header */
    uint32_t ChunkCount;     /* Number of chunk offsets following header */
} BC250_DXBC_RAW_HEADER;

typedef struct BC250_DXBC_RAW_CHUNK_HEADER {
    uint32_t FourCC;
    uint32_t ChunkSize;
} BC250_DXBC_RAW_CHUNK_HEADER;
#pragma pack(pop)

typedef struct BC250_DXBC_CHUNK {
    uint32_t FourCC;
    uint32_t ChunkOffset;
    uint32_t DataOffset;
    uint32_t DataSize;
    const void *Data;
} BC250_DXBC_CHUNK;

typedef struct BC250_DXBC_CONTAINER {
    const void *RawData;
    uint32_t RawSize;
    uint32_t TotalSize;
    uint32_t Version;
    uint32_t ChunkCount;
    BC250_DXBC_CHUNK Chunks[BC250_DXBC_MAX_CHUNKS];

    /* Fast lookup pointers (NULL if absent) */
    const BC250_DXBC_CHUNK *RdefChunk;
    const BC250_DXBC_CHUNK *IsgnChunk;
    const BC250_DXBC_CHUNK *OsgnChunk;
    const BC250_DXBC_CHUNK *ShaderChunk; /* SHDR or SHEX */
    const BC250_DXBC_CHUNK *StatChunk;

    /* Decoded shader header (from SHDR/SHEX) */
    BC250_DXBC_SHADER_TYPE ShaderType;
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t OpcodeTokenCount;
    const uint32_t *OpcodeTokens;
} BC250_DXBC_CONTAINER;

/* Instruction token scanner */
#define BC250_DXBC_OPCODE_MASK       0x000007FFu
#define BC250_DXBC_OPCODE_LEN_SHIFT  24u
#define BC250_DXBC_OPCODE_LEN_MASK   0x0000007Fu
#define BC250_DXBC_OPCODE_CUSTOMDATA 116u /* D3D10_SB_OPCODE_CUSTOMDATA */

typedef struct BC250_DXBC_INSTRUCTION {
    uint32_t Opcode;
    uint32_t LengthDwords;
    const uint32_t *Tokens;
} BC250_DXBC_INSTRUCTION;

typedef struct BC250_DXBC_INSTRUCTION_ITERATOR {
    const uint32_t *Current;
    const uint32_t *End;
} BC250_DXBC_INSTRUCTION_ITERATOR;

BC250_DXBC_STATUS Bc250DxbcParse(
    const void *Buffer,
    uint32_t BufferSize,
    BC250_DXBC_CONTAINER *Container);

const BC250_DXBC_CHUNK *Bc250DxbcFindChunk(
    const BC250_DXBC_CONTAINER *Container,
    uint32_t FourCC);

void Bc250DxbcInitInstructionIterator(
    const BC250_DXBC_CONTAINER *Container,
    BC250_DXBC_INSTRUCTION_ITERATOR *Iterator);

BOOL Bc250DxbcNextInstruction(
    BC250_DXBC_INSTRUCTION_ITERATOR *Iterator,
    BC250_DXBC_INSTRUCTION *Instruction);

const char *Bc250DxbcStatusName(BC250_DXBC_STATUS Status);
const char *Bc250DxbcShaderTypeName(BC250_DXBC_SHADER_TYPE Type);

#ifdef __cplusplus
}
#endif

#endif /* BC250_DXBC_PARSER_H */
