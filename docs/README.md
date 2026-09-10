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

## Handoffs — estado por sessão (leia o mais novo primeiro)

| Documento | O que carrega |
|---|---|
| [`207_HANDOFF_2026-09-08_PASSAGEM_DE_BASTAO_DISPUTA_VMID8.md`](207_HANDOFF_2026-09-08_PASSAGEM_DE_BASTAO_DISPUTA_VMID8.md) | **Mais recente.** Passagem de bastão: disputa física de VMID 8 entre Present e Node 1 isolada no silício, telemetria VMID8_DISPUTE_PROBE implementada, pacote 3.94.4.0 assinado e opções arquiteturais |
| [`206_HANDOFF_2026-09-08_15-43_PRIMEIRO_SUBMIT_NODE1_E_DISPUTA_VMID8.md`](206_HANDOFF_2026-09-08_15-43_PRIMEIRO_SUBMIT_NODE1_E_DISPUTA_VMID8.md) | Corrida 15:43: B7.1 validada (MapAllocation), primeiro submit Node 1 no silício (WPTR 204->216), tripé fail-closed e prova da disputa do VMID 8 |
| [`205_HANDOFF_2026-09-08_15-18_B2_PROVADO_E_MAPALLOCATION_PENDING.md`](205_HANDOFF_2026-09-08_15-18_B2_PROVADO_E_MAPALLOCATION_PENDING.md) | B2 fechado no silício (DWM E_NOTIMPL provado) e isolamento do E_PENDING (0x8000000A) no MapAllocation |
| [`197_HANDOFF_2026-09-08_B7_NODE1_CANDIDATO_3_90_1.md`](197_HANDOFF_2026-09-08_B7_NODE1_CANDIDATO_3_90_1.md) | Candidato lab 3.90.1 assinado e auditado, hashes SYS/UMD/INF/CAT congelados, gates default-off; não instalado e sem corrida física |
| [`196_HANDOFF_2026-09-08_B7_NODE1_PHYSICAL_RUNNER.md`](196_HANDOFF_2026-09-08_B7_NODE1_PHYSICAL_RUNNER.md) | Acionador físico Node 1 default-off, restrito por processo/token e single-shot; runner coleta UMD+KMD e recusa pacote/UMD divergentes; nenhuma corrida feita |
| [`195_HANDOFF_2026-09-08_B7_NODE1_TRIPOD_TRANSACTION.md`](195_HANDOFF_2026-09-08_B7_NODE1_TRIPOD_TRANSACTION.md) | Transação offline completa da UMD para poison, submit Node 1, fence exata, readback byte a byte e teardown; acionador físico ainda bloqueado |
| [`194_HANDOFF_2026-09-08_B7_UMD_NODE1_SUBMISSION_OFFLINE.md`](194_HANDOFF_2026-09-08_B7_UMD_NODE1_SUBMISSION_OFFLINE.md) | Auditoria de sinceridade e rota offline UMD -> upload -> submit Node 1; gate D3D e Draw permanecem fail-closed; prova física ainda aberta |
| [`190_HANDOFF_PARA_PROXIMA_IA_B7.md`](190_HANDOFF_PARA_PROXIMA_IA_B7.md) | Texto autocontido para a próxima IA assumir o B7 a partir de Node 1 + winsys, preservando as provas e restrições atuais |
| [`189_CHECKLIST_ATE_JOGOS_BC250.md`](189_CHECKLIST_ATE_JOGOS_BC250.md) | Checklist operacional completo: estado atual, caminho crítico, trabalho restante e prova exigida até jogos |
| [`188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md`](188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md) | **Mais recente.** Corrida 3.89: TDR 0x117, restart provado e 40 submissões readmitidas; display visível continua dependente do B7 |
| [`187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md`](187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md) | Corrida física: TDR chegou ao par Reset/Restart, restart falhou e terminou em 0x116; candidato 3.89 reconstruiu o SDMA0 e foi validado pela corrida do doc 188 |
| [`186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md`](186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md) | Corrige a ordem do ensaio TDR e registra o pacote 3.88 armado no INF, assinado/auditado; foi o pacote usado na corrida do doc 187 |
| [`185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md`](185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md) | QueryEngineStatus por medição e hang físico induzido; contém correção posterior apontando para o doc 186 |
| [`184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md`](184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md) | Reset/Restart de TDR com backend físico fail-closed; antecede a detecção e o gatilho dos docs 185/186 |
| [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md) | Produção em Código 0, MMU global, CommitVidPn e 12 Present; tela preta e dois dumps confirmam o fail-fast D3D11 |
| [`178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md`](178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md) | Corrida física 15:49: Código 43 porque a allowlist recusou o primeiro snapshot em `0xA610`; correção validada pela corrida 16:19 |
| [`177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md`](177_HANDOFF_2026-09-07_15-16_GLOBAL_MMU_ALLOCATOR.md) | Corrida física 15:16: Código 43 fail-closed por allocator ainda não staged; watchdog restaurou BasicDisplay Código 0. Registra a correção superada pela corrida 15:49 |
| [`176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md`](176_HANDOFF_2026-09-07_MMU_FAIL_CLOSED.md) | Corrige a atribuição falsa da corrida 14:32, registra a prova isolada VMID8 e o primeiro candidato WDDM global-MMU fail-closed |
| [`175_HANDOFF_2026-09-07_MMU_GLOBAL_E_CORRIDA_LAB.md`](175_HANDOFF_2026-09-07_MMU_GLOBAL_E_CORRIDA_LAB.md) | Handoff original da MMU global; contém correção obrigatória no topo porque misturava o log acumulado com o bind das 14:32 |
| [`174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md`](174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md) | Sessão offline: fonte única da geometria GPUVA e telemetria SDMA0. O aviso de probe armado é histórico; o valor estava ausente às 15:05 |
| [`173_TOOLCHAIN_VS2026_WDK28000.md`](173_TOOLCHAIN_VS2026_WDK28000.md) | Migração de toolchain validada offline; efeito no silício não medido |
| [`173_DIMENSIONAMENTO_D3D11_PARA_DWM.md`](173_DIMENSIONAMENTO_D3D11_PARA_DWM.md) | Dimensionamento do D3D11 que o `dwmcore` exige — e o item zero (`UMDRIVERPRIVATE`) |
| [`172_HANDOFF_2026-09-06_20-34.md`](172_HANDOFF_2026-09-06_20-34.md) | RADV parou dentro de `vkCreateDevice`; dois contextos Node 1, zero submit |
| [`171_HANDOFF_2026-09-06.md`](171_HANDOFF_2026-09-06.md) | Sessão de 06/09 |

### Rastreamento das perguntas de registrador do item B1

| Documento | Papel |
|---|---|
| [`167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md`](167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md) | **As 5 perguntas.** 1, 3, 4 e 5 **FECHADAS** (07/09, por leitura). A **2** segue aberta, com dados e hipótese testável |
| [`168_HANDOFF_2026-09-05_INDIRECT_SDMA.md`](168_HANDOFF_2026-09-05_INDIRECT_SDMA.md) | As 4 variantes de submissão, as 10 hipóteses mortas, e a telemetria crua de §5.3 — que o doc 174 decodificou |

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
