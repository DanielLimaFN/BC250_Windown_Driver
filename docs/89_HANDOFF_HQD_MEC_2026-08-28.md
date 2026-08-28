# Handoff técnico — BC-250 MEC/HQD

## Objetivo e regra de verdade

Este repositório desenvolve um miniport MCDM (`bc250mcdm.sys`) para a AMD BC-250 (`1002:13FE`, GC 10.1.3/Cyan Skillfish 2). O desktop deve permanecer protegido: o miniport deliberadamente fica em **Code 43** e o ensaio ativo é fail-closed.

Não declare compute funcional sem o tripé físico no silício: `RPTR` consumindo o `WPTR`, fence escrita pela GPU e resultado de dados correto. Nenhuma fence, scratch, RPTR ou resultado pode ser simulado pela CPU. O estado atual é: **NÃO VALIDADO EM HARDWARE** para compute MEC/HQD.

## O que já foi efetivamente provado

| Área | Evidência física | Estado |
|---|---|---|
| CP Ring 0 (PFP/ME/CE) | Ring PM4 `RPTR/WPTR=0→15/16`, fence `0x43DAFFFE→0xBC250123`; PCs PFP/ME/CE ativos | Provado |
| SDMA0 | Ring consumido, fence física, cópia de 4 DWORDs com 0 mismatch | Provado |
| TLB/GCVM | Invalidate/ACK, VMID 8 e rollback físico | Provado para a transação medida |
| HQD MEC2/P0/Q0 | Banco configurável, scheduler RLC e doorbell/DPTR observáveis; sem RPTR/fence/result | Não provado |

O CP Ring 0 e SDMA0 não autorizam afirmar que a fila MEC/HQD execute. São engines independentes.

## Evidência HQD mais recente: 3.18.0.0

O último ensaio físico está em `docs/evidence/2026-08-28_09-07-20-DESKTOP-HQAG6T4-mcdm-replace`.

O pacote 3.18.0.0 / SchemaVersion 91 passou assinatura e integridade, mas a submissão HQD terminou em `status=6`, que é `BC250_HQD_ACTIVE_READBACK_FAILED`.

Fatos do log:

- `RLC_CP_SCHEDULERS=0x58504840→0x585048C0`, com rollback para `0x58504840` aprovado.
- A seleção dinâmica derivou corretamente ME2/P0/Q0: seletor GRBM `0x8`.
- A fila não chegou a ACTIVE/kick neste ensaio: `applied=0→0`, doorbell `0`, coerência `0` e MEC_PC sem amostras.
- O snapshot ativo observou `InitialWptr=0:0`.
- A fase de baseline escreveu `WPTR=0`, porém o readback obrigatório observou `16`, portanto o fluxo abortou seguro antes de ativar a fila.
- O rollback do baseline voltou a `0` em uma leitura (`restore=0/1 polls=1`).
- O snapshot independente posterior ainda mostra o banco com `RPTR/WPTR=0/16`; isso conflita com a leitura da fase ativa e exige investigação, não interpretação especulativa.

Conclusão correta: 3.18 **não** confirma que os writes são apenas “postados” ou atrasados. A hipótese surgiu no 3.17, mas foi enfraquecida pelo comportamento contraditório do 3.18. A próxima IA não deve partir dela como fato.

## Linha do tempo curta e decisões tomadas

1. **3.10–3.14: rota e seleção.** O scheduler físico do Windows é `0x58504840`; o byte baixo descreve ME2/P0/Q0. O driver preserva os outros bytes e aplica somente `ENABLE=0x80`, obtendo `0x585048C0` e seletor `0x8`. Isso substituiu tentativas estáticas ME1/P0 e ME2/P1.

   Decisão: derivar a fila do `RLC_CP_SCHEDULERS` real a cada boot, pois escolher um MEC/pipe fixo contradizia a evidência do hardware.

2. **3.15: WPTR direto como diagnóstico.** Quando o doorbell BAR2 não alterava o banco escolhido, foi escrito `CP_HQD_PQ_WPTR_LO=16` com snapshot/readback. Em um ensaio o MEC2 mudou de `0x6A2` para `0x1CFF` em 93 de 196 amostras; RPTR continuou 0, fence poison e scratch inalterados.

   Decisão: usar o WPTR direto apenas para separar problema de rota de doorbell de problema de scheduler/fetch. Nunca avançar RPTR/fence/scratch por software.

3. **3.16: observação física do banco.** Acrescentadas leituras periódicas e read-only do banco selecionado (`PQ_RPTR`, `PQ_WPTR_LO`, `HQ_STATUS0`, `HQD_ERROR`, `PQ_CONTROL`). O valor `HQ_STATUS0=0x00008000` foi registrado, mas não interpretado: o header público GC10.1 só define os bits 0, 4–5, 6 e 30 para esse registro.

   Decisão: registrar campo desconhecido como observação, sem inventar semântica.

4. **3.17: baseline WPTR.** Um ensaio anterior deixou `WPTR=16`; o 3.17 passou a zerar WPTR antes de `ACTIVE=1`, seguindo a ordem de `gfx_v10_0_kiq_init_register`, e depois restaurar o snapshot. O baseline e o kick foram vistos, mas os readbacks de rollback divergiram e o gate bloqueou.

   Decisão: separar a inicialização de uma fila reutilizada da publicação do trabalho, preservando o snapshot e mantendo rollback obrigatório.

5. **3.18: polling de rollback.** Foram adicionados polls de até 1000 leituras para os dois rollbacks de WPTR. O ensaio 3.18 abortou antes de atingir esse estágio porque o readback do baseline já era inconsistente (`esperado 0`, observado 16).

   Decisão: o polling é mantido como instrumento de observação, mas não resolve nem explica por si a inconsistência atual.

## Código relevante no estado atual

- `driver/core/bc250_compute_mqd_contract.c`
  - `Bc250ComputeRunActiveHqdSubmissionTransaction` começa próximo da linha 1148.
  - Seleção dinâmica do scheduler: aproximadamente linhas 1378–1418.
  - Snapshot inicial do WPTR: aproximadamente linhas 1438–1446.
  - Baseline WPTR 0: aproximadamente linhas 1518–1539.
  - Doorbell e diagnóstico: aproximadamente linhas 1660–1750.
  - Fallback de WPTR direto: aproximadamente linhas 1760–1790.
  - Poll de RPTR/MMIO/PC: aproximadamente linhas 1800–1850.
  - Teardown e polls de rollback: aproximadamente linhas 1870–1950.
- `driver/core/bc250_compute_mqd_contract.h`: enum de status e `BC250_HQD_ACTIVE_SUBMISSION_EVIDENCE`.
- `driver/wdk/bc250_active_miniport.c`: registro persistente da evidência.
- `driver/wdk/bc250_probe_telemetry.[ch]`: IDs e nomes das propriedades do Registro.
- `tools/audit-bc250mcdm-failclosed-binding.ps1`: gate que exige a telemetria e falha fechado.
- `docs/03_CATALOGO_DE_REGISTRADORES.md`: único catálogo permitido para MMIO.
- `docs/09_PONTO_DE_RETOMADA.md`: histórico, mas a entrada 3.18 precisa ser corrigida com o resultado físico acima.

## Próxima ação recomendada

Não incrementar a versão nem alterar a sequência de ativação sem primeiro resolver a contradição do WPTR.

Crie um ensaio **diagnóstico read-mostly e isolado** para o banco ME2/P0/Q0 que, usando sempre o mesmo seletor e sem ativar a fila, faça:

1. Captura dupla estável de `ACTIVE`, `PQ_RPTR`, `PQ_WPTR_LO`, `PQ_WPTR_HI`, `PQ_CONTROL`, `PQ_BASE_LO/HI`, `DOORBELL_CONTROL`.
2. Aplicação de uma única escrita de WPTR com valor conhecido somente se o banco estiver inequivocamente inativo e a configuração PQ já estiver válida.
3. Leitura em série do **mesmo banco selecionado**, com selector readback entre as amostras, registrando cada valor e o tempo/poll de observação.
4. Restauração e nova captura dupla estável antes de qualquer plan teardown.
5. Nenhum `ACTIVE=1`, scheduler enable, doorbell, HDP/L2/GCVM ou PM4 nesse diagnóstico.

O objetivo não é “fazer compute funcionar”; é responder de modo físico e repetível: o offset `0xC8FC` é gravável nesse banco e em qual condição/o que faz o valor lido alternar entre 0 e 16? Só depois disso retomar a submissão ativa.

Ao implementar, preserve a máquina de estados:

`[PREFLIGHT] → [SNAPSHOT] → [APPLY] → [READBACK] → [PROVA NO SILÍCIO] → [ROLLBACK]`

Se qualquer etapa for ambígua, retorne status distinto, registre valores crus e faça rollback. Não contorne o gate de auditoria e não altere o Code 43.

## Pacote e comando atuais

- Pacote: 3.18.0.0 / SchemaVersion 91.
- Source SYS SHA-256: `34EB3BA73B3DAD88BEEA359FCCC22D74848C05B08CB09F5B98BEF048A00D4C09`
- Signed SYS SHA-256: `39C60A8B37396C0B630367EB945C5D0E5068732C44574C5CF7A2079DD82736E3`
- Catalog SHA-256: `C7110B1AAF9EBF4F37B37AD419501CD7477F7522B89B637ED1784C621A95F46A`
- Último log: `docs/evidence/2026-08-28_09-07-20-DESKTOP-HQAG6T4-mcdm-replace`.
- Instalação/ensaio: `powershell -ExecutionPolicy Bypass -File tools\replace-bc250mcdm.ps1`

O worktree já estava muito modificado; não use reset/checkout destrutivo nem apague evidências.
