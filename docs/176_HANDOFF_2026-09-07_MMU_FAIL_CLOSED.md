# 176 — Handoff: MMU global fail-closed no WDDM

**Data:** 2026-09-07 15:05 BRT  
**Plataforma:** BC-250 `1002:13FE`, GC 10.1.3, Windows 10 22H2 build 19045.2965  
**Veredito do candidato:** compilado, assinado e auditado; **NÃO VALIDADO EM HARDWARE**.

## 1. Correção da leitura da evidência

O ensaio isolado `2026-09-07_13-59-59-vmid8-ib-lab` prova que o conjunto de
programação global da MMU permite executar o IB SDMA sob VMID8:

- `run.txt:325-343`: `RPTR==WPTR`, fence física substituindo poison em 11 ms e
  quatro dwords conferidos no destino;
- `run.txt:447-519`: com PDE deliberadamente corrompida para `0x61`, o destino
  reteve poison, o RPTR parou antes do WPTR e o GFXHUB registrou
  `FAULT_STATUS=0x00801B01`, `FAULT_ADDR=0x201`, VMID8 e `MAP_ERR=1`, isto é,
  página virtual `0x201000`.

Isso é prova causal do **conjunto** de 26 registradores. Não mede qual registrador
individual é a causa e não mede a latência da fault: o script fez espera limitada de
aproximadamente dois segundos antes de ler o latch. Não usar a expressão “fault
imediata”.

A corrida WDDM `2026-09-07_14-32-49-DESKTOP-HQAG6T4-lab-binding` não validou a
integração:

- `display-log-this-bind.txt:391` registra
  `[SetRootPageTable] Global GFXHUB MMU FAILED on demand: 2`, onde 2 é
  `SNAPSHOT_FAILED`;
- o código daquele candidato continuou e publicou a root mesmo assim: fail-open;
- o log delimitado ao bind tem zero Op 11, zero update concluído, zero submit,
  zero alocação padrão, zero commit, zero source address e zero present;
- `bc250_display_log-full.txt` é acumulado e contém eventos de corridas anteriores;
  não pode ser usado para atribuir paginação ou commit ao bind das 14:32;
- o RADV foi corretamente pulado (`radv-display-prerequisite-gate.json` com
  `RADV_SKIPPED`).

## 2. Defeitos corrigidos offline

1. `StartDevice` agora inicializa a MMU global antes da divisão entre produção e
   laboratório. Falha de preflight, snapshot, apply, readback ou flush aborta o
   `StartDevice`.
2. `SetRootPageTable` exige a MMU pronta antes de publicar o estado do objeto e
   antes de instalar VMID8. Falha retorna o status e não segue adiante.
3. A dummy page não usa mais o endereço fixo `VramMcBase + 0xA80000`, que
   sobrepunha os últimos 48 KiB do scanout GOP (`LocalSegmentBase=0xA8C000`). Ela
   é uma reserva de 4 KiB do allocator, top-down e CPU-visible.
4. `QueryAdapterInfo/QUERYSEGMENT4` trunca o fim do segmento local antes da dummy
   page, impedindo que o VidMm receba a mesma página.
5. Rollback de programação é LIFO, confere cada readback, registra falhas e faz
   flush final. O trigger transitório de `L2_CNTL2` é mascarado somente na
   comparação.
6. `StopDevice` confere o restore, registra falha honestamente, libera a reserva
   e propaga erro de teardown. Não há mais restore ignorado seguido de log de
   sucesso.

Arquivos centrais:

- `driver/core/bc250_gcvm_global_mmu.{c,h}`
- `driver/wdk/bc250_active_miniport.{c,h}`
- `driver/tests/gcvm_global_mmu_test.c`
- `driver/tests/active_miniport_test.c`
- `tools/audit-bc250mcdm-safety.py`

## 3. Testes nomeados e resultado

- `driver/wdk/build-contract.bat`: `EXIT=0`.
- `gcvm_global_mmu_test.exe`: cobre apply/readback, bits transitórios, falha de
  readback, falha de escrita, rollback verificável, rollback que também falha e
  restore exato.
- `active_miniport_test.exe`: prova offline que root não é publicada com a MMU
  indisponível.
- `audit-bc250mcdm-safety.py`: exige init comum aos perfis, retorno fail-closed,
  dummy page via allocator e restore conferido.
- `build-bc250mcdm-package.ps1`: `EXIT=0`.
- `audit-bc250mcdm-package.ps1`: `PASS (INTEGRITY ONLY)`; fonte SYS
  `87C129611101BFAABB04CE971EAB1676F6E79C971561E13A46FE0D81009DB30A`,
  SYS assinado
  `7C74CBDA5A9BE8341CA5BB1A7D6E9D4B2F10CE98E4422B83A6A267A6BA6AA3F4`.
- instalação/bind: não executados; **NÃO VALIDADO EM HARDWARE**.

O `output_lab` não foi reconstruído: seu SYS assinado é de 14:16 e o manifesto
aponta a fonte `5D7333F4...`, anterior a esta correção. A reconstrução exige a
aceitação explícita `-IUnderstandThisEnablesDestructiveBringup` do dono.

## 4. Próximo teste físico — um único bind de produção

**Teto:** uma corrida após boot frio. Não bisseccionar registradores nem executar
RADV nessa corrida.

**Comando do Daniel:**

```powershell
powershell -ExecutionPolicy Bypass -File tools\invoke-elevated-lab-bind.ps1
```

**PASS mínimo do candidato:** no mesmo `display-log-this-bind.txt`, observar a
inicialização global com readback/flush aprovados, adapter Código 0, SharedPrimary,
`CommitVidPn`, `SetVidPnSourceAddress` e `Present`; rollback do watchdog deve
preservar BasicDisplay se não houver confirmação visual.

**Prova para fechar paginação WDDM (item 40):** além do acima, o DMA de paginação
originado pelo VidMm precisa chegar a `SubmitCommand[Virtual]` e produzir o tripé
completo: `RPTR==WPTR`, fence física sobre poison e bytes conferidos. Arquivar tudo
em `docs/evidence/<timestamp>-...-mcdm-display-binding/`.

**Abandono da corrida:** qualquer falha de MMU global, ausência de Código 0,
ausência de commit/source address, timeout ou tripé incompleto. Nesses casos não
rodar RADV, deixar o watchdog restaurar e fazer novo boot frio antes de outro bind.

Até essa evidência existir, a integração WDDM e o display deste candidato são
**NÃO VALIDADOS EM HARDWARE**.
