# Candidato 3.54 — Correção Estrutural do PACKET3_INDIRECT_BUFFER (GFX10)

> **Pacote**: `3.54.0.0` | **Schema Telemetria**: `125` | **Modo**: `MCDM_LAB_BRINGUP (Bring-up Ativo / P1 Persistent Submission)`  
> **Status**: Pronto para ensaio físico no silício  
> **Veredito Atual**: `NÃO VALIDADO EM HARDWARE`

---

## 🎯 1. Diagnóstico do Silício no Candidato 3.53

A observabilidade aprofundada do Candidato 3.53 capturou com precisão cirúrgica o estado físico da GPU:

1. **Validação dos Endereços Base do HQD**:
   - `HQD PQ_BASE`: `0x00000000:0xF400AA00` $\rightarrow$ corresponde exatamente a `0xF400AA0000` (deslocado por 8 bits para alinhamento de 256 bytes).
   - `RPTR_REPORT`: `0x000000F4:0x00A92000` (endereço linear completo).
   - `WPTR_POLL`: `0x000000F4:0x00A92100` (endereço linear completo).
   - `VMID`: `0`.
   - `HQD_ERROR`: `0x00000000` (nenhum fault de barramento).
   - **Conclusão**: O HQD aponta para o backing físico exato da VRAM.

2. **Identificação da Causa-Raiz no Dump do Anel**:
   O dump dos 12 DWORDs reais do anel revelou:
   ```text
   [ 0.. 3]: 0xC0023F00  0x00A96000  0x000000F4  0x0010002D
   ```
   - DWORD 0: `0xC0023F00` (Header: `PACKET3_INDIRECT_BUFFER`, Count 2)
   - DWORD 1: `0x00A96000` (`IB_BASE_LO`)
   - DWORD 2: `0x000000F4` (`IB_BASE_HI`)
   - DWORD 3: `0x0010002D` (`CONTROL`) $\rightarrow$ **ERRO CRÍTICO**:
     - O bit 20 (`0x00100000`) estava ligado.
     - O bit 23 (`INDIRECT_BUFFER_VALID`, `0x00800000`) estava **desligado (0)**!
     - Na arquitetura GFX10 / SOC15, o CP Command Packet Fetcher (CPF) exige o bit 23 (`INDIRECT_BUFFER_VALID`) para considerar o descritor de IB válido. Sem esse bit, o CPF descarta o pacote ou paralisa o fetch, explicando `CPF_BUSY = 0x68420000` e o congelamento em `RPTR=0`.

---

## 🛠️ 2. Modificações Estruturais Implementadas no Candidato 3.54

1. **Correção do Construtor de Pacotes PM4 (`bc250_pm4_packet_builder.c` / `.h`)**:
   - Adicionada constante `BC250_PM4_IB_VALID_BIT` (`1u << 23`).
   - Removido shift incorreto de VMID em DWORD 2 (`IB_BASE_HI`).
   - DWORD 3 corrigido para a especificação GFX10:
     ```c
     Bc250Pm4WriteDword(RingBuffer, RingDwords, CursorDwords,
         (SizeInDwords & BC250_PM4_IB_SIZE_MASK) |
         ((Vmid & 0xFu) << BC250_PM4_IB_VMID_SHIFT) |
         BC250_PM4_IB_VALID_BIT);
     ```
     Para 45 DWORDs e VMID 0: DWORD 3 agora é `0x0080002D`.

2. **Ajuste de Testes e Contratos**:
   - `persistent_ring_contract_test.c`: verificação de `(8u | BC250_PM4_IB_VALID_BIT)`.
   - `submit_command_contract_test.c`: verificação de `(1024u | BC250_PM4_IB_VALID_BIT)`.
   - `bc250_submission_engine.c`: expectativa do anel auditada com `BC250_PM4_IB_VALID_BIT`.

3. **Empacotamento e Assinatura**:
   - `package-bc250mcdm-lab.ps1`: bump para `DriverVer 3.54.0.0` e `SchemaVersion 125`.

---

## 📋 3. Comandos de Ensaio Físico no Hardware

```powershell
# 1. Quarentena de resíduo anterior
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine

# 2. Instalação e Binding do Candidato 3.54
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission

# 3. Confirmação do Device Manager (Código 0)
powershell -ExecutionPolicy Bypass -File tools\confirm-bc250mcdm-lab.ps1

# 4. Execução do Teste de Aceitação P1
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

---

## 🔒 4. Veredito e Cláusula de Hardware

**`NAO VALIDADO EM HARDWARE`**  
O candidato está compilado, assinado e empacotado de forma fail-closed. O veredito definitivo virá exclusivamente da prova física na APU BC-250.
