# 689 — Corrida 262: Candidato 3.408.1.0 (Item 5F) — Trava Dura aos ~2,97 s do Bind na Submissão/Flush #3316

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

**Data:** 10/10/2026 (06:43:42)  
**Candidato:** Pacote de Laboratório `3.408.1.0` (WDDM 2.7, DISPLAY_RENDER_LAB, Item 5F: 24 DW SDMA Ring Reservation, TRAP Worst-Case, GCR em EmitGpuPageTableUpdate)  
**Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), VRAM 16 GB GDDR6 (CMOS 256 MB)  
**Evidência:** `docs/evidence/2026-10-10_06-43-42-DESKTOP-HQAG6T4-lab-binding/`  
**Runner:** `tools/EXECUTAR_CORRIDA262_5F_RING_CAPACITY_STRESS_600S.cmd`  

---

## 1. Veredito e Fato Físico Estrito

- **Houve avanço?** **NÃO.** A máquina travou antes que qualquer estresse ou carga adicional pudesse ser disparada pelo usuário.
- **Duração do bind:** **~2,97 segundos** (Delta QPC: 29.713.311 ticks a 10 MHz, entre QPC 1476841101 e 1506554412).
- **Veredito:** **FALHA (Trava Dura Prematura aos 2,97 s).**
- **Esclarecimento e Retificação de Registro:** O teste de estresse de paginação manual (`tools/RODAR_ESTRESSE_VRAM_500MB.cmd`) **NÃO FOI EXECUTADO** nesta corrida. O arquivo residual `tools/bc250-paging-stress.log` presente no disco possuía carimbo de data/hora `06:43:02`, anterior ao início do bind (`06:43:42`), tratando-se de resíduo de ensaio anterior que jamais deve ser computado para a Corrida 262.

---

## 2. Métricas Físicas Medidas no Silício Durante os 2,97 Segundos

| Métrica | Medição Real no Silício |
| :--- | :--- |
| **Tempo até a Trava Dura** | **2,97 segundos** (QPC 1476841101 → 1506554412) |
| **Presents do DWM Concluídos** | **13** (`Present #1` a `Present #13`) |
| **Operações de Paginação FLUSH_TLB** | **3.315** concluídas com status=0; travou na **#3316** |
| **Submissões SDMA0 Virtuais** | **3.522** submissões aceitas |
| **Estresse de VRAM 500 MB** | **0 ciclos** (travou antes de ser iniciado) |
| **Reboot do Sistema** | 06:44:47 (`RESET_REASON raw=0x40080400`) |
| **Fusível Anti-Boot-Loop** | Disparou no reboot (`State=2, Boots=2`), contendo o sistema em Código 43 seguro |
| **Recuperação Online** | Watchdog restaurou `BasicDisplay` em Código 0 |

---

## 3. Análise da Causa Raiz da Trava Prematura

### 3.1 Ponto Exato da Parada no KMD

As últimas linhas gravadas no log recuperado antes do congelamento:
```text
BC250_HANG v=1 op=PAGING_DDI phase=LEAVE id=3315 tid=000000000000278C qpc=1506552919 operation=12 status=0x00000000
[SubmitVirtual Node0 SDMA] CTX_PROBE: Ctx=0000000400000004, Proc=0000000100000001, hasRoot=1, targetPtPhys=0x460A8C000, Vmid=8, instCtx=0000000400000004, instRoot=0x460A8C000
BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=3316 tid=000000000000278C qpc=1506554412 operation=12
```

A thread de paginação do sistema (`tid=000000000000278C`) entrou na operação 12 (`DXGK_OPERATION_FLUSH_TLB`) para a chamada `id=3316`. O retorno (`phase=LEAVE id=3316`) **nunca ocorreu**.

### 3.2 Diagnóstico do Congelamento aos 2,97 s

1. **Volume Anômalo de Operações em Janela Curtíssima:**
   - Em menos de 3 segundos, o sistema processou **3.316 chamadas de `FLUSH_TLB`** e **3.522 submissões SDMA0** (uma taxa superior a 1.100 submissões/s no boot do desktop).
2. **Contenção no Ponto de Entrada de `FLUSH_TLB`:**
   - Em `Bc250ActiveFlushTlbHandler`:
     ```c
     ExAcquirePushLockExclusive(&active->ObjectLock);
     ```
     O handler tenta adquirir `active->ObjectLock` em modo exclusivo.
   - Concomitantemente, a thread do DWM realizou `[Present #13]` chamando `ResolveGpuVa`, que adquire `ExAcquirePushLockShared(&g_Bc250AdapterDirectoryLock)` e depois `ExAcquirePushLockShared(&active->ObjectLock)`.
   - Outras operações de criação de recursos WDDM no boot inicial disputam locks de objeto.
3. **Comportamento em Falha de Lock / Interrupção:**
   - Se uma thread em nível de paginação aguarda exclusividade de push lock enquanto outra thread de apresentação ou alocação está bloqueada ou em I/O, a thread do VidMm para de processar cercas de paginação (`Paging Fence`).
   - Sem avanço da cerca de paginação do VidMm, a GPU e o subsistema de vídeo entram em congelamento total, exigindo reset manual do sistema (0xCF9).

---

## 4. Recuperação e Estado Atual

1. **Recuperação Online:** O mecanismo de watchdog completou com sucesso a recuperação, restaurando o `BasicDisplay` em Código 0.
2. **Fusível:** Protegeu a máquina de boot-loop, mantendo estabilidade.
3. **Plano para a Próxima Corrida (Item 5G):**
   - Eliminar a aquisição pesada de `ExAcquirePushLockExclusive(&active->ObjectLock)` no caminho de `DXGK_OPERATION_FLUSH_TLB` quando `PtUpdateGpuVirtualArmed == 1`.
   - Utilizar apenas a primitiva `Bc250PagingSplitRecord` (que já é protegida por spinlock leve `g_Bc250PagingSplitLock`), sem reter o push lock de objetos do adaptador na thread de paginação do VidMm.
