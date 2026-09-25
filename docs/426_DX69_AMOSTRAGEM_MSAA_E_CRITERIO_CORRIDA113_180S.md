# DX.69: reduzir o log da query MSAA e medir velocidade em 180 s

> **Data:** 24/09/2026. **Base:** [Corrida 112](425_CORRIDA112_TRAVAMENTO_FLUSH_TLB_E_DIAGNOSTICO_DE_VELOCIDADE.md). Esta e' uma alteracao **offline**; nenhum bind com DX.69 ocorreu. A meta final segue jogos e desktop acelerado na BC-250, sem WARP/CPU fallback.

## Variavel isolada

`Bc250Umd10CheckMultisampleQualityLevels` devolve exatamente o mesmo resultado: 1 nivel para `SampleCount<=1`, zero para os demais. Somente seu log de sucesso passou a sair nas primeiras 16 chamadas por processo e depois a cada 4.096 chamadas, com ordinal. Na Corrida 112, o Explorer emitiu aproximadamente **550.800** linhas desta query (85% das linhas do arquivo desse processo); cada linha da UMD atravessa `OutputDebugStringA`, `stderr` e ate dois arquivos `FILE_FLAG_WRITE_THROUGH`. O ganho de FPS e a relacao causal ainda sao **H** ate a Corrida 113.

KMD, pagina de migalhas DX.68, rastro DX.65, GfxWaveProbe, modo de display, batching desligado, UMD de renderizacao e politica da Radeon Software seguem com o contrato da 112. O pacote tem KMD recompilado dos mesmos fontes por exigencia do empacotador; o hash do binario assinado muda com o novo pacote, mas o diff funcional da fonte e' a amostragem UMD.

## Portoes offline e candidato

- `driver/umd/build-umd.bat`: EXIT=0; `driver/wdk/build-contract.bat` e lab: EXIT=0; auditorias de seguranca e pacote: PASS.
- Pacote lab assinado **3.273.1.0**, perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, GfxWaveProbe=1. SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`; UMD `62CE1872F9CB8A3645C468E620C2376D702719E6F8DAB98C2739806BCCE88B33`; INF `494DE8F1711371D88E35B9F58F96B57D14F1C2F58D4DDCE351C52C4A79338B7F`; CAT `FF332E95A3C69A3C9FCE4815BBCF8DFC2FFEE933FE41F340F970A6DCEA9AAEC2`.
- Preflight somente leitura `tools/prepare-corrida113-dx69.ps1`: PASS. Runner elevado `tools/EXECUTAR_CORRIDA113_DX69_180S.cmd` fixa o guardiao em 180 s. Nenhuma instalacao foi feita.

## Criterio escrito antes da Corrida 113

**Controle:** boot frio com corte de energia porque `CLEAN_BOOT_REQUIRED` esta ativo; BasicDisplay Codigo 0 antes do bind; Radeon Software impedida de abrir; hashes KMD/UMD/RADV e manifesto conferidos; DX.67 publica segmento abaixo de 0xFA00000; DX.68 arma 96 slots. `DX69_MSAA_SAMPLE` deve aparecer com ordinais iniciais e depois 4096, sem linhas individuais intermediarias. O teste mede desempenho por 180 s; nao fecha estabilidade de 600 s.

| Desfecho | Condicoes | Interpretacao |
|---|---|---|
| **A** | Controles completos, desktop legivel sem artefatos, BasicDisplay/0 em cinco amostras no mesmo boot; log MSAA amostrado; DWM **>=2,5 Presents/s** nos primeiros 120 s a partir do primeiro `Present` | Ganho grande frente a 111 (2,13/s) e 112 (1,35/s) na mesma janela. Ainda nao prova que so o log causou o ganho. |
| **B** | Controles completos, log amostrado, mas DWM <2,5 Presents/s na janela ou sem melhora material do harness | Query MSAA nao basta; atacar os demais custos medidos, sem alegar 60 FPS. |
| **E** | Travamento, TDR/BugCheck, artefatos, falha de controle, rollback fora do mesmo boot ou janela incompleta | Descrever a observacao; nao declarar A ou B. Em travamento, reset a quente e coleta das migalhas. |

Medir tambem FPS do harness, p50/p90 dos intervalos entre Presents, tempo dentro do Present, `BridgeLinearToWddm`, flush/espera, tamanho e numero de linhas dos logs Explorer/DWM/KMD, `WaitPaging`, `FLUSH_TLB` e pixels. O objetivo intermediario e' aproximar o desktop do teto de **56,13 Hz do modo GOP atual**. 60/90/200 atualizacoes visiveis requerem outro modo de display, fora deste ensaio.

**Recuperacao:** o guardiao deve restaurar BasicDisplay no mesmo boot apos 180 s. Se o kernel congelar, ele so podera agir apos o reset. Nao apagar `CLEAN_BOOT_REQUIRED`; antes do bind fazer boot frio. O travamento duro continua bloqueador de produto e nao e' considerado resolvido por uma corrida curta.
