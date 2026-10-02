# 501 — Corrida 153: 600 s sem trava, TDR real recuperado com o desktop vivo; causa das paradas do SDMA achada (DX.95); critério da Corrida 154

> Corrida de 28/09/2026. Bind às 10:29:23, candidato 3.303.1.0 (DX.94), sem KD, harness atrasado 150 s. Rollback online às 10:39:40 (`RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras).
>
> Evidência:
> - pasta `docs/evidence/2026-09-28_10-29-23-DESKTOP-HQAG6T4-lab-binding/`;
> - log completo `display-log-this-bind-FINAL.txt` (864.916 linhas);
> - logs FINAL da UMD, com o DWM incluído.

## 1. Relato (cinco pontos)

**1. Houve avanço?** Sim, funcional e de diagnóstico:
- **funcional:** 600 s sem trava com o harness rodando, e um TDR real recuperado com o desktop continuando;
- **diagnóstico:** a causa das paradas do SDMA que viraram 0x10E nas 135 e 151.

**2. Sucessos (P0):**
- **Estabilidade:** ~593 s de log, sem trava e sem BugCheck. O Daniel relata que não travou.
- **Harness 60/60** `PRESENT_OK`, zero Presents falhos, **1,6 FPS médio (1,8 na janela)**.
- **Explorer:** 1 `vkAllocateMemory -2`, contra 110 na 152 (só ~173 s) e 53 na 150.
- **DX.94:** as linhas `OCUPACAO` mostram `Cap=4096`, com `Mapped` 1.012 → 1.800 → 2.392. Houve só 3 `PUBLICACAO_FALHA`, e as três são **contenção** (`PublishStatus=16 Attempts=16 Busy=16`), não capacidade: com 1.024 faixas o espaço teria esgotado. Na 152 foram 26.195 em 173 s.
- **TDR real recuperado com o desktop vivo** (a primeira vez fora de um hang induzido):
  - ~1,2 s depois das primeiras linhas com horário do log, `IB_TRIPOD TIMEOUT (ASYNC 2010 ms)`;
  - `TDR_QUIESCE` e depois `TDR_RESTART PROVADO`;
  - o DWM reiniciou (pid 1284 → 2944, 10:29:48);
  - **depois do TDR vieram 1.907 Presents e 35.167 `IB EXECUTADO`**.
- Zero `GFXHUB_FAULT` na telemetria do SDMA, zero `DEVICE_REMOVED` nos logs do DWM, zero `Blt Timeout`.

**3. Falhas (P0):**
- **O TDR em si:** o SDMA parou dentro de um fill de paginação (causa na §2).
- **FLUSH_TLB mais lento na cauda:**

  | Medida | 152 | 153 |
  |---|---|---|
  | p50 | 0,64 ms | 0,73 ms |
  | p99 | 1,37 ms | **9,64 ms** |
  | Máximo | — | 104 ms |

  O p99 fica entre 7 e 13 ms **desde o primeiro minuto**, sem crescer. A fatia do tempo dentro do FLUSH_TLB é ~20 s por minuto (33%), igual às 151 e 152.
- **O FPS do harness não subiu:** 1,6 contra 1,64 na 150. **A FPS.1 não aumentou o FPS do harness** (P0). Os Presents do DWM por segundo (2,2–2,9) não servem de comparação: o DWM só apresenta quando algo muda na tela.
- O `Mapped` cresce ao longo da corrida (três amostras). Se estabiliza ou vaza **NÃO FOI MEDIDO**: a linha de ocupação só sai quando uma publicação falha.
- `Journal NO_SPACE`: 754 (antes 309 em 173 s), que é a classe separada do journal.

**4. Veredito contra o doc 500:** **D, pela assinatura** (p99 do FLUSH_TLB ≥ 5 ms). A trava não aconteceu e o harness passou.

A implicação escrita para D era "reverter a DX.94". **Proponho não reverter.** O custo medido é só de cauda: a fatia de tempo é a mesma, e o 60/60 do harness e o FPS continuam iguais. O ganho, por outro lado, é grande: o Explorer passou de 110 falhas para 1, e as falhas de publicação por capacidade zeraram. O remédio que o próprio D nomeia (aplicar o lote numa reconstrução só, FPS.2) é o próximo trabalho de desempenho. **A decisão é do Daniel.**

**5. Recuperação e pendências:**
- Rollback online limpo, BasicDisplay em Código 0.
- **NÃO VALIDADO EM HARDWARE:**
  - a DX.95;
  - se o `Mapped` estabiliza;
  - o FPS do harness acima de 1,6.

## 2. A causa das paradas do SDMA (P0 nas duas corridas)

A mesma sequência apareceu nas duas corridas que pararam o SDMA:

| Passo | Corrida 151 (virou 0x10E_4) | Corrida 153 (recuperada por TDR) |
|---|---|---|
| Fill de paginação submetido no VMID8 (raiz de paginação `0x450A8C000`) | 4 MiB, fence 4941 | 11.075.584 bytes, fence 4216, destino `[0x260000, 0xCF0000)` |
| Logo em seguida, `SetRootPageTable` de **contexto de usuário**, com `[Vmid8Install] readback OK` para a raiz **dele** | ctx `0x22A6000002A3`, PT `0x45F943000` | ctx `0x58`, PT `0x45F9D0000`; `GCVM_INVENTARIO_2 VMID08 ... -> 0x45F9D0000` |
| Falha de tradução no VMID8 | — | `GFX_FAULT_SENTINELA`: `0x00841B55 Vmid=8`, GpuVa `0x52E000`, **dentro do destino do fill** |
| `AVISO CONTEXT SWITCH COM IN_FLIGHT`, `STATUS InsideIb=1`, RPTR congelado | RPTR 353.952 | RPTR 7.712 |
| Desfecho | `Blt Timeout` → **0x10E_4** | `IB_TRIPOD TIMEOUT` → TDR → recuperado |

**O defeito (P1, código):** `Bc250ActiveSetRootPageTableObject` escrevia no VMID8, com invalidação da TLB do VMID8, a raiz de **qualquer** contexto dos nós 0 e 1. O VMID8 é o VMID da **paginação**. Na 153 foram 763 `SetRootPageTable`, cada um reescrevendo o VMID8. O caminho de submissão devolvia a raiz de paginação no submit seguinte, 760 vezes. Na maioria delas o anel estava vazio e nada acontecia. Quando havia um IB longo em voo, a tabela era trocada embaixo dele.

**Correções de registro:**
- A hipótese de que o tamanho de 4 MiB era a causa (SDMA.4M, doc 498) está **refutada como causa**. O fill grande só alonga a janela em que o IB fica em voo. Os limites da Mesa ficam, porque são corretos por procedência.
- A 150 e a 152 tiveram zero switches com IB em voo, e zero paradas do SDMA.

## 3. DX.95 (pacote 3.304.1.0)

**Mudança:** `SetRootPageTable` passa a **só registrar** a raiz, para todos os nós, como o nó de computação já fazia. A raiz é instalada no VMID certo no momento do submit:
- VMID8 para paginação;
- VMID7 ou o pool para usuário.

Nesse momento o estado do anel é conhecido. Se o contexto era o instalado no VMID8, só a contabilidade é invalidada, sem MMIO, e o próximo submit de paginação reinstala. Nova linha no log: `[SetRootPageTable] DX95 NodeN: raiz registrada ...`.

**Portão `audit_dx95_set_root_never_touches_vmid8`:**
- proíbe, dentro de `SetRootPageTable`: `Bc250GcvmVmid8Install`, `Vmid8Uninstall`, `Bc250GcvmInstallVmid`, escrita MMIO de VMID e flush de VMID;
- exige o log novo e a invalidação da contabilidade;
- **duas mutações, duas reprovações.**

**Provas offline:**
- `build-contract` EXIT 0;
- `active_miniport_test`, `mcdm_callbacks_test` e `gpuva_space_contract_test` EXIT 0;
- auditorias de segurança, matriz DDI e no-op PASS;
- build de laboratório EXIT 0 e auditoria do pacote PASS;
- conferido por string no SYS assinado: a linha `DX95 Node%u` está presente e a linha `[Vmid8Install] readback OK` saiu.

A DX.94 continua no pacote.

**FPS.3a (telemetria, no mesmo pacote):** a linha `IB EXECUTADO (INDIRECT)` ganhou `retire_qpc=` e `FromDpc=`. Motivo, na 153:
- o harness passa **449 ms de 584 ms** por quadro dentro de `pfnPresentCb` (p50 449, p90 460), o mesmo que na 150 (440 ms), então a FPS.1 não mudou isso;
- o DWM compõe rápido e fica ~400 ms esperando o quadro do app;
- dentro da espera há **~19 IBs de paginação em série por quadro**, com mediana de ~14 ms entre um e o próximo.

A conclusão do IB não tinha horário, e por isso a latência submit → conclusão não era mensurável. **H:** a conclusão depende do timer de 1 ms, que só dispara no tique do relógio do Windows (15,6 ms por padrão). Não muda comportamento. O pacote foi para 3.305.1.0; o 3.304.1.0 não chegou a ser instalado.

| Artefato (3.305.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `2E7E4A650E1EBAB72EEBF915337D5DC035FA1179D49403A43B352374CE4C89F0` |
| `bc250mcdm.sys` fonte lab | `8D778F52F4AB7444546F3B2376AB43A91D6F7AE81F17D266A68915DF430DA853` |
| `bc250umd.dll` (igual à 149–153) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `017990C6226D1FAAA2872EC7AFB36FB7A87183D1FDF1C41B7C254F4A6B369E91` |
| `bc250mcdm.cat` | `3A996970345817456E82ACE5A52642AB01EB75D8012E38A3D1B7D36B846919D4` |

## 4. Critério da Corrida 154 (escrito antes)

- **Runner:** `tools\EXECUTAR_CORRIDA154_DX95_600S.cmd` (preparo `tools\prepare-corrida154-dx95.ps1`), sem KD, harness atrasado 150 s, 600 s.
- **Boot frio antes.**
- **Linha de base:** a Corrida 153.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | Linhas `DX95 Node` presentes; **zero** `GCVM_INVENTARIO_2 VMID08 MUDOU` para raiz diferente de `0x450A8C000`; `VMID8 CONTEXT SWITCH` ≤ 2 (só a instalação inicial da paginação); zero `CONTEXT SWITCH COM IN_FLIGHT`; zero `IB_TRIPOD TIMEOUT`, `Blt Timeout`, TDR e BugCheck; harness 60/60 | O VMID8 trocado debaixo do SDMA era a causa das paradas. A investigação de estabilidade volta à trava dura, e a de desempenho vai para a FPS.2. |
| **B** | VMID8 estável como em A, mas uma parada do SDMA **ainda** acontece | Há outra causa de parada. Ler `IB_ESTADO`/`IN_FLIGHT` e o `GFX_FAULT_SENTINELA` dessa parada. |
| **C** | Falha de submissão de paginação: `VMID8 CONTEXT SWITCH FALHOU`, recusa ou fault no VMID8 logo no início | A instalação no submit não cobre algum caminho que dependia do `SetRootPageTable`. Reverter a DX.95 e mapear esse caminho. |
| **D** | Regressão: tela preta, `DEVICE_REMOVED`, pixels errados ou o harness falhando | Isolar a DX.95 da DX.94 (voltar um dos dois). |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

**Medição de FPS (FPS.3a), fora dos desfechos A–E:** para cada IB de paginação, calcular:
- **submit → conclusão**, de `KMD_SDMA_SUBMIT qpc` até `retire_qpc`;
- **conclusão → próximo submit**, o tempo do VidMm.

Se a primeira se agrupar perto de 15,6 ms com `FromDpc=1`, a hipótese do tique fica confirmada e a correção é notificar a conclusão sem depender do timer. Se ficar abaixo de 1 ms, o tempo está do lado do VidMm, e o próximo passo é contar quantas operações de paginação cada quadro exige.

Observações que não entram nos desfechos:
- FLUSH_TLB (p50/p99);
- `Mapped` nas linhas de ocupação;
- `Journal NO_SPACE`;
- falhas `-2` do Explorer;
- a trava dura, caso volte (a 153 não a reproduziu).

**NADA DA DX.95 FOI VALIDADO EM HARDWARE.**

> **Correção (doc 504):** "o DWM reiniciou (pid 1284 → 2944)" **não** é efeito do TDR. Na 155, sem TDR, o DWM também reinicia ~10 s depois de carregar: é a troca normal do bind. Continua valendo que depois do TDR vieram 1.907 Presents e 35.167 IBs.
