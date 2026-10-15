# 694 — T5 e T6: mineração do processo novo na Corrida 263 e inventário de MMIO em regime

**Data:** 10/10/2026. **Natureza:** offline, sem corrida. **Base:** [doc 693](693_CORRIDA263_LINHA_DE_BASE_TRAVA_DENTRO_DO_22O_PROCESSO_NOVO_DESFECHO_B.md) e [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md) §7.

---

## 1. T5 — o que o KMD fez na janela de cada processo novo (Corrida 263)

**Método:**
- O log da T3, o da UMD e as linhas do KMD com `qpc=` usam o mesmo relógio (QPC, 10 MHz).
- Recortei o log do KMD recuperado na janela de cada filho (do `INICIO` ao `FIM` + 300 ms; o 22º vai até o fim) e contei as operações.
- Script: `t5_janelas.py`, na pasta de rascunho da sessão; reproduzível sobre a pasta de evidência da 263.
- Ressalva: o log do KMD é amostrado por classe (memória `bc250-log-de-display-e-amostrado`). Só as classes que saem sempre (`BC250_HANG … PAGING_DDI`, `KMD_SDMA_SUBMIT`, `IB EXECUTADO`, `DX91_PAGING_MOVE`) foram contadas como frequência.

### 1.1 Volume por processo: o 22º não é anormal no total

| Ciclo | FLUSH_TLB na janela | FILL (MB) | TRANSFER (MB) | IBs do SDMA0 |
|---|---|---|---|---|
| 1–6 (típico) | 366–430 | ~64 | ~32 | ~150–170 |
| 7 | 1.046 | 70,5 | 20,0 | 390 |
| 8–11 | 264–268 | ~40 | ~8 | ~102 |
| 13 | 531 | 65,9 | 52,0 | 212 |
| 21 | 563 | 48,1 | 60,1 | 221 |
| **22 (travou)** | **506** (janela de ~0,5 s) | 32,1 | 8,0 | 169 |

Cada filho aloca 32 MB de texturas e gera **40 a 65 MB de zeramento (FILL)**. É a memória em dobro: a alocação WDDM e a imagem da RADV (T10). Também gera de 8 a 60 MB de despejo (TRANSFER) de outros processos.

### 1.2 Intensidade nos primeiros 500 ms: o 22º é o maior da corrida

| Ciclo | FLUSH_TLB nos primeiros 500 ms | Submits do GFX (DWM) nos mesmos 500 ms |
|---|---|---|
| típico (1–21) | 150–225 | 7–16 |
| 7 | 451 | 22 |
| 13 | 368 | 9 |
| **22** | **506** | **25** |

O 22º teve, ao mesmo tempo, a maior rajada de invalidação de TLB e o maior número de submits do GFX do DWM. O ciclo 7 chegou perto (451/22) e passou. É n=1: sinal, não prova.

### 1.3 O fim da janela 22

- A última drenagem do log do KMD é de ~20:44:43,025, ~180 ms depois da última linha do filho na UMD. O DWM ainda apresentou às 20:44:43,028.
- Nos últimos 100 ms: 154 FLUSH_TLB (um a cada ~0,6 ms, cada um ~0,6 ms dentro do handler) intercalados com IBs de paginação do SDMA0 de 128–256 bytes, todos executados (RPTR==WPTR, fence sobre poison).
- Nesse mesmo intervalo, um submit do GFX do DWM na VMID 1 (`DmaSize=10720`), aposentado.
- No último tique do rastro: tudo aposentado, SDMA1 ocioso.
- A taxa de ~1.500 FLUSH_TLB/s **não é inédita**: houve 15 janelas de 100 ms com taxa parecida na corrida, sem trava. O que muda no 22º é a combinação com o trabalho do GFX.

### 1.4 Achado de código (P1): todo FLUSH_TLB de processo de usuário invalida todas as VMIDs

`driver/wdk/bc250_active_miniport.c:16824-16828`, com a 4c.4 armada:

```c
uint32_t splitMask = (active->PagingProcessHandle != NULL &&
    (BC250_MCDM_HANDLE)pFlushTlb->hProcess == active->PagingProcessHandle) ?
    (1u << BC250_GCVM_VMID8) : Bc250ActiveRingTlbFlushVmidMask(active);
```

- `Bc250ActiveRingTlbFlushVmidMask` (`:21445`) devolve as VMIDs 7, 8, 9 **mais todas as VMIDs do pool instaladas**. Isso inclui a VMID do DWM, que o GFX está usando.
- A invalidação vai no anel do SDMA0 (motor de invalidação 1).
- O Windows informa no FLUSH_TLB **qual processo e qual raiz** mudaram (`hProcess`, `RootPageTableAddress`, faixa de VA). O driver ignora isso e invalida todo mundo.
- Para um processo novo que **nem tem VMID ainda**, o caso dos filhos da T3, que não submetem ao GFX, a invalidação correta é **nenhuma**: as tabelas dele não estão em TLB nenhuma. A instalação da VMID já invalida na hora certa.
- **O amdgpu** invalida só a VMID da VM cujas tabelas mudaram. A troca de base e a invalidação vão no anel da engine que usa aquela VMID, antes do job seguinte (`gmc_v10_0_emit_flush_gpu_tlb`, `gmc_v10_0.c:359`). Ele não invalida a VMID de outro processo enquanto o GFX traduz com ela.

**Classificação:** a diferença é P1 (código conferido contra o upstream). Que ela cause a trava é **H**.

O quanto ela ocorre na janela da trava é **não medido**: as linhas `[ActiveFlushTlb]` e `[PT_GPU_SPLIT]` são amostradas, e o rastro `PAGING_DDI` não registra o `hProcess`. Nas linhas amostradas, os splits do processo de paginação levam máscara `0x0100` (só a VMID 8), e a máscara cheia aparece no início do boot (`0x0380`). Falta o contador por tipo de máscara.

### 1.5 O que a T5 propõe

1. **Instrumentação barata (sem mudar comportamento):** contadores no rastro durável por FLUSH_TLB:
   - quantos são do processo de paginação;
   - quantos são de processo **sem VMID**;
   - quantos são de processo **com** VMID;
   - quantas VMIDs foram invalidadas no total.

   Confirma o mecanismo na próxima corrida, qualquer que seja o desfecho.
2. **Mudança de paridade candidata (a definir com a T6):** FLUSH_TLB de processo de usuário invalida só a VMID daquele processo; processo sem VMID não invalida nada.

---

## 2. T6 — inventário de acessos da CPU a registradores em regime

**Método:** leitura do código por um agente (só leitura), conferida por mim nos pontos que pesam. Abaixo, o que roda **depois** do StartDevice, sem TDR e sem teardown, com o equivalente no amdgpu (cópia local em `referencias/linux_upstream_2026-09-29/` e `inc/linux_ref/`).

### 2.1 Acessos em regime que o amdgpu não faz (P1, conferidos)

| # | O quê | Quando | Onde | amdgpu |
|---|---|---|---|---|
| 1 | Instalação de VMID do pool pela CPU: escreve PT_BASE, START, END e CNTL e invalida no ENG17 com sondagem do ACK | 1ª submissão GFX de cada contexto novo (12× na 263) | `bc250_active_miniport.c:15738-15751`; `core/bc250_gcvm_vmid8_transaction.c:305` | CNTL/START/END uma vez na inicialização; base e invalidação no anel do job (`gmc_v10_0.c:359`); pela CPU só via KIQ |
| 2 | Reinstalação do "VMID 7 legado" pela CPU | Toda alternância de contexto no GFX (**89×** na 263), embora o IB use a VMID do pool | `bc250_active_miniport.c:15790-15815` | Sem equivalente |
| 3 | Desarme da VMID no DestroyContext (CNTL=0, PT zerada, ENG17) | Contexto que morre com VMID do pool (4×) | `bc250_active_miniport.c:5599`; `gcvm_vmid8_transaction.c:416` | Os contextos 1–15 nunca são desligados em regime |
| 4 | Leituras de SQ_DEBUG_STS_GLOBAL, CP_RB0_RPTR/WPTR e CP_ME_CNTL | Toda submissão GFX (2.054) e cada troca de VMID | `bc250_active_miniport.c:16000-16008`, `2073`, `2449`, `2523` | Não lê o SQ; RPTR do writeback |
| 5 | Poll do MEC: escreve GRBM_GFX_INDEX/GRBM_GFX_CNTL e lê CP_HQD_PQ_RPTR | Cada tique do timer de 1 ms | `bc250_submission_engine.c:1574-1626` | Seleção GRBM só sob `srbm_mutex`, fora do regime |
| 6 | SDMA1: RPTR e WPTR por MMIO | Cada rota de faixa de PTE (15.548 na 263) | `bc250_sdma_present.c:2587`, `2677-2685` | Doorbell e writeback (`sdma_v5_0.c:326-388`) |
| 7 | SDMA0: RB_RPTR por MMIO; espera ativa a cada 5 µs nas cópias de 11 MB (flip e COMPOSITE_BLT) | Toda submissão do Node 0 e todo flip | `bc250_sdma_present.c:2923`, `1101-1110`, `3387` | Writeback e fence por interrupção |
| 8 | IH: RPTR por MMIO (a P3.3 por doorbell encurtou a vida nas 242/242b) | Cada interrupção (9.670) | `bc250_sdma_present.c:3950` | Doorbell |
| 9 | Contador de quadros do OTG (DCN, BAR+0x14030) por mapeamento próprio | A cada 4 ms (DPC de VSYNC) | `bc250_display_ddi.c:1585`, `1622` | Na L1 (dc=0, sem trava) nenhum acesso a DCN |
| 10 | Tctl pelo par SMN 0x38/0x3C e status do SDMA1 | A cada ~1,2 s (rastro) | `bc250_active_miniport.c:8714-8790` | Não sonda THM em laço |

Sem MMIO em regime (paridade): FLUSH_TLB (HDP pulado, invalidação no anel), FILL, TRANSFER, UPDATE_PAGE_TABLE e o WPTR do GFX e do SDMA0 (doorbell). As DDIs de CreateProcess/Device/Context/Allocation **não tocam registrador**.

**Achado do pacote:** `GfxWaveProbe=1` está **só no INF de laboratório** (`output_lab/bc250mcdm.inf:116`; o de produção tem 0). É ele que liga as leituras do item 4 em toda submissão do GFX.

### 2.2 Cruzamento com a hora da trava (Corrida 263)

- **Itens 1–3 não estavam acontecendo na trava.** A última instalação e a última troca do VMID 7 pela CPU foram **12,4 s antes** do fim do log. Dos 22 processos, só os ciclos 2, 7, 12 e 19 tiveram troca de VMID na janela; o 22º, **nenhuma**. As linhas `CONTEXT SWITCH` e `VMID_POOL` saem sempre, não são amostradas.
- **Estavam ativos na janela da trava:**
  - itens 4 (havia submissão do GFX do DWM no último milissegundo), 5 (timer de 1 ms), 6 e 7 (rajada de paginação do processo novo), 8, 9 e 10;
  - a invalidação de todas as VMIDs pelo anel do SDMA0 em cada FLUSH_TLB (§1.4).
- O upstream registra um risco do silício (`amdgpu_gmc.c:744`): *"The SDMA on Navi 1x has a bug which can theoretically result in memory corruption if an invalidation happens at the same time as an VA translation."* Na janela da trava, o SDMA0 invalidava VMIDs enquanto o SDMA1 escrevia PTEs sob a VMID 8 (P4.2) e o GFX traduzia na VMID do DWM. Que essa concorrência ocorra exatamente assim é **H**: depende da ordem FENCE/POLL entre os anéis, que não foi conferida aqui.

### 2.3 Candidatos para as próximas corridas (uma diferença por corrida)

| Ordem | Mudança | Por quê | Custo |
|---|---|---|---|
| 1 | **Contadores no rastro** (FLUSH_TLB por tipo de processo e VMIDs invalidadas; rotas para o SDMA1; leituras do SQ), sem mudar comportamento, na repetição **263b** | Mede o mecanismo da §1.4 e da §2.2 na trava; a 263b já é obrigatória | Baixo |
| 2 | **`GfxWaveProbe=0`** (como a produção) | Tira leituras do SQ e do CP que o amdgpu não faz, em toda submissão do DWM, ativas na trava | Só o INF |
| 3 | **FLUSH_TLB invalida só a VMID do processo que mudou** (nada para processo sem VMID) | Paridade com o amdgpu; tira a invalidação da VMID do DWM durante a residência de processo novo | Médio |
| 4 | **Poll do MEC sem escrever GRBM_GFX_INDEX/CNTL a cada 1 ms** | Escrita de seletor pela CPU em regime, sem paralelo no amdgpu | Médio |
| 5 | **VMID pelo anel** (itens 1–3) | Diferença real, mas não ativa nesta trava | Alto |

**Classificação:** todos os itens das tabelas são diferenças P1 (código e log). **Nenhum** está ligado à causa da trava: **NÃO VALIDADO EM HARDWARE**.
