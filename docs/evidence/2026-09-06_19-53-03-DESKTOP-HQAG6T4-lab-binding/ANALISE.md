# Análise da corrida 2026-09-06 19:53

## Fatos medidos

- Perfil aplicado: `DISPLAY_RENDER_LAB`, `RenderNode=1`, `RenderOnlyLab=0`,
  `GpuvaLabPromotion=1`, contrato WDDM 2.0.
- O harness carregou o ICD explícito correto, criou a instância, enumerou a
  BC-250 e parou dentro de `vkCreateDevice` por 20 segundos.
- O KMD recebeu dois `McdmCreateContext Node=1`, mas nenhuma
  `McdmSubmitCommandVirtual Node=1`.
- Duas submissões de paginação Node 0 passaram o tripé físico completo:
  `81920` bytes (fence `0x1002`) e `4096` bytes (fence `0x1003`).
- O processo com root instalada em `SegmentId=1, Offset=0` teve seus leaf
  updates estagiados, mas cada flush foi registrado como
  `Pending=1, CanCommit=0`. O snapshot GPUVA não foi publicado.
- O watchdog restaurou `BasicDisplay`, Código 0. O fusível permaneceu `ARMED`;
  boot frio continua obrigatório antes de outra corrida.
- Não houve dump do hang. O último log foi `McdmBuildPagingBuffer Op=11`, sem
  informação suficiente para atribuir o bloqueio final a uma função interna.

## Defeito de contrato encontrado

O offset zero é válido dentro do segmento local: a root física instalada foi
`VramMcBase + LocalSegmentBaseOffset + 0 = 0x450A8C000`. Porém o object
contract e o journal também usavam zero como sentinela de “root ausente”. Isso
impedia o commit da transação e impediria uma futura submissão Node 1.

O candidato seguinte codifica a identidade entregue ao journal como
`SegmentOffset + 1`, mantém zero exclusivamente como sentinela interna e usa
`RootPageTableSetCount + SegmentId + Entries` para provar presença no object
contract. Também captura um dump do processo vivo com CDB antes de terminá-lo
se o timeout reaparecer.

Correção e próximo candidato: **NÃO VALIDADO EM HARDWARE**.

## Artefato preservado

- `bc250_display_log-full.txt`
- SHA-256: `AB8107B9CA18BA5403B6B5A4E40F994FC2CEAFCEA5A4BE8A9D08971321DBC2A5`
