# DOSSIÊ TÉCNICO DE AUDITORIA EXTERNA — F5.1 & F5.2 (PAREDE B)

> **Documento para Auditoria Independente por IA Externa**
> *Target: AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 Pro 22H2 x64 (19045)*
> **Data:** 2026-09-14 | **Branch:** `p6-rendernode-wddm`
> **Norma de Referência:** `BASE_DE_AVANCO.md`, `PLANO_PRESENT_E_FAIL.md`, Leis 1, 4 e 8

---

## 🎯 1. Contexto e Objetivo Desta Entrega

Nas fases F0 a F4 e F5.0, o driver alcançou:
1. **Parede A (Apresentação DXGI/DWM) derrubada no silício:** 222 chamadas consecutivas de `Present` com `hr=0x00000000` (`S_OK`), laço de device-lost cessado (`DestroyDevice = 0`), rotação contínua de superfícies MPO3 em VRAM real no KMD.
2. **F5.0 (Derivação Estrita de Primariedade) validada no silício:** Na corrida `23:16:53`, o `Explorer.EXE` alocou 290 recursos com **zero recusas** (eliminando a regressão de 142 recusas na corrida 22:17).
3. **Parede B (Rasterização / Pipeline 3D PM4) identificada como o único bloqueador remanescente:** As superfícies apresentadas continuavam pretas porque qualquer chamada de renderização gráfica (`Draw`, `DrawIndexed`, etc.) ou era recusada honestamente com `E_NOTIMPL` ou caía na substituição experimental por cópia SDMA 1:1.

### ⚠️ Retratações e Correções Mandatórias da Corrida 23:16:53

Antes de avaliar o código novo, a auditoria externa deve registrar os seguintes fatos apurados:
1. **Falso Marco 6 Retratado:** A linha `[Present #5] *** DESKTOP REAL DETECTADO ***` disparou na instrumentação de kernel com apenas `2/256` dwords não-nulos (254 zeros). O `BYTE_PROOF` físico saiu 4× `INCONCLUSIVO` e a distribuição na corrida foi 47× `0/256`, 4× `2/256`, 1× `3/256`, 2× `5/256` (quadro preto). Com a Parede B intacta e `DrawIndexed: RECUSADO` no log, **não houve rasterização nem desktop real**. Tratava-se de ruído em quadro preto, devidamente expurgado do veredito.
2. **Contagem Precisa de Superfícies do Explorer.EXE:** Na corrida `23:16:53`, exatamente 3 superfícies de dimensão `1024x832` (`Size=3407872`) foram criadas com `Primary=0` (1 no PID 10700 e 2 no PID 8452), todas com sucesso (`CreateResource` OK).

---

## 📦 2. Resumo das Alterações Realizadas

Esta entrega implementa e fecha **offline** duas fases críticas do roadmap:
- **Fase F5.1:** Tabela DXGI 1.6 Completa (22 slots, 176 bytes) com fail-closed honesto.
- **Fase F5.2:** Parede B — Pipeline Gráfico PM4 3D no Node 1 (Registradores de Contexto GFX10, Render Target, Scissor, Emissão de Draw Auto e Telemetria Isolada).

---

## 🔍 3. Detalhamento da Fase F5.1 (Tabela DXGI 1.6)

### 3.1 O Problema
A UMD expunha apenas a tabela base `DXGI_DDI_BASE_FUNCTIONS` (DXGI 1.0). Runtimes modernos do Windows 10 D3D11/DXGI solicitam extensões mais recentes (`pDXGIDDIBaseFunctions6_1`) durante `CreateDevice`. A falta dessa tabela causava restrição de recursos e impediria recursos avançados de swapchain e residência.

### 3.2 O que foi implementado
1. **Definição da Estrutura:**
   - Em `driver/umd/bc250_d3d_umd.h`: declarada a struct `DXGI1_6_1_DDI_BASE_FUNCTIONS` com todos os 22 ponteiros de função canônicos (`sizeof == 176` bytes verificado).
2. **Preenchimento dos 22 Slots:**
   - Em `driver/umd/bc250_d3d_umd.c` (`Bc250UmdPopulateDxgiBaseFuncs`):
     - Slots 1 a 9: `pfnPresent`, `pfnGetInfo`, `pfnBlt`, `pfnSetDisplayMode`, `pfnGetQueryDataAddress`, `pfnSetResourcePriority`, `pfnQueryResourceResidency`, `pfnRotateResourceIdentities`, `pfnBlt1`.
     - Slots 10 a 16: `pfnOfferResources`, `pfnReclaimResources`, `pfnGetMultiplaneOverlayCaps`, `pfnCheckMultiplaneOverlayColorSpaceSupport`, `pfnPresentMultiplaneOverlay`, `pfnOfferResources1`, `pfnReclaimResources1`.
     - Slots 17 a 22: `pfnCheckMultiplaneOverlaySupport`, `pfnPresent1`, `pfnTrimNotification`, `pfnCheckMultiplaneOverlaySupport1`, `pfnPresentMultiplaneOverlay1`, `pfnCheckMultiplaneOverlayColorSpaceSupport1`.
3. **Honestidade de Capacidades (Lei 4):**
   - `Bc250DxgiGetMultiplaneOverlayCaps`: Retorna expressamente `MaxPlanes = 1` e `NumCapabilityGroups = 0`, declarando honestamente ao SO que o hardware **não possui planos MPO de hardware dedicados**, evitando que o DWM monte planos sintéticos sem suporte.
4. **Conexão no `CreateDevice`:**
   - Em `Bc250Umd10CreateDevice`: detecta se `pCreateData->pDXGIDDIBaseFunctions` possui interface estendida e conecta `pDXGIDDIBaseFunctions6_1`.

### 3.3 Portão de Auditoria e Mutações
- Portão `audit_dxgi_1_6_table_complete` em `tools/audit-bc250mcdm-safety.py`.
- Suíte `tools/test-f5-1-dxgi-mutations.py`: **7/7 mutações detectadas com sucesso** (omissão de `pfnPresent1`, omissão de `pfnBlt1`, falsificação de planos MPO `MaxPlanes=2`, grupos MPO falsos, omissão do wiring em `CreateDevice`, remoção de asserções de teste).

---

## 🧱 4. Detalhamento da Fase F5.2 (Parede B — Pipeline Gráfico PM4 3D)

### 4.1 O Problema
O driver possuía suporte a submissão de Compute no Node 1 e cópia SDMA no Node 0, mas **zero emissão de pacotes gráficos PM4 3D**. Chamadas a `pfnDraw` e `pfnDrawIndexed` não alcançavam a GPU como comandos de rasterização.

### 4.2 O que foi implementado

#### A. Registradores de Contexto GFX10 e Pacotes PM4 (`bc250_pm4_packet_builder.h` / `.c`)
1. Mapeamento de registradores de contexto GFX10 (`BC250_PM4_CONTEXT_REG_BASE = 0xA000`):
   - `BC250_PM4_REG_CB_COLOR0_BASE (0xA318)`: Base GPUVA >> 8.
   - `BC250_PM4_REG_CB_COLOR0_PITCH (0xA319)`: `(pitchPixels / 8) - 1`.
   - `BC250_PM4_REG_CB_COLOR0_SLICE (0xA31A)`: Contagem de tiles no slice.
   - `BC250_PM4_REG_CB_COLOR0_VIEW (0xA31B)`: Slice start/max e mip level.
   - `BC250_PM4_REG_CB_COLOR0_INFO (0xA31C)`: Formato (ex: `0x16` para 8_8_8_8), `CompSwap` (1 para BGRA, 0 para RGBA), `BlendBypass = 1`.
   - `BC250_PM4_REG_CB_COLOR0_ATTRIB (0xA31D)`: Modo de ladrilhamento.
   - `BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_TL (0xA081)`: Scissor Top/Left com flag `OffsetDisable` (bit 31).
   - `BC250_PM4_REG_PA_SC_WINDOW_SCISSOR_BR (0xA082)`: Scissor Bottom/Right.
2. Emissores Canônicos PM4:
   - `Bc250Pm4EmitColorTarget0`: Valida parâmetros e alinhamento de 256 bytes na base GPUVA; calcula fatiamento de tiles e emite os 6 registradores de `CB_COLOR0`.
   - `Bc250Pm4EmitWindowScissor`: Emite `PA_SC_WINDOW_SCISSOR_TL` e `BR`.
   - `Bc250Pm4EmitDrawIndexAuto`: Emite pacote Tipo 3 (`Opcode 0x2D`) com `IndexCount` e `DrawInitiator` (topologia de primitiva: `TRIANGLELIST`, `POINTLIST`, `LINELIST`, `RECTLIST`).

#### B. Mecanismo de Emissão na UMD (`bc250_d3d_umd.h` / `.c`)
1. **Função `Bc250UmdEmitPm4Draw` (Travada sob Tunable Fail-Closed):**
   - **Tunable `D3D11ExperimentalPm4Draw` (default 0):** Para cumprir a Lei 4, a função recusa imediatamente se o tunable não estiver armado (`!Bc250UmdExperimentalPm4DrawArmed()`).
   - **Verificação de Elegibilidade:** Valida `pDevice->NativeNode1Ready`, contexto virtual ativo em `BC250_WINSYS_NODE_COMPUTE` e buffer de comandos inicializado.
   - **Validação de Render Target:** Recupera o RTV ligado no slot 0 do Output Merger (`pDevice->PipelineState.BoundRTVs[0]`); valida residência e endereço GPUVA.
   - **Trava de Alinhamento GFX10:** Rejeita estritamente qualquer superfície cujo endereço GPUVA não seja alinhado a 256 bytes (`dst->Allocation.GpuVirtualAddress & 0xFFull != 0`).
   - **Construção de Estado e Desenho:** Emite `CB_COLOR0`, configura o Scissor com base no viewport/scissor rect ativo e emite `IT_DRAW_INDEX_AUTO` de acordo com a topologia ligada.
   - **Submissão:** Descarrega o comando via `Bc250UmdCommandBufferFlush` (Node 1).
   - **Telemetria Isolada:** Incrementa `pDevice->NativePm4DrawCount++` (estritamente separado de `SubstitutedDrawCount` e `RejectedDrawCount`).
2. **Encadeamento nas DDIs de Desenho (Inversão de Precedência Honesta):**
   - `Bc250Umd10Draw`, `Bc250Umd10DrawIndexed`, `Bc250Umd10DrawInstanced`, `Bc250Umd10DrawIndexedInstanced`:
     - **Default de Produção (Tunable Desarmado):** `Bc250UmdEmitPm4Draw` recusa; `Bc250UmdSubstituteDrawWithBlt` (desarmado) recusa; cai no ramo honesto **`Bc250UmdRejectUnbackedDraw`** (`Draw: RECUSADO: compilador/shader-state/draw PM4 ausente`, `RejectedDrawCount++`).
     - **Modo Experimental (Tunable Armado via Registro ou Env):** Executa `Bc250UmdEmitPm4Draw`. Se bem-sucedido, incrementa `NativePm4DrawCount` e retorna.

#### C. Testes e Auditoria
1. **Teste de Contrato Unitário (`TestDrawPm4EmissionOnNode1` em `bc250_d3d_umd_test.c`):**
   - Testa primeiro a recusa padrão fail-closed com o tunable desarmado: comprova `NativePm4DrawCount == 0` e `RejectedDrawCount == 1`.
   - Arma explicitamente o tunable via `SetEnvironmentVariableA(BC250_D3D11_EXPERIMENTAL_PM4_DRAW_ENV_NAME, "1")`.
   - Executa `pfnDraw` (VertexCount=3): comprova `NativePm4DrawCount == 1`, `SubstitutedDrawCount == 0`, `SubmitGpuVa == 1`.
   - Executa `pfnDrawIndexed` (IndexCount=6): comprova `NativePm4DrawCount == 2`, `SubmitGpuVa == 2`.
   - Teste negativo: base GPUVA desalinhada (`0x10000004`): comprova recusa sem subir `NativePm4DrawCount` (`RejectedDrawCount == 2`).
   - Desarma o tunable ao final do teste.
2. **Portão de Segurança (`audit_f5_2_pm4_draw_pipeline` em `tools/audit-bc250mcdm-safety.py`):**
   - Exige a definição de `BC250_D3D11_EXPERIMENTAL_PM4_DRAW_REG_NAME`, declaração de `Bc250UmdExperimentalPm4DrawArmed`, chamada da trava fail-closed dentro de `Bc250UmdEmitPm4Draw` e asserção de rejeição com tunable desarmado no teste de contrato.
3. **Suíte de Mutação Dedicada (`tools/test-f5-2-pm4-draw-mutations.py`):**
   - **11/11 mutações detectadas com sucesso (EXIT=0):**
     - M1: Omissão de `ColorTarget0` $\rightarrow$ REPROVADO ✅
     - M2: Omissão de `WindowScissor` $\rightarrow$ REPROVADO ✅
     - M3: Omissão de `DrawIndexAuto` $\rightarrow$ REPROVADO ✅
     - M4: Remoção do check de alinhamento 256B $\rightarrow$ REPROVADO ✅
     - M5: Omissão de `NativePm4DrawCount++` $\rightarrow$ REPROVADO ✅
     - M6: Omissão da chamada em `Bc250Umd10Draw` $\rightarrow$ REPROVADO ✅
     - M7: Deleção do assert de contagem no teste $\rightarrow$ REPROVADO ✅
     - M8: Deleção do caso negativo de desalinhamento no teste $\rightarrow$ REPROVADO ✅
     - M9: Omissão do check `Bc250UmdExperimentalPm4DrawArmed` em `Bc250UmdEmitPm4Draw` $\rightarrow$ REPROVADO ✅
     - M10: Deleção do assert fail-closed (`NativePm4DrawCount == 0`) no teste $\rightarrow$ REPROVADO ✅
     - M11: Omissão do define do tunable em `bc250_umd_caps_probe.h` $\rightarrow$ REPROVADO ✅

---

## 📋 5. Guia de Reprodução para a IA Auditora

A IA auditora pode reproduzir e validar cada uma dessas etapas executando os seguintes comandos no terminal:

```powershell
# 1. Compilar UMD e rodar todas as 31 suites unitarias (incluindo F5.1 e F5.2)
cmd /c "driver\umd\build-umd.bat"
# Saida esperada: EXIT=0, [TEST] F5.2 PM4 3D Draw Emission PASS (Fail-Closed Default + Experimental Mode Tested)

# 2. Compilar suíte de contratos WDK (45 suites)
cmd /c "driver\wdk\build-contract.bat"
# Saida esperada: EXIT=0, ZERO driver, ZERO install, ZERO hardware

# 3. Rodar auditoria estatica de seguranca do driver
python tools\audit-bc250mcdm-safety.py
# Saida esperada: EXIT=0, "DXGI 1.6 table complete", "Parede B (PM4 3D draw on Node 1...): enforced"

# 4. Rodar as 3 suites de mutacao da fase F5
python tools\test-f5-audit-mutations.py      # F5.0b Primaria: 8/8 MUTATIONS DETECTED (EXIT=0)
python tools\test-f5-1-dxgi-mutations.py     # F5.1 DXGI 1.6:  7/7 MUTATIONS DETECTED (EXIT=0)
python tools\test-f5-2-pm4-draw-mutations.py # F5.2 Parede B:  11/11 MUTATIONS DETECTED (EXIT=0)
```

---

## ⚖️ 6. Veredito e Próximos Passos da Parede B

- **Fase F5.1 (DXGI 1.6 DDI):** ✅ **FECHADA offline** e auditada (`audit_dxgi_1_6_table_complete`, 7/7 mutações detectadas).
- **Fase F5.2 (Pipeline Gráfico PM4 3D):** ❌ **NÃO FECHADA offline (4 Bloqueadores Declarados):**
  1. *Fila errada:* Pacotes gráficos ME/PFP submetidos ao Node 1 (MEC).
  2. *Não há shader:* Sem registradores `SPI_SHADER*` e sem compilador ACO ativo.
  3. *Não há geometria de entrada:* Sem vertex buffers ou input layout.
  4. *Proveniência dos registradores:* Offsets GFX10 sem catalogação P1 em `docs/03`.
- **Contenção da Lei 4 Garantida:** O código construído foi colocado sob o tunable fail-closed `D3D11ExperimentalPm4Draw` (default 0), restaurando a recusa honesta `DrawIndexed: RECUSADO` como padrão incondicional de produção.
- **Roadmap para Fechamento Real da Parede B:**
  - `B.1`: Anel gráfico real (ME/PFP, `CP_RB0_*`) ou Node 1 reconfigurado.
  - `B.2`: Shaders DXBC -> SPIR-V -> ACO carregados em VRAM real.
  - `B.3`: Descritores, vertex buffer, input layout, transformações de viewport.
  - `B.4`: `DRAW_INDEX_AUTO` + catalogação de registradores em `docs/03` com proveniência P1.
  - `B.5`: Prova de bytes física (`BYTE_PROOF PASS`) de triângulo de gabarito rasterizado sobre poison.
- **Efeito no Silício:** Conforme a Lei 8:
  **NÃO VALIDADO EM HARDWARE** até a execução do próximo ensaio físico (`bind-bc250mcdm-lab.ps1`).
