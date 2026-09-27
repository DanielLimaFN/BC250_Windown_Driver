# Handoff 194 — B7: rota de submissão Node 1 da UMD, offline

**Data:** 08/09/2026  
**Escopo:** somente código, testes e auditoria offline  
**Corridas físicas gastas nesta sessão:** **0**  
**Veredito físico:** **NÃO VALIDADO EM HARDWARE**

## Resultado

A UMD agora possui uma rota explícita e fail-closed para montar, copiar e submeter um
command buffer de prova pelo **Node 1**. O teste offline nomeado
`umd_node1_submit_contract_test` fecha esta cadeia simulada por callbacks:

```text
CreatePagingQueue(Node 1)
  -> CreateContextVirtual(Node 1)
  -> Allocate + MapGpuVirtualAddress + MakeResident
  -> WRITE_DATA de cinco canários + RELEASE_MEM de fence
  -> Lock/Unlock copia 17 DWORDs para a alocação WDDM
  -> SubmitCommand pelo contexto Node 1
  -> Evict/Free/DestroyContext/DestroyPagingQueue
```

O teste confirma o conteúdo dos 17 DWORDs e o contexto usado. Ele **não** confirma que o
MEC leu o IB, que a fence foi escrita pela GPU ou que os canários chegaram ao destino.

## Auditoria de sinceridade que motivou a mudança

A leitura da implementação anterior encontrou quatro lacunas objetivas:

1. `Draw*` apenas incrementava contadores CPU-side e aparentava progresso sem backend.
2. `Bc250UmdCommandBufferFlush` ignorava `NodeOrdinal`.
3. o command buffer tentava alocar com token zero, rejeitado pelo próprio winsys.
4. o PM4 ficava num `calloc` privado da CPU e nunca era copiado para a alocação WDDM.

Agora `Draw*` rejeita com `E_NOTIMPL`, conta a rejeição separadamente e não incrementa
contadores de draw. O gate `BC250_D3D10_11_NATIVE_READY` permanece em **0**.

## Mudanças relevantes

- `Bc250WinsysSubmitGpuVaForNode` confere o `NodeOrdinal` real do contexto antes do submit.
- `Bc250WinsysUploadAllocation` exige `Lock/Unlock` válidos e copia os bytes reais.
- o command buffer usa token não nulo, GPUVA executável e residência antes do submit.
- `Bc250UmdBuildNode1ProofCommand` monta `WRITE_DATA` com cinco valores não nulos e
  `RELEASE_MEM` para uma fence previamente envenenada pelo ensaio físico futuro.
- `Bc250UmdStartNode1Submission` cria paging queue, contexto virtual Node 1 e command buffer,
  com rollback em toda falha.
- o build da UMD passou a executar o harness Node 1 e a auditoria semântica com mutação.
- o batch deixou de engolir falhas de `assert` que retornam NTSTATUS negativo; cada teste
  agora é conferido por `ERRORLEVEL != 0`.

## Testes executados

| Teste | Resultado | O que prova |
|---|---:|---|
| `driver\\umd\\build-umd.bat` | PASS | Compilação, contratos UMD/winsys e harness Node 1 offline |
| `umd_node1_submit_contract_test` | PASS | 17 DWORDs copiados e encaminhados ao contexto Node 1 por callbacks |
| `tools/audit-umd-node1-submission.py` | PASS | Gate fechado, Draw fail-closed, upload real exigido e routing por nó |
| mutação: remover chamada de upload | DETECTADA | A auditoria não aceita submit de buffer CPU destacado |
| `driver\\wdk\\build-contract.bat` | PASS | Suíte integral de contratos offline |
| `tools/audit-bc250mcdm-safety.py` | PASS | Auditoria estrutural de segurança |
| `tools/audit-mcdm-ddi-matrix.py` | PASS | Matriz estrutural de DDIs |
| `tools/audit-ddi-noop-callbacks.py` | PASS | Nenhum novo falso sucesso aceito |

Compilar e passar esses testes **não** prova aceleração. **NÃO VALIDADO EM HARDWARE**.

## Teste físico que fecha esta entrega

**Nome:** `B7_UMD_NODE1_TRIPOD`  
**Arquivo de evidência:** `docs/evidence/<data>_B7_UMD_NODE1_TRIPOD/`

O teste só passa com as três pernas no mesmo submit originado pela UMD:

1. `RPTR == WPTR` usando a redução canônica de wrap;
2. fence física escrita pela GPU sobre poison;
3. os cinco canários do destino substituídos e conferidos byte a byte.

Também deve registrar NodeOrdinal, contexto, command-buffer GPUVA, destination GPUVA,
fence GPUVA, valores pré/pós, snapshots do journal GPUVA e contadores
Submitted/Completed. Qualquer ausência resulta em falha ou **INCONCLUSIVO**, nunca PASS.

## Limite de corrida e critério de abandono

Nenhuma corrida deve ser armada apenas com este código: ainda não existe o harness físico
que aloca/envenena/lê o destino e arquiva todas as três pernas. Depois desse harness e de
seus preflights passarem offline, o teto proposto é **uma corrida física**. Abandonar o
ensaio no primeiro preflight inválido, ausência de GPUVA resolvida, falha de residência ou
falta de espaço de evidência; não substituir a prova por telemetria anunciada pela UMD.

## Dívidas que permanecem abertas

- falta o chamador físico transacional e a leitura pós-GPU do destino/fence;
- o contexto implementado aqui é o caminho Node 1; Node 0 separado continua fail-closed;
- recursos D3D ainda usam shadows CPU e não constituem recursos gráficos utilizáveis;
- tokens de recursos comuns ainda precisam de identidade/lifetime por objeto;
- `Draw*` está deliberadamente recusado;
- DXBC -> SPIR-V, ACO/RADV, pipeline gráfico, WSI e DWM visível não existem;
- `BC250_D3D10_11_NATIVE_READY` não pode ser promovido antes de pipeline e draw físicos.

Portanto submissão Node 1 da UMD, pipeline D3D e desktop visível seguem
**NÃO VALIDADOS EM HARDWARE**.
