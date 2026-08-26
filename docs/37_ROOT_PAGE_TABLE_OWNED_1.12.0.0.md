# Root page table owned por processo - pacote 1.12.0.0

Data: 2026-08-26.

Status: **GATE FISICO FAIL-CLOSED APROVADO; GPUVA NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   70%
Miniport MCDM funcional            42%
Memoria/GPUVA WDDM                 15%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Contrato implementado

`DxgkDdiSetRootPageTable` agora atravessa uma interface active-only e associa
a root page table ao objeto de processo dono do contexto. O registro guarda:

- segmento VidMm e offset no segmento, sem tratar o offset como PA bruta;
- numero de entradas e contador de associacoes;
- ownership derivado de `Context -> Device -> Process`;
- validacao de contexto, segmento 1, alinhamento de 4 KiB, faixa de 48 bits,
  limite do segmento local e numero maximo de entradas;
- par zero (`SegmentId=0`, offset e entradas zero) para desassociacao.

O caminho exige `RootPageTableAbiReady`, `IsActive`, caps GPUVA completas e
um contexto owned. O provider de producao mantem o gate zero.

## Correcao de semantica WDDM

`D3DGPU_PHYSICAL_ADDRESS` e um par `SegmentId/SegmentOffset`, nao um endereco
fisico cru. O primeiro build detectou essa diferenca pelo proprio WDK e o
contrato foi corrigido antes do empacotamento.

Segundo a documentacao Microsoft, `DxgkDdiGetRootPageTableSize` so e chamado
para uma hierarquia de dois niveis, cuja root pode crescer ou encolher. Para a
hierarquia fixa de quatro niveis que esta sendo modelada, ele permanece zero;
o tamanho de cada nivel vem de `PAGETABLELEVELDESC`.

Referencias:

- [Microsoft: GPU virtual address](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address)
- [Microsoft: DXGKARG_SETROOTPAGETABLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_setrootpagetable)
- [Microsoft: DxgkDdiGetRootPageTableSize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_getrootpagetablesize)

## Gate fisico esperado

Schema 20, ainda fail-closed:

- `RootPageTableAbiReady=0`;
- `RootPageTableMaxEntries=0`;
- `GpuVaDescriptorReady=0`;
- geometria de allocation e `QUERYSEGMENT4` preservadas;
- Code 43 e zero chamadas operacionais de root page table;
- SYS ativo identico ao pacote assinado.

Esses zeros sao intencionais: a DDI esta ligada estruturalmente, mas VidMm nao
pode chama-la enquanto StartDevice e as caps GPUVA permanecem fechados.

## Build e integridade

- KMD `/W4 /WX`, testes de objetos, lifecycle e callbacks: PASS;
- matriz MCDM e auditoria de seguranca: PASS;
- signability/catalogo: PASS, sem erros ou avisos;
- source SYS: `C732F42F11FB670129D0BB3CD611BC7664432ED2B581E7249C2837D3E487F56A`;
- signed SYS: `A0887EB233BE81D7E28A308D03D9C7D6983E60C53E3CA35DF1D73D6F6E50B7A5`;
- INF: `5A90042C6630812A44F13C7DCBF4D4A92BAB7918EFD483B328A243F720F59929`;
- CAT: `B0C6B86001E2BB972AB329774837A1AE0886B81B6AFFF16D2CAB82869ACBC885`;
- `HardwareValidated=false`.

## Proximo incremento

Depois do schema 20, implementar o tradutor de `DXGK_OPERATION_UPDATE_PAGE_TABLE`
para PTEs GC10 e o subconjunto CPU-virtual de `BuildPagingBuffer`, ainda sem
armar `GpuVaReady`. O primeiro gate operacional exigira escrever, ler de volta
e restaurar uma page table owned antes de habilitar invalidacao TLB.

Veredito: **INTEGRIDADE APROVADA; GPUVA/ACELERACAO NAO VALIDADAS**.

## Resultado fisico

Evidencia `docs/evidence/2026-08-26_08-13-48-DESKTOP-HQAG6T4-mcdm-replace`:

- schema 20 e SYS ativo identico ao pacote assinado;
- `RootPageTableAbiReady=0` e `RootPageTableMaxEntries=0`;
- `GpuVaDescriptorReady=0` e `McdmFullCapsReady=0`;
- segmento local e transacao VRAM permaneceram aprovados;
- Code 43 intencional; nenhuma root page table foi associada pelo Dxgkrnl.

Veredito fisico: **FAIL-CLOSED APROVADO; GPUVA NAO VALIDADA**.
