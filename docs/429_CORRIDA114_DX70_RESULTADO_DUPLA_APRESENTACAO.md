# Corrida 114 (DX.70): log menor, duplicacao visual por scanout direto

> Bind: 24/09/2026 23:15:09, pacote lab 3.274.1.0, SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`. Evidencia: `docs/evidence/2026-09-24_23-15-09-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: doc 428, janela de 180 s.

## Avanco, sucessos e falhas

**Avanco funcional parcial frente a 113, mas falha no criterio visual da 114.** O DWM iniciou 372 Presents nos primeiros 120 s = **3,09/s** (113: 313 = 2,60/s, +19%); ficou abaixo dos **3,25/s** exigidos para A. No periodo observado, 593 Presents em 161,19 s = 3,67/s (113: 593 em 171,16 s = 3,46/s). Harness D3D11: 60/60 `PRESENT_OK`, **1,3 FPS** medio ao final (113: 1,0), mas sem readback de pixels. Tres selftests RADV reportaram 5/5. `[DX70_SAMPLE]` apareceu 811 vezes no DWM. Log DWM final: **10.871.272 bytes**, contra 18.190.554 na 113 (40,2% menor; duracoes/cargas diferem).

**Falha visual observada por Daniel:** o quadrado azul e triangulo permanecem dentro da janela; uma segunda imagem grande aparece sobre o desktop e pisca. A [foto arquivada](evidence/2026-09-24_23-15-09-DESKTOP-HQAG6T4-lab-binding/observacao-visual-janela-e-scanout.jfif) mostra as duas imagens simultaneas. O harness desenha a mesma cor azul e o mesmo triangulo a cada quadro, sem alternancia intencional. Nao chamar essa duplicacao de comportamento normal ou de sucesso visual. Nao houve relato de outros artefatos apos o encerramento do harness.

Os logs mostram **593 chamadas `radv_bc250_present_scanout (DIRECT SCANOUT)` do DWM e 60 do harness**, alem de 593 e 60 chamadas de `pfnPresentCb`, respectivamente. O codigo da UMD copia a imagem para a alocacao WDDM e, antes do callback DXGI, tambem escreve o scanout fisico diretamente. Esta segunda escrita contorna a composicao da janela e e' uma **explicacao forte, ainda nao validada por ensaio de correcao**, para a imagem grande deslocada e o piscar. No KMD houve 2 `BYTE_PROOF PASS`, 0 `BYTE_PROOF FAIL`, 3 `MODECHANGE_BLANK_SKIP` e 647 `REDUNDANT_SKIP`; o sucesso de copia de bytes em alguns quadros nao prova posicionamento ou estabilidade da janela. Nao atribuir causa a GPUVA, DCN ou triangulo desenhado sem evidencia adicional.

Os intervalos do DWM tiveram p50 **183 ms**, p90 **528 ms**, p99 **840 ms** (113: 174/469/1729 ms). Tempo interno de Present p50/p90 **40/98 ms** (113: 41/120); ponte Vulkan->WDDM **26/82 ms** (113: 27/103). Houve 2.390 draws e 5.079 flushes do DWM. Os resultados mistos e a variacao de carga impedem atribuir o ganho de taxa exclusivamente a DX.70. Nem 3,09/s nem 3,67/s se aproximam dos 56,13 Hz do GOP.

Nao houve `BYTE_PROOF FAIL`, ResetFromTimeout, RestartFromTimeout, GFXHUB_FAULT ou BugCheck nos registros consultados; isso se limita aos ~180 s do teste. A classe de travamento em 3–8 minutos continua **NAO VALIDADA** por esta janela curta.

## Veredito frente ao criterio previo

**E: falha visual.** A tela nao cumpriu a condicao "sem artefatos"; a taxa dos primeiros 120 s tambem ficou abaixo de A. Os controles de amostragem e a recuperacao R1 passaram, mas a linha B nao prevalece sobre E quando ha falha visual observada. Os 60 `PRESENT_OK` sao aceite de API, nao prova de composicao correta.

## Recuperacao, prova e proximo passo

O guardiao registrou `RECOVERY_OK_ONLINE` as 23:18:20 e cinco amostras BasicDisplay Codigo 0 entre 23:18:14 e 23:18:18, todas no mesmo boot (`boot_ticks=armed_boot_ticks=639258991555000000`). BasicDisplay e' recuperacao, nao sucesso do nosso DWM. `CLEAN_BOOT_REQUIRED` permanece ativo; boot frio com corte de energia antes de novo bind.

DX.71 deve deixar o callback WDDM como unico proprietario da apresentacao de DXGI, mantendo a ponte Vulkan->alocacao WDDM e recusando quando a ponte falhar. Validar em hardware: triangulo **somente dentro da janela**, sem copia grande ou piscar; desktop legivel; 60 Presents; `BYTE_PROOF` de quadros com conteudo; rollback R1. Medir taxa DWM contra 113/114, pois a rota KMD pode copiar mais quadros e alterar o custo. Nao declarar o travamento longo resolvido.

SHA-256 dos arquivos finais: KMD `B0931AFD237BCDDF08E50224012432FC9569836BD600E1FC91E7780B5E32B5D7`; DWM `7BD61E26A1012B46CAE142CBA83E8F651CFB1DC7001D53ACCCCCE186028BF703`; harness UMD `B9D2FFDF9ACBA985B3BE1B9448433928519D60BB5E91C4F28D5ECFA6111980B8`; foto arquivada (174.156 bytes) `1005E6797C679BB988D5331234A9CC2363571E055BE5D3E56545A49490C80FF4`.
