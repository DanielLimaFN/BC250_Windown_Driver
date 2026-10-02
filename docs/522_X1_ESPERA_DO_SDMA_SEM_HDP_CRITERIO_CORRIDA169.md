# 522 — X1: a espera do SDMA sem HDP e sem ler VRAM enquanto a engine trabalha; critério da Corrida 169

> 29/09/2026, offline. Candidato **3.314.1.0** (inclui HDP.1, T3.1 e EVT.1). **Nada desta faixa foi validado em hardware.**

## 1. Por quê

A Corrida 168 (doc 521 §7, P0) mediu a máquina parando com um **FILL de paginação de 2,27 MB submetido ao SDMA e sem conclusão**. A última migalha de MMIO foi `SDMA_REG_READ 0xFC4` concluída na CPU00: a leitura de volta da invalidação do HDP, no laço de espera. As 10 travas de 28 e 29/09 terminam com um FILL.

Leitura do código que manda neste passo (P1):
- **Aposentadoria assíncrona** (`Bc250SdmaPresentPollRetirement`, timer e DPC da CPU00): a cada tique lia a fence **na VRAM pela BAR** e, sem ela, fazia flush e invalidação do HDP e lia de novo.
- **Blts do Present** (`BltCore`, `BltRectCore`): a cada 5 µs, até 10.000 voltas e com o spinlock do anel seguro, liam a fence **e** o writeback do RPTR na VRAM, mais HDP a cada 128 voltas.
- **Essas leituras de VRAM não tinham migalha.** "Última migalha = `0xFC4` concluída" é compatível com a CPU parada na leitura seguinte, a da fence na VRAM, invisível às migalhas.

O Linux não sonda assim: fence e RPTR ficam em memória do sistema, e o HDP só é mexido quando a CPU vai ler dados da GPU (P1, `sdma_v5_0.c`, `amdgpu_hdp.c`).

## 2. A mudança (uma variável de comportamento)

1. **`Bc250TripodRingEntryConsumed`** (contrato puro, `bc250_blt_proof_contract.c`): decide se a engine consumiu a entrada só pelo RPTR do registrador, reduzido a uma volta do anel.
   - A condição é `dist(Início→RPTR) ≥ dist(Início→Alvo)` e `≤ dist(Início→último WPTR)`.
   - O segundo limite impede que um RPTR ainda nos NOPs de volta do anel pareça concluído.
   - Argumento incoerente devolve 0 (falha fechada).
2. **Aposentadoria assíncrona:** o portão vem primeiro. Enquanto ele não passa, **nenhum HDP e nenhuma leitura de VRAM**. Quando passa, um flush e invalidação do HDP e a leitura da fence, agora **com migalha** (`VRAM_FENCE_READ`). A prova continua a mesma (fence sobre poison, e o tripé com canários quando houver).
3. **Blts do Present:** o mesmo portão (`Bc250SdmaSyncEntryConsumed`) e a mesma leitura com migalha (`Bc250SdmaReadFenceAfterConsumed`). Saiu o HDP periódico `(poll & 0x7F)`.
4. **Timeout:** a linha diz `FenceLida=0/1`, para não mostrar como poison uma fence que não foi lida.
5. **Medição:** `[X1_POLL] leitura de fence #n so' depois do RPTR … | sondagens sem HDP ate aqui=N` (#1–#4 e a cada 65.536).

**Não tocados:**
- os caminhos síncronos de fill e de IB indireto (`Bc250SdmaPresentFill`, `ExecuteIndirect`): sem chamador em tempo de execução;
- o `SyncNop` do restart de TDR;
- o HDP antes do WPTR no submit, que é necessário: a CPU escreveu o anel e os pacotes na VRAM.

## 3. Provas offline

- `blt_proof_contract_test`: **EXIT 0**, com o bloco novo `TestRingEntryConsumedByRptrOnly`. Casos:
  - o medido em 07/09 (RPTR cru 4612 contra alvo 516);
  - entrada no meio e no início;
  - entrada atravessando a volta do anel;
  - RPTR nos NOPs antes do início;
  - RPTR além do último WPTR;
  - três argumentos incoerentes.
- Auditoria de segurança: **PASS**, com o gate novo `audit_x1_no_hdp_or_vram_read_while_sdma_busy`.
- **6 mutações, todas reprovaram**, e a auditoria voltou a PASS (`scratchpad/mutate_x1.py`):
  - X1a: a aposentadoria lê a fence antes do portão;
  - X1b: o blt volta a ler VRAM a cada volta;
  - X1c: leitura de fence sem migalha;
  - X1d: o teste perde o caso dos NOPs;
  - X1e: portão da aposentadoria desligado com `0u &&`;
  - X1f: portão do blt desligado.
  - **A X1e passou na primeira versão do gate**: ele procurava só o nome da função. Foi endurecido para exigir a condição inteira.
- Build de laboratório EXIT 0 e auditoria do pacote **PASS**.
- Strings conferidas no SYS assinado: `X1_POLL`, `VRAM_FENCE_READ`, `FenceLida=%u`, `EVT1_EM_VOO` e `T31_UC_MAP`.
- `tools/prepare-corrida169-x1.ps1` (leitura): EXIT 0.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `AAF5D521215A0F51C6B3F9734F6FA27204A23450207CA19E8A4F3962B5CC5458` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `B12807C78BD26226CDB1B406D385636D90C6E25BEB41008A82DF96E1FD4E9A83` |
| `bc250mcdm.cat` | `829AA934F898FBD73EA54F0AFE0035CD1820B65F8C244C75D05C5335327A7AF5` |

- DriverVer `3.314.1.0` (fonte `3.314.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA169_X1_600S.cmd`. Mesmo roteiro, **com a T6 revertida** (`IDLEDISABLE 0`), para ficar uma variável só.

## 5. Critério da Corrida 169 (escrito antes; Lei 10)

**Base:** uma limpa não prova nada; duas seguidas são o mínimo; uma trava diz "não basta".

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava **e** `[X1_POLL]` presente com "sondagens sem HDP" crescendo **e** tripé e Present sem regressão | Primeira limpa. Repetir idêntico. Duas seguidas: a sonda com HDP e leitura de VRAM durante a rajada do SDMA era o gatilho. |
| **B1** | Trava; no boot seguinte o anel mostra de novo um `PAGING_FILL` em voo **e** nenhuma migalha `VRAM_FENCE_READ` em fase ANTES | A sonda não era o gatilho: a parada acontece com o FILL executando mesmo sem a CPU tocar HDP ou VRAM. A frente passa ao FILL em si (X2: fatiar e espaçar, destino e PTE). |
| **B2** | Trava com migalha `VRAM_FENCE_READ` fase **ANTES** | A CPU parou dentro da leitura da fence na VRAM, depois que o RPTR já dizia "consumido". Mesma classe de acesso, janela menor. |
| **C** | Regressão: `IB_TRIPOD TIMEOUT` com `FenceLida=0` e RPTR já no alvo, TDR, blt do Present em timeout, ou Present falhando onde passava | O portão está errado para algum caso (aritmética do anel ou tail). Ler o RPTR, o início e o alvo no log e corrigir antes de repetir. |
| **D** | `[X1_POLL]` ausente | O caminho novo não rodou. A corrida não julga a hipótese. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** A exige `[X1_POLL]` presente.
- **Informação nova em qualquer desfecho:** a razão entre sondagens sem HDP e leituras de fence mostra quanta atividade de HDP e VRAM a CPU deixou de fazer durante as rajadas.

## 6. Resultado da Corrida 169 (29/09/2026, bind 08:54:56, pacote 3.314.1.0, T6 revertida)

**Evidência:** `docs/evidence/2026-09-29_08-54-56-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 22.693.256 bytes, SHA-256 `72ED7651…C9E7`. Evento 41 às 08:59:24. `IDLEDISABLE` AC = `0x0` (T6 revertida, P0).

**Trava em 227,0 s.** É a **primeira trava depois de 200 s** no corpus de 28 e 29/09: as 19 anteriores caíram entre 27 e 196 s. É n=1, não prova.

**Veredito: B1.**
- O X1 rodou: `[X1_POLL]` #1–#4 com a leitura de fence só depois de `Rptr == Alvo`.
- No boot seguinte, o anel (17 válidos, **31 degradados**) mostra:
  - o último evento é um `PAGING_FILL` de 3.670.016 bytes montado;
  - 8,4 ms antes, um `SUBMIT_SDMA` de 32 bytes (fence 5639): o IB do FILL de 3.407.872 bytes montado 10,6 ms antes, **sem conclusão registrada**.
- Não há migalha `VRAM_FENCE_READ` em fase ANTES. A última migalha da máquina é de novo `SDMA_REG_READ 0xFC4` concluída na CPU00.
- O nó 1 (GFX) também mostra o último submit (fence 1303, 34 ms antes) sem conclusão. Com 31 eventos degradados, a conclusão pode ter se perdido.

**Sem regressão, e com mais trabalho feito antes da trava:** 5.639 `IB EXECUTADO`, 3.464 `TRIPE_PROVADO`, 846 Presents, 5 `BYTE_PROOF PASS`, 3.471 FILLs montados; zero timeout (`FenceLida=0` = 0), fault, TDR, `DEVICE_REMOVED` e `Blt Timeout`.

**Leitura:**
- Tirar HDP e leitura de VRAM das esperas **não impede a trava** (n=1).
- Mas a máquina durou mais que em qualquer trava anterior. Isso é compatível com a espera ter sido um fator de exposição, não o gatilho. É H: uma corrida.
- Pela assinatura, a parada acontece de novo com um FILL de paginação executando ou recém-submetido no SDMA. **11 de 11** travas terminam assim.

**Pendência de interpretação:** depois da `0xFC4` da CPU00 não há migalha. Os caminhos que fazem HDP e depois acessam VRAM sem migalha são:
- os canários do tripé depois da conclusão (`PollRetirement`, leitura pela janela WC);
- as escritas do anel no submit, entre os dois HDP.

**Recuperação:** Código 43 e fusível TRIPPED esperados: `BC250_RECOVERY.cmd`, `-Reset` e boot frio.
