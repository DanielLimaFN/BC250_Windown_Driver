# 459 — Corrida 129 (DX.82): travamento precoce, paginação pendente e causa ainda aberta

**Data:** 25/09/2026, bind às 18:09:04 (São Paulo). **Candidato:** DX.82, DriverVer 3.287.1.0, SYS SHA-256 `DABE03E2BFDBC7471B0DAF345A9370855031FA993BBFEE083900262ADB0DC8DF`. **Critério anterior:** [458](458_CRITERIO_CORRIDA129_DX82_SINCRONIZACAO_TEARDOWN_E_600S.md). **Provas:** [pasta da corrida](evidence/2026-09-25_18-09-04-DESKTOP-HQAG6T4-lab-binding). A cópia `display-log-this-bind.txt` foi feita cedo, às ~18:09:15; para esta análise, recuperei o delta posterior do log KMD e o rastro binário de paginação pelos offsets dos manifestos da pasta. SHA-256 dos deltas: log `65F92C18F1622A877EB86BB376732DBAC9DFD03436789854CD2C29D1DF6DC837`; trace `A7AC423A60FDF3CC451552A4C5C875572AEAFA2C6DA5D6D285747E7E5F4E4891`.

## Avanço e veredito

**Não houve avanço funcional frente à Corrida 128. Falha no critério de 600 s.** A 128 chegou a ~214 s e completou o harness 60/60; a 129 deixou de progredir cerca de 90 s após o bind, antes de completar o primeiro `Present` do harness. Houve **avanço diagnóstico parcial**: a última etapa do Explorer é espera da paging fence 7004, enquanto o DWM está no teardown da bridge; a evidência não demonstra que o `SRWLOCK` esteja correto nem que seja a causa do hang. A correção DX.82 **não fechou** a barreira dos dez minutos.

## Linha temporal com o que foi medido

| Horário | Evidência | Leitura |
|---|---|---|
| 18:09:04–05 | baseline e configuração do watchdog | Início do bind DX.82, meta 600 s. |
| 18:09:23 | UMD DWM pid 1196 | Três `vkAllocateMemory (Image)` falharam com `res=-2`; `MakeResident` falhou com `0x8007000E` e `CreateResource` recusou. O DWM desse pid não é prova de composição estável. |
| 18:10:09–17 | UMD DWM pid 14644 | Primeiro `VkBridgeShutdown` demorou 8,3 s, mas **concluiu**. |
| 18:10:29–30 | UMD harness pid 15160 | Shader/pipeline compilados; primeiro draw e leitura do alvo tinham pixels não nulos (centro `0xFF7F4040`, grade 28800/28800). Às 18:10:30.837 entrou em `pfnPresentCb`, sem linha de retorno. `conformance-stdout.tmp` ficou no anúncio do pipeline, sem `PRESENT_OK` nem Exit 0. |
| 18:10:30–31 | UMD DWM pid 14644 | `DestroyDevice` começou 18:10:30.960 e `VkBridgeShutdown ENTER` às 18:10:31.001; não há `LEAVE` na cauda recuperada. Esse log não identifica em qual chamada interna parou; a primeira é `vkDeviceWaitIdle`. |
| 18:10:31–37 | UMD Explorer pid 6232 | `Allocate` de 4 KiB ficou ~6,8 s no callback, retornou; `MapAllocation`/`MakeResident` deram pending, e `WaitPaging ENTER` para fence 7004 ocorreu às 18:10:37.935 sem `LEAVE`. Portanto essa espera é posterior ao início do segundo shutdown do DWM. |
| antes do reboot | delta KMD e DX.56 | 14.258 linhas do boot original; 215 registros `[Present #` (várias linhas por chamada), 4 `COMPOSITE_BLT OK`, 16 recusas por `GEOMETRIA_DA_ORIGEM_DESCONHECIDA`. Zero linhas `Journal NO_SPACE`, `PUBLICACAO_FALHA`, `McdmResetFromTimeout`, `IB_TRIPOD TIMEOUT` ou `Blt Timeout` nesse trecho. Último submit SDMA #1024 foi aceito; não há conclusão registrada. |
| boot original, fim do trace | DX.56 | 930 registros válidos. Último: writer tick 930, `latest_sequence=112504`, `active_count=1`, estágio 5/ operação 12 (`FLUSH_TLB`) com `STATUS_PENDING`. O escritor do rastro permaneceu vivo ~12,3 s após a última transição de paginação. Estágio 5/operação 12 também aparece em corridas antigas sem localizar a causa. |
| 18:14:40–46 | EventLog e watchdog | Houve novo boot. O `boot_ticks` mudou; a migalha DX.66 disse `EM_EXECUCAO(sem StopDevice)`. Não há dump novo nem registro de BugCheck que classifique o reset. |
| 18:19:06–12 | watchdog | O rollback executou **no boot posterior** e confirmou `BasicDisplay` Código 0 em 5 amostras. Isso não é recuperação online do travamento no boot original. |

## O que a corrida discrimina — e o que não discrimina

1. **A hipótese da 128 foi afirmada com certeza excessiva.** Na 128, a sobreposição temporal entre `WaitPaging`/`vkAllocateMemory` e teardown do Explorer identificava uma corrida plausível, mas não provava deadlock nem o mecanismo do congelamento global. Na 129, a proteção nova foi aplicada, mas o sistema congelou mais cedo com uma sequência diferente: DWM em teardown, harness em `Present`, Explorer em `WaitPaging` e paginação KMD sem avanço. Não cabe afirmar que a mesma corrida foi reproduzida, nem que a DX.82 a eliminou.
2. **O SRWLOCK pode piorar esperas, mas não está incriminado por estes logs.** `CreateResource` mantém lock compartilhado durante callbacks WDDM e `WaitPaging`; teardown requer exclusivo. Uma paging fence que não progride deixa o teardown desse *mesmo device* esperando indefinidamente. Os logs finais não mostram um `DestroyDevice` do Explorer concorrendo com esse `CreateResource`, e o shutdown final é de outro processo (DWM). Instrumentar aquisição/liberação por device, thread e tempo antes de atribuir causalidade.
3. **Há falhas anteriores ao congelamento que comprometem a meta de desktop íntegro.** O DWM inicial teve falta de memória Vulkan/WDDM; o KMD recusou apresentações pela geometria de origem desconhecida. Mesmo que a máquina sobrevivesse, essa corrida não fecharia a qualidade visual apenas com `Exit 0`.
4. **O último evento não prova causa.** O submit SDMA #1024 aceito não prova que ele travou. A migalha global DX.66 mais recente foi `OTG_FRAMECOUNT_READ DEPOIS` (seq. 929069); a `PAGING_DDI ... ANTES` na CPU10 tem seq. 810127, anterior. O trace DX.56 tem operação de paginação pendente por ~12 s enquanto seu escritor roda; isso é sinal para investigar o caminho de paginação e as dependências de fence, sem declarar `FLUSH_TLB` causa raiz.

## Recuperação e próximo teste justificado

Estado após rollback: `BasicDisplay` Código 0; `CLEAN_BOOT_REQUIRED` ativo. **Não validar como R1 no boot original.** Continuam não validados em hardware: 600 s contínuos, harness 60/60 nesta versão, renderização/desktop sustentados sem falha, 60 FPS, jogos e a segurança real da sincronização DX.82.

Antes de outro bind, obter um discriminante para cada espera: logs UMD com PID/TID/device e ENTER/LEAVE ao redor do `SRWLOCK`, `vkDeviceWaitIdle`, `pfnPresentCb` e `WaitPaging`; no KMD, ligar os IDs de paging fence e submit às filas/contexts, registrar WPTR/RPTR/fence físico e status de `FLUSH_TLB` sem escrever DCN. Capturar dump/ETW do travamento se o mecanismo de coleta suportar. Investigar separadamente `0x8007000E`/`VK_ERROR_OUT_OF_DEVICE_MEMORY` e as recusas de geometria. Não escolher outra correção causal só pela última linha de log.
