# Candidato 3.49.0.0 (SchemaVersion 120) — estado global da fila persistente

**Data**: 30/08/2026  
**Estado**: observacional. **NÃO VALIDADO EM HARDWARE.**

Pacote construído e auditado: SHA-256 do SYS
`360266FBE28A6E89C62579806B71A5A0E742A1180B87217738A52803C02B288D`.

Sem nova escrita, cada fotografia já existente da fila passa a incluir:

```text
CP_MEC_CNTL
CP_ME_CNTL
RLC_CP_SCHEDULERS
CP_PQ_WPTR_POLL_CNTL
CP_PQ_WPTR_POLL_CNTL1
```

Os registros são capturados após ativação, após submissão e após re-arme. Todos
os offsets já estão catalogados e usados pelo contrato. O objetivo é separar:

- MEC/ME haltado depois que o one-shot restaurou estado;
- scheduler0 perdido durante a vida persistente;
- queue mask ausente em `CNTL1`;
- polling global indisponível com `CNTL=0` apesar do kick.

O 3.48 permanece intacto: publicação transacional do scheduler0 e coerência
HDP/GL2/GCVM continuam ativas. **NÃO VALIDADO EM HARDWARE.**
