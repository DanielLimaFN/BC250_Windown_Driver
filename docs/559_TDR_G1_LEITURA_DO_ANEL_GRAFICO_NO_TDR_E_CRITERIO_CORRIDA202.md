# 559 — TDR.G1: leitura do anel gráfico no TDR; critério da Corrida 202 (repetição da CG.1)

**Data:** 02/10/2026 · **Tipo:** offline, sem corrida · **Candidato:** lab **3.349.1.0**
**Antecede:** [doc 558](558_CORRIDA201_PRIMEIRA_CORRIDA_256MB_SEM_TRAVA_DURA_MGCG_DO_POST.md) (Corrida 201).

---

## 1. Por que a 202 é uma repetição, e o que ela acrescenta

A 201 foi a primeira corrida com 256 MB sem trava dura, com **uma** variável: o MGCG do SDMA0 voltando ao valor do POST. Uma amostra não fecha a causa. As travas desta semana vieram entre 1 e 3 minutos. Se a próxima corrida empilhar outra mudança de comportamento e travar, ninguém saberá qual das duas falhou.

Por decisão do Daniel (opção B), a 202 repete a 201 e acrescenta **somente leitura** no TDR, para que o hang gráfico visto na 201 deixe nome se voltar.

## 2. O que a 201 já disse sobre o TDR (P0, instrumentação existente)

- O IB do app foi **consumido inteiro**: `IB1 Base=0x2407FC000`, que é o IB de VMID 10 (1.064 DW), com `RestamDW=0`.
- O ME parou num **`WAIT_REG_MEM`** (0x3C) logo depois de um `RELEASE_MEM`, com `SemPolling=1`, `EopDoneBusy=1`, `GfxContextBusy=1` e **nenhuma onda em voo**. O anel tinha 10 DW pendentes (`RPTR=7070`, `WPTR=7080`).
- Essa submissão trocou de VMID 2 para 10 (`VM_FLUSH REQ=0x00F80400 ACK_MASK=0x400`, `WaitFence=430`).
- A troca de VMID emite duas esperas pelo nosso construtor PM4 (`bc250_pm4_packet_builder.c`, P1 `nvd.h`):
  - `PFP_MEM_GE` sobre a fence do anel;
  - `ME_REG_EQ` sobre o registrador de ACK da invalidação.

**O que falta:** **qual** `WAIT_REG_MEM` estava preso, com que endereço, referência e máscara, e quanto valia a fence naquele instante.

## 3. O que foi feito (TDR.G1), somente leitura

`Bc250ActiveTdrDumpGfxRingTail`, chamado logo depois do `[TDR_GFX_QUIESCE] Snapshot` e **antes** do soft-reset:
- reduz `RPTR`/`WPTR` a uma volta do anel (A15);
- imprime 40 DW antes do `RPTR` mais os pendentes, com teto de 64, lidos da **memória nossa** (`RingCpuVirtual`);
- imprime a fence em memória (`FenceCpuVirtual`) com a última submetida e a última retirada;
- decodifica cada `WAIT_REG_MEM` da janela: função, espaço `MEM`/`REG`, engine, endereço, referência e máscara. Marca se é a fence do anel e se ainda estava pendente.

**Nenhum** registrador é lido ou escrito por essa função.

**Provas offline:**
- `build-contract` EXIT=0.
- `audit_tdrg1_ring_tail` novo: **4 de 4 mutações reprovam** (ponteiro sem reduzir à volta; chamada removida; teto removido; leitura MMIO dentro do dump).
- Auditoria de segurança e auditoria do pacote: PASS.

**Pacote 3.349.1.0:**

| Arquivo | SHA-256 |
|---|---|
| SYS | `DBE80BF3FFEC6406BCEA27B26CA26B5AFF5BFDCB350072634AAEF8DE0E8F3526` |
| INF | `9CCCB509C1043851D59E3501EC485075C6AE0980892D88064CAAC6130911FEE1` |
| CAT | `2FF90388819719BD4F4A702547A2DFB92A4CB37F1EA050F1B5BF63D2B2A51FCA` |
| UMD | `ED025FAB…` (a mesma da 199–201) |

- RADV `5447A17D…`.
- `[TDR_GFX_RING_TAIL]` e `[SDMA_CG]` estão no SYS assinado.
- Scripts: `tools\prepare-corrida202-repeticao-cg1.ps1` e `tools\EXECUTAR_CORRIDA202_REPETICAO_CG1_600S.cmd`.

## 4. Corrida 202 — critério (escrito antes, Lei 10)

**Comportamento igual ao da 201.** A única diferença é uma leitura que só roda dentro do TDR.

**Preparação:**
1. **Boot frio de verdade**: tirar da tomada, sem reset quente depois.
2. `EXECUTAR_CORRIDA202_REPETICAO_CG1_600S.cmd` elevado.
3. **Mesmo uso da 201:** Pesquisa algumas vezes, Gerenciador de Tarefas, Configurações. Anotar a hora de cada tela preta.
4. Se travar de vez: `tools\coletar-pos-trava.ps1` antes de tudo.

**Pergunta 1 — a CG.1 se sustenta?**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A — repetiu** | `[SDMA_CG] … RESTAURADOS`, BCG1 com `0x000C0100`, rollback programado sem trava dura, **e** `res=-2` ocorrendo | n=2: a CG.1 vira correção e entra no produto. A trava dura sai do topo da lista |
| **B — a trava voltou** | Restauração confirmada **e** trava dura | A 201 foi sorte ou carga menor; CG.1 refutada como causa suficiente. Volta a hipótese da pressão de memória (doc 557 §2.3) |
| **C — não comparável** | Sem `res=-2` e sem Pesquisa/Gerenciador de Tarefas abertos | A carga foi menor que a da 201; a repetição não testa. Repetir com o uso certo |

**Pergunta 2 — se houver TDR, onde ele parou:**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **T1 — espera da fence** | `WAIT_REG_MEM … Espaco=MEM … = FENCE DO ANEL (PENDENTE)` e `FenceMem` menor que a `Ref` | O CP esperava uma fence que não chegou. Implicado: o `RELEASE_MEM` do IB anterior (EOP não concluído, `EopDoneBusy=1`) |
| **T2 — ACK de invalidação** | `WAIT_REG_MEM … Espaco=REG (PENDENTE)` com Ref/Mask iguais ao `ACK_MASK` da troca de VMID | A invalidação de TLB do VMID novo não foi confirmada. Implicado: o caminho de troca de VMID (`VM_FLUSH`) |
| **T3 — sem `WAIT_REG_MEM` pendente** | Nenhuma linha `WAIT_REG_MEM … (PENDENTE)` | O que está preso não é uma espera do nosso anel; olhar `RELEASE_MEM`/EOP |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Regras:**
- **Lei 10.3:** A exige os quatro sinais. Sem TDR, a pergunta 2 fica sem resposta, e isso não é falha.
- **Risco declarado:** nenhum caminho novo fora do TDR. A leitura do anel no TDR é de VRAM pela CPU, a mesma janela que o log já usa nesse ponto.

**NÃO VALIDADO EM HARDWARE.**
