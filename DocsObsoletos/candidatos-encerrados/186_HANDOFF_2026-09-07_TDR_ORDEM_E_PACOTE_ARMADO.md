# 186 — item 25: ordem corrigida e pacote TDR armado desde o INF

**Data:** 2026-09-07, continuação do doc 185  
**Corridas gastas:** **zero** (offline)  
**Estado:** pacote de ensaio construído e auditado, **NÃO INSTALADO**.  
**Item 25:** continua **ABERTO** e **NÃO VALIDADO EM HARDWARE**.

## 1. A recusa do script estava correta

`arm-tdr-induce-hang.ps1 -Arm` foi executado antes da instalação e encontrou ausente:

```text
HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\Parameters
```

Não era falha do gate. A chave é criada pelo `AddReg` do INF durante a instalação, enquanto
o KMD lê `TdrInduceHang` no `StartDevice`. Escrever depois do bind também não muda o bind já
ativo; o valor só vale no próximo `StartDevice`. Criar a chave manualmente violaria a origem
transacional do pacote.

## 2. Correção offline

- `build-bc250mcdm-package.ps1` ganhou `-ArmTdrInducedHang`, exige
  `-IAcceptDeliberateHangAndPossibleBugCheck` e uma `-PackageDriverVersion` nova.
- O INF **fonte** permanece fail-closed em `TdrInduceHang=0`.
- Somente o INF **empacotado**, antes do `Inf2Cat`, recebe `TdrInduceHang=1`; o manifesto
  deriva o valor desse INF e usa `BuildKind=MCDM_TDR_INDUCED_HANG`.
- O binder recusa pacote armado sem `-IAcceptDeliberateHangAndPossibleBugCheck`.
- Os scripts de registro agora dizem que o valor só entra no próximo `StartDevice` e
  proíbem criar a chave manualmente.
- `audit_induced_hang_package_order` trava esses contratos.

## 3. Pacote `3.88.0.0` — assinado, auditado, NÃO INSTALADO

| Item | Valor |
|---|---|
| `BuildKind` | `MCDM_TDR_INDUCED_HANG` |
| `DriverVer` | `09/07/2026,3.88.0.0` |
| SYS fonte | `5B6F07EDFA23E0B8DC84CEDD60C8233355A266875E21331F07263F16FFF14A17` |
| SYS assinado | `C4498C9B67643FE7AA75D9C005199D18A004346142606BF7101255B38FF234F9` |
| CAT | `5F0E41F6CB0B8B3600800992A3C668E00DE3F2109E04F6F5DE8BCD4D8C7248E6` |
| INF | `45CDC5697145ED2F5153AA29F867200A781AAE6B3B2F3CF12D3E8621732E3AD1` |
| INF fonte | `TdrInduceHang=0` |
| INF empacotado / manifesto | `TdrInduceHang=1`, `TdrInduceHangArmed=true` |

Auditorias: matriz DDI **PASS**, safety audit **PASS**, manifesto CP **PASS**, `Inf2Cat`
sem erros/avisos, assinatura do SYS/CAT **válida**, package audit **PASS (INTEGRITY ONLY)**.
Gate negativo medido: tentar empacotar armado sem o aceite explícito foi **RECUSADO**.
Reutilizar a versão fonte `3.87.0.0` também foi **RECUSADO antes de mutar o pacote**.

## 4. Única corrida proposta

**Teto:** uma instalação/bind após boot frio. Não repetir o mesmo pacote.  
**Abandono:** watchdog/rollback, BugCheck/reboot, `TDR_RESTART FALHOU`, ou ausência da cadeia
TDR após o timeout — qualquer um encerra a corrida e manda minerar a evidência antes de nova
mudança.

Em PowerShell elevado, partindo de BasicDisplay Código 0, fusível `CLEAN`, watchdog/recovery
prontos e `D3D11CapsProbe` desarmado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\stage-bc250mcdm-package.ps1 -Execute

powershell -NoProfile -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1 `
  -Execute -IAcceptBlackScreenRisk -IAcceptDeliberateHangAndPossibleBugCheck
```

Não execute `arm-tdr-induce-hang.ps1` antes desse primeiro bind: o pacote já nasce armado.

## 5. Teste que fecha

O teste físico continua sendo: `TDR_HANG_INDUZIDO` → `ENGINE_PROBE Responsive=0` →
`McdmResetFromTimeout`/`TDR_QUIESCE` → `TDR_RESTART PROVADO` com fence física sobre poison →
nova submissão aceita → display sobrevivendo. Evidência integral em
`docs/evidence/<timestamp>-*-mcdm-display-binding/`.

Como o backend D3D10/11 ainda não existe, o display sobrevivendo não pode ser atribuído ao
caminho WDDM acelerado nesta corrida. O item 25 só fecha integralmente junto com B7.
**NÃO VALIDADO EM HARDWARE.**
