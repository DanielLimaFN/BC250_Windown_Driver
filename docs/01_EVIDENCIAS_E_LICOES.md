# Evidências e lições das tentativas anteriores

## Resumo executivo

Os projetos anteriores reduziram muito a incerteza sobre acesso ao silício, mas nenhum demonstrou uma cadeia completa de aceleração gráfica ou compute no Windows. O maior ganho não é uma implementação reutilizável: é saber quais interfaces físicas respondem, quais testes geram falso positivo e quais endereços históricos estavam errados.

Estado global do produto completo, sob as regras atuais:

**NÃO VALIDADO EM HARDWARE**

Isso se aplica a renderização, compute e ao futuro driver WDDM. Existem provas
físicas independentes de SDMA, GCVM e RLC, descritas abaixo, mas elas não podem
ser promovidas a aceleração gráfica nem a prontidão do produto.

**Atualizacao MCDM 0.5.0.0, evidencia de 2026-08-24 22:43:** o preflight
readonly percorreu o contrato completo. Schema 5 registrou PCI/recursos/MMIO,
topologia 512/256 MiB, `OwnershipFlags=0x7F`, ReBAR 256 MiB, contratos de
endereco/PSP, manifesto real e plano candidato de TMR/staging. Auditoria
posterior mostrou que exclusividade e inventario completo dentro de `0x7F`
eram autoatribuidos pela DDI; portanto ownership e o plano nao ficam validados
fisicamente. O limite ficou
materializado na mesma captura: `FirmwareBytesStaged=0`,
`PspTransportCalls=0` e `ReadonlyGatePassed=1`. Portanto esta evidencia aprova
o planejamento, nao firmware carregado nem compute. O Codigo 43 continua sendo
o retorno fail-closed deliberado.

**Trabalho offline posterior:** foi criado um contrato de staging transacional
fora da DDI. Ele exige SHA-256 antes da primeira escrita, guarda os bytes
anteriores, confere readback e restaura/verifica o conteudo original em falha ou
teardown. Negativos cobrem escrita parcial, corrupcao, rollback impossivel,
faixa, identidade e alias de buffers. Uma lease adicional agora amarra token,
faixa, snapshot, inventario e plano e exige evidencias separadas de espaco livre
runtime e exclusao de scanout/reservas antes de qualquer I/O. Os testes usam
evidencia sintetica: isso fecha logica host, nao ownership fisico, transporte
PSP ou firmware aceito. O miniport atual ainda nao pode emitir essa lease.

**Fonte WDK identificada para scanout herdado:**
`DxgkCbAcquirePostDisplayOwnership[2]` devolve endereco fisico e geometria do
framebuffer anterior. O adaptador offline agora converte uma resposta valida em
reserva `SCANOUT` limitada ao BAR PnP. O callback transfere ownership do display,
portanto nao foi ligado a DDI readonly nem exercitado no hardware. Ele cobre uma
faixa de scanout, nao o inventario completo da VRAM.

**Fonte runtime identificada para IP Discovery TMR:** a referencia AMDGPU usa
`DRIVER_SCRATCH_0/1/2` para offset de 64 bits e tamanho quando a assinatura na
localizacao default nao passa. O leitor offline agora captura os tres valores
em duas passagens readonly e recusa instabilidade, overflow e faixa fora da
VRAM. `SCRATCH_2=0` termina em `NOT_REPORTED`; nenhum fallback no fim da VRAM e
aceito sem assinatura fisica.

**Atualizacao MCDM de 2026-08-25 01:22:** o miniport 0.4.0.0 comprovou no
hardware topologia readonly e ReBAR: 512/256 MiB, mascara `0x700`, 256 MiB
selecionados/atribuidos e teardown limpo. Os flags `0x7F` nao provam ownership,
pois exclusividade/inventario eram acrescentados pelo codigo. Ele parou
intencionalmente antes do manifesto, com Codigo 43. A licao de
firmware seguinte e estrutural: MEC_JT1 e a faixa final do proprio blob MEC,
nao o arquivo `mec2.bin`. Nesta captura, MEC principal tem 267440 bytes e JT1
896 bytes. O 0.5.0.0 confirmou depois, no hardware, somente o planejamento
dessas cargas, com zero bytes staged e zero chamadas PSP.

**Estado autoritativo em 2026-08-24 após as evidências das 17:23, 18:27 e
18:44:** workspace em ABI 29, compilado e assinado, mas não instalado. A última
execução mutante do CP continua sendo a ABI 27 das 16:28: PFP, ME, CE e MEC1
saíram do HALT, porém ficaram com PC constante em `0x0000041E`; `JT_LOADED=0` e
o RPTR do ring GFX não consumiu NOP, probe nem `WRITE_DATA`. Os preflights ABI
29 posteriores foram estritamente readonly. Portanto CP, PM4, compute e WDDM
continuam **NÃO VALIDADOS EM HARDWARE**.

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
| SDMA0 | P0 (100% validado) | SDMA0 inicializado, ring buffer de 4 KB operando; cópias host-to-device e device-to-device com fence e conferência bit a bit no kernel. | Próximo passo é migração para WDDM KMD. |
| GCVM / Tradução VMID 0 | P0 (100% validado) | Page Table de 512 PTEs montada na VRAM mapeando 2 MB em VA 8 TB (`0x0000080000000000`); L2 e GFXHUB TLB invalidation com 20/20 gates aprovados. | VMID 0 ativo para ring e microcódigo. |
| RLC fora da ROM | P0 (100% validado) | PSP carrega firmware RLC (`cyan_skillfish2_rlc.bin`) via GPCOM ring; `RLC_ENABLE_F32 = 1` transiciona `RLC_GPM_STAT.EXEC_ROM_CODE` para 0; CGCG e PG desabilitados. | RLC pronto para suporte ao CP e PM4. |
| CP: controle, staging e rollback | P0 para controle; execução não provada | Quatro ucodes conferidos na VRAM; HALT/unhalt, clocks, golden registers e rollback de 30 registradores exercitados. | PC constante em `0x041E` não prova execução. `JT_LOADED=0`; aceitação efetiva de `IC_BASE`/Jump Table ainda não demonstrada pelo comportamento. |
| CP ring PM4 (Ring 0) | bloqueado | Ring 0 configurável na VRAM ou em VA traduzida; WPTR publicado pelo host. | RPTR permanece em 0; NOP, probe e `WRITE_DATA` não são consumidos. Configuração do ring não é execução. |
| Compute/shaders | bloqueado | MEC microcode carregado na VRAM (`cyan_skillfish2_mec.bin`); MEC1 PC em `0x0000041E`. | Filas HQD/MQD aguardando inicialização completa do CP. |
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

**Fato preservado, interpretação revogada.** O `0x80000203` do CP reproduz o
valor da referência por outro caminho de payload. Isso torna improvável um erro
simples de framing, mas não permite concluir “o SOS não carrega CP”: ambos os
caminhos omitiram diferenças agora identificadas em relação à sequência Linux.

### Caminho direct-load que foi investigado

O direct-load continua sendo um caminho upstream real, mas somente quando
`firmware.load_type == AMDGPU_FW_LOAD_DIRECT`; ele não pode mais ser inferido do
`0x80000203`. Nesse ramo, o mecanismo não é o antigo streaming integral de
ucode por registrador: em `gfx_v10_0_cp_gfx_load_pfp_microcode()` o driver

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


---

## A hipótese da abertura cai: o endereço era alcançável (2026-08-24, `gcvm dump`)

Somente leitura, zero boot. A predição era: se a abertura de sistema do VMID 0 não
cobrisse `0xF4_0E00_0000`, a busca de instrução não teria como resolver.

Decodificado com a convenção do upstream — `gfxhub_v2_0_init_system_aperture_regs()`
programa a abertura com `>> 18` e o `FB_LOCATION` com `>> 24`:

| Registrador | Lido | Decodificado |
|---|---:|---:|
| `SYS_APERTURE_LOW` | `0x003D0000` | `0xF4_0000_0000` |
| `SYS_APERTURE_HIGH` | `0x003D0800` | `0xF4_2000_0000` |
| `FB_LOCATION_BASE` | `0x0000F400` | `0xF4_0000_0000` |
| `FB_LOCATION_TOP` | `0x0000F41F` | `0xF4_1F00_0000` |
| nosso `IC_BASE` | — | `0xF4_0E00_0000` |

**O endereço está dentro das duas faixas.** A abertura de sistema cobre a VRAM
inteira e o `FB_LOCATION` também. A hipótese, na forma testável em que foi
escrita, está **refutada**. Anotado como refutada, não reinterpretada.

E o `MX_L1_TLB_CNTL` reforça: `0x00000501`, com `SYSTEM_ACCESS_MODE` nos bits
[3:2] igual a **0** — o bypass físico documentado, o mesmo pelo qual a SDMA
alcança memória. Endereço físico dentro da abertura, com bypass ligado, é
alcançável.

### O que a leitura revelou de diferente

| Registrador | Aqui | Linux (esperado) |
|---|---:|---|
| `CONTEXT0_CNTL` bit 0 `ENABLE_CONTEXT` | **0** | 1 |
| `CTX0_PT_BASE` | `0x0` | endereço da page table da GART |
| `CTX0_PT_START` / `END` | `0x0` / `0x0` | faixa da GART |
| `L2_CNTL` bit 0 `ENABLE_L2_CACHE` | **0** | 1 |

Ou seja: o VMID 0 daqui **não tem page table nenhuma e o contexto está
desabilitado**, enquanto o amdgpu entrega ao `IC_BASE` um endereço de GTT que só
existe *através* dessa page table. A diferença com o Linux é real — só não é a que
eu havia previsto. Não é "a abertura não cobre"; é "não há contexto de tradução
ativo, e a L2 do GC está desligada".

Isso deixa em aberto uma questão que a leitura não resolve: **a busca de instrução
da microengine honra o bypass do `MX_L1_TLB_CNTL`, como a SDMA, ou vai pelo
UTCL1/UTCL2 do GC e exige contexto ativo?** Se exigir contexto, o endereço estar
dentro da abertura não basta.

Os dois caminhos que sobram, e nenhum é especulação livre:

1. **A busca exige contexto de tradução ativo.** Testável com o que já está
   provado: o gate de memória fechou com tradução por page table (20/20, VA
   `0x800000001000` → VRAM `0x0FD01000`). Ligar `CONTEXT0`, mapear a VRAM do
   ucode e apontar o `IC_BASE` para a VA correspondente reproduz o que o amdgpu
   faz. Custo: um boot, e o caminho de rollback já é conhecido.
2. **O `IC_BASE` não é aceito de verdade.** Volta ao topo agora que a explicação
   alternativa perdeu a base. Continua indemonstrável por readback do lado do
   usuário — e vale lembrar a ressalva já registrada de que o kernel do Linux faz
   read-modify-write nesse registrador, o que sugere que *ele* lê algo útil.

A ordem é 1 antes de 2: a hipótese 1 é construtiva, usa maquinaria provada, e se
funcionar entrega o passo; a 2 só entrega diagnóstico.

---

## ABI 21: o `IC_BASE` passa a aceitar endereço explícito (2026-08-24)

Consequência direta do rastreamento e da refutação da abertura. O amdgpu entrega
ao `IC_BASE` um endereço **traduzido** com `VMID = 0`; este laboratório entregava
`BC250_VRAM_GPU_BASE + offset`, um físico cru. O `gcvm dump` mostrou que o físico
era alcançável, então a pergunta virou outra: **a busca de instrução da
microengine exige contexto de tradução ativo?**

Para responder era preciso poder apontar o `IC_BASE` para uma VA. Dois campos:

```c
unsigned long OverrideValid;
unsigned long long OverrideGpuAddr;
```

`OverrideValid = 0` mantém o comportamento anterior byte a byte — a mudança não
altera nenhuma medição já feita. Na CLI, `cp icbase <engine> <off> -va <addr>`, e
a saída diz explicitamente `VA 0x... (VMID 0, pela page table)` mais o aviso de
que a VA só resolve com `CONTEXT0` ligado e a página mapeada.

Uma correção veio de brinde: a validação de alinhamento conferia `VramOffset`,
não o endereço que ia para o registrador. Com override, o campo conferido
passaria a não ser o campo usado. Agora confere `addr`, que é o que o hardware
recebe.

### O ensaio, e o que ele mede que os anteriores não mediam

`tools/test-cp-va-unhalt.ps1` não inventa caminho novo: junta a fase de tradução
do ensaio que **fechou o gate de memória** (20/20) com a fase de CP do
`test-cp-unhalt.ps1`. A ordem dos sete estágios e a **contagem de escritas de
cada um** (6/10/6/4/1/5/1) são as medidas naquele ensaio — estágio que escreva
número diferente está fazendo outra coisa, e descobrir isso depois de ligar o
`context0` seria caro.

A diferença que importa: a page table mapeia a VRAM **do ucode do CP**
(`0x0E000000`, 336 páginas) em vez de `0x0FD00000`, e a VA de cada engine é a
base de 8 TB mais o deslocamento de VRAM dela.

O gate novo é `H2`, e ele vale mesmo se o ensaio falhar:
`GCVM_L2_PROTECTION_FAULT_STATUS` / `_ADDR` lidos antes e depois do unhalt.
Registrado como **observação e não aprovação**, porque os dois resultados são
informativos e opostos — falha nova na faixa de VA do ucode significa que a
microengine emitiu a busca e a tradução a rejeitou; ausência de falha significa
que a busca não chega ao caminho de memória. Um gate pass/fail aqui esconderia
metade da informação.

E o veredito final do ensaio não vem da contagem de gates (lição 25): vem de o PC
**variar entre amostras**. Os vinte gates podem passar com o CP parado — foi o
que os três boots anteriores mostraram.

**Ainda não rodado. Veredito: NÃO VALIDADO EM HARDWARE.**

---

## A tradução ligou, o `IC_BASE` recebeu VA, e a busca ainda não acontece (2026-08-24 02:07)

Boot limpo, 20 gates, 19 aprovados. O único reprovado é o único que importa.

O que foi **medido no silício**, não afirmado:

| | Antes deste ensaio | Depois |
|---|---:|---:|
| `CONTEXT0_CNTL` | `0x007FFE80` | **`0x007FFE01`** (`ENABLE_CONTEXT` = 1) |
| `CTX0_PT_BASE_LO` | `0x0` | **`0x5FF10000`** (tabela em `0x0FF10000` + `vram_base_offset`) |
| TLB do VMID 0 | — | invalidada com **ACK do hardware** |
| `IC_BASE` das três engines | físico `0xF4_0E..` | **VA `0x800000000000` / `+0x80000` / `+0x100000`** |

Os sete estágios do GCVM aplicaram com a **mesma contagem de escritas** medida no
gate de memória (6/10/6/4/1/5/1), zero divergências. A tradução estava ligada de
verdade.

E o resultado:

```
PFP / ME / CE            0x0000041E / 0x0000041E / 0x0000041E
variou entre amostras    não   (8 amostras, valor único em cada ponteiro)
GCVM_L2_PROTECTION_FAULT_STATUS   antes 0x00000000   depois 0x00000000
GCVM_L2_PROTECTION_FAULT_ADDR     antes 0x0          depois 0x0
```

### O que o gate `H2` estabeleceu, e ele é o achado desta execução

**Nenhuma falha de tradução.** Com `CONTEXT0` ligado, a page table cobrindo a
faixa `0x800000000000..0x8000014FFFF` e o `IC_BASE` apontado para dentro dela, uma
busca de instrução que tivesse sido *emitida* teria produzido acerto (e o PC
andaria) ou falha (e o `FAULT_STATUS` acenderia). Não houve nem um nem outro.

Isso é mais forte que "não funcionou": **a microengine não emite a busca de
instrução**. O bloqueio está antes do caminho de memória, e não nele.

Foi para isso que o `H2` foi escrito como observação e não como gate pass/fail —
a ausência de falha era um dos dois resultados informativos, e é o que veio.

### O que caiu com isto

Quatro boots limpos, quatro configurações, mesmo `0x0000041E`:

| Boot | Configuração | PFP/ME/CE |
|---|---|---:|
| 00:19 | ucode + `IC_BASE` físico + jump table | `041E` |
| 00:47 | idem, reprodução | `041E` |
| 00:54 | **controle:** nada de ucode, `IC_BASE` ou jt | `041E` |
| 02:07 | **tradução ligada + `IC_BASE` em VA mapeada** | `041E` |

Toda a família de hipóteses de **endereço** está encerrada: físico fora de
alcance (refutado pelo `gcvm dump`), físico entregue a consumidor que traduz
(refutado aqui, com tradução ligada), ucode ou cache errados (refutado pelo
controle de 00:54). Nenhuma delas sobrevive.

### A hipótese que sobra, e por que ela é de outra natureza

Todas as quatro execuções assumiram **carga direta**: o driver escreve o ucode e
aponta o `IC_BASE`. Se este chip carrega o GFX **pelo PSP**, o driver nunca
deveria tocar o `IC_BASE` — o PSP escreve o ucode no TMR dele e libera as
engines. E a busca não acontecer seria exatamente o esperado, em todas as quatro.

O que apoia isso com o que já está em mãos, e não por suposição:

- os arquivos deste projeto são `cyan_skillfish2_*.bin`, a variante 2;
- o PSP **deste board** aceitou o RLC da mesma família com `RespStatus = 0` e
  habilitou o `RLC_ENABLE_F32` **por conta própria** — medido, sem nenhum
  `rlc start` nosso;
- o caminho de PSP deste laboratório **já implementa** `CP_PFP`, `CP_ME`, `CP_CE`
  e `CP_MEC`, e nunca foi exercitado para eles.

O ensaio existe: `tools/test-cp-psp-load.ps1`, 11 gates. Troca as fases D..G
(staging, `icinval`, `icbase`, jump table) por `psp loadfw pfp | me | ce` e mantém
tudo o mais idêntico — mesmo pré-requisito de RLC, mesmo unhalt, mesma
amostragem, mesmo rollback. Isso o torna também um segundo controle: se o PC
variar ali e não variou nas quatro execuções anteriores, **a diferença é o
caminho de carga, e nada mais**.

Gate novo `E2`: o `CP_ME_CNTL` lido depois da carga pelo PSP e antes de qualquer
unhalt nosso. Se o PSP liberar as engines sozinho — como fez com o F32 do RLC —,
aparece ali. Observação, não aprovação.

E não existe gate K neste ensaio, deliberadamente: nenhum ucode é escrito na VRAM
pelo driver, então conferir devolução de VRAM reprovaria por ausência de
snapshot, escondendo com um vermelho falso o único resultado que importa.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## O ungate do clock do CPF nunca chegou ao registrador (2026-08-24 02:14)

### Primeiro, um boot gasto por minha conta

Propus o ensaio de carga pelo PSP sem consultar a evidência do próprio projeto.
`docs/evidence/2026-08-23_14-21-34-...-cp-firmware` já havia medido, e a seção
"O PSP não carrega o CP nesta placa" já dizia, com o mesmo `0x80000203` nos mesmos
quatro tipos. A execução das 02:14 reproduziu um resultado conhecido:

| Firmware | Tipo | RespStatus |
|---|---:|---:|
| `pfp` | 2 | `0x80000203` |
| `me` | 1 | `0x80000203` |
| `ce` | 3 | `0x80000203` |

`CP_ME_CNTL` seguiu `0x15000000` depois da carga — o PSP não libera as engines,
porque não aceitou nada. Fence alcançado nas três: o PSP processou e recusou.

Uma coisa nova saiu: a hipótese "os .bin do CP não são assinados, e é por isso que
o PSP recusa" **cai por conferência de arquivo**, sem hardware. Todos os seis
firmwares têm a mesma estrutura, header de 0x100 + ucode, sobra zero — incluindo
o `rlc`, que o PSP **aceita**. A diferença não está no arquivo.

**Regra que faltava:** antes de propor ensaio que gasta boot, procurar o valor nas
evidências. O grep de `0x80000203` no `docs/` responderia em um segundo.

### O achado: nove endereços errados no `BcGfxInit`

Auditoria contra `gc_10_1_0_offset.h`, zero hardware:

| Registrador | `BASE_IDX` | Correto | No driver | |
|---|---:|---:|---:|---|
| `mmCGTT_CPF_CLK_CTRL` | 1 | `0x3C2C4` | `0x18C44` | **errado** |
| `mmCGTT_SPI_CLK_CTRL` | 1 | `0x3C200` | `0x18C48` | **errado** |
| `mmCGTT_SQ_CLK_CTRL` | 1 | `0x3C230` | `0x18C4C` | **errado** |
| `mmCGTT_SQG_CLK_CTRL` | 1 | `0x3C234` | `0x18C50` | **errado** |
| `mmCGTT_VGT_CLK_CTRL` | 1 | `0x3C210` | `0x18C54` | **errado** |
| `mmCGTT_WD_CLK_CTRL` | 1 | `0x3C218` | `0x18C58` | **errado** |
| `mmGRBM_GFX_INDEX` | 1 | `0x30800` | `0x0802C` | **errado** |
| `mmGB_ADDR_CONFIG` | 0 | `0x098F8` | `0x09800` | **errado** |
| `mmCP_RB_WPTR_DELAY` | 0 | `0x08704` | `0x0C128` | **errado** |

O padrão é único e é a lição 12: o `dw` estava certo (`0x50B1` para o CPF), e
somaram a base do **segmento 0** onde o header diz `BASE_IDX 1`. `0x18C44` é
`(0x1260 + 0x50B1) * 4` — outro registrador.

E os dois `PSP_DEBUG` (`0x1B9C0`, `0x1B9C4`, rotulados "GPA override / Direct
Physical") **não existem no header autoritativo**: zero ocorrências. Estavam
sendo escritos sem endereço conferido, contra a regra do projeto. Foram para
quarentena, comentados, não apagados.

### Por que isto é candidato de primeira linha, e não mais uma hipótese

`CGTT_CPF_CLK_CTRL` lê `0x00000100` aqui e `0xF8000100` no Linux com o CP
executando — os cinco `SOFT_OVERRIDE_*` ligados. Essa diferença estava registrada
como **pendência sem explicação** desde 2026-08-23, com a nota de que "devem
aparecer sozinhos como consequência do RLC". Não apareceram. Agora se sabe por
quê: o ungate nunca chegou ao registrador.

**CPF é o Command Processor FETCHER** — o bloco que executa a busca de instrução.
Com o clock dele podendo estar gated, a microengine sai do HALT, roda a ROM
interna e para num ponto fixo sem nunca buscar da memória. É exatamente o
observado em cinco boots limpos: PC `0x0000041E` constante nas três engines, e
zero falhas de tradução **com a tradução ligada e o `IC_BASE` em VA mapeada**.

E há um corolário que amplia o alcance: `IC_OP_CNTL` e `IC_BASE_*` são do domínio
do CPF. Um clock parado ali explicaria **também** por que aquelas escritas parecem
não ter efeito nenhum. Por isso o ungate entrou **antes** do staging, do
`icinval` e do `icbase` — não apenas antes do unhalt.

### ABI 22: o ungate sob transação

`BC250DBG_CP_TXN_REG_COUNT` foi de 10 para 11 — `CGTT_CPF_CLK_CTRL` entrou na
transação, então o rollback o desfaz. Modo novo `BC250DBG_CP_MODE_CLOCKS`, CLI
`cp clocks`.

Diferença importante em relação a tudo o que se escreveu no CP até aqui: **este
registrador responde.** O `0xF8000100` é conferido por readback de verdade, e o
gate `C7_clock_do_cpf_ungated` exige o valor exato. Não é "emitido sem recusa".

O valor não é inventado nem copiado de outra geração: é o medido no Linux com o CP
vivo, os cinco `SOFT_OVERRIDE_*` sobre o mesmo `0x100` que este sistema já tem.

`tools/test-cp-unhalt.ps1` agora tem 15 gates. Um boot.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## O clock do CPF era real, e não era o bloqueio (2026-08-24 08:32)

Boot limpo, 15 gates, 14 aprovados. O ungate funcionou, **confirmado por readback
de verdade** — este registrador responde, ao contrário dos `IC_BASE`:

```
0x3C2C4 : 0x00000100 -> 0xF8000100  (queria 0xF8000100)
```

O endereço corrigido chegou ao registrador e os cinco `SOFT_OVERRIDE_*` do CPF
ficaram ligados, iguais ao Linux com o CP executando. A pendência aberta desde
08-23 está **fechada**.

E o PC continuou `0x0000041E` nas três engines, constante em 8 amostras.

Então o clock do fetcher **não era o bloqueio**. Isso não é decepção: é a primeira
vez que essa diferença com o Linux sai da lista, e ela saiu por medição, não por
argumento.

### A lista de diferenças com o Linux está quase vazia

| Diferença | Situação |
|---|---|
| `CP_ME_CNTL` HALT | resolvida (unhalt confirmado) |
| `RLC_CNTL.RLC_ENABLE_F32` | resolvida (PSP, gate B2) |
| `RLC_GPM_STAT.EXEC_ROM_CODE` | resolvida (gate B2) |
| `CGTT_CPF_CLK_CTRL` | **resolvida agora, por readback** |
| `CP_RB0_CNTL`, `SCRATCH_REG0` | consequência do ring, não causa |
| `CP_PFP_INSTR_PNTR` | **o sintoma** |

Seis boots limpos, seis configurações, o mesmo `0x0000041E`. Endereço físico,
endereço traduzido com contexto ligado, nada carregado, carga pelo PSP, clock do
fetcher ligado. Nenhuma delas mudou um bit.

### Sobra uma afirmação, e ela era indemonstrável

"`IC_BASE_*` é write-only, logo nenhum readback pode dizer se a escrita foi
aceita." Verdadeiro — e, do jeito que estava, **impossível de tocar por medição**.
Afirmação assim não ajuda a decidir nada.

Há como medir efeito sem readback: escrever **dois valores que o hardware deveria
tratar de forma diferente** e comparar o comportamento. `EXE_DISABLE` do
`IC_BASE_CNTL` desabilita execução a partir da cache de instruções; `0` é o valor
do upstream.

`tools/test-cp-icbase-ab.ps1` faz as duas passagens **no mesmo boot**, com o mesmo
RLC, o mesmo ucode já na VRAM e o mesmo ungate de CPF. Só o bit muda:

- **PC diferente entre A e B** → as escritas de `IC_BASE_CNTL` **chegam** ao
  hardware. O registrador é aceito, e a pergunta passa a ser por que a busca não
  resolve mesmo aceita.
- **PC bit a bit idêntico** → as escritas não produzem efeito observável. A
  suspeita deixa de ser inverificável e passa a ter **evidência positiva**; o
  trabalho vira descobrir o que falta para o bloco aceitar escrita.

O gate `P_diferencial_do_exe_disable` é **sempre verdadeiro** de propósito: os dois
resultados são informativos e opostos, e reprovar um esconderia metade da
informação atrás de um vermelho. A saída diz o que foi medido (lição 25).

ABI 23 (`0x00170000`): campo `ExeDisable`, CLI `cp icbase ... -exedisable`. Com
o campo em 0, o comportamento é byte a byte o anterior.

Uma guarda que precisou existir: `StrictMode 1.0` **não** erra em propriedade
inexistente. Sem checar `$pcA`, a passagem A ausente daria `$null` nos três PCs, a
comparação diria "sem diferença" e o ensaio concluiria o oposto do que mediu — a
lição 2 no lugar mais caro possível. Há um `throw` explícito antes da comparação.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## O A/B respondeu, e o ensaio imprimiu o contrário (2026-08-24 08:45)

Evidência: `docs/evidence/2026-08-24_08-45-43-...-cp-unhalt`. 17 de 18 gates.

### Primeiro, o defeito, porque ele vem antes da conclusão

A saída anunciou:

```
  A (EXE_DISABLE=0): 0x0000041E / 0x0000041E / 0x0000041E
  B (EXE_DISABLE=1): 0x / 0x / 0x
  ACHADO: o bit MUDOU o comportamento.
```

`0x` vazio. `Read-Pntrs` **já faz o próprio laço** de `$Samples` e devolve um
objeto com `.primeira`, `.amostras`, `.valores_distintos` e
`.algum_em_movimento`. Envolvi a chamada num segundo laço, então `$pntrsB` virou
um array desses objetos e `$pntrsB[0].CP_PFP_INSTR_PNTR` **não existe nessa
forma**. `StrictMode 1.0` não erra em propriedade inexistente: devolveu `$null`, e
a comparação `0x041E -ne $null` deu diferença.

Eu havia escrito exatamente essa guarda para o lado A, com o comentário de que
sem ela "o ensaio concluiria o oposto do que mediu". Não a repliquei no lado B, e
foi ali que aconteceu. O gate `P` passou porque foi feito para sempre passar — o
que está certo — e a mensagem final, que era o único lugar onde a resposta
aparecia, disse o inverso do medido.

Terceira vez nesta sequência que um veredito calculado errado passa por um gate.
Registrado como lição 26: **quando uma guarda é escrita para um lado de uma
comparação, ela vale para os dois lados; o assimétrico é o que passa.**

### O que a evidência realmente diz

Os dados estavam gravados e são utilizáveis. As oito amostras da passagem B:

```
CP_PFP_INSTR_PNTR = 1054 (0x041E)   em todas as 8
CP_ME_INSTR_PNTR  = 1054 (0x041E)   em todas as 8
CP_CE_INSTR_PNTR  = 1054 (0x041E)   em todas as 8
algum_em_movimento = false
```

| | `EXE_DISABLE = 0` | `EXE_DISABLE = 1` |
|---|---:|---:|
| PFP / ME / CE | `041E` / `041E` / `041E` | **`041E` / `041E` / `041E`** |
| variou entre amostras | não | não |

**Idêntico bit a bit.** No mesmo boot, com o mesmo RLC, o mesmo ucode já conferido
na VRAM, o mesmo ungate de CPF confirmado por readback, e um único bit de
diferença num registrador que o upstream usa para desabilitar execução pela cache
de instruções.

### O que isso estabelece

O ramo que a evidência aponta é o outro: **as escritas em `IC_BASE_CNTL` não
produzem efeito observável.** A suspeita de que o bloco não está aceitando escrita
deixa de ser inverificável e passa a ter evidência positiva — obtida sem readback,
que é justamente o que era impossível.

Sete boots limpos agora, e o `0x0000041E` sobreviveu a: endereço físico, endereço
traduzido com contexto ligado e zero falhas de tradução, nada carregado, carga
pelo PSP, clock do fetcher ligado, e agora `EXE_DISABLE` invertido.

O trabalho passa a ser: descobrir o que falta para o bloco do CP aceitar escrita
nesses registradores. Candidatos que a auditoria de endereços já pôs na mesa, e
nenhum deles é especulação livre:

- **`GRBM_GFX_INDEX` nunca foi programado no caminho do CP.** O único lugar do
  driver que tentava escrevê-lo usava `0x0802C`, endereço errado — o correto é
  `0x30800`. Vale ler o valor atual antes de escrever qualquer coisa.
- **`CPG_PSP_DEBUG` / `CPC_PSP_DEBUG`**, rotulados "GPA override / Direct
  Physical" no bloco herdado, **não existem no header autoritativo**. Estão em
  quarentena. Se a origem daqueles endereços for estabelecida, voltam à mesa.
- **Domínio de reset do bloco do CP.** `GRBM_STATUS` bate com o Linux em
  execução, o que enfraquece a hipótese, mas ela não foi testada diretamente.

O ensaio foi corrigido: uma única chamada a `Read-Pntrs`, leitura de
`.amostras[0]` e `.algum_em_movimento`, e um `throw` explícito se os três
ponteiros não vierem — pelo mesmo motivo que o lado A já tinha.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## O header que faltava estava no `OldDBG` (2026-08-24)

Fui olhar o `OldDBG/` procurando avanço na execução do CP. **Não há**: o
`BcGfxInit` de lá é init de anel, sem carga de ucode — não toca `IC_BASE`, não faz
jump table, não carrega microcódigo. E a sonda de anel do CLI antigo faz força
bruta sobre bases candidatas usando `0x0C100`, `0x0C154`, `0x0C150`, `0x08700`,
endereços **anteriores à auditoria de segmento**, alguns hoje em quarentena.
Qualquer `SUCESSO!` daquele código teria sido sobre registrador errado, e não há
evidência gravada de que tenha impresso. Nada de lá é reaproveitável como
conclusão.

Mas havia uma observação, e ela abriu duas portas.

### O que o código antigo fazia e nós não fazemos

```c
BcRegWriteRaw(A_CP_ME_CNTL, 0x15000150);  /* ME/PFP/CE HALT + I-cache invalidate */
```

`0x150` são os bits 4, 6 e 8. Conferido no header autoritativo:

```
CP_ME_CNTL__CE_INVALIDATE_ICACHE_MASK   0x00000010
CP_ME_CNTL__PFP_INVALIDATE_ICACHE_MASK  0x00000040
CP_ME_CNTL__ME_INVALIDATE_ICACHE_MASK   0x00000100
```

Existe um caminho de invalidação da cache de instruções pelo **`CP_ME_CNTL`**, um
registrador que **responde**. O caminho que usávamos é o `CP_*_IC_OP_CNTL`, que lê
`0xFFFFFFFF` e já produziu falso positivo neste projeto — a espera por
`INVALIDATE_CACHE_COMPLETE` era satisfeita pelo próprio `0xFFFFFFFF`.

### E o header entregou algo maior

```
CP_ME_CNTL__CE_PIPE0/1_RESET_MASK   0x00010000 / 0x00020000
CP_ME_CNTL__PFP_PIPE0/1_RESET_MASK  0x00040000 / 0x00080000
CP_ME_CNTL__ME_PIPE0/1_RESET_MASK   0x00100000 / 0x00200000
```

**O domínio de reset das microengines está dentro do `CP_ME_CNTL`.** Era a
hipótese que faltava testar, e deixa de precisar de endereço novo, registrador
novo ou varredura: é o mesmo registrador que já lemos, escrevemos, snapshotamos e
revertemos. Nosso `0x15000000` tem os três HALT e **zero reset** — em oito boots
nunca assertamos as pipes nem as tiramos do reset.

### O defeito de fundo, que é o mais importante desta entrada

O `gc_10_1_0_sh_mask_extract.h` em uso tem **zero** campos de `CP_ME_CNTL`. O
header completo nunca foi trazido para `inc/`. Escrevemos `0x15000000` como
**constante mágica** durante oito boots, sem os campos à mão — e por isso os bits
de reset e de invalidação passaram despercebidos o tempo todo. Não foi falta de
ideia; foi falta de olhar a definição do registrador que estávamos escrevendo.

Registrado como lição 27: **antes de escrever um registrador como constante,
trazer o `sh_mask` dele e montar o valor por campo. O que não está no header não é
considerado, e o que não é considerado não aparece na lista de hipóteses.**

O `gc_10_1_0_sh_mask.h` completo (4,7 MB) agora está em `inc/linux_ref/`.

### ABI 24

Dois modos novos, ambos sobre `CP_ME_CNTL`, ambos dentro da transação existente:

| CLI | O que faz |
|---|---|
| `cp pipereset <assert\|deassert>` | assere/limpa os seis bits `PIPE_RESET` **mantendo os HALT** — isto mexe nas pipes, não tira engine do HALT |
| `cp icinval-mecntl` | pede invalidação da I-cache pelo `CP_ME_CNTL` e devolve o registrador ao valor de entrada |

Os bits de invalidação **podem ser auto-limpantes**: são um pedido, não um estado.
Se a leitura logo após a escrita não os devolver, isso significa "auto-limpante"
**ou** "não aceito", e as duas coisas não se distinguem por readback. O driver
relata o que leu e não afirma qual das duas é; o ensaio registra como observação,
não como gate.

`tools/test-cp-pipereset.ps1`, 18 gates. O pulso de reset vem **antes** da carga:
se o reset limpasse algo que a carga escreve, a carga precisaria vir depois.

**Ainda não rodado. Veredito: NÃO VALIDADO EM HARDWARE.**

---

## Três ensaios invalidados por uma linha de zeragem (2026-08-24 13:03)

O ensaio do pulso de reset reprovou o gate `C8` e imprimiu que a hipótese do
domínio de reset estava fechada. **Não estava.** O que a evidência mostra:

```
0x086D8 : 0x15000000 -> 0x15000000  (queria 0x15000000)
```

**`queria 0x15000000`** — mas o valor pretendido era `0x153F0000`. O driver
construiu o valor sem os bits de reset, ou seja, `Io->PipeResetAssert` chegou
zerado. Não é o hardware recusando: é o pedido nunca ter sido feito.

### A causa

```c
/* Zera so a metade de saida, preservando entrada. */
RtlZeroMemory((PUCHAR)Io + FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr),
              sizeof(*Io) - FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
```

`GpuAddr` é a fronteira entre entrada e saída, e **tudo depois dela é zerado antes
de ser usado**. Acrescentei os campos de entrada das ABIs 21, 23 e 24 no **fim**
da estrutura — do lado errado da fronteira. Chegavam preenchidos do user mode e
eram apagados pelo driver antes da primeira leitura. Sem erro, sem aviso, sem
divergência de readback.

### O que cai, conferido na evidência de cada um

| Ensaio | O que dizia | O que de fato aconteceu |
|---|---|---|
| **02:07, ABI 21** — `IC_BASE` por VA traduzida | apontou o `IC_BASE` para `0x800000000000` | `OverrideValid` zerado: escreveu o **físico** `0xF4_0E00_0000`. A evidência registra `queria 0x0E000000 / 0x000000F4`, e o CLI imprimiu a linha do caminho antigo (`VRAM → GPU`). **Estava na tela e eu li por cima.** |
| **08:45, ABI 23** — A/B do `EXE_DISABLE` | comparou o bit em 0 contra o bit em 1 | `ExeDisable` zerado: as duas passagens rodaram com o bit em **0**. "Idêntico bit a bit" comparava configurações idênticas. |
| **13:03, ABI 24** — pulso de reset | assertou e desassertou as pipes | `PipeResetAssert` zerado: escreveu `0x15000000` nos dois lados. As pipes nunca foram assertadas. |

### O que isso muda no estado do projeto

Três conclusões são **retiradas**, e é importante dizer o que cada uma volta a ser:

1. **"O `IC_BASE` por VA traduzida não destravou a busca"** — não medido. O que
   continua válido daquele ensaio é o resto, que não passou por esses campos: a
   tradução foi ligada e confirmada no silício (`CONTEXT0_CNTL 0x007FFE01`,
   `CTX0_PT_BASE_LO 0x5FF10000`, TLB com ACK) e `FAULT_STATUS` ficou em `0` — só
   que agora sem surpresa nenhuma, porque o `IC_BASE` apontava para um endereço
   físico dentro da abertura.
2. **"As escritas em `IC_BASE_CNTL` não produzem efeito observável"** — não
   medido. Volta a ser indemonstrável, como era antes do A/B.
3. **"A hipótese do domínio de reset está fechada"** — não medido. Volta para a
   fila, e continua sendo a mais barata de testar.

O que **não** cai, porque não depende desses campos: RLC fora da ROM, ungate do
CPF, power gating limpo, tradução do VMID 0, recusa do PSP, e o `0x0000041E`
constante em todos os boots.

### A correção, e por que ela é estrutural

ABI 25. Os quatro campos de entrada foram movidos para **antes** de `GpuAddr`, e o
arquivo ganhou guardas de compilação:

```c
C_ASSERT(FIELD_OFFSET(BC250DBG_CP_UCODE_IO, PipeResetAssert) <
         FIELD_OFFSET(BC250DBG_CP_UCODE_IO, GpuAddr));
```

Campo de entrada do lado errado da fronteira passa a ser **erro de compilação**,
não ensaio perdido com boot limpo gasto.

### Lição 28

**Campo de entrada novo numa estrutura que o driver zera parcialmente é falha
silenciosa, e falha silenciosa não aparece em gate.** Os três ensaios passaram
seus gates de "escritas emitidas sem recusa" — porque as escritas foram mesmo
emitidas, só que com o valor errado. A regra: *ao acrescentar campo de entrada,
conferir de que lado da fronteira de zeragem ele caiu, e travar isso em
compilação.*

E a lição operacional, que dói mais: **a prova estava impressa na evidência das
três execuções.** `queria 0x15000000` quando eu esperava `0x153F0000`; `queria
0x0E000000` quando eu esperava a VA. Eu li o veredito do gate e não li o valor. É
a lição 2 outra vez, do lado do leitor em vez do lado do ensaio.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## Evidência pós-ABI 25: o bloqueio é o latch da Jump Table (2026-08-24 13:17–15:59)

Depois de mover os campos de entrada para antes de `GpuAddr`, novas execuções
voltaram a exercer VA explícita, golden registers, quatro ucodes e ring traduzido.
As pastas de evidência vão de `2026-08-24_13-17-24-...-cp-unhalt` até
`2026-08-24_15-59-26-...-cp-ring-translated`.

O último ensaio restaurou 30 registradores do CP e 35 do GCVM sem divergência e
sem bugcheck. Page table, tradução VMID 0, clocks/golden, quatro ucodes e ring em
VA traduzida passaram seus gates de preparação. Os gates de execução reprovaram:

- PFP, ME, CE e MEC1 ficaram em `0x0000041E`, constantes;
- `CP_PFP_JT_STAT`, `CP_CE_JT_STAT` e `CP_MEC_JT_STAT` permaneceram em zero;
- `JT_LOADED=0` em todas as amostras;
- NOP, sequência de contexto e `PACKET3_WRITE_DATA` não foram consumidos;
- RPTR não alcançou WPTR e o resultado físico não foi produzido.

Uma falha metodológica existia nos relatórios: o gate
`I_microengines_em_execucao` aprovava PC não-zero e chamava `0x041E` de execução,
enquanto outro gate no mesmo relatório mostrava `JT_LOADED=0` e os pacotes não
consumidos. Em ABI 27 o gate foi rebaixado a observabilidade dos ponteiros, e o
latch passou a exigir `JT_LOADED=3` para PFP e CE, igual à referência Linux.

**Veredito:** configuração emitida e rollback comprovado; execução do CP e PM4
**NÃO VALIDADOS EM HARDWARE**.

---

## Evidência ABI 27: pulso real de reset não destrava o CP (2026-08-24 16:28)

Evidência:
`docs/evidence/2026-08-24_16-28-16-DESKTOP-HQAG6T4-cp-unhalt`.
Driver SHA-256:
`206A7EEEA270CCF78B45BA57B5E37C5CC7397DEBF5A7D646F858B327DE38E11D`.

O ensaio pós-correção da ABI confirmou no readback o pulso que antes havia sido
silenciosamente zerado: `CP_ME_CNTL` foi de `0x15000000` para `0x153F0000` e
voltou a `0x15000000`, com os seis bits de `PIPE_RESET` e os três HALT mantidos.
Depois da carga e do unhalt:

- `CP_PFP_JT_STAT` e `CP_CE_JT_STAT` continuaram com `JT_LOADED=0`;
- PFP, ME e CE ficaram em `0x0000041E` nas três amostras, sem variação;
- 197.344 dwords das regiões de ucode voltaram sem divergência;
- 30 registradores do CP voltaram sem divergência;
- não houve bugcheck.

Portanto fica fechada somente a hipótese específica **“as pipes nunca receberam
reset”**. O resultado não prova que toda a sequência de reset/init do bloco está
correta. Também não valida firmware, PM4 ou execução: o latch continuou recusado
e não houve prova de consumo.

O relatório registrou `19 medidos de 18 previstos` porque o novo gate de latch
foi acrescentado sem atualizar a constante de apresentação. O harness ABI 27 foi
corrigido para 19 gates no modo normal e 15 no controle. Essa falha contábil não
altera nenhum valor lido do hardware nem o veredito.

Próxima investigação autorizada: auditoria estática da ordem completa de
`gfx_v10_0_hw_init()` para Cyan Skillfish, com inventário de endereços, máscaras,
snapshot e rollback antes de introduzir novas escritas MMIO. O sweep Linux já
foi coletado e não deve ser repetido sem uma lacuna concreta.

**Veredito do passo 3: NÃO VALIDADO EM HARDWARE.**

---

## ABI 29: preflight golden readonly e bloqueio do broadcast

Auditoria estática posterior à evidência das 16:28:

- as 34 entradas de `golden_settings_gc_10_0_cyan_skillfish[]` foram copiadas
  com a mesma ordem, máscara AND e valor OR;
- os endereços foram derivados por SOC15 com `GC_BASE[0]={0x1260,0xA000}` e
  todos aparecem estáveis nas duas passagens do sweep Linux;
- `tools/audit-cp-golden-static.ps1` confere automaticamente 34/34 nomes,
  máscaras, valores e endereços;
- `cp golden preflight` lê duas vezes os 34 registros e os dois GPA, sem armar e
  sem escrever MMIO;
- `SQ_LDS_CLK_CTRL` é a única exceção explícita em que `0xFFFFFFFF` é valor
  legítimo, porque o upstream pede esse valor e o Linux o mede nesse endereço;
- `cp golden apply` continua recusado no kernel com `BC250DBG_OPS_BADOP`.

O motivo da recusa não é mais falta de offset ou máscara. A primeira entrada da
tabela põe `GRBM_GFX_INDEX=0xE0000000`, habilitando broadcast. Um snapshot único
por offset não guarda possíveis diferenças entre SE/SH/instâncias. Restaurar um
único valor em broadcast e anunciar zero divergências seria fabricar rollback.

Os harnesses `test-cp-unhalt.ps1`, `test-cp-pipereset.ps1`,
`test-cp-ring-pm4.ps1` e `test-cp-ring-translated.ps1` foram bloqueados na entrada
da ABI 29. Nenhum deles pode chegar a RLC, staging, unhalt ou ring até existir
snapshot por topologia.

Nenhuma leitura física ABI 29 foi feita nesta etapa e nenhuma escrita foi
emitida.

**NÃO VALIDADO EM HARDWARE.**

---

## Licao MCDM: `STATUS_REVISION_MISMATCH` nao prova erro de versao

Os bindings forcados de 22:30 e 23:51 chegaram a `DriverEntry`, mas pararam
antes de AddDevice com `DxgkInitializeStatus=0xC0000059`. O segundo ensaio usou
uma imagem confirmada em WDDM 2.7 (`0xC004`) e repetiu o resultado; isso revogou
a primeira explicacao por incompatibilidade de versao.

A desmontagem do `dxgkrnl.sys` exato da maquina mostrou que a rotina local usa
o mesmo `STATUS_REVISION_MISMATCH` quando falta qualquer callback base
obrigatorio. A auditoria campo a campo encontrou
`DxgkDdiDispatchIoRequest == NULL` no offset `0x28`; os demais callbacks desse
gate estavam presentes.

Licao operacional: um nome de status e apenas uma pista. Antes de atribuir a
causa, comparar a imagem realmente carregada, a estrutura completa e o caminho
de validacao da build local. O pacote 0.2.2.0 agora preenche o callback com stub
fail-closed e passou somente a validacao offline. Isso ainda nao prova AddDevice,
StartDevice, compute nem execucao no silicio.

**NÃO VALIDADO EM HARDWARE.**

### Contrato IP Discovery para identidade GC

`driver/core/bc250_ip_discovery_contract.{h,c}` implementa o layout packed da
IP Discovery sem reutilizar structs Linux no ABI Windows. As fontes primárias
são o [`discovery.h` upstream](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/discovery.h),
o [`soc15_hw_ip.h` upstream](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/soc15_hw_ip.h)
e o consumidor [`amdgpu_discovery.c`](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c).

O parser verifica assinatura binária `0x28211407`, versões 0/1/2 do container,
checksum do binário, assinatura IP `0x53445049`, versões 0–4 da tabela, checksum
da tabela, até 16 dies e cada record variável conforme número/largura das bases.
Somente um `GC_HWID=11`, instância 0, não harvested e exatamente 10.1.3 produz
identidade. Offset fora da tabela, registro truncado, GC ausente/duplicado ou
versão divergente reprovam e zeram a saída.

Os testes usam blob sintético e cobrem GC válido, versão errada, duplicação,
harvest, checksum corrompido, die fora da tabela e assinatura inválida.

Objeto WDK reproduzível:
`93886E6F4BF6675EC5BB48009708CBBF0E8DCE10AA48C94A4B584BF4339450DF`.
Teste WDK reproduzível:
`A0247C82490B89EB3A25E32D2DC8066E2DC1F1B28456B8F1B7777CEDA784FE43`.

O upstream define uma região de 10 KiB em `VRAM_total - 64 KiB`. Para os 512
MiB medidos, o blob começa em offset VRAM `0x1FFF0000`, fora da abertura CPU de
256 MiB. A captura Windows readonly dessa região ainda não existe. Arquivo
sideloaded não será aceito como identidade runtime da unidade.

Nenhuma VRAM ou MMIO real foi acessada e a DDI ainda mantém o gate de identidade
staged bloqueando validação física.

**NÃO VALIDADO EM HARDWARE.**

---

## Contrato offline da topologia VRAM

O upstream confirma fontes diferentes e não intercambiáveis:

- `RCC_CONFIG_MEMSIZE` é lido como MiB e convertido com `<< 20`;
- `GCMC_VM_FB_LOCATION_BASE` define o início MC com `<< 24`;
- `GCMC_VM_FB_OFFSET` define `vram_base_offset` com `<< 24`;
- `amdgpu_gmc_vram_location` deriva o fim a partir do início e de
  `mc_vram_size`.

`driver/core/bc250_vram_topology_contract.{h,c}` materializa essa semântica sem
acessar MMIO. O caso sintético baseado nas medições desta unidade exige
`0x200 MiB`, BASE/TOP `0xF400/0xF41F`, offset `0x450` e janela CPU de 256 MiB.
O resultado é total 512 MiB, MC
`0xF400000000..0xF41FFFFFFF` e base física `0x450000000`.

Bits reservados, fontes de gerações distintas, intervalo invertido, tamanho
RCC divergente e janela CPU maior/desalinhada são recusados e zeram a saída.
ReBAR continua descrevendo somente a abertura CPU, não o total da VRAM.

Objeto WDK:
`DC712EC7BFE0C312A34F018B45CC8E9647E1B89EC75AE34B57C65C50EC77445D`.
Teste WDK:
`FED5711BED3905CF87D046A0DEEC46175FCF227637C3C73064904BD5665EFB2F`.

Nenhum reader MMIO foi implementado nesta etapa; nenhuma leitura ou escrita de
hardware ocorreu.

**NÃO VALIDADO EM HARDWARE.**

### Reader readonly com duas passagens

`driver/core/bc250_vram_topology_reader.{h,c}` acrescenta a captura tipada sem
acoplar o núcleo a um mapeamento físico. A única superfície de transporte é um
callback `Read32`; não existe callback de escrita. Os offsets vêm das fontes
upstream e do cálculo SOC15:

| Registro | Offset final em bytes |
|---|---:|
| `RCC_CONFIG_MEMSIZE` | `0x378C` |
| `GCMC_VM_FB_OFFSET` | `0xA5AC` |
| `GCMC_VM_FB_LOCATION_BASE` | `0xA600` |
| `GCMC_VM_FB_LOCATION_TOP` | `0xA604` |

O reader exige flags separadas para GC 10.1.3, ownership do MMIO e transporte
readonly. Ele lê os quatro registradores duas vezes, na mesma ordem, e recusa
o snapshot se qualquer valor mudar. Somente depois entrega os valores ao
contrato RCC/GCMC já existente.

Os testes usaram callback sintético e cobriram oito leituras estáveis, falha
intermediária, identidade incompleta, mudança entre passagens e range
incoerente. O transporte PnP/MMIO real ainda não foi implementado.

Objeto WDK reproduzível:
`E5AA57A91357499D8FF7264C3137F938C7CE799AAAC53B74D683AAFFC55ECA5E`.
Teste WDK reproduzível:
`89B4C927D6067D1DE00625ABF4531F88B3CE291D4F643C5A508DB2AB6DC6D63B`.

Nenhum MMIO real foi acessado.

**NÃO VALIDADO EM HARDWARE.**

### Transporte WDK para o reader de topologia

`driver/wdk/bc250_mmio_topology_transport.{h,c}` implementa o limite entre o
recurso PnP traduzido e o callback `Read32`. O adaptador exige exatamente um BAR
resolvido com o índice solicitado, comprimento suficiente para `0xA604+4`,
comprimento representável pela ABI WDK e intervalo físico sem overflow.

O mapeamento usa exclusivamente `DxgkCbMapMemory` com `InIoSpace=FALSE`,
`MapToUserMode=FALSE` e `MmNonCached`. O callback resultante aceita somente os
quatro offsets da topologia; todo outro offset é recusado antes da leitura.
Não há função de escrita, sizing write ou endereço físico default.

O teste WDK substitui map/unmap por callbacks falsos, passa o buffer mapeado ao
reader de duas passagens e confirma oito leituras. Também cobre BAR curto, BAR
duplicado, falha de map e falha de unmap. Nesta última, o estado permanece
mapeado para permitir retry; só um unmap aprovado limpa o ponteiro.

Objeto WDK reproduzível:
`2F5342087C67DA2D4EA9CA8C8FCE16D5DEEACFF098444D703D5D95B99EFE4BA7`.
Teste WDK reproduzível:
`5CC12A63A9738546C07FA748A288269C625E3CD837A42994C135376F3DF774F0`.

O `.sys` fail-closed permaneceu byte a byte igual. Nenhum callback real foi
chamado, nenhum BAR real foi mapeado e nada foi instalado.

**NÃO VALIDADO EM HARDWARE.**

### Correção estática: semântica da máscara golden

A implementação autoritativa em `soc15_program_register_sequence()` é:

```text
novo = (lido & ~mascara) | (valor_OR & mascara)
```

Quando a máscara vale `0xFFFFFFFF`, o upstream escreve diretamente `valor_OR`.
A fórmula inversa anteriormente registrada em parte da documentação foi
revogada antes de qualquer `cp golden apply`, que sempre permaneceu em
quarentena.

Aplicando somente a fórmula, offline, aos 34 valores do preflight ABI 29:

- 30 registros mudariam e 4 já estão no valor projetado;
- 31 offsets existem no sweep Linux;
- 30/31 projeções coincidem exatamente com o Linux;
- a exceção é `RMI_SPARE`: projeção `0xFFFF3101`, Linux `0xFFFF3100`.

O bit divergente é
`RMI_ARBITER_STALL_TIMER_ENABLED_ALLOW_STREAMING`. Nenhuma segunda escrita
upstream foi localizada; auto-clear ou mudança de runtime ainda são hipóteses,
não fatos. O auditor offline e o auditor estático agora conferem também a
semântica correta do RMW.

O upstream enumera SE e SA, mas usa `instance=0xFFFFFFFF` em todas as leituras
de CU/RB observadas. Logo ele não fornece cardinalidade para `INSTANCE_INDEX`;
não inferir “uma instância” desse padrão.

O header também expõe `RLC_RLCS_UTCL2_CNTL` em `0x3BB98`. O sweep Linux mede
`0x00000018`: `GPA_OVERRIDE_VALUE=3`, porém `GPA_OVERRIDE=0`. Nenhuma sequência
upstream localizada usa esse registrador no lugar de `CPG/CPC_PSP_DEBUG`; logo
ele é marcador de estado do RLC, não alternativa autorizada para desabilitar
GPA. Uma tentativa de leitura Windows não abriu `\\.\BC250Dbg` no contexto não
elevado (erro 2), embora o serviço estivesse `RUNNING`; isso não é leitura MMIO
e não produz valor de hardware.

**NÃO VALIDADO EM HARDWARE.**

---

## ABI 29 física: preflight golden recusado com zero escritas

Evidência de 2026-08-24 17:23:
`docs/evidence/2026-08-24_17-23-21-DESKTOP-HQAG6T4-cp-golden-preflight`.

- ABI 29 e armamento `readonly` confirmados;
- 36 valores reportados (34 golden + 2 GPA), com duas leituras por endereço;
- zero escrita autorizada e zero escrita reportada;
- nenhum bugcheck na janela do ensaio;
- quatro leituras estáveis em `0xFFFFFFFF`: `0x33884`, `0x3380C`, `0x3F040`
  e `0x3F044`;
- SHA-256 de `result.txt`:
  `7BCAE2DBAECC4550245C2ED3B4F59D5459AC5BE7D31354CA67DAE16ED52C6712`;
- SHA-256 de `result.json`:
  `8BB6081C909416A4D668ABF353C277FE06F1F578DFF0845207DAB4626E4C02D1`.

Os dois GL2 são offsets autoritativos e o sweep Linux mede `0xFFFFFFCF` após o
golden init. Como o campo cobre 32 bits e a leitura Windows veio antes do init,
`0xFFFFFFFF` pode ser conteúdo real; ainda assim, não há como distingui-lo de
uma não resposta usando apenas essa leitura. Eles ficam **ambíguos**, não
aprovados nem declarados mortos.

Os dois GPA têm offsets SOC15 corretos (`mm 0x5c10/0x5c11`, base 1), mas não
decodificam no estado Windows medido. Eles são dependência da fase direct-load,
não entradas da tabela golden de 34 registros. Permanecem indisponíveis, mas a
auditoria posterior mostrou que não são bloqueadores do ramo PSP.

Classificação conservadora: **32 golden confirmados, 2 golden ambíguos e 2 GPA
indisponíveis**. `cp golden apply` e os harnesses CP invasivos permanecem em
quarentena. O harness foi corrigido para não confundir exit 1 do gate com falha
de transporte e para sempre tentar o `info_after`.
`tools/audit-cp-golden-preflight-evidence.ps1` reproduz essa classificação
offline e passa tanto no Windows PowerShell 5.1 quanto no PowerShell 7.

**NÃO VALIDADO EM HARDWARE.**

---

## Leitura física de `RLC_RLCS_UTCL2_CNTL` e correção do caminho PSP

Leitura elevada fornecida em 2026-08-24:

```text
bar 0x0003BB98 = 0x00000018
```

O valor é idêntico ao sweep Linux. Decodificação: `GPA_OVERRIDE_VALUE=3` e
`GPA_OVERRIDE=0`. Isso confirma um marcador de estado do RLC nos dois sistemas,
mas não torna esse registrador equivalente a `CPG/CPC_PSP_DEBUG`.

A auditoria da ordem upstream corrigiu duas conclusões:

1. `gfx_v10_0_disable_gpa_mode()` só é chamado quando
   `firmware.load_type == AMDGPU_FW_LOAD_DIRECT`. A captura Linux detecta o
   bloco `psp_v11_0_8`, reserva TMR e inicializa SMU; a seleção do bloco PSP em
   `amdgpu_discovery.c` ocorre no ramo `AMDGPU_FW_LOAD_PSP`. Portanto os dois
   `PSP_DEBUG` indisponíveis não bloqueiam o caminho PSP saudável.
2. `psp_v11_0_8.c` conter somente funções do ring não significa ausência de
   carga do CP. `amdgpu_psp.c` genérico executa `LOAD_IP_FW` para a lista de
   ucodes quando o load type é PSP. CE, PFP, ME, MEC e MEC Jump Table entram
   nessa lista.

Isso revoga a interpretação de que o `0x80000203` medido em PFP/ME/CE/MEC prova
direct load obrigatório. O valor continua sendo evidência física válida da
sequência de 2026-08-23, mas aquela sequência não reproduziu o upstream:

- não executou `SETUP_TMR` antes das cargas;
- carregou RLC antes de CP, enquanto a lista upstream encontra CP antes de RLC;
- enviou MEC inteiro como tipo 4, enquanto o caminho PSP separa MEC e MEC1_JT;
- usou endereço físico de CPU no campo em que o upstream fornece endereço MC
  do buffer de firmware.

`tools/test-cp-psp-load.ps1` foi colocado em quarentena sem bypass. Repeti-lo
gastaria um boot e reproduziria uma medição conhecida sem isolar as diferenças
acima. O próximo instrumento deve começar readonly e só ganhar escrita depois
de fechar TMR, endereço MC, ordem, goldens e recuperação.

Esse instrumento readonly foi criado como
`tools/test-cp-psp-preflight.ps1`. Ele não exige igualdade de hash, não chama
`arm`, `SETUP_TMR` ou `LOAD_IP_FW`, e registra duas leituras de bootload, UTCL2,
RLC, controles e ponteiros do CP. Ele foi executado às 18:27 e a evidência está
registrada na seção seguinte.

**NÃO VALIDADO EM HARDWARE.**

---

## ABI 29 física: preflight PSP readonly e quarentena de TMR

Evidência de 2026-08-24 18:27:
`docs/evidence/2026-08-24_18-27-33-DESKTOP-HQAG6T4-cp-psp-preflight`.

- ABI 29 permaneceu em `readonly`; foram feitas 20 leituras e zero escritas;
- `C2PMSG_64=0x80000000`: TOS pronto, sem ring criado pelo laboratório;
- `RLC_RLCS_BOOTLOAD_STATUS=0`: `IRAM_LOADED=0` e
  `BOOTLOAD_COMPLETE=0`; isso descreve esses bits, mas não prova sozinho
  presença ou ausência de TMR;
- `RLC_RLCS_UTCL2_CNTL=0x18`, exatamente igual ao Linux;
- `RLC_GPM_STAT=0x00F40016` no Windows contra `0x00B40016` no Linux. O XOR é
  somente `0x00400000`, o bit `RLC_EXEC_ROM_CODE`: neste estado Windows o RLC
  está na ROM; na captura Linux saudável está fora dela;
- `CP_ME_CNTL=0x15000000` e `CP_MEC_CNTL=0x50000000`; PFP, ME, CE e MEC1
  têm ponteiro de instrução zero nas duas amostras;
- nenhum bugcheck;
- SHA-256 de `result.json`:
  `631C60D1770DD636A367FF7B0F692DF204578E2CD5EDA9E23FD803D7371891AC`;
- SHA-256 de `result.txt`:
  `3DF47F4B745B135AB4C5B2F33D1E662F2E75A2B1B77082AF43A3BA4A09FB46A6`.

`tools/audit-cp-psp-preflight-evidence.ps1` reproduz a classificação offline.
O resultado fornece um baseline limpo para projetar a sequência PSP; ele não
autoriza `SETUP_TMR` nem `LOAD_IP_FW`.

A implementação antiga de `BcPspSetupTmr` foi então auditada e reprovada: ela
assumia tamanho/offset fixos dentro de uma VRAM presumida e derivava PA de CPU
de `GCMC_VM_FB_OFFSET`, sem alocador ou prova de posse exclusiva. A captura
Linux, por sua vez, reservou a TMR em `0xF41F800000`, demonstrando que o endereço
histórico não é invariante.

Defesa em profundidade adicionada, sem instalação:

- kernel retorna `STATUS_NOT_SUPPORTED`, sem submit PSP ou acesso MMIO;
- biblioteca retorna `ERROR_NOT_SUPPORTED`, sem IOCTL;
- `bc250ctl psp tmr` e o `gfx bringup` legado recusam antes de `arm`/escrita;
- `LOAD_IP_FW` dos tipos CP 1..5 também é recusado antes de staging/IOCTL;
  RLC e SDMA não foram alterados por essa guarda;
- `tools/audit-psp-tmr-quarantine.ps1` verifica essas três fronteiras e passa.

Build ABI 29 compilado e assinado, mas **não instalado**:

- `output/bc250dbg.sys`:
  `3C647C4770A3210FA67073491B1D299541EAA3DF0F2BF689A21B5E3DE1BA080B`.

Os executáveis user-mode com PDB incorporam identidade de cada build e seus
hashes mudam ao recompilar. Não usar um hash antigo deles como identidade do
workspace; os testes portáteis abaixo são construídos separadamente com
`/Brepro`.

Reabilitar TMR exige que uma única reserva dinâmica e exclusiva forneça tamanho,
PA de CPU e endereço MC/GPU coerentes. Até lá, tentar reproduzir `SETUP_TMR` no
silício seria escrever por suposição.

**NÃO VALIDADO EM HARDWARE.**

---

## Recursos PCI Windows readonly: janela CPU de 256 MiB confirmada

Captura de 2026-08-24 18:44:
`docs/evidence/2026-08-24_18-44-30-DESKTOP-HQAG6T4-pci-resources`.

O novo `tools/capture-bc250-pci-resources.ps1` consultou exclusivamente
`Get-PnpDevice` e as associações CIM `Win32_PNPAllocatedResource`. Nenhum
driver IOCTL, acesso MMIO ou escrita em configuração PCI foi executado.

O provedor publicou um descritor agregado e um filho contido. A decomposição
somente pelas fronteiras CIM produziu três segmentos físicos disjuntos:

```text
0xC0000000..0xCFFFFFFF  0x10000000  256 MiB
0xD0000000..0xD01FFFFF  0x00200000    2 MiB
0xFE800000..0xFE87FFFF  0x00080000  512 KiB
```

A maior janela CPU atribuída é, portanto, 256 MiB. A captura Linux mede 512 MiB
de VRAM e TMR no offset `0x1F800000`, com fim em `0x1FBFFFFF`. Nenhuma janela
Windows atribuída cobre a VRAM inteira nem alcança esse fim de TMR.

Isto valida o tamanho da abertura CPU desta sessão; **não** concede ownership
de seus bytes ao `bc250dbg`, não associa por suposição cada segmento a um número
de BAR e não autoriza mapear além do recurso.

Integridade:

- SHA-256 de `result.json`:
  `18B7B68A0FF4E8B3F0CE5518A502E40695D70D314916B366B2F048BF0C26B321`;
- SHA-256 de `result.txt`:
  `924C2C1CC8633FB5B948968E282339DE63275A93F0FF67AFA4BC48FE46C361C8`.

`tools/audit-bc250-pci-resources.ps1` reproduz os gates offline e passa.

O contrato foi convertido em código portátil em
`driver/core/bc250_resource_contract.{h,c}`. Ele recebe BARs identificados por
índice e recursos traduzidos normalizados, sem consultar hardware, e resolve o
comprimento efetivo pela interseção dos descritores e pela base do próximo BAR.
Ele recusa overflow, duplicidade, ausência de recurso, saída insuficiente e
descritores exatos conflitantes.

`driver/tests/resource_contract_test.c` cobre o layout capturado, ordem
arbitrária e todos esses casos fail-closed. O teste é compilado com `/W4 /WX`,
executado automaticamente por `studio/build.bat` e passou repetidamente. Seu
binário foi reproduzível nas passagens realizadas:
SHA-256 `42B21726B4C08D13CEAD53678DAF31F8B71563F22FA681D457A69EDDF7A6087A`.

A adaptação WDK existe em `driver/wdk/bc250_cm_resource_adapter.{h,c}`. Ela
percorre por cursor todas as `CM_FULL_RESOURCE_DESCRIPTOR` de tamanho variável,
coleta `CmResourceTypeMemory`, decodifica `CmResourceTypeMemoryLarge` 40/48/64 e
delega a resolução ao contrato portátil. Contagens excessivas, endereço
negativo, range inválido, saída curta e `CmResourceTypeDeviceSpecific` são
recusados. O último é deliberado: seus bytes extras mudam o layout e não são
necessários para identificar BARs. O adaptador e o contrato compilaram como
objetos kernel com WDK, `/W4 /WX /Brepro`. Um executável host construído com os
tipos reais `CM_*` do WDK percorreu duas full lists, `MemoryLarge`, o layout
agregado/filho capturado e os negativos fail-closed. Não houve driver, instalação
ou hardware. Hashes reproduzidos em duas passagens:

- contrato kernel: `27CA8F07554698691B471DAF4303D52D081713B173053F8281523E258F928527`;
- adaptador WDK: `EBC9F1817E66FD07F0FB03B97E59CE965D89384EDC717ED4BEA0A59CBCB8604E`.
- teste host WDK: `14FE6A7E67A38F8F6B0DAECBC572FD5C4F4F701E5E6A0E0939DAA626068C247D`.

Isso prova compilação, percurso e determinismo offline, não que uma lista tenha
sido entregue por Dxgkrnl nem que algum BAR tenha sido mapeado.

O segundo núcleo portátil é `driver/core/bc250_vram_allocator.{h,c}`. Ele
administra somente offsets e comprimentos dentro de `TotalBytes`, mantendo
`CpuVisibleBytes` como limite separado. Reservas fixas e alocações top-down ou
bottom-up compartilham a mesma tabela de ownership. A API recusa:

- alinhamento que não é potência de dois e offset desalinhado;
- overflow, fim fora da VRAM e região CPU-visible além do BAR;
- qualquer sobreposição;
- tabela cheia, token antigo e liberação de reserva persistente;
- role/flag desconhecida.

O teste baseado na topologia capturada usa 512 MiB totais e 256 MiB
CPU-visíveis. Com os 4 MiB superiores previamente marcados como reserva de
plataforma, uma alocação TMR top-down de 4 MiB resulta em offset `0x1F800000`;
um buffer CPU-visible equivalente fica em `0x0FB00000`, antes da page table
reservada. Isso valida o planejador de intervalos, não a origem da reserva.

`driver/tests/vram_allocator_test.c` passa com `/W4 /WX` e hash reproduzível
nas duas passagens:
`EAB267E2BA6559EFF1717B8FB8CD6A15F3CF22A4DA5C27DE0A96C46FC834BB26`.
O mesmo núcleo também compila como objeto WDK reproduzível:
`8FFCA50A53BBF24609A413B4E73C700583216A4117FF38E0F9070B131FD6DF67`.
PA de CPU e MC/GPUVA não existem nessa API por desenho.

O vínculo foi implementado separadamente em
`driver/core/bc250_address_contract.{h,c}`. Ele exige um token vivo e um
snapshot runtime completo (`TotalBytes`, janela CPU, `vram_start/end` e
`vram_base_offset`), confere que a topologia descreve exatamente o mesmo
alocador e deriva da mesma região:

```
MC             = vram_start + offset
system_phy_addr = vram_base_offset + offset
```

O contrato não transforma a base física do BAR em `system_phy_addr`. A janela
CPU é usada apenas para dizer se a região pode ser preenchida pelo host. No
cenário sintético que reproduz a captura, a TMR em `0x1F800000` gera MC
`0xF41F800000`, PA `0x46F800000` e é corretamente marcada como fora da janela
CPU de 256 MiB. O teste reproduzível tem SHA-256
`4EEE88E8D4403E255E1ADEBAE7161B97971B051E3CFDC668AB0208EEEC3F717A`;
o objeto WDK tem
`18448DA69D745E569021F82CD4190C53FA34E2C9B70C2B8986FA261C81F94FB6`.

`driver/core/bc250_psp_contract.{h,c}` constrói os seis dwords de `SETUP_TMR`
e os quatro dwords de `LOAD_IP_FW` somente a partir de token + snapshot. Não há
API pública que aceite MC/PA prontos. Staging de firmware exige role firmware,
região integralmente CPU-visible, tamanho em bytes e tipo CP 1..5; MEC e
MEC_JT1 continuam separados. O teste reproduzível tem SHA-256
`F48B7E6AF1231E9374135B908415065FE9A8FA372C326B1BAC8E7559BAD0FCB2`;
o objeto WDK tem
`0B84D6FB846696B2EA90E67FFC2786BF44F7A54F985B5E2D7D57BFD6B141054D`.

Auditoria de unidade: o campo PSP `fw_size` é bytes (`ucode->ucode_size` no
upstream). Embora a ABI legacy receba `DwordCount`, `dbg/psp.c` já calcula
`fwBytes = Count * sizeof(ULONG)` antes de preencher o comando. Portanto a
unidade histórica estava correta e não explica o `0x80000203`; o contrato novo
também serializa bytes explicitamente.

Esses payloads não foram ligados ao ring PSP. Não houve submit nem escrita.

**NÃO VALIDADO EM HARDWARE.**

### Bootstrap CP offline, atômico e sem transporte

`driver/core/bc250_compute_bootstrap.{h,c}` agora compõe os contratos anteriores
num lifecycle fail-closed: inventário aberto para reservas herdadas, selo com
snapshot runtime, alocação persistente de TMR, staging CPU-visible e manifesto
CP completo. A sequência aceita é `PFP, ME, CE, MEC, MEC_JT1`; os cinco
tamanhos são bytes vindos do manifesto, e o staging tem o tamanho do maior.

O preflight é construído sobre uma cópia do alocador e só é confirmado quando
todos os seis payloads foram aceitos. Assim, falta de espaço e manifesto
inválido não deixam reserva persistente parcial. Alteração do inventário após o
selo, ausência de evidência runtime, entrada duplicada/fora de ordem e fusão de
MEC com JT falham antes de qualquer transporte. A camada não oferece submit.

Teste host reproduzível:
`A917F7DFF1A84102A3DFE7C94984C5F8F2C500E2A8C583F198ADAF5D671EFACC`.
Objeto WDK reproduzível:
`FD8E3B2A72FBD77D90E95F30FA34221BEF786B234698BE8A12A326F30B45F017`.

Isso aumenta a cobertura do desenho e dos testes offline, não a prova de
Compute. Nenhum comando foi enviado ao PSP e nenhuma engine executou.

**NÃO VALIDADO EM HARDWARE.**

### Fronteira de ownership do adapter

`driver/core/bc250_owned_adapter_contract.{h,c}` impede que um BAR traduzido
seja confundido com ownership da VRAM. Para construir um bootstrap selado, o
chamador precisa apresentar separadamente: recursos PnP traduzidos, identidade
PCI do BAR, total de VRAM runtime, faixa MC runtime, `vram_base_offset` runtime,
ownership exclusivo do adapter e inventário completo de reservas herdadas.

O teste usa o layout sintético equivalente à captura: BAR0 CPU-visible de
256 MiB e VRAM total de 512 MiB. O contrato preserva essa diferença, insere as
reservas persistentes e produz a TMR esperada em `0x1F800000`. Casos sem
ownership exclusivo, com faixa MC divergente, BAR0 duplicado e reserva herdada
não persistente são recusados sem publicar estado parcial.

Os flags de evidência não criam ownership. Eles são pré-condições que o futuro
miniport deverá satisfazer a partir do seu lifecycle real. O laboratório legacy
atual não satisfaz ownership exclusivo nem inventário completo.

Teste host reproduzível:
`6DE6F6205944EECBACC0C54938A270158B8452846D2C51FB24A2EC74B82D6E17`.
Objeto WDK reproduzível:
`38F8780B96042977B931123D4F480EECF33F6BDF020F7BF1891618E14591128A`.

Zero mapeamento, zero submit e zero acesso ao hardware.

**NÃO VALIDADO EM HARDWARE.**

### Adaptador WDK de StartDevice, ainda sem DDI

`driver/wdk/bc250_start_device_adapter.{h,c}` recebe uma
`CM_RESOURCE_LIST`, BARs identificados por PCI, topologia runtime e inventário.
Somente depois de a resolução CM passar ele acrescenta as evidências de recurso
traduzido e identidade do BAR; ownership exclusivo, inventário completo, total
de VRAM, faixa MC e offset físico continuam obrigações externas.

O teste WDK executa `Initialize -> PrepareOwnership -> BuildReadonlyPreflight ->
Teardown`, reprova um segundo start e comprova que um manifesto incompleto
permite retry sem perder o ownership selado. O teardown devolve o contexto a
zero. Não houve mapeamento nem chamada a Dxgkrnl.

Objeto WDK reproduzível:
`6B116C9B477D6D8EF81F6C3DB7488D3FB8060F7A3675803886E347A5C1DAEF94`.
Teste reproduzível:
`999454280A9E170966BD665B583F95F527E216F26F161120342C78EBF270BE75`.

**NÃO VALIDADO EM HARDWARE.**

### DDI Dxgkrnl readonly e fail-closed

`driver/wdk/bc250_dxgk_readonly_ddi.{h,c}` implementa as assinaturas reais
`PDXGKDDI_START_DEVICE` e `PDXGKDDI_STOP_DEVICE`. StartDevice valida o tamanho
da interface, salva apenas os bytes fornecidos, chama
`DxgkCbGetDeviceInformation` e entrega `TranslatedResourceList` à cadeia de
contratos já testada.

Sem produtor runtime de exclusividade/inventario, retorna
`STATUS_NOT_SUPPORTED` em `RUNTIME_MISSING`, antes de ownership. Com callbacks
e evidencia sinteticos, StartDevice resolve novamente os BARs PnP, mapeia o BAR
MMIO identificado, captura RCC/GCMC em duas passagens, desmonta o mapeamento e
cruza a topologia derivada com o inventario injetado. O input staged não contém
mais total, faixa MC ou `FB_OFFSET` manuais. Mesmo chegando a `PreflightReady`
nos testes offline, retorna
deliberadamente `STATUS_NOT_SUPPORTED`, com zero sources e zero children.

O teste usou uma `DXGKRNL_INTERFACE` inteiramente falsa. Além do caminho
completo, força falha de unmap durante StartDevice: a DDI aborta e preserva o
mapeamento; StopDevice repete o unmap e só então limpa o contexto. Nada foi
registrado no Dxgkrnl ou instalado.

Objeto WDK reproduzível:
`B0CD15853BAE83317B2A985FC19558AF187F4954BDC7B4B043F62C1DEDE75F9D`.
Teste WDK reproduzível:
`FC0AA0270A08431556F9558AE543E8ED5B29918888E27AF13532BF2DF9F4E6FA`.

### Descoberta PCI readonly pelo callback Dxgkrnl

`driver/wdk/bc250_pci_bar_reader.{h,c}` remove as bases PCI da entrada manual
da DDI. Ele lê exatamente o header comum por `DxgkCbReadDeviceSpace`, valida
vendor/device `1002:13FE` e decodifica BARs de memória 32/64 bits. Nenhum sizing
write é executado; os tamanhos continuam pertencendo à `CM_RESOURCE_LIST`.

O teste cobre BAR0 64-bit, BAR2/BAR5 32-bit, retorno parcial, device errado,
BAR de I/O e par 64-bit truncado. Toda falha apaga resultados parciais.

Objeto WDK:
`3D6BD9FDB0E995ABA408C701C91F0126AF3F3262D53FACC83340117991AFAC63`.
Teste WDK:
`E2127E3A8CB11D3CA7D587D6C0C132438485B624B6528736F2CC22AC0990A1B4`.

### ReBAR: suportado, selecionado e atribuído são fatos distintos

`driver/wdk/bc250_rebar_reader.{h,c}` percorre extended capabilities até o ID
`0x0015`, com limite, alinhamento e detecção de loop. Para o BAR alvo, extrai a
máscara de tamanhos suportados e o tamanho selecionado, e compara este último
com o comprimento PnP já resolvido.

O cenário sintético prova seleção de 256 MiB, suporte a 256/512 MiB e recurso
atribuído de 256 MiB. Isso não autoriza anunciar 512 MiB de VRAM: suporte é
capacidade da abertura, não memória presente. Mismatch, seleção fora da máscara
e cadeia cíclica reprovam.

Objeto WDK:
`FF5C9CA99F40793AF2686992E027DEF32E13BAA2CA393BDF77A5368774F5EBE5`.
Teste WDK:
`5793567BE34FDE2C21C12ED58F7C6F961DDCBFF45CF0F6B629358CA94A0EDCD3`.

### Marco historico: imagem de miniport ainda nao registravel

`driver/wdk/bc250_readonly_miniport.{h,c}` implementa DriverEntry, AddDevice e
RemoveDevice, e preenche uma `DRIVER_INITIALIZATION_DATA` contendo apenas
Add/Start/Stop/Remove. As entradas de submit, interrupt, query-adapter, paging,
power e reset ficam nulas.

Naquele marco, DriverEntry não chamava `DxgkInitialize`: retornava
`STATUS_NOT_SUPPORTED`. Ainda assim, o conjunto foi ligado como PE Native
`bc250readonly.sys`, permitindo conferir ABI, imports e entry point sem criar
um driver instalável. Add/Remove foram executados em teste com pool falso.

A import table do `.sys` contém exclusivamente `ExAllocatePool2` e
`ExFreePoolWithTag`; map/unmap e PCI são callbacks indiretos do Dxgkrnl. Não há
DispLib, `MmMapIoSpace`, write callback ou escrita MMIO. DriverEntry ainda
impedia que essa superficie fosse registrada ou chamada pelo Windows.

Objeto WDK:
`322B2A2CDAC692E89C2EA054B2EBB6F13E21FEB62FBA371FD37C9446FD75366F`.
Teste WDK:
`F9342C34D4D060270FE44A17FFAE51DFDFB9AFE3F29F1068AC70BAC57EE88CF4`.
Imagem:
`E58E6785F67229C08C6DBD8F1133D024392B32B73643E2586AC7AC7151C8A5C6`.

RemoveDevice agora propaga falha de StopDevice: se o unmap falhar, o contexto
e o pool permanecem vivos. O teste força essa condição e só permite liberar o
pool depois que um segundo Remove conclui o unmap.

Naquele marco nenhum INF havia sido criado e nada foi instalado. Esta secao e
historica: as versoes MCDM posteriores passaram a chamar `DxgkInitialize`,
ganharam INF assinado e chegaram fisicamente ao StartDevice. O estado atual e o
binding 0.5.0.0 documentado no topo, ainda fail-closed.

**NÃO VALIDADO EM HARDWARE.**

---

## Evidência Linux: topologia máxima 40, ativos 24

Captura somente leitura:
`referencias/linux-gpu-info-fedora-2026-08-24_14-14-07`.

O amdgpu no kernel Fedora 6.19.10 registrou para a BC-250 `1002:13FE`:

```text
SE 2, SH per SE 2, CU per SH 10, active_cu_number 24
```

Portanto:

- topologia máxima: 2 SE, 2 SH por SE e 10 CU por SH, total 40;
- unidades ativas: 24;
- 40 não pode ser anunciado como quantidade utilizável;
- IP Discovery estava presente e confirmou GC 10.1.3 nas bases
  `0x1260/0xA000/0x02402C00`;
- `num_instance=0` da IP Discovery é ordinal do bloco GC, não cardinalidade do
  seletor interno `GRBM_GFX_INDEX.INSTANCE_INDEX`.

A captura fecha SE/SH, mas não fecha snapshot/restauração por banco e não valida
o caminho Windows.

**NÃO VALIDADO EM HARDWARE.**

---

## ABI 29: fontes de topologia e limite do GRBM

Continuação estática, sem instalar o driver ABI 29 e sem acesso MMIO:

- com IP Discovery presente, o kernel usa a tabela GC e pula o `gpu_info`;
- sem Discovery, o nome pedido é `cyan_skillfish_gpu_info.bin`, não
  `cyan_skillfish2_gpu_info.bin`;
- o payload v1.0 fornece `gc_num_se`, `gc_num_cu_per_sh` e
  `gc_num_sh_per_se`, usados pelo upstream para configurar os limites GFX;
- `tools/fwinspect.py` passou a validar e decodificar esse formato offline;
- o kernel registra no `dmesg` as dimensões efetivamente adotadas; ainda não há
  captura dessa linha nesta árvore;
- essas fontes não declaram diretamente a quantidade do eixo `INSTANCE_INDEX` de
  `GRBM_GFX_INDEX`.

Consequência: capturar a topologia Linux é necessário, mas não suficiente para
um rollback verdadeiro das escritas golden em broadcast. A quarentena continua.

**NÃO VALIDADO EM HARDWARE.**
