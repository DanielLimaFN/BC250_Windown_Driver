# Corrida 118: triangulo visivel, pixel central preservado e recuperacao R1

> Bind: **25/09/2026 09:06:32**, lab **3.277.1.0**, SYS assinado SHA-256 `5C33FCB9633CD58EAEA61ED7F83ED9BDB640AD1C27D8FBA346512CA6834C70C5`. Evidencia: `docs/evidence/2026-09-25_09-06-32-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: [doc 436](436_DX73_PIXEL_CENTRAL_DA_COPIA_E_CRITERIO_CORRIDA118.md). Daniel confirmou: **triangulo apenas dentro da janela, desktop legivel e sem piscar ate o watchdog agir**.

## 1. Houve avanco?

**Sim, funcional no ensaio curto frente a 117:** o triangulo, ausente na 117, voltou a aparecer na janela sem duplicacao nem piscar; o guardiao recuperou BasicDisplay no mesmo boot. A nova medicao fechou a fronteira fisica da copia: **60/60** registros DX.73 mostraram centro `Src=Dst=FF7F4040`, canto `Src=Dst=FF2673E5` e `Match=5/5`. A DX.73 so acrescentou log dos valores que ja eram lidos; o reaparecimento do triangulo **nao prova** que ela corrigiu a causa intermitente. **Nao houve avanco de velocidade:** harness 0,9 FPS contra 1,1 na 117; DWM primeiros 120 s 2,68/s contra 2,78/s.

## 2. Sucessos e metricas

- Proveniencia `binding-provenance.json`: pacote 3.277.1.0 `DISPLAY_RENDER_LAB`, SYS esperado, RADV `FCAA11FE...`, PDB exato. Log KMD final completo em duas passagens (`complete_at_capture=true`).
- Harness: **60/60 `PRESENT_OK`**, media final **0,9 FPS** (117: 1,1). UMD: 60 `DXGI Present ENTER cb`, 60 `LEAVE cb` com `hr=0`, 60 `[DX71_WDDM_ONLY]`, zero `DIRECT SCANOUT`. Usuario viu triangulo so na janela; o harness ainda nao le de volta os pixels finais.
- KMD: 60/60 `COMPOSITE_CENTER` do harness com `Src=Dst=FF7F4040`, `CornerSrc=CornerDst=FF2673E5`, `Match=5/5`, mesmos enderecos fisicos `SrcMc=0xF405E8C000` e `DstMc=0xF40504C000`. Isso prova o centro e os outros quatro pontos amostrados **ate o destino da copia**; a observacao do Daniel fecha o visual no monitor neste ensaio. Todos os 60 tempos DX.72 `Blt=1` foram `STATUS_SUCCESS` no contexto `0000079C0000079C`, p50/p90 **0,746/13,171 ms** (117: 0,676/3,316 ms). Cinco `BYTE_PROOF PASS` completos e zero FAIL; 583 `COMPOSITE_DATA_PROOF SAMPLED_PASS`, zero FAIL; 523 `REDUNDANT_SKIP`; migalha DX.66 `ESTADO LIMPO`. Sem ResetFromTimeout/RestartFromTimeout/GFXHUB_FAULT no log final.
- DWM PID 1200: 524 inicios de DXGI Present em 171,343 s; **322 nos primeiros 120 s = 2,68/s** (117: 334 = 2,78/s; 116: 288 = 2,40/s). 523 callbacks completos e marcadores DX.71, zero scanout direto. Intervalo entre Presents p50/p90 **174/543 ms**. Tres selftests RADV 5/5.
- Harness `pfnPresentCb` p50/p90 **443/465 ms** (117: 444/465). Inicio de `DXGI Present` ate ponte WDDM p50 **107 ms**; saida do callback ate inicio do proximo Present p50 **224 ms**. Sao etapas distintas por quadro; nao somar medianas como se compusessem um quadro tipico. Os 220 `FlushCommands` registrados tiveram `SubmitUs` p50 **64,3 ms**; 60 lotes tinham draw e 160 tinham `DrawsNoLote=0`, o que **nao prova** ausencia de clears/copies ou que possam ser eliminados. Esse custo e a espera do callback sao alvos de investigacao, sem atribuicao causal conclusiva.
- Guardiao: `ROLLBACK_STARTED` as 09:09:34; final `RECOVERY_OK_ONLINE` as 09:09:43, `boot_ticks=armed_boot_ticks=639259346665000000`; cinco amostras BasicDisplay Codigo 0 entre 09:09:37 e 09:09:41. O `BLOCKED` intermediario no journal nao foi resultado terminal. `C:\Windows\MEMORY.DMP` continua antigo (24/09 13:56:30).

## 3. Falhas e limites

- Nao houve falha visual, hang, TDR, BugCheck ou device-lost **na janela de 180 s observada**. Isso nao encerra travamentos intermitentes anteriores em 2–8 min nem prova 600 s.
- A taxa segue muito baixa: DWM **2,68 Presents/s** nos primeiros 120 s, harness **0,9 FPS**, frente a modo GOP de ~56,13 Hz. O callback do harness ainda leva ~443 ms apesar de `DxgkDdiPresent` ter mediana abaixo de 1 ms; o tempo fora dessa DDI nao foi localizado. O log de 60 pixels DX.73 acrescentou I/O, mas a diferenca de FPS entre 117/118 nao identifica causalidade.
- A 117 teve triangulo ausente e a 118 presente com UMD/RADV identicos e mudanca KMD somente de telemetria. O defeito visual e' **intermitente** no escopo observado; nao afirmar correcao definitiva.

## 4. Veredito

**A — sucesso do criterio 436 na janela de 180 s:** 60/60 quadros aceitos, 60/60 copias com centro/canto esperados e 5/5 amostras iguais, triangulo visivel apenas na janela, desktop normal e R1 no mesmo boot. **Nao** fecha estabilidade de 600 s, fluidez, 60 FPS ou jogos.

## 5. Recuperacao e proximo passo

BasicDisplay esta ativo e OK/Codigo 0. `CLEAN_BOOT_REQUIRED` foi atualizado ao boot da 118; boot frio com corte de energia antes de qualquer novo bind. A prova do pixel desloca a proxima pergunta de velocidade: medir por que `pfnPresentCb` consome ~443 ms enquanto a DDI Present consome ~0,746 ms, e distinguir os lotes `DrawsNoLote=0` que carregam trabalho real daqueles realmente vazios antes de eliminar flush. Manter um ensaio de **600 s** para estabilidade depois de preparar a coleta apropriada; a corrida curta nao basta.

Provas SHA-256: KMD `display-log-this-bind-FINAL.txt` (24.554.878 bytes) `D6AD824E671EBF25936DBD2A8B783BD6FF5AC02C72D26C9B03826779C66F7005`; DWM `bc250_umd_log_1200-FINAL.txt` `FE8B488BE9773B094CB89BF690002AA70F9CEE3DD360A62822063D6A16CEC8A6`; harness `bc250_umd_log_8012-FINAL.txt` `D955DA4C4C598175F0304B0F2B9AF992E0784A339DCE713B72CA4270599089C6`; guardiao `safe-bind-final.json`.
