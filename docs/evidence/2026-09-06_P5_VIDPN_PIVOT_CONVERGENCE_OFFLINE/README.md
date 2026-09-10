# P5_VIDPN_PIVOT_CONVERGENCE — evidência offline

Data local: 2026-09-06. Esta pasta não contém prova física.

## Defeito coberto

`Bc250DxgkDdiEnumVidPnCofuncModality` alterava os campos de suporte de scaling e
rotation mesmo quando o respectivo campo era o pivô da enumeração. O candidato preserva
o campo-pivô e sanitiza somente o campo mutável.

## Testes executados

```text
driver\wdk\build-contract.bat                       PASS
driver\wdk\output\display_vidpn_test.exe           exit 0
python tools\audit-bc250mcdm-safety.py              PASS
tools\audit-bc250mcdm-package.ps1                   PASS (integrity only)
tools\audit-bc250mcdm-lab-package.ps1               PASS
PowerShell parser (4 scripts modificados)            PASS
```

O teste unitário inclui pivôs `D3DKMDT_EPT_SCALING` e
`D3DKMDT_EPT_ROTATION`: o campo associado ao pivô mantém bits sentinela, enquanto o
outro termina apenas com suporte Identity/Offset0 permitido.

## Artefatos auditados

```text
active SYS  2FA639F0AFF30BC6FAE35C12DE8E5C942F3ED6157C651DBE4B077BB48D8BBCDE
active INF  6BF6B13D753373BB1E0D6414A9B2949D3A29C73EE02B2D26BC7DFB45E6B4F3B7
active CAT  7873CCD1A58D361AAA8A7930EC933835AE7B2C20B126C0FF4BD05F603B0DE4EC
lab SYS     DB12B08C63FBF9843FF45FCE5EE2799494730C269BCBE7F0A9C33C511ACBBFCB
lab INF     1B8E94B608149A2568D26B3B285FB8F023176491904D9F373FE9B847F76C368D
lab CAT     42A70E5C7E50E9B8F95694615671665E5DD838D1D8697B0D633F652E942AD78C
```

Os artefatos acima foram recompilados em 07/09 com VS2026/MSVC 19.51 e SDK/WDK
10.0.28000.0. O histórico e os manifests estão registrados em
`docs/173_TOOLCHAIN_VS2026_WDK28000.md`. Esta atualização substitui somente os binários
offline; o teste físico continua **NÃO VALIDADO EM HARDWARE**.

## Teste físico que pode fechar o item

`P5_VIDPN_PIVOT_CONVERGENCE`: no log correspondente exclusivamente ao novo bind,
observar em sequência `GetStandardAllocationDriverData(SharedPrimary)`, `CommitVidPn` e
`SetVidPnSourceAddress`. A primeira corrida deve usar o pacote ativo fail-closed e não
executar RADV. Se o ciclo continuar sem commit, a hipótese de que a violação de pivô era
a causa suficiente é rejeitada.

Resultado físico: **NÃO VALIDADO EM HARDWARE**.
