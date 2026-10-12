# 675 — Corrida 254: C3 (VRAM só em WC) ativa e trava dura ao abrir o menu. Desfecho B

**Data/hora:** 09/10/2026, bind às 10:51:52, trava por volta de 10:52:44, reboot às 10:53:18 (Kernel-Power 41 às 10:53:24).
**Candidato:** lab **3.400.1.0** (SYS `452C12FD5EF1F379…`, conferido no `binding-provenance.json`), UMD `B73F9BDA`, RADV `B4AE2E14`.
**Evidência:** `docs/evidence/2026-10-09_10-51-52-DESKTOP-HQAG6T4-lab-binding/`.
- `display-log-this-bind-RECUPERADO.txt`: resgatado do log cumulativo pelo offset do baseline, SHA-256 `51bc02c9638b6898…`.
- `b.txt`: o bind cortado antes do reboot; `b2.txt`: só a sessão da corrida.
- `paging-trace-this-bind-RECUPERADO.bin`.
- `monitor-corrida.csv`.
- `umd-logs-pos-travamento/`.
**Critério:** doc 674 §4.

## 1. Pré-condição (completa, P0)

| Item | Medido |
|---|---|
| `[P1B_VRAM_DRAM] ATIVO` | sim: BAR 0xC0000000 → DRAM 0x460000000, alias provado por escrita |
| `[C3_VRAM_WC]` | `chave=1 P1b=1 mapeamentos WC da DRAM pelo KMD ate aqui=8` (≥ 4) |
| `[C1_GFX_DOORBELL] ATIVA` | `CONTROL=0xC0000458 HIT=1`, prova por 4 NOPs |
| Desktop | 662 `Present #`, 455 `COMPOSITE_BLT OK`; o DWM, a Pesquisa e o menu apresentaram com `hr=0` |
| `GFXHUB_FAULT` | 0 |

A única divergência registrada foi a de sempre, `[G1_GOLDEN] RMI_SPARE` (bit 0 não grava). Ela aparece igual na 253.

## 2. O que aconteceu

**Volume de trabalho:**
- 2.625 IBs do SDMA0 executados. A última submissão (`dxg_fence=2625`) foi aposentada (`FenceId=2625`).
- 540 submissões do GFX, todas aposentadas (`GfxRetire` com `UltimaFence=541`).
- Zero TDR, zero timeout, zero fault.

**C3 sem efeito colateral:**
- O GFX, o MEC, a GART e o SDMA0 funcionaram com todos os mapeamentos da DRAM pelo KMD em WC.
- Não houve preâmbulo travado, fence sem fechar nem divergência de readback.
- Logo, o desfecho D não ocorreu.

**Duração e régua:**
- ~42,5 s de rastro de paginação (QPC do primeiro ao último registro).
- 407 FILLs, 49 despejos e 6.885 FLUSH_TLB.
- Comparação: 253 com 255 FILLs/~40 s; 252 com 831/~124 s; mediana histórica ~793.

**A trava:**
- O menu Iniciar (`StartMenuExperienceHost`, pid 6916) carregou a UMD às 10:52:42.177.
- A última linha dele saiu às 10:52:43.956: `MapAllocation`/`MakeResident` com `E_PENDING` e um `WAIT_PAGING` ENTER (fence de paginação 7080).
- O DWM e a Pesquisa apresentavam normalmente até 10:52:43.966.
- O KMD termina numa rajada normal de `PAGING_DDI` operação 12 (FLUSH_TLB), com a GPU ociosa e nada pendente.
- É a mesma assinatura das corridas 252 e 253: a máquina morre entre duas observações (doc 582).

**Plataforma (monitor):**
- Tctl entre 81,1 e 83,2 °C, abaixo da 253.
- Último registro do monitor às 10:52:42: 113 IBs/s, 3D 4,3 %, Copy 22,5 %.

## 3. Veredito

**Desfecho B.** Pré-condição completa e trava dura de sempre, com a GPU ociosa, ao abrir o menu.
- O tipo de memória misto (UC no KMD, WC no VidMm e no display) **sai das causas suficientes**.
- A C3 fica: é paridade com o `ioremap_wc` do `amdgpu` e não custa performance.

## 4. Relato (5 pontos)

1. **Houve avanço?** Não funcional. Diagnóstico sim: mais uma diferença com o Linux eliminada, e a C3 provou que não quebra nenhum caminho.
2. **Sucessos:**
   - C3 armada (8 mapeamentos WC, zero UC).
   - Desktop desenhado e menu apresentando.
   - 2.625 IBs do SDMA0 e 540 do GFX aposentados.
   - Zero TDR, fault e timeout.
   - C1 e P1b em regime; Tctl ≤ 83,2 °C.
3. **Falhas:**
   - Trava dura aos ~43 s de paginação, ~1,8 s depois de o menu carregar.
   - Placa em Código 43 (`CM_PROB_FAILED_POST_START`) pelo fusível depois do reboot.
4. **Veredito:** falha frente ao objetivo (600 s). O critério da corrida foi respondido com B.
5. **Recuperação e pendências:**
   - Driver ativo ao final: o nosso, em Código 43. O rollback do watchdog não rodou (trava dura).
   - Antes de novo bind: `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset` e boot frio.
   - **NÃO VALIDADO EM HARDWARE:** a causa da trava.
   - Próximo item da lista do doc 671: **C4** (zeramento por `CONST_FILL` em fatias de 4 MiB, como `amdgpu_fill_buffer`). A segunda corrida do teto não é necessária: B é conclusivo para o critério.
