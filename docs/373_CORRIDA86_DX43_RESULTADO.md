# Corrida 86 — DX.43: o TDR de VMID anterior não aparece; sobra um TDR com a GPU ociosa

> **Data:** 23/09/2026. Bind às 14:55:26, TDR às 14:56:14 (Event Log 4101,
> "parou de responder e se recuperou"), `RECOVERY_OK_ONLINE` às 14:57:36.
>
> **Candidato:** lab `3.247.1.0`, SYS `9D28E368…`, conferido em
> `binding-provenance.json`; UMD inalterada; RADV DX.42 `FCAA11FE…`.
>
> **Critério:** [docs/372](372_DATACAO_TDR_CORRIDA85_E_DX43_CRITERIO_CORRIDA86.md).
>
> **Evidência:** `docs/evidence/2026-09-23_14-55-26-DESKTOP-HQAG6T4-lab-binding/`.
> - O FINAL veio com `complete_at_capture=false`, mas faltavam poucos bytes; a
>   cauda foi resgatada em `bc250_display_log-tail-recovered.txt` (SHA-256
>   `80952886…`) e confere com as contagens do FINAL.
> - A coleta RADV pós-rollback **não tinha rodado** no wrapper (faltava o JSON,
>   1 arquivo e 2 fluxos cresceram depois). Rodei o coletor manualmente depois:
>   281 IB/meta, 9 fluxos, 2 atualizados, índice regenerado. A causa da falha do
>   wrapper é **NÃO DETERMINADA**.

## 1. Houve avanço?

**Sim, funcional e de diagnóstico.** Mas não fecha desktop.

| Medida | 84 | 85 | **86** |
|---|---|---|---|
| Submissões gráficas | 104 | 316 | **491** |
| Trocas de VMID | 49 | 47 | **106** (106 com aquecimento) |
| TDRs | 4 | 4 | **1** |
| TDRs com ondas no VMID anterior | 3 | 4 | **0** |
| Faltas `Cid=9` (busca de instrução) | 3 | 4 | **0** |
| Harness | 1/60 | 0/60 | **2/60** |
| Autotestes RADV 5/5 | 1 de 3 | 1 de 3 | **3 de 3** |
| `BYTE_PROOF PASS` de tela cheia | 0 | 0 | **2** (Present #1 e #25, 256/256) |

Daniel viu o desktop travado e, num momento, o quadrado azul com o triângulo
por cima do desktop.

## 2. Sucessos

- **O aquecimento foi exercitado sempre (P0):** 106 de 106 trocas com
  `DX43_VMID_WARMUP_IB Emitido=1 Motivo=EMITIDO Head=0xC0012800[lido=1]`, sem
  nenhum pulo.
- **Nenhum TDR da classe de VMID anterior (P0)** e nenhuma falta de busca de
  instrução, em 106 trocas.
  - Na taxa das 84/85 (7 TDRs dessa classe em 96 trocas), seriam esperados ~8
    em 106.
  - Observar zero é forte evidência estatística (probabilidade ≈ e⁻⁸), mas
    ainda é **uma corrida**.
- **O aquecimento não travou o CP:** o único TDR teve a fence 188 escrita (a
  do IB com aquecimento), RPTR==WPTR e zero ondas. Não é o desfecho D.
- **Controles:**
  - restart GFX/SDMA provado;
  - `TDR_FENCE_RECONCILE PASS` com `Committed=0`;
  - sem BugCheck;
  - 414 `RADV_PREAMBLE_IDENTITY` e nenhum prefixo zero.
- **Display:** 2 `BYTE_PROOF PASS` de tela cheia (Present #1 e #25), a primeira
  vez nesta série; 54 `PRIMARY_COPY`.

## 3. Falhas

- **Um TDR, de outra classe (desfecho E):**
  - GFX ocioso: `Submetidas=188 Aposentadas=188 EmVoo=0`, fence 188 escrita
    pela GPU e `RPTR=WPTR=5244`;
  - `SQ_DEBUG_STS_GLOBAL=0`, sem ondas; SDMA também ocioso (`STATUS1=0x3FF`);
  - `TDR_FENCE_RECONCILE Committed=0`: não havia fence nossa pendente;
  - sem falta latchada;
  - ME no `PFP_SYNC_ME`, PFP em opcode `0x0E`, `GfxContextBusy=1`,
    `GRBM CP_BUSY=1`;
  - `IB1` aponta para o IB do harness (fence 188), que **terminou**.
  - Sem live dump em `LiveKernelReports`, então **não se sabe qual nó o
    dxgkrnl considerou travado**.
- **Harness:** 2/60. O quadro 3 falhou com `DXGI_ERROR_DEVICE_REMOVED`, motivo
  `0x887A0020` (`DRIVER_INTERNAL_ERROR`), depois do TDR.
- **Display:**
  - desktop travado;
  - 14 `DIRTY_RECTS_RECUSADOS` caindo em cópia de tela cheia, contra 4
    aceitos;
  - uma escrita CB sem permissão em `0x2401A2000` (`Vmid=1`, `Cid=0`), a frente
    de render target, ainda aberta.

**Achado de código ligado ao TDR ocioso (P0 no código, H como causa).**
`Bc250McdmPreemptCommand` (`bc250_mcdm_callbacks.c:1892`) devolve
`STATUS_SUCCESS` sem fazer nada e sem log, enquanto o driver declara
`PreemptionAware=1` com granularidade `DMA_BUFFER_BOUNDARY`.

- Pelo contrato WDDM, depois de um `PreemptCommand` o driver precisa reportar
  `DXGK_INTERRUPT_DMA_PREEMPTED`.
- Se isso nunca chega, o VidSch declara TDR **mesmo com a GPU ociosa**, o que
  casa com este TDR.
- É também violação da Lei 4: DDI sem backend devolvendo sucesso.
- **A corrida não prova que o dxgkrnl chamou a DDI:** ela é muda, e o registro
  de probes some com o rollback.

## 4. Veredito frente ao critério

- **Desfecho A, com todos os sinais:**
  - 106 trocas com `Emitido=1`, acima do mínimo de 30;
  - zero TDR com ondas no VMID do IB anterior;
  - o único TDR não tem ondas.
- **Implicação:** o VMID do dispatch passa a atualizar numa fronteira de IB. A
  DX.43 fica como correção provisória. **Uma corrida não prova eliminação.**
- **O TDR restante é E, fora do mapa:** GPU ociosa, sem fence nossa pendente.
- **Função: falha.** 2/60, device removed, desktop travado.

## 5. Recuperação e pendências

- **Recuperação:** `RECOVERY_OK_ONLINE` às 14:57:36. O marcador de boot frio
  foi regravado. **Desligamento completo antes de outra corrida.**
- **NÃO VALIDADO EM HARDWARE:**
  - eliminação definitiva da classe de VMID anterior (pede repetição);
  - causa do TDR ocioso;
  - desktop íntegro, 60/60;
  - causa da falha da coleta pós-rollback no wrapper.

**Próximo passo proposto (DX.44):** tornar `Bc250McdmPreemptCommand` falante.

- Registrar, sem mudar comportamento: entrada, `NodeOrdinal`,
  `PreemptionFenceId`, a última fence submetida e aposentada do nó, e o
  retorno.
- Se ela aparecer logo antes de um TDR ocioso, a causa fica identificada, e a
  correção seguinte é reportar `DMA_PREEMPTED` depois de o nó drenar (legal
  para a granularidade `DMA_BUFFER_BOUNDARY`).
- Se ela não aparecer, a hipótese cai e o próximo suspeito é o caminho de sinal
  de fence monitorada. O harness fez dois submits `cs=0` com sinal logo antes
  do IB 188.
- A DX.43 continua no candidato.
