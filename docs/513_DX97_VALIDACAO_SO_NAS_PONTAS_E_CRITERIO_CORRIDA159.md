# 513 — DX.97: validação de SRC/DEST só nas pontas; critério da Corrida 159

> 28/09/2026, offline. Base: Corrida 158 (doc 511) e referências da AMD (doc 512).

## 1. Mudança (uma variável de comportamento)
- `Bc250ActiveValidateGpuVaRangeEnds`, nova: caminha só a **primeira** e a **última** página da faixa. `FirstResolvedPhysical` continua vindo da primeira página, que alimenta os canários do tripé e o DIRECT_FILL.
- A validação de SRC e DEST no submit do Node 0 passa a usá-la. O IB do anel (Node 0 e Node 1) continua caminhado por inteiro.
- Log novo: `[PT_RANGE SRC|DEST] PASS_PONTAS: VA=[..) Pages=<total> Caminhadas=<1|2>`.
- **Decisão revertida, com o motivo registrado:** a C3.16 (12/09) tinha trocado "primeira/meio/última" por "todas" como precaução depois de uma submissão de 750 páginas. Na 158 isso custou **~7.900 páginas/s** de leitura de tabelas pela BAR (1.013.696 em 128 s, 99% em SRC e DEST), e estava em curso no instante da trava. Página inválida no miolo cai na página dummy e é observável no `GFX_FAULT_SENTINELA`.

## 2. Provas offline
- Portão novo `audit_dx97_range_walk_only_endpoints`; o portão antigo da C3.16 foi atualizado com o motivo. **Três mutações, três reprovações:** DEST de volta a todas as páginas, SRC de volta a todas, e o IB passando a ser só nas pontas.
- `build-contract` EXIT 0, com "TODOS OS TESTES APROVADOS (0 falhas)"; matriz DDI, segurança e no-op PASS; build de laboratório EXIT 0; auditoria do pacote PASS.
- Conferido por string no SYS assinado: `PASS_PONTAS`, `[DX96_HDP]` e `[FPS3B]`.

| Artefato (3.308.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `3CB6FAC7F95D1AFAAD74486597CB9C5AECB2F4A3CDE688CA8FEBA3F0E6E7089B` |
| `bc250mcdm.sys` fonte lab | `868C91C785EFD3BDD95D76B63FA89D453D4AE1DE70F47B2C433B67C970809DFA` |
| `bc250umd.dll` (igual à 149–158) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `83D049E3CAEEB0270C2EBBACF4B3A653963C34BE6F4D172E2FA7E0E2799979C7` |
| `bc250mcdm.cat` | `5B20E0F326BB6E68C9E60862EB50D3BA2B78FA40F3F29735084BAE1C71E0B41C` |

## 3. Critério da Corrida 159 (escrito antes)
- **Runner:** `tools\EXECUTAR_CORRIDA159_DX97_600S.cmd`, com o harness aos 30 s e reset em 10–15 s se travar.
- **Linha de base:** a 158 (trava aos ~2 min 13 s; ~7.900 páginas/s; quadro de 454 ms).

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | `PASS_PONTAS` presente; 600 s sem trava; zero `GFX_FAULT_SENTINELA`, `IB_TRIPOD TIMEOUT` e TDR; harness 60/60 | A leitura concorrente das tabelas pela BAR é o gatilho mais forte. Repetir: duas corridas limpas seguidas dão nome à trava. |
| **B** | Trava como antes | A leitura não era o gatilho. Próximo: a DX.98 (tabelas escritas pela GPU, modelo da AMD). |
| **C** | Sem trava, mas com `GFX_FAULT_SENTINELA` ou dados errados | O miolo das faixas tinha páginas inválidas que a validação completa pegava. Nomear quais antes de seguir. |
| **D** | Outra regressão | Reverter a DX.97 e isolar. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observação que não entra nos desfechos: o quadro do harness (`tools/analyze-fps3-retire.py`), para ver quanto a validação completa custava no submit.

**NADA DA DX.97 FOI VALIDADO EM HARDWARE.**
