# Relatório Técnico — Candidato 1.51.0.0: Endereçamento Físico VRAM MC para o Ring 0 do CP

## 1. Diagnóstico e Descoberta Fundamental

A análise da transição do PC do Command Processor revelou que:
- O microcódigo PFP/ME/CE **já sai de 0 e salta para a instrução `1054` (`0x0000041E`)**, que é o loop de espera do Fetcher de Anel (CPF).
- No entanto, o registrador `CP_RB0_BASE` estava sendo configurado com o offset virtual `0x00105000 >> 8 = 0x00001050` em vez do endereço físico real de VRAM (**MC Address**).
- Como o hardware do CPF busca o ring no barramento de memória (MC), ele tentava ler o anel em `0x0000000000105000` (espaço não mapeado), nunca alcançando os 17 dwords do Ring 0.

---

## 2. Implementação do Candidato 1.51.0.0

1. **Endereçamento Físico MC Real**:
   - `ringMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RING_OFFSET` (`0xF40FAAE000 + 0x105000 = 0xF40FBB3000`)
   - `rptrWbMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_RPTR_WB_OFFSET`
   - `wptrPollMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_WPTR_POLL_OFFSET`
   - `fenceMc = Evidence->FirmwareMcAddress + BC250_CP_RB0_FENCE_OFFSET`
   - `CP_RB0_BASE` configurado com `(ringMc >> 8)` e `CP_RB0_BASE_HI` com `(ringMc >> 40)`!

2. **Ring 0 Alias e PM4 WRITE_DATA**:
   - `CP_RING0_BASE_LO` e `CP_RING0_BASE_HI` configurados com `ringMc`;
   - `PACKET3_WRITE_DATA` configurado para gravar `0xBC250123` diretamente em `fenceMc` físico na VRAM.

3. **Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.51.0.0`
   - `SchemaVersion`: `57`

---

## 3. Conformidade Fail-Closed

- Testes e auditorias: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
