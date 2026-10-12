# 680 — Corrida 256: 4C (HDP flush bypass) ativo. 600 s sustentados sem trava, 226 MB de VRAM e 70.889 FLUSH_TLBs. Desfecho A!

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

**Data/Hora:** 09/10/2026 ~13:23–13:34.  
**Candidato:** lab **3.402.1.0** (SYS assinado `1C01B05201F85C5266076DBC99BD244695FC0EB55B965C2DD34A64A27116A817`).  
**Evidência:** `docs/evidence/2026-10-09_13-23-46-DESKTOP-HQAG6T4-lab-binding`.  
**Critério prévio:** [doc 679](679_4C_BYPASS_MMIO_HDP_NO_FLUSH_TLB_E_CRITERIO_CORRIDA256.md).

---

## 1. O que aconteceu

A Corrida 256 atingiu o **DESFECHO A** do critério prévio:
- A máquina operou por **~600 segundos completos** (595 s medidos por QPC e watchdog) com o nosso driver ativo.
- O Menu Iniciar (`StartMenuExperienceHost.exe`) e a Pesquisa (`SearchApp.exe`) rodaram ativamente: o log UMD do Menu Iniciar gerou **27.793 linhas** entre 13:24:34 e 13:33:15.
- A VRAM dedicada subiu até **226.4 MB** (de 256 MB físicos), com **68 MB de transbordo** para a memória de sistema compartilhada (Segmento 2).
- O VidMm realizou **2.020 evicções de VRAM** e **70.889 operações de `FLUSH_TLB`**, com **3.123 FILLs**, **14.484 Presents/Blts**, **24.385 IBs executados** e **30.706 submissões GFX**.
- **Zero congelamento, zero BugCheck, zero TDR, zero fault no GFXHUB.**
- Ao final dos 600 s, o watchdog expirou por prazo regulamentar e executou o **rollback online** para o `BasicDisplay` em Código 0, sem necessidade de reboot.
- O fusível de boot permaneceu **CLEAN** (não houve queda de energia nem reboot forçado).

---

## 2. Métricas comparativas (Corridas 252 a 256)

| Métrica | Corrida 252 (P5) | Corrida 253 (C1) | Corrida 254 (C3) | Corrida 255 (C4) | **Corrida 256 (4C)** |
|---|---|---|---|---|---|
| **Duração** | ~124 s | ~40 s | ~43 s | ~73 s | **~595 s (600 s watchdog)** |
| **Desfecho** | B (trava) | B (trava) | B (trava) | B (trava) | **A (600 s sem trava)** |
| **Menu Iniciar** | Travou ao abrir | Travou ao abrir | Travou ~1,8 s pós | Travou ~17 s pós | **Vivo ~9 min (27.793 linhas)** |
| **VRAM Alocada** | ~180 MB | ~160 MB | ~170 MB | ~190 MB | **226.4 MB (90% do teto)** |
| **Evicções VidMm** | ~120 | ~80 | ~95 | ~140 | **2.020 evicções** |
| **FLUSH_TLB** | ~1.500 | ~600 | ~700 | ~1.100 | **70.889 operações** |
| **FILLs** | 831 | 255 | 407 | 561 | **3.123 FILLs** |
| **Presents/Blts** | 1.842 | 620 | 815 | 1.230 | **14.484 Presents** |
| **Rollback** | Reset físico | Reset físico | Reset físico | Reset físico | **Online (Código 0)** |

---

## 3. Pré-condições do Critério (Conferência P0)

Todas as pré-condições exigidas no doc 679 foram validadas no log do KMD:
1. `[4C_HDP_FLUSH_BYPASS] ATIVO: HdpNeeded=0 (APU=0 -> MMIO 0x3FC4 suprimido em FLUSH_TLB, _mm_mfence() preservado)`: **CONFIRMADO**.
2. `[C4_FILL] chave=1 X2b=0 fatia_max=0x400000: zeramento por CONST_FILL ...`: **CONFIRMADO**.
3. `[C3_VRAM_WC] chave=1 P1b=1 mapeamentos WC da DRAM pelo KMD ate aqui=8`: **CONFIRMADO**.
4. `[C1_GFX_DOORBELL] ATIVA (ONLINE)`: **CONFIRMADO**.
5. `GFXHUB_FAULT: count=0`: **CONFIRMADO**.
6. `TDR_QUIESCE: count=0`: **CONFIRMADO**.
7. `TIMEOUT: count=0`: **CONFIRMADO**.

---

## 4. Conclusão e Próximo Passo

A supressão do MMIO redundante de HDP (`0x3FC4`) no caminho crítico de `FLUSH_TLB` removeu o gargalo/congestionamento do Data Fabric da APU sob alta taxa de paginação.

Conforme a Lei 10 e o critério do doc 679, **a repetição é obrigatória quando o resultado é Desfecho A** para provar que a estabilidade é reprodutível contra a dispersão estatística. Em seguida, poderemos avançar com o teste de carga sintética 4D ([`tools/bc250-vram-paging-stress.exe`](../tools/bc250-vram-paging-stress.exe)).
