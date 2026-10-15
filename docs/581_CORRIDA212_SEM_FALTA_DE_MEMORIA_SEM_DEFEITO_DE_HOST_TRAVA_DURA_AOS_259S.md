# 581 — Corrida 212: sem falta de memória e sem defeito de host, a trava dura voltou aos ~259 s

**Data:** 04/10/2026, bind às 16:07:28 · **Candidato:** lab **3.360.1.0** · **Critério:** [doc 579](579_M1B1_LIGADA_RADV_NO_SEGMENTO2_E_CRITERIO_CORRIDA211.md) §3, mais o sinal do [doc 580](580_CORRIDA211_M1B1_ATIVA_TELA_AZUL_POR_RECUSA_DE_IB_DIVIDIDO_E_PEDACOS_DE_ANEL.md)
**Evidência:** `docs/evidence/2026-10-04_16-07-28-DESKTOP-HQAG6T4-lab-binding/`
- log recuperado de 12 MB, 95.423 linhas;
- logs da UMD e da RADV;
- trace de paginação.

O coletor rodou antes da recuperação. Não há dump: tela preta e reinício, a assinatura da trava dura.

## 1. Medido (P0)

| Sinal | 212 | 211 | 210 |
|---|---|---|---|
| `[M1B_RADV_SISTEMA]` | 18 (amostrado) | 20 | — |
| `MakeResident STATUS_NO_MEMORY` | **0** | 0 | 211 |
| Instâncias do `dwm.exe` | **1** (pid 1276, 16:07:38 → 16:11:57) | 1 | 4 |
| `vkAllocateMemory res=-2` (lado da RADV) | 36 | 3 | 480 |
| IB dividido em **pedaços de anel** | 4 envios, **zero `RECUSADO`**, zero `EXCEDENTE`, zero `ORFAO` | — | — |
| `IB_TRIPOD TIMEOUT` / `TDR_QUIESCE` / `GFXHUB_FAULT` / `accepted=0` | 0 / 0 / 0 / 0 | — | — |
| IBs de paginação / submissões gráficas / Present | 4.825 / 1.185 / 1.298 | — | — |
| `COMPOSITE_BLT OK` / `Present RECUSADO 0xC01E0200` | 943 / 0 | — | — |
| Tctl | máximo 83,4 °C, último 80,5 °C | — | — |
| Duração até a trava | **~259 s** | ~380 s (0x119, defeito meu) | ~228 s |

- **A correção dos pedaços de anel funcionou no hardware (P0):** quatro envios precisaram de mais de um pedaço, e todos foram executados.
- **Os 36 `res=-2` não têm `NO_MEMORY` por baixo e ninguém perdeu o device.** O resíduo do lado da RADV ficou maior que na 211. Isso é H, e fica para investigar.

## 2. A trava

**Último segundo (BPG1):** 7 despejos (6,9 MB), 21 fills e 269 `FlushTlb`.

**Últimas operações:**
1. `TRANSFER` de 12,6 MB e de 2,6 MB para fora da VRAM;
2. `FILL_POR_COPIA` de uma alocação **nova** de 3,4 MB;
3. o início de uma submissão do Node 0.

É a assinatura de sempre (202–212): **zeramento de alocação nova no meio de uma rajada de paginação**.

**As duas preempções do Node 1 bem no fim não são sinal:** a 210 não teve nenhuma, e na 207 e na 208 elas estavam longe da trava.

## 3. Veredito

**Desfecho B do doc 579.**
- A M1b está ativa, com zero `NO_MEMORY` e um só DWM.
- O desktop foi desenhado.
- A trava dura veio com a assinatura de sempre.

**A trava é independente da falta de memória** (P0, agora com n=2: a 207 sem `NO_MEMORY` e a 212 sem `NO_MEMORY`). **Também é independente** da CPU tocar nas tabelas de página (4c inteira) e dos defeitos de host corrigidos na 209 e na 211.

**Avanço funcional (P0):**
- a cascata do DWM está resolvida (n=2);
- o IB dividido ficou robusto.

**O que não avançou:** a trava, que segue com causa desconhecida.

## 4. O que já foi eliminado como causa suficiente (com 256 MB)

1. SDMA1
2. MGCG do SDMA0 (CG.1)
3. `CONST_FILL` (X2b)
4. leitura da VRAM linear pela CPU (4b)
5. invalidação de TLB pela CPU (4c.2)
6. leitura de tabela de página pela CPU (4c.3)
7. escrita de PTE pela CPU (4c.4)
8. falta de memória e cascata do DWM (M1b.1)

**Diferenças com o Linux que sobram** (L-B, doc 552): fences por interrupção (nós fazemos polling), SMU e SDMA1 ativo.

**NÃO VALIDADO EM HARDWARE:** 600 s sem trava com 256 MB, e a causa da trava.
