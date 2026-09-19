# Handoff 195 — B7: transação do tripé Node 1 na UMD

**Data:** 08/09/2026  
**Escopo executado:** código e testes offline  
**Instalação/bind/corrida física:** nenhum  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Entrega desta faixa

`Bc250UmdRunNode1ProofTransaction` implementa a transação que deverá produzir as pernas de
dados e fence do primeiro submit Node 1 originado pela UMD:

```text
PREFLIGHT DO CONTEXTO NODE 1
  -> Allocate/MapGpuVA/MakeResident do destino
  -> Upload de cinco poisons distintos
  -> Readback e comparação do poison antes do submit
  -> CreateMonitoredFence com poison
  -> WRITE_DATA(5 valores) + RELEASE_MEM(fence)
  -> SubmitCommand no contexto Node 1
  -> WaitForSynchronizationObjectFromCpu
  -> comparação EXATA da fence
  -> Lock/readback e comparação byte a byte dos cinco valores
  -> DestroyFence + Evict/FreeGpuVA/Deallocate
```

O retorno é `S_OK` somente se todas as etapas, inclusive o teardown, passarem. Um callback
de espera que retorna sucesso sem avançar a fence é recusado. Dados que permanecem no
poison também são recusados.

## Contratos acrescentados

- `Bc250WinsysReadAllocation`: leitura da alocação WDDM real por `Lock/Unlock`; a prova não
  compara dois shadows CPU.
- `Bc250WinsysCreateMonitoredFence`: agora também recusa GPUVA de fence igual a zero.
- `BC250_UMD_NODE1_PROOF_RESULT`: preserva GPUVAs, poison, esperado, observado e cada perna
  do veredito, inclusive cleanup.
- tokens da alocação de prova usam classe própria e sequência não nula por processo.

## Teste nomeado

`umd_node1_tripod_contract_test` executa três cenários por callbacks:

1. fence e cinco dados avançam: PASS offline;
2. submit aceito e espera retorna `S_OK`, mas fence fica no poison: rejeitado;
3. fence avança, mas os dados ficam no poison: rejeitado.

`tools/audit-umd-node1-submission.py` passou a exigir poison pré-submit, fence exata,
readback byte a byte e cleanup. Mutações que removem o upload ou a comparação de dados são
detectadas.

## Verificação executada

| Comando | Resultado |
|---|---:|
| `driver\\umd\\build-umd.bat` | PASS |
| `umd_node1_tripod_contract_test` | PASS offline, três cenários |
| `tools\\audit-umd-node1-submission.py` | PASS, duas mutações detectadas |
| `driver\\wdk\\build-contract.bat` | PASS |
| `tools\\audit-bc250mcdm-safety.py` | PASS |
| `tools\\audit-mcdm-ddi-matrix.py` | PASS estrutural |

## Bloqueador antes de qualquer corrida

A transação ainda não possui um **acionador físico controlado pelo runtime D3D**. O gate
`BC250_D3D10_11_NATIVE_READY` continua em 0 e deve permanecer assim. Não é aceitável ligar
o gate de produção apenas para alcançar este teste.

A próxima faixa deve criar um modo de prova dedicado que:

1. seja default 0 e exija aceite explícito;
2. só autorize o executável do harness, nunca `dwm.exe`;
3. use desafio/token por corrida para impedir disparo acidental por outro processo;
4. execute a transação uma única vez e sempre devolva falha de device D3D, sem anunciar
   pipeline gráfico;
5. grave resultado estruturado e permita correlacionar os contadores KMD antes/depois;
6. seja envolvido pelo watchdog e rollback já existentes.

Sem esse acionador e sem o coletor de RPTR/WPTR, a corrida está **RECUSADA POR PROJETO**.

## Teste físico que continua aberto

**Nome:** `B7_UMD_NODE1_TRIPOD`  
**Evidência:** `docs/evidence/<data>_B7_UMD_NODE1_TRIPOD/`  
**Teto:** uma corrida depois de todos os preflights offline  
**Pass:** RPTR/WPTR drenados + fence física sobre poison + cinco valores conferidos byte a
byte + teardown completo, tudo no mesmo submit.

Até essa evidência existir: **NÃO VALIDADO EM HARDWARE**.
