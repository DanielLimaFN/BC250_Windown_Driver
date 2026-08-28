# Relatório Técnico — Candidato 1.54.0.0: Sequência Canônica de Preâmbulo PM4 e Encerramento de Clear State

## 1. Diagnóstico do Microcódigo GFX10

No microcódigo do CP (PFP/ME), a emissão de `PACKET3_PREAMBLE_BEGIN_CLEAR_STATE` abre um bloco de bufferização interna de preâmbulo.
Se o driver não emitir os pacotes de fechamento:
- `PACKET3_PREAMBLE_END_CLEAR_STATE`
- `PACKET3_CLEAR_STATE`
- `PACKET3_SET_BASE` (CE_PARTITION_BASE)

O microcódigo permanece aguardando o encerramento do preâmbulo e suspende o dispatch de pacotes subsequentes (incluindo `WRITE_DATA`).

---

## 2. Implementação do Candidato 1.54.0.0

1. **Sequência Canônica Completa de Ring 0**:
   - `PREAMBLE_BEGIN_CLEAR_STATE` (dwords 0..1)
   - `CONTEXT_CONTROL` (dwords 2..4)
   - `PREAMBLE_END_CLEAR_STATE` (dwords 5..6)
   - `CLEAR_STATE` (dwords 7..8)
   - `SET_BASE` CE_PARTITION_BASE (dwords 9..12)
   - `NOP` (Marker `0xBC250154`, dwords 13..14)
   - `WRITE_DATA` Scratch `0x30100` (dwords 15..19)
   - `WRITE_DATA` Fence VRAM física (dwords 20..24)
   - `cursor` = 25 dwords
2. **Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.54.0.0`
   - `SchemaVersion`: `60`

---

## 3. Conformidade Fail-Closed

- Testes e integridade: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
