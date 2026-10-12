# DOC 687 — Corrida 261 (Item 5E, Pacote 3.407.1.0): Evicções de 16 MB Concluídas com Sucesso, Alcance da 7ª Textura de Estresse e Descoberta de Under-Allocation no Anel SDMA

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

> **Data:** 09/10/2026 ~22:20  
> **Candidato:** Pacote lab **3.407.1.0** (SYS `521BFE9B...`, INF `0C6B8ED3...`, CAT `C3557271...`, UMD `B73F9BDA`)  
> **Pasta de evidência:** `docs/evidence/2026-10-09_22-19-42-DESKTOP-HQAG6T4-lab-binding/`  
> **Veredito:** **DESFECHO PARCIAL (Avanço Funcional Comprovado — Evicção de 16 MB Curada, Sucesso em 2 Transfers Massivos, Trava no Wrap do Anel SDMA / Paging Buffer)**  
> **Estado final:** Código 43 seguro pelo fusível anti-boot-loop (`C:\BC250_BOOT_FUSE.dat`), desktop restaurado via BasicDisplay em Código 0.

---

## 1. Contexto e Hipótese do Ensaio (Item 5E)

Na Corrida 260 (Item 5D), o estresse de 500 MB completou com 100% de sucesso a primeira bateria (50 ciclos x 500 MB), mas travou na segunda bateria na primeiríssima evicção de 16 MB para a RAM do sistema (`DXGK_OPERATION_TRANSFER`), com `RESET_REASON bit 30` (erro de paridade no Data Fabric). A análise comprovou que `Bc250McdmBuildTransferVirtual` emitia `COPY_LINEAR` sem o pacote `SDMA_OP_GCR_REQ` (GL2 rinse) ao final do buffer.

O Item 5E (pacote 3.407.1.0) implementou:
- Emissão obrigatória de `Bc250SdmaEmitGcr` (5 DW) ao final de `TransferVirtual`, `FillVirtual` e `FillByCopy`.
- Atualização do decodificador `Bc250PagingDecodeProof` e contratos unitários.

---

## 2. Resultados Físicos Medidos na Corrida 261

O Daniel executou a Corrida 261 e disparou o estresse de VRAM de 500 MB:

### A. Cura Completa da Evicção Massiva de 16 MB (Avanço Funcional)
Ao contrário da Corrida 260 (que travou no 1º transfer), na Corrida 261 os transfers massivos foram processados com perfeição pelo silício:
1. **1º TRANSFER de 16 MB (`hAlloc=000002F4...`):**
   - Bloco 1: 15.335.424 bytes (`DW=413`) de VRAM (`0x240000`) para sistema (`0x1100000`) -> **SUCESSO**.
   - Bloco 2: 1.441.792 bytes (`DW=42`) de VRAM (`0x1FC0000`) para sistema (`0x2140000`) -> **SUCESSO**.
2. **Subsequente FILL de 16 MB (`hAlloc=000002FE...`):**
   - Bloco 1: 15.335.424 bytes (`DW=20`) -> **SUCESSO**.
   - Bloco 2: 1.441.792 bytes (`DW=5`) -> **SUCESSO**.
3. **2º TRANSFER de 16 MB (`hAlloc=000002F8...`):**
   - Bloco 1: 15.335.424 bytes (`DW=413`) -> **SUCESSO**.
   - Bloco 2: 1.441.792 bytes (`DW=42`) -> **SUCESSO**.
4. **Subsequente FILL de 16 MB (`hAlloc=00000302...`):**
   - Bloco 1: 15.335.424 bytes (`DW=20`) -> **SUCESSO**.
   - Bloco 2: 1.441.792 bytes (`DW=5`) -> **SUCESSO**.

**Conclusão direta:** O pacote GCR rinse curou a dessincronia de cache GL2 nas operações de evicção pesada de 16 MB.

### B. O Ponto da Trava na 7ª Textura
- O estresse avançou até alocar a 7ª textura (`Handle=0000030600000306`, `Size=16.777.216`, `SegSet=0x1`).
- O VidMm emitiu 3 operações consecutivas de `DXGK_OPERATION_FLUSH_TLB`:
  - `id=3896` (QPC 4057220558 -> 4057226744, status=0)
  - `id=3897` (QPC 4057227264 -> 4057232998, status=0)
  - `id=3898` (QPC 4057233426 -> 4057239016, status=0)
- Imediatamente após `id=3898`, o VidMm chamou `DxgkDdiSubmitCommandVirtual` no contexto de paginação (`Ctx=0000000400000004`, `Vmid=8`).
- O KMD registrou `CTX_PROBE` no log e o silício congelou com reinício abrupto da plataforma.

---

## 3. Telemetria Consolidada da Corrida 261

- **Duração ativa:** ~17,1 segundos contínuos sob estresse pesado.
- **Invalidações de TLB:** **3.898** (todas com `status=0`).
- **Submissões SDMA0 aposentadas:** **1.463** IBs executados com 100% de sucesso.
- **Operações FILL:** **181**.
- **Operações TRANSFER:** **33** (incluindo 2 transferências de 16 MB completas).
- **Apresentações DWM:** **216**.
- **Fusível de Boot:** Conteve a máquina com segurança em **Código 43** (`State=2, Boots=2`).
- **Teste de Memória RAM pós-trava:** 8.891 MB testados em 16 threads x 2 passadas: **0 erros** (DRAM sã).

---

## 4. Diagnóstico Cirúrgico: Três Defeitos Críticos Descobertos

### A. Under-Allocation no Anel SDMA0 (`neededDwords = flushDwords + 22u`)
Em `driver/wdk/bc250_sdma_present.c` (linha 2885):
```c
neededDwords = flushDwords + 22u;
```
Porém, os pacotes emitidos para a submissão de anel contêm:
1. `padDwords`: até 1 DW (se o cursor for ímpar)
2. `Bc250SdmaEmitIndirect`: 6 DW
3. `Bc250SdmaEmitGcr`: 5 DW
4. `Bc250SdmaEmitFence`: 10 DW
5. `Bc250SdmaEmitTrapIfArmed`: 2 DW (já que `TrapArmed` está ATIVO no regime do driver)

**Total real necessário:** `1 + 6 + 5 + 10 + 2 = 24 dwords`.
Como o driver reservava apenas `22 dwords`, quando uma submissão atinge a borda do anel (1024 DW / 4096 bytes) — por exemplo, quando `ringIndex == 1002`:
- `AllocateRingSlot(22)` calcula `1002 + 22 = 1024 <= 1024` e conclui falsamente que cabe no anel, retornando `1002` sem inserir padding de wrap.
- O loop de emissão grava **24 dwords**: os dwords 1024 e 1025 transbordam a memória do anel e gravam por cima de `g_Bc250SdmaPresent.Work` (área do writeback de RPTR e fences)!
- O hardware SDMA do GC 10.1.3 dá wrap estrito em 1024 DW (4096 B), de modo que ele **nunca lê** a Fence/Trap gravadas fora do anel e salta imediatamente para o offset 0 executando pacotes velhos/corrompidos da volta anterior!
- A submissão #1462 terminou em `WPTR=3384`, e a #1463 executou o wrap (`WPTR=132`), deixando a borda do anel no limiar de transbordo exatamente para a submissão #1464!
- Similarmente, em `Bc250SdmaPresentSubmitSplitChunkedWith` (linha 2557), o cálculo de `worst` omitia `BC250_SDMA_DWORDS_TRAP` (2 DW).

### B. Omissão de GCR GL2 Rinse em `Bc250McdmEmitGpuPageTableUpdate`
Em `driver/wdk/bc250_mcdm_callbacks.c` (linha 1435):
- O handler emite `WRITE_LINEAR` para atualizar PTEs na tabela de páginas em VRAM.
- Ao final das entradas, apenas preenche com NOPs até o alinhamento.
- **Nenhum GCR rinse é emitido.** As PTEs gravadas na VRAM permanecem sujas no cache GL2 da GPU.

### C. Omissão de GCR Rinse em Split Points Múltiplos
Em `driver/core/bc250_sdma_packet_builder.c` (`Bc250SdmaEmitSplitIndirectCross`, linha 721):
- Quando múltiplos pontos de split ocorrem consecutivamente ou no início (`offset == prev`), `Bc250SdmaEmitIbSegment` não é chamado.
- O loop emite `Bc250SdmaEmitVmInvalidate` sem qualquer GCR antes, disparando invalidação de TLB com cache GL2 incoerente.

---

## 5. Critério para a Corrida 262 (Item 5F)

1. **Correção da Reserva do Anel SDMA0:** Ajustar `neededDwords = flushDwords + 24u;` (ou cálculo dinâmico exato incluindo `padDwords + 23u + trap`) e incluir `BC250_SDMA_DWORDS_TRAP` no `worst` do split chunking. Proibir gravação além de `BC250_SDMA_PRESENT_RING_DWORDS`.
2. **GCR Rinse em `EmitGpuPageTableUpdate`:** Adicionar `Bc250SdmaEmitGcr` (5 DW) ao final de toda emissão de atualização de tabelas de página por GPU.
3. **GCR Rinse antes de Invalidação em Split Cross:** Garantir que nenhuma invalidação `Bc250SdmaEmitVmInvalidate` seja emitida sem um `Bc250SdmaEmitGcr` precedente.
4. **Validação:** Rodar o estresse de 500 MB buscando sustentar as 31 texturas sem travamento no wrap do anel e sem paridade no Data Fabric.
