# 523 — X2a: FILL de paginação fatiado em 256 KiB e migalhas nos acessos à VRAM; critério da Corrida 170

> 29/09/2026, offline. Candidato **3.315.1.0** (inclui HDP.1, T3.1, EVT.1 e X1). **Nada desta faixa foi validado em hardware.**

## 1. Por quê

- **11 de 11 travas** de 28 e 29/09 terminam com um FILL de paginação no SDMA. Nas duas últimas (168 e 169) isso é P0: o anel EVT.1 mostrou o submit do IB do FILL sem conclusão.
- A TRANSFER, fatiada em **256 KiB** ("fluxo estável via PCIe/GFXHUB"), nunca foi o último trabalho de uma trava, embora seja ~40% dos movimentos.
- O FILL ia em pacotes CONST_FILL de até **4 MiB−4**: rajada só de escrita na DRAM que a GPU divide com a CPU.
- **H:** o doc 501 já refutou o tamanho do FILL como causa das paradas de SDMA de 151 e 153. Aqui a hipótese é outra (trava da plataforma, não do SDMA) e continua sem prova.
- A 169, sem HDP nas esperas, durou 227 s (recorde) e travou de novo com FILL. Depois do último `0xFC4` da CPU00 havia acessos à VRAM **sem migalha**: canários e anel.

## 2. A mudança

1. **Uma variável de comportamento:** `Bc250McdmBuildFillVirtual` fatia em `BC250_SDMA_PAGING_FILL_MAX_BYTES = 256 KiB`, como a TRANSFER.
   - Mesmo total, mesmo pacote e mesmo destino; só mais pacotes por IB.
   - Uma primária de 11.075.584 bytes vira 43 pacotes (215 dwords), dentro do paging buffer de 64 KiB.
   - Buffer pequeno usa o multipasse legal (`INSUFFICIENT_DMA_BUFFER` + `MultipassOffset`).
2. **Migalhas, só instrumentação:**
   - `VRAM_CANARY_READ`: canários lidos depois da conclusão (`PollRetirement`);
   - `VRAM_CANARY_WRITE`: canários plantados antes do submit (3 sítios);
   - `VRAM_RING_WRITE`: toda cópia de pacotes para o anel na VRAM (9 sítios, inclusive os blts do Present).

## 3. Provas offline

- `mcdm_callbacks_test` reescrito para a semântica nova, sem afrouxar:
  - o primeiro passo do FILL de 11.075.584 bytes em buffer de 64 DW devolve `INSUFFICIENT_DMA_BUFFER`, com 12 pacotes de exatamente `262143` na contagem, 4 NOPs e `MultipassOffset = 12 × 256 KiB`;
  - a faixa de destino publicada é só a deste passo;
  - a publicação de dwords úteis é 60 de 64;
  - os passos seguintes cobrem **exatamente** 11.075.584 bytes, em 4 passos (12+12+12+7), nenhuma fatia acima de 256 KiB e cada passo múltiplo de 8 DW;
  - o caso fail-closed de publicação divergente foi mantido.
- `mcdm_callbacks_test`, `blt_proof_contract_test`, `paging_contract_test` e `sdma_packet_builder_test`: **EXIT 0**.
- Auditoria de segurança: **PASS**, com o gate novo `audit_x2a_paging_fill_sliced_and_vram_crumbed`.
- **4 mutações, todas reprovaram**, e a auditoria voltou a PASS (`scratchpad/mutate_x2a.py`):
  - Y1: o FILL volta a 4 MiB;
  - Y2: o teste deixa de exigir o teto por fatia;
  - Y3: uma cópia para o anel sem migalha;
  - Y4: canários lidos sem migalha.
- Build de laboratório EXIT 0 e auditoria do pacote **PASS**. Strings conferidas no SYS assinado.
- `tools/prepare-corrida170-x2a.ps1`: EXIT 0.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `9D6C0725E691ADD94BCDE7F6615F1A7CA632BF9A946DBAD9FA94B9734F46861D` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `61AD3923BF479A8906773B058C3CE2FF7526EC7961F34B0B5C7F37502B91F489` |
| `bc250mcdm.cat` | `511622BB7F57CE47E6D8CF6F19C3DB5C237C0B5FDC18CDFD15A765E09062DD97` |

- DriverVer `3.315.1.0` (fonte `3.315.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA170_X2A_600S.cmd`. Mesmo roteiro, T6 revertida.

## 5. Critério da Corrida 170 (escrito antes; Lei 10)

**Base:** uma limpa não prova nada; duas seguidas são o mínimo. A 169 durou 227 s: travar depois de 200 s já não é inédito. Uma trava diz "não basta".

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava **e** linhas `[DX91_PAGING_MOVE] op=FILL` com `DW` compatível com fatias de 256 KiB (primária de 11 MB com `DW=215`) **e** sem regressão | Primeira limpa; repetir idêntico. Duas seguidas: a rajada do FILL grande era o gatilho. |
| **B1** | Trava, com o anel mostrando de novo o IB de um `PAGING_FILL` sem conclusão, e nenhuma migalha de VRAM em fase ANTES | Fatiar não basta: o FILL continua no centro, independentemente do tamanho do pacote. Próximo: X2b (zerar por cópia de uma página de zeros) ou examinar destino e PTE do FILL. |
| **B2** | Trava com migalha `VRAM_RING_WRITE`, `VRAM_CANARY_READ`, `VRAM_CANARY_WRITE` ou `VRAM_FENCE_READ` em fase **ANTES** | A CPU parou num acesso à VRAM pela BAR. O sítio e o offset dizem qual. O próximo passo é esse acesso. |
| **B3** | Trava sem FILL no anel perto do fim (último trabalho é TRANSFER, blt ou GFX) | O fatiamento mudou o padrão. Descrever o novo último trabalho e seguir por ele. |
| **C** | Regressão: `IB_TRIPOD TIMEOUT`, TDR, `0x10E`/`0x119` (VidMm recusando o multipasse), FILL com `SDMA encode failed` | O multipasse ou o tamanho do IB quebrou algo. Corrigir antes de repetir. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** A exige o `DW` das linhas de FILL mostrando o fatiamento ativo.

## 6. Resultado da Corrida 170 (29/09/2026, bind 09:19:46, pacote 3.315.1.0)

**Evidência:** `docs/evidence/2026-09-29_09-19-46-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 3.823.335 bytes, SHA-256 `0EB2212A…645C`. Evento 41 às 09:21:29. **Trava em 61,1 s.**

**Fatiamento ativo (P0):** FILLs de 11.075.584 bytes com `DW=215` e de 11.796.480 com `DW=225`, zero `SDMA encode failed`, e nenhum multipasse necessário, porque o paging buffer real comporta.

**Veredito: B2, e é a primeira vez que a migalha pega um acesso pendente.**

```
[DX66_MIGALHA] CPU00 camada=INTERNA seq=131822 sitio=VRAM_CANARY_READ detalhe=0x00F280 fase=ANTES(acesso NAO concluido) <- ULTIMA DA MAQUINA
[DX66_MIGALHA] CPU07 camada=PAGINACAO seq=131810 sitio=PAGING_DDI detalhe=0x00000C fase=ANTES(acesso NAO concluido)
```

- **P0:** a CPU00, na aposentadoria de um IB do SDMA, entrou na leitura dos canários do tripé **pela janela WC da VRAM** (página `0xF280` → offset de VRAM `0xF280000`, dentro do segmento do VidMm) e **a leitura nunca terminou**. É a última operação da máquina.
- **Ao mesmo tempo (anel EVT.1, 22 válidos):** o último evento é o `SUBMIT_SDMA` de 192 bytes (fence 1030), que é o IB de um `PAGING_FILL` de 2.273.280 bytes (`op=0x2D` = 45 dwords, 48 × 4 = 192) montado 1,86 ms antes, sem conclusão. **O SDMA começava a escrever 2,27 MB enquanto a CPU00 lia VRAM pela BAR.**
- A CPU07 estava dentro de uma DDI de paginação FLUSH_TLB (seq 131.810, recente), sem MMIO pendente na camada interna: provavelmente esperando um lock que a CPU00 segurava. A leitura dos canários acontece com o `RingLock` seguro.

**O que isso muda:**
- Nas 12 travas, a última migalha "concluída" `0xFC4` era compatível com um acesso seguinte sem migalha. Agora esse acesso tem nome: **uma leitura da VRAM pela BAR feita pela CPU enquanto o SDMA escreve em rajada**.
- É n=1 para o sítio exato. Os outros leitores de VRAM pela BAR (caminhada de tabela de página no submit, amostragem e prova de bytes do Present) são a mesma classe e continuam ativos.

**X2a não basta** (trava com o fatiamento ativo). Mantido, porque não há regressão: 1.029 `IB EXECUTADO`, 975 `TRIPE_PROVADO`, 288 Presents, zero timeout, fault e TDR.

**Recuperação:** Código 43 e fusível TRIPPED esperados: `BC250_RECOVERY.cmd`, `-Reset` e boot frio.
