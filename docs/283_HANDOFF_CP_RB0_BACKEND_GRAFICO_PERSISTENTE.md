# docs/283 — Handoff: Backend Gráfico Real (CP_RB0 / PFP / ME) no KMD e Recuperação TDR

**Data:** 16/09/2026  
**Status:** Backend Gráfico Real `CP_RB0` implementado no KMD, contratos offline aprovados (`build-contract.bat` Exit 0); **NÃO VALIDADO EM HARDWARE**.  
**Alvo:** AMD BC-250 APU (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 Pro 22H2 x64  
**Diretrizes:** `DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`, `BASE_DE_AVANCO.md`  

---

## 1. Relatório Mandatório da Corrida Física Anterior

Conforme a diretriz do proprietário de 16/09/2026, segue o registro obrigatório de 5 pontos da corrida física de 16/09 09:26:14:

1. **Houve avanço?**  
   **Inconclusivo funcionalmente / Avanço no diagnóstico.** Não houve pixels 3D gerados no display, mas identificou-se com precisão de registrador a causa raiz da falha que impedia o avanço do pipeline gráfico.

2. **Sucessos:**  
   - A divisão por zero (`0xC0000094`) foi superada no escopo dos casos testados (layouts multi-slot DWM slots 0..4).  
   - 238 submissões de paginação SDMA concluídas com tripé completo no Node 0 sob VMID 8 (`Submitted=238, Retired=238, Divergentes=0`).  
   - 1 Blt SDMA de 640×640 executado em 697 µs (`Fence=000010F0`).

3. **Falhas:**  
   - **Zero pixels 3D comprovados.**  
   - **BugCheck `0x116` (`VIDEO_TDR_ERROR`)** em `watchdog.sys` / `dxgkrnl.sys`.  
   - O microcode do MEC (Compute) travou em `TC_UTCL1_ERROR 0x00080000` (`CpStalled1 0x00000C00`) ao receber pacotes gráficos PM4 3D emitidos pela UMD via winsys do Mesa RADV (`vkQueueSubmit`). Ao tentar processar o comando gráfico, o fetcher do MEC disparou leitura para o endereço físico do próprio anel (`0xF40FFA0000`) sob **VMID 9**, gerando a falta de proteção GCVM `FaultStatus=0x00900B32` (`ADDR=0x0F40FFA0`).

4. **Veredito:**  
   **FALHA frente ao critério de desktop acelerado / D3D11.** Demonstrou de forma inequívoca que comandos gráficos 3D não podem ser despachados para a fila assíncrona do MEC.

5. **Recuperação e pendências:**  
   - O fusível anti-boot-loop funcionou perfeitamente no reboot (`C:\BC250_BOOT_FUSE.dat` em `State=2 TRIPPED`), prevenindo boot loop e restaurando o adaptador em Código 43 / `BasicDisplay`.  
   - **O que continua NÃO VALIDADO EM HARDWARE:** Execução de pacotes gráficos PM4 no anel gráfico real `CP_RB0`, rasterização física de pixels (triângulo/cubo), backend D3D11 em hardware e composição acelerada pelo DWM.

---

## 2. Decisão Arquitetural: Encerramento Definitivo de Emulação por Compute

O projeto encerra formalmente qualquer tentativa de emular ou rasterizar gráficos 3D via compute shaders ou despachar pacotes gráficos para a fila MEC.  
A arquitetura WDDM estabelece a separação estrita de motores físicos:
- **Node 0 (COPY):** Motor SDMA (`DXGK_ENGINE_TYPE_COPY`).
- **Node 1 (3D GRAPHICS):** Anel gráfico real `CP_RB0` acionado pelo Command Processor (PFP/ME) sob **VMID 8** (`DXGK_ENGINE_TYPE_3D`).
- **Node 2 (COMPUTE):** MicroEngine Compute (MEC) na HQD (`DXGK_ENGINE_TYPE_COMPUTE`).

**Garantia Fail-Closed:** Se `GfxEngine.State != 1`, qualquer submissão para o Node 1 é rejeitada imediatamente com `STATUS_DEVICE_NOT_READY` e telemetria explícita. **É estritamente proibido redirecionar trabalho do Node 1 para o MEC.**

---

## 3. Implementação Concluída no KMD

### A. Motor Gráfico Persistente `CP_RB0` (`bc250_active_miniport.c/.h`)
- Alocação estática de buffer de anel de 64 KiB e aux fence de 4 KiB em VRAM física.
- Mapeamento MMIO dos registradores `CP_RB0_BASE`, `CP_RB0_BASE_HI`, `CP_RB0_CNTL`, `CP_RB0_RPTR`, `CP_RB0_WPTR`.
- Inicialização do Command Processor gráfico em `Bc250ActiveGfxEngineOnline`:
  - Configura tamanho do anel (`BUF_SIZE=13` -> 64 KiB, `RB_NO_UPDATE=1`).
  - Des-halta os microengines PFP e ME (`CP_ME_CNTL = 0`).
  - Ativa o anel (`CP_RB0_CNTL |= 1`).
  - Valida leitura de hardware de registradores e cursores zerados.
- Desativação limpa em `Bc250ActiveGfxEngineOffline`:
  - Aguarda drenagem, desativa o anel, desfaz mapeamentos kernel e libera alocação VRAM.

### B. Submissão e Despacho Indireto sob VMID 8
- `Bc250ActiveGfxEngineSubmit`:
  - Emite `PACKET3_INDIRECT_BUFFER` (`0x3F`) apontando para a GPUVA do command buffer do dxgkrnl/UMD configurado sob VMID 8 (`BC250_GCVM_VMID8`).
  - Emite `PACKET3_RELEASE_MEM` (`0x49`) para sinalizar término de execução (EOP) gravando a cerca WDDM de 32 bits em VRAM física.
  - Executa flush de cache HDP (`HDP_MEM_COHERENCY_FLUSH_CNTL`).
  - Avança o registrador de hardware `CP_RB0_WPTR`.

### C. Aposentadoria de Fences por Motor (`bc250_submission_engine.c/.h`)
- Implementado `Bc250WddmFenceTrackerDrainEx(Tracker, RetiredMecFence, RetiredGfxFence, Out, Max)`:
  - Fences de submissões no Node 1 são confrontadas contra `RetiredGfxFence`.
  - Fences de submissões no Node Compute são confrontadas contra `RetiredMecFence`.
  - Drena de forma desacoplada e estritamente auditada per-engine.
- Integrado em `Bc250ActiveRetireAndNotify`: pesquisa `GfxEngine.LastRetiredFence` e notifica o scheduler WDDM via `DxgkCbNotifyInterrupt`.

### D. Ciclo de Vida TDR com `GRBM_SOFT_RESET`
- `Bc250ActiveTdrQuiesceLifecycle`:
  - Captura estado forense de registradores (`CP_RB0_ACTIVE`, `RPTR`, `WPTR`, `GRBM_STATUS`, `CP_ME_CNTL`).
  - Desativa o anel (`CP_RB0_ACTIVE = 0`).
  - Emite pulso de soft-reset em `GRBM_SOFT_RESET` (`0x08020`) com as máscaras `SOFT_RESET_CP`, `SOFT_RESET_GFX`, `SOFT_RESET_CPF` e `SOFT_RESET_CPG` por 50 µs.
  - Marca `GfxEngine.State = 0` (fail-closed).
- `Bc250ActiveTdrRestartLifecycle`:
  - Reprograma bases e cursores do anel `CP_RB0`.
  - Zera memória física do anel e fence na VRAM.
  - Libera microengines (`CP_ME_CNTL = 0`) e reativa anel (`CP_RB0_ACTIVE = 1`).
  - Validação estrita de readback via `Bc250TdrValidateGfxRestartReadback`.

---

## 4. Verificação Offline

- `driver/wdk/build-contract.bat`: **EXIT = 0**
- Testes de unidade de contrato:
  - `tdr_recovery_contract_test.exe`: 4 suítes, **PASS**.
  - `active_miniport_test.exe`: mocks MMIO compilados e linkados, **PASS**.
  - 45 suítes de teste de contrato executadas com sucesso sem regressões.

---

## 5. Próximo Passo

1. Atualizar `BASE_DE_AVANCO.md` com a nova corrida e a remoção de emulação compute.
2. Compilar e empacotar a imagem com o backend `CP_RB0` ativo.
3. Submeter a corrida de validação do anel gráfico em hardware no silício sob supervisão do Daniel.
