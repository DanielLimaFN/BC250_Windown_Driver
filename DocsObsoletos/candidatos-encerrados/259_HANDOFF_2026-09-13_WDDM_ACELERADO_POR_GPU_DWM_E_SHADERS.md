# Handoff 259 — WDDM Acelerado por GPU: DWM, Tradutor de Shaders e Blt SDMA

**Data:** 2026-09-13  
**Escopo:** Implementação offline, testes unitários, suíte de contratos e empacotamento assinado do candidato WDDM acelerado por GPU.  
**Veredito físico:** **NÃO VALIDADO EM HARDWARE** (pendente de boot frio na máquina física).

---

## 1. Contexto e Objetivo

Diretriz explícita do operador: **"a meta inicial é wddm acelerado por gpu e depois vamos para os jogos"**.

Após a estabilização do Marco 3 (600 quadros sustentados a 60 FPS no silício via swapchain Vulkan RADV + MEC Node 1 + SDMA Blt sem TDR e com fusível limpo), o próximo objetivo é fazer o compositor nativo do Windows (**DWM - Desktop Window Manager**) rodar acelerado na GPU da BC-250 (`1002:13FE`), gerando as janelas, área de trabalho e barra de tarefas diretamente na VRAM do monitor conectado ao DisplayPort, sem fallbacks de CPU e em estrita conformidade com a Lei 4.

---

## 2. Componentes Desenvolvidos e Validados

### 2.1. Tradutor de Token Streams de Shaders da UMD (Direct Token Stream → DXBC → SPIR-V)
- **Problema anterior:** O DWM envia shaders em streams de tokens brutos SM4/SM5 (sem contêiner DXBC completo). O `vkd3d_shader_compile` rejeita streams sem contêiner com erro -2/-3, e rejeita shaders com assinaturas (`ISGN`/`OSGN`) ausentes ou corrompidas.
- **Solução implementada:**
  - `driver/umd/bc250_vkd3d_shader_abi.h`: Definição de `BC250_VKD3D_DXBC_SECTION_DESC` e protótipo de `vkd3d_shader_serialize_dxbc`.
  - `driver/umd/bc250_vkd3d_shader_adapter.c`: Carregamento dinâmico de `vkd3d_shader_serialize_dxbc` a partir da `libvkd3d-shader-1.dll`.
  - `driver/core/bc250_dxbc_spirv_bridge.c`: Função `Bc250DxbcTranslateTokenStreamToSpirv`. Constrói chunks binários `ISGN` e `OSGN` a partir de `D3D10DDIARG_STAGE_IO_SIGNATURES`, empacota com as seções `SHDR`/`SHEX` e invoca `SerializeDxbc` para gerar contêiner com checksum MD5 oficial Microsoft.
  - `driver/tests/vkd3d_shader_real_smoke.c`: Harness atualizado para testar tanto contêineres `.cso` quanto Direct Token Streams com a DLL real x64 (`libvkd3d-shader-1.dll` 1.17 do Fedora 44).
  - **Resultado do teste real (`tools/test-vkd3d-shader-real.ps1`):**
    - `smoke-vs.cso`: DXBC 624 B → SPIR-V 1480 B (Estágio 1 - Vertex)
    - `VS TokenStream`: Tokens=57 → SPIR-V 1480 B (Estágio 1 - Vertex)
    - `smoke-ps.cso`: DXBC 452 B → SPIR-V 396 B (Estágio 0 - Pixel)
    - `PS TokenStream`: Tokens=14 → SPIR-V 584 B (Estágio 0 - Pixel)
    - **`VKD3D SHADER REAL SMOKE: PASS`**

### 2.2. Composição de Sub-Retângulos de Janelas no SDMA0 da GPU
- **Problema anterior:** `Bc250UmdCompositeDraw` exigia viewport cobrindo a tela inteira (1920x1080), recusando qualquer draw de janelas com dimensões menores.
- **Solução implementada:**
  - `driver/umd/bc250_d3d_umd.c` (`Bc250UmdSubstituteDrawWithBlt`): Extrai coordenadas `TopLeftX/Y` e `Width/Height` da viewport ativa e preenche `bltArgs.DstLeft`, `DstTop`, `DstRight`, `DstBottom`.
  - O despachador `Bc250UmdBltGpu` gera pacotes SDMA scanline a scanline com row pitch diretamente no SDMA0 da GPU.
  - **Zero fallbacks de CPU (`memcpy` removido)**: Cada retângulo de janela do DWM é copiado no silício pela GPU.

### 2.3. Correção de Encoding e Chaves do Tradutor no UMD
- Em `driver/umd/bc250_d3d_umd.c` (`Bc250UmdShaderTranslatorReadPath`), corrigida leitura do caminho da DLL com conversão explícita ANSI → UTF-16 via `MultiByteToWideChar`, evitando corrupção de ponteiro `wchar_t`.
- Criado `tools/arm-d3d11-shader-translator.ps1` para inspeção e arme controlado das chaves `D3D11ShaderTranslator` e `D3D11ShaderTranslatorPath`.

### 2.4. Integração ao Binder de Laboratório e Empacotamento
- `tools/bind-bc250mcdm-lab.ps1`: Adicionados parâmetros `-EnableD3D11ShaderTranslator` e `-D3D11ShaderTranslatorDllPath`. O binder arma as chaves de UMD antes do bind e limpa na saída ou em caso de falha.
- `tools/build-bc250mcdm-lab-bringup.ps1`: Recompilado `driver/wdk/output_lab/bc250mcdm.sys` com bring-up ativo.
- `tools/package-bc250mcdm-lab.ps1`: Pacote de laboratório assinado gerado em `driver/wdk/package/output_lab/`:
  - `bc250mcdm.sys` (assinado)
  - `bc250umd.dll` (assinado)
  - `bc250mcdm.cat` (gerado e assinado via Inf2Cat/SignTool)
  - Manifesto `output_lab.manifest.json` atualizado.
- `tools/audit-bc250mcdm-lab-package.ps1`: **PASS**.
- `tools/audit-bc250mcdm-safety.py`: **PASS**.

### 2.5. Script de Execução Automatizada para o Operador
- Criado `tools/EXECUTAR_WDDM_ACELERADO.cmd` (com finais de linha CRLF validados):
  1. Verifica preflight de boot frio (`C:\BC250_REQUIRE_CLEAN_BOOT.json`).
  2. Reseta o fusível de boot (`C:\BC250_BOOT_FUSE.dat` → `CLEAN`).
  3. Dispara o bind WDDM com DWM acelerado:
     - `RenderNode=1` (D3D10/11)
     - `PersistentSubmission=1` (MEC Node 1)
     - `D3D11LabPromotion=1` (Device D3D10/11)
     - `D3D11DrawBltSubstitution=1` (Blt de sub-retângulos no SDMA)
     - `D3D11ShaderTranslator=1` (Tradução para SPIR-V via vkd3d-shader)
     - Watchdog temporizado de 180s com rollback incondicional.

---

## 3. Estado dos Contratos e Auditorias

| Verificação | Comando | Resultado |
|---|---|---|
| Contratos do Miniport | `driver/wdk/build-contract.bat` | **EXIT=0 (35/35 PASS)** |
| DDI Matrix | `tools/audit-mcdm-ddi-matrix.py` | **PASS (18 prohibited, 37 minimum, 10 display)** |
| Segurança de Silício | `tools/audit-bc250mcdm-safety.py` | **PASS (24 verificações)** |
| Auditoria UMD Node 1 | `tools/audit-umd-node1-submission.py` | **PASS** |
| Compilação e Testes UMD | `driver/umd/build-umd.bat` | **EXIT=0 (Todos os harnesses PASS)** |
| Smoke Test vkd3d-shader Real | `tools/test-vkd3d-shader-real.ps1` | **PASS (Tokens VS=57, PS=14 traduzidos)** |
| Pacote Lab Assinado | `tools/audit-bc250mcdm-lab-package.ps1` | **PASS** |
| Git Diff Check | `git diff --check` | **PASS (zero conflitos de whitespace/CRLF)** |

---

## 4. Próximo Passo Físico

1. **Boot Frio na Máquina Física**:
   - Necessário para descarregar contextos de anel e microcódigo residual da execução anterior.
2. **Executar Ensaio**:
   - Rodar `tools\EXECUTAR_WDDM_ACELERADO.cmd` em Prompt de Comando elevado (como Administrador).
3. **O que observar no hardware**:
   - Se o monitor no DisplayPort da BC-250 exibe o desktop do Windows (janelas e barra de tarefas).
   - O log em `C:\Windows\Temp\BC250_UMD_LOG_*.txt` registrará:
     - `SHADER_TRANSLATOR: TokenStream -> SPIR-V ...`
     - `Draw SUBSTITUIDO por copia 1:1 no SDMA ...`
