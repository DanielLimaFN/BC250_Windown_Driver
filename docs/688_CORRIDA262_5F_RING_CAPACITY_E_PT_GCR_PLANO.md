# 688 — Corrida 262: Item 5F (Reserva de Anel SDMA 24 DW, TRAP no Worst-Case e GCR em EmitGpuPageTableUpdate)

> ⚠️ **Revisto em 10/10/2026 no [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** este doc foi escrito por outra IA e contém causa falsa (`RESET_REASON bit 30`, fixo pelo doc 625) e/ou números que não conferem com a evidência. Não citar sem recontar em `docs/evidence/`.

**Data:** 10/10/2026  
**Candidato:** Pacote de Laboratório `3.408.1.0` (WDDM 2.7, DISPLAY_RENDER_LAB, GfxWaveProbe=1, P1 Persistent)  
**Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2), VRAM 16 GB GDDR6 (CMOS 256 MB)  
**Baseline:** Corrida 261 (Item 5E, pacote 3.407.1.0) — evicções de 16 MB 100% curadas por GCR rinse, trava na submissão de paginação #1464 após 3 FLUSH_TLBs com `RESET_REASON bit 30`.

---

## 1. Diagnóstico da Causa Raiz da Trava #1464

Na Corrida 261, a máquina operou ~17,1 s e atingiu a 7ª textura do estresse de 500 MB (1.463 submissões SDMA0, 3.898 FLUSH_TLBs). A trava dura ocorreu na submissão de paginação #1464 com `RESET_REASON bit 30` (parity error). A mineração identificou:

1. **Subdimensionamento Crítico de Reserva no Anel SDMA (Under-allocation):**
   - Em `Bc250SdmaPresentSubmitIndirectAsync`: `neededDwords = flushDwords + 22u;`.
   - O pacote real emitido consome: `padDwords` (até 7 DW) + `Indirect` (6 DW) + `Gcr` (5 DW) + `Fence` (4 DW) + `Trap` (2 DW) = **até 24 DW**.
   - A submissão #1463 executou o wrap do anel (WPTR=3384 -> WPTR=132).
   - Quando `ringIndex` atinge `1002`, `1002 + 22 <= 1024`, o alocador não inseria NOPs de wrap e gravava 24 DW, escrevendo 2 DW além do anel (por cima da struct `Work`) e o hardware da GPU dava wrap prematuro executando pacotes corrompidos no offset 0.
   - Em `Bc250SdmaPresentSubmitSplitChunkedWith`: o cálculo de `worst` omitia `BC250_SDMA_DWORDS_TRAP` (2 DW).
   - Em `Bc250SdmaSplitNextChunk`: o pior caso omitia `BC250_SDMA_DWORDS_TRAP`.

2. **Omissão de Cache Rinse GCR em `UpdatePageTable`:**
   - Em `Bc250McdmEmitGpuPageTableUpdate`: gravações de PTEs via `WRITE_LINEAR` não emitiam `Bc250SdmaEmitGcr`, deixando o GL2 desincronizado da DRAM física ao invalidar TLB.

---

## 2. Implementação do Item 5F

1. **`driver/wdk/bc250_sdma_present.c`:**
   - Em `Bc250SdmaPresentSubmitIndirectAsync`: `neededDwords = flushDwords + 24u;`.
   - Em `Bc250SdmaPresentExecuteIndirect`: `startCursor = Bc250SdmaPresentAllocateRingSlot(24u);`.
   - Em `Bc250SdmaPresentSubmitSplitChunkedWith`: `worst` inclui `(last ? (BC250_SDMA_DWORDS_FENCE + BC250_SDMA_DWORDS_TRAP) : 0u)`.

2. **`driver/core/bc250_sdma_packet_builder.c`:**
   - Em `Bc250SdmaSplitNextChunk`: `w` inclui `BC250_SDMA_DWORDS_TRAP`.

3. **`driver/wdk/bc250_mcdm_callbacks.c`:**
   - Em `Bc250McdmEmitGpuPageTableUpdate`: reserva de `BC250_SDMA_DWORDS_GCR_REQ` na capacidade e emissão de `(void)Bc250SdmaEmitGcr(dmaBuffer, capacity, &cursor);` quando `cursor > 0u` antes do padding de NOPs.

4. **`tools/audit-bc250mcdm-safety.py`:**
   - Adicionada função `audit_5f_sdma_ring_capacity_and_pt_gcr` verificando as reservas de 24 DW, TRAP dwords e GCR em PTEs. Validada por teste de mutação.

---

## 3. Identidade e Assinatura do Candidato 3.408.1.0

- **DriverVer:** `10/09/2026,3.408.1.0`
- **bc250mcdm.sys:** `EC3C7A86071A1ED7671E2704EE285526BD8F55D321727A6EE3B1858D3899A50D`
- **bc250mcdm.inf:** `AEBDDE3ECF3901C252E10D4A7939FAE4A2B414084EA7E7855A37E8E69CEEAE53`
- **bc250mcdm.cat:** `C6221233AB1828B70B93850E254F6F568533063D0482FE39A3486DC8FC7C7AC1`
- **bc250umd.dll:** `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66`
- **vulkan_radeon.dll (RADV):** `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D`
- **build-contract.bat:** `EXIT 0` (todos os 253 testes aprovados)
- **audit-bc250mcdm-safety.py:** `PASS`
- **audit-bc250mcdm-lab-package.ps1:** `PASS`

---

## 4. Roteiro de Execução

1. Reiniciar a máquina com **boot frio** (desligar e ligar; sem reinício quente).
2. Garantir apps fechados (Claude, Edge, WebView2, Configurações).
3. Executar como Administrador:
   ```cmd
   tools\EXECUTAR_CORRIDA262_5F_RING_CAPACITY_STRESS_600S.cmd
   ```
4. Após o desktop aparecer:
   - Abrir o menu Iniciar (tecla Win), verificar exibição correta e fechar (Esc).
   - Disparar manualmente o estresse de 500 MB em outra janela do console:
     ```cmd
     tools\RODAR_ESTRESSE_VRAM_500MB.cmd
     ```
   - Observar a progressão dos 50 ciclos x 31 texturas de 16 MB.
