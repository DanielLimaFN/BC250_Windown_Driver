# 591 — Corrida 217: VMID 0 única funcionou; a D2 cai com fence invisível na RAM; trava dura aos ~4 min sem a D2

**Data:** 05/10/2026, bind às 09:29:29; trava ao abrir o menu; reinício às 09:34:14.
**Candidato:** lab **3.365.1.0** (SYS `6A6A886A…`).
**Critério:** [doc 590](590_LC1_ROLLBACK_SEM_LACO_V01_VMID0_UNICA_E_CRITERIO_CORRIDA217.md) §5.
**Evidência:** `docs/evidence/2026-10-05_09-29-29-DESKTOP-HQAG6T4-lab-binding/display-log-217-vivo.txt`. É o log vivo inteiro: começa no offset 0 e as últimas linhas são do boot seguinte, recusado pelo fusível.

## 1. Medido (P0)

**Pré-condição de validade: cumprida.**
- `[D2_GART] MEC (HQD_ATIVO) na tabela da GART ... divergentes=0` e `MEC (MOTOR_DE_SUBMISSAO) ... divergentes=0`.
- `MEC_CURSOR_RECONCILE PASS ... Submitted=1 Retired=1`: a fence do MEC em VA `0x2200` traduziu pela tabela da GART.
- `[D2_ANEL_SISTEMA] ATIVO RingGpu=0x20000 WorkGpu=0x21000`.
- `StartDevice LEAVE status=0`.

**A D2 caiu cedo.** Foram 117 IBs executados até o timeout, por volta do vsync #120, antes do desktop. Correção da leitura parcial que fiz durante a corrida: eu disse "~4.000 IBs", mas 0x1079 é o contador de fence do hardware, que inclui o boot.

```
IB_TRIPOD TIMEOUT (ASYNC 2000 ms): TargetWPTR=57024, WPTR=59480, RPTR=59480, ... FenceExp=00001079, FenceObs=43DAFFFE FenceLida=1
[D2_GART_FOTO] timeout de IB: Ligada=1 MEC=1 ... divergentes=0 ... PTE[32..35] ... divergentes=0 FAULT_STATUS=0x00000000
```

- **Assinatura nova, diferente da 215/216:**
  - `RPTR == WPTR`: o anel foi **drenado** e a engine ficou ociosa (`STATUS1 0x3FF` no quiesce);
  - sem fault (`FAULT_STATUS 0`);
  - contexto 0 e PTEs intactos;
  - a fence esperada **não apareceu** na RAM, que continuou com o valor de poison.

  A engine terminou o trabalho, mas a escrita da fence pela GART não ficou visível para a CPU.
- **TDR:** o restart pela GART falhou de novo (o NOP de prova não retirou). Depois veio `D2_GART MANTIDA no rollback` (a VMID 0 ficou com o MEC), e `anel de volta na VRAM e provado`. Não houve tela azul.

**Sem a D2, a corrida seguiu até a trava dura**, no vsync #13.260, ~236 s depois do início dos vsyncs, ao abrir o menu.
- **Contagens:** 9.949 `IB EXECUTADO`, 2.214 `Present`, zero `GFXHUB_FAULT`, zero `NO_MEMORY`, zero `DEVICE_REMOVED`, 8 envios forçados (todos resolvidos).
- **O fim é a assinatura de sempre (doc 582):** o último evento da GPU é um `RETIRE_SDMA` (fence 9959), e `EVT1_EM_VOO` diz "CONCLUIDO". A GPU estava ociosa e a máquina morreu entre duas observações.
- A VMID 0 estava **ligada** (raiz da GART) durante a carga.

**Coleta pós-trava (`coletar-pos-trava.ps1`, 09:37):**
- `RESET_REASON raw=0x40080400` (bits 10, 19 e 30): **idêntico** ao das 12 travas anteriores com 256 MB (199–212). O reset foi feito pelo botão, e não houve sinal de erro de plataforma novo.
- O rastro recuperado (`paging-trace-this-bind-RECUPERADO.bin`) termina com zero registros depois da última paginação (`writer_tick` 2.091, ~209 s), a mesma assinatura do doc 582: a máquina morre entre duas observações.
- CMOS com `UMA_SIZE=0256 MB` aplicada.

## 2. Veredito pelo critério do doc 590

| Desfecho | Ocorreu? | Por quê |
|---|---|---|
| Pré-condição | sim | MEC na GART nas duas entradas, D2 ativa |
| A — D2 sobrevive | não | timeout depois de 117 IBs |
| B — D2 sobrevive e trava | não | a D2 não sobreviveu |
| C — MEC falha na tabela | não | `Retired=1` |
| **D — D2 cai com a foto** | **sim** (variante "tudo intacto") | registradores e PTEs intactos, sem fault; nova: `RPTR==WPTR` com fence invisível |
| **E — fora do mapa** | sim | trava dura aos ~4 min **sem a D2 e com a VMID 0 ligada** |
| Condição do rollback | não se aplica | a trava dura impediu o rollback |

**Veredito:**
- **V0.1 é sucesso:** a disputa da VMID 0 acabou e a tradução ficou intacta no timeout.
- **A D2 é falha:** a escrita da fence na RAM fica invisível.
- **A trava dura não mudou.**
- **A LC.1 não foi exercitada:** não houve teardown.

## 3. Leitura

- **D2.** Com a tradução intacta e o anel drenado, o defeito está no caminho de escrita da GPU para a RAM, ou na leitura dela pela CPU. Fiz uma verificação por leitura: a fence assíncrona fica dentro da página de 4 KB do Work (`0x800`/`0x880`). Logo, a hipótese de o slot cair na página dummy está refutada. Candidatos (H):
  - atributo da PTE do Work (`0x77`, snooped) incompatível com a RAM alocada `MmNonCached`;
  - política de cache da escrita do pacote FENCE para memória de sistema;
  - visibilidade de L2 para escrita em sistema.

  Precisa comparar com o `amdgpu`: as flags de PTE do GTT e o `emit_fence` do sdma_v5_0.
- **Trava dura.** É a 10ª com a mesma assinatura, agora com a VMID 0 ligada em formato válido (PA \| VALID). Isso não refuta a correlação da 216 (VMID 0 desligada, sem trava), mas também não a sustenta: a 201 já não travou com a VMID 0 ligada. A hipótese continua aberta e sem mecanismo.

## 4. Pendências

- **Daniel:** coleta pós-trava feita. Falta: a recuperação, o reset do fusível e o boot frio.
- **NÃO VALIDADO EM HARDWARE:**
  - a D2 estável;
  - a LC.1 no teardown;
  - 600 s sem trava com 256 MB.
