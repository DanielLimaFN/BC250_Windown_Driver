# DX.83 — captura do travamento sem teclado (offline)

**25/09/2026.** Resposta à Corrida 129 ([análise 459](459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md)) e execução das partes 2 e 3 do [plano 460](460_PLANO_CAPTURA_TRAVAMENTO_SEM_TECLADO_POS129.md). A configuração de WinDbg/KDNET no segundo PC fica com Daniel. **Nenhum bind ou ensaio físico da DX.83 foi feito.**

## Candidato e mudança

Pacote lab assinado **3.288.1.0**, perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, `GfxWaveProbe=1`, demais opções da DX.82 preservadas. Esta revisão adiciona observabilidade; não é uma correção demonstrada do travamento.

| Artefato | SHA-256 |
|---|---|
| SYS | `8FD29E6215B634BCB376BA4ED2ECDDBF03CECEC35B204922C105775C26963BD8` |
| UMD | `85A789344A04B557B2ED1840DA9EBC5B2DDA7749E9912FE5C99C377A15A94F07` |
| INF | `8F8A69E9CD7D2FAEB2676A6889945415B3D922FEDFEA08F7A401E3E3C034886E` |
| CAT | `E31E0A88060289F94966A0CA45E315A0A7F4908EAA9F3F60B8C8AFA0532F7BCD` |
| RADV | `FCAA11FE9DFEF68225A12D3FC455D9F777651E2AD3F31B2E32525A889DD1008C` |

UMD: `BC250_HANG v=1` escreve no log por processo, com PID/TID, QPC, sequência e `span` que emparelha entrada e saída. O gate `HKLM\SOFTWARE\BC250\Umd\HangTrace=1` é ativado apenas pela captura ETW e restaurado ao valor anterior no encerramento. Há pares em `WaitPaging`, espera/aquisição dos locks shared e exclusive, `pfnPresentCb`, shutdown Vulkan, `vkDeviceWaitIdle`, `vkDestroyDevice` e `FreeLibrary`. O log usa o sink write-through já existente. Esta emissão síncrona adiciona latência diagnóstica; medir o efeito na corrida.

KMD: o rastro binário DX.56 permanece no mesmo formato. Novos estágios para `FLUSH_TLB`: `7` handler entrou, `8` retornou, `40/41` espera/aquisição de `ObjectLock`, `42/43` início/fim do flush físico, `44/45` início/fim do commit do journal, `46` liberação do lock. A DDI operação 12 ganha `BC250_HANG` ENTER/LEAVE com ID, thread e QPC no log KMD. Os novos estágios usam o anel sem I/O no caminho quente; a thread DX.56 continua persistindo snapshots. Decodificar `C:\BC250_PAGING_TRACE.bin` com `python tools\decode-bc250-paging-trace.py C:\BC250_PAGING_TRACE.bin --last 20`.
Adicionalmente no KMD:
1. **Dump FIFO de 8 entradas dos headers de CP**: em `Bc250ActiveTdrQuiesceEngine`, leitura sequencial de 8 entradas de `mmCP_ME_HEADER_DUMP` (`0x00008684u`) e `mmCP_PFP_HEADER_DUMP` (`0x00008688u`), registrando tipo PM4 (PKT0/PKT3) e Opcode individual de cada pacote pendente no micro-engine ME e PFP.
2. **Protocolo RLC Safe Mode e Soft-Reset Seguro**: rotinas `Bc250EnterRlcSafeMode` e `Bc250ExitRlcSafeMode` via `RLC_SAFE_MODE` (`0x0003B014u`) com handshake atômico (comando `0x3` para entrar, `0x1` para sair, polling do bit `CMD=0`), isolando o escalonador do RLC durante o soft-reset de GFX/CP (`GRBM_SOFT_RESET`) em TDR quiesce, engine offline e `StopDevice`. Offset `0x0003B014u` adicionado com segurança às allowlists de MMIO topology read/write.

ETW: `tools\bc250-hang-etw.ps1` inicia **antes** do bind dois arquivos circulares: `kernel-scheduler.etl` (threads, trocas de contexto com stacks, dispatcher e disco, limite 256 MiB) e `graphics-dxg-dwm.etl` (DxgKrnl, DWM e processo, limite 128 MiB). O manifesto registra boot, sessões, arquivos e hashes. O script recusa iniciar sem elevação, `xperf` ou providers. Se o ETW falhar, o runner recusa o bind. A captura depende de buffers/disco e pode perder eventos finais quando houver reset forçado; o arquivo recuperado não deve ser tratado como completo.

### Teste adicional: EDID do monitor exposto pelo Windows

Após ler a [referência macOS 462](462_EXTRACAO_TECNICA_REFERENCIA_RDNA1_MACOS.md), acrescentamos uma coleta **somente leitura** antes do bind, durante o bind após o gate de display e após o rollback. `tools\capture-bc250-monitor-edid.ps1` identifica o monitor PnP ativo e seu dispositivo PCI pai, salva os bytes `EDID` do registro, calcula SHA-256 e confere cabeçalho/checksum. Nas fases PreBind e PostRecovery, lê também os blocos da API WMI `WmiGetMonitorRawEEdidV1Block` para comparar com o registro; na fase Bound pula WMI para reduzir impacto. O preflight recusa o bind se não houver ao menos um bloco base válido associado a `PCI\VEN_1002&DEV_13FE`. A falha da coleta Bound fica registrada e não cancela o ensaio principal. Após reset forçado, `bc250-hang-etw.ps1 -Recover` repete a coleta. Os arquivos `edid-PreBind.json`, `edid-PostRecovery.json` e `.bin` ficam na pasta ETW; `edid-Bound.json` e seu `.bin` ficam na pasta do binder.

**Limite de prova:** Windows pode fornecer EDID em cache ou substituído por override. Esta coleta não realiza uma transação DisplayPort AUX nova no nosso KMD e **não fecha o item 77**. O símbolo `AuxChannelProxy::transferI2c` no binário macOS indica a arquitetura, mas não entrega aqui o sequenciamento completo da controladora AUX para a BC-250. Um probe físico por AUX exigiria escritas nos registradores de display, reservadas a P9 pela governança do projeto, e acrescentaria uma variável ao teste do travamento. Na inspeção offline do BasicDisplay, o monitor ativo `DISPLAY\GSM5B98` era filho do PCI da BC-250; o bloco base de 128 bytes tinha cabeçalho/checksum válidos e hash igual pelo registro e WMI. O byte 126 anunciava **uma extensão**, mas ela não estava no valor de registro e a chamada WMI para o bloco 1 retornou erro. Portanto até o EDID completo permanece não demonstrado.

Fontes da API: [WmiGetMonitorRawEEdidV1Block](https://learn.microsoft.com/en-us/windows/win32/wmicoreprov/wmigetmonitorraweedidv1block-wmimonitordescriptormethods) e [override de EDID no Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/overriding-monitor-edids).

## Execução física futura

Após **boot frio** e com o depurador remoto pronto, executar **como Administrador** `tools\EXECUTAR_CORRIDA130_DX83_600S.cmd`. O preflight `tools\prepare-corrida130-dx83.ps1` fixa hashes, auditorias, estado da Radeon Software e configuração de dump. O runner define 600 s, liga ETW antes do binder e para/hasheia ETW após o rollback; o fluxo de recuperação permanece no roteiro existente. Diretório ETW: `docs\evidence\<hora>-DX83-HANG-ETW`. O ponteiro local `C:\BC250_HANG_ETW_LATEST.txt` aponta para o manifesto.

Se o sistema congelar, tentar **Break no host WinDbg** para capturar stacks e locks. Se não responder, registrar a hora e reiniciar pelo botão. No boot seguinte, antes de nova corrida, executar PowerShell elevado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bc250-hang-etw.ps1 -Recover
```

`-Recover` restaura `HangTrace`, registra tamanho/hash dos ETL sobreviventes, copia/hasheia `C:\BC250_PAGING_TRACE.bin` quando existir, repete a coleta EDID e marca `RECOVERED_AFTER_RESET`. Não para sessão de outro boot. Preservar também logs UMD em `%TEMP%` e `C:\Windows\Temp`, log KMD e evidência do binder. O watchdog só pode restaurar BasicDisplay após o reset, como na 129.

## Leitura causal e veredito

Ordenar os novos eventos textuais UMD/KMD por QPC dentro do mesmo boot e conferir os pares por `span`/ID. O rastro binário DX.56 usa horário de sistema em unidades de 100 ns; alinhá-lo por esse relógio e pela sequência, depois cruzar com ETW e pilhas do depurador. Distinguir: (A) lock/`vkDeviceWaitIdle` parado; (B) paging `FLUSH_TLB`, flush físico ou journal parado; (C) `pfnPresentCb` sem completion enquanto o KMD avança; (D) CPU/disco/PCIe parados mais amplamente. A última linha isolada de qualquer arquivo não prova causa. A ausência de LEAVE só localiza uma espera se houver batimento posterior confiável ou stack independente. Se os ETLs forem truncados, registrar a lacuna.

**Critério da Corrida 130:** primeiro, obter captura utilizável que diferencie essas dependências; segundo, medir desktop, harness e rollback contra a 129. Sucesso diagnóstico não equivale a 600 s estáveis. Não declarar correção funcional, 60 FPS ou jogos a partir deste pacote offline.

## Verificações offline

`build-umd.bat` PASS, `build-bc250mcdm-lab-bringup.ps1` PASS, auditoria de segurança PASS, auditoria do pacote lab PASS, decoder DX.56 PASS e preflight DX.83 PASS. O capturador EDID passou em BasicDisplay para a fase PreBind e em simulação de PostRecovery no mesmo boot; isso não valida a fase após uma corrida. Providers DxgKrnl, DWM e Kernel-Process existem nesta instalação; `xperf` e `logman` foram encontrados. **Start/Stop ETW elevados, integridade de ETL após reset, fase EDID física e instrumentação no silício continuam NÃO VALIDADOS EM HARDWARE.**
