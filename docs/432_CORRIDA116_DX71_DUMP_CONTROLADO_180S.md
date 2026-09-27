# Corrida 116: repetir DX.71 com captura de dump se houver travamento

> Preparada em 25/09/2026, **sem bind**. Base: [Corrida 115](431_CORRIDA115_DX71_JANELA_CORRIGIDA_TRAVAMENTO_E_RASTRO.md). Mesmo pacote lab assinado 3.275.1.0, UMD `884A0A60CB5B1AE3C55D849A72491CC961C64EF268285EB248FDE0E2DAB29AC7`, SYS `A4809EEA5341951A2F6E061AA21F9B3A37B3C0CD5C996FAD42B863E1E1DC3A7A`, mesmo RADV. Nenhuma mudanca de KMD, UMD, INF ou perfil desde a 115.

## Por que uma repeticao pode informar

A Corrida 115 corrigiu a imagem duplicada durante a janela observada, mas travou em ~2 min 19 s e nao produziu BugCheck/dump. A ultima amostra `FLUSH_TLB` fase 5 ocorreu em 933/1296 amostras, logo nao discrimina a causa. Os logs nao demonstram que a DX.71 aumentou a quantidade de copias finais e causou o travamento: 114 e 115 tiveram duracoes diferentes e o KMD amostra algumas mensagens. Uma nova corrida **sem captura adicional** seria repeticao cega. A maquina ja esta configurada com `CrashDumpEnabled=2`, `AlwaysKeepMemoryDump=1` e `CrashOnCtrlScroll=1` em `i8042prt` e `kbdhid`. A combinacao de teclas pode gerar um BugCheck **manual 0xE2** e dump de kernel se a pilha de teclado continuar viva durante o congelamento; isso nao e' BugCheck espontaneo do driver. O dump pode revelar as pilhas e locks das CPUs naquele instante, mas nao garante causa unica.

O `C:\Windows\MEMORY.DMP` anterior tem 887.730.418 bytes e LastWriteUtc `2026-09-24T16:56:30.5050077Z` (Corrida 103, antigo). Preflight somente leitura `tools/prepare-corrida116-dx71-dump.ps1`: PASS em 25/09; pacote, auditorias e RADV conferidos; chaves de dump conferidas; C: com 194 GiB livres. Runner `tools/EXECUTAR_CORRIDA116_DX71_DUMP_180S.cmd` usa o binder existente com prazo 180 s. `CLEAN_BOOT_REQUIRED` da 115 segue ativo: **boot frio com corte de energia antes do bind**. O runner recusa mesmo boot; nao usar `-Force`.

## Procedimento e criterio previo

1. Apos boot frio, executar o runner **elevado**. Observar se o triangulo permanece somente dentro da janela e anotar hora aproximada de qualquer parada.
2. **Se travar e nao houver BugCheck automatico:** segurar **Ctrl direito** e pressionar **Scroll Lock duas vezes**. Se aparecer tela azul, aguardar a gravacao do dump e o reinicio. Se nao responder, registrar "atalho sem resposta" e so entao reiniciar pelo botao, sem cortar energia.
3. Apos voltar, coletar `MEMORY.DMP` novo antes de outra corrida, conferir hora/tamanho/evento 1001 e analisar com `cdb` elevado. O evento 0xE2 sera identificado como inducao de coleta, nao como falha espontanea. Recuperar logs UMD/KMD e rastro DX.56; comparar boot ticks do guardiao para classificar R1.

| Desfecho | Condicoes | Conclusao permitida |
|---|---|---|
| **A** | Janela completa de 180 s, visual sem duplicacao, 60/60 Present, DWM >=2,60 Presents/s nos primeiros 120 s e BasicDisplay/0 em cinco amostras no mesmo boot | Sucesso **curto** reproduzido; travamento intermitente e 600 s seguem abertos. |
| **B** | Travamento, atalho responde, dump de kernel **novo** arquivado e analisavel | Falha funcional, mas avanco diagnostico. Investigar stacks antes de alterar o driver. |
| **E** | Travamento e atalho nao responde, sem dump; BugCheck espontaneo, imagem duplicada ou rollback apenas em boot posterior | Falha literal. Nao repetir cegamente; considerar depurador externo ou outra captura independente. |

O guardiao agendado nao consegue executar enquanto o Windows inteiro estiver travado. O atalho tambem pode falhar se a pilha de teclado ou o kernel estiverem parados. Nao anunciar estabilidade com uma unica janela de 180 s nem chamar recuperacao do BasicDisplay no novo boot de R1.
