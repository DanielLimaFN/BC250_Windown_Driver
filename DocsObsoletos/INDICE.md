# DocsObsoletos — arquivo morto do projeto BC-250

**Criado em:** 2026-09-02 · **Atualizado em:** 2026-09-02 (2ª passada: limpeza da raiz)
**Nada foi apagado.** Todo arquivo aqui foi movido de `docs/` ou da raiz preservando o
histórico do git (`git mv`). Se qualquer item for necessário de novo, basta mover de volta.

**Motivo do arquivamento:** `docs/` tinha 171 arquivos `.md`, dos quais ~90% eram diários
por candidato de trabalho **já encerrado e validado**. Isso tornava impossível achar o
estado atual. Restaram 24 arquivos ativos em `docs/`, e a raiz passou de 10 `.md` para 5.

**Documento base do projeto:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) na raiz.

---

## O que ficou ativo em `docs/`

| Arquivo | Por que fica |
|---|---|
| `164_ROADMAP_WDDM_JOGOS.md` | **Roadmap vigente** |
| *(a tabela unica virou* `BASE_DE_AVANCO.md` *na raiz)* | Documento base do projeto |
| `161_PLANO_MESTRE_POS_GATE_1_5.md` | Análise que localizou o gargalo do Código 43 |
| `162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md` | Resultado negativo medido — evita reabrir o caminho |
| `163_FASE_0_1_2_CONCLUSAO_UMD_D3D.md` | O que foi entregue no candidato 3.81 |
| `148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md` | **Prova física** de P1 |
| `159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md` | **Prova física** de P2 |
| `26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md` | **Prova física** de ownership de display — base do caminho sem DCN |
| `17_POST_DISPLAY_OWNERSHIP.md` | Contrato de ownership de display |
| `01_EVIDENCIAS_E_LICOES.md` | Lições acumuladas |
| `02_ARQUITETURA_AMDGPU.md` | Referência de arquitetura |
| `03_CATALOGO_DE_REGISTRADORES.md` | Catálogo com proveniência — insubstituível |
| `05_INVENTARIO_BRUTO_DE_ENDERECOS.md` | Inventário de endereços |
| `10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md` | Sequência de init do GFX |
| `11_PSP_TMR_OWNERSHIP.md` | Ownership de TMR do PSP |
| `12_RECUPERACAO_DRIVER_TESTE.md` | Recuperação |
| `21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md` | Necessário na Fase A (KDNET) |
| `21_TESTE_DE_ACEITACAO_SILICIO.md` | Critérios de aceitação |
| `84_MEC_HQD_PROGRAMMING_GATE.md` | Gate de programação do HQD |
| `92_COMPUTE_GPUVA_SDMA_GATE.md` | Gate GPUVA/SDMA |
| `93_RUNBOOK_F4D_INTERMITENCIA_PFP.md` | Runbook em uso |
| `98_RUNBOOK_SHADER_DISPATCH_F8B.md` | Runbook em uso |
| `160_RUNBOOK_ENSAIO_RENDERNODE...md` | Runbook de ensaio |
| `BC250_HARDWARE_TEST_GUIDE.md` | Guia de ensaio físico |
| `README.md`, `evidence/` | Índice e corpus de evidência |

---

## `candidatos-encerrados/` — 132 arquivos

Diários por candidato de trabalho **concluído e validado no silício**. As conclusões
sobrevivem nas provas físicas (`148`, `159`), na `BASE_DE_AVANCO.md` e no roadmap
`164`. Cobre:

| Faixa | Assunto | Estado do trabalho |
|---|---|---|
| `22`–`46` | Pacote MCDM, GCVM, root page table, PSP/GPCOM | Encerrado |
| `47`–`53` | SDMA (F32, ring, copy, fence, layout) | ✅ Validado — item 19/48/49 |
| `54`–`64` | RLC e PFP (direct load, unhalt, icache) | ✅ Validado — itens 14/15 |
| `65`–`80` | CP / PM4 / ring0 / PSP secure boot | ✅ Validado — itens 16/17/20 |
| `81`–`87` | MEC / HQD (selector, snapshot, programming) | ✅ Validado — item 43 |
| `88`–`91` | Handoffs de CP/HQD e tela preta do 3.22/3.24 | Superseded por `162`/`164` |
| `94`–`97` | Handoffs e resposta a auditoria | Superseded |
| `99`–`147` | Saga shader dispatch → P1 (candidatos 3.28–3.54) | ✅ Encerrada em `148` |
| `149`–`158` | Saga P2 (candidatos 3.55–3.61) | ✅ Encerrada em `159` |

**Não reabrir esses documentos para decidir o que fazer.** Eles servem só como
proveniência de um resultado já provado.

## `planos-superseded/` — 14 arquivos

Planos, roadmaps e pontos de retomada substituídos:

| Arquivo | Substituído por |
|---|---|
| `04_PLANO_DO_DRIVER_NOVO.md` | `164` |
| `06`, `07`, `08`, `09` | `BASE_DE_AVANCO.md` |
| `13`, `14`, `15`, `16`, `18`, `19` | Marcos MCDM encerrados; ver `162` |
| `20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md` | `164` |
| `21_AUDITORIA_RECOVERY_MCDM...`, `21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md` | `164` |

## `raiz-superseded/` — 14 arquivos

Documentos da raiz do repositório que competiam com o roadmap vigente:

| Arquivo | Substituído por | Nota |
|---|---|---|
| `ROADMAP_BC250_WDDM_DISPLAY_RENDER_DCN.md` | `docs/164` | **Continha a premissa errada de que DCN é pré-requisito para jogos** |
| `ROADMAP_WDDM_RENDER.md` | `docs/164` | |
| `ROADMAP_VULKAN_100.md` | `docs/164` Fase G | |
| `STATUS_E_ROADMAP_CONSOLIDADO.md` | `BASE_DE_AVANCO.md` | 1460 linhas de estado defasado |
| `AUDITORIA_WDDM_DISPLAY_RENDER_COMPLETO_2026-08-31.md` | `BASE_DE_AVANCO.md` | Auditoria honesta, mas anterior ao encerramento do Gate 1.5 |
| `PLANO_AB_WDDM20_WDDM27.md` | — | A/B concluído: item 93, ambos falham igual |
| `HANDOFF_GATE_1_5_RENDERONLYLAB.md`, `HANDOFF_3_76_RENDERONLY_SAFE.md` | `docs/162` | Gate 1.5 **reprovado** |
| `HANDOFF_IA_RENDERNODE_WDDM_APOS_3_71.md` | `docs/161` | |
| `POSTMORTEM_3_75_TELA_PRETA_E_REGISTRO_DA_SESSAO.md` | `docs/162` | |
| `CHECKLIST_COMPLETO_BC250_WDDM_DISPLAY_RENDER.md` | **`BASE_DE_AVANCO.md`** | A numeracao `#` dos 182 itens foi preservada na nova base |
| `BC250_HARDWARE_RULES.md` | `rules.md` | Conteudo era subconjunto de `rules.md` (duplicacao normativa) |
| `INSTRUCTIONS.md` | `CLAUDE.md` + `BASE_DE_AVANCO.md` | Comandos uteis absorvidos no `CLAUDE.md` |
| `Informações.md` | `BASE_DE_AVANCO.md` | Base de conhecimento congelada na "Fase 2: Compute & SDMA" |

## `artefatos-build-raiz/` — 15 arquivos + `tmp/` e `scratch/`

Saída de build e logs de corrida que estavam soltos na raiz: `*.obj` (4) e
`bind/confirm/quarantine/rollback/live_test` `*.log` (9). São reprodutíveis ou já
consolidados em `docs/evidence/`. Movidos apenas para desentulhar a raiz. Na 2ª passada entraram tambem os arquivos de 1 linha `query` e `start`, e os diretorios `tmp/` e `scratch/`. O `clean_all_drivers.ps1` foi para `tools/`, nao para o arquivo morto.

---

## Regra de manutenção

Um documento vai para cá quando **as três** condições valem:

1. O trabalho que ele descreve está encerrado — validado, reprovado ou abandonado.
2. A conclusão está registrada em documento ativo (prova física, checklist ou roadmap).
3. Nenhum documento ativo depende dele para ser executado.

Documento com prova física de item ✅ **fica ativo**, mesmo que antigo — é a evidência
que sustenta o veredito.
