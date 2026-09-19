# 178 — Corrida 15:49: snapshot bloqueado pela allowlist MMIO

> **CORREÇÃO VALIDADA NO ENSAIO SEGUINTE:** a corrida 16:19 inicializou os 26
> registradores, chegou a Código 0, CommitVidPn e Present. Continue em
> [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md).

**Data:** 2026-09-07 15:49–15:52 BRT  
**Evidência do bind:** `docs/evidence/2026-09-07_15-49-55-DESKTOP-HQAG6T4-mcdm-display-binding/`  
**Evidência do rollback:** `docs/evidence/2026-09-07_15-51-57-DESKTOP-HQAG6T4-mcdm-online-rollback/`  
**Resultado:** falha segura em Código 43; watchdog restaurou BasicDisplay Código 0.

## O que foi medido

O candidato ultrapassou a falha anterior de alocação da dummy e mapeou o transporte MMIO.
Logo depois, o snapshot da MMU global falhou no primeiro offset:

```text
[StartDevice] Global MMU transport map status: ok (State=1, Base=FFFF8A0148340000)
[StartDevice] Global GFXHUB MMU FALHOU: status=2 step=3 reg=0x0A610 val=0x00000000 rollback=0/0
```

`status=2` é `BC250_GCVM_GLOBAL_MMU_SNAPSHOT_FAILED`; `0x0A610` é
`GCMC_VM_AGP_BASE`. `device-after.json` registra `ConfigManagerErrorCode=43` e o
`pnputil` registra `CM_PROB_FAILED_POST_START`.

Daniel informou que havia imagem, mas a confirmação foi corretamente recusada porque a
BC-250 não estava em Código 0. Imagem visível durante um bind que terminou em Código 43
não prova que o candidato assumiu o display, comitou VidPn ou apresentou pela GPU.

Há ainda uma linha histórica que se autodenomina `TRIPÉ` com `WPTR=68 RPTR=52`. Pelas leis
do projeto, valores diferentes não satisfazem a perna `RPTR==WPTR`; esta linha não é usada
como prova nesta corrida, apesar do rótulo e dos `Dados=4/4 OK`.

## Causa localizada no código

`Bc250ActiveVmidRead32` encaminha a leitura para `Bc250MmioTopologyRead32`. Antes de tocar
o BAR, esse transporte consulta `Bc250MmioTopologyOffsetAllowed`. A lista não continha
`BC250_REG_GCMC_VM_AGP_BASE` nem a maior parte dos outros offsets da transação global.
Portanto, o retorno zero foi uma recusa do software **antes do acesso MMIO**; não é uma
resposta do silício e não permite inferir o valor físico de `0xA610`.

## Correção offline

- `bc250_mmio_topology_transport.h` inclui a fonte única dos offsets globais.
- `Bc250MmioTopologyGlobalMmuOffsetAllowed` enumera exatamente os 26 registradores usados
  por `Bc250GcvmGlobalMmuProgram`, sem intervalo permissivo.
- As allowlists de leitura e escrita chamam o mesmo helper.
- `mmio_topology_transport_test` grava e relê todos os 26 offsets e verifica que o vizinho
  não catalogado `0xA188` continua recusado.
- A auditoria de segurança exige todos os 26 `case` e o uso do helper nas duas direções.

Teste que fecha esta correção: `driver/tests/mmio_topology_transport_test.c`. Ele passou
20 vezes junto de `active_miniport_test.exe`; `gcvm_global_mmu_test.exe`, a suite integral
e a auditoria de segurança também passaram. Esses são testes offline com MMIO falso.

## Rollback confirmado

`rollback-result.json` registra:

```text
service_after: BasicDisplay
problem_code_after: 0
matching_mcdm_inf_after: 0
success: true
hardware_acceleration_validated: false
```

Logo o reboot do watchdog cumpriu a recuperação prevista; não validou aceleração.

## Pacote seguinte

- `driver/wdk/build-contract.bat`: `EXIT=0`.
- `BC250 MCDM SAFETY AUDIT`: PASS.
- `BC250 MCDM PACKAGE AUDIT`: PASS, integridade somente.
- fonte SYS: `F0375F9496434B29DB12675B838092340338A2030B19CE689BB5712BED59A83D`.
- SYS assinado: `069A73307D665B2B84C0A6CC09A37E5C508C70A071BF2CC6E410E996662B9DF7`.
- catálogo: `F549887BE8F236B383A5BD180BF848385B84EBD4CE11E1B8EE1149BB9FCF9CD3`.

O pacote `output_active` acima não foi instalado: **NÃO VALIDADO EM HARDWARE**. O
`output_lab` permanece estagnado e não foi reconstruído.

## Próxima corrida nomeada

Uma corrida de produção, após boot frio, por
`tools/invoke-elevated-lab-bind.ps1`, com teto de 120 segundos e watchdog. O primeiro gate
é obter snapshot/readback dos 26 registradores e `Global GFXHUB MMU initialized`. Só então
avaliar Código 0, `CommitVidPn`, `SetVidPnSourceAddress` e `Present`. Qualquer Código 43,
falha de readback ou ausência de confirmação mantém o candidato reprovado e deixa o
watchdog restaurar BasicDisplay.
