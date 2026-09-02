# Resultado físico 3.51 — `QUEUE_MASK` não destrava a fila

**Data:** 30/08/2026  
**Pacote:** `3.51.0.0 / SchemaVersion 122`  
**SYS:** `348F60DBA47E7D8EB77D816173CCB10E2669E6BE93D7B7C35462337D7B9113F2`  
**Veredito:** **P1 REPROVADO — NÃO VALIDADO EM HARDWARE.**

O pacote integral foi confirmado no silício:

```text
QUEUE_MASK applied/readback/passed = 1 / 1 / 1
ACTIVE off/on                     = 0 / 1
WPTR / RPTR                       = 12 / 0
fence dispatch                    = 0xA55A0FF0 (poison)
resultado                         = 0x43DAFFFE (poison)
fence do anel                     = 0
```

Assim, `CNTL1` mais uma nova borda de `ACTIVE` não bastam para fazer a fila
persistente consumir. Essa hipótese está encerrada e não deve ser repetida.

O cruzamento com o one-shot fisicamente aprovado revelou a próxima diferença:

```text
                         one-shot aprovado   persistente 3.51
CP_MEC_CNTL              0x40000000          0x00000000
CP_ME_CNTL               preservado          0x00000000
```

No baseline limpo, `CP_MEC_CNTL=0x50000000` e `CP_ME_CNTL=0x15000000`. O
one-shot limpa somente `MEC_ME2_HALT`, mantendo MEC1, PFP, ME e CE haltados. O
ramo persistente zerava os dois registradores inteiros, liberando cinco
microengines sem necessidade e criando estado global diferente antes do fetch.

Evidência:
`docs/evidence/2026-08-30_21-55-33-DESKTOP-HQAG6T4-p1-submission/`.

**NÃO VALIDADO EM HARDWARE.**
