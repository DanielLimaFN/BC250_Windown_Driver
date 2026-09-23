# Handoff Corrida 64 — 22/09/2026 (08:54:17)

> **Candidato testado:** lab **3.225.1.0** (faixa DX.17)
> **SYS SHA-256:** `5C2950513E5C66CEE312A2535F773C2093CA1B9075465A63F21DB3B289236072`
> **Pasta de evidência:** `docs/evidence/2026-09-22_08-54-17-DESKTOP-HQAG6T4-lab-binding/`
> **Critério prévio:** [docs/323_CRITERIO_CORRIDA64_ESCREVEMOS_LIMPO_LEEM_SUJO.md](323_CRITERIO_CORRIDA64_ESCREVEMOS_LIMPO_LEEM_SUJO.md)

---

## 1. Resumo Executivo e Veredito

- **Houve avanço?** **SIM, avanço diagnóstico decisivo; NÃO funcional.**
- **Veredito frente ao critério de docs/323:** **PARCIAL DIAGNÓSTICO.**
  - **A pergunta da escrita da CPU foi FECHADA:** Em **99 de 99 escritas** no índice 4096 (25 não-nulas, todas limpas), o readback imediato pela CPU leu exatamente o valor codificado (`arenaReadback == arenaEncoded`). **DIVERGENCIA = 0.** A nossa escrita chega à memória física com 100% de integridade; a hipótese de ponteiro corrompido ou mapeamento falso na CPU está **REFUTADA**.
  - **O `Cid=10` (perda de 8 bits) foi REPRODUZIDO pela sétima vez:** `FaultAddrPagina=0x2431` (`GpuVa=0x2431000`), parando em `PDE=0` no L1 idx 18, enquanto `0x243100000` (`VA<<8`) resolveu perfeitamente para `PA=0x451CCC000` (`PDE=0x45FDB1001`, `PTE=0x451CCC071`).
  - **Identidade física de `0x243100000` identificada no log da UMD:** Pertence à **superfície primária do DWM** (`2560x1080 bpp=4`, 11.059.200 bytes, `Base=0x242F00000`, `Primary=1`). O offset `0x200000` (2 MiB) corresponde à página `0x243100000`.

---

## 2. Métricas e Controles Medidos (P0)

| Controle | Corrida 63 (3.224.1.0) | Corrida 64 (3.225.1.0) | Observação |
|---|---:|---:|---|
| **`IB EXECUTADO (INDIRECT)`** | 1.877 | **1.723** | Execução de DMA buffers sob VMID 8 estável |
| **TDRs / Restarts provados** | 6 / 6 | **7 / 7** | SDMA0 e CP_RB0 rearmados com fence física |
| **Faults drenados / sem latch** | 2 / 4 | **1 / 6** | 1 fault drenado (`Cid=10`), 6 sem latch |
| **`[PDE_WRITE] ARENA`** | 106 | **99** | 25 não-nulas, todas limpas (`0x...001`) |
| **`[PDE_WRITE] DIVERGENCIA`** | *(não existia)* | **0** | **Readback bate 99/99 vezes** |
| **`[VMID_POOL] DESARMADO`** | 24 | **21** | 21/21 desarmes OK no `DestroyContext` |
| **`HdpFlush=1 Ok=1`** | 75 / 75 | **66 / 66** | Coerência de CPU/HDP mantida |
| **BugCheck** | 0 | **0** | Nenhum BSOD |
| **Watchdog Rollback** | `RECOVERY_OK_ONLINE` | **`RECOVERY_OK_ONLINE`** | BasicDisplay Código 0 em 5/5 amostras |
| **D3D11 Native Harness** | Quadro 1 fail | **Quadro 1 fail** | `0x887A0005`, `DRR=0x887A0020`, tela preta |

---

## 3. Análise Detalhada dos Achados

### 3.1 A escrita da CPU chega à memória física (DX.17 validada)
Das 99 chamadas em que `UpdatePageTable` tocou o índice 4096 da arena:
- 74 escritas foram zeradas (limpeza de diretório/teardown).
- 25 escritas foram mapeamentos válidos com `V=1, LargePage=0, Seg=1`, todas codificadas como `0x000000045FD...001` (`Reservado[5:3] = 0x0`).
- Em todas as 99 vezes, a leitura imediata `destination[4096]` retornou exatamente o valor recém-escrito.
- **Conclusão:** O caminho da CPU (`PageTableAddress.CpuVirtual`) grava no local correto e a cache do processador não corrompe os bits. Se em corridas como 59, 61 e 63 o TDR leu valor com bits [5:3] sujos (`0x...071`), essa alteração **ocorre a posteriori** (por reciclagem de página pelo VidMm ou por outro escritor que toca a mesma página após a nossa escrita).

### 3.2 O fault `Cid=10` e a Primária do DWM
No TDR registrado na linha 9951:
```
[TDR_GCVM_FAULT] DRENO #0: Status=0x0020153C -> Vmid=2 WalkerError=6 MappingError=1 PermissionFaults=0x3 Cid=10 Rw=0 MoreFaults=0 | AddrPagina=0x0000000000002431
```
- `FaultAddrPagina = 0x2431` $\longrightarrow$ `GpuVa = 0x2431000`.
- A caminhada `[PT_WALK]` em `0x2431000` parou no L1 idx 18 com `PDE=0` (**não mapeado**).
- A caminhada em `0x243100000` (`VA << 8`) resolveu:
  - L1 idx 4632: `PDE = 0x000000045FDB1001` (V=1)
  - L0 idx 256: `PTE = 0x0000000451CCC071` (V=1, PA=`0x451CCC000`).
- Cruzamento com o log da UMD (`bc250_umd_log_10844-FINAL.txt:6993`):
  ```
  CreateResource: res=... Dim=3 Fmt=87 Size=11059200 (2560x1080 bpp=4) GpuVa=0x242F00000 Primary=1
  ```
  A alocação em `0x242F00000` é exatamente o buffer de scanout primário do DWM. A página `0x243100000` fica a 2 MiB do início dessa primária.

---

## 4. Estado da Árvore e Próximo Passo

- **Candidato testado:** `3.225.1.0` (instalado, executado, arquivado em `docs/evidence/2026-09-22_08-54-17-DESKTOP-HQAG6T4-lab-binding`).
- **Recuperação:** O watchdog desinstalou o pacote experimental e restabeleceu o `BasicDisplay` em Código 0 com integridade.
- **O que continua NÃO VALIDADO EM HARDWARE:**
  - `DX.5` (tradução passar a funcionar sem TDR).
  - Execução de shader sem fault no hardware.
  - O primeiro pixel do DWM / D3D11 nativo.
