# 639 — Mineração da P3.3: as taxas não mudam; a faixa de doorbell do MEC é diferente da do amdgpu

**07/10/2026, offline, zero corridas.** Pergunta do [doc 638](638_CORRIDA242B_P33_EM_REGIME_E_TRAVA_DURA_EM_20S_P33_DESLIGADA.md) §4: o que muda quando a P3.3 acelera a trava?

## 1. Taxas por segundo (P0, logs do KMD por QPC)

Script: `scratchpad/minera_taxas.py`. Eventos com `qpc=`: `KMD_SDMA_SUBMIT` e `PAGING_DDI ENTER`; IH pelas linhas de regime.

| Corrida | P3.3 | Duração do log | Pico SDMA0/s | Pico paginação/s | IH/s (última janela) |
|---|---|---|---|---|---|
| 231–236 | não | 41–584 s | 335–488 | 877–1.224 | 29–147 |
| 239 | não | 548 s | 484 | 1.264 | 193 |
| 240 | não | 421 s | 483 | 1.245 | 174 |
| 241b | não | 321 s | 399 | 934 | 17 |
| 242 | **sim** | 7 s | 447 | 1.098 | n/d |
| 242b | **sim** | 14 s | 255 | 721 | 169 |

**Leitura:**
- A 242 e a 242b morreram durante o pico de partida do desktop, com taxas iguais ou menores que as das corridas sem P3.3.
- Todas as corridas sem P3.3 atravessaram esse mesmo pico.
- **A hipótese "mais tráfego de doorbell/IH" (doc 638 §4) está refutada.** A P3.3 não aumenta o volume; ela muda **para onde** vai uma escrita: a doorbell do IH, no byte `0xBC0` da abertura.

## 2. Achado de paridade: a faixa de doorbell do MEC (P0 contra P1)

Probes da 241b e da 241 (lidos de volta):

```
CP_MEC_DOORBELL_RANGE_LOWER = 0x00000110
CP_MEC_DOORBELL_RANGE_UPPER = 0x0FFFFFFC
```

**O nosso driver nunca escreve esses registradores.**
- `bc250_compute_mqd_contract.c` só os lê e usa o LOWER como offset da doorbell da nossa fila (`0x110`, índice `0x44`).
- O valor é o que o firmware/POST deixou. A faixa vai de `0x110` até o fim da abertura.

**O amdgpu** (`inc/linux_ref/gfx_v10_0.c:6697`, `gfx_v10_0_kiq_init_register`):

```
CP_MEC_DOORBELL_RANGE_LOWER = (doorbell_index.kiq * 2) << 2           = 0x000
CP_MEC_DOORBELL_RANGE_UPPER = (doorbell_index.userqueue_end * 2) << 2 = 0x8A*8 = 0x450
```

A faixa do upstream cobre só as filas do CP (KIQ, MEC, filas de usuário). A nossa cobre também:
- a doorbell do SDMA0, em `0x800` (ativa desde a P2c, corrida 223);
- a doorbell do IH, em `0xBC0` (P3.3).

**Ressalva (P1):** o amdgpu também deixa `CP_RB_DOORBELL_RANGE_UPPER` no máximo (`gfx_v10_0.c:6081`), então uma sobreposição de faixa do CP sobre endereços do SDMA não é, sozinha, um problema. O BIF roteia pelas faixas dele (`BIF_SDMA0/IH_DOORBELL_RANGE`). Se o MEC também reage a uma escrita que o BIF entrega ao IH, ninguém mediu (H).

**O que fica:**
- A diferença é real e medida (P0 contra P1). É barata de corrigir e de testar.
- Hipótese (H) que liga os fatos: a escrita do RPTR do IH em `0xBC0`, ~170 vezes por segundo, cai dentro da faixa que o MEC reivindica. O MEC trataria essas escritas como doorbell de fila e acordaria o firmware sem fila correspondente. O mesmo valeria, em menor grau, para o WPTR do SDMA0 em `0x800`.
- A H não está provada. O `CP_RB_DOORBELL_RANGE` da nossa placa também não foi lido.

## 3. Ensaio proposto (MEC.1, uma variável)

Programar a faixa do MEC como o amdgpu, com snapshot, readback e rollback no teardown:
- `CP_MEC_DOORBELL_RANGE_LOWER = 0`
- `CP_MEC_DOORBELL_RANGE_UPPER = 0x450`

A doorbell da nossa fila (`0x110`) continua dentro da faixa. Também passam a ser lidos, só para registro, `CP_RB_DOORBELL_RANGE_LOWER/UPPER` (`0xC168`/`0xC16C`, gc_10_1_0_offset.h 0x1DFA/0x1DFB) e `CP_RB_DOORBELL_CONTROL`.

**O desenho que mais informa é com a P3.3 religada,** porque ela é o gatilho reproduzível (2 de 2 travas em menos de 25 s):
- sem trava precoce: o mecanismo da P3.3 está achado;
- trava precoce igual: a faixa do MEC sai das causas.

Uma corrida responde. Em qualquer dos dois desfechos, a faixa fica como no amdgpu, porque é paridade. Critério completo a escrever antes, se o Daniel aprovar.

**NÃO VALIDADO EM HARDWARE:** tudo deste doc além das leituras de probe citadas.
