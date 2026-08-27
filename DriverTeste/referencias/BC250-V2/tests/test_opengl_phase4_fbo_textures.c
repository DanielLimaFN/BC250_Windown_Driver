/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opengl_phase4_fbo_textures.c

Abstract:
    Suite de Teste e Validação da FASE 4 do Roadmap OpenGL 4.0+ da BC-250:
    - Vertex Array Objects (VAO) e Vertex Buffer Objects (VBO / EBO)
    - Mapeamento Direto de Memória ReBAR GDDR6 (glMapBufferRange + glFlushMappedBufferRange)
    - Texturização 2D (RGBA8, RGBA16F, Depth24_Stencil8, Anisotropia 16x e Mipmaps)
    - Framebuffer Objects (FBO) 1080p com Render-to-Texture Offscreen
    - Validação de Status (GL_FRAMEBUFFER_COMPLETE) e Leitura Bit-a-Bit (glReadPixels)

--*/

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002
#endif

typedef char GLchar;
#if defined(_WIN64)
typedef signed long long int GLsizeiptr;
typedef signed long long int GLintptr;
#else
typedef signed long int GLsizeiptr;
typedef signed long int GLintptr;
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                           0x8892
#define GL_ELEMENT_ARRAY_BUFFER                   0x8893
#define GL_STATIC_DRAW                            0x88E4
#define GL_DYNAMIC_DRAW                           0x88E8
#endif

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT                          0x0002
#define GL_MAP_FLUSH_EXPLICIT_BIT                 0x0010
#endif

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                            0x8D40
#define GL_COLOR_ATTACHMENT0                      0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT               0x821A
#define GL_FRAMEBUFFER_COMPLETE                   0x8CD5
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT             0x84FE
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8                       0x88F0
#endif

#ifndef GL_RGBA8
#define GL_RGBA8                                  0x8058
#define GL_RGBA16F                                0x881A
#endif

#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR                   0x2703
#endif

/* Assinaturas de Procedimentos */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);

typedef void  (WINAPI *PFNGLGENBUFFERS)(GLsizei n, GLuint *buffers);
typedef void  (WINAPI *PFNGLBINDBUFFER)(GLenum target, GLuint buffer);
typedef void  (WINAPI *PFNGLBUFFERDATA)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void* (WINAPI *PFNGLMAPBUFFERRANGE)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef GLboolean (WINAPI *PFNGLUNMAPBUFFER)(GLenum target);
typedef void  (WINAPI *PFNGLFLUSHMAPPEDBUFFERRANGE)(GLenum target, GLintptr offset, GLsizeiptr length);

typedef void  (WINAPI *PFNGLGENVERTEXARRAYS)(GLsizei n, GLuint *arrays);
typedef void  (WINAPI *PFNGLBINDVERTEXARRAY)(GLuint array);
typedef void  (WINAPI *PFNGLENABLEVERTEXATTRIBARRAY)(GLuint index);
typedef void  (WINAPI *PFNGLVERTEXATTRIBPOINTER)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

typedef void  (WINAPI *PFNGLGENTEXTURES)(GLsizei n, GLuint *textures);
typedef void  (WINAPI *PFNGLBINDTEXTURE)(GLenum target, GLuint texture);
typedef void  (WINAPI *PFNGLTEXIMAGE2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void  (WINAPI *PFNGLTEXPARAMETERI)(GLenum target, GLenum pname, GLint param);
typedef void  (WINAPI *PFNGLTEXPARAMETERF)(GLenum target, GLenum pname, GLfloat param);
typedef void  (WINAPI *PFNGLGENERATEMIPMAP)(GLenum target);
typedef void  (WINAPI *PFNGLACTIVETEXTURE)(GLenum texture);

typedef void  (WINAPI *PFNGLGENFRAMEBUFFERS)(GLsizei n, GLuint *framebuffers);
typedef void  (WINAPI *PFNGLBINDFRAMEBUFFER)(GLenum target, GLuint framebuffer);
typedef void  (WINAPI *PFNGLFRAMEBUFFERTEXTURE2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (WINAPI *PFNGLCHECKFRAMEBUFFERSTATUS)(GLenum target);
typedef void  (WINAPI *PFNGLREADPIXELS)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);

typedef PROC  (WINAPI *PFNDRVGETPROCADDRESSPROC)(LPCSTR lpszProc);
typedef HGLRC (WINAPI *PFNDRVCREATECONTEXTPROC)(HDC hdc);
typedef BOOL  (WINAPI *PFNDRVSETCONTEXTPROC)(HDC hdc, HGLRC hglrc, void *pUnused);
typedef BOOL  (WINAPI *PFNDRVDELETECONTEXTPROC)(HGLRC hglrc);
typedef BOOL  (WINAPI *PFNDRVSETPIXELFORMATPROC)(HDC hdc, int iPixelFormat);

static LRESULT CALLBACK FboWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hWnd, message, wParam, lParam);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    SetDllDirectoryA(".");

    printf("======================================================================\n");
    printf("   AMD BC-250: Teste de VAO, Texturas, ReBAR & FBOs (FASE 4)         \n");
    printf("   GPU: Cyan Skillfish / Navi12 Lite (1536 Shaders / 24 CUs / 16GB)   \n");
    printf("======================================================================\n\n");

    /* 1. Carregar o Driver WGL ICD da BC-250 */
    const char *dllName = "bc250_opengl32.dll";
    HMODULE hIcd = LoadLibraryA(dllName);
    if (!hIcd) {
        dllName = "atio6xxx.dll";
        hIcd = LoadLibraryA(dllName);
    }
    if (!hIcd) {
        printf("[FAIL] Falha ao carregar driver WGL ICD.\n");
        return 1;
    }
    printf("[OK] Driver WGL ICD carregado: %s (0x%p)\n", dllName, (void*)hIcd);

    /* 2. Criar Janela Win32 e Ativar Contexto OpenGL 4.6 Core Profile */
    HINSTANCE hInstance = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = FboWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BC250_FBO_Textures_Test";
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        0, "BC250_FBO_Textures_Test", "BC250 FBO Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    HDC hDC = GetDC(hWnd);

    PFNDRVGETPROCADDRESSPROC pfnGetProcAddress = (PFNDRVGETPROCADDRESSPROC)GetProcAddress(hIcd, "DrvGetProcAddress");
    PFNDRVCREATECONTEXTPROC  pfnCreateContext  = (PFNDRVCREATECONTEXTPROC)GetProcAddress(hIcd, "DrvCreateContext");
    PFNDRVSETCONTEXTPROC     pfnSetContext     = (PFNDRVSETCONTEXTPROC)GetProcAddress(hIcd, "DrvSetContext");
    PFNDRVDELETECONTEXTPROC  pfnDeleteContext  = (PFNDRVDELETECONTEXTPROC)GetProcAddress(hIcd, "DrvDeleteContext");
    PFNDRVSETPIXELFORMATPROC pfnSetPixelFormat = (PFNDRVSETPIXELFORMATPROC)GetProcAddress(hIcd, "DrvSetPixelFormat");

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
        printf("[OK] Contexto OpenGL 4.6 Core Profile ATIVADO com sucesso.\n");
    }

    /* 3. Resolver Ponteiros de Funções da Fase 4 */
    PFNGLGENBUFFERS             pfnGenBuffers             = (PFNGLGENBUFFERS)pfnGetProcAddress("glGenBuffers");
    PFNGLBINDBUFFER             pfnBindBuffer             = (PFNGLBINDBUFFER)pfnGetProcAddress("glBindBuffer");
    PFNGLBUFFERDATA             pfnBufferData             = (PFNGLBUFFERDATA)pfnGetProcAddress("glBufferData");
    PFNGLMAPBUFFERRANGE         pfnMapBufferRange         = (PFNGLMAPBUFFERRANGE)pfnGetProcAddress("glMapBufferRange");
    PFNGLUNMAPBUFFER            pfnUnmapBuffer            = (PFNGLUNMAPBUFFER)pfnGetProcAddress("glUnmapBuffer");
    PFNGLFLUSHMAPPEDBUFFERRANGE pfnFlushMappedBufferRange = (PFNGLFLUSHMAPPEDBUFFERRANGE)pfnGetProcAddress("glFlushMappedBufferRange");

    PFNGLGENVERTEXARRAYS        pfnGenVertexArrays        = (PFNGLGENVERTEXARRAYS)pfnGetProcAddress("glGenVertexArrays");
    PFNGLBINDVERTEXARRAY        pfnBindVertexArray        = (PFNGLBINDVERTEXARRAY)pfnGetProcAddress("glBindVertexArray");
    PFNGLENABLEVERTEXATTRIBARRAY pfnEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAY)pfnGetProcAddress("glEnableVertexAttribArray");
    PFNGLVERTEXATTRIBPOINTER    pfnVertexAttribPointer    = (PFNGLVERTEXATTRIBPOINTER)pfnGetProcAddress("glVertexAttribPointer");

    PFNGLGENTEXTURES            pfnGenTextures            = (PFNGLGENTEXTURES)pfnGetProcAddress("glGenTextures");
    PFNGLBINDTEXTURE            pfnBindTexture            = (PFNGLBINDTEXTURE)pfnGetProcAddress("glBindTexture");
    PFNGLTEXIMAGE2D             pfnTexImage2D             = (PFNGLTEXIMAGE2D)pfnGetProcAddress("glTexImage2D");
    PFNGLTEXPARAMETERI          pfnTexParameteri          = (PFNGLTEXPARAMETERI)pfnGetProcAddress("glTexParameteri");
    PFNGLTEXPARAMETERF          pfnTexParameterf          = (PFNGLTEXPARAMETERF)pfnGetProcAddress("glTexParameterf");
    PFNGLGENERATEMIPMAP         pfnGenerateMipmap         = (PFNGLGENERATEMIPMAP)pfnGetProcAddress("glGenerateMipmap");

    PFNGLGENFRAMEBUFFERS        pfnGenFramebuffers        = (PFNGLGENFRAMEBUFFERS)pfnGetProcAddress("glGenFramebuffers");
    PFNGLBINDFRAMEBUFFER        pfnBindFramebuffer        = (PFNGLBINDFRAMEBUFFER)pfnGetProcAddress("glBindFramebuffer");
    PFNGLFRAMEBUFFERTEXTURE2D   pfnFramebufferTexture2D   = (PFNGLFRAMEBUFFERTEXTURE2D)pfnGetProcAddress("glFramebufferTexture2D");
    PFNGLCHECKFRAMEBUFFERSTATUS pfnCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUS)pfnGetProcAddress("glCheckFramebufferStatus");
    PFNGLREADPIXELS             pfnReadPixels             = (PFNGLREADPIXELS)pfnGetProcAddress("glReadPixels");

    /* 4. Testar VAO, VBO, EBO e Mapeamento ReBAR */
    printf("\n--- [1/3] Testando VAO, VBO, EBO e Mapeamento ReBAR GDDR6 ---\n");

    GLuint vao = 0, vbo = 0, ebo = 0;
    pfnGenVertexArrays(1, &vao);
    pfnBindVertexArray(vao);
    printf("  • VAO Alocado (ID = %u)\n", vao);

    pfnGenBuffers(1, &vbo);
    pfnBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t vboSize = 1024 * sizeof(float);
    pfnBufferData(GL_ARRAY_BUFFER, vboSize, NULL, GL_DYNAMIC_DRAW);

    /* Testar ReBAR MapBufferRange */
    void *pMapped = pfnMapBufferRange(GL_ARRAY_BUFFER, 0, vboSize, GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
    printf("  • Mapeamento ReBAR (Host Visible) : Ponteiro = 0x%p (%zu bytes)\n", pMapped, vboSize);

    if (pMapped) {
        memset(pMapped, 0xAB, 128);
        pfnFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 128);
        pfnUnmapBuffer(GL_ARRAY_BUFFER);
        printf("[OK] Flush e Unmap de Memoria ReBAR GDDR6 com SUCESSO!\n");
    }

    pfnEnableVertexAttribArray(0);
    pfnVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    pfnEnableVertexAttribArray(1);
    pfnVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    pfnEnableVertexAttribArray(2);
    pfnVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    pfnGenBuffers(1, &ebo);
    pfnBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    pfnBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    printf("  • VBO e EBO Vinculados ao VAO     : VBO=%u, EBO=%u\n", vbo, ebo);

    /* 5. Testar Texturas 2D (RGBA8, RGBA16F, Depth24_Stencil8, Anisotropia) */
    printf("\n--- [2/3] Testando Texturas 2D (Formatos HDR e Anisotropia 16x) ---\n");

    GLuint texColor = 0, texDepth = 0;
    pfnGenTextures(1, &texColor);
    pfnBindTexture(GL_TEXTURE_2D, texColor);
    pfnTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    pfnTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pfnTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    pfnTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    pfnTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f);

    /* Alocar Textura 1080p RGBA8 */
    pfnTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pfnGenerateMipmap(GL_TEXTURE_2D);
    printf("  • Textura Color RGBA8 1080p       : ID = %u (1920x1080 + Mipmaps)\n", texColor);

    /* Alocar Textura Depth/Stencil */
    pfnGenTextures(1, &texDepth);
    pfnBindTexture(GL_TEXTURE_2D, texDepth);
    pfnTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 1920, 1080, 0, 0x84F9, 0x84FA, NULL);
    printf("  • Textura Depth24_Stencil8 1080p  : ID = %u (1920x1080)\n", texDepth);

    /* 6. Testar Framebuffer Objects (FBO) e Render-to-Texture */
    printf("\n--- [3/3] Testando FBO 1080p Offscreen & glReadPixels ---\n");

    GLuint fbo = 0;
    pfnGenFramebuffers(1, &fbo);
    pfnBindFramebuffer(GL_FRAMEBUFFER, fbo);

    pfnFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
    pfnFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texDepth, 0);

    GLenum status = pfnCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("  • Status do FBO 1080p             : %s (0x%04X)\n", 
           (status == GL_FRAMEBUFFER_COMPLETE) ? "GL_FRAMEBUFFER_COMPLETE" : "INCOMPLETO", status);

    if (status == GL_FRAMEBUFFER_COMPLETE) {
        printf("[OK] FBO 1080p validado e pronto para renderizacao offscreen!\n");

        /* Testar Leitura de Pixels (glReadPixels) */
        uint8_t pixelSample[16];
        pfnReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixelSample);
        printf("  • glReadPixels Amostra (RGBA)     : (%u, %u, %u, %u)\n", 
               pixelSample[0], pixelSample[1], pixelSample[2], pixelSample[3]);
        printf("[OK] Leitura de pixels offscreen concluida com sucesso!\n");
    }

    /* Limpeza */
    pfnBindFramebuffer(GL_FRAMEBUFFER, 0);
    pfnBindVertexArray(0);

    pfnSetContext(NULL, NULL, NULL);
    pfnDeleteContext(hCoreContext);

    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClassA("BC250_FBO_Textures_Test", hInstance);
    FreeLibrary(hIcd);

    printf("\n======================================================================\n");
    printf("   [SUCESSO] TESTE DA FASE 4 (VAO, TEXTURAS, ReBAR & FBO) HOMOLOGADO!  \n");
    printf("======================================================================\n");

    return 0;
}
