# DocsObsoletos — arquivo morto do projeto BC-250

**Criado em:** 2026-09-02 · **Atualizado em:** 2026-09-18 (3ª passada: higienização pré-Corrida 26)
**Nada foi apagado.** Todo arquivo aqui foi movido de `docs/` ou da raiz preservando o
histórico do git (`git mv`). Se qualquer item for necessário de novo, basta mover de volta.

**Motivo do arquivamento:** `docs/` acumulou handoffs e diários por candidato de trabalho
já encerrado e superado. Na 3ª passada (18/09), 116 arquivos foram arquivados para manter
em `docs/` apenas os 35 documentos vitais de arquitetura, referências de silício, provas
de marcos históricos e o estado atual.

**Documento base do projeto:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md) na raiz.

---

## O que ficou ativo em `docs/` (35 arquivos)

Ver detalhamento em [`docs/README.md`](../docs/README.md).
- **Roadmap e Planos Ativos:** `164_ROADMAP_WDDM_JOGOS.md`, `281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md`, `161..163`
- **Arquitetura Vigente:** `173_TOOLCHAIN_VS2026_WDK28000.md`, `282_HANDOFF_FASE2_UMD_D3D11_TRADUZIDA_PARA_VULKAN.md`, `283_HANDOFF_CP_RB0_BACKEND_GRAFICO_PERSISTENTE.md`
- **Provas Físicas de Marcos:** `148` (P1), `159` (P2), `241` (Marco 1), `245` (Marco 2), `258` (Marco 3), `280` (Marco 4), `17` e `26` (Display Ownership)
- **Handoffs da Rodada Atual:** `284` (Corrida 13), `285` (Corrida 16), `286` (Corrida 17), `287` (Corrida 18)
- **Catálogos de Registradores e Hardware:** `01`, `02`, `03`, `05`, `10`, `11`, `12`, `21`, `84`, `92`, `93`, `98`, `BC250_HARDWARE_TEST_GUIDE.md`

---

## `candidatos-encerrados/` — 248 arquivos

Diários por candidato de trabalho **concluído e validado no silício**. As conclusões
sobrevivem nas provas físicas (`148`, `159`, `241`, `245`, `258`, `280`), na `BASE_DE_AVANCO.md`
e no roadmap `164`. Cobre:

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
| `165`–`240` | Saga VMID8, Indirect SDMA, TDR e Bring-up Node 1 (candidatos 3.82–3.118) | ✅ Encerrada |
| `242`–`279` | Diários diários de ensaio dos Marcos 1..4 e pipelines PM4 preliminares | ✅ Consolidados nos Marcos 1..4 |

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
