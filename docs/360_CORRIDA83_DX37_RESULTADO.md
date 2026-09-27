# Corrida 83 — DX.37 `CONTEXT_CONTROL`: resultado

> **23/09/2026, início 11:00:09, recuperação concluída 11:03:30 (BRT).** Candidato lab **3.244.1.0**, SYS SHA-256 `9D0FF839119FDDCA8B86FA47CE82966C0B334DDDC09E4FA0ECC369B8F9905ED9` conferido em `binding-provenance.json`. Critério prévio: [docs/359](359_DX37_CRITERIO_CORRIDA83_CONTEXT_CONTROL.md).
> Evidência: `docs/evidence/2026-09-23_11-00-09-DESKTOP-HQAG6T4-lab-binding/`. O `display-log-this-bind-FINAL.txt` foi capturado com `complete_at_capture=false`; a cauda foi resgatada em `bc250_display_log-tail-recovered.txt` (SHA-256 `94D1165A9B50D9E0A828CDA8E2CF1E6B821F174A964DB07683FD39148F0EE2B5`). O resgate vai até o fim do log vivo e pode conter atividade após o bind; os três TDRs e os dados abaixo estão no `FINAL` e também na cauda.

## 1. Houve avanço?

**Não funcional.** Daniel relatou “desktop todo zoado”; o harness aceitou **0/60** quadros, como na 82. Houve **3 TDRs**, igual à 82. O pacote novo não eliminou a classe de VMID anterior: dois TDRs reproduziram a assinatura. No terceiro, a onda VMID4 pode pertencer à fence 67 já escrita, enquanto as fences 68 e 69 ainda estavam em voo; não prova que a onda seja do IB atual. É aprendizado diagnóstico inconclusivo, não sucesso.

## 2. Sucessos medidos

- **ATIVO e anel vivo:** o dump da primeira troca contém `C0004200 00000000 C0012800 81018003 00000000 C0023F00`; `PM4DW=34`. O `FINAL` registra 75 trocas com `Eng=0`, e submissões/fences avançaram depois delas. Nenhum header de TDR ficou em `CONTEXT_CONTROL` (ME `EVENT_WRITE`, PFP `PFP_SYNC_ME`). Nenhum `SWITCH_BUFFER` reapareceu.
- **Recuperação de cada TDR:** 3/3 `TDR_GFX_RESTART_PROOF PASS` e 3/3 `TDR_RESTART PROVADO` do SDMA. O anel voltou a submeter após cada reset.
- **Controles:** `PREAMBLE_CNTL` ausente no log; nenhum BugCheck observado; `nodcc,nonggc` passou S1–S5 com contador 65536. `nodcc` e `nodcc,nongg` tiveram S1 PASS e etapas posteriores **NÃO MEDIDAS** por `MAP_FALHOU`, não FAIL.
- **Display no escopo medido:** um Present com `BYTE_PROOF PASS` e 256/256 amostras não nulas; duas provas de retângulos sujos, 25/25 e 5/5. Dezesseis `MODECHANGE_BLANK_SKIP` preservaram o scanout amostrado. Isto não prova desktop íntegro ou interativo.

## 3. Falhas e classificação dos TDRs

| TDR | Carimbo de fence atual / IB1 no snapshot | VMID carimbo atual / anterior | VMID das 4 ondas lidas | Fault e classificação |
|---|---|---|---|---|
| 1 | 22 / `0x2400E0000` | 4 / 1 | **1** | `Cid=9`, execução, VA `0x200040000`, página do PC `0x200040C00`. **C**: VMID anterior persiste. |
| 2 | 33 / `0x2400E0000` | 2 / 3 | **3** | `Cid=9`, execução, VA `0x200040000`, página do PC `0x200040800`. **C**: VMID anterior persiste. |
| 3 | 69 / `IB1=0x240030000` (IB da fence 68; o 69 é `0x24024C000`) | 4 / 3 | **4** | A onda coincide com o VMID da fence 69 **e também da 67**. A fence física observada era `0x43` = **67**; 68 e 69 continuavam em voo. O `IB1` aponta para a 68 e a `GCVM_FAULT_TIMELINE` capturou `Vmid=4 Cid=9` em `0x2003C0000`, página do PC `0x2003C0B00`, **antes** da submissão da 69. A auditoria posterior do dump com VA/tamanho do IB68 achou `COMPUTE_PGM_LO=0x2003C0B00` e `DISPATCH_DIRECT`: associação forte da onda com a 68, embora o dump não contenha fence/contexto imutáveis ([docs/362](362_AUDITORIA_OFFLINE_IB_CONTEXTOS_FENCES_CORRIDA83.md)). Não atribuir a onda/falta ao IB 69. Lacuna **E** quanto ao fault próprio do 69 e **G** pela ambiguidade temporal residual. |

Nos três TDRs, `SQ_DEBUG_STS_GLOBAL` indicou 240 ondas e as ondas lidas tinham `STATUS=0x00818000` ou `0x00810000` (`FATAL_HALT`/`XNACK_ERROR`, conforme decodificação já estabelecida). Não houve assinatura D de travamento no pacote novo. A classificação global é **C**, porque basta um TDR com VMID anterior e houve dois. Desfecho parcial não é desfecho.

Falhas adicionais: o desktop estava visualmente corrompido segundo Daniel; harness **0/60**, resultado final Exit 4. O **Present #5 falhou na prova de bytes** (`FirstMismatch=0x2C5001`, 11.059.200 bytes), foi recusado com `0xC01E0200` e não contou como apresentação correta. O Present #10 teve a prova positiva 256/256 citada acima; esse sucesso pontual não apaga a falha do #5. A `GCVM_FAULT_TIMELINE` também registrou escritas `Cid=0` sem permissão em `0x2401A2000` e `0x2401A3000`, recorrência da frente independente do render target somente leitura. Quatro `REJECT source contract` aparecem no `FINAL`; não atribuir a eles a aparência da tela sem correlação adicional.

## 4. Veredito frente ao critério

**Falha funcional e desfecho C da hipótese DX.37.** `CONTEXT_CONTROL` foi emitido e atravessado pelo anel, mas não bastou para impedir a execução de ondas com o VMID anterior. O terceiro TDR não salva a hipótese: a condição global B exige que **todos** os TDRs coerentes tenham VMID atual, e a onda VMID4 pode pertencer à fence 67, anterior à 69; o `IB1` ainda estava na 68. Controles de autoteste ficaram parcialmente não medidos; não foram contados como FAIL. Sem 60 quadros nem desktop íntegro.

## 5. Recuperação e pendências

O watchdog tentou rollback às 11:02:09, não concluiu online e pediu **reboot automático** (`RECOVERY_REBOOT_REQUESTED`, Exit 5). No boot seguinte, a tentativa 2 concluiu `RECOVERY_OK_ONLINE` às 11:03:30; cinco amostras PnP confirmaram **BasicDisplay Código 0**. `bc250mcdm` foi removido no rollback. O marcador de boot frio foi gravado. BasicDisplay é recuperação, não sucesso do DWM deste driver.

**NÃO VALIDADO EM HARDWARE:** eliminação do TDR, desktop íntegro e interativo, 60 quadros, causa do VA do render target somente leitura. Próximo passo: investigar offline por que `CONTEXT_CONTROL` atravessa o CP mas duas ondas ainda retêm o VMID anterior, incluindo a diferença entre o gatilho do Linux (`current_ctx`) e nosso gatilho (`vmidSwitch`) e o estado de CE/preâmbulo. Não repetir esta corrida nem introduzir outro pacote sem novo discriminante e critério prévio. Se houver outra corrida, exigir novo boot frio.

## 6. Registro da tentativa e limite da conclusão

**DX.37, alteração offline antes da Corrida 83:** o KMD passou a emitir `PFP_SYNC_ME` seguido de `CONTEXT_CONTROL` (`C0012800 81018003 00000000`) antes do IB quando detecta troca de VMID. O PM4 cresceu de 31 para 34 DW. A intenção, baseada no caminho GFX10 do Linux citado em [docs/359](359_DX37_CRITERIO_CORRIDA83_CONTEXT_CONTROL.md), era carregar os registradores de shader do contexto novo antes do dispatch. Build, testes, auditorias e mutações passaram offline; isto só atestou a construção do candidato.

**Resultado físico:** a Corrida 83 confirmou a presença e passagem do pacote no anel, mas reproduziu a falha essencial em duas das três capturas de TDR: ondas ainda eram observáveis no VMID do IB anterior. A origem temporal dessas ondas não foi medida. Portanto, a hipótese operacional de que este `CONTEXT_CONTROL` isolado resolveria a troca de VMID foi **refutada no cenário medido**. O dump associado ao IB68 torna provável que a terceira captura pertença à mesma classe, mas não fecha essa atribuição sem identidade imutável de dump/fence/contexto. A corrupção visual relatada, 0/60 quadros e a recuperação via BasicDisplay impedem qualquer anúncio de desktop funcional. Esta conclusão não identifica ainda se faltam preâmbulo/CE, outra transição do CP, sincronização ou uma diferença no critério de troca; essas possibilidades exigem comparação de código e prova discriminante, não mais uma tentativa às cegas.
