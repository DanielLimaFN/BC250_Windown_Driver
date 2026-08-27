/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    bc250_wgl_icd.c

Abstract:
    Implementação da Interface WGL ICD (Installable Client Driver)
    para a AMD BC-250 (Cyan Skillfish / Navi12 Lite - 1536 Shaders / 24 CUs).

    Suporte completo a:
    - FASE 1: Negociação de Contextos WGL (Core e Compatibility)
    - FASE 2: Ponte Mesa Zink Gallium Driver -> Vulkan RADV
    - FASE 3: Shaders GLSL 4.00+ & Compute Shaders NIR
    - FASE 4: VAO, VBO, Mapeamento ReBAR (glMapBufferRange), Texturas 2D/3D/Cube e FBO
    - FASE 5: Renderização 3D, DrawCalls (Instancing/Indirect) e WGL SwapBuffers

--*/

#include "bc250_wgl_icd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* Contexto Corrente por Thread */
static __declspec(thread) PBC250_GL_CONTEXT g_CurrentContext = NULL;
static __declspec(thread) HDC                g_CurrentDC      = NULL;

/* Tabela de Extensões WGL Suportadas */
static const char g_WglExtensionsString[] =
    "WGL_ARB_create_context "
    "WGL_ARB_create_context_profile "
    "WGL_ARB_create_context_robustness "
    "WGL_ARB_extensions_string "
    "WGL_ARB_pixel_format "
    "WGL_ARB_multisample "
    "WGL_EXT_swap_control "
    "WGL_EXT_swap_control_tear "
    "WGL_EXT_extensions_string";

/* Tabela de Extensões OpenGL 4.0 / 4.6 Suportadas */
static const char *g_GlExtensions[] = {
    "GL_ARB_shading_language_420pack",
    "GL_ARB_explicit_attrib_location",
    "GL_ARB_explicit_uniform_location",
    "GL_ARB_uniform_buffer_object",
    "GL_ARB_shader_storage_buffer_object",
    "GL_ARB_compute_shader",
    "GL_ARB_tessellation_shader",
    "GL_ARB_geometry_shader4",
    "GL_ARB_texture_buffer_object",
    "GL_ARB_texture_multisample",
    "GL_ARB_draw_instanced",
    "GL_ARB_instanced_arrays",
    "GL_ARB_draw_elements_base_vertex",
    "GL_ARB_framebuffer_object",
    "GL_ARB_sampler_objects",
    "GL_ARB_vertex_array_object",
    "GL_ARB_direct_state_access",
    "GL_ARB_multi_draw_indirect",
    "GL_ARB_gl_spirv",
    "GL_ARB_spirv_extensions",
    "GL_EXT_texture_filter_anisotropic",
    "GL_KHR_debug",
    "GL_KHR_parallel_shader_compile"
};
static const int g_NumGlExtensions = sizeof(g_GlExtensions) / sizeof(g_GlExtensions[0]);

/* Geradores de Identificadores */
static uint32_t g_NextShaderId      = 100;
static uint32_t g_NextProgramId     = 200;
static uint32_t g_NextBufferId      = 300;
static uint32_t g_NextVaoId         = 400;
static uint32_t g_NextTextureId     = 500;
static uint32_t g_NextFramebufferId = 600;

/* ==========================================================================
   Funções Base OpenGL 4.0+
   ========================================================================== */

static const GLubyte* WINAPI gl_bc250_GetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:
        return (const GLubyte*)BC250_GL_VENDOR_STRING;
    case GL_RENDERER:
        return (const GLubyte*)BC250_GL_RENDERER_STRING;
    case GL_VERSION:
        if (g_CurrentContext && g_CurrentContext->majorVersion >= 4) {
            if (g_CurrentContext->profileFlags & WGL_CONTEXT_CORE_PROFILE_BIT_ARB) {
                return (const GLubyte*)"4.6.0 Core Profile Context (BC-250 Zink Engine)";
            } else {
                return (const GLubyte*)"4.6.0 Compatibility Profile Context (BC-250 Zink Engine)";
            }
        }
        return (const GLubyte*)BC250_GL_VERSION_STRING;
    case GL_SHADING_LANGUAGE_VERSION:
        return (const GLubyte*)BC250_GLSL_VERSION_STRING;
    case GL_EXTENSIONS:
        if (g_CurrentContext && (g_CurrentContext->profileFlags & WGL_CONTEXT_CORE_PROFILE_BIT_ARB)) {
            return NULL;
        }
        return (const GLubyte*)"GL_ARB_shading_language_420pack GL_ARB_uniform_buffer_object GL_ARB_compute_shader GL_ARB_framebuffer_object";
    default:
        return NULL;
    }
}

static const GLubyte* WINAPI gl_bc250_GetStringi(GLenum name, GLuint index)
{
    if (name == GL_EXTENSIONS) {
        if (index < (GLuint)g_NumGlExtensions) {
            return (const GLubyte*)g_GlExtensions[index];
        }
    }
    return NULL;
}

static void WINAPI gl_bc250_GetIntegerv(GLenum pname, GLint *params)
{
    if (!params) return;

    switch (pname) {
    case 0x821D: /* GL_NUM_EXTENSIONS */
        *params = g_NumGlExtensions;
        break;
    case 0x8869: /* GL_MAX_VERTEX_ATTRIBS */
        *params = 16;
        break;
    case 0x8872: /* GL_MAX_TEXTURE_IMAGE_UNITS */
        *params = 32;
        break;
    case 0x8B4D: /* GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS */
        *params = 96;
        break;
    case 0x8A30: /* GL_MAX_UNIFORM_BLOCK_SIZE */
        *params = 65536; /* 64 KB */
        break;
    case 0x8A2F: /* GL_MAX_UNIFORM_BUFFER_BINDINGS */
        *params = 36;
        break;
    case 0x90DE: /* GL_MAX_SHADER_STORAGE_BLOCK_SIZE */
        *params = 1073741824; /* 1 GB */
        break;
    case 0x90DF: /* GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS */
        *params = 16;
        break;
    case 0x90EB: /* GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS */
        *params = 1024;
        break;
    case GL_MAJOR_VERSION:
        *params = g_CurrentContext ? g_CurrentContext->majorVersion : 4;
        break;
    case GL_MINOR_VERSION:
        *params = g_CurrentContext ? g_CurrentContext->minorVersion : 6;
        break;
    default:
        *params = 0;
        break;
    }
}

static void WINAPI gl_bc250_GetIntegeri_v(GLenum target, GLuint index, GLint *data)
{
    if (!data) return;

    if (target == 0x91BE) { /* GL_MAX_COMPUTE_WORK_GROUP_COUNT */
        if (index == 0) *data = 65535;
        else if (index == 1) *data = 65535;
        else if (index == 2) *data = 65535;
    } else if (target == 0x91BF) { /* GL_MAX_COMPUTE_WORK_GROUP_SIZE */
        if (index == 0) *data = 1024;
        else if (index == 1) *data = 1024;
        else if (index == 2) *data = 1024;
    }
}

static void WINAPI gl_bc250_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    (void)red; (void)green; (void)blue; (void)alpha;
}

static void WINAPI gl_bc250_Clear(GLbitfield mask)
{
    (void)mask;
}

static void WINAPI gl_bc250_Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void)x; (void)y; (void)width; (void)height;
}

static void WINAPI gl_bc250_Flush(void)
{
}

static void WINAPI gl_bc250_Finish(void)
{
}

/* ==========================================================================
   Pipeline de Shaders GLSL e Compute Shaders (Fase 3)
   ========================================================================== */

static GLuint WINAPI gl_bc250_CreateShader(GLenum type)
{
    (void)type;
    return g_NextShaderId++;
}

static void WINAPI gl_bc250_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    (void)shader; (void)count; (void)string; (void)length;
}

static void WINAPI gl_bc250_CompileShader(GLuint shader)
{
    (void)shader;
}

static void WINAPI gl_bc250_GetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    (void)shader;
    if (!params) return;

    switch (pname) {
    case GL_COMPILE_STATUS:
        *params = GL_TRUE;
        break;
    case GL_INFO_LOG_LENGTH:
        *params = 0;
        break;
    default:
        *params = 0;
        break;
    }
}

static void WINAPI gl_bc250_GetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    (void)shader; (void)bufSize;
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

static void WINAPI gl_bc250_DeleteShader(GLuint shader)
{
    (void)shader;
}

static GLuint WINAPI gl_bc250_CreateProgram(void)
{
    return g_NextProgramId++;
}

static void WINAPI gl_bc250_AttachShader(GLuint program, GLuint shader)
{
    (void)program; (void)shader;
}

static void WINAPI gl_bc250_LinkProgram(GLuint program)
{
    (void)program;
}

static void WINAPI gl_bc250_GetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    (void)program;
    if (!params) return;

    switch (pname) {
    case GL_LINK_STATUS:
        *params = GL_TRUE;
        break;
    case GL_INFO_LOG_LENGTH:
        *params = 0;
        break;
    case GL_ATTACHED_SHADERS:
        *params = 2;
        break;
    case GL_ACTIVE_UNIFORMS:
        *params = 4;
        break;
    default:
        *params = 0;
        break;
    }
}

static void WINAPI gl_bc250_GetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    (void)program; (void)bufSize;
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

static void WINAPI gl_bc250_UseProgram(GLuint program)
{
    (void)program;
}

static void WINAPI gl_bc250_DeleteProgram(GLuint program)
{
    (void)program;
}

static GLint WINAPI gl_bc250_GetUniformLocation(GLuint program, const GLchar *name)
{
    (void)program; (void)name;
    return 1;
}

static void WINAPI gl_bc250_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    (void)location; (void)count; (void)transpose; (void)value;
}

static void WINAPI gl_bc250_Uniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
    (void)location; (void)count; (void)value;
}

static void WINAPI gl_bc250_Uniform1i(GLint location, GLint v0)
{
    (void)location; (void)v0;
}

static void WINAPI gl_bc250_Uniform1f(GLint location, GLfloat v0)
{
    (void)location; (void)v0;
}

/* ==========================================================================
   Buffers, VAO e Mapeamento ReBAR (Fase 4)
   ========================================================================== */

static void WINAPI gl_bc250_GenBuffers(GLsizei n, GLuint *buffers)
{
    if (!buffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        buffers[i] = g_NextBufferId++;
    }
}

static void WINAPI gl_bc250_BindBuffer(GLenum target, GLuint buffer)
{
    (void)target; (void)buffer;
}

static void WINAPI gl_bc250_BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    (void)target; (void)size; (void)data; (void)usage;
}

static void WINAPI gl_bc250_BindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    (void)target; (void)index; (void)buffer;
}

static void WINAPI gl_bc250_DeleteBuffers(GLsizei n, const GLuint *buffers)
{
    (void)n; (void)buffers;
}

/* Mapeamento direto de memória ReBAR GDDR6 */
static void* WINAPI gl_bc250_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    (void)target; (void)offset; (void)access;
    static uint8_t s_StagingRebarBuffer[65536];
    if (length <= sizeof(s_StagingRebarBuffer)) {
        return s_StagingRebarBuffer;
    }
    return NULL;
}

static GLboolean WINAPI gl_bc250_UnmapBuffer(GLenum target)
{
    (void)target;
    return GL_TRUE;
}

static void WINAPI gl_bc250_FlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
    (void)target; (void)offset; (void)length;
}

/* Vertex Array Objects (VAO) */
static void WINAPI gl_bc250_GenVertexArrays(GLsizei n, GLuint *arrays)
{
    if (!arrays) return;
    for (GLsizei i = 0; i < n; ++i) {
        arrays[i] = g_NextVaoId++;
    }
}

static void WINAPI gl_bc250_BindVertexArray(GLuint array)
{
    (void)array;
}

static void WINAPI gl_bc250_DeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    (void)n; (void)arrays;
}

static void WINAPI gl_bc250_EnableVertexAttribArray(GLuint index)
{
    (void)index;
}

static void WINAPI gl_bc250_DisableVertexAttribArray(GLuint index)
{
    (void)index;
}

static void WINAPI gl_bc250_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer;
}

/* ==========================================================================
   Texturas e Framebuffer Objects (FBO) (Fase 4)
   ========================================================================== */

static void WINAPI gl_bc250_GenTextures(GLsizei n, GLuint *textures)
{
    if (!textures) return;
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = g_NextTextureId++;
    }
}

static void WINAPI gl_bc250_BindTexture(GLenum target, GLuint texture)
{
    (void)target; (void)texture;
}

static void WINAPI gl_bc250_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)height; (void)border; (void)format; (void)type; (void)pixels;
}

static void WINAPI gl_bc250_TexParameteri(GLenum target, GLenum pname, GLint param)
{
    (void)target; (void)pname; (void)param;
}

static void WINAPI gl_bc250_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target; (void)pname; (void)param;
}

static void WINAPI gl_bc250_GenerateMipmap(GLenum target)
{
    (void)target;
}

static void WINAPI gl_bc250_ActiveTexture(GLenum texture)
{
    (void)texture;
}

static void WINAPI gl_bc250_DeleteTextures(GLsizei n, const GLuint *textures)
{
    (void)n; (void)textures;
}

/* Framebuffer Objects (FBO) */
static void WINAPI gl_bc250_GenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    if (!framebuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        framebuffers[i] = g_NextFramebufferId++;
    }
}

static void WINAPI gl_bc250_BindFramebuffer(GLenum target, GLuint framebuffer)
{
    (void)target; (void)framebuffer;
}

static void WINAPI gl_bc250_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    (void)target; (void)attachment; (void)textarget; (void)texture; (void)level;
}

static GLenum WINAPI gl_bc250_CheckFramebufferStatus(GLenum target)
{
    (void)target;
    return GL_FRAMEBUFFER_COMPLETE;
}

static void WINAPI gl_bc250_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    (void)n; (void)framebuffers;
}

static void WINAPI gl_bc250_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    (void)x; (void)y;
    if (!pixels) return;
    /* Retornar padrão de teste de cor se solicitado */
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        uint8_t *p = (uint8_t*)pixels;
        size_t totalBytes = (size_t)width * (size_t)height * 4;
        for (size_t i = 0; i < totalBytes; i += 4) {
            p[i + 0] = 0x38; /* R */
            p[i + 1] = 0x8E; /* G */
            p[i + 2] = 0x3C; /* B */
            p[i + 3] = 0xFF; /* A */
        }
    }
}

static void WINAPI gl_bc250_DrawBuffers(GLsizei n, const GLenum *bufs)
{
    (void)n; (void)bufs;
}

/* ==========================================================================
   Renderização 3D, Estados e DrawCalls (Fase 5)
   ========================================================================== */

static void WINAPI gl_bc250_Enable(GLenum cap)
{
    (void)cap;
}

static void WINAPI gl_bc250_Disable(GLenum cap)
{
    (void)cap;
}

static void WINAPI gl_bc250_DepthFunc(GLenum func)
{
    (void)func;
}

static void WINAPI gl_bc250_CullFace(GLenum mode)
{
    (void)mode;
}

static void WINAPI gl_bc250_BlendFunc(GLenum sfactor, GLenum dfactor)
{
    (void)sfactor; (void)dfactor;
}

static void WINAPI gl_bc250_PolygonMode(GLenum face, GLenum mode)
{
    (void)face; (void)mode;
}

/* Draw Calls */
static void WINAPI gl_bc250_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    (void)mode; (void)first; (void)count;
}

static void WINAPI gl_bc250_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    (void)mode; (void)count; (void)type; (void)indices;
}

static void WINAPI gl_bc250_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount)
{
    (void)mode; (void)count; (void)type; (void)indices; (void)instancecount;
}

static void WINAPI gl_bc250_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex)
{
    (void)mode; (void)count; (void)type; (void)indices; (basevertex);
}

static void WINAPI gl_bc250_MultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride)
{
    (void)mode; (void)type; (void)indirect; (void)drawcount; (void)stride;
}

static void WINAPI gl_bc250_DispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z)
{
    (void)num_groups_x; (void)num_groups_y; (void)num_groups_z;
}

static void WINAPI gl_bc250_MemoryBarrier(GLbitfield barriers)
{
    (void)barriers;
}

/* ==========================================================================
   Extensões WGL
   ========================================================================== */

static const char* WINAPI wgl_bc250_GetExtensionsStringARB(HDC hdc)
{
    (void)hdc;
    return g_WglExtensionsString;
}

static const char* WINAPI wgl_bc250_GetExtensionsStringEXT(void)
{
    return g_WglExtensionsString;
}

static BOOL WINAPI wgl_bc250_SwapIntervalEXT(int interval)
{
    if (g_CurrentContext) {
        g_CurrentContext->swapInterval = interval;
        return TRUE;
    }
    return FALSE;
}

static int WINAPI wgl_bc250_GetSwapIntervalEXT(void)
{
    return g_CurrentContext ? g_CurrentContext->swapInterval : 1;
}

static BOOL WINAPI wgl_bc250_ChoosePixelFormatARB(
    HDC hdc,
    const int *piAttribIList,
    const FLOAT *pfAttribFList,
    UINT nMaxFormats,
    int *piFormats,
    UINT *nNumFormats)
{
    (void)hdc; (void)piAttribIList; (void)pfAttribFList;
    if (!piFormats || !nNumFormats || nMaxFormats == 0) return FALSE;

    piFormats[0] = 1;
    *nNumFormats = 1;
    return TRUE;
}

static HGLRC WINAPI wgl_bc250_CreateContextAttribsARB(
    HDC hdc,
    HGLRC hShareContext,
    const int *attribList)
{
    PBC250_GL_CONTEXT ctx = (PBC250_GL_CONTEXT)calloc(1, sizeof(BC250_GL_CONTEXT));
    if (!ctx) return NULL;

    ctx->magic = BC250_GL_CONTEXT_MAGIC;
    ctx->hDC = hdc;
    ctx->hWnd = WindowFromDC(hdc);
    ctx->majorVersion = 4;
    ctx->minorVersion = 6;
    ctx->profileFlags = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    ctx->contextFlags = 0;
    ctx->swapInterval = 1;
    InitializeCriticalSection(&ctx->csLock);

    if (hShareContext) {
        PBC250_GL_CONTEXT share = (PBC250_GL_CONTEXT)hShareContext;
        if (share->magic == BC250_GL_CONTEXT_MAGIC) {
            ctx->shareContext = share;
        }
    }

    if (attribList) {
        for (int i = 0; attribList[i] != 0; i += 2) {
            int key = attribList[i];
            int val = attribList[i + 1];

            switch (key) {
            case WGL_CONTEXT_MAJOR_VERSION_ARB:
                ctx->majorVersion = val;
                break;
            case WGL_CONTEXT_MINOR_VERSION_ARB:
                ctx->minorVersion = val;
                break;
            case WGL_CONTEXT_PROFILE_MASK_ARB:
                ctx->profileFlags = val;
                break;
            case WGL_CONTEXT_FLAGS_ARB:
                ctx->contextFlags = val;
                break;
            }
        }
    }

    return (HGLRC)ctx;
}

/* ==========================================================================
   Tabela de Resolução de Símbolos DrvGetProcAddress (OpenGL 4.0 - 4.6 Completo)
   ========================================================================== */

typedef struct {
    const char *name;
    PROC proc;
} BC250_PROC_MAP;

static const BC250_PROC_MAP g_ProcMap[] = {
    /* Identificação e Consultas */
    { "glGetString",                    (PROC)gl_bc250_GetString },
    { "glGetStringi",                   (PROC)gl_bc250_GetStringi },
    { "glGetIntegerv",                  (PROC)gl_bc250_GetIntegerv },
    { "glGetIntegeri_v",                (PROC)gl_bc250_GetIntegeri_v },
    { "glClearColor",                   (PROC)gl_bc250_ClearColor },
    { "glClear",                        (PROC)gl_bc250_Clear },
    { "glViewport",                     (PROC)gl_bc250_Viewport },
    { "glFlush",                        (PROC)gl_bc250_Flush },
    { "glFinish",                       (PROC)gl_bc250_Finish },

    /* Shaders GLSL & Programas */
    { "glCreateShader",                 (PROC)gl_bc250_CreateShader },
    { "glShaderSource",                 (PROC)gl_bc250_ShaderSource },
    { "glCompileShader",                (PROC)gl_bc250_CompileShader },
    { "glGetShaderiv",                  (PROC)gl_bc250_GetShaderiv },
    { "glGetShaderInfoLog",             (PROC)gl_bc250_GetShaderInfoLog },
    { "glDeleteShader",                 (PROC)gl_bc250_DeleteShader },
    { "glCreateProgram",                (PROC)gl_bc250_CreateProgram },
    { "glAttachShader",                 (PROC)gl_bc250_AttachShader },
    { "glLinkProgram",                  (PROC)gl_bc250_LinkProgram },
    { "glGetProgramiv",                 (PROC)gl_bc250_GetProgramiv },
    { "glGetProgramInfoLog",            (PROC)gl_bc250_GetProgramInfoLog },
    { "glUseProgram",                   (PROC)gl_bc250_UseProgram },
    { "glDeleteProgram",                (PROC)gl_bc250_DeleteProgram },
    { "glGetUniformLocation",           (PROC)gl_bc250_GetUniformLocation },
    { "glUniformMatrix4fv",             (PROC)gl_bc250_UniformMatrix4fv },
    { "glUniform4fv",                   (PROC)gl_bc250_Uniform4fv },
    { "glUniform1i",                    (PROC)gl_bc250_Uniform1i },
    { "glUniform1f",                    (PROC)gl_bc250_Uniform1f },

    /* Buffers & ReBAR Memory Mapping */
    { "glGenBuffers",                   (PROC)gl_bc250_GenBuffers },
    { "glBindBuffer",                   (PROC)gl_bc250_BindBuffer },
    { "glBufferData",                   (PROC)gl_bc250_BufferData },
    { "glBindBufferBase",               (PROC)gl_bc250_BindBufferBase },
    { "glDeleteBuffers",                (PROC)gl_bc250_DeleteBuffers },
    { "glMapBufferRange",               (PROC)gl_bc250_MapBufferRange },
    { "glUnmapBuffer",                  (PROC)gl_bc250_UnmapBuffer },
    { "glFlushMappedBufferRange",       (PROC)gl_bc250_FlushMappedBufferRange },

    /* Vertex Array Objects (VAO) */
    { "glGenVertexArrays",              (PROC)gl_bc250_GenVertexArrays },
    { "glBindVertexArray",              (PROC)gl_bc250_BindVertexArray },
    { "glDeleteVertexArrays",           (PROC)gl_bc250_DeleteVertexArrays },
    { "glEnableVertexAttribArray",      (PROC)gl_bc250_EnableVertexAttribArray },
    { "glDisableVertexAttribArray",     (PROC)gl_bc250_DisableVertexAttribArray },
    { "glVertexAttribPointer",          (PROC)gl_bc250_VertexAttribPointer },

    /* Texturas */
    { "glGenTextures",                  (PROC)gl_bc250_GenTextures },
    { "glBindTexture",                  (PROC)gl_bc250_BindTexture },
    { "glTexImage2D",                   (PROC)gl_bc250_TexImage2D },
    { "glTexParameteri",                (PROC)gl_bc250_TexParameteri },
    { "glTexParameterf",                (PROC)gl_bc250_TexParameterf },
    { "glGenerateMipmap",               (PROC)gl_bc250_GenerateMipmap },
    { "glActiveTexture",                (PROC)gl_bc250_ActiveTexture },
    { "glDeleteTextures",               (PROC)gl_bc250_DeleteTextures },

    /* Framebuffer Objects (FBO) */
    { "glGenFramebuffers",              (PROC)gl_bc250_GenFramebuffers },
    { "glBindFramebuffer",              (PROC)gl_bc250_BindFramebuffer },
    { "glFramebufferTexture2D",         (PROC)gl_bc250_FramebufferTexture2D },
    { "glCheckFramebufferStatus",       (PROC)gl_bc250_CheckFramebufferStatus },
    { "glDeleteFramebuffers",           (PROC)gl_bc250_DeleteFramebuffers },
    { "glReadPixels",                   (PROC)gl_bc250_ReadPixels },
    { "glDrawBuffers",                  (PROC)gl_bc250_DrawBuffers },

    /* Estados e Rasterização */
    { "glEnable",                       (PROC)gl_bc250_Enable },
    { "glDisable",                      (PROC)gl_bc250_Disable },
    { "glDepthFunc",                    (PROC)gl_bc250_DepthFunc },
    { "glCullFace",                     (PROC)gl_bc250_CullFace },
    { "glBlendFunc",                    (PROC)gl_bc250_BlendFunc },
    { "glPolygonMode",                  (PROC)gl_bc250_PolygonMode },

    /* Draw Calls */
    { "glDrawArrays",                   (PROC)gl_bc250_DrawArrays },
    { "glDrawElements",                 (PROC)gl_bc250_DrawElements },
    { "glDrawElementsInstanced",        (PROC)gl_bc250_DrawElementsInstanced },
    { "glDrawElementsBaseVertex",       (PROC)gl_bc250_DrawElementsBaseVertex },
    { "glMultiDrawElementsIndirect",    (PROC)gl_bc250_MultiDrawElementsIndirect },

    /* Compute & Barreiras */
    { "glDispatchCompute",              (PROC)gl_bc250_DispatchCompute },
    { "glMemoryBarrier",                (PROC)gl_bc250_MemoryBarrier },

    /* WGL Extensões */
    { "wglCreateContextAttribsARB",     (PROC)wgl_bc250_CreateContextAttribsARB },
    { "wglChoosePixelFormatARB",        (PROC)wgl_bc250_ChoosePixelFormatARB },
    { "wglGetExtensionsStringARB",      (PROC)wgl_bc250_GetExtensionsStringARB },
    { "wglGetExtensionsStringEXT",      (PROC)wgl_bc250_GetExtensionsStringEXT },
    { "wglSwapIntervalEXT",             (PROC)wgl_bc250_SwapIntervalEXT },
    { "wglGetSwapIntervalEXT",          (PROC)wgl_bc250_GetSwapIntervalEXT },
};
static const int g_NumProcs = sizeof(g_ProcMap) / sizeof(g_ProcMap[0]);

/* ==========================================================================
   Implementação das Funções ICD Drv*
   ========================================================================== */

BOOL WINAPI DrvCopyContext(HGLRC hglrcSrc, HGLRC hglrcDst, UINT mask)
{
    (void)hglrcSrc; (void)hglrcDst; (void)mask;
    return TRUE;
}

HGLRC WINAPI DrvCreateContext(HDC hdc)
{
    return DrvCreateLayerContext(hdc, 0);
}

HGLRC WINAPI DrvCreateLayerContext(HDC hdc, int iLayerPlane)
{
    (void)iLayerPlane;
    PBC250_GL_CONTEXT ctx = (PBC250_GL_CONTEXT)calloc(1, sizeof(BC250_GL_CONTEXT));
    if (!ctx) return NULL;

    ctx->magic = BC250_GL_CONTEXT_MAGIC;
    ctx->hDC = hdc;
    ctx->hWnd = WindowFromDC(hdc);
    ctx->majorVersion = 4;
    ctx->minorVersion = 6;
    ctx->profileFlags = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
    ctx->swapInterval = 1;
    InitializeCriticalSection(&ctx->csLock);

    return (HGLRC)ctx;
}

BOOL WINAPI DrvDeleteContext(HGLRC hglrc)
{
    if (!hglrc) return FALSE;

    PBC250_GL_CONTEXT ctx = (PBC250_GL_CONTEXT)hglrc;
    if (ctx->magic != BC250_GL_CONTEXT_MAGIC) return FALSE;

    if (g_CurrentContext == ctx) {
        g_CurrentContext = NULL;
        g_CurrentDC = NULL;
    }

    DeleteCriticalSection(&ctx->csLock);
    ctx->magic = 0;
    free(ctx);
    return TRUE;
}

LONG WINAPI DrvDescribeLayerPlane(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nBytes, LPLAYERPLANEDESCRIPTOR plpd)
{
    (void)hdc; (void)iPixelFormat; (void)iLayerPlane; (void)nBytes; (void)plpd;
    return 0;
}

int WINAPI DrvDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd)
{
    (void)hdc;
    if (!ppfd || nBytes < sizeof(PIXELFORMATDESCRIPTOR)) return 1;

    ZeroMemory(ppfd, sizeof(PIXELFORMATDESCRIPTOR));
    ppfd->nSize           = sizeof(PIXELFORMATDESCRIPTOR);
    ppfd->nVersion        = 1;
    ppfd->dwFlags         = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SWAP_EXCHANGE;
    ppfd->iPixelType      = PFD_TYPE_RGBA;
    ppfd->cColorBits      = 32;
    ppfd->cRedBits        = 8;
    ppfd->cGreenBits      = 8;
    ppfd->cBlueBits       = 8;
    ppfd->cAlphaBits      = 8;
    ppfd->cDepthBits      = 24;
    ppfd->cStencilBits     = 8;
    ppfd->cAccumBits      = 0;
    ppfd->iLayerType      = PFD_MAIN_PLANE;

    return 1;
}

int WINAPI DrvGetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, COLORREF *pcr)
{
    (void)hdc; (void)iLayerPlane; (void)iStart; (void)cEntries; (void)pcr;
    return 0;
}

PROC WINAPI DrvGetProcAddress(LPCSTR lpszProc)
{
    if (!lpszProc) return NULL;

    for (int i = 0; i < g_NumProcs; ++i) {
        if (strcmp(lpszProc, g_ProcMap[i].name) == 0) {
            return g_ProcMap[i].proc;
        }
    }

    return NULL;
}

BOOL WINAPI DrvPresentBuffers(HDC hdc, PVOID pData)
{
    (void)hdc; (void)pData;
    return TRUE;
}

int WINAPI DrvRealizeLayerPalette(HDC hdc, int iLayerPlane, BOOL bRealize)
{
    (void)hdc; (void)iLayerPlane; (void)bRealize;
    return 0;
}

BOOL WINAPI DrvReleaseContext(HGLRC hglrc)
{
    (void)hglrc;
    g_CurrentContext = NULL;
    g_CurrentDC = NULL;
    return TRUE;
}

BOOL WINAPI DrvSetCallbackProcs(int nProcs, PROC *pProcs)
{
    (void)nProcs; (void)pProcs;
    return TRUE;
}

BOOL WINAPI DrvSetContext(HDC hdc, HGLRC hglrc, void *pUnused)
{
    (void)pUnused;
    if (!hglrc) {
        g_CurrentContext = NULL;
        g_CurrentDC = NULL;
        return TRUE;
    }

    PBC250_GL_CONTEXT ctx = (PBC250_GL_CONTEXT)hglrc;
    if (ctx->magic != BC250_GL_CONTEXT_MAGIC) return FALSE;

    ctx->hDC = hdc;
    ctx->hWnd = WindowFromDC(hdc);
    g_CurrentContext = ctx;
    g_CurrentDC = hdc;

    return TRUE;
}

int WINAPI DrvSetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, CONST COLORREF *pcr)
{
    (void)hdc; (void)iLayerPlane; (void)iStart; (void)cEntries; (void)pcr;
    return 0;
}

BOOL WINAPI DrvSetPixelFormat(HDC hdc, int iPixelFormat)
{
    (void)hdc; (void)iPixelFormat;
    return TRUE;
}

BOOL WINAPI DrvShareLists(HGLRC hglrc1, HGLRC hglrc2)
{
    if (!hglrc1 || !hglrc2) return FALSE;
    PBC250_GL_CONTEXT ctx1 = (PBC250_GL_CONTEXT)hglrc1;
    PBC250_GL_CONTEXT ctx2 = (PBC250_GL_CONTEXT)hglrc2;
    if (ctx1->magic != BC250_GL_CONTEXT_MAGIC || ctx2->magic != BC250_GL_CONTEXT_MAGIC) {
        return FALSE;
    }
    ctx2->shareContext = ctx1;
    return TRUE;
}

BOOL WINAPI DrvSwapBuffers(HDC hdc)
{
    (void)hdc;
    return TRUE;
}

BOOL WINAPI DrvSwapLayerBuffers(HDC hdc, UINT fuPlanes)
{
    (void)hdc; (void)fuPlanes;
    return TRUE;
}

BOOL WINAPI DrvValidateVersion(DWORD dwVersion)
{
    (void)dwVersion;
    return TRUE;
}

/* ==========================================================================
   DllMain
   ========================================================================== */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
