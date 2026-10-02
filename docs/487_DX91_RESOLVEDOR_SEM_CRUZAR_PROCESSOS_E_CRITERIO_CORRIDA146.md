# 487 — DX.91: o resolvedor de GPUVA da composição cruzava processos; correção fail-closed, log das cópias de paginação e critério da Corrida 146

> 27/09/2026, offline. Origem: a corrupção visual da Corrida 145 (doc 486).

## 1. Revisão do caminho de despejo e restauração (leitura de código)
| Ponto | Veredito |
|---|---|
| `VIRTUAL_TRANSFER` (`Bc250McdmBuildTransferVirtual`) | Correto pelo contrato (P1). As duas VAs são do processo de paginação; `SourcePageTable`/`DestinationPageTable` só valem com a capacidade legada `SourcePageTableVaInTransfer`, que não declaramos. A cópia é linear, em pedaços de 256 KiB, com multipass. |
| `VIRTUAL_FILL` | Correto (P1). |
| `UPDATE_PAGE_TABLE` escrito pela CPU | Correto: declaramos `DXGK_PAGETABLEUPDATE_CPU_VIRTUAL` (`bc250_mcdm_callbacks.c:2698`, log `Mode=0`), e nesse modo o VidMm sincroniza com as cópias pendentes. |
| Codificação de PTE de páginas de sistema (segmentos 0/2) | Endereço físico com SYSTEM\|SNOOPED; sem defeito evidente. |
| **`Bc250ActiveResolveDisplayGpuVa`, a varredura de fallback** | **DEFEITO (P1 pelo código, P0 pelo log).** Detalhe abaixo. |

**O defeito.**
- Quando a resolução pelo dono falhava, o fallback aceitava a primeira faixa mapeada que contivesse a VA, **de qualquer processo**. Cada processo tem seu próprio espaço de GPUVA, e a mesma VA existe em vários ao mesmo tempo.
- Os dois escapes (`Bc250ActiveEscapeResolveGpuVaToMc` e a origem do blt) ainda tentavam uma terceira vez com `NULL`, "qualquer dono".
- **Medido na 145:** 120 resoluções estritas falharam com `status=10` (dono diferente), e a varredura devolveu, nas 120, faixas de **outros processos**. Exemplo: o harness pediu `0x240440000` e `0x2407D0000` e recebeu faixas de `…0037` e `…001E`.
- **Contraste:** zero ocorrências em 136, 138, 141 e 142, que não tiveram corrupção; 2 na 143 e 4 na 144, que travaram; zero na 139/140.
- A causa de fundo é conhecida: com o journal no teto, a faixa do próprio processo não entra no snapshot, e o resolvedor ia buscar a de outro.

## 2. O que mudou
1. **A varredura só aceita faixa do mesmo dono.** Sem dono, aceita só se houver uma **única** candidata. Resolução ambígua sai como `[ResolveGpuVa] RANGE_SCAN RECUSADO ... de_outro_dono=N` (isenta de amostragem). Perder um quadro é correto; copiar memória de outro processo, não.
2. **Os escapes** só resolvem sem dono quando não trazem nem contexto nem device.
3. **`[DX91_PAGING_MOVE]`**, uma linha por passo, isenta de amostragem:
   - TRANSFER: hAlloc, offset, tamanho, passo, origem, destino, direção e flags;
   - FILL: destino e padrão;
   - DISCARD: segmento e endereço.
4. **Portão `audit_dx91_gpuva_resolve_never_crosses_process`**, com **cinco mutações e cinco reprovações**: filtro de dono removido, candidata única relaxada, `NULL` sem guarda no escape, linha TRANSFER removida, isenção removida.
5. `build-contract` EXIT 0, build de laboratório EXIT 0, auditoria de segurança PASS, auditoria de pacote PASS.

| Artefato (3.297.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `C316673B4CABFE9FBE9A1DDF35CB57A9DB76390DE87215BC5A56DC430FEEE635` |
| `bc250mcdm.sys` fonte lab | `D928030CD19BCAB476E676C86E42ECB3E9CA227FC9D2DFB1F18B26060E1D9B9C` |
| `bc250umd.dll` (inalterada) | `3EB5806557EF3DD9E41D5FD45CD5737D8A2D6FFC06038824946BF5C327F9CFB8` |
| `bc250mcdm.inf` | `47AF1FF5AF6333015395BAC462DE4A974F7A048CAC9B6FD10C99A754D6BE1542` |
| `bc250mcdm.cat` | `2B9C903C541984702F26C942326910E9EAF41D2B630189ED6FB3D135B997A11A` |

O SYS assinado contém `RANGE_SCAN RECUSADO`, `DX91_PAGING_MOVE` e `BGH1`.

## 3. Critério da Corrida 146 (escrito antes)
**Runner:** `tools\EXECUTAR_CORRIDA146_DX91_600S.cmd`. Harness atrasado 150 s; KDNET permitido, como na 145; atalho de dump com qualquer Ctrl. Boot frio antes. Usar o desktop como na 145.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Sem corrupção visual durante o harness **e** pelo menos um `RANGE_SCAN RECUSADO` com `de_outro_dono>0` | A resolução cruzada causava a corrupção. Próximo: por que a faixa do dono some do snapshot (journal no teto). |
| **B** | Corrupção visual de novo, com zero `RANGE_SCAN SUCCESS` de outro dono | A causa é outra. As linhas `DX91_PAGING_MOVE` do instante dizem quais movimentos houve. |
| **C** | Trava | BGH1, rastro, a última `DX91_PAGING_MOVE` e se houve `RANGE_SCAN RECUSADO` antes dela. |
| **D** | Nem corrupção nem `RANGE_SCAN RECUSADO` | A condição não se formou; inconclusivo. |
| **E** | Algo fora do mapa, incluindo tela preta sustentada ou janelas sem conteúdo por quadros recusados | Descrever antes de julgar. |

Controle a não regredir: desktop visível, DWM apresentando, harness 60/60, paginação ENTER==LEAVE, zero TDR.

**NADA DA DX.91 FOI VALIDADO EM HARDWARE.**
