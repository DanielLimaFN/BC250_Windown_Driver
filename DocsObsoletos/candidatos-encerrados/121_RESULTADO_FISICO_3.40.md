# Resultado Físico do Candidato 3.40

Data local: 30/08/2026  
Ensaiado: **3.40.0.0 / SchemaVersion 111**  
Veredito: **NÃO VALIDADO EM HARDWARE**

Evidência canônica:
`docs/evidence/2026-08-30_14-25-36-DESKTOP-HQAG6T4-lab-binding/`.

---

## 1. Ciclo de Vida Executado

- Boot frio, `BasicDisplay`, Código 0;
- Quarentena de resíduo: `PASS` (`0A7CED56...` isolado em `C:\BC250_DRIVER_QUARANTINE`);
- Uma submissão física (DriverVer `08/30/2026,3.40.0.0`, SchemaVersion 111, `ShaderDispatch=1`);
- Dispositivo entrou em `bc250mcdm`, Código 0, com display confirmado visível pelo operador;
- Watchdog desarmado com token exato;
- Acceptance read-only executado contra a telemetria live do hardware;
- Veredito fail-closed: **NÃO VALIDADO EM HARDWARE**.

---

## 2. Descoberta Causal Crítica: Transições de `CP_CPF_BUSY_STAT`

O candidato 3.40 capturou o registrador `CP_CPF_BUSY_STAT` (`BAR5 0x08220`) em 7 marcos temporais dentro da mesma transação física:

| Marco Temporal da Ativação HQD | Valor Bruto (Hex) | Decodificação de Bits Ativos (`gc_10_1_0_sh_mask.h`) |
|---|---|---|
| **1. Antes de `CP_HQD_ACTIVE = 1`** | `0x20000000` | `HQD_WPTR_POLL_BUSY` (bit 29) |
| **2. Logo após `ACTIVE = 1` (readback)** | `0x60C60000` | `HQD_PQ_BUSY` (bit 30)<br>`HQD_WPTR_POLL_BUSY` (bit 29)<br>`HQD_CONSUMED_RPTR_BUSY` (bit 22)<br>`HQD_PQ_FETCHER_BUSY` (bit 18)<br>`HQD_MESSAGE_BUSY` (bit 17) |
| **3. Antes da observação inicial de WPTR** | `0x68460000` | Adiciona `HQD_ROQ_PQ_BUSY` (bit 27) |
| **4. Depois da publicação do WPTR** | `0x68460000` | Invariante (`0x68460000`) |
| **5. Imediatamente antes do kick BAR2** | `0x68460000` | Invariante (`0x68460000`) |
| **6. Imediatamente após o kick BAR2** | `0x68460000` | Invariante (`0x68460000`) |
| **7. Polling final (após 50.000 amostras)** | `0x68460000` | Invariante (`0x68460000`) |

### Conclusão Causal:
1. **Antes de `ACTIVE = 1`**, o fetcher e a fila **NÃO estavam ocupados** (apenas `WPTR_POLL_BUSY` estava ativo).
2. O acionamento de `CP_HQD_ACTIVE = 1` **disparou instantaneamente** os estados `HQD_PQ_BUSY`, `HQD_CONSUMED_RPTR_BUSY`, `HQD_PQ_FETCHER_BUSY` e `HQD_MESSAGE_BUSY`.
3. A escrita de `CP_PQ_STATUS` (Doorbell Enable pós-Active) ligou `HQD_ROQ_PQ_BUSY`, consolidando o valor em `0x68460000`.
4. A doorbell do BAR2 e a publicação do WPTR não alteraram o estado, provando que o travamento ocorre no **protocolo interno de inicialização do fetcher ativado por `CP_HQD_ACTIVE`**.

---

## 3. Estado do Tripé e Rollback

```text
resultado  0x43DAFFFE (poison intacto)
fence      0xA55A0FF0 (poison intacto)
RPTR/WPTR  0/40 (RPTR não consumiu os 40 dwords)
```

O rollback interno divergiu no estágio 9 (`HQD_PLAN`):
`CP_HQD_PERSISTENT_STATE (0xC834)`: esperado `0x0BE05301`, observado `0x0BEC5301`.

---

## 4. Próxima Direção (Candidato 3.41 / Schema 112)

Agora que o limite causal está exatamente na transição `ACTIVE=0 -> ACTIVE=1` e no estado do PQ Fetcher:
- Investigar os registradores pré-requisitos para o fetcher antes de subir `ACTIVE = 1`:
  - `CP_HQD_DEQUEUE_REQUEST`
  - `CP_HQD_IB_CONTROL`
  - `CP_HQD_PERSISTENT_STATE`
  - Sequência de ativação do upstream Linux `gfx_v10_0_hqd_load` / `amdgpu_ring_init_mqd`.

**NÃO VALIDADO EM HARDWARE.**
