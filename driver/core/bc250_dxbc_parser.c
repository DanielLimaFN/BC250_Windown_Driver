#include <string.h>
#include "bc250_dxbc_parser.h"

BC250_DXBC_STATUS Bc250DxbcParse(
    const void *Buffer,
    uint32_t BufferSize,
    BC250_DXBC_CONTAINER *Container)
{
    const BC250_DXBC_RAW_HEADER *rawHeader;
    const uint32_t *chunkOffsets;
    uint32_t i;

    if (Buffer == NULL || Container == NULL) {
        return BC250_DXBC_BAD_ARGUMENT;
    }

    memset(Container, 0, sizeof(*Container));
    Container->RawData = Buffer;
    Container->RawSize = BufferSize;
    Container->ShaderType = BC250_DXBC_SHADER_UNKNOWN;

    if (BufferSize < sizeof(BC250_DXBC_RAW_HEADER)) {
        return BC250_DXBC_BUFFER_TOO_SMALL;
    }

    rawHeader = (const BC250_DXBC_RAW_HEADER *)Buffer;
    if (rawHeader->Magic != BC250_DXBC_MAGIC_DXBC) {
        return BC250_DXBC_INVALID_MAGIC;
    }

    if (rawHeader->TotalSize > BufferSize) {
        return BC250_DXBC_BUFFER_TOO_SMALL;
    }

    if (rawHeader->TotalSize < sizeof(BC250_DXBC_RAW_HEADER)) {
        return BC250_DXBC_INVALID_TOTAL_SIZE;
    }

    if (rawHeader->ChunkCount > BC250_DXBC_MAX_CHUNKS) {
        return BC250_DXBC_INVALID_CHUNK_COUNT;
    }

    /* Check chunk offset table bounds */
    if (sizeof(BC250_DXBC_RAW_HEADER) + rawHeader->ChunkCount * sizeof(uint32_t) > rawHeader->TotalSize) {
        return BC250_DXBC_INVALID_CHUNK_COUNT;
    }

    Container->TotalSize = rawHeader->TotalSize;
    Container->Version = rawHeader->Version;
    Container->ChunkCount = rawHeader->ChunkCount;

    chunkOffsets = (const uint32_t *)((const uint8_t *)Buffer + sizeof(BC250_DXBC_RAW_HEADER));

    for (i = 0; i < Container->ChunkCount; i++) {
        uint32_t offset = chunkOffsets[i];
        const BC250_DXBC_RAW_CHUNK_HEADER *chunkHdr;
        BC250_DXBC_CHUNK *chunk;

        if (offset + sizeof(BC250_DXBC_RAW_CHUNK_HEADER) > Container->TotalSize) {
            return BC250_DXBC_CHUNK_OFFSET_OUT_OF_BOUNDS;
        }

        chunkHdr = (const BC250_DXBC_RAW_CHUNK_HEADER *)((const uint8_t *)Buffer + offset);

        if (offset + sizeof(BC250_DXBC_RAW_CHUNK_HEADER) + chunkHdr->ChunkSize > Container->TotalSize) {
            return BC250_DXBC_CHUNK_DATA_OUT_OF_BOUNDS;
        }

        chunk = &Container->Chunks[i];
        chunk->FourCC = chunkHdr->FourCC;
        chunk->ChunkOffset = offset;
        chunk->DataOffset = offset + (uint32_t)sizeof(BC250_DXBC_RAW_CHUNK_HEADER);
        chunk->DataSize = chunkHdr->ChunkSize;
        chunk->Data = (const uint8_t *)Buffer + chunk->DataOffset;

        /* Assign fast lookup pointers */
        if (chunk->FourCC == BC250_DXBC_FOURCC_RDEF && Container->RdefChunk == NULL) {
            Container->RdefChunk = chunk;
        } else if (chunk->FourCC == BC250_DXBC_FOURCC_ISGN && Container->IsgnChunk == NULL) {
            Container->IsgnChunk = chunk;
        } else if (chunk->FourCC == BC250_DXBC_FOURCC_OSGN && Container->OsgnChunk == NULL) {
            Container->OsgnChunk = chunk;
        } else if ((chunk->FourCC == BC250_DXBC_FOURCC_SHDR || chunk->FourCC == BC250_DXBC_FOURCC_SHEX) &&
                   Container->ShaderChunk == NULL) {
            Container->ShaderChunk = chunk;
        } else if (chunk->FourCC == BC250_DXBC_FOURCC_STAT && Container->StatChunk == NULL) {
            Container->StatChunk = chunk;
        }
    }

    /* If a shader chunk (SHDR or SHEX) is present, decode its header */
    if (Container->ShaderChunk != NULL) {
        const uint32_t *shaderWords;
        uint32_t versionToken;
        uint32_t tokenCount;
        uint32_t typeField;

        if (Container->ShaderChunk->DataSize < 8u) {
            return BC250_DXBC_INVALID_SHADER_HEADER;
        }

        shaderWords = (const uint32_t *)Container->ShaderChunk->Data;
        versionToken = shaderWords[0];
        tokenCount = shaderWords[1];

        /* tokenCount indicates the number of DWORDs in the opcode stream */
        if (tokenCount * sizeof(uint32_t) > Container->ShaderChunk->DataSize - 8u) {
            return BC250_DXBC_INVALID_SHADER_HEADER;
        }

        typeField = (versionToken >> 16u) & 0xFFFFu;
        Container->MajorVersion = (versionToken >> 4u) & 0xFu;
        Container->MinorVersion = versionToken & 0xFu;

        switch (typeField) {
        case 0u: Container->ShaderType = BC250_DXBC_SHADER_PIXEL; break;
        case 1u: Container->ShaderType = BC250_DXBC_SHADER_VERTEX; break;
        case 2u: Container->ShaderType = BC250_DXBC_SHADER_GEOMETRY; break;
        case 3u: Container->ShaderType = BC250_DXBC_SHADER_HULL; break;
        case 4u: Container->ShaderType = BC250_DXBC_SHADER_DOMAIN; break;
        case 5u: Container->ShaderType = BC250_DXBC_SHADER_COMPUTE; break;
        default: Container->ShaderType = BC250_DXBC_SHADER_UNKNOWN; break;
        }

        Container->OpcodeTokenCount = tokenCount;
        Container->OpcodeTokens = shaderWords + 2;
    }

    return BC250_DXBC_OK;
}

const BC250_DXBC_CHUNK *Bc250DxbcFindChunk(
    const BC250_DXBC_CONTAINER *Container,
    uint32_t FourCC)
{
    uint32_t i;

    if (Container == NULL) return NULL;

    for (i = 0; i < Container->ChunkCount; i++) {
        if (Container->Chunks[i].FourCC == FourCC) {
            return &Container->Chunks[i];
        }
    }
    return NULL;
}

void Bc250DxbcInitInstructionIterator(
    const BC250_DXBC_CONTAINER *Container,
    BC250_DXBC_INSTRUCTION_ITERATOR *Iterator)
{
    if (Iterator == NULL) return;

    if (Container != NULL && Container->OpcodeTokens != NULL && Container->OpcodeTokenCount > 0u) {
        Iterator->Current = Container->OpcodeTokens;
        Iterator->End = Container->OpcodeTokens + Container->OpcodeTokenCount;
    } else {
        Iterator->Current = NULL;
        Iterator->End = NULL;
    }
}

BOOL Bc250DxbcNextInstruction(
    BC250_DXBC_INSTRUCTION_ITERATOR *Iterator,
    BC250_DXBC_INSTRUCTION *Instruction)
{
    uint32_t token;
    uint32_t opcode;
    uint32_t lenDwords;

    if (Iterator == NULL || Iterator->Current == NULL || Iterator->Current >= Iterator->End ||
        Instruction == NULL) {
        return FALSE;
    }

    token = *Iterator->Current;
    opcode = token & BC250_DXBC_OPCODE_MASK;
    lenDwords = (token >> BC250_DXBC_OPCODE_LEN_SHIFT) & BC250_DXBC_OPCODE_LEN_MASK;

    if (opcode == BC250_DXBC_OPCODE_CUSTOMDATA) {
        if (Iterator->Current + 2 > Iterator->End) {
            return FALSE;
        }
        lenDwords = Iterator->Current[1] + 2u;
        if (Iterator->Current + lenDwords > Iterator->End) {
            return FALSE;
        }
    } else {
        if (lenDwords == 0u) {
            return FALSE;
        }
        if (Iterator->Current + lenDwords > Iterator->End) {
            return FALSE;
        }
    }

    Instruction->Opcode = opcode;
    Instruction->LengthDwords = lenDwords;
    Instruction->Tokens = Iterator->Current;

    Iterator->Current += lenDwords;
    return TRUE;
}

const char *Bc250DxbcStatusName(BC250_DXBC_STATUS Status)
{
    switch (Status) {
    case BC250_DXBC_OK: return "OK";
    case BC250_DXBC_BAD_ARGUMENT: return "BAD_ARGUMENT";
    case BC250_DXBC_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case BC250_DXBC_INVALID_MAGIC: return "INVALID_MAGIC";
    case BC250_DXBC_INVALID_TOTAL_SIZE: return "INVALID_TOTAL_SIZE";
    case BC250_DXBC_INVALID_CHUNK_COUNT: return "INVALID_CHUNK_COUNT";
    case BC250_DXBC_CHUNK_OFFSET_OUT_OF_BOUNDS: return "CHUNK_OFFSET_OUT_OF_BOUNDS";
    case BC250_DXBC_CHUNK_DATA_OUT_OF_BOUNDS: return "CHUNK_DATA_OUT_OF_BOUNDS";
    case BC250_DXBC_CHUNK_NOT_FOUND: return "CHUNK_NOT_FOUND";
    case BC250_DXBC_INVALID_SHADER_HEADER: return "INVALID_SHADER_HEADER";
    case BC250_DXBC_UNKNOWN_SHADER_TYPE: return "UNKNOWN_SHADER_TYPE";
    default: return "UNKNOWN";
    }
}

const char *Bc250DxbcShaderTypeName(BC250_DXBC_SHADER_TYPE Type)
{
    switch (Type) {
    case BC250_DXBC_SHADER_PIXEL: return "PixelShader";
    case BC250_DXBC_SHADER_VERTEX: return "VertexShader";
    case BC250_DXBC_SHADER_GEOMETRY: return "GeometryShader";
    case BC250_DXBC_SHADER_HULL: return "HullShader";
    case BC250_DXBC_SHADER_DOMAIN: return "DomainShader";
    case BC250_DXBC_SHADER_COMPUTE: return "ComputeShader";
    default: return "UnknownShader";
    }
}
