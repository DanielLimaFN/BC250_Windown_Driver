# Relatório Técnico — Candidato 1.50.0.0: Harmonização do Preflight PSP e Pipeline PM4

## 1. Diagnóstico do Ensaio 1.49.0.0

A auditoria pós-ensaio 1.49.0.0 identificou que a verificação de preflight do anel PSP (`bc250_pfp_direct_load.c`) continha uma restrição espúria:
- Exigia que `ringLowBefore == 0`, `ringHighBefore == 0` e `ringSizeBefore == 0`.
- Como a transação do SDMA0 já havia sido executada anteriormente no mesmo boot, os registradores mantinham os valores físicos da transação prévia.
- Isso fazia com que a rotina PSP do CP fosse pulada (`cpPspLoadedCount = 0`).

---

## 2. Implementação do Candidato 1.50.0.0

1. **Preflight Harmonizado do PSP**:
   - Ajustada a verificação para avaliar exclusivamente a prontidão do SecureOS: `(c2pmsg64Before & BC250_PSP_RING_TOS_READY) != 0` e `c2pmsg64Before != 0xFFFFFFFF`.
   - Permite que o anel seja alocado e execute os 5 comandos `LOAD_IP_FW` sequencialmente.

2. **Pipeline PM4 e GFXOFF Guard**:
   - GFXOFF monitorado via RLC antes da transição do CP;
   - Sequência canônica PM4 preservada com marcadores atualizados.

3. **Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.50.0.0`
   - `SchemaVersion`: `56`

---

## 3. Conformidade Fail-Closed

- Testes de integridade: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
