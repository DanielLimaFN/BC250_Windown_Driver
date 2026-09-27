#ifndef BC250_VKD3D_SHADER_ADAPTER_H
#define BC250_VKD3D_SHADER_ADAPTER_H

#include <windows.h>
#include "../core/bc250_dxbc_spirv_bridge.h"

typedef struct BC250_VKD3D_SHADER_ADAPTER {
    HMODULE Module;
    FARPROC CompileProc;
    FARPROC FreeCodeProc;
    FARPROC GetVersionProc;
    FARPROC SerializeDxbcProc;
    unsigned int VersionMajor;
    unsigned int VersionMinor;
    char VersionText[96];
} BC250_VKD3D_SHADER_ADAPTER;

BOOL Bc250Vkd3dShaderAdapterOpen(
    const wchar_t *AbsoluteDllPath,
    BC250_VKD3D_SHADER_ADAPTER *Adapter);
void Bc250Vkd3dShaderAdapterClose(BC250_VKD3D_SHADER_ADAPTER *Adapter);
BOOL Bc250Vkd3dShaderAdapterGetTranslator(
    BC250_VKD3D_SHADER_ADAPTER *Adapter,
    BC250_DXBC_SPIRV_TRANSLATOR *Translator);

#endif
