# Auditoria read-only do recovery MCDM - 2026-08-25

## Veredito

**BLOQUEADO. NAO VALIDADO EM HARDWARE.**

Nenhum arquivo, servico, pacote PnP ou binding foi alterado nesta auditoria.

## Evidencia observada

- fonte `tools\BC250_RECOVERY.cmd`:
  `2B0367F2C5501609841C0768B6B78F67143D2E336A48A898F29B2F2D88F61ADD`;
- copia `C:\BC250_RECOVERY.cmd`:
  `40FA0076A05D3A095DE43ED9DD07F3785E710163B83F9B56B0DB6EF6FBEB8FD7`;
- os arquivos divergem;
- `bc250mcdm` esta presente no Service Control Manager;
- `bc250mcdm.sys` esta ausente de `System32\drivers`;
- `oem18.inf` satisfaz a identidade estrita do pacote MCDM;
- o `/DRY-RUN` da fonte detecta o servico e `oem18.inf`;
- o `/DRY-RUN` instalado nao mostra `bc250mcdm` e informa incorretamente que
  nao ha pacote BC250 readonly correspondente.

## Diferenca de cobertura

A fonte versionada inclui:

- remocao exata do servico `bc250mcdm`;
- quarentena exata de `bc250mcdm.sys`;
- exibicao do servico e binario no `DRY-RUN`;
- reconhecimento de INF somente pelo conjunto classe `ComputeAccelerator`,
  hardware ID `PCI\VEN_1002&DEV_13FE`, servico `bc250mcdm` e binario
  `bc250mcdm.sys`.

A copia instalada nao possui nenhuma dessas quatro coberturas. Por isso,
`tools\build-bc250mcdm-package.ps1` deve continuar recusando a geracao do
pacote.

## Gate automatizado

Execute sem elevacao:

```powershell
powershell -ExecutionPolicy Bypass -File tools\audit-bc250-recovery-readiness.ps1
```

O auditor e somente leitura e deve retornar exit 1 enquanto a copia instalada
divergir ou nao cobrir `bc250mcdm`.

O auditor foi ligado ao inicio de `tools\build-bc250mcdm-package.ps1`, antes
de qualquer mutacao de `output_active`. Uma tentativa controlada retornou exit
1 e o manifesto SHA-256 do pacote permaneceu integralmente inalterado:

- `bc250mcdm.cat`:
  `83C8B743961DAED837BBC2758A49EF21A0DEE09F5369DFC0EF89FD32E7FA67AD`;
- `bc250mcdm.inf`:
  `6C08F4E8B4B8BC02504C701758729C33ED1F12BB005E109E4FADF6613C95C0E5`;
- `bc250mcdm.sys`:
  `3F74BB8F547BBDD8488FCB61F09F89969F384493339A064DBD8E2DB91890B738`.

## Acao futura que exige autorizacao

Somente apos autorizacao explicita, executar elevado:

```cmd
tools\INSTALL_BC250_RECOVERY.cmd
```

O instalador copia a fonte, compara byte a byte e executa o `/DRY-RUN`. Depois
disso, o auditor acima precisa retornar exit 0 antes de qualquer build, stage
ou binding do pacote ativo.

## Tentativa autorizada sem elevacao

O usuario autorizou a sincronizacao, mas o processo desta tarefa nao possui
token administrativo:

- tentativa de backup em `C:\` recusada com acesso negado;
- copia anterior preservada em
  `docs\evidence\2026-08-25_recovery-sync-preflight\`;
- `INSTALL_BC250_RECOVERY.cmd` retornou exit 2 antes de copiar;
- hash de `C:\BC250_RECOVERY.cmd` permaneceu
  `40FA0076A05D3A095DE43ED9DD07F3785E710163B83F9B56B0DB6EF6FBEB8FD7`;
- nenhum servico, INF, driver ou binding foi alterado.

A sincronizacao precisa ser executada pelo usuario em um terminal elevado.
