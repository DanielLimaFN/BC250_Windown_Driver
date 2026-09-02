# Relatório Técnico — Candidato 1.49.0.0: Carga do CP no Ring PSP Persistente

## 1. Descoberta e Diagnóstico no Silício do Candidato 1.48.0.0

No ensaio do Candidato 1.48.0.0, a telemetria comprovou um resultado monumental:
- `PfpDirectFaultAddrLow = 1`: O microcódigo `ME` (`cyan_skillfish2_me.bin`, tipo 1) foi **autenticado com sucesso** pelo processador de segurança (PSP) com `ResponseStatus = 0`!
- No entanto, a rotina destruía o anel do PSP (`BC250_PSP_RING_DESTROY`) imediatamente após o primeiro firmware, impedindo que PFP, CE, MEC e MEC_JT1 fossem submetidos.

---

## 2. Implementação do Candidato 1.49.0.0

1. **Ring PSP Persistente Multi-Payload**:
   - Cria o anel PSP KM uma única vez (`BC250_PSP_RING_CREATE_KM`);
   - Submete os 5 componentes sequencialmente na mesma fila ativa:
     1. `ME` (tipo 1, 263.168 bytes)
     2. `PFP` (tipo 2, 263.168 bytes)
     3. `CE` (tipo 3, 263.040 bytes)
     4. `MEC` (tipo 4, 267.440 bytes)
     5. `MEC_JT1` (tipo 5, 896 bytes)
   - Destrói o anel PSP com segurança somente após todos os 5 serem autenticados.
2. **Registro e Verificação**:
   - Acompanha o avanço de fence (`1..5`) e o `ResponseStatus` de cada payload.
3. **Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.49.0.0`
   - `SchemaVersion`: `55`

---

## 3. Conformidade Fail-Closed

- Testes de integridade: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
