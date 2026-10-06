# 601 — Corrida 223: doorbell ativa pela primeira vez, trava dura aos ~338 s, e artefatos do app Clima

**Corrida 223:** 05/10/2026 14:29:51 · lab **3.371.1.0** (SYS `806424F8…`) · critério: [doc 600](600_CORRIDA222_600S_SEM_TRAVA_DOORBELL_NAO_CHEGA_ABERTURA_DO_BIF_P2C_E_CRITERIO_CORRIDA223.md) §5
**Evidência:** `docs/evidence/2026-10-05_14-29-51-DESKTOP-HQAG6T4-lab-binding/`. Coleta automática do boot das 14:36:40 (cauda, rastro decodificado, motivo do reset, teste de RAM) e log cumulativo.

**Relato do Daniel:**
- abriu o app Clima, que mostrou artefatos;
- o menu ficou bem lento;
- ao abrir o menu, a máquina travou com tela preta;
- reiniciou.

## 1. Pré-condição: válida (P0)

- `APLICADO NA RECONSTRUCAO (F32 parado): APER_EN=0x00000001 (antes 0x00000000) DOORBELL=0x10000000 OFFSET=0x800 BIF_RANGE=0x00140800 readback=confere; prova=1 Kicks 0->1`.
- `[P2_DOORBELL] ATIVA`: a prova foi entregue **só** pela doorbell, com o poll da shadow desligado.
- **Sinal independente:** 17 `DbHit ... HitObserved=1` no MEC. Até a 222 era `HitObserved=0` sempre.
- **A doorbell funciona nesta placa pela primeira vez**, e a causa era a abertura do BIF (P2c) (P0).
- A doorbell ficou ativa até a trava: nenhum `NAO ATIVA` nem `TDR_RESTART` depois do StartDevice.
- Base mantida: D2, P7 e `DrenoOk=1`.

## 2. O que aconteceu (P0)

**Trava dura aos ~338 s** (último tique 3383), com a mesma assinatura:
- os últimos eventos são `SUBMIT/RETIRE_SDMA` (fences 11407–11410), todos retirados;
- nenhum evento em voo;
- Kernel-Power 41 e EventLog 6008;
- `RESET_REASON=0x40080400`.

**Contadores até a trava:**
- 11.409 IBs;
- 3.606 Presents;
- zero TDR, timeout, fault, `NO_SPACE`/`NO_MEMORY` ou `DEVICE_REMOVED`.

**Paginação no fim:** rajada de despejo no tique 3360, 2 s antes da trava: 43 despejos, 47,9 MB em um segundo. É o padrão de "rajada antes da trava" já visto nas 207/210.

**Teste de RAM após a trava:** 7.875 MB × 2 passadas, zero erros.

## 3. Veredito: desfecho B

A doorbell ficou ativa e a trava veio igual. **A publicação do WPTR do SDMA0 por MMIO sai das causas suficientes.**

Os ~338 s são o maior tempo antes da trava na série D2–P2 (contra 99–139 s), mas **não são atribuíveis**:
- a 222, sem a P2, passou 600 s;
- o uso nesta corrida foi diferente (Clima aberto);
- a trava é intermitente.

## 4. Os artefatos do Clima: lacuna funcional, não a P2 (P0 + H)

A 223 tem dois tipos de recusa que as 219, 220 e 222 não tiveram (contagem zero):

| Recusa | Contagem | O que diz o log |
|---|---|---|
| `[ResolveGpuVa] RANGE_SCAN RECUSADO ... de_outro_dono=4` | 16 | A origem do `COMPOSITE_BLT` (VA `0x200200000` do processo `0x511`) só existe em alocações **de outro processo**. A regra DX.91 recusa resolver cruzando processos. |
| `[ActiveOpenAlloc] RECUSADO ... result=0xC0000035` | 72 | Abertura de alocação compartilhada recusada com `STATUS_OBJECT_NAME_COLLISION`. |

**Leitura (H):**
- o Clima é UWP e entrega a janela ao DWM por superfície compartilhada entre processos;
- o nosso caminho de composição não abre nem resolve essa superfície, então a janela sai com artefatos;
- nenhum dos dois caminhos é tocado pela P2.

**É uma lacuna funcional real, separada da trava.** Recursos compartilhados entre processos (`OpenAllocation` de alocação já aberta, e a resolução de VA pelo dono certo) precisam ser feitos para qualquer app UWP. Fica registrada como item próprio.

## 5. Próximo passo

**Pelo critério: P1b, o HDP por MMIO em cada submissão do SDMA0.** Na leitura de código hoje, cada submissão faz:
- escrita em `0x385C` (flush);
- leitura de `0x378C`;
- escrita e leitura do `HDP_INVALIDATE` do SDMA0.

Com a D2, o anel e o Work estão na RAM. O DMA buffer do VidMm também está na RAM: `IB_PROOF_BIND PA=0x39EE…`, abaixo da base da VRAM `0x460000000`. Logo a CPU não escreveu nada na VRAM que o SDMA precise ver.

O amdgpu só faz HDP flush pela CPU depois de a CPU escrever na VRAM (`amdgpu_device_flush_hdp`). Ele não faz em cada `set_wptr`.

## 6. Pendências

- **NÃO VALIDADO EM HARDWARE:**
  - 600 s sem trava de forma repetível;
  - a causa da trava dura;
  - composição de superfície compartilhada entre processos.
- **Daniel:** recuperação, fusível, boot frio e CMOS antes do próximo bind.
