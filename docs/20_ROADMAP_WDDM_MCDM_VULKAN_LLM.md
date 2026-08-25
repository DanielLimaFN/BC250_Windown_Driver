# Roadmap BC-250: MCDM/WDDM, Vulkan compute e LLMs no Windows

Atualizado em 2026-08-25.

Estado inicial: **NAO VALIDADO EM HARDWARE**.

```text
Conformidade BC250_HARDWARE_RULES   45%
Miniport MCDM funcional            20%
Memoria/GPUVA WDDM                  0%
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

Status: **em andamento, 75%**.

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
- sincronizar `C:\BC250_RECOVERY.cmd` continua exigindo autorizacao explicita.

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

Status: **0%**.

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

## Fase 3 - segmentos VidMm, GPUVA e page tables

Status: **0%**.

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

## Fase 4 - PSP, TMR e firmware owned

Status: **contratos offline 60%; hardware 0%**.

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

Status: **0% no miniport; bloqueio historico conhecido**.

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

Implementar a Fase 1 em pequenos cortes:

1. manter memoria, paging e submit fail-closed;
2. auditar a divergencia entre recovery instalado e fonte sem alterar o host;
3. sincronizar recovery somente com autorizacao explicita;
4. concluir o binding fail-closed da Fase 0;
5. iniciar a Fase 2 com ownership PnP e inventario runtime integral.
