# Corrida 116: DX.71 visual estavel por 180 s, R1, taxa DWM abaixo do piso

> Bind: 25/09/2026 08:14:45, pacote lab 3.275.1.0, SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`. Evidencia: `docs/evidence/2026-09-25_08-14-45-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: doc 432, mesmo binario da 115. Daniel confirmou: "Sim, imagem normal ate o rollback" (triangulo so dentro da janela, sem duplicacao ou piscar).

## 1. Houve avanco?

**Sim, funcional no escopo curto frente a 115:** a 115 travou em ~2 min 19 s e o guardiao so recuperou BasicDisplay no boot seguinte; a 116 completou a janela de 180 s sem travamento relatado e recuperou BasicDisplay/0 no **mesmo boot**. A composicao visual correta da 115 repetiu-se e agora foi observada ate o rollback. **Nao houve avanco de velocidade:** DWM nos primeiros 120 s caiu de 3,18/s na 115 para **2,40/s** na 116. Uma corrida curta que passa nao encerra o travamento intermitente nem a meta de 600 s.

## 2. Sucessos e metricas

- Proveniencia completa: `binding-provenance.json` registra pacote lab 3.275.1.0, `DISPLAY_RENDER_LAB`, RADV `FCAA11FE...`, SYS assinado esperado. Preflight de dump passou; nenhum dump manual foi acionado porque nao houve congelamento. `C:\Windows\MEMORY.DMP` continua antigo, de 24/09 13:56:30.
- Harness D3D11: **60/60 `PRESENT_OK`**, media final **1,2 FPS** (115: 1,1; 114: 1,3); sem leitura de pixel de volta. Tres selftests RADV reportaram 5/5.
- DWM: **421 inicios de Present** entre 08:14:56.579 e 08:17:37.308 (160,73 s = **2,62/s** na janela observada). Primeiros 120 s: **288 = 2,40/s**; referencias 115: 382 = 3,18/s, 114: 372 = 3,09/s, 113: 313 = 2,60/s. DWM registrou 421 `[DX71_WDDM_ONLY]` e zero `DIRECT SCANOUT`; harness 60 e zero, respectivamente.
- Intervalo entre Presents do DWM: p50 **293 ms**, p90 **610 ms** (115: 172/589 ms; 114: 183/543 ms, recontados pelo mesmo script). Dentro da funcao DXGI Present: p50 **43 ms**, p90 **129 ms** (115: 44/106; 114: 44/105). Ponte Vulkan→WDDM: p50 **25 ms**, p90 **106 ms** (115: 27/89; 114: 26/82). A queda de taxa acompanha pausas **entre** Presents mais longas, nao um aumento comparavel da mediana da ponte ou da funcao Present. A causa dessas pausas nao foi isolada.
- Workload fixo do harness (60 quadros): a chamada UMD a `pfnPresentCb` levou p50/p90 **433/466 ms** na 116; 115: 445/469; 114: 444/450; 113: 431/448. Este custo de ~0,43 s persiste com as otimizacoes de log e com a remocao do scanout direto. E' tempo de ida e volta do callback, **nao** tempo comprovado dentro de `DxgkDdiPresent`; sem marcacao interna do KMD nao da para separar execucao do driver, scheduler e espera do dxgkrnl. A ponte do harness foi p50 17 ms na 116. Esta e' a proxima medicao de velocidade com carga repetivel.
- KMD: 2 `BYTE_PROOF PASS` de quadros com conteudo, zero `BYTE_PROOF FAIL`, 455 `REDUNDANT_SKIP`; `[DX66_MIGALHA] ESTADO LIMPO` no StopDevice. Sem `ResetFromTimeout`, `RestartFromTimeout` ou `GFXHUB_FAULT` no log final.
- Guardiao: `ROLLBACK_STARTED` as 08:17:47 e `RECOVERY_OK_ONLINE` as 08:18:02. `boot_ticks=armed_boot_ticks=639259315905000000`; cinco amostras BasicDisplay Codigo 0 entre 08:17:56 e 08:18:00. O journal contem um estado intermediario `BLOCKED` durante a mesma tentativa, seguido do `RECOVERY_OK_ONLINE` final; nao interpretar esse estado intermediario como resultado terminal.

## 3. Falhas e limites

- O piso **2,60 Presents/s nos primeiros 120 s** do criterio A da 116 nao foi atingido. A meta de fluidez esta muito distante: 2,40/s vs 56,13 Hz do modo GOP. A variacao entre 115 e 116 com binario identico impede atribuir a queda a uma mudanca de codigo.
- Nenhum travamento, TDR, BugCheck, device-lost ou defeito visual foi observado **nesta janela medida**. O travamento da 115 permanece aberto; 180 s nao provam estabilidade de 600 s. Nao houve dump novo nem prova de que o atalho de crash por teclado funciona sob hang, pois ele nao foi necessario.
- O criterio previo 432 definiu A para sucesso curto com taxa >=2,60/s, B para travamento com dump e E para falhas. Nao previu o caso "janela completa e visual correto, mas taxa abaixo do piso". Nao encaixar artificialmente este resultado em A/B/E nem reescrever o criterio apos a medicao.

## 4. Veredito

**Parcial:** sucesso de composicao visual e recuperacao R1 na janela de 180 s, **falha da meta de taxa** previamente fixada; logo **A nao fecha**. B e E nao descrevem o desfecho observado. Para o produto, estabilidade longa, 60 FPS, jogos e recuperacao durante hang seguem **NAO VALIDADOS EM HARDWARE**.

## 5. Recuperacao e proximo passo

Atualmente BasicDisplay esta em OK/Codigo 0. `CLEAN_BOOT_REQUIRED` foi gravado no boot da 116; boot frio com corte de energia antes de qualquer novo bind. Nao ha justificativa para repetir 180 s com o mesmo pacote apenas para melhorar uma media. Prioridade offline: medir o tempo **dentro** do KMD `DxgkDdiPresent` com amostragem leve e compara-lo com os ~433 ms de ida e volta do callback no harness. Se o KMD for curto, investigar scheduler/composicao do Windows; se for longo, instrumentar a fase de copia/SDMA especifica. Preservar a captura de dump manual para eventual novo hang. Depois, ensaio de 600 s com visual e recuperacao; nao tratar o resultado curto como estabilidade definitiva.

Provas finais: `display-log-this-bind-FINAL.txt` 23.220.496 bytes, SHA-256 `1982EFBA621B97A1A9C738C622AB81FEF5A4B41A9DC09FD8E62CB84FD665E251`; DWM `bc250_umd_log_1208-FINAL.txt` 9.228.127 bytes, SHA-256 `8F98227DF81CB8D7A1B4530B36E4E2DB23BE16143548635A40B67CA07101F96B`; harness `bc250_umd_log_11840-FINAL.txt` 408.692 bytes, SHA-256 `0EB0126A13F21AB0D1EDDE9F022B853903071D5FF264313CD7D033B3B3CFDF37`; guardiao em `safe-bind-final.json`.
