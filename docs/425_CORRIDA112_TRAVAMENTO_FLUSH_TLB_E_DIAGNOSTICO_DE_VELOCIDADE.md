# Corrida 112: travamento durante paginacao e diagnostico da lentidao

> **Bind:** 24/09/2026 22:23:35, DX.68, pacote lab 3.272.1.0, SYS `51BF07646E9998A48883AF3443B45DF33FB073AD8FF426B8D9560D6D022EF886`. **Evidencia:** `docs/evidence/2026-09-24_22-23-35-DESKTOP-HQAG6T4-lab-binding/`. **Criterio previo:** doc 424. O Daniel relatou travamento duro e reset pelo botao. Este documento separa medicao de hipotese.

## 1. Houve avanco?

**Funcional: nao. Diagnostico: sim.** A Corrida 111 travou com todos os slots de MMIO em `DEPOIS`; a 112 recuperou uma DDI externa de paginacao ainda em `ANTES`: CPU08, `BuildPagingBuffer` operacao 12 (`FLUSH_TLB`). A ultima leitura SDMA instrumentada, na CPU00, estava em `DEPOIS`. Isto localiza uma chamada em andamento no ultimo estado preservado, **nao** a instrucao exata nem a causa de todo o congelamento. A sobreposicao VidMm/TMR corrigida na DX.67 continuou ausente.

## 2. Sucessos e medidas

- **P0:** `[DX67_VRAM_MAPA]` manteve segmento `[0xA8C000,0xFA00000)`, antes da pagina DX.66 e do TMR. A pagina armou em `VRAM 0xFA00000`, `Phys 0xCFA00000`, 96 slots em tres camadas. No boot seguinte: mesmo nonce, estado `EM_EXECUCAO(sem StopDevice)`, sequencia global 5.673.475.
- **P0:** CPU08, camada `PAGINACAO`, seq. 5.673.438, `PAGING_DDI detalhe=0xC`, fase `ANTES`; CPU00, camada interna, seq. 5.673.475, `SDMA_REG_READ 0xFCC`, fase `DEPOIS`; slots recentes de submissao estao em `DEPOIS`. A CPU08 nao tem slot interno mais novo que sua entrada na DDI. Uma espera por lock ou operacao nao instrumentada e' possivel, ainda H.
- **P0:** rastro DX.65 recuperado com **4.149 registros validos e zero invalidos**. O boot seguinte comeca no indice 4.132. Ultimo registro do boot da corrida: `writer_tick=4132`, `snapshot_time_100ns=6069907503`, `active_count=1`, ultima etapa `BEFORE_DISPATCH(5)`, `operation=12`, seq. 2.097.020. Zero batimento posterior. Boot 22:20:35 + 606,99 s = aproximadamente **22:30:42**; ~7 min 7 s apos o bind. A hora e' da ultima gravacao duravel, nao da transicao visual exata.
- **P0:** harness 60/60 `Present` aceitos, media final **1,0 FPS**; os testes RADV reportaram 5/5. No DWM, 734 inicios de `Present` em 413 s: **1,78/s** medio; intervalo p50 185 ms, p90 1.382 ms, maximo 8.886 ms. A Corrida 111 teve 1.115 inicios em 362 s: **3,07/s**, p50 136 ms, p90 593 ms. Uma comparacao de duas corridas nao atribui a queda a DX.68.
- **P0:** na 112, o tempo dentro de `Present` foi p50 **30 ms**, p90 105 ms; a ponte Vulkan→WDDM foi p50 **15 ms**, p90 78 ms. O flush de comandos teve `SubmitUs` p50 **1,87 ms** e `EsperaUs` p50 **0,78 ms**. Logo a copia final e a espera de fence nao explicam sozinhas o intervalo mediano de 185 ms entre quadros.
- **P0:** os **3.250 draws** registrados pelo DWM tinham `Anel=0, Direto=0, FlushAgora=1`: **100% com flush imediato**. Houve 6.173 flushes no processo. O lote esta desligado por contrato DX.55 apos piscar na 96 e travamentos com lote ligado na 94–96/99; religar cegamente nao e' correção demonstrada.
- **P0:** o Explorer escreveu 66,6 MB de log entre 22:24:58 e 22:30:40: cerca de **550.800** linhas de `CheckMultisampleQualityLevels`, 19.437 de `CheckFormatSupport`, 1.512 `WaitPaging ENTER` e 1.511 `LEAVE`, alem de 661 ocorrencias `0x80070057`. O DWM escreveu 21,6 MB de log e o KMD recuperado 46,8 MB. O sink da UMD usa `FILE_FLAG_WRITE_THROUGH` e ate dois `WriteFile` por linha; o KMD em PASSIVE abre, escreve, faz `ZwFlushBuffersFile` e fecha por linha. A linha de query MSAA, cujo resultado e' apenas 1 para uma amostra e 0 para as demais, representa **85% das ~646 mil linhas do Explorer** e e' um alvo de custo mensuravel.
- **P0:** o modo herdado do GOP e' **2560×1080 a 56,13 Hz** (`VSync=5613/100`). Com o modo atual, o alvo de atualizacao visivel e' perto de 56 quadros/s. 90 ou 200 quadros/s exigem outro modo/timing e trabalho posterior de display; renderizacao interna acima do refresh e' uma metrica distinta.

## 3. Falhas, limites e hipoteses

- **P0:** travamento duro antes de 600 s; o watchdog nao reverteu o sistema parado. Nao ha `ResetFromTimeout`, `RestartFromTimeout` nem `GFXHUB_FAULT` no log recuperado; nao foi identificado novo BugCheck. O ultimo log KMD do boot original pode ter cauda perdida em cache. Nao atribuir a falha a ultima linha textual.
- **P0/H:** a DDI `FLUSH_TLB` estava em andamento e o escritor duravel parou com `active_count=1`. O handler entra em `ObjectLock` e, enquanto o segura, faz flush HDP e invalida VMIDs; ha caminho potencial de espera por lock ou operacao longa. Ainda nao ha evidencia para escolher entre bloqueio no lock, MMIO nao instrumentado, interdependencia com outra CPU ou parada externa ao driver. A fase `ANTES` isolada nao prova deadlock.
- **P0/H:** o log UMD do Explorer termina em `WaitPaging ENTER` para fence 7002 as 22:30:40, sem `LEAVE` posterior. Isto e' coerente com paginacao parada, mas um processo aguardando uma fence pendente pode ser vitima, nao causa.
- **H de desempenho:** as escritas sincrónicas de log e o laço de queries do Explorer consomem tempo; o volume e o mecanismo sao P0, o ganho de FPS ao reduzir logs ainda **nao foi medido**. O flush por draw e' P0, mas sua contribuicao isolada e' incerta. A ponte de 11 MB por quadro e a amostragem de pixels tambem contribuem, porem a amostragem de grade ja ocorre so a cada 64 Presents.

## 4. Veredito

**Falha** frente ao criterio de 600 s; **desfecho B** do doc 424. A instrumentacao DX.68 cumpriu seu objetivo diagnostico, mas nao resolveu o travamento. A degradacao de velocidade permanece grave: DWM medio 1,78/s na 112; harness 1,0 FPS. Nenhum resultado desta corrida valida 60 FPS, estabilidade ou jogos.

## 5. Recuperacao e proxima prioridade

O guardiao confirmou BasicDisplay **Codigo 0** em cinco amostras as 22:37:54–58 e `RECOVERY_OK_ONLINE` as 22:38:01, **no boot seguinte** (`armed_boot_ticks=639258960355000000`, `boot_ticks=639258970355000000`). Nao e' recuperacao R1 no boot travado. `CLEAN_BOOT_REQUIRED` segue ativo; nao apagar nem fazer bind antes de boot frio com corte de energia.

**Decisao:** pausar ensaios longos de 600 s para desenvolver velocidade em etapas curtas e mensuraveis, mantendo migalhas e rastro de paginacao. Primeiro reduzir, de modo controlado, o log de sucesso da query MSAA no Explorer; seu contrato de retorno nao muda. Medir 180 s com o mesmo workload, DWM Present/s e p50/p90, harness FPS, tamanho/linhas de log, eventos de paginacao e pixels/artefatos. Se houver ganho, atacar outros logs quentes e depois o laço de recurso compartilhado do Explorer com ETW/contrato; so apos isso reconsiderar batching com prova de residencia. O travamento continua bloqueador de confiabilidade e precisa de volta antes de declarar desktop pronto.

## Provas arquivadas

- `display-log-this-bind-RECUPERADO.txt`: 46.832.624 bytes; SHA-256 `A8676625A8CEEA6357B3C8D67401F186AFA736C60B1B7D11846380CD4511E396`.
- `paging-trace-this-bind-RECUPERADO.bin`: 2.290.248 bytes; SHA-256 `C75EFEE4C9C97B3882F8AA4DAE0261048CB60AC3C221B02AE9BADCD5F82E74FA`.
- Logs UMD apos travamento, `safe-bind-final.json`, manifesto e pacote assinado ficam na pasta da corrida.
