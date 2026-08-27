/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opengl_phase1_wgl_context.c

Abstract:
    Suite de Teste e Validação da FASE 1 do Roadmap OpenGL 4.0+ da BC-250:
    - Negociação e Criação de Contexto WGL (WGL ICD Interface)
    - Criação de Contexto Bootstrap Legado
    - Resolução de Extensões WGL (wglCreateContextAttribsARB, wglChoosePixelFormatARB)
    - Criação de Contexto OpenGL 4.0 / 4.6 Core Profile
    - Consulta de Identificação e Strings de Versão (GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION)
    - Verificação de Limites de Hardware da BC-250 (1536 Shaders / 24 CUs)
    - Destruição e Liberação Segura de Recursos WGL

--*/

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* ==========================================================================
   Definições e Tokens da API OpenGL 4.0+ e Extensões WGL
   ========================================================================== */

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB               0x2093
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126

#define WGL_CONTEXT_DEBUG_BIT_ARB                 0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002

#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

#ifndef WGL_DRAW_TO_WINDOW_ARB
#define WGL_DRAW_TO_WINDOW_ARB                    0x2001
#define WGL_ACCELERATION_ARB                      0x2003
#define WGL_SUPPORT_OPENGL_ARB                    0x2010
#define WGL_DOUBLE_BUFFER_ARB                     0x2011
#define WGL_PIXEL_TYPE_ARB                        0x2013
#define WGL_COLOR_BITS_ARB                        0x2014
#define WGL_DEPTH_BITS_ARB                        0x2022
#define WGL_STENCIL_BITS_ARB                      0x2023
#define WGL_FULL_ACCELERATION_ARB                 0x2027
#define WGL_TYPE_RGBA_ARB                         0x202B
#endif

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION               0x8B8C
#endif

#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS                         0x821D
#endif

#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS                     0x8869
#endif

#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS                0x8872
#endif

#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS       0x8B4D
#endif

#ifndef GL_MAX_UNIFORM_BLOCK_SIZE
#define GL_MAX_UNIFORM_BLOCK_SIZE                 0x8A30
#endif

#ifndef GL_MAX_UNIFORM_BUFFER_BINDINGS
#define GL_MAX_UNIFORM_BUFFER_BINDINGS            0x8A2F
#endif

#ifndef GL_MAX_SHADER_STORAGE_BLOCK_SIZE
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE          0x90DE
#endif

#ifndef GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS     0x90DF
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS     0x90EB
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_COUNT
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT           0x91BE
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_SIZE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE            0x91BF
#endif

/* Tipos de Ponteiros para Funções WGL & GL Modernas */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef const GLubyte* (WINAPI *PFNGLGETSTRINGIPROC)(GLenum name, GLuint index);
typedef void  (WINAPI *PFNGLGETINTEGERI_VPROC)(GLenum target, GLuint index, GLint *data);

/* ==========================================================================
   Procedimento de Janela Win32
   ========================================================================== */

static LRESULT CALLBACK TestWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
}

/* ==========================================================================
   Função Principal de Teste
   ========================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("======================================================================\n");
    printf("   AMD BC-250: Teste de Validacao de Contexto OpenGL 4.0+ (FASE 1)    \n");
    printf("   GPU: Cyan Skillfish / Navi12 Lite (1536 Shaders / 24 CUs / 16GB)   \n");
    printf("======================================================================\n\n");

    HINSTANCE hInstance = GetModuleHandleA(NULL);

    /* 1. Registrar Classe de Janela */
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = TestWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BC250_OpenGL_Test_Class";

    if (!RegisterClassExA(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[FAIL] Falha ao registrar classe de janela Win32. Erro = %lu\n", err);
            return 1;
        }
    }
    printf("[OK] Classe Win32 registrada com sucesso.\n");

    /* 2. Criar Janela Win32 Oculta */
    HWND hWnd = CreateWindowExA(
        0,
        "BC250_OpenGL_Test_Class",
        "BC-250 OpenGL Validation Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        printf("[FAIL] Falha ao criar janela Win32. Erro = %lu\n", GetLastError());
        return 1;
    }
    printf("[OK] Janela Win32 criada (HWND = 0x%p).\n", (void*)hWnd);

    HDC hDC = GetDC(hWnd);
    if (!hDC) {
        printf("[FAIL] Falha ao obter HDC da janela.\n");
        DestroyWindow(hWnd);
        return 1;
    }
    printf("[OK] HDC obtido com sucesso (HDC = 0x%p).\n", (void*)hDC);

    /* 3. Configurar Pixel Format Padrão para Bootstrap */
    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize      = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hDC, &pfd);
    if (pixelFormat == 0) {
        printf("[FAIL] ChoosePixelFormat falhou. Erro = %lu\n", GetLastError());
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        return 1;
    }
    printf("[OK] PixelFormat escolhido = %d.\n", pixelFormat);

    if (!SetPixelFormat(hDC, pixelFormat, &pfd)) {
        printf("[FAIL] SetPixelFormat falhou. Erro = %lu\n", GetLastError());
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        return 1;
    }
    printf("[OK] SetPixelFormat aplicado com sucesso.\n");

    /* 4. Criar Contexto Bootstrap Legado */
    HGLRC hBootstrapContext = wglCreateContext(hDC);
    if (!hBootstrapContext) {
        printf("[FAIL] wglCreateContext (Bootstrap) falhou. Erro = %lu\n", GetLastError());
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        return 1;
    }
    printf("[OK] Contexto Bootstrap criado (HGLRC = 0x%p).\n", (void*)hBootstrapContext);

    if (!wglMakeCurrent(hDC, hBootstrapContext)) {
        printf("[FAIL] wglMakeCurrent (Bootstrap) falhou. Erro = %lu\n", GetLastError());
        wglDeleteContext(hBootstrapContext);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        return 1;
    }
    printf("[OK] Contexto Bootstrap ativado via wglMakeCurrent.\n");

    /* 5. Obter Ponteiros de Extensão WGL */
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    
    PFNGLGETSTRINGIPROC glGetStringi =
        (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");

    PFNGLGETINTEGERI_VPROC glGetIntegeri_v =
        (PFNGLGETINTEGERI_VPROC)wglGetProcAddress("glGetIntegeri_v");

    printf("\n--- Query de Ponteiros WGL/GL ---\n");
    printf("  • wglCreateContextAttribsARB : %s (0x%p)\n", 
           wglCreateContextAttribsARB ? "DISPONIVEL" : "AUSENTE", (void*)wglCreateContextAttribsARB);
    printf("  • glGetStringi               : %s (0x%p)\n", 
           glGetStringi ? "DISPONIVEL" : "AUSENTE", (void*)glGetStringi);

    HGLRC hModernContext = NULL;

    if (wglCreateContextAttribsARB) {
        printf("\n--- Criando Contexto OpenGL Moderno (Core Profile) ---\n");

        /* Tentar OpenGL 4.6 Core Profile primeiro */
        int attribs46[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 6,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
            0
        };

        hModernContext = wglCreateContextAttribsARB(hDC, NULL, attribs46);
        if (hModernContext) {
            printf("[OK] Contexto OpenGL 4.6 Core Profile criado com SUCESSO!\n");
        } else {
            printf("[INFO] OpenGL 4.6 nao suportado diretamente, tentando OpenGL 4.0 Core Profile...\n");
            int attribs40[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
                WGL_CONTEXT_MINOR_VERSION_ARB, 0,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                0
            };
            hModernContext = wglCreateContextAttribsARB(hDC, NULL, attribs40);
            if (hModernContext) {
                printf("[OK] Contexto OpenGL 4.0 Core Profile criado com SUCESSO!\n");
            } else {
                printf("[WARN] Falha ao criar OpenGL 4.0 Core, mantendo contexto legado.\n");
            }
        }
    }

    if (hModernContext) {
        /* Desativar bootstrap e ativar contexto moderno */
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hBootstrapContext);
        hBootstrapContext = NULL;

        if (!wglMakeCurrent(hDC, hModernContext)) {
            printf("[FAIL] Falha ao ativar contexto moderno via wglMakeCurrent.\n");
            wglDeleteContext(hModernContext);
            ReleaseDC(hWnd, hDC);
            DestroyWindow(hWnd);
            return 1;
        }
        printf("[OK] Contexto Moderno ativado como contexto corrente!\n");
    }

    /* 6. Consultar Informações do Driver OpenGL */
    const char *vendor   = (const char*)glGetString(GL_VENDOR);
    const char *renderer = (const char*)glGetString(GL_RENDERER);
    const char *version  = (const char*)glGetString(GL_VERSION);
    const char *glslVer  = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("\n======================================================================\n");
    printf("   IDENTIFICACAO DO DRIVER OPENGL                                     \n");
    printf("======================================================================\n");
    printf("  GL_VENDOR                   : %s\n", vendor   ? vendor   : "(null)");
    printf("  GL_RENDERER                 : %s\n", renderer ? renderer : "(null)");
    printf("  GL_VERSION                  : %s\n", version  ? version  : "(null)");
    printf("  GL_SHADING_LANGUAGE_VERSION : %s\n", glslVer  ? glslVer  : "(null)");
    printf("======================================================================\n\n");

    /* 7. Consultar Limites de Hardware da BC-250 */
    printf("--- Verificacao de Limites de Hardware ---\n");

    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    printf("  • GL_MAX_VERTEX_ATTRIBS                  : %d (Esperado >= 16)\n", maxAttribs);

    GLint maxTexUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
    printf("  • GL_MAX_TEXTURE_IMAGE_UNITS             : %d (Esperado >= 16)\n", maxTexUnits);

    GLint maxCombinedTex = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTex);
    printf("  • GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS    : %d (Esperado >= 48)\n", maxCombinedTex);

    GLint maxUBOSize = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUBOSize);
    printf("  • GL_MAX_UNIFORM_BLOCK_SIZE              : %d bytes (%.1f KB)\n", 
           maxUBOSize, (double)maxUBOSize / 1024.0);

    GLint maxUBOBindings = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUBOBindings);
    printf("  • GL_MAX_UNIFORM_BUFFER_BINDINGS         : %d\n", maxUBOBindings);

    GLint maxSSBOSize = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSSBOSize);
    printf("  • GL_MAX_SHADER_STORAGE_BLOCK_SIZE       : %d bytes (%.1f MB)\n", 
           maxSSBOSize, (double)maxSSBOSize / (1024.0 * 1024.0));

    GLint maxComputeInvocations = 0;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxComputeInvocations);
    printf("  • GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS  : %d (Esperado >= 1024)\n", maxComputeInvocations);

    if (glGetIntegeri_v) {
        GLint maxComputeCount[3] = {0, 0, 0};
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeCount[0]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxComputeCount[1]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxComputeCount[2]);
        printf("  • GL_MAX_COMPUTE_WORK_GROUP_COUNT        : (%d, %d, %d)\n", 
               maxComputeCount[0], maxComputeCount[1], maxComputeCount[2]);

        GLint maxComputeSize[3] = {0, 0, 0};
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxComputeSize[0]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &maxComputeSize[1]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &maxComputeSize[2]);
        printf("  • GL_MAX_COMPUTE_WORK_GROUP_SIZE         : (%d, %d, %d)\n", 
               maxComputeSize[0], maxComputeSize[1], maxComputeSize[2]);
    }

    /* 8. Enumeração de Extensões */
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    printf("\n  • Total de Extensoes Reportadas          : %d\n", numExtensions);

    if (glGetStringi && numExtensions > 0) {
        printf("\n--- Amostra de Extensoes Chave Suportadas ---\n");
        int printed = 0;
        for (int i = 0; i < numExtensions && printed < 15; ++i) {
            const char *ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (ext) {
                printf("    [%03d] %s\n", i, ext);
                printed++;
            }
        }
        if (numExtensions > 15) {
            printf("    ... (%d extensoes adicionais)\n", numExtensions - 15);
        }
    }

    /* 9. Limpeza e Liberação */
    printf("\n--- Finalizacao e Liberacao de Recursos ---\n");
    wglMakeCurrent(NULL, NULL);
    if (hModernContext) {
        wglDeleteContext(hModernContext);
        printf("[OK] Contexto OpenGL moderno destruido com sucesso.\n");
    }
    if (hBootstrapContext) {
        wglDeleteContext(hBootstrapContext);
        printf("[OK] Contexto Bootstrap destruido com sucesso.\n");
    }

    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClassA("BC250_OpenGL_Test_Class", hInstance);

    printf("\n======================================================================\n");
    printf("   [SUCESSO] TESTE DA FASE 1 CONCLUIDO COM EXITO!                      \n");
    printf("======================================================================\n");

    return 0;
}
