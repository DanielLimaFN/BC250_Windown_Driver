# 525 — X4: sem canários no caminho de execução; critério da Corrida 172

> 29/09/2026, offline. Candidato **3.317.1.0** (inclui HDP.1, T3.1, EVT.1, X1, X2a e X3). **Nada desta faixa foi validado em hardware.**

## 1. Por quê

Duas corridas seguidas pararam **dentro do mesmo acesso** (P0):

| Corrida | Última migalha da máquina | O que o anel EVT.1 mostra |
|---|---|---|
| 170 | CPU00 `VRAM_CANARY_READ` fase **ANTES**, VRAM `0xF280000` | `SUBMIT_SDMA` do FILL de 2.273.280 bytes sem conclusão |
| 171 | CPU00 `VRAM_CANARY_READ` fase **ANTES**, VRAM `0xF3CC000` | `SUBMIT_SDMA` do FILL de 1.048.576 bytes, feito pela própria CPU00, sem conclusão |

- Na 171 o portão do X3 **nunca agiu**: a leitura aconteceu com o SDMA ocioso. O que trava é a CPU lendo pela janela WC, logo depois do flush e da invalidação do HDP, **o destino que o FILL acabou de escrever**.
- A leitura da fence, feita do mesmo jeito mas por mapeamento UC na faixa privada, nunca travou.
- **Casa com o padrão da trava (H forte):** cada alocação nova gera um FILL com canários, e a partida e a abertura do menu são as fases de mais alocações novas. A 171 travou no clique do menu.

## 2. A mudança (uma variável de comportamento)

- `Bc250X4DisableRuntimeCanaries`, chamada logo depois de montar o plano em `Bc250SdmaPresentSubmitIndirectAsync` e `Bc250SdmaPresentSubmitFillAsync`, antes do lock do anel.
- Ela zera `dataPlan.Usable`. A CPU **não planta nem lê** canários em execução.
- A prova desses IBs fica com duas pernas (RPTR e fence gravada pela GPU sobre poison). A de dados sai declarada: `[X4_CANARY] … perna de dados NAO CONFERIDA` (amostrado) e `IB_ENGINE_2LEGS` por IB.
- O texto de `IB_EFEITO_DECLARADO` deixou de dizer "canaries plantados".
- O tripé completo continua no ensaio de boot (`bc250_sdma_ring_work.c`, fora do caminho de execução).
- **Não tocados:** os caminhos síncronos sem chamador em execução; as provas de bytes do Present e do escape de composição (leem a origem e o scanout, não o destino de um FILL); a caminhada de tabelas.

## 3. Provas offline

- `mcdm_callbacks_test`, `blt_proof_contract_test`, `paging_contract_test`, `sdma_packet_builder_test` e `active_miniport_test`: **EXIT 0**.
- Auditoria de segurança: **PASS**, com o gate novo `audit_x4_no_runtime_canaries`. Nenhum gate antigo exigia os canários em execução.
- **3 mutações, todas reprovaram**, e a auditoria voltou a PASS (`scratchpad/mutate_x4.py`):
  - W1: o submit indireto volta a usar canários;
  - W2: o submit de fill volta a usar canários;
  - W3: o helper não desliga.
- Build de laboratório EXIT 0 e auditoria do pacote **PASS**.
- Strings conferidas no SYS assinado: `X4_CANARY` e `X4: sem canarios em execucao`.
- `tools/prepare-corrida172-x4.ps1`: EXIT 0.

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `E29882ED846F9AE25529C3EC133AC3AFC7DE4A227DEE9979EF862EFB327664C6` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `A0751F6C487BF91A2028F16044B4C48BE51791A3E43134CD15A890EA63ADF309` |
| `bc250mcdm.cat` | `61071BC93FA0AD2B8611F8EF232EFD759F71608A96292CCD7F6FEA9E1CB63C9A` |

- DriverVer `3.317.1.0` (fonte `3.317.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA172_X4_600S.cmd`.

## 5. Critério da Corrida 172 (escrito antes; Lei 10)

**Roteiro:** o mesmo, e **abrir e fechar o menu Iniciar várias vezes**, porque a 171 travou no clique e o menu é um gerador de alocações novas.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | 600 s sem trava **e** `[X4_CANARY]` presente **e** zero `VRAM_CANARY_READ` no log **e** o menu aberto e fechado sem travar **e** sem regressão (`IB EXECUTADO`, Presents) | Primeira limpa. Repetir idêntico (173). Duas seguidas: a leitura dos canários pela BAR logo depois do FILL era o gatilho. Três: causa. |
| **B1** | Trava com outra migalha de VRAM em fase ANTES (`VRAM_PT_READ`, `VRAM_IB_READ`, `VRAM_PROOF_READ`, `VRAM_SAMPLE_LOOP`, `VRAM_COMPARE`, `VRAM_FENCE_READ`, `VRAM_RING_WRITE`) | A classe "CPU acessando VRAM pela BAR" continua, em outro sítio. Seguir por ele. |
| **B2** | Trava sem migalha de VRAM pendente | O acesso dos canários era um disparador, não o único. Ler o anel de eventos e as migalhas. |
| **C** | Regressão: `IB_SEM_TRIPE`, TDR, timeout, perda de Present, ou o VidMm rejeitando algo | Corrigir antes de repetir. |
| **D** | `[X4_CANARY]` ausente | O caminho não rodou. A corrida não julga. |
| **E** | Fora do mapa | Descrever antes de julgar. |

- **Desfecho parcial não conta:** A exige os 600 s e o menu exercitado.

## 6. Resultado da Corrida 172 (29/09/2026, bind 10:01:27, pacote 3.317.1.0)

**Evidência:** `docs/evidence/2026-09-29_10-01-27-DESKTOP-HQAG6T4-lab-binding/display-log-this-bind-RECUPERADO.txt`, 4.295.813 bytes, SHA-256 `02656EF2…ED2B`. Evento 41 às 10:02:58. **Trava em 51,9 s.**

**Veredito: B2.** O X4 rodou: `[X4_CANARY]` presente, **zero** `VRAM_CANARY_*`, 1.340 `IB_ENGINE_2LEGS`, e a trava veio do mesmo jeito, **sem nenhuma migalha de VRAM pendente**:

```
CPU00 seq=310162 sitio=SDMA_REG_READ detalhe=0x001B8C fase=DEPOIS(concluido) <- ULTIMA DA MAQUINA   (RB_RPTR, portao do X1)
```

- Anel EVT.1 (31 válidos): o último evento é o `SUBMIT_SDMA` de 32 bytes (fence 1341), sem conclusão. O último FILL montado antes dele é de 65.536 bytes (`op=0x5`, 8 × 4 = 32 bytes: casa).
- O GFX tinha concluído 3,2 ms antes.
- **Um FILL de 64 KB executa em microssegundos:** a máquina parou **durante a execução, ou logo depois do disparo, de um FILL pequeno**, com a CPU00 só lendo o registrador RPTR.

**Sem regressão:** 1.340 `IB EXECUTADO`, 213 Presents, 5 `BYTE_PROOF PASS`; zero timeout, fault, TDR e `DEVICE_REMOVED`.

**Leitura corrigida:**
- **14 de 14 travas** terminam com um FILL de paginação submetido ao SDMA sem conclusão. Nas 5 com anel (168–172) isso é P0.
- O último acesso da CPU varia: leitura de volta do HDP, canário pela BAR, registrador RPTR. **É colateral: é o que a CPU que sondava estava fazendo.** O gatilho fica do lado da GPU e do fabric, na execução do FILL.
- A leitura dos canários **não** era a causa; X3 e X4 **não bastam**. O X4 fica: tira da CPU uma leitura de VRAM sem valor funcional.
- **Fraqueza da instrumentação:** `PHASE_BEFORE=0xB0` e `PHASE_AFTER=0xA0` diferem num único bit (bit 4), e a VRAM que decai vira bits de 0 para 1. **Um DEPOIS degradado aparece como ANTES.** Isso explica os `PAGING_DDI … ANTES` antigos e isolados de quase todas as corridas, e rebaixa o "ANTES" dos canários de 170/171 de P0 para **P2**. A correção é códigos de fase com distância de Hamming grande.

**Recuperação:** Código 43 e fusível TRIPPED esperados: `BC250_RECOVERY.cmd`, `-Reset` e boot frio.

**Próximo proposto (X2b, discriminante):** zerar por **cópia** em vez de CONST_FILL. A TRANSFER (cópia) nunca foi o último trabalho de uma trava.
- Uma região de zeros dentro do próprio paging buffer, cuja GPUVA o contrato dá (`DXGKARG_BUILDPAGINGBUFFER.DmaBufferGpuVirtualAddress`, d3dkmddi.h:5197), é a origem de `COPY_LINEAR` para o destino.
- Se as travas pararem: é o CONST_FILL. Se continuarem, com cópia no lugar do fill: é o destino (a primeira escrita sob PTEs novas), não o pacote.
- Junto, só instrumentação: fases das migalhas com distância de Hamming grande, e o endereço físico do destino de cada FILL no anel de eventos.
