# 124 — Handoff Técnico de IA Após Candidato 3.41.0.0 (SchemaVersion 112)

**Data**: 30 de Agosto de 2026  
**Status**: Transição de Candidato 3.41.0.0 para 3.42.0.0  
**Dispositivo**: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)  
**Veredito Global**: **NÃO VALIDADO EM HARDWARE**

---

## 1. Conquistas do Candidato 3.41
1. **Restauração Limpa dos Registradores**: `RestoreFailureCount = 0`. O mascaramento dos bits voláteis de microcode em `CP_HQD_PERSISTENT_STATE` eliminou os falsos bloqueios de teardown.
2. **Desarme Seguro**: Watchdog desarmado pelo operador com display funcional.
3. **Telemetria de Causa Raiz**:
   - `CP_CPF_BUSY_STAT` transiciona de `0x20000000` (pré-`ACTIVE`) -> `0x60C60000` (pós-`ACTIVE`) -> `0x68460000` (pós-`DOORBELL`).
   - O Fetcher aguarda a interface ROQ / PQ e a autorização de quantum de despacho do MEC.

---

## 2. Próximos Passos para Candidato 3.42.0.0 (Schema 113)
1. **Rebuild Completo de Objetos**: Executar `cmd /c driver\wdk\build-contract.bat` para garantir alinhamento perfeito de todos os `.obj` com a nova tabela de enum do probe.
2. **Configuração de Quantum e VMID do HQD**:
   - Upstream Linux GC 10.1 (`gfx_v10_0_hqd_load`):
     - `CP_HQD_QUANTUM`: configurar com `QUANTUM_EN = 1` e valor padrão `0x00000001` / `0x0000000A`.
     - `CP_HQD_VMID`: assegurar `VMID = 0` explícito com autorização no pipe do MEC.
     - `CP_HQD_QUEUE_PRIORITY`: configurar prioridade de compute (`0x0000000F`).
