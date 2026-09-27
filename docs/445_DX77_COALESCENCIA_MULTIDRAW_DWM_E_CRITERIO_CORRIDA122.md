# Corrida 122 — Candidato DX.77: Coalescência de Multi-Draws no Caminho Direto da UMD e Redução de Flushes no DWM

> **Data:** 25/09/2026  
> **Candidato:** DX.77 / `3.281.1.0` (INF fonte `3.281.0.0`)  
> **Objetivo:** Eliminar o dreno síncrono incondicional antes de cada draw direto (`Bc250UmdVkDrainPending`), permitindo que os ~8 draws por quadro executados pelo DWM coalesçam em um único command buffer e sejam submetidos de forma consolidada no `Bc250DxgiPresent`, reduzindo as esperas de GPU de ~120 ms para ~15 ms por quadro.

---

## 1. Contexto e Diagnóstico da Corrida 121

Na [Corrida 121](444_CORRIDA121_DX76_UNIFICACAO_DRAW_DESTILE_E_RESULTADO.md) (DX.76):
- A unificação do Clear + Draw + Destile no command buffer foi **provada fisicamente** no harness `conformance.exe`: os flushes do harness caíram de 123 (Corrida 120) para **66** (46% de redução), operando com **exatamente 1 flush por quadro** do quadro 4 ao 60 (`DrawsNoLote=1`, `RingUsado=0`, sem dreno intermediário).
- A preservação dos pixels no `LinearPresentBuf` foi total (`Center(640,360)=0xFF7F4040`, 28.800/28.800 pixels não-nulos), com rollback online para `BasicDisplay` Código 0 em 5/5 amostras (R1).
- Contudo, a taxa de apresentação do DWM permaneceu restrita a **3,12 Presents/s** (561 Presents em ~180 s).
- A mineração detalhada de `bc250_umd_log_1204-FINAL.txt` (12,39 MB) revelou a causa raiz exata: o DWM executa aproximadamente 8 draws por quadro (composição de fundo, janelas, bordas, ícones, texto), e a cada draw a linha 3305 de [`driver/umd/bc250_d3d_umd.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/umd/bc250_d3d_umd.c#L3305) disparava:
  ```text
  [DX49_BATCH] DRENO antes de draw pelo caminho direto: DrawsPendentes=1
  ```
- Isso forçava um `vkEndCommandBuffer` + `vkResetFences` + `vkQueueSubmit` + `vkWaitForFences` a cada draw individual (~15 ms por draw, somando ~120 ms de stall de sincronização de GPU por quadro).

---

## 2. Alterações Implementadas no Candidato DX.77

1. **Eliminação do Dreno Incondicional no Caminho Direto (`Bc250UmdExecuteVkDraw`):**
   - Substituição da guarda `else if (pDevice->VkBridge.PendingDraws > 0 && ...)` por verificação de teto de capacidade real:
     ```c
     } else if (pDevice->VkBridge.PendingDraws >= BC250_DX49_BATCH_MAX_DRAWS ||
                (pDevice->VkBridge.PersistentPoolSets >= BC250_DX54_POOL_MAX_SETS - 2)) {
         if (Bc250UmdVkDrainPending(&pDevice->VkBridge, "teto do lote no caminho direto") != VK_SUCCESS) {
             pDevice->LastVkDrawFailureReason = "FLUSH_ANTES_DO_DRAW_FALHOU";
             return FALSE;
         }
     }
     ```
   - Draws subsequentes dentro da mesma composição do DWM (ou aplicativo) agora acumulam comandos no mesmo `VkCommandBuffer` sem interrupção de submit/fence.

2. **Proteção Contra Sobrescrita de Memória em `Bc250Umd10ResourceMap`:**
   - Se a aplicação chamar `ResourceMap` com `D3D10_DDI_MAP_WRITE_DISCARD` em um buffer com draws pendentes, a UMD drena o lote para garantir que o render em andamento conclua antes do descarte da memória.
   - Chamadas com `D3D10_DDI_MAP_WRITE_NOOVERWRITE` (como as que o DWM utiliza para streaming contínuo de vértices e índices com offsets crescentes) **não drenam**, garantindo fluidez total na gravação do lote.
   - Rastreamento expandido com `MapType` e `MapFlags` para observabilidade completa.

3. **Submissão Consolidada no Present:**
   - O lote de draws é finalizado, tem sua cópia de destilação para o `LinearPresentBuf` gravada e é submetido em um único submit em `Bc250DxgiPrepareScanout` (`Bc250UmdVkFlushCommands`), reduzindo as esperas de GPU de ~8 esperas para **1 única espera** por quadro.

---

## 3. Identificação do Pacote e Hashes de Controle

- **DriverVer Empacotado:** `09/25/2026,3.281.1.0` (INF fonte `3.281.0.0`)
- **Perfil:** `DISPLAY_RENDER_LAB`, `GfxWaveProbe=1`, `P1 PERSISTENT SUBMISSION`
- **Hashes SHA-256:**
  - `bc250mcdm.sys`: `7BDCA67C06FB3270A9F5D83E83F4EE21AE306128AA7D2756D2B74C1639F6DB40`
  - `bc250mcdm.inf`: `477B513B03DC06DA2CE65BA69165F965D4F76A898E5D336239A3A0851444E12E`
  - `bc250mcdm.cat`: `BA13F3FE88D1E26A122FB438AFC5AFF251B9618A5034E17AB315D46F48B98518`
  - `bc250umd.dll`: `53FC997C3C5C04B31E35077B081ABB02D99AFA6D51AA483E29310486071D45F5`
  - RADV externo: `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`
- **Validação Offline:**
  - `driver\wdk\build-contract.bat`: **EXIT 0** (40+ testes aprovados).
  - `driver\umd\build-umd.bat`: **EXIT 0** (DLLs e harnesses compilados e linkados).
  - `python tools\audit-bc250mcdm-safety.py`: **PASS**.
  - `powershell tools\audit-bc250mcdm-lab-package.ps1`: **PASS**.
  - `powershell tools\prepare-corrida122-dx77.ps1`: **PASS**.
  - `tools\EXECUTAR_CORRIDA122_DX77_180S.cmd`: **CRLF estrito validado**.

---

## 4. Critérios de Avaliação da Corrida 122

| Desfecho | Critério | Interpretação |
|---|---|---|
| **A (Avanço Pleno)** | (1) Janela de 180 s sem travamento, TDR ou BugCheck;<br>(2) Desktop legível e triângulo visível na janela do harness sem artefatos;<br>(3) Conformance harness com 60/60 `PRESENT_OK`;<br>(4) Log do DWM comprova coalescência: flushes por quadro caem para perto de 1 (média < 2 flushes/Present, `DrawsNoLote > 1` no Present);<br>(5) Taxa de apresentação do DWM supera o piso histórico de 3,12 Presents/s (alvo: > 6/s);<br>(6) Rollback online restaura `BasicDisplay` Código 0 em 5/5 amostras (R1). | Avanço funcional e de performance expressivo: eliminação do gargalo interno de renderização do DWM validada em silício. |
| **B (Avanço Parcial / Coalescência com Outro Gargalo)** | Mesmos critérios 1, 2, 3, 4 e 6 do Desfecho A, mas a taxa de apresentação do DWM não supera 4 Presents/s devido à latência do `pfnPresentCb` / VidMm scheduler. | Avanço arquitetural comprovado (coalescência funcional sem colisão), isolando o gargalo restante na fila de apresentação WDDM. |
| **C (Falha Visual / Conflito de Buffers)** | Execução sem crash, mas com corrupção de janelas, desktop piscando ou triângulo distorcido por colisão de memória em draws não drenados. | Refutação da hipótese de não-sobreposição nos buffers diretos; exigirá renomeação dinâmica de buffers. |
| **D (Falha Operacional / Crash / Hang)** | BugCheck, TDR, travamento duro antes dos 180 s ou falha no rollback R1. | Regressão de estabilidade; análise de dump e telemetria de fence. |

---

## 5. Procedimento de Execução Física

1. **Reinicialização a Frio Obrigatória:** Como a Corrida 121 realizou rollback, `C:\BC250_REQUIRE_CLEAN_BOOT.json` exige boot frio com corte de energia antes do novo bind.
2. Após o boot frio, executar como Administrador:
   ```cmd
   tools\EXECUTAR_CORRIDA122_DX77_180S.cmd
   ```
3. Observar visualmente o desktop e a janela do harness durante a janela de 180 s e aguardar a restauração automática do `BasicDisplay` pelo watchdog.
