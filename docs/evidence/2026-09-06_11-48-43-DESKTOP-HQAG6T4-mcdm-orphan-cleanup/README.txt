BC250 PREBINDING ORPHAN CLEANUP -- TRANSACAO ABORTADA E REVERTIDA
Data: 2026-09-06 11:48:43 (local)

Este diretorio esta vazio de proposito: a transacao abortou antes de produzir
qualquer artefato, e o rollback devolveu o sistema ao estado anterior.

O QUE ACONTECEU

O preflight passou com o estado real da maquina:

  device: BasicDisplay / 0
  service: Absent; service key present: False
  matching MCDM packages: 0
  marker: present; bytes: 11; valid OEM name: True
  orphan SYS: present; bytes: 372000
  orphan SYS sha256: 32839AD098A77E766112714CAF13478828FED4CF581560B4932B0DAEFBF14386
  orphan SYS last write (UTC): 2026-09-06T01:16:39.7584894Z
  last boot (UTC):             2026-09-06T13:24:22.5000000Z
  pending rename for SYS: none

O marcador C:\BC250_TEST_OEMINF.txt foi movido para quarentena; em seguida o
Move-Item de C:\Windows\System32\drivers\bc250mcdm.sys falhou com "Acesso
negado ao caminho", mesmo em PowerShell elevado. O rollback restaurou o
marcador e a execucao terminou com exit 1. Nada ficou na quarentena.

CAUSA MEDIDA

A ACL do binario nao concede DELETE a Administradores. Entradas presentes:

  AUTORIDADE NT\SISTEMA                     FullControl
  NT SERVICE\TrustedInstaller               FullControl
  BUILTIN\Usuarios                          ReadAndExecute, Synchronize
  ...\TODOS OS PACOTES DE APLICATIVOS       ReadAndExecute, Synchronize

Nao existe ACE de Administradores. Probe direto de FileSystemRights::Delete
nesta sessao elevada: DENIED_BY_ACL. Somente SYSTEM ou TrustedInstaller podem
mover ou apagar o arquivo.

CORRECAO APLICADA

tools\cleanup-bc250mcdm-prebinding-residue.ps1 passou a fazer esse probe de
DELETE no PREFLIGHT e a recusar antes de mover o marcador. Uma transacao que
nao pode terminar nao deve comecar; o rollback funcionou, mas depender dele
era o defeito.

DECISAO

Nao tomar posse do arquivo nem alterar a ACL. O binario orfao nao bloqueia a
trilha de laboratorio -- o proprio install o sobrescreve. Para a trilha
fail-closed, a remocao deve ser AGENDADA ao Windows
(PendingFileRenameOperations), que a executa como SYSTEM no boot seguinte.

NAO VALIDADO EM HARDWARE.
