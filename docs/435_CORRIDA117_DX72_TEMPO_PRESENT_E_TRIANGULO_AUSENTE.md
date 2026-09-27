# Corrida 117: DX.72 localiza o custo fora de Present, mas triangulo nao apareceu

> Bind: **25/09/2026 08:38:51**, lab **3.276.1.0**, SYS assinado SHA-256 `CC55CA619D0E658A62CF893D1205F82EA9ED80723CEC8ACA027C7C3B61ED19EA`. Evidencia: `docs/evidence/2026-09-25_08-38-51-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: [doc 434](434_DX72_TEMPO_INTERNO_PRESENT_E_CRITERIO_CORRIDA117.md). Daniel informou: **desktop normal e watchdog agiu, mas o triangulo nao apareceu**. A pergunta sobre o que foi visto na janela permanece registrada separadamente; nao inventar detalhe visual.

## 1. Houve avanco?

**Sim, diagnostico de velocidade; nao, funcional.** A 116 ja recuperara R1 com visual correto e 2,40 Presents/s DWM nos primeiros 120 s. A 117 mediu 60 Blt do harness no KMD, todos `STATUS_SUCCESS`, e separou a DDI de `pfnPresentCb`: a mediana dentro de `DxgkDdiPresent` foi **0,676 ms**, enquanto a ida e volta do callback na UMD foi **444 ms**. O maior custo medido do callback esta fora da DDI cronometrada, sem localizacao exata na pilha Windows/driver. O triangulo ausente e' regressao visual observada frente a 116; uma unica corrida nao prova que a DX.72 a causou.

## 2. Sucessos e metricas

- Proveniencia `binding-provenance.json`: pacote 3.276.1.0 `DISPLAY_RENDER_LAB`, SYS esperado, RADV `FCAA11FE...`, PDB exato. Log KMD final arquivado em duas passagens, `complete_at_capture=true`.
- Harness: **60/60 `PRESENT_OK`**, 1,1 FPS medio final (116: 1,2). UMD: 60 `DXGI Present ENTER cb`, 60 `LEAVE cb` com `hr=0`, 60 `[DX71_WDDM_ONLY]`, zero `DIRECT SCANOUT`. Duracao do callback p50/p90 **444/465 ms** (116: 433/466 ms). Ponte `BridgeLinearToWddm` reportou sucesso 60 vezes. O harness nao le pixels finais de volta.
- KMD: **60/60** linhas `[DX72_PRESENT_KMD_TEMPO]` `Blt=1`, todas no contexto `0000097C0000097C`, status `0x00000000`, ordinais `n=11..186`; p50/p90 **0,676/3,316 ms**, maximo 50,976 ms. A instrumentacao cobre a DDI valida ate antes da nova linha de log, nao o trabalho posterior do scheduler nem todo o retorno do callback. Os 548 Flip DWM somados aos 60 Blt explicam as 608 entradas de Present; 29 Flip tiveram amostra de tempo KMD.
- DWM: 549 inicios de DXGI Present em 167,816 s; **334 nos primeiros 120 s = 2,78/s** (116: 288 = 2,40/s). Intervalo entre Presents p50/p90 **198/519 ms** (116: 293/610 ms); callback DWM p50/p90 **2/15 ms**. A taxa variou com binarios UMD/RADV iguais; nao atribuir o ganho ao contador DX.72 como otimizacao.
- KMD registrou **618 `COMPOSITE_DATA_PROOF SAMPLED_PASS`**, dos quais 60 copias 1280x720 do harness e 558 2560x1080; zero FAIL. O teste existente confere cinco pontos, inclusive o centro, entre origem e destino da copia, mas seu log guarda apenas a contagem de acertos, **nao os valores dos pixels**. Quatro `BYTE_PROOF PASS` em frames completos, zero FAIL; 558 `REDUNDANT_SKIP`; migalha DX.66 `ESTADO LIMPO`. Tres selftests RADV 5/5.
- Recuperacao: watchdog `ROLLBACK_STARTED` as 08:41:53 e final `RECOVERY_OK_ONLINE` as 08:42:02, mesmo boot (`boot_ticks=armed_boot_ticks=639259327445000000`), BasicDisplay Codigo 0 em cinco amostras 08:41:56–08:42:00. O `BLOCKED` no journal foi intermediario, nao o resultado final. `C:\Windows\MEMORY.DMP` ainda e' antigo, 24/09 13:56:30.

## 3. Falhas e limites

- **Triangulo nao apareceu**, conforme observacao direta do Daniel. Portanto a condicao visual do desfecho A **falhou**, apesar dos 60 `PRESENT_OK`. Nao houve leitura de pixel na janela nem no scanout final para fechar o caminho. A UMD mostrou `Center(640,360)=0xFF7F4040` no alvo Vulkan, igual ao valor da 116, e o bridge reportou sucesso; isso prova apenas o alvo antes da cadeia de apresentacao. Falta valor de centro na origem/destino do `COMPOSITE_BLT` e no resultado DWM para localizar a perda.
- O `CenterPixel` historico de `[Present #120]` usa **pitch do modo display 2560x1080** ao amostrar uma origem Blt 1280x720; por isso `0xFF00A0F4` nessa linha **nao** e' leitura valida do centro do harness. Na 116, em que o triangulo foi visto, o mesmo probe deu `0xFF2673E5`. Nao usa-lo como prova de perda do pixel.
- Nao houve hang, TDR, BugCheck, device-lost ou falha de rollback observados na janela; 180 s nao encerram o travamento intermitente de 3–8 min. DWM a 2,78/s e harness a 1,1 FPS ainda estao muito longe da meta de fluidez. A diferenca entre 444 ms UMD e 0,676 ms KMD **nao identifica** se a espera ocorreu em VidMm, scheduler, sincronizacao ou outro componente.

## 4. Veredito

**E — falha visual**, pelo triangulo ausente frente ao criterio 434. A medicao de tempo e' aproveitavel como diagnostico, mas **A — diagnostico valido com imagem normal — nao fecha**. Nao declarar sucesso de composicao, 60 FPS, 600 s ou jogos.

## 5. Recuperacao e proximo passo

BasicDisplay esta ativo e OK/Codigo 0. `CLEAN_BOOT_REQUIRED` atualizado para o boot da 117; boot frio com corte de energia antes de qualquer outro bind. Proximo trabalho offline: registrar **valores** do pixel central antes/depois da copia `COMPOSITE_BLT` 1280x720, usando os pitches corretos que ja estao no escape, para distinguir perda na ponte/copia de perda na composicao DWM. Manter a observacao visual como gate. Separadamente, a latencia de ~444 ms fora de `DxgkDdiPresent` precisa de rastro do caminho `pfnPresentCb`/VidMm; reduzir codigo dentro dessa DDI nao tem sustentacao como solucao do 1 FPS.

Provas SHA-256: `display-log-this-bind-FINAL.txt` (27.903.669 bytes) `F1AEF246E6B35F7FB4494A7948C606A4CFF045D95F90841AB8F1566BDC85EA05`; UMD DWM `bc250_umd_log_1224-FINAL.txt` `8D2FDB48B8EFA6BC1C325897896F11B018BCB3B24A6B4AB438F914BECE92A707`; UMD harness `bc250_umd_log_11508-FINAL.txt` `6F9E619E97744B04E3D7657E30ADE8FBC06D64C42F75D05AA83CB5556B493AC9`; resultado do guardiao em `safe-bind-final.json`.
