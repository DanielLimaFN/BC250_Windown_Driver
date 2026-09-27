# Corrida 113 (DX.69): ganho de DWM, desktop legivel e rollback no mesmo boot

> **Bind:** 24/09/2026 22:57:39, pacote lab 3.273.1.0, SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`. **Evidencia:** `docs/evidence/2026-09-24_22-57-39-DESKTOP-HQAG6T4-lab-binding/`. **Criterio previo:** doc 426, janela 180 s. Daniel confirmou: "Sim, ficou legível e sem artefatos".

## 1. Houve avanco?

**Sim, funcional no escopo curto e diagnostico de velocidade.** A Corrida 112 travou antes de 600 s; a 113 completou a janela de 180 s sem travamento, com desktop legivel e rollback do guardiao **no mesmo boot**. O DWM iniciou 313 Presents nos primeiros 120 s desde seu primeiro `Present`: **2,60/s**, acima dos 2,5/s definidos antes e dos 1,35/s da 112 (111: 2,13/s) na mesma janela. Isto e' ganho observado sob a DX.69, **nao** prova que a reducao do log MSAA e' a causa exclusiva; as corridas variam em carga.

## 2. Sucessos e metricas

- **P0:** pacote/manifesto confere 3.273.1.0, KMD `A4809EEA…`, perfil `DISPLAY_RENDER_LAB`, GfxWaveProbe=1, RADV fixado. Segmento DX.67 `[0xA8C000,0xFA00000)` sem reserva interna; migalhas DX.68 armadas em `0xFA00000`, 96 slots. No StopDevice, estado `LIMPO`, ultima seq. 2.737.397.
- **P0:** DWM 593 inicios de `Present` em 171,16 s = **3,46/s** medio; intervalo p50 **174 ms**, p90 **469 ms**, p99 1.729 ms. Na 112: 734 em 412,95 s = 1,78/s, p50 185 ms, p90 1.369 ms. A cauda longa melhorou mais que a mediana. No inicio comparavel de 120 s: 113 **2,60/s**, 112 **1,35/s**, 111 **2,13/s**.
- **P0:** tempo dentro de `Present` p50 **41 ms**, p90 120 ms; ponte Vulkan→WDDM p50 **27 ms**, p90 103 ms. Ambos foram *mais lentos por chamada* que na 112 (30/105 e 15/78 ms), apesar de mais quadros totais. Logo o ganho nao vem de uma copia final mais rapida; ha menos pausas entre Presents. `SubmitUs` p50 1,56 ms e `EsperaUs` p50 0,68 ms (112: 1,87/0,78 ms).
- **P0:** amostragem DX.69 operou: explorer.exe PID 11440 registrou 37 linhas MSAA ate ordinal 86.016; PID 7744 registrou 67 ate ordinal 208.896. O DWM registrou somente os 16 ordinais iniciais. Isto evita centenas de milhares de `WriteFile` write-through; o contrato de retorno da query permaneceu igual em codigo. Os dois logs Explorer somaram 5,46 MB no ensaio, contra 66,61 MB da 112 em 342 s de processo (duracoes diferentes).
- **P0:** harness D3D11 60/60 Presents aceitos, zero falhos, **1,0 FPS medio final** — sem ganho visivel nesse processo. Tres selftests RADV reportaram 5/5. O relato visual do Daniel fecha desktop legivel e sem artefatos nesta janela; nao e' prova de pixels byte a byte nem de jogo.
- **P0:** guardiao iniciou rollback as 23:00:41, e `RECOVERY_OK_ONLINE` as 23:00:49. `armed_boot_ticks` e `boot_ticks` sao ambos **639258981135000000**; cinco amostras BasicDisplay Codigo 0 entre 23:00:45 e 23:00:49. Houve recuperacao R1 no mesmo boot.

## 3. Falhas e limites

- O desktop continua longe do modo GOP de 56,13 Hz: media DWM 3,46/s e harness 1,0 FPS. Ainda houve **2.770 draws** do DWM com flush imediato; batching permanece desligado. A ponte final p50 27 ms ja excede o orcamento de 17,8 ms para 56,13 Hz, antes do restante do quadro.
- O laço do Explorer persiste: nos dois PIDs foram 821 `WaitPaging ENTER` e 821 `LEAVE`, alem de 361 ocorrencias `0x80070057`. A DX.69 cortou log, nao corrigiu o contrato de recurso compartilhado. Nao houve `ResetFromTimeout`, `RestartFromTimeout` nem `GFXHUB_FAULT` no log final; nenhuma falha dessas foi observada nesta janela medida.
- O KMD arquivou 27,82 MB de log e o DWM 18,19 MB em cerca de 3 minutos. O sink KMD ainda abre/escreve/flusha/fecha em PASSIVE; o sink UMD ainda faz write-through em ate dois arquivos para muitos outros eventos. O custo exato de cada classe ainda nao foi isolado.
- Uma janela de 180 s nao testa o travamento recorrente em ~3–8 min. Nao declarar estabilidade de 600 s, jogos ou causa do travamento resolvidos. A melhora pode resultar de carga diferente; requer ensaios controlados ou perfil de tempo para atribuicao causal.

## 4. Veredito

**Sucesso no criterio A da Corrida 113**, restrito a 180 s, visual sem artefatos, taxa DWM >=2,5/s nos primeiros 120 s, amostragem ativa e rollback R1. **Sucesso parcial para o produto:** o desktop ainda e' muito lento e o travamento de longa duracao segue aberto.

## 5. Recuperacao e proximo trabalho

Atualmente BasicDisplay esta em OK/Codigo 0. O rollback deixou `CLEAN_BOOT_REQUIRED` ativo; nao apagar o marcador. Antes de novo bind, boot frio com corte de energia.

**Proximo alvo offline:** reduzir o custo dos logs rotineiros de renderizacao do DWM e do KMD preservando erros, transicoes e amostras de tempo, pois a DX.69 tratou so uma query do Explorer. Medir ganho contra a mesma janela de 120 s da 113; nao religar o lote de draws por palpite, pois ele teve piscar/travamento em corridas anteriores. Em paralelo, analisar o laço de recurso compartilhado do Explorer e a alta frequencia de invalidações sem mudar contrato de GPUVA por palpite. Depois de mais desempenho, voltar ao ensaio de 600 s: a recuperacao R1 e a estabilidade longa continuam metas distintas.

## Provas

- `display-log-this-bind-FINAL.txt`: 27.820.945 bytes, SHA-256 `E4E827B90523725D6F4C20B923D792BE29172D2E7750185D5A35C6E0EF2503DD`, arquivo final completo em duas passagens.
- `bc250_umd_log_1204-FINAL.txt` (DWM): 18.190.554 bytes, SHA-256 `F8F088A5F3EF174977E6714584F6388EBFB7465C2BB32EAF79D2A60D2023098C`.
- `bc250_umd_log_11440-FINAL.txt` e `bc250_umd_log_7744-FINAL.txt` (Explorer): SHA-256 `B59082B2198AFEE7279C9FB514AA5E5ED60B664638BC43BAE308FBF34CC43BB0` e `516ED168B5E01A1F85A0C26099CFD44CF4E83709309E4559DE6E58CACFF27A3B`.
- `safe-bind-final.json`, `safe-bind-pnp-samples.json`, `d3d11_native_conformance.log`, `selftest-resumo.txt` e manifesto de proveniencia na pasta da corrida.
