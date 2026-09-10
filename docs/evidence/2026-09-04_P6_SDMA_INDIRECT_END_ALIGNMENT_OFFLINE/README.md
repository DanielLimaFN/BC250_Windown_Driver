# P6_SDMA_INDIRECT_END_ALIGNMENT_OFFLINE

Data local: 2026-09-04.

## Objetivo

Provar offline que todo pacote SDMA 5.0 `INDIRECT` de 6 DWORDs emitido pelo
caminho de paging termina em fronteira de 8 DWORDs.

## Falha física que motivou o teste

Na corrida 22:00, o anel iniciou a IB em DW 17 sem padding. Assim, o
`INDIRECT` terminou em DW 23, divergindo da regra de
`sdma_v5_0_ring_emit_ib()` usada pelo upstream para Cyan Skillfish.

## Contrato verificado

Para WPTRs 0..31:

```text
pad = (2 - WPTR_DW) & 7
(WPTR_DW + pad + 6) & 7 == 0
```

Casos explicitamente cobertos: WPTR 0, 2, 13 e 17. Para a corrida observada,
WPTR 17 produz `pad=1` e `IndirectEndDW=24`.

## Execução offline

- `driver/wdk/output/sdma_packet_builder_test.exe`: PASS.
- `driver/wdk/build-contract.bat`: exit code 0; todos os contratos aprovados.
- `tools/audit-bc250mcdm-safety.py`: PASS.
- `tools/build-bc250mcdm-package.ps1`: PASS; assinatura e Inf2Cat aprovados.
- `tools/audit-bc250mcdm-package.ps1`: PASS (integridade apenas).

Artefatos do candidato:

```text
Source SYS SHA256: E7A66FDE34E08EC0AAAA392053A5A37E1603B0B884F7A4FCC3DB3A51C0B3D74B
Signed SYS SHA256: 494E10AACB9681411A596BAC8D7A893F1C7902D0B4E4EB8BDA7E02AFE625D48F
CAT SHA256:        653E9DE0069B466E1007EDC5893A7E51690CF06F4D0190C3E8ADC87DAC91CD57
```

O dump `C:\Windows\MEMORY.DMP` não foi lido integralmente. Uma invocação do
debugger limitada a BugCheck, stack curta e módulos BC250 foi recusada com
Win32 error 5 (`Acesso negado`).

## Veredito

Contrato de alinhamento: PASS offline.

Estado histórico no momento deste ensaio offline: execução física ainda
**NÃO VALIDADO EM HARDWARE**. A corrida posterior de 22:16 provou a primeira
IB; ver `../2026-09-04_P6_FIRST_VIRTUAL_IB_TRIPOD/README.md`.
