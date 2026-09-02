# `docs/` — documentos ativos

**O documento base do projeto está na raiz:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md)
— Tabela 1 (já feito, com prova física) e Tabela 2 (o que falta, por prioridade, com o
teste que fecha cada item).

Este diretório tinha 171 arquivos. Os 146 que descreviam trabalho encerrado foram movidos
para [`DocsObsoletos/`](../DocsObsoletos/INDICE.md) com `git mv` — **nada foi apagado**.

---

## Plano vigente

| Documento | Papel |
|---|---|
| [`164_ROADMAP_WDDM_JOGOS.md`](164_ROADMAP_WDDM_JOGOS.md) | **Roadmap vigente.** Fases A→H, decisão de arquitetura sem DCN, fontes |
| [`163_FASE_0_1_2_CONCLUSAO_UMD_D3D.md`](163_FASE_0_1_2_CONCLUSAO_UMD_D3D.md) | O que foi entregue no candidato 3.81 (INF `.HW` + UMD D3D) |
| [`162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md`](162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md) | Render-only **reprovado por medição** — não reabrir |
| [`161_PLANO_MESTRE_POS_GATE_1_5.md`](161_PLANO_MESTRE_POS_GATE_1_5.md) | Análise que localizou o gargalo do Código 43 |

## Provas físicas — a evidência que sustenta os ✅

| Documento | Prova |
|---|---|
| [`148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md`](148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md) | P1: 17/17, duas fences GPU, `RPTR == WPTR` |
| [`159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md`](159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md) | P2: 16/16 aposentadas, zero mismatches, n=2 |
| [`26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md`](26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md) | Ownership de display **físico** — base do caminho sem DCN |
| [`17_POST_DISPLAY_OWNERSHIP.md`](17_POST_DISPLAY_OWNERSHIP.md) | Contrato de ownership de display |
| `evidence/` | Corpus bruto de todas as corridas |

## Referência técnica

| Documento | Conteúdo |
|---|---|
| [`03_CATALOGO_DE_REGISTRADORES.md`](03_CATALOGO_DE_REGISTRADORES.md) | Catálogo com proveniência — **única fonte válida de offsets** |
| [`05_INVENTARIO_BRUTO_DE_ENDERECOS.md`](05_INVENTARIO_BRUTO_DE_ENDERECOS.md) | Inventário bruto de endereços |
| [`02_ARQUITETURA_AMDGPU.md`](02_ARQUITETURA_AMDGPU.md) | Arquitetura amdgpu de referência |
| [`10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md`](10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md) | Sequência de init do GFX |
| [`11_PSP_TMR_OWNERSHIP.md`](11_PSP_TMR_OWNERSHIP.md) | Ownership de TMR do PSP |
| [`01_EVIDENCIAS_E_LICOES.md`](01_EVIDENCIAS_E_LICOES.md) | Lições acumuladas |

## Runbooks e gates em uso

| Documento | Uso |
|---|---|
| [`21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md`](21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md) | KDNET — **fora do plano** (sem segunda máquina); ver P3 da base |
| [`21_TESTE_DE_ACEITACAO_SILICIO.md`](21_TESTE_DE_ACEITACAO_SILICIO.md) | Critérios de aceitação no silício |
| [`160_RUNBOOK_ENSAIO_RENDERNODE_3.66_SEM_WATCHDOG.md`](160_RUNBOOK_ENSAIO_RENDERNODE_3.66_SEM_WATCHDOG.md) | Ensaio sem watchdog |
| [`98_RUNBOOK_SHADER_DISPATCH_F8B.md`](98_RUNBOOK_SHADER_DISPATCH_F8B.md) | Dispatch de shader |
| [`93_RUNBOOK_F4D_INTERMITENCIA_PFP.md`](93_RUNBOOK_F4D_INTERMITENCIA_PFP.md) | Intermitência do PFP |
| [`92_COMPUTE_GPUVA_SDMA_GATE.md`](92_COMPUTE_GPUVA_SDMA_GATE.md) | Gate GPUVA/SDMA |
| [`84_MEC_HQD_PROGRAMMING_GATE.md`](84_MEC_HQD_PROGRAMMING_GATE.md) | Gate de programação do HQD |
| [`12_RECUPERACAO_DRIVER_TESTE.md`](12_RECUPERACAO_DRIVER_TESTE.md) | Recuperação do driver de teste |
| [`BC250_HARDWARE_TEST_GUIDE.md`](BC250_HARDWARE_TEST_GUIDE.md) | Guia de ensaio físico |

---

**Regra de arquivamento:** um documento sai de `docs/` quando o trabalho que ele descreve
está encerrado, a conclusão está registrada em documento ativo, e nada ativo depende dele.
Documento com prova física de item ✅ **fica**, mesmo que antigo.
