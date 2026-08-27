# Analise do binding forcado MCDM 0.2.4.0

## Resultado comprovado

O schema 2 preservou todos os gates anteriores e acrescentou:

| Campo | Valor |
|---|---:|
| `CmStatus` | `0` |
| `ResourceStatus` | `0` |
| `ResolvedBarCount` | `3` |
| `RuntimeMissing` | `1` |

As identidades lidas do header PCI e os comprimentos/intervalos da lista
traduzida do PnP foram cruzados sem duplicidade, ambiguidade ou recurso ausente.
O codigo 43 permanece o retorno fail-closed posterior a esse inventario.

## Proximo probe offline 0.3.0.0

O inventario foi separado do bootstrap. Para o dispositivo estrito
`1002:13FE`, BAR0 e BAR5 precisam existir entre os recursos resolvidos. BAR5 e
mapeada por `DxgkCbMapMemory` como kernel-only/noncached. A superficie de leitura
aceita apenas RCC_CONFIG_MEMSIZE e os tres campos GCMC de FB; sao duas passagens
e oito leituras. O unmap ocorre antes de qualquer progresso e sua falha bloqueia
o teardown.

Telemetria schema 3 registra `MmioStatus`, `TopologyReaderStatus`,
`TopologyStatus`, contagens e valores runtime. Nao existe primitiva de escrita
nesse transporte.

Pacote offline 0.3.0.0:

- `bc250readonly.sys`: `59D419150D2A59299CF8381FCCD27EEE4E2F27422441029204844CD03BC9DAA7`;
- `bc250readonly.cat`: `3B02297AF6D727B79E9D9FD2FB53BB9454E2690050973FFE0A0BD5A46ABD3CA7`;
- `/W4 /WX`, testes, Inf2Cat e assinaturas: aprovados.

0.3.0.0 nao foi instalado. Nenhum firmware, ring, fence ou compute foi
executado.

**NAO VALIDADO EM HARDWARE**
