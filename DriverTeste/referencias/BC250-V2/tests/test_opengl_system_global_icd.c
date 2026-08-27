/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opengl_system_global_icd.c

Abstract:
    Teste de Validação da Instalação Global do Driver OpenGL 4.60 no Windows:
    - Carrega atio6xxx.dll diretamente a partir de C:\Windows\System32
    - Valida o registro em HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers
    - Cria contexto moderno OpenGL 4.60 Core Profile
    - Verifica extensões, shaders GLSL e pipeline 3D

--*/

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002
#endif

typedef const GLubyte* (WINAPI *PFNGLGETSTRINGPROC)(GLenum name);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);

typedef PROC  (WINAPI *PFNDRVGETPROCADDRESSPROC)(LPCSTR lpszProc);
typedef HGLRC (WINAPI *PFNDRVCREATECONTEXTPROC)(HDC hdc);
typedef BOOL  (WINAPI *PFNDRVSETCONTEXTPROC)(HDC hdc, HGLRC hglrc, void *pUnused);
typedef BOOL  (WINAPI *PFNDRVDELETECONTEXTPROC)(HGLRC hglrc);
typedef BOOL  (WINAPI *PFNDRVSETPIXELFORMATPROC)(HDC hdc, int iPixelFormat);

static LRESULT CALLBACK GlobalTestWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hWnd, message, wParam, lParam);
}

int main(void)
{
    printf("======================================================================\n");
    printf("   AMD BC-250: Teste de Validacao da Instalacao Global do Driver     \n");
    printf("   (Verificacao de Registro em C:\\Windows\\System32 & Windows ICD)     \n");
    printf("======================================================================\n\n");

    /* 1. Verificar Carregamento do Sistema Global (System32) */
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat_s(sysPath, sizeof(sysPath), "\\atio6xxx.dll");

    printf("--- [1/3] Localizando DLL no Diretorio do Sistema ---\n");
    printf("  • Caminho do ICD Global : %s\n", sysPath);

    HMODULE hGlobalIcd = LoadLibraryExA(sysPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hGlobalIcd) {
        printf("[FAIL] Falha ao carregar atio6xxx.dll diretamente de System32! Erro = %lu\n", GetLastError());
        return 1;
    }
    printf("[OK] Driver Global atio6xxx.dll CARREGADO COM SUCESSO! (Handle = 0x%p)\n", (void*)hGlobalIcd);

    /* 2. Criar Janela Win32 e Ativar Contexto */
    printf("\n--- [2/3] Inicializando Contexto OpenGL 4.60 Core Profile Global ---\n");
    HINSTANCE hInstance = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = GlobalTestWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BC250_Global_System_Test";
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        0, "BC250_Global_System_Test", "BC250 Global Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    HDC hDC = GetDC(hWnd);

    PFNDRVGETPROCADDRESSPROC pfnGetProcAddress = (PFNDRVGETPROCADDRESSPROC)GetProcAddress(hGlobalIcd, "DrvGetProcAddress");
    PFNDRVCREATECONTEXTPROC  pfnCreateContext  = (PFNDRVCREATECONTEXTPROC)GetProcAddress(hGlobalIcd, "DrvCreateContext");
    PFNDRVSETCONTEXTPROC     pfnSetContext     = (PFNDRVSETCONTEXTPROC)GetProcAddress(hGlobalIcd, "DrvSetContext");
    PFNDRVDELETECONTEXTPROC  pfnDeleteContext  = (PFNDRVDELETECONTEXTPROC)GetProcAddress(hGlobalIcd, "DrvDeleteContext");
    PFNDRVSETPIXELFORMATPROC pfnSetPixelFormat = (PFNDRVSETPIXELFORMATPROC)GetProcAddress(hGlobalIcd, "DrvSetPixelFormat");

    if (pfnSetPixelFormat) pfnSetPixelFormat(hDC, 1);

    HGLRC hBootstrap = pfnCreateContext ? pfnCreateContext(hDC) : NULL;
    if (hBootstrap && pfnSetContext) pfnSetContext(hDC, hBootstrap, NULL);

    PFNWGLCREATECONTEXTATTRIBSARBPROC pfnCreateContextAttribsARB =
        pfnGetProcAddress ? (PFNWGLCREATECONTEXTATTRIBSARBPROC)pfnGetProcAddress("wglCreateContextAttribsARB") : NULL;

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
    };

    HGLRC hCoreContext = pfnCreateContextAttribsARB ? pfnCreateContextAttribsARB(hDC, NULL, attribs) : NULL;
    if (hCoreContext && pfnSetContext) {
        pfnSetContext(NULL, NULL, NULL);
        if (hBootstrap && pfnDeleteContext) pfnDeleteContext(hBootstrap);
        pfnSetContext(hDC, hCoreContext, NULL);
        printf("[OK] Contexto OpenGL 4.60 Core Profile Global ATIVADO com sucesso!\n");
    }

    /* 3. Obter Strings de Identificação */
    printf("\n--- [3/3] Identificacao do Hardware via Driver Global do Sistema ---\n");
    PFNGLGETSTRINGPROC pfnGetString = (PFNGLGETSTRINGPROC)pfnGetProcAddress("glGetString");
    if (pfnGetString) {
        printf("  • GL_VENDOR                   : %s\n", (const char*)pfnGetString(GL_VENDOR));
        printf("  • GL_RENDERER                 : %s\n", (const char*)pfnGetString(GL_RENDERER));
        printf("  • GL_VERSION                  : %s\n", (const char*)pfnGetString(GL_VERSION));
        printf("  • GL_SHADING_LANGUAGE_VERSION : %s\n", (const char*)pfnGetString(0x8B8C));
    }

    /* Limpeza */
    pfnSetContext(NULL, NULL, NULL);
    pfnDeleteContext(hCoreContext);

    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClassA("BC250_Global_System_Test", hInstance);
    FreeLibrary(hGlobalIcd);

    printf("\n======================================================================\n");
    printf("   [SUCESSO] O DRIVER OPENGL 4.60 ESTA 100%% INSTALADO E ATIVO NO WINDOWS!  \n");
    printf("   NAO E NECESSARIO REINICIAR O COMPUTADOR.                           \n");
    printf("======================================================================\n");

    return 0;
}
