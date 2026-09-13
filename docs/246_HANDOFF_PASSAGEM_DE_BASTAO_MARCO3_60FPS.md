# docs/246_HANDOFF_PASSAGEM_DE_BASTAO_MARCO3_60FPS.md
# PASSAGEM DE BASTÃO TÉCNICO — MARCO 3: SWAPCHAIN VULKAN E O CAMINHO ATÉ OS JOGOS NA BC-250

> **Destinatário:** Próxima IA / Engenheiro de Sistemas assumindo o desenvolvimento.  
> **Alvo de Hardware:** APU AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64.  
> **Data:** 12 de Setembro de 2026.  
> **Estado do Fusível de Boot:** `C:\BC250_BOOT_FUSE.dat` em estado `CLEAN (0)` (Desligamento Limpo).  
> **Integridade dos Gates de Segurança:** 22/22 PASS (`tools/audit-bc250mcdm-safety.py`).  
> **Suíte de Contratos C:** 45/45 PASS, EXIT=0 (`driver/wdk/build-contract.bat`).

---

## 🎯 1. Onde Queremos Chegar (Visão do Projeto)

O objetivo final imutável deste repositório é:
**Jogos reais no Windows 10 22H2 rodando na APU AMD BC-250 (24 CUs, 16 GB GDDR6), com monitor conectado diretamente na saída física DisplayPort da placa, via Vulkan e Direct3D 12, SEM WARP e SEM fallback de emulação de software.**

### O Roadmap em Marcos:
- **Marco 1 [CONCLUÍDO NO SILÍCIO - P0]**: Execução de Compute Shader real via Mesa RADV / ACO na fila MEC Node 1 (HQD), fence física em VRAM real, byte proof aprovado (`0xBC250C01`), zero desvio de CPU.
- **Marco 2 [CONCLUÍDO NO SILÍCIO - P0]**: Renderização de tela cheia (2560×1080) com barras SMPTE + badge e apresentação física direta no DisplayPort via motor de hardware SDMA0 (`Bc250SdmaPresentBlt`) herdando o scanout do UEFI GOP em VRAM (`0xF400000000`). Confirmado visualmente pelo operador humano.
- **Marco 3 [EM ANDAMENTO AVANÇADO - 447 QUADROS CONSECUTIVOS NO SILÍCIO]**: Swapchain contínuo de animação fluida a 60 FPS (600 quadros).
- **Marco 4 [PRÓXIMO BLOQUEADOR]**: Suporte ao Desktop Window Manager (DWM) através de backend D3D10/11 nativo na UMD (`bc250umd.dll`), eliminando a necessidade de harness dedicado.
- **Marco 5 [OBJETIVO FINAL]**: Execução de títulos de jogos comerciais via DXVK / Vulkan nativo / D3D12.

---

## 📊 2. O Que Foi Feito e Resultados Conquistados

### 2.1. Superação do Gargalo de CPU no Marco 2.5
- **Problema anterior:** Cada quadro demorava ~440 ms (taxa de apenas 2.2 FPS) porque uma chamada a `RtlCopyMemory` em `bc250_active_miniport.c:9217` lia 11 MB de memória de vídeo Write-Combined / Uncached através da CPU a ~25 MB/s.
- **Solução implementada:** O fallback de CPU foi restrito exclusivamente ao Frame 1. Para todos os frames seguintes (2 a 600), o envio ocorre **100% via SDMA de hardware** em menos de 0.7 ms.
- **Resultado:** A latência por quadro despencou de 544 ms para **33-36 ms** (~26.5 FPS).

### 2.2. Resolução Definitiva do Travamento do Anel SDMA (Wrap-Around)
- **Problema diagnosticado (Corrida das 10:10):** Ao atingir o fim da 2ª volta do anel de 1024 dwords (4096 bytes), o código resetava o cursor para 0 sem preencher o restante do anel com NOPs e regredia o `WPTR` de hardware (`WPTR = 120` contra `RPTR = 8312`). A engine lia lixo residual e a fence ficava presa em `POISON` (`0x43DAFFFE`), travando a animação no quadro 61.
- **Solução implementada em [`driver/wdk/bc250_sdma_present.c`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_sdma_present.c):**
  - Criada a função [`Bc250SdmaPresentAllocateRingSlot`](file:///c:/Users/Daniel/Desktop/NamixWDDM/driver/wdk/bc250_sdma_present.c#L557-L574): sempre que um pacote não cabe no espaço restante, preenche compulsoriamente os dwords restantes com `BC250_SDMA_OP_NOP` (`0x00000000u`).
  - O `CurrentCursor` e o `WPTR` foram transformados em **contadores cumulativos de 32 bits estritamente monotônicos** em todas as 8 rotinas de submissão do SDMA.
- **Resultado Físico no Silício (Corrida das 10:42):**
  - O log do KMD registrou:  
    `[SdmaPresent] Teardown Complete. TripodValidated=1, Submitted=447, Completed=447, Fallbacks=0`
  - **447 BLTS CONSECUTIVOS DE 11 MB COM ZERO TIMEOUTS E ZERO FALLBACKS!**
  - Mais de 13 voltas completas do anel executadas com 100% de sucesso pelo hardware SDMA.

### 2.3. Resultado da Corrida das 10:42 (`docs/evidence/2026-09-12_10-42-25-DESKTOP-HQAG6T4-lab-binding`)
- O monitor DisplayPort exibiu **17 segundos ininterruptos de animação fluida** a ~26.5 FPS:
  ```text
  [SWAPCHAIN] Quadro   61 / 600 | Instant:  25.3 FPS | Media:  25.8 FPS | FrameTime: 35.03 ms
  [SWAPCHAIN] Quadro  121 / 600 | Instant:  26.8 FPS | Media:  26.3 FPS | FrameTime: 36.15 ms
  [SWAPCHAIN] Quadro  181 / 600 | Instant:  25.8 FPS | Media:  26.1 FPS | FrameTime: 36.61 ms
  [SWAPCHAIN] Quadro  241 / 600 | Instant:  26.7 FPS | Media:  26.3 FPS | FrameTime: 36.70 ms
  [SWAPCHAIN] Quadro  301 / 600 | Instant:  27.7 FPS | Media:  26.5 FPS | FrameTime: 36.02 ms
  [SWAPCHAIN] Quadro  361 / 600 | Instant:  26.3 FPS | Media:  26.5 FPS | FrameTime: 33.79 ms
  [SWAPCHAIN] Quadro  421 / 600 | Instant:  26.1 FPS | Media:  26.4 FPS | FrameTime: 33.74 ms
  ```
- No quadro 448, o harness registrou:
  `[RADV_BC250_PRESENT #448] D3DKMTEscape returned NTSTATUS 0xC00002B6 (STATUS_DEVICE_REMOVED)`
- O harness encerrou o processo com segurança (`cleanup vkDeviceWaitIdle -> process_exit`), e o watchdog de proteção restaurou o driver básico (`BasicDisplay`), retornando a tela ao estado preto.

---

## 🔍 3. Por Que a Tela Fica Preta Após a Apresentação?

O usuário observou: *"mostrou apenas 1 vez ai depois tela preta"*. É crucial que a nova IA compreenda a arquitetura de display:

1. **Apresentação Direta via Escape:**  
   O nosso harness (`bc250-radv-present-gate.exe`) renderiza via Vulkan e escreve diretamente no scanout do UEFI GOP em VRAM (`0xF400000000`) através da chamada privada de escape do KMD (`0xBC250001u`). Enquanto o harness está executando e enviando quadros, o monitor DisplayPort exibe a animação perfeita.
2. **DWM Não Funciona Nativo Ainda:**  
   O Windows 10 usa o DWM (Desktop Window Manager) para desenhar a área de trabalho. O DWM exige Direct3D 10/11. A nossa DLL de user-mode (`driver/umd/bc250_d3d_umd.c`) tem `BC250_D3D10_11_NATIVE_READY = 0` (retorna `E_NOTIMPL` no `OpenAdapter10`). Por esse motivo, quando o nosso teste termina ou cai, o DWM não consegue assumir o controle da tela e a área de trabalho fica preta.
3. **Watchdog de Segurança (180 segundos):**  
   O script de laboratório ativa um watchdog que reverte o adaptador para o `BasicDisplay` (driver padrão VGA da Microsoft) caso não haja intervenção, garantindo que o sistema operacional nunca fique permanentemente inacessível.

---

## 🛠️ 4. Ponto Exato de Parada e Diagnóstico para a Próxima IA

> **CORREÇÃO DE ENQUADRAMENTO (12/09, mineração posterior da mesma evidência):**
> o bloco `MEC_PENDING Fence=463 Observed=462` abaixo é a primeira observação
> transitória da submissão, arquivada para diagnóstico; **não é o estado final
> da fila**. Linhas posteriores do mesmo recorte mostram
> `ObservedFence=464`, `RptrReport=RptrMmio=5568`, `Sub=Ret=464` e
> `FenceNow=464`. Portanto, a hipótese “MEC parou na fence 463” está
> **REFUTADA pela própria corrida**. A origem de `STATUS_DEVICE_REMOVED`
> permanece aberta. O Event Log do host registra uma sequência de crashes do
> `dwm.exe` (`e0464645`) durante a janela do ensaio, mas correlação não prova
> causalidade. O runner agora arquiva `windows-events-this-run.json` para que a
> próxima corrida distinga crash storm, TDR e remoção PnP sem reutilizar eventos
> antigos do host.

### O Evento do Quadro 448 (`0xC00002B6: STATUS_DEVICE_REMOVED`)
Na corrida das 10:42, após 17 segundos contínuos (447 quadros), a chamada a `D3DKMTEscape` retornou `0xC00002B6`.

No log do KMD (`docs/evidence/2026-09-12_10-42-25-DESKTOP-HQAG6T4-lab-binding/bc250_display_log-full.txt`), logo antes do StopDevice:
```text
[NODE1_STOP_GCVM_FAULT] Valid=0x7 STATUS=0x00000000 ADDR=0x00000000:00000000
[MEC_PENDING_ARCHIVE] Fence=463 Observed=462 RPTR=5548 Writeback=5544 ValidReads=0x3F Active=00000001 Error=00000000 HqStatus=00008040 WPTR=5556 Vmid=00000900 IbControl=8030000B
[MEC_IB_DESCRIPTOR_ARCHIVE] Raw=C0023F00,00AC4000,00000002,09800030
[SdmaPresent] Teardown Complete. TripodValidated=1, Submitted=447, Completed=447, Fallbacks=0
```

### Análise Física:
1. **O motor SDMA estava 100% saudável**: 447 submissões, 447 conclusões, 0 fallbacks, 0 timeouts.
2. **O snapshot transitório capturou a Fence 463 ainda pendente**:
   - `Fence=463` submetida, `Observed=462`.
   - `WPTR=5556`, `RPTR=5548`. A diferença é de exatamente **8 dwords** (o pacote `RELEASE_MEM` de fence no fim do anel).
   - `HqStatus=00008040` (a fila estava aguardando ou foi desalocada).
3. **Estado da causa do `STATUS_DEVICE_REMOVED`**:
   - **ABERTA.** Não há `QueryEngineStatus`, `ResetFromTimeout` ou
     `RestartFromTimeout` no recorte que sustente atribuição a TDR. A fila MEC
     terminou drenada e sem fault GCVM. A próxima corrida deve correlacionar o
     instante do retorno `0xC00002B6` com os eventos System/Application
     arquivados pelo runner e com o estado final de MEC/SDMA.

---

## 🚀 5. Instruções Imediatas para a Próxima IA

1. **Ambiente de Trabalho e Scripts**:
   - Diretório: `c:\Users\Daniel\Desktop\NamixWDDM`
   - Teste do Marco 3: `tools\EXECUTAR_MARCO3.cmd` (Executa reset do fusível + bind com swapchain).
   - Suíte de Contratos: `cmd /c driver\wdk\build-contract.bat`
   - Auditoria de Segurança: `python tools\audit-bc250mcdm-safety.py` (deve ser sempre 22/22 PASS).
   - Compilação do driver: `powershell -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`
   - Empacotamento: `powershell -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission`

2. **Ações Recomendadas**:
   - **Ação 1**: Repetir os 600 quadros com o novo recorte de eventos. Não reduzir o critério de fechamento: 300 quadros servem somente para bissecção.
   - **Ação 2**: Usar `Sleep(1)` apenas em uma variante de bissecção. Um yield que muda o desfecho não prova estabilidade no ritmo alvo.
   - **Ação 3**: Correlacionar o primeiro `0xC00002B6` com `windows-events-this-run.json`, com o estado final de MEC/SDMA e com os crashes do DWM. Não usar `MEC_PENDING_ARCHIVE` isoladamente como veredito.
   - **Ação 4**: Uma vez fechado o Marco 3 com Exit 0 nos 600 quadros, avançar para o **Marco 4** (backend D3D10/11 real para o DWM).

---
*Este documento é a fonte de verdade para a continuidade imediata dos trabalhos na AMD BC-250.*
