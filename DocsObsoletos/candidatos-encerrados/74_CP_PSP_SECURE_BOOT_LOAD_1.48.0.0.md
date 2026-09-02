# Relatório Técnico — Candidato 1.48.0.0: Carga do Command Processor via PSP Ring (Secure Boot)

## 1. Contexto e Diagnóstico

No Candidato 1.47.0.0, a quadra completa de microcódigos (PFP, ME, CE e MEC) foi preparada e mapeada com sucesso em VRAM física sob broadcast `GRBM_GFX_INDEX = 0xE0000000`, mas todas as microengines permaneceram paradas em `0x0000041E`.

A análise física comparativa com o SDMA (que roda perfeitamente com 15 transições de PC e fence física gravada) revelou que a APU BC-250 opera sob **Secure Boot de hardware**:
- O microcódigo SDMA é autenticado e carregado pelo processador de segurança (PSP) via comando `LOAD_IP_FW` (`type = 9`).
- O Command Processor do chip Cyan Skillfish 2 possui Boot ROM interna que aguarda a validação criptográfica do PSP antes de liberar a execução do microcódigo do CP. Em cargas diretas (sem PSP), a Boot ROM entra em espera perpétua no vetor `0x0000041E`.

---

## 2. Modificações Implementadas no Candidato 1.48.0.0

1. **Expansão da Transação PSP `LOAD_IP_FW`**:
   - `bc250_psp_load_fw_transaction.h` e `bc250_psp_load_fw_transaction.c` agora suportam os tipos de firmware do CP:
     - `BC250_PSP_LOAD_FW_CP_ME_TYPE = 1` (263.168 bytes)
     - `BC250_PSP_LOAD_FW_CP_PFP_TYPE = 2` (263.168 bytes)
     - `BC250_PSP_LOAD_FW_CP_CE_TYPE = 3` (263.040 bytes)
     - `BC250_PSP_LOAD_FW_CP_MEC_TYPE = 4` (267.440 bytes)
     - `BC250_PSP_LOAD_FW_CP_MEC_JT1_TYPE = 5` (896 bytes)

2. **Pipeline de Carga do CP pelo PSP no Miniport**:
   - `bc250_pfp_direct_load.c` aloca buffers de ring, comando, fence e payload contíguos em host memory;
   - Submete os 5 componentes do CP individualmente ao PSP através do comando `LOAD_IP_FW` no ring do PSP;
   - Registra a contagem de sucessos do PSP em `Evidence->FaultAddressLow`.

3. **Coordenação com o Pipeline de Execução**:
   - Mantém as jump tables e registradores canônicos sob broadcast `0xE0000000`;
   - Executa unhalt sincronizado (`CP_ME_CNTL = 0`, `CP_MEC_CNTL = 0`);
   - Submete o pacote PM4 (17 dwords) no Ring 0 e monitora avanço de RPTR, fence e PCs.

4. **Avanço de Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.48.0.0`
   - `SchemaVersion`: `54`

---

## 3. Conformidade Fail-Closed

- Testes de compilação e unitários: 100% PASS.
- Auditoria Authenticode e INF: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
