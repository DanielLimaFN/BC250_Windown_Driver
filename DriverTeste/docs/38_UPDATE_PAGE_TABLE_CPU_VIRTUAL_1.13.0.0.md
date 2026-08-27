# UpdatePageTable CPU-virtual - pacote 1.13.0.0

Data: 2026-08-26.

Status: **ENSAIO FISICO BLOQUEADO; PACOTE SUPERADO**.

```text
Conformidade BC250_HARDWARE_RULES   70%
Miniport MCDM funcional            42%
Memoria/GPUVA WDDM                 15%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Implementacao

`DxgkDdiBuildPagingBuffer` agora reconhece exclusivamente
`DXGK_OPERATION_UPDATE_PAGE_TABLE` no modo
`DXGK_PAGETABLEUPDATE_CPU_VIRTUAL` quando todos os gates GPUVA/paging estao
armados. Nesse modo o Dxgkrnl fornece um mapeamento CPU da page table e a
atualizacao e imediata, podendo vir com `pDmaBuffer=NULL`.

O tradutor:

- valida nivel, `StartIndex`, quantidade, reservado, protection e pagina 4 KiB;
- faz uma passagem completa de validacao antes de escrever qualquer PTE;
- aceita PTE invalida e escreve zero;
- aceita inicialmente apenas segmento local 1;
- soma o offset da pagina a `LocalSegmentBaseOffset` comprovado fisicamente;
- traduz `ReadOnly`, `NoExecute` e `CacheCoherent` para os bits GC10;
- rejeita system/GART, zero-page, large page, linked adapter e 64 KiB;
- preserva integralmente a page table se qualquer entrada for invalida.

Segmento 0 permanece recusado porque o caminho system-memory/GART e a sua
traducao fisica ainda nao foram demonstrados. `FLUSH_TLB`, transfer e fill
continuam `STATUS_NOT_SUPPORTED`.

As regras seguem a semantica oficial de
[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable),
[CPU-virtual update mode](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_pagetableupdatemode) e
[DXGK_PTE](https://learn.microsoft.com/pt-br/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_dxgk_pte).

## Gate de producao

O provider fisico continua com `GpuVaReady=0` e `PagingReady=0`. Assim, o novo
caminho nao pode ser chamado na imagem instalada e nao altera VRAM/MMIO.

Schema 21 esperado:

- `PagingCpuUpdateReady=0`;
- `GpuVaDescriptorReady=0`;
- `RootPageTableAbiReady=0`;
- geometria 1.11/1.12 preservada;
- Code 43 e zero page tables operacionais.

## Build e integridade

- KMD `/W4 /WX` e todos os testes: PASS;
- conversao valid/invalid, permissoes e rollback sem escrita parcial: PASS;
- matriz MCDM e auditoria de seguranca: PASS;
- signability/catalogo: PASS;
- source SYS: `0CBCE26B49C3A7FDF882716965D5A1F36796DE6B5A329BE32AFB8D889CCAEBB2`;
- signed SYS: `B72216E6ED7ED14DAC9EE93FB3F7B1FCB5D8B2611E0AA04523B67F796A2499CC`;
- INF: `779CA69382C8CDD29F462ECA3AA4EABD2991360E5680962CB239C0A66FB84157`;
- CAT: `2C4E874AEC5FD71AA4377429FFD57D01A80C98A78461AFE9D640687EE0538E6C`;
- `HardwareValidated=false`.

## Resultado fisico e incidente de procedencia

O ensaio de `2026-08-26_08-19-47` instalou a versao 1.13.0.0 e preservou
Code 43, geometria, transacao VRAM e o hash do pacote. Entretanto,
`PagingCpuUpdateReady` ficou ausente do registro, embora o INF schema 21 e o
auditor ja o exigissem.

A causa foi identificada no processo de build: o pacote foi assinado depois da
mudanca de fonte/telemetria sem nova execucao de `build-contract.bat`, portanto
carregou um SYS anterior. O ensaio nao aprova o gate 1.13 e nao prova paging.

O empacotador agora recusa qualquer SYS mais antigo que os fontes `.c/.h` do
driver. A recusa foi testada antes da compilacao 1.14.0.0. Evidencia:
`docs\evidence\2026-08-26_08-19-47-DESKTOP-HQAG6T4-mcdm-replace`.

## Proximo incremento

Modelar e validar a invalidacao GFXHUB por VMID/root page table e o contrato de
`DXGK_OPERATION_FLUSH_TLB`. O gate `PagingReady` so podera subir quando update,
flush e rollback tiverem prova fisica; isso ainda nao autoriza submit PM4.

Veredito: **GATE FISICO NAO APROVADO; SUPERADO PELO PACOTE 1.14.0.0**.
