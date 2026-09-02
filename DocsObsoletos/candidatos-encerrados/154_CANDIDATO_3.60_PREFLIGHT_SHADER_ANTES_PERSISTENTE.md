# Candidato 3.60 — gate físico de shader antes da fila persistente

**Data:** 31/08/2026  
**Pacote:** `3.60.0.0`  
**Schema:** `127`  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Evidência diferencial

O candidato 3.59 restaurou corretamente `INDIRECT_BUFFER_VALID` e o MEC
avançou o `RPTR` de `0` para `4`. Isso prova o consumo do descritor e o salto
para o IB, mas a execução parou dentro do bloco indireto:

- `CP_HQD_IB_CONTROL=0x84300000`;
- header MEC2 em `0xC0037602` (`SET_SH_REG`);
- fences e resultado permaneceram envenenados;
- `P2FinalRptrMmio=4`, `P2FinalWptr=204`.

A comparação com a captura vencedora do 3.54 revelou a diferença causal de
controle de fluxo:

- 3.54: `ShaderPhysicalReady=1`, `ShaderRptrAfter=ShaderWptrAfter=45`, fence e
  resultado físicos válidos antes da prova persistente;
- 3.59: todos os campos `Shader*` ausentes, pois o ramo
  `PersistentSubmission` havia se tornado mutuamente exclusivo com a
  transação one-shot de shader.

## Correção

Quando `PersistentSubmission=1`, o StartDevice agora exige esta sequência:

```text
shader one-shot (tripé + rollback)
    -> ShaderPhysicalReady == 1
    -> SubmissionEngineOnline
    -> self-test P1 físico
    -> P1 PhysicalReady == 1
    -> P2 com 16 submissões
```

Se o gate one-shot ou a P1 falharem, a P2 não é executada. Dessa forma uma
fila travada não recebe outros 16 lotes e a evidência da primeira falha não é
mascarada.

Compilação, assinatura e contratos offline não substituem a nova prova no
silício. Até o próximo boot completo, **NÃO VALIDADO EM HARDWARE**.
