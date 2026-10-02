# 524 — X3: a CPU não lê os canários na VRAM com o SDMA ocupado; critério da Corrida 171

> 29/09/2026, offline. Candidato **3.316.1.0** (inclui HDP.1, T3.1, EVT.1, X1 e X2a). **Nada desta faixa foi validado em hardware.**

## 1. Por quê

A Corrida 170 (doc 523 §6) pegou o acesso pendente pela primeira vez (P0):

```
CPU00 camada=INTERNA seq=131822 sitio=VRAM_CANARY_READ detalhe=0x00F280 fase=ANTES(acesso NAO concluido) <- ULTIMA DA MAQUINA
```

- A CPU00, aposentando um IB do SDMA, lia os canários do tripé **na VRAM pela BAR** (offset `0xF280000`).
- No mesmo instante, o SDMA recebia um FILL de paginação de 2,27 MB (anel EVT.1, submit sem conclusão).
- A leitura nunca terminou.
- As 12 travas de 28 e 29/09 têm o mesmo quadro: rajada de escrita do SDMA na DRAM compartilhada e uma CPU lendo VRAM pela BAR.

## 2. A mudança (uma variável de comportamento)

1. **`Bc250SdmaPresentPollRetirement`:** a leitura dos canários só acontece com o **anel ocioso**, isto é, o IB que acabou de terminar é o único em voo e o RPTR do registrador já chegou ao último WPTR publicado.
   - Caso contrário, a perna de dados daquele IB fica **NÃO CONFERIDA**: prova de duas pernas, declarada como `IB_ENGINE_2LEGS`, com `[X3_CANARY] #n perna de dados NAO CONFERIDA: SDMA ocupado …` amostrado.
   - Nunca simulada (Lei 2 e Lei 4).
   - Contadores de lidas e adiadas.
2. **Migalhas, só instrumentação**, nos outros leitores de VRAM pela BAR:
   - `VRAM_PT_READ`: entrada de tabela de página na caminhada (`Bc250ActiveReadVramU64`);
   - `VRAM_IB_READ`: inspeção de IB e cópia do IB do nó 1;
   - `VRAM_PROOF_READ`: amostras de prova do escape de composição (3 sítios).

**Escopo declarado:** só o SDMA entra na conta de "ocupado". O GFX pode estar escrevendo na VRAM durante a leitura. Os outros leitores (tabelas, provas do Present) continuam lendo com o SDMA ocupado; agora com migalha.

## 3. Provas offline

- `mcdm_callbacks_test`, `blt_proof_contract_test`, `paging_contract_test`, `sdma_packet_builder_test` e `active_miniport_test`: **EXIT 0**.
- Auditoria de segurança: **PASS**, com o gate novo `audit_x3_no_canary_vram_read_while_sdma_busy`.
- **4 mutações, todas reprovaram**, e a auditoria voltou a PASS (`scratchpad/mutate_x3.py`):
  - Z1: "ocioso" ignora outros IBs em voo;
  - Z2: o portão não zera a perna de dados;
  - Z3: leitura de tabela sem migalha;
  - Z4: adiamento calado.
- Build de laboratório EXIT 0 e auditoria do pacote **PASS**.
- Strings conferidas no SYS assinado: `X3_CANARY`, `VRAM_PT_READ`, `VRAM_IB_READ`, `VRAM_PROOF_READ` e `VRAM_CANARY_READ`.
- `tools/prepare-corrida171-x3.ps1`: EXIT 0.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `F73EDD27AEC6A69903FA884CE854560E35231299C602DDA5759883E27681DFA8` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `93395E10B0F1D0A2B614F1374C20EDAAFCBF26F50FBD27D03D0F45C4F434FD1D` |
| `bc250mcdm.cat` | `F8384B9E0B4A2DC1AB78679BD256F4B7D599D77FBD00F0A6597397AF2EEAA86F` |

- DriverVer `3.316.1.0` (fonte `3.316.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA171_X3_600S.cmd`. Mesmo roteiro.

## 5. Critério da Corrida 171 (escrito antes; Lei 10)

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava **e** `[X3_CANARY]` presente (o portão agiu) **e** tripé e Present sem regressão | Primeira limpa. Repetir idêntico. Duas seguidas: a leitura de canários pela BAR durante a rajada do SDMA era o gatilho. Terceira corrida para chamar de causa. |
| **B1** | Trava com a última migalha em **outro** leitor de VRAM em fase ANTES (`VRAM_PT_READ`, `VRAM_IB_READ`, `VRAM_PROOF_READ`, `VRAM_SAMPLE_LOOP`, `VRAM_COMPARE`, `VRAM_FENCE_READ`) | Confirma a classe (leitura de VRAM pela BAR durante a rajada) e troca o sítio. Próximo: estender o portão a esse leitor, ou tirar todas as leituras de VRAM da CPU das janelas de SDMA ocupado. |
| **B2** | Trava com `VRAM_CANARY_READ` em fase ANTES | O portão não pegou o caso: o SDMA estava ocupado de um jeito que o critério não vê (blt síncrono, anel com trabalho não registrado). Rever o critério. |
| **B3** | Trava sem nenhuma migalha de VRAM pendente | A classe não é suficiente. Descrever o anel de eventos e as migalhas antes de seguir. |
| **C** | Regressão: `IB_SEM_TRIPE`, TDR, timeout, perda de Present | Corrigir antes de repetir. |
| **D** | `[X3_CANARY]` ausente em corrida que dura | O portão nunca agiu (SDMA nunca ocupado no momento da leitura). A corrida não julga a hipótese. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** A exige `[X3_CANARY]` presente.
- **Reset em até 10 s:** o anel de eventos degrada rápido na trava.

## 6. Resultado da Corrida 171 (29/09/2026, bind 09:47:14, pacote 3.316.1.0)

**Evidência:** `docs/evidence/2026-09-29_09-47-14-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 3.769.236 bytes, SHA-256 `52FEF25B…3C7B`. Evento 41 às 09:48:43. **Trava em 53,8 s, no momento em que o Daniel abriu o menu Iniciar.**

**Assinatura (P0):**

```
CPU00 camada=INTERNA seq=390248 sitio=VRAM_CANARY_READ detalhe=0x00F3CC fase=ANTES(acesso NAO concluido) <- ULTIMA DA MAQUINA
CPU05 camada=PAGINACAO seq=390236 sitio=PAGING_DDI detalhe=0x00000C fase=ANTES
```

- Anel EVT.1 com 43 válidos: o último evento é o `SUBMIT_SDMA` de 96 bytes (fence 1019), o IB de um `PAGING_FILL` de 1.048.576 bytes (`op=0x14` = 20 DW, 24 × 4 = 96) montado 1,9 ms antes, **submetido pela própria CPU00**. O GFX estava concluído havia 59 ms.
- **`[X3_CANARY]` = 0:** o portão nunca encontrou o SDMA ocupado na hora de ler canários, **nesta corrida inteira**. O critério da 171 cai no desfecho **D**, e o **B2** vale como assinatura: a leitura travou mesmo com o anel ocioso.

**Leitura corrigida (a premissa do X3 estava errada):**
- A sequência na CPU00 é: submete o FILL, a engine termina rápido (1 MB), a aposentadoria vê RPTR e fence, o anel está ocioso e a CPU lê **os canários que estão dentro do destino que o FILL acabou de escrever**. Essa leitura pela janela WC não termina.
- Na 170 foi igual: FILL de 2,27 MB submetido e a CPU00 lendo canários (`0xF280000`) logo em seguida.
- **Não é concorrência com o SDMA trabalhando:** é **a CPU lendo pela BAR, logo depois do flush e da invalidação do HDP, uma região que o SDMA acabou de escrever**.
- A mesma CPU lê a fence (mapeamento **UC**, faixa privada) do mesmo jeito, e isso **nunca** travou. Os canários são lidos pela janela **WC**. Diferença registrada, sem conclusão (H).
- **Casa com o "só na partida"** (H forte): cada alocação nova do VidMm gera um FILL com canários plantados e lidos. A partida e a abertura do menu são justamente as fases de muitas alocações novas. Na 169 (227 s), a trava também veio quando alocações novas voltaram a aparecer.

**Sem regressão:** 1.018 `IB EXECUTADO`, 969 `TRIPE_PROVADO`, 285 Presents, zero timeout, fault e TDR.

**Recuperação:** Código 43 e fusível TRIPPED esperados: `BC250_RECOVERY.cmd`, `-Reset` e boot frio.

**Próximo proposto (X4):** tirar do caminho de execução a prova de dados por canário nos IBs de paginação. A CPU deixa de plantar e ler na VRAM pela BAR o destino que a GPU acabou de escrever. Esses IBs ficam com prova de duas pernas (RPTR e fence da GPU), declarada. O tripé completo continua no ensaio de boot. Uma variável.
