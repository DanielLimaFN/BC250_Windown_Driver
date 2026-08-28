# Relatório Técnico — Candidato 1.47.0.0: Carga Integrada da Quadra do CP (PFP + ME + CE + MEC)

## 1. Contexto e Motivação

No Candidato 1.46.0.0, a sincronização de broadcast GRBM e GCVM Context 0 operaram de forma segura e com 100% de integridade de restauração de hardware, mas os ponteiros de instrução das microengines gráficas (PFP, ME, CE) permaneceram em espera no vetor de bootloader `0x0000041E`.

A investigação no código upstream (`gfx_v10_0.c` linhas 7244-7253) e nos dumps do Linux operacional no BC-250 (`linux-cp-info`) evidenciou que a inicialização do Command Processor no silício Cyan Skillfish 2 depende da carga coordenada de toda a quadra de microengines:
1. `gfx_v10_0_cp_gfx_load_microcode` (PFP, CE, ME)
2. `gfx_v10_0_cp_compute_load_microcode` (MEC1, com `cyan_skillfish2_mec.bin`)

Como a APU BC-250 é um chip de computação desacoplado de vídeo, a falta da MEC impedia o CP de concluir sua fase de pré-inicialização.

---

## 2. Modificações Implementadas no Candidato 1.47.0.0

1. **Inclusão da MEC no Pacote de Produção**:
   - Firmware: `cyan_skillfish2_mec.bin` (268.592 bytes no disco, 268.336 bytes de payload, 224 dwords de Jump Table no offset 66.860 dwords, versão 0x90).
   - Adicionado a `[BC250Mcdm.CopyFiles]` no INF e nos scripts de build e auditoria.

2. **Alocação Contígua de VRAM Expandida (1,1 MiB)**:
   - Slice 0: PFP (260 KiB)
   - Slice 1: ME (260 KiB)
   - Slice 2: CE (260 KiB)
   - Slice 3: MEC (264 KiB)
   - Slice 4: Ring 0 Buffer (4 KiB)
   - Slice 5: RPTR Writeback / WPTR Polling / VRAM Fence (1 KiB)
   - Slice 6: Page Table VMID 0 (4 KiB, 512 PTEs mapeando 2 MiB)

3. **Sequência Canônica da Quadra sob Broadcast `GRBM_GFX_INDEX = 0xE0000000`**:
   - Pass 1: Invalidação e programação de `PFP_IC_*` + JT de PFP (128 dwords);
   - Pass 2: Invalidação e programação de `CP_CE_IC_*` + JT de CE (96 dwords);
   - Pass 3: Invalidação e programação de `CP_ME_IC_*` + JT de ME (128 dwords);
   - Pass 4: Invalidação e programação de `CP_CPC_IC_*` + JT de MEC (224 dwords em `mmCP_MEC_ME1_UCODE_*`);

4. **Alinhamento de Registradores Canônicos**:
   - `GRBM_GFX_CNTL` corrigido para o offset real `0x00008088` (Pipe 0);
   - `CP_MAX_CONTEXT` configurado para `0x00000007` (conforme medido no Linux);
   - Amostragem de `SCRATCH_REG0` direcionada para `0x00030100` (Segmento 1, dword `0x2040`);
   - Amostragem de `CP_MEC1_PC` (`0x000086A0`) incluída na telemetria de diagnóstico.

5. **Avanço de Versão e Schema**:
   - DriverVer: `08/27/2026,1.47.0.0`
   - SchemaVersion: `53`

---

## 3. Conformidade Fail-Closed

- Testes offline: 36/36 DDIs + 5/5 GPUVA + 1/1 compatibilidade = PASS.
- Auditoria de pacote Authenticode: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
