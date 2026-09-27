#include "../umd/bc250_vkd3d_shader_abi.h"
#include "../core/bc250_dxbc_spirv_bridge.h"
#include <windows.h>

__declspec(dllexport) const char *__cdecl vkd3d_shader_get_version(
    unsigned int *Major, unsigned int *Minor)
{
    if (Major) *Major = 1;
    if (Minor) *Minor = 99;
    return "bc250-mock-vkd3d-shader-1.99";
}

__declspec(dllexport) int __cdecl vkd3d_shader_compile(
    const BC250_VKD3D_COMPILE_INFO *Info, BC250_VKD3D_CODE *Output, char **Messages)
{
    static const unsigned int spirv[5] = {
        BC250_SPIRV_MAGIC, 0x00010300u, 0u, 2u, 0u
    };
    void *copy;
    if (Messages) *Messages = NULL;
    if (!Info || !Output || Info->type != BC250_VKD3D_STRUCTURE_TYPE_COMPILE_INFO ||
        Info->source_type != BC250_VKD3D_SOURCE_DXBC_TPF ||
        Info->target_type != BC250_VKD3D_TARGET_SPIRV_BINARY ||
        !Info->source.code || !Info->source.size) return -1;
    copy = HeapAlloc(GetProcessHeap(), 0, sizeof(spirv));
    if (!copy) return -1;
    CopyMemory(copy, spirv, sizeof(spirv));
    Output->code = copy;
    Output->size = sizeof(spirv);
    return 0;
}

__declspec(dllexport) void __cdecl vkd3d_shader_free_shader_code(BC250_VKD3D_CODE *Code)
{
    if (!Code) return;
    if (Code->code) HeapFree(GetProcessHeap(), 0, (void *)Code->code);
    Code->code = NULL;
    Code->size = 0;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    (void)Instance; (void)Reason; (void)Reserved;
    return TRUE;
}
