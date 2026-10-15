# 648 — RLC.1 sobre a G.1: repetição da pergunta da 244; critério da Corrida 247

**Escrito antes do ensaio (Lei 10), 07/10/2026, offline.** Continua o [doc 647](647_CORRIDA246B_G1_COM_KD_TRAVA_AOS_5MIN_A_246_FOI_DISPERSAO_E_O_VKD3D_NO_WEBVIEW.md) §5.1.

## 1. Mudança (uma variável)

`BC250_RLC1_RUNNING_LIKE_AMDGPU` volta a **1**, sobre a base da 246/246b: G.1=1, P3.3=0, MEC.1=1 e GFXRST.1=1.

A ordem no StartDevice fica como no `gfx_v10_0_hw_init` do amdgpu:
1. goldens (G.1);
2. trecho do PSP: SDMA0, SDMA1 e RLC_G;
3. `rlc_resume`;
4. PFP direct load, que equivale ao `cp_resume`.

O código da RLC.1 é o mesmo da 244 (doc 642).

| Artefato | Valor |
|---|---|
| Pacote | **3.391.1.0** |
| SYS lab | `047DAB5E…` |
| SYS assinado | `58EEB2FB…` |
| INF | `65E97E6A…` |
| CAT | `B91B57B3…` |
| UMD | `8782A5E5…` (a mesma) |
| RADV | `B4AE2E14` (a mesma) |
| Auditorias de pacote e de segurança | PASS |
| Pacote 3.390 (246) guardado em | `driver\wdk\package\output_lab_3.390.1.0_246\` |
| Script | `tools\EXECUTAR_CORRIDA247_RLC1_SOBRE_G1_MENU_600S.cmd` |

**Sem depurador de kernel.** O preflight recusa o KD, e esta corrida mede tempo; ver doc 647 §6. O roteiro é o mesmo: só o menu até o rollback. O Microsoft 365 Copilot fica fechado durante a corrida inteira, porque relança o WebView2.

## 2. Pré-condição de validade

1. `[G1_GOLDEN] RESUMO ... ARMADO`.
2. `[RLC1] boot (depois do PSP): ATIVO` com `EXEC_ROM_CODE=0`.
3. `[RLC_ESTADO] boot` com `RLC_ENABLE_F32=1`.
4. `rptr_por_doorbell=0`.
5. Nenhum salto de QPC acima de 3 s no log do KMD, ou seja, nenhuma pausa externa.

## 3. Desfechos

Base sem RLC: 245 ≈ 289 s e 246b ≈ 228 s de execução; histórico 45–584 s.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | Pré-condição OK e a máquina passa de **60 s** (trava depois disso ou 600 s) | A 244 (~40 s) foi dispersão. A RLC.1 **fica**, por paridade. Com o RLC rodando, o safe mode e o reset do GFX passam a ter quem responda |
| B | Pré-condição OK e trava em **menos de 60 s** | Com n=2 (244 e 247), o RLC rodando encurta a vida da máquina. A RLC.1 sai. Próximo: RLC.1b, com o handshake RLC↔SMU desligado (bit 23 de `RLC_PG_CNTL`) |
| C | `[RLC1] RECUSADO` ou PSP com `Status != 0` | O ensaio não aconteceu; registrar o motivo |
| D | Regressão no boot: StartDevice, PFP direct load ou desktop | Desligar e investigar |
| T (soma) | Houve TDR do GFX | Ler as linhas `[GFXRST1]` e `[RLC1] pos-reset`, e contar `outros` do IH depois do reset (doc 642 §4) |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** 1 corrida. Se der B, a 244 e a 247 juntas decidem.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc.

## 4. Variante com KD (decisão do Daniel, 08/10)

Lançador: `tools\EXECUTAR_CORRIDA247_RLC1_SOBRE_G1_COM_KD_MENU_600S.cmd`. Ele grava as opções de boot e libera o preflight com `BC250_ALLOW_KERNEL_DEBUG=1`.

A pré-condição 5 deixa de valer. As pausas do depurador aparecem como saltos de QPC (doc 647 §6) e são **descontadas**: o desfecho usa o **tempo de execução real** (QPC total menos as pausas), com o limite de 60 s.

Pausas que caírem nos 60 s antes da trava ficam registradas como fator possível, não excluído.
