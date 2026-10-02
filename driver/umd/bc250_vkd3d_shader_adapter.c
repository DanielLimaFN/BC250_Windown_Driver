#include "bc250_vkd3d_shader_adapter.h"
#include "bc250_vkd3d_shader_abi.h"
#include "bc250_descriptor_binding.h"
#include <stdint.h>
#include <string.h>

static BOOL Bc250IsAbsoluteDrivePath(const wchar_t *Path)
{
    return Path && Path[0] && Path[1] == L':' &&
        (Path[2] == L'\\' || Path[2] == L'/');
}

BOOL Bc250Vkd3dShaderAdapterOpen(
    const wchar_t *AbsoluteDllPath, BC250_VKD3D_SHADER_ADAPTER *Adapter)
{
    BC250_PFN_VKD3D_GET_VERSION getVersion;
    const char *version;
    size_t length;
    if (!Adapter) return FALSE;
    memset(Adapter, 0, sizeof(*Adapter));
    if (!Bc250IsAbsoluteDrivePath(AbsoluteDllPath)) return FALSE;
    Adapter->Module = LoadLibraryExW(AbsoluteDllPath, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!Adapter->Module) return FALSE;
    Adapter->CompileProc = GetProcAddress(Adapter->Module, "vkd3d_shader_compile");
    Adapter->FreeCodeProc = GetProcAddress(Adapter->Module, "vkd3d_shader_free_shader_code");
    Adapter->GetVersionProc = GetProcAddress(Adapter->Module, "vkd3d_shader_get_version");
    Adapter->SerializeDxbcProc = GetProcAddress(Adapter->Module, "vkd3d_shader_serialize_dxbc");
    Adapter->FreeMessagesProc = GetProcAddress(Adapter->Module, "vkd3d_shader_free_messages");
    if (!Adapter->CompileProc || !Adapter->FreeCodeProc || !Adapter->GetVersionProc) {
        Bc250Vkd3dShaderAdapterClose(Adapter); return FALSE;
    }
    getVersion = (BC250_PFN_VKD3D_GET_VERSION)Adapter->GetVersionProc;
    version = getVersion(&Adapter->VersionMajor, &Adapter->VersionMinor);
    if (!version || Adapter->VersionMajor == 0u) {
        Bc250Vkd3dShaderAdapterClose(Adapter); return FALSE;
    }
    length = strlen(version);
    if (length >= sizeof(Adapter->VersionText)) length = sizeof(Adapter->VersionText) - 1u;
    memcpy(Adapter->VersionText, version, length);
    Adapter->VersionText[length] = '\0';
    return TRUE;
}

void Bc250Vkd3dShaderAdapterClose(BC250_VKD3D_SHADER_ADAPTER *Adapter)
{
    if (!Adapter) return;
    if (Adapter->Module) FreeLibrary(Adapter->Module);
    memset(Adapter, 0, sizeof(*Adapter));
}

/*
 * Doc 516: resultado e mensagens da ultima compilacao, por thread. Quebras de
 * linha viram " | " para caber numa linha de log; truncado com "...".
 */
static __declspec(thread) char s_Bc250Vkd3dLastMessages[1024];
static __declspec(thread) int s_Bc250Vkd3dLastResult;

static void Bc250Vkd3dRecordMessages(
    BC250_VKD3D_SHADER_ADAPTER *Adapter, int Result, char *Messages)
{
    size_t out = 0, i;
    int truncated = 0;
    s_Bc250Vkd3dLastResult = Result;
    s_Bc250Vkd3dLastMessages[0] = '\0';
    if (!Messages) return;
    for (i = 0; Messages[i] != '\0'; ++i) {
        char c = Messages[i];
        if (c == '\r') continue;
        if (c == '\n' && Messages[i + 1] == '\0') break;   /* \n final: ignora */
        /* reserva 4 bytes: ate 3 de "..." e o terminador */
        if (out + (c == '\n' ? 3u : 1u) + 4u > sizeof(s_Bc250Vkd3dLastMessages)) {
            truncated = 1;
            break;
        }
        if (c == '\n') {
            s_Bc250Vkd3dLastMessages[out++] = ' ';
            s_Bc250Vkd3dLastMessages[out++] = '|';
            s_Bc250Vkd3dLastMessages[out++] = ' ';
        } else {
            s_Bc250Vkd3dLastMessages[out++] = c;
        }
    }
    if (truncated) {
        s_Bc250Vkd3dLastMessages[out++] = '.';
        s_Bc250Vkd3dLastMessages[out++] = '.';
        s_Bc250Vkd3dLastMessages[out++] = '.';
    }
    s_Bc250Vkd3dLastMessages[out] = '\0';
    if (Adapter && Adapter->FreeMessagesProc) {
        ((BC250_PFN_VKD3D_FREE_MESSAGES)Adapter->FreeMessagesProc)(Messages);
    }
}

const char *Bc250Vkd3dShaderAdapterLastMessages(int *LastResult)
{
    if (LastResult) *LastResult = s_Bc250Vkd3dLastResult;
    return s_Bc250Vkd3dLastMessages;
}

static int Bc250Vkd3dCompile(void *Context, const void *Dxbc, uint32_t DxbcBytes,
    const void **Spirv, uint32_t *SpirvBytes)
{
    BC250_VKD3D_SHADER_ADAPTER *adapter = (BC250_VKD3D_SHADER_ADAPTER *)Context;
    BC250_VKD3D_COMPILE_INFO info;
    BC250_VKD3D_CODE output;
    BC250_VKD3D_INTERFACE_INFO interfaceInfo;
    BC250_VKD3D_RESOURCE_BINDING bindings[BC250_DESCRIPTOR_MAX_BINDINGS];
    unsigned int count = 0, stage, kind, slot;
    BC250_PFN_VKD3D_COMPILE compile;
    BC250_PFN_VKD3D_FREE_CODE freeCode;
    void *copy;
    char *messages;
    int result;
    if (!adapter || !adapter->CompileProc || !adapter->FreeCodeProc ||
        !Spirv || !SpirvBytes) return -1;
    memset(&info, 0, sizeof(info)); memset(&output, 0, sizeof(output));
    info.type = BC250_VKD3D_STRUCTURE_TYPE_COMPILE_INFO;
    info.source.code = Dxbc; info.source.size = DxbcBytes;
    info.source_type = BC250_VKD3D_SOURCE_DXBC_TPF;
    info.target_type = BC250_VKD3D_TARGET_SPIRV_BINARY;
    memset(&interfaceInfo, 0, sizeof(interfaceInfo));
    memset(bindings, 0, sizeof(bindings));
    for (stage = 0; stage < BC250_DESCRIPTOR_STAGE_COUNT; ++stage) {
        for (kind = 0; kind < 3; ++kind) {
            const unsigned int limits[] = {BC250_DESCRIPTOR_CB_COUNT, BC250_DESCRIPTOR_SRV_COUNT, BC250_DESCRIPTOR_SAMPLER_COUNT};
            const unsigned int bases[] = {BC250_DESCRIPTOR_CB_BASE, BC250_DESCRIPTOR_SRV_BASE, BC250_DESCRIPTOR_SAMPLER_BASE};
            const int types[] = {2, 0, 3}; /* CBV, SRV, SAMPLER */
            for (slot = 0; slot < limits[kind]; ++slot) {
                BC250_VKD3D_RESOURCE_BINDING *b = &bindings[count++];
                b->type = types[kind];
                b->register_index = slot;
                b->shader_visibility = stage == 0 ? 1 : 5; /* VERTEX / PIXEL */
                /* SHADER.2 (Corrida 161): SRV aceita BUFFER|IMAGE. So' IMAGE
                 * recusava Buffer<T> em t# ("E2000: Could not find descriptor
                 * binding for type 0") -- o PS do menu Iniciar. Um shader nao
                 * usa o mesmo t# dos dois jeitos; o tipo Vulkan sai da reflexao. */
                b->flags = kind == 0 ? 1u : (kind == 1 ? 3u : 2u); /* BUFFER / BUFFER|IMAGE / IMAGE */
                b->binding.binding = stage * BC250_DESCRIPTOR_STAGE_STRIDE + bases[kind] + slot;
                b->binding.count = 1u;
            }
        }
    }
    interfaceInfo.type = 1; /* VKD3D_SHADER_STRUCTURE_TYPE_INTERFACE_INFO */
    interfaceInfo.bindings = bindings;
    interfaceInfo.binding_count = count;
    info.next = &interfaceInfo;
    compile = (BC250_PFN_VKD3D_COMPILE)adapter->CompileProc;
    freeCode = (BC250_PFN_VKD3D_FREE_CODE)adapter->FreeCodeProc;
    /* Doc 516: pede as mensagens do vkd3d (nivel WARNING) quando a DLL exporta
     * vkd3d_shader_free_messages; sem isso a recusa chegava muda. */
    info.log_level = adapter->FreeMessagesProc ? BC250_VKD3D_LOG_WARNING : 0;
    messages = NULL;
    result = compile(&info, &output, adapter->FreeMessagesProc ? &messages : NULL);
    Bc250Vkd3dRecordMessages(adapter, result, messages);
    if (result < 0 || !output.code || output.size == 0u || output.size > UINT32_MAX) {
        if (output.code) freeCode(&output);
        return -1;
    }
    copy = HeapAlloc(GetProcessHeap(), 0, output.size);
    if (!copy) { freeCode(&output); return -1; }
    memcpy(copy, output.code, output.size);
    *Spirv = copy; *SpirvBytes = (uint32_t)output.size;
    freeCode(&output);
    return 0;
}

static void Bc250Vkd3dFree(void *Context, const void *Spirv)
{
    (void)Context;
    if (Spirv) HeapFree(GetProcessHeap(), 0, (void *)Spirv);
}

static int Bc250Vkd3dSerializeDxbc(
    void *Context,
    size_t SectionCount,
    const BC250_DXBC_SECTION_DESC *Sections,
    const void **Dxbc,
    uint32_t *DxbcBytes)
{
    BC250_VKD3D_SHADER_ADAPTER *adapter = (BC250_VKD3D_SHADER_ADAPTER *)Context;
    BC250_PFN_VKD3D_SERIALIZE_DXBC serialize;
    BC250_PFN_VKD3D_FREE_CODE freeCode;
    BC250_VKD3D_DXBC_SECTION_DESC vkd3dSections[8];
    BC250_VKD3D_CODE output;
    int result;
    size_t i;
    void *copy;

    if (!adapter || !adapter->SerializeDxbcProc || !adapter->FreeCodeProc ||
        !Sections || !Dxbc || !DxbcBytes)
        return -1;
    if (SectionCount == 0 || SectionCount > 8)
        return -1;

    serialize = (BC250_PFN_VKD3D_SERIALIZE_DXBC)adapter->SerializeDxbcProc;
    freeCode = (BC250_PFN_VKD3D_FREE_CODE)adapter->FreeCodeProc;
    memset(&output, 0, sizeof(output));
    for (i = 0; i < SectionCount; ++i) {
        vkd3dSections[i].tag = Sections[i].Tag;
        vkd3dSections[i].data.code = Sections[i].Data;
        vkd3dSections[i].data.size = Sections[i].DataBytes;
    }

    result = serialize(SectionCount, vkd3dSections, &output, NULL);
    if (result < 0 || !output.code || output.size == 0u || output.size > UINT32_MAX) {
        if (output.code) freeCode(&output);
        return -1;
    }

    copy = HeapAlloc(GetProcessHeap(), 0, output.size);
    if (!copy) {
        freeCode(&output);
        return -1;
    }
    memcpy(copy, output.code, output.size);
    *Dxbc = copy;
    *DxbcBytes = (uint32_t)output.size;
    freeCode(&output);
    return 0;
}

static void Bc250Vkd3dFreeDxbc(void *Context, const void *Dxbc)
{
    (void)Context;
    if (Dxbc) HeapFree(GetProcessHeap(), 0, (void *)Dxbc);
}

BOOL Bc250Vkd3dShaderAdapterGetTranslator(
    BC250_VKD3D_SHADER_ADAPTER *Adapter, BC250_DXBC_SPIRV_TRANSLATOR *Translator)
{
    if (!Adapter || !Adapter->Module || !Adapter->CompileProc ||
        !Adapter->FreeCodeProc || !Translator) return FALSE;
    Translator->Context = Adapter;
    Translator->Compile = Bc250Vkd3dCompile;
    Translator->Free = Bc250Vkd3dFree;
    Translator->SerializeDxbc = Adapter->SerializeDxbcProc ? Bc250Vkd3dSerializeDxbc : NULL;
    Translator->FreeDxbc = Bc250Vkd3dFreeDxbc;
    return TRUE;
}
