# docs/256 — Corrida 20:52: StartDevice recusou HQD travada pelo TDR da corrida anterior

**Candidato tentado:** LAB 3.161.1.0  
**Desfecho visual:** desktop normal no Microsoft Basic Display.  
**C3.18:** não exercitada (StartDevice falhou fechado antes de tomar o display).

## Cadeia medida

O script EXECUTAR_MARCO3.cmd executou a vinculação do pacote 3.161.1.0, mas o StartDevice devolveu STATUS_DEVICE_NOT_READY antes de anunciar fontes de vídeo:

`	ext
RADV_PRESENT_SKIPPED
shared_primary=false
commit_vidpn=false
set_vidpn_source_address=false
`

O Windows manteve o monitor no Microsoft Basic Display (Código 0). Em seguida, o rollback automático de segurança restaurou o ambiente de forma limpa.

## Causa física medida

Na corrida das 20:16 (3.160.1.0), o Node 1/MEC executou 140 quadros sucessivos antes de sofrer o TDR gerado pelo timeout assíncrono do SDMA no quadro 139. O TDR e a recuperação online do watchdog restauraram o serviço PnP para BasicDisplay, mas **não reinicializaram o microengine físico do MEC na APU**.

A telemetria do StartDevice da corrida 20:52 registrou:
`	ext
HqdActiveBefore=0
HqdActiveWptrAfterKick=45
HqdActiveRptrAfter=0
HqdActiveStatus=7 (PM4_NOT_CONSUMED)
HqdActivePhysicalReady=0
HqdActivePollIterations=50000
HqdActiveMecPcFirst=6142
HqdActiveMecPcLast=6143
HqdActiveCpStalledStat1After=3072
`

O microcódigo do MEC permaneceu em loop travado (PC 6142..6143) desde o TDR das 20:16. O autoteste do StartDevice detectou a falta de resposta do hardware e falhou fechado por segurança, impedindo que o driver tomasse o controle do display com um motor de computação sem resposta.

Um reload de software do driver não consegue resetar os microengines internos da APU quando travados por TDR; um reboot (shutdown /r /t 0) é indispensável para reiniciar o hardware limpo.

## Estado atual do sistema

1. **Recuperação:** Concluída com sucesso (BasicDisplay, Código 0 confirmado).
2. **Fusível:** C:\BC250_BOOT_FUSE.dat limpo (CLEAN).
3. **Marcador de segurança:** C:\BC250_REQUIRE_CLEAN_BOOT.json gravado.
4. **Pacote LAB 3.161.1.0:** Totalmente compilado, assinado e auditado (PASS em todas as auditorias).

## Próximo passo

1. Reiniciar o computador (shutdown /r /t 0).
2. Após o boot, abrir o terminal como Administrador e rodar:
   `cmd
   tools\EXECUTAR_MARCO3.cmd
   `
