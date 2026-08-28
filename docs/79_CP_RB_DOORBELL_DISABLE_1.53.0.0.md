# Relatório Técnico — Candidato 1.53.0.0: Desativação Explícita de Doorbell e rbCntl Canônico GFX10

## 1. Análise Comparativa com Upstream Linux (`gfx_v10_0.c`)

Ao auditar a inicialização do Ring 0 no Linux (`gfx_v10_0_cp_gfx_resume`):
1. **Desativação de Doorbell**: Quando o ring opera em modo MMIO (`ring->use_doorbell == false`), o registrador `CP_RB_DOORBELL_CONTROL` (`0x0000C3B4`) é **explicitamente zerado (`DOORBELL_EN = 0`)**. Se esse registrador contiver valores residuais de boot, o hardware ignora escritas MMIO em `CP_RB0_WPTR` esperando escritas na abertura de Doorbell PCI.
2. **Máscara rbCntl Canônica**: No Linux, `CP_RB0_CNTL` é configurado apenas com `RB_BUFSZ = 9` e `RB_BLKSZ = 7` (`0x00000709`). Os bits 28 (`RB_EXE`) e 30 não devem ser ativados manualmente na inicialização.

---

## 2. Implementação do Candidato 1.53.0.0

1. **`CP_RB_DOORBELL_CONTROL = 0`**: Desativa explicitamente o envio por Doorbell para o Ring 0, habilitando o avanço via escrita MMIO em `CP_RB0_WPTR`.
2. **`CP_RB0_CNTL = 0x00000709`**: Configuração canônica pura GFX10.
3. **Versão e Schema**:
   - `DriverVer`: `08/27/2026,1.53.0.0`
   - `SchemaVersion`: `59`

---

## 3. Conformidade Fail-Closed

- Testes e integridade: PASS.
- Veredito: **NÃO VALIDADO EM HARDWARE**.
