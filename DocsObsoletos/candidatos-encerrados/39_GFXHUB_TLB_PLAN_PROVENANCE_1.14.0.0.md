# Procedencia selada e plano GFXHUB TLB - pacote 1.14.0.0

Data: 2026-08-26.

Status: **PACOTE CONSTRUIDO; NAO VALIDADO EM HARDWARE**.

```text
Conformidade BC250_HARDWARE_RULES   70%
Miniport MCDM funcional            42%
Memoria/GPUVA WDDM                 15%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

## Correcao de procedencia

`tools\build-bc250mcdm-package.ps1` agora compara a data do SYS com todos os
fontes `.c/.h` de `driver\core` e `driver\wdk`, excluindo apenas artefatos de
`output` e `package`. O pacote e recusado se qualquer fonte for mais novo.

O gate foi exercitado em estado stale e recusou o pacote. Depois disso foi
executado o build completo, todos os testes e somente entao a assinatura.
O script de replace agora executa e arquiva automaticamente a auditoria fisica
fail-closed depois do binding.

## Contrato de invalidacao GFXHUB

O antigo `Bc250PagingBuildFlushTlb` emitia um SDMA NOP e o tratava como
barreira. Isso foi removido: o caminho legado retorna
`BC250_PAGING_UNSUPPORTED_OPERATION` sem alterar o buffer ou cursor.

O novo builder puro produz um plano GC 10.1.3 verificavel, sem tocar MMIO:

- GFXHUB, engine de invalidacao 17;
- VMID de processo entre 1 e 15; VMID 0 recusado;
- request `0xA2D0`, ACK `0xA318`;
- range low/high `0xA3A4`/`0xA3A8`, esperado `FFFFFFFF`/`0000001F`;
- bits de PTE/PDE0/PDE1/PDE2/L1 invalidados;
- ACK isolado pelo bit do VMID;
- leitura dummy do request obrigatoria depois da escrita;
- nenhum semaforo GFXHUB.

Testes cobrem VMID 8, enderecos, masks, leitura dummy, ausencia de semaforo e
rejeicao de VMID 0, MMHUB e flush type fora do campo. A auditoria estatica
proibe reintroduzir `Bc250SdmaEmitNop` como invalidacao.

## Gate fisico

Schema 22 deve registrar:

- `PagingCpuUpdateReady=0`;
- `TlbFlushPhysicalReady=0`;
- `GpuVaDescriptorReady=0`;
- `RootPageTableAbiReady=0`;
- geometria e transacao VRAM anteriores preservadas;
- Code 43 fail-closed.

Mesmo com o plano correto, `DXGK_OPERATION_FLUSH_TLB` continua recusado. O gate
so pode subir depois que houver transporte MMIO owned, escrita do range/request,
leitura dummy, espera limitada do ACK correto e restauracao/teardown provados no
hardware. Nenhum submit PM4 ou compute foi autorizado.

## Build e integridade

- build completo e testes: PASS;
- matriz MCDM e auditoria fail-closed: PASS;
- signability/catalogo: PASS;
- source SYS: `A0BD24164884E1C88082AFB491F0FCC211D4E885759A1C11D942893E68F99CEC`;
- signed SYS: `E2A942DB49F5B596237EDE78F6366729E14A8998F8DDCDD2F3C6D4CC7DA36DF7`;
- INF: `156902F7CC6FBEF37385F03643A659185C4FA2E17B80DD2A7DAAAEF106E41292`;
- CAT: `61D6CFF461038EF62CF1736BC1991DE55E72A50F2424D2E03B5303AD781BE50A`;
- `HardwareValidated=false`.

Veredito: **INTEGRIDADE APROVADA; TLB/PAGING/GPUVA NAO VALIDADOS**.

## Resultado fisico do pacote

O ensaio `2026-08-26_08-27-42` aprovou schema 22 e confirmou no SYS ativo os
campos `PagingCpuUpdateReady=0` e `TlbFlushPhysicalReady=0`. Lifecycle,
geometria, transacao VRAM e hash do pacote foram preservados. Isso aprova o
gate de procedencia/observabilidade 1.14, mas nao executou invalidacao TLB.

Evidencia:
`docs\evidence\2026-08-26_08-27-42-DESKTOP-HQAG6T4-mcdm-replace`.
