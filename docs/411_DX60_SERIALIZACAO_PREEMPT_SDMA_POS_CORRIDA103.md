# DX.60 — serialização da preempção SDMA após a Corrida 103 (offline)

## Evidência e limite da conclusão

A [Corrida 103](410_CORRIDA103_DX59_RESULTADO_E_0x119.md) terminou às 13:55:40,666 de 24/09/2026 em `0x119 Arg1=1`. No dump, nosso callback `DMA_COMPLETED` notificava a fence 3071 do nó 0; a marca local ainda era 3070. O scheduler tinha 3072 nos campos `[rdi+0x40]` e `[rdi+0x48]`, índice 1 e ponteiro nulo nessa entrada da tabela. A divergência é medida. Nenhum registro `PREEMPT_DONE Node=0` foi recuperado; **preempção como causa da Corrida 103 permanece hipótese**.

## Corrida real no código

`Bc250SdmaPresentPollRetirement` remove uma submissão concluída da fila sob `RingLock`. Depois, `Bc250ActiveSdmaRetireAndNotify` envia `DMA_COMPLETED` em lote. A guarda `SdmaRetireBusy` cobre esse intervalo inteiro. Em paralelo, `Bc250ActiveTryCompletePreemptions` podia chegar ao callback de `DMA_PREEMPTED` do nó 0, ler `InFlightCount=0` e notificar preempção **antes** de a fence recém-retirada chegar ao scheduler. O caminho genérico tinha `RetireBusy`, mas não compartilhava `SdmaRetireBusy` com o retirement SDMA.

Essa ordem permitiria que o scheduler avançasse e, em seguida, recebesse uma fence mais antiga. É compatível com o dump, sem atribuição causal comprovada: o estado do scheduler também pode ter avançado por outro evento ainda não observado.

## Correção offline

No nó 0, `Bc250ActiveTryCompletePreemptions` agora adquire `SdmaRetireBusy` por CAS **antes** de consultar a fila e o estado de fence; libera somente depois da tentativa de `DxgkCbSynchronizeExecution` e da atualização de pendência. Se a guarda estiver ocupada, adia a preempção para o próximo tick do timer periódico, sem spin em DPC. Os demais nós mantêm o fluxo anterior. O teste estático `audit_dx47_preemption_completes` passou a exigir aquisição antes do callback e liberação depois.

Essa mudança fecha a janela **entre retirar um slot SDMA e notificar sua fence**. Ainda não demonstra ausência de outras causas para `0x119`, nem resolve a recusa `E_INVALIDARG` da textura compartilhada após o KMD retornar sucesso. A alteração é apenas offline; **NÃO VALIDADA EM HARDWARE**. Não instalar o pacote da Corrida 103 novamente. Próxima corrida precisa de candidato novo, boot limpo e critério que inclua ausência de BugCheck/TDR, sequência de fences dos dois lados, pixels e rollback no mesmo boot.

Verificação offline em 24/09/2026: `python tools/audit-bc250mcdm-safety.py` **PASS**; `driver/wdk/build-contract.bat` **EXIT=0** (compilou o KMD e executou as suítes de contrato). Essas verificações não provam comportamento do scheduler nem estabilidade do desktop.

## Candidato e próximo critério

O pacote histórico 3.264.1.0 da Corrida 103 foi preservado em `docs/evidence/2026-09-24_13-54-18-DESKTOP-HQAG6T4-lab-binding/candidate-3.264.1.0/` antes de substituí-lo. O candidato **3.265.1.0** foi construído, assinado e auditado offline, sem instalar ou vincular: SYS SHA-256 `942084B778CDCDE61871962E0F5A155B0206A5B183661AA71AE5DCF1C30B9163`; PDB `902459A9A92BEDB56F831FC5FCBD605CE9DBCC9434CC68567F6493DFFB8D1464`; UMD `0FC62149B358D9ACEFDBBF7D84395E63B76A8D2F47B34EB691F9A41505E1D1B2`; INF `CEE3911A100BF0B1E531F73DC7D2F51DCA4627AA452D3A1824081C2C5CFEE39B`; CAT `09B2D43577283DBE67D056A945B155E38AAC7B9899AC4C714823BF245F7DB93D`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, `GfxWaveProbe=1`; UMD e RADV mantidas da 103. `tools/audit-bc250mcdm-lab-package.ps1` e `tools/prepare-corrida104-dx60.ps1` passaram. Pacote ainda **NÃO VALIDADO EM HARDWARE**.

A Corrida 104 foi preparada em `tools/EXECUTAR_CORRIDA104_DX60.cmd`, com pré-voo de hashes e watchdog de 180 s. Só iniciar após boot frio que satisfaça o marcador `CLEAN_BOOT_REQUIRED`. Resultado mínimo: sem BugCheck/TDR e BasicDisplay Código 0 por rollback **no mesmo boot**; medir também `PREEMPT_WAIT Node=0`, `PREEMPT_DONE Node=0`, sequência de `DMA_COMPLETED`, `DX59_ALLOC_QUERY`, pixels e saída do harness. Se a guarda não for exercitada, ausência de BugCheck por 180 s é apenas triagem de estabilidade, não prova causal da correção. A recusa `E_INVALIDARG` pode persistir porque DX.60 não altera alocação.
