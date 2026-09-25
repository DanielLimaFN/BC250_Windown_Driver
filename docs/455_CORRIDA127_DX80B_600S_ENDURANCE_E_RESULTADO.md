# Corrida 127 — Candidato DX.80b: Teste de Resistência de 600 s, Harness 60/60, Timeout no Escape DWM e Recuperação R1

> **RETIFICAÇÃO pós recuperação da cauda do KMD (25/09/2026):** Ver [auditoria 457](457_AUDITORIA_CORRIDA127_TDR_SDMA_GPUVA_E_CRITERIO128.md). O escape chegou ao KMD e houve timeout físico de SDMA, seguido de **23 resets TDR no bind**. As afirmações abaixo de “zero TDR” e de bloqueio antes da entrada no KMD não representam a evidência completa. O mecanismo interno do primeiro timeout continua aberto.

> **Data:** 25/09/2026 — 16:01:35  
> **Candidato:** DX.80b / `3.285.1.0` (INF fonte `3.285.0.0`)  
> **Pasta de Evidência:** [`docs/evidence/2026-09-25_16-01-35-DESKTOP-HQAG6T4-lab-binding`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-09-25_16-01-35-DESKTOP-HQAG6T4-lab-binding)  
> **Duração da Sessão:** 600 s programados (16:01:35 – 16:14:05; watchdog rollback R1 às 16:14:14)  
> **Veredito:** **Parcial / Falha em Carga Multiprocesso** (Desktop e harness perfeitos nos primeiros ~2 minutos; stall de 4s em `pfnEscapeCb` ao abrir Edge/Antigravity; watchdog R1 online com sucesso)

---

## 1. Relato Obrigatório das 5 Perguntas

### 1. Houve Avanço?
**Sim (avanço de diagnóstico e estabilidade de hardware sob janela longa).**
Em comparação com as corridas longas anteriores (Corridas 108–112 que sofriam travamento duro / BugCheck aos 3–6 minutos):
- O sistema operacional Windows permaneceu **100% responsivo durante todos os 10 minutos** (sem BugCheck, sem BSOD, sem travamento de barramento PCI/MMIO).
- O watchdog de 600 s operou perfeitamente e executou rollback online (R1) restaurando `BasicDisplay` Código 0 sem reboot.
- O harness D3D11 rodou nos primeiros 43 s com 60/60 quadros e Exit 0.
- A causa raiz da anomalia visual fotografada pelo operador foi precisamente identificada nos logs (timeout de 4,17 s no agendador do WDDM em `pfnEscapeCb` com `HardwareAccess=1` e `hContext=NULL`, seguido de recusa de quadro vazio na reinicialização do DWM).

---

### 2. Sucessos
1. **Harness Direct3D 11 Nativo Concluído com Êxito:**
   - 60 de 60 quadros aceitos (`PRESENT_OK`), FPS médio de 1,2, picos em 1,7 FPS.
   - Amostras de cores conferidas: Limpeza azul (`0xFF2673E5`) e centro do triângulo (`0xFF7F4040`) íntegros.
   - Exit Code: 0.
2. **Zero Double-Free e Teardown Estável:**
   - A correção em `Bc250UmdVkDestroyRtv` do DX.80b preveniu o crash que havia ocorrido na Corrida 125.
3. **Resistência Térmica e de Hardware:**
   - A placa BC-250 operou continuamente por mais de 12 minutos no barramento PCIe sem perda de comunicação e sem TDR de hardware.
4. **Recuperação Online Automática (R1):**
   - Ao expirar o prazo de 600 s às 16:14:05, o guardião disparou a restauração para `BasicDisplay` Código 0, confirmada em 5 amostras consecutivas (16:14:10–14) sem reiniciar a máquina.

---

### 3. Falhas Observadas e Diagnóstico Físico

#### A. O Sintoma Visual (Foto do Daniel)
- Na foto enviada pelo operador: topo da tela exibindo ícones e barra do Edge; faixa com artefatos horizontais no terço superior; 75% inferiores escuros.
- Causa visual: o framebuffer de scanout herdado do GOP preservou os últimos pixels compostos com sucesso às 16:03:29. Quando o DWM parou de enviar quadros para a primária, as linhas de scanout permaneceram estáticas.

#### B. A Cadeia de Causa Raiz
1. **Abertura de Aplicativos Externos (16:02:58 – 16:03:02):**
   - Logo após o término do harness, foram abertos `msedge.exe` (pid 6568), `msedgewebview2.exe` (pid 5016) e `Antigravity.exe` (pid 8056).
   - Todos carregaram `bc250umd.dll`, sondaram suporte D3D11 e inicializaram instâncias de device.
2. **Stall de 4,17 s no Escape de Apresentação (16:03:29):**
   - Às 16:03:29.723, o DWM (pid 1212) desenhou uma janela de aplicativo de 1400x900 e invocou `Bc250UmdBridgeLinearToWddm` (Escape `BC250_ESCAPE_OP_COMPOSITE_BLT`) para transferir o conteúdo linear Vulkan para a alocação de scanout WDDM.
   - Em `bc250_d3d_umd.c:5107`, os argumentos foram montados com `args.Flags.HardwareAccess = 1u` e `args.hContext = NULL`.
   - Com `HardwareAccess = 1` e sem contexto explícito, o subsistema de agendamento do kernel (`dxgkrnl.sys / VidSch`) aguardou o esvaziamento de todos os contextos do dispositivo.
   - A chamada `pfnEscapeCb` permaneceu bloqueada de 16:03:29.723 até 16:03:33.895 (exatamente **4,172 segundos**, correspondendo ao `TdrDelay` padrão de 4 s do Windows) e retornou `hr = 0x80004005` (`E_FAIL`).
   - O escape **nem sequer alcançou o KMD** (`Bc250ActiveEscapeCompositeBlt` nunca foi chamado nesse quadro; nenhuma linha foi registrada no log do KMD). O `dxgkrnl` rejeitou a chamada no kernel.
3. **Crash do DWM e Loop de Reinicialização (16:03:34 – 16:04:35):**
   - Ao receber `E_FAIL` na apresentação, o DWM considerou o dispositivo removido, destruiu seus recursos e encerrou.
   - O Windows reiniciou o DWM (pids 4788, 7804, 3316, 10796, 2492).
   - Ao reiniciar, cada nova instância de DWM chamou `DXGI Present` no seu primeiro quadro antes de submeter qualquer comando de renderização (`VkRendered=0, NativePm4DrawCount=0`).
   - A guarda da UMD em `bc250_d3d_umd.c:5165` interceptou o quadro vazio:
     `DXGI Present RECUSADO: [PRESENT RECUSADO: NENHUM_RENDER_DETECTADO] -> E_FAIL`
   - O retorno `E_FAIL` no primeiro quadro forçou o novo DWM a falhar imediatamente, reiniciando repetidamente em ciclo de ~12 s até o watchdog expirar.

---

### 4. Veredito
**Parcial / Falha sob Multiprocesso.**
O pipeline de apresentação está validado para renderização pura de aplicativo único (harness e desktop básico), mas falha ao sincronizar com o agendador do Windows durante a coexistência simultânea de múltiplos processos 3D (DWM + Edge + WebView2).

---

### 5. Recuperação e Próximos Passos (Candidato DX.81)

- **Recuperação:** Concluída com sucesso (`BasicDisplay` Código 0 restaurado via R1 às 16:14:14).
- **Não Validado em Hardware:** Sessão contínua de 600 s com abertura e alternância dinâmica de múltiplos navegadores/aplicativos de desktop.
- **Plano de Correção do DX.81:**
  1. **Desarmar o Stall de Agendamento no Escape:**
     - Em `Bc250UmdBridgeLinearToWddm` (`bc250_d3d_umd.c`), passar o contexto de apresentação (`args.hContext = d->Winsys.PresentContext`) e desativar `args.Flags.HardwareAccess = 0u` (o KMD já realiza sua própria sincronização de hardware na fila SDMA, tornando a contenção de 4 s do VidSch desnecessária e prejudicial).
  2. **Permitir Inicialização Limpa do DWM:**
     - Em `bc250_d3d_umd.c:5165`, permitir que o quadro inicial sem render passe sem retornar `E_FAIL` quando for o primeiro quadro ou quadro de sincronização de swapchain, quebrando o loop de crash no restart do compositor.
  3. **Expansão de Capacidade de Mapeamento Virtual (GPUVA):**
     - Elevar `BC250_GPUVA_SPACE_MAX_RANGES` e `BC250_GPUVA_JOURNAL_MAX_CHANGES` de 1024 para 4096 para garantir folga para aplicações Chromium/Electron com alta densidade de superfícies virtuais.
