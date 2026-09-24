# DX.54: velocidade do desktop; critério da Corrida 99

> 24/09/2026. Faixa **offline**, sem bind.
> **Base:** [Corrida 98R](398_CORRIDA98R_DX53_RESULTADO.md), primeira janela sem travamento desde a 93. O operador descreveu o desktop como "a 1 FPS".
> **Decisão do Daniel:** atacar a velocidade (e o agrupamento sem piscar) antes de repetir a janela longa. Três peças comportamentais na mesma corrida, cada uma com interruptor próprio para bissecção.
> **Teto:** uma corrida, depois de desligamento completo.

## 1. Onde o tempo vai (mineração da 98R, P0 de tempo)

**Fonte:** log durável do dwm.exe (`bc250_umd_log_1204-FINAL.txt`), soma dos intervalos entre linhas consecutivas.

| Trecho | Vezes | Média | Total |
|---|---|---|---|
| `FlushCommands ENTER` → conclusão | 3908 | 16,3 ms | 63,7 s |
| upload do CB → `[VK_DESCRIPTOR] BOUND` | 2062 | 13,9 ms | 28,6 s |
| `INSPECAO LINEAR_BUF` (grade lida pela CPU) | 449 | 28,8 ms | 12,9 s |
| linha → linha do trace durável | milhares | ~0,2 ms | — |

**Intervalo entre Presents do DWM:** p50 391 ms, p90 1864 ms, máximo 7,4 s (n=408).

**O dado que decide:** o flush leva ~14 ms, mas a espera pela fence dentro dele foi **`EsperaUs=190`**. O custo é de CPU, antes da espera (fim do command buffer e `vkQueueSubmit` no RADV/WDDM), e não da GPU.

**Entre o upload e o `BOUND`, só há:**
- `vkCreateImageView`;
- `vkCreateSampler`;
- **`vkCreateDescriptorPool` novo por draw**, que no RADV aloca um BO (ida ao kernel) e é destruído depois do flush;
- mais a alocação e a atualização do conjunto.

**No KMD:** 72.892 `FLUSH_POOL_VMIDS` (invalidação de TLB) em ~5 min, ~243/s. É compatível com mapeia/desmapeia por draw somado ao laço do Explorer. **H**, não atribuído.

**Log de fluxo do RADV:** `bc250_diag_printf` abre e fecha o arquivo a cada linha até 4 MB por processo. O dwm.exe bateu o teto às 09:27, então isso pesou só nos primeiros ~3 min. Não mexido (está fora deste repositório).

## 2. A mudança (três peças, três interruptores)

### 2.1 Pool de descritores persistente — `s_Bc250Dx54PersistentDescriptors = TRUE`

- **Um pool por device:**
  - criado no primeiro draw com descritor;
  - 64 conjuntos, 1792 CB, 2048 SRV, 2048 samplers (`BC250_DX54_POOL_*`).
- **Reset:** `vkResetDescriptorPool` **só** no ramo de fence concluída do flush, depois de liberar os lotes. Com fence falhada, os conjuntos ficam presos até o shutdown.
- **Pool esgotado ou recusado:** o draw usa o pool próprio de antes, contado (`DescriptorFallbackPools`) e dito no log: `[DX54_DESC] POOL POR DRAW (fallback #N)`, 8 primeiros e depois 1 em 256.
- **Cache de samplers** (64 entradas):
  - chave = `VkSamplerCreateInfo` inteiro, zerado com `memset`;
  - sampler do cache **nunca** entra no lote que é destruído;
  - pool e cache são liberados no `BridgeShutdown`.
- **Views:** continuam por draw (sem BO no RADV).

### 2.2 Agrupamento religado com anel pequeno — `s_Bc250Dx51DrawBatchEnabled = TRUE`

- **Anel:** 16 MB → **4 MB**. O dwm.exe usa ~180 KB por draw (VB 160 KB + IB 16 KB + CBs), então 4 MB guardam ~22 draws. Anel cheio drena antes do draw (lógica da DX.49).
- **Criação:** o anel só nasce num device com `NativeVkDrawCount >= 32` (`BC250_DX54_RING_MIN_DIRECT_DRAWS`). Os devices do laço do Explorer fazem 1 draw cada e morrem; **nunca criam anel**.
- **H que isto testa:** o piscar da 96 era a ponte perdendo residência (438 `SpaceResolveAddress status=9`) com 16 MB mapeados por device, inclusive nos ~36 devices/min do Explorer.

### 2.3 Inspeção do Present amostrada — `s_Bc250Dx54SampledPresentInspection = TRUE`

- **Regra:** a grade do buffer linear é lida nos 16 primeiros Presents do device e depois em 1 a cada 64.
- **Fora da amostra:** o trace diz `NonZeroGrade=NAO_AMOSTRADO`, nunca um número. Linha única `[DX54_INSPECAO]` no 17º Present.

### 2.4 Só medição

- **Flush:** `[DX54_TEMPO] FimCbUs SubmitUs DescritoresNoLoteUs PoolResets PoolsPorDraw`.
- **`BOUND`:** ganhou `[DX54_DESC] Pool=PERSISTENTE|POR_DRAW DescUs=`.

### 2.5 Sem mudança

- **KMD:** DX.53 (VMID desarmado antes da devolução) e DX.52. O SYS foi recompilado, e as marcas da DX.53 estão no binário.
- **Dirty rects:** desligados (DX.50).
- **Explorer:** o laço **continua**. A causa da recusa da textura compartilhada (Texture2D 1024×832 BGRA, RT|SRV|PRESENT, MISC_SHARED) não é identificável offline. O KMD aceita, o dxgkrnl recusa, e a sequência de DDIs dele cai na amostragem do log.

## 3. Verificação offline

- **Build:**
  - `build-umd` EXIT=0;
  - `umd_descriptor_binding_test` com bloco novo DX.54: 64 draws usam 1 pool, sampler criado 1 vez, 65º conjunto sem reset cai no pool por draw contado, lote não destrói sampler do cache nem pool persistente. Os casos antigos rodam com o interruptor desligado;
  - `umd_dx49_batch_test` PASS.
- **Portão novo `audit_dx54_desktop_speed`:** substitui o `audit_dx51_draw_batch_bisection`, que exigia lote desligado. `tools/test-dx54-speed-mutations.py` **8/8 reprovam**. A primeira versão passou uma mutação, porque o alvo aparecia duas vezes; o alvo foi apertado.
- **`tools/test-dx51-batch-bisection-mutations.py`:** fica **obsoleto** (os alvos dele exigem `FALSE`).
- **Demais suítes:** DX.49 19/19, DX.50 6/6, DX.52 6/6, DX.53 5/5, auditoria de segurança PASS.
- **Incidente de ambiente:** a suíte DX.49 falhou duas vezes com `OSError [Errno 22]` ao escrever arquivo. Na segunda, a **restauração** falhou e deixou `bc250_umd_blt.inl` mutado (dreno do BltViaKmd removido). A auditoria pegou (`DX.49: BltViaKmd sem dreno do lote`), o dreno foi restaurado à mão, e a terceira execução passou 19/19. A UMD e o SYS foram recompilados **depois** disso.

## 4. Candidato

| Componente | Valor |
|---|---|
| KMD | lab `3.260.1.0`, SYS `CF03A31B9C1235F22040FDF44D4ECF8E77552CEB1E3C19A2A45A039CD2BDD0EE`, INF `EE6E5196…C715`, CAT `0E0E10F9…A3A5`. Auditoria do pacote PASS |
| UMD | `B884F0F342276B35929CFD74A2C9999E7B0C0A2D17C60A945EADF044E22115DE`; marcas DX.54 conferidas no binário |
| RADV | DX.42 `FCAA11FE…` (inalterado) |
| Pré-voo | `tools/prepare-corrida99-dx54.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA99_DX54.cmd`, elevado, **600 s**, depois de **desligamento completo** (marcador do rollback de 09:29:50 armado) |

## 5. Critério, escrito antes

**Controle (tem de aparecer, senão a corrida não mediu a DX.54):**
- no dwm.exe:
  - `[DX54_DESC] POOL PERSISTENTE criado`;
  - maioria dos `BOUND` com `Pool=PERSISTENTE`;
  - `[DX49_BATCH] ANEL de dados por draw criado: 4194304 bytes`;
  - flushes com `DrawsNoLote` > 1;
  - `[DX54_INSPECAO]`;
- nos logs do Explorer: **zero** `ANEL de dados por draw criado`;
- no KMD: `[DX53_VMID] DESARME ANTES DA DEVOLUCAO` presente.

**Métricas (comparadas com a 98R):**
- `DXGI Present LEAVE hr=0` por minuto no dwm.exe (98R: 87–120);
- intervalo entre Presents p50 (98R: 391 ms);
- `DescUs` mediano (98R: ~14–24 ms, estimado pelo intervalo);
- `SubmitUs`/`FimCbUs`;
- `SpaceResolveAddress status=9` no KMD (98R: 0; 96: 438).

| | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem travamento** (guardião reverte sozinho) **e** Present/min ≥ 2× a 98R (≥ ~220/min) ou p50 ≤ 200 ms **e** zero `SpaceResolveAddress status=9` | Velocidade ganha sem reabrir o piscar; estabilidade além de 5 min medida. Próximo: o laço do Explorer e o próximo custo nomeado por `[DX54_TEMPO]`. |
| **B** | Sem travamento, com ganho de velocidade, mas `SpaceResolveAddress status=9` > 0 ou piscar relatado | O anel ainda pesa na residência. Próxima corrida: `s_Bc250Dx51DrawBatchEnabled = FALSE`, pool persistente mantido. |
| **C** | Sem travamento e sem ganho (< 1,3× a 98R) | O custo dominante está em outro lugar. `SubmitUs`/`FimCbUs`/`DescUs` dizem onde. Não religar mais nada às cegas. |
| **D** | Travamento (guardião não reverte, ou reboot forçado) | A DX.54 ou a instabilidade além de 5 min. Bissecção pelos interruptores, começando pelo lote (estava ligado nas 94–96). A 98R mediu só 5 min. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Regras:**
- **Desfecho parcial não é desfecho:** A exige os três sinais.
- **Controle ausente** (por exemplo, sem `POOL PERSISTENTE criado` no dwm.exe): a corrida não mediu a DX.54, seja qual for o desfecho visual.

**Secundárias:**
- `PoolsPorDraw` (fallback) perto de zero;
- carga do Explorer comparável à 98R (~290 contextos/min);
- zero `QUARENTENA`;
- descrição do operador (fluidez, piscar).

**Depois da corrida:** as 5 seções e a tabela-checklist.
