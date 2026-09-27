# Corrida 101 — DX.56, rastro durável e travamento (24/09/2026)

## Identidade e critério

- Bind às 11:42:50 (America/Sao_Paulo), após boot frio informado pelo Daniel. Candidato lab `3.262.1.0`, SYS SHA-256 `2C350D18A69EB17EACFE9AFB3C2E4171E5C5B0EEA0F4C38198347ED28939F5AC`; UMD `51288052EA9B12670702D4264C409F3D24025354EE12275016802CDBF53DD6E`; RADV `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`.
- Critério escrito antes da corrida: [docs/404](404_DX56_RASTRO_DURAVEL_DE_PAGINACAO_E_CRITERIO_CORRIDA101.md). Variável comportamental única: persistir snapshots dos estágios de paginação; limite de 600 s.
- Evidência: [pasta da corrida](evidence/2026-09-24_11-42-50-DESKTOP-HQAG6T4-lab-binding/). O Daniel informou que precisou reiniciar porque o watchdog não reverteu durante o travamento. Os nove logs UMD originais foram copiados de `C:\Windows\Temp` depois do reboot para `umd-logs-pos-travamento/`, com `SHA256SUMS.txt`.

## Linha do tempo e provas

| Hora local | Fato | Prova / limite |
|---|---|---|
| 11:42:50–57 | Bind e watchdog armados; prazo absoluto ~11:52:52. | `binding-provenance.json`, `safe-bind-watchdog-config.json`, `safe-bind-watchdog-journal.jsonl`. |
| 11:43–11:44 | Harness selecionou BC-250 `1002:13FE`; 60/60 `Present` aceitos, 0 falhos. Média mostrada de 1,0 FPS ao final. | `conformance-stdout.tmp`; isso não mede pixel no monitor nem velocidade estável do desktop. |
| 11:45:03–07 | O binder copiou os logs UMD enquanto a corrida ainda estava ativa. A bridge havia inspecionado amostras/grade não nulas no buffer linear. | `bc250_umd_log*.txt`, `d3d11-native-conformance-console.txt`; estes arquivos são um **snapshot intermediário**, não a cauda do travamento. A amostra da UMD não prova scanout. |
| 11:49:27,041–058 | Últimas linhas dos originais UMD: DWM em `DestroyResource`/`Bc250UmdVkDestroyResource` e Explorer destruindo estados do device. | `umd-logs-pos-travamento/BC250_UMD_LOG_1176.txt` e `_2532.txt`; ausência de linha seguinte não atribui a causa à chamada. |
| Até ~11:49:27,714 | DX.56 gravou 3.567 registros válidos do boot original, sem corrupção, último seq. 1.991.231 e `active_count=1`. | `paging-trace-this-bind-RECUPERADO.bin/json`, `paging-trace-recovery-manifest.json`; SHA-256 `F188648D456A6D33C89D930A0E3038B920AE618F4E0DBE2BF7C656D8EE90E897`. Hora derivada dos ticks do boot e tempo de interrupção, não de carimbo de parede em cada registro. |
| ~11:52:52 | Prazo de 600 s do watchdog. | Não há execução do rollback no jornal antes do reboot; a máquina não permitiu que a tarefa de usuário agisse. |
| 11:59:37–12:00:04 | Reinício manual após travamento; eventos de boot e desligamento inesperado (Kernel-Power 41, EventLog 6008). | Evento 41 não é, por si, BugCheck. Não foi encontrado Event 1001 na janela consultada. |
| 12:00:13–24 | Guardião retomou no novo boot, fez rollback e confirmou `BasicDisplay` Código 0 em 5 amostras. | `safe-bind-watchdog-journal.jsonl`, `safe-bind-final.json`; `RECOVERY_OK_ONLINE` é recuperação do BasicDisplay, não êxito do DWM no driver BC-250. |

O arquivo DX.56 tem **3.568** registros válidos de 552 bytes, sem registros inválidos: 3.567 do boot da corrida e **um registro zerado do boot seguinte**. O horário de modificação do arquivo perto de 12h corresponde ao registro pós-reboot; não significa que o rastro da corrida avançou até 12h. A captura do log KMD pelo offset inicial tem 40.249.235 bytes, SHA-256 `29C6D6346522686A14A8F8B50EECABC4B3DD4A076B61AEA00A114383120F1DEF`; seu final também contém linhas do boot seguinte e precisa ser separado antes de interpretar a cauda.

No último registro pré-reboot, estágio 5 = antes do despacho de `BuildPagingBuffer`, operação 12 = `DXGK_OPERATION_FLUSH_TLB` (valor confirmado no WDK `d3dkmddi.h`), status de entrada `0x103`. **Isso não localiza a causa.** Nos 100 snapshots finais, 76 já mostravam esse mesmo par estágio/operação, enquanto a sequência continuava crescendo; é uma posição comum de amostragem. Os 20 últimos snapshots chegaram em intervalos de ~100 ms. O KMD arquivado termina com GFX RingFence 4509 aceito, SDMA #22402 com tripé provado e SDMA #22403 aceito sem conclusão gravada; a última linha do logger também não prova que essa submissão travou. Houve uma linha `ActiveFlushTlb ... STATUS_IO_DEVICE_ERROR` muito antes da cauda, cuja relação com o travamento é desconhecida.

O binder copiou os logs UMD às 11:45:03–07, durante a corrida. Por isso, as linhas finais daquela cópia não eram evidência de bloqueio. A **cauda real** foi recuperada: DWM seguiu até 11:49:27,041 e Explorer até 11:49:27,058. O último registro DX.56 é ~0,66 s posterior. A última ação UMD de DWM foi destruição de recurso Vulkan; a última de Explorer, destruição de blend state. Nenhuma das duas localiza a causa sozinha. Hashes SHA-256: DWM `3A8B5C3B29A8889EF703FEFE6B4F883CECC048D370A177C6DAA05D9367E8BA00`; Explorer `FCFEBD444E9E68B2EABCD8AE57A79BD31FDBFE0312C68CA15C1AF5731B841C78`. O log KMD contém 2.215 `KMD_CONTEXT_CREATE` e 2.161 `KMD_CONTEXT_DESTROY` antes do reboot, da mesma ordem de ~345 criações/min da Corrida 100; o laço de contextos permanece candidato a carga, não causa provada.

## Avanço, falhas e veredito

**Avanço diagnóstico: sim.** Na Corrida 100, o rastro de paginação se perdia com o reset. Agora a trilha persistiu com checksum até 11:49:27. O harness, que não rodou na 100 por defeito do wrapper, concluiu 60/60 na 101. **Avanço funcional/estabilidade: não demonstrado**; o sistema voltou a travar e os 600 s não foram concluídos.

**Falhas:** travamento duro relatado pelo Daniel; rollback não ocorreu no boot travado; reboot manual necessário. Não há TDR/timeout ou BugCheck identificado nos registros examinados, mas a ausência de dump e a perda possível da cauda do logger não provam que nenhum ocorreu. A taxa média de 1,0 FPS do harness é baixa e não fecha a meta de desktop rápido. Pixel correto no monitor, aplicação/jogo não modificado, 600 s de desktop estável e recuperação sem reboot continuam **NÃO VALIDADOS EM HARDWARE**.

**Veredito frente ao critério prévio: parcial, desfecho A (travamento com rastro recente).** A persistência DX.56 foi validada no silício e seu último registro é ~0,66 s após a última linha UMD do DWM recuperada. Isso satisfaz a proximidade prevista no [critério da Corrida 101](404_DX56_RASTRO_DURAVEL_DE_PAGINACAO_E_CRITERIO_CORRIDA101.md), embora a hora exata em que a tela congelou não tenha sido medida. A última operação registrada não identifica a causa. A janela completa e a recuperação online no boot original falharam.

**Recuperação e próximo passo:** o guardião executou rollback apenas após o reboot manual, confirmou BasicDisplay Código 0 e retirou o fusível; marcou `CLEAN_BOOT_REQUIRED`. Antes de outra corrida física, cumprir esse boot frio. Investigar a rotatividade de contextos do Explorer e a recusa repetida da textura compartilhada de 1024x832 (`hr=0x80070057`) que a alimenta; selecionar uma correção discriminante sem tratar a última operação gravada como causa. A limitação do watchdog é estrutural: a tarefa de usuário não executa enquanto o Windows inteiro está travado; a recuperação persistente no boot seguinte funcionou.
