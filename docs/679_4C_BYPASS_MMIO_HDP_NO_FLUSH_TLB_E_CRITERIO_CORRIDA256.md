# 679 — 4C: bypass de MMIO de HDP no caminho crítico de FLUSH_TLB em APU. Critério da Corrida 256

**Data:** 09/10/2026. **Natureza:** preparação para a Corrida 256.
**Candidato:** lab **3.402.1.0** (Corrida 256).

---

## 1. A causa investigada (4C)

Nas últimas 20 corridas (236 a 255), a assinatura do travamento permaneceu invariante:
- **A GPU está 100% ociosa:** todos os anéis drenados (`RPTR == WPTR`), zero faults no GFXHUB, zero TDRs.
- **As CPUs travam simultaneamente:** interrupções de relógio param, sem BugCheck nem TDR.
- **Momento da parada:** o KMD frequentemente tem seus últimos registros durante rajadas intensas de paginação (`PAGING_DDI op=12` = `FLUSH_TLB`) disparadas pelas alocações do DWM e da Pesquisa/Menu Iniciar.

### A anomalia de HDP em APU:
No silício da APU BC-250 (`Cyan Skillfish`), a VRAM é DRAM contígua do sistema gerenciada pelo Memory Controller da APU, acessada diretamente pela CPU por endereços físicos de DRAM via WC (`P1b` e `C3`). Não existe uma ponte PCIe discreta nem barramento de GPU remota exigindo flush de leitura de HDP (`HDP_READ_CACHE_INVALIDATE`, offset `0x3FC4`).

No entanto, no nosso código até a Corrida 255:
- Toda chamada de `FLUSH_TLB` (tanto em `Bc250ActiveFlushTlbHdpOnly` quanto em `Bc250ActiveFlushMappedGfxhubTlb`) executava leitura e escrita MMIO direta no registrador `0x3FC4`.
- Sob centenas de operações de paginação por segundo ao abrir o menu, isso gerava um volume severo de acessos MMIO redundantes no barramento interno da APU (Data Fabric / MMHUB).

---

## 2. O que mudou (Implementação da 4C)

Em `driver/wdk/bc250_active_miniport.c`:
1. **`Bc250ActiveFlushTlbHdpOnly`:**
   ```c
   if (!Bc250VramCpuHdpNeeded()) {
       _mm_mfence();
       return;
   }
   ```
   Como `Bc250VramCpuHdpNeeded()` é `FALSE` em APU (quando a CPU acessa VRAM via DRAM/P1b), 100% das leituras/escritas MMIO no registrador `0x3FC4` são suprimidas. A barreira de instruções da CPU (`_mm_mfence()`) é estritamente preservada para garantir a ordem de coerência de memória.
2. **`Bc250ActiveFlushMappedGfxhubTlb`:**
   A chamada prévia de HDP flush só é acionada se `Bc250VramCpuHdpNeeded()` for verdadeiro.
3. **Anúncio no boot:**
   Em `Bc250ActiveStartDevice`, uma linha de telemetria é emitida:
   `[4C_HDP_FLUSH_BYPASS] ATIVO: HdpNeeded=0 (APU=0 -> MMIO 0x3FC4 suprimido em FLUSH_TLB, _mm_mfence() preservado)`.

---

## 3. Entregas complementares da sessão preparadas offline

Além da 4C (única variável funcional no SYS da Corrida 256), foram implementadas ferramentas e contratos auxiliares:
- **4A (Diagnóstico pós-trava):** Criado `tools/coletar-mca-whea.ps1` e integrado na etapa `[1b]` de `tools/coletar-pos-trava.ps1`. O coletor agora extrai registros CPER/WHEA (`WhLg`), criadores `PSHD` e `KRNL`, eventos WHEA-Logger e Kernel-Power 41 no primeiro boot após uma trava.
- **4B (Contrato SMU/DPM):** Adicionadas as definições e funções `Bc250SmuGetEnabledFeatures` e `Bc250SmuRequestGfxclk` em `driver/core/bc250_smu_telemetry_contract.h/.c`, cobertas com testes em `driver/tests/smu_telemetry_contract_test.c`. Permanece offline / desarmado para não misturar variáveis na 256.
- **4D (Estresse sintético de VRAM/paginação):** Criado utilitário nativo Win32 `tools/bc250-vram-paging-stress.exe` para permitir estressar paginação/VRAM de forma controlada sem depender de abrir/fechar o Menu Iniciar.

---

## 4. Pacote lab 3.402.1.0 (Corrida 256)

| Arquivo | Hash SHA-256 |
|---|---|
| **SYS assinado** | `1C01B05201F85C5266076DBC99BD244695FC0EB55B965C2DD34A64A27116A817` |
| **INF** | `E8EF6697C6B0F8A7185C7CFAA0E2196AB402915EF29AED6AB87F89C2B278CA73` |
| **CAT** | `FFFC77F14EFE93672C404DC468F7CCF69F04442D9D279756B52F3D19805B9BC4` |
| **UMD** | `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66` (inalterada) |
| **RADV** | `B4AE2E1402505405F98EE625C9A736513F171604D051BF957AC25931F9ED0B6D` (inalterada) |
| **RLC** | `20ACEFDB6128A36275F4382425A109A7C9927F6053EAB685BB51164FF1D18CFB` (inalterado) |

- **Backup arquivado:** `driver/wdk/package/output_lab_3.402.1.0_256`.
- **Preflight:** `tools/prepare-corrida256-4c-hdp-flush-bypass.ps1`.
- **Lançador:** `tools/EXECUTAR_CORRIDA256_4C_HDP_FLUSH_BYPASS_600S.cmd`.
- **Portões:** `BC250 MCDM SAFETY AUDIT: PASS` e `BC250 MCDM LAB PACKAGE AUDIT: PASS`.

---

## 5. Critério da Corrida 256 (escrito antes da execução física)

**Procedimento pré-bind:**
1. Em cmd elevado: `tools\BC250_RECOVERY.cmd`.
2. Em PowerShell: `tools\inspect-boot-fuse.ps1 -Reset`.
3. **Desligar completamente a máquina (boot frio).** Não reiniciar.
4. Ligar e rodar `tools\EXECUTAR_CORRIDA256_4C_HDP_FLUSH_BYPASS_600S.cmd`.
5. Roteiro: só o menu Iniciar (abrir com Win, esperar 10 s, fechar com Esc, esperar 30 s).

**Pré-condições obrigatórias:**
- `[4C_HDP_FLUSH_BYPASS] ATIVO: HdpNeeded=0` presente no log do KMD.
- `[C4_FILL] chave=1 X2b=0 fatia_max=0x400000` ativo.
- `[C3_VRAM_WC] chave=1 P1b=1 ... =8` ativo.
- `[C1_GFX_DOORBELL] ATIVA` ativo.
- Desktop do Windows visível e operante.
- Zero `GFXHUB_FAULT`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Pré-condição completa e **600 s sustentados sem trava** | O congestionamento MMIO de HDP no Data Fabric era o gatilho da parada. Repetição obrigatória. |
| **B** | Pré-condição completa e trava dura de sempre com a GPU ociosa | O MMIO de HDP no `FLUSH_TLB` não era a causa suficiente. A 4C fica por paridade com APU e higiene de tráfego. Próximo passo: DPM/SMU (4B) ou investigação de refresh do Memory Controller. |
| **C** | Linha `[4C_HDP_FLUSH_BYPASS]` ausente do log | O candidato não armou. A corrida é inconclusiva. |
| **D** | Falha de coerência de memória, pixels corrompidos ou BugCheck no `FLUSH_TLB` | A supressão de HDP causou inconsistência (improvável, pois APU tem memória unificada e `_mm_mfence()` está ativo). |
| **E** | Fora do mapa (BugCheck diferente, TDR, etc.) | Analisar logs e WHEA coletados pelo novo script 4A. |

**NÃO VALIDADO EM HARDWARE:** efeito no silício da supressão do MMIO de HDP no `FLUSH_TLB`.
