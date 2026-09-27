# 460 — Como identificar o travamento sem depender do teclado

**25/09/2026 · após a Corrida 129 DX.82.** Este documento registra a proposta original. As partes 2 e 3 foram implementadas em escopo diagnóstico no [DX.83 offline e critério da Corrida 130](461_DX83_RASTRO_TRAVAMENTO_ETW_E_CRITERIO_CORRIDA130.md); os demais itens aqui continuam como possibilidades, não como entregas. Base medida: [análise 459](459_CORRIDA129_DX82_TRAVAMENTO_PRECOCE_ANALISE.md). A 129 deixou DWM em `VkBridgeShutdown`, harness em `pfnPresentCb`, Explorer em `WaitPaging` da fence 7004 e um `BuildPagingBuffer` pendente; a ordem observada não identifica o primeiro bloqueio causal.

## 1. Fonte independente do Windows travado: depurador remoto

**Primeira opção:** segundo PC com WinDbg conectado *antes* do bind por KDNET, através de NIC suportada. Reproduzir uma sessão de teste em BasicDisplay para provar que o host consegue interromper o alvo, ler pilhas e retomar. Na corrida real, se a tela congelar, usar o **Break no PC host** para capturar pilhas de CPU e threads, estado de locks, VidSch/VidMm, IRQL/DPC e filas do driver. Salvar a sessão e um dump pelo host se o transporte responder. O teclado, shell e disco do alvo deixam de ser o gatilho. **Limitação:** break-in ainda depende de CPU/transporte de depuração vivos; se barramento/interrupts estiverem travados, pode não responder. A presença de `Microsoft Kernel Debug Network Adapter` não prova que o transporte funciona: nesta inspeção, o Realtek físico apareceu `Not Present/Error`. Verificar NIC/cabo/suporte com `kdnet.exe`, sem publicar a chave de conexão em logs do repositório.

**Segunda opção condicional:** NMI físico da placa-mãe/BMC (se existir e for suportado). Ele não depende de teclado; pode provocar bugcheck e dump. O botão de *reset* comum **não** é NMI. No Windows 10, a chave histórica `NMICrashDump` não é necessária. Testar NMI e gravação do dump previamente, em ambiente controlado, antes de acreditar que salvará a corrida. Se a pilha de armazenamento estiver travada, até um NMI pode deixar de produzir arquivo.

**Estado atual de dump:** `CrashDumpEnabled=2` (kernel), `AlwaysKeepMemoryDump=1`, pagefile C: ~15.872 MiB e `CrashOnCtrlScroll=1` em `kbdhid`/`i8042prt`. `MEMORY.DMP` ainda é de 24/09. O atalho já estava armado; seu não funcionamento na 129 indica que ele não pode ser a estratégia principal. O script [arm-crash-dump-on-hang.ps1](../tools/arm-crash-dump-on-hang.ps1) diz que teclado é a "única forma prática sem hardware extra"; essa frase é inadequada como plano de diagnóstico, pois não cobre o congelamento observado.

## 2. Rastro pré-travamento que sobreviva a reset

**Estender DX.56, preservando o registro v2 e criando v3/arquivo separado:** a thread PASSIVE já grava com `FILE_WRITE_THROUGH` e `ZwFlushBuffersFile` a cada 100 ms. Na 129 ela ficou ativa ~12,3 s depois da última mudança de paginação. Usar esse espaço temporal para snapshots compactos das *dependências*, não mais uma cópia da última linha textual.

Por bind, gerar `run_id` e carimbo de tempo comum. Cada evento deve ter `event_id` monotônico, fase ENTER/LEAVE, PID/TID quando aplicável, CPU/IRQL, contexto/engine, `hAllocation` ou token de recurso e ID da fila de paginação. Registrar exatamente:

| Fronteira | Campos que respondem à pergunta causal |
|---|---|
| UMD `CreateResource` | device e thread; lock shared: tentativa/adquirido/liberado; callback `Allocate`, `MapAllocation`, `MakeResident`; `WaitPaging` target/current/queue e duração; `vkAllocateMemory` entrada/saída. |
| UMD teardown | device e thread; `TeardownActive` antes/depois; lock exclusive: tentativa/adquirido/liberado; `vkDeviceWaitIdle` entrada/saída e tempo; cada fase de destruição até `FreeLibrary`. Na 129 só existe `BridgeShutdown ENTER`, logo não se sabe qual fase bloqueou. |
| UMD `Present` | surface/allocation/context e thread; `pfnPresentCb` entrada/saída, resultado, fila/fence e duração. Na 129 falta o retorno do primeiro callback. |
| KMD paginação | operação, instância, thread, fase, `STATUS_PENDING`/retorno; `FLUSH_TLB` com VMID, request/ack e tempo; ligação à alocação/queue/fence quando disponível. O estágio 5/operação 12 isolado não prova que `FLUSH_TLB` travou. |
| KMD SDMA/GFX | engine, contexto, DMA buffer, fence dxg/hw, RPTR/WPTR normalizados, status, instante aceito/apresentado/aposentado; distinguir submit aceito de conclusão de hardware e de notificação ao Windows. |
| Batimentos | progresso do escritor, VSync, DWM `Present`, harness, paging, submit e completion em contadores separados. Se um para e outro avança, a falha é localizada por dependência, não pela última linha. |

Manter os eventos de caminho quente em anel pequeno sem alocação/bloqueio, com commit por sequência e checksum. A thread PASSIVE faz a cópia periódica ao disco. Não chamar arquivo/flush/espera dentro de DPC, spinlock ou callbacks em IRQL alto. Conservar DX.66 em VRAM como canal adicional; ele sobreviveu à 129, mas o slot `PAGING_DDI ANTES` era muito mais antigo que a última sequência global, logo não nomeia o instante do travamento.

**Pré-requisito de prova:** ensaio controlado em BasicDisplay ou modo lab sem hang real: provocar uma espera curta conhecida e demonstrar que o decodificador reconstrói ENTER sem LEAVE, IDs ligados e o contador de writer ainda vivo. Medir o custo de I/O e recusar o bind se o arquivo durável não abrir. Guardar hash, offset inicial e delta recuperado após reboot; a cópia `display-log-this-bind.txt` da 129 foi feita cedo demais.

## 3. ETW como visão de fora do driver

Em paralelo ao rastro próprio, capturar sessão **ETW em modo arquivo** com eventos CPU/thread scheduling, DxgKrnl/VidSch/VidMm, DWM e I/O, mais um provider próprio mínimo da UMD/KMD. A ideia é ver qual thread espera em qual objeto e se o scheduler deixou de despachar/completar uma fence. Arquivar o `.etl` bruto após reset e marcar se terminou limpo ou se é parcial; ETW também depende do sistema/disco e pode perder sua cauda. GPUView/WPA ajudam a correlacionar CPU e GPU. Não depender de `DxgkDdiCollectDbgInfo` como gatilho: no código atual ele só é chamado quando o Windows decide coletar informação de TDR; a 129 não registrou TDR.

## 4. Gatilho autônomo somente depois da instrumentação

Se não houver segundo PC/NMI, uma thread supervisora *de laboratório* pode detectar falta de progresso **com trabalho comprovadamente pendente**, gravar snapshot durável e tentar gerar dump controlado antes de o kernel inteiro parar. Primeiro operar em modo **somente snapshot** (por exemplo, aos 5 s) e medir falsos positivos; o primeiro teardown do DWM na 129 demorou 8,3 s e concluiu, portanto **8 s não pode disparar BugCheck**. Um limiar de crash precisaria ser maior, calibrado por medições de pior caso (por exemplo, 30 s), com chave explícita do candidato e aceitação prévia do ensaio. Não usar "não houve Present" sozinho: desktop ocioso é normal. A 129 mostra por que o gatilho deve observar simultaneamente pending paging/submit, ausência de completion e batimento do escritor. Mesmo esse mecanismo falha se o scheduler ou armazenamento já estiverem congelados.

## 5. Critério de saída do próximo ensaio diagnóstico

O próximo bind de diagnóstico deve responder, com evidência correlacionada, **qual dependência deixou de progredir primeiro**: (A) UMD lock/vkDeviceWaitIdle, (B) fence de paginação/`FLUSH_TLB`, (C) submissão/conclusão SDMA/GFX, ou (D) parada mais ampla de CPU/PCIe/armazenamento. Se só houver últimas linhas sem pares ENTER/LEAVE e sem pilhas/dump, o veredito causal é **inconclusivo**. Não preparar correção funcional DX.83 baseada apenas no último log da 129.

## Referências primárias

- [Microsoft — configurar depuração de kernel por KDNET](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-a-network-debugging-connection-automatically)
- [Microsoft — capturar dump por NMI quando o teclado não responde](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/generate-a-kernel-or-complete-crash-dump)
- [Microsoft — diagnóstico de congelamento do Windows](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/windows-based-computer-freeze-troubleshooting)
- [Microsoft — ETW/GPUView para DirectX](https://learn.microsoft.com/en-us/windows/win32/direct2d/profiling-directx-applications)
- [Microsoft — TDR em WDDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery)
