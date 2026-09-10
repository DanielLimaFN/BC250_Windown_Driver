# P6_FIRST_VIRTUAL_IB_TRIPOD

Data local: 2026-09-04 22:16–22:17.

## Prova física positiva de controle

Fonte: `C:\BC250_DISPLAY_LOG.txt`, linhas 1133–1138 da captura com timestamp
22:17:02.

```text
[FillVirtual] Completo: 11075584 bytes, pattern=0x00000000, em 15 dwords
[McdmSubmitCommandVirtual] Node=0, FenceId=1, DmaSize=60
[SubmitVirtual Node0 SDMA] FenceId=1, DmaSize=60, DmaVA=0x24F000
[SubmitVirtual Node0 SDMA] IB_TRIPOD PASS #1: WPTR=132 RPTR=132 Fence=00001001 Poison=43DAFFFE Size=60 Vmid=8 in 1 polls
[SubmitVirtual Node0 SDMA] IB EXECUTADO: DMA buffer de 60 bytes em VA=0x24F000; DMA_COMPLETED notificado para FenceId=1
```

Duas pernas observadas: `RPTR == WPTR` e a fence GPU substituiu o poison. O
conteúdo da IB era o `FillVirtual` de 15 DWORDs produzido imediatamente antes.
Sem comparação byte a byte do destino, esta evidência não deve ser chamada de
tripé completo da Lei 2.

## Motivo da parada seguinte

```text
[McdmSubmitCommandVirtual] Node=0, FenceId=2, DmaSize=60
[SubmitVirtual Node0 SDMA] FenceId=2, DmaSize=60, DmaVA=0x24F03C
[SdmaPresent] ExecuteIndirect invalid params: VA=0x24F03C, Size=60, Vmid=8
[SubmitVirtual Node0 SDMA] GFXHUB_FAULT_SNAPSHOT: Read=1/1/1 STATUS=00000000 ADDR=00000000:00000000
```

A segunda submissão não foi enviada à SDMA. O endereço resultou do avanço de
60 bytes e não atende o alinhamento de 32 bytes do pacote `INDIRECT`.

## Correção offline subsequente

`VIRTUAL_FILL` e `VIRTUAL_TRANSFER` agora completam o próprio DMA buffer com
NOPs até múltiplo de 8 DWORDs. Testes acrescentados ao
`mcdm_callbacks_test.exe` verificam:

- fill de 11.075.584 bytes: 15 DW úteis + 1 NOP = 16 DW;
- transfer de 9.437.184 bytes: 21 DW úteis + 3 NOPs = 24 DW;
- próxima GPUVA permanece alinhada a 32 bytes.

Teste nomeado: `P6_PAGING_IB_SIZE_ALIGNMENT_OFFLINE`.

- `driver/wdk/build-contract.bat`: PASS.
- safety audit: PASS.
- package audit: PASS (integridade apenas).

Novo pacote:

```text
Source SYS SHA256: C84C83650542FD8CFE34C3AC21335FD6063FCE9C203AF877AB7F5F327214A56C
Signed SYS SHA256: 2AECF5BE6AD32A0FED376E54AD77D8F65BF4F0293BBF2DDDCB0C269C1E0EA085
CAT SHA256:        6A2BFCF65E39F3AFEE4C57E1CDD7A2D8DF4D6AC7F234B32BC32135FFB5AD53D4
```

## Veredito

Fetch/retirement da primeira IB virtual: **PROVADO NO SILÍCIO**.

Efeito byte a byte do `FillVirtual`: **NÃO VALIDADO EM HARDWARE**.

Padding do tamanho e IBs consecutivas: **NÃO VALIDADO EM HARDWARE**.
