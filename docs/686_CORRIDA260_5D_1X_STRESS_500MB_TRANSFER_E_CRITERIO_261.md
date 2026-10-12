# DOC 686 — Corrida 260 (Item 5D, Pacote 3.406.1.0): 1ª Bateria de 500 MB Concluída com Sucesso, Trava na Evicção de 16 MB na 2ª Bateria e Critério para Corrida 261

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

> **Data:** 09/10/2026 ~17:41  
> **Candidato:** Pacote lab **3.406.1.0** (SYS `9719B0B2...`, INF `CF508A7A...`, CAT `81B23B44...`, UMD `B73F9BDA`)  
> **Pasta de evidência:** `docs/evidence/2026-10-09_17-41-06-DESKTOP-HQAG6T4-lab-binding/`  
> **Veredito:** **DESFECHO PARCIAL (Avanço de Diagnóstico Cirúrgico — 1 Bateria de 500 MB Concluída a 100%, Trava Isolada na Evicção/TRANSFER de 16 MB)**  
> **Estado final:** Código 43 seguro pelo fusível anti-boot-loop (`C:\BC250_BOOT_FUSE.dat`), desktop restaurado via BasicDisplay em Código 0.

---

## 1. Contexto e Hipótese do Ensaio (Item 5D)

Na Corrida 259 (Item 5C), o driver completou com 100% de sucesso duas baterias de 500 MB (100 ciclos x 208 MB), mas travou na 3ª bateria durante concorrência de `FLUSH_TLB` e submissão SDMA0 com `RESET_REASON bit 30` (Data Fabric parity error). O diagnóstico apontou que:
1. No split chunking do SDMA (`Bc250SdmaPresentSubmitSplitChunkedWith`), o pacote GCR (`SDMA_OP_GCR_REQ` GL2 rinse) era emitido apenas no último pedaço (`last == TRUE`), deixando pedaços intermediários sem flush de cache antes de novas invalidações.
2. Na invalidação de TLB no anel do SDMA (`flushStream`), não havia emissão de GCR antes do loop de pacotes de invalidação.

O Item 5D (implementado no pacote 3.406.1.0) implementou:
- Emissão incondicional de GCR GL2 rinse em **100% dos chunks de split** (intermediários e final).
- Emissão de GCR GL2 rinse no `flushStream` do anel antes de invalidar a TLB.

---

## 2. Resultados Físicos Medidos na Corrida 260

O Daniel executou a Corrida 260 e disparou o estresse de 500 MB (`tools/RODAR_ESTRESSE_VRAM_500MB.cmd`):

### A. Bateria 1 de Estresse (PID 8160, 17:41:40 a 17:42:05 — 25 s)
- **Status:** **100% CONCLUÍDA COM SUCESSO.**
- Alocou e desalocou continuamente 31 texturas de 16 MB por **50 ciclos completos**.
- Log UMD arquivado: `BC250_UMD_LOG_8160.txt` (1.184.605 bytes).
- Término perfeito: todos os recursos liberados, `D3D10 DestroyDevice` executado, `CloseAdapter` retornado com `S_OK`.

### B. Bateria 2 de Estresse (PID 1940, iniciada às 17:42:34)
- O processo iniciou, criou o dispositivo D3D11 e a ponte RADV Vulkan com sucesso.
- Criou as texturas 1 a 5 de 16 MB com sucesso (80 MB de novas alocações).
- Ao alocar a Textura 6 (`hAlloc=0x40000b00`, `MakeResident TargetFence=7014`), a VRAM atingiu a capacidade máxima de 256 MB (somada ao DWM e Explorer).
- O VidMm acionou o mecanismo de **evicção e transferência** (`DXGK_OPERATION_TRANSFER`) para mover uma textura inteira de 16 MB (15.335.424 + 1.441.792 bytes) de VRAM para a memória de sistema.
- Às 17:42:35, durante a transferência e concorrência com `FLUSH_TLB` (id=15905), a máquina congelou.
- Log UMD arquivado: `BC250_UMD_LOG_1940.txt`, parou em:
  `[WDDM_WINSYS] WaitPaging ENTER: TargetFence=7014 CurrentFence=7012 SyncObj=0x40000480`
  `BC250_HANG v=1 seq=30 span=13 tid=9132 qpc=7617716776 op=WAIT_PAGING phase=ENTER`.

---

## 3. Telemetria Global Consolidada da Corrida 260

A telemetria do monitor em tempo real (`C:\BC250_MONITOR.csv`) e o log recuperado do KMD (`display-log-this-bind-RECUPERADO.txt`) revelam:
- **Tempo ativo contínuo:** **~94 segundos** (17:41:01 a 17:42:35).
- **FLUSH_TLBs executados:** **15.487 operações** (todas com `status=0` e latência de ~0,05 ms via bypass 4C).
- **Fills e Evicções do VidMm:** **363 Fills** e **371 evicções** processadas.
- **Linhas de log do KMD:** **101.406 linhas** registradas.
- **Temperatura (Tctl):** Entre 83,0 °C e 89,6 °C (estável, sem sobreaquecimento).
- **Ambos os anéis drenados:** No último registro de telemetria, `sdma1_idle=true`, `sdma1_rb_rptr == sdma1_rb_wptr == 0x000EECC4`.

### Teste de Memória Pós-Trava
O script automático de recuperação executou o teste de RAM (`ramtest-pos-trava.txt`):
- 8.891 MB testados em 16 threads × 2 passadas completas.
- **Erros detectados: 0**. A DRAM física permanece 100% íntegra.

---

## 4. Diagnóstico Cirúrgico da Trava na Evicção (TRANSFER)

### A. Motivo do Reset no Silício (FCH)
A leitura de CMOS/FCH pós-trava (`reset-reason-pos-trava.txt`) registrou:
```
=== MOTIVO DO ULTIMO RESET DA PLATAFORMA (FCH S5_RESET_STATUS 0xFED803C0) ===
RESET_REASON raw=0x40080400
RESET_REASON bit 10: (sem nome no upstream)
RESET_REASON bit 19: software wrote 0x6 to reset control register 0xCF9
RESET_REASON bit 30: a parity error occurred
```
O bit 30 confirma novamente **erro de paridade no Data Fabric**.

### B. O que estava ocorrendo no KMD no momento exato da trava
No log do KMD (`display-log-this-bind-RECUPERADO.txt`):
```
[DX91_PAGING_MOVE] op=TRANSFER hAlloc=000010F3000010F3 AllocOff=0x0 Size=15335424 Pass=[0,15335424) Src=0x240000 Dst=0x1100000 Dir=0 Flags=0x0 DW=413
BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=15901 tid=0000000000001FA4 qpc=7617778995 operation=12
BC250_HANG v=1 op=PAGING_DDI phase=LEAVE id=15901 tid=0000000000001FA4 qpc=7617799075 operation=12 status=0x00000000
[DX91_PAGING_MOVE] op=TRANSFER hAlloc=000010F3000010F3 AllocOff=0xEA0000 Size=1441792 Pass=[0,1441792) Src=0x1FC0000 Dst=0x2140000 Dir=0 Flags=0x0 DW=42
BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=15902 tid=0000000000001FA4 qpc=7617800522 operation=12
...
BC250_HANG v=1 op=PAGING_DDI phase=ENTER id=15905 tid=0000000000001FA4 qpc=7617829674 operation=12
[PT_WALK] IB_PROOF_BIND: PA=0x22C21A700 Bytes=15584 ExactMetadata=0 (miss: no poison)
```

### C. A Causa Raiz: Lacuna de GCR em `TransferVirtual` e `FillVirtual`
A análise aprofundada dos handlers em `driver/wdk/bc250_mcdm_callbacks.c` revelou a lacuna exata:
1. **`Bc250McdmBuildTransferVirtual` (Linhas 2012–2075):**
   - O VidMm divide alocações pesadas e emite loops de `Bc250SdmaEmitCopyLinear` (DW=7 por pacote de cópia).
   - Ao final da cópia de 16 MB (`15.335.424 + 1.441.792 bytes`), o handler chama apenas `Bc250PagingAppendIbAlignmentNops` para alinhar o fim do DMA buffer com NOPs.
   - **NENHUM PACOTE GCR (`SDMA_OP_GCR_REQ`) É EMITIDO!**
   - No `amdgpu` Linux upstream (`sdma_v5_0.c` / `amdgpu_vm_sdma.c`), toda transferência de buffer e movimentação de páginas exige explicitamente `sdma_v5_0_ring_emit_mem_sync` com invalidação e write-back do GL2 (`GCR_REQ`).
   - Sem o GCR rinse, 16 MB de dados recém-escritos pelo SDMA na DRAM física da APU permanecem pendentes no cache GL2 da GPU. Quando a CPU ou o VidMm imediatamente tenta ler ou remapear essas páginas durante o `FLUSH_TLB` subsequente, o Data Fabric detecta inconsistência de paridade de cache e dispara o reset `bit 30`.
2. **`Bc250McdmBuildFillVirtual` (Linhas 2310–2340):**
   - Similarmente, o zeramento de alocações novas (`CONST_FILL`) não emite GCR rinse ao final do preenchimento.

---

## 5. Critério e Plano para a Corrida 261 (Item 5E)

1. **Emissão de GCR Rinse em `TransferVirtual`:**
   - Em `driver/wdk/bc250_mcdm_callbacks.c` (`Bc250McdmBuildTransferVirtual`), emitir `Bc250SdmaEmitGcr` obrigatoriamente antes do alinhamento de NOPs (`Bc250PagingAppendIbAlignmentNops`) em todo DMA buffer de transferência concluído ou multipasse.
2. **Emissão de GCR Rinse em `FillVirtual`:**
   - Em `Bc250McdmBuildFillVirtual`, emitir `Bc250SdmaEmitGcr` ao final do preenchimento de paginação.
3. **Reserva de Espaço para GCR:**
   - Atualizar a contagem de pior caso `dwordsAvailable` em `Bc250McdmBuildTransferVirtual` e `Bc250McdmBuildFillVirtual` para incluir `BC250_SDMA_DWORDS_GCR_REQ` (5 dwords).
4. **Alvo de Validação:**
   - Executar a Corrida 261 sob o pacote 3.407.1.0 e disparar múltiplas baterias contínuas do estresse de 500 MB, superando a fase de evicção/despejo de texturas de 16 MB sem disparo de paridade no Data Fabric.
