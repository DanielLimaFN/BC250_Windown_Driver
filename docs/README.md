# `docs/` — documentos ativos

**Ponto de partida em 25/09/2026:** [handoff 438 — Corridas 111–118](438_HANDOFF_2026-09-25_CORRIDAS_111_A_118_VISUAL_VELOCIDADE_E_TRAVAMENTO.md), com estado da máquina, proveniência do pacote, falhas conhecidas, erros anteriores e próximos testes. [Critério da 118](436_DX73_PIXEL_CENTRAL_DA_COPIA_E_CRITERIO_CORRIDA118.md) e [resultado da 118](437_CORRIDA118_DX73_TRIANGULO_PIXELS_COPIA_E_R1.md) preservam a prova do ensaio mais recente. Os handoffs de 17/09 e 24/09 abaixo são históricos.

**O documento base do projeto está na raiz:** [`BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md)
— Tabela 1 (já feito, com prova física) e Tabela 2 (o que falta, por prioridade, com o
teste que fecha cada item).

Este diretório mantém estritamente os documentos essenciais de arquitetura, referências
de hardware e silício, provas físicas de marcos e o estado da fase atual. Documentos
intermediários e diários de trabalho encerrado foram movidos para
[`DocsObsoletos/`](../DocsObsoletos/INDICE.md) com `git mv` — **nada foi apagado**.

---

## 1. Plano Vigente e Roadmaps

| Documento | Papel |
|---|---|
| [`164_ROADMAP_WDDM_JOGOS.md`](164_ROADMAP_WDDM_JOGOS.md) | **Roadmap vigente.** Fases A→H, decisão de arquitetura sem DCN, fontes |
| [`281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md`](281_PLANO_PRODUTO_DESKTOP_E_JOGOS_TRANSPARENTES.md) | **Plano de Produto.** Desktop acelerado e jogos D3D11/Vulkan sem configuração |
| [`163_FASE_0_1_2_CONCLUSAO_UMD_D3D.md`](163_FASE_0_1_2_CONCLUSAO_UMD_D3D.md) | O que foi entregue no candidato 3.81 (INF `.HW` + UMD D3D) |
| [`162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md`](162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md) | Render-only **reprovado por medição** — não reabrir |
| [`161_PLANO_MESTRE_POS_GATE_1_5.md`](161_PLANO_MESTRE_POS_GATE_1_5.md) | Análise que localizou o gargalo do Código 43 |

---

## 2. Arquitetura Ativa e Engenharia

| Documento | Papel |
|---|---|
| [`173_TOOLCHAIN_VS2026_WDK28000.md`](173_TOOLCHAIN_VS2026_WDK28000.md) | Toolchain oficial: VS2026 18.9.1 / MSVC 19.51 / WDK 10.0.28000.0 |
| [`282_HANDOFF_FASE2_UMD_D3D11_TRADUZIDA_PARA_VULKAN.md`](282_HANDOFF_FASE2_UMD_D3D11_TRADUZIDA_PARA_VULKAN.md) | Arquitetura UMD D3D11 traduzida para Vulkan (WDDM winsys + Mesa RADV) |
| [`283_HANDOFF_CP_RB0_BACKEND_GRAFICO_PERSISTENTE.md`](283_HANDOFF_CP_RB0_BACKEND_GRAFICO_PERSISTENTE.md) | Backend Gráfico Físico `CP_RB0` (PFP/ME) sob VMID 7 |

---

## 3. Provas Físicas dos Marcos no Silício

| Documento | Prova |
|---|---|
| [`148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md`](148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md) | P1: 17/17, duas fences GPU, `RPTR == WPTR` |
| [`159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md`](159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md) | P2: 16/16 aposentadas, zero mismatches, n=2 |
| [`241_HANDOFF_2026-09-11_RADV_COMPUTE_TRIPOD_PASSOU_NO_SILICIO.md`](241_HANDOFF_2026-09-11_RADV_COMPUTE_TRIPOD_PASSOU_NO_SILICIO.md) | **Marco 1:** RADV WDDM Compute Tripod no silício (Exit 0) |
| [`245_HANDOFF_2026-09-11_MARCO2_RADV_PRESENT_CONCLUIDO_NO_SILICIO.md`](245_HANDOFF_2026-09-11_MARCO2_RADV_PRESENT_CONCLUIDO_NO_SILICIO.md) | **Marco 2:** RADV Present e Swapchain no silício |
| [`258_HANDOFF_2026-09-12_21-22_MARCO3_600_QUADROS_SUSTENTADO.md`](258_HANDOFF_2026-09-12_21-22_MARCO3_600_QUADROS_SUSTENTADO.md) | **Marco 3:** SDMA Blt sustentado 600 quadros sem TDR/BugCheck |
| [`280_MARCO4_VULKAN_WSI_CUBE_CONCLUIDO_NO_SILICIO.md`](280_MARCO4_VULKAN_WSI_CUBE_CONCLUIDO_NO_SILICIO.md) | **Marco 4:** Vulkan WSI nativo com cubo 3D giratório no silício |
| [`26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md`](26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md) | Ownership de display **físico** — base do caminho sem DCN |
| [`17_POST_DISPLAY_OWNERSHIP.md`](17_POST_DISPLAY_OWNERSHIP.md) | Contrato de ownership de display |

---

## 4. Handoffs históricos (17/09 e 18/09)

| Documento | Assunto |
|---|---|
| [`284_HANDOFF_2026-09-17_15-12_D3D11_NATIVO_GFX_RB0_E_ISOLAMENTO_SCANOUT.md`](284_HANDOFF_2026-09-17_15-12_D3D11_NATIVO_GFX_RB0_E_ISOLAMENTO_SCANOUT.md) | Corrida 13: 16 submissões 3D aposentadas em `CP_RB0` sob VMID 7 |
| [`285_HANDOFF_2026-09-17_18-37_CORRIDA16_0x133_E_CORRECAO_RETIREMENT.md`](285_HANDOFF_2026-09-17_18-37_CORRIDA16_0x133_E_CORRECAO_RETIREMENT.md) | Corrida 16: Diagnóstico e eliminação do BugCheck 0x133 no retirement |
| [`286_HANDOFF_2026-09-17_19-32_CORRIDA17_0x139_E_CORRECAO_DPC_INIT.md`](286_HANDOFF_2026-09-17_19-32_CORRIDA17_0x139_E_CORRECAO_DPC_INIT.md) | Corrida 17: Diagnóstico e eliminação do BugCheck 0x139 |
| [`287_HANDOFF_2026-09-17_20-10_CORRIDA18_0x0A_E_SEPARACAO_DPCS.md`](287_HANDOFF_2026-09-17_20-10_CORRIDA18_0x0A_E_SEPARACAO_DPCS.md) | Corrida 18: Separação de DPCs do Node 0 e Node 1 |

---

## 5. Referência Técnica de Hardware e Registradores

| Documento | Conteúdo |
|---|---|
| [`03_CATALOGO_DE_REGISTRADORES.md`](03_CATALOGO_DE_REGISTRADORES.md) | Catálogo com proveniência — **única fonte válida de offsets** |
| [`05_INVENTARIO_BRUTO_DE_ENDERECOS.md`](05_INVENTARIO_BRUTO_DE_ENDERECOS.md) | Inventário bruto de endereços |
| [`02_ARQUITETURA_AMDGPU.md`](02_ARQUITETURA_AMDGPU.md) | Arquitetura amdgpu de referência |
| [`10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md`](10_AUDITORIA_INIT_GFX_CYAN_SKILLFISH.md) | Sequência de init do GFX |
| [`11_PSP_TMR_OWNERSHIP.md`](11_PSP_TMR_OWNERSHIP.md) | Ownership de TMR do PSP |
| [`01_EVIDENCIAS_E_LICOES.md`](01_EVIDENCIAS_E_LICOES.md) | Lições acumuladas |

---

## 6. Runbooks e Guias Operacionais

| Documento | Uso |
|---|---|
| [`BC250_HARDWARE_TEST_GUIDE.md`](BC250_HARDWARE_TEST_GUIDE.md) | Guia de ensaio e instalação em hardware real |
| [`21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md`](21_RUNBOOK_KERNEL_DEBUG_RENDERNODE.md) | KDNET — kernel debug |
| [`21_TESTE_DE_ACEITACAO_SILICIO.md`](21_TESTE_DE_ACEITACAO_SILICIO.md) | Critérios de aceitação no silício |
| [`98_RUNBOOK_SHADER_DISPATCH_F8B.md`](98_RUNBOOK_SHADER_DISPATCH_F8B.md) | Dispatch de shader |
| [`93_RUNBOOK_F4D_INTERMITENCIA_PFP.md`](93_RUNBOOK_F4D_INTERMITENCIA_PFP.md) | Intermitência do PFP |
| [`92_COMPUTE_GPUVA_SDMA_GATE.md`](92_COMPUTE_GPUVA_SDMA_GATE.md) | Gate GPUVA/SDMA |
| [`84_MEC_HQD_PROGRAMMING_GATE.md`](84_MEC_HQD_PROGRAMMING_GATE.md) | Gate de programação do HQD |
| [`12_RECUPERACAO_DRIVER_TESTE.md`](12_RECUPERACAO_DRIVER_TESTE.md) | Recuperação do driver de teste |
| `evidence/` | Corpus de evidências físicas preservadas |
