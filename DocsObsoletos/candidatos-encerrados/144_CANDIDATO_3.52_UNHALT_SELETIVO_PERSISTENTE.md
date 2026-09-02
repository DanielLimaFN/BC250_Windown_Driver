# Candidato 3.52 — unhalt seletivo na fila persistente

**Data:** 30/08/2026  
**Pacote:** `3.52.0.0 / SchemaVersion 123`  
**SYS assinado:** `94357936D791648E83C1EE6546D1B97AE731C45693B610065AA9126ED806941A`  
**Estado:** pronto offline. **NÃO VALIDADO EM HARDWARE.**

O ramo especial que escrevia zero em `CP_MEC_CNTL` e `CP_ME_CNTL` durante a
ativação persistente foi removido. Todos os modos usam agora a mesma receita
fisicamente provada pelo one-shot:

```text
CP_MEC_CNTL = snapshot & ~MEC_ME2_HALT
CP_ME_CNTL  = snapshot, somente leitura de confirmação
```

Com o baseline observado, o estado esperado durante toda a fila é:

```text
CP_MEC_CNTL = 0x40000000  (MEC2 livre; MEC1 haltado)
CP_ME_CNTL  = 0x15000000  (PFP, ME e CE preservados/haltados)
```

O teardown restaura `CP_MEC_CNTL=0x50000000`; `CP_ME_CNTL` não é escrito porque
nunca foi alterado. Um teste contratual novo cobre ativação, readbacks e
rollback persistentes. O gate físico exige esses valores após ativação, submit
e re-arme, além do tripé habitual.

Build integral, testes, assinatura, Inf2Cat, auditoria do pacote e auditoria de
segurança passaram.

**NÃO VALIDADO EM HARDWARE.**
