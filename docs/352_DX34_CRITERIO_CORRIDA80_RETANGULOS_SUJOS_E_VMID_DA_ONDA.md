# DX.34 — marcador de boot frio no watchdog, retângulos sujos no Present, VMID da onda presa. Critério da Corrida 80

> **23/09/2026. OFFLINE; zero bind.** Parte do resultado da Corrida 79 ([docs/351](351_CORRIDA79_DX33_RESULTADO.md)).
> Critério escrito **antes** do ensaio (Leis 9 e 10). **NÃO VALIDADO EM HARDWARE.**

## 1. O que motiva (P0, Corrida 79)

- **Bind no mesmo boot do rollback anterior:** o watchdog não gravava `C:\BC250_REQUIRE_CLEAN_BOOT.json` em
  `RECOVERY_OK_ONLINE`, e o binder aceitou o reboot quente do próprio watchdog como "boot posterior".
- **Present apagando o scanout:** `Present #8` com `SubRects=1` do tamanho de um cursor e a superfície 0/256. O KMD
  copiava os 11.059.200 bytes, e o scanout ia a 0/256. Resultado visível: tela preta com o cursor sozinho.
- **VMID por MMIO não serve:** `CP_ME0_PIPE0_VMID` e `CP_RB_VMID` leram 0 (VMID do anel) nos 3 TDRs, e
  `COMPUTE_VMID` leu lixo.

## 2. O que mudou

| # | Mudança | Tipo |
|---|---|---|
| H1 | O watchdog grava o marcador de boot frio com os ticks do **boot corrente**, no início do rollback e de novo em `RECOVERY_OK_ONLINE`. Formato igual ao de `rollback-bc250mcdm-lab.ps1`. | Host (correção de defeito) |
| V1 | O Present Blt de tela cheia honra `pDstSubRects`: com 1–16 retângulos válidos e `SrcRect == DstRect ==` tela cheia, copia **só os retângulos** por `COPY_LINEAR_SUB_WINDOW`, com origem no mesmo (x,y). Fora disso, tela cheia como antes, com `DIRTY_RECTS_RECUSADOS`. Prova amostral por retângulo (centro e 4 cantos, origem × scanout) em `DIRTY_RECTS ... DIRTY_RECT_PROOF Match=`. A prova integral de 11 MB fica **desligada** nesse caminho, porque reprovaria um present correto. | **Comportamental** |
| V2 | A sonda de ondas lê `SQ_WAVE_HW_ID2` (ix 0x0118; `VM_ID` em 27:24; `gc_10_1_0_offset.h:11201`, `sh_mask.h:42910`) **logo depois do STATUS**, e o TDR imprime `[TDR_GFX_WAVE_VMID] WaveVmid=`. O pacote **arma** `GfxWaveProbe=1` (`-ArmGfxWaveProbe -IAcceptSelectorWritesDuringTdr`). | **Comportamental no TDR** (a sonda escreve `GRBM_GFX_INDEX`/`SQ_IND_INDEX` depois do hang) |

**Três mudanças, duas comportamentais, por decisão do Daniel.** Separação:
- V1 só age no `DxgkDdiPresent` com retângulos;
- V2 só age depois de detectado um TDR com ondas em voo;
- H1 não toca o driver.

**Por que V2 e não o "marcador escrito pela GPU no IB" que eu propus.**
- A leitura dos registradores mostrou que nenhum registrador do CP expõe o VMID das ondas: os dois lidos na 79 mostram
  o do anel.
- Um `COPY_DATA` no IB só gravaria em memória um desses mesmos registradores.
- O VMID com que o SQ lançou a onda está em `SQ_WAVE_HW_ID2.VM_ID` da própria onda. A única via é a sonda indireta, que
  já existe, já rodou no silício (19/09: 1 onda com STATUS/PC reais; 20/09: `selector_stuck`) e só precisava ler mais
  um índice.

**Riscos declarados:**
- **V2:** nas duas corridas armadas anteriores o readback de `GRBM_GFX_INDEX` voltou 0 (`selector_stuck`). O mesmo 0
  aparece sem a sonda (Corrida 78: "divergência não é prova de falha"), e o quiesce repõe broadcast de novo antes do
  soft-reset. BugCheck e reboot são desfechos possíveis e fazem parte do resultado.
- **V1:** se a superfície de origem não tiver o mesmo pitch da tela, o retângulo sai deslocado. Nesse caso o critério
  manda reverter.
- **V1:** cada retângulo é uma submissão SDMA síncrona, com no máximo 16 por Present.

**Verificação offline:**
- `build-contract` EXIT=0, com `gfx_ring_contract_test` estendido: `ValidMask 0x7F`, `VM_ID=4` decodificado, e
  prioridade do HW_ID2 com orçamento de 2 leituras (`ValidMask 0x41`);
- mutação de teste (tirar o bit do HW_ID2) derruba a suíte;
- `test-bc250-safebind-offline` PASS;
- auditorias de segurança, matriz de DDI, callbacks no-op e RADV: PASS;
- ferramentas de mutação DX.31 e DX.33 continuam passando;
- portão novo `audit_dx34_marker_dirty_rects_and_wave_vmid`: **11 de 11 mutações reprovam**.

**Pacote lab 3.241.1.0**, assinado, `Lab package audit: PASS`, manifesto com `GfxWaveProbe=1`, NÃO INSTALADO:

| Artefato | SHA-256 |
|---|---|
| SYS assinado | `0D681AC88C0993472E6E5C4F996AEDF433D388AFFC8E0A3013BF1641B68B03A7` (fonte `2B7CC74A...`) |
| UMD | `239B046C...` (inalterada) |
| INF / CAT | `4AA05814...` / `D438E012...` |
| ICD | `5988B38E...` (inalterado) |

As strings `TDR_GFX_WAVE_VMID`, `DIRTY_RECT_PROOF`, `DIRTY_RECTS_RECUSADOS` e `MODECHANGE_BLANK_SKIP` foram
conferidas no SYS empacotado.

## 3. Critério da Corrida 80

### 3.1 Sinais
- **BOOT-FRIO:** o `boot_ticks` do watchdog da 80 é diferente do da 79 (`639257589905000000`), e a pasta não tem
  "rollback já foi tentado neste boot".
- **RECT-ATIVO:** linhas `DIRTY_RECTS n=... SDMA=YES` com `DIRTY_RECT_PROOF Match=k/k`.
- **SCANOUT-VIVO:** depois de um Present por retângulos, a amostra seguinte do scanout **não** cai a 0/256 sem uma cópia
  de origem zerada em tela cheia que explique a queda.
- **WAVE-VMID:** linhas `[TDR_GFX_WAVE_VMID] WaveVmid=` sem `INDETERMINADO:`, comparadas com os `CARIMBO -0`/`-1` do
  mesmo TDR.
- **Controles:**
  - autoteste 5/5 nas três variantes, com NÃO MEDIDO por device perdido não contando como FAIL;
  - zero BugCheck;
  - zero `PREAMBLE_CNTL`;
  - `Eng=0`;
  - `MODECHANGE_BLANK_SKIP` presente;
  - watchdog chega a um estado final: `RECOVERY_OK_ONLINE` ou reboot com marcador de ticks corrente.
- **Visual (Daniel):** o que aparece, se fica, e se o cursor se move.

### 3.2 Desfechos do display (V1)

**A — A imagem fica**
- Assinatura: RECT-ATIVO e SCANOUT-VIVO, e o Daniel vê imagem persistente (desktop, janela do harness ou restos do
  quadro anterior) com o cursor se atualizando por cima.
- Implicação: o apagador era a cópia de tela cheia. Próximo: TDRs.

**B — Retângulos executam, a tela continua preta**
- Assinatura: RECT-ATIVO e scanout 0/256 **antes** dos Presents por retângulo. Não havia quadro a preservar: a
  superfície do DWM está vazia.
- Implicação: o problema é o conteúdo do DWM, e não a cópia. Ligar à classe de TDR, que derruba o DWM.

**C — Retângulos recusados**
- Assinatura: só `DIRTY_RECTS_RECUSADOS`, com `SrcRect`/`DstRect` diferente da tela ou mais de 16 retângulos.
- Implicação: V1 não foi exercitada; descrever a forma real das listas.

**D — V1 prejudica**
- Assinatura: `DIRTY_RECT_PROOF` com `Match < Probes` repetido, **ou** o Daniel vê pedaços deslocados ou borrados.
- Implicação: reverter V1.

### 3.3 Desfechos dos TDRs (V2, medição)

**T0 — Sem TDR.** Descrever.

**T1 — A onda roda com o VMID anterior**
- Assinatura: `WaveVmid` igual ao VMID do `CARIMBO -1` e diferente do `-0`.
- Implicação: o SQ lança o dispatch sob o VMID antigo. O estado de VMID do pipe não acompanha o `INDIRECT_BUFFER`.
  Próximo: sequência de troca do upstream (`CONTEXT_CONTROL`/`PFP_SYNC_ME` e o que a precede).

**T2 — A onda roda com o VMID certo**
- Assinatura: `WaveVmid` igual ao VMID do `CARIMBO -0`.
- Implicação: não é VMID errado. Próximo: o próprio shader de cópia e a visibilidade (`ACQUIRE_MEM`), com o PC e a
  instrução que a mesma sonda imprime.

**T3 — Sonda sem resposta**
- Assinatura: `selector_stuck` sem onda válida, `WaveVmid` `INDETERMINADO:`, ou nenhum `[TDR_GFX_WAVE_VMID]`.
- Implicação: a sonda indireta não responde neste estado. Parar a linha de medição do VMID e voltar à leitura upstream.

**T4 — VMID fora dos dois.** Descrever. Não forçar para T1 ou T2.

### 3.4 Transversais

**F — Regressão de controle**
- Assinatura: autoteste FAIL (não NÃO MEDIDO), BugCheck, `PREAMBLE_CNTL`, perda de `Eng=0`.
- Implicação: atribuir antes de ler o resto. Se houver BugCheck, analisar primeiro se ele veio da sonda (V2).

**H — Host**
- Assinatura: o bind da 80 sai no mesmo boot de um rollback, ou o watchdog termina sem marcador.
- Implicação: H1 falhou; corrigir antes de outra corrida.

**G — Fora do mapa:** descrever antes de julgar.

Desfecho parcial não é desfecho (Lei 10.3). Display (A–D) e TDR (T0–T4) são julgados em separado.

### 3.5 Execução
- `C:\BC250_REQUIRE_CLEAN_BOOT.json` **está armado com o boot atual** (08:16:30). O binder recusa até o próximo boot.
- Fazer **desligamento completo**: `shutdown /s /t 0`, esperar apagar e ligar pelo botão. "Reiniciar" não serve.
  **Limite:** o guarda só distingue boot novo, não frio de quente; a disciplina de desligar continua sendo do operador.
- Conferir preflight PASS e rodar `tools\EXECUTAR_D3D11_NATIVO.cmd`, elevado, **sem `-Force`**.
- Anotar o que aparece, se fica, e se o cursor se move.
- Teto: uma corrida.
