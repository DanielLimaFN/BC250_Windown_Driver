# Handoff para a proxima IA — estado apos o candidato 3.42

Data local: 30/08/2026
Ultimo pacote ensaiado: **3.42.0.0 / SchemaVersion 113**
Veredito: **NAO VALIDADO EM HARDWARE** (MEC Ring Consumption 100% Provado)

---

## 1. Conquista Critica Provada em Silicio no 3.42

1. **Drenagem Completa do Anel**:
   - `RPTR == WPTR == 40` (todos os 40 dwords consumidos pelo silicio).
   - `RPTR Report Memory = 40` (hardware escreveu writeback em memoria com sucesso).
   - `PQ_EMPTY = 1` (`CP_HQD_PQ_CONTROL = 0xD030890D`).
   - `QUEUE_IDLE = 1` (`CP_HQD_HQ_STATUS0 = 0x40008040`).
2. **Execucao e Decodificacao de Todos os 4 Pacotes PM4**:
   - MEC FIFO Header Dump comprovou a retirada sequencial de:
     - `0xC0000E00` (`PACKET3_PREAMBLE_CNTL`)
     - `0xC0064900` (`PACKET3_SET_SH_REG`)
     - `0xC0004600` (`PACKET3_DISPATCH_DIRECT`)
     - `0xC0031500` (`PACKET3_RELEASE_MEM`)

---

## 2. Ajustes Necessarios para o Candidato 3.43 (Schema 114)

1. **Mascara de Rollback para `CP_HQD_QUANTUM` (`0x0000C840`)**:
   - O hardware ativa o bit 31 (`QUANTUM_ACTIVE = 0x80000000`) quando o timer do quantum de fila esta ativo.
   - Em `Bc250HqdWritableReadbackMask`, adicionar para `BC250_HQD_MMIO_QUANTUM`:
     `return 0x7FFFFFFFu;` (mascarando o bit 31).
2. **Habilitacao de Tabela de Paginas VMID 0 (`Vmid0PageTable=1`) ou `WRITE_DATA` Direto**:
   - O pipeline de shaders (SQ / CUs) precisa de traducao UTCL1 para `COMPUTE_PGM_LO/HI` e `global_store_dword`.
   - Alternativamente, testar pacote com `WRITE_DATA` direto na memoria (DST_SEL=5) e validar a gravacao de dados e fence na VRAM fisica.
