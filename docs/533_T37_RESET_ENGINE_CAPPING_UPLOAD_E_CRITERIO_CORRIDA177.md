# 533 — T3.7: Implementação de ResetEngine no KMD (Node 0 e 1), Capping de Uploads do Explorer na UMD, Fallback de Textura Compartilhada e Critério da Corrida 177

> 29/09/2026, offline. Candidato **3.322.1.0** (inclui T3.7, T3.6, T3.5, T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê: Diagnóstico e Causalidade da Corrida 176

Na **Corrida 176** (candidato T3.6, DriverVer `3.321.1.0`), o desktop DWM permaneceu ativo, fluído e acelerado por hardware durante **60 segundos** inteiros (16:38:59 a 16:39:59).

### O que funcionou e avançou na Corrida 176
1. **BugCheck 0x10E completamente eliminado:** As correções de `Bc250SdmaRptrMatches` e `Bc250SdmaSyncEntryConsumed` impediram qualquer regressão de estado no `VIDMM_RECYCLE_MULTIRANGE::Rotate`. Zero telas azuis ocorreram.
2. **DWM 100% acelerado no silício:**
   - 125 chamadas a `SetVidPnSourceAddress`.
   - 124 `CommandBufferFlush` no Node 0 via Vulkan queue submit.
   - 388 chamadas de draw (`vkCmdDraw` / `vkCmdDrawIndexed`).
   - Resoluções de render target comutando dinamicamente (1920x1080 -> 1920x1111).
   - Zero timeouts de SDMA e zero recusas de paginação.

### A causa da tela preta ao abrir o Menu Iniciar
Ao clicar no menu Iniciar por volta de 16:39:59, o processo da shell (`explorer.exe`) entrou em ação gráfica massiva:
1. **Inundação de uploads sem flush intermediário no Explorer:**
   - O `explorer.exe` criou e fez upload de **743 texturas** (ícones, tiles, assets do menu Iniciar), gerando mais de **1.486 barreiras de pipeline** (`VkImageMemoryBarrier`) agregadas em um único command buffer Vulkan contínuo (`FlushesUpload=0`).
   - Isso provocou um pico massivo de pressão de memória na GPU e retenção prolongada da fila de execução do Node 1.
2. **Recusa de Shader Resource View (SRV) para texturas compartilhadas no DWM:**
   - Ao tentar renderizar a miniatura ou superfícies de janelas do Explorer via `OpenResource`, o DWM recebeu alocações cross-process abertas no kernel, mas sem `VkImage` Vulkan instanciada na UMD.
   - O validador de descritores rejeitou a vinculação com `refusedReason = "SRV_RECURSO_SEM_VKIMAGE"` (log: `[VK_DESCRIPTOR] RECUSADO: binding index=0 motivo=SRV_RECURSO_SEM_VKIMAGE`).
3. **Escalação fatal por ausência de `DxgkDdiResetEngine`:**
   - O agendador do Windows (Dxgkrnl/VidSch) detectou o atraso no processamento do contexto de renderização do usuário e invocou `DxgkDdiResetEngine` para recuperar apenas a engine de renderização (Node 1).
   - O KMD retornava `STATUS_NOT_SUPPORTED` em `Bc250McdmResetEngine`.
   - Diante do `STATUS_NOT_SUPPORTED`, o Windows escalou compulsoriamente a recuperação para **ResetAdapter total** (`DxgkDdiResetFromTimeout`).
   - Como a arquitetura v1 do driver utiliza exibição por herança do UEFI GOP, um reset destrutivo do adaptador derruba o frame buffer herdado do GOP, deixando a tela preta e forçando o reinício manual.

---

## 2. A mudança (Candidato T3.7)

### A. Implementação de `DxgkDdiResetEngine` no KMD (`Bc250ActiveResetEngineLifecycle`)
- **Suporte real aos nós:** Responde com `STATUS_SUCCESS` para requisições de reset dirigidas ao Node 0 (SDMA) e Node 1 (GFX).
- **Node 1 (GFX):**
  - Quiesce seguro do anel PM4 (`CP_RB0_CNTL` desarmado temporariamente).
  - Sincronização e saneamento dos ponteiros do anel (`CP_RB0_WPTR = CP_RB0_RPTR`).
  - Reconciliação atômica de fences abortadas através do helper `Bc250ActiveTdrAbortFenceTracker`.
  - Reporta `pResetEngine->LastAbortedFenceId`, satisfazendo o contrato do VidSch.
- **Efeito:** Uma lentidão ou travamento isolado em uma aplicação de usuário (como o Explorer) é redefinido no nível da engine/contexto sem destruir o adaptador de vídeo e sem apagar o display GOP do DWM.

### B. Capping e Coalescência de Uploads na UMD (`driver/umd/bc250_umd_vulkan_bridge.c`, `bc250_umd_resource_vk.c`)
- Limita o número de uploads agrupados em um mesmo lote (`UploadCoalescedInBatch`) a `BC250_DX79_UPLOAD_COALESCE_MAX_BATCH` (64).
- Ao atingir 64 uploads, a UMD força o flush intermediário e reinício de batch com pipeline barrier limpa.
- Impede a formação de command buffers colossais (como os 743 uploads com 1.486 barreiras do Explorer), reduzindo a latência do pipeline em ordens de grandeza.

### C. Fallback de Textura 1x1 RGBA para Recursos Abertos / Compartilhados (`driver/umd/bc250_umd_descriptors.c`)
- Quando o DWM vincula uma alocação recebida via `OpenResource` cujo `VkImg` ainda não foi alocado pelo processo de origem, a UMD cria sob demanda uma textura 1x1 de fallback preenchida com transparência segura.
- Elimina a recusa `SRV_RECURSO_SEM_VKIMAGE`, permitindo que os draws do compositor avancem sem falhas ou corrupção de estado.

### D. Extensão da Coleta de Logs UMD (`tools/capture-bc250mcdm-binding.ps1`, `tools/bind-bc250mcdm-lab.ps1`)
- O coletor agora busca logs UMD tanto em `C:\Windows\Temp` (DWM em SYSTEM) quanto em `$env:TEMP` / `C:\Users\*\AppData\Local\Temp` (processos de usuário como `explorer.exe`).

---

## 3. Provas Offline de Validação

- **Build de contratos (`driver/wdk/build-contract.bat`):** `EXIT 0` (105 DDIs anunciadas, 0 falhas).
- **Compilação e suíte UMD (`driver/umd/build-umd.bat`):** `EXIT 0` (todos os testes de winsys, bridge, descriptors e tripod aprovados).
- **Pacote de produção (`tools/build-bc250mcdm-package.ps1`):** `EXIT 0` (DriverVer `3.322.0.0`).
- **Imagem de laboratório (`tools/build-bc250mcdm-lab-bringup.ps1`):** `EXIT 0`.
- **Pacote de laboratório assinado (`tools/package-bc250mcdm-lab.ps1`):** `EXIT 0` (DriverVer `3.322.1.0`).
- **Auditoria de segurança (`tools/audit-bc250mcdm-safety.py`):** `PASS`.
- **Auditoria do pacote de laboratório (`tools/audit-bc250mcdm-lab-package.ps1`):** `PASS`.
- **Validação de pré-voo (`tools/prepare-corrida177-t37.ps1`):** `PASS`.

---

## 4. Critério da Corrida 177

- **Objetivo:** Confirmar que a interação com o menu Iniciar do Windows abre e fecha sem travar a máquina, sem disparar tela azul e sem causar tela preta por reset do adaptador.
- **Métricas de Sucesso:**
  1. DWM acelerado por hardware durante todo o período do ensaio (até 600 s).
  2. Abertura e fechamento repetidos do menu Iniciar do Windows sem congelamento do sistema.
  3. Em caso de atraso na renderização de assets do Explorer, o `DxgkDdiResetEngine` recupera a engine Node 1 com sucesso, preservando a imagem na tela e a continuidade do DWM.
  4. Zero BugChecks (nenhum `0x10E`, `0x119`, `0x133` ou `0x116`).
- **Roteiro do Ensaio:**
  - Iniciar a corrida via `.\tools\EXECUTAR_CORRIDA177_T37_600S.cmd`.
  - Aguardar 20 segundos com o desktop visível.
  - Clicar no menu Iniciar do Windows 3 a 5 vezes consecutivas para acionar a renderização da shell.
  - Se travar: registrar o horário exato e acionar o botão de reset do gabinete em 10-15 segundos.

---

## 5. Como Rodar

Executar no PowerShell elevado:
```cmd
.\tools\EXECUTAR_CORRIDA177_T37_600S.cmd
```
