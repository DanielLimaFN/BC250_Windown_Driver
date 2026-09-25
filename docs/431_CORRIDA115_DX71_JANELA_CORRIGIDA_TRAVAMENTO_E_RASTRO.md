# Corrida 115 (DX.71): janela unica, mas travamento duro

> Bind: 24/09/2026 23:33:42, candidato lab 3.275.1.0. Evidencia: `docs/evidence/2026-09-24_23-33-42-DESKTOP-HQAG6T4-lab-binding/`. Criterio previo: doc 430 (180 s). SYS esperado e arquivado `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`; INF esperado `5D6782F9ABFF4075E5FC2C1C0F09F46DE6A53EE635004102BEF0553C136463C2`. `binding-provenance.json` e manifesto final do binder nao foram escritos antes do travamento; o guardiao arquivou versao/hash esperado e os logs da UMD contem o marcador DX.71. Esta lacuna nao deve ser ocultada.

## Avanco

**Sim na composicao visual relatada; nao na estabilidade.** Daniel viu apenas a janela do harness com quadrado azul e triangulo, sem a segunda imagem grande da Corrida 114. Nos logs, harness 60 e DWM 413 linhas `[DX71_WDDM_ONLY]`, **zero** `DIRECT SCANOUT` nos dois processos. Isso sustenta que a DX.71 eliminou a rota dupla durante a parte observada; nao e' prova byte a byte do posicionamento final. A Corrida 114 completou a janela de 180 s com imagem duplicada; a 115 travou antes de registrar fim da janela. Nao atribuir o travamento exclusivamente a DX.71 sem isolamento.

## Sucessos e metricas

- Harness D3D11: 60/60 `PRESENT_OK`, sem device-lost relatado, **1,1 FPS** medio final (114: 1,3; 113: 1,0). O harness ainda nao faz readback de pixel.
- DWM: **382 Presents nos primeiros 120 s = 3,18/s** (114: 3,09/s; 113: 2,60/s), acima do piso de 2,60/s do criterio A; 413 `Present` aceitos entre 23:33:53.040 e 23:36:01.613, cerca de 128,57 s. Taxa observada total ~3,21/s ate o corte dos logs, nao media de 180 s.
- KMD recuperado: 3 `BYTE_PROOF PASS` de quadros com conteudo, zero `BYTE_PROOF FAIL`; 413 `REDUNDANT_SKIP` de copia primaria. Tres selftests RADV indicaram 5/5. Esses dados nao provam desktop/jogos estaveis.

## Falha e limite diagnostico

O ultimo log UMD do DWM termina as **23:36:01.628** e o do Explorer as 23:36:01.691, cerca de **2 min 19 s apos o bind** e antes do prazo de 180 s do guardiao. O log KMD recuperado termina em uma sequencia de submissoes SDMA; o trecho seguinte ja pertence ao novo boot. Nenhum `ResetFromTimeout`, `RestartFromTimeout`, `GFXHUB_FAULT`, `BYTE_PROOF FAIL` ou BugCheck foi encontrado na evidencia consultada. O evento System 41 do boot seguinte registra reinicio sem desligamento correto; nao ha dump novo identificado. A hora exata em que a imagem parou ainda depende do relato de Daniel.

O rastro duravel DX.56 recuperado tem 1.325 registros validos; 1.296 pertencem ao boot do ensaio, e os demais ao boot seguinte. O ultimo do ensaio foi `writer_tick=1308`, `latest_sequence=835563`, `active_count=1`, `operation=12` (`FLUSH_TLB`) e `stage=5` (`BEFORE_DISPATCH`). **Isto nao identifica a causa:** a mesma combinacao `active_count=1, op12, stage5` ocorreu em **933/1.296** amostras do boot (~72%). E' uma operacao muito frequente; aparecer por ultimo nao constitui evidencia discriminante de deadlock no flush. A pagina DX.66 nao conservou magic valido no boot seguinte (`ANTERIOR AUSENTE`), portanto as 96 migalhas de MMIO/DDI nao puderam resolver a instrucao. Nao repetir a inferencia anterior de que a fase 5, por si, localiza o defeito.

## Veredito e recuperacao

**E: falha por travamento e rollback apenas em outro boot.** Houve correcao visual observada e DWM acima do piso, mas o criterio A exige completar a janela e R1 no mesmo boot. O guardiao permaneceu em `OBSERVING` no boot armado (`armed_boot_ticks=639259002235000000`). Somente apos novo boot (`boot_ticks=639259304215000000`) iniciou rollback as 07:54:15 de 25/09 e confirmou `RECOVERY_OK_ONLINE` as 07:54:26, com cinco amostras BasicDisplay Codigo 0 entre 07:54:19 e 07:54:24. BasicDisplay e' recuperacao, nao sucesso do nosso DWM. `CLEAN_BOOT_REQUIRED` esta ativo. O fusivel recusou uma tentativa de StartDevice no novo boot antes do rollback; o inspetor atual mostra arquivo de fusivel ausente apos a recuperacao.

## Proximo trabalho

Nao repetir a mesma corrida sem uma medicao que diferencie as hipoteses. Preservar a DX.71 por enquanto como candidata visual, mas tratar confiabilidade como bloqueador. A inspecao somente leitura de `tools/arm-crash-dump-on-hang.ps1` em 25/09 encontrou `CrashDumpEnabled=2` (kernel), `AlwaysKeepMemoryDump=1` e `CrashOnCtrlScroll=1` nos drivers `i8042prt` e `kbdhid`. O `C:\Windows\MEMORY.DMP` continua antigo (24/09 13:56:30, 887.730.418 bytes). Na proxima janela controlada, se houver travamento e o teclado ainda responder, tentar **Ctrl direito segurado + Scroll Lock duas vezes** antes de reset fisico; aguardar a gravacao do dump e analisar as pilhas. Se o teclado nao responder, registrar isso como resultado diagnostico e so entao resetar. O atalho pode falhar quando o kernel/teclado tambem esta bloqueado. A alta frequencia de `FLUSH_TLB` torna a ultima etapa de paginacao pouco informativa, e a pagina DX.66 nao sobreviveu a este reset. Nenhum 600 s, 60 FPS ou jogo esta validado.

Provas recuperadas: `bc250_display_log-tail-recovered.txt` 22.022.411 bytes, SHA-256 `C472DB3E6D513459C6B1AC10D9EC27E62280323302B27B675A1D7177ABF32E82`; `paging-trace-this-bind-RECUPERADO.bin` SHA-256 `CD976C7FE4049254D8DF1C99149FEF2770E9AC9379EA9AACE030E0E106380EE1`; DWM UMD SHA-256 `385EB69A6B06564D1EA8C72DEB8EB1051B6CFA770E08C64A3A27CAD2171DBD99`; demais logs e `safe-bind-final.json` na pasta.
