# 173 — Migração da toolchain para VS2026 + SDK/WDK 28000

## Decisão

A cadeia canônica de compilação do BC-250 passou de Visual Studio 2022 com WDK 26100
para Visual Studio 2026 com o par SDK/WDK 28000. Os componentes antigos não foram
desinstalados e permanecem disponíveis para rollback, mas os builds canônicos recusam
herdar um `cl.exe` antigo pelo `PATH`.

## Ambiente instalado

```text
Visual Studio       Community 2026 18.9.1
MSVC cl.exe         19.51.36256.0 (toolset 14.51.36231)
Windows SDK         10.0.28000.2526
Windows WDK         10.0.28000.2526
layout Include/Lib  10.0.28000.0
```

O SDK e o WDK foram instalados pelo `winget` com os IDs oficiais
`Microsoft.WindowsSDK.10.0.28000` e `Microsoft.WindowsWDK.10.0.28000`.

## Alterações realizadas

- `driver/wdk/build-contract.bat` chama explicitamente o `vcvars64.bat` do VS2026 e
  fixa `WDKVER=10.0.28000.0`;
- `driver/umd/build-umd.bat`, build lab, RADV gate, ferramenta de diagnóstico, Studio e
  testes isolados foram direcionados ao VS2026;
- empacotadores priorizam `Inf2Cat` e `SignTool` do WDK 28000 e recusam gerar o pacote
  canônico se outra toolchain for efetivamente escolhida;
- manifestos ativo e lab registram VS, caminho/versão do compilador, kit, Inf2Cat e
  SignTool;
- a auditoria estática impede reintroduzir fallback VS2022 nos builds KMD/UMD;
- o pacote lab agora deriva `3.81.1.0` da versão ativa `3.81.0.0`, evitando que o PnP
  prefira o pacote ativo mais novo sobre o candidato lab.

## Validação offline

Passaram com a nova cadeia:

```text
driver/wdk/build-contract.bat
driver/umd/build-umd.bat
tools/build-bc250mcdm-lab-bringup.ps1
tools/build-bc250compute-contracts-isolated.cmd
tools/build-radv-compute-gate.bat
tools/compile-test-cmos-vram.bat
dbg/build.bat
studio/build.bat
tools/audit-bc250mcdm-safety.py
tools/audit-bc250mcdm-package.ps1
tools/audit-bc250mcdm-lab-package.ps1
```

O primeiro build foi corretamente bloqueado porque o patch introduziu finais de linha
mistos nos `.bat/.cmd`. Eles foram normalizados para CRLF e a suíte completa passou na
reexecução.

## Artefatos finais

```text
ativo  DriverVer 3.81.0.0
ativo  SYS 2FA639F0AFF30BC6FAE35C12DE8E5C942F3ED6157C651DBE4B077BB48D8BBCDE
ativo  INF 6BF6B13D753373BB1E0D6414A9B2949D3A29C73EE02B2D26BC7DFB45E6B4F3B7
ativo  CAT 7873CCD1A58D361AAA8A7930EC933835AE7B2C20B126C0FF4BD05F603B0DE4EC

lab    DriverVer 3.81.1.0 / WDDM 2.0 / DISPLAY_RENDER_LAB
lab    SYS DB12B08C63FBF9843FF45FCE5EE2799494730C269BCBE7F0A9C33C511ACBBFCB
lab    INF 1B8E94B608149A2568D26B3B285FB8F023176491904D9F373FE9B847F76C368D
lab    CAT 42A70E5C7E50E9B8F95694615671665E5DD838D1D8697B0D633F652E942AD78C
```

## Limite da conclusão

A migração comprova apenas que fontes, contratos, testes, assinatura e empacotamento são
compatíveis com VS2026/WDK28000. Ela não comprova que o novo binário converge VidPn,
apresenta imagem ou submete Node1. Efeito da nova toolchain no silício:
**NÃO VALIDADO EM HARDWARE**.

O próximo teste físico continua sendo `P5_VIDPN_PIVOT_CONVERGENCE`, sem RADV, com teto
de uma corrida e evidência em `docs/evidence/`.
