# Compute — Gate GPUVA + SDMA em Dois Contextos

## Estado

Foi adicionado ao `bc250compute` o IOCTL
`IOCTL_BC250_GPUVA_SDMA_TWO_CONTEXT` (`0x80C`) e o cliente
`bc250-gpuva-sdma-gate.exe`. A ABI é versionada e descreve duas leases opacas
de alocação, duas GPUVAs distintas, uma fence GPUVA, dados poison e a evidência
de RPTR/WPTR, fence, dados e rollback.

O caminho `DRY_RUN` somente valida a transação. Ele não toca MMIO, não aloca
memória, não faz cópia por CPU e não submete SDMA. O caminho `EXECUTE` retorna
`STATUS_NOT_SUPPORTED` enquanto não existir um backend que seja proprietário
do adaptador MCDM e dos objetos de processo/alocação. Isso evita que um
processo em user mode use GPUVAs, VMIDs ou doorbells arbitrários.

Portanto, este é um gate de ABI e segurança; não é teste de aceleração.
**NÃO VALIDADO EM HARDWARE.**

O contrato offline `bc250_gpuva_sdma_broker_contract` é a ponte planejada para
esse backend. Ele só emite lease a partir de uma `OPEN_ALLOCATION` MCDM do
processo dono, exige root page table previamente registrado e recusa um par
que reutilize processo, allocation ou GPUVA. A lease não é autorização para
user mode: ela deve viajar apenas no canal confiável entre o miniport e o
executor do adaptador.

A ponte interna agora reside no contexto do miniport ativo. Ela inicializa o
broker em `AddDevice`, emite leases sob `ObjectLock` somente para objetos MCDM
reais e revoga leases ao fechar a open allocation ou destruir o processo. Não
há chamada desse caminho em `StartDevice`, nem MMIO, doorbell ou submissão.
Isso ainda não é um backend físico e exige um novo candidato antes de qualquer
teste no dispositivo.

O contrato `bc250_gpuva_sdma_executor_contract` também foi adicionado. Ele
obriga a sequência `Snapshot -> Submit -> Readback -> Rollback`, inclusive
quando o submit falha, e só aceita o tripé lógico quando `RPTR == WPTR`, a
fence observada iguala a esperada e não há mismatch. O resultado desse contrato
não é validação de hardware: somente callbacks de um backend físico poderão
produzir essa evidência.

O executor também não pode reutilizar `Bc250ActiveRunSdmaRingWork`: esse
trabalho de laboratório usa offsets MC fixos e programa diretamente o SDMA.
Ele não representa isolamento GPUVA entre processos e fica explicitamente
rejeitado pelo audit do `bc250compute`.

## Fluxo obrigatório para o backend físico futuro

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILÍCIO] -> [ROLLBACK]
```

1. O miniport/broker proprietário cria dois processos Dxgkrnl e duas
   alocações distintas, emitindo leases opacas pelo canal confiável para o
   executor do adaptador.
2. Mapeia uma GPUVA diferente por processo e conserva a tabela de páginas e
   VMID em snapshot.
3. Inicializa origem com poison A e destino com poison B através de mecanismo
   já provado, sem fazer da cópia por CPU a evidência do teste.
4. Emite SDMA COPY usando exclusivamente GPUVAs autorizadas, publica WPTR e
   coleta RPTR até drenagem ou timeout.
5. Exige fence escrita pela GPU, comparação integral do destino e
   `RPTR == WPTR`; qualquer timeout ou mismatch falha fechado.
6. Desfaz submit, mappings e leases; registra a evidência somente após a
   prova física completa.

Só então `ExecutionAttempted`, `PhysicalFenceObserved` e
`HardwareValidated` podem ser publicados como 1. Nenhum desses campos é
alterado pelo implemento atual.

## Validação de fonte

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test-bc250compute-gpuva-sdma-gate.ps1
```

O build completo continua proibido enquanto o pacote Display 3.24 estiver
ativo, pois ele recria `driver/wdk/package/output_active`, usado pelo rollback
online. A compilação/instalação do serviço compute e qualquer `EXECUTE` exigem
autorização específica e uma janela de recuperação separada do display.

Para validar apenas os contratos novos, sem gerar pacote Display, use:

```cmd
cmd.exe /d /c tools\build-bc250compute-contracts-isolated.cmd
```

Para validar a suíte completa de contratos, drivers e testes de unidade:

```cmd
cmd.exe /c driver\wdk\build-contract.bat
```

## Avanço: Integração e Teste da Ponte de Leases no Miniport Ativo

A ponte interna de leases no miniport ativo (`driver/wdk/bc250_active_miniport.c`)
foi integrada à suíte de testes em `driver/tests/active_miniport_test.c`:

1. **Rejeição sem Page Table**: `Bc250ActiveIssueGpuvaSdmaLease` recusa a emissão
   se o processo não possuir tabela de páginas raiz (`STATUS_DEVICE_NOT_READY`).
2. **Emissão Válida**: Após `DxgkDdiSetRootPageTable`, emite lease opaca com
   identificador único e associado aos objetos reais do MCDM.
3. **Validação de Par Isolado**: `Bc250ActiveValidateGpuvaSdmaLeasePair` exige
   dois processos e duas alocações distintas (`STATUS_SUCCESS`), recusando pares
   que compartilhem o mesmo processo (`STATUS_INVALID_PARAMETER`).
4. **Proteção Anti-Aliasing**: Rejeita alocações ou leases duplicadas na origem e
   destino.
5. **Revogação no Ciclo de Vida**: Leases são destruídas automaticamente quando
   a open allocation é fechada (`DxgkDdiCloseAllocation`) ou o processo é
   eliminado (`DxgkDdiDestroyProcess`), retornando `STALE_LEASE` em tentativas
   posteriores.

### Mapeamento com as Diretrizes Mestras de Silício (`Informações.md`)

Qualquer transição futura para o backend físico deve respeitar:
- **Isolamento de Scanout**: Alocações e tabelas de páginas devem residir acima de
  **16 MB da VRAM** (`>= 0x01000000`), protegendo os ~11 MB do framebuffer GOP
  (2560x1080 @ 60Hz).
- **Isolamento do Engine 0**: Operar estritamente em **VMID 8 ou superior**,
  nunca emitindo invalidação em `R_GCVM_INVALIDATE_ENG0_REQ` (`0x00028210u`), o
  que desligaria o controlador de display.
- **COMPUTE_GFXOFF_GUARD**: Desativar o GFXOFF via mailbox SMU (`0x15`) antes de
  interagir com os anéis de hardware e reativar (`0x14`) após drenagem.
- **Flush de Coerência**: HDP (`0x000086D0 = 1`) e L2 (`0x00028208 = 3`) antes
  de atualizar WPTR.

