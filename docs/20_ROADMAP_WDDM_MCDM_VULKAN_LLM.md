# Roadmap BC-250: MCDM/WDDM, Vulkan compute e LLMs no Windows

Atualizado em 2026-08-26.

Estado atual: **FIRMWARE SDMA0 E F32 ATIVA APROVADOS; PROXIMO GATE E O RING SDMA COM COPY/FENCE; ACELERACAO NAO VALIDADA**.

```text
Conformidade BC250_HARDWARE_RULES   91%
Miniport MCDM funcional            54%
Memoria/GPUVA WDDM                 32%
PM4/compute pelo miniport           0%
Vulkan ICD                          0%
LLM acelerado                       0%
```

Os percentuais medem gates demonstrados, nao quantidade de arquivos ou retorno
de testes offline.

## 1. Objetivo e decisao de arquitetura

O primeiro produto funcional sera um **MCDM compute-only**, classe
`ComputeAccelerator`, render-only e sem fontes de display. MCDM e um subconjunto
de WDDM e permite command buffers opacos, mas exige isolamento de memoria para
multitarefa. A documentacao atual da Microsoft exige uma tabela extensa de DDIs
minimas; quando GPUVA e usada, tambem exige `CreateProcess`, `DestroyProcess`,
`GetRootPageTableSize`, `SetRootPageTable` e `SubmitCommandVirtual`.

Fontes:

- [Microsoft: MCDM overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/mcdm)
- [Microsoft: MCDM KMD implementation guidelines](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/mcdm-implementation-guidelines)
- [Microsoft: MCDM architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/mcdm-architecture)

O segundo produto sera um **Vulkan ICD compute-only** sobre o mesmo KMD. O
Vulkan Loader descobre ICDs, cria instancias/devices e encaminha as chamadas
por dispatch tables. Para o primeiro LLM, nao e necessario controlar o display:
o backend Vulkan do `llama.cpp` precisa de compute, buffers, descriptors,
pipelines, command buffers, queues, synchronization e queries coerentes.

Fontes:

- [Khronos: Vulkan Loader](https://docs.vulkan.org/guide/latest/loader.html)
- [Khronos: Loader/driver interface architecture](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md)
- [llama.cpp: build com Vulkan no Windows](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)

Uma trilha opcional posterior transforma o miniport em full WDDM display/render
para swapchain e desktop acelerado. Ela nao bloqueia LLMs ou Vulkan compute.

## 2. Regras globais para todas as fases

Cada fase segue:

```text
preflight -> snapshot -> apply -> readback -> prova funcional -> teardown
                                  |                             |
                                  +---------- rollback ----------+
```

Regras:

1. nenhuma capacidade sobe por constante, mock ou retorno do proprio driver;
2. nenhum endereco bruto vindo de user mode chega a MMIO, PSP ou ring;
3. cada handle tem owner, tipo, geracao e teardown idempotente;
4. toda allocation usada pela GPU nasce de VidMm ou de uma reserva owned e
   selada pelo miniport;
5. fence de CPU nunca valida trabalho de GPU;
6. toda submissao exige RPTR/WPTR, fence fisica e resultado integral;
7. timeout limitado, coleta de fault e falha fechada em todo gate;
8. instalacao, binding e ensaio fisico sao operacoes separadas;
9. o recovery precisa cobrir exatamente o pacote que sera ativado;
10. testes offline nao alteram percentuais fisicos.

## Fase 0 - baseline, recuperacao e pacote fail-closed

Status: **em andamento, 99%**.

Objetivo: garantir que qualquer binario instalavel falhe sem inventar aceleracao
e possa ser removido em Modo de Seguranca.

Entregaveis:

- remover runtime/ownership/TMR/firmware sinteticos;
- DDIs sem backend retornam `STATUS_NOT_SUPPORTED`;
- `BC250_RECOVERY.cmd` cobre `bc250mcdm` por identidade quadrupla;
- auditor estatico impede regressao de sucesso fabricado;
- pacote candidato versionado, assinado e separado do pacote antigo;
- harness de stage/binding com `DRY-RUN`, hashes e evidencia datada.

Gate de saida:

- build `/W4 /WX` e testes passam;
- recovery instalado e hash-identico a fonte;
- pacote antigo nao pode ser confundido com o candidato;
- binding do candidato chega a DriverEntry/Add/Start e falha fechado sem
  bugcheck, leak ou arquivo orfao;
- recovery restaura `BasicDisplay`, Codigo 0.

Ainda nao autoriza `StartDevice=STATUS_SUCCESS`.

Auditoria read-only de 2026-08-25:

- fonte e copia instalada do recovery possuem hashes diferentes;
- a fonte cobre integralmente `bc250mcdm`; a copia instalada nao cobre servico,
  binario nem INF do pacote ativo;
- estado observado: servico `bc250mcdm` presente, binario ausente e
  `oem18.inf` detectado pela identidade estrita;
- os dois `/DRY-RUN` retornaram exit 0 e nao alteraram o host, mas somente a
  fonte mostrou o estado MCDM real;
- `audit-bc250-recovery-readiness.ps1` transforma a divergencia em gate exit 1;
- o gate foi conectado antes da primeira mutacao do builder; uma tentativa
  bloqueada preservou todos os hashes de `output_active`;
- naquele momento, sincronizar `C:\BC250_RECOVERY.cmd` ainda exigia autorizacao
  explicita; o marco seguinte registra a sincronizacao concluida.

Marco de pacote fail-closed 1.0.1.0 em 2026-08-25:

- recovery foi sincronizado pelo usuario e o auditor passou com hashes
  identicos;
- `oem18.inf` nao existe mais; o servico `bc250mcdm` permanece parado e sem
  binario, portanto ainda exige limpeza antes de ensaio de binding;
- pacote 1.0.1.0 foi reconstruido, catalogado e assinado sem erros/avisos;
- manifesto externo registra origem bruta, SYS assinado, INF, catalogo,
  recovery, certificado e `HardwareValidated=false`;
- auditor independente do pacote passou em integridade, Authenticode e
  identidade; nenhuma instalacao, stage ou troca de binding foi realizada;
- detalhes em `docs\22_PACOTE_MCDM_FAIL_CLOSED_1.0.1.0.md`.

Marco de pre-binding limpo em 2026-08-25:

- gate read-only exige BasicDisplay Codigo 0, cardinalidade fisica unica,
  recovery/pacote integros, zero servicos/binarios/INFs experimentais, marcador
  ausente e nenhum reboot pendente conhecido;
- host passou em todos os itens exceto o servico `bc250mcdm`, presente e
  parado; por isso o gate e o stage retornam exit 1;
- stage 1.0.1.0 foi separado do binding e usa somente `/add-driver`; a auditoria
  de seguranca proibe `/install`, `INSTALLFLAG_FORCE` e NewDev nesse script;
- detalhes em `docs\23_GATE_PREBINDING_MCDM_1.0.1.0.md`.

Marco de host limpo em 2026-08-25:

- recovery real foi executado pelo usuario e seguido de reboot;
- recovery, pacote e pre-binding retornaram exit 0 independentemente;
- BC-250 esta em BasicDisplay Codigo 0; todos os servicos e binarios
  experimentais estao ausentes; nao ha INF experimental, marcador ou reboot
  pendente conhecido;
- preflight do stage retornou exit 0 e a verificacao posterior confirmou zero
  escrita (`marker=false`, cardinalidade INF MCDM zero);
- o pre-stage separado foi autorizado e concluido posteriormente, sem binding.

Marco de stage sem binding em 2026-08-25:

- o usuario executou o stage elevado; `pnputil /add-driver` publicou somente
  `oem19.inf` e criou `C:\BC250_TEST_OEMINF.txt`;
- auditoria independente confirmou exatamente um INF e um diretorio no
  FileRepository, com INF/SYS/CAT identicos ao manifesto e assinaturas validas;
- a BC-250 permaneceu em `BasicDisplay`, Codigo 0; `bc250mcdm` continuou
  ausente e nenhum `bc250mcdm.sys` apareceu em `System32\drivers`;
- o recovery em `DRY-RUN` reconhece tanto `oem19.inf` quanto o marcador;
- `tools\audit-bc250mcdm-staged.ps1` retornou exit 0;
- harness de binding foi preparado e passou em `DRY-RUN`, com zero escrita e
  dupla trava `-Execute -IAcceptBlackScreenRisk` para a rota mutavel;
- nenhum binding ou validacao fisica do miniport ocorreu;
- detalhes em `docs\24_STAGE_MCDM_1.0.1.0.md`.

Marco de binding fail-closed 1.0.1.0 em 2026-08-25:

- binding forcado foi autorizado explicitamente e a API NewDev retornou sucesso,
  sem reboot requerido;
- `DriverEntry`, inicializacao da tabela, `AddDevice` e `StartDevice` foram
  observados fisicamente; Stop/Remove tambem ocorreram no unwind;
- PCI retornou 64 bytes; tres BARs foram resolvidos; a leitura readonly de
  topologia foi estavel em duas passagens;
- foram observados 512 MiB de VRAM, 256 MiB CPU-visible e ReBAR atribuido de
  256 MiB;
- discovery TMR retornou `NOT_REPORTED` (status 5, tamanho zero), logo
  `RuntimeMissing=1` bloqueou `StartDevice` e o dispositivo ficou em Codigo 43;
- nenhum PM4, firmware, PSP, queue, fence ou compute foi executado;
- `tools\audit-bc250mcdm-failclosed-binding.ps1` retornou exit 0;
- falta executar recovery em Modo de Seguranca e provar `BasicDisplay`, Codigo 0,
  para concluir a Fase 0;
- detalhes em `docs\25_BINDING_FAIL_CLOSED_MCDM_1.0.1.0.md`.

## Fase 1 - tabela MCDM minima e lifecycle de objetos

Status: **concluida, 100% estrutural / 0% fisico**.

Objetivo: implementar a ABI obrigatoria de MCDM sem anunciar engine ou memoria
que ainda nao existam.

Entregaveis KMD:

- matriz automatica das DDIs obrigatorias para o WDDM alvo;
- resolver a contradicao da documentacao Microsoft sobre
  `DxgkDdiQueryConnectionChange`: o texto o lista como minimo e tambem como
  proibido; ate esclarecimento por HLK/Dxgkrnl, prevalece a proibicao explicita;
- `AddDevice/StartDevice/StopDevice/RemoveDevice/Unload` idempotentes;
- objetos `adapter`, `process`, `device`, `context` e `allocation` com:
  assinatura, geracao, referencia, owner e lista intrusiva protegida;
- handles nao derivados de ordinais ou constantes pequenas;
- validacao de transicao de estado e teardown em ordem inversa;
- `CollectDbgInfo` e telemetria que preservam o primeiro erro;
- nenhum `STATUS_SUCCESS` em operacao nao materializada.

Testes offline:

- double-create/double-destroy;
- handle antigo, cruzado entre processos e tipo incorreto;
- falha de pool em cada ponto;
- surprise remove com objetos vivos;
- zero leak em todas as permutacoes.

Gate fisico:

- miniport carregado, callbacks completos aceitos pelo Dxgkrnl;
- ainda pode encerrar `StartDevice` fail-closed;
- Stop/Remove limpos em repeticoes e Driver Verifier.

Marco estrutural de 2026-08-25:

- auditor `tools/audit-mcdm-ddi-matrix.py` comprova 36/36 ponteiros minimos,
  5/5 ponteiros GPUVA e 1/1 requisito de compatibilidade observado localmente;
- `CreateProcess`, `DestroyProcess`, `GetRootPageTableSize`,
  `SetRootPageTable` e `SubmitCommandVirtual` existem, mas falham fechados ou
  anunciam tamanho zero;
- transicoes de energia que antes retornavam sucesso apenas em estado de
  software agora retornam `STATUS_NOT_SUPPORTED`;
- isso prova somente a forma da ABI. Lifecycle, memoria, submit e hardware
  continuam nao implementados e `StartDevice` continua bloqueado.

Marco de lifecycle offline de 2026-08-25:

- registry owned incorporado ao contexto do adaptador para `process`, `device`,
  `context` e `allocation`;
- handles sao enderecos de registros owned, procurados antes de qualquer
  dereferencia, nunca ordinais ou constantes pequenas;
- cada registro carrega assinatura, geracao, tipo, owner, parent e contagem de
  filhos;
- tombstones nao sao reutilizados durante a vida do adaptador, impedindo alias
  imediato de handles antigos;
- `StopDevice` executa teardown inverso e repetivel;
- testes cobrem owner cruzado, tipo incorreto, handle forjado/antigo,
  double-destroy, parent com filhos vivos, exaustao e double-teardown;
- falta conectar esse registry aos callbacks DDI e adicionar sincronizacao para
  chamadas concorrentes antes de considerar o lifecycle implementado no KMD.

Marco de integracao KMD de 2026-08-25:

- `CreateProcess` e `DestroyProcess` agora atravessam uma interface instalada
  apenas pela imagem ativa e materializam/removem registros sob `EX_PUSH_LOCK`;
- a imagem readonly deixa a interface nula e continua retornando
  `STATUS_NOT_SUPPORTED`;
- o estado ativo usa operacoes interlocked e e verificado antes e depois da
  aquisicao do lock, fechando a corrida com `StopDevice`;
- a interface e removida em `Unload`;
- o teste de integracao comprova `DEVICE_NOT_READY` antes da ativacao,
  create/destroy materializados na fixture, duplicata, stale handle e teardown;
- `Device`, `Context` e `Allocation` ainda nao estao conectados aos DDIs.

Marco de handles sem `hAdapter` de 2026-08-25:

- diretorio global limitado a oito adaptadores owned, protegido por
  `EX_PUSH_LOCK` e preenchido/removido em `AddDevice`/`RemoveDevice`;
- ordem de locks fixada em diretorio -> registry;
- `CreateDevice` materializa um filho do processo e `DestroyDevice` encontra o
  registry por comparacao do handle contra cada conjunto owned, sem
  dereferencia previa do valor recebido;
- processo com device vivo retorna `STATUS_DEVICE_BUSY`;
- handles forged e tombstones retornam `STATUS_INVALID_HANDLE`;
- `Context` e `Allocation` continuam fail-closed.

Marco de contextos de 2026-08-25:

- `CreateContext` e `DestroyContext` usam o mesmo diretorio owned, sem
  dereferenciar o handle recebido antes de localizar o registry;
- um gate `ContextAbiReady`, separado de `IsActive`, impede sucesso enquanto
  tamanhos de DMA buffer e capacidades de contexto nao forem comprovados;
- o `DXGK_CONTEXTINFO` e zerado antes de qualquer falha;
- fixture offline injeta um ABI de teste, comprova create/destroy, parent busy,
  forged handle e tombstone; a imagem de producao nunca arma esse gate;
- naquele marco, `Allocation` ainda permanecia fail-closed; o marco seguinte
  registra sua implementacao estrutural.

Marco de allocations de 2026-08-25:

- `CreateAllocation` agora modela allocations adapter-owned; associacao por
  device foi reservada corretamente para `OpenAllocation`;
- private data V1 exige magic `BCA1`, versao, tamanho exato, flags zero,
  token de allocation nao nulo, byte-size nao nulo e alinhamento power-of-two
  entre 4 KiB e 2 MiB;
- `AllocationAbiReady` permanece zero na imagem de producao;
- nenhuma preferencia ou conjunto de segmentos e anunciado;
- criacao multipla tem duas passagens e rollback integral; destroy rejeita
  duplicatas e pre-valida a lista inteira antes de mutar o registry;
- testes cobrem private data invalida, gate fechado, batch create/destroy,
  lista duplicada e stale handles;

Marco de open/close allocations de 2026-08-25:

- `OpenAllocation.hAllocation` e tratado corretamente como identificador
  `D3DKMT_HANDLE` de 32 bits do `dxgkrnl`, nunca convertido em ponteiro KMD;
- um token explicito no private ABI liga a abertura a allocation adapter-owned;
  token, tamanho e alinhamento precisam coincidir integralmente;
- cada abertura cria handle device-specific com parent no device e referencia
  separada na allocation global; device e allocation ficam busy ate o close;
- open em lote faz validacao previa e rollback; close rejeita duplicatas,
  owner/device cruzado, forged e stale handles antes de mutar o registry;
- testes offline cobrem token ausente/duplicado/desconhecido, rollback parcial,
  referencias busy, close duplicado e tombstones;
- isso continua sendo somente lifecycle estrutural: o gate
  `AllocationAbiReady` permanece desligado em producao, nenhum segmento foi
  anunciado e nenhuma memoria fisica foi materializada.

Marco de diagnostico e teardown de 2026-08-25:

- `CollectDbgInfo` produz somente snapshot versionado de software: gates,
  registry, contadores e sequencia de probe;
- o caminho de diagnostico e auditado para nao acessar MMIO, PCI config ou
  qualquer transporte de hardware;
- a primeira falha NTSTATUS e preservada atomicamente e nao e substituida por
  erros posteriores;
- buffer curto falha com `STATUS_BUFFER_TOO_SMALL`; adapter forged falha sem
  dereferencia; a extensao informa offset DMA zero porque nao existe execucao;
- testes offline esgotam todos os 128 tombstones do registry, confirmam
  `STATUS_INSUFFICIENT_RESOURCES`, executam Stop/teardown e verificam que uma
  DDI posterior falha com `STATUS_DEVICE_NOT_READY`;
- `AddDevice` sob falha de pool nao publica contexto parcial; Stop repetido via
  Remove e `Unload` repetido sao cobertos, incluindo desconexao das interfaces
  active-only;
- stress multithread real sob Driver Verifier ainda e necessario; o marco
  atual comprova ordenacao e gates no harness offline, nao concorrencia fisica.

Fechamento estrutural da Fase 1 em 2026-08-25:

- o harness de host passou 100 execucoes consecutivas; cada execucao realizou
  16 disputas entre `CreateProcess` e `StopDevice`, com 128 chamadas por disputa;
- o push lock fake anterior foi substituido no teste por exclusao atomica real,
  mantendo a ordem diretorio -> registry usada pelo miniport;
- em toda intercalacao, somente sucesso antes do gate,
  `STATUS_INSUFFICIENT_RESOURCES` ou `STATUS_DEVICE_NOT_READY` sao aceitos;
- depois de cada Stop, `IsActive=0`, `LiveCount=0`; o Stop adicional executado
  por Remove nao deixa pool alocado e nenhum resultado inesperado e tolerado;
- isso conclui a ABI/lifecycle offline da Fase 1. Nao comprova scheduler,
  memoria, GPUVA, MMIO, firmware, interrupcao, PM4, Vulkan ou LLM.

## Fase 2 - ownership PnP e inventario runtime integral

Status: **100% para o escopo conservador atual**.

Objetivo: produzir as evidencias que hoje estao ausentes, sem constantes.

Entregaveis:

- BARs exclusivamente de `TranslatedResourceList` + PCI readonly;
- map/unmap somente por callbacks Dxgkrnl e dentro do recurso atribuido;
- topologia VRAM por RCC/GCMC em duas passagens;
- ReBAR suportado/selecionado/atribuido mantidos como fatos distintos;
- `DxgkCbAcquirePostDisplayOwnership[2]` em fase mutante recuperavel para obter
  o framebuffer herdado;
- Discovery TMR por `DRIVER_SCRATCH_0/1/2` e assinatura real quando necessario;
- inventario de scanout, Discovery TMR, PSP TMR, page tables, rings, firmware e
  reservas de plataforma;
- selo de geracao invalidado em qualquer mudanca.

Gate de saida:

- zero flags autoatribuidos;
- soma e intersecoes de todas as regioes verificadas;
- nenhum acesso alem dos 256 MiB CPU-visible sem transporte owned especifico;
- rollback devolve ownership/display anterior;
- duas capturas fisicas identicas apos reboot limpo.

Marco fisico de post-display ownership em 2026-08-25:

- pacote 1.1.0.0 integrou `DxgkCbAcquirePostDisplayOwnership2` somente depois
  do inventario PCI/BAR/topologia/ReBAR e manteve `StartDevice` fail-closed;
- callback 2 retornou sucesso e `FrameBufferStateInitializedByFirmware`;
- framebuffer observado: 2560 x 1080, pitch 10240, 11.059.200 bytes;
- endereco fisico `0x00000000C0000000`, offset zero dentro do BAR VRAM;
- a faixa inteira passou nos limites do BAR PnP CPU-visible de 256 MiB;
- topologia repetiu 512 MiB VRAM, 256 MiB CPU-visible, tres BARs e duas
  passagens MMIO estaveis; Discovery TMR continuou `NOT_REPORTED`;
- binding terminou em Codigo 43 por `RuntimeMissing=1`, sem PM4/compute;
- evidencia em
  `docs\evidence\2026-08-25_19-29-47-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md`.

Pendencias para fechar a fase: inventario das demais reservas (PSP/TMR, page
tables, rings e firmware), selo integral de geracao e segunda captura apos
reboot limpo.

Marco de inventario parcial 1.2.0.0 em 2026-08-25:

- o contrato deixou de promover qualquer conjunto de reservas para
  `INVENTORY_COMPLETE` sem tres provas independentes: scanout sondado,
  Discovery TMR sondado e carveouts de plataforma sondados;
- o ensaio fisico retornou `INCOMPLETE`, evidence flags `0x3`, uma reserva e
  11.059.200 bytes conhecidos como ocupados;
- `RuntimeInventoryComplete=0`; ownership/staging permanecem desligados e zero
  bytes sao tratados como livres enquanto faltam os carveouts;
- SYS ativo corresponde ao hash assinado do pacote 1.2.0.0;
- detalhes em `docs\27_INVENTARIO_RUNTIME_PARCIAL_1.2.0.0.md`.

Marco de snapshot de plataforma 1.3.0.0 em 2026-08-25:

- captura física passou com schema 10, 24 leituras e duas passagens estáveis;
- GCVM base/start/end zerados: nenhuma page table herdada ativa;
- CP RB0 em repouso (`CNTL=0x00A00000`) e base sentinela `0xFEDCBAEF` fora da
  VRAM; SDMA0 desabilitado (`0x80840000`) e base zero;
- nenhum ring herdado desses engines foi promovido a reserva;
- inventário segue incompleto porque PSP/TMR e outros carveouts continuam sem
  prova; PM4/compute não foram executados;
- evidencia em
  `docs\evidence\2026-08-25_19-55-48-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\28_SNAPSHOT_REGISTRADORES_PLATAFORMA_1.3.0.0.md`.

Marco de ponteiros e mailbox 1.4.0.0 em 2026-08-25:

- 54 leituras físicas coerentes em duas passagens, schema 11;
- ponteiros e writebacks CP/SDMA todos zero; nenhum ring herdado válido;
- mailboxes PSP 35/36 não reportam endereço e a 81 é heartbeat variável;
- a descrição GPCOM precisa ser lida em `C2PMSG_64/67/69/70/71` antes de
  classificar qualquer reserva PSP;
- evidencia em
  `docs\evidence\2026-08-25_20-35-01-DESKTOP-HQAG6T4-mcdm-replace`.

Marco PSP/GPCOM 1.5.0.0 em 2026-08-25:

- schema 12, 64 leituras coerentes em duas passagens e SYS ativo confirmado;
- TOS pronto em `C2PMSG_64=0x80000000`;
- WPTR, endereço e tamanho GPCOM zerados: nenhum ring PSP herdado ativo;
- mailbox 81 confirmada como heartbeat, não reserva;
- TMR interna do PSP continua não reportada e não foi inferida por constante;
- evidencia em
  `docs\evidence\2026-08-25_20-39-11-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\30_PSP_GPCOM_READONLY_1.5.0.0.md`.

Repetição pós-reboot 1.5.0.0:

- todos os campos estruturais repetiram sem divergência;
- apenas o heartbeat PSP 81 mudou, como esperado;
- gate de repetibilidade pós-reboot: PASS;
- evidencia em
  `docs\evidence\2026-08-25_20-43-40-DESKTOP-HQAG6T4-mcdm-replace`.

Marco de inventário integral 1.6.0.0:

- inventário físico `OK`, flags `0x7`, duas reservas e 257.376.256 bytes
  CPU-visible livres;
- a metade não visível foi preservada integralmente, sem constante TMR;
- gate de inventário integral: PASS EM HARDWARE;
- evidencia em
  `docs\evidence\2026-08-25_20-46-41-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\31_INVENTARIO_OPACO_NAO_VISIVEL_1.6.0.0.md`.

Marco de runtime selado 1.7.0.0:

- segunda passagem atingiu `PREFLIGHT_READY` com ownership `0x7F`;
- TMR e staging foram alocados apenas no plano, dentro dos 256 MiB visíveis;
- zero escrita de firmware e zero chamada PSP;
- a Fase 2 está fechada para a política conservadora que reserva integralmente
  a VRAM não visível;
- evidencia em
  `docs\evidence\2026-08-26_01-36-01-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\32_PREFLIGHT_RUNTIME_SELADO_1.7.0.0.md`.

## Fase 3 - segmentos VidMm, GPUVA e page tables

Status: **25% fisico**.

Objetivo: substituir enderecos fixos por memoria administrada pelo Windows e
isolada por processo.

Entregaveis:

- `QueryAdapterInfo` derivado do inventario runtime;
- segmento local VRAM e segmento GART coerentes, sem anunciar 16 GiB fixos;
- `Create/Open/Close/DestroyAllocation` com private data validada;
- `CreateProcess/DestroyProcess`;
- root page table por processo;
- `GetRootPageTableSize` e `SetRootPageTable`;
- construtor PDE/PTE GC 10.1.3;
- `BuildPagingBuffer` para map/unmap/transfer/fill/TLB;
- poison, bounds, overflow, alinhamento e fault capture;
- CPU mapping apenas quando autorizado pelo segmento.

Gate fisico:

- duas GPUVAs de processos diferentes mapeiam buffers diferentes;
- SDMA escreve por GPUVA e nao por PA/offset presente no pacote;
- unmap provoca fault controlado, nunca acesso ao buffer anterior;
- paging e eviction sob pressao sem corrupcao;
- rollback volta ao estado pre-teste.

Marco de transação VRAM 1.8.0.0 em 2026-08-26:

- primeira escrita owned executada em 64 bytes do staging planejado;
- write/readback e restauração dos bytes originais passaram;
- nenhuma escrita atingiu scanout, VRAM não visível, PSP ou engines;
- isso prova acesso CPU à memória owned, mas ainda não prova VidMm, GPUVA ou
  acesso da GPU;
- evidencia em
  `docs\evidence\2026-08-26_01-41-23-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\33_TRANSACAO_VRAM_REVERSIVEL_1.8.0.0.md`.

Candidato de capacidades runtime 1.9.0.0 em 2026-08-26:

- `QueryAdapterInfo` passa a derivar driver caps, physical caps e um segmento
  local CPU-visible do inventario fisico integral;
- dois nos sao descritos: SDMA0 para copy/paging e Compute MEC;
- tamanho local e endereco CPU vem do snapshot runtime e do BAR PnP, nunca de
  constantes do callback;
- GART, GPUVA, GPU MMU, submit e engine operacional continuam desativados;
- build, testes offline, auditoria, catalogo e assinatura passaram;
- o segmento exclui explicitamente o scanout e o staging/TMR planejado;
- o pacote 1.9.0.0 ainda precisa do ensaio fisico schema 17 e permanece
  intencionalmente em Codigo 43;
- detalhes em
  `docs\\34_CAPACIDADES_RUNTIME_SEGMENTO_LOCAL_1.9.0.0.md`.

Resultado fisico 1.9.0.0:

- schema 17 e SYS ativo identico ao pacote;
- provider retornou dois nos, base `0x00A8C000` e tamanho `0x0F132000`;
- inventario, segundo preflight e transacao VRAM permaneceram aprovados;
- Code 43 continua intencional; VidMm, GPUVA e engine ainda nao foram ativados;
- evidencia em
  `docs\\evidence\\2026-08-26_02-01-26-DESKTOP-HQAG6T4-mcdm-replace`.

Correcao de conformidade 1.10.0.0:

- a revisao da especificacao oficial identificou que MCDM exige
  `QUERYSEGMENT4` e proibe `QUERYSEGMENT/2/3`;
- `NUMPOWERCOMPONENTS` e `HISTORYBUFFERPRECISION` foram adicionados;
- `SupportNonVGA` foi zerado;
- caps completos agora exigem gates separados de GPUVA, paging, TDR,
  preempcao, submit e history buffer; a imagem fisica mantem todos desligados;
- detalhes em
  `docs\\35_CONFORMIDADE_QUERYADAPTERINFO_MCDM_1.10.0.0.md`.

Resultado fisico 1.10.0.0:

- schema 18, SYS ativo identico ao pacote e Code 43 intencional;
- `McdmSegment4Ready=1`, base `0x00A8C000`, tamanho `0x0F132000`;
- `McdmFullCapsReady=0`, portanto GPUVA, paging e submit nao anunciados;
- inventario, segundo preflight e transacao VRAM preservados;
- evidencia em
  `docs\\evidence\\2026-08-26_02-19-18-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.11.0.0:

- a private ABI de allocation descreve segmento 1, read/write set 1,
  CPU-visible e limite derivado de `LocalSegmentBytes`;
- `GPUMMUCAPS` e `PAGETABLELEVELDESC` foram implementados atras de
  `FullMcdmReady` e `GpuVaReady`;
- o provider de producao mantem esses gates em zero ate existir root page table
  e paging fisicos;
- detalhes em
  `docs\\36_ALLOCATION_SEGMENTO_GPUVA_LATENTE_1.11.0.0.md`.

Resultado fisico 1.11.0.0:

- schema 19 e SYS ativo identico ao pacote;
- `AllocationSegmentSet=1`, limite `0x0F132000`;
- `GpuVaDescriptorReady=0`, conforme o gate fail-closed;
- evidencia em
  `docs\\evidence\\2026-08-26_03-32-48-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.12.0.0:

- `SetRootPageTable` associa `SegmentId/SegmentOffset` ao processo dono do
  contexto, com alinhamento, bounds e ownership;
- `GetRootPageTableSize` permanece zero para a hierarquia fixa de quatro
  niveis, conforme a semantica WDDM;
- producao mantem `RootPageTableAbiReady=0` ate existir paging real;
- detalhes em `docs\\37_ROOT_PAGE_TABLE_OWNED_1.12.0.0.md`.

Resultado fisico 1.12.0.0:

- schema 20, SYS ativo identico ao pacote e Code 43 intencional;
- `RootPageTableAbiReady=0`, `RootPageTableMaxEntries=0`;
- geometria e transacao VRAM preservadas;
- evidencia em
  `docs\\evidence\\2026-08-26_08-13-48-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.13.0.0:

- `BuildPagingBuffer` traduz `UPDATE_PAGE_TABLE` CPU-virtual imediatamente;
- `DXGK_PTE` local vira PTE GC10 com base do segmento fisicamente derivada;
- validacao em duas passagens impede escrita parcial;
- system/GART, large pages, TLB flush e demais operacoes permanecem fechados;
- detalhes em `docs\\38_UPDATE_PAGE_TABLE_CPU_VIRTUAL_1.13.0.0.md`.

Resultado fisico 1.13.0.0:

- gate nao aprovado: `PagingCpuUpdateReady` ausente;
- causa: pacote assinado com SYS anterior a mudanca de telemetria;
- o empacotador agora bloqueia SYS mais antigo que qualquer fonte do driver;
- evidencia em
  `docs\\evidence\\2026-08-26_08-19-47-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.14.0.0:

- remove o falso TLB flush por SDMA NOP;
- modela GFXHUB engine 17, request/ACK, range, leitura dummy e VMID 1..15;
- execucao MMIO permanece fechada e `TlbFlushPhysicalReady=0`;
- detalhes em `docs\\39_GFXHUB_TLB_PLAN_PROVENANCE_1.14.0.0.md`.

Resultado fisico 1.14.0.0:

- schema 22 e SYS ativo identico ao pacote;
- `PagingCpuUpdateReady=0` e `TlbFlushPhysicalReady=0` presentes;
- procedencia/observabilidade aprovadas, sem write ou invalidacao TLB;
- evidencia em
  `docs\\evidence\\2026-08-26_08-27-42-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.15.0.0:

- write MMIO autorizado e restrito a range/request GFXHUB engine 17;
- request VMID 8 seguido da leitura dummy exigida para GC 10.1.3;
- ACK `0xA318` somente leitura, rejeitando all-ones e timeout acima de 1000 us;
- range anterior restaurado e conferido em sucesso ou falha;
- detalhes em `docs\\40_GFXHUB_TLB_TRANSACAO_FISICA_1.15.0.0.md`.

Resultado fisico 1.15.0.0:

- request engine 17/VMID 8 aceito e relido: `0x00F80100`;
- ACK `0x00000000 -> 0x00000100` no primeiro poll;
- range integral programado e estado anterior restaurado;
- `TlbFlushPhysicalReady=1` com SYS ativo identico ao pacote;
- evidencia em
  `docs\\evidence\\2026-08-26_08-51-40-DESKTOP-HQAG6T4-mcdm-replace`.

Resultado fisico 1.16.0.0:

- pagina de tabela e alvo alocados pelo allocator owned;
- PTE `VALID|READ|WRITE` escrita, relida e restaurada;
- VMID8 programado ainda desabilitado, TLB fisica concluida e todos os
  registradores restaurados;
- evidencia em
  `docs\\evidence\\2026-08-26_09-33-47-DESKTOP-HQAG6T4-mcdm-replace`.

Resultado fisico 1.17.0.0, refinado pelo pacote seguinte:

- o KMD escreveu `0x00020000`/`0x000C0000` e o PSP publicou o bit 31;
- o poll provou resposta TOS, mas ainda nao verificava o status baixo;
- evidencia preservada em
  `docs\\evidence\\2026-08-26_12-26-53-DESKTOP-HQAG6T4-mcdm-replace`.

Resultado fisico 1.18.0.0:

- exige `(C2PMSG_64 & 0x8000FFFF) == 0x80000000`;
- teste de regressao mantem cada comando stale e comprova timeout fail-closed;
- schema 26 e auditor recusam os dois falsos ACKs do pacote anterior;
- create/destroy passaram no primeiro poll com status baixo zero;
- restauracao integral e `PspRingPhysicalReady=1`;
- evidencia em
  `docs\\evidence\\2026-08-26_12-50-42-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\\43_PSP_ACK_ESTRITO_1.18.0.0.md`.

## Fase 4 - PSP, TMR e firmware owned

Status: **contratos offline 78%; hardware 65%**.

Resultado fisico 1.19.0.0:

- GPCOM create e destroy com ACK estrito;
- `LOAD_IP_FW` consumido, fence PSP `0 -> 1` no segundo poll;
- resposta bruta `0xFFFF0008` e teardown integral;
- firmware recusado porque o header AMD de 256 bytes foi incluido no payload;
- evidencia em
  `docs\\evidence\\2026-08-26_13-12-27-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.20.0.0:

- autentica o arquivo completo de 33.792 bytes;
- envia apenas `PA + 0x100`, 33.536 bytes, como no ensaio legacy aprovado;
- checksum e medido do silicio e nao fixado no KMD;
- detalhes em `docs\\45_PSP_LOAD_IP_FW_PAYLOAD_1.20.0.0.md`.

Resultado fisico 1.20.0.0:

- fence PSP novamente no segundo poll, mas resposta `0xFFFF0008`;
- diferenca restante: `PA + 0x100` desalinhada, enquanto o legacy copiava o
  payload para o inicio alinhado de uma alocacao nova;
- evidencia em
  `docs\\evidence\\2026-08-26_13-16-37-DESKTOP-HQAG6T4-mcdm-replace`.

Resultado fisico 1.21.0.0:

- payload movido para PA alinhada;
- SHA-256 separado do payload validado antes do DMA;
- PAs completas de ring/comando/fence/payload persistidas no schema 29;
- PSP retornou status zero, fence `0 -> 1` em tres polls e checksum fisico
  `0x55B182E3`;
- destroy e restauracao passaram, sem buffer liberado enquanto pertencente ao
  PSP;
- evidencia em
  `docs\evidence\2026-08-26_13-21-21-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\\46_PSP_PAYLOAD_ALINHADO_1.21.0.0.md`.

Resultado fisico 1.22.0.0:

- reset F32 SDMA0 transacional apos o `LOAD_IP_FW` aprovado;
- readback das mascaras de clock/reset e amostragem do PC fisico;
- PC avancou de `385` para `932`, com 12 transicoes em 16 amostras;
- readback da configuracao e restauracao integral passaram;
- evidencia em
  `docs\evidence\2026-08-26_13-30-47-DESKTOP-HQAG6T4-mcdm-replace`;
- detalhes em `docs\47_SDMA_F32_ATIVA_1.22.0.0.md`;
- ainda nao constitui submissao nem aceleracao.

Proximo gate:

- ring SDMA owned, com enderecos MC derivados da topologia detectada;
- pacote `COPY` seguido de fence com poison;
- exigir `RPTR == WPTR`, fence escrita pela SDMA e comparacao integral do
  destino;
- rollback do ring e de toda memoria owned.

Candidato 1.23.0.0 implementado para esse gate:

- duas paginas CPU-visible obtidas do allocator owned;
- enderecos MC derivados da topologia runtime;
- `COPY_LINEAR` de 256 bytes seguido de fence com poison;
- schema 31 exige `RPTR == WPTR`, fence, 64 dwords corretos e rollback;
- detalhes em `docs\48_SDMA_RING_COPY_FENCE_1.23.0.0.md`.

Resultado fisico 1.23.0.0:

- F32 permaneceu ativa e `STATUS_REG` mudou no kick;
- RPTR ficou zero e a fence permaneceu no poison por 1000 polls;
- registradores e memoria foram restaurados integralmente;
- causa isolada: campo endian/reservado do COPY estava no dword final, em vez
  de preceder os enderecos;
- evidencia em
  `docs\evidence\2026-08-26_17-47-29-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.24.0.0:

- layout COPY Navi10 corrigido para `header,count,endian,src,dst`;
- GCR e FENCE MTYPE_UC mantidos;
- schema 32 registra WPTR no registrador, shadow e RB_CNTL aplicado;
- detalhes em `docs\49_SDMA_COPY_LAYOUT_1.24.0.0.md`.

Resultado fisico 1.24.0.0:

- WPTR chegou a 64 tanto no registrador quanto no shadow;
- `RB_CNTL=0x80841015`, F32 ativa e rollback integral;
- RPTR permaneceu zero e a fence no poison;
- portanto o layout corrigido nao foi suficiente para fazer o consumidor
  buscar o ring;
- evidencia em
  `docs\evidence\2026-08-26_17-53-47-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.25.0.0:

- reproduz o `WRITE_LINEAR` de quatro dwords aprovado no legacy;
- usa primitivas de registro para todas as leituras/escritas BAR0;
- mantem GCR, fence UC e teardown integral;
- schema 33 acrescenta header, RPTR MMIO e controle de polling;
- detalhes em `docs\50_SDMA_WRITE_LINEAR_1.25.0.0.md`.

Resultado fisico 1.25.0.0:

- firmware/F32 e rollback continuaram aprovados;
- header, `WPTR` registrador/shadow e `RB_CNTL` foram confirmados;
- `RPTR` registrador/writeback permaneceu zero e o fence no poison;
- evidencia: `docs/evidence/2026-08-26_18-01-43-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.26.0.0:

- reserva pelo allocator o layout contiguo exato comprovado no legacy em
  `0x0FF00000..0x0FF04FFF`;
- schema 34 captura STATUS1/2/3, ERROR_LOG e estados/XNACK/timeout UTCL1;
- detalhes em `docs\51_SDMA_LAYOUT_LEGACY_DIAGNOSTICO_1.26.0.0.md`.

Resultado fisico 1.26.0.0:

- firmware/F32 permaneceu aprovado;
- reserva exata em `0x0FF00000..0x0FF04FFF` colidiu no allocator;
- o ensaio abortou antes de qualquer escrita ou submissao;
- evidencia: `docs/evidence/2026-08-26_18-09-01-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.27.0.0:

- aloca um unico bloco owned contiguo de 20 KiB em endereco livre;
- preserva os deslocamentos relativos ring/RPTR/WPTR/fence/destino do legacy;
- schema 35 registra colisao fixa, modo dinamico, MCs e diagnosticos UTCL;
- detalhes em `docs\52_SDMA_LAYOUT_CONTIGUO_OWNED_1.27.0.0.md`.

Resultado fisico 1.27.0.0:

- ring consumido: `RPTR MMIO == WPTR == 68`;
- fence fisica mudou de `0x43DAFFFE` para `0xBC250123`;
- writeback de RPTR permaneceu em 52, igual ao legado funcional;
- rollback integral; comparacao de dados nao executada pelo gate antigo;
- evidencia: `docs/evidence/2026-08-26_18-12-58-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.28.0.0:

- conclui pelo RPTR MMIO junto da fence fisica;
- preserva RPTR writeback como diagnostico;
- prossegue obrigatoriamente para a comparacao dos quatro dwords;
- detalhes em `docs\53_SDMA_GATE_RPTR_MMIO_DADOS_1.28.0.0.md`.

Resultado fisico 1.28.0.0 - marco SDMA:

- firmware aceito pelo PSP e F32 fisicamente ativa, com 14 transicoes;
- `RPTR MMIO == WPTR == 68`;
- fence GPU `0x43DAFFFE -> 0xBC250123`;
- quatro dwords produzidos, zero divergencias contra a CPU;
- rollback de registradores e memoria `1/1`;
- evidencia: `docs/evidence/2026-08-26_18-15-42-DESKTOP-HQAG6T4-mcdm-replace`;
- **SDMA validada nesta operacao fisica**;
- PM4/compute permanecem nao validados.

Proxima frente ativa:

- integrar no miniport a carga RLC tipo 8 ja comprovada no laboratorio;
- exigir `RLC_GPM_STAT.EXEC_ROM_CODE: 1 -> 0` e `RLC_ENABLE_F32` fisicos;
- usar direct-load owned para PFP/ME/CE/MEC/JT1, pois o PSP recusa os tipos CP
  nesta unidade com `0x80000203`;
- somente depois criar MQD/HQD, submeter PM4 e fechar o tripe compute.

Candidato 1.29.0.0:

- inclui o firmware RLC autenticado no pacote;
- generaliza o transporte PSP provado para o tipo 8 sem aplicar a verificacao
  de checksum especifica da SDMA;
- exige fence/resposta PSP e a transicao fisica
  `RLC_GPM_STAT.EXEC_ROM_CODE: 1 -> 0`;
- registra `RLC_CNTL`, `RLC_GPM_STAT`, enable F32 e restore no schema 37;
- detalhes em `docs\54_RLC_PSP_EXEC_ROM_1.29.0.0.md`.

Resultado fisico 1.29.0.0:

- PSP aceitou RLC tipo 8 com fence `1/1`, resposta zero e teardown completo;
- `RLC_GPM_STAT.EXEC_ROM_CODE: 1 -> 0`;
- `RLC_CNTL.RLC_ENABLE_F32=1`, seguido de restore confirmado;
- evidencia:
  `docs/evidence/2026-08-26_18-28-15-DESKTOP-HQAG6T4-mcdm-replace`;
- RLC validado no escopo de carga e saida da ROM; PM4/compute nao executados.

Candidato 1.30.0.0:

- direct-load isolado do PFP em VRAM owned CPU-visible;
- autentica arquivo/payload e compara 65792 dwords depois do staging;
- programa IC base/jump table write-only sem fabricar readback;
- libera apenas `PFP_HALT`, mede 32 amostras do PC e faults GFXHUB;
- restaura `CP_ME_CNTL`, `RLC_CNTL` e toda a VRAM;
- schema 38; detalhes em
  `docs\55_PFP_DIRECT_LOAD_ISOLADO_1.30.0.0.md`.

Resultado 1.30.0.0: o RLC ja estava fora da ROM no preflight; a recarga o
devolveu a ROM e o gate bloqueou antes de qualquer staging/escrita do PFP.
Evidencia: `docs/evidence/2026-08-26_22-19-24-DESKTOP-HQAG6T4-mcdm-replace`.

Candidato 1.31.0.0: reconhece RLC ja ativo (`ROM 0 -> 0` + F32 observado),
evita a recarga destrutiva e libera o primeiro ensaio fisico real do PFP.

Resultado 1.31.0.0: PSP aceitou o RLC, mas `RLC_CNTL=0` e `ROM 1 -> 1`;
PFP corretamente nao executado. O 1.32 ativa explicitamente o F32 depois da
carga PSP e mede a saida da ROM antes de prosseguir.

Resultado fisico 1.33.0.0:

- gate RLC isolado aprovado sem tela preta;
- `RLC_CNTL 0 -> 1`, `EXEC_ROM_CODE 1 -> 0`, F32 ativo e restore confirmado;
- SDMA completa repetida com sucesso;
- PFP deliberadamente nao executado;
- evidencia:
  `docs/evidence/2026-08-26_22-32-01-DESKTOP-HQAG6T4-mcdm-replace`;
- a tela preta do 1.32 fica isolada no caminho PFP, nao na ativacao do RLC.

Objetivo: inicializar firmware sem memoria fabricada nem DMA use-after-free.

Entregaveis:

- TMR reservada pelo allocator owned;
- PA e MC derivados do mesmo token/snapshot;
- staging CPU-visible com lease, backup, SHA-256, readback e restauracao;
- `SETUP_TMR` transacional;
- ordem `PFP -> ME -> CE -> MEC -> MEC_JT1` com hashes reais;
- estados separados `TRANSPORT_PROVEN`, `FW_ACCEPTED` e `FW_ACTIVE`;
- `DESTROY_TMR` no teardown; em timeout, memoria permanece pinada ate reset.

Gate fisico:

- PSP ring/fence/response de comando suportado;
- cinco firmwares aceitos com status cru;
- versao e estado fisico de cada engine;
- nenhum byte fora das reservas;
- reboot/recovery comprovados em falha injetada.

## Fase 5 - IH, monitored fences e TDR

Status: **contratos offline 40%; hardware 0%**.

Objetivo: possuir conclusao assincrona e recuperacao antes de liberar submit.

Entregaveis:

- ring IH owned e programado;
- ISR valida source/client/ring/VMID e apenas agenda DPC;
- DPC atualiza monitored fences pelo callback correto do Dxgkrnl;
- timeout por engine, coleta de debug e primeira falha preservada;
- preempcao real;
- reset de queue/engine e escalada para reset do adapter;
- surprise-remove e D3 sem DMA posterior ao free.

Gate fisico:

- interrupcao real causada pela engine;
- fence monitorada acorda o scheduler sem polling de user mode;
- interrupcao espuria nao conclui trabalho;
- hang injetado aciona TDR e retorna a um estado conhecido;
- Driver Verifier sem leak/bugcheck.

## Fase 6 - submissao SDMA pelo Dxgkrnl

Status: **laboratorio legacy provado; miniport 0%**.

Objetivo: primeira engine funcional do miniport usando allocations e GPUVA
reais.

Entregaveis:

- contexto SDMA e ring por adapter/context;
- command validation fechada;
- `SubmitCommandVirtual` ou caminho selecionado conforme GPUVA;
- WPTR escrito somente depois de flush/barrier;
- fence EOP em allocation monitored;
- status/fault antes, durante e depois;
- assinatura de fence-sem-dado condena a engine.

Gate fisico:

- 1.000 `CONST_FILL/COPY` alternados;
- wrap, idle/resume e multiplos contexts;
- RPTR/WPTR + fence GPU + comparacao integral em cada operacao;
- nenhum fallback CPU;
- timeout e teardown deterministas.

## Fase 7 - CP/PM4 GFX ring pelo miniport

Status: **RLC fisico aprovado; candidato PFP isolado construido; PM4 0%**.

Objetivo: destravar `JT_LOADED`, executar PM4 e fechar o gate que bloqueia
compute.

Entregaveis:

- golden settings por banco com snapshot/rollback completo;
- constants, CAM/GPA, RLC e CP em fases separadas;
- ring GFX owned, RPTR writeback e monitored fence;
- PM4 allowlist: NOP, WRITE_DATA, RELEASE_MEM e IB inicialmente;
- parser rejeita packet3 invalido, privilegio e endereco fora da allocation;
- fault capture CP/GRBM/GCVM.

Gate fisico T2:

- `JT_LOADED` fisico no estado esperado;
- PCs variam e deixam o polling `0x041E`;
- WPTR avanca, RPTR alcanca;
- fence e escrita B substituem poisons A;
- GRBM/CP reage e GFXOFF sai durante a carga;
- repeticao, wrap, idle e reset.

## Fase 8 - MEC, MQD/HQD e dispatch compute

Status: **contratos offline 40%; hardware 0%**.

Objetivo: primeira fila compute real e isolada.

Entregaveis:

- MQD 512-byte em allocation GPU;
- EOP buffer, HQD base/control, RPTR report, WPTR poll e doorbell;
- VMID por processo/contexto;
- KIQ ou caminho privileged equivalente para map/unmap queue;
- ISA validator GFX10.1.3;
- dispatch de shader Wave32 inicialmente;
- SGPR/VGPR/LDS/scratch declarados e limitados.

Gate fisico:

- shader grava vetor conhecido;
- matriz pequena e reducao comparadas bit a bit/tolerancia definida;
- duas filas/processos sem vazamento de memoria;
- caso negativo sem kick/opcode adulterado nao produz resultado;
- RPTR/WPTR, fence, resultado, clock, temperatura e GFXOFF completos.

Ao fim desta fase existe **compute minimo real no Windows**, ainda sem Vulkan.

## Fase 9 - runtime compute e kernels para LLM

Status: **0%**.

Objetivo: executar um LLM antes do ICD Vulkan completo, reduzindo o caminho de
diagnostico.

Entregaveis:

- UMD privado `bc250compute.dll` sobre D3DKMT/MCDM;
- buffers, upload/download, command lists, events e synchronization;
- cache de shaders/ISA com hashes;
- kernels: copy, quantize/dequantize, matmul Q4/Q8, RMSNorm, RoPE, softmax,
  elementwise e attention;
- backend experimental do `ggml/llama.cpp` sem fallback silencioso;
- contador de bytes/operacoes e telemetria por dispatch.

Gate fisico:

- testes unitarios de cada kernel contra CPU;
- modelo GGUF pequeno gera logits/tokens iguais dentro da tolerancia;
- `-ngl` efetivamente coloca camadas na GPU;
- remocao do kick ou do shader reprova o teste;
- throughput reportado somente com GPU confirmada.

Ao fim desta fase existe **LLM acelerado pelo backend privado**, util para
validar a pilha antes de Vulkan.

## Fase 10 - Vulkan ICD compute-only minimo

Status: **0%**.

Objetivo: oferecer a API exigida pelo backend Vulkan do `llama.cpp`.

Escopo Vulkan inicial:

- loader negotiation e manifest JSON do ICD;
- instance, physical device e logical device;
- uma queue family compute + transfer;
- memory heaps/types derivados de VidMm;
- buffers e bind de memoria;
- descriptor set layouts/pools/sets;
- shader modules SPIR-V e pipeline compute;
- command pools/buffers;
- pipeline barriers;
- fences, semaphores e timestamp queries;
- queue submit/wait idle;
- propriedades, features, limits e formats estritamente verdadeiros.

Fora do primeiro corte:

- surface/swapchain/present;
- graphics pipeline, rasterizacao e display;
- extensoes nao exigidas pelo consumidor escolhido.

Gate:

- Loader encontra somente o ICD instalado e enumera `1002:13FE`;
- `vulkaninfo` serve apenas como enumeracao, nao prova de compute;
- Vulkan compute sample passa com prova do KMD;
- validation layers sem erros no subconjunto implementado;
- nenhuma feature/extension anunciada sem implementacao.

## Fase 11 - llama.cpp pelo backend Vulkan

Status: **0%**.

Objetivo: executar o `llama.cpp` upstream com `GGML_VULKAN=ON`.

Entregaveis:

- matriz das features/extensoes realmente usadas pela revisao fixada do
  `llama.cpp`;
- suporte aos kernels SPIR-V do backend ou fallbacks GPU equivalentes;
- memory budgeting, staging e cache de pipelines;
- suporte a modelos maiores que a VRAM por offload parcial;
- ferramenta de evidencia que correlaciona cada benchmark a submissions KMD.

Gate final LLM/Vulkan:

- `llama-cli --list-devices` encontra a BC-250;
- modelo pequeno com `-ngl 99` roda sem backend CPU para as camadas offloaded;
- logits/tokens comparados ao CPU dentro da tolerancia;
- RPTR/WPTR, fences e resultados registrados durante inferencia;
- clock/temperatura/GFXOFF reagem;
- `llama-bench` reporta prompt processing e token generation reproduziveis;
- teste negativo com ICD desabilitado ou submit bloqueado nao produz o mesmo
  resultado por fallback silencioso.

Ao fim desta fase existe o produto minimo solicitado: **driver Windows MCDM +
Vulkan compute ICD + LLM real na BC-250**.

## Fase 12 - robustez, instalacao e distribuicao

Status: **0%**.

Entregaveis:

- HLK/WHQL quando aplicavel ao alvo;
- Driver Verifier prolongado;
- stress multi-processo, paging, suspend/resume, reboot e update;
- ETW/WPP, dumps e simbolos;
- instalador/rollback transacional;
- versionamento KMD/UMD/ICD e compatibilidade de ABI;
- assinatura de distribuicao e politica de update.

Gate:

- 24 h de carga sem leak, hang, corrupcao ou bugcheck;
- atualizacao e downgrade recuperaveis;
- falhas injetadas preservam memoria e display;
- pacote nao depende de modo de teste para distribuicao final.

## Fase 13 opcional - Vulkan graphics e full WDDM display

Objetivo: adicionar graphics queues, render passes/dynamic rendering,
rasterizacao, images/formats, presentation, VidPN e DCN. Esta fase e muito
maior e nao e requisito para LLMs ou Vulkan compute.

## 3. Caminho critico

```text
F0 recovery/fail-closed
  -> F1 lifecycle MCDM
  -> F2 ownership/inventario
  -> F3 VidMm/GPUVA
  -> F4 PSP/firmware
  -> F5 IH/TDR
  -> F6 SDMA pelo Dxgkrnl
  -> F7 CP/PM4
  -> F8 MEC compute
  -> F9 runtime/kernels LLM
  -> F10 Vulkan ICD
  -> F11 llama.cpp Vulkan
  -> F12 robustez/distribuicao
```

Fases nao podem ser puladas por retorno de API. Em especial, criar um ICD que
faz `vulkaninfo` enumerar antes de F8 nao aumenta o progresso: sem queue fisica,
fence GPU e resultado, ele e apenas uma fachada proibida pelas regras.

## 4. Proximo incremento autorizado

O pacote 1.16.0.0 associou uma page table owned ao VMID 8, escreveu uma PTE
para um buffer owned, programou PT base/start/end com o contexto desabilitado,
executou TLB e restaurou VRAM/registradores. O ensaio fisico foi aprovado;
detalhes em `docs\41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md`.

O pacote 1.18.0.0 aprovou create/destroy do ring GPCOM owned com ACK estrito.
O candidato 1.19.0.0 envia `LOAD_IP_FW` para SDMA0 usando firmware autenticado,
fence PSP, resposta bruta, checksum e teardown. Se aprovado, o proximo gate
configura um ring SDMA owned ainda em enderecamento fisico/bypass e comprova
RPTR/WPTR, fence GPU, copia integral, faults e rollback. Manter
`PagingCpuUpdateReady=0`,
`RootPageTableAbiReady=0` e `GpuVaDescriptorReady=0` ate esse gate completo.
Monitored fence/IH/TDR e submit owned continuam obrigatorios antes do primeiro
`StartDevice=STATUS_SUCCESS`.
