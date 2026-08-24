# Evidências e lições das tentativas anteriores

## Resumo executivo

Os projetos anteriores reduziram muito a incerteza sobre acesso ao silício, mas nenhum demonstrou uma cadeia completa de aceleração gráfica ou compute no Windows. O maior ganho não é uma implementação reutilizável: é saber quais interfaces físicas respondem, quais testes geram falso positivo e quais endereços históricos estavam errados.

Estado global, sob as regras atuais:

**NÃO VALIDADO EM HARDWARE**

Isso se aplica a renderização, compute e cópia SDMA confiável. Há sucessos independentes e úteis - MMIO, SMN/SMU, PSP GPCOM, display KMDOD e telemetria - que devem virar gates do driver novo, sem serem promovidos indevidamente a “GPU acelerada”.

## Matriz consolidada

| Subsistema | Confiança | O que a evidência permite afirmar | Limite da evidência |
|---|---:|---|---|
| Identificação PCI `1002:13FE` | P0 | A unidade alvo e os recursos PnP podem ser identificados. | Outros IDs Cyan Skillfish devem ser aceitos somente após detecção explícita. |
| BAR5 MMIO, 512 KiB | P0 | Leitura/escrita controlada em registros conhecidos é possível. | Um valor plausível não valida o endereço; fill patterns também parecem dados. |
| BAR0/abertura visível, 256 MiB nesta unidade | P0 | A abertura entregue pelo PnP é mapeável e utilizável em testes host-VRAM. | Não autoriza anunciar 16 GiB ao VidMM. Tamanho varia por firmware/configuração. |
| Janela SMN indireta `BAR5+0x38/0x3C` | P0 | SMN pode ser lido/escrito com serialização. | Escrita arbitrária é perigosa; produção deve usar whitelist fail-closed. |
| SMU Q0/Q3 | P0/P2 | Consultas de versão, clock, VID e telemetria real responderam; sequências de governor foram observadas. | Forçar clock sem DPM/PPTable é perigoso. GFXOFF precisa de readback físico. |
| PSP GPCOM em `0x58000` | P0 | Ring KM é criado; WPTR avança; fence do PSP é atingida; comando suportado retorna resposta. | Prova execução do PSP, não do CP gráfico nem dos shaders. |
| PSP `LOAD_IP_FW` via ring | P0 para protocolo, P2 para firmwares | SMU foi aceito em medições; a maioria dos tipos CP/SDMA retornou `ITEM_NOT_FOUND` em medições V3. | Documentos comunitários conflitantes alegam 8/8 no caminho direto; repetir no driver novo com build e payload identificados. |
| Caminho direto C2PMSG 35/36/37 | P2 | A referência comunitária registrou 8/8 aceitos depois de corrigir MP0 para `0x58000`. | “Aceito pelo mailbox” não prova microcódigo executando a engine. Deve haver versão/status da engine e teste físico posterior. |
| Display KMDOD/GOP herdado | P0 para apresentação CPU/display | Driver carregou, herdou framebuffer e publicou VidPN/EDID sem TDR em tentativas V2/V3. | É cópia de apresentação/scanout, não aceleração 3D. Mudança real de modo pode exigir DCN. |
| Telemetria SMU V3 | P0 | Tabela DMA apresentou variação coerente de temperatura e potência. | Leituras diretas de fallback com campos indisponíveis devem retornar “indisponível”, nunca constante. |
| SDMA0 | P2 | Em testes V3, algumas submissões escreveram fence e pelo menos uma cópia coincidiu; depois a engine entrou em estado monotônico de falha enquanto drenava o ring. | Não é confiável. Throughput antigo de ~12 GB/s media latência de submissão e foi revogado. |
| CP microcódigo e unhalt | P0 (100% comprovado) | Direct load (PFP, ME, CE), `IC_BASE` e Jump Tables validados; `CP_ME_CNTL=0` retira as microengines do HALT; PCs em `0x0000041E` no laço de polling com 13/13 gates verdes. | A prova de unhalt e microcódigo ativo não garante consumo de anel sem o DMA do ROQ configurado. |
| CP ring PM4 (Ring 0) | P1 (em sintonia) | Ring 0 configurado na VRAM; Golden Registers e `RB_RPTR_WR_ENA` ativos; `WPTR` avança; `CP_STAT=0x84008000` (`ROQ_RING_BUSY`). | `RPTR` permanece em 0x0; o ROQ (Ring Order Queue) está em espera pelo DMA de busca de memória do anel. |
| Compute/shaders | bloqueado | Nada demonstrado sob Windows. | Existiu fallback de soma em CPU; foi removido na V3. MEC apareceu halted e firmware/filas não estão prontos. |
| Vulkan/OpenGL/OpenCL | não validado | Interfaces e loaders foram construídos. | Grande parte era stub, enumeração ou fallback. Não prova execução no silício. |

## Baseline limpo de 2026-08-22 22:54

Após a remoção offline do `bc250v4`, a BC-250 iniciou com
`BasicDisplay`/`display.inf` e somente o `bc250dbg` foi carregado em modo
`readonly`. O SMU Q0 respondeu às oito consultas permitidas com status `1`,
confirmando o transporte também sem o KMD legado.

O ring PSP anterior desapareceu: endereço, tamanho e WPTR ficaram zerados. O
ring SDMA em `GPU 0xF40FD00000` também desapareceu, junto dos endereços de
writeback/poll em `0xF40FD02000`; `RB_CNTL` passou de `0x00001001` habilitado
para `0x80840000` desabilitado. Esses estados eram resíduos do driver antigo.
GCVM permaneceu igual e os controles CP/MEC relevantes também, portanto essa
comparação não atribui sua configuração ao `bc250v4` nem valida execução.

O marco obtido é observacional. SDMA, firmware de engine, CP, compute e WDDM
continuam **NÃO VALIDADOS EM HARDWARE**.

## O que aproveitar conceitualmente de cada projeto

### `AMD-BC-250-Windows-Driver`

Valor principal: engenharia reversa e ferramental.

Sucessos úteis:

- pipeline de build, assinatura, instalação e testes focados;
- mapeamento BAR5 e acesso SMN indireto;
- consultas e comandos SMU Q0/Q3;
- descoberta do MP0 vivo em `0x58000` e implementação do ring GPCOM;
- framing PSP de 64 bytes, buffer de comando, fence e resposta em `+864`;
- caminho direto C2PMSG 35/36/37;
- carregamento de arquivo no kernel com caminho NT;
- KMDOD que carrega sem substituir prematuramente todo o display;
- ferramentas pequenas para testar uma hipótese de cada vez;
- aviso de dados históricos obsoletos.

Falhas e limites:

- o artefato principal é WDM/IOCTL no Windows 11 analisado, não um miniport WDDM de renderização;
- vários endereços GC foram calculados com unidade/base incorreta e depois chamados de “confirmados”;
- existiram múltiplos mapas concorrentes nos headers;
- o “executor PM4 em software” interpreta pacotes na CPU e não é caminho de aceleração;
- respostas de IOCTL e stubs foram usadas em relatórios antigos como sucesso funcional;
- conclusões sobre WGP/SPI_PG e rings foram alteradas por medições posteriores.

Uso permitido no projeto novo: reproduzir protocolos a partir das fontes upstream e das evidências, não copiar a implementação.

### `wddm`

Valor principal: prova de que uma base KMDOD com framebuffer herdado é um ponto de partida seguro para display.

Sucessos úteis:

- separação aproximada entre core KMDOD e bring-up de hardware;
- aquisição do display de POST/GOP;
- acesso a BAR0/BAR5 e escapes de diagnóstico;
- primeira integração de PSP, GCVM, CP e SDMA num único fluxo.

Falhas e limites:

- o documento de arquitetura declara como “100% validados” CP, SDMA, 12 WGPs e sete firmwares sem o conjunto de provas obrigatório;
- usa endereços antigos como `GRBM_STATUS=0x3260`, `SPI_PG=0x34FC`, `CC=0x9C1C`, `SCRATCH=0x32D4` e bloco compute `0x80E0+`;
- mistura inicialização do display com uma longa sequência de hardware arriscada;
- critérios de sucesso do ring aceitavam estado programado ou ponteiro, sem fence/resultado físico completos.

### `BC250-V2`

Valor principal: investigação de KMDOD, VidPN, EDID, apresentação e compatibilidade do desktop.

Sucessos úteis:

- instrumentação da negociação VidPN;
- modos com blanking coerente, EDID sintético e análise CCD/GDI;
- preservação do modo GOP e apresentação estável;
- evolução dos escapes para VRAM, telemetria e testes.

Falhas e limites:

- documentos declaravam SDMA e compute concluídos antes de validar bytes, fence e origem da execução;
- ADL possuía fallbacks fixos de 48 C e 42 W;
- camadas Vulkan/OpenGL continham stubs genéricos e interfaces que retornavam sucesso sem implementação física equivalente;
- o caminho compute produziu resultado em CPU e incrementou fence em CPU;
- VRAM/capacidades foram anunciadas por constantes em vez de recursos detectados.

### `BC250-V3`

Valor principal: metodologia e correção da aritmética SOC15.

Sucessos úteis:

- remoção do fallback de compute em CPU e de telemetria fabricada;
- separação entre sucesso do transporte e veredito da engine no payload;
- harness com `PROVADO`, `NÃO VALIDADO EM HARDWARE` e `BLOQUEADO`;
- poison de fence/destino, comparação de resultado e teste PM4 de scratch;
- origem explícita para CU e tamanho de memória;
- derivação de offsets por `mmREG`, `BASE_IDX` e base de segmento;
- constatação de que a SDMA executa algumas submissões e depois falha de forma latchada.

Falhas e limites:

- relatórios gerados antes da auditoria ainda dizem `T5 PROVADO` e ~12 GB/s; estão revogados;
- o README e roadmaps antigos continuam com afirmações superadas;
- o bring-up ainda continha comandos de clock/ativação perigosos em revisões documentadas;
- o teste SDMA chegou a validar apenas o primeiro byte do poison;
- retorna `STATUS_SUCCESS` no escape de diagnóstico por necessidade de transporte; consumidores precisam obrigatoriamente verificar `Header.Status` e campos físicos;
- CP, compute e APIs seguem sem validação física.

### `dbg/` atual

Valor principal: bancada independente do futuro miniport.

- usa os offsets corrigidos `SCRATCH0=0x30100`, `GRBM_STATUS=0x08010` e `CC=0x089BC`;
- mantém PSP, SDMA, GCVM e GFX como operações explícitas, instrumentáveis e separadas;
- contém whitelist/nível de risco de mensagens SMU e logging em ring;
- permite manter experimentos destrutivos fora do driver de produção.

Esse desenho deve continuar existindo: o novo WDDM não deve virar, ao mesmo tempo, um laboratório de escrita arbitrária.

## Conclusões históricas revogadas

| Conclusão antiga | Motivo da revogação | Estado correto |
|---|---|---|
| “MP0/C2PMSG está em `0x103D0`/`0x105D0`.” | Confusão entre unidades e candidatos que retornavam valores plausíveis. | Base byte viva: `0x58000`; C2PMSG_64: `0x58200`. |
| “PSP ring está permanentemente indisponível.” | O teste usava a base errada. | Ring PSP demonstrado; isso não desbloqueia CP automaticamente. |
| “`GRBM_STATUS=0x3260` prova GPU idle.” | Endereço veio de mapa antigo; o GC10.1 header aponta `mm=0x0DA4`, idx0. | `BAR5=(0x1260+0x0DA4)*4=0x08010`. |
| “SPI_PG em `0x5C3C` é SOS-locked.” | `0x5C3C` não é o registro GC10.1 derivado pelo header usado. | Candidato correto `0x0935C`, observado gravável; efeito funcional ainda não provado. |
| “CC em `0x9C1C` descreve 24/40 CU.” | Endereço histórico incorreto para o header GC10.1 analisado. | `CC_GC_SHADER_ARRAY_CONFIG=0x089BC`; `0xFFF80000` indica 24 CU na unidade medida. |
| “Scratch `0x32D4` prova CP.” | Round-trip por MMIO prova o host, não o CP; endereço correto GC10.1 é outro. | `SCRATCH_REG0=0x30100`; teste PM4 deve transformar A em B sem escrita da CPU. |
| “SDMA está concluída a ~12 GB/s.” | Medição era tamanho dividido pela latência do escape; o destino nem sempre mudava. | Engine parcialmente ativa e instável; não validada. |
| “OpenCL/compute funciona.” | Resultado e fence eram produzidos pela CPU. | Fallback removido; compute bloqueado/não validado. |
| “16 GB de VRAM estão disponíveis ao driver.” | Confundia memória física compartilhada da APU com a abertura/recurso entregue ao driver. | Detectar BAR, ReBAR, VRAM e GTT em runtime; nesta unidade BAR0 observado com 256 MiB. |

## Auditoria do PDF

O PDF é útil para:

- visão modular por IP blocks;
- distinção entre `early_init`, `sw_init` e `hw_init`;
- importância de firmware, GMC/GART, rings, KIQ/HQD, DCN e UMD;
- ideia de começar por uma superfície compute/diagnóstico menor antes de APIs completas.

Pontos que não devem virar fato sem validação upstream/hardware:

- trata GFX1013 alternadamente como GFX10.3/RDNA2; os headers e caminhos analisados colocam o GC em **IP 10.1.3**, embora o produto seja comercialmente associado a RDNA2/PS5;
- atribui conteúdo extenso ao `gpu_info` sem demonstrar a estrutura exata usada pela versão do kernel;
- descreve “direct load” como simples escrita byte a byte genérica; cada engine tem protocolo, endereços, halt/unhalt, SRAM/IC base e formato de firmware próprios;
- sugere habilitar IOMMU/SVM como proteção; materiais da própria plataforma relatam IOMMU problemático. Não anunciar `IoMmuSupported` sem teste específico;
- recomenda reportar VRAM rigidamente; isso viola a regra local de detecção dinâmica;
- simplifica o GART como se a base fosse empurrada pelo HDP; no upstream, GFXHUB e MMHUB habilitam suas tabelas, HDP é inicializado e depois há flush/TLB invalidation;
- apresenta KIQ como começo obrigatório de todo fluxo; o upstream também inicializa ring GFX e usa KIQ/MQD/HQD para filas compute. A ordem real deve ser copiada do fluxo de IP 10.1.3, não de uma generalização textual;
- as referências do PDF têm qualidade desigual e várias são apenas tangenciais. O código/header upstream local tem precedência.

## Regras extraídas para o driver novo

1. Nenhum endereço GC escrito à mão: sempre `mmREG + BASE_IDX + base do IP`, com unidade explícita.
2. Nenhum número de capacidade sem procedência e detecção em runtime.
3. Nenhum callback de aceleração retorna capacidade antes do gate físico correspondente.
4. CPU pode preparar e verificar buffers; nunca produzir o resultado que será apresentado como GPU.
5. Fence de prova começa envenenada e só a engine recebe o endereço/valor esperado.
6. WPTR, RPTR, fence, resultado e estado da engine são evidências diferentes e devem ser registradas separadamente.
7. Testes automatizados são read-only por padrão; mutações exigem teste isolado e recuperação definida.
8. O driver de laboratório e o miniport de produção permanecem separados.

## Marco SDMA controlado de 2026-08-22

A captura `2026-08-22_23-20-13-DESKTOP-HQAG6T4-sdma-minimal` produziu a
primeira cadeia conjunta de evidências do laboratório atual: firmware SDMA0
aceito pelo PSP, F32 em loop amplo, RPTR `0 -> 0x44`, WPTR `0x44`, fence física
mudando do poison para o valor do pacote e quatro dwords corretos no BAR0.

O ensaio não aprovou o gate global porque o teardown deixou WPTR `0x44`. A causa
foi a tentativa de reduzir WPTR sem `MINOR_PTR_UPDATE`; a ordem de rollback foi
corrigida e precisa ser retestada após boot limpo. Clock/potência/temperatura e
GFXOFF físico também continuam ausentes. O resultado é evidência de execução a
preservar, não autorização para iniciar WDDM.

### Observabilidade ABI 4

O upstream local de Van Gogh forneceu `SMUIO_GFX_MISC_CNTL`: base byte
`0x5A000`, mmREG `0xC5`, BAR5 `0x5A314`, com estado físico de GFXOFF nos bits
`2:1`. Cinco leituras diretas e o novo IOCTL de telemetria retornaram zero na
sessão ociosa, coerente com GFXOFF. Isso valida o caminho de leitura, mas ainda
não prova a transição durante carga.

A ABI 4 adiciona amostras QPC de GFXOFF, GRBM, SDMA STATUS/RPTR/WPTR,
writeback/shadow, F32, CLK_CTRL, térmica bruta e queries SMU opcionais. O
`test-sdma-minimal.ps1` inicia um sampler CSV concorrente e também captura
before/kick/after dentro do próprio IOCTL de submissão, evitando perder comandos
curtos apenas por latência de user mode. Potência continua indisponível e não é
fabricada.

### ABI 5 - modos negativos e robustez (implementado, ainda sem evidencia)

A ABI foi elevada para 5 para dar ao ensaio de robustez tres modos negativos no
proprio caminho de submissao da SDMA. Eles existem por uma razao unica: um
caminho de sucesso que nunca foi visto falhar nao prova nada sobre o que ele
mede.

| Modo | O que e removido | Resultado obrigatorio |
|---|---|---|
| `NO_KICK` | O WPTR nao e publicado: nem shadow, nem registrador, nem ponteiro de software. | RPTR parado, fence ainda com o poison, destino inalterado, `Completed=0`. |
| `BAD_FENCE` | O pacote FENCE carrega um valor diferente do que a CPU compara. | RPTR pode alcancar o WPTR, mas `FenceReached=0` e o valor observado tem de ser o emitido, nao o esperado. |
| `BAD_OPCODE` | O pacote de operacao vira apenas um header com opcode reservado, sem payload, sem GCR e sem pacote FENCE. | Fence continua com o poison e `Completed=0`, mesmo que o RPTR avance. |

Decisoes de projeto que valem registrar:

- `BAD_OPCODE` emite somente o header. Sem payload, nenhum endereco arbitrario
  chega a engine, entao um decode errado nao pode virar escrita em VRAM. O
  preco e que o teste passa a medir apenas o gate de conclusao, o que e
  exatamente o que se quer medir.
- Os modos sao mutuamente exclusivos por validacao no driver. Combinados, o
  resultado deixaria de identificar qual condicao removida causou a falha.
- Bits desconhecidos em `Flags` sao recusados, para que nenhum modo negativo
  entre por engano e nenhum bit novo seja ignorado em silencio.
- O gate de conclusao nao foi afrouxado para acomodar os negativos: continua
  exigindo RPTR igual ao WPTR publicado e fence escrita pela GPU com o valor
  que a CPU esperava.

O `tools/test-sdma-robustness.ps1` encadeia repeticao, wraparound do anel,
ocioso/retomada, os tres negativos, uma submissao normal de recuperacao depois
de cada um, rollback e verificacao final, incluindo varredura de bugcheck na
janela do ensaio. Nada disso foi executado no silicio: o build ABI 5 existe no
workspace, mas a maquina ainda tem o build ABI 4 instalado e a sessao atual
esta contaminada por firmware SDMA e WPTR residual.

**NAO VALIDADO EM HARDWARE**

## Ciclo limpo de 2026-08-23 com ABI 5

Boot 00:16:44, build ABI 5 instalado, `readonly` no início e no fim.
Evidência: `docs/evidence/2026-08-23_00-17-59-DESKTOP-HQAG6T4-sdma-minimal`.

O que ficou provado nesta passagem (P0, uma única repetição):

- fence física: poison `0x43DAFFFE` trocado por `0xBC250001`, valor que a CPU
  nunca escreveu no destino e que só existia dentro do pacote FENCE;
- RPTR `0x00 -> 0x44` igual ao WPTR publicado, com shadow em `0x44`;
- quatro dwords corretos lidos de volta pelo BAR0;
- `STATUS_REG` `0x46DEE557 -> 0x46DEED57` e, no instante do kick medido dentro
  do kernel, `0x44CC7042`: a engine foi observada ocupada durante o comando, e
  não apenas antes e depois;
- **rollback correto**: 24 registradores e 1029 dwords de VRAM restaurados sem
  divergência, WPTR físico de volta a `0x00000000` e destino restaurado. A
  correção do `MINOR_PTR_UPDATE` está confirmada no silício.

### GFXOFF não transiciona com carga de SDMA

38 amostras do sampler e as três capturas before/kick/after ficaram em
`0[GFXOFF]`, `GFX_MISC` cru `0x00000000`. Interpretação conservadora: SDMA e GFX
são domínios de energia distintos, então a cópia não exige tirar o GFX do
GFXOFF. Consequência prática: a regra 9 de `BC250_HARDWARE_RULES.md` não pode
ser satisfeita por trabalho de SDMA. Ela fica pendente até existir carga no
próprio GFX/compute.

### O teardown deixa o F32 rodando

Medido: `F32_CNTL` `0x00000001` antes do ensaio e `0x00000000` depois do
rollback; `SDMA0_CLK_CTRL` `0x000C0100 -> 0xFF0C0100`.

Causa: o rollback halta o F32 e em seguida restaura os 24 registradores do
snapshot — que foi tirado em `sdma init`, ou seja, depois do reset do F32, já
com `HALT=0`. A restauração desfaz o HALT.

Não é perigoso na configuração atual (anel desabilitado, base zero, ponteiros
zerados, nada para consumir), mas contradiz o que a documentação afirmava sobre
o teardown e é exatamente o tipo de resíduo que o gate de WDDM exige eliminar.

Correção aplicada em 2026-08-23: o `HALT` virou a última escrita do teardown,
depois da verificação de fidelidade dos 24 registradores — a ordem preserva a
conferência contra o snapshot e ainda termina em estado seguro. Se o F32 recusar
o HALT, isso conta como divergência e o rollback falha. `test-sdma-minimal.ps1`
e `test-sdma-robustness.ps1` passaram a reprovar a sessão quando `F32_CNTL`
volta com o bit de HALT em zero.

**Confirmado no silício** em
`docs/evidence/2026-08-23_00-30-46-DESKTOP-HQAG6T4-sdma-robustness`:
`F32_CNTL=0x00000001` depois do rollback, 24 registradores e 133 dwords de VRAM
restaurados sem divergência. A evidência veio de um ensaio que abortou por
defeito do próprio script — literal hexadecimal acima de `0x7FFFFFFF` lido como
`Int32` negativo pelo PowerShell —, o que também mostrou que o bloco de
recuperação faz o teardown mesmo quando a falha é do harness.

### Ponteiro isolado continua não servindo como prova

Na leitura imediatamente posterior à conclusão, o writeback do RPTR apareceu em
`0x34` enquanto o registrador já estava em `0x44`. A conclusão foi decidida pelo
registrador somado à fence física. Se o critério fosse um ponteiro sozinho, a
mesma operação poderia ser lida como incompleta ou como completa dependendo de
qual fonte fosse consultada.

**NÃO VALIDADO EM HARDWARE** enquanto repetição, negativos, recuperação, GCVM e
compute não passarem.

## Ensaio de robustez de 2026-08-23 00:36

Evidência: `docs/evidence/2026-08-23_00-36-35-DESKTOP-HQAG6T4-sdma-robustness`,
`passed=true`, sem bugcheck na janela do ensaio.

Doze submissões seguidas, todas com fence física e quatro dwords conferidos,
WPTR de `0x44` a `0x330` sempre crescente. O anel de 512 bytes deu a volta na
submissão 7 e as submissões seguintes continuaram corretas. Depois de 6 s
ocioso, a engine voltou a executar sem reinicialização. Os dois negativos
executados (`-nokick` e `-badfence`) foram reprovados exatamente pelos motivos
previstos, e uma submissão normal voltou a funcionar depois de cada um. O
teardown terminou com anel desabilitado, base e WPTR zerados, F32 haltado e os
68 dwords do destino restaurados.

O que isso **não** significa: determinismo. É uma passagem única, num boot
único, sem o negativo de opcode reservado e sem teste de timeout controlado.

### O sampler em user mode não cobre a janela de carga

Duas coisas apareceram juntas:

1. O CSV deste ensaio veio vazio porque o primeiro argumento de
   `telemetry watch` é contagem de amostras, não duração, e `174000` excede o
   limite de `100000`. O CLI recusou e não emitiu nenhuma amostra.
2. Revisando o ensaio mínimo, as 38 amostras cobriram apenas **0,665 s** de uma
   execução de dezenas de segundos, com intervalo médio de 18 ms e máximo de
   57 ms. O sampler parou de amostrar assim que o ensaio começou a fazer
   trabalho pesado. A causa ainda não foi determinada.

Lição registrada: um artefato de telemetria precisa declarar a própria
cobertura. Um CSV com dezenas de linhas e sem janela declarada passa por
cobertura completa quando na verdade cobre menos de um segundo. O ensaio agora
grava `sampler_window_seconds`, `run_seconds` e `covers_full_run`, e cada
submissão carrega a telemetria medida dentro do kernel — a única que cobre
comandos curtos de forma confiável.

**NÃO VALIDADO EM HARDWARE** enquanto determinismo, opcode reservado, timeout,
GCVM e compute não passarem.

## Pacote malformado quebra a engine sem travar o F32 (2026-08-23 00:42)

Evidência: `docs/evidence/2026-08-23_00-42-15-DESKTOP-HQAG6T4-sdma-robustness`.

O caso negativo de opcode reservado fez exatamente o que devia: a engine
consumiu o pacote e o RPTR alcançou o WPTR, mas a fence ficou com o poison e o
destino não foi tocado. Isso é a demonstração mais limpa até agora de que
**RPTR igual a WPTR não prova execução**. Um projeto que usasse ponteiro como
critério de conclusão teria declarado sucesso nesta operação.

O estado em que a engine ficou depois é o achado importante:

| Sintoma | Valor |
|---|---|
| Writeback do RPTR | congelado em `0x440` com registrador em `0x444` (`Pendente: 4 bytes`) |
| F32 | laço principal rodando, 22 endereços distintos em 24 amostras |
| Submissão normal seguinte | RPTR alcança o WPTR, mas nem WRITE_LINEAR nem fence escrevem memória |
| `STATUS_REG` | `0x46DEED57` -> `0x46DEE056` |
| Rollback | 24 registradores sem divergência, 2 divergências de VRAM |

Conclusão provisória: a engine entra num modo em que drena o anel sem executar,
e o microcontrolador seguir rodando não indica saúde. Nenhuma hipótese sobre a
causa é adotada aqui — falta dado.

Consequências para o projeto:

1. O gate de recuperação está **reprovado**. Antes do WDDM é obrigatório ter um
   caminho determinístico de volta ao estado utilizável depois de um pacote
   malformado, e ele ainda não existe.
2. `Pendente != 0` no `sdma status` é um sinal de engine suspeita e deve ser
   tratado como tal pelos próximos ensaios.
3. Uma contagem de divergências sem localização não é diagnóstico. A ABI 6
   passou a reportar região, offset, valor lido e valor esperado das primeiras
   oito divergências de VRAM e de registrador.

**NÃO VALIDADO EM HARDWARE.**

## Reset do F32 não recupera a engine (2026-08-23 00:47)

Evidência: `docs/evidence/2026-08-23_00-47-33-DESKTOP-HQAG6T4-sdma-robustness`.

`sdma reset` faz o que promete no microcontrolador: PC saiu de `0x006E` para
`0x01FA`, com 15 transições em 16 amostras. Ainda assim a submissão seguinte
repetiu o padrão da engine quebrada — RPTR alcançou o WPTR, nenhuma escrita em
memória, fence intocada — e o `Pendente` do writeback subiu de 4 para 60 bytes.

Conclusão: **microcontrolador saudável não implica engine saudável**. O PC
andando é evidência sobre o F32, não sobre o pipeline de execução de pacotes.
Qualquer diagnóstico futuro que use "PC se move" como sinal de recuperação está
errado.

O rollback desta passagem voltou sem divergência nenhuma, o que revoga a leitura
de que as 2 divergências de VRAM de 00:42 seriam determinísticas. Elas seguem
como pendência, agora instrumentadas.

## Um relatório que superestima é pior que a falha que ele esconde

Nesta mesma execução o ensaio imprimiu "Fases executadas sem contradicao" e
gravou `passed=true` com duas fases de recuperação reprovadas. As fases tinham
sido registradas como informativas, e só a variável de falha alimentava o
veredito.

O defeito é do tipo que este projeto existe para evitar: o artefato dizia mais
do que a medição sustentava. Corrigido — qualquer fase reprovada derruba o
veredito, entra em `failed_phases` e aparece na listagem final, fase por fase.

Regra registrada: **todo agregador de resultado precisa derivar o veredito das
fases, nunca de uma variável paralela**. Se existe um caminho pelo qual uma
reprovação não chega ao veredito, esse caminho vai ser usado mais cedo ou mais
tarde.

**NÃO VALIDADO EM HARDWARE.**

## A engine perde só o caminho de escrita (2026-08-23 00:53)

Evidência: `docs/evidence/2026-08-23_00-53-27-DESKTOP-HQAG6T4-sdma-robustness`.

Os quatro degraus da escada de recuperação reprovaram: nada, reset do F32,
recriar o anel e recarregar o firmware pelo PSP. O degrau mais informativo é o
último — com firmware recém-aceito pelo PSP (`RespStatus=0`), anel novo e F32
rodando, a submissão teve RPTR **e** writeback chegando a `0x44`, iguais ao
WPTR, e ainda assim nem `WRITE_LINEAR` nem `FENCE` escreveram um dword.

Isso elimina duas hipóteses:

- **não é cache**: a fence é emitida com `MTYPE=UC`; escrita uncached não
  ficaria presa na L2 esperando um rinse;
- **não é o microcontrolador**: firmware novo, PC andando, ponteiros corretos.

O que sobra é o caminho de escrita em memória da engine — tradução de endereço
(UTCL1/GCVM) ou o bloco de write da SDMA. Daí o `sdma fault` da ABI 7, com
`UTCL1_WR_XNACK0/1` no centro: tradução recusada silencia exatamente escritas,
sem parar o microcontrolador nem os ponteiros.

### Limite que não será atravessado

O upstream resolve isso com `GRBM_SOFT_RESET.SOFT_RESET_SDMA0`. O offset do
registro é conhecido; a posição do bit não está em nenhum header disponível
offline. Chutar o bit num registro de soft reset da GPU que também toca CP, RLC
e GFX, numa máquina que usa essa mesma GPU para o display, é exatamente o tipo
de escrita por suposição que este projeto proíbe. Fica bloqueado até haver
fonte autoritativa.

**NÃO VALIDADO EM HARDWARE.**

## Primeiro dump de falha da SDMA (2026-08-23 01:05)

Evidência: `docs/evidence/2026-08-23_01-05-54-DESKTOP-HQAG6T4-sdma-robustness`.

Referência de engine **saudável**, tirada logo após uma submissão que
funcionou:

```
STATUS_REG    0x46DEED57   STATUS1_REG 0x000003FF   STATUS2_REG 0x00050604
STATUS3_REG   0x03F00000   FREEZE      0x00000000   ERROR_LOG   0x0000000F
EDC_COUNTER   0x00000000   POWER_CNTL  0x40000050   CNTL        0x000400E2
UTCL1_CNTL    0xD0000793   UTCL1_RD_STAT 0x01891555 UTCL1_WR_STAT 0x51811555
UTCL1_RD_XNACK 0x0045FE00 0x00686B00
UTCL1_WR_XNACK 0x0045FE00 0x01050500
UTCL1_TIMEOUT 0x00000000   UTCL1_PAGE  0x004C5C20
GCVM FAULT    cntl 0x3FFFFFFC  status 0x00000000  addr 0x0
```

### O dump saudável derrubou a hipótese principal

`WR_XNACK` **já é diferente de zero numa engine que acabou de funcionar**, e
`ERROR_LOG` vale `0x0000000F` no estado saudável. Esses registradores carregam
configuração ou contadores, não um flag binário de erro. A leitura de que
"XNACK != 0 significa tradução recusada" está **revogada**.

Isso é exatamente o motivo de ter capturado a referência saudável antes da
falha. Sem ela, os mesmos valores no dump da engine quebrada teriam sido lidos
como causa, e a investigação seguiria por um caminho errado com aparência de
diagnóstico.

Consequência prática: o comando `sdma fault` deixou de marcar "sinais
suspeitos". Ele imprime os valores e diz explicitamente que valor isolado não
diagnostica nada — o uso correto é comparar dois dumps.

### O que muda de fato quando a engine quebra

Diferença entre saudável e logo após o opcode reservado, campo a campo:

| Campo | Saudável | Após o opcode reservado |
|---|---|---|
| `STATUS2_REG` | `0x00050604` | `0x003F0FE8` |
| `UTCL1_RD_STAT` | `0x01891555` | `0x01831555` |
| `UTCL1_RD_XNACK1` | `0x00686B00` | `0x00848400` |
| `UTCL1_WR_XNACK1` | `0x01050500` | `0x01454500` |

E o que **não** mudou, que é igualmente informativo:

- `STATUS1_REG` continua `0x000003FF` — pelo critério do upstream
  (`stat1 & 0x3FF == 0x3FF`), a engine se declara **ociosa**;
- `GCVM_L2_PROTECTION_FAULT_STATUS` continua `0`: **não há falha de proteção
  latchada na L2 do GC**;
- `FREEZE`, `EDC_COUNTER` e `UTCL1_TIMEOUT` continuam zerados;
- `ERROR_LOG` não mudou.

### O estado depois de recarregar o firmware é o mais desconcertante

Depois do PSP recarregar o firmware e do anel ser recriado, o dump fica **mais
limpo que o saudável**: `UTCL1_RD_XNACK1` e `WR_XNACK1` caem para
`0x00000000`/`0x00000100`, `RD_STAT`/`WR_STAT` perdem bits, ponteiros zerados.
E mesmo assim nenhuma escrita chega à VRAM.

Ou seja: **todo o estado visível do bloco SDMA parece saudável enquanto o
caminho de escrita continua morto**. Nenhum dos 26 registradores expostos
explica a falha.

Hipóteses que sobram, nenhuma testada:

1. o dano está fora do bloco SDMA — hub de memória, L2 do GC ou roteamento de
   apertura — em estado que só um reset mais profundo desfaz;
2. o dano está em estado interno da engine que não é exposto por registrador
   nenhum desta lista;
3. falta ler o bloco certo, ainda não catalogado.

O caminho de investigação que o upstream indica continua sendo
`GRBM_SOFT_RESET.SOFT_RESET_SDMA0`, bloqueado por falta da posição do bit em
fonte autoritativa.

**NÃO VALIDADO EM HARDWARE.**

## A engine latcha o opcode invalido (decodificação de 2026-08-23)

Com o `sh_mask` autoritativo em mãos, os dumps da ABI 7 foram decodificados por
`tools/decode-sdma-fault.py`. A comparação entre a engine saudável e a engine
logo após o pacote com opcode reservado tem **exatamente dois campos
diferentes**:

| Campo | Saudável | Após o opcode reservado |
|---|---|---|
| `STATUS2_REG.CMD_OP` | `5` | **`63` = `0x3F`** |
| `STATUS2_REG.F32_INSTR_PTR` | `385` | `1018` |
| `UTCL1_RD_STATUS.NEXT_RD_VECTOR` | `4` | `1` |

`0x3F` é precisamente o opcode reservado que o caso negativo emite. A engine
**latchou o comando inválido em `STATUS2_REG.CMD_OP`** e ficou lá.

Todo o resto continua reportando saúde. No estado quebrado:

- `STATUS_REG`: `IDLE`, `RB_EMPTY`, `MC_WR_IDLE`, `MC_RD_IDLE`, `EX_IDLE`,
  `PREV_CMD_IDLE`, `SEM_IDLE`, `INT_IDLE` — tudo em 1;
- `STATUS1_REG` = `0x3FF`: todas as unidades de cópia ociosas;
- `STATUS3_REG`: `EXCEPTION_IDLE`, `TLBI_IDLE`, `GCR_IDLE`, `INVREQ_IDLE`;
- `UTCL1_WR_STATUS`: sem `PAGE_FAULT`, sem `WR_XNACK_TIMEOUT`, sem
  `PAGE_NULL`, FIFOs vazias;
- `GCVM_L2_PROTECTION_FAULT_STATUS` = 0.

Ou seja: **a falha é silenciosa em todos os registradores de erro**. A engine
se declara ociosa e saudável enquanto simplesmente não executa mais nada que
toque memória. A única marca que o pacote inválido deixa é o `CMD_OP` latchado.

Isso explica por que recarregar o firmware não resolve: o microcódigo é
substituído, mas a máquina de estados do decodificador de comandos do bloco não
é reinicializada por nada abaixo de um reset do bloco.

### O bit autoritativo foi obtido

`GRBM_SOFT_RESET.SOFT_RESET_SDMA0` está no shift `0x17`, máscara `0x00800000`,
segundo `gc_10_1_0_sh_mask.h` do kernel (SHA-256
`cba20414ecf02e91bafdcfbff7d4ba686326f0bdaf236d1a883364f08410c167`). O extrato
dos registros usados foi copiado para
`referencias/BC250-windowsDriverTest/docs/upstream/generated/gc_10_1_0_sh_mask_extract.h`
com a procedência no cabeçalho.

Com isso o soft reset de bloco saiu de hipótese bloqueada para implementação:
`sdma enginereset` segue a sequência do upstream — parar o anel, `FREEZE` e
esperar `FROZEN` (ou `STATUS1 == 0x3FF`), `HALT` no F32, desligar
`UTC_L1_ENABLE`, pulso de 50 µs só no bit da SDMA0 com os demais preservados
por leitura-modificação-escrita, e depois tirar o `FREEZE`.

O anel é perdido no reset e o driver zera seu próprio estado de anel, para não
seguir achando que tem um. Quem chama precisa refazer `sdma init`.

**Ainda NÃO VALIDADO EM HARDWARE**: falta executar.

## O soft reset de bloco funciona, mas apaga o microcódigo (2026-08-23 07:35)

Evidência: `docs/evidence/2026-08-23_07-35-43-DESKTOP-HQAG6T4-sdma-robustness`.

Primeira execução de `sdma enginereset`. O reset em si fez exatamente o que
deveria:

```
FREEZE          : 0x00000000 -> 0x00000010, FROZEN=nao apos 100 iteracoes
STATUS1_REG     : 0x000003FF (ocioso pelo criterio do upstream: sim)
GRBM_SOFT_RESET : 0x00000000 -> 0x00800000 -> 0x00000000
STATUS2_REG     : 0x00000804 -> 0x00000000
CMD_OP latchado : 128 -> 0
```

**O `CMD_OP` latchado foi limpo.** É a primeira ação que mexe nesse estado —
reset do F32, recriar o anel e recarregar firmware não mexiam.

Dois aprendizados do mesmo experimento:

1. **`FROZEN` nunca apareceu** em 100 iterações de 1 ms. O critério alternativo
   do upstream salvou a operação: `STATUS1_REG == 0x3FF` confirmou a engine
   ociosa, e por isso o reset seguiu. Se o código exigisse `FROZEN`, teria
   recusado sem motivo.
2. **O reset leva junto a instruction RAM do F32.** Na submissão seguinte o PC
   ficou congelado em `0x006B` nas três amostras (antes, no kick e depois),
   enquanto numa engine saudável ele varre a faixa `0x0011..0x03E0`. O anel
   voltou a "andar sem executar": RPTR alcançou o WPTR e nada foi escrito.

A sequência estava incompleta. Depois do reset de bloco é obrigatório
**recarregar o firmware pelo PSP** antes de religar o F32 e recriar o anel —
que é, aliás, o que o `hw_init` do upstream faz depois de um reset de IP.

Correção aplicada: o degrau `E3e` passou a fazer
`enginereset` -> `psp loadfw sdma0` -> `sdma reset` -> `sdma init` -> submissão.

### Corrida no writeback durante o rollback

O rollback acusou uma divergência: `rptr_wb` em `0x0FF01000` lido `0x00000044`,
esperado `0x00000488`. Graças à instrumentação da ABI 6, dessa vez o driver
disse **onde**.

É corrida, não corrupção: o writeback é escrito pela própria engine, e uma
escrita em voo pousa depois da restauração. A verificação agora espera 5 ms
antes de conferir e, num dwords de controle divergente, reescreve uma vez e
reconfere, registrando a tentativa no log. Divergência persistente continua
contando como falha.

**NÃO VALIDADO EM HARDWARE.**

## Descoberta central: o laboratório inteiro depende de `UTCL1_PAGE.USE_BC`

Evidência: `docs/evidence/2026-08-23_07-40-27-DESKTOP-HQAG6T4-sdma-robustness`.

A sequência corrigida rodou: soft reset do bloco, firmware recarregado pelo PSP
(`RespStatus=0`), F32 religado com o laço principal varrendo `0x0142..0x03E0`,
anel recriado. E a escrita continuou falhando.

Comparando o dump pós-reset com o da engine saudável, quatro campos de
configuração voltaram ao padrão de reset e ninguém os reprogramou:

| Registro | Saudável (POST) | Depois do soft reset | Campo perdido |
|---|---|---|---|
| `UTCL1_PAGE` | `0x004C5C20` | `0x000C5C20` | **`USE_BC` 1 -> 0** |
| `UTCL1_CNTL` | `0xD0000793` | `0xD0000191` | `RESP_MODE` 3 -> 0, `REDO_DELAY` 9 -> 8 |
| `CHICKEN_BITS` | `0x03AB0107` | `0x01AF0107` | `TIME_BASED_QOS` 1 -> 0, `T2L_256B_ENABLE` 0 -> 1 |
| `CNTL` | `0x000400E2` | `0x000400C2` | `MIDCMD_PREEMPT_ENABLE` 1 -> 0 |

`USE_BC` é o achado que reorganiza o entendimento do projeto inteiro.

**Todo o sucesso de SDMA obtido até aqui depende de um bit que o POST deixou
ligado, não de algo que o laboratório configurou.** Com `USE_BC=1` a UTCL1
opera em modo de bypass e a engine alcança a VRAM sem page table nenhuma — que
é exatamente por que este laboratório escreve memória sem nunca ter montado
GCVM/VMID0. Quando o soft reset zera esse bit, a engine saudável, com firmware
novo e anel novo, para de escrever memória silenciosamente.

Consequências:

1. O `sdma enginereset` passou a capturar `UTCL1_PAGE`, `UTCL1_CNTL`,
   `CHICKEN_BITS`, `UTCL1_WATERMK` e `CLK_CTRL` antes do pulso e restaurá-los
   depois, conferindo cada readback. Sem isso o "reset de recuperação" quebrava
   uma engine que estava boa.
2. Para o driver definitivo isto vira requisito explícito: qualquer reset de
   bloco precisa reprogramar a configuração da UTCL1, e a alternativa correta a
   depender do POST é montar tradução de verdade — o gate GCVM/VMID0 que já
   estava planejado deixa de ser opcional.
3. Fica registrado como risco: um driver que resete a SDMA sem reprogramar
   `USE_BC` produz exatamente o sintoma mais difícil de diagnosticar deste
   projeto — ponteiros avançando, engine se declarando ociosa, e nenhuma
   escrita chegando.

Nota metodológica: esse bit só apareceu porque o ensaio tira um dump de
referência **da engine saudável** antes de qualquer falha. Comparação mecânica
achou em segundos o que nenhuma leitura isolada de registrador teria mostrado.

**NÃO VALIDADO EM HARDWARE**: a recuperação com a configuração restaurada ainda
não foi executada.

## Onde exatamente `USE_BC` se perde (2026-08-23 07:51)

Evidência: `docs/evidence/2026-08-23_07-51-42-DESKTOP-HQAG6T4-sdma-robustness`.

A restauração de configuração foi implementada e reportou "5 restaurados, 0
divergências" — mas restaurou o valor errado. A saída denuncia:

```
UTCL1_PAGE : 0x000C5C20 -> reset 0x000C5C20 -> 0x000C5C20
```

O valor "antes" já vinha sem `USE_BC`. Rastreando os cinco dumps do ensaio, a
sequência é clara:

| Momento | `UTCL1_PAGE` |
|---|---|
| engine saudável | `0x004C5C20` |
| após o opcode reservado | `0x004C5C20` |
| após reset do F32 | `0x004C5C20` |
| após recriar o anel | `0x004C5C20` |
| após recarregar o firmware | `0x004C5C20` |
| dentro do `enginereset`, na captura | `0x000C5C20` |

`USE_BC` sobrevive a tudo — inclusive ao pacote malformado — e some **dentro**
do próprio `enginereset`. A causa é o ponto de captura: eu lia a configuração
depois de já ter desligado `CNTL.UTC_L1_ENABLE`. Com a UTCL1 desligada, os
registradores dela passam a ler o valor de reset.

Ou seja, o código capturava o estado degradado que ele mesmo tinha acabado de
produzir e depois o "restaurava" com sucesso aparente — inclusive contabilizando
`5 restaurados, 0 divergencias`. Um contador de sucesso que confirma a própria
falha.

Correções:

1. A configuração passou a ser lida **antes de qualquer escrita**, no topo da
   função.
2. Na restauração, `CNTL` vem primeiro, porque é ele que religa
   `UTC_L1_ENABLE`; com a UTCL1 desligada os registradores dela não guardam o
   que se escreve. Só depois vêm `UTCL1_PAGE`, `UTCL1_CNTL`, `UTCL1_WATERMK`,
   `CHICKEN_BITS` e `CLK_CTRL`, cada um com readback conferido.

Lição registrada: **um readback só vale se o registrador estiver no estado em
que ele é observável**. Ler configuração de um bloco desligado devolve valor de
reset, e comparar isso com ele mesmo dá "sucesso" sem significado nenhum.

O achado anterior sobre `USE_BC` permanece válido: o bit é do POST, é o que
permite escrita sem page table, e o soft reset o derruba. O que muda é que ele
não some antes do reset — some por causa da própria sequência de parada.

**NÃO VALIDADO EM HARDWARE.**

## REVOGADO: `USE_BC` não é a causa. É artefato de leitura com o F32 haltado

Correção de uma conclusão minha, publicada horas antes neste mesmo documento.

Afirmei que o soft reset de bloco apagava `UTCL1_PAGE.USE_BC` e que isso
explicava a perda de escrita. Cruzando as 20 amostras de dump acumuladas nas
cinco execuções do dia, a correlação é perfeita e é outra:

| Estado do F32 | `UTCL1_PAGE` | Amostras |
|---|---|---:|
| rodando (`F32_CNTL=0x00000000`) | `0x004C5C20` | 16 |
| haltado (`F32_CNTL=0x00000001`) | `0x000C5C20` | 4 |

Nenhuma exceção. Todos os dumps `apos_badop`, `apos_reset_f32`, `apos_reinit` e
`apos_recarregar_firmware` — inclusive com a engine já quebrada — mostram
`0x004C5C20`. O único estado que mostra `0x000C5C20` é o dump tirado logo após o
`enginereset`, quando o F32 está haltado.

Ou seja: **`UTCL1_PAGE` não é observável de forma confiável com o F32
haltado**. A diferença de `0x00400000` que eu li como "perda de USE_BC causada
pelo reset" acompanha o estado do microcontrolador, não o reset.

O que fica revogado:

- que o reset de bloco apaga `USE_BC`;
- que a restauração dessa configuração era o passo que faltava;
- por consequência, a afirmação de que "todo o sucesso de SDMA depende de um bit
  que o POST deixou ligado". Ela pode até ser verdadeira, mas **não está
  demonstrada** por estes dados.

O que permanece válido:

- o `CMD_OP` latchado em `0x3F` após o pacote reservado, e o fato de o soft
  reset de bloco ser a única ação que o limpa;
- que nenhuma das cinco ações testadas devolve a capacidade de escrever.

Erro de método a registrar: eu comparei dois dumps sem checar se a condição de
observação era a mesma nos dois. A regra que faltava, agora explícita: **um
registrador só é comparável entre dois dumps se o bloco estiver no mesmo estado
de execução nos dois**. O ensaio passou a registrar o estado do F32 junto de
cada dump, e o driver escolhe a fonte de configuração conforme o F32 esteja
rodando ou haltado, registrando qual usou.

### Próximo experimento: testar o reset numa engine saudável

O ensaio ganhou a fase `F`, executada **antes** dos negativos: soft reset de
bloco numa engine comprovadamente boa, seguido de firmware, F32 e anel novos, e
uma submissão normal.

A razão é metodológica. Testar um mecanismo de recuperação só depois de quebrar
a engine mistura duas perguntas — "o reset está correto?" e "o dano é
recuperável?" — e o resultado não distingue as duas. Com a engine saudável, a
resposta é sobre o reset e mais nada:

- se a escrita voltar, o reset é um mecanismo válido e o dano do opcode
  reservado é que sobrevive a ele;
- se a escrita não voltar, a sequência de reset está incompleta, e não adianta
  julgá-la em cima de uma engine já quebrada.

**NÃO VALIDADO EM HARDWARE.**

## A fase F falhou por defeito meu, e isso invalida conclusões anteriores

Evidência: `docs/evidence/2026-08-23_08-09-45-DESKTOP-HQAG6T4-sdma-robustness`.

A fase F — soft reset numa engine saudável — reprovou. Mas não pelo hardware:

```
sdma_init_pos_reset_saudavel : falha ao inicializar SDMA ring: erro 170
escrita_pos_reset_saudavel   : falha no WRITE_LINEAR: erro 21
```

Erro 170 é `ERROR_BUSY`, e vem de `BcSdmaCaptureSnapshot`, que recusa capturar
quando já existe snapshot válido. O `sdma enginereset` zerava o estado de anel
do driver (`SdmaRingGpuAddr`) mas deixava `SdmaSnapshotValid = TRUE`. O `sdma
init` seguinte era recusado, a escrita nem chegou ao silício, e o ensaio
registrou "não recuperou".

**A fase F nunca testou o hardware. Testou o meu bookkeeping.**

### O que isso invalida

Os degraus `E3c` (recriar o anel), `E3d` (recarregar firmware) e `E3e` (soft
reset do bloco) todos assumem que o caminho de re-bring-up funciona. Esse
caminho **nunca foi verificado isoladamente**. Enquanto ele não for validado
numa engine saudável, "não recuperou" nesses degraus não distingue:

- dano irreversível causado pelo pacote malformado; de
- sequência de re-bring-up incorreta.

As conclusões daqueles degraus ficam **suspensas**, não revogadas: podem estar
certas, mas não estão demonstradas.

### Uma contradição que impede conclusão sobre `UTCL1_PAGE`

No mesmo ensaio, dentro do `enginereset`, com o F32 já haltado, a leitura de
`UTCL1_PAGE` depois do pulso deu `0x004C5C20`. No dump `sdma fault` tirado
segundos depois, também com o F32 haltado, deu `0x000C5C20`.

Duas leituras do mesmo registrador, no mesmo estado declarado do F32, com
valores diferentes. Então nem "o reset apaga `USE_BC`" nem "haltado sempre lê
valor de reset" descrevem o comportamento por completo. O registrador fica
marcado como **não confiável para inferência** até existir um modelo que
explique as duas observações. Nenhuma conclusão do projeto deve se apoiar nele.

### Correções e mudança de método

1. `sdma enginereset` passou a **recusar** quando existe snapshot aberto, com
   mensagem explícita mandando rodar `sdma rollback` antes. Resetar o bloco
   invalida os valores de registrador do snapshot; perder o ponto de
   restauração em silêncio era pior que recusar.
2. Novo `tools/test-sdma-recovery.ps1`: experimento controlado que responde
   primeiro "a sequência funciona numa engine saudável?" e só depois "ela
   recupera uma engine danificada?". Três variantes em ordem crescente de
   intervenção — sem reset, com soft reset de bloco, com reset também do F32 —
   cada uma terminando numa escrita que precisa passar. Se uma falha, o ensaio
   para: medir a próxima em cima de uma engine já quebrada é o erro que este
   script existe para evitar.

Lição de método, que vale mais que o resultado do dia: **um mecanismo de
recuperação tem de ser validado num sistema saudável antes de ser usado como
prova sobre um sistema quebrado.** Cinco execuções foram gastas medindo
recuperação sem esse controle.

**NÃO VALIDADO EM HARDWARE.**

## Controle positivo: o re-bring-up funciona, inclusive com soft reset de bloco

Evidência: `docs/evidence/2026-08-23_08-21-45-DESKTOP-HQAG6T4-sdma-recovery`,
`passed=true`.

Primeiro experimento controlado sobre recuperação. Numa engine comprovadamente
saudável, três variantes, cada uma terminando em escrita com fence física e
quatro dwords conferidos:

| Variante | Sequência | Resultado |
|---|---|---|
| referência | escrita direta após bring-up limpo | passou |
| V1 | rollback -> `psp loadfw` -> `sdma init` -> escrita | **passou** |
| V2 | rollback -> `sdma enginereset` -> `psp loadfw` -> `sdma init` -> escrita | **passou** |
| V3 | V2 + `sdma reset` do F32 antes do init | **passou** |

Destino restaurado ao conteúdo original nas 24 dwords guardadas.

### O que isso estabelece

1. **O caminho de re-bring-up é válido.** Recarregar o firmware pelo PSP e
   recriar o anel devolve uma engine funcional, com ou sem soft reset de bloco
   no meio. As conclusões dos degraus `E3c`, `E3d` e `E3e` saem de suspensas:
   como o mecanismo funciona em engine saudável, a falha deles sobre a engine
   danificada é **dano real**, não sequência errada.
2. **O soft reset de bloco é um mecanismo utilizável**, não uma operação que
   destrói o estado. Isso é requisito de qualquer driver que vá lidar com
   engine travada.

### A hipótese `USE_BC` morreu por controle positivo

No V2 o `enginereset` "restaurou" `UTCL1_PAGE = 0x000C5C20` — isto é, sem o bit
`USE_BC` — porque a leitura de entrada aconteceu com o F32 já haltado pelo
rollback anterior. **E a escrita seguinte funcionou, com fence física e dados
conferidos.**

Ou seja: a engine volta a escrever memória sem que ninguém reprograme
`UTCL1_PAGE`. Quem reestabelece o que o bloco precisa é o firmware carregado
pelo PSP mais o `sdma init`. A hipótese está encerrada por experimento, não por
argumento.

Consequência prática: a restauração de configuração foi **removida** do
`sdma enginereset`. Ela escrevia cinco registradores a partir de leituras cuja
validade depende do estado do bloco, para resolver um problema que não existe.
Os valores continuam no payload como observação, e os contadores de restauração
ficam em zero de propósito.

Lição: quando uma hipótese sobrevive só porque nunca foi testada isoladamente,
o experimento que a mata custa menos que o código que ela gera. O controle
positivo que faltava custou uma execução; a hipótese `USE_BC` já tinha gerado
duas rodadas de código e duas seções de documento.

**NÃO VALIDADO EM HARDWARE** para a pergunta que resta: a injúria de opcode
reservado sobrevive a V2?

## A engine mentiu, e só a comparação de dados pegou (2026-08-23 08:31)

Evidência: `docs/evidence/2026-08-23_08-31-41-DESKTOP-HQAG6T4-sdma-recovery`.

Experimento com controle interno na mesma sessão: V1, V2 e V3 passaram na engine
saudável, aí a injúria de opcode reservado foi injetada e o V2 foi aplicado. A
submissão seguinte produziu isto:

```
RPTR 0x0 -> 0x44 (WPTR 0x44)  concluido
FENCE VRAM 0x0FF03000 : poison 0x43DAFFFE -> observado 0xBC250001,
                        esperado 0xBC250001 [ALCANCADO]
STATUS no kick      : 0x46DC7042
Conclusao do pacote : SIM
  0x0FF04050 = 0x51DF0AA4  DIVERGIU
  0x0FF04054 = 0x5A378940  DIVERGIU
```

Leia com atenção o que está acontecendo aí:

- o RPTR alcançou o WPTR;
- a fence foi escrita **pela GPU**, com o valor que só existia dentro do pacote;
- o STATUS mostrou a engine ocupada durante o comando;
- **e os dwords de destino não mudaram.**

O driver declarou `Completed = 1`. Pelos dois critérios que o projeto usava —
ponteiros e fence física — esta operação teria sido registrada como sucesso. Só
a comparação dos dados, feita em user mode pelo CLI, reprovou.

Essa é exatamente a armadilha que a regra 6 de `BC250_HARDWARE_RULES.md` existe
para evitar, e ela apareceu no silício: **fence física escrita pela GPU não
prova que o trabalho foi feito.**

### Correção estrutural: a conferência do dado passou para o kernel

ABI 10. `BcSdmaExecOp` agora lê os dwords de destino de volta dentro do driver,
conta divergências e **exige zero divergências para declarar `Completed`**. O
critério deixou de ser "ponteiro + fence" e passou a ser "ponteiro + fence +
dado". Não é mais possível o driver anunciar conclusão sem a prova completa.

Também foi criada a marca de **engine suspeita**: quando uma submissão normal
alcança a fence e não produz o dado, o driver marca a engine e passa a recusar
submissões novas na sessão, com log explícito. Nada dentro da sessão limpa a
marca, porque nada dentro da sessão recupera a engine. O laboratório pode
contornar com `-ignoresuspect` para medir o dano; produção não deve.

### O dano também não é um estado único

Nos ensaios de robustez de 07:40, 07:51 e 08:00, a submissão pós-recuperação
tinha `[NAO ALCANCADO]` na fence. Aqui, com uma injúria única seguida
imediatamente de V2, a fence **foi** alcançada e só o dado faltou.

Então o estado pós-injúria varia conforme o que se faz depois. "A engine para de
escrever memória" está **incorreto** como descrição geral: ela escreveu a fence.
O que se pode afirmar com os dados de hoje:

- o dado do `WRITE_LINEAR` não fica visível pela abertura do BAR0 após a
  injúria, em todas as tentativas;
- a fence, emitida com `MTYPE=UC`, pode ou não ficar visível, dependendo do
  caminho de recuperação aplicado;
- nenhuma sequência testada devolve a engine a um estado em que o dado apareça.

A diferença entre fence uncached visível e dado não visível reabre — agora com
base em medição, não em suposição — a possibilidade de o problema estar na
visibilidade/coerência da escrita cacheada, não na execução. Isso é hipótese,
não conclusão, e exige experimento próprio.

**NÃO VALIDADO EM HARDWARE.**

## Gate mínimo repassou com o critério endurecido (2026-08-23 08:41)

Evidência: `docs/evidence/2026-08-23_08-41-33-DESKTOP-HQAG6T4-sdma-minimal`,
`passed=true`, driver `bc250dbg v10.0 ABI 10`.

Primeira execução com a conferência de dado dentro do kernel fazendo parte do
critério de conclusão. Passou: fence física `0x43DAFFFE -> 0xBC250001`, quatro
dwords corretos, rollback com 24 registradores e 1029 dwords sem divergência,
`F32_CNTL=0x00000001` no fim e destino restaurado.

O que isso demonstra é modesto e específico: **o critério mais rígido não
inventou falha onde não há.** O caminho saudável continua passando quando a
prova exigida aumenta — que é a única forma de confiar num critério novo.

### Um defeito de processo meu, encontrado ao conferir a evidência

A saída do ensaio ainda mostrava o texto antigo, sem a linha de dados
conferidos. Investigando: um dos meus patches no CLI abortou no meio — a quarta
substituição não casou, o script saiu por exceção e as três anteriores nunca
foram gravadas. O driver tinha a mudança; o CLI não.

Pior: os ensaios já passavam `-ignoresuspect`, opção que o CLI não conhecia.
Todas as medições pós-injúria teriam falhado com "opcao desconhecida" em vez de
medir.

Correção aplicada duas vezes: as edições foram refeitas e, agora, **conferidas
por marcador no arquivo depois de salvar**, não pela mensagem de sucesso do
próprio patch. Escrever ferramenta que aborta no meio sem sinalizar é o mesmo
tipo de erro que o projeto persegue no silício — sucesso parcial reportado como
sucesso.

## Gate reescrito aprovado no silício (2026-08-23 08:53)

Evidência: `docs/evidence/2026-08-23_08-53-37-DESKTOP-HQAG6T4-sdma-failclosed`,
`passed=true`, dez gates aprovados, zero bugcheck.

### G1 — o driver não emite pacote malformado

Nove entradas inválidas, todas recusadas com `erro 87`
(`ERROR_INVALID_PARAMETER`), e o WPTR imóvel em `0x00000044` antes e depois da
bateria:

```
[recusado] opcode 0 (invalido)              [recusado] zero dwords
[recusado] opcode 99 (invalido)             [recusado] dwords acima do maximo
[recusado] flag desconhecida 0x80           [recusado] destino desalinhado
[recusado] dois negativos juntos            [recusado] poison igual ao esperado
[recusado] fence fora do dword reservado
```

Depois da bateria, uma submissão normal continuou funcionando com fence física e
dado conferido — a fronteira não tem efeito colateral.

### G2 — detecção determinística do estado mentiroso

Após a injúria, na ordem:

1. `sdma fault` reportou `ENGINE SUSPEITA: SIM (motivo 2)`, marcado pelo próprio
   driver sem depender de quem chama;
2. uma submissão normal foi **recusada pelo driver** com `erro 483`
   (`ERROR_DEVICE_HARDWARE_ERROR`) — não chegou ao anel;
3. forçando a medição com `-ignoresuspect`, o resultado foi:

```
Dados conferidos    : 4 dwords, 4 divergencias (conferido no kernel)
Conclusao do pacote : NAO (exige RPTR=WPTR, fence GPU e dado conferido)
```

O item 3 é o que fecha o gate: o critério novo reprova a operação **dentro do
kernel**, sem depender de comparação em user mode.

Nesta execução a fence ficou `NAO ALCANCADO`, ao contrário da execução de 08:31,
onde foi alcançada. A variabilidade do estado pós-injúria, já registrada,
aparece de novo — e não muda o veredito, porque o critério agora exige o dado.

### G3 — falha limpa

Com a engine condenada: rollback com 24 registradores e VRAM sem divergência,
anel desabilitado, WPTR de volta a zero, F32 haltado, 32 dwords de destino
restaurados, driver respondendo a `info`, nenhum bugcheck.

### O que este gate não diz

Não diz que a engine foi recuperada. A injúria de opcode reservado continua sem
caminho de volta por software, e o item 4 — recuperação em nível de dispositivo —
segue **em aberto e não auditado**, porque o display compartilha esta GPU. Isso
está no campo `not_covered` da própria evidência, não só na documentação.

A diferença entre este gate e o anterior não é ambição menor: é exigir prova do
que existe em vez de prometer o que a medição mostrou não existir.

## Estado de tradução do GC medido: não existe page table (2026-08-23 09:26)

Evidência: `docs/evidence/2026-08-23_09-26-23-DESKTOP-HQAG6T4-gcvm-baseline`,
captura read-only, armamento `readonly`, nenhuma escrita.

Três perguntas que estavam abertas foram respondidas pela mesma leitura.

### 1. VMID 0 está desabilitado e não há page table nenhuma

```
CONTEXT0_CNTL : 0x007FFE80   ->  ENABLE_CONTEXT = 0
CTX0_PT_BASE  : 0x0000000000000000
CTX0_PT_START : 0x0000000000000000
CTX0_PT_END   : 0x0000000000000000
```

Contexto desabilitado, base, início e fim zerados. **Não existe tabela de
páginas nesta placa.** Toda a execução de SDMA demonstrada neste projeto — as
doze submissões sequenciais, o wraparound, a fence física, os dwords conferidos
— aconteceu sem tradução de endereço.

### 2. O mecanismo do bypass, agora medido

```
MX_L1_TLB_CNTL : 0x00000501
    ENABLE_L1_TLB                = 1
    SYSTEM_ACCESS_MODE           = 0   (upstream exige 3)
    ENABLE_ADVANCED_DRIVER_MODEL = 0   (upstream exige 1)
```

O POST deixou a TLB L1 habilitada em modo de acesso físico, com o modelo de
driver avançado desligado. É por isso que a SDMA alcança a VRAM sem page table.

Isso **substitui** a especulação anterior sobre `UTCL1_PAGE.USE_BC`, que foi
revogada por ser artefato de leitura. A explicação correta estava em outro
registrador, e agora é medição com nome de campo do header autoritativo, não
inferência.

### 3. A hipótese de coerência de cache está encerrada

```
L2_CNTL : 0x00080602  ->  ENABLE_L2_CACHE = 0
```

**A L2 do GC está desabilitada.** Ficou registrado como hipótese aberta que o
dado do `WRITE_LINEAR` pós-injúria poderia estar sujo na L2 e invisível pelo
HDP, enquanto a fence uncached aparecia. Com a L2 desligada durante todo o
trabalho — nada que o laboratório faz escreve `L2_CNTL` — essa explicação não se
sustenta. Hipótese fechada por medição.

### Confirmação da abertura de VRAM pelo hardware

```
FB_LOCATION_BASE : 0x0000F400   ->  base 0xF4_0000_0000
FB_LOCATION_TOP  : 0x0000F41F   ->  topo 0xF4_1FFF_FFFF  (512 MB)
SYS_APERTURE_LOW : 0x003D0000   ->  0xF4_0000_0000
SYS_APERTURE_HIGH: 0x003D0800   ->  0xF4_2000_0000
FB_OFFSET        : 0x00000450
```

A constante `BC250_VRAM_GPU_BASE = 0xF400000000`, que o laboratório vinha usando
desde a primeira submissão, é confirmada pelos registradores de localização de
framebuffer. Classe **P0**: medido, não derivado de suposição. A abertura tem
512 MB, contra os 256 MB do BAR0 — que é exatamente por que as validações de
endereço da SDMA usam a janela inteira e não o BAR.

### O escopo real de montar VMID 0

Onze divergências contra `gfxhub_v2_0.c`, e nenhuma delas é defeito:

| Registro | Campo | POST | Upstream |
|---|---|---:|---:|
| `L2_CNTL` | `ENABLE_L2_CACHE` | 0 | 1 |
| `L2_CNTL` | `ENABLE_L2_FRAGMENT_PROCESSING` | 1 | 0 |
| `L2_CNTL` | `ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY` | 0 | 1 |
| `L2_CNTL3` | `BANK_SELECT` | 7 | 9 |
| `L2_CNTL3` | `L2_CACHE_BIGK_FRAGMENT_SIZE` | 0 | 6 |
| `L2_CNTL4` | `VMC_TAP_PDE_REQUEST_PHYSICAL` | 1 | 0 |
| `L2_CNTL4` | `VMC_TAP_PTE_REQUEST_PHYSICAL` | 1 | 0 |
| `CONTEXT0_CNTL` | `ENABLE_CONTEXT` | 0 | 1 |
| `CONTEXT0_CNTL` | `RETRY_PERMISSION_OR_INVALID_PAGE_FAULT` | 1 | 0 |
| `MX_L1_TLB_CNTL` | `SYSTEM_ACCESS_MODE` | 0 | 3 |
| `MX_L1_TLB_CNTL` | `ENABLE_ADVANCED_DRIVER_MODEL` | 0 | 1 |

Além disso, e não aparecendo como divergência porque o upstream calcula em
runtime: page table a alocar e programar (`PT_BASE`/`START`/`END`), página
default de falha (`FAULT_DEFAULT_ADDR` está em zero), aberturas AGP (todas em
zero) e a engine de invalidação.

### O risco que esse escopo carrega

`VMC_TAP_PDE/PTE_REQUEST_PHYSICAL = 1` e `SYSTEM_ACCESS_MODE = 0` são
justamente o que faz o caminho atual funcionar. Habilitar tradução de verdade
**substitui** o bypass do qual todo o sucesso medido depende.

Consequência para o plano: o VMID 0 tem de ser montado de forma transacional,
com snapshot e rollback de todos os registradores da tabela acima, e a primeira
submissão traduzida só vale depois de a page table ser conferida por leitura da
CPU. Se a tradução for programada errado, a SDMA para de escrever — e hoje já
sabemos que engine quebrada nesta placa não volta sem reboot.

**NÃO VALIDADO EM HARDWARE** para qualquer afirmação sobre tradução funcionando:
nada foi escrito ainda.

## Primeira tentativa do passo 1: falha do laboratório, não da placa (09:39)

Evidência: `docs/evidence/2026-08-23_09-39-59-DESKTOP-HQAG6T4-gcvm-pagetable`.

`gcvm ptbuild` retornou `erro 21` (`ERROR_NOT_READY`) com tudo zerado:

```
tabela            : 0x0FF10000 (0 PTEs, 0 bytes)
GCMC_VM_FB_OFFSET : 0x00000000  -> vram_base_offset 0x0
snapshot          : AUSENTE, 0 dwords
```

Causa: a abertura de VRAM (BAR0) é mapeada sob demanda, e nada naquela sessão
havia tocado VRAM antes. `BcGcvmPageTable` exigia `g_Dev.Vram != NULL` em vez de
chamar `BcVramMap()`, como `BcSdmaInit` faz. Corrigido: a função mapeia sob
demanda.

Vale registrar o padrão, porque é a terceira vez no mesmo dia: **um ensaio que
reprova por defeito próprio se parece com resultado de hardware.** Os três casos
foram distinguíveis só porque a saída trazia os valores crus — `0 PTEs`,
`FB_OFFSET 0x00000000` e `snapshot AUSENTE` são impossíveis como resultado
físico. Um ensaio que só imprimisse "FALHOU" teria virado horas de investigação
no lugar errado.

Regra reforçada: **toda falha tem de sair acompanhada dos valores que a
sustentam**, e valor impossível é sinal de defeito de instrumento, não de
silício.

Os gates A, C e E passaram nessa mesma execução, o que confirma que a construção
não tocou registrador de tradução — inclusive quando falhou.

## Passo 1 do gate de memória APROVADO (2026-08-23 09:46)

Evidência: `docs/evidence/2026-08-23_09-46-05-DESKTOP-HQAG6T4-gcvm-pagetable`,
cinco gates aprovados.

```
tabela            : 0x0FF10000 (512 PTEs, 4096 bytes)
paginas alvo      : VRAM 0x0FD00000 em diante
GCMC_VM_FB_OFFSET : 0x00000450  -> vram_base_offset 0x450000000
PA coberto        : 0x45FD00000 .. 0x45FEFF000
flags do PTE      : 0x3000000000071 (VALID|EXEC|READ|WRITE|MTYPE_UC)
PTE[0]            : 0x000300045FD00071
PTE[1]            : 0x000300045FD01071
conferencia       : 0 divergencias em 512 entradas
```

Rollback: 1024 dwords devolvidos, zero divergências. Dump GCVM idêntico antes e
depois; `CONTEXT0_CNTL` no fim igual ao do início.

### A aritmética foi confirmada por fonte independente

O valor medido bate exatamente com o cálculo feito fora do driver:

```
vram_base_offset = 0x450 << 24                = 0x4_5000_0000
PA[0]   = 0x0FD00000 + 0x450000000            = 0x4_5FD0_0000
PTE[0]  = (PA & 0x0000FFFFFFFFF000) | 0x71 | MTYPE_UC
                                              = 0x000300045FD00071
PA[511] = 0x0FD00000 + 511*4096 + 0x450000000 = 0x4_5FEF_F000
```

As 512 entradas incrementam de 4 KB em 4 KB, como esperado, e o último PA
confere com o reportado. A cadeia inteira — máscara de 48 bits, flags,
`MTYPE_UC = 3`, conversão `mc2pa` e `vram_base_offset` lido do registrador — está
consistente com o silício.

### O que isso estabelece, e o que não

Estabelece: o laboratório constrói PTEs no formato que esta geração usa, com o
endereço físico derivado de registrador lido em runtime, e devolve a memória
tocada ao estado original.

**Não estabelece que a GPU aceita essa tabela.** Isso é o passo 5 do plano e
exige ligar tradução — o que substitui o bypass do qual todo o acesso a memória
atual depende. Nada disso aconteceu aqui, e o gate C do ensaio existe exatamente
para provar que não aconteceu.

### Correção de um erro meu de aritmética

Na mensagem em que anunciei a mudança do alvo, calculei de cabeça
`PTE[0] = 0x000300045D000071` — errado, faltou um nibble. O valor correto,
confirmado pela medição, é `0x000300045FD00071`.

A previsão original, registrada no documento antes da primeira execução e
calculada por ferramenta em vez de mentalmente, estava certa
(`0x000300045FF20071` para o alvo `0x0FF20000`). A lição é sobre o método: cálculo
de endereço feito de cabeça não deve entrar em documento nem em mensagem sem
passar por ferramenta.

## O dump "somente leitura" adquiria um semáforo de hardware (2026-08-23 10:05)

Evidência: `docs/evidence/2026-08-23_10-05-03-DESKTOP-HQAG6T4-gcvm-transaction`.

O passo 2 passou em oito dos nove gates. O gate E — dump de tradução idêntico
antes e depois do restore — reprovou com exatamente duas diferenças:

```
INVALIDATE_ENG0_SEM : 0x00000001 -> 0x00000000
INVALIDATE_ENG0_ACK : 0x00000000 -> 0x00010000
```

Nenhuma das duas é efeito do restore. `GCVM_INVALIDATE_ENG0_ACK.SEMAPHORE` é o
bit 16, então `0x00010000` significa "semáforo retido". E o `SEM` passou a ler 0
porque já estava retido.

Quem o reteve foi o próprio dump. O upstream diz literalmente, em
`gmc_v10_0.c`:

- `/* a read return value of 1 means semaphore acuqire */`
- `/* write with 0 means semaphore release */`

**Ler `GCVM_INVALIDATE_ENG0_SEM` não é observação: é aquisição.** O comando
`gcvm dump`, anunciado como somente leitura e usado como tal desde a captura das
09:26, adquiria o semáforo da engine de invalidação do GFXHUB e nunca o
devolvia. As duas diferenças do gate E são um único fato: a segunda leitura do
dump encontrou o semáforo que a primeira havia tomado.

### Por que isso importa mais do que parece

1. **Um instrumento com efeito colateral contamina tudo que mede.** Todas as
   capturas GCVM anteriores incluíram uma aquisição de semáforo não declarada.
   Nenhum dano foi observado, porque com tradução desligada nada mais usa essa
   engine — mas isso é sorte de contexto, não desenho.
2. **O passo 4 do plano usa exatamente essa engine.** Invalidar a TLB exige o
   protocolo adquirir/invalidar/liberar. Começar esse passo já segurando um
   semáforo tomado por um dump anterior quebraria o protocolo antes da primeira
   tentativa, e o sintoma apareceria longe da causa.
3. **O gate E fez o trabalho dele.** Ele existia para provar que o restore não
   mudava nada, e o que ele pegou foi o instrumento. Um ensaio que só comparasse
   os 24 registradores da transação teria passado e escondido isto.

### Correções

- `GCVM_INVALIDATE_ENG0_SEM` **saiu** do dump. Não há leitura segura desse
  registrador.
- O estado do semáforo passou a ser observado pelo bit `SEMAPHORE` de `ACK`, que
  não tem efeito de leitura, e o CLI imprime `SEMAFORO retido: SIM/nao`.
- Novo comando `gcvm semrelease`, que escreve 0 e confere pelo `ACK` se o
  semáforo saiu de retido. É ação explícita, exige `arm write`, e existe separada
  de qualquer dump — porque adquirir e liberar semáforo são ações e não podem
  estar embutidas numa leitura.

### O que o passo 2 já demonstrou, apesar disso

Os gates A, B, C, D, F, G, H e I passaram: o bypass funcionava antes, os 24
registradores foram guardados, o `verify` não achou divergência, o `restore` foi
aceito por todos os 24 com readback conferido, **uma submissão SDMA com fence
física e dado conferido voltou a funcionar depois das 24 escritas**, o `verify`
final ficou limpo, o destino foi restaurado e não houve bugcheck.

Ou seja: reescrever os mesmos valores nesses 24 registradores não tem efeito
colateral sobre o caminho de memória. A rede de segurança do passo 3 existe. O
que faltou foi um instrumento que não mexesse no que estava medindo.

## Passo 2 do gate de memória APROVADO, nove de nove (2026-08-23 10:12)

Evidência: `docs/evidence/2026-08-23_10-12-46-DESKTOP-HQAG6T4-gcvm-transaction`.

Com o dump corrigido — sem ler o semáforo — o gate E passou e o passo 2 fechou:

| Gate | Resultado |
|---|---|
| A | bypass funcionando antes de tocar na transação |
| B | 24 registradores guardados |
| C | `verify` sem divergência |
| D | `restore` aceito pelos 24, com readback conferido |
| E | dump de tradução idêntico antes e depois |
| F | submissão SDMA com fence física funcionando depois das 24 escritas |
| G | `verify` final sem divergência |
| H | destino restaurado, WPTR zero |
| I | nenhum bugcheck |

O que isso estabelece: **existe rede de segurança para o passo 3.** Reescrever os
24 registradores de configuração de tradução não perturba o caminho de memória, e
o mecanismo de snapshot/restore foi exercitado de verdade em vez de assumido.

Nenhum valor novo foi escrito em registrador de tradução por este ensaio.

### Fragilidade de manutenção corrigida na mesma passada

Os ensaios fixavam a versão da ABI no preflight (`bc250dbg v10.0 ABI 10`, etc.).
Com a ABI subindo de 4 para 14 ao longo do dia, cada mudança deixava ensaios
antigos reprovando por motivo cosmético — e um preflight que recusa por
maquiagem treina quem opera a ignorar preflight.

A compatibilidade real já é imposta pela biblioteca, que recusa o driver quando
`AbiVersion` diverge do header com que o CLI foi compilado. Os ensaios passaram a
checar apenas que `info` respondeu com versão/ABI e que o armamento está em
`readonly`.

## Passo 3, estágios inertes: 24 valores novos escritos e desfeitos (2026-08-23 12:59)

Evidência: `docs/evidence/2026-08-23_12-59-45-DESKTOP-HQAG6T4-gcvm-program`.

Dezessete dos dezoito gates passaram. O décimo oitavo foi defeito de instrumento,
detalhado no fim desta seção.

### O que foi escrito no silício, e conferido

Vinte e quatro escritas em registradores de tradução, todas confirmadas por
readback:

| Estágio | Registrador | Antes | Depois |
|---|---|---:|---:|
| `gart` | `CTX0_PT_BASE_LO` (`0x0A3AC`) | `0x00000000` | `0x5FF10000` |
| `gart` | `CTX0_PT_BASE_HI` (`0x0A3B0`) | `0x00000000` | `0x00000004` |
| `gart` | `CTX0_PT_END_LO` (`0x0A4AC`) | `0x00000000` | `0x000001FF` |
| `system` | `AGP_BOT` (`0x0A60C`) | `0x00000000` | `0x00FFFFFF` |
| `system` | `SYS_APERTURE_HIGH` (`0x0A618`) | `0x003D0800` | `0x003D07FF` |
| `system` | `SYS_APERTURE_DEFAULT_LSB` (`0x0A5B0`) | `0x00000000` | `0x0045FF20` |
| `system` | `FAULT_DEFAULT_ADDR_LO` (`0x0A13C`) | `0x00000000` | `0x0045FF20` |
| `system` | `FAULT_CNTL2` (`0x0A124`) | `0x00020000` | `0x00060000` |
| `identity` | `IDENTITY_APER_LOW_LO` (`0x0A148`) | `0x00000000` | `0xFFFFFFFF` |
| `identity` | `IDENTITY_APER_LOW_HI` (`0x0A14C`) | `0x00000000` | `0x0000000F` |
| `invrange` | `ENG0_ADDR_RANGE_LO` (`0x0A31C`) | `0x00000000` | `0xFFFFFFFF` |
| `invrange` | `ENG0_ADDR_RANGE_HI` (`0x0A320`) | `0x00000000` | `0x0000001F` |

As outras doze escritas gravaram o mesmo valor que já estava lá — e o readback
confirma cada uma, o que é a informação relevante: não são escritas que
"falharam", são campos cujo valor do POST já coincidia com o do upstream.

Duas confirmações que valem por si:

- **`PT_BASE` bate com a aritmética independente.** `FB_OFFSET = 0x00000450`
  dá `vram_base_offset = 0x450000000`; tabela em `0x0FF10000` dá
  `PA = 0x45FF10000`, que divide em `LO = 0x5FF10000` e `HI = 0x00000004`.
  É o que o hardware leu de volta.
- **A divergência de `SYS_APERTURE_HIGH` foi prevista antes da medição.**
  `fb_end >> 18` dá `0x003D07FF`; o POST tinha deixado `0x003D0800`. Estava
  registrado em `03_CATALOGO_DE_REGISTRADORES.md` como diferença esperada, com a
  razão (o upstream usa `max(fb_end, agp_end)`, o POST arredondou). Previsão
  escrita antes vale mais que explicação escrita depois.

### Os dois controles negativos recusaram, e sem deixar marca

`gcvm program gart` **antes** do snapshot: recusado, zero escritas, e o dump de
tradução comparado depois ficou idêntico. Recusa que escreve seria pior que
aceite honesto, porque a saída afirma que nada aconteceu.

`gcvm program tlb` **sem** `-danger`, já armado e com snapshot aberto: recusado
pela regra do estágio de risco, zero escritas.

### O gate que importa

Com os 24 valores novos no lugar, uma submissão SDMA com fence física e dado
conferido no kernel: `4 dwords, 0 divergencias`. A memória continua alcançável.
Readback diz que o registrador aceitou o valor; só a submissão diz que o caminho
de memória sobreviveu.

Depois: `restore` dos 33 registradores sem divergência, dump de tradução idêntico
ao do início do ensaio, e a mesma submissão voltando a funcionar. Rollback da
page table e do anel limpos, `WPTR` de volta a zero, nenhum bugcheck.

E o gate `K`, que era o mais desconfortável de escrever: dos oito campos do dump
que mudaram, **nenhum** estava fora da lista prevista. Nada foi escrito por
acidente.

### Traducão continua desligada

`CONTEXT0_CNTL.ENABLE_CONTEXT` nunca saiu de zero. `SYSTEM_ACCESS_MODE` continua
0 e `VMC_TAP_PDE/PTE_REQUEST_PHYSICAL` continuam 1. Nada aqui prova que a placa
traduz endereço — prova que os registradores de tradução aceitam configuração
nova, com readback, e que desfazer funciona. A tradução em si é o passo 5.

### Falha de instrumento: terceira aparição da mesma classe

O gate `D` reprovou dizendo que a page table não foi construída. A saída crua na
mesma tela: `conferencia : 0 divergencias em 512 entradas`, com
`PTE[0] = 0x000300045FD00071`, idêntico à medição do passo 1.

Causa: o `ptbuild` imprime o número **antes** da palavra
(`conferencia : N divergencias em M entradas`), e o helper genérico do ensaio
procura `divergencias : N`. Devolveu `$null`, e `$null -eq 0` é falso.
`test-gcvm-pagetable.ps1` sempre usou o padrão certo; eu reusei o helper errado.

É a terceira vez que **falha de instrumento se parece com achado de hardware**
neste projeto — depois do veredito lido de variável paralela e do gate avaliado
sem ter sido exercitado. E é a terceira vez que o que salva é a mesma coisa: a
saída trazer sempre os valores crus. `0 divergencias em 512 entradas` ao lado de
um gate reprovado é uma contradição visível em uma leitura.

Regra reforçada: **helper de parse é acoplado ao formato de quem imprime.**
Reusar um parser em saída de outro comando é suposição, não economia.

## TRADUÇÃO DE ENDEREÇO PROVADA NO SILÍCIO (2026-08-23 13:49)

Evidência: `docs/evidence/2026-08-23_13-49-44-DESKTOP-HQAG6T4-gcvm-translate`.
Vinte gates, vinte aprovados. O gate de memória fechou: passos 1 a 6.

### A prova

```
$ bc250ctl.exe sdma write -rawaddr 0x80000001000 -verifyat 0x0FD01000 0x0 \
      0xBC25F1A1 0xBC25F1A2 0xBC25F1A3 0xBC25F1A4

WRITE_LINEAR: endereco CRU 0x80000001000 no pacote, 4 dwords
  conferencia em      : VRAM 0x0FD01000 (redirecionada)
  RPTR 0xCC -> 0x110 (WPTR 0x110)  concluido
  FENCE VRAM 0x0FF03000 : poison 0x43DAFF32 -> observado 0xBC2500CD [ALCANCADO]
  Dados conferidos    : 4 dwords, 0 divergencias (conferido no kernel)
  Conclusao do pacote : SIM (exige RPTR=WPTR, fence GPU e dado conferido)
  leitura de volta pelo BAR0 (offset 0x0FD01000):
    0x0FD01000 = 0xBC25F1A1  ok
    0x0FD01004 = 0xBC25F1A2  ok
    0x0FD01008 = 0xBC25F1A3  ok
    0x0FD0100C = 0xBC25F1A4  ok
```

O pacote carregava `0x0000_0800_0000_1000`. O dado apareceu em `0x0FD01000`.
**Esse offset não aparece em lugar nenhum do pacote.** Só a page table liga um
endereço ao outro, entrada 1 de 512, `PA = 0x45FD01000`.

### Por que não é coincidência

Quatro controles, todos medidos na mesma execução:

| Controle | Medido |
|---|---|
| Destino envenenado imediatamente antes | `0x5A5A0001..4` lidos de volta pela CPU |
| Tradução realmente ligada | `ENABLE_CONTEXT=1`, `SYSTEM_ACCESS_MODE=3`, `L2` habilitada |
| Abertura de framebuffer ainda funciona | submissão para `0x0FF04000` com dado conferido |
| Offset baixo intocado | `0x00001000` = `0xFF0190EC` antes e depois |

O terceiro é o que separa "traduziu" de "quebrou tudo": sem ele, sucesso na prova
poderia ser sorte e falha seria ambígua. O quarto descarta erro de laboratório —
se o modo cru tivesse sido ignorado e a parte baixa do VA virasse offset de VRAM,
o dado estaria lá.

E a fence: escrita fisicamente pela GPU, com um valor que só existia dentro do
pacote. Mais o dado conferido no kernel. O critério de conclusão exige os três.

### Passo 4: invalidação confirmada

```
engine            : 17  (a da GART no upstream)
REQ escrito       : 0x00F80001
REQ relido        : 0x00F80001  (leitura obrigatoria neste chip)
ACK               : 0x00000000 -> 0x00000001  (mascara 0x00000001)
espera            : 0 iteracoes de 1 us
FAULT_STATUS      : 0x00000000
```

Zero iterações: o ACK já estava aceso na primeira leitura depois da leituda muda
de `REQ`. Engine 17, sem semáforo, com a leitura descartada que GC < 10.3.0
exige — as três correções que a leitura do upstream trouxe, todas confirmadas.

### Passo 6: o caminho de volta

`restore` dos 35 registradores sem divergência, dump de tradução idêntico ao do
início do ensaio, e a mesma submissão de abertura de framebuffer voltando a
funcionar com fence e dado conferidos. `FAULT_STATUS` limpo, `WPTR` de volta a
zero, nenhum bugcheck.

### Estado de tradução, ligado e desligado

| Registro | POST (bypass) | Tradução ligada |
|---|---:|---:|
| `GCMC_VM_MX_L1_TLB_CNTL` | `0x00000501` | `0x00001D59` |
| `GCVM_CONTEXT0_CNTL` | `0x007FFE80` | `0x007FFE01` |
| `GCVM_L2_CNTL` | `0x00080602` | `0x00080E01` |
| `GCVM_CONTEXT0_PT_BASE` | `0x0` | `0x4_5FF10000` |

### O que isto NÃO prova

- **Uma página, não a faixa.** Uma entrada de 512 foi exercitada como destino
  traduzido. As outras 511 foram escritas e conferidas pela CPU, não usadas pela
  GPU.
- **Um boot.** Uma execução. Determinismo exige repetição.
- **VMID 0 apenas.** Contextos 1..15 continuam como o POST deixou.
- **SDMA apenas.** Nada aqui diz nada sobre CP/PM4 ou compute. O que foi provado
  é a SDMA traduzindo pelo GFXHUB — o hub que `sdma_v5_0.c` atribui a ela.
- **Divergência declarada em pé.** `L2_CNTL3/4/5` partiram do valor do POST e não
  da constante de reset do upstream, que não existe nos headers disponíveis.

### A tentativa anterior, e o que ela ensinou (13:40)

Evidência: `docs/evidence/2026-08-23_13-40-21-DESKTOP-HQAG6T4-gcvm-translate`.

O estágio `cache` abortou em `GCVM_L2_CNTL2`: escreveu `0x00000003`, leu
`0x00000000`. Não era hardware. Os bits `INVALIDATE_ALL_L1_TLBS` (0) e
`INVALIDATE_L2_CACHE` (1) são **gatilhos**: escrever 1 dispara a invalidação e o
hardware limpa o bit. O upstream escreve esse registrador e nunca o lê de volta.

**Segunda aparição da mesma classe**, depois do semáforo de invalidação:
registrador cuja *escrita é uma ação*, não uma configuração.

A correção não foi enfraquecer a regra de readback. `L2_CNTL2` é misto — bits
21..30 são configuração de verdade e continuam conferidos. Cada escrita do plano
passou a carregar uma máscara de bits excluídos, e a saída declara isso em voz
alta:

```
0x0A104 : 0x00000000 -> 0x00000000  (queria 0x00000003)
          bits 0x00000003 fora da conferencia: sao gatilhos,
          o hardware os limpa depois de agir
```

Exceção silenciosa à regra de readback seria pior que não ter a regra.

E um defeito meu, mais grave: o ensaio aplicava `tlb`, `cache` e `context0` e só
depois conferia os três. Com o `cache` falhando no meio, o `context0` ligou
tradução sobre configuração incompleta — `L2_CNTL3/4/5` nunca escritos. Deu para
desfazer, e os quatro gates finais passaram, mas foi a rede de segurança
funcionando, não desenho. Cada estágio de risco agora aborta na hora.

Vale notar o que essa execução acidentalmente mediu: a placa sobreviveu a uma
configuração de tradução meio-feita e voltou inteira pelo rollback. O passo 2
pagou o que prometeu.

## O PSP não carrega o CP nesta placa, e isso reorienta a frente (2026-08-23 14:21)

Evidência: `docs/evidence/2026-08-23_14-21-34-DESKTOP-HQAG6T4-cp-firmware`.
Seis gates, seis aprovados — mas o resultado que importa está nos dados, não nos
gates.

| Firmware | Tipo | Payload | RespStatus | Veredito |
|---|---:|---:|---:|---|
| `rlc` | 8 | 6272 dw | `0x00000000` | **aceito** |
| `pfp` | 2 | 65792 dw | `0x80000203` | recusado |
| `me` | 1 | 65792 dw | `0x80000203` | recusado |
| `ce` | 3 | 65760 dw | `0x80000203` | recusado |
| `mec` | 4 | 67084 dw | `0x80000203` | recusado |

Nenhum ponteiro de instrução se moveu, antes ou depois. Carregar firmware não põe
o CP a executar, e nada neste ensaio o tirou de reset — o que era o esperado, e
está registrado como gate para que uma surpresa aqui não passasse batida.

### Uma revogação e uma confirmação, no mesmo resultado

**Revogação.** O projeto de referência mediu RLC e SDMA retornando
`0xFFFF0008 = TEE_ERROR_ITEM_NOT_FOUND` e concluiu que o SOS da BC-250 "não tem
itens de firmware para esses tipos". Nossa medição contradiz: com o firmware
correto desta placa, os dois são aceitos com `RespStatus = 0` — e o SDMA
demonstravelmente **executou** depois (F32 fora do HALT, pacotes consumidos,
fence física). Aquela medição usou `navi12_sdma.bin`, firmware de outro chip.

O erro era de **conteúdo**, não de tipo não suportado. `ITEM_NOT_FOUND` era o SOS
dizendo "esse blob não é meu", e foi lido como "esse tipo não existe".

**Confirmação.** O `0x80000203` do CP reproduz exatamente o que a referência
mediu, por um caminho de payload completamente diferente — eles com o driver
lendo o arquivo por caminho NT, nós com o payload vindo de user mode. Dois
caminhos independentes chegando ao mesmo código tornam improvável que seja
defeito de protocolo. É o SOS recusando as microengines do CP, e ponto.

### E o upstream diz por quê

O PSP desta geração (`psp_v11_0_8`) não tem caminho de `load_ip_fw` para o CP. O
GFX10 carrega o CP gráfico por **direct load**, e o mecanismo não é o antigo
streaming de ucode por registrador: em `gfx_v10_0_cp_gfx_load_pfp_microcode()` o
driver

1. põe o ucode em memória alcançável pela GPU;
2. invalida a cache de instruções (`CP_PFP_IC_OP_CNTL.INVALIDATE_CACHE`, com
   espera por `INVALIDATE_CACHE_COMPLETE`);
3. programa `CP_PFP_IC_BASE_CNTL` com `VMID = 0`, `CACHE_POLICY = 0`,
   `EXE_DISABLE = 0`, `ADDRESS_CLAMP = 1`;
4. aponta `CP_PFP_IC_BASE_LO/HI` para o endereço do ucode;
5. escreve a **jump table** por `CP_PFP_UCODE_ADDR`/`DATA`, e termina escrevendo
   a versão do firmware no `ADDR`.

Ou seja: **a microengine busca o próprio ucode da memória, pela cache de
instruções, com `VMID = 0`.**

### O gate de memória não era trabalho paralelo. Era pré-requisito

`CP_PFP_IC_BASE_CNTL` tem campo `VMID`, programado com 0 pelo upstream. Se a
busca de ucode do CP passa pelo VMID 0 do GFXHUB, então a tradução provada às
13:49 é dependência direta do CP — e não uma frente ao lado.

Isso muda a ordem de leitura do projeto: o gate de memória virou fundação do CP,
não um item concluído em paralelo.

### Defeito de instrumento na mesma execução: `BASE_IDX` faz parte do endereço

`RLC_CNTL` e `RLC_SAFE_MODE` são `BASE_IDX 1`. A primeira versão do ensaio
calculou os dois com a base do segmento 0, porque a tabela só guardava
`mmREG_dw`. Os valores relatados como RLC naquela execução **não eram desses
registradores** — eram de outro endereço qualquer, e o gate de legibilidade
passou lendo a coisa errada.

Não mudou nenhuma conclusão sobre firmware, porque o resultado do PSP não depende
desses dois. Mas é a terceira vez que um instrumento errado passa por um gate, e
a correção é estrutural: **toda entrada de registrador declara o segmento**, e a
tabela do ensaio agora traz `dw` e segmento em comentário ao lado do byte.

Isso também resolveu uma pendência antiga do catálogo: a entrada
`UCODE_ADDR/DATA = 0x3E200/0x3E204`, que estava em classe P2 "aguardando
reconciliação de segmento". Com o `BASE_IDX` respeitado, o endereço do PFP é
`0x3E050`/`0x3E054`. `0x3E200` fica em quarentena e não deve ser escrito.

## Segmento 1 do MMIO localizado, e dois headers de terceiros corrigidos (2026-08-23 17:54)

Evidência: `docs/evidence/2026-08-23_17-54-39-DESKTOP-HQAG6T4-mmio-segment1`.
Sete gates, sete aprovados. Sem custo de boot: nada de firmware, sessão não
contaminada.

### A base do segmento 1 é `0xA000` dwords, agora medida

`SCRATCH_REG0..7` em `0x30100`..`0x3011C` guardaram oito valores **distintos**, e
depois o complemento de cada um:

```
escrito  0xBC250000 .. 0xBC250007   lido idêntico
escrito  0x43DAFFFF .. 0x43DAFFF8   lido idêntico
```

Oito endereços consecutivos com oito valores diferentes descartam espelhamento.
O padrão complementar descarta valor preso que por acaso coincidiu.

E o controle: `TCP_WATCH0_ADDR_H` em `0x0CA80` — o **mesmo dword `0x2040`** no
segmento 0 — ficou em `0x00000000` antes e depois. As duas derivações não
colidem, e as escritas foram para onde deviam.

Testemunhas em segmento 0 estáveis: `GRBM_STATUS 0x00003028`,
`GRBM_STATUS2 0x00000008`, `CP_STAT 0x00000000`, `CP_ME_CNTL 0x15000000`.

Classe do segmento 1: **P1 → P0**. E com isso o caminho de direct load do CP,
que mora todo nesse segmento, deixa de depender de derivação não conferida.

### O bônus: `CP_ME_CNTL = 0x15000000` decodificado, e dois headers errados

Para decodificar esse valor eu precisava das máscaras do CP, que o extrato local
não tinha — ele só cobre GCVM/GCMC. Baixei o header autoritativo do kernel de
novo, e o **SHA-256 bateu exatamente** com o registrado no cabeçalho do extrato
(`cba20414...410c167`): mesmo arquivo, integridade confirmada.

O header autoritativo diz:

| Campo de `CP_ME_CNTL` | Shift | Máscara |
|---|---:|---:|
| `CE_HALT` | 0x18 (24) | `0x01000000` |
| `PFP_HALT` | 0x1a (26) | `0x04000000` |
| `ME_HALT` | 0x1c (28) | `0x10000000` |

Dois headers de terceiros em `referencias/` dizem `PFP_HALT = 1 << 30` e
`CE_HALT = 1 << 29`. **Estão errados**, e não é detalhe cosmético: o código deles
escreve `ME_HALT | PFP_HALT | CE_HALT` para haltar as três, o que com os bits
errados vale `0x70000000` — na realidade `ME_HALT` mais `ME_STEP` mais
`PFP_STEP`. Pediria single-step no ME em vez de haltar PFP e CE.

A validação cruzada é o valor medido. `0x15000000` = bits 28, 26, 24 = as três
engines haltadas, que é o estado coerente para o POST. Com os bits de terceiros o
mesmo valor viraria `ME_HALT + PFP_STEP + CE_HALT` — incoerente.

### `CP_HYP_*` é alias, não registrador novo

O upstream escreve a jump table em `mmCP_HYP_PFP_UCODE_ADDR`, que não existe em
`gc_10_1_0_offset.h`. Resolvido lendo `gfx_v10_0.c`, linhas 140-141: o próprio
arquivo define

```c
#define mmCP_HYP_PFP_UCODE_ADDR			0x5814
#define mmCP_HYP_PFP_UCODE_ADDR_BASE_IDX	1
```

Mesmo dword e mesmo segmento de `mmCP_PFP_UCODE_ADDR`. Nome diferente,
registrador único. Idem para ME (`0x5816`) e CE (`0x5818`).

### Headers de ucode conferidos, em arquivo

`bc250ctl cp info` lê e valida `gfx_firmware_header_v1_0` sem tocar em hardware:

| Engine | header | IP | versão | payload | jump table |
|---|---|---|---:|---:|---|
| pfp | v1.0 | v10.1 | `0x94` | 65792 dw | dw 65536, 128 dw |
| me | v1.0 | v10.1 | `0x63` | 65792 dw | dw 65536, 128 dw |
| ce | v1.0 | v10.1 | `0x25` | 65760 dw | dw 65536, 96 dw |

`IP v10.1` bate com o GC 10.1.3 desta placa, e a jump table cabe dentro do
payload nas três. Coerência de header não diz nada sobre o CP funcionar — mas
manda lixo ao hardware quem não confere.

### A armadilha do PowerShell, de novo — e o grep que a pegou

Escrevi `(if ...)` como expressão duas vezes no ensaio novo do CP. É sintaxe de
PS 7; esta máquina roda 5.1, onde falha em tempo de execução, e o
`Parser::ParseFile` **não acusa**. O que pegou foi o grep por `[^$]( *if (`, que
passou a ser parte da checagem depois da primeira ocorrência.

Regra reforçada: **quando uma armadilha reaparece, a checagem dela entra no
procedimento** — confiar em lembrar não escala.

## REVOGAÇÃO: os passos 2, 3 e 4 do CP não estão provados (auditoria de 2026-08-23)

Uma sessão de trabalho registrou os passos 2, 3 e 4 do CP como aprovados e a
barra em 95%. A auditoria das evidências não sustenta isso. O que segue é o que
os arquivos de evidência mostram, e não o que foi escrito sobre eles.

### O fato central: os registradores do CP no segmento 1 leem `0xFFFFFFFF`

Em `docs/evidence/2026-08-23_18-58-23-DESKTOP-HQAG6T4-cp-directload`:

```
IC_BASE da engine pfp:
    0x3E108        : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0xFC7FFFF0)  DIVERGENCIA
    0x3E100        : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0x0E000000)  DIVERGENCIA
    0x3E104        : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0x000000F4)  DIVERGENCIA
```

Ler todos-os-bits-em-um e a escrita ser descartada é assinatura de endereço que
**não está sendo decodificado**. E não é problema de endereço: no mesmo segmento
1, `SCRATCH_REG0..7` em `0x30100` respondem normalmente (medido às 17:54, dois
padrões complementares, 7 de 7 gates), e no segmento 0 `CP_ME_CNTL` lê
`0x15000000` de forma estável em todas as execuções.

### O falso positivo que deixou isso passar

`IC_OP_CNTL.INVALIDATE_CACHE_COMPLETE` é o bit 1. **`0xFFFFFFFF` tem esse bit
ligado.** A espera terminava na primeira leitura:

```
IC_OP_CNTL : 0xFFFFFFFF -> 0xFFFFFFFF -> 0xFFFFFFFF
espera     : 0 iteracoes de 1 us
RESULTADO  : ok, o hardware confirmou por COMPLETE
```

Isso apareceu **idêntico em duas sessões** (16:19 e 18:58) e o gate
`F_cache_invalidada_com_confirmacao` aprovou nas duas. O gate nunca mediu nada.
Culpa minha: escrevi a espera sem checar se o registrador respondia.

Regra nova: **antes de esperar por um bit de status, conferir que o registrador
responde.** Todos-em-um satisfaz qualquer máscara de bit.

### A instabilidade que ninguém explicou

Nos **mesmos endereços**, o comportamento mudou entre sessões:

| Sessão | `0x3E100` antes | depois da escrita |
|---|---|---|
| 16:19 | `0xFFFFFFFF` | `0x0E000000` — **aceito** |
| 18:15 | `0xFFFFFFFF` | `0xFFFFFFFF` — descartado |
| 18:58 | `0xFFFFFFFF` | `0xFFFFFFFF` — descartado |

Uma escrita que cola numa sessão e é descartada em outra, no mesmo endereço, não
é base para declarar passo aprovado. É exatamente o tipo de resultado que este
projeto trata como não medido até entender a variável que mudou.

### Consequência para os passos declarados aprovados

| Passo declarado | O que a evidência sustenta |
|---|---|
| 1. Header & ucode audit | **Sustenta.** É parsing de arquivo, conferido nas três engines |
| 2. Direct load na VRAM | **Parcial.** O ucode na VRAM é real (CPU→VRAM→CPU, dword a dword). `IC_BASE` e a invalidação de cache **não estão provados** |
| 3. CP unhalt, "13/13, PC em `0x0000041E`" | **Não sustentado.** Se `IC_BASE` nunca recebeu endereço válido, o PFP foi liberado do HALT sem ucode. PC parado num valor constante não é execução — é o oposto: o gate de movimento deste projeto exige valores distintos entre amostras |
| 4. Graphics Ring 0 / PM4 | **Não sustentado.** Sem passo 2 e 3 firmes, `RPTR` não avançar não tem interpretação |

### Um endereço que foi trocado por derivação errada

O código chegou à auditoria com `A_CP_PFP_IC_BASE_LO = 0x1AA80`, e o comentário
denuncia a conta: `dw 0x5840 (0x6AA0 * 4)`, isto é `(0x1260 + 0x5840) * 4` — a
base do **segmento 0** aplicada a um registrador `BASE_IDX 1`.

`mmCP_PFP_IC_BASE_LO` existe **somente** no segmento 1. Não há registrador algum
com dw `0x5840` no segmento 0. O endereço correto é `(0xA000 + 0x5840) * 4 =
0x3E100`, e é o que as próprias evidências de 16:19 e 18:58 usaram.

Isto é a lição 12 reaparecendo: *anotar `mmREG` sem `BASE_IDX` é anotar metade do
endereço.* Endereços restaurados para o segmento 1 nos 18 defines afetados.

### Sobre a hipótese do RLC

O diagnóstico proposto foi: o ROQ emite leitura do anel, o barramento não
responde porque o RLC está inativo, e a correção é carregar o RLC pelo PSP,
escrever `RLC_CNTL = 0x1` em `0x17980` e `RLC_SAFE_MODE = 0x0` em `0x17994`.

**A direção é plausível** — o RLC é quem gerencia energia e clock do GFX, e um
bloco de GFX desligado é a explicação mais econômica para `0xFFFFFFFF`. Mas a
receita, como escrita, não pode ser executada:

1. **Os endereços estão errados, pelo mesmo erro.** `RLC_CNTL` é dw `0x4c00`
   **`BASE_IDX 1`** → `(0xA000 + 0x4c00) * 4 = 0x3B000`, não `0x17980`.
   `RLC_SAFE_MODE` é dw `0x4c05` seg 1 → `0x3B014`, não `0x17994`.
   `0x17980` e `0x17994` são derivações do segmento 0 — e foi exatamente esse
   erro que já fez um ensaio relatar como RLC valores de outro endereço qualquer.
2. **Pode ser circular.** Se os registradores de GFX no segmento 1 não respondem,
   `RLC_CNTL` em `0x3B000` provavelmente também lê `0xFFFFFFFF` — e então a
   correção não conseguiria se aplicar. Isso é uma pergunta, não uma objeção: só
   a medição responde.
3. **O mecanismo descrito não bate com o sintoma.** "Silent AXI hang na busca do
   anel" explicaria `RPTR` parado com registradores respondendo. Não explica
   `IC_BASE` lendo `0xFFFFFFFF` antes de qualquer anel existir.

### A medição que decide, e ela é de graça

Um mapa de responsividade do segmento 1, **somente leitura**: quais endereços
respondem e quais devolvem `0xFFFFFFFF`. Já há dois pontos:

| Endereço | dw | Registrador | Leitura |
|---|---:|---|---|
| `0x30100` | `0x2040` | `SCRATCH_REG0` | responde, aceita escrita |
| `0x3E100` | `0x5840` | `CP_PFP_IC_BASE_LO` | `0xFFFFFFFF` |

Falta o mais importante: `RLC_CNTL` em `0x3B000`. Se ele responder, a hipótese do
RLC tem por onde começar. Se devolver `0xFFFFFFFF`, o problema é anterior ao RLC
e a rota muda de novo.

Não custa boot, não contamina sessão, e decide qual das duas rotas existe.

## Mapa de responsividade: a hipótese do GFX desligado cai (2026-08-23 19:36)

Evidência: `docs/evidence/2026-08-23_19-36-05-DESKTOP-HQAG6T4-mmio-responsividade`.
Somente leitura.

| Registrador | dw | seg | Leitura | Veredito |
|---|---:|---:|---|---|
| `GRBM_STATUS` | `0x0da4` | 0 | `0xA0003028` | responde |
| `GRBM_STATUS2` | `0x0da2` | 0 | `0x51000008` | responde |
| `CP_ME_CNTL` | `0x0f56` | 0 | `0x15000000` | responde |
| `CP_STAT` | `0x0f40` | 0 | `0x84008000` | responde |
| `CP_RB0_CNTL` | `0x1de1` | 0 | `0x00000709` | responde |
| `SCRATCH_REG0/7` | `0x2040/47` | 1 | `0x00000000` | responde |
| `GRBM_GFX_INDEX` | `0x2200` | 1 | `0x00000000` | responde |
| **`RLC_CNTL`** | `0x4c00` | 1 | **`0x00000002`** | **responde** |
| `RLC_SAFE_MODE` | `0x4c05` | 1 | `0x00000000` | responde |
| `CP_PFP_UCODE_ADDR` | `0x5814` | 1 | `0xFFFFFFFF` | **não responde** |
| `CP_PFP_IC_BASE_LO/CNTL/OP_CNTL` | `0x5840..43` | 1 | `0xFFFFFFFF` | **não responde** |
| `CP_ME_IC_BASE_LO` | `0x5844` | 1 | `0xFFFFFFFF` | **não responde** |
| `CP_CE_IC_BASE_LO` | `0x5848` | 1 | `0xFFFFFFFF` | **não responde** |

### O que isso derruba

**A hipótese de "bloco de GFX sem energia" está falsificada.** `RLC_CNTL` e
`GRBM_GFX_INDEX` são registradores de GFX no segmento 1 e **respondem**. Se o
bloco estivesse desligado, eles também devolveriam `0xFFFFFFFF`.

A não-resposta é **localizada**: começa em dw `0x5814` e cobre a vizinhança
`CP_*_UCODE_*` / `CP_*_IC_*`. Tudo abaixo de dw `0x4c05` responde.

### E o que ela revela: a sessão estava contaminada

Decodificado com as máscaras do header autoritativo (`gc_10_1_0_sh_mask.h`,
SHA-256 `cba20414...410c167`), comparado com o valor de repouso medido em todas as
sessões anteriores:

| Registrador | Antes (repouso) | Agora | Bits novos |
|---|---|---|---|
| `GRBM_STATUS` | `0x00003028` | `0xA0003028` | `GUI_ACTIVE`, `CP_BUSY` |
| `GRBM_STATUS2` | `0x00000008` | `0x51000008` | `RLC_BUSY`, `CPF_BUSY`, `CPG_BUSY` |
| `CP_STAT` | `0x00000000` | `0x84008000` | `CP_BUSY`, `CE_BUSY`, `PFP_BUSY` |
| `CP_RB0_CNTL` | — | `0x00000709` | `RB_BUFSZ`, `RB_BLKSZ` — **anel configurado** |

**E há uma contradição no meio disso:** `CP_ME_CNTL = 0x15000000` diz que PFP, ME
e CE estão todos em HALT, enquanto `CP_STAT` diz `PFP_BUSY` e `CE_BUSY`. Engine
haltada reportando ocupada não é operação normal — é estado pendurado.

Nada disso vem deste ensaio, que só lê. Vem das execuções de anel PM4 de uma
sessão anterior, que deixaram o CP travado e o anel configurado.

### A explicação que a linha do tempo sustenta

| Hora | O que rodou | `IC_BASE` |
|---|---|---|
| 16:19 | direct load, **antes de existir anel** | escrita **aceita**, readback exato |
| 16:29–16:59 | ensaios de anel PM4 | deixam CP travado |
| 18:15, 18:58, 19:36 | direct load / mapa | `0xFFFFFFFF` |

Isso inverte causa e efeito em relação ao diagnóstico anterior. Não é "o RLC
inativo causa o travamento do ROQ": é **o travamento deixou o bloco do CP sem
responder.** Antes de haver anel, os mesmos registradores nos mesmos endereços
aceitavam escrita.

### Uma correção de decodificação, de passagem

O diagnóstico anterior leu `CP_STAT = 0x84008000` como
`ROQ_RING_BUSY | PFP_BUSY | ME_BUSY`. Pelo header autoritativo é
`CP_BUSY | CE_BUSY | PFP_BUSY`. `ROQ_RING_BUSY` e `ME_BUSY` não estão acesos.

E `RLC_CNTL = 0x00000002` é `FORCE_RETRY`, **não** `RLC_ENABLE_F32` — o bit 0
está apagado. Isso é consistente com a intenção de escrever `RLC_CNTL = 0x1`,
mas o endereço da receita (`0x17980`) continua sendo a derivação errada do
segmento 0; o correto é `0x3B000`, que é onde esta leitura foi feita.

### O teste que decide, e custa um reboot

Rodar o **mesmo mapa em sessão limpa**. As duas leituras possíveis levam a rotas
diferentes:

- **`IC_BASE` responde em boot limpo** → o bloco está íntegro, e o problema é o
  estado pendurado deixado pelos ensaios de anel. A rota do RLC fica
  desnecessária, e o que falta é não travar o CP.
- **`IC_BASE` continua `0xFFFFFFFF`** → é propriedade do bloco, e aí a rota do
  RLC volta à mesa, com os endereços corretos.

O ensaio passou a decodificar os bits de ocupação e a dizer na saída quando a GPU
não está em repouso, porque um mapa tirado sobre GPU travada não é linha de base.

## Boot limpo: a segunda hipótese cai também (2026-08-23 19:43)

Evidência: `docs/evidence/2026-08-23_19-43-11-DESKTOP-HQAG6T4-mmio-responsividade`.
Somente leitura, GPU em repouso.

| Registrador | Sessão contaminada (19:36) | Boot limpo (19:43) |
|---|---|---|
| `GRBM_STATUS` | `0xA0003028` | **`0x00003028`** — repouso |
| `GRBM_STATUS2` | `0x51000008` | **`0x00000008`** — repouso |
| `CP_STAT` | `0x84008000` | **`0x00000000`** — repouso |
| `CP_RB0_CNTL` | `0x00000709` | `0x00A00000` — default do POST |
| `RLC_CNTL` | `0x00000002` | `0x00000000` |
| `CP_PFP_IC_BASE_LO` | `0xFFFFFFFF` | **`0xFFFFFFFF`** |
| `CP_PFP_IC_OP_CNTL` | `0xFFFFFFFF` | **`0xFFFFFFFF`** |

**A GPU está comprovadamente em repouso** — os três registradores de ocupação
voltaram ao valor de repouso, e o `CP_RB0_CNTL` voltou ao default, confirmando
que o `0x00000709` era resíduo dos ensaios de anel.

**E a vizinhança do CP continua sem responder.** Portanto a hipótese do estado
pendurado está falsificada também.

Duas hipóteses, duas quedas:

| Hipótese | Como caiu |
|---|---|
| Bloco de GFX sem energia | `RLC_CNTL` e `GRBM_GFX_INDEX`, GFX no segmento 1, respondem |
| Estado pendurado de sessão anterior | Boot limpo, GPU em repouso, mesma não-resposta |

Isso não é retrocesso: são duas explicações eliminadas por medição, cada uma com
um custo próximo de zero. O que sobra é mais estreito e mais preciso.

### O que agora é fato

A vizinhança `CP_*_UCODE_*` / `CP_*_IC_*` (dw `0x5814` a `0x5848`, segmento 1)
**não decodifica** — em boot limpo, GPU em repouso, com os endereços conferidos.
Tudo até dw `0x4c05` no mesmo segmento decodifica.

### A anomalia que ficou, e ela é minha

Às 16:19 as escritas nesses mesmos endereços foram aceitas com readback exato.
Comparando as duas saídas, o código era diferente:

| | 16:19 | 18:58 |
|---|---|---|
| valor querido em `IC_BASE_CNTL` | `0x00000010` | `0xFC7FFFF0` |

`0x00000010` é o valor construído **a partir de zero**, só com `ADDRESS_CLAMP`.
`0xFC7FFFF0` é o valor construído **a partir do readback** — e o readback era
`0xFFFFFFFF`.

Ou seja: o meu read-modify-write, sobre um registrador que devolve
todos-os-bits-em-um, **propaga o lixo para o valor escrito**. Ele ia escrever
`0xFC7FFFF0`, com todos os bits reservados ligados, e chamar isso de
configuração.

Regra nova: **read-modify-write herda lixo de registrador que não responde.**
Antes de ler-modificar-escrever, conferir que a leitura vale. A guarda
`BcRegResponds()` já recusa a operação; a lição é que o defeito era duplo — não
só esperar por bit num registrador morto, mas também derivar valor dele.

Isso não explica por que 16:19 aceitou. Continua em aberto, e é a única
observação do dia que nenhuma hipótese cobre.

### Terceira aparição do hazard de literal hexadecimal do PowerShell

A execução de 19:43 abortou no fim com

```
Não é possível converter o valor "-2147483648" para o tipo "System.UInt32"
```

`0x80000000` como chave de hashtable foi tipado como `Int32` e virou negativo.
É o mesmo hazard que a telemetria já enfrentou: **acima de `0x7FFFFFFF` o literal
precisa do sufixo `L`**. Corrigido trocando a tabela por lista de objetos com
máscara explicitamente `[uint32]`, e o decode foi testado **em execução**, não só
no parse — porque parse não pega esta classe.

O mapa em si não foi afetado: a exceção veio depois da coleta, e a evidência está
completa.

### O próximo corte, e continua de graça

O mapa foi ampliado com sondas que localizam a fronteira, todas somente leitura:

| Sonda | dw | byte | Para que |
|---|---:|---:|---|
| `CP_MES_DOORBELL_CTRL1` | `0x283c` | `0x320F0` | abaixo da zona morta |
| `RLC_PG_CNTL` | `0x4c43` | `0x3B10C` | power gating do GFX |
| `RLC_CGCG_CGLS_CTRL` | `0x4c49` | `0x3B124` | clock gating de granularidade grossa |
| `RLC_SPARE_INT` | `0x4ccc` | `0x3B330` | usado pela interface RLCG de acesso a registrador |
| `RLC_GPM_STAT` | `0x4e6e` | `0x3B9B8` | estado do GPM do RLC |
| `CGTT_CPF_CLK_CTRL` | `0x50b1` | `0x3C2C4` | **suspeito: clock gating do CPF** |
| `CGTT_CPC_CLK_CTRL` | `0x50b2` | `0x3C2C8` | **suspeito: clock gating do CPC** |
| `CP_MEC_ME1_UCODE_ADDR` | `0x581a` | `0x3E068` | dentro da zona morta, compute |
| **`RLC_GPM_UCODE_ADDR`** | `0x5b61` | `0x3ED84` | **decide: ACIMA da zona morta** |

A última é a que importa. Se `RLC_GPM_UCODE_ADDR` responder, a janela de MMIO
alcança acima da vizinhança do CP e a não-resposta é **propriedade do bloco**. Se
não responder, a hipótese passa a ser limite de janela ou de aperture, e o alvo
da investigação muda de novo.

Os `CGTT_*_CLK_CTRL` entram porque clock gating de bloco é a explicação mais
econômica que ainda está viva para um bloco que não decodifica numa GPU em
repouso com o resto do segmento respondendo.

## O diff Windows/Linux fecha a questão: o RLC está rodando ROM (2026-08-23 23:07)

Evidências: `docs/evidence/2026-08-23_23-07-34-DESKTOP-HQAG6T4-mmio-responsividade`
(Windows, boot limpo, GPU em repouso) e
`referencias/linuxDump/linux-cp-info-192.168.1.204-2026-08-23_20-01-48.json`
(Linux, amdgpu com o CP comprovadamente executando).

Mesmos offsets de byte, mesma placa, os dois somente leitura.

### Prova de que o CP no Linux está executando

| Evidência | Valor |
|---|---|
| `CP_ME_CNTL` | `0x00000000` — nenhum HALT |
| PCs de PFP/ME/CE/MEC1 | `0x172E` / `0xE19` / `0x5CF` / `0x447` |
| `CP_RB0_CNTL` / `BASE` / `RPTR` / `WPTR` | `0x00A0080A` / `0x64D0` / `0x300` / `0x92B00` |
| `SCRATCH_REG0` | `0xDEADBEEF` — o valor literal de `gfx_v10_0_ring_test_ring()` |

### O diff, campo por campo, decodificado com o header autoritativo

| Registrador | Windows | Linux | Campo que difere |
|---|---:|---:|---|
| `CP_ME_CNTL` | `0x15000000` | `0x00000000` | `CE_HALT`, `PFP_HALT`, `ME_HALT` |
| `CP_PFP_INSTR_PNTR` | `0x00000000` | `0x0000172E` | ucode rodando |
| `CP_RB0_CNTL` | `0x00A00000` | `0x00A0080A` | `RB_BUFSZ` 0→0xA, `RB_BLKSZ` 0→8 |
| `SCRATCH_REG0` | `0x00000000` | `0xDEADBEEF` | ring test |
| **`RLC_CNTL`** | `0x00000000` | `0x00000001` | **`RLC_ENABLE_F32` 0→1** |
| **`RLC_GPM_STAT`** | `0x00F40016` | `0x00B40016` | **`RLC_EXEC_ROM_CODE` 1→0** |
| `RLC_CGCG_CGLS_CTRL` | `0x0001003C` | `0x00000000` | `CGCG_GFX_IDLE_THRESHOLD` 0x100→0, `CGLS_REP_COMPANSAT_DELAY` 0xF→0 |
| `CGTT_CPF_CLK_CTRL` | `0x00000100` | `0xF8000100` | os cinco `SOFT_OVERRIDE_*` 0→1 |
| **zona morta `0x3E050..0x3ED84`** | `0xFFFFFFFF` | **`0xFFFFFFFF`** | **igual nos dois** |

### O achado: `RLC_EXEC_ROM_CODE = 1`

No Windows o RLC está **executando código da ROM** — o bootloader interno, sem
ucode. No Linux, com o firmware carregado e `RLC_ENABLE_F32 = 1`, esse bit cai
para 0 e ele roda o microcódigo de verdade.

Isso é o análogo exato do F32 da SDMA saindo do HALT, e é **prova física** de que
o RLC nunca foi inicializado do nosso lado. Não é inferência: é um bit de status
que diz "estou rodando ROM".

### A zona morta não era o bloqueio, e a investigação dela se encerra

`CP_*_UCODE_ADDR` e `CP_*_IC_*` leem `0xFFFFFFFF` **nos dois sistemas**, e o CP
funciona no Linux. Portanto esses endereços não são o caminho que o hardware usa,
ou não são legíveis por MMIO direto em plataforma alguma — provavelmente
write-only.

Três hipóteses minhas morreram nessa medição:

| Hipótese | Como caiu |
|---|---|
| Bloco de GFX sem energia | `RLC_CNTL` e `GRBM_GFX_INDEX` respondem |
| Estado pendurado de sessão anterior | Boot limpo, GPU em repouso, mesma leitura |
| **Clock gating do CPF/CPC** | `CGTT_*` responde, e o CP funciona no Linux com a zona morta igual |

E o `CGTT_CPF_CLK_CTRL` inverteu de papel: os cinco `SOFT_OVERRIDE_*` que o Linux
tem ligados são **consequência** do RLC rodando, não causa do problema. Eu tinha
eleito clock gating do CPF como suspeito principal; o dado diz que ele é efeito.

### A rota do RLC estava certa na direção

O diagnóstico que chegou de outra sessão apontou o RLC. Eu critiquei os endereços
— corretamente, `0x17980` era derivação do segmento 0 de um registrador
`BASE_IDX 1` — e fui atrás de clock gating. **O alvo dele estava certo e o meu
suspeito estava errado.** Vale registrar nessa ordem.

O que muda em relação à receita original: o RLC **não** é carregado por
`RLC_GPM_UCODE_ADDR`/`DATA` (esses não respondem nem no Linux). É carregado pelo
**PSP**, e isso já foi medido aqui: `RespStatus = 0` para o RLC em 14:21, e o
`amdgpu_firmware_info` do Linux confirma `RLC firmware version: 0x0000000d` —
exatamente a versão do `cyan_skillfish2_rlc.bin` que o nosso PSP aceitou.

### Sequência derivada, com prova física definida antes de executar

De `gfx_v10_0_rlc_resume()`, com os endereços do segmento 1 já conferidos:

| Passo | Ação | Endereço |
|---|---|---|
| 1 | carregar RLC pelo PSP | já aceito (`RespStatus = 0`) |
| 2 | `RLC_CGCG_CGLS_CTRL = 0` | `0x3B124` (Windows tem `0x0001003C`) |
| 3 | `RLC_PG_CNTL = 0` | `0x3B10C` (já é 0; escrita idempotente) |
| 4 | `RLC_CNTL.RLC_ENABLE_F32 = 1` | `0x3B000` |
| 5 | **conferir `RLC_GPM_STAT.RLC_EXEC_ROM_CODE` cair de 1 para 0** | `0x3B9B8` |

O passo 5 é o gate. Sem ele, "RLC habilitado" seria afirmação sobre escrita
aceita — e este projeto já sabe o que vale uma escrita aceita sem prova de
execução.

Só depois disso vale reavaliar o CP: com o RLC rodando, os `SOFT_OVERRIDE_*` do
`CGTT_CPF_CLK_CTRL` devem aparecer, e o comportamento da vizinhança do CP pode
mudar — ou não, e aí já não importa, porque o Linux mostra que ela não precisa
responder para o CP funcionar.

## O RLC SAIU DA ROM (2026-08-23 23:32)

Evidência: `docs/evidence/2026-08-23_23-32-03-DESKTOP-HQAG6T4-rlc-bringup`.

O gate era `RLC_GPM_STAT.RLC_EXEC_ROM_CODE` cair de 1 para 0. Caiu.

```
RLC start (ENABLE_F32 = 1):
  RLC_CNTL           : 0x00000001  [ENABLE_F32 1]
  RLC_GPM_STAT       : 0x00B40016  [EXEC_ROM_CODE 0, RLC_BUSY 0]
  RLC_CGCG_CGLS_CTRL : 0x00000000
```

**`RLC_GPM_STAT = 0x00B40016` é bit a bit idêntico ao Linux** com o CP
executando. Não é "um valor plausível": é o mesmo valor.

Primeira prova física na frente do CP, e é do mesmo tipo que provou o
microcontrolador da SDMA: um estado de execução observável mudando, não uma
escrita aceita.

### Quem fez o trabalho não fui eu

O passo `start` mostra isto:

```
0x3B000 : 0x00000001 -> 0x00000001  (queria 0x00000001)
```

O valor **antes** da escrita já era 1. Ou seja: o `RLC_ENABLE_F32` já estava
ligado quando o `start` rodou, e o `start` foi um no-op.

Quem habilitou foi o **PSP**, ao carregar o ucode (`RespStatus = 0`, tipo 8,
6272 dwords). A sequência real foi:

| Passo | Efeito medido |
|---|---|
| `stop` | `ENABLE_F32` a 0 |
| `cgcg` | `RLC_CGCG_CGLS_CTRL` de `0x0001003C` a 0 |
| `pg` | `RLC_PG_CNTL` a 0 |
| **PSP load** | **ucode entregue, `ENABLE_F32` volta a 1, RLC sai da ROM** |
| `start` | no-op |

A prova é real e o crédito é do PSP. Registrar isso importa: sem olhar o `antes`
da escrita, eu teria atribuído ao `start` um efeito que ele não teve — e essa é
exatamente a classe de erro que este projeto persegue.

Fica uma pergunta barata em aberto: `cgcg` e `pg` eram necessários, ou o PSP
sozinho bastaria? Discriminação de um comando.

### O que o RLC mexeu por conta própria depois de subir

| Registrador | Depois do `pg = 0` | No fim |
|---|---:|---:|
| `RLC_PG_CNTL` | `0x00000000` | `0x00000008` |
| `RLC_SRM_CNTL` | `0x00000000` | `0x00000002` |

Nenhum dos dois foi escrito por nós no intervalo — o `srm` nem foi chamado.
São sinais de um microcontrolador vivo mexendo no próprio estado.

### Uma hipótese minha caiu na mesma execução

Eu previ que os cinco `SOFT_OVERRIDE_*` do `CGTT_CPF_CLK_CTRL` apareceriam
sozinhos com o RLC rodando, como consequência. **Não apareceram:**

```
CGTT_CPF_CLK_CTRL : 0x00000100 -> 0x00000100   (Linux: 0xF8000100)
```

Com o RLC fora da ROM, o registrador não mudou. Então não é consequência
automática, e o amdgpu escreve isso em outro lugar que ainda não mapeei. O gate
era declaradamente não-crítico, e virou registro do fato em vez de requisito.

### O gate que recusou pelo motivo errado

`B_recusa_sem_snapshot` reprovou, e a causa é minha:

```
$ bc250ctl.exe rlc stop
falha em 'stop': erro 5 (rode 'arm write')
```

Erro 5 é `ACCESS_DENIED` por **armamento**, não por falta de snapshot. Eu havia
posto o controle negativo antes do `arm write`, então a guarda que ele queria
exercitar — a do snapshot — **nunca foi exercitada**. O gate recusou pela razão
errada.

Isto é pior que a lição 7 ("gate não exercitado não reprova"). Ali o gate não
existia; aqui ele existiu, rodou, e mediu outra coisa — dando a impressão de ter
testado o que não testou.

Regra nova: **controle negativo tem de isolar a guarda que pretende testar.**
Se há duas guardas no caminho, a de fora precisa estar satisfeita, e a mensagem
exigida tem de distinguir uma da outra.

Corrigido: o negativo agora roda **depois** do armamento e **antes** do
snapshot, exige a mensagem `nenhum snapshot`, e registra separadamente se a
recusa veio por armamento — caso em que o próprio gate se declara inválido.


---

## A anomalia das 16:19 tinha causa, e a causa era o instrumento (2026-08-24)

Durante a preparação do ensaio de unhalt do CP encontrei, no próprio driver, o
defeito que explica a anomalia que sobreviveu a todas as hipóteses.

### O código

Modo `icbase`, três escritas em `IC_BASE_CNTL`, `IC_BASE_LO` e `IC_BASE_HI`:

```c
BcRegWriteRaw(e->IcBaseCntl, cntl);
Io->WrOffset[0] = e->IcBaseCntl;
Io->WrBefore[0] = 0xFFFFFFFF;   /* constante, nunca lido */
Io->WrWanted[0] = cntl;
Io->WrAfter[0]  = cntl;         /* o valor QUERIDO, nunca lido */
```

`WrAfter` é o campo de "depois" da evidência. Ele recebia o valor que se queria
escrever. Nenhuma leitura do hardware acontecia em nenhum dos dois lados.

### O que a saída mostrava

```
0x3E108        : 0xFFFFFFFF -> 0x00000001  (queria 0x00000001)
```

Isso é exatamente o que um readback bem-sucedido imprimiria. Foi lido como
"escrita confirmada pelo silício" — e daí saiu a sessão das 16:19, em que o
`icbase` "colou" e nas sessões seguintes, 18:15 e 18:58, "foi descartado nos
mesmos endereços".

Não havia instabilidade de hardware nenhuma. Havia um campo constante numa
sessão e um caminho de código diferente nas outras. **Toda a linha de hipóteses
sobre estado de GPU, clock gating residual e hang remanescente foi construída
sobre um valor que o driver escreveu em si mesmo.**

Registrado como lição 19, e é o defeito mais grave já encontrado nesta árvore —
mais grave que qualquer erro de endereço, porque erro de endereço produz leitura
absurda e chama atenção, enquanto este produzia a leitura *perfeita*.

### A correção

Toda escrita do CP passa por `BcCpWriteEx(Io, offset, valor, writeOnly)`, que lê
o hardware antes e depois, sem exceção. Quando o registrador é write-only, o
campo `WrWriteOnly` marca isso e a CLI imprime:

```
0x3E108        : 0xFFFFFFFF -> 0xFFFFFFFF  (queria 0x00000001)  [WRITE-ONLY: readback nao confirma]
** Escrita write-only nao tem confirmacao possivel. A prova
   destes passos e o ponteiro de instrucao da microengine
   ANDAR depois do unhalt, variando entre amostras.
```

A saída ficou pior de ler e passou a ser verdadeira.

### A referência do Linux resolveu a segunda metade

A guarda `BcRegResponds()`, que eu havia criado para matar o falso positivo do
`INVALIDATE_CACHE_COMPLETE` (lição 13), começou a recusar o `icbase` — e depois o
`jt` — porque os registradores leem `0xFFFFFFFF`. O dump do Linux, da máquina
onde o CP **está comprovadamente executando**, mostra:

| Registrador | dw | seg | byte | Linux |
|---|---|---|---|---|
| `CP_PFP_IC_BASE_CNTL` | `0x5842` | 1 | `0x3E108` | `0xFFFFFFFF` |
| `CP_PFP_UCODE_ADDR` | `0x5814` | 1 | `0x3E050` | `0xFFFFFFFF` |
| `CP_CE_UCODE_ADDR` | `0x5818` | 1 | `0x3E060` | `0xFFFFFFFF` |
| `RLC_GPM_UCODE_ADDR` | `0x5b61` | 1 | `0x3ED84` | `0xFFFFFFFF` |

São write-only. Recusar por não responderem recusaria exatamente o caminho que
funciona do outro lado. A guarda estava certa sobre *não dá para confirmar* e
errada sobre *então não escreva* — lição 20.

### O que isso muda no ensaio de unhalt

Três gates deixaram de exigir prova impossível:

- `E_tres_caches_invalidadas` exigia `confirmou por COMPLETE`. Passou a
  `E_tres_caches_invalidacao_pedida`: a invalidação foi **emitida sem recusa**,
  e o texto do gate diz que isso não é confirmação. Se alguma engine confirmar de
  verdade, fica registrado como observação, não como requisito.
- `F_tres_icbase_apontados` continua exigindo as três escritas, mas o "ok" da CLI
  agora declara que significa *emitidas sem recusa*, não *confirmadas*.
- `G_tres_jump_tables_escritas` deixou de poder ser recusado pela guarda.

E o gate que decide ficou mais estreito, não mais largo:
`I_ponteiros_de_instrucao_ativos` exigia PC **diferente de zero** — critério que
`0x0000041E` parado satisfaz, e foi assim que um passo do CP já foi declarado
aprovado. Agora exige **variação entre amostras**; sair de zero virou observação
fraca, registrada com a nota de que uma engine sem ucode válido para num valor
fixo do bootloader.

Junto com isso, `M_sem_bugcheck` estava literalmente `Add-Gate 'M_sem_bugcheck'
$true` — um gate que afirmava sem nunca consultar o log de eventos. Agora
consulta `Get-WinEvent` na janela do ensaio.

### E o pré-requisito que faltava

O ensaio de unhalt não fazia o bring-up do RLC. Como as 23:32 mostraram que deste
lado o RLC roda a ROM até o PSP carregar o ucode, e que no Linux com o CP vivo ele
está fora da ROM, tentar o unhalt antes disso é medir sobre base ausente. Foi
inserido o gate `B2_rlc_fora_da_rom`, com a sequência que já funcionou —
snapshot, `stop`, `cgcg`, `pg`, `psp loadfw rlc` — e **sem `rlc start`**, porque
está medido que quem liga o F32 é o PSP e o `start` era no-op (lição 18). Se o
RLC não sair da ROM, o ensaio aborta ali.

---

## O unhalt do CP funciona e o CP não executa (2026-08-24 00:19)

Boot limpo, 14 gates, um reprovado — e o reprovado é o que decide.

### O que o unhalt fez, confirmado por readback

`CP_ME_CNTL` é um registrador que **responde**, ao contrário de todo o bloco
`IC_*`. Então aqui há readback de verdade:

```
CP_ME_CNTL antes   : 0x15000000     (PFP_HALT | ME_HALT | CE_HALT)
CP_ME_CNTL depois  : 0x00000000     (os tres HALT limpos)
```

E o efeito nos ponteiros de instrução é real e reversível:

| Momento | PFP | ME | CE | MEC1 |
|---|---:|---:|---:|---:|
| Haltado (baseline) | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| **Depois do unhalt** | `0x0000041E` | `0x0000041E` | `0x0000041E` | `0x00000000` |
| Re-haltado | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |

Liberar o HALT tira os PCs de zero, e reativá-lo os devolve a zero. O caminho de
controle está vivo e faz o que se pede.

### E o que não aconteceu

Oito amostras, valor **constante**: `0x0000041E` nas três engines, do início ao
fim. `variou_entre_amostras = false`. **Não é execução.**

Este é exatamente o valor sobre o qual um passo do CP já foi declarado aprovado
antes, com o critério antigo de "PC diferente de zero". O gate corrigido nesta
mesma sessão reprovou. A correção se pagou na primeira execução.

### O achado: os três param no MESMO endereço

Comparado com o Linux, onde o CP está comprovadamente executando:

| Registrador | Aqui, pós-unhalt | Linux, CP vivo | |
|---|---:|---:|---|
| `GRBM_STATUS` | `0x00003028` | `0x00003028` | idêntico |
| `GRBM_STATUS2` | `0x00000008` | `0x00000008` | idêntico |
| `CP_STAT` | `0x00000000` | `0x00000000` | idêntico |
| `CP_ME_CNTL` | `0x00000000` | `0x00000000` | idêntico |
| `CP_CPF_STATUS` | — | `0x00000000` | idêntico ao repouso |
| `CP_PFP_INSTR_PNTR` | `0x0000041E` | `0x0000172E` | **difere** |
| `CP_ME_INSTR_PNTR` | `0x0000041E` | `0x00000E19` | **difere** |
| `CP_CE_INSTR_PNTR` | `0x0000041E` | `0x000005CF` | **difere** |

Todo registrador de status global concorda com o sistema onde o CP funciona. Isso
derruba de uma vez a família de hipóteses de "bloco desligado, gated ou pendurado":
não há nada em estado anômalo para consertar.

O que sobra é o formato dos ponteiros. No Linux os três estão em **três endereços
diferentes** — cada microengine dentro do próprio ucode, cada ucode com o próprio
tamanho e layout. Aqui os três estão no **mesmo** endereço.

Três microengines com três ucodes diferentes não param no mesmo endereço se
estiverem executando o próprio ucode. O que elas têm em comum é o bootloader
interno. A leitura mais simples é que `0x041E` é ponto de parada comum, e que as
engines nunca chegaram a buscar o nosso microcódigo.

### O que ainda NÃO está estabelecido

Que `0x041E` seja da ROM interna. É a leitura mais simples, não uma medição. Duas
coisas poderiam produzir o mesmo número, e distingui-las custa um boot:

- as engines rodam a ROM, tentam buscar o ucode, a busca nunca retorna e param;
- as engines buscam algo, mas o `IC_BASE` não aponta para onde pensamos — e ele é
  write-only, então nenhum readback pode desmentir isso.

O discriminador é um controle negativo de um bit: **unhalt sem carregar ucode
nenhum, sem `icbase`, sem jump table.** Se o PC ainda parar em `0x0000041E`, a
carga não teve efeito algum sobre as engines e o problema está antes da execução.
Se parar em outro valor, a carga mudou o comportamento e `0x041E` é do nosso
caminho.

Implementado como `-ControleSemUcode`. Um boot, um bit, metade do espaço de
hipóteses eliminado — e é o mesmo padrão que matou as três hipóteses anteriores
da frente do CP.

### Outra observação, de passagem

`RLC_PG_CNTL` era `0x00000000` antes da carga do PSP e virou `0x00000008` depois,
sem que ninguém escrevesse nele. O Linux tem `0x00000000`. Foi o RLC que mexeu
nele por conta própria ao subir — segundo caso de registrador que o RLC altera
sozinho. Anotado, não perseguido: `CGTT_CPF_CLK_CTRL` continua `0x00000100`
contra `0xF8000100` do Linux, e segue não explicado.

### Seis gates gravaram a string `[pscustomobject]@` no lugar dos dados

Achado ao ler a evidência desta execução. Argumento passado como
`[pscustomobject]@{...}` **sem parênteses**, depois de continuação de linha com
crase, é lido pelo PowerShell em modo de argumento como bareword: o gate recebe a
string `"[pscustomobject]@"`.

O booleano do gate é outro argumento, então **o gate passa normalmente** e a
saída do console fica idêntica. Só a evidência desaparece. Perderam dados os
gates B, C, H, J, K e L — incluindo o `CP_ME_CNTL` depois do unhalt, o segundo
número mais importante da execução. Recuperei do log de eventos, que grava a
saída crua dos comandos, e é o único motivo de esta página existir.

Corrigidos os seis, mais dez ocorrências em `test-cp-ring-pm4.ps1`, achadas por
varredura em todos os ensaios. Registrado como lição 23.

---

## O controle que não rodou, e a repetição que valeu (2026-08-24 00:47)

Boot limpo, `-ControleSemUcode` na linha de comando, e o ensaio rodou em **modo
normal**. Catorze gates, não dez. `controle_sem_ucode` saiu **vazio** no
`evidence.json`.

### Por que não engatou

O `param` nunca recebeu o switch. No meu script de patch, a lista de linhas foi
capturada **antes** das substituições, e o `'\n'.join(lines)` no fim sobrescreveu
`s`, descartando a edição do `param` junto. As substituições feitas depois dessa
linha sobreviveram — por isso os blocos `if ($ControleSemUcode)` existem no
arquivo e o parâmetro não.

Com a variável inexistente, o PowerShell resolveu `$ControleSemUcode` para
`$null`, `$null` é falso, e o ensaio seguiu pelo ramo normal **sem uma palavra**.
Um boot gasto.

Registrado como lição 24. O que corrigiu não foi só o `param`:

- `Set-StrictMode -Version 1.0` — variável não inicializada passa a **estourar**
  em vez de virar `$null`. Todas as flags do ensaio já eram inicializadas
  explicitamente, então o custo é zero e a classe inteira morre.
- O modo agora é **impresso na primeira linha** e vai para o `evidence.json`:
  `Modo do ensaio: CONTROLE-SEM-UCODE (10 gates)` contra `NORMAL (14 gates)`.
  Modo que não engata tem de ser visível antes do ensaio começar, não descoberto
  depois na contagem de gates.

Verificado sem gastar boot: rodando sem `-Execute`, o switch liga o eco do modo e
para no guard. Prova o binding com custo zero de hardware.

### O que a execução perdida deu de graça

Ela repetiu o ensaio normal em **segundo boot limpo**, e o resultado é bit a bit
o mesmo:

| | 00:19 | 00:47 |
|---|---:|---:|
| `CP_ME_CNTL` depois do unhalt | `0x00000000` | `0x00000000` |
| PFP / ME / CE | `041E` / `041E` / `041E` | `041E` / `041E` / `041E` |
| variou entre amostras | não | não |

Isso não é pouco. A lição 14 deste projeto nasceu de uma escrita que colou numa
sessão e foi descartada em duas outras — comportamento que muda entre sessões sem
explicação é **não medido**. O `0x0000041E` agora tem duas execuções
independentes, em dois boots limpos, com o mesmo valor nas três engines. É
medição, não coincidência.

O discriminador continua pendente, e agora engata de verdade.

---

## O controle responde: a carga do ucode não tem efeito nenhum (2026-08-24 00:54)

Boot limpo, modo `CONTROLE-SEM-UCODE`, 10 gates. **Nada** de staging de ucode,
`icbase` ou jump table — só bring-up do RLC pelo PSP e o unhalt.

| | Com ucode (00:19 e 00:47) | **Sem ucode nenhum (00:54)** |
|---|---:|---:|
| `CP_ME_CNTL` depois do unhalt | `0x00000000` | `0x00000000` |
| PFP / ME / CE | `041E` / `041E` / `041E` | **`041E` / `041E` / `041E`** |
| variou entre amostras | não | não |
| `CP_STAT` | `0x00000000` | `0x00000000` |
| `GRBM_STATUS` | `0x00003028` | `0x00003028` |

Bit a bit igual. Três execuções, três boots limpos, dois caminhos de código
diferentes — um que carrega 65792 dwords por engine, configura `IC_BASE` e
escreve jump table, e outro que **não faz nada disso**.

### O que isso estabelece

`0x0000041E` é o ponto de parada das microengines quando saem do HALT, e **não
depende do que carregamos**. Portanto:

- o ucode na VRAM, conferido dword a dword, é irrelevante para as engines nesta
  configuração — elas nunca o buscaram;
- o `IC_BASE`, o `IC_OP_CNTL` e a jump table também são irrelevantes por
  enquanto: com ou sem eles o comportamento é idêntico;
- a busca de instrução **nunca acontece**.

Isto encerra, de uma vez, toda a linha de investigação que assumia "o ucode ou a
configuração da cache estão errados". Eles podem estar errados, e não importa: as
engines não chegam lá.

O que sobra é bem mais estreito. Ou o `IC_BASE` não está sendo aceito de verdade
— e ele é write-only, então nenhum readback pode dizer — ou o caminho de memória
para busca de instrução do CP não existe nesta configuração. Vale lembrar que a
tradução está desligada em repouso e a SDMA só alcança memória pelo bypass do
POST; o CP busca instrução por outro caminho, que este laboratório nunca
configurou.

### E o que continua verdade

`CP_ME_CNTL` responde e obedece: `0x15000000` → `0x00000000` com readback, PCs
saindo de zero e voltando a zero no re-halt. Todo status global permanece
idêntico ao Linux com o CP vivo. O passo de unhalt está correto; o que falta é
antes dele.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.** O controle não aprova nada —
ele acabou de demonstrar o contrário.

### Dois defeitos do instrumento nesta execução

**O ensaio imprimiu `SUCESSO: Passo 3 do CP concluido com 100% dos gates
aprovados!`** No controle, todos os gates passando significa que o *controle*
funcionou — e o controle foi feito para **não** executar nada. A mensagem
anunciava aprovação exatamente na execução que demonstrou o oposto. É a lição 2
de novo, e a pior versão dela: o veredito derivava da contagem de gates em vez do
que foi medido. Agora, em modo controle, a saída diz o valor do PC, a leitura, e
`Veredito do passo 3: NAO VALIDADO EM HARDWARE`.

**Os campos `modo` e `controle_sem_ucode` caíram dentro do hashtable do gate
B2**, não no JSON de topo, então saíram vazios no `evidence.json` das três
execuções — apesar de o console mostrar o modo certo. Segunda vez nesta sessão
que um patch meu acerta a linha errada. Corrigido; a evidência agora registra o
modo.

Registrado como lição 25.

---

## A busca de instrução do CP é um acesso TRADUZIDO (2026-08-24, só referência)

Rastreamento de `gfx_v10_0.c` entre `gfx_v10_0_rlc_resume()` e o unhalt, sem
gastar hardware. Três candidatos entraram, dois caíram, um explica tudo.

### Caiu: a jump table pela porta `HYP`

O upstream escreve a jump table em `mmCP_HYP_PFP_UCODE_ADDR`/`_DATA`, não em
`mmCP_PFP_UCODE_ADDR`. Pareceu um erro de endereço nosso, da classe da lição 12.
Conferido no header:

```
gfx_v10_0.c:140   #define mmCP_HYP_PFP_UCODE_ADDR   0x5814
gc_10_1_0_offset.h:10243  #define mmCP_PFP_UCODE_ADDR  0x5814
```

**Mesmo registrador.** `HYP` é só o nome que o upstream usa no caminho de
hipervisor. Nosso `0x3E050` = `(0xA000 + 0x5814) * 4` está correto. Achado
dissolvido por conferência, antes de virar trabalho.

### Caiu: o `IC_BASE_CNTL` estar errado

Campo por campo, o upstream faz `VMID=0`, `CACHE_POLICY=0`, `EXE_DISABLE=0`,
`ADDRESS_CLAMP=1`. Nosso driver constrói exatamente isso, e do zero em vez de por
read-modify-write (correção da lição 15). Bate.

### O achado: o ucode do CP mora em GTT, e o endereço é VIRTUAL

```c
r = amdgpu_bo_create_reserved(adev, pfp_hdr->header.ucode_size_bytes,
                              PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
                              &adev->gfx.pfp.pfp_fw_obj,
                              &adev->gfx.pfp.pfp_fw_gpu_addr, ...);
...
WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_LO,
             adev->gfx.pfp.pfp_fw_gpu_addr & 0xFFFFF000);
```

`AMDGPU_GEM_DOMAIN_GTT` — memória de sistema através da GART. O que vai para o
`IC_BASE` é `pfp_fw_gpu_addr`, um endereço **de GPU traduzido**, e o
`IC_BASE_CNTL` declara `VMID = 0`.

Ou seja: **a busca de instrução da microengine é um acesso traduzido, com VMID 0,
pelo GFXHUB.** Não é leitura de endereço físico de VRAM.

E é exatamente isso que nós fazemos:

```c
Io->GpuAddr = BC250_VRAM_GPU_BASE + (ULONGLONG)Io->VramOffset;   /* 0xF4_0E00_0000 */
```

Endereço físico cru de VRAM, entregue a um consumidor que traduz. Com a tradução
desligada em repouso e o VMID 0 do GFXHUB nunca configurado por este laboratório,
a busca não resolve — e a microengine para no ponto do bootloader.

**Isto casa com todas as três execuções**: o ucode conferido dword a dword é
irrelevante, o `IC_BASE` é irrelevante, a jump table é irrelevante, e o PC é o
mesmo com e sem carga. Nada disso é alcançado porque a primeira busca de
instrução nunca resolve um endereço.

A hipótese 1 (o `IC_BASE` não ser aceito) não precisa mais ser invocada para
explicar o observado. Continua não descartada — é write-only e indemonstrável por
readback — mas deixou de ser necessária.

### Uma ressalva que precisa ficar escrita

A conclusão "`IC_BASE_*` é write-only porque o Linux também lê `0xFFFFFFFF`" veio
de um coletor **em espaço de usuário**. O `gfx_v10_0.c` faz
`RREG32_SOC15(...IC_BASE_CNTL)` e usa o valor num read-modify-write — o que só faz
sentido se o kernel lê algo útil. Pode ser que o caminho de leitura do kernel veja
valores reais e o de usuário não. Não muda nenhuma decisão atual, porque a
construção do `cntl` do zero é correta de qualquer forma, mas **a etiqueta
"write-only" está apoiada em um único método de leitura** e isso tem de estar
declarado.

### O próximo passo é somente leitura, e o instrumento já existe

Se a busca é traduzida por VMID 0 do GFXHUB, então o que decide é a configuração
do VMID 0 e da abertura de sistema. Esses registradores já estão no `gcvm dump`:

```
GCMC_VM_FB_LOCATION_BASE / TOP              0x0A600 / 0x0A604
GCMC_VM_SYSTEM_APERTURE_LOW_ADDR / HIGH     0x0A614 / 0x0A618
GCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB/MSB 0x0A5B0 / 0x0A5B4
```

Em GFXHUB v2, acesso de VMID 0 dentro de `[SYSTEM_APERTURE_LOW, HIGH]` resolve
direto para VRAM via `FB_LOCATION`, sem page table. Então há duas leituras
possíveis, e um dump somente leitura separa as duas:

- a abertura de sistema **não** cobre `0xF4_0E00_0000` → a busca não tinha como
  resolver, hipótese confirmada, e a correção é apontar o `IC_BASE` para um
  endereço que o VMID 0 resolva;
- a abertura **cobre** → então o endereço físico era alcançável e a hipótese cai,
  voltando o foco para o `IC_BASE` não ser aceito.

Custo: zero boot, zero risco, um comando. É o próximo passo.

E a favor do caminho: o gate de memória deste projeto já está **fechado e provado
no silício** (tradução por page table, 20/20 gates, VA `0x800000001000` → VRAM
`0x0FD01000`). Existe tradução funcionando aqui que o CP ainda não usa. A peça que
falta é ligar uma coisa na outra, não inventar uma nova.
