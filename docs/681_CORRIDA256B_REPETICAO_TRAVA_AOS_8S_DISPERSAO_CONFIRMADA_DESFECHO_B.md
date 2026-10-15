# 681 — Corrida 256b: repetição da 256. Trava dura aos ~8,2 s após o bind (Desfecho B). Dispersão histórica confirmada

**Data/Hora:** 09/10/2026 ~13:48–13:51.  
**Candidato:** lab **3.402.1.0** (mesmo binário da 256, SYS assinado `1C01B05201F85C5266076DBC99BD244695FC0EB55B965C2DD34A64A27116A817`).  
**Evidência:** `docs/evidence/2026-10-09_13-48-21-DESKTOP-HQAG6T4-lab-binding`.  
**Critério prévio:** [doc 679](679_4C_BYPASS_MMIO_HDP_NO_FLUSH_TLB_E_CRITERIO_CORRIDA256.md) (Repetição mandatória pela Lei 10 após Desfecho A).

---

## 1. O que aconteceu na repetição (256b)

Conforme a Lei 10, a Corrida 256 foi repetida com o mesmo candidato para verificar se o Desfecho A (600 s) era estável ou sujeito à dispersão da plataforma.
- A máquina subiu em boot limpo e iniciou o bind às 13:48:21.
- O driver inicializou normalmente com todas as pré-condições ativas:
  - `[4C_HDP_FLUSH_BYPASS] ATIVO: HdpNeeded=0`
  - `[C4_FILL] chave=1 X2b=0 fatia_max=0x400000`
  - `[C3_VRAM_WC] chave=1 P1b=1 ... =8`
  - `[C1_GFX_DOORBELL] ATIVA`
- Aos **~8,21 segundos** de atividade da GPU (QPC 2165770168 → 2247836537), a máquina sofreu a trava dura clássica.
- **Estado da GPU no congelamento:**
  - **Zero BugCheck, zero TDR, zero GFXHUB Fault, zero timeout.**
  - Último comando: `[SubmitVirtual Node0 SDMA] IB EXECUTADO (INDIRECT): a GPU leu o DMA buffer de 32 bytes em VA=0x229EE0; FenceId=2114 ... retire_qpc=2247836858 FromDpc=1`.
  - A GPU concluiu e aposentou todos os 2.114 IBs (`RPTR == WPTR`).
- **Métricas no momento da trava:**
  - Duração: **8,21 s**.
  - FILLs concluídos: **292**.
  - Presents concluídos: **313**.
  - IBs executados: **2.114**.
  - Submissões GFX (Node 1): **649**.
  - `FLUSH_TLB`: **5.492 operações**.
  - VRAM dedicada: **162.2 MB** (com 83.4 MB compartilhados no Segmento 2).
  - Temperatura Tctl: **94.4 °C**.
- **Diagnóstico pós-trava:**
  - `tools/coletar-mca-whea.ps1`: Kernel-Power 41 (BugcheckCode: 0, P1–P4=0). Zero eventos de erro em `Microsoft-Windows-WHEA-Logger` (CPU não disparou MCE).
  - CMOS `RESET_REASON`: `0x40080400` (bits 30, 19, 10 de sempre).
  - Fusível de boot: **disparou como esperado** (`State=2, Boots=2`), impedindo loop de boot e colocando o dispositivo em Código 43 seguro.

---

## 2. Quadro comparativo: 256 vs 256b

| Métrica | Corrida 256 (1ª rodada) | Corrida 256b (Repetição) |
|---|---|---|
| **Candidato** | lab 3.402.1.0 | lab 3.402.1.0 (idêntico) |
| **Duração** | ~595 s (terminou no watchdog) | **8,21 s** (trava dura) |
| **Desfecho** | A | **B** |
| **FILLs** | 3.123 | 292 |
| **Presents** | 14.484 | 313 |
| **FLUSH_TLB** | 70.889 | 5.492 |
| **IBs Executados** | 24.385 | 2.114 |
| **GPU na parada** | RPTR == WPTR (ociosa) | RPTR == WPTR (ociosa) |
| **Tctl** | 92.4–94.4 °C | 94.4 °C |

---

## 3. Conclusão Metodológica

1. A Corrida 256 que durou 595 s confirmou o que já havia sido observado nas Corridas 201 (591 s), 216 (600 s) e 226 (600 s): a plataforma apresenta uma **dispersão estocástica brutal** que varia de ~8 s a ~600 s sob carga.
2. A supressão do MMIO de HDP no `FLUSH_TLB` (4C) **fica mantida** porque é arquiteturalmente correta para APU (zero escritas em registrador fantasma de PCIe, reduzindo tráfego espúrio), mas **não é a causa suficiente** que elimina a trava dura.
3. Como a GPU sempre termina seu trabalho (`RPTR == WPTR`), a parada ocorre no subsistema Zen 2 / Data Fabric / Memory Controller da APU. Os dados de temperatura elevada (94.4 °C) e a ausência completa de controle de energia/DPM via SMU (Item 4B) despontam como o principal vetor físico remanescente.
