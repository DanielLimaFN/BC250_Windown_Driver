# Handoff 197 — candidato 3.90.1 para o tripé Node 1 da UMD

**Data:** 08/09/2026  
**Trabalho:** build, assinatura e auditoria offline  
**Instalações/binds/corridas físicas:** 0  
**Veredito:** **NÃO VALIDADO EM HARDWARE**

## Resultado

Foi produzido o primeiro pacote lab que contém a UMD com a transação
`B7_UMD_NODE1_TRIPOD`. O pacote está em `driver/wdk/package/output_lab`, não foi instalado e
nenhum executável físico foi iniciado.

| Campo | Valor |
|---|---|
| DriverVer | `09/08/2026,3.90.1.0` |
| Perfil | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| Contrato | WDDM 2.0 (`WddmContractVersion=20`) |
| Shader / submissão persistente | `1 / 1` |
| `GpuvaLabPromotionEnabled` | `false` |
| `Node1TripodDefaultArmed` | `false` |
| `D3D10_11NativeReady` | `false` |
| Signer | `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B` |

## Identidade auditada

| Artefato | SHA-256 |
|---|---|
| SYS fonte lab | `9906712CB6ACA231BC0565CE6BB7C9751AA65606549DFE75F9B907289A0B2FA0` |
| SYS assinado | `7CDA9481D760CCD44531A91171F57E7418EAE408BF3CF6E7329EA7ECD7092EC9` |
| UMD fonte | `6BFCCC20022BA15D4D6B7C255052F65C30AF740C339B1ECADD601CC80C6EDB19` |
| UMD assinada | `2927FCBCE19AA1BE9A345D8798468E1EA8AE10DF2993428793F87F8BE88F3371` |
| INF | `B7744F012338AEBA535F959E0B00225A016A76A9A0B4D9BA81BEDEDEFB442C97` |
| CAT | `3AD34B0746B745436F736A5CA37FE9127E7A822225B6C5F0917731032BFE28E2` |

O manifesto agora registra separadamente os hashes da UMD fonte e da cópia assinada. O
runner usa `PackageUmdSha256` como raiz de identidade e exige que a DLL instalada seja
idêntica à UMD assinada do pacote; comparar contra a DLL fonte seria incorreto porque a
assinatura altera o arquivo.

## Gates executados

| Gate | Resultado |
|---|---:|
| `driver/umd/build-umd.bat` | PASS |
| `tools/audit-umd-node1-submission.py` | PASS, incluindo duas mutações |
| `driver/wdk/build-contract.bat` pela reconstrução lab | PASS |
| `tools/audit-bc250mcdm-safety.py` | PASS |
| `tools/audit-bc250mcdm-lab-package.ps1` | PASS |
| `tools/test-b7-umd-node1-tripod.ps1` sem `-Execute` | DRY-RUN PASS; GPU e registro intocados |
| `Inf2Cat` | zero erros, zero avisos |
| Authenticode SYS/UMD/CAT | válido e signer idêntico ao manifesto |

## Próxima etapa — decisão do dono

O doc 198 integrou o runner ao binder para que a tela preta não exija uma segunda janela.
A próxima ação já é física: após boot frio, executar uma única vez o bind protegido com
`-RunUmdNode1Tripod`; o binder fornece internamente o diretório de evidência e o aceite ao
runner.

Critério de aprovação: autorização e poison pré-submit; submit Node 1 aceito; fence exata
gravada pela GPU; cinco canários conferidos byte a byte; cleanup; retirement; anel drenado
com `RPTR==WPTR`; zero rejeições. Qualquer preflight inválido ou primeira falha encerra o
ensaio. Evidência: `docs/evidence/<data>_B7_UMD_NODE1_TRIPOD/`.

Até essa corrida: **NÃO VALIDADO EM HARDWARE**.
