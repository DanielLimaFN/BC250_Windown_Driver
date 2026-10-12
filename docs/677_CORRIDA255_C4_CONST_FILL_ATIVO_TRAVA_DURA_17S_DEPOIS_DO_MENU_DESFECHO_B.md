# 677 — Corrida 255: C4 (zeramento por CONST_FILL de 4 MiB) ativa e trava dura ~17 s depois de abrir o menu. Desfecho B

**Data/hora:**
- Bind às 11:26:16 de 09/10/2026; desktop às 11:26:24 (UMD carregada no `dwm.exe`).
- Menu carregado às 11:27:19,963; último sinal de vida às 11:27:37,3.
- Reboot às 11:28:55 (Kernel-Power 41 às 11:29:02).

**Candidato:**
- Lab **3.401.1.0**, SYS `A022A815F2C2362C…` (conferido no `binding-provenance.json`).
- UMD `B73F9BDA`, RADV `B4AE2E14`.

**Evidência:** `docs/evidence/2026-10-09_11-26-16-DESKTOP-HQAG6T4-lab-binding/`.
- `display-log-this-bind-RECUPERADO.txt`, resgatado pelo offset do baseline (SHA-256 `f4306b460c34ad4b…`).
- `b2.txt`, só a sessão da corrida.
- `paging-trace-this-bind-RECUPERADO.bin`.
- `monitor-corrida.csv`.
- `umd-logs-pos-travamento/`.

**Critério:** doc 676 §4.

## 1. Pré-condição (completa, P0)

| Item | Medido |
|---|---|
| `[C4_FILL]` | `chave=1 X2b=0 fatia_max=0x400000`; 1º FILL `Size=11075584 Pattern=0x00000000 DW=15` (3 pacotes, como previsto no teste) |
| `FILL_POR_COPIA` / `[X2B_FILL]` | 0 / 0 |
| FILLs em `op=FILL` | 585, todos com padrão 0. São 564 de um pacote (≤ 4 MiB) e 21 de três pacotes (tela cheia), 355.725.312 B zerados |
| `[C3_VRAM_WC]` | `chave=1 P1b=1 ... =8` |
| `[C1_GFX_DOORBELL] ATIVA` | `CONTROL=0xC0000458 HIT=1` |
| `[P1B_VRAM_DRAM] ATIVO` | sim |
| Desktop | 1.261 `Present #`, 982 `COMPOSITE_BLT OK`. O menu e a Pesquisa apresentaram com `hr=0` até o fim |
| `GFXHUB_FAULT` | 0 |

A única divergência foi a de sempre, `[G1_GOLDEN] RMI_SPARE`.

## 2. O que aconteceu

**Engines:**
- 4.136 IBs do SDMA0. A última submissão (`dxg_fence=4136`) foi aposentada (`FenceId=4136`).
- 1.133 submissões do GFX, todas aposentadas (`UltimaFence=1134`).
- Zero TDR, timeout, `INSIDE_IB` e fault. A C4 rodou sem defeito: o desfecho D não ocorreu.

**Duração e régua:**
- ~72,6 s de rastro de paginação.
- 561 FILLs, 246 despejos e 10.833 FLUSH_TLB.
- Comparação: 254 com 407 FILLs/~43 s; 253 com 255/~40 s; 252 com 831/~124 s; mediana histórica ~793.

**A trava:**
- O menu abriu às 11:27:19 e a última linha de todos os processos (DWM, Explorer, menu e Pesquisa) foi um `Present` com `hr=0`, às 11:27:36–37.
- O KMD termina numa rajada normal de `PAGING_DDI` operação 12 (FLUSH_TLB), com a GPU ociosa e nada pendente.
- É a assinatura de sempre (doc 582).

**Plataforma (monitor):**
- Tctl entre 82,5 e 85,1 °C.
- Última amostra às 11:27:36: 211 IBs/s, 33 Presents/s, 3D 12 %, Copy 38 %.

## 3. Veredito

**Desfecho B.** Pré-condição completa e trava dura de sempre, com a GPU ociosa.
- O zeramento (pacote, tamanho da fatia e tráfego) **sai das causas suficientes**.
- A C4 fica: é paridade com o `amdgpu_fill_buffer` e tem menos tráfego.
- **A lista C1–C4 do doc 671 está esgotada:**
  - C1, C3 e C4 foram feitas e provadas, e nenhuma é a causa.
  - C2 já estava igual ao Linux.

## 4. Relato (5 pontos)

1. **Houve avanço?** Não funcional. No diagnóstico, sim: a última diferença da lista foi eliminada, e a C4 funcionou sem defeito.
   - Esta corrida foi a mais longa das três últimas: 72 s e 561 FILLs, contra 43 s/407 e 40 s/255.
   - Isso cabe na dispersão de sempre (33–584 s) e não é atribuível à C4.
2. **Sucessos:**
   - C4 armada (585 FILLs por CONST_FILL, zero cópia de zeros).
   - C3, C1 e P1b em regime.
   - Desktop, menu e Pesquisa apresentando.
   - 4.136 IBs do SDMA0 e 1.133 do GFX aposentados.
   - Zero TDR, fault e timeout.
3. **Falhas:**
   - Trava dura ~17 s depois de o menu carregar (~73 s de corrida).
   - Placa em Código 43 (`CM_PROB_FAILED_POST_START`) pelo fusível.
4. **Veredito:** falha frente ao objetivo (600 s). O critério foi respondido com B.
5. **Recuperação e pendências:**
   - Driver ativo ao final: o nosso, em Código 43. O rollback não rodou (trava dura).
   - Antes de novo bind: `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset` e boot frio.
   - **NÃO VALIDADO EM HARDWARE:** a causa da trava.
   - Próximo: um novo inventário de diferenças com o `amdgpu`, porque a lista do doc 671 se esgotou. Nenhuma mudança foi preparada.
