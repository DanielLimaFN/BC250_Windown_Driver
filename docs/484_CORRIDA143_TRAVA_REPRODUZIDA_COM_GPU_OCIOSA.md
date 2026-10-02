# 484 — Corrida 143 (DX.90, harness atrasado 150 s): a trava reproduziu, e a GPU estava ociosa até o fim

> **Corrida:** 27/09/2026. Bind às 20:11:01 após boot frio; trava ~15 s depois do início do harness, que começou ~180 s depois do início do log do KMD. O Daniel reiniciou pelo reset. **Candidato:** DX.90 / 3.296.1.0, com `BC250_D3D11_START_DELAY=150`.
> **Evidência:**
> - pasta `docs/evidence/2026-09-27_20-11-01-DESKTOP-HQAG6T4-lab-binding` (sem `-FINAL`);
> - cauda do KMD a partir do offset 3.348.725.095 de `C:\BC250_DISPLAY_LOG.txt`, 194.476 linhas até o reboot;
> - rastro a partir do offset 102.789.136, cortado no reinício do `writer_tick` (1.700 ticks).
>
> Os logs da UMD desta corrida ainda não foram copiados de `C:\Windows\Temp`.
> **Relato do Daniel:** "estava mexendo no sistema, aí travou e deu tela preta, reiniciei".

## 1. Relato obrigatório
**1. Houve avanço?** Sim, de diagnóstico. A trava das 139/140 foi reproduzida de propósito, e a pergunta da DX.90 foi respondida para ela.

**2. Sucessos (P0):**
- **Condição de validade cumprida:** o harness começou com N≈830 (tick ~1.550), acima de 700.
- **O último BGH1 é DEPOIS, completo** (`ReadMask=0x3F`), no tick 1.700, o mesmo tick da última paginação. Não há ANTES órfão.
- Nos últimos 5 ticks (500 ms):
  - anel gráfico ocioso (`CP_RB0_RPTR==WPTR`, 0x3408→0x3414, `pending_dw=0`);
  - fence em memória igual à última submetida (0x1FF→**0x200**; a submissão `RingFence=512` do harness foi **concluída**);
  - `CP_STAT=0`, `GRBM_STATUS=0xA0003028` (a assinatura ociosa medida na 142);
  - MMIO respondendo.
- Até a trava: paginação 35.653/35.653, 609 Presents, zero TDR, fault, `InsideIb` ou `DEVICE_REMOVED`.

**3. Falhas:**
- Trava dura com tela preta ~15 s depois do início do harness.
- O último estágio do rastro é de novo o commit do journal (44), com N=666.
- BCR1 só mostra `PAGING_DDI` (FLUSH_TLB) na CPU 3, sem acesso a registrador em andamento.
- A sequência final do KMD é a mesma das 139/140:
  1. `SubmitVirtual Node1 GFX` do harness (VMID 3, IB de 792 DW em 0x240030000, `SUBMIT SUCESSO`);
  2. `COMPOSITE_BLT OK` 1280x720;
  3. `Present #322` em 653 µs;
  4. `Vsync`;
  5. fim.
- 140 `NO_SPACE`.

**4. Veredito frente ao critério do doc 483: C.** Houve trava; os últimos DEPOIS são ociosos (`pending=0`, `fence_behind=0`, `CP_STAT=0`) até o fim.
- **Ressalva de escopo:** a amostra é de 100 ms. Depois da última amostra, o KMD não registrou nenhuma submissão gráfica nova; logo, na janela medida, a GPU não tinha trabalho pendente.
- **Implicação:** a GPU não travou antes da máquina, e o MMIO não parou. O problema está do lado da CPU/SO.

**5. Recuperação e pendências:**
- Ao analisar, o watchdog ainda estava em OBSERVING (prazo por volta das 20:21). Rollback e `CLEAN_BOOT_REQUIRED` só depois dele.
- Os logs da UMD precisam ser copiados com o script elevado.
- **NÃO VALIDADO EM HARDWARE:** a causa.

## 2. O que isto elimina e o que sobra
- **Eliminado (P0 na janela de 100 ms):** trava de GPU no trabalho gráfico do harness, e MMIO/barramento sem resposta a registradores.
- ~~Não eliminado: acesso da CPU à VRAM pela BAR. O BGH1 lê registradores, não VRAM.~~ **Corrigido na seção 3:** o writeback e a fence que o BGH1 lê ficam na VRAM, pela BAR0. O caminho CPU→VRAM também respondia 100 ms antes. Continua possível só um acesso travado a VRAM *depois* da última amostra.
- **Não eliminado:** espera circular ou IPI entre CPUs em IRQL elevado.
- A máquina inteira parou em menos de 100 ms, inclusive a thread do rastro em PASSIVE, o que aponta para algo que para todas as CPUs.
- **Próximo dado:** logs da UMD desta corrida, para ver a última linha de cada processo no instante da trava. Em seguida, com base neles, decidir a instrumentação seguinte.

## 3. Logs da UMD (copiados depois do rollback; 6 arquivos com SHA-256 em `umd-logs-pos-travamento`)
Última linha de cada processo vivo, todas entre 20:14:26,05 e 20:14:26,18:
- **harness (7536):** `DXGI Present ENTER cb` e `PRESENT_CB phase=ENTER`, **sem LEAVE**. O KMD registrou o `DdiPresent` correspondente concluído (`Present #322`, 653 µs). **Na 139 a última linha do harness também era `PRESENT_CB ENTER`**; na 140 era `FlushCommands ENTER`.
- **DWM (7380):** `Upload LEAVE hr=0` de 11.059.200 bytes (`UpdateSubresourceUP` 2560x1080, cópia pela CPU), concluído.
- **Explorer (5748):** `DRENO antes de destruir recurso: DrawsPendentes=1` (duas vezes), ou seja, entrando num flush de desenho dentro de `DestroyResource`.

**Leitura:** o anel gráfico e a sua área de writeback/fence estão na VRAM, lidas pela CPU pela BAR0. Logo o batimento do tick 1.700 prova que, 100 ms antes, **respondiam tanto os registradores quanto a VRAM acessada pela CPU** (P0).
- O `pfnPresentCb` do harness não voltou depois de o nosso `DdiPresent` retornar. O que o dxgkrnl faz depois (fila do Present, submissão do DMA buffer no Node 0, nossa ISR/DPC) roda em IRQL elevado.
- Linhas de log em IRQL elevado vão para o anel em RAM e se perdem na trava.
- Uma CPU presa com interrupções bloqueadas faz as outras pararem no próximo IPI (TLB flush), o que casa com "a máquina inteira em <100 ms". Isso é H.

## 4. Próximo passo: Corrida 144, sem rebuild
Reproduzir a 143 (mesmo runner e pacote) e, **na trava, forçar o dump pelo teclado**: Ctrl direito + Scroll Lock duas vezes. `CrashOnCtrlScroll` está armado em `i8042prt` e `kbdhid`, e o dump de kernel está configurado; o preparo confere as duas coisas. O dump traz a pilha de **cada CPU**, que é o dado que nenhuma instrumentação nossa consegue gravar.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Dump gerado (`MEMORY.DMP` novo, BugCheck 0xE2 MANUALLY_INITIATED_CRASH) com uma CPU dentro de `bc250mcdm` | A função nomeada é o ponto de parada. |
| **B** | Dump gerado, sem quadro nosso nas CPUs presas (por exemplo, dxgkrnl/VidSch ou KeFlushTb) | O mecanismo está no SO/dxgkrnl; a pilha diz qual. |
| **C** | Ctrl+Scroll Lock não gera dump (máquina não responde ao teclado) | Todas as CPUs presas com interrupções bloqueadas ou barramento travado. Próximo: instrumentar ISR/DPC nas migalhas. |
| **D** | Sem trava (N ≥ 700 no início do harness) | Não reproduziu; repetir. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |
