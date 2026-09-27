# DX.56 — rastro durável de paginação e critério da Corrida 101 (24/09/2026)

## Estado e motivo

A Corrida 100 travou ~6 min após o bind. A última linha do KMD no arquivo foi uma submissão SDMA de paginação, mas isso **não localiza a falha**: durante paginação e acima de PASSIVE_LEVEL o logger acumula linhas num anel de RAM de 128 KB. Esse anel se perde no reset. Não houve dump ou BugCheck. Ver [Corrida 100](402_CORRIDA100_DX55_RESULTADO.md) e [handoff 403](403_HANDOFF_2026-09-24_TRAVAMENTO_DURO_E_VELOCIDADE_DO_DESKTOP.md).

## Mudança offline

Uma thread própria do KMD grava, a cada 100 ms quando há avanço, os 16 estágios de paginação mais recentes em `C:\BC250_PAGING_TRACE.bin`. Cada estágio tem sequência, operação, status, identidade da thread e tempo de interrupção. O registro fixo inclui checksum; o decodificador rejeita escrita incompleta e reencontra o próximo registro válido. O arquivo é aberto com `FILE_WRITE_THROUGH` e cada registro completo recebe `ZwFlushBuffersFile`.

A DDI de paginação **não** faz I/O de disco, não aloca e não espera a thread. Ela só atualiza os slots em RAM já existentes. A thread nasce antes de `DxgkInitialize`; no unload, o KMD espera seu término real antes de descarregar o código. Falha de criação recusa o bind. O binder grava o offset inicial em `paging-trace-baseline.json`. Após reboot, `tools/recover-bc250-paging-trace.ps1 -EvidenceDir <pasta>` copia somente a cauda da corrida, calcula SHA-256 e chama `tools/decode-bc250-paging-trace.py`.

**Limite declarado:** o reset pode perder o intervalo desde o último flush confirmado, nominalmente até 100 ms, ou mais se a thread/armazenamento estiver atrasada. Um estágio marcado como último não prova que sua função causou o travamento: a thread pode ter parado por causa externa ou outra DDI/processador pode ter travado. A persistência efetiva do arquivo ainda é **NÃO VALIDADA EM HARDWARE**. Não há escrita em DCN nem mudança no caminho de renderização, SDMA ou UMD.

## Critério escrito antes da Corrida 101

**Candidato preparado:** lab `3.262.1.0`, SYS SHA-256 `2C350D18A69EB17EACFE9AFB3C2E4171E5C5B0EEA0F4C38198347ED28939F5AC`, UMD SHA-256 `51288052EA9B12670702D4264C409F3D24025354EE12275016802CDBF53DD6E`, RADV SHA-256 `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`. Pré-voo `tools/prepare-corrida101-dx56.ps1` passou offline. Wrapper `tools/EXECUTAR_CORRIDA101_DX56.cmd` está em CRLF. **Nenhum bind foi executado.**

**Variável comportamental única:** ativar o rastro DX.56; manter DX.55 (lote e dirty rects desligados, pool de descritores ligado). **Teto:** uma corrida física, janela de até 600 s, com boot frio antes do bind. Daniel executa. O candidato precisa ter DriverVer novo, SYS/UMD/RADV identificados por hash e build/auditorias PASS. A abertura do arquivo pelo KMD é verificada no bind; falha recusa a instalação ativa. A pasta de evidência será `docs/evidence/<data>-...-lab-binding/` gerada pelo binder.

| Desfecho | Assinatura observável | Implicação para a investigação |
|---|---|---|
| A — travamento com rastro recente | Travamento duro; após reboot, cópia com hash e ao menos um registro de checksum válido datado perto do último log da UMD/KMD, com `active_count` e sequência por thread. | Comparar os estágios por thread com os outros sinais; selecionar uma hipótese discriminante para a Corrida 102. **Não atribuir causa apenas à última linha.** |
| B — travamento com rastro antigo/ausente | Travamento duro; arquivo não cresceu, ou último registro válido termina muito antes dos últimos logs, ou só há registro cortado. | Investigar atraso/bloqueio da thread ou falta de persistência do armazenamento; a DX.56 não cumpriu a função diagnóstica. |
| C — janela completa sem travamento | 600 s, retorno do binder e rastro válido com sequências crescentes; recuperação online conferida. | Observação de estabilidade nesta janela. Não declarar o travamento resolvido por instrumentação; repetir ou ampliar somente após analisar a carga. |
| D — bind recusado | `DX56_PAGING_TRACE START FALHA` e nenhum StartDevice do candidato. | Corrigir criação/acesso ao arquivo ou thread antes de qualquer nova corrida. |
| E — algo fora do mapa | Qualquer combinação diferente das assinaturas acima, descrita com horários e arquivos antes do julgamento. | Definir o próximo experimento a partir do observado; desfecho parcial não é desfecho. |

**Controle necessário:** confirmar que o desktop e o caminho D3D11/SDMA não regrediram, que o rastro não induziu volume de disco ou latência inesperada, e que rollback/boot fuse funcionaram. A Corrida 100 não executou o harness por defeito de CRLF no wrapper; verificar 60/60 quando a janela permitir. Após a corrida, relatar avanço, sucessos, falhas, veredito, recuperação e pendências conforme a diretriz do projeto; atualizar `BASE_DE_AVANCO.md` somente com medição nova.
