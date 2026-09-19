# 177 — Corrida 15:16: fail-closed da dummy page e correção do allocator

> **SUPERADO PELO ENSAIO SEGUINTE:** a corrida das 15:49 confirmou que o allocator já
> ultrapassa este ponto, mas expôs a allowlist MMIO incompleta. Continue em
> [`178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md`](178_HANDOFF_2026-09-07_15-49_MMIO_ALLOWLIST.md).

**Data:** 2026-09-07 15:16–15:18 BRT  
**Evidência:** `docs/evidence/2026-09-07_15-16-09-DESKTOP-HQAG6T4-mcdm-display-binding/`  
**Resultado:** falha segura em Código 43; rollback automático para BasicDisplay Código 0.

## Medição

O stage via `UpdateDriverForPlugAndPlayDevicesW` retornou sucesso, mas o dispositivo
terminou com `CM_PROB_FAILED_POST_START`, Código 43. A confirmação visual informada pelo
Daniel foi recusada pelo script porque o serviço não estava em `bc250mcdm` Código 0.
A imagem visível era a sobrevivência/retorno do caminho de segurança, não sucesso do
candidato.

O log delimitado ao bind registra:

```text
[SdmaPresent] TRIPÉ NO SILÍCIO COMPROVADO ... Fence=00001001 Dados=4/4 OK
[StartDevice] Global MMU transport map status: ok
[StartDevice] Global GFXHUB MMU RECUSADA: dummy page sem reserva valida (vramStatus=6)
[StartDevice] RECUSADO fail-closed: MMU global status=0xC000009A
```

`vramStatus=6` é `BC250_VRAM_NO_SPACE`. A causa está provada pelo estado do código:
na primeira passagem readonly, `RuntimeStaged=0` termina em `RUNTIME_MISSING`; portanto o
allocator de `Core.StartDevice.Owned.Bootstrap` ainda estava zerado. A integração tentou
reservar a dummy nele antes do segundo stage. Não houve escrita da MMU global.

O watchdog removeu `oem34.inf`, verificou BasicDisplay Código 0 e arquivou rollback em
`docs/evidence/2026-09-07_15-18-10-DESKTOP-HQAG6T4-mcdm-online-rollback/`.

## Correção offline

- A MMU global ganhou allocator próprio no contexto do adapter, inicializado com
  `TotalBytes` e `CpuVisibleBytes` da topologia capturada.
- O scanout herdado é reservado antes da alocação top-down da dummy.
- Com a geometria medida de 16 GiB, BAR0 de 256 MiB e scanout de `0xA8C000`, o teste
  coloca a dummy em `0x0FFFF000`.
- No perfil lab, a dummy entra no inventário runtime como carveout persistente, impedindo
  que o bootstrap de firmware/TMR reutilize a página.
- O teardown libera a dummy no allocator que efetivamente a criou.

Teste nomeado: `Bc250RunGlobalMmuDummyAllocatorTest`, dentro de
`active_miniport_test.exe`, congela a geometria física acima, a exclusão do scanout e a
herança pelo inventário lab.

## Gates e pacote seguinte

- `driver/wdk/build-contract.bat`: `EXIT=0`.
- `BC250 MCDM SAFETY AUDIT`: PASS.
- `BC250 MCDM PACKAGE AUDIT`: PASS, integridade somente.
- fonte SYS: `9A80C7AA6ECA6D4D0AEF1E403572FB66A05ABB8564A20FAC1A0AA4883281CC79`.
- SYS assinado: `97C96152F83F64B0F37CE95D10F15F2F489267B54527DE08326815DBD711BD0E`.
- novo pacote não instalado: **NÃO VALIDADO EM HARDWARE**.

## Próxima corrida

Teto: uma corrida de produção após boot frio. O primeiro critério é ultrapassar a linha
da reserva e registrar `Global GFXHUB MMU initialized`, com dummy fora do scanout.
Depois disso: Código 0, SharedPrimary, `CommitVidPn`, `SetVidPnSourceAddress` e `Present`.
Qualquer falha mantém RADV fora da corrida e deixa o watchdog restaurar BasicDisplay.
