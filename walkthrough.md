# Walkthrough — Etapa B.4: Pipeline Integrado de Renderização de Triângulo 3D (`CP_RB0`)

Implementação, integração e auditoria da **Etapa B.4** da Parede B concluídas com sucesso, estabelecendo:
1. **Pipeline 3D Completo no Anel Gráfico (GFX10 CP_RB0)**: emissão atômica de um stream PM4 contíguo de exatamente **64 DWORDs** (256 bytes, rigorosamente alinhado a 16 bytes), unificando todos os 7 blocos do pipeline gráfico da BC-250.
2. **7 Blocos Estruturais Canônicos**:
   - **Bloco 1 (Shaders VS + PS)**: `SET_SH_REG` (0x76, 12 DW) apontando para os blobs autenticados de VS e PS em VRAM real.
   - **Bloco 2 (Color Target 0)**: `SET_CONTEXT_REG` (0x69, 8 DW) configurando `CB_COLOR0_BASE..VIEW` (0x0318) com endereço GPUVA em VRAM alinhado a 256B e formato RGBA8.
   - **Bloco 3 (Target Mask)**: `SET_CONTEXT_REG` (0x69, 4 DW) habilitando o mask de escrita RGBA em RT0 (`BC250_PM4_REG_CB_TARGET_MASK = 0xA08Eu`).
   - **Bloco 4 (Viewport & Scissors)**: `SET_CONTEXT_REG` (0x69, 20 DW) com matemática IEEE-754 calculando transformações de viewport e retângulos de scissor.
   - **Bloco 5 (Primitive Assembler)**: `SET_UCONFIG_REG` (0x79, 8 DW) configurando a topologia `DI_PT_TRILIST` (`0x04u`) e `VGT_NUM_INSTANCES = 1`.
   - **Bloco 6 (Draw Initiator)**: `IT_DRAW_INDEX_AUTO` (0x2D, 4 DW) disparando o desenho indexado de 3 vértices no modo canônico `AUTO_INDEX` (`0x02u`).
   - **Bloco 7 (Physical Fence em VRAM)**: `IT_WRITE_DATA` (0x37, 8 DW) com confirmação de escrita em memória física VRAM sobrescrevendo poison.
3. **Verificação Prévia Estrita (`Bc250GfxVerify3DDrawStream`)**: cada uma das 64 dwords do stream é validada antes de tocar em qualquer registrador MMIO de hardware.
4. **Tripé da Lei 2 no Silício**: injeção de poison na fence antes do disparo (`0x43DAFFFEu`), avanço de `CP_RB0_WPTR`, polling com redução de volta de anel da Faixa A15 e verificação estrita de `BC250_TRIPOD_PROVEN`.
5. **Integração no Driver WDDM e Laboratório**:
   - Adicionado Bringup Level 4 (`-GfxRingBringupLevel 4`) no miniport ativo (`bc250_active_miniport.c`) e script [`tools/bind-bc250mcdm-lab.ps1`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/bind-bc250mcdm-lab.ps1).
   - Alocação estrita de buffer VRAM (256 KB) para Render Target 0.
   - Rollback incondicional (Lei 5) devolvendo o anel e as micro-engines ao estado seguro.
6. **Falsificabilidade e Auditoria (Leis 1, 2 e 4)**:
   - Teste de contrato dedicado em [`driver/tests/gfx_ring_contract_test.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/tests/gfx_ring_contract_test.c).
   - Portão de auditoria `audit_gfx_3d_draw_is_honest` em [`tools/audit-bc250mcdm-safety.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/audit-bc250mcdm-safety.py).
   - Suíte de mutações [`tools/test-b4-gfx-3d-draw-mutations.py`](file:///c:/Users/Daniel/Desktop/NamixWDDM/tools/test-b4-gfx-3d-draw-mutations.py) com **18/18 mutações detectadas (100%)**.

Handoff oficial do repositório: [`docs/279_HANDOFF_B4_PIPELINE_3D_DRAW_PM4.md`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/279_HANDOFF_B4_PIPELINE_3D_DRAW_PM4.md)

---

## 1. Entregas da Etapa B.4

1. **Módulo de Transação do Anel Gráfico (`bc250_gfx_ring_transaction.h` / `.c`):**
   - Constantes de contagem de cada bloco estrutural e total canônico (`BC250_GFX_3D_DRAW_STREAM_TOTAL_DWORDS = 64u`).
   - Estruturas `BC250_GFX_RING_SUBMIT_3D_DRAW_PLAN` e `BC250_GFX_RING_SUBMIT_3D_DRAW_EVIDENCE`.
   - Implementação de `Bc250GfxVerify3DDrawStream` com validação de 64 DW.
   - Implementação de `Bc250GfxRingTxnSubmit3DDraw` integrando os 7 blocos e tripé da Lei 2.

2. **Integração no Miniport Ativo (`bc250_active_miniport.h` / `.c`):**
   - Campo `GfxRing3DDrawEvidence` em `BC250_DXGK_ACTIVE_CONTEXT`.
   - Bringup Level 4 com alocação de 256 KB em VRAM e submissão 3D.
   - Suporte a rollback incondicional (Lei 5).

3. **Tooling de Laboratório:**
   - Suporte ao parâmetro `-GfxRingBringupLevel 4` em `tools/bind-bc250mcdm-lab.ps1`.

4. **Portão de Segurança e Testes Unitários:**
   - Auditoria `audit_gfx_3d_draw_is_honest` em `tools/audit-bc250mcdm-safety.py`.
   - Suíte de mutações: `tools/test-b4-gfx-3d-draw-mutations.py` com **18/18 detectadas (100%)**.
   - Teste de contrato em `driver/tests/gfx_ring_contract_test.c` cobrindo caminho feliz, timeout negativo, detecção de corrupção e rollback.

---

## 2. Resultados de Verificação

| Verificação | Comando | Resultado |
|---|---|---|
| **Build de Contrato Offline** | `cmd.exe /c "driver\wdk\build-contract.bat"` | **EXIT=0** (`47 testes de contrato passaram`) |
| **Auditoria Geral de Segurança** | `python tools/audit-bc250mcdm-safety.py` | **PASS** (`verdict: NAO VALIDADO EM HARDWARE`) |
| **Suíte de Mutações B.4** | `python tools/test-b4-gfx-3d-draw-mutations.py` | **PASS (18/18 detectadas · 100%)** |
| **Suíte de Mutações B.3.3** | `python tools/test-b3-3-gfx-draw-initiator-mutations.py` | **PASS (16/16 detectadas · 100%)** |
| **Suíte de Mutações B.3.2** | `python tools/test-b3-2-gfx-primitive-assembler-mutations.py` | **PASS (16/16 detectadas · 100%)** |
| **Suíte de Mutações B.3.1** | `python tools/test-b3-1-gfx-viewport-scissor-mutations.py` | **PASS (22/22 detectadas · 100%)** |
| **Suíte de Mutações B.2.4** | `python tools/test-b2-4-gfx-sh-reg-packet-mutations.py` | **PASS (26/26 detectadas · 100%)** |
| **Suíte de Mutações B.2.3** | `python tools/test-b2-3-gfx-shader-staging-mutations.py` | **PASS (27/27 detectadas · 100%)** |
| **Suíte de Mutações B.2.2** | `python tools/test-b2-2-gfx-observable-shader-mutations.py` | **PASS (16/16 detectadas · 100%)** |
| **Suíte de Mutações B.2** | `python tools/test-b2-gfx-shader-mutations.py` | **PASS (15/15 detectadas · 100%)** |
| **Suíte de Mutações B.1** | `python tools/test-b1-gfx-ring-mutations.py` | **PASS (30/30 detectadas · 100%)** |
| **Total Mutações Parede B** | Mutações B.1 + B.2 + B.2.2 + B.2.3 + B.2.4 + B.3.1 + B.3.2 + B.3.3 + B.4 | **PASS (186/186 detectadas · 100%)** |
| **Integridade de Codificação** | Inspeção de BOM UTF-8 | **Sem BOM (`BOM: False` em todos os arquivos)** |

---

## 3. Sub-etapa 4.0: Isolamento Puro de Stream (64 DW, Sem Context Regs)

Após a análise física do ensaio 4.4 no silício (`C:\BC250_DISPLAY_LOG.txt`), duas correções arquiteturais foram implementadas:
1. **Limpeza Prévia de VRAM / Writeback (Lei 2 & Lei 4)**:
   - `RtlZeroMemory` aplicado imediatamente sobre toda a abertura mapeada em `bc250_active_miniport.c`.
   - `*SubmitPlan->RptrWbCpuAddress` e `*SubmitPlan->WptrPollCpuAddress` zerados antes de qualquer escrita no anel, eliminando leituras fantasmas residuais de ensaios passados.
2. **Adição do Estágio de Isolamento Puro (Sub-etapa 4.0 / Stage 0)**:
   - Constante `BC250_GFX_ISOLATION_STAGE_4_0_NO_CONTEXT = 5` no contrato de transação.
   - O stream de 64 DW emite: Shaders VS+PS (12 DW) + 44 NOPs (Blocos 2..6) + Fence RELEASE_MEM (8 DW).
   - Validação e verificação de stream (`Bc250GfxVerify3DDrawStream`) estendidas e testadas.
   - Permite bissectar se o PFP avança o anel completo de 64 DW sem os pacotes de contexto.

---

## 4. Próximo Passo: Execução no Silício Físico

Executar o ensaio de isolamento no silício real da BC-250 com o PowerShell elevado:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -GfxRingBringupLevel 4 -GfxRingStage 0 -IAcceptAutomaticRecoveryReboot
```
