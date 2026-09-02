# Allocation no segmento local e GPUVA latente - pacote 1.11.0.0

Data: 2026-08-26.

Status: **GATE FISICO APROVADO PARA GEOMETRIA DE ALLOCATION; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   70%
Miniport MCDM funcional            42%
Memoria/GPUVA WDDM                 15%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

Os percentuais nao sobem com testes offline.

## Mudanca implementada

O provider fisico aprovado no 1.9/1.10 agora alimenta o contrato de allocation:

- segmento local: ID 1, bit de conjunto `1`;
- limite por allocation: `LocalSegmentBytes`, atualmente `0x0F132000`;
- read/write segment set: segmento 1;
- preferencia primaria: segmento 1;
- allocation marcada CPU-visible e physical adapter 0;
- pedido maior que o segmento ou geometria ausente falha fechado.

O gate `AllocationAbiReady` continua zero em producao. Portanto, esta entrega
torna a descricao coerente, mas ainda nao permite que o Dxgkrnl crie memoria
operacional enquanto `StartDevice` termina em Code 43.

Tambem foram adicionadas as consultas `DXGKQAITYPE_GPUMMUCAPS` e
`DXGKQAITYPE_PAGETABLELEVELDESC`. Elas exigem simultaneamente
`FullMcdmReady`, `GpuVaReady` e descritores nao nulos. O provider de producao
nao arma nem preenche esses campos. Os valores estruturais usados nos testes
de host nao constituem descricao do hardware.

## Gate fisico esperado

Schema 19, ainda fail-closed:

- `AllocationSegmentSet=1`;
- `AllocationMaxLow32=252911616` (`0x0F132000`);
- `GpuVaDescriptorReady=0`;
- `McdmSegment4Ready=1` e `McdmFullCapsReady=0`;
- Code 43 intencional;
- inventario, segundo preflight e transacao VRAM preservados;
- SYS ativo identico ao pacote assinado.

Esse gate prova apenas que a geometria fisica chegou ao contrato de allocation.
Nao prova allocation VidMm, root page table, GPUVA, paging ou acesso da GPU.

## Build e integridade

- KMD `/W4 /WX` e todos os testes de contrato: PASS;
- testes de allocation segmento 1 e consultas GPUVA condicionais: PASS;
- matriz MCDM e auditoria de seguranca: PASS;
- signability e catalogo: PASS, sem erros ou avisos;
- source SYS: `2DCA2917D1FC6FD17577B4CF88627E48BAAE7D26AE1B544D03525878C9EBE46C`;
- signed SYS: `BCF63C40987BEC8E206B74CA0C46DA9BBAA1AB6B330FA0B4F6D4B68D4151AD8E`;
- INF: `72454B682350E5917EC93D5DB8B355C71533F3D82CA0132BA7D917047668D574`;
- CAT: `495CA8B9640FE13A5D73BF61307024BEB3BBE0AEBFA5058037AE17601E3A46D3`;
- `HardwareValidated=false` no manifesto.

## Proximo passo

Executar:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\replace-bc250mcdm.ps1
```

Depois do gate schema 19, o proximo incremento e materializar root page table
owned por processo e implementar `GetRootPageTableSize`, `SetRootPageTable` e
o primeiro subconjunto real de `BuildPagingBuffer`, ainda sem submit PM4.

Veredito: **INTEGRIDADE APROVADA; ACELERACAO NAO VALIDADA**.

## Resultado fisico

Evidencia `docs/evidence/2026-08-26_03-32-48-DESKTOP-HQAG6T4-mcdm-replace`:

- schema 19 e SYS ativo identico ao pacote assinado;
- `AllocationSegmentSet=1`;
- `AllocationMaxLow32=252911616` (`0x0F132000`);
- `GpuVaDescriptorReady=0`;
- `McdmSegment4Ready=1`, `McdmFullCapsReady=0`;
- Code 43 intencional e auditoria fail-closed aprovada;
- nenhuma submissao PM4/SDMA ou prova de GPUVA ocorreu.

Veredito fisico: **GEOMETRIA DE ALLOCATION APROVADA; ACELERACAO NAO VALIDADA**.
