# Gate de pre-binding MCDM 1.0.1.0

Data: 2026-08-25.

## Veredito atual

**GATE LIMPO APROVADO. STAGE/BINDING NAO EXECUTADOS. NAO VALIDADO EM HARDWARE.**

O gate foi executado somente em leitura. Nenhum stage, instalacao ou binding
foi realizado.

## Estado observado

- uma BC-250 presente;
- servico ligado ao dispositivo: `BasicDisplay`;
- `ConfigManagerErrorCode=0`;
- `bc250readonly`, `bc250dbg` e `bc250v4`: ausentes;
- `bc250mcdm`: ausente;
- binarios `bc250*.sys` experimentais: ausentes de `System32\drivers`;
- pacotes OEM BC-250 experimentais: nenhum;
- marcador `C:\BC250_TEST_OEMINF.txt`: ausente;
- reboot pendente pelos gates auditados: nenhum;
- recovery e pacote 1.0.1.0: integridade aprovada.

## Gate automatizado

`tools\audit-bc250mcdm-prebinding.ps1` exige simultaneamente:

1. recovery instalado integro;
2. pacote assinado e manifesto integro;
3. exatamente uma BC-250 em `BasicDisplay`, Codigo 0;
4. nenhum servico ou binario experimental;
5. nenhum INF experimental no Driver Store;
6. marcador OEM ausente;
7. nenhum reboot pendente conhecido.

Depois da recuperacao executada pelo usuario e do reboot, o gate retornou exit
0. O SCM confirmou `bc250mcdm` ausente. O preflight do stage tambem retornou
exit 0 e uma verificacao posterior confirmou marcador ausente e zero INF MCDM
no Driver Store, comprovando que nenhuma escrita ocorreu.

## Stage separado

`tools\stage-bc250mcdm-package.ps1`:

- chama obrigatoriamente o gate acima;
- sem `-Execute`, faz zero escrita;
- com `-Execute`, usa somente `pnputil /add-driver`;
- nao possui `/install`, force-binding ou chamada a `newdev.dll`;
- grava o nome `oemNN.inf` somente depois de cardinalidade unica;
- nao pode ser executado enquanto o servico orfao existir.

A auditoria fail-closed do driver agora tambem impede regressao que introduza
binding no script de stage.
