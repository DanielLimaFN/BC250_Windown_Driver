# 593 — Corrida 218: a UC.1 funcionou, a D2 ficou de pé, a trava dura veio igual; inventário de paridade com o amdgpu

**Data:** 05/10/2026, bind às 10:08:36; reinício às 10:11:20.
**Candidato:** lab **3.366.1.0** (SYS `4FD6B2AD…`).
**Critério:** [doc 592](592_UC1_PTE_DA_GART_COM_MTYPE_UC_COMO_O_LINUX_E_CRITERIO_CORRIDA218.md) §3.
**Evidência:** `docs/evidence/2026-10-05_10-08-36-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-218-bind.txt`: o trecho do bind, linhas 181.849 a 257.985 do log vivo;
- `display-log-218-vivo.txt`: o log vivo inteiro;
- o pacote do `coletar-pos-trava`.

## 1. Medido (P0)

**Pré-condição de validade: cumprida.**
- `PTE[32]=0x30004538FF077`: MTYPE_UC visível no valor.
- As duas linhas `MEC (...) na tabela da GART` com `divergentes=0`.
- `MEC_CURSOR_RECONCILE ... Retired=1`.

**A UC.1 corrigiu a visibilidade:** `[D2_ANEL_SISTEMA] ATIVO ... RPTR_Reg=40 RPTR_Wb_RAM=40 (writeback na RAM confere)`. Na 217 essa mesma linha dizia `RPTR_Wb_RAM=24 ... DIVERGE`.

**A D2 ficou de pé até a trava:**

| Contagem | Valor |
|---|---|
| `IB EXECUTADO`, todos pelo anel na RAM via GART | 4.193 |
| `IB_TRIPOD TIMEOUT` / TDR | 0 / 0 |
| `GFXHUB_FAULT` / `NO_MEMORY` / `DEVICE_REMOVED` | 0 / 0 / 0 |
| Envios forçados | 0 |
| `Present` | 787 |

**Trava dura** no vsync #6.900, ~123 s depois do início. A assinatura é a de sempre (doc 582):
- o último evento da GPU é uma retirada (`RETIRE_SDMA` da fence 4188), com GFX e SDMA retirando normalmente nos 700 ms anteriores;
- `RESET_REASON=0x40080400`, idêntico ao das travas anteriores.

## 2. Veredito pelo critério do doc 592

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| Pré-condição | sim | — |
| A — a D2 sobrevive e não trava | não | trava dura |
| **B — a D2 sobrevive e trava** | **sim** | zero timeout e anel na RAM até a trava |
| C / D — a D2 cai | não | — |
| E | não | — |

- **UC.1: sucesso** (P0). A D2 funciona como no Linux: anel, writeback e fences do SDMA0 em memória de sistema, pela GART da VMID 0, com MTYPE_UC.
- **A trava dura não depende de onde ficam o anel e as fences.** A D2 sai das causas suficientes, como já tinham saído CG.1, 4b, 4c e M1b.1.
- A LC.1 não foi exercitada (não houve teardown).

## 3. Decisão do Daniel (05/10)

> "podemos testar quantas vezes quisermos, então o melhor dos mundos é ter um driver perfeito igual o do linux, o amdgpu"

A estratégia muda:
- **antes:** caçar a causa por eliminação, economizando corridas;
- **agora:** **paridade com o amdgpu**, uma diferença por corrida, sem economizar corridas.

A Lei 10 (critério antes de cada corrida) continua valendo.

## 4. Inventário de paridade

**Já alinhado (P0 ou P1 com prova):**
- firmware idêntico (doc 552);
- clock e power gating como o POST deixa (CG.1);
- SMU gerenciada pelo firmware (doc 556);
- PTE de VM gráfica escrita pelo SDMA (4c.4);
- memória de usuário no segmento 2, como o GTT (M1b.1);
- anel, writeback e fences do SDMA0 em GTT pela GART da VMID 0, com uma tabela só e MTYPE_UC (D2 + V0.1 + UC.1);
- `MX_L1_TLB_CNTL.MTYPE=UC`.

**Ainda diferente (ordem proposta: o que o Linux nunca faz em regime e é mais barato tirar vem primeiro):**

| # | Diferença | Linux (P1) | Nós | Custo |
|---|---|---|---|---|
| **P1** | **Acesso da CPU à VRAM e ao MMIO por instrumentação, em regime** | nenhum: o kernel não lê VRAM pela BAR nem amostra registradores em laço | migalhas DX.66 gravadas na VRAM a cada acesso de MMIO, a amostragem `BYTE_PROOF` (256 dwords da VRAM por Present, `VRAM_SAMPLE_LOOP`), o batimento do rastro lendo GRBM/CP a cada 100 ms, a leitura de verificação do HDP a cada retirada | baixo: chaves de desligamento |
| P2 | WPTR do SDMA0 por doorbell | `ring->use_doorbell = true` (`sdma_v5_0.c`) | a confirmar; suspeita de escrita MMIO do WPTR | médio |
| P3 | Conclusão por interrupção | anel IH + MSI, `trap_irq` (`sdma_v5_0.c:1422`); 37.701 interrupções na L-B | polling de 1 ms por timer/DPC lendo MMIO | alto |
| P4 | SDMA1 | usado | parado | médio |
| P5 | Estrutura da VM | 4 níveis, 48 bits | 2 níveis, 34 bits | alto |
| P6 | MTYPE de BO coerente da RADV no segmento 2 | `COHERENT/UNCACHED` → UC (`gmc_v10_0_get_vm_pte`) | sempre NC | baixo/médio |

**Por que P1 primeiro:** a assinatura das 11 travas é "GPU ociosa e máquina morta entre duas observações", e as travas aparecem em rajadas de paginação. Na BC-250, CPU e GPU dividem a mesma GDDR6. O tráfego que o Linux não tem e nós temos, com mais volume, é a CPU acessando a VRAM e o MMIO pela BAR para instrumentação. A 4b tirou só uma fonte (a leitura do buffer linear pela UMD).
- **Custo da P1:** a corrida fica mais cega (sem migalhas na VRAM nem amostragem). As migalhas podem ir para a RAM em vez de sumir, o que preserva parte da observação.
- **Implicação dos dois desfechos:** se não travar, reintroduzimos a instrumentação item por item até travar. Se travar, a instrumentação sai das causas e seguimos para P2.

## 5. Pendências

- **Daniel:** recuperação, reset do fusível e boot frio.
- **Eu (offline):** levantar no código cada acesso de instrumentação à VRAM e ao MMIO em regime, para a P1, e confirmar como o WPTR do SDMA0 é publicado, para a P2.
- **NÃO VALIDADO EM HARDWARE:**
  - 600 s sem trava com 256 MB;
  - a LC.1 no teardown.
