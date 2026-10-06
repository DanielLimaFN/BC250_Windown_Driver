# 568 — Corrida 205 (4c.1): invalidação por pacote PROVADA no anel, IB para no `SRBM_WRITE`; 600 s sem trava dura

**Data/hora:** 02/10/2026, bind 15:03:09, rollback 15:13:03 · **Candidato:** lab **3.352.1.0** (SYS `4B1894A7…3533F`, UMD `6654865D…` e RADV `5447A17D…` iguais às da 204) · **CMOS:** 256 MB
**Evidência:** `docs/evidence/2026-10-02_15-03-01-DESKTOP-HQAG6T4-lab-binding/` (log do KMD `display-log-this-bind-FINAL.txt`, 45,7 MB; logs `-FINAL` da UMD e da RADV; `umd-logs-appcontainer/`)
**Critério:** [doc 567 §5](567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md), escrito antes.

---

## 1. O ensaio de boot (a pergunta da corrida)

Linhas do KMD, antes do desktop (P0):

| Passada | Veredito | Medição |
|---|---|---|
| Faixa do engine 1 | OK | `LO 00000000->FFFFFFFF HI 00000000->0000001F (readback OK)` |
| **Anel** (VMID 8) | **PROVADO** | Fence `00001002` sobre veneno; `RPTR_Reg=244 = WPTR`; dados 0/16 divergentes; `REQ 02F80000->00F80100` (esperado `00F80100`, prova=2); `ACK 00000000->00000100` (prova=2); 2 polls |
| **IB** (VMID 9, IB em VMID 0) | **TIMEOUT** | Fence no veneno (`43DAFFFE`); `RPTR_Reg=288`, `WPTR=324`; **dados 0/16 divergentes** (o `WRITE_LINEAR` dentro do IB rodou); `REQ` ficou em `00F80100` (o `SRBM_WRITE` não escreveu); `ACK` sem o bit 9; `STATUS1=000003FF` |
| Recuperação | PROVADA | `TDR_QUIESCE` (`RPTR=288 WPTR=324 STATUS1=3FF`) → `TDR_RESTART` com NOP e fence → `[SDMA_CG] TDR_RESTART ... OVERRIDES DO POST RESTAURADOS` |

`ERR_LOG=0000000F` aparece nas duas passadas, inclusive na que provou; não discrimina nada.

**Desfecho pelo critério: C** (anel `PROVADO`, IB `TIMEOUT`).

**Leitura (H, apoiada em três medições):** o SDMA executou o IB até o `WRITE_LINEAR` e **parou no `SRBM_WRITE`**: os dados chegaram, o `REQ` não mudou e a engine ficou com trabalho pendente e `STATUS1` ocioso. É compatível com escrita de registrador ser pacote privilegiado, aceito no anel e não dentro de IB. O Linux só emite a invalidação no anel (`emit_vm_flush`), o que casa.

**Implicação para a 4c.2:** a invalidação da TLB **não** pode ir dentro do paging buffer. Vai no **anel, entre IBs**: o KMD divide a execução do paging buffer no ponto do `FlushTlb` e emite a sequência de 15 dwords no anel, entre os dois `INDIRECT`.

## 2. O resto da corrida (runtime igual ao da 204)

**Sucessos (P0):**
- **~594 s sem trava dura**, até o watchdog: `prazo absoluto expirou sem confirmacao` às 15:13:03 e `RECOVERY_OK_ONLINE` (BasicDisplay Código 0 em 5 amostras).
- 8.076 `IB EXECUTADO` no SDMA0, 5.696 submissões gráficas, 6.220 `Present`, 5.128 `FILL_POR_COPIA`.
- Zero `GFXHUB_FAULT`, zero `IB_TRIPOD TIMEOUT`, zero `IB NAO EXECUTADO`, zero StopDevice, zero BugCheck, zero falha de flush de VMID. O único `TDR_QUIESCE` do log é a recuperação do ensaio no boot.

**Falhas:**
- **Três reinícios do `dwm.exe`**, que batem com as telas pretas anotadas pelo Daniel:

| DWM | Vida | Fim |
|---|---|---|
| pid 1284 | 15:03:12 → 15:04:26 | `D3DKMTSubmitCommand` → `0xC01E0200` às 15:04:25.755, `vkQueueSubmit res=-4`, teardown ordenado (tela preta das 15:04) |
| pid 5116 | 15:04:30 → 15:05:17 | log termina no meio de draws; motivo não registrado |
| pid 2968 | 15:05:22 → 15:11:28 | `vkAllocateMemory res=-2` (34 no processo) e depois `vkQueueSubmit res=-4` às 15:11:27 (metade da tela preta ao abrir a Pesquisa, 15:11) |
| pid 4912 | 15:11:30 → fim | sem falha registrada |

- `0xC01E0200` = `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE` (ntstatus.h do WDK 28000): o `dxgkrnl` marcou o **device do DWM** como em erro. O log do KMD **não** mostra TDR, fault, recusa nem falha de paginação no intervalo (P0 da ausência, nas classes não amostradas). Quem marcou e por quê: **não identificado**. H: falha de residência do VidMm com a VRAM cheia (217/256 MB dedicados no Gerenciador de Tarefas; `res=-2` logo antes no pid 2968).
- Menu Iniciar lento: o Daniel pediu às 15:09 e ele abriu às 15:10. 58 `res=-2` no `StartMenuExperienceHost` (pid 3560).
- 207 linhas `RECUSA` no KMD, nenhuma nova: 30 `DIRTY_RECTS_RECUSADOS` (bissecção DX50 desligada), 12 `ResolveGpuVa` recusados e 2 `COMPOSITE_BLT RECUSADO (resolucao)`.

## 3. O que isso diz (e o que não diz)

- **A 4c.1 respondeu a pergunta dela:** invalidação de TLB pelo SDMA funciona no anel (P0) e não dentro de IB (P0 do sintoma, H da causa).
- **A ausência de trava dura NÃO é atribuível à 4c.** O runtime era o da 204. Com 256 MB: 199, 200, 202, 203 e 204 travaram; 201 e 205 não. É n=1 e alterna.
- **Diferença de boot que existe, declarada:** depois do timeout do IB, o SDMA0 passou por quiesce e restart do TDR (soft reset do GRBM e anel reconstruído) antes de o VidMm começar. O clock gating voltou ao do POST (`[SDMA_CG] TDR_RESTART`), igual à 204. Não há base para dizer que isso mudou alguma coisa, mas é uma diferença.

## 4. Recuperação e pendências

- **Driver ao final:** BasicDisplay Código 0 (rollback online limpo, sem reboot). Boot frio antes do próximo bind.
- **NÃO VALIDADO EM HARDWARE:** 4c.2 a 4c.5; causa do `0xC01E0200`; correção da trava.
- **Próximo:** 4c.2 com o flush **no anel**: dividir a execução do paging buffer no `FlushTlb` e invalidar pelo SDMA entre os IBs, com o MMIO de invalidação fora do caminho de paginação. Corrida 206.
