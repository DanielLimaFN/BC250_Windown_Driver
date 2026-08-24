/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_wgl_icd.h

Abstract:
    Definições, estruturas e assinaturas da interface WGL ICD
    (Installable Client Driver) para a AMD BC-250 (Cyan Skillfish).

--*/

#ifndef BC250_WGL_ICD_H
#define BC250_WGL_ICD_H

#include <windows.h>
#include <GL/gl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WGL_EXPORT
#define WGL_EXPORT __declspec(dllexport)
#endif

/* ==========================================================================
   Versão e Identificação da GPU BC-250
   ========================================================================== */
#define BC250_WGL_DRIVER_VERSION_MAJOR  1
#define BC250_WGL_DRIVER_VERSION_MINOR  0
#define BC250_WGL_DRIVER_BUILD          20260820

#define BC250_GL_VENDOR_STRING          "Advanced Micro Devices, Inc."
#define BC250_GL_RENDERER_STRING        "AMD BC-250 (Cyan Skillfish / Navi12 Lite - 1536 Shaders / 24 CUs / 16GB)"
#define BC250_GL_VERSION_STRING         "4.6.0 Compatibility Profile Context"
#define BC250_GLSL_VERSION_STRING       "4.60"

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION               0x8B8C
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION                          0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION                          0x821C
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS                         0x821D
#endif

typedef char GLchar;
#if defined(_WIN64)
typedef signed long long int GLsizeiptr;
typedef signed long long int GLintptr;
#else
typedef signed long int GLsizeiptr;
typedef signed long int GLintptr;
#endif

/* ==========================================================================
   Tokens de Shaders, Buffers e Compute (OpenGL 4.0 - 4.6)
   ========================================================================== */
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                        0x8B30
#define GL_VERTEX_SHADER                          0x8B31
#define GL_GEOMETRY_SHADER                        0x8DD9
#define GL_TESS_EVALUATION_SHADER                 0x8E87
#define GL_TESS_CONTROL_SHADER                    0x8E8E
#define GL_COMPUTE_SHADER                         0x91B9
#endif

#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                         0x8B81
#define GL_LINK_STATUS                            0x8B82
#define GL_VALIDATE_STATUS                        0x8B83
#define GL_INFO_LOG_LENGTH                        0x8B84
#define GL_ATTACHED_SHADERS                       0x8B85
#define GL_ACTIVE_UNIFORMS                        0x8B86
#define GL_ACTIVE_UNIFORM_MAX_LENGTH              0x8B87
#define GL_SHADER_SOURCE_LENGTH                   0x8B88
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                           0x8892
#define GL_ELEMENT_ARRAY_BUFFER                   0x8893
#define GL_UNIFORM_BUFFER                         0x8A11
#define GL_SHADER_STORAGE_BUFFER                  0x90D2
#define GL_STATIC_DRAW                            0x88E4
#define GL_DYNAMIC_DRAW                           0x88E8
#endif

#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT        0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT              0x00000002
#define GL_UNIFORM_BARRIER_BIT                    0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT              0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT        0x00000020
#define GL_COMMAND_BARRIER_BIT                    0x00000040
#define GL_PIXEL_BUFFER_BARRIER_BIT               0x00000080
#define GL_TEXTURE_UPDATE_BARRIER_BIT             0x00000100
#define GL_BUFFER_UPDATE_BARRIER_BIT              0x00000200
#define GL_FRAMEBUFFER_BARRIER_BIT                0x00000400
#define GL_TRANSFORM_FEEDBACK_BARRIER_BIT         0x00000800
#define GL_ATOMIC_COUNTER_BARRIER_BIT             0x00001000
#define GL_SHADER_STORAGE_BARRIER_BIT             0x00002000
#define GL_ALL_BARRIER_BITS                       0xFFFFFFFF
#endif

#ifndef GL_READ_ONLY
#define GL_READ_ONLY                              0x88B8
#define GL_WRITE_ONLY                             0x88B9
#define GL_READ_WRITE                             0x88BA
#endif

#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT                           0x0001
#define GL_MAP_WRITE_BIT                          0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT               0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT              0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT                 0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT                 0x0020
#define GL_MAP_PERSISTENT_BIT                     0x0040
#define GL_MAP_COHERENT_BIT                       0x0080
#endif

/* ==========================================================================
   Tokens de FBO, Texturas e Renderização (Fases 4 e 5)
   ========================================================================== */
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                            0x8D40
#define GL_READ_FRAMEBUFFER                       0x8CA8
#define GL_DRAW_FRAMEBUFFER                       0x8CA9
#define GL_RENDERBUFFER                           0x8D41
#define GL_COLOR_ATTACHMENT0                      0x8CE0
#define GL_COLOR_ATTACHMENT1                      0x8CE1
#define GL_COLOR_ATTACHMENT2                      0x8CE2
#define GL_COLOR_ATTACHMENT3                      0x8CE3
#define GL_DEPTH_ATTACHMENT                       0x8D00
#define GL_STENCIL_ATTACHMENT                     0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT               0x821A
#define GL_FRAMEBUFFER_COMPLETE                   0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT      0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D                             0x806F
#define GL_TEXTURE_CUBE_MAP                       0x8513
#define GL_TEXTURE_MIN_FILTER                     0x2801
#define GL_TEXTURE_MAG_FILTER                     0x2800
#define GL_TEXTURE_WRAP_S                         0x2802
#define GL_TEXTURE_WRAP_T                         0x2803
#define GL_TEXTURE_WRAP_R                         0x8072
#define GL_LINEAR                                 0x2601
#define GL_NEAREST                                0x2600
#define GL_LINEAR_MIPMAP_LINEAR                   0x2703
#define GL_CLAMP_TO_EDGE                          0x812F
#define GL_REPEAT                                 0x2901
#define GL_RGBA8                                  0x8058
#define GL_RGB8                                   0x8051
#define GL_RGBA16F                                0x881A
#define GL_RGBA32F                                0x8814
#define GL_DEPTH24_STENCIL8                       0x88F0
#define GL_TEXTURE_MAX_ANISOTROPY_EXT             0x84FE
#define GL_TEXTURE0                               0x84C0
#endif

#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST                             0x0B71
#define GL_CULL_FACE                              0x0B44
#define GL_BLEND                                  0x0BE2
#define GL_LESS                                   0x0201
#define GL_LEQUAL                                 0x0203
#define GL_SRC_ALPHA                              0x0302
#define GL_ONE_MINUS_SRC_ALPHA                    0x0303
#define GL_FRONT_AND_BACK                         0x0408
#define GL_FILL                                   0x1B02
#define GL_LINE                                   0x1B01
#endif

/* ==========================================================================
   Constantes e Tokens de Extensões WGL
   ========================================================================== */
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB               0x2093
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126

#define WGL_CONTEXT_DEBUG_BIT_ARB                 0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002
#define WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB         0x0004
#define WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256

#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

#define WGL_DRAW_TO_WINDOW_ARB                    0x2001
#define WGL_DRAW_TO_BITMAP_ARB                    0x2002
#define WGL_ACCELERATION_ARB                      0x2003
#define WGL_NEED_PALETTE_ARB                      0x2004
#define WGL_NEED_SYSTEM_PALETTE_ARB               0x2005
#define WGL_SWAP_LAYER_BUFFERS_ARB                0x2006
#define WGL_SWAP_METHOD_ARB                       0x2007
#define WGL_NUMBER_OVERLAYS_ARB                   0x2008
#define WGL_NUMBER_UNDERLAYS_ARB                  0x2009
#define WGL_TRANSPARENT_ARB                       0x200A
#define WGL_TRANSPARENT_RED_VALUE_ARB             0x2037
#define WGL_TRANSPARENT_GREEN_VALUE_ARB           0x2038
#define WGL_TRANSPARENT_BLUE_VALUE_ARB            0x2039
#define WGL_TRANSPARENT_ALPHA_VALUE_ARB           0x203A
#define WGL_TRANSPARENT_INDEX_VALUE_ARB           0x203B
#define WGL_SHARE_DEPTH_ARB                       0x200C
#define WGL_SHARE_STENCIL_ARB                     0x200D
#define WGL_SHARE_ACCUM_ARB                       0x200E
#define WGL_SUPPORT_GDI_ARB                       0x200F
#define WGL_SUPPORT_OPENGL_ARB                    0x2010
#define WGL_DOUBLE_BUFFER_ARB                     0x2011
#define WGL_STEREO_ARB                            0x2012
#define WGL_PIXEL_TYPE_ARB                        0x2013
#define WGL_COLOR_BITS_ARB                        0x2014
#define WGL_RED_BITS_ARB                          0x2015
#define WGL_RED_SHIFT_ARB                         0x2016
#define WGL_GREEN_BITS_ARB                        0x2017
#define WGL_GREEN_SHIFT_ARB                       0x2018
#define WGL_BLUE_BITS_ARB                         0x2019
#define WGL_BLUE_SHIFT_ARB                        0x201A
#define WGL_ALPHA_BITS_ARB                        0x201B
#define WGL_ALPHA_SHIFT_ARB                       0x201C
#define WGL_ACCUM_BITS_ARB                        0x201D
#define WGL_ACCUM_RED_BITS_ARB                    0x201E
#define WGL_ACCUM_GREEN_BITS_ARB                  0x201F
#define WGL_ACCUM_BLUE_BITS_ARB                   0x2020
#define WGL_ACCUM_ALPHA_BITS_ARB                  0x2021
#define WGL_DEPTH_BITS_ARB                        0x2022
#define WGL_STENCIL_BITS_ARB                      0x2023
#define WGL_AUX_BUFFERS_ARB                       0x2024
#define WGL_NO_ACCELERATION_ARB                   0x2025
#define WGL_GENERIC_ACCELERATION_ARB              0x2026
#define WGL_FULL_ACCELERATION_ARB                 0x2027
#define WGL_SWAP_EXCHANGE_ARB                     0x2028
#define WGL_SWAP_COPY_ARB                         0x2029
#define WGL_SWAP_UNDEFINED_ARB                    0x202A
#define WGL_TYPE_RGBA_ARB                         0x202B
#define WGL_TYPE_COLORINDEX_ARB                   0x202C

#define WGL_SAMPLE_BUFFERS_ARB                    0x2041
#define WGL_SAMPLES_ARB                           0x2042

/* ==========================================================================
   Estrutura Interna de Contexto OpenGL da BC-250
   ========================================================================== */
typedef struct _BC250_GL_CONTEXT {
    uint32_t       magic;          /* 0xBC250C78 */
    HDC            hDC;
    HWND           hWnd;
    int            pixelFormat;
    int            majorVersion;
    int            minorVersion;
    uint32_t       profileFlags;   /* CORE / COMPATIBILITY */
    uint32_t       contextFlags;   /* FORWARD_COMPATIBLE / DEBUG */
    struct _BC250_GL_CONTEXT *shareContext;
    void          *pGalliumContext;
    void          *pGalliumScreen;
    int            swapInterval;
    CRITICAL_SECTION csLock;
} BC250_GL_CONTEXT, *PBC250_GL_CONTEXT;

#define BC250_GL_CONTEXT_MAGIC 0xBC250C78

/* ==========================================================================
   Funções Exportadas da Interface WGL ICD
   ========================================================================== */
WGL_EXPORT BOOL WINAPI DrvCopyContext(HGLRC hglrcSrc, HGLRC hglrcDst, UINT mask);
WGL_EXPORT HGLRC WINAPI DrvCreateContext(HDC hdc);
WGL_EXPORT HGLRC WINAPI DrvCreateLayerContext(HDC hdc, int iLayerPlane);
WGL_EXPORT BOOL WINAPI DrvDeleteContext(HGLRC hglrc);
WGL_EXPORT LONG WINAPI DrvDescribeLayerPlane(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nBytes, LPLAYERPLANEDESCRIPTOR plpd);
WGL_EXPORT int WINAPI DrvDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
WGL_EXPORT int WINAPI DrvGetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, COLORREF *pcr);
WGL_EXPORT PROC WINAPI DrvGetProcAddress(LPCSTR lpszProc);
WGL_EXPORT BOOL WINAPI DrvPresentBuffers(HDC hdc, PVOID pData);
WGL_EXPORT int WINAPI DrvRealizeLayerPalette(HDC hdc, int iLayerPlane, BOOL bRealize);
WGL_EXPORT BOOL WINAPI DrvReleaseContext(HGLRC hglrc);
WGL_EXPORT BOOL WINAPI DrvSetCallbackProcs(int nProcs, PROC *pProcs);
WGL_EXPORT BOOL WINAPI DrvSetContext(HDC hdc, HGLRC hglrc, void *pUnused);
WGL_EXPORT int WINAPI DrvSetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, CONST COLORREF *pcr);
WGL_EXPORT BOOL WINAPI DrvSetPixelFormat(HDC hdc, int iPixelFormat);
WGL_EXPORT BOOL WINAPI DrvShareLists(HGLRC hglrc1, HGLRC hglrc2);
WGL_EXPORT BOOL WINAPI DrvSwapBuffers(HDC hdc);
WGL_EXPORT BOOL WINAPI DrvSwapLayerBuffers(HDC hdc, UINT fuPlanes);
WGL_EXPORT BOOL WINAPI DrvValidateVersion(DWORD dwVersion);

#ifdef __cplusplus
}
#endif

#endif /* BC250_WGL_ICD_H */
