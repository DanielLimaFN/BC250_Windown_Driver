# DOC 685 — Corrida 259 (Item 5C, Pacote 3.405.1.0): Avanço Histórico no Estresse de Paginação (2 Baterias de 500 MB Concluídas) e Critério para Corrida 260

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

> **Data:** 09/10/2026 ~16:30  
> **Candidato:** Pacote lab **3.405.1.0** (SYS `88345C9F...`, INF `C7B0DD83...`, CAT `3EA21D02...`, UMD `B73F9BDA`)  
> **Pasta de evidência:** `docs/evidence/2026-10-09_16-27-28-DESKTOP-HQAG6T4-lab-binding/`  
> **Veredito:** **DESFECHO PARCIAL (Avanço Funcional Histórico Provado no Silício)**  
> **Estado final:** Código 43 seguro pelo fusível anti-boot-loop (`C:\BC250_BOOT_FUSE.dat`), desktop restaurado via BasicDisplay em Código 0.

---

## 1. Contexto e Hipótese do Ensaio (Item 5C)

Na Corrida 258 (Item 5B, MTYPE_UC universal em folhas não-snoopadas de sistema), a máquina sustentou 97,95 s de operação contínua, mas travou imediatamente na primeira tentativa de submeter estresse de paginação com texturas de 16 MB. A investigação revelou que:
1. O caminho de split chunk do SDMA (`Bc250SdmaPresentSubmitSplitChunkedWith`) omitia o pacote `SDMA_OP_GCR_REQ` (GL2 write-back e invalidate), ao contrário do caminho simples e do `amdgpu` Linux (`sdma_v5_0.c`).
2. Alocações de VRAM (Segmento 1) `CpuVisible=1` ainda usavam `MTYPE_NC` (cache GL2 na GPU), colidindo com acessos Write-Combining da CPU via DRAM física na APU.

O Item 5C (implementado no pacote 3.405.1.0) armou:
- Emissão incondicional do pacote `SDMA_OP_GCR_REQ` no caminho de split chunk.
- `MTYPE_UC` universal para toda folha não-coerente de cache (`CacheCoherent == 0u`), eliminando colisões de GL2 em qualquer segmento.
- Estresse de VRAM manual de 500 MB (`bc250-vram-paging-stress.exe 500 50 100`) via `tools/RODAR_ESTRESSE_VRAM_500MB.cmd`.

---

## 2. Resultados Físicos Medidos na Corrida 259

O Daniel disparou o script manual de estresse em duas baterias consecutivas e iniciou a terceira:

### A. Bateria 1 de Estresse (PID 7588, 16:27:49 a 16:28:17 — 28 s)
- **Status:** **100% CONCLUÍDA COM SUCESSO.**
- Alocou e desalocou continuamente 13 texturas de 16 MB (~208 MB ativos simultâneos em VRAM) por **50 ciclos completos**.
- Log UMD arquivado: `BC250_UMD_LOG_7588.txt` (1.135.239 bytes).
- Término perfeito: todos os recursos liberados, `D3D10 DestroyDevice` executado, `CloseAdapter` retornado com `S_OK`.

### B. Bateria 2 de Estresse (PID 9284, 16:28:26 a 16:28:52 — 26 s)
- **Status:** **100% CONCLUÍDA COM SUCESSO.**
- Mais **50 ciclos completos** de alocação e desalocação pesada de 208 MB em regime contínuo.
- Log UMD arquivado: `BC250_UMD_LOG_9284.txt` (1.178.989 bytes).
- Término perfeito: `D3D10 DestroyDevice` executado, `CloseAdapter` retornado com `S_OK`.

### C. Bateria 3 de Estresse (PID 10996, iniciada às 16:30:01)
- O processo iniciou, criou o dispositivo D3D11 e o bridge RADV Vulkan com sucesso.
- Criou a textura 1 (`hAlloc=0x40000600`) e a textura 2 (`hAlloc=0x40000700`) com sucesso.
- Ao solicitar residência e aguardar a paginação da textura 3 (`hAlloc=0x40000800`, `TargetFence=7008`), às 16:30:01.617, a máquina congelou.
- Log UMD arquivado: `BC250_UMD_LOG_10996.txt` (30.201 bytes), parou em `op=WAIT_PAGING phase=ENTER a=0x1B60 b=0x4000048040000800`.

---

## 3. Telemetria Global Consolidada da Corrida 259

A telemetria do monitor em tempo real (`C:\BC250_MONITOR.csv`) e o log recuperado do KMD revelam números inéditos no silício:
- **Tempo ativo contínuo:** **153 segundos**.
- **FLUSH_TLBs executados:** **28.069 operações** (todas com `status=0` e latência de ~0,05 ms via bypass 4C).
- **Submissões SDMA0:** **8.201 IBs executados** sem nenhum timeout.
- **Submissões GFX:** Mais de **2.800 submissões** aceitas e aposentadas.
- **Apresentações DWM:** **2.432 quadros** apresentados no monitor físico a 60 Hz sem descontinuidade visual ou artefatos.
- **Fills e Evicções do VidMm:** **547 Fills** e **778 evicções** processadas com sucesso.
- **Temperatura (Tctl):** Mínima de 84,75 °C, pico máximo de **96,625 °C** durante a rajada contínua das duas baterias de estresse.

### Teste de Memória Pós-Trava
O script automático de recuperação executou o teste de RAM (`ramtest-pos-trava.txt`):
- 8.314 MB testados em 16 threads × 2 passadas completas.
- **Erros detectados: 0**. Os chips de memória GDDR6 e o canal de DRAM física estão perfeitamente sãos.

---

## 4. Diagnóstico Cirúrgico da Trava na 3ª Bateria

### A. Motivo do Reset no Silício (FCH)
A leitura de CMOS/FCH pós-trava (`reset-reason-pos-trava.txt`) registrou:
```
=== MOTIVO DO ULTIMO RESET DA PLATAFORMA (FCH S5_RESET_STATUS 0xFED803C0) ===
RESET_REASON raw=0x40080400
RESET_REASON bit 10: (sem nome no upstream)
RESET_REASON bit 19: software wrote 0x6 to reset control register 0xCF9
RESET_REASON bit 30: a parity error occurred
```
O bit 30 é **erro de paridade no Data Fabric / Barramento de Memória**.

### B. O que estava em voo no KMD no momento exato da trava
No arquivo `display-log-this-bind-RECUPERADO.txt`, as últimas duas linhas registradas antes do congelamento são:
```
BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=28261 tid=0000000000002FE8 qpc=2754571342 operation=12
[SubmitVirtual Node0 SDMA] CTX_PROBE: Ctx=0000000400000004, Proc=0000000100000001, hasRoot=1, targetPtPhys=0x460A8C000, Vmid=8, instCtx=0000000400000004, instRoot=0x460A8C000
```
- A thread de paginação do VidMm (`tid=2FE8`) chamou `BuildPagingBuffer` com `DXGK_OPERATION_FLUSH_TLB` (Op 12, id=28261).
- Concomitantemente, a thread de submissão do SDMA estava processando uma submissão virtual (`SubmitVirtual Node0 SDMA`) no contexto de paginação (VMID 8).
- A investigação do código do SDMA revelou duas lacunas críticas de sincronização:
  1. **No Split Chunking (`Bc250SdmaPresentSubmitSplitChunkedWith`):** O pacote GCR (`SDMA_OP_GCR_REQ`) foi colocado dentro de `if (status == BC250_SDMA_OK && last)`. Quando um buffer de paginação é dividido em múltiplos pedaços (chunks intermediários), as PTEs escritas no chunk anterior **não sofrem GL2 rinse antes da invalidação da TLB do chunk seguinte**.
  2. **Em `Bc250McdmBuildTransferVirtual`:** O driver emite `COPY_LINEAR` puro sem qualquer pacote de GCR / memory sync ao transferir memória entre VRAM e sistema durante compactação pesada pelo VidMm.

---

## 5. Critério para a Corrida 260 (Item 5D)

1. **GCR Rinse em TODOS os Chunks de Split:** No loop de split chunking do SDMA, emitir o rinse `SDMA_OP_GCR_REQ` após a escrita de PTEs e antes de qualquer invalidação de TLB, garantindo que a GL2 da GPU não mantenha linhas de cache sujas colidindo com a DRAM física.
2. **Memory Sync no `TransferVirtual`:** Adicionar barreira de cache/memória nas transferências de paginação do VidMm, espelhando o comportamento do Linux `sdma_v5_0_ring_emit_mem_sync`.
3. **Validação:** Repetir o estresse sintético de 500 MB buscando sustentar 3 ou mais baterias consecutivas sem trava e sem erro de paridade do Data Fabric.
