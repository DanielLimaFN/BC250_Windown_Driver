# Handoff — Candidato 3.61.0.0 (Correção de Endereços Físicos no IB)

**Data do corte:** 31/08/2026, após 09:14 (UTC-3)  
**Workspace:** `C:\Users\Daniel\Desktop\NamixWDDM`  
**Pacote preparado:** `3.61.0.0` / Schema de telemetria `127`  
**Estado físico do 3.61:** **NÃO VALIDADO EM HARDWARE** (Aguardando boot frio)

---

## 1. Resumo Executivo e Causa-Raiz Corrigida

O ensaio físico do Candidato 3.60 provou com 100% de sucesso o gate one-shot de computação (`ShaderPhysicalReady=1`), mas o motor de submissão persistente (P1) parou dentro do Indirect Buffer (`RPTR=4`, `CP_HQD_IB_CONTROL=0x84300000`, `CP_MEC_ME2_HEADER_DUMP=0xC0037602`).

### Diagnóstico Causal:
No modo de operação Host PA (`Vmid0PageTable=0`), a GPU não utiliza tradução dinâmica de GPUVA para o VMID 0.
Em `driver/wdk/bc250_submission_engine.c`, o construtor do IB utilizava constantes virtuais:
- `descriptor.ShaderGpuAddress = BC250_HQD_SHADER_GPUVA;` (`0x00004000`)
- `descriptor.ResultGpuAddress = BC250_HQD_RESULT_GPUVA;` (`0x00005000`)
- `descriptor.FenceGpuAddress  = BC250_SUBMISSION_DISPATCH_FENCE_OFFSET;` (`0x00002300`)

Isso fazia com que o `COMPUTE_PGM_LO` fosse gravado como `0x40`, instruindo a GPU a buscar código em `0x4000` (baixa memória do sistema) em vez de `0xF400A94000`, travando a Wave.

### Correção Implementada no 3.61:
1. Em `Bc250SubmissionEngineSelfTest`:
   ```c
   descriptor.ShaderGpuAddress = engine->Buffers.ShaderGpuAddress;
   descriptor.ResultGpuAddress = engine->Buffers.ResultGpuAddress;
   descriptor.FenceGpuAddress  = engine->BasePhysical + BC250_SUBMISSION_DISPATCH_FENCE_OFFSET;
   ```
2. Em `Bc250SubmissionEngineP2StreamTest`:
   ```c
   descriptor.ShaderGpuAddress = engine->Buffers.ShaderGpuAddress;
   descriptor.ResultGpuAddress = resultGpuAddress;
   descriptor.FenceGpuAddress  = dispatchFenceGpuAddress;
   ```

---

## 2. Artefato Exato Construído e Assinado

Manifesto: `driver/wdk/package/output_lab.manifest.json`

| Artefato | SHA-256 |
|---|---|
| `output_lab/bc250mcdm.sys` | `99521CA828AE5182B40878DB9D0BD41D1E692A30ECFD0BEA6D71C941B2FB9179` |
| `output_lab/bc250mcdm.inf` | `5956272C0322944F366E39D5D076C0578B903A967EF3322812FD960E56456157` |
| `output_lab/bc250mcdm.cat` | `EFBFD8C3490C23EE98AA16F2C49D68AB204DFD96A5F8C949A451DC8FCD133A13` |

---

## 3. Procedimento Obrigatório para o Próximo Ensaio

> **IMPORTANTE**: Estado de microengine e Command Processor não caem com desbind. Como o 3.60 deixou a engine em `PROCESSING_IB`, **é estritamente necessário reiniciar a máquina** (`shutdown /r /t 0`) antes de ligar o 3.61 para garantir hardware limpo.

### Sequência Pós-Reboot:
1. Confirmar `LastBootUpTime` posterior a `31/08/2026 09:14:00` e BC-250 em `BasicDisplay / Código 0`.
2. Executar quarentena de resíduo:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine
   ```
3. Executar binding com Watchdog (180s):
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission -WatchdogTimeoutSeconds 180
   ```
4. Confirmar sinal visual e desarmar watchdog:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1 -EvidenceDir <evidence_dir> -InstanceId <instance_id> -Token <token> -IConfirmDisplayIsVisible
   ```
5. Executar teste do tripé P2:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\test-p2-multi-submission.ps1
   ```
