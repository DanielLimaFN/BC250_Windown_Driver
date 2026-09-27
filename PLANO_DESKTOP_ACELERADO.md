# PLANO — DESKTOP ACELERADO E ESTÁVEL

> **Escopo desta etapa:** levar o Windows 10 22H2 a compor o desktop (DWM + Explorer) **pela GPU
> da BC-250**, no modo fixo herdado do UEFI GOP, de forma estável e sobrevivendo a TDR.
> **Fora de escopo deste documento:** jogos, Steam, D3D12, e DCN (mode set, refresh, hotplug,
> VRR, HDR). Esses entram em planos próprios **depois** que a fase D5 fechar.
>
> **Data:** 2026-09-17 20:05 · revisado 2026-09-18 · **Target:** AMD BC-250 (`1002:13FE`, GC 10.1.3) | Windows 10 22H2 x64
> **Governança:** subordinado a [`AGENTS.md`](AGENTS.md), [`GEMINI.md`](GEMINI.md) e
> [`DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md`](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md).
> **Status (revisado 2026-09-21, após a Corrida 58):** Em execução ativa.
> O que fechou até aqui são principalmente fases de **emissão e diagnóstico**; o que falta é
> o **efeito gráfico**. Ver §2.0.
>
> **O bloqueador observado mais recente tem endereço `0x2469A6000`.** Na Corrida 58, três
> TDRs repetiram uma escrita (`Cid=8`, `Rw=1`) nessa GPUVA. No terceiro, a janela de
> publicação ficou inalterada e a caminhada pela raiz do VMID 2 encontrou folha válida com
> R/W/X. A imagem CPU-visível estava completa; ainda falta provar o que a GPU efetivamente
> observou e discriminar visibilidade/invalidação, hierarquia ou uso da alocação.
>
> A Corrida 58 resolveu a pergunta deixada pela 57: em 25/25 mapeamentos com geometria da
> arena de 256 KiB, `0x200040000` saiu `NoExecute=0` e com o bit executável na PTE. Portanto,
> falta de permissão de execução nesse alvo foi refutada como causa suficiente. A classe do
> token RADV ainda saiu desconhecida, e DX.5 continua aberto porque o produto ainda trava.
>
> **Próxima faixa offline:** o FlushTlb do VidMm cobre hoje apenas 8/7/9 e não os VMIDs
> dinâmicos READY do pool usados pelo GFX. Corrigir essa cobertura é o próximo trabalho; a
> relação causal continua hipótese até uma corrida com máscara, ACK e carga equivalente.
> Passagem autocontida e VIGENTE: `docs/324_PASSAGEM_DE_BASTAO_2026-09-22_CORRIDAS_59_A_63.md` (o docs/317 está desatualizado: a faixa que ele propunha foi medida na Corrida 59 e refutada como causa).
>
> **DX.10 e DX.11 continuam fechados como implementação offline, agora com validação física
> parcial.** A Corrida 58 provou a seleção `VIVA_READY` da raiz e mediu permissões/PTE do alvo;
> não exercitou `RETIDA_POS_MORTE`, e o token da arena permaneceu sem classe semântica. A
> última raiz instalada continua retida por VMID fora da entrada reciclável, e o log distingue
> `VIVA_READY`, `RETIDA_POS_MORTE` e `AUSENTE`. `0x200040 << 12` ganha veredito dedicado,
> enquanto `<<20` é recusado por exceder 34 bits.
>
> (bloco anterior, mantido como registro:) Em execução ativa.
> **A Fase D0 deixou de ser o bloqueador.** O `WPTR_READBACK_DIVERGENTE` que a criou saiu
> **zero vezes** na Corrida 46, e o anel gráfico passou a **consumir**: 89 vereditos com
> `RPTR_Reg == WPTR` e **105 de 105** fences escritas **pela GPU** sobre poison. **D0.5 e D0.6
> estão FECHADOS NO SILÍCIO**; **D0.7 continua aberto** (perna 3, byte a byte, nunca executou).
>
> **O bloqueador agora é outro, e tem nome: falha de tradução sob VMID 7.** Medido nas Corridas
> 45 e 46, em candidatos diferentes, com a mesma assinatura:
> `GCVM_L2_PROTECTION_FAULT_STATUS = 0x0070153C` → **`VMID=7`** (o mesmo VMID de todas as
> submissões gráficas), **`WALKER_ERROR=6`**, `MAPPING_ERROR=1`, em **`GpuVa=0x2431000`** e
> `0x245B000`. No mesmo TDR, a onda lida pela sonda trouxe `SQ_WAVE_STATUS FATAL_HALT=1` com
> `SQ_WAVE_TRAPSTS XNACK_ERROR=1`. Dois registradores independentes contam a mesma história: o
> caminhador de tabela de páginas falha, a onda recebe XNACK e morre, e o CP espera para sempre
> por quem nunca vai retirar. **Enquanto isso não fechar, D1.6–D1.8 seguem inalcançáveis** — e
> agora pelo motivo certo, não mais por falta de consumo do anel.
>
> **Estabilidade: três corridas seguidas (44, 45, 46) com ZERO BugCheck**, recuperação de TDR
> 5/5 e rollback limpo para BasicDisplay. `MEMORY.DMP` congelado no da Corrida 43.
>
> ⚠️ **Este bloco corrige o status anterior**, que dizia "o anel gráfico nunca executou uma
> submissão em todo o projeto". Isso era verdade em 18/09 e **deixou de ser** em 20/09.
>
> ⚠️ **Numeração de corridas:** três esquemas incompatíveis em uso. Este documento dizia "Corrida 18
> (próxima)"; a `BASE_DE_AVANCO.md` para na 18 de 17/09; uma das análises chama a corrida de 18/09
> de 27. **Identificar corrida por data/hora + hash do SYS + pasta de evidência, nunca por número.**

---

## 0. Por que o desktop vem antes dos jogos

O DWM é o **melhor primeiro cliente real que existe**: é um único aplicativo D3D11, sempre o
mesmo, com um conjunto **finito e determinístico** de shaders a cada boot. Dá para enumerar cada
shader que ele compila e fazer cada um funcionar. "Qualquer jogo da Steam" é um conjunto
infinito. Fechar o DWM é um problema **limitado**; fechar jogos não é.

Consequência que muda a ordem do trabalho: **o compilador de shader é pré-requisito do desktop,
não dos jogos.** O DWM exercita o tradutor DXBC→SPIR-V no primeiro quadro.

---

## 1. A regra de conclusão — vale para TODAS as tarefas

Uma tarefa só é marcada ✅ quando as **cinco** perguntas abaixo têm resposta medida e arquivada.
Qualquer uma sem resposta mantém a tarefa 🟡, por mais que o código esteja pronto.

| # | Pergunta | Como responder | Se falhar |
|---|---|---|---|
| **R1** | O efeito foi produzido pela **GPU**? | Lei 2 — tripé completo: `RPTR == WPTR`, fence física gravada pela GPU **sobre poison**, e bytes do destino conferidos contra gabarito | Não fecha. "Executou" sem tripé é operação contada, não efeito provado |
| **R2** | Algum caminho de **CPU** produziu o resultado? | `HostCpuCopies == 0`, `FallbackCpuCount == 0`, portão `no_host_copy_fallback` PASS, `SubstitutedDrawCount == 0` | Fecha como **falso sucesso**. Ver §5 |
| **R3** | O resultado é **distinguível de zero**? | `COMPOSITE_DATA_PROOF` com `NonZeroSamples=5/5` — **nunca** `ZERO_INCONCLUSIVO`; `BYTE_PROOF PASS`, nunca `INCONCLUSIVO` | Zero contra zero não prova pixel |
| **R4** | O **binário testado** é o binário novo? | SHA-256 do SYS e da UMD mudou no manifesto, `DriverVer` fonte **subiu**, e as strings novas foram conferidas no binário **assinado** | O empacotador **copia, não compila**. Sem isso você testou o candidato anterior |
| **R5** | O **teste** poderia falhar? | Mutação verificada: quebrar o comportamento **reprova** a suíte e o portão | Portão que não reprova nada é carimbo, não prova |

**Coluna "Fecha como" nas tabelas abaixo:**

| Marca | Significado | Custo |
|---|---|---|
| 🔧 **OFFLINE** | Fecha por código + suíte de contrato + portão + mutação verificada. **Sem hardware.** | Barato — pode fechar várias por dia |
| 🔬 **CORRIDA N** | Só fecha lendo o log de uma corrida física. Cada corrida fecha **um lote** de tarefas | Caro — 5 corridas no plano inteiro |

---

## 2. Fases e tarefas

### 2.0 Placar por fase (21/09, após a Corrida 58)

**Placar do catálogo:** 22 itens fechados de 51 registrados (**43%**), mais 3 itens que
deixaram de pertencer ao caminho vigente (D0.3/D0.4 obsoletos e DX.3 retirado).

**Placar do trabalho ativo:** excluindo esses três itens mortos do denominador, são **23 de
48 fechados (48%)**, com 25 abertos. Esta é a porcentagem útil para planejamento. O placar
antigo de 14/45 não regrediu nem estava aritmeticamente errado: ele antecede a expansão da
fase DX de 5 para 12 itens. O escopo cresceu em sete itens e nove entregas fecharam desde
então (D0.2 e DX.4/DX.6–DX.17).

| Fase | O que é | Fechado | Aberto ativo | Fora do caminho | Estado |
|---|---|---:|---:|---:|---|
| **D0** | anel gráfico consome | 4 | 1 (D0.7) | 2 obsoletos | ✅ quase pronto |
| **DX** | tradução nos VMIDs de contexto | 10 | 1 (DX.5) | 1 retirado | ⛔ **bloqueador atual** |
| **D1** | primeiro pixel não-nulo | 5 | 3 | 0 | 🟡 bloqueada por DX |
| **D2** | compilador de shader | 2 | 4 | 0 | 🟡 dependência externa permanece |
| **D3** | cobertura D3D11 do DWM | 0 | 8 | 0 | ⬜ **maior bloco** |
| **D4** | desktop acelerado | 0 | 6 | 0 | ⬜ não iniciado |
| **D5** | desktop estável | 2 | 3 | 0 | 🟡 backend de recuperação existe |

**Corrente de bloqueio.** Cada elo só abre depois do anterior:

```
DX.5 (Cid=10 = perda de 8 bits CONFIRMADA; Cid=9 = PDE sujo que nao escrevemos)
 └→ D0.7 (IB executa, byte a byte)
     └→ D1.6-1.8 (primeiro pixel)
         └→ D2.5-2.6 (shaders do DWM)
             └→ D3.1-3.8 (DWM sem recusa)   <-- maior bloco do plano
                 └→ D4 (desktop acelerado)
                     └→ D5 (estável sob TDR)
```

**Aviso que precisa ficar escrito:** a Fase **D3 é o verdadeiro volume de trabalho e ninguém
tocou nela**. Oito itens — inventário de DDIs do DWM real, formatos e pitch, estado
(blend/rasterizer/depth/samplers), SRVs e descriptor sets. Hoje
`DefaultDescriptorSetLayout = VK_NULL_HANDLE` e não há draw nativo na última corrida. Mesmo com
o VMID consertado e o primeiro pixel aceso, é D3 que separa "um triângulo" de "o DWM compondo",
e ela é maior que D0 e DX somadas.

---

### Fase D0 — O anel gráfico consome uma submissão  ✅ **4 fechados, 1 aberto, 2 obsoletos**

> **Por que esta fase existe (criada em 18/09, depois da corrida `14:20:07`):** o plano media a
> **emissão** de comandos e não o **consumo**. D1.3, D1.4, D1.5 e D2.4 fecharam no silício — PSO
> criado, vertex buffer subido, 379 draws Vulkan nativos — e **zero** pixel foi produzido, porque
> todos esses passos acontecem **antes** de o comando chegar à GPU. Entre "a RADV montou o IB" e
> "o clear produziu cor" não havia tarefa nenhuma. Este é o degrau que faltava, e ele é
> **anterior** a D1.6.
>
> **A medição que obrigou a criar a fase (P0, corrida `2026-09-18_14-20-07`):** 4 de 4 submissões
> deram `[GfxEngine] SUBMIT RECUSA: WPTR_READBACK_DIVERGENTE -- escrevemos 12 e o registrador
> devolveu 0`, com o transporte MAPEADO, `Write32` devolvendo 1 e a leitura de volta funcionando.
> O registrador `CP_RB0_WPTR` (`0x0C150`) **não retém** a escrita MMIO. No mesmo log, o SDMA0 fez
> 313 submissões com `WPTR=1476 RPTR_Reg=1476 RPTR_Wb=1476` — logo o transporte, o allowlist e a
> função de escrita estão descartados como causa.
>
> **Consequência retroativa que precisa ficar escrita:** as *"45 submissões aposentadas no
> `CP_RB0`"* da Corrida 15, as *"20 submissões 3D"* da Corrida 16 e as *"16 submissões gráficas"*
> da Corrida 13 foram **aceitas, não executadas**.
>
> ✅ **RESOLVIDO EM 20/09 (Corridas 44-46).** A frase original terminava com "nenhuma submissão
> gráfica jamais foi provada executada neste anel" — isso **deixou de valer**. Na Corrida 46 o
> `WPTR_READBACK_DIVERGENTE` saiu **zero vezes**, houve **89** vereditos com `RPTR_Reg == WPTR` e
> **105 de 105** fences escritas pela GPU sobre poison. O anel **busca e consome**. O que ainda
> NÃO está provado é a execução do conteúdo do IB (D0.7, perna 3), e o log declara isso em toda
> linha: `TRIPE INCOMPLETO, 2 de 3 pernas`.

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D0.1** | Inventário dos canais de publicação de WPTR do anel gráfico, com proveniência | ✅ **FECHADO (OFFLINE, 18/09)** | Quatro canais identificados por leitura de código e header: `CP_RB_WPTR_POLL_ADDR_LO/HI` (`0x0C3AC/0C3B0`) **programados** em `bc250_gfx_ring_transaction.c:166`; `CP_RB_WPTR_POLL_CNTL` (`0x08708`) **só lido**, e sem bit de enable no header (`POLL_FREQUENCY[15:0]`, `IDLE_POLL_COUNT[31:16]`); `CP_RB_DOORBELL_CONTROL` (`0x0C3B4`) escrito **`0`** em `:150`; MMIO `CP_RB0_WPTR` (`0x0C150`), o único aberto. | Fechamos dois canais e deixamos aberto justamente o que o silício não retém. O inventário é P1; qual canal resolve é **H** |
| **D0.2** | O veredito de tripé é emitido **mesmo quando a submissão é recusada** | ✅ **FECHADO OFFLINE** | `GFX_TRIPE_VEREDITO_RECUSA` mede RPTR/WPTR/fence no caminho de recusa; `audit_phase_d0_refusal_tripod_and_wptr_veto` exige a chamada no helper e mantém a divergência de WPTR como veto | Corrige a dívida da Corrida 42, em que 4/4 recusas produziram zero vereditos. Ainda não transforma recusa em execução nem fecha D0.7 |
| **D0.3** | Divergência de MMIO deixa de ser veto **quando outro canal publicou com prova** | ⬛ **OBSOLETO (20/09)** | Enquanto o MMIO é o único canal, divergência **continua** recusando (fail-closed preservado). Com um segundo canal publicado e conferido, a divergência passa a ser **fato registrado** (`WPTR_CANAL_MMIO_NAO_RETEM`) e a submissão segue | Sem isto o canal novo **não pode ser testado**: a submissão morre antes de o CP ter chance de buscar. Afrouxar sem o segundo canal provado seria voltar ao falso sucesso |
| **D0.4** | `CP_RB_WPTR_POLL_CNTL` programado com valor de proveniência declarada e readback | ⬛ **OBSOLETO (20/09)** | Valor derivado dos campos do header (P1), escrito na transação com snapshot/readback/rollback, e o valor do SDMA (`0x00403004`) **proibido** por `Bc250GfxRingPollCntlIsBorrowed` | Este registrador **não tem enable** — programá-lo configura frequência, não liga o mecanismo. Declarar isso no log, senão a ausência de efeito será lida como refutação do canal errado |
| **D0.5** | **O `RPTR` do anel gráfico avança** | ✅ **FECHADO NO SILÍCIO (Corridas 44-46)** | Corrida 46: **89** vereditos `Perna1=REGISTRADOR` com `RPTR_Reg == WPTR` em valores reais (72, 80, 96...), e **zero** `WPTR_READBACK_DIVERGENTE`. 157 submissões aceitas, `Submetidas=160 Aposentadas=160 EmVoo=0` | `RPTR` avançar prova que o CP **buscou**. Não prova que executou o IB — o `RELEASE_MEM` que escreve a fence é emitido pelo KMD **no anel**, então fence retirada prova consumo do anel, não execução do IB (Diretriz Universal nº 3) |
| **D0.6** | A fence do anel gráfico sai do poison | ✅ **FECHADO NO SILÍCIO (Corridas 44-46)** | **105 de 105** `Perna2=FENCE_ESCRITA_PELA_GPU` na Corrida 46, com o poison `0x43DAFFFE` excluído explicitamente e os estados `POISON_INTACTO` e `SLOT_ZERADO` distinguidos | `POISON_INTACTO_GPU_NAO_ESCREVEU` é o veredito honesto enquanto não sair. Fence grande passa comparação numérica — o poison tem de ser excluído explicitamente |
| **D0.7** | O IB é executado, não apenas buscado | 🔬 **ABERTO — único item restante da D0** | Efeito declarado do IB conferido **byte a byte** no destino, sobre canários não-nulos plantados antes do disparo. **Estado atual: 105 de 105 `Perna3=NAO_EXECUTADA_SEM_GABARITO_DE_DESTINO`** — o tripé GFX está em **2 de 3 pernas**, e o log declara isso | Zero sobre zero é inconclusivo. Esta é a Perna 3, e é ela que separa "o CP consumiu o anel" de "o shader rodou" |

---

### Fase DX — A tradução de memória nos VMIDs de contexto  ⛔ **BLOQUEADOR ATUAL, substitui a D0**

> **Criada em 20/09, depois das Corridas 45 e 46.** A Fase D0 foi criada porque o anel não
> consumia; isso foi resolvido. O que impede o pixel agora é **anterior ao pixel e posterior ao
> anel**: o shader morre.
>
> **Medição (P0, reproduzida em DOIS candidatos diferentes):**
> `GCVM_L2_PROTECTION_FAULT_STATUS = 0x0070153C` nos cinco TDRs →
> **`VMID=7`**, **`WALKER_ERROR=6`**, `MAPPING_ERROR=1`, `PERMISSION_FAULTS=3`, `CID=10`, em
> `GpuVa=0x2431000` e `0x245B000`. No mesmo TDR, a onda: `SQ_WAVE_STATUS` com **`FATAL_HALT=1`**
> e `SQ_WAVE_TRAPSTS` com **`XNACK_ERROR=1`**.
>
> **Leitura:** o shader pede um endereço, o caminhador de tabela de páginas não resolve, a onda
> recebe XNACK e morre em halt fatal, e o CP fica esperando por quem nunca vai retirar — que é o
> stall em `RPTR=24/WPTR=32` perseguido desde a Corrida 42.
>
> **Como isso passou despercebido:** o valor estava no log **em hexadecimal cru** dentro de
> `[TDR_MEC_SNAPSHOT]` havia muitas corridas. A busca do projeto era pela string `GFXHUB_FAULT`,
> que é outro sítio e sai **zero**. Não era lacuna de conhecimento; era lacuna de **leitura**.

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **DX.1** | Fault de tradução sai **decodificado**, não em hexadecimal cru | ✅ **FECHADO (OFFLINE, 20/09)** | `[TDR_GCVM_FAULT] DECODIFICADO` nomeia `Vmid`, `WalkerError`, `MappingError`, `PermissionFaults`, `Cid`, `Rw`, `MoreFaults` e converte a página em GpuVa. Ausência de fault é declarada, nunca silenciosa | Máscaras copiadas de `gc_10_1_0_sh_mask.h`. A primeira versão teve os shifts transcritos de cabeça e **os seis saíram errados** — decodificação errada é pior que hexadecimal cru |
| **DX.2** | Composição do lote de GPUVA medida | ✅ **FECHADO NO SILÍCIO (Corrida 46)** | 11 de 11 `PUBLICACAO_COMPOSICAO` com **`Unmaps=0`** e `Maps > Livres`. **Refuta por medição** a hipótese de que reordenar unmaps antes de maps ajudaria: não há unmaps | O defeito é **capacidade real** (`Cap=1024`, o DWM pede 961 com 138 já vivos). `Teardown=0`: a reciclagem está sã |
| **DX.3** | O GpuVa que falha veio de um lote nao publicado? | RETIRADO (Corrida 47) | A comparacao usava a conversao `<<12`, que a Corrida 47 REFUTOU (5/5 `<<20` DENTRO de faixa viva). O numero que sustentava a conclusao era do espaco errado, entao a conclusao CAI -- a independencia entre fault e publicacao NAO esta estabelecida. | Retirar conclusao e mais barato que defende-la: ela vinha de uma interpretacao que a medicao derrubou |
| **DX.4** | **Por que o endereco nao traduz** | FECHADO (Corridas 47-49) | Tres corridas encadeadas: `<<20` e a granularidade (5/5 DENTRO de faixa viva, com alocacao nomeada); o segmento e **Seg=1 LOCAL_VRAM**, nao GART -- refutando memoria de sistema; e a caminhada mostrou que **a PTE PODE EXISTIR** (`PA=0x4519CC000`) enquanto outra falha no PDE de nivel 1. A Corrida 49 fechou a causa: **raiz trocada entre contextos no VMID 7**. | Tres hipoteses minhas morreram no caminho -- o deslocamento de 8 bits (era codificacao correta de CB_COLOR0_BASE), "o IB aponta para 36 MB" (eram ponteiros de 9,2 GB) e memoria de sistema. Todas por leitura de codigo, nenhuma por corrida |
| **DX.6** | Os 16 contextos GCVM sao legiveis, e o inventario nao e aliasing | ✅ **FECHADO NO SILÍCIO (Corrida 51)** | Segundo inventário: `Mudaram=3 de 16`, VMID 0 mudou de `Cntl=0x007FFE80/PtBase=0` para `0x007FFE01/0xF40FF93000`, VMID 7 também mudou, e saiu `offsets DISTINTOS, inventario CONFIRMADO` | A allowlist de leitura cobre os 16; isso não autoriza escrita indiscriminada. A escrita continua sendo uma faixa separada e auditada |
| **DX.7** | **Alocador de VMID por contexto** | ✅ **FECHADO NO SILÍCIO (Corridas 52–54)** | Mapa medido (52), instalação 32/32 (53) e o `INDIRECT_BUFFER` carregando o `poolVmid` (54, provado pelos faults nomearem VMID 1/2/3 e nunca o 7) | **Não consertou o fault.** A Corrida 56 mostrou por quê: a assinatura segue o ENDEREÇO, não o contexto. O alocador era pré-requisito para enxergar isso, não o conserto |
| **DX.8** | **O silício aceita raiz nos VMIDs do pool** | ✅ **FECHADO NO SILÍCIO (Corrida 53)** | **32 de 32** `INSTALACAO LATERAL` com `InstallStatus=0 RecordStatus=0 Readback=1 Flush=1 Ready=1`, em **cinco** VMIDs distintos (1–5). Zero `LATERAL FALHOU`, zero `LATERAL PULADA`, zero exaustões | **Só 1–5 foram exercitados.** Os VMIDs 6 e 10–15 continuam sem escrita medida — a carga nunca pediu mais de 5 slots. Dizer "os doze funcionam" seria extrapolação |
| **DX.9** | **O latch de fault é drenável e restaurável** | ✅ **FECHADO NO SILÍCIO (Corrida 56)** | 4/4 drenos com `ClearAcked=1`, `ClearIneficaz=0` e `CntlReadback == CntlAntes == 0x3FFFFFFC` | Antes disso, dez corridas leram um latch preso (`AceitaAtualizacoes=0`, medido na 55). `MoreFaults=1` **não** significa fila — significa algo ainda sem nome |
| **DX.10** | **A caminhada do fault sobrevive à morte do contexto** | ✅ **FECHADO OFFLINE (21/09)** | `Bc250VmidPoolRootForFaultWalk` prefere a entrada viva `READY` e usa a última instalação comprovada retida quando o contexto já morreu; fonte, contexto e geração saem no log | A retenção fica fora da entrada reciclável e nunca dá autoridade à submissão. Build e safety audit PASS; mutação removendo a retenção reprovou. **NÃO VALIDADO EM HARDWARE** |
| **DX.11** | **O que é o endereço `0x200040`** | ✅ **FECHADO OFFLINE (21/09)** | `0x200040 << 12 = 0x200040000` é resolvido contra o snapshot e publica handle, token imutável, nome, segmento e offset; ausência também é explícita | Corrida 58: 25/25 arenas de 64 páginas eram executáveis, mas o token RADV permaneceu `CLASSE_DESCONHECIDA`. Permissão validada no silício; identidade semântica ainda parcial |
| **DX.12** | **A invalidação de TLB segue os VMIDs do pool** | ✅ **FECHADO NO SILÍCIO (Corrida 59)** | `Bc250VmidPoolReadyFlushMask` em fonte única; o flush soma os slots `READY` aos fixos 7/8/9, solta o `VmidPoolLock` antes do MMIO e publica `PoolReadyMask`/`FalhouMask`; flush parcial fecha o veredito global | Corrida 59: `PoolReadyMask` até `0x001E`, ACK em todos, zero falhas, `Polls=1`. **A hipótese caiu como causa suficiente** — a taxa de fault não mudou (3/7 contra 3/6 e 4/6). A cobertura está certa; ela não era o defeito |
| **DX.13** | **A escrita de diretório de páginas é observável e fecha** | ✅ **FECHADO NO SILÍCIO (Corrida 60)** | Status do encode deixou de ser descartado (recusa não grava e se declara); `LargePage` no nível ≥ 1 deixou de ser invisível; `[PDE_WRITE] ARENA` publica o que escrevemos no índice 4096 e um tripwire recusa bits [5:3] | Corrida 60 respondeu: **escrevemos o índice 4096 101 vezes, sempre limpo**; zero tripwire, zero recusa. O PDE sujo da Corrida 59 não reproduziu — aquele achado era mais fraco do que eu o apresentei (as três caminhadas diziam `TABELA MUDOU`) |
| **DX.14** | **O HDP é drenado antes da invalidação de TLB** | ✅ **FECHADO NO SILÍCIO (Corrida 61)** | `HDP_MEM_COHERENCY_FLUSH_CNTL` escrito com readback **antes** do laço por VMID; `HdpFlush=` publicado e falha declarada; o portão trava a ORDEM | Corrida 61: **75/75 `HdpFlush=1 Ok=1`** sob carga comparável, e o fault permaneceu. **A hipótese caiu** — a visibilidade da tabela não era a causa. A drenagem continua correta e fica; o alvo passa a ser o `PT_BASE` no instante da submissão |
| **DX.15** | **O VMID é devolvido também no silício** | ✅ **FECHADO NO SILÍCIO (Corrida 62)** | `Bc250GcvmUninstallVmid` desarma `CNTL`, zera os endereços e invalida a TLB daquele VMID, nessa ordem, com readback e falha declarada; chamado no `Release` fora do lock | Corrida 62: **24/24 `status=0 Readback=1 Flush=1`**, zero falhas, zero BugCheck — e o fault permaneceu. Fecha a Lei 5 (APPLY sem TEARDOWN) e **não era a causa** |
| **DX.17** | **Readback do PDE da arena: escrevemos limpo, leem sujo** | ✅ **FECHADO NO SILÍCIO (Corrida 64)** | A escrita em `destination[4096]` é lida de volta pelo mesmo ponteiro e comparada; divergência é declarada e **não** corrigida; `[PDE_ARENA_CONFRONTO]` põe o último valor escrito e o que o TDR encontrou no **mesmo log** | Corrida 64: **99 de 99 escritas limpas** com readback idêntico (25 não-nulas, todas `0x...001`, `Reservado[5:3]=0`), **zero DIVERGENCIA**. Provou no silício que a escrita da CPU chega íntegra à memória física; o defeito NÃO é da CPU |
| **DX.5** | A tradução passa a funcionar | 🔬 CORRIDA | O fault deixa de ser latchado e as ondas param de morrer em `FATAL_HALT` | Corrida 58: escrita em `0x2469A6000` falhou apesar de PTE R/W/X válida e contemporânea na visão da CPU. Exigir submissão GFX completando; ausência isolada de fault não fecha |

---

### Fase D1 — Primeiro pixel não-nulo, por GPU, por um backend só

> ⚠️ **Nada em D1.6–D1.8 é alcançável enquanto a Fase DX não fechar.** Atualizado em 20/09: o
> motivo mudou. Antes era "o anel não executa" — isso foi resolvido (D0.5/D0.6). Agora é "o
> shader morre por falha de tradução". Na Corrida 46 houve 157 submissões gráficas, 89 com
> `RPTR == WPTR`, e ainda assim **62 amostras com `NonZero=0/256`**: não há pixel porque a onda
> que o produziria morre antes.


| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D1.1** | `Bc250UmdEmitPm4Draw` sai do caminho de Draw; vira ferramenta de bring-up do KMD | ✅ **FECHADO (OFFLINE)** | `Bc250Umd10Draw`/`DrawIndexed`/`*Instanced` chamam exclusivamente `ExecuteVkDraw`. `Bc250UmdEmitPm4Draw` removida do caminho de render; portão `audit_draw_has_no_cpu_fallback` em PASS (40/40 gates). | Mutação verificada: reintroduzir chamada reprova o portão. |
| **D1.2** | Present recusa com **motivo nomeado** quando o backend que renderizou ≠ o que vai apresentar | ✅ **FECHADO (OFFLINE)** | `Bc250UmdPresentVulkanDetileAndScanout` avalia os contadores e recusa formalmente com mensagens nomeadas (`[PRESENT RECUSADO: DIVERGENCIA_BACKEND_DRAW_VS_PRESENT]` e `[PRESENT RECUSADO: NENHUM_RENDER_DETECTADO]`). | Recusa ruidosa e explícita no log, sem cópia cega de zeros. |
| **D1.3** | PSO gráfico Vulkan criado de fato: shader modules, pipeline layout, render pass CLEAR + LOAD | ✅ **FECHADO NO SILÍCIO (Corrida 15)** | `Bc250UmdVkCreateRtv` e `Bc250UmdPipelineCacheInit` criam `passClear`, `passLoad` e compilam `VkGraphicsPipeline` no slot 0 (`pipeline=0x5405F50`, `layout=0x5405CD0`, `bc250_umd_log_7364-FINAL.txt:2774`). | Shaders VS/PS vinculados e compilados sem descriptors ausentes. |
| **D1.4** | `pInitialDataUP` consumido e vertex buffer subido à GPU | ✅ **FECHADO NO SILÍCIO (Corrida 15)** | `Bc250UmdUploadBufferData` alocou buffer e transferiu 72 bytes para `VkDeviceMemory` visível pelo host (`bc250_umd_log_7364-FINAL.txt:2755`: `UploadBufferData SUCESSO: 72 bytes copiados para VkMem=0x4A0B40`). | Dados gravados na memória acessível pela GPU, não apenas em shadow de CPU. |
| **D1.5** | `ExecuteVkDraw` deixa de fazer fallback | ✅ **FECHADO NO SILÍCIO (Corrida 15)** | 60 de 60 draws executados nativamente via Vulkan (`bc250_umd_log_7364-FINAL.txt:2776, 2798, 3548`: `DRAW EXECUTADO NATIVO EM VULKAN, VkDraws=1..60, Total=60, Verts=3`) com `NativePm4DrawCount == 0`. | Zero fallback para PM4; execução direta no driver Vulkan RADV. |
| **D1.6** | Clear produz cor não-nula no render target | 🔬 CORRIDA 18 | `COMPOSITE_DATA_PROOF` com `NonZeroSamples=5/5` | `ZERO_INCONCLUSIVO` não conta. Zero contra zero não prova pixel |
| **D1.7** | Triângulo RGB visível sobre o clear azul | 🔬 CORRIDA 18 | Confirmação visual do operador **mais** amostras não-nulas em 3 regiões distintas do quadro | Clear sozinho pinta a tela toda de azul e passaria em D1.6 — as amostras têm que distinguir triângulo de fundo |
| **D1.8** | A imagem é atribuível ao **nosso** driver | 🔬 CORRIDA 18 | Timestamp da imagem **anterior** ao `RECOVERY_OK_ONLINE`; `bc250mcdm` em Código 0 na hora | Imagem que aparece no rollback é o **BasicDisplay**. Esse engano já foi cometido em 07/09 e 10/09 |

---

### Fase D2 — Compilador de shader dentro do driver

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D2.1** | Tradutor DXBC→SPIR-V vendorizado no repositório | ✅ **FECHADO (OFFLINE)** | `libvkd3d-shader-1.dll` vendorizada e integrada via `bc250_vkd3d_shader_adapter.c`; proveniência LGPL documentada em `docs/`. | Verificada ausência de conflitos de licença ou símbolos não resolvidos. |
| **D2.2** | Tradutor **linkado** na `bc250umd.dll`; `LoadLibrary` externo removido | 🔧 OFFLINE | `bc250_vkd3d_shader_adapter.c` não carrega DLL de caminho externo; a DLL cresce no tamanho esperado | Hoje ele carrega de `C:\BC250_LIB` ou de um caminho no registro ([bc250_d3d_umd.c:400](driver/umd/bc250_d3d_umd.c:400)). Dependência externa não vai no pacote assinado |
| **D2.3** | Portão que **proíbe** carregar tradutor de caminho externo | 🔧 OFFLINE | Portão novo em `audit-bc250mcdm-safety.py` | Mutação: reintroduzir o `LoadLibrary` externo **reprova** |
| **D2.4** | Suíte de tradução com shaders DXBC reais | ✅ **FECHADO NO SILÍCIO (Corrida 15)** | Shaders DXBC reais (VS 29 tokens / 768 bytes SPIR-V, PS 17 tokens / 780 bytes SPIR-V) traduzidos em runtime por `libvkd3d-shader` e compilados em `VkShaderModule` no hardware (`bc250_umd_log_7364-FINAL.txt:2729-2736`). | Estrutura SPIR-V validada pela engine Vulkan com criação bem-sucedida do PSO. |
| **D2.5** | O driver funciona com a pasta externa **ausente** | 🔬 CORRIDA 2 | `C:\BC250_LIB` renomeada antes do bind, e a tradução continua acontecendo | Este é o teste que prova que D2.2 é real. Sem ele, "interno" é afirmação, não medição |
| **D2.6** | Todo shader que o DWM compila é traduzido | 🔬 CORRIDA 2 | Zero `SHADER_TRANSLATOR: traducao ... falhou`; zero `SHADER_TRANSLATOR: desarmado` | `desarmado` significa que **nada** foi traduzido — é ausência de teste, não aprovação. E `default_ps_spv.inl` não pode ser a fonte do pixel de um shader de aplicativo |

---

### Fase D3 — Cobertura D3D11 suficiente para o DWM

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D3.1** | Inventário: quais DDIs o DWM realmente chama, e quais recusam hoje | 🔧 OFFLINE | Lista extraída dos logs das corridas 1 e 2, com cada DDI marcada atendida/recusada/muda | Inventário tirado de teste próprio não vale — tem que ser do **DWM real**, senão a lista é o que nós achamos que ele chama |
| **D3.2** | Recursos: texturas 2D, formatos e pitch que o DWM usa | 🔧 OFFLINE | Cada formato do inventário cria recurso sem recusa; suíte cobre pitch e dimensões | Criar recurso ≠ recurso utilizável. Conferir que o mapeamento de formato DXGI→Vulkan é o correto, não só não-nulo |
| **D3.3** | Estado: blend, rasterizer, depth/stencil, samplers | 🔧 OFFLINE | O estado pedido pela API chega ao PSO; suíte com mutação por campo | Estado **ignorado** silenciosamente é a classe de defeito da faixa C1. Cada campo ignorado tem que recusar ou ser aplicado |
| **D3.4** | SRVs, constant buffers e descriptor sets reais | 🔧 OFFLINE | Descriptor set populado e vinculado; suíte prova o binding | `DefaultDescriptorSetLayout = VK_NULL_HANDLE` só é aceitável enquanto nenhum shader usar recurso |
| **D3.5** | Nenhuma DDI devolve sucesso sem backend | 🔧 OFFLINE | `audit-ddi-noop-callbacks.py` PASS sem exceção nova | Lei 4. Sucesso sem backend é a mentira mais cara do projeto |
| **D3.6** | O DWM percorre um quadro inteiro sem recusa | 🔬 CORRIDA 3 | Zero `RECUSADO`, zero `E_NOTIMPL`, zero `DrawIndexed: RECUSADO` numa janela de 60 s | Zero `Draw SUBSTITUIDO por copia 1:1` junto — substituição por blt **não é rasterização** |
| **D3.7** | O `dwm.exe` para de morrer | 🔬 CORRIDA 3 | Zero fail-fast (`0xe0464645`, `0x887A0004`) no Event Log da janela | DWM vivo sem compor não fecha nada. Precisa vir com D3.6 |
| **D3.8** | O quadro veio do render target **do DWM** | 🔬 CORRIDA 3 | A superfície apresentada é a alocação que o DWM criou e desenhou | Superfície nossa pré-preenchida passaria em D3.6 e D3.7 e seria falso sucesso |

---

### Fase D4 — Desktop acelerado

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D4.1** | Cadeia de flip/swapchain do DWM | 🔧 OFFLINE | `hDstAllocation` e os buffers da cadeia registrados e coerentes | A corrida de 13/09 mostrou `hDstAllocation=0` e buffers criados avulsos. Cadeia não registrada é present que o dxgkrnl recusa |
| **D4.2** | Present por GPU com tripé, sem caminho de CPU | 🔧 OFFLINE | Portões `audit_present_is_gpu_attributable` e `no_host_copy_fallback` PASS | Mutação: reintroduzir cópia por CPU **reprova** |
| **D4.3** | ≥ 60 quadros consecutivos apresentados por GPU | 🔬 CORRIDA 4 | `BYTE_PROOF PASS` em todos; `BlankStreak == 0` | `INCONCLUSIVO (FRAME EM BRANCO)` não conta como quadro |
| **D4.4** | Desktop **interativo**: mover janela atualiza a tela | 🔬 CORRIDA 4 | Operador move uma janela e o conteúdo acompanha, sem rastro nem congelamento | Imagem estática correta passaria em D4.3. Interatividade é o que separa desktop de screenshot |
| **D4.5** | Explorer e menu Iniciar compõem | 🔬 CORRIDA 4 | Ambos abrem e desenham | São clientes D3D11 distintos do DWM; exercitam caminhos que o DWM sozinho não exercita |
| **D4.6** | Nada disso veio da CPU | 🔬 CORRIDA 4 | `HostCpuCopies == 0` e `FallbackCpuCount == 0` na janela **inteira** | O present de 11/09 22:46 foi por CPU e o gate carimbou PASS. Conferir a corrida inteira, não uma amostra |

---

### Fase D5 — Desktop estável

| ID | Entrega | Fecha como | Critério de conclusão | ...porém verificar |
|---|---|---|---|---|
| **D5.1** | Par de TDR revalidado contra o caminho novo de present | ✅ **FECHADO (OFFLINE)** | Portões `audit_tdr_pair_has_backend` e `audit_engine_status_measures` em PASS na suíte de auditoria (`audit-bc250mcdm-safety.py` 40/40 gates). | Revalidado com preflights estritos e proibição de stubs sem backend. |
| **D5.2** | ≥ 10 min de desktop contínuo | 🔬 CORRIDA 5 | Composição ativa do início ao fim da janela, sem degradação de FPS | Zero `status_device_removed`; `Sub == Ret == Notified` |
| **D5.3** | TDR induzido dispara e o driver recupera | ✅ **FECHADO NO SILÍCIO (Corrida 15)** | No Frame 20, hang de hardware tratado pelo WDDM (`0xC00002B6`); KMD executou quiesce de SDMA0 e `CP_RB0`, soft-reset GRBM (CP, GFX, CPF, CPG) e restart comprovado por NOP físico; sistema operacional não travou e o harness continuou até o Frame 60 (`Exit 0`). | Engine rearmada sem BSOD ou BugCheck 0x116/0x119. |
| **D5.4** | **O display sobrevive ao TDR** | 🔬 CORRIDA 5 | Desktop volta a compor sem reboot e sem perder a imagem | Esta é a perna que nunca fechou (item 25). Desktop restaurado pelo watchdog/BasicDisplay é **recuperação**, não sucesso nosso: o `RECOVERY_OK_ONLINE` tem que vir **depois** da prova |
| **D5.5** | Sem regressão de estabilidade | 🔬 CORRIDA 18 / CORRIDA 5 | Zero BugCheck `0x116`, `0x119`, `0x133`, `0x139`, zero `DPC_WATCHDOG_VIOLATION` | 0x133 eliminado na Corrida 17 por desacoplamento `FromDpc` + `RetireBusy`; 0x139 eliminado por inicialização explícita de DPCs no `AddDevice`. Ausência comprovada no silício da corrida inteira |

---

## 3. Orçamento de corridas

> ⚠️ **ESTE ORÇAMENTO ESTOUROU, e o registro tem de dizer isso.** O plano previa **6 corridas de
> marco**; foram gastas cerca de **50**. Não foi desperdício: as Corridas 44 a 50 fecharam uma
> cadeia diagnóstica inteira, e três achados — base `>>8` no restart, fault de VMID 7, e raiz
> trocada entre contextos — **só existem porque foram medidos**, um por corrida. Mas a frase
> "5 corridas no plano inteiro" na tabela de custos abaixo é **falsa** e fica mantida apenas como
> registro do que se estimou em 17/09.

O plano previa caber em **6 corridas físicas de marco**. Cada uma
fecha um lote — e cada uma produz informação nos dois desfechos, o que é a exigência da Lei 9.

| Corrida de Marco | Fecha | Pergunta única | Se refutar |
|---|---|---|---|
| **Marco 0** | D0.5 – D0.7 | O CP busca e executa uma submissão do anel gráfico? | Se o `RPTR` não andar com o canal novo publicado e conferido, o mecanismo de publicação **não** era a causa, e o suspeito passa a ser o estado do CP: `CP_ME_CNTL`, o preâmbulo em `0x200014000` e por que `GUI_ACTIVE=1` com todos os blocos 3D limpos |
| **Marco 1** | D1.5 – D1.8 | O caminho Vulkan produz pixel não-nulo no scanout? | ~~O suspeito passa a ser a criação do PSO (D1.3)~~ — **esta cláusula foi refutada em 18/09**: não era o PSO nem o Present, era o anel não consumir. Marco 1 só pode ser gasto **depois** do Marco 0 |
| **Marco 2** | D2.5 – D2.6 | O tradutor interno cobre os shaders do DWM? | A lista de shaders que falharam vira o backlog de D2 |
| **Marco 3** | D3.6 – D3.8 | O DWM percorre um quadro sem recusa? | Cada `RECUSADO` do log vira uma tarefa D3.x nova |
| **Marco 4** | D4.3 – D4.6 | O desktop compõe e responde pela GPU? | O ponto de parada no log nomeia a DDI, não a hipótese |
| **Marco 5** | D5.2 – D5.5 | O desktop sobrevive a um TDR? | O par quiesce/restart já é medido; a falha diz qual das duas pernas caiu |

### 3.1 Mapeamento e Progresso das Corridas Físicas

- **Corrida 15 (17/09 17:48):** Validou D1.3, D1.4, D1.5, D2.1, D2.4, D5.1 e D5.3 no silício (60/60 quadros D3D11 Exit 0, 45 submissões no `CP_RB0`, 60 draws Vulkan nativos, TDR de hardware tratado no Frame 20 com restart comprovado).
- **Corrida 16 (17/09 18:35):** 20 submissões 3D no `CP_RB0`, isolamento do BugCheck `0x133` (`VidSchDdiNotifyDpc`) via dump completo de memória, corrigido com guarda `RetireBusy` e desacoplamento de DPC.
- **Corrida 17 (17/09 19:31):** `0x133` comprovadamente extinto no hardware; isolamento de BugCheck `0x139` (CFG de DPC não inicializada), corrigido com inicialização estrita no `Bc250DxgkDdiAddDeviceActive`.
- **Corrida 18 (17/09 20:09):** `0x133` e `0x139` extintos no silício; BugCheck `0x0A` isolado (KDPC compartilhada entre timer e `KeInsertQueueDpc`), corrigido por desacoplamento estrito de DPCs.
- **Corrida de 18/09 14:20:07 (lab 3.184.1.0, SYS `E7E7A3B2…`) — A CORRIDA QUE CRIOU A FASE D0:** 4 de 4 submissões gráficas recusadas com `WPTR_READBACK_DIVERGENTE` (escrevemos 12, o registrador devolveu 0), transporte MAPEADO e `Write32` aceito. Zero TDR, zero BugCheck, zero `DEVICE_REMOVED`, zero `GFXHUB_FAULT`. SDMA0 com 313 submissões e 287 `TRIPE_PROVADO` no mesmo log, provando que MMIO de ponteiro de anel funciona — só não no `CP_RB0`. `STALL_BASELINE` tirada antes de qualquer submissão refutou atribuir o stall ao nosso IB. Classificador de IB corrigido: 4/4 `IB_LINEAR_CONFIRMADO`. 379 draws Vulkan nativos e `NonZero=0/256` em tudo: tela preta pela causa agora nomeada. **Veredito: PARCIAL — avanço diagnóstico decisivo, zero avanço de entrega.**
- **Corrida 44 (20/09 14:39, lab 3.202.1.0):** H44 confirmada — restart do CP_RB0 com `BASE = GpuVa>>8`, 5/5 em `TDR_GFX_RESTART_BASE Pass=1` e `TDR_GFX_RESTART_PROOF PASS`. **167 submissões GFX contra 1 na corrida anterior**, fences até 173 escritas pela GPU. Zero BugCheck. Foi aqui que D0.5/D0.6 começaram a fechar.
- **Corrida 45 (20/09 20:49, lab 3.205.1.0):** a sonda de onda leu uma onda inteira — `FATAL_HALT=1`, `XNACK_ERROR=1` — e a análise achou `FaultStatus=0x0070153C` (`VMID=7`, `WALKER_ERROR=6`) nos cinco TDRs, em hexadecimal cru, que ninguém havia decodificado. **Veredito: desfecho D** (a sonda não restaurou o seletor; foi desarmada).
- **Corrida 46 (20/09 21:37, lab 3.206.1.0):** critério fechou nos três itens. Fault **reproduzido** em outro candidato; 11/11 `PUBLICACAO_COMPOSICAO` com `Unmaps=0` → a publicação de GPUVA falha por **capacidade**, não por ordem. 89 `RPTR == WPTR`, 105/105 fence pela GPU, zero BugCheck (terceira seguida).
- **Corrida 47 (20/09 22:07, lab 3.207.1.0):** o discriminador de granularidade fechou o garfo — 5/5 `<<20` DENTRO de faixa viva com alocação nomeada, `<<12` num vazio de gigabytes. Duas alocações: `0xF3` (`Size=0xA8C000` = 2560x1080x4) e `0xDF`.
- **Corrida 48 (21/09 07:58, lab 3.209.1.0):** os DOIS desfechos na mesma corrida — em `0x243100000` a caminhada RESOLVEU (`PA=0x4519CC000`), em `0x242100000` o PDE de nível 1 estava ZERO. As faixas são **`Seg=1 LOCAL_VRAM`**, refutando memória de sistema. O defeito deixou de ser "PTE nunca escrita" e virou dessincronização no tempo.
- **Corrida 49 (21/09 08:46, lab 3.210.1.0):** **a causa.** 5 comparações de raiz, TODAS divergentes (2, 8, 9, 14, 15 de 16 carimbos); 6 raízes distintas em 82 submissões; o mesmo IB sob três raízes. Contextos disputam o VMID 7.
- **Corrida 50 (21/09 09:34, lab 3.211.1.0):** inventário dos 16 contextos — `EmUso=0 Livres=16 Ilegiveis=0`. VMID por contexto é viável com folga. Coleta interrompida por reset manual; cauda resgatada (2.690.800 bytes, SHA-256 `E11D8D24…`), e o inventário teve de ser lido do log cumulativo vivo — **defeito de coleta a consertar**.
- **Corrida 51 (21/09 10:00, lab 3.212.1.0):** segundo inventário fechou o aliasing: 3 de 16 mudaram, incluindo VMID 0 (`0x007FFE80/0` → `0x007FFE01/0xF40FF93000`) e o alvo VMID 7; veredito `offsets DISTINTOS, inventario CONFIRMADO`. DX.6 fechado.
- **Corrida 52 (21/09 11:24, lab 3.213.1.0, SYS `831C79B7…`):** desfecho A do critério prévio. Quatro faults com 2/3/3/3 contextos vivos; a raiz do TDR coincide com um contexto e diverge dos demais. 27 atribuições, 24 liberações, zero exaustões, pico simultâneo 4. Cinco TDR/restart, mas só quatro faults decodificados; um declarou `SEM FAULT LATCHADO`. Zero BugCheck e rollback `RECOVERY_OK_ONLINE`.
- **Faixa offline 1 pós-Corrida 52 concluída:** pool serializado por spin lock dedicado; aquisição, raiz, liberação e snapshot de TDR compartilham a mesma proteção.
- **Faixa offline 2 pós-Corrida 52 concluída:** leitura e escrita MMIO permitem somente os sete registradores exatos dos VMIDs derivados do pool. Reservados e vizinhos têm testes negativos; build e auditorias passaram, com portões provados por mutação.
- **Faixa offline 3 pós-Corrida 52 concluída:** `InstallVmid` preserva a identidade até o flush e o pool distingue raiz observada de raiz realmente instalada, com invalidação de estado velho em falha e reciclagem. O miniport ainda não popula esse estado.
- **Faixa offline 4 pós-Corrida 52 concluída:** corrigida a janela de `DestroyContext`: `IN_FLIGHT_SUBMISSIONS` preserva GPUVA, vínculo e VMID; a devolução só ocorre depois da destruição concluída. Teste integrado e mutação do portão passaram.
- **Próxima faixa:** integrar a instalação no `poolVmid` e registrar seu resultado por entrada, mantendo ainda o `INDIRECT_BUFFER` em VMID 7 para isolar a mudança. Na faixa seguinte, instalação e pacote passam juntos ao mesmo VMID e o diagnóstico de fault deixa de assumir 7. Não empacotar nem gastar corrida entre essas faixas offline.

**Regra:** corrida que não tem critério escrito **antes** não é corrida, é tentativa. Critério
escrito depois vira racionalização do que aconteceu.

---

## 4. Sequência obrigatória de cada corrida

Imposta por `CLAUDE.md` e pelo histórico. Pular qualquer passo já custou corrida.

| Passo | Ação | Por quê |
|---|---|---|
| 1 | Subir o `DriverVer` fonte | Hoje três binários distintos carregam `3.181.1.0` |
| 2 | `driver/umd/build-umd.bat` **e** `driver/wdk/build-contract.bat`, ambos EXIT=0 | O empacotador **não compila** |
| 3 | `tools\build-bc250mcdm-lab-bringup.ps1` + as auditorias da fase | Pacote sem auditoria PASS não vai ao hardware |
| 4 | Conferir as strings novas no binário **assinado** | Prova de que o empacotador não copiou o candidato anterior |
| 5 | `tools\inspect-boot-fuse.ps1 -Reset` | O fusível está TRIPPED e o binder recusa |
| 6 | **Boot frio** | Estado de engine não cai com desbind |
| 7 | Escrever o critério de sucesso e o de abandono | Lei 9 |
| 8 | Corrida, resgatar a cauda do log e arquivar com SHA-256 | `C:\BC250_DISPLAY_LOG.txt` é cumulativo; a pasta de evidência trunca |

---

## 5. Armadilhas já pagas — reler antes de marcar qualquer ✅

Dezoito vezes o portão, o teste ou o relato carimbou o falso sucesso — ou a medição certa existia e não foi lida.

| # | O que aconteceu | Como não repetir |
|---|---|---|
| 1 | `A15` — a auditoria **exigia** a comparação crua de `RPTR`, obrigando o defeito a existir | Todo portão novo precisa de mutação que o reprove |
| 2 | `mcdm_callbacks_test` exigia `STATUS_SUCCESS` de TDR **sem executor** instalado | Teste não pode congelar a mentira |
| 3 | `C1` — `Draw` atendido por `memcpy` entre shadows de CPU, marcado sucesso antes de tentar GPU | R2 da §1 |
| 4 | `sdma_blt_executed` casava com a string `PRESENT_BLT`, cego à cópia por CPU que o escape fazia | Contador mede o **efeito**, não a string |
| 5 | Corrida 13 — 1787 `COMPOSITE_BLT` com `Prova=ZERO_INCONCLUSIVO` contados como progresso | R3 da §1 |
| 6 | Imagem atribuída ao nosso driver quando era o **rollback** restaurando o BasicDisplay | Comparar timestamp da imagem com o do rollback |
| 7 | Corrida 16 — Deadlock e BugCheck `0x133` por chamar `DxgkCbNotifyDpc` síncrono dentro de `SubmitCommandVirtual` segurando lock do VidSch | Desacoplamento temporal: enfileirar DPC (`FromDpc = FALSE`) e serializar com guarda atômica `RetireBusy` |
| 8 | Corrida 17 — BugCheck `0x139` (CFG) por chamar `KeInsertQueueDpc` sobre struct com `DeferredRoutine` não inicializada | Inicialização estrita e incondicional de todas as DPCs e timers no `Bc250DxgkDdiAddDeviceActive` |
| 9 | Corrida 18 — BugCheck `0x0A` (IRQL_NOT_LESS_OR_EQUAL) por colisão concorrente de `KeInsertQueueDpc` em `KDPC` associada a `KeSetTimerEx` periódico | Desacoplamento estrito de DPCs: `RetirementTimerDpc` exclusiva do timer e `RetirementNotifyDpc` exclusiva do enfileiramento assíncrono |
| 10 | 18/09 — **"submissão aceita" contada como "submissão executada"**, três corridas seguidas. `SUBMIT SUCESSO ... NewWptr=12` era o KMD relatando o **seu próprio shadow**; o registrador estava em 0 e o CP nunca buscou nada. Isso inflou o registro de progresso de três corridas | Ponteiro de anel só conta como avanço com **readback do registrador**. `RPTR` avançando prova busca; fence sobre poison prova consumo do anel; byte a byte prova execução do IB. São três coisas distintas (Diretriz Universal nº 3) |
| 11 | 18/09 — **a correção fail-closed apagou a própria telemetria que a mediria.** Recusar a submissão antes do retirement fez `GFX_TRIPE_VEREDITO` sair zero vezes, na corrida em que as Pernas 1 e 2 foram consertadas | Veredito de medição tem de ser emitido no caminho de **recusa** também. Instrumentação que só existe no caminho de sucesso desaparece exatamente quando é necessária |
| 12 | 18/09 — **decodificação de registrador afirmada sem conferir o dono do bit**, duas corridas seguidas. `CpStalled1=0x00000C00` foi lido como `GFX_CNTX_NOT_AVAIL_TO_ME \| ME_WAIT_ON_AVAIL_BUFFER`, que são campos do **`CP_STALLED_STAT2`** (`:6466-6467`), não do STAT1 (cujos bits 10/11 são `ME_HAS_ACTIVE_CE_BUFFER_FLAG` / `ME_HAS_ACTIVE_DE_BUFFER_FLAG`, `:6428-6429`). O STAT2 medido é `0x00000000` | Nome de campo vem do header, com número de linha citado. Dois registradores irmãos têm campos homônimos em posições diferentes — conferir **qual** registrador antes de nomear o bit |
| 14 | 20/09 — **medição existente em hexadecimal cru equivale a medição ausente.** `FaultStatus=0x0070153C` estava no log havia corridas, dizendo `VMID=7 WALKER_ERROR=6`; ninguém leu porque a busca era pela string `GFXHUB_FAULT`, que sai zero | Todo registrador de diagnóstico sai **decodificado por campo**, com as máscaras copiadas do header. Hexadecimal num campo que ninguém lê não é evidência |
| 15 | 20/09 — **decodificação com shifts transcritos de cabeça: os seis saíram errados** (`VMID` em 18 no lugar de 20, `WALKER_ERROR` em 8 no lugar de 1, `RW` em 31 no lugar de 18) | Máscara vem do header por cópia literal, nunca de memória. Decodificação errada é pior que o cru: o cru não mente |
| 16 | 20/09 — **linha de telemetria nova sumiu por amostragem.** `PUBLICACAO_COMPOSICAO` saiu **zero vezes** em 10 falhas; a isenção do amostrador casa por **substring** (`FALHA`, `OCUPACAO`, `PASS`) e o nome escolhido não tinha token isento | Toda linha nova em caminho raro e crítico precisa conferir a lista de isenção do amostrador **antes** da corrida |
| 17 | 20/09 — **quase-corrida gasta com perfil rebaixado.** O empacotador aceitou `DISPLAY_LAB` em modo READ-ONLY para um ensaio que exige `DISPLAY_RENDER_LAB`; o audit do pacote deu PASS porque confere o **pacote**, não a **intenção** | O binder agora registra o perfil ligado e **recusa** candidato mais fraco que o do último bind, nomeando a capacidade que caiu |
| 18 | 20/09 — **um conserto quase implementado sem medir o efeito.** Reordenar unmaps antes de maps parecia obviamente certo; a medição mostrou **`Unmaps=0`** em 11 de 11 lotes — teria tido efeito zero | Fix cujo efeito não pode ser previsto por medição vira **telemetria primeiro**, conserto depois |
| 13 | 18/09 — **verificação de string no binário errado.** `ONLINE SUCESSO`, que provadamente executou, aparece como **ausente** no `bc250mcdm.sys` de `build-contract.bat`, que é a *MCDM DEVELOPMENT IMAGE* | R4 se confere no SYS **assinado** de `package/output_lab`. Positivos valem em qualquer binário; **negativos só valem no assinado** |

---

## 6. Portões offline obrigatórios, por fase

| Fase | Auditorias | Portões específicos |
|---|---|---|
| **D0** | `audit-bc250mcdm-safety.py` | `audit_gfx_ring_tripod_and_ib_inspection` (já existe: trava a exclusão do poison, a inspeção do IB e o veredito de fonte única), mais o portão novo que exige veredito de tripé **no caminho de recusa** e proíbe afrouxar a divergência de MMIO sem segundo canal provado |
| **D1** | `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py` | `audit_draw_has_no_cpu_fallback`, `audit_present_is_gpu_attributable`, `audit_present_path_is_not_mute`, `audit_f5_2_pm4_draw_pipeline` |
| **D2** | + `audit-package-readiness.py` | `audit_gfx_shader_contract_is_honest`, `audit_gfx_observable_shader_is_honest` |
| **D3** | + `audit-umd-node1-submission.py` | `audit_dxgi_present_chain_honest`, `audit_f3_pixel_and_residency_truth`, `audit_describe_allocation_is_not_mute` |
| **D4** | todas | `audit_primary_allocation_is_declared_to_the_kernel`, `audit_present_source_uses_segment_base`, `audit_composition_never_submits_a_context` |
| **D5** | todas | `audit_tdr_pair_has_backend`, `audit_engine_status_measures`, `audit_induced_hang_is_fail_closed`, `audit_submit_dispatch_level_safety` |

Suítes de contrato: [`bc250_d3d_umd_test.c`](driver/tests/bc250_d3d_umd_test.c),
[`bc250_wddm_winsys_test.c`](driver/tests/bc250_wddm_winsys_test.c),
[`blt_proof_contract_test.c`](driver/tests/blt_proof_contract_test.c),
[`active_miniport_test.c`](driver/tests/active_miniport_test.c).

---

## 7. O que NÃO fecha esta etapa

Declarado para não haver ambiguidade depois:

- Compilar, assinar, carregar DLL, enumerar adaptador ou abrir swapchain **não** é aceleração.
- `Código 0` e `Exit 0` isolados **não** fecham teste.
- Tela com imagem **não** é prova se o timestamp coincide com o rollback do watchdog.
- Quadro produzido por `RtlCopyMemory` da CPU **não** conta, mesmo que apareça na tela.
- 600 quadros de um teste Vulkan próprio **não** substituem o DWM compondo.
- Mudar resolução ou refresh está **fora desta etapa** — é DCN, e vem depois de D5.

---

## 8. Depois de D5

Fora do escopo deste documento, registrado apenas para fixar a ordem acordada:

| Fase seguinte | Conteúdo | Observação medida |
|---|---|---|
| **DCN-a** | Trocar timing/refresh **mantendo** o link como o GOP treinou | `docs/03` já cataloga `OTG0_OTG_H_TOTAL`, `V_TOTAL`, `H_SYNC_A`, `V_SYNC_A`, `BLANK_START_END`, `OTG_CONTROL` e `HUBPREQ0_DCSURF_*`. Que o OTG aceite reprogramação a quente sem re-treinar é **H**, não medido |
| **DCN-b** | Resolução arbitrária: AUX, EDID real, link training DP, PLL/DTO | Item 75 (AUX) está ⬜ e item 77 (EDID real) 🟡 e depende dele. Nada do lado do link está catalogado |
| **Jogos** | D3D12, Steam, títulos reais | Plano próprio |

**Regra que vale já:** nunca publicar um modo no VidPn antes de provar que ele é entregável no
silício. Publicar modo que o driver não entrega troca "modo fixo que funciona" por "tela preta na
mão do usuário".
