# 626 — Corrida 237: parado no desktop, 600 s sem trava dura, teardowns provados; critério da 238

**Corrida:** 06/10/2026, bind às 18:52:38. Candidato lab **3.384.1.0**, o mesmo da 236 (SYS `DA42466A…`), RADV `C6BEC54B…`, CMOS em 256 MB (`vram-cmos/2026-10-06_18-52-09`). Critério: [doc 625 §4](625_RESET_REASON_RESPONDIDO_POR_MINERACAO_W1_E_CRITERIO_CORRIDA237_PARADO.md).
**Evidência:** `docs/evidence/2026-10-06_18-52-38-DESKTOP-HQAG6T4-lab-binding/` (`display-log-this-bind-FINAL.txt` 15,9 MB, logs `-FINAL` da UMD, `safe-bind-final.json`).

---

## 1. O que aconteceu

- **StartDevice `0x0`**; IH, EOP do CP, SDMA1 (P4.2), MMHUB, D2, P7, P2 e P1b armados como na 236.
- **~584 s de log do nosso driver** (QPC 84.848.399.479 → 90.690.476.790) e rollback do watchdog às 19:02:39, por prazo expirado: `RECOVERY_OK_ONLINE`, BasicDisplay Código 0 em 5 amostras, sem reboot.
- **Zero:** TDR, `GFXHUB_FAULT`, `IB_TRIPOD TIMEOUT`, `Blt Timeout`, `INSIDE_IB`, `DEVICE_REMOVED`, OpenAlloc recusado.
- **Carga:** 4.180 IBs (≈7,2/s, contra ≈105/s na 236 e ≈12,9/s na 229), 2.629 `[Present #`, 6.369 cópias da primária, 72 `[PT_GPU_SPLIT]`.
- **Processos com a UMD:** `dwm.exe` (até 19:02:29, 4 `CreateDevice`, zero `DestroyDevice`/device-lost/falta de memória), `Explorer.EXE` (até 19:01:53, 6 `CreateDevice`, zero `DestroyDevice`), `TextInputHost.exe` (sobe sozinho, ativo só 18:52–18:53), `dxgiadaptercache.exe` e `taskmgr.exe` (uma linha cada, às 18:52:58). Nenhum log de `StartMenuExperienceHost` nem da Pesquisa: **menu e Pesquisa não abriram** (P0 pela ausência dos logs de AppContainer).

## 2. Teardowns medidos em regime pela primeira vez desde a 233 (P0)

| Bloco | Linha do teardown |
|---|---|
| IH | `DESARMADO`: ISR=6.445 = reclamadas; vetores 6.456 = 4.181 TRAPs do SDMA0 + 2.275 EOP; `traps_emitidos=4181`; overflows=0; NBIO de volta ao snapshot |
| SDMA1 (P4.2) | `DESANEXADO`: Go = Done = 8.230, RPTR = WPTR = 790.148, `inline=0 sem_espaco=0`; `DESLIGADO`: F32=1 (parado), RAM devolvida |
| EOP do CP | `CP_INT_CNTL_RING0 0x04000000 -> 0` (lido 0), 2.275 emitidos = 2.275 recebidos = 2.275 DPCs |
| GART do MMHUB | `falhas=0`, `CONTEXT0_CNTL` de volta ao snapshot `0x007FFE80` |
| D2 (GART do GFXHUB) | `falhas=0`, contexto 0 no snapshot; anel do SDMA0 de volta à VRAM com readback, RAM liberada |
| P7 / P2 / P1b | PTEs na dummy e RAM devolvida / doorbell desarmada (10.547 kicks) / `HDP omitido 122215 vezes` |
| MMU global | `Regs=26` restaurados; VMID 8 desinstalada e TLB invalidada |

Isso fecha a pendência 4 do doc 624 e o "NÃO VALIDADO EM HARDWARE: teardown do SDMA1/EOP/IH/MMHUB" das corridas 233–236.

## 3. Veredito frente ao critério

**Desfecho A**, com duas ressalvas de pré-condição a confirmar com o Daniel:
1. **Toque:** a análise não acha uso (sem menu, sem Pesquisa, sem app novo). Falta o relato dele de que não tocou em nada.
2. **`taskmgr.exe` às 18:52:58:** carregou a UMD no instante do bind, então já estava aberto antes. O log dele parou ali (10 KB), mas o Gerenciador aberto amostra contadores da GPU em segundo plano. Se estava aberto, é carga leve de fundo, não uso.
3. **Boot frio:** a leitura da CMOS antes do bind deu `0x40080400` (bit 19, reset a quente). Não discrimina: o último `-Ler` antes desse foi às 15:41, então o bit pode vir do reinício antes do W1 (16:31). O binder aceitou o marcador de boot frio.

**Leitura (H):** sob a taxa de fundo estimada (uma trava a cada ~333 s, doc 625 §2), 600 s sem trava por acaso tem ~16,5% de chance. É indício, não prova, de que a trava precisa de uso. Pelo critério, **a Corrida 238 repete parado**: duas seguidas levam o acaso a ~2,7%.

## 4. Critério da Corrida 238 (repetição, escrito antes)

**Igual ao doc 625 §4, com o mesmo pacote e o mesmo `.cmd`** (`tools/EXECUTAR_CORRIDA237_PARADO_600S.cmd`; os hashes não mudaram). Pré-condição reforçada: **Gerenciador de Tarefas fechado** antes do bind, desligar a máquina da tomada ou pelo botão por alguns segundos (boot frio de verdade), e não tocar em nada depois do desktop.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| A | 600 s sem trava, `RECOVERY_OK_ONLINE`, sem toque | Com a 237: n=2 parado sem trava (acaso ~2,7%). A trava precisa de uso. Próximo: bissecção por carga, uma ação por corrida (só menu; só Pesquisa; só um app), com o mesmo pacote |
| B | Trava dura com a assinatura de sempre, sem toque | A 237 foi sorte (16,5%); uso não é necessário. Próximo: corrida "máquina limpa" (rastro, logs da UMD e coleta no mínimo) |
| C | Toque ou janela aberta sozinha | Inválida; repetir |
| D | Código 43 no início, TDR, tela azul | Defeito próprio; não conta |
| E | Fora do mapa | Descrever antes de julgar |

**Teto:** a 238 é a última corrida parada. Com A, a 239 já é bissecção por carga.

**NÃO VALIDADO EM HARDWARE:** a causa da trava; que ela dependa de uso (n=1).
