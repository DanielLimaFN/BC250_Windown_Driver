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
    FARPROC FreeMessagesProc; /* opcional: vkd3d_shader_free_messages */
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

/*
 * Menu Iniciar (Corrida 160, doc 516): o vkd3d-shader recusou um pixel shader e
 * a mensagem era descartada (compile(..., NULL)). Resultado e mensagens da
 * ULTIMA compilacao DESTE thread (armazenamento por thread: shaders compilam em
 * threads diferentes). String vazia quando nao houve mensagem.
 */
const char *Bc250Vkd3dShaderAdapterLastMessages(int *LastResult);

#endif
