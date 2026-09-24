# DX.53 — VMID desarmado antes de voltar ao pool; critério da Corrida 98 revisada

> 24/09/2026. Faixa **offline**, sem bind.
> **Base:** [doc 396](396_REVISAO_DAS_AUDITORIAS_EXTERNAS_CHATGPT_GEMINI.md),
> achado da auditoria externa (ChatGPT) conferido no código, e
> [Corrida 97](394_CORRIDA97_RESULTADO_E_DX52_CRITERIO_CORRIDA98.md).
> **Substitui** a Corrida 98 do doc 394 (que era só a DX.52).
> **Teto:** uma corrida, depois de desligamento completo.

## 1. Pergunta

**Pergunta:** o travamento duro das 94–97 vem da **janela de reuso do VMID**?

**A janela (P0 de leitura do código):**
1. o `DestroyContext` devolvia o VMID ao pool;
2. soltava o lock;
3. só depois desarmava o contexto GCVM no silício.

Nesse meio, o `SubmitCommandVirtual` GFX de outro contexto podia adquirir o mesmo
VMID, e o desarme atrasado desligava o dono novo.

**Carga que abre a janela:** o laço do Explorer, ~290 contextos/min.

## 2. A mudança

**Variável comportamental (uma):** `bc250_active_miniport.c`, bloco de devolução
do VMID em `DestroyContext`.

**Ordem nova:**
1. `Bc250VmidPoolLookup` sob `VmidPoolLock`, para saber o VMID do contexto;
2. se o slot estiver `INSTALLING`: **retido**, com log (antes era silencioso);
3. `Bc250GcvmUninstallVmid(alvo)` fora do lock, **com o slot ainda pertencendo
   ao contexto morto**. O `Acquire` pula slot com dono, então ninguém o recebe
   durante o desarme;
4. desarme OK (readback e flush): `Bc250VmidPoolRelease` → `[VMID_POOL]
   LIBERADO … DEPOIS do desarme`;
5. desarme falho: **QUARENTENA**. O slot fica fora de circulação até o
   StopDevice, com `[VMID_POOL] QUARENTENA: VMID%02u NAO devolvido`. Antes, o
   slot voltava ao pool armado.

**Só medição:** antes de cada desarme, `[DX53_VMID] DESARME ANTES DA DEVOLUCAO:
VMID Ctx Medido PendentesNoRastreador Reservadas`. É uma leitura do rastreador
de fences **sincronizada com a ISR** (`DxgkCbSynchronizeExecution`, operação
PENDING). Mede a outra janela da auditoria (trabalho em voo no desarme) e não
muda a decisão.

**Vai junto, sem comportamento:**
- a DX.52 (recusas de CreateAllocation nomeadas; UMD descreve o recurso
  recusado);
- a UMD é a mesma do pacote DX.52.

**Configuração igual à 97:**
- lote desligado;
- dirty rects desligados;
- staging persistente ligado;
- trace durável.

**O laço do Explorer fica, de propósito:** é a carga que abre a janela.

## 3. Verificação offline

- **Build:** build lab EXIT=0 (inclui `build-contract`).
- **`active_miniport_test`:** reescrito para a regra nova. Offline não há MMIO;
  o desarme falha e o slot tem de ficar **em quarentena**. O teste confere que
  outro contexto recebe **outro** VMID. Antes, o teste exigia a devolução sem
  desarme e travava a ordem antiga.
- **Portões antigos do pool:** apontados para a ordem nova, sem afrouxar.
- **Portão novo `audit_dx53_vmid_disarm_before_release`:** ordem Lookup →
  Uninstall → (OK) Release / (falha) quarentena; nenhum Release antes do
  desarme; um único ponto de devolução; medição sincronizada.
  `tools/test-dx53-vmid-mutations.py` **5/5 reprovam**.
- **Demais suítes:** DX.52 6/6, DX.48 6/6, DX.47 9/9, DX.43 7/7, auditoria de
  segurança PASS.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.259.1.0` SYS `2D95145B1E4A4794563E7ACEF053E0376F142B10D03B284BBAD46EA308C3B593`, INF `EAB077C2…35D7`, CAT `DD5ADF53…CB41`. Auditoria do pacote PASS; `DESARME ANTES DA DEVOLUCAO`, `QUARENTENA: VMID` e `CREATE_ALLOC_RECUSADO` presentes no SYS |
| UMD | `8D4EE96D…A3CB` (a da DX.52) |
| RADV | DX.42 `FCAA11FE…` |
| Pré-voo | `tools/prepare-corrida98r-dx53.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA98R_DX53.cmd`, elevado, 300 s, depois de **desligamento completo** (marcador do rollback de 00:21:12) |

## 5. Critério, escrito antes

**Controle:**
- `[DX53_VMID] DESARME ANTES DA DEVOLUCAO` presente;
- `[VMID_POOL] LIBERADO … DEPOIS do desarme`;
- zero `[VMID_POOL] LIBERADO` sem "DEPOIS";
- configuração igual à 97: zero `ANEL … criado`, `DX50_BISSECCAO_DESLIGADO`.

**Carga (obrigatório medir):**
- `KMD_CONTEXT_CREATE`/min e ciclos do laço do Explorer/min;
- comparáveis à 97 (~290 contextos/min, ~156 ciclos) = carga equivalente.

| | Assinatura | Implicação |
|---|---|---|
| **A** | **Janela inteira sem travamento** (o guardião reverte sozinho em 300 s), carga equivalente à 97, harness 60/60 | A janela de reuso do VMID era a causa do travamento (P0 de corrida). A próxima corrida corrige o laço do Explorer e religa a vazão com segurança. |
| **B** | Travamento com carga equivalente | O VMID não era a causa, ou não a única. Próximo corte: staging persistente ou o log síncrono do KMD. Ler `PendentesNoRastreador` nos últimos desarmes. |
| **C** | Carga caiu muito (contextos/min bem abaixo da 97) | Inconclusivo para o VMID. Descrever o que mudou a carga. |
| **D** | Aparece `QUARENTENA` no hardware | O desarme falha de verdade no silício. Registrar passo/registrador/valor e quantos slots entraram em quarentena. Não é regressão; é medição nova. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Secundárias:**
- `PendentesNoRastreador > 0` no instante do desarme (a outra janela);
- `CreateResource RECUSADO desc:` no log do Explorer (a DX.52 descreve o
  recurso recusado pelo dxgkrnl);
- desktop sem piscar;
- Present/min.

**Depois da corrida:** as 5 seções e a tabela-checklist.
