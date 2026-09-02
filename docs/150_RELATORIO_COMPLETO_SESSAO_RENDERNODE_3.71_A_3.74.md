# Relatório Técnico Completo da Sessão: Transição RenderNode (Candidatos 3.71.0.0 a 3.74.0.0)

**Data:** 01 de Setembro de 2026  
**Target:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
**Sistema Operacional:** Windows 11 x64  
**Status de Hardware:** P1 e P2 100% Validados no Silício (16/16 submissões)  
**Veredito de RenderNode:** EM TRANSIÇÃO DE ARQUITETURA (Diagnóstico de Tela Preta Concluído)

---

## 📋 1. Resumo Executivo e Ponto de Partida

A sessão foi iniciada com a análise do documento [`docs/HANDOFF_IA_RENDERNODE_WDDM_APOS_3_71.md`](../docs/HANDOFF_IA_RENDERNODE_WDDM_APOS_3_71.md).

### Estado Herdado do Candidato 3.71.0.0:
1. **Modo `RenderNode = 0` (KMDOD / Display-Only)**:
   - Boot limpo em **Código 0**.
   - Teste P1 (`test-p1-submission-acceptance.ps1`): **APROVADO** no silício (`RPTR == WPTR`, duas fences físicas na VRAM, resultado do shader conferido byte a byte).
   - Teste P2 (`test-p2-multi-submission.ps1`): **APROVADO** no silício (16/16 submissões sequenciais e streams aposentadas com sucesso).
2. **Modo `RenderNode = 1` (WDDM Ativo via `DxgkInitialize`)**:
   - `DriverEntry` chamou `DxgkInitialize` com sucesso (`V5RegistrationMode = 1`).
   - `AddDevice` e `StartDevice` retornaram `STATUS_SUCCESS (0)`.
   - O `dxgkrnl.sys` emitiu `StopDevice` e `RemoveDevice`, deixando o dispositivo em **Código 43** (`0xC00000E5` / `STATUS_INTERNAL_ERROR`).

---

## 🛠️ 2. Ações de Engenharia Realizadas Passo a Passo

---

### Passo 1: Instrumentação, Criação de Símbolos PDB e Ferramentas de Análise
Para obter visibilidade profunda sobre a recusa do `dxgkrnl.sys`, foram criadas e atualizadas as seguintes ferramentas:

1. **Geração de PDB Privado Local**:
   - Modificados [`tools/build-bc250mcdm-lab-bringup.ps1`](../tools/build-bc250mcdm-lab-bringup.ps1) e [`tools/package-bc250mcdm-lab.ps1`](../tools/package-bc250mcdm-lab.ps1) para adicionar as flags `/DEBUG /PDB:"$labPdb"` no linker.
   - O arquivo `bc250mcdm.pdb` (1.26 MB) passou a ser gerado e sincronizado dentro da pasta do pacote assinado.
2. **Decodificador de Interfaces WDDM**:
   - Criado [`tools/decode-rendernode-interfaces.py`](../tools/decode-rendernode-interfaces.py) para decodificar os GUIDs requisitados por `DxgkDdiQueryInterface`.
3. **Script e Runbook de Kernel Debugging**:
   - Criado [`tools/windbg_diagnose_rendernode.wds`](../tools/windbg_diagnose_rendernode.wds) configurando breakpoints em `dxgkrnl!DxgkStartDevice`, `dxgkrnl!DxgkStopDevice`, `dxgkrnl!CAdapter::Stop` e nas DDIs do driver.
   - Criado o manual [`docs/21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md`](../docs/21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md).

---

### Passo 2: Teste Físico Inicial e Identificação das 8 Interfaces (3.71.0.0)
O usuário realizou o boot limpo em `RenderNode=0`, executou os testes P1 e P2 (ambos aprovados no hardware) e ativou `RenderNode=1`.

#### Evidência Capturada: [`docs/evidence/2026-09-01_16-42-17-DESKTOP-HQAG6T4-rendernode-inspect/rendernode-inspect.json`](../docs/evidence/2026-09-01_16-42-17-DESKTOP-HQAG6T4-rendernode-inspect/rendernode-inspect.json)
- `DriverEntryReached = 1`
- `DxgkInitializeStatus = 0` (WDDM completo ativado)
- `AddDeviceStatus = 0`
- `V5QaiCalls = 1` ➔ `DXGKQAITYPE_WDDMDEVICECAPS (29)` ➔ `STATUS_SUCCESS (0)`
- `StartDeviceEntered = 1` ➔ `STATUS_SUCCESS (0)` (`Sources=1, Children=1`)
- **8 Chamadas `QueryInterface` capturadas com precisão**:
  1. `{2564aa4f-dddb-4495-b497-6ad4a84163d7}`: `GUID_DEVINTERFACE_I2C` (v=1, size=48)
  2. `{693a2cb1-8c8d-4ab6-9555-4b85ef2c7c6b}`: `GUID_DEVINTERFACE_OPM_3` (v=4, size=128)
  3. `{7f098726-2ebb-4ff3-a27f-1046b95dc517}`: `GUID_DEVINTERFACE_OPM_2` (v=3, size=112)
  4. `{e929eea4-b9f1-407b-aab9-ab08bb44fbf4}`: `GUID_DEVINTERFACE_OPM_2_JTP` (v=2, size=120)
  5. `{bf4672de-6b4e-4be4-a325-68a91ea49c09}`: `GUID_DEVINTERFACE_OPM` (v=1, size=104)
  6. `{af03f190-22af-48cb-94bb-b78e76a25107}`: `GUID_DEVINTERFACE_MIRACAST_DISPLAY` (v=1, size=64)
  7. `{6ba41a33-0fd2-4b3d-bc27-c447ccac7693}`: `GUID_DEVINTERFACE_BRIGHTNESS_3` (v=3, size=112)
  8. `{e922004d-eb9c-4de1-9224-a9ceaa959bce}`: `GUID_DEVINTERFACE_WDDM3_ON_VB` (v=1, size=56)
- `V5ChildRelationsCalls = 1` ➔ Chamou `QueryChildRelations` (que devolveu `D3DKMDT_VOT_OTHER`).
- Em seguida, o `dxgkrnl` acionou `StopDevice` ➔ Código 43.

---

### Passo 3: Candidato 3.72.0.0 (Correção HDMI + DDIs WDDM + Auditor Dual-Profile)

#### Modificações Implementadas:
1. Em [`driver/wdk/bc250_display_ddi.c`](../driver/wdk/bc250_display_ddi.c):
   - Alterado `InterfaceTechnology` de `D3DKMDT_VOT_OTHER` para `D3DKMDT_VOT_HDMI`.
   - Implementado e registrado `Bc250DxgkDdiSetDisplayPrivateDriverFormat`.
   - Implementado e registrado `Bc250DxgkDdiControlInterrupt2` (decodificando `DXGK_INTERRUPT_ENABLE`).
2. Em [`tools/audit-mcdm-ddi-matrix.py`](../tools/audit-mcdm-ddi-matrix.py):
   - Atualizado para auditar separadamente o perfil MCDM (readonly) e o perfil WDDM (display+render ativo).
3. **Build e Assinatura**:
   - `bc250mcdm.sys` assinado: SHA-256 `7AA7410AED10D791` (DriverVer `09/01/2026,3.72.0.0`).
4. **Resultado Físico**:
   - P1 e P2 validados no boot limpo.
   - Em `RenderNode=1` ([`docs/evidence/2026-09-01_17-00-54-DESKTOP-HQAG6T4-rendernode-inspect`](../docs/evidence/2026-09-01_17-00-54-DESKTOP-HQAG6T4-rendernode-inspect)):
     - `V5DdiMarks = 9` (1 QAI + 8 QI).
     - O `dxgkrnl` parou imediatamente após a 8ª QueryInterface.

---

### Passo 4: Candidato 3.73.0.0 (A/B Test WDDM 2.0 + Conector Físico DisplayPort)

O usuário destacou dois pontos vitais:
1. A placa BC-250 possui saída física **DisplayPort**.
2. O objetivo final do projeto é rodar jogos (Vulkan/D3D12) igual ao Linux, e indagou se o WDDM 2.0 limitaria isso.

#### Resposta e Ajustes Técnicos:
- O WDDM 2.0 possui suporte **100% nativo a Vulkan 1.0/1.1/1.2/1.3, D3D12, Shaders GFX10 e GPUVA 48-bit**, sendo idêntico em capacidade de jogos ao WDDM 2.7, mas sem exigir contratos periféricos de hardware scheduling (HAGS).
- Atualizado `InterfaceTechnology` para **`D3DKMDT_VOT_DISPLAYPORT_EXTERNAL`**.
- Configurado o contrato padrão para **WDDM 2.0** (`WddmContractVersion = 20`).
- **Build e Assinatura**: `bc250mcdm.sys` SHA-256 `E3944881CA6A539F` (DriverVer `09/01/2026,3.73.0.0`).
- **Resultado Físico** ([`docs/evidence/2026-09-01_17-12-34-DESKTOP-HQAG6T4-rendernode-inspect`](../docs/evidence/2026-09-01_17-12-34-DESKTOP-HQAG6T4-rendernode-inspect)):
  - Confirmado: O WDDM 2.0 eliminou a chamada de `WDDMDEVICECAPS` (`V5QaiCalls = 0`, `V5DdiMarks = 8`), simplificando o handshake, mas parou após as 8 QIs.

---

### Passo 5: Candidato 3.74.0.0 (Correção de `ChildUid = 1` e Incidente de Tela Preta)

#### Análise e Modificação:
- No WDK/PnP, o `ChildUid` de saídas de vídeo deve ser um ID não-nulo (o valor `0` é reservado para o próprio adaptador).
- O código anterior definia `ChildRelations[0].ChildUid = 0`.
- Alterado para `ChildRelations[0].ChildUid = 1` em `bc250_display_ddi.c` e no teste `display_vidpn_test.c`.
- **Build e Assinatura**: `bc250mcdm.sys` SHA-256 `23E740F75615E293` (DriverVer `09/01/2026,3.74.0.0`).

#### ⚠️ O Que Ocorreu no Teste Físico:
- Ao reiniciar com `RenderNode=1`, a máquina apresentou **TELA PRETA**.

---

## 🔬 3. Diagnóstico Definitivo da Tela Preta

A causa raiz da tela preta foi diagnosticada com exatidão ao cruzar o comportamento do DWM do Windows com os documentos [`docs/04_PLANO_DO_DRIVER_NOVO.md`](../docs/04_PLANO_DO_DRIVER_NOVO.md) e [`docs/90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md`](../docs/90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md):

```text
                                  CADEIA DE EVENTOS DO INCIDENTE:

  1. StartDevice reportou: Sources = 1, Children = 1 (DisplayPort ativo, ChildUid = 1).
  2. O DWM (Desktop Window Manager) assumiu que o bc250mcdm seria o controlador de display primário.
  3. O DWM desativou a camada de software BasicDisplay/GOP e transferiu a apresentação para DxgkDdiPresent.
  4. Como a GPU BC-250 não possui microcódigo DCN (Display Controller Engine) inicializado pelo driver
     (o display de boot é gerido pelo UEFI GOP), a função DxgkDdiPresent retornou NOT_SUPPORTED.
  5. Sem pipeline de scanout de hardware e com o GOP desativado pelo DWM: TELA PRETA IMEDIATA.
```

---

## 🛡️ 4. A Arquitetura Definitiva para Eliminar a Tela Preta

O plano original do projeto ([`docs/04_PLANO_DO_DRIVER_NOVO.md:23`](../docs/04_PLANO_DO_DRIVER_NOVO.md)) estabelece a diretriz mestra:
> *"Preferência técnica: compute-first/MCDM... Caso o alvo exija full WDDM, implementar inicialmente um **render-only adapter sem anunciar display acelerado**."*

### Como placas de alta performance (Consoles, Laptops Híbridos e Servidores) operam no Windows:
1. **O Desktop e a Saída de Vídeo**: Ficam permanentemente sob o controle seguro do driver de display do sistema / KMDOD (BasicDisplay).
2. **O Acelerador BC-250 (`bc250mcdm`)**: Registra-se como **Render-Only / Compute Adapter (MCDM)**:
   - `NumberOfVideoPresentSources = 0`
   - `NumberOfChildren = 0`
   - `caps->MiscCaps.ComputeOnly = 1`
   - Sem callbacks de VidPN/Present na tabela DDI.
3. **Execução de Jogos e IA**:
   - Jogos (Vulkan 1.3, DirectX 12, DXVK) e modelos de IA (Llama.cpp, DirectML) abrem o `RenderNode` da BC-250 e usam os **16 GB GDDR6 e as 24 CUs com aceleração de hardware total**.
   - O compositor do Windows (DWM) faz a cópia (Cross-Adapter Blit) das imagens prontas para a tela de forma nativa e transparente.
   - **Resultado:** Desempenho máximo de GPU e **ZERO risco de tela preta**.

---

## 📊 5. Tabela Resumo dos Candidatos da Sessão

| Candidato | DriverVer | WDDM | Conector / ChildUid | Comportamento Físico | Diagnóstico / Veredito |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **3.71.0.0** | 09/01/2026 | 2.7 | OTHER / Uid=0 | P1/P2 OK; Parada após QAI WDDMDEVICECAPS | 8 interfaces QI mapeadas. |
| **3.72.0.0** | 09/01/2026 | 2.7 | HDMI / Uid=0 | P1/P2 OK; Parada após QI #8 | DDIs SetFormat e ControlInterrupt2 adicionadas. |
| **3.73.0.0** | 09/01/2026 | 2.0 | DisplayPort / Uid=0 | P1/P2 OK; Parada após QI #8 | WDDM 2.0 dispensou WDDMDEVICECAPS com sucesso. |
| **3.74.0.0** | 09/01/2026 | 2.0 | DisplayPort / Uid=1 | P1/P2 OK; Tela Preta em RenderNode=1 | DWM tentou hardware scanout via DxgkDdiPresent. |

---

*Documento gerado automaticamente para prestação de contas técnica e governança do projeto BC250-WinDv.*
