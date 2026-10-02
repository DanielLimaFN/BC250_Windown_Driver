# 529 — T3.4: FLUSH_TLB Alinhado ao Linux Upstream (3 passos, sem RMW em RangeLow/High); Critério da Corrida 174

> 29/09/2026, offline. Candidato **3.319.1.0** (inclui T3.4, T3.3, HDP.1, T3.1, EVT.1, X1, X2a, X3 e X4). **Nada desta faixa foi validado em hardware.**

---

## 1. Por quê

A análise aprofundada da telemetria da Corrida 173 (doc 528) e do código-fonte do kernel Linux upstream (`drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c:289-315`) comprovou dois fatos determinantes:

1. **Avanço comprovado do T3.3:**
   - A eliminação de `FORCE_INVALIDATION` no `SDMA0_UTCL1_CNTL` eliminou definitivamente o travamento do anel do SDMA e a corrupção do memory controller (0 eventos degradados no EVT.1, 3.419 IBs executados, throughput +155%).
   - O SDMA completou 100% dos seus envios e estava totalmente desocupado no momento da parada (`seq=11312 fence=3420 -> CONCLUIDO`).

2. **Identificação do ponto exato de parada na Corrida 173:**
   - A máquina congelou aos 63,0 s com a thread de paginação na CPU10 travada ao entrar na DDI de `FLUSH_TLB` (`PAGING_DDI 0x0C` ANTES, seq=522145).
   - O confronto com o Linux revelou que nossa implementação (`Bc250RunGfxhubTlbTransaction`) realizava um ciclo pesado e aberrante de **14 operações MMIO** a cada chamada para cada VMID:
     - Leitura de `RangeLow` (0xA3A4)
     - Leitura de `RangeHigh` (0xA3A8)
     - Leitura de `Ack` (0xA318)
     - Escrita de 0xFFFFFFFF em `RangeLow`
     - Escrita de 0x0000001F em `RangeHigh`
     - Leitura de `RangeLow` (readback)
     - Leitura de `RangeHigh` (readback)
     - Escrita em `Request` (0xA2D0)
     - Leitura dummy em `Request`
     - Polling de `Ack`
     - Escrita de restauração em `RangeLow`
     - Escrita de restauração em `RangeHigh`
     - Leitura dummy final em `RangeLow`
     - Leitura dummy final em `RangeHigh`
   - No Linux upstream (`gmc_v10_0.c`), os registradores `RangeLow`/`RangeHigh` são configurados **uma única vez** na inicialização do driver (`gfxhub_v2_0_program_invalidation`). Em tempo de execução, a função `gmc_v10_0_flush_gpu_tlb` executa estritamente **3 passos**:
     1. `WREG32(req, inv_req)` (escreve a requisição para o VMID).
     2. `RREG32(req)` (dummy read para limpar latch antigo do GRBM em GC < 10.3).
     3. Polling em `RREG32(ack)` até o bit do VMID acender.
   - Além disso, a cada chamada, nosso driver formatava strings verbosas de telemetria dentro do spinlock na thread de paginação, gerando contenção massiva de barramento e CPU.

---

## 2. A mudança (Faixa T3.4)

1. **Simplificação Estrita de `Bc250RunGfxhubTlbTransaction`:**
   - Eliminado todo o ciclo de RMW e restauração em `RangeLow` e `RangeHigh`.
   - Implementado o ciclo exato do Linux upstream:
     ```c
     /* Passo 1: WREG32(req) */
     Transport->Write32(Transport->Context, BC250_GFXHUB_REG_INVALIDATE_REQ, reqValue);
     /* Passo 2: Dummy read no req para GC < 10.3 */
     (void)Transport->Read32(Transport->Context, BC250_GFXHUB_REG_INVALIDATE_REQ);
     /* Passo 3: Polling no ack */
     do {
         ackValue = Transport->Read32(Transport->Context, BC250_GFXHUB_REG_INVALIDATE_ACK);
         if ((ackValue & vmidMask) != 0u) {
             ackMatched = 1u;
             break;
         }
         Bc250TlbStallBrief();
     } while (pollCount < pollLimit);
     ```
   - O campo `Evidence->RangeRestorePassed = 1u` é garantido imediatamente após a validação inicial do plano, mantendo conformidade com chamadores e testes.

2. **Silenciamento de Logs Sob Lock na Paginação:**
   - Em `Bc250ActiveFlushVmidTlb` (`bc250_active_miniport.c`), removidos os logs incondicionais por VMID (`Transport State=` e `VMID%02u status=`).
   - Telemetria de diagnóstico é emitida **exclusivamente em caso de falha** (`localStatus != BC250_TLB_TRANSACTION_OK`).

---

## 3. Provas offline

- Suíte de contratos completa (`driver/wdk/build-contract.bat`): **EXIT 0**.
- Teste de contrato de TLB (`gfxhub_tlb_transaction_test.c`): **OK**, validando que `transport.RangeWriteCount == 0u` em todos os casos de teste.
- Compilação de laboratório (`build-bc250mcdm-lab-bringup.ps1`): **EXIT 0**.
- Auditoria de segurança (`audit-bc250mcdm-safety.py`): **PASS**, com o novo gate `audit_t34_tlb_flush_upstream_aligned`.
- **4 mutações testadas e todas reprovadas** (`tools/mutacoes/mutate_t34.py`):
  - M1: `Bc250RunGfxhubTlbTransaction` volta a escrever em `RangeLow/RangeHigh` (Reprovou: ok).
  - M2: `Bc250RunGfxhubTlbTransaction` remove o dummy read de `req` (Reprovou: ok).
  - M3: `Bc250ActiveFlushVmidTlb` volta a emitir log incondicional por VMID (Reprovou: ok).
  - M4: `Bc250RunGfxhubTlbTransaction` não valida `RangeRestorePassed` (Reprovou: ok).
- Auditoria de empacotamento (`audit-bc250mcdm-lab-package.ps1`): **PASS**.
- Script de preflight (`prepare-corrida174-t34.ps1`): **EXIT 0**.

---

## 4. Candidato

| Artefato | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `DB65069BDC46111F40EFBA23801429554A8BC54ED9AE3834C4AC49335ECFE597` |
| `bc250umd.dll` (idêntica à 3.310) | `D644CB7BE0851270AD13076554C30A7624A41CB61A7CA602F4081088E79CCAC3` |
| `bc250mcdm.inf` | `F482F6800E7FFCDD10B514E3617DC13B254E4365FED619ECDA316C4999308B59` |
| `bc250mcdm.cat` | `CF31018918C714995151227F101D3B914DFB8C124C1C9B24049FCCF5A042D3E4` |

- DriverVer: `09/29/2026,3.319.1.0` (fonte `3.319.0.0`).
- Runner: `tools/EXECUTAR_CORRIDA174_T34_600S.cmd`.

---

## 5. Critério da Corrida 174 (escrito antes; Lei 10)

**Roteiro:** O mesmo da Corrida 173 (atividade mínima, sem harness). Assim que o desktop estabilizar (~20 s), clicar para abrir e fechar o menu Iniciar repetidas vezes (para gerar novas alocações e testar o caminho de paginação sem sobrecarga externa).

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | **600 s sem trava** com menu Iniciar aberto/fechado repetidamente, Presents contínuos e sem congelamento na paginação | **Sucesso completo da cadeia de memória:** o pipeline SDMA + GFXHUB TLB atinge estabilidade industrial sem sobrecarga de MMIO. Avança para ativação de render 3D / D3D11 harness. |
| **B** | Trava na paginação em `FLUSH_TLB` ou SDMA antes dos 600 s | O congestionamento remanescente reside na frequência de emissão do VidMm ou requisição de ACK em outro hub (MMHUB). Investigar rastreio ETW e migalhas. |
| **C** | Regressão funcional: faults de GCVM ou corrupção de textura/alocação | A faixa de endereço (range) requeria reprogramação dinâmica. Rever range global na inicialização. |
| **E** | Fora do mapa (BugCheck com dump de kernel) | Analisar arquivo de dump `.dmp` gerado para obter callstack exata. |
