# Analise do binding forcado MCDM 0.3.0.0

## Inventario MMIO readonly comprovado

| Campo | Valor |
|---|---:|
| `MmioStatus` | `0` |
| `TopologyReaderStatus` / `TopologyStatus` | `0` / `0` |
| `TopologyReadCount` / `TopologyStablePasses` | `8` / `2` |
| `VramTotalMiB` / `CpuVisibleMiB` | `512` / `256` |
| `FbBase16M` / `FbTop16M` | `0xF400` / `0xF41F` |
| `FbOffset16M` | `0x450` |
| `RuntimeMissing` | `1` |

BAR5 foi mapeada kernel-only/noncached a partir do recurso PnP resolvido. O
transporte permitiu somente quatro offsets, duas passagens e fez unmap antes do
gate final. Os valores coincidem com a captura Linux, mas ainda nao provam
compute.

O pacote offline 0.4.0.0 acrescenta ownership selado e auditoria ReBAR:

- `.sys`: `383A61C4804DC99FCD149B5385B28A273730CA2FE9B2E856E42492A318E59B1F`;
- `.cat`: `A7E0308F9733E7C1008EE7EE49D76E34FCEAAE35E6A7B0F10162FBB6D80873D3`;
- `/W4 /WX`, testes, Inf2Cat e assinaturas: aprovados.

0.4.0.0 nao foi instalado. Nenhum firmware, ring, fence ou compute foi
executado.

**NAO VALIDADO EM HARDWARE**
