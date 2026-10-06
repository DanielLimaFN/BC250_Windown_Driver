# 595 — Corrida 219: P1 válida, trava igual; a coleta automática funcionou; teste de RAM

**Data:** 05/10/2026, bind às 11:08:39; reinício às 11:11:44.
**Candidato:** lab **3.367.1.0** (SYS `B47B6170…`).
**Critério:** [doc 594](594_P1_SILENCIO_DA_BAR_COLETA_AUTOMATICA_NO_BOOT_E_CRITERIO_CORRIDA219.md) §3.
**Evidência:** `docs/evidence/2026-10-05_11-08-39-DESKTOP-HQAG6T4-lab-binding/`:
- `display-log-219-bind.txt` (o trecho do bind);
- `display-log-this-bind-RECUPERADO.txt`;
- `paging-trace-decodificado.json`;
- `reset-reason-pos-trava.txt`;
- `coleta-automatica.json`;
- `ramtest-2026-10-05_11-17-08.txt`.

## 1. Medido (P0)

**Pré-condição de validade: cumprida.**
- `[DX66_MIGALHA] NAO ARMADA (P1 ...)` no log;
- `BYTE_PROOF SKIPPED (P1 ...)` nos Presents 1, 5, 10 e 25;
- 1.076 linhas `SAMPLE_UNAVAILABLE`: o Present seguiu copiando sem amostrar;
- `gpu_heartbeat` vazio no rastro decodificado (nenhuma leitura de registrador do rastro);
- `gpu_events_ram` com **6.191 eventos**: os `BEV1` chegaram ao disco;
- a base da 218 se manteve: `D2_ANEL_SISTEMA ATIVO` (writeback confere) e as duas linhas do MEC na GART.

**Carga até a trava:**

| Contagem | Valor |
|---|---|
| `IB EXECUTADO` | 4.060 |
| `IB_TRIPOD TIMEOUT` / TDR | 0 / 0 |
| `GFXHUB_FAULT` / `NO_MEMORY` | 0 / 0 |
| Envios forçados | 3 (todos resolvidos) |
| `Present` | 1.397 |

**Trava dura** no vsync #7.800 (~139 s), ou `writer_tick` 1.171 (~117 s de rastro).
- **Os últimos 12 eventos** são `SUBMIT_GFX`/`RETIRE_GFX` alternados (fences 1486–1491), todos retirados. Nada ficou em voo.
- **O rastro** continuou vivo 768 ms depois da última paginação.
- **Paginação leve no fim:** no último segundo, 1 despejo de 64 KB e 26 `FlushTlb`. **Não havia rajada de paginação.** As travas anteriores "no zeramento" não se repetem aqui; o vínculo com a paginação enfraquece.
- **`RESET_REASON=0x40080400`**, idêntico às 12 travas anteriores.

## 2. Veredito pelo critério do doc 594

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| Pré-condição | sim | — |
| A — 600 s sem trava | não | trava em ~139 s |
| **B — trava com a mesma assinatura** | **sim** | GPU ociosa, último evento retirado, mesmo motivo de reset |
| C — regressão funcional | não | o Present seguiu, zero timeout |
| D — StartDevice/D2 falham | não | — |
| E | sim, parcial | o fim da corrida foi composição gráfica, sem paginação; é a primeira trava fora de uma rajada de paginação |

**Veredito:** a P1 é **válida** e a trava continua. O acesso de instrumentação à VRAM e ao MMIO **sai das causas suficientes**.

## 3. Coleta automática no boot: funcionou (P0)

No boot das 11:11:44, a tarefa rodou sozinha. A transcrição está em `C:\BC250_COLETA_AUTOMATICA.log`.
- Viu o desligamento sujo;
- coletou a pasta da 219 (motivo do reset, logs da UMD, caudas do log e do rastro, decodificação, SHA-256);
- terminou às 11:13:40 com `exit=0`;
- gravou `coleta-automatica.json`.

## 4. Teste de RAM (pedido do Daniel)

**Motivo:** na BC-250, a RAM do Windows e a VRAM são a mesma GDDR6. A assinatura das travas (GPU ociosa, máquina morta entre duas observações, sem MCE) é compatível com instabilidade de memória.

**`tools/bc250-ramtest/` (`bc250_ramtest.c`, `build.bat`, `out/bc250_ramtest.exe`):**
- aloca 70% da RAM livre (teto de 12 GB);
- escreve com todas as CPUs ao mesmo tempo (16 threads) oito padrões: endereço, inverso, zeros, uns, xadrez 55/AA, um bit andando e pseudoaleatório;
- confere palavra por palavra e registra endereço, esperado, lido e bits trocados;
- exit: 0 sem erros, 1 erros, 2 não rodou.

**`--selftest`** injeta 1 bit trocado de propósito. **Verificado:** saiu exatamente 1 erro, no bit 3 e no endereço certo, com veredito FALHA. Ou seja, o teste pega erro.

**Integração:**
- a coleta automática agora roda o teste logo depois de coletar (`ramtest-pos-trava.txt` na pasta da corrida, e `ramtest_exit` no `coleta-automatica.json`);
- avulso: `tools\rodar-teste-de-ram.ps1`, sem elevação.

**Resultado neste boot, logo depois da trava da 219 (P0 no escopo):** `SEM ERROS`, 8.025 MB × 2 passadas, de 15,7 a 34,5 GB/s.

**Limites declarados:**
- o teste dura segundos;
- cobre a RAM do Windows, não o pedaço de 256 MB da VRAM;
- roda com a GPU parada.

Ele **não** exclui um defeito que só aparece com GPU e CPU disputando a memória ao mesmo tempo. Uma versão sob carga da GPU é possível depois.

## 5. Inventário de paridade (doc 593 §4), atualizado

- **Saíram das causas suficientes:** CG.1, 4b, 4c, M1b.1, D2 (GART, UC) e **P1** (instrumentação pela BAR).
- **Seguem diferentes:**

| # | Diferença |
|---|---|
| **P1b** | HDP flush por MMIO dentro dos laços de espera, com a D2 já ativa (o Linux não faz) |
| **P2** | WPTR do SDMA0 por doorbell |
| **P3** | conclusão por interrupção (o polling de 1 ms ainda lê o RPTR por MMIO) |
| **P7** | anel e fences do GFX em GTT (hoje na VRAM pela BAR; é o que estava rodando na hora desta trava) |
| P4 | SDMA1 |
| P5 | VM em 4 níveis |
| P6 | MTYPE de BO coerente da RADV |

## 6. Pendências

- **Daniel:** recuperação, reset do fusível e boot frio.
- **Eu:** escolher e preparar o próximo item do inventário.
- **NÃO VALIDADO EM HARDWARE:**
  - 600 s sem trava com 256 MB;
  - a LC.1 no teardown;
  - memória estável sob carga simultânea de GPU e CPU.
