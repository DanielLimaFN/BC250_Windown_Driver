# Resultado físico do 3.48 — scheduler e coerência passaram, fetch não

**Data**: 30/08/2026  
**Pacote**: 3.48.0.0 / SchemaVersion 119  
**SYS**: `317C8F9523E2FA5A28CE1C745A80652E30FF03C69EA38F2F34AB32F1B93E7CC8`  
**Veredito**: **NÃO VALIDADO EM HARDWARE**.

O scheduler deixou de bloquear o caminho e o one-shot voltou a fechar:

```text
HqdActiveStatus                         = 0
HqdActiveRlcCpSchedulersReadback        = 0x585048C0
HqdActiveRlcCpSchedulersRestorePassed   = 1
ShaderPhysicalReady                     = 1
```

A fila persistente ativou, submeteu e fechou a nova coerência:

```text
P1ActivateStatus          = 0
P1EngineState             = 1
P1CoherHdpApplied         = 1
P1CoherInvalidateAck      = 1
P1CoherInvalidatePolls    = 1
P1CoherencyPassed         = 1
P1SubmitAccepted          = 1
WPTR shadow/MMIO          = 12/12
```

Mesmo assim, `RPTR` permaneceu zero, fences e resultado ficaram nos poisons e
o re-arme simples não destravou. A hipótese "faltava somente invalidação de
cache" foi refutada como causa suficiente.

Próximo discriminador: fotografar `CP_MEC_CNTL`, `CP_ME_CNTL`,
`RLC_CP_SCHEDULERS`, `CP_PQ_WPTR_POLL_CNTL` e `CNTL1` depois da ativação,
submissão e re-arme persistentes.

Evidências:

- `docs/evidence/2026-08-30_21-29-10-DESKTOP-HQAG6T4-lab-binding`;
- `docs/evidence/2026-08-30_21-29-38-DESKTOP-HQAG6T4-p1-submission`.

