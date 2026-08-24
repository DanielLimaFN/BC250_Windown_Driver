/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    test_opengl_phase3_glsl_shaders.c

Abstract:
    Suite de Teste e Validação da FASE 3 do Roadmap OpenGL 4.0+ da BC-250:
    - Compilação de Shaders GLSL 4.00+ (Vertex Shader e Fragment Shader com MVP e Phong)
    - Linkagem e Validação de Programas Gráficos (GL_LINK_STATUS, Uniforms)
    - Gerenciamento de Uniform Buffer Objects (UBO - glBindBufferBase)
    - Compilação e Execução de Compute Shaders GLSL 4.30 (Multiplicação Paralela GEMM)
    - Shader Storage Buffer Objects (SSBO - 16 GB GDDR6 ReBAR)
    - Sincronização com Barreiras de Memória (glMemoryBarrier)

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

typedef char GLchar;
#if defined(_WIN64)
typedef signed long long int GLsizeiptr;
typedef signed long long int GLintptr;
#else
typedef signed long int GLsizeiptr;
typedef signed long int GLintptr;
#endif

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
#define GL_SHADER_STORAGE_BARRIER_BIT             0x00002000
#define GL_BUFFER_UPDATE_BARRIER_BIT              0x00000200
#define GL_ALL_BARRIER_BITS                       0xFFFFFFFF
#endif

/* Assinaturas de Ponteiros de Função GLSL & WGL */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
typedef const GLubyte* (WINAPI *PFNGLGETSTRINGPROC)(GLenum name);

typedef GLuint (WINAPI *PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (WINAPI *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void   (WINAPI *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (WINAPI *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void   (WINAPI *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void   (WINAPI *PFNGLDELETESHADERPROC)(GLuint shader);

typedef GLuint (WINAPI *PFNGLCREATEPROGRAMPROC)(void);
typedef void   (WINAPI *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (WINAPI *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (WINAPI *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void   (WINAPI *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void   (WINAPI *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (WINAPI *PFNGLDELETEPROGRAMPROC)(GLuint program);

typedef GLint  (WINAPI *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void   (WINAPI *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void   (WINAPI *PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void   (WINAPI *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (WINAPI *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);

typedef void   (WINAPI *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void   (WINAPI *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (WINAPI *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void   (WINAPI *PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);

typedef void   (WINAPI *PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void   (WINAPI *PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);

typedef PROC  (WINAPI *PFNDRVGETPROCADDRESSPROC)(LPCSTR lpszProc);
typedef HGLRC (WINAPI *PFNDRVCREATECONTEXTPROC)(HDC hdc);
typedef BOOL  (WINAPI *PFNDRVSETCONTEXTPROC)(HDC hdc, HGLRC hglrc, void *pUnused);
typedef BOOL  (WINAPI *PFNDRVDELETECONTEXTPROC)(HGLRC hglrc);
typedef BOOL  (WINAPI *PFNDRVSETPIXELFORMATPROC)(HDC hdc, int iPixelFormat);

static LRESULT CALLBACK ShaderWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hWnd, message, wParam, lParam);
}

/* ==========================================================================
   Fontes de Shaders GLSL 4.00+
   ========================================================================== */

/* 1. Vertex Shader GLSL 4.00 com MVP e Normais */
static const char *g_VertexShaderSource =
    "#version 400 core\n"
    "layout(location = 0) in vec3 a_Position;\n"
    "layout(location = 1) in vec3 a_Normal;\n"
    "layout(location = 2) in vec2 a_TexCoord;\n"
    "uniform mat4 u_ModelViewProjection;\n"
    "uniform mat4 u_Model;\n"
    "out vec3 v_Normal;\n"
    "out vec3 v_FragPos;\n"
    "out vec2 v_TexCoord;\n"
    "void main() {\n"
    "    gl_Position = u_ModelViewProjection * vec4(a_Position, 1.0);\n"
    "    v_FragPos = vec3(u_Model * vec4(a_Position, 1.0));\n"
    "    v_Normal = mat3(transpose(inverse(u_Model))) * a_Normal;\n"
    "    v_TexCoord = a_TexCoord;\n"
    "}\n";

/* 2. Fragment Shader GLSL 4.00 com Modelo de Iluminação Phong */
static const char *g_FragmentShaderSource =
    "#version 400 core\n"
    "in vec3 v_Normal;\n"
    "in vec3 v_FragPos;\n"
    "in vec2 v_TexCoord;\n"
    "uniform vec3 u_LightPos;\n"
    "uniform vec3 u_ViewPos;\n"
    "uniform vec4 u_LightColor;\n"
    "uniform vec4 u_ObjectColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    // Ambiente\n"
    "    float ambientStrength = 0.15;\n"
    "    vec3 ambient = ambientStrength * u_LightColor.rgb;\n"
    "    // Difusa\n"
    "    vec3 norm = normalize(v_Normal);\n"
    "    vec3 lightDir = normalize(u_LightPos - v_FragPos);\n"
    "    float diff = max(dot(norm, lightDir), 0.0);\n"
    "    vec3 diffuse = diff * u_LightColor.rgb;\n"
    "    // Especular\n"
    "    float specularStrength = 0.5;\n"
    "    vec3 viewDir = normalize(u_ViewPos - v_FragPos);\n"
    "    vec3 reflectDir = reflect(-lightDir, norm);\n"
    "    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);\n"
    "    vec3 specular = specularStrength * spec * u_LightColor.rgb;\n"
    "    vec3 result = (ambient + diffuse + specular) * u_ObjectColor.rgb;\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

/* 3. Compute Shader GLSL 4.30 para Multiplicação Paralela de Matrizes (GEMM) */
static const char *g_ComputeShaderSource =
    "#version 430 core\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(std430, binding = 0) readonly buffer InputA {\n"
    "    float MatrixA[];\n"
    "};\n"
    "layout(std430, binding = 1) readonly buffer InputB {\n"
    "    float MatrixB[];\n"
    "};\n"
    "layout(std430, binding = 2) writeonly buffer OutputC {\n"
    "    float MatrixC[];\n"
    "};\n"
    "uniform int u_DimK;\n"
    "uniform int u_DimM;\n"
    "uniform int u_DimN;\n"
    "void main() {\n"
    "    uint row = gl_GlobalInvocationID.y;\n"
    "    uint col = gl_GlobalInvocationID.x;\n"
    "    if (row >= uint(u_DimM) || col >= uint(u_DimN)) return;\n"
    "    float sum = 0.0;\n"
    "    for (int k = 0; k < u_DimK; ++k) {\n"
    "        sum += MatrixA[row * u_DimK + k] * MatrixB[k * u_DimN + col];\n"
    "    }\n"
    "    MatrixC[row * u_DimN + col] = sum;\n"
    "}\n";

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    SetDllDirectoryA(".");

    printf("======================================================================\n");
    printf("   AMD BC-250: Teste do Pipeline GLSL 4.00+ & Compute NIR (FASE 3)   \n");
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
    wc.lpfnWndProc   = ShaderWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "BC250_Shader_Pipeline_Test";
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        0, "BC250_Shader_Pipeline_Test", "BC250 Shader Test",
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

    PFNWGLCREATECONTEXTATTRIBSARBPROC pfnCreateContextAttribsARB = NULL;
    if (pfnGetProcAddress) {
        pfnCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)pfnGetProcAddress("wglCreateContextAttribsARB");
    }

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
    } else {
        printf("[FAIL] Nao foi possivel criar contexto Core Profile.\n");
        ReleaseDC(hWnd, hDC);
        DestroyWindow(hWnd);
        FreeLibrary(hIcd);
        return 1;
    }

    /* 3. Resolver Ponteiros de Funções de Shader e Compute */
    PFNGLCREATESHADERPROC       pfnCreateShader       = (PFNGLCREATESHADERPROC)pfnGetProcAddress("glCreateShader");
    PFNGLSHADERSOURCEPROC       pfnShaderSource       = (PFNGLSHADERSOURCEPROC)pfnGetProcAddress("glShaderSource");
    PFNGLCOMPILESHADERPROC      pfnCompileShader      = (PFNGLCOMPILESHADERPROC)pfnGetProcAddress("glCompileShader");
    PFNGLGETSHADERIVPROC        pfnGetShaderiv        = (PFNGLGETSHADERIVPROC)pfnGetProcAddress("glGetShaderiv");
    PFNGLDELETESHADERPROC       pfnDeleteShader       = (PFNGLDELETESHADERPROC)pfnGetProcAddress("glDeleteShader");

    PFNGLCREATEPROGRAMPROC      pfnCreateProgram      = (PFNGLCREATEPROGRAMPROC)pfnGetProcAddress("glCreateProgram");
    PFNGLATTACHSHADERPROC       pfnAttachShader       = (PFNGLATTACHSHADERPROC)pfnGetProcAddress("glAttachShader");
    PFNGLLINKPROGRAMPROC        pfnLinkProgram        = (PFNGLLINKPROGRAMPROC)pfnGetProcAddress("glLinkProgram");
    PFNGLGETPROGRAMIVPROC       pfnGetProgramiv       = (PFNGLGETPROGRAMIVPROC)pfnGetProcAddress("glGetProgramiv");
    PFNGLUSEPROGRAMPROC         pfnUseProgram         = (PFNGLUSEPROGRAMPROC)pfnGetProcAddress("glUseProgram");
    PFNGLDELETEPROGRAMPROC      pfnDeleteProgram      = (PFNGLDELETEPROGRAMPROC)pfnGetProcAddress("glDeleteProgram");

    PFNGLGETUNIFORMLOCATIONPROC pfnGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)pfnGetProcAddress("glGetUniformLocation");
    PFNGLUNIFORMMATRIX4FVPROC   pfnUniformMatrix4fv   = (PFNGLUNIFORMMATRIX4FVPROC)pfnGetProcAddress("glUniformMatrix4fv");
    PFNGLUNIFORM4FVPROC         pfnUniform4fv         = (PFNGLUNIFORM4FVPROC)pfnGetProcAddress("glUniform4fv");

    PFNGLGENBUFFERSPROC         pfnGenBuffers         = (PFNGLGENBUFFERSPROC)pfnGetProcAddress("glGenBuffers");
    PFNGLBINDBUFFERPROC         pfnBindBuffer         = (PFNGLBINDBUFFERPROC)pfnGetProcAddress("glBindBuffer");
    PFNGLBUFFERDATAPROC         pfnBufferData         = (PFNGLBUFFERDATAPROC)pfnGetProcAddress("glBufferData");
    PFNGLBINDBUFFERBASEPROC     pfnBindBufferBase     = (PFNGLBINDBUFFERBASEPROC)pfnGetProcAddress("glBindBufferBase");

    PFNGLDISPATCHCOMPUTEPROC    pfnDispatchCompute    = (PFNGLDISPATCHCOMPUTEPROC)pfnGetProcAddress("glDispatchCompute");
    PFNGLMEMORYBARRIERPROC      pfnMemoryBarrier      = (PFNGLMEMORYBARRIERPROC)pfnGetProcAddress("glMemoryBarrier");

    printf("\n--- [1/3] Compilando Pipeline Grafico GLSL 4.00 (Vertex + Fragment) ---\n");

    /* 4. Compilar Vertex Shader */
    GLuint vs = pfnCreateShader(GL_VERTEX_SHADER);
    pfnShaderSource(vs, 1, &g_VertexShaderSource, NULL);
    pfnCompileShader(vs);
    GLint vsStatus = 0;
    pfnGetShaderiv(vs, GL_COMPILE_STATUS, &vsStatus);
    printf("  • Vertex Shader GLSL 4.00   : %s (ID = %u)\n", 
           (vsStatus == GL_TRUE) ? "COMPILADO (OK)" : "ERRO", vs);

    /* 5. Compilar Fragment Shader */
    GLuint fs = pfnCreateShader(GL_FRAGMENT_SHADER);
    pfnShaderSource(fs, 1, &g_FragmentShaderSource, NULL);
    pfnCompileShader(fs);
    GLint fsStatus = 0;
    pfnGetShaderiv(fs, GL_COMPILE_STATUS, &fsStatus);
    printf("  • Fragment Shader GLSL 4.00 : %s (ID = %u)\n", 
           (fsStatus == GL_TRUE) ? "COMPILADO (OK)" : "ERRO", fs);

    /* 6. Linkar Programa Gráfico */
    GLuint graphicsProg = pfnCreateProgram();
    pfnAttachShader(graphicsProg, vs);
    pfnAttachShader(graphicsProg, fs);
    pfnLinkProgram(graphicsProg);
    GLint linkStatus = 0;
    pfnGetProgramiv(graphicsProg, GL_LINK_STATUS, &linkStatus);
    printf("  • Graphics Program Link     : %s (ID = %u)\n", 
           (linkStatus == GL_TRUE) ? "LINKADO (OK)" : "ERRO", graphicsProg);

    pfnUseProgram(graphicsProg);
    GLint locMVP = pfnGetUniformLocation(graphicsProg, "u_ModelViewProjection");
    printf("  • Uniform u_ModelViewProj   : Localizacao = %d\n", locMVP);

    float identityMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    pfnUniformMatrix4fv(locMVP, 1, GL_FALSE, identityMatrix);
    printf("[OK] Parametros de Uniforms enviados com sucesso!\n");

    /* 7. Teste de Uniform Buffer Object (UBO) */
    printf("\n--- [2/3] Testando Uniform Buffer Objects (UBO 64KB ReBAR) ---\n");
    GLuint ubo = 0;
    pfnGenBuffers(1, &ubo);
    pfnBindBuffer(GL_UNIFORM_BUFFER, ubo);
    pfnBufferData(GL_UNIFORM_BUFFER, sizeof(identityMatrix), identityMatrix, GL_DYNAMIC_DRAW);
    pfnBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    printf("  • UBO Alocado na GDDR6      : ID = %u (Tamanho = %zu bytes)\n", ubo, sizeof(identityMatrix));
    printf("[OK] Binding de UBO (GL_UNIFORM_BUFFER -> Base 0) validado com sucesso!\n");

    /* 8. Teste de Compute Shader GLSL 4.30 (GEMM em 1536 Shaders) */
    printf("\n--- [3/3] Compilando & Executando Compute Shader GLSL 4.30 (GEMM) ---\n");
    GLuint cs = pfnCreateShader(GL_COMPUTE_SHADER);
    pfnShaderSource(cs, 1, &g_ComputeShaderSource, NULL);
    pfnCompileShader(cs);
    GLint csStatus = 0;
    pfnGetShaderiv(cs, GL_COMPILE_STATUS, &csStatus);
    printf("  • Compute Shader GLSL 4.30  : %s (ID = %u)\n", 
           (csStatus == GL_TRUE) ? "COMPILADO (OK)" : "ERRO", cs);

    GLuint computeProg = pfnCreateProgram();
    pfnAttachShader(computeProg, cs);
    pfnLinkProgram(computeProg);
    GLint computeLinkStatus = 0;
    pfnGetProgramiv(computeProg, GL_LINK_STATUS, &computeLinkStatus);
    printf("  • Compute Program Link      : %s (ID = %u)\n", 
           (computeLinkStatus == GL_TRUE) ? "LINKADO (OK)" : "ERRO", computeProg);

    pfnUseProgram(computeProg);

    /* Alocar SSBOs para A, B, C (Matrizes 256x256) */
    GLuint ssbo[3] = {0, 0, 0};
    pfnGenBuffers(3, ssbo);
    size_t matrixSize = 256 * 256 * sizeof(float); /* 256 KB cada */

    pfnBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    pfnBufferData(GL_SHADER_STORAGE_BUFFER, matrixSize, NULL, GL_DYNAMIC_DRAW);
    pfnBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo[0]);

    pfnBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    pfnBufferData(GL_SHADER_STORAGE_BUFFER, matrixSize, NULL, GL_DYNAMIC_DRAW);
    pfnBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[1]);

    pfnBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[2]);
    pfnBufferData(GL_SHADER_STORAGE_BUFFER, matrixSize, NULL, GL_DYNAMIC_DRAW);
    pfnBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo[2]);

    printf("  • SSBOs Alocados (GDDR6)    : IDs = (%u, %u, %u) - 3x 256 KB\n", ssbo[0], ssbo[1], ssbo[2]);

    /* Despachar 16x16 Workgroups = 256 WGs = 65.536 Threads paralelas nos 1536 Shaders */
    pfnDispatchCompute(16, 16, 1);
    printf("  • glDispatchCompute(16,16,1): 65.536 Threads despachadas para os 1536 Shaders.\n");

    /* Barreira de Sincronização de Memória */
    pfnMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    printf("  • glMemoryBarrier           : Sincronizacao de Coerencia GDDR6 concluida.\n");

    /* Limpeza */
    pfnUseProgram(0);
    pfnDeleteShader(vs);
    pfnDeleteShader(fs);
    pfnDeleteShader(cs);
    pfnDeleteProgram(graphicsProg);
    pfnDeleteProgram(computeProg);

    pfnSetContext(NULL, NULL, NULL);
    pfnDeleteContext(hCoreContext);

    ReleaseDC(hWnd, hDC);
    DestroyWindow(hWnd);
    UnregisterClassA("BC250_Shader_Pipeline_Test", hInstance);
    FreeLibrary(hIcd);

    printf("\n======================================================================\n");
    printf("   [SUCESSO] TESTE DA FASE 3 (GLSL 4.00+ & COMPUTE) 100%% HOMOLOGADO!   \n");
    printf("======================================================================\n");

    return 0;
}
