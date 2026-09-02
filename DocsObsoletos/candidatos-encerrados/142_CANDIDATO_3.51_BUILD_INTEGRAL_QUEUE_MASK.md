# Candidato 3.51 — build integral e re-arme de `QUEUE_MASK`

**Data:** 30/08/2026  
**Pacote:** `3.51.0.0 / SchemaVersion 122`  
**SYS assinado:** `348F60DBA47E7D8EB77D816173CCB10E2669E6BE93D7B7C35462337D7B9113F2`  
**Estado:** pronto offline. **NÃO VALIDADO EM HARDWARE.**

## Correção da cadeia de build

O builder de laboratório agora executa obrigatoriamente `build-contract.bat`
antes do link e recusa qualquer objeto que não tenha sido renovado nessa
invocação. Depois do link, procura no próprio SYS:

- `P1RearmQueueMaskApplied`;
- `P1RearmQueueMaskReadback`;
- `P1RearmQueueMaskPassed`.

A auditoria do pacote repete essa prova no SYS já assinado para Schema 122 ou
superior. Isso impede nova mistura silenciosa entre fontes e objetos.

## Experimento físico preservado

O código de hardware é o experimento pretendido no 3.50:

```text
ACTIVE=0 -> confirmar inativo
QUEUE_MASK=1 -> readback obrigatório
ACTIVE=1 somente se o readback passou
RPTR + duas fences + resultado do shader como prova física
```

Não há escrita de RPTR, fence ou resultado pela CPU. Build contratual,
assinatura, Inf2Cat, auditoria de segurança, hashes e presença dos três campos
no binário passaram offline.

**NÃO VALIDADO EM HARDWARE.**
