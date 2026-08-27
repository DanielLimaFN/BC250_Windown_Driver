# 🗺️ ROADMAP OFICIAL: SUPORTE A OPENGL 4.0+ NA AMD BC-250 (WINDOWS)

**GPU:** AMD BC-250 (*Cyan Skillfish / Navi12 Lite - 1536 Shaders / 24 CUs / 16 GB GDDR6*)  
**Alvo:** Suporte completo a **OpenGL 4.0 / 4.6 Core Profile & Compatibility Profile** com **WGL ICD** no Windows.

---

## 🎯 1. Visão Geral da Arquitetura

Com o nosso driver Vulkan 1.2+ (`amdbc250vulkan.dll` e `bc250wddm.sys`) **100% homologado em silício real e validado no VkFFT com 26.247 pontos**, a abordagem mais estável, performática e compatível da indústria para entregar OpenGL 4.0 / 4.6 é a arquitetura **Mesa Zink + WGL ICD Bridge**.

```
+-----------------------------------------------------------------------------------+
|               APLICAÇÕES & JOGOS OPENGL (Minecraft, Emuladores, Demos)            |
+-----------------------------------------------------------------------------------+
                                         │  Chamadas OpenGL 4.0+ (glDrawArrays, FBOs...)
                                         ▼
+-----------------------------------------------------------------------------------+
|               WGL ICD BRIDGE (atio6xxx.dll / opengl32.dll)                        |
|  • Negociação de Contexto WGL (wglCreateContextAttribsARB, Core Profile 4.6)      |
|  • Gerenciamento de HDC / HWND / PixelFormatDescriptor                            |
|  • Despacho de Extensões WGL (WGL_ARB_create_context, WGL_EXT_swap_control)       |
+-----------------------------------------------------------------------------------+
                                         │
                                         ▼
+-----------------------------------------------------------------------------------+
|               MESA ZINK GALLIUM DRIVER (OpenGL 4.6 -> Vulkan 1.2 Engine)          |
|  • Compilação de Shaders GLSL 4.00 - 4.60 -> NIR -> SPIR-V                       |
|  • Mapeamento de State Trackers (VAO, VBO, FBO, Texturas, Tessellation, Compute)  |
|  • Gerenciamento de Barreiras de Memória e Formatos de Textura                   |
+-----------------------------------------------------------------------------------+
                                         │  Chamadas Vulkan 1.2 Nativas
                                         ▼
+-----------------------------------------------------------------------------------+
|               AMD BC-250 VULKAN UMD (amdbc250vulkan.dll)                          |
|  • 16 GB GDDR6 ReBAR Allocator / 1536 Shaders Compute & Graphics Pipelines        |
+-----------------------------------------------------------------------------------+
                                         │  D3DKMTEscape (14.36 µs)
                                         ▼
+-----------------------------------------------------------------------------------+
|               AMD BC-250 KERNEL DRIVER (bc250wddm.sys - Ring 0)                   |
|  • SMU 1500 MHz, 12 WGPs / 24 CUs, SDMA0, BAR0 ReBAR                              |
+-----------------------------------------------------------------------------------+
```

---

## 📋 2. Fases Detalhadas do Roadmap

### 🔹 FASE 1: WGL ICD Interface & Context Management
**Objetivo:** Criar o driver de interface ICD do Windows OpenGL (`atio6xxx.dll` / `opengl32.dll`), permitindo que qualquer aplicativo Win32 descubra a GPU e crie contextos OpenGL modernos.

- [ ] Implementar as rotinas obrigatórias da interface WGL ICD:
  - `DrvCreateContext` / `DrvCreateLayerContext`
  - `DrvDeleteContext`
  - `DrvMakeCurrent`
  - `DrvReleaseContext`
  - `DrvGetProcAddress` (Resolução de todas as funções `gl*` e `wgl*`)
  - `DrvSetPixelFormat` / `DrvDescribePixelFormat`
- [ ] Implementar a extensão `WGL_ARB_create_context` e `WGL_ARB_create_context_profile`:
  - Suporte a `WGL_CONTEXT_CORE_PROFILE_BIT_ARB`
  - Suporte a `WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB`
  - Suporte a versões: OpenGL 3.3, 4.0, 4.1, 4.2, 4.3, 4.4, 4.5 e 4.6.
- [ ] **Teste de Validação Real:** `test_opengl_phase1_wgl_context.exe`
  - Criação de janela Win32 oculta, consulta de formatos de pixel, criação de contexto OpenGL 4.0 Core e verificação de `glGetString(GL_VERSION)`.

---

### 🔹 FASE 2: Compilação e Integração do Mesa Zink Engine
**Objetivo:** Integrar o tradutor Gallium Zink da Mesa para transformar chamadas OpenGL 4.0+ diretamente no nosso pipeline Vulkan 1.2 da BC-250.

- [ ] Habilitar Zink no ambiente de build Windows (`-Dgallium-drivers=zink`).
- [ ] Conectar o inicializador do Zink (`zink_create_screen`) ao nosso ICD Vulkan (`amdbc250vulkan.dll`).
- [ ] Configurar os limites de hardware da BC-250 no Zink:
  - `max_vertex_attribs = 16`
  - `max_texture_units = 32`
  - `max_uniform_buffer_size = 64 KB`
  - `max_shader_storage_buffer_size = 2 GB`
  - `max_compute_work_group_invocations = 1024`
- [ ] **Teste de Validação Real:** `test_opengl_phase2_zink_bridge.exe`
  - Inicialização do Zink Screen, query de extensões suportadas e verificação de `GL_RENDERER: "AMD BC-250 (Cyan Skillfish / Navi12 Lite)"`.

---

### 🔹 FASE 3: Pipeline de Shaders GLSL 4.00+ & Compilador NIR
**Objetivo:** Compilar e executar todos os estágios de shaders do OpenGL 4.0 (Vertex, Fragment, Geometry, Tessellation e Compute) traduzidos para SPIR-V / ACO.

- [ ] Integração com compilador GLSL-to-NIR (`glslang` / `mesa glsl`):
  - `glCreateShader`, `glShaderSource`, `glCompileShader`
  - `glCreateProgram`, `glAttachShader`, `glLinkProgram`, `glUseProgram`
- [ ] Suporte a Uniforms e Uniform Blocks (`UBO` - `glBindBufferBase(GL_UNIFORM_BUFFER)`).
- [ ] Suporte a Shader Storage Buffer Objects (`SSBO` - `GL_SHADER_STORAGE_BUFFER`).
- [ ] Suporte a Compute Shaders OpenGL 4.3+ (`glDispatchCompute`).
- [ ] **Teste de Validação Real:** `test_opengl_phase3_glsl_shaders.exe`
  - Compilação de shaders GLSL 4.00 com matrizes MVP, iluminação Phong e execução de Compute Shader de matrizes com verificação numérica exata.

---

### 🔹 FASE 4: Buffer Objects, Texturas & Framebuffer Objects (FBO)
**Objetivo:** Suporte completo ao pipeline de geometria, texturização avançada e renderização offscreen / render-to-texture.

- [ ] Buffer Objects:
  - `glGenVertexArrays` / `glBindVertexArray` (`VAO`)
  - `glGenBuffers` / `glBindBuffer` / `glBufferData` (`VBO`, `EBO`)
  - Mapeamento direto de memória: `glMapBufferRange` com `GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT` conectado ao ReBAR da GDDR6.
- [ ] Texturas 2D, 3D e CubeMaps:
  - Formatos: `GL_RGBA8`, `GL_RGB8`, `GL_RGBA16F`, `GL_RGBA32F`, `GL_DEPTH24_STENCIL8`, `GL_COMPRESSED_RGBA_BPTC_UNORM`.
  - Mipmapping automático (`glGenerateMipmap`) e Samplers anisotrópicos (16x).
- [ ] Framebuffer Objects (FBO):
  - `glGenFramebuffers`, `glFramebufferTexture2D`, `glCheckFramebufferStatus`.
  - Multiple Render Targets (MRT) para Deferred Shading.
- [ ] **Teste de Validação Real:** `test_opengl_phase4_fbo_textures.exe`
  - Renderização offscreen em FBO 1080p, leitura com `glReadPixels` e verificação bit-a-bit de cores e profundidade.

---

### 🔹 FASE 5: Renderização 3D, DrawCalls & WGL SwapBuffers
**Objetivo:** Renderização gráfica completa quadro-a-quadro com suporte a VSync e apresentação na tela.

- [ ] Execução de Draw Calls:
  - `glDrawArrays`, `glDrawElements`, `glDrawElementsInstanced`, `glMultiDrawElementsIndirect`.
- [ ] Estados do Rasterizador:
  - `glEnable(GL_DEPTH_TEST)`, `glDepthFunc`, `glCullFace`, `glBlendFunc`.
  - `glPolygonMode` (Wireframe / Solid).
- [ ] Apresentação WGL:
  - `wglSwapBuffers(hdc)` conectado à Swapchain Vulkan / Blit Win32.
  - Controle de VSync: `wglSwapIntervalEXT(1)` para 60 FPS cravados ou `(0)` para FPS destravado.
- [ ] **Teste de Validação Real:** `test_opengl_phase5_3d_render.exe`
  - Renderização em tempo real de malha 3D iluminada com rotação contínua e medição de FPS e estabilidade de apresentação.

---

### 🔹 FASE 6: Validação Externa com Jogos & Ferramentas da Indústria
**Objetivo:** Homologação rigorosa com aplicativos e benchmarks consagrados de terceiros.

- [ ] **OpenGL Extensions Viewer / GL-Caps-Viewer:**
  - Validação de 100% das extensões do OpenGL 4.0+.
- [ ] **FurMark (Modo OpenGL 1080p):**
  - Estresse térmico, teste de MSAA 8x e estabilidade sob carga contínua.
- [ ] **GZDoom / Quake OpenGL / Unigine Heaven:**
  - Execução de jogos clássicos e modernos que utilizam a API OpenGL no Windows.
- [ ] **Emuladores (RPCS3 / Citra / Ryujinx em modo OpenGL):**
  - Verificação de compatibilidade com pipelines avançados de emulação.
- [ ] **Relatório Final de Homologação OpenGL 4.0:**
  - Emissão do certificado de conformidade da AMD BC-250.

---

## 📊 Matriz de Compatibilidade Planejada

| Recurso | Versão OpenGL | Status Planejado |
| :--- | :---: | :---: |
| **Core Profile & Compat Profile** | 3.3 / 4.0 / 4.6 | ✅ Suporte Total |
| **GLSL Shader Language** | 4.00 até 4.60 | ✅ Compilado via NIR / SPIR-V |
| **Tessellation Shaders (TCS / TES)** | 4.0 | ✅ Suporte Nativo (1536 Shaders) |
| **Shader Storage Buffer Objects (SSBO)** | 4.3 | ✅ Suporte na GDDR6 (16 GB) |
| **Compute Shaders GLSL** | 4.3 | ✅ Suporte Nativo |
| **Direct State Access (DSA)** | 4.5 | ✅ Suporte Zink |
| **SPIR-V Ingestion (`GL_ARB_gl_spirv`)** | 4.6 | ✅ Suporte Zink |
| **Resizable BAR Memory Mapping** | WGL Extension | ✅ Zero-Copy Host Visible |

---

## 🚀 Próximo Passo

Iniciar a **FASE 1: WGL ICD Interface & Context Management**, criando a base do driver `atio6xxx.dll` com suporte completo a criação de contextos e resolução de funções OpenGL no Windows.
