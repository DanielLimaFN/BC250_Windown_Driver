# docs/255 — Corrida 19:58: desktop normal porque StartDevice recusou HQD residual

**Candidato tentado:** LAB `3.160.1.0`  
**Desfecho visual:** desktop normal no Microsoft Basic Display.  
**C3.17:** não exercitada.

## Cadeia medida

O bind instalou o pacote e iniciou o KMD, mas `StartDevice` devolveu
`0xC00000A3` (`STATUS_DEVICE_NOT_READY`) antes de anunciar fontes de vídeo:

```text
RADV_PRESENT_SKIPPED
shared_primary=false
commit_vidpn=false
set_vidpn_source_address=false
bc250umd.dll nunca escreveu: a DLL nao foi carregada
```

Portanto, permanecer no desktop normal não foi uma apresentação bem-sucedida do nosso
driver. O Windows manteve/restaurou o Basic Display porque o candidato não completou o
StartDevice.

## Causa

A fila MEC/HQD entrou na nova tentativa ainda ativa com os cursores da corrida anterior:

```text
ActiveBefore=1
RptrBefore=216
InitialWptrLo=216
```

O autoteste tentou publicar 45 dwords, mas `RPTR` ficou em zero e terminou como
`HqdActiveStatus=7` (`PM4_NOT_CONSUMED`), `PhysicalReady=0`. A restauração registrou três
falhas. O StartDevice falhou fechado, antes do persistent engine, VidPn, UMD e gate físico.

Isto demonstra que o rollback online anterior restaurou `BasicDisplay`, mas não zerou o
estado físico da HQD. Código 0 no PnP não é prova de fila compute limpa. Um boot frio é
necessário antes de repetir o candidato atual.

## Watchdog

O guardião funcionou novamente: às 20:01 executou recovery, removeu `oem34.inf` e confirmou
`BasicDisplay`, Código 0, em cinco amostras. O prebinding posterior passou limpo.

## Próximo ensaio

Reiniciar a máquina, executar a auditoria somente-leitura e então repetir exatamente o
Marco 2 com `3.160.1.0`. Nenhuma alteração no candidato é justificada por esta tentativa:
o código C3.17 não foi alcançado.

