# docs/282 — Handoff Fase 2: UMD D3D11 Nativa Traduzida para Vulkan

**Data:** 16/09/2026  
**Status:** Fase 2 CONCLUÍDA OFFLINE (Compilação `/W4 /WX`, Testes de Contrato Exit 0, Auditorias Semânticas PASS).  
**Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64  
**Diretriz de Referência:** `DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md` (16/09/2026)  

---

## 1. Escopo e Objetivos da Fase 2

A Fase 2 estabelece a UMD D3D11 nativa do projeto (`bc250umd.dll`) com backend gráfico real baseado em Vulkan, eliminando a dependência de DLLs locais por aplicativo (como DXVK na pasta do jogo) e habilitando o desktop acelerado do Windows (`dwm.exe`) e aplicações Direct3D 10.1/11 não modificadas a executarem sobre o silício físico da BC-250.

### Pilares Implementados:
1. **Vulkan Bridge (`bc250_umd_vulkan_bridge.c/.h`)**:
   - Carregamento dinâmico do Mesa RADV (`vulkan_radeon.dll`).
   - Negociação de interface ICD e criação de `VkInstance`.
   - Enumeração de adaptadores e vinculação estrita à GPU física BC-250 (`vendorID == 0x1002`, `deviceID == 0x13FE`).
   - Criação de `VkDevice`, fila gráfica (`GraphicsQueueFamily`), Command Pool, Command Buffer primário e Fence de sincronização.
   - Proteção defensiva com Structured Exception Handling (`__try / __except`) contra falhas em adaptadores genéricos no host.

2. **Mapeamento de Recursos D3D11 -> Vulkan (`bc250_umd_resource_vk.c/.h`)**:
   - Criação de `VkBuffer` para vertex buffers, index buffers e constant buffers com memória host-visible / device-local.
   - Criação de `VkImage` e `VkImageView` para Textures 2D com formatos B8G8R8A8_UNORM, R8G8B8A8_UNORM, D24_UNORM_S8_UINT e D32_FLOAT.
   - Criação de `VkRenderPass` e `VkFramebuffer` para Render Target Views (RTV) e Depth Stencil Views (DSV).

3. **Compilação de Shaders e PSO Cache (`bc250_umd_pipeline_cache.c/.h`)**:
   - Tradução DXBC -> SPIR-V via `libvkd3d-shader-1.dll` (com carregamento automático e fallback incorporado).
   - Shaders de fallback pré-compilados embutidos (`default_vs_spv.inl` e `default_ps_spv.inl`).
   - Criação de `VkShaderModule` para Vertex Shader e Pixel Shader.
   - Gerenciador de Pipeline State Object (PSO Cache) de 64 entradas indexado por chave completa:
     - VS, PS, GS, RenderPass, Topologia, Rasterizer (FillMode, CullMode), Blend (BlendEnable, ColorWriteMask), DepthStencil, InputLayout.
   - Emissão de pipelines gráficos reais via `vkCreateGraphicsPipelines`.

4. **Roteamento de DDIs e Execução Gráfica (`bc250_d3d_umd.c`)**:
   - Roteamento de chamadas de draw (`Bc250Umd10Draw`, `DrawIndexed`, `DrawInstanced`, `DrawIndexedInstanced`) para `Bc250UmdExecuteVkDraw`.
   - Inicialização dinâmica de RenderPass (`vkCmdBeginRenderPass` / `vkCmdEndRenderPass`), bind de pipeline, vertex buffer, index buffer, viewport, scissor e chamada canônica `vkCmdDraw` / `vkCmdDrawIndexed`.
   - Sincronização e flushing (`Bc250Umd10Flush`, `Bc250DxgiPresent`) com submissão à fila gráfica (`vkQueueSubmit`) e apresentação WDDM.

5. **Conformidade D3D11 e Testes**:
   - Harness de conformidade D3D11 nativo (`driver/tests/d3d11_native_conformance_test.cpp`).
   - Script de compilação (`tools/build-d3d11-native-conformance.bat`) e executável `bc250-d3d11-conformance.exe`.
   - Script de ensaio físico automatizado (`tools/test-d3d11-native-conformance.ps1`) e lançador direto (`tools/EXECUTAR_D3D11_NATIVO.cmd`).

---

## 2. Status de Validação Offline (Zero Erros)

Todos os passos de compilação e baterias de testes offline passaram com código de saída 0:

| Componente / Teste | Comando de Execução | Resultado |
|---|---|---|
| **Build Completo UMD** | `driver\umd\build-umd.bat` | **EXIT 0** (Sem erros `/W4 /WX`) |
| **UMD Runtime Contract** | `umd_runtime_contract_test.exe` | **PASS** (21/21 invariantes) |
| **D3D UMD Contract** | `bc250_d3d_umd_test.exe` | **PASS** (42/42 testes) |
| **VKD3D-Shader Adapter** | `vkd3d_shader_adapter_test.exe` | **PASS** |
| **WDDM Winsys Contract** | `bc250_wddm_winsys_test.exe` | **PASS** |
| **UMD Node 1 Submit** | `umd_node1_submit_contract_test.exe` | **PASS** |
| **UMD Node 1 Tripod** | `umd_node1_tripod_contract_test.exe` | **PASS** |
| **Auditoria Semântica UMD**| `audit-umd-node1-submission.py` | **PASS** (Fail-closed estrito mantido) |
| **Build de Contratos WDK** | `driver\wdk\build-contract.bat` | **EXIT 0** |
| **Auditoria de Segurança MCDM** | `audit-bc250mcdm-safety.py` | **PASS** |
| **Build Teste D3D11 Nativo**| `build-d3d11-native-conformance.bat` | **EXIT 0** (`bc250-d3d11-conformance.exe`) |

---

## 3. Binários Produzidos

- `driver/umd/output/bc250umd.dll`: Driver de Modo de Usuário (UMD) D3D10.1/11 com backend Vulkan.
- `driver/umd/output/bc250_d3d_umd_test.exe`: Harness de teste D3D UMD.
- `driver/tests/output_d3d11_native/bc250-d3d11-conformance.exe`: Executável de conformidade D3D11 nativo.
- `driver/tests/output_radv_gate/bc250-radv-graphics-gate.exe`: Executável do Marco 3 (Color Bars).
- `driver/tests/output_radv_gate/bc250-radv-cube-gate.exe`: Executável do Marco 4 (Cubo Vulkan).

---

## 4. Próximos Passos (Validação em Hardware com Daniel)

Com a Fase 2 concluída e validada offline, o próximo passo é realizar os ensaios no silício físico da BC-250 sob supervisão do usuário Daniel:
1. **Boot Frio Obrigatório** (Lei 5).
2. **Executar Marco 3 (Color Bars)** via `tools\test-marco3-graphics-draw.ps1 -Execute`.
3. **Executar Marco 4 (Cubo Vulkan 3D)** via `tools\test-marco4-graphics-cube.ps1 -Execute`.
4. **Executar Teste de Conformidade D3D11 Nativo** via `tools\EXECUTAR_D3D11_NATIVO.cmd`.
