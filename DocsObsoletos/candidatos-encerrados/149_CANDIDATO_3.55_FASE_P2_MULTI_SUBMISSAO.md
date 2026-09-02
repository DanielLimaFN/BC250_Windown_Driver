# Candidato 3.55 — Fase P2: Multi-Submissão Sequencial e Streams Contínuos

> **Pacote**: `3.55.0.0` | **Schema Telemetria**: `126` | **Modo**: `MCDM_LAB_BRINGUP (Fase P2 Multi-Submissão)`  
> **Status**: Pronto para ensaio físico no silício  
> **Veredito Atual**: `NÃO VALIDADO EM HARDWARE`

---

## 🎯 1. Objetivos da Fase P2 no Silício

Com a submissão persistente unitária (Fase P1) 100% comprovada no silício (Candidato 3.54), a **Fase P2** exercita o comportamento do driver sob rajadas contínuas de trabalho:

1. **Multi-Submissão Sequencial de $N = 16$ Dispatches**:
   - Submissão consecutiva de 16 pacotes pelo anel MEC2 ativo sem interrupção nem reativação da fila.
   - Atribuição de fences estritamente monotônicas ($F_1 = 1, \dots, F_{16} = 16$).
2. **Prova de Integridade em 16 Slots de Dados**:
   - 16 saídas de dados em VRAM (`+0x5000 + k*4`) e 16 fences de dispatch (`+0x2300 + k*4`) isoladas.
   - Cada slot é previamente envenenado (`0x43DA0000 | (k+1)`) e conferido após o término das 24 CUs da GPU.
3. **Aposentadoria Dinâmica e Sincronização**:
   - O loop `Bc250SubmissionEnginePoll` drena as fences físicas gravadas pelo pacote `RELEASE_MEM` e aposenta todas as 16 submissões ($16/16$).

---

## 🛠️ 2. Arquitetura e Modificações Realizadas

1. **Engine de Submissão (`bc250_submission_engine.c` / `.h`)**:
   - Adicionada estrutura `BC250_SUBMISSION_P2_STREAM_EVIDENCE` e implementada a função `Bc250SubmissionEngineP2StreamTest`.
   - Parametrização dinâmica de cada Indirect Buffer em `+0x6000 + k*256` para direcionar a execução das waves para o slot $k$.
2. **Telemetria de Kernel (Schema 126)**:
   - Registrados 12 novos campos específicos da Fase P2 em `HKLM:\...\Parameters\Probe`:
     - `P2StreamAttempted`, `P2BatchCount`, `P2SubmissionsAccepted`, `P2SubmissionsRetired`, `P2WrapCount`, `P2ResultMismatches`, `P2LastFenceObserved`, `P2FinalWptr`, `P2FinalRptrMmio`, `P2FinalRptrReport`, `P2TriadPassed`, `P2PhysicalReady`.
3. **Novo Script de Aceitação**:
   - [`tools/test-p2-multi-submission.ps1`](tools/test-p2-multi-submission.ps1): Confere os 16 resultados no silício e emite o veredito oficial da Fase P2.

---

## 🧪 3. Sequência de Ensaio Físico no Hardware

Execute no PowerShell elevado da máquina BC-250:

```powershell
# 1. Quarentena preventiva do binário anterior
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine

# 2. Instalação e Binding do Candidato 3.55 (Fase P2)
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission

# 3. Confirmação do Display (Código 0)
powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1

# 4. Execução do Teste de Aceitação da Fase P2
powershell -ExecutionPolicy Bypass -File tools\test-p2-multi-submission.ps1
```
