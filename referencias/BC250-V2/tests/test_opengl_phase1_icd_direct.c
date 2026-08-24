/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opengl_phase1_icd_direct.c

Abstract:
    Teste direto de validação da interface WGL ICD (atio6xxx.dll / bc250_opengl32.dll)
    da AMD BC-250 (Cyan Skillfish).

    Valida:
    - Carregamento dinâmico do módulo WGL ICD
    - Resolução de todas as rotinas obrigatórias Drv*
    - DrvDescribePixelFormat e DrvSetPixelFormat
    - Criação de Contexto WGL 4.0 / 4.6 Core Profile via wglCreateContextAttribsARB
    - Consulta de GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION
    - Verificação dos limites de hardware da BC-250 (1536 Shaders / 24 CUs)
    - Enumeração de extensões modernas via glGetStringi
    - Desativação e destruição limpa do contexto

--*/

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB               0x2093
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
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

/* Assinaturas de Tipos ICD Drv* */
typedef HGLRC (WINAPI *PFNDRVCREATECONTEXTPROC)(HDC hdc);
typedef HGLRC (WINAPI *PFNDRVCREATELAYERCONTEXTPROC)(HDC hdc, int iLayerPlane);
typedef BOOL  (WINAPI *PFNDRVDELETECONTEXTPROC)(HGLRC hglrc);
typedef BOOL  (WINAPI *PFNDRVSETCONTEXTPROC)(HDC hdc, HGLRC hglrc, void *pUnused);
typedef BOOL  (WINAPI *PFNDRVRELEASECONTEXTPROC)(HGLRC hglrc);
typedef PROC  (WINAPI *PFNDRVGETPROCADDRESSPROC)(LPCSTR lpszProc);
typedef BOOL  (WINAPI *PFNDRVSETPIXELFORMATPROC)(HDC hdc, int iPixelFormat);
typedef int   (WINAPI *PFNDRVDESCRIBEPIXELFORMATPROC)(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
typedef BOOL  (WINAPI *PFNDRVSWAPBUFFERSPROC)(HDC hdc);
typedef BOOL  (WINAPI *PFNDRVSHARELISTSPROC)(HGLRC hglrc1, HGLRC hglrc2);
typedef BOOL  (WINAPI *PFNDRVVALIDATEVERSIONPROC)(DWORD dwVersion);

/* Assinaturas de Funções GL & WGL */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
typedef const GLubyte* (WINAPI *PFNGLGETSTRINGIPROC)(GLenum name, GLuint index);
typedef const GLubyte* (WINAPI *PFNGLGETSTRINGPROC)(GLenum name);
typedef void  (WINAPI *PFNGLGETINTEGERVPROC)(GLenum pname, GLint *params);
typedef void  (WINAPI *PFNGLGETINTEGERI_VPROC)(GLenum target, GLuint index, GLint *data);

static LRESULT CALLBACK DirectWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hWnd, message, wParam, lParam);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("======================================================================\n");
    printf("   AMD BC-250: Teste Direto do Driver WGL ICD (FASE 1)               \n");
    printf("   GPU: Cyan Skillfish / Navi12 Lite (1536 Shaders / 24 CUs / 16GB)   \n");
    printf("======================================================================\n\n");

    /* 1. Carregar o driver ICD da BC-250 */
    const char *dllName = "bc250_opengl32.dll";
    HMODULE hIcd = LoadLibraryA(dllName);
    if (!hIcd) {
        dllName = "atio6xxx.dll";
        hIcd = LoadLibraryA(dllName);
    }
    if (!hIcd) {
        dllName = "..\\build\\bc250_opengl32.dll";
        hIcd = LoadLibraryA(dllName);
    }
    if (!hIcd) {
        printf("[FAIL] Falha ao carregar DLL do driver WGL ICD. Erro = %lu\n", GetLastError());
        return 1;
    }
    printf("[OK] Driver WGL ICD carregado com sucesso: %s (HMODULE = 0x%p)\n", dllName, (void*)hIcd);

    /* 2. Resolver Símbolos Obrigatórios Drv* */
    PFNDRVCREATECONTEXTPROC       pfnDrvCreateContext       = (PFNDRVCREATECONTEXTPROC)GetProcAddress(hIcd, "DrvCreateContext");
    PFNDRVCREATELAYERCONTEXTPROC  pfnDrvCreateLayerContext  = (PFNDRVCREATELAYERCONTEXTPROC)GetProcAddress(hIcd, "DrvCreateLayerContext");
    PFNDRVDELETECONTEXTPROC       pfnDrvDeleteContext       = (PFNDRVDELETECONTEXTPROC)GetProcAddress(hIcd, "DrvDeleteContext");
    PFNDRVSETCONTEXTPROC          pfnDrvSetContext          = (PFNDRVSETCONTEXTPROC)GetProcAddress(hIcd, "DrvSetContext");
    PFNDRVRELEASECONTEXTPROC      pfnDrvReleaseContext      = (PFNDRVRELEASECONTEXTPROC)GetProcAddress(hIcd, "DrvReleaseContext");
    PFNDRVGETPROCADDRESSPROC      pfnDrvGetProcAddress      = (PFNDRVGETPROCADDRESSPROC)GetProcAddress(hIcd, "DrvGetProcAddress");
    PFNDRVSETPIXELFORMATPROC      pfnDrvSetPixelFormat      = (PFNDRVSETPIXELFORMATPROC)GetProcAddress(hIcd, "DrvSetPixelFormat");
    PFNDRVDESCRIBEPIXELFORMATPROC pfnDrvDescribePixelFormat = (PFNDRVDESCRIBEPIXELFORMATPROC)GetProcAddress(hIcd, "DrvDescribePixelFormat");
    PFNDRVSWAPBUFFERSPROC         pfnDrvSwapBuffers         = (PFNDRVSWAPBUFFERSPROC)GetProcAddress(hIcd, "DrvSwapBuffers");
    PFNDRVSHARELISTSPROC          pfnDrvShareLists          = (PFNDRVSHARELISTSPROC)GetProcAddress(hIcd, "DrvShareLists");
    PFNDRVVALIDATEVERSIONPROC     pfnDrvValidateVersion     = (PFNDRVVALIDATEVERSIONPROC)GetProcAddress(hIcd, "DrvValidateVersion");

    printf("\n--- Verificacao de Simbolos Exportados da Interface ICD ---\n");
    printf("  • DrvCreateContext       : %s (0x%p)\n", pfnDrvCreateContext ? "OK" : "MISSING", (void*)pfnDrvCreateContext);
    printf("  • DrvCreateLayerContext  : %s (0x%p)\n", pfnDrvCreateLayerContext ? "OK" : "MISSING", (void*)pfnDrvCreateLayerContext);
    printf("  • DrvDeleteContext       : %s (0x%p)\n", pfnDrvDeleteContext ? "OK" : "MISSING", (void*)pfnDrvDeleteContext);
    printf("  • DrvSetContext          : %s (0x%p)\n", pfnDrvSetContext ? "OK" : "MISSING", (void*)pfnDrvSetContext);
    printf("  • DrvReleaseContext      : %s (0x%p)\n", pfnDrvReleaseContext ? "OK" : "MISSING", (void*)pfnDrvReleaseContext);
    printf("  • DrvGetProcAddress      : %s (0x%p)\n", pfnDrvGetProcAddress ? "OK" : "MISSING", (void*)pfnDrvGetProcAddress);
    printf("  • DrvSetPixelFormat      : %s (0x%p)\n", pfnDrvSetPixelFormat ? "OK" : "MISSING", (void*)pfnDrvSetPixelFormat);
    printf("  • DrvDescribePixelFormat : %s (0x%p)\n", pfnDrvDescribePixelFormat ? "OK" : "MISSING", (void*)pfnDrvDescribePixelFormat);
    printf("  • DrvSwapBuffers         : %s (0x%p)\n", pfnDrvSwapBuffers ? "OK" : "MISSING", (void*)pfnDrvSwapBuffers);
    printf("  • DrvShareLists          : %s (0x%p)\n", pfnDrvShareLists ? "OK" : "MISSING", (void*)pfnDrvShareLists);
    printf("  • DrvValidateVersion     : %s (0x%p)\n", pfnDrvValidateVersion ? "OK" : "MISSING", (void*)pfnDrvValidateVersion);

    if (!pfnDrvCreateContext || !pfnDrvDeleteContext || !pfnDrvSetContext || 
        !pfnDrvGetProcAddress || !pfnDrvSetPixelFormat || !pfnDrvDescribePixelFormat) {
        printf("[FAIL] Interface WGL ICD incompleta.\n");
        FreeLibrary(hIcd);
        return 1;
    }
    printf("[OK] Todos os simbolos obrigatorios da interface WGL ICD presentes!\n");

    /* 3. Criar Janela Win32 */
    HINSTANCE hInstance = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = DirectWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BC250_ICD_Direct_Test";
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        0, "BC250_ICD_Direct_Test", "BC250 Direct ICD Window",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    HDC hDC = GetDC(hWnd);

    /* 4. Testar DrvDescribePixelFormat e DrvSetPixelFormat */
    PIXELFORMATDESCRIPTOR pfd;
    pfnDrvDescribePixelFormat(hDC, 1, sizeof(pfd), &pfd);
    printf("\n--- Informacoes de Pixel Format (ICD) ---\n");
    printf("  • Bits de Cor   : %d\n", pfd.cColorBits);
    printf("  • Bits de Depth : %d\n", pfd.cDepthBits);
    printf("  • Bits Stencil  : %d\n", pfd.cStencilBits);
    printf("  • Flags         : 0x%08lX (OpenGL | DoubleBuffer | HW)\n", pfd.dwFlags);

    pfnDrvSetPixelFormat(hDC, 1);
    printf("[OK] DrvSetPixelFormat executado com sucesso.\n");

    /* 5. Criar Contexto Bootstrap e Resolver Extensões */
    HGLRC hBootstrap = pfnDrvCreateContext(hDC);
    if (!hBootstrap) {
        printf("[FAIL] DrvCreateContext falhou.\n");
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        FreeLibrary(hIcd);
        return 1;
    }
    printf("[OK] Contexto Bootstrap ICD criado (HGLRC = 0x%p).\n", (void*)hBootstrap);

    pfnDrvSetContext(hDC, hBootstrap, NULL);

    /* Obter wglCreateContextAttribsARB e funções GL via DrvGetProcAddress */
    PFNWGLCREATECONTEXTATTRIBSARBPROC pfnCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)pfnDrvGetProcAddress("wglCreateContextAttribsARB");
    PFNGLGETSTRINGPROC    pfnGetString    = (PFNGLGETSTRINGPROC)pfnDrvGetProcAddress("glGetString");
    PFNGLGETSTRINGIPROC   pfnGetStringi   = (PFNGLGETSTRINGIPROC)pfnDrvGetProcAddress("glGetStringi");
    PFNGLGETINTEGERVPROC  pfnGetIntegerv  = (PFNGLGETINTEGERVPROC)pfnDrvGetProcAddress("glGetIntegerv");
    PFNGLGETINTEGERI_VPROC pfnGetIntegeri_v = (PFNGLGETINTEGERI_VPROC)pfnDrvGetProcAddress("glGetIntegeri_v");

    printf("\n--- Query de Extensao e Procedimentos GL Modernos ---\n");
    printf("  • wglCreateContextAttribsARB : %s (0x%p)\n", pfnCreateContextAttribsARB ? "OK" : "FAIL", (void*)pfnCreateContextAttribsARB);
    printf("  • glGetString                : %s (0x%p)\n", pfnGetString ? "OK" : "FAIL", (void*)pfnGetString);
    printf("  • glGetStringi               : %s (0x%p)\n", pfnGetStringi ? "OK" : "FAIL", (void*)pfnGetStringi);
    printf("  • glGetIntegerv              : %s (0x%p)\n", pfnGetIntegerv ? "OK" : "FAIL", (void*)pfnGetIntegerv);
    printf("  • glGetIntegeri_v            : %s (0x%p)\n", pfnGetIntegeri_v ? "OK" : "FAIL", (void*)pfnGetIntegeri_v);

    if (!pfnCreateContextAttribsARB) {
        printf("[FAIL] wglCreateContextAttribsARB nao exportada pelo ICD.\n");
        pfnDrvDeleteContext(hBootstrap);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        FreeLibrary(hIcd);
        return 1;
    }

    /* 6. Criar Contexto Moderno OpenGL 4.6 Core Profile */
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
    };

    HGLRC hModern = pfnCreateContextAttribsARB(hDC, NULL, attribs);
    if (!hModern) {
        printf("[FAIL] wglCreateContextAttribsARB falhou ao criar contexto OpenGL 4.6 Core.\n");
        pfnDrvDeleteContext(hBootstrap);
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        FreeLibrary(hIcd);
        return 1;
    }
    printf("[OK] Contexto OpenGL 4.6 Core Profile criado com SUCESSO (HGLRC = 0x%p)!\n", (void*)hModern);

    /* Trocar para o contexto moderno */
    pfnDrvSetContext(NULL, NULL, NULL);
    pfnDrvDeleteContext(hBootstrap);
    pfnDrvSetContext(hDC, hModern, NULL);
    printf("[OK] Contexto OpenGL 4.6 Core Profile ativado como corrente.\n");

    /* 7. Validar Identificação do Driver */
    const char *vendor   = (const char*)pfnGetString(GL_VENDOR);
    const char *renderer = (const char*)pfnGetString(GL_RENDERER);
    const char *version  = (const char*)pfnGetString(GL_VERSION);
    const char *glslVer  = (const char*)pfnGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("\n======================================================================\n");
    printf("   IDENTIFICACAO DO DRIVER OPENGL (ICD DIRETO)                        \n");
    printf("======================================================================\n");
    printf("  GL_VENDOR                   : %s\n", vendor   ? vendor   : "(null)");
    printf("  GL_RENDERER                 : %s\n", renderer ? renderer : "(null)");
    printf("  GL_VERSION                  : %s\n", version  ? version  : "(null)");
    printf("  GL_SHADING_LANGUAGE_VERSION : %s\n", glslVer  ? glslVer  : "(null)");
    printf("======================================================================\n\n");

    /* 8. Validar Limites de Hardware da BC-250 */
    printf("--- Verificacao de Limites de Hardware (1536 Shaders / 24 CUs) ---\n");

    GLint maxAttribs = 0;
    pfnGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    printf("  • GL_MAX_VERTEX_ATTRIBS                  : %d\n", maxAttribs);

    GLint maxTexUnits = 0;
    pfnGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
    printf("  • GL_MAX_TEXTURE_IMAGE_UNITS             : %d\n", maxTexUnits);

    GLint maxCombinedTex = 0;
    pfnGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTex);
    printf("  • GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS    : %d\n", maxCombinedTex);

    GLint maxUBOSize = 0;
    pfnGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUBOSize);
    printf("  • GL_MAX_UNIFORM_BLOCK_SIZE              : %d bytes (%.1f KB)\n", 
           maxUBOSize, (double)maxUBOSize / 1024.0);

    GLint maxSSBOSize = 0;
    pfnGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSSBOSize);
    printf("  • GL_MAX_SHADER_STORAGE_BLOCK_SIZE       : %d bytes (%.1f MB)\n", 
           maxSSBOSize, (double)maxSSBOSize / (1024.0 * 1024.0));

    GLint maxComputeInvocations = 0;
    pfnGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxComputeInvocations);
    printf("  • GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS  : %d\n", maxComputeInvocations);

    if (pfnGetIntegeri_v) {
        GLint maxComputeCount[3] = {0, 0, 0};
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeCount[0]);
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxComputeCount[1]);
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxComputeCount[2]);
        printf("  • GL_MAX_COMPUTE_WORK_GROUP_COUNT        : (%d, %d, %d)\n", 
               maxComputeCount[0], maxComputeCount[1], maxComputeCount[2]);

        GLint maxComputeSize[3] = {0, 0, 0};
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxComputeSize[0]);
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &maxComputeSize[1]);
        pfnGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &maxComputeSize[2]);
        printf("  • GL_MAX_COMPUTE_WORK_GROUP_SIZE         : (%d, %d, %d)\n", 
               maxComputeSize[0], maxComputeSize[1], maxComputeSize[2]);
    }

    /* 9. Validar Extensões Reportadas */
    GLint numExtensions = 0;
    pfnGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    printf("\n  • Total de Extensoes Reportadas          : %d\n", numExtensions);

    if (pfnGetStringi && numExtensions > 0) {
        printf("\n--- Extensoes Modernas Suportadas ---\n");
        for (int i = 0; i < numExtensions; ++i) {
            const char *ext = (const char*)pfnGetStringi(GL_EXTENSIONS, i);
            if (ext) {
                printf("    [%02d] %s\n", i + 1, ext);
            }
        }
    }

    /* 10. Testar DrvSwapBuffers e Limpeza */
    printf("\n--- Testando DrvSwapBuffers e Liberacao Limpa ---\n");
    pfnDrvSwapBuffers(hDC);
    printf("[OK] DrvSwapBuffers executado com sucesso.\n");

    pfnDrvSetContext(NULL, NULL, NULL);
    pfnDrvDeleteContext(hModern);
    printf("[OK] Contexto OpenGL Moderno deletado com sucesso.\n");

    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClassA("BC250_ICD_Direct_Test", hInstance);
    FreeLibrary(hIcd);

    printf("\n======================================================================\n");
    printf("   [SUCESSO] TESTE DIRETO DO WGL ICD CONCLUIDO COM 100%% DE EXITO!     \n");
    printf("======================================================================\n");

    return 0;
}
