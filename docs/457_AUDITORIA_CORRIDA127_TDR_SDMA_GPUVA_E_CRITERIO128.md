# Auditoria da Corrida 127 — timeout SDMA, TDRs e pressão GPUVA omitidos do veredito inicial

**Data da auditoria:** 25/09/2026. **Natureza:** reanálise offline de uma corrida física já executada; nenhum bind ou teste de hardware novo. **Candidato ensaiado:** DX.80b / 3.285.1.0, Corrida 127. **Candidato seguinte:** DX.81 / 3.286.1.0, apenas preparado offline.

**Evidência principal:** `docs/evidence/2026-09-25_16-01-35-DESKTOP-HQAG6T4-lab-binding/bc250_display_log-tail-recovered.txt`. O manifesto `display-log-tail-recovered.json` informa `bind_start_offset=2680671713`, `tail_start_offset=2681134819`, `tail_end_offset=2741160628`, SHA-256 `D970BA07224CFCA4E0E997BCD0E5EDBA90C770E210FF63B59FFDE114720463D2` e delimitação pelo próximo bind. O `display-log-this-bind.txt` menor foi copiado antes do fim do ensaio; não usar sua ausência de eventos como prova de ausência no bind.

## Veredito da auditoria

**O diagnóstico causal publicado em `docs/455` e reutilizado em `docs/456` não está sustentado pelo log completo.** O primeiro escape problemático **chegou ao KMD**. Antes dele, a fila SDMA teve `IB_TRIPOD TIMEOUT`; dentro do escape houve `SdmaPresent Blt Timeout` e `COMPOSITE_BLT FALHOU no silicio`. Em seguida ocorreu `McdmResetFromTimeout`. O retorno `E_FAIL` observado pela UMD é compatível com essa falha física de cópia. A hipótese de que `HardwareAccess=1` com `hContext=NULL` reteve o escape no VidSch **antes de chamar o KMD** está refutada para esse quadro.

O trecho recuperado contém **23** linhas `[McdmResetFromTimeout] TDR: quiesce fisico do SDMA0 iniciado` e **23** linhas de restart correspondente. Portanto “zero TDR” e “sem TDR de hardware” no resumo da Corrida 127 e no topo da base são falsos para o escopo desse log. O Windows permaneceu recuperável e o rollback R1 foi bem-sucedido; isso não equivale a 600 s de operação sem TDR nem a desktop contínuo.

## Linha do tempo da primeira falha

| Ordem | Evidência direta | Localização |
|---|---|---|
| 1 | DWM `pid=1212` conclui flush Vulkan, `DrawsNoLote=3`; entra no bridge 2560×1080 com `SrcVa=0x245400000`, `DstVa=0x2000D0000` às 16:03:29.723. | `bc250_umd_log_1212.txt:37140-37143` |
| 2 | Pouco antes da cópia do escape, Node 0 registra `IB_TRIPOD TIMEOUT (ASYNC 2000 ms)`: `TargetWPTR=530884`, `RPTR=530848`, `FenceObs=43DAFFFE` (*poison*). | `bc250_display_log-tail-recovered.txt:84649` |
| 3 | O KMD registra `McdmEscape ENTER #365`, `COMPOSITE_BLT ENTER` e resolução bem-sucedida **das mesmas duas GPUVAs**. Logo, o escape entrou no KMD. | `bc250_display_log-tail-recovered.txt:84650-84652` |
| 4 | A cópia SDMA não completa: `TargetWPTR=531132`, `RPTR=530848`, `FenceObs=43DAFFFE`; o KMD escreve `COMPOSITE_BLT FALHOU no silicio`. | `bc250_display_log-tail-recovered.txt:84653-84654` |
| 5 | O KMD inicia `ResetFromTimeout` e depois registra restart físico de SDMA/CP_RB0. | `bc250_display_log-tail-recovered.txt:84655`, `84690-84691` |
| 6 | A UMD retorna do bridge às 16:03:33.895 com `hr=0x80004005`; intervalo de 4,172 s. `ResultCode=0` no payload não transforma `hr` falho em sucesso. | `bc250_umd_log_1212.txt:37142-37145` |

**Limite:** os logs não provam por que o primeiro IB SDMA deixou de progredir. O timeout inicial e a fence sobre poison são observações; atribuir o bloqueio a GPUVA, concorrência entre engines, pacote, ring ou sequenciamento de reset ainda exige discriminação. A semelhança dos 4,172 s com um prazo do scheduler não demonstra, por si só, onde o tempo foi gasto.

## Outros achados relevantes

1. **TDR repetido e GFX também parando.** As 23 entradas de reset começam na linha 84655 e a última está na 477462. Já o segundo evento vem precedido de `[TDR_GFX_PROBE] Responsive=0 (RPTR 316->316 WPTR 376->376)` nas linhas 92767-92768. Em vários eventos posteriores o tripé GFX registra `Perna1=NAO_CONVERGIU`. Não classificar todos os 23 resets como falha exclusivamente SDMA: o mecanismo exato de cada evento precisa de análise individual.
2. **GPUVA sob pressão real.** O mesmo trecho tem 367 linhas `Journal NO_SPACE` (a primeira na linha 26) e 28.534 linhas `PUBLICACAO_FALHA` (a primeira na linha 47386). Essa primeira publicação falha em lote de 913 mapas quando há 787 faixas livres (`Cap=1024`, `Mapped=237`): além de capacidade, a ordem/atomicidade de publicação merece auditoria. A ocupação `Mapped=1024`, sem faixas livres, aparece pela primeira vez na linha 137146, **depois** do primeiro TDR na 84655. A ampliação para 4096 pode aliviar pressão tardia, mas não está provado que explica ou corrige o primeiro timeout.
3. **Semântica do erro de journal.** Em `driver/wdk/bc250_active_miniport.c:15089-15103`, `Journal NO_SPACE` aborta a transação parcial do journal, marca os mapeamentos não operacionais, mas retorna `STATUS_SUCCESS` depois de escrever PTEs físicas. Não interpretar esse `SUCCESS` como publicação GPUVA bem-sucedida. O log de `ActiveFlushTlb` mostra `STATUS_IO_DEVICE_ERROR` quando a publicação posterior falha.
4. **A guarda `NENHUM_RENDER_DETECTADO` é um defeito observável, mas não explica sozinha todos os reinícios.** Nos cinco logs arquivados de novos `dwm.exe` (`4788`, `7804`, `3316`, `10796`, `2492`), cada processo registra uma recusa desse tipo, **e depois registra três** `DXGI Present LEAVE cb: hr=0x00000000`. Exemplo: `bc250_umd_log_4788.txt:1886-1887` e `:4835-4837`. Assim, é incorreto dizer que *cada* DWM morre imediatamente no primeiro quadro apenas por essa recusa. O passthrough DX.81 pode melhorar a inicialização, mas precisa provar pixels novos e não apenas `Present` aceito.
5. **O pacote DX.81 altera três variáveis de uma vez.** `HardwareAccess=0` e contexto de present no escape, passthrough de quadro sem render e limites GPUVA 4096. Se a Corrida 128 melhorar, a causalidade de cada mudança ficará indeterminada sem telemetria discriminante ou ensaios separados. O candidato ainda não foi validado em hardware.

## Hipóteses, por prioridade de investigação

| Prioridade | Hipótese | Estado | Evidência que a distingue |
|---|---|---|---|
| P0 | Uma submissão anterior deixou a SDMA sem progresso; o escape chegou atrás dela e sua cópia também expirou. | **Fortemente sustentada como mecanismo imediato**, causa interna ainda aberta. | Correlacionar último `IB` aceito, WPTR/RPTR normalizados, fence física, VMID, endereço e estado do ring antes do primeiro timeout; capturar snapshot **antes do reset**. |
| P0 | Falhas de publicação GPUVA geraram mapeamento ausente/incoerente para trabalho em voo. | **Plausível, não provada.** Há falhas anteriores ao primeiro TDR, mas o escape resolveu suas duas GPUVAs. | Correlacionar `PUBLICACAO_FALHA` com o contexto, VA e fence exatos do IB que ficou pendente, não apenas com o mesmo bind. |
| P1 | Há uma falha separada no caminho GFX ou na sincronização GFX↔SDMA após o primeiro reset. | **Aberta.** O segundo TDR mostra GFX não responsivo; muitos eventos seguintes mostram ring GFX não convergindo. | Classificar os 23 TDRs por engine/fence/IB e estabelecer o primeiro evento de cada classe. |
| P1 | A recusa de quadro inicial dificulta a recuperação do DWM. | **Observada, causalidade parcial.** Há Presents aceitos posteriormente nos mesmos processos. | Medir vida de cada PID, retornos de Present, `DxgkDdiPresent`, pixel novo e momento de saída do processo. |
| P2 | `HardwareAccess=1`, `hContext=NULL` cria uma espera adicional no escape. | **Possível efeito secundário, não causa exclusiva demonstrada.** A tese “impediu entrada no KMD” foi refutada. | Medir tempo UMD↔KMD com timestamps/QPC nos dois lados após corrigir ou instrumentar o timeout SDMA. |

## Critério recomendado para a Corrida 128

O critério A de `docs/456` já exige zero TDR, mas sua justificativa causal precisa ser corrigida. **Não declarar “stall eliminado” apenas porque o bridge deixou de retornar `E_FAIL` ou o DWM deixou de reiniciar.** A interpretação física deve exigir, além de desktop íntegro e 60/60:

1. Zero `IB_TRIPOD TIMEOUT`, zero `Blt Timeout`, zero `McdmResetFromTimeout`; todas as cópias de apresentação relevantes com fence aposentada e pixels corretos no destino/scanout.
2. Contagem de `Journal NO_SPACE`, `PUBLICACAO_FALHA` e ocupação máxima por processo; zero falha de publicação para os mapeamentos usados por SDMA/GFX. Aumento para 4096 só conta como correção se não houver falha equivalente ou apenas adiamento da saturação.
3. Linha do tempo por processo para DWM, Edge/WebView e harness, com contexto, fence e evento de reset. Confirmar que o DWM compôs **quadros novos durante os 600 s**, não somente que o sistema respondeu ao watchdog.
4. Qualquer TDR ou corrupção visual mantém o desfecho parcial/falha, mesmo que o rollback termine em BasicDisplay Código 0. Se o teste for executado com as três mudanças DX.81 juntas, relatar o resultado funcional sem atribuir causalidade exclusiva a uma delas.

**Próximo trabalho offline recomendado:** reconciliar as 23 sequências TDR com as falhas de `ActiveFlushTlb` e com os IBs/fences anteriores; depois ajustar a instrumentação ou o candidato físico para testar a causa do primeiro timeout SDMA. Não há prova nova de hardware nesta auditoria.
