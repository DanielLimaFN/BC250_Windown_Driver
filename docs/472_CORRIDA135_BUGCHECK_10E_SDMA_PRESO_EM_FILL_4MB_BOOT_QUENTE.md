# 472 — Corrida 135 (DX.87, repetição): BugCheck 0x10E após SDMA preso dentro de um IB de fill de 4 MB — boot QUENTE, protocolo violado

> **Corrida:** 26/09/2026, bind 21:50:57, BugCheck ~21:52, reboot 21:53:33. **Candidato:** DX.87 / 3.292.1.0 (mesmo da 134).
> **Evidência:** `docs/evidence/2026-09-26_21-50-57-DESKTOP-HQAG6T4-lab-binding` (sem `-FINAL`), cauda do KMD a partir do offset 2.900.853.021 de `C:\BC250_DISPLAY_LOG.txt`, rastro a partir do offset 51.498.736, `C:\Windows\MEMORY.DMP` (830.029.178 bytes, 21:53). **O dump ainda não foi aberto:** exige elevação.
> **Relato do Daniel:** "deu tela azul, porém esqueci de desligar da tomada". A corrida rodou depois de reboot a quente, sem o boot frio que a norma exige. O preflight não distingue reboot a quente de boot frio.

## 1. Relato obrigatório
**1. Houve avanço?** Não funcional. É aprendizado de diagnóstico, **confundido** pelo boot quente: o estado de engine da 134 pode ter sobrevivido ao reboot.

**2. Sucessos:** o mecanismo de dump funcionou (primeiro dump novo desde 24/09). A paginação estava rápida até a falha: 33.856 flushes e 848 Presents no KMD.

**3. Falhas (P0 no log do KMD):**
- IB `hw_fence=13042` (dxg 7837, 32 bytes, VMID8, `DEST [0x250000, 0x650000)` = 1.024 páginas) **nunca concluiu**.
- `AVISO CONTEXT SWITCH COM IN_FLIGHT: WPTR=645948 RPTR=645792 STATUS=46DE6756 (Idle=0 InsideIb=1) ERR_LOG=0x0F FenceObs=43DAFFFE`.
- `[SdmaPresent] Blt Timeout #1` e `COMPOSITE_BLT FALHOU no silicio` (cópia de 11 MB para o scanout `0xF400AAC000`).
- ~1 s depois: **BugCheck 0x10E (VIDEO_MEMORY_MANAGEMENT_INTERNAL), argumentos (4, 0, 0xA, 0)**. O significado do subcódigo 4 só sai da análise do dump; não se infere.
- O rastro mostra a paginação parando ~1,94 s antes do fim do escritor. Nenhuma migalha em fase ANTES no último registro: a CPU não estava presa em MMIO; a GPU é que parou.

**4. Veredito frente ao critério do doc 471 (Corrida 135): E — fora do mapa.** Nem trava dura nem 600 s: BugCheck com engine SDMA presa dentro de um IB. E a corrida não vale como repetição, porque o protocolo de boot frio foi violado.

**5. Recuperação e pendências:** reboot pelo BugCheck. Salvar o `MEMORY.DMP` antes da próxima corrida (será sobrescrito). **NÃO VALIDADOS EM HARDWARE:** estabilidade repetível, causa do travamento do SDMA.

## 2. Leitura
- É a mesma classe de uma dívida antiga e aberta: SDMA parado dentro do IB (`InsideIb=1`) num fill grande sob VMID8, visto em 09/09 com o IB de 11 MB e `GFXHUB_FAULT 00841B40`. Aqui não houve linha de `GFXHUB_FAULT`.
- Não há como separar "defeito nosso" de "estado residual do boot quente" com esta corrida só. Próximo passo: a mesma repetição **com boot frio**; se o SDMA preso em fill reaparecer, ele vira o alvo principal.

## 3. Análise do dump (26/09, cópia `MEMORY-0x10E.DMP` na pasta de evidência; saída em `dump-0x10E-analyze.txt`)
- **0x10E Arg1=4: "Rotation failed in a must succeed path"** (texto do próprio `!analyze`). Bucket `0x10e_4_dxgmms2!VIDMM_MEMORY_SEGMENT::RotateCopyCallback`.
- Pilha (thread de trabalho do VidMm, processo corrente `TextInputHost.exe`): `VidMmWorkerThreadProc → ProcessDeferredCommand → EvictOneAllocation → EvictResource → TransferToSystem → VIDMM_GLOBAL::Rotate → VIDMM_RECYCLE_*::Rotate → nt!MmRotatePhysicalView → RotateCopyCallback → WdLogEvent5_WdCriticalError → KeBugCheckEx`. **Nenhum quadro do bc250mcdm.**
- Leitura (P0 dump + P0 log, com a conexão entre os dois em H forte): o VidMm despejava uma alocação da VRAM para a memória do sistema e a cópia da rotação, obrigatória, falhou. No mesmo segundo, o SDMA estava preso dentro do IB de fill 13042 (`InsideIb=1`), e toda cópia pelo SDMA expirava (`Blt Timeout`, `COMPOSITE_BLT FALHOU`). Não houve TDR_QUIESCE: o VidMm desistiu antes do TDR recuperar a engine.
- Contexto estrutural: o segmento de VRAM declarado tem ~257 MB (harness vê "VRAM: 239 MB"), o que força despejo frequente e muita cópia pelo SDMA. Não é causa do travamento, mas aumenta a exposição a ele.
- O que fica aberto: **por que o SDMA parou dentro do fill**, e se o boot quente contribuiu. Próximo passo: repetição com boot frio.
