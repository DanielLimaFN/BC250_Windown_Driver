# Passagem de bastão — retarget drenado de VMID e eliminação do stall RCIU (3.117.1.0)

> **Data:** 2026-09-09  
> **Pacote:** laboratório `3.117.1.0`, assinado e auditado, **NÃO INSTALADO**  
> **Estado:** implementação e auditoria offline concluídas; aguardando ensaio físico após boot frio  
> **Veredito:** **NÃO VALIDADO EM HARDWARE**

---

## 1. Origem da mudança — Resultados do 3.115 e 3.116

A corrida 3.116.1.0 (17:47) validou a reconciliação do cursor no silício (`WPTR: 0 -> 12`), mas refutou que a divergência de cursores fosse a causa do não consumo do anel:
- `RPTR` permaneceu estagnado em 0.
- Fence 18 não foi produzida; canários de VRAM permaneceram intactos.
- `CP_CPC_STALLED_STAT1 = 0x00040000` (`MEC2_WAIT_ON_RCIU_READ`) reapareceu **antes mesmo do doorbell** (`MEC_PRE_SUBMIT_CPF`).
- A ausência do stall na corrida 3.115 foi meramente transitória.

A investigação profunda da arquitetura de Command Processor da AMD (GFX10) identificou o gatilho do stall: **a tentativa de reprogramar `CP_HQD_VMID` para o VMID 9 enquanto a HQD ainda estava em execução (`CP_HQD_ACTIVE = 1`)**. As 17 submissões de autoteste do driver rodavam sob VMID 0; ao preparar a submissão 18 para o harness WDDM, o driver gravava diretamente o novo VMID na HQD ativa. Na arquitetura GFX10, reatribuir VMID com a HQD ativa corrompe o microcódigo do MEC e coloca o pipeline de compute em espera permanente da malha RCIU.

---

## 2. O que foi Implementado no Candidato 3.117

### A. Retarget Drenado de VMID (`Bc250SubmissionEngineRetargetDrainedIbVmid`)
Local: `driver/wdk/bc250_submission_engine.c`
A rotina é chamada exclusivamente sob `SubmissionEngine.Lock` em `PrepareComputeVmid` antes de planejar o submit WDDM:
1. **Portão de fila drenada**: exige `LastSubmittedFence == LastRetiredFence` e executa reconciliação de cursores físicos (`Bc250PersistentRingReconcileDrainedCursor`). Se houver trabalho pendente ou o hardware estiver ocupado, recusa (`STATUS_GRAPHICS_ALLOCATION_BUSY`).
2. **Desativação limpa**: se `ACTIVE == 1`, escreve `BC250_HQD_MMIO_ACTIVE = 0` e aguarda até `BC250_SUBMISSION_ACTIVE_POLLS` por confirmação do hardware. Se não desativar, falha fechado com timeout.
3. **Barreira de memória e shadow**: sincroniza `ComputeWptrShadowVirtual` com o cursor reconciliado sob `KeMemoryBarrier()`.
4. **Reprogramação ordenada**:
   - `BC250_HQD_MMIO_PQ_WPTR_LO` recebe o cursor reconciliado.
   - `BC250_HQD_MMIO_VMID` recebe `0x900` (VMID 9) com verificação de readback estrito.
   - `BC250_HQD_MMIO_CP_PQ_WPTR_POLL_CNTL1` rearma a máscara do anel (`1 << QueueMaskBit`).
5. **Reativação da HQD**: escreve `BC250_HQD_MMIO_ACTIVE = 1` com verificação de readback (`active & 1 != 0`).
6. **Restauração do seletor**: restaura `BC250_GRBM_GFX_CNTL` e emite a telemetria `[MEC_VMID_RETARGET]`.
7. **Regra de ouro mantida**: `RPTR` físico **NUNCA** é escrito.

### B. Telemetria de Estágios RCIU (`Bc250SubmissionEngineCaptureRciuStage`)
Amostra o estado do CP/CPC/CPF sem selecionar uma HQD (evitando que a leitura indexada afete o estado da fila):
- `BEFORE_GCVM_INSTALL` e `AFTER_GCVM_INSTALL`
- `RETARGET_ENTRY` e `RETARGET_EXIT`
- `PUBLISH_BEFORE_COHERENCY` e `PUBLISH_AFTER_COHERENCY`
- `PUBLISH_AFTER_MMIO_WPTR` e `PUBLISH_AFTER_DOORBELL`

### C. Correção dos Testes de Mutação Offline
Local: `tools/test-mec-vmid-retarget-safety.py`
- Corrigida a substituição da allowlist de escrita (`Bc250MmioTopologyWriteOffsetAllowed`) para validar com exatidão que `CP_BUSY_STAT` não pode ter acesso de escrita.

---

## 3. Identidade Criptográfica do Pacote 3.117.1.0

Localização: `driver/wdk/package/output_lab`  
Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| **SYS de laboratório** (`bc250mcdm.sys`) | `8DFC8A3E36E3D6AE6101DEF48616AB69FEF80F2A9B8BA67548DA49638D32E38A` |
| **UMD de laboratório** (`bc250umd.dll`) | `2A25BE2C6D807917BDCA2D8A03BDDEA94FB3DD018B312EC1FDAF1AD5D460969B` |
| **INF de laboratório** (`bc250mcdm.inf`) | `E45F4C103669A229449758F9E6FB5C0E81ADBDD43910CBC1F9569BAEBD7450A2` |
| **Catálogo assinado** (`bc250mcdm.cat`) | `EA6DC1EB741DF0CC0DA9E890877023400760EAB3D21DD5448036B3D2D4908FFF` |
| **DriverVer** | `09/09/2026,3.117.1.0` |
| **Perfil** | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| **WddmContractVersion** | `27` (`WDDM 2.7`) |
| **Modo** | `P1 PERSISTENT SUBMISSION` |

---

## 4. Validação Offline Executada

| Verificação | Comando | Resultado |
|---|---|---|
| Mutações de Retarget VMID | `python tools/test-mec-vmid-retarget-safety.py` | **PASS** (6/6 mutações) |
| Mutações do Cursor MEC | `python tools/test-mec-cursor-reconciliation-safety.py` | **PASS** (5/5 mutações) |
| Mutações de Política WGP | `python tools/test-wgp-policy-safety.py` | **PASS** (4/4 mutações) |
| Auditoria de Segurança Geral | `python tools/audit-bc250mcdm-safety.py` | **PASS** |
| Auditoria UMD Node 1 | `python tools/audit-umd-node1-submission.py` | **PASS** |
| Contratos Unitários WDK | `driver/wdk/build-contract.bat` | **EXIT=0** |
| Auditoria do Pacote Lab | `tools/audit-bc250mcdm-lab-package.ps1` | **PASS** |

---

## 5. Procedimento para o Ensaio no Hardware (Regra 10)

> [!IMPORTANT]
> A corrida anterior (3.116) gravou o marcador `C:\BC250_REQUIRE_CLEAN_BOOT.json`.
> Antes de qualquer bind, **é obrigatório reiniciar o computador** para iniciar em um boot limpo com `BasicDisplay`.

Após o reinício da máquina:
```powershell
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -EnableRenderNode -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -RunUmdNode1Tripod
```

### Critério Físico de Aceite do Tripé Node 1:
- `P1SubmitAccepted`: 17 -> 18
- `P1RetiredTotal`: 17 -> 18
- `RPTR` físico avançando: `0 -> 12`
- Fence 18 escrita sobre o poison
- Canários de VRAM alterados conforme esperado.
- Linha `[MEC_VMID_RETARGET]` confirmando `Active: 1 -> 0 -> 1` e `Target=0x900`.
- Eliminação estável de `MEC2_WAIT_ON_RCIU_READ` nos logs `[MEC_RCIU_STAGE]`.
