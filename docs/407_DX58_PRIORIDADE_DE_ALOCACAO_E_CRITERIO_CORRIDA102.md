# DX.58 — prioridade inicial de alocação e critério da Corrida 102 (24/09/2026)

## Fato medido e hipótese

Na [Corrida 101](405_CORRIDA101_DX56_RESULTADO.md), o Explorer registrou **201** recusas `pfnAllocateCb` com `hr=0x80070057` ao criar a textura compartilhada BGRA 1024×832, 3.407.872 bytes, `Bind=0xA8` e `Misc=0x2`. A contagem vem das linhas `CreateResource RECUSADO desc: Dim=3 1024x832` no original `BC250_UMD_LOG_2532.txt` de `umd-logs-pos-travamento/`, SHA-256 `FCFEBD444E9E68B2EABCD8AE57A79BD31FDBFE0312C68CA15C1AF5731B841C78`. Em corrida anterior, o KMD retornou sucesso para o token correspondente e o runtime recusou depois; ver [revisão 396](396_REVISAO_DAS_AUDITORIAS_EXTERNAS_CHATGPT_GEMINI.md). A rotatividade de contextos e o travamento duro persistem. Não há prova de que essa recusa cause o travamento.

No `Bc250ActiveCreateAllocationLifecycle`, o KMD preenchia tamanho, segmentos, flags e handle, mas deixava `DXGK_ALLOCATIONINFO.AllocationPriority` sem atribuição. O [contrato oficial de `DXGK_ALLOCATIONINFO`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo) define o campo como **saída** do KMD, diz que prioridade inicial zero é inválida e recomenda `D3DDDI_ALLOCATIONPRIORITY_NORMAL` quando o driver não diferencia prioridades. O header WDK 10.0.28000.0 também marca esse campo `out`. Portanto, deixar zero viola o contrato. A conexão entre essa violação e `E_INVALIDARG` do Explorer é **inferência a testar**, pois outras alocações foram aceitas pelo mesmo caminho.

## Correção offline

`driver/wdk/bc250_active_miniport.c` agora escreve `info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL` antes de entregar cada alocação ao VidMm. É a única mudança de comportamento do KMD frente à Corrida 101. UMD e RADV permanecem byte a byte idênticas. DX.56 permanece para rastro durável; DX.57 apenas identifica a cópia intermediária dos logs UMD e arquiva a cauda após recovery. Não há novo tunable, escrita DCN ou alteração do agendador/SDMA/GFX.

A auditoria `audit_dx58_allocation_priority` exige a atribuição antes da publicação do handle; `test-dx58-allocation-priority-mutation.py` rejeita a remoção. Build completo do KMD, contrato, auditoria de segurança e auditoria do pacote assinado passaram offline. Pacote lab `3.263.1.0`: SYS assinado SHA-256 `FE58E378B971293F50C59672024AE169705621608202EAB8550B44895A57C753`; UMD `51288052EA9B12670702D4264C409F3D24025354EE12275016802CDBF53DD6E`; RADV `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C`; INF `92FB3FF77BC4E4F94618E68583B493D20C1EBD2CA2A56906BCC403843CA0C828`; CAT `2E22B48E74BE5B45495D88C21FE5237140E6C75CFC157ED55A0231EC28CDD7C3`.

## Critério escrito antes da Corrida 102

**Teto:** uma corrida física de até 600 s após boot frio, executada pelo Daniel. `tools/prepare-corrida102-dx58.ps1` confere hashes, manifesto e auditorias antes do bind; `tools/EXECUTAR_CORRIDA102_DX58.cmd` usa o guardião de 600 s. O `CLEAN_BOOT_REQUIRED` da 101 deve ser satisfeito pelo desligamento completo antes da tentativa. A pasta de evidência será criada pelo binder. Não interpretar ausência de tentativa de alocação como correção.

| Desfecho | Assinatura observável | Veredito restrito |
|---|---|---|
| A — contrato e função melhoram | Ao menos uma criação da textura compartilhada 1024×832 chega a `Allocate LEAVE: hr=0x00000000`; nenhuma recusa `0x80070057` dessa classe; 600 s sem travamento, harness 60/60, guardião recupera BasicDisplay Código 0 no mesmo boot. | Evidência a favor da hipótese sobre o laço e estabilidade nesta janela; ainda exigir pixel no monitor, desempenho e repetição para declarar desktop pronto. |
| B — recusa persiste | A textura 1024×832 é tentada e ao menos uma recusa `0x80070057` permanece, com ou sem travamento. | Prioridade não é correção suficiente dessa recusa; comparar saída KMD, flags e validação do runtime antes de outra mudança. |
| C — recusa some, travamento persiste | A textura antes recusada passa ao menos uma vez, mas ocorre travamento duro/reboot necessário antes de 600 s. | Avanço local no contrato da alocação; causa do travamento permanece aberta. Recuperar rastro DX.56 e cauda DX.57. |
| D — sem tentativa observável | Não há tentativa da textura nos logs disponíveis, mesmo que a janela conclua. | Efeito sobre a recusa é inconclusivo; registrar carga efetiva e limites dos logs. |
| E — falha diferente | Bind recusado, TDR, BugCheck, device-lost, problema do guardião ou outra assinatura. | Relatar hora, candidato e evidência específica; não converter saída do script em sucesso gráfico. |

Comparar também número de contextos por minuto, Presents DWM/min, tempo do harness e resposta visual do desktop com as Corridas 100/101. A 101 teve ~2.215 criações de contexto antes do reboot e 201 recusas dessa textura no Explorer; não usar apenas `Exit 0` ou Código 0 como sucesso. Se o sistema travar, o watchdog de usuário pode ficar impedido de executar; a DX.57 só copiará os logs originais quando a recovery voltar a rodar. Após a corrida, declarar avanço, sucessos, falhas, veredito, recuperação e pendências conforme `AGENTS.md`. **A DX.58 e a DX.57 ainda NÃO FORAM VALIDADAS EM HARDWARE. Nenhum bind foi executado na preparação.**
