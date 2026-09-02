# Roadmap BC-250 — WDDM display+render, DCN e Vulkan

**Data-base:** 2026-09-01  
**Hardware:** AMD BC-250 `1002:13FE`, Cyan Skillfish 2, GC 10.1.3  
**Meta:** utilizar a BC-250 no Windows como uma GPU normal: monitor ligado ao DisplayPort da placa, desktop acelerado, render WDDM real, Vulkan, jogos e compute/LLM.  
**Estado honesto:** P1/P2 diretas estao provadas no silicio; display KMDOD/GOP foi provado; WDDM display+render, DCN assumido pelo driver, submissao pelo `dxgkrnl`, GPUVA dinamica, UMD e Vulkan ainda nao estao validados em hardware.

## 1. Decisao de arquitetura

O destino final nao e MCDM compute-only nem render-only dependente de outro adaptador. O destino final e um miniport WDDM completo:

```text
BC-250 / WDDM full adapter
├── Display
│   ├── DCN 2.0.1 detectado e inicializado
│   ├── DisplayPort, HPD, AUX e EDID
│   ├── VidPN, mode-set, scanout, flip e VSync
│   └── ownership transacional do framebuffer de boot
└── Render
    ├── segments, allocations, residency e GPUVA
    ├── device/context/queues
    ├── submissao originada pelo dxgkrnl
    ├── interrupcao, fence e retirement fisicos
    └── UMD D3D/Vulkan
```

O modo `RenderOnlyLab=1` existe como diagnostico A/B e como **entregavel intermediario**, mas nao substitui a arquitetura final.

Render-only nao e' o destino, e ainda assim carrega valor real que nao depende de DCN:

- desenvolver GPUVA 48-bit com journal, generation e rollback;
- exercitar D3DKMT de ponta a ponta;
- provar submissao originada pelo `dxgkrnl`;
- validar fence e retirement WDDM fisicos;
- iniciar UMD e Vulkan compute;
- possivelmente executar LLMs antes de o display estar completo.

Descartar esse estagio junto com a arquitetura render-only seria jogar fora meses de trabalho que o caminho display exige de qualquer forma.

## 2. Auditoria do trabalho 3.71–3.74

### Manter

| Alteracao | Decisao | Condicao |
|---|---|---|
| Geracao e preservacao do PDB privado | MANTER | Verificar correspondencia GUID/age entre SYS e PDB; nao copiar PDB pelo INF |
| Build contratual completo antes do relink de laboratorio | MANTER | E uma barreira importante contra objetos hibridos/stale |
| Runbook KD/WinDbg | MANTER, MAS CORRIGIR | Exigir host/target real via KDNET; depuracao local `-kl` nao aceita breakpoints/stack/execution |
| Decodificador de GUIDs | MANTER, MAS CORRIGIR | Hoje possui mapeamento contraditorio para `e922004d...`; cada GUID deve citar header/WDK exato |
| Auditoria separada de perfil MCDM e WDDM | MANTER | O teste atual e apenas estrutural; nao chamar ponteiro preenchido de implementacao funcional |
| `D3DKMDT_VOT_DISPLAYPORT_EXTERNAL` | MANTER | A placa possui DisplayPort; confirmar pela topologia/ATOM/IP discovery da propria BC-250 |
| `ChildUid=1` | MANTER PROVISORIAMENTE | ID deve ser estavel e usado de forma coerente em todas as DDIs; a mudanca e plausivel, mas nao prova a tela preta |
| `DxgkDdiControlInterrupt2` | MANTER | Somente para tipos realmente suportados; todo tipo desconhecido continua fail-closed |
| Contrato WDDM 2.0 como baseline | MANTER | Vulkan nao depende de WDDM 2.7; avancar versao somente por requisito medido |

### Retirar, reverter ou impedir

| Alteracao/afirmacao | Decisao | Motivo |
|---|---|---|
| “Causa definitiva: DWM chamou `DxgkDdiPresent` no 3.74” | RETIRAR DO VEREDITO | Nao existe trace/telemetria 3.74 provando entrada, status ou stack de `Present` |
| “BC-250 nao possui DCN inicializavel” | RETIRAR | O silicio possui display core; upstream identifica `13FE` como Cyan Skillfish e materiais da plataforma selecionam DCN 2.0.1 |
| “Injetar somente microcodigo DCN resolve” | RETIRAR | DCN e um pipeline de hardware; firmware DMCU/DMUB pode ser um componente, nao o driver inteiro |
| `SetDisplayPrivateDriverFormat`, `SetPalette` e `CollectDbgInfo` retornando sucesso vazio | **IDENTIFICADOS, RETIRADA ADIADA** | Encontrados pela auditoria automatica `tools/audit-ddi-noop-callbacks.py`, nao a mao. Foram retirados no 3.75 — corretamente em si, mas no mesmo candidato que estreou o perfil `RenderOnlyLab` e um bind de bring-up fisico. Quando aquela corrida falhou, nenhum eixo pode ser responsabilizado. **No 3.76 a tabela KMDOD esta congelada na baseline**, divida inclusa, para que o perfil render-only seja o unico eixo novo. A divida esta registrada em `ACCEPTED_DEBT` (impressa em toda execucao) e `WDDM_RESTORED_AS_DEBT` (reprova se a tabela divergir da baseline). A retirada volta em candidato proprio, depois do Gate 1.5 |
| Render-only como arquitetura definitiva | RETIRAR | Nao controla o DisplayPort da BC-250 nem entrega a experiencia de GPU normal pedida |
| “BasicDisplay/GOP permanece como segundo display adapter” | RETIRAR | Nao se deve assumir coexistencia independente no mesmo PCI function |
| “Cross-adapter blit automatico garante jogos” | RETIRAR | Exige outro display adapter real, allocations compartilhadas, UMD e contratos de apresentacao |
| “Zero risco de tela preta” | PROIBIR | Alteracoes de ownership/display sempre possuem risco; usar snapshot/readback/recovery |
| Breakpoint `QueryChildRelations+0x10` | REMOVER | Offset de corpo nao e ponto de retorno e muda a cada build |
| Breakpoints em simbolos internos sem `x`/validacao | CORRIGIR | Simbolos privados do `dxgkrnl` podem nao existir nos public symbols |
| Opcao WinDbg local `-kl` para executar o script de breakpoints | REMOVER | Microsoft documenta que `bp/bu`, `g`, registradores e stacks nao estao disponiveis localmente |

#### Defeito estatico CONFIRMADO no perfil display+render

Este item saiu da categoria de hipotese. A confirmacao e' leitura de codigo, nao inferencia sobre a corrida:

- `DriverEntry` bifurca em dois contratos mutuamente exclusivos (`bc250_active_miniport.c`). Com `RenderNode=0` registra por `DxgkInitializeDisplayOnlyDriver` e usa `DxgkDdiPresentDisplayOnly`, que **e** implementada e valida os argumentos. Com `RenderNode=1` registra por `DxgkInitialize` com a tabela que `Bc250DisplayPopulateCallbacks` preenche.
- Nessa segunda tabela, o unico caminho de apresentacao anunciado era `Bc250DxgkDdiPresent`, cujo corpo devolvia `STATUS_NOT_SUPPORTED` incondicionalmente. `PresentDisplayOnly` nao existe nessa estrutura.
- Simultaneamente, `StartDevice` devolvia `NumberOfVideoPresentSources = 1` e `NumberOfChildren = 1`, e `QueryChildRelations` descrevia um `D3DKMDT_VOT_DISPLAYPORT_EXTERNAL`.

**Formulacao correta:** o perfil display+render anunciava uma saida de video fisica cujo unico caminho de apresentacao recusava toda apresentacao. Isso e' objetivamente invalido para o contrato anunciado, e e' candidata forte para a tela preta — mas **a causalidade da corrida 3.74 nao foi capturada**.

### Manter apenas como hipotese a verificar

- `ChildUid=1` pode ter feito o Windows aceitar a saida e iniciar uma transicao de ownership.
- O driver anunciar uma source/child sem possuir scanout WDDM completo pode explicar a tela preta.
- Que `DxgkDdiPresent` tenha sido **efetivamente chamada** antes da perda de imagem. A tela tambem poderia ter apagado durante a transicao de ownership, `CommitVidPn`, `SetVidPnSourceAddress`, mudanca de visibility, invalidacao do framebuffer, `Stop`/`Remove`, ou alteracao do estado do GOP.
- Nada disso esta provado para o 3.74 enquanto nao houver uma evidencia pos-boot ou trace KD. O diretorio de evidencias possui corrida 3.73 ate `17:12:34`, mas nao possui um `rendernode-inspect` 3.74; ha apenas um novo `lab-binding` as `17:19:14`.

### Como o defeito foi corrigido — e como NAO deve ser corrigido

Duas "correcoes" obvias sao ambas erradas:

| Tentacao | Por que e' errada |
|---|---|
| Remover `DxgkDdiPresent` da tabela mantendo source/child | So' troca a tela preta por outra falha: rejeicao da tabela no `dxgkrnl`, Codigo 43 mais cedo, falha de criacao da primary ou estado inconsistente de display. Quem anuncia display fisico deve fornecer o conjunto coerente de DDIs daquele modelo. |
| Implementar um `Present` "minimo" agora | `DXGKDDI_PRESENT` exige handles validos de device/context, interpretacao de allocations, geracao de comandos em DMA buffer ou validacao do modelo de flip, patch/allocation lists coerentes, copia/render para a primary e integracao com scheduler e scanout. Sem allocations, contextos, primary surface e backend fisico, o resultado seria outro falso sucesso. |

A correcao adotada e' de **perfil, nao de callback**: ou o adaptador anuncia o conjunto inteiro de display, ou nao anuncia display nenhum. Ver Gate 1.5.

## 3. “Podemos injetar o DCN?”

**Sim, podemos desenvolver suporte ao DCN; nao devemos chamar isso de uma simples injecao.**

DCN 2.0.1 e um conjunto de blocos e politicas:

- DCHUB/HUBP: busca da surface em memoria;
- DPP/MPC/OPP: processamento e composicao de pixel;
- OTG/OPTC: timing, blanking, sync e frame counter;
- DIO/DIG/PHY: saida fisica DisplayPort;
- DCCG/SMU: clocks, resets e power domains;
- HPD/AUX/DDC: deteccao, DPCD, EDID e link training;
- IH: VBlank, HPD e page-flip interrupts;
- DMCU/DMUB, se exigido pela variante/feature: firmware, memoria, inbox/outbox e handshake.

O upstream Linux separa Display Core, que contem programacao e resource management em grande parte independente do SO, de `amdgpu_dm`, que traduz a API DRM. A estrategia correta e portar/adaptar o nucleo DCN 2.0.1 necessario e escrever uma camada WDDM equivalente ao Display Manager, nao copiar `amdgpu_dm` literalmente.

Antes de qualquer escrita, precisamos provar no hardware BC-250:

1. IP Discovery e base exata do DCN para `13FE`.
2. Versao `DCN_VERSION_2_01`, numero real de pipes e link encoders.
3. Estado herdado do GOP: HUBP, OTG, clock source, surface address/pitch e frame counter.
4. Qual entidade fez link training durante o boot e se a BIOS/firmware fornece ATOM tables utilizaveis.
5. Se DMCU/DMUB esta presente, rodando e necessario para o modo basico; nao inventar firmware.
6. Quais registradores estao catalogados e liberados para escrita.

## 4. Roadmap por gates

### Gate 0 — congelar e recuperar uma baseline confiavel

- Recuperar para `BasicDisplay / Codigo 0` e boot limpo.
- Arquivar pacote 3.74, SYS/INF/CAT/PDB e hashes sem reinstalar.
- Marcar o relatorio 150 como “hipotese de tela preta, nao causa definitiva”.
- Corrigir o decoder de GUIDs e o runbook KD.
- Remover callbacks que retornam sucesso sem implementar semantica.
- Criar tunables independentes: `RenderNode`, `RenderOnlyLab`, `DcnProbe`, `DcnTakeover`.
- Todos desligados por padrao; `DcnTakeover=0` e fail-closed.
- **Feito:** `RenderOnlyLab` existe, default 0 no INF, resolvido em `DriverEntry` e fail-closed (ver Gate 1.5). `DcnProbe` e `DcnTakeover` ainda nao existem — dependem do sub-gate 2.0.

**PASS:** baseline KMDOD Codigo 0, P1 e P2 novamente aprovadas, hashes/PDB coerentes.  
**FAIL:** qualquer divergencia de pacote ou dirty-engine exige recovery e novo boot.

### Gate 1 — diagnostico real da tela preta 3.74

- Usar duas maquinas e KDNET; nao usar `windbg -kl` para breakpoints.
- Colocar breakpoints somente depois de validar simbolos com `lmvm` e `x`.
- Instrumentar no driver contadores/status para:
  - `QueryChildRelations`, `QueryChildStatus`, `QueryDeviceDescriptor`;
  - `IsSupportedVidPn`, `EnumVidPnCofuncModality`, `CommitVidPn`;
  - `SetVidPnSourceAddress`, visibility, active present path;
  - `DxgkDdiPresent` e `PresentDisplayOnly` separadamente;
  - ControlInterrupt/VSync, Stop/Remove.
- Capturar call stack do primeiro erro antes de `StopDevice` ou perda de imagem.
- Nao modificar DCN nesta corrida.

**PASS:** cadeia causal sustentada por timestamp, DDI, argumentos, status e stack.  
**Nao aceitar:** “ficou preto depois de ChildUid” como prova suficiente.

### Gate 1.5 — RenderNode isolado, zero sources e zero children

> **Estado em 2026-09-01:** candidato **3.76.0.0 `RENDERONLY_SAFE`** assinado e auditado, aguardando ensaio. Ver `HANDOFF_3_76_RENDERONLY_SAFE.md`.
>
> A primeira tentativa (3.75) **nao chegou a testar o gate**. O ensaio rodou sobre a imagem de LABORATORIO, que executa PFP direct load, CP soft reset, GCVM e HQD durante o `StartDevice` detendo o display — exatamente o oposto do que este gate exige. Terminou em tela preta e Codigo 39, sem responder nada.
>
> Duas causas de processo, ambas corrigidas:
>
> 1. **Veiculo errado.** O gate agora tem pacote proprio (`output_renderonly_safe`), compilado **sem** `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP`, com `SkipHwBringup=1`. Scripts seguros proprios recusam o pacote de laboratorio por `BuildKind`, e a auditoria executa a prova negativa.
> 2. **Preflight que forcava a corrida perigosa.** O `prepare` antigo exigia prontidao fisica de P1/P2/shader — que este perfil nunca usa — obrigando um boot de bring-up so' para satisfazer o portao. O `prepare` seguro exige o oposto: P1/P2 **zerados**.
>
> **Sobre o Codigo 39:** o evento `\Driver\bc250mcdm falhou ao carregar` tambem existe no boot do 3.74, com a tabela KMDOD antiga. Nao e' novidade do 3.75, e sua causa continua **nao diagnosticada**. O CP soft reset e' hipotese, nao conclusao.

Este gate foi inserido antes do trabalho de DCN, e a razao e' de custo: nao precisamos concluir DCN, link training e mode-set para responder se um adaptador render **sem display** sobrevive em Codigo 0. Essa pergunta e' o bloqueio atual e mensuravel, e o ensaio custa um par de boots, nao meses.

**Os dois perfis sao explicitos e mutuamente exclusivos:**

| | `RenderNode` | `RenderOnlyLab` | Sources | Children | Superficie de display |
|---|---|---|---|---|---|
| KMDOD (herdado) | 0 | 0 | 1 | 1 | `PresentDisplayOnly` implementado |
| **Gate 1.5** | 1 | 1 | **0** | **0** | **nenhuma DDI anunciada** |
| Destino final | 1 | 0 | 1 | 1 | conjunto completo e coerente |

`RenderOnlyLab` e' tunable independente e fail-closed: `RenderOnlyLab=1` com `RenderNode!=1` e' recusado com `STATUS_DEVICE_CONFIGURATION_ERROR`, porque o caminho KMDOD e' display por definicao. Valores fora de `{0,1}` nao sao adivinhados.

**O que o perfil render-only faz:**

- registra por `DxgkInitialize` com a tabela base + render;
- devolve zero sources e zero children em `StartDevice`;
- **nao** adquire post-display ownership e **nao** chama `Bc250DisplayInit` — o GOP continua dono do scanout, o modo herdado nao e' tocado, e um FAIL nao custa tela preta nem boot frio;
- `QueryChildRelations` devolve lista vazia e `QueryChildStatus` reporta desconectado, coerentes com `NumberOfChildren=0`;
- mantem `ComputeOnly=0`. Anunciar `ComputeOnly=1` exige antes confirmar que o contrato selecionado aceita a combinacao; nao foi confirmado, entao nao foi anunciado.

**O que ele NAO faz:** promover `ContextAbiReady`, `AllocationAbiReady` ou `RootPageTableAbiReady`. Elas ficam em zero, iguais ao caminho de display de producao. Promove-las anunciaria objetos WDDM sem backend fisico provado. A promocao e' decisao de um gate posterior, com evidencia propria.

**Matriz de decisao:**

| Resultado | Interpretacao |
|---|---|
| Render-only continua Codigo 43 no estagio 3 | O problema nao depende de display nem de `Present` |
| Render-only avanca nas caps e termina Codigo 43 | Novo estagio identificado; investigar o estagio novo |
| Render-only permanece Codigo 0 | Tabela render/base correta; display e' o bloqueio seguinte |
| Render-only chega a D3DKMT/submissao | Vulkan compute pode ser desenvolvido antes do DCN |
| Display+render depois fica Codigo 0 | Arquitetura final alcancada |

**PASS:** `V5AdapterProfile=2`, `V5RegistrationMode=2`, `V5DisplaySurfaceAnnounced=0`, `V5StartDeviceSources=0`, `V5StartDeviceChildren=0`, Codigo 0 e progresso alem de `V5PostStartStage=3`.  
**FAIL:** qualquer divergencia entre o perfil pedido e o resolvido invalida a corrida antes de interpretar o resto.

### Gate 2 — inventario DCN somente leitura

**Sub-gate 2.0 — estender o IP Discovery para o bloco de display.**  
Nao existe uma unica linha de DCN no repositorio, e o parser de discovery conhece apenas `BC250_DISCOVERY_GC_HWID = 11` e `BC250_DISCOVERY_OSSSYS_HWID = 30`. O "inventario somente leitura" exige, antes, escrever a enumeracao do bloco DMU/DCE e derivar seu HWID da tabela real da placa. Sem esse sub-gate o Gate 2 nao e' executavel.

**PASS do sub-gate:** identidade e base do bloco de display derivadas da propria BC-250, com checksum e bounds, sob teste offline — antes de qualquer leitura de registrador de DCN.

- Derivar bases de IP Discovery da propria placa.
- Comparar com Cyan Skillfish/DCN 2.0.1 upstream e patch da plataforma.
- Ler somente registradores catalogados de HUBP, OTG, DCCG, DIG/PHY, AUX/HPD e DMCUB.
- Capturar dois snapshots com intervalo e provar contadores vivos sem escrever.
- Decodificar framebuffer herdado: endereco, pitch, formato, viewport, timing e modo.
- Validar `OTG_STATUS_FRAME_COUNT` avancando e `SURFACE_INUSE` coerente.

**PASS:** mapa DCN 2.0.1 especifico da BC-250 documentado e provado.  
**FAIL:** base/mascara ambigua fica em quarentena.

### Gate 3 — takeover sem alterar modo

- Adquirir post-display ownership pelo contrato WDDM correto.
- Fazer snapshot completo dos registradores que serao tocados.
- Preservar exatamente modo, link, clock e framebuffer do GOP.
- Inicializar apenas interrupcoes/estado de software necessario.
- Publicar uma source/child apenas depois do readback coerente.
- Provar imagem visivel, frame counter, VSync e present copy.
- Rollback restaura o snapshot se qualquer gate falhar.

**PASS:** WDDM full em Codigo 0 mantendo o modo herdado, sem reprogramar link.  
**Observacao:** este e o primeiro alvo display; mode-set completo vem depois.

### Gate 4 — flip/scanout WDDM real

- Implementar allocations primarias e address translation para scanout.
- Implementar `SetVidPnSourceAddress` com master-update lock e flip control.
- Implementar `GetScanLine`, VBlank interrupt e notificacao ao `dxgkrnl`.
- Implementar `DxgkDdiPresent` para gerar comandos/copia validos ou anunciar apenas o modelo de flip realmente suportado.
- Nunca retornar sucesso sem programar/validar o efeito.
- Testar poison surface, flip A/B e confirmar visual + address-in-use + frame counter.

**PASS:** duas surfaces alternam por flip fisico; VBlank e retirement coerentes.  
**Ainda nao:** troca de resolucao, hotplug ou jogos.

### Gate 5 — HPD, AUX/EDID e mode-set DisplayPort

- Mapear HPD e interrupcoes no IH.
- Implementar AUX read/write, DPCD e EDID reais.
- Implementar link capability, lane count/rate, training pattern e verificacao de status.
- Portar calculos de timing/bandwidth relevantes do DC/DML para um unico pipe.
- Implementar clock source, OTG, HUBP/DPP/OPP e DIG/PHY em transacao.
- Comecar com um modo conhecido e seguro; depois enumerar modos EDID.
- Tratar desconexao/reconexao e rollback do link.

**PASS:** cold boot, reboot, mode change e hotplug mantem imagem e Codigo 0.

### Gate 6 — RenderNode WDDM em Codigo 0

- Com display ownership estavel, ativar `RenderNode=1`.
- Exigir progresso alem de `V5PostStartStage=3`.
- Responder caps e segmentos com valores derivados do hardware.
- Implementar somente DDIs exigidas pela versao/feature anunciada.
- Manter teardown limpo e display vivo.

**PASS:** display+render registrado, Codigo 0, sem `StopDevice` imediato.

### Gate 7 — memoria e objetos WDDM

- Segments, aperture, VRAM e system-memory coerentes.
- Process/device/context/context-virtual.
- Allocations, open/close, residency, paging e teardown.
- GPUVA 48-bit com journal, generation, epoch e rollback.
- Paging via SDMA somente depois de prova isolada.

**PASS:** harness D3DKMT cria/destroi objetos e mappings sem vazamento ou VA stale.

### Gate 8 — submissao e fence originadas pelo `dxgkrnl`

- Harness: `OpenAdapterFromLuid -> CreateDevice -> CreateContextVirtual/HwQueue -> CreateAllocation -> Submit`.
- Entrada real em `SubmitCommandVirtual`/hardware queue pelo `dxgkrnl`.
- Validar IB, GPUVA, `RPTR==WPTR`, poison fence e dados.
- IH/DPC notifica completion pelo contrato WDDM.
- Provar fence/retirement real; proibido completar pela CPU.
- Testar wrap, sequencia, timeout, cancel, reset e teardown pendente.

**PASS:** submissao WDDM fisicamente aposentada e reconhecida pelo scheduler.

### Gate 8.5 — reset, TDR e recuperacao

Vem imediatamente depois da primeira submissao WDDM real, e antes de Vulkan, jogos ou qualquer carga longa. A ordem nao e' negociavel: desenvolver UMD sobre um adaptador que nao se recupera de hang transforma todo bug de shader em reboot.

- `DxgkDdiResetEngine` por node, com dequeue de HQD e reset de SDMA provados no silicio.
- `DxgkDdiResetFromTimeout` e `DxgkDdiRestartFromTimeout` com quiesce completo.
- Deteccao de hang em duas passagens; sem completar fence pela CPU em nenhuma hipotese.
- Teardown com submissao pendente, cancel e timeout.
- Recuperacao apos TDR sem perder o display nem o Codigo 0.

**PASS:** hang induzido e' detectado, o engine e' resetado, o adaptador volta a aceitar submissao e o display sobrevive — tudo com evidencia fisica.

### Gate 9 — UMD e Vulkan

Subdividido: o gate original era grande demais para ter um unico PASS util.

**9a — ABI e enumeracao.**  
ABI KMD/UMD versionada; UMD declarada no INF somente quando a DLL funcional existir; loader, instance e physical device.  
*PASS:* `vulkaninfo` enumera a BC-250 com caps derivadas do hardware.

**9b — memoria e command buffers.**  
Heaps e tipos de memoria coerentes com os segments do Gate 7; alocacao, mapeamento, command pools e command buffers; submissao ate o `dxgkrnl`.  
*PASS:* command buffer gravado pelo UMD e' fisicamente aposentado.

**9c — sincronizacao e compute.**  
Fences, semaforos, eventos e barreiras; pipeline de compute e dispatch.  
*PASS:* compute shader Vulkan produz resultado conferido byte a byte na VRAM.

**9d — WSI e apresentacao.**  
Swapchain, surface e present pelo DisplayPort da propria BC-250.  
*PASS:* triangulo apresentado na tela ligada a placa.

**9e — adaptacao do RADV.**  
Portar o winsys RADV para os objetos WDDM deste driver. Nao assumir que o RADV do Linux roda inalterado.  
*PASS:* pipeline grafico completo sobre o winsys adaptado, estavel em long-run.

**9f — camadas de API.**  
Zink (OpenGL), DXVK (D3D9/10/11), VKD3D-Proton (D3D12).  
*PASS:* cada camada roda sua suite de conformidade sem WARP nem fallback.

### Gate 10 — cargas finais

- DirectML e llama.cpp com backend Vulkan para LLM.
- Testes de jogos, shader compilation, pressao de memoria, suspend/resume e long-run.

**PASS final:** desktop acelerado, jogos e LLMs usando a BC-250, sem WARP/fallback e com recuperacao funcional.

## 5. Ordem pratica recomendada agora

1. Nao instalar 3.74 novamente sem KDNET ou telemetria ampliada.
2. Corrigir infraestrutura de debug e falsos sucessos — feito: auditoria automatica de callbacks anunciados-mas-vazios em `tools/audit-ddi-noop-callbacks.py`, ligada ao build.
3. **Executar o Gate 1.5** (`RenderNode=1`, `RenderOnlyLab=1`). E' o experimento mais barato disponivel e responde o bloqueio atual sem tocar em DCN nem arriscar tela preta.
4. Somente depois, Gate 2 somente leitura — comecando pelo sub-gate 2.0, que estende o IP Discovery para o bloco de display.
5. Implementar takeover preservando o modo GOP, antes de tentar mode-set ou “injetar firmware”.
6. Voltar ao perfil display+render com `Present` e scanout coerentes, nunca anunciando display pela metade.

### Orcamento de corridas

Gates 3 a 5 mexem em ownership de display: cada FAIL custa tela preta, recovery e boot frio, porque estado de engine nao cai com desbind. Antes de entrar no Gate 3, definir teto de tentativas por gate e criterio de abandono. O Gate 1.5 nao tem esse custo — e' outra razao para ele vir primeiro.

## 6. Fontes primarias

- Microsoft, `DXGKDDI_PRESENT`: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_present
- Microsoft, `DXGKDDI_START_DEVICE`: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_start_device
- Microsoft, depuracao local e limitacoes: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/performing-local-kernel-debugging
- Linux AMD Display Core: https://github.com/torvalds/linux/blob/master/Documentation/gpu/amdgpu/display/index.rst
- Linux DCN overview: https://github.com/torvalds/linux/blob/master/Documentation/gpu/amdgpu/display/dcn-overview.rst
- Linux DCN programming model: https://github.com/torvalds/linux/blob/master/Documentation/gpu/amdgpu/display/programming-model-dcn.rst
- Linux ASIC IDs (`13FE` Cyan Skillfish): https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/include/dal_asic_id.h
- Patch de plataforma existente no repositorio: `referencias/AMD-BC-250-Windows-Driver/docs/ps5-linux.patch.txt`

