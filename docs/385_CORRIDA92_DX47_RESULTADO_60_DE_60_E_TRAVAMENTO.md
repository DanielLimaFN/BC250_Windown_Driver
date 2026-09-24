# Corrida 92 — DX.47: 60/60, zero TDR, preempção concluída; o PC travou depois do harness

> 23/09/2026. Bind às 19:12:54, janela de 300 s. O harness terminou às 19:15:31
> com **EXIT 0**. A máquina travou logo depois: nenhum arquivo foi escrito entre
> 19:15:32 e o reboot manual do Daniel, uns 8 minutos depois.
> - Boot novo às 19:25:19; o guardião retomou e fez `RECOVERY_OK_ONLINE` às
>   19:26:00.
> - Sem BugCheck: o `MEMORY.DMP` é das 09:42.
>
> **Candidato:** lab `3.252.1.0` (SYS `CFCDF849…`).
>
> **Critério:** [docs/384](384_DX47_PREEMPCAO_CONCLUIDA_E_CRITERIO_CORRIDA92.md).
>
> **Evidência:** `docs/evidence/2026-09-23_19-12-54-DESKTOP-HQAG6T4-lab-binding/`.
> - Sem FINAL (o binder morreu no travamento).
> - A cauda do KMD foi resgatada em `bc250_display_log-tail-recovered.txt`:
>   linhas 1–73039 são da corrida; a 73040 já é o boot seguinte
>   (`[FUSE] STARTDEVICE RECUSADO`).
> - Os logs da UMD e do harness são os da janela.

## 1. Houve avanço?

**Sim, o objetivo de GPU da série foi alcançado; e apareceu uma falha grave.**
- **Harness 60/60:** "Quadros com Present aceito: 60 | Presents falhos: 0",
  `EXIT CODE FINAL: 0`. É a primeira vez em toda a série.
- **Zero TDR** na corrida inteira.
- **Depois do harness, travamento duro da máquina.** É o segundo da série,
  depois da 88.

| Medida | 91 | **92** |
|---|---|---|
| Harness | 53/60, device removed | **60/60, EXIT 0** |
| TDRs | 1 (preempção sem resposta) | **0** |
| Preempção | 1 pedido, sem resposta | **1 pedido, 1 `PREEMPT_DONE`** |
| WPTR máximo | 19080 | **30872** (quase duas voltas do anel) |
| Submissões GFX | 1316 | **1416** |
| Travamento | não | **sim, depois do harness** |

## 2. Sucessos

- **DX.47 validada (P0):**
  - `[PREEMPT_DONE] Node=1 Engine=0 PreemptionFenceId=1
    LastCompletedFenceId=172 -- DMA_PREEMPTED notificado com o no drenado`;
  - o log de `REGISTRADO` sai logo depois porque o DPC concluiu antes de a DDI
    escrever a linha dela;
  - zero `PREEMPT_WAIT` e **nenhum TDR ocioso** depois disso.
- **Anel:** o WPTR foi a 30872 sem TDR (DX.46 de novo).
- **DX.43, sétima amostra:** 323/323 aquecimentos e zero TDR de VMID anterior
  (1262 trocas em 86–92). Zero `Cid=9`.
- 4 `BYTE_PROOF PASS` de tela cheia. Autotestes RADV 3/3.
- **Guardião durável (DX.45b host) validado no primeiro travamento real:**
  depois do reboot manual, a tarefa `AtStartup` rodou (19:25:52), fez o
  rollback e confirmou o BasicDisplay em Código 0. Na 88, sem essa correção, o
  driver ficou vinculado.

## 3. Falhas

- **Travamento duro depois do harness:** o binder e o guardião não
  conseguiram agir (nada em user-mode rodava) e foi preciso reboot manual.
- **Assinatura, 2 de 2 travamentos (P0):** a última linha gravada é
  `[McdmQAI] Type=29, OutSize=4, CapsStatus=0xC00000A3`.
  - Em todas as corridas essa linha aparece **uma vez**, no início do bind
    (normal: consulta antes do StartDevice).
  - **Só nas duas que travaram (88 e 92) ela aparece uma segunda vez**, no meio
    da corrida, como última linha.
- **Leitura do código (P0):**
  - `Type=29` é `DXGKQAITYPE_WDDMDEVICECAPS`, que o dxgkrnl consulta logo
    depois de um `DxgkDdiAddDevice` (comentário do próprio handler);
  - `CapsStatus=0xC00000A3` sai quando o contexto **não passou pelo
    StartDevice** (sem inventário selado e sem BARs resolvidos);
  - portanto, no meio da corrida o dxgkrnl consultou um contexto de adaptador
    **ainda não iniciado**.
- **Lacuna de instrumentação:** `AddDevice` não escreve nenhuma linha, e o
  QAI não imprime o `hAdapter`. Não dá para provar se era um segundo
  `AddDevice` nem o que aconteceu depois.
- **Hipótese (H):** uma segunda instância do adaptador foi criada no meio da
  corrida, e o que vem depois (StartDevice ou reprogramação de hardware com a
  primeira instância viva) trava a máquina.
- **Visual:** sem relato; desktop e interação **NÃO MEDIDOS**.

## 4. Veredito frente ao critério

- **Desfecho A para a DX.47, com todos os sinais:**
  - todo pedido de preempção foi concluído (1/1);
  - nenhum TDR ocioso;
  - harness 60/60.
- **E, fora do mapa:** travamento duro depois do harness, com a mesma última
  linha da 88.
- **Função:** o harness passou. Mas o driver **não está estável**: travou a
  máquina, e desktop/interação não foram medidos.

## 5. Recuperação e próximo passo

- **Recuperação:** `RECOVERY_OK_ONLINE` às 19:26:00 pelo guardião no boot
  seguinte, BasicDisplay em Código 0, fusível limpo. **Desligamento completo
  antes de outra corrida.**
- **NÃO VALIDADO EM HARDWARE:**
  - causa do travamento;
  - desktop interativo;
  - estabilidade por minutos;
  - desempenho (~0,5–1 FPS).

**Próximo passo proposto (DX.48, só medição):**
- `DxgkDdiAddDevice`, `StartDevice`, `StopDevice` e `RemoveDevice` passam a
  escrever uma linha síncrona de entrada e saída, com o ponteiro do contexto e
  o PDO;
- o `[McdmQAI]` passa a imprimir o `hAdapter` e se ele é o contexto já
  iniciado (diretório de adaptadores).

Se o travamento se repetir, o log vai dizer se há uma segunda instância, quem é
ela e qual foi a última etapa antes de parar.
