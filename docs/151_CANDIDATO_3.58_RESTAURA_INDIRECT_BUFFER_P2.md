# Candidato 3.58 — Restauração do descritor INDIRECT_BUFFER na P2

**Data:** 31/08/2026  
**Pacote:** `3.58.0.0`  
**Schema de telemetria:** `127`  
**Veredito atual:** **NÃO VALIDADO EM HARDWARE**

## Evidência física do 3.57

O kick único de 64 bits em `BAR2+0x110` mudou o comportamento do silício:

- `DOORBELL_CONTROL` antes/depois: `0x40000110`;
- falhas de escrita de doorbell: `0`;
- WPTR publicado: `53` no primeiro lote e `901` ao final;
- RPTR MMIO: avançou de `0` para exatamente `45`;
- RPTR writeback: permaneceu `0`;
- dispatch fence e resultado: permaneceram envenenados;
- `CP_STALLED_STAT1 = 0x00000C00`;
- tripé P2: reprovado.

Evidência:
`docs/evidence/2026-08-31_07-49-03-DESKTOP-HQAG6T4-p2-multi-submission/`.

## Causa raiz no software

O self-test e o stream P2 montavam corretamente cada IB de 45 DWORDs numa
região própria de VRAM e calculavam seu endereço físico. Apesar disso, ambos
chamavam `Bc250SubmissionEngineSubmitDirect`, copiando os 45 DWORDs do IB para
o ring primário e descartando o endereço staged.

O avanço físico para `RPTR=45` coincide exatamente com esse tamanho e isola a
fronteira defeituosa. A P1 validada no candidato 3.54 usou o descritor
`PACKET3_INDIRECT_BUFFER` de quatro DWORDs, com `INDIRECT_BUFFER_VALID`, seguido
pelo `RELEASE_MEM` de oito DWORDs.

## Correção do 3.58

Self-test e P2 passam a chamar `Bc250SubmissionEngineSubmit` com:

- endereço físico do IB staged;
- tamanho real produzido pelo contrato de dispatch;
- VMID e bit `INDIRECT_BUFFER_VALID` emitidos pelo builder já provado;
- fence sequencial do ring mantida no epílogo.

O caminho `SubmitDirect` continua disponível para consumidores que realmente
forneçam PM4 de ring primário; ele deixa de ser usado indevidamente pelos
testes que constroem IBs.

O aceite P2 também passa a exigir os dois cursores:
`RPTR report == RPTR MMIO == WPTR`.

Compilação ou assinatura não provam a correção. Até novo boot frio fechar o
tripé de 16 lotes, o estado permanece **NÃO VALIDADO EM HARDWARE**.
