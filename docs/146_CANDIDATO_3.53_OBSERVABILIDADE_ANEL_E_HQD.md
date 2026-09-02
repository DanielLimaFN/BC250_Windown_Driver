# Candidato 3.53 — Observabilidade Aprofundada (Anel Real, Base HQD e Microengines)

> **Pacote**: `3.53.0.0` | **Schema Telemetria**: `124` | **Modo**: `MCDM_LAB_BRINGUP (Bring-up Ativo / P1 Persistent Submission)`  
> **Status**: Pronto para ensaio físico no silício  
> **Veredito Atual**: `NÃO VALIDADO EM HARDWARE`

---

## 🎯 1. Objetivo do Candidato 3.53

O Candidato 3.52 comprovou que o unhalt seletivo do MEC2 (`CP_MEC_CNTL = 0x40000000`, `CP_ME_CNTL = 0x15000000`) é seguro, limpo e reproduzível com display ativo (Código 0). No entanto, o `RPTR` permaneceu em 0 enquanto `WPTR` registrou 12.

O Candidato 3.53 implementa a observabilidade profunda necessária para responder sem especulação:
1. **O anel contém exatamente os 12 DWORDs esperados?**
   - Captura dos 12 DWORDs reais da memória mapeada (`P1RingDword0` .. `P1RingDword11`) e auditoria contra o contrato (`P1RingDwordsMismatch == 0`).
2. **O HQD aponta para o endereço físico correto da VRAM?**
   - Readback direto dos registradores base do HQD: `CP_HQD_PQ_BASE_LO`, `CP_HQD_PQ_BASE_HI`, `CP_HQD_PQ_RPTR_REPORT_ADDR_LO/HI`, `CP_HQD_PQ_WPTR_POLL_ADDR_LO/HI`, `CP_HQD_VMID`, `CP_HQD_MQD_CONTROL`, `CP_HQD_IB_CONTROL`.
3. **Qual é o estado interno dos micro-blocos do CP após a submissão?**
   - Readback de `CP_CPC_STATUS`, `CP_CPC_BUSY_STAT`, `CP_CPC_STALLED_STAT1`, `CP_CPF_STATUS`, `CP_CPF_BUSY_STAT`, `CP_CPF_STALLED_STAT1`, `CP_STAT`, `CP_STALLED_STAT1` e `MEC2_PC` (`0x000086A4`).

---

## 🔬 2. Modificações Estruturais Implementadas

1. **Camada de Telemetria (`bc250_probe_telemetry.h` / `.c`)**:
   - Adicionados 31 novos campos de telemetria (`BC250_PROBE_P1_RING_DWORD_0` até `BC250_PROBE_P1_POST_MEC_PC`).
   - Bump do `TelemetrySchemaVersion` para `124`.

2. **Motor de Submissão (`bc250_submission_engine.h` / `.c`)**:
   - `BC250_SUBMISSION_SELFTEST_EVIDENCE`: adicionados buffers de 12 DWORDs reais/esperados e contadores de mismatch.
   - `Bc250SubmissionEngineSelfTest`:
     - Leitura e conferência byte a byte dos 12 DWORDs do anel emitido.
     - Leitura dos registradores base e endereços do HQD via seleção `GRBM_GFX_CNTL`.
     - Leitura dos registradores de status do microengine pós-submit.
   - `Bc250SubmissionEngineRecordTelemetry`: publicação de todos os novos registros no Hive de Probe do driver.

3. **Harness de Validação e Auditoria (`test-p1-submission-acceptance.ps1` / `audit-bc250mcdm-lab-package.ps1`)**:
   - Exibição formatada dos 12 DWORDs e registradores base na saída padrão e no arquivo de evidência JSON `p1-submission-acceptance.json`.
   - Exigência estrita de conformidade com o Schema 124.

---

## 📋 3. Comandos de Ensaio Físico no Hardware

Para executar a validação física no silício:

```powershell
# 1. Quarentena preventiva de resíduo anterior (se houver)
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine

# 2. Instalação e Binding do Candidato 3.53
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptLabInstallation

# 3. Confirmação do Device Manager (Código 0)
powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1

# 4. Execução do Teste de Aceitação P1
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

---

## 🔒 4. Veredito e Cláusula de Hardware

**`NAO VALIDADO EM HARDWARE`**  
O candidato está compilado, assinado e empacotado de forma fail-closed. O veredito definitivo será extraído exclusivamente da prova no silício físico através do tripé (`RPTR == WPTR`, fences de VRAM e integridade dos dados).
