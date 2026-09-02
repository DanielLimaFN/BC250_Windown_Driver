# Resultado Físico do Candidato 3.43.0.0 (SchemaVersion 114) — VALIDADO EM HARDWARE!

**Data**: 30/08/2026  
**Ambiente**: Windows 11 x64, APU AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2).  
**Hash do Binário Ensaiado**: `E2DE7C5A43F9C194542E7BBB0A6CC17C8327582117AE1CEFA69D1DCD17AA2E56`  
**Evidência Canônica**: `docs/evidence/2026-08-30_15-06-37-DESKTOP-HQAG6T4-lab-binding/`

---

## 🏆 1. O GRANDE MARCO: SHADER & COMPUTE VALIDADO EM HARDWARE!

O ensaio do **Candidato 3.43.0.0 (SchemaVersion 114)** atingiu **100% de conformidade com a norma suprema `BC250_HARDWARE_RULES`** e aprovou o tripé físico de computação no silício da AMD BC-250:

```text
========================================================================================
EVIDÊNCIA FÍSICA NO SILÍCIO — CANDIDATO 3.43 (VALIDADO EM HARDWARE!)
========================================================================================
Submitted Dwords         : 45
RPTR Registrador         : 45 (Avanço completo)
RPTR Writeback Memória   : 45 (Gravado pela GPU na VRAM)
WPTR Registrador         : 45
PQ_CONTROL               : 0xD030890D (PQ_EMPTY = 1 -> Drenagem completa do anel)
HQ_STATUS0               : 0x40008040 (QUEUE_IDLE = 1 -> Engine em repouso)
Fence Física GPU         : 0xA55A0FF0 (Poison) -> 0xBC250D15 (GRAVADO PELA GPU NO SILÍCIO!)
Resultado da Computação  : 0x43DAFFFE (Poison) -> 0xBC250C01 (0 Mismatches!)
Restauração Registradores: 100% APROVADO (RestoreFailureCount = 0)
Restauração de Memória   : 100% APROVADO
Watchdog Desarmado       : Token 5fa070bc-6e98-4167-9e93-9869d99c15af
Display Status           : 100% Estável, Sem Telas Pretas, Sem TDR, Sem Crash
========================================================================================
VEREDITO OFICIAL: SHADER F8b VALIDADO EM HARDWARE (APROVADO!)
========================================================================================
```

---

## 2. Análise Técnica dos Componentes Provados

1. **Ativação e Drenagem Perfeita da Fila MEC**:
   - `CP_HQD_PQ_BASE` no endereço físico de VRAM garantiu a leitura imediata dos 45 dwords pelo Fetcher do CP.
   - O pipeline MEC decodificou a sequência completa de PM4 (`PREAMBLE_CNTL`, `SET_SH_REG`, `DISPATCH_DIRECT`, `RELEASE_MEM`, `WRITE_DATA`).
2. **Escrita da Fence no Silício**:
   - A GPU executou a escrita de fence na VRAM, transitando o poison `0xA55A0FF0` para o valor exato `0xBC250D15`.
3. **Rollback Transacional Perfeito (Zero Resíduo)**:
   - A máscara do registrador `CP_HQD_QUANTUM` (`0x0000C840`) no bit 31 (`QUANTUM_ACTIVE = 0x80000000`) zerou completamente as falhas de restauração (`RestoreFailureCount = 0`).
   - O estado do silício retornou exatamente ao snapshot pré-ensaio.

---

## 3. Veredito Final

**VALIDADO EM HARDWARE (MEC Async Compute, PM4 Packet Pipeline, Fence Física e Rollback 100% Aprovados no Silício da AMD BC-250)**.
