# 606 — Corrida 227 (repetição da 226): tela azul 0x119/0x9000 no flip MPO3

**Corrida 227:** 05/10/2026 16:57:47 · lab **3.374.1.0** (SYS `8FC974FA…`, mesmo pacote da 226) · critério: [doc 605](605_CORRIDA226_P1B_ATIVA_600S_SEM_TRAVA_DURA_E_CRITERIO_DA_REPETICAO.md) §4
**Evidência:** `docs/evidence/2026-10-05_16-57-47-DESKTOP-HQAG6T4-lab-binding/`. A coleta automática do boot das 17:00:30 trouxe:
- `display-log-this-bind-RECUPERADO.txt`;
- `dump-analyze-RECUPERADO.txt`;
- o rastro de paginação;
- o teste de RAM;
- o motivo do reset.

## 1. O que aconteceu (P0)

**Pré-condição válida:**
- `[P1B_VRAM_DRAM] ATIVO` (alias por escrita 1024/1024);
- StartDevice `status=0`;
- D2, P2 e P7 ativos, GFX online;
- zero `[HDP1_FLUSH]`/`[HDP_PT_FLUSH]` em regime.

**Tela azul `0x119 VIDEO_SCHEDULER_INTERNAL_ERROR`** (Arg1=`0x9000`, Arg3=`0xFFFFFFFFFFFFFFFF`), cerca de 2 min depois do bind, com 4.294 IBs. `MEMORY.DMP` de 833 MB.

**Pilha (do dump):**
1. `bc250mcdm` (nossa DPC de timer de VSync);
2. `dxgkrnl!DxgNotifyDpcCB`;
3. `dxgmms2!VidSchiProcessDpcVSyncCookie`;
4. `VidSchiExecuteNextFlipQueueEntry`;
5. `VidSchiExecuteMmIoFlip`;
6. `VidSchiExecuteMmIoFlipMultiPlaneOverlay3`;
7. `watchdog!WdLogEvent5_WdCriticalError`;
8. `KeBugCheckEx`.

`FAILURE_BUCKET_ID: 0x119_9000_UNKNOWN_dxgmms2!VidSchiExecuteMmIoFlipMultiPlaneOverlay3`.

**Antes do bugcheck, no log:** zero TDR, zero timeout, zero fault, zero `DEVICE_REMOVED`.
- A última cópia da primária registrada, `[PRIMARY_COPY #1023] END Route=MPO3 Sdma=YES`, terminou OK.
- Não há linha `FAIL` nem `MPO3 FAIL_CLOSED`: o bugcheck cortou a drenagem do log (doc 582, o fim do log não é o fim).

**Pós-trava:**
- teste de RAM 8.866 MB × 2, zero erros;
- `RESET_REASON=0x40080400`, igual ao das travas duras;
- fusível disparado, StartDevice recusado no boot seguinte (como deve);
- watchdog com `RECOVERY_OK_ONLINE`, BasicDisplay em Código 0.

## 2. Leitura de código (P1, sobre a pilha P0)

`Bc250DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3` (`bc250_display_ddi.c`):
- chama `Bc250DisplayCopyPrimaryToGop("MPO3", ...)`;
- **se a cópia falhar, devolve `STATUS_INVALID_PARAMETER`** (fail-closed declarado no comentário);
- `Bc250DisplayCopyPrimaryToGop` falha com `GPU_EXCEPTION` quando `Bc250SdmaPresentBlt` devolve FALSE (`Sdma=0`).

Um flip por MMIO que o driver recusa leva o `dxgmms2` a `WdCriticalError` e `0x119`. Isso é exatamente a pilha.

- **P2:** a flip #1024 falhou na cópia SDMA para o scanout e o nosso fail-closed virou tela azul. A pilha casa; a linha de falha se perdeu.
- **H:** o motivo da falha do `Bc250SdmaPresentBlt` (timeout, anel ocupado ou outro). Não está no log.

**Histórico:**
- Em todas as corridas de 05/10, `PRIMARY_COPY ... FAIL` = 0.
- A 226 fez 4.224 cópias MPO3 sem falha; a 227 fez 1.023 antes da que falhou.
- `SAMPLE_UNAVAILABLE` aparece em todas desde a 219, então não é efeito da P1b.

## 3. Veredito: desfecho E (fora do mapa)

A repetição **não** chegou a 600 s, mas também **não** reproduziu a assinatura da trava dura (B): aqui o driver detectou uma falha e a máquina caiu por bugcheck com dump.

- **Não fecha:** que a P1b elimine a trava dura. A contagem segue 1 corrida de 600 s em 2 com a P1b.
- **Informação nova:**
  - com a P1b o sistema morreu de modo **observável** (cópia SDMA do flip falhou), não em silêncio;
  - o `RESET_REASON=0x40080400` também aparece depois de um bugcheck comum, então ele **não distingue** trava dura de tela azul. Correção de leitura das corridas anteriores: esse valor nunca foi assinatura da trava.

## 4. Próximo passo (offline, antes de nova corrida)

1. **Ler `Bc250SdmaPresentBlt`** (`bc250_sdma_present.c:1269`) e listar cada saída FALSE com a sua condição.
2. **Fazer o motivo sobreviver ao bugcheck:**
   - gravar o código da falha da cópia MPO3 no registro persistente do rastro de paginação (BEV1, que sobrevive ao reboot) **antes** de devolver o erro;
   - logar a linha `FAIL` antes da amostragem do destino.
3. **Decidir o contrato da falha:** se o flip MPO3 deve continuar devolvendo erro (`0x119`) ou cair no caminho de TDR. Decisão do Daniel, depois de saber o motivo.

## 4b. Motivo, extraído do dump depois (doc 607 §6)

`Blt Timeout #1` com `FenceExp=0x2CE2`, `FenceObs=0x2CE3`, `RPTR=WPTR` e o SDMA0 ocioso. A cópia terminou e a composição seguinte sobrescreveu a fence compartilhada; a igualdade exata nunca casou. É um defeito de host (corrida entre o flip e a composição), corrigido na FB.2.

## 5. Pendências

**NÃO VALIDADO EM HARDWARE:**
- a P1b eliminar a trava dura;
- o motivo da falha da cópia SDMA na flip #1024.
