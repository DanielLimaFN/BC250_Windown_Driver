# Baseline BC-250 — terminal do operador

## Proveniência

Captura transcrita da saída fornecida pelo operador em 2026-08-22, após build,
assinatura, instalação e início de `bc250dbg.sys` v2.0/ABI 2. Não foi gerada
pelo coletor automatizado e não contém ainda os hashes da sessão instalada.

## Ambiente observado

| Item | Valor |
|---|---:|
| PCI | `01:00.0`, `1002:13FE`, revisão `00` |
| BAR0 CPU physical aperture | `0x00000000C0000000`, raw `0xC000000C` |
| BAR2 | `0x00000000D0000000`, raw `0xD000000C` |
| BAR4 | I/O `0xEF00`, raw `0x0000EF01` |
| BAR5 MMIO | `0x00000000FE800000`, 512 KiB, mapeado |
| armamento | `readonly` |

BAR0 físico CPU e base GPU/MC são domínios distintos. Esta captura não confirma
nem refuta a antiga hipótese de base MC `0xF400000000`.

## Registros

| Registro | Endereço BAR5 | Valor | Veredito limitado |
|---|---:|---:|---|
| `GPU_ID` | `0x00000` | `0x9FFF9700` | dispositivo e janela respondem coerentemente |
| `SCRATCH_REG0` | `0x30100` | `0x00000000` | leitura no offset SOC15; sem write-stick nesta sessão |
| endereço scratch legado | `0x032D4` | `0x4D585042` | conteúdo diferente; não representa scratch GC |
| `GRBM_STATUS` | `0x08010` | `0x00003028` | coincide com baseline Linux/histórico |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x089BC` | `0xFFF80000` | sustenta o mapa SOC15; não prova CUs utilizáveis |

## Thermal

| Sonda | Raw |
|---|---:|
| edge | `0xFFFFFFFF` |
| junction | `0x00000000` |
| mem | `0x00001BA8` |

As sondas são incompatíveis entre si e não estão calibradas. Nenhum valor deve
ser convertido em temperatura. `arm unsafe` deve permanecer bloqueado.

## Conclusão

Provados nesta sessão: serviço carregado, ABI compatível, enumeração PCI,
mapeamento BAR5 e leituras MMIO coerentes de identidade/mapa SOC15.

Não foram testados SMU, PSP, firmware, SDMA, CP, compute, GPUVA ou fences.

**NÃO VALIDADO EM HARDWARE**

