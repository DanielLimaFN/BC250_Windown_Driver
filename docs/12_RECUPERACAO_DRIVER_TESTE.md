# Recuperacao do driver experimental BC-250

## Objetivo

`C:\BC250_RECOVERY.cmd` permite retirar os drivers experimentais do boot caso
um futuro ensaio do miniport deixe a tela preta. A copia versionada fica em
`tools\BC250_RECOVERY.cmd`.

Para colocar uma copia identica na raiz, abra CMD ou PowerShell como
Administrador na raiz do repositorio e execute uma vez:

```cmd
tools\INSTALL_BC250_RECOVERY.cmd
```

O instalador compara os dois arquivos byte a byte e executa o `DRY-RUN`.

O script trata exclusivamente os servicos e binarios `bc250readonly`,
`bc250mcdm` e `bc250dbg`. Para cobrir pacotes orfaos, ele percorre somente
`oem*.inf` e aceita um pacote para remocao apenas quando quatro identidades
aparecem juntas no conteudo: classe `ComputeAccelerator`, hardware ID
`PCI\VEN_1002&DEV_13FE` e o par exato servico/binario `bc250readonly` ou
`bc250mcdm`. Ele nao remove drivers AMD, `BasicDisplay` ou o legado `bc250v4`.

Em 2026-08-25, a copia instalada em `C:\` ainda diverge da fonte e nao cobre
`bc250mcdm`. Consulte `docs\21_AUDITORIA_RECOVERY_MCDM_2026-08-25.md`. Nenhum
pacote ativo pode ser construido ou instalado enquanto o auditor read-only
retornar bloqueio.

## Auditoria sem alteracoes

No Prompt de Comando:

```cmd
C:\BC250_RECOVERY.cmd /DRY-RUN
```

## Recuperacao apos tela preta

1. Entre no Ambiente de Recuperacao do Windows.
2. Abra **Opcoes avancadas > Configuracoes de Inicializacao**.
3. Inicie o **Modo de Seguranca com Prompt de Comando**.
4. Abra/continue no prompt elevado e execute:

```cmd
C:\BC250_RECOVERY.cmd
```

5. Reinicie o Windows.

A execucao real e recusada fora do Modo de Seguranca. Os `.sys` encontrados
sao movidos, e nao apagados, para `C:\BC250_DRIVER_QUARANTINE`. O registro da
execucao fica em `C:\BC250_RECOVERY.log`.

### Correcao apos o ensaio 0.2.1.0

O pacote PnP deixou `bc250readonly.sys` com proprietario `TrustedInstaller`.
Mesmo no Modo de Seguranca, o primeiro `move` recebeu acesso negado. A versao
inicial do recuperador apenas registrava o aviso e encerrava com sucesso, o que
deixou um binario orfao em `System32\drivers`.

O recuperador agora, somente depois de o primeiro isolamento falhar:

1. assume propriedade do arquivo experimental de nome exato;
2. concede controle aos Administradores usando o SID conhecido;
3. tenta novamente mover o arquivo para a quarentena;
4. confirma que a origem desapareceu;
5. retorna erro global se pacote, servico ou qualquer binario nao puder ser
   tratado.

Nenhuma busca ampla, curinga ou remocao de driver AMD foi adicionada.

Depois do ensaio 0.3.0, o pacote e o binario foram removidos, mas o marcador
ficou orfao. O recuperador agora verifica explicitamente a exclusao do marcador
e, quando o `oemNN.inf` exato ja nao existe em `Windows\INF`, remove somente o
marcador sem tentar inferir ou apagar outro pacote.

### Correcao apos o ensaio 0.4.0.0

O recovery removeu servico, binario e marcador, mas `oem16.inf` permaneceu
associado ao dispositivo. Depois do pre-stage de `oem17.inf`, o Windows mostrou
o binding antigo em Codigo 19 (`CM_PROB_REGISTRY`), enquanto o marcador podia
representar apenas o pacote novo. Assim, confiar exclusivamente no marcador
nao cobria varias geracoes do pacote readonly.

O recuperador agora enumera e mostra no `DRY-RUN` todos os `oem*.inf` que
passam a identidade quadrupla acima. Na execucao em Modo de Seguranca, remove
cada um por nome publicado, falha globalmente se qualquer remocao falhar e so
entao apaga o marcador. No estado observado, a auditoria selecionou exatamente
`oem16.inf` e `oem17.inf`; `display.inf` nao entra na enumeracao.

## Contrato para o futuro instalador

O instalador grava em
`C:\BC250_TEST_OEMINF.txt` somente o nome publicado retornado pelo Windows,
por exemplo `oem52.inf`. O marcador continua sendo auditado, mas a recuperacao
nao depende dele para localizar geracoes orfas do mesmo pacote com identidade
estrita.
como prova de funcionamento.

**NAO VALIDADO EM HARDWARE**
