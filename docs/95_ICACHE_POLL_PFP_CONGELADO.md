# 95 — I-Cache do CP: o `0x11` sem poll congela o PFP

> Continuação de [`94_HANDOFF_IA_2026-08-29.md`](94_HANDOFF_IA_2026-08-29.md).
> Escopo: instrumentação e correção do carregador do CP. **Nada aqui está provado em
> hardware ainda** — este documento descreve a mudança e o ensaio que a decide.

---

## 1. O que a evidência de 29/08 realmente diz

Quatro corridas do ensaio depois do handoff, todas com `PfpDirectStatus=14`
(`PM4_NOT_CONSUMED`):

| Hora | PC final | `transitions` | Leitura |
|---|---|---|---|
| 11:49 | `00000000` | 0 | imagem de produção, bring-up desligado — não mede nada |
| 12:26 | `000017F6` | **29** | PFP em **loop ativo** |
| 12:35 | `000017F4` | **1** | PFP **parado** |
| 12:37 | `000017F4` | **1** | PFP **parado** |
| 12:42 | `000017F4` | **1** | PFP **parado** |

O laço de amostragem de [`bc250_pfp_direct_load.c`](../driver/wdk/bc250_pfp_direct_load.c)
roda **100 iterações**, não 4. Portanto `transitions=1` sobre 100 amostras não é "quase
assentou": é o PC **imóvel** durante toda a janela de observação.

**A conclusão que importa**: o patch de I-Cache introduzido entre 12:26 e 12:35 não aproximou
o PFP de `0x284` — ele trocou *loop* por *congelamento*. É um modo de falha diferente, e
provavelmente pior. O ensaio classificava os dois como `FAIL-LOOP` porque olhava só o PC.

## 2. A causa mecânica

O patch escrevia `0x11` nos quatro `CP_*_IC_OP_CNTL` e esperava 50 µs fixos. Conferindo os
campos em [`gc_10_1_0_sh_mask.h:39510`](../inc/linux_ref/gc_10_1_0_sh_mask.h:39510):

| Bit | Campo | Natureza |
|---|---|---|
| 0 | `INVALIDATE_CACHE` | comando |
| 1 | `INVALIDATE_CACHE_COMPLETE` | **status** |
| 4 | `PRIME_ICACHE` | comando |
| 5 | `ICACHE_PRIMED` | **status** |

`0x11` dispara **invalidação e priming ao mesmo tempo**, e o código **nunca lia os bits de
status**. O `amdgpu` faz as duas operações separadas e com poll obrigatório em cada uma.
Destravar o microengine com um preenchimento de I-Cache em voo é exatamente a receita para o
PC parar num endereço arbitrário — que é o que `0x17F4` com `transitions=1` mostra.

Um segundo problema de ordem: a primeira das três invocações acontecia **antes do staging dos
payloads**, ou seja, pedia priming de uma cache cujo conteúdo de origem ainda era o antigo.

## 3. O que mudou

**[`bc250_pfp_direct_load.c`](../driver/wdk/bc250_pfp_direct_load.c)**

- `Bc250PfpIcacheOp()`: aplica a fórmula de bits SOC15 sobre `CP_*_IC_OP_CNTL` e **aguarda o
  bit de conclusão** (até 1000 iterações de 1 µs). Retorna 1 só com confirmação do hardware.
- `Bc250PfpIcacheOpAll()`: aplica aos quatro engines e devolve máscara PFP/ME/CE/CPC.
- Sítio de entrada: **só invalida** (priming antes do staging foi removido).
- Sítio pré-`UNHALT`: invalida com poll, **depois** prima com poll — nessa ordem.
- Sítio de `Restore`: só invalida; os engines estão em HALT.
- `PfpPcSamples` deixou de ser a constante `4u` e passa a reportar a contagem real.

**Telemetria** — três campos novos, para que a próxima corrida separe as hipóteses:

| Campo de registro | Significado |
|---|---|
| `PfpDirectIcInvEntryMask` | invalidação na entrada, máscara dos engines que confirmaram |
| `PfpDirectIcInvUnhaltMask` | invalidação antes do `UNHALT` |
| `PfpDirectIcPrimeUnhaltMask` | priming antes do `UNHALT` |

`0x0F` = os quatro confirmaram. Qualquer coisa menor é evidência direta de cache não tratada.

**[`ensaio-pfp-intermitencia.ps1`](../tools/ensaio-pfp-intermitencia.ps1)**

- Novo veredito `FAIL-FROZEN`, separado de `FAIL-LOOP`: mesmo PC, `transitions <= 1` sobre
  `>= 8` amostras.
- Colunas `trans/amostras` e `IC inv/prime` na tabela e no CSV.
- **Gate de instrumentação**: se `PfpDirectIcInvUnhaltMask` não chegar, o resumo avisa que o
  binário carregado é anterior à instrumentação e que nenhuma conclusão sobre a cache pode
  sair daquela corrida. Aplicação direta da regra do doc 94 § 7.

## 4. Estado de build

`build-contract.bat` passou limpo, sem warnings nos arquivos alterados, com todas as
auditorias em `PASS` — inclusive `BC250 MCDM SAFETY AUDIT: PASS`.

Binário inspecionado, não presumido: a string larga `PfpDirectIcPrimeUnhaltMask` está em
`driver\wdk\output\bc250mcdm.sys` no offset `76216`. A flag pegou.

## 5. O que decide

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

Depois, reinstalar a imagem de laboratório e rodar o ensaio. As três leituras possíveis:

| Resultado | Significado |
|---|---|
| `IC inv/prime` = `AUSENTE` | binário velho; corrida descartada, nada foi medido |
| `inv=15 prime=0` | o hardware recusa o priming — o `0x11` original nunca poderia funcionar |
| `inv=15 prime=15` e PC ainda ≠ `0x284` | a I-Cache está tratada; a causa é outra e o suspeito sai da lista |

## 6. Pendência que não é minha e precisa de decisão

O diff não commitado de
[`bc250_compute_service.c`](../driver/wdk/bc250_compute_service.c) programa
`R_GCVM_CONTEXT0_PT_BASE_LO/HI` e liga `CONTEXT0_CNTL` — a **causa-raiz nº 1 da tela preta**,
que o STATUS § 2.0.1 declarava mitigada justamente por esse registrador nunca ser escrito. O
mesmo diff removeu o re-halt de PFP/ME/CE no teardown do `Bc250ComputeExecutePm4Dispatch`, e
mantém `ExecutionSuccess` com `OR` entre RPTR e fence — o afrouxamento do tripé apontado no
STATUS § 4.

Esse binário **está carregado agora**: o serviço `bc250compute` está `Running` e o arquivo em
`output\bc250compute_pkg.sys` está bloqueado pelo kernel, com SHA-256 idêntico ao
`bc250compute.sys` de 14:35. O código de GCVM só executa sob IOCTL de dispatch — está armado,
não disparado.

---

# Adendo — resultado do ensaio e a causa que ele revelou

Corrida de 2026-08-29 15:01, imagem de laboratório assinada e vinculada, `SkipHwBringup=0`,
bring-up ativo com o dispositivo detendo o display.

## A1. A hipótese da I-Cache está fechada

```text
PfpDirectIcInvEntryMask     0x0000000F
PfpDirectIcInvUnhaltMask    0x0000000F
PfpDirectIcPrimeUnhaltMask  0x0000000F
```

Os quatro microengines confirmaram invalidação **e** priming, e o PFP continua sem assentar em
`0x284`. **A I-Cache não é a causa.** O suspeito sai da lista — e sai com evidência, não com
suposição: o gate de instrumentação teria escrito `AUSENTE` se o binário fosse antigo.

O congelamento, esse sim, acabou: `0x17F4` com 1 transição virou `0x21B` com **39 transições
sobre 100 amostras reais**. O microengine voltou a executar, agora no loop de fetch de
instruções.

Efeito colateral relevante: **o display sobreviveu ao bring-up completo**, com
`ConfigManagerErrorCode=0` e sem tela preta.

## A2. A causa real — o CP chega ligado

O dump de telemetria do boot mostra o que nenhuma linha do ensaio olhava:

```text
PfpDirectMeCntlBefore    0x00000000   <- CP NAO estava em HALT
PfpDirectPcBefore        0x0000021A   <- PFP JA estava executando
PfpDirectMePcBefore/Last 0x00000E19   <- ME estatico
PfpDirectCePcBefore/Last 0x0000004D   <- CE estatico
PfpDirectStagingMismatches        0   <- staging perfeito
PfpDirectFaultAddrLow    0x11111111   <- scratch intacto: o CP nunca executou os pacotes
```

Cruzando com as 177 capturas históricas do corpus
([`corpus-cp-herdado-2026-08-29.csv`](evidence/corpus-cp-herdado-2026-08-29.csv)):

| Condição de entrada | Sucesso | Falha |
|---|---|---|
| `MeCntlBefore = 0x15000000` (PFP\|ME\|CE em HALT) | **140** | 31 |
| `MeCntlBefore = 0x00000000` | **0** | 0 |
| `PcBefore = 0x00000000` | **140** | 37 |
| `PcBefore ≠ 0` | **0** | 0 |

`0x15000000` é exatamente `BC250_PFP_HALT | BC250_ME_HALT | BC250_CE_HALT`. As seis capturas
com `MeCntlBefore = 0` são todas `status=1` (`BAD_ARGUMENT`) com `staged=0` — abortos antes de
o campo ser preenchido, não observações.

**Em 171 observações válidas, o CP chegou em HALT 171 vezes. Em 177, o PFP chegou no PC 0
todas as 177.** O estado de hoje não tem precedente no corpus inteiro.

O direct load pressupõe carregar um CP parado. Quando ele chega rodando, os 65792 dwords vão
para a VRAM sem divergência e **ninguém os busca** — `R_CP_PFP_IC_BASE_LO/HI/CNTL` estão
declarados e nunca escritos, então os engines seguem executando o que o PSP apontou.

## A3. O mecanismo — e ele é nosso

[`bc250_compute_service.c`](../driver/wdk/bc250_compute_service.c) tinha **dois pontos de
UNHALT e nenhum de re-HALT**. O `Bc250ComputeExecutePm4Dispatch` perdeu o re-halt num diff não
commitado, e o `Bc250ComputeExecuteHqdDispatch`, criado depois, nunca teve.

Parar o serviço não desfaz isso: `sc stop` descarrega o driver, não devolve o silício ao
estado anterior. Um único dispatch deixa o CP destravado **até o próximo reboot**, e todo
`StartDevice` seguinte herda um PFP em execução.

Isso também explica a "intermitência" da seção 2.2 do STATUS — seis falhas consecutivas a
partir das 18:17 e recuperação às 21:14, **depois de um reboot**.

## A4. Correções aplicadas

- **Re-HALT restaurado** nos dois teardowns de `bc250_compute_service.c`, com alvo explícito
  `0x15000000` / `0x00000003`. Restaurar um valor capturado propagaria o estado corrompido, que
  é justamente o defeito.
- **Ensaio**: novo veredito `FAIL-INHERITED-ACTIVE`, que domina os demais — se o CP não chegou
  em HALT no PC 0, a corrida não está medindo o carregador. Coluna `ME_CNTL/PC antes` na tabela
  e no CSV, e aviso no resumo mandando reiniciar antes de concluir qualquer coisa.
- **Assinatura nomeada**: `0x218..0x21B` agora é `FAIL-FETCH-LOOP`, distinto do loop alto
  `0x17Fx`.

Build limpo, `BC250 MCDM SAFETY AUDIT: PASS`.

## A5. O ensaio que decide

A predição é falsificável e barata: **reiniciar a máquina e rodar o ensaio sem nunca iniciar o
`bc250compute`.** Se `MeCntlBefore` voltar a `0x15000000` e `PcBefore` a `0`, o latch está
confirmado e o corpus volta a ser comparável.

Enquanto essa corrida não existir, nada sobre o loop `0x21B` é conclusivo — ele foi observado
sob uma condição de entrada que nenhuma das 177 capturas boas tinha.

## A6. Nota operacional

O watchdog reverteu para `BasicDisplay` às 15:04:30, 183 s após o bind: a confirmação não
chegou dentro da janela de 180 s. Sem perda — a telemetria do boot já estava no registro e está
preservada no CSV do corpus. A imagem de laboratório precisa ser revinculada para a próxima
corrida.

---

# Adendo 2 — a corrida limpa, e o gate que nunca poderia passar

Boot limpo às 15:11:11, `bc250compute` nunca iniciado, bind de laboratório às 15:12:09.

## B1. O latch está confirmado

```text
ME_CNTL/PC antes = 15000000/00000000      CP herdado EM EXECUCAO: 0
```

O estado canônico voltou sozinho após o reboot. A predição de A5 se sustentou: o
`MeCntlBefore=0` das 15:01 era resíduo do serviço de compute sem re-HALT, e nada além de um
reboot o limpava. O re-HALT restaurado em A4 fecha o buraco na origem.

## B2. O resultado, agora sem contaminação

```text
status=14  PC=0000021B  trans=47/100  RPTR/WPTR=0/16  fence=43DAFFFE  IC=15/15
veredito: FAIL-FETCH-LOOP
```

Com a I-Cache comprovadamente tratada **e** o CP herdado no estado canônico, o PFP continua
caindo no loop de fetch `0x218..0x21B`. As duas contaminações que mascaravam o diagnóstico
saíram do caminho; **este é o modo de falha real**, medido em condição comparável ao corpus.

### Diferencial contra as capturas de sucesso — e a correção do primeiro diagnóstico

> **Correção.** A primeira leitura desta seção afirmava que `PFP=0x03` e `CE=0x03` batiam com
> todos os sucessos e que só o `CP_MEC_JT_STAT` divergia. **Está errado**, e o erro tem uma
> causa concreta: `PfpDirectFaultAddrHigh` é um campo **sobrecarregado** — recebe o
> empacotamento `CP_STAT/GRBM_STATUS` na fase 4c e depois o empacotamento `JT_STAT` no laço de
> amostragem. Comparar capturas por ele mistura dois significados diferentes.

Pelo layout de [`gc_10_1_0_sh_mask.h:19477`](../inc/linux_ref/gc_10_1_0_sh_mask.h:19477),
`JT_LOADED` tem 2 bits em `CP_PFP_JT_STAT` e `CP_CE_JT_STAT`, e **8 bits** em
`CP_MEC_JT_STAT`. Então `0xFE` significa "7 dos 8 carregados, o bit 0 não".

Distribuição real sobre as 178 capturas:

| `JT_STAT` (MEC/CE/PFP) | OK | FAIL |
|---|---|---|
| `0xFF / 0x03 / 0x03` | **109** | 11 |
| `0x00 / 0x00 / 0x00` | 16 | 11 |
| `0x00 / 0x04 / 0x1E` | 15 | 15 |
| `0xFE / 0x03 / 0x03` | 0 | **1** (este boot, ocorrência única) |

**`CP_MEC_JT_STAT` não prevê o resultado**: há 31 sucessos com `MEC=0x00`. E os dois clusters
com `MEC=0x00` são justamente aqueles em que o campo carrega o *outro* significado — mais um
sintoma da sobrecarga. O `0xFE` continua sendo ocorrência única em 178 capturas, mas com
`n=1` não sustenta conclusão.

### O resultado negativo que importa mais

Restringindo ao cluster homogêneo `MEC=0xFF` — **109 sucessos contra 11 falhas, mesma
assinatura de jump table** — e comparando os 487 campos:

```text
campos que separam os dois grupos: 2
  PfpDirectPhysicalReady   OK=1  FAIL=0
  PfpDirectStatus          OK=0  FAIL=14
```

Ambos são derivados do desfecho. **Nenhum campo do retrato de `StartDevice` prevê a falha.**

Isso confirma o item 4 das pendências do doc 94 — não existe instrumento de runtime — e
converge com a seção 2.2 do STATUS, onde o *mesmo binário* sucede e falha dentro do mesmo
boot. O discriminador é temporal, não é um valor de registrador no fim do processo. Nenhuma
mineração adicional do corpus vai encontrá-lo.

Pista descartada no caminho: `PspRlcLoadStatus=1` (falha de carga do RLC pelo PSP) aparece
**também** entre as capturas de sucesso. Não discrimina.

## B3. O gate de confirmação era impossível, não atrasado

[`confirm-bc250mcdm-display.ps1:37`](../tools/confirm-bc250mcdm-display.ps1:37) exige
`SkipHwBringup=1` — o modo display-only da imagem de **produção**. A imagem de laboratório
existe para rodar com `SkipHwBringup=0`. **Aquele gate rejeita o binding de laboratório
sempre, por construção.**

O rollback das 15:01 não foi confirmação atrasada, como registrei em A6. Foi confirmação
impossível, e aumentar a janela de 180 s para 600 s não resolveria nada. A cadeia de
laboratório (`package-lab` → `bind-lab` → watchdog) apontava para o confirm de produção, que
não fecha com ela.

[`confirm-bc250mcdm-lab.ps1`](../tools/confirm-bc250mcdm-lab.ps1) resolve sem afrouxar
produção: gate próprio, asserção **invertida** no único ponto divergente (`SkipHwBringup=0`,
porque a imagem de laboratório só tem valor se o bring-up rodou), todos os demais gates
preservados, evidência do bring-up gravada no `watchdog-disarm.json`, e cada gate imprimindo o
valor observado antes de falhar.

## B4. Marco: bring-up completo com o display vivo

Confirmação aprovada às 15:17:27, com **todos** os gates passando:

```text
Servico=bc250mcdm  ConfigMgrError=0   SkipHwBringup=0   <- bring-up RODOU
DmmVsyncCount=4    DmmPresentEntered=1  DmmPresentStatus=0
DmmCommitCount=1   DmmIsSupportedCount=20  DmmControlInterruptCount=65
eventos de display/TDR/bugcheck desde o boot: 0
```

O `StartDevice` executou PFP direct load, CP, GCVM e HQD **enquanto o dispositivo detinha o
display**, e a tela continuou funcionando: VSync contando, Present entrando, Commit feito,
zero eventos.

**Isto revisa a seção 6 do STATUS.** A causa-raiz nº 2 — invalidação do Engine 0 — tinha
substituto no ENG17 (commit `7e926c0`), e o substituto está agora demonstrado em silício. O
cenário de tela preta que justificava manter o bring-up fora da imagem de produção **não se
reproduziu**.

Limite honesto: **um boot, uma sessão.** Não é soak, não é reprodução independente, e o CP
continua sem consumir o ring. O que mudou é que "carregar o firmware do CP derruba o display"
deixou de ser verdade observável.

---

# Adendo 3 — o traço do PC, e o que ele aponta

Corrida de 2026-08-29 15:26, imagem de laboratório com a instrumentação de trajetória.

## C1. O instrumento funcionou

```text
traco PC pos-UNHALT : 00000218>0000021B>0000021A>0000021B>00000218>0000021B
JT_STAT PFP/CE/MEC  : 0x03/0x03/0xFE
```

Amostras cruas: `0x21B` domina 12 das 16 leituras, com passagens ocasionais por `0x218` e
`0x21A`. Isso não é código correndo à toa — é **um laço de espera**, e o PFP entra nele
**imediatamente após o UNHALT**, na primeira amostra.

O que o retrato não podia dizer, a trajetória diz: o PFP **nunca passa pela região `0x18xx`**
que os 109 sucessos atravessam a caminho de `0x284`. Não é "chega e volta". É "nunca sai do
começo". E continua lá 100 amostras depois, com o anel ativo e `WPTR=16` — o laço sobrevive à
ativação do ring.

Contexto do corpus: `PcLast=0x21B` aparece **uma única vez** em 178 capturas históricas (uma
falha). Hoje reproduz em todas as corridas.

## C2. A suspeita: a troca do ENG17

Linha do tempo:

| Quando | O quê |
|---|---|
| 2026-08-28 21:14 | **último sucesso do corpus** (`PcLast=0x284`) |
| 2026-08-29 11:16:56 | commit `7e926c0` — invalidação do carregador do CP sai do ENG0 para o ENG17 |
| 2026-08-29 12:26 em diante | **nenhum sucesso**, e surge a assinatura `0x218..0x21B` |

O próprio commit `7e926c0` é explícito sobre o que assumiu e o que não verificou:

- *"Qualquer engine != 0 serve"* — hipótese, não medição.
- *"Se a engine escolhida nao responder, o poll de ACK esgota e o fluxo segue **sem
  invalidacao**"* — modo de falha previsto, aceito, e **nunca instrumentado**.
- *"NAO VALIDADO EM HARDWARE"* — declarado no próprio corpo do commit.

Se o ENG17 não responde, a invalidação é um **no-op silencioso** e o CP nunca enxerga o
firmware recém-escrito. Um PFP que executa ucode obsoleto travando num laço de espera logo no
arranque é exatamente o que o traço mostra.

E a troca teria então apenas movido o problema: o display sobrevive (adendo B4) **porque** o
ENG0 não é mais invalidado, e o CP morre pela mesma razão. O F7 não foi resolvido — foi
trocado de lado.

**Ressalva honesta**: a correlação é confundida. Entre o último sucesso e agora mudaram também
a imagem de laboratório, o patch de I-Cache e o serviço de compute. A correlação sozinha não
prova nada — mas a medição que falta prova, e ela é direta.

## C3. A medição que faltava

Nenhum dos dois sítios de invalidação registrava se a engine respondeu:

- **Sítio do staging** (após escrever o firmware): tinha poll de ACK, e **descartava o
  resultado**. Agora grava `InvalidateAckStaging` e `InvalidatePollsStaging`.
- **Sítio pré-WPTR** (antes de armar o ring): **não tinha poll nenhum** — disparava o REQ e
  seguia em frente. Agora tem poll e grava `InvalidateAckPreWptr` / `InvalidatePollsPreWptr`.
- `InvalidateEngine` registra qual engine rodou, para que a evidência seja auto-contida.

O ensaio imprime a linha em vermelho quando qualquer um dos dois ACKs não vier.

## C4. O que a próxima corrida decide

| `ack staging` / `ack pre-WPTR` | Conclusão |
|---|---|
| `0` em qualquer um | **ENG17 é no-op silencioso.** A invalidação nunca aconteceu, o CP roda sobre firmware obsoleto, e a troca do `7e926c0` precisa de outra engine ou de outro mecanismo de coerência |
| `1` nos dois | O ENG17 responde e a coerência está feita. A hipótese cai, e o laço `0x218` tem outra origem — provavelmente um handshake entre PFP, ME e CE, que estão parados em `0x21B`, `0x0E19` e `0x4D` |

Nos dois casos a resposta é barata e não ambígua. Ao contrário do `JT_STAT`, aqui não há campo
sobrecarregado nem `n=1`.

---

# Adendo 4 — o CP voltou, e a causa era nossa

Corrida de 2026-08-29 18:24, boot limpo às 18:20:46, imagem de laboratório com os dois blocos
de `4df3484` reativados por tunable.

## D1. Três de três

```text
iter  status  PC-final  trans/amostras  RPTR/WPTR   fence-CP   HQD
   1       0  00000284       2/100        15/16     BC250123    1   OK-SETTLED
   2       0  00000284       2/100        15/16     BC250123    1   OK-SETTLED
   3       0  00000284       2/100        15/16     BC250123    1   OK-SETTLED

blocos de 4df3484 : soft reset CP=1  page table VMID0=1 (ack=1)
JT_STAT PFP/CE/MEC : 0x03/0x03/0xFF
```

Primeiros sucessos do CP desde 2026-08-28 21:14. **A bissecção estava certa**: os dois blocos
removidos no trabalho de preservação do display eram o que derrubava o carregador do CP.

O traço mostra a diferença com clareza — o PFP agora percorre o caminho inteiro em vez de
parar no começo:

```text
antes : 00000218>0000021B>0000021A>0000021B ...        (laco de espera, nunca sai)
agora : 00000001>00001BA0>00001BC0>00001C00>00001C40>00001C60>00001CA0>00001CC0>
        00001D00>00000026>0000020A>0000021B>00000260>0000026E  -> assenta em 0x284
```

`0x21B` aparece **de passagem** no caminho bom. Não era destino, era um ponto onde o PFP
ficava preso por falta do que vinha antes. E `CP_MEC_JT_STAT` voltou sozinho de `0xFE` para
`0xFF` — era consequência, não causa, o que fecha em definitivo aquele fio.

## D2. O compute veio junto

```text
HqdActiveStatus     0        HqdActiveTriadPassed  1
HqdActiveRptrAfter  11       HqdActiveFenceAfter   0xBC250123
```

O estágio do HQD só roda quando o direct load retorna OK, então ele estava bloqueado pelo
mesmo motivo. Com o PFP assentado, a fila de compute fecha o próprio gate estrito.

## D3. E não teve tela preta

```text
Status=OK  Service=bc250mcdm  ConfigManagerErrorCode=0
DmmVsyncCount=4  DmmPresentStatus=0
eventos de display/TDR/bugcheck desde o boot: 0
```

**Isto derruba a premissa central da seção 6 do STATUS.** Programar `CONTEXT0_PT_BASE` do
VMID 0 e disparar `GRBM_SOFT_RESET` do CP eram tratadas como as causas da tela preta — e as
duas rodaram três vezes, com o display servido pelo mesmo dispositivo, sem um único evento.

A causa-raiz nº 1 ("invasão do framebuffer") não se sustenta como formulada. O que sobra a
explicar é por que a tela preta acontecia antes, e a resposta mais provável está no que **mais**
mudou desde então — o caminho de display do `771d108`, que hoje reserva e respeita o scanout.

## D4. O que ainda NÃO está fechado

**`RPTR=15` contra `WPTR=16`.** O tripé de `CLAUDE.md` exige `RPTR == WPTR` estrito. O gate do
CP aceita `RPTR >= cursor-1` — o afrouxamento auditado na seção 4 do STATUS — e é só por isso
que o veredito saiu `OK`. Não é ruído de amostragem: o laço roda 100 iterações e o RPTR fica em
15 nas três corridas, de forma determinística.

A fence física está correta (`0xBC250123`, contra o veneno `0x43DAFFFE`) e o PC assentou. Mas
enquanto o RPTR não igualar o WPTR, **a perna do CP Ring 0 não fecha o tripé** e o veredito
correto para ela continua sendo `NÃO VALIDADO EM HARDWARE`. A perna do HQD, essa sim, fecha o
próprio gate estrito.

Limite honesto: uma sessão, três ciclos, sem soak.

## D5. Correção de instrução

Eu disse para conferir `tunables lidos : mascara=0xF`. Errado: a máscara só acende os bits dos
tunables efetivamente passados. Com `-Vmid0PageTable 1 -CpSoftReset 1` o valor correto é
`0xC`, e foi o que saiu.

---

# Adendo 5 — dos dois blocos, só um faz falta

## E1. O ciclo 10 não era o watchdog

A falha reproduziu na iteração 10 com o watchdog comprovadamente desarmado. A explicação do
adendo anterior estava errada.

`PfpDirectStatus=1` também não era falha do carregador: é o **sentinela de inicialização** de
[`bc250_active_miniport.c:2220`](../driver/wdk/bc250_active_miniport.c:2220). O estágio do CP
roda atrás de `SdmaRingWorkStatus == OK` e simplesmente **não foi chamado**.

```text
SdmaRingWorkStatus  = 10 (TIMEOUT)     SdmaRingPolls = 1000 (esgotado)
SdmaRingWptr        = 68               SdmaRingRptrAfter = 0
SdmaRingFenceObserved = 0x43DAFFFE     (veneno intacto)
SdmaRingUtcl1WriteXnack0After = 0x00000001
SdmaRingUtcl1ReadXnack0After  = 0x0045FA00
SdmaRingErrorLogAfter         = 0x0000000F
```

**XNACK é falha de tradução.** O SDMA endereça o próprio anel por VMID 0 com endereço físico
de MC (`0xF40FBB9000`); a page table que reativamos mapeia 2 MiB a partir de `firmwarePa`.
Todo acesso VMID 0 fora dessa janela falha.

## E2. E o bloco culpado não era necessário

```text
blocos de 4df3484 : soft reset CP=1  page table VMID0=0
   1       0  00000284       2/100        15/16     BC250123    1   OK-SETTLED
```

Com o VMID 0 desligado o CP assenta igual, com fence física e o tripé do HQD fechado.
**Dos dois blocos removidos em `4df3484`, só o `GRBM_SOFT_RESET` do CP fazia falta.**

Três consequências:

1. A receita que traz o CP de volta **não toca no VMID 0** — o risco de invasão do framebuffer
   sai de cena por completo.
2. O timeout do SDMA no ciclo 10 tem causa identificada e removível: era colateral do bloco
   desnecessário.
3. A causa-raiz nº 1 da seção 6 do STATUS deixa de ser relevante para este caminho.

O bloco continua no código, atrás do tunable com default 0, com o comentário registrando que
**não deve ser ligado** sem antes resolver a cobertura do VMID 0.

## E3. O que falta medir

Um soak com `-CpSoftReset 1 -Vmid0PageTable 0`. Se o ciclo 10 deixar de falhar, a cadeia
XNACK → timeout do SDMA → CP pulado está confirmada de ponta a ponta.

E o `RPTR=15` contra `WPTR=16` segue de pé: o tripé do CP Ring 0 continua **`NÃO VALIDADO EM
HARDWARE`** por igualdade estrita.

---

# Adendo 6 — 40/40, e a receita final

## F1. O SDMA: éramos nós ligando AUTO_CTXSW

O diferencial sobre o soak de 40 ciclos (37 bons, 3 ruins) devolveu **um único campo
anterior** que separa os grupos sem exceção:

```text
SdmaStatusBefore   bons  = 0x46DEE557  (37/37)
                   ruins = 0x46DEED57  (3/3)
```

Diferença: bit 11 de `SDMA0_STATUS_REG` = **`EX_IDLE_POLL_TIMER_EXPIRE`**
([`gc_10_1_0_sh_mask.h:204`](../inc/linux_ref/gc_10_1_0_sh_mask.h:204)). Todos os outros 29
"separadores" eram consequência a jusante — zerados porque o estágio do CP nem foi chamado.

E [`bc250_sdma_ring_work.c:347`](../driver/wdk/bc250_sdma_ring_work.c:347) **ligava**
`AUTO_CTXSW_ENABLE` antes de armar o anel. O upstream faz o oposto durante o bring-up
(`sdma_v5_0_ctx_switch_enable(adev, false)`), e só religa com o anel de pé. Com o auto context
switch ligado e o timer de idle já expirado, o SDMA troca de contexto sozinho e nunca pega o
anel — `WPTR=68`, `RPTR=0`, 1000 polls esgotados.

**Resultado com `SdmaAutoCtxSw=0`: 40/40.** E a prova causal está no ciclo 35, que herdou
`EX_IDLE_POLL_TIMER_EXPIRE=1` e **passou** (`SdmaWorkStatus=0`, CP em `0x284`, fence
`0xBC250123`, tripé do HQD fechado). A mesma condição que falhava 3/3 virou sobrevivível.

```text
CntlBefore 0x000000C2 -> CntlApplied 0x000000E2    (AUTO_CTXSW nao setado)
```

## F2. A receita completa

| Tunable | Valor | Por quê |
|---|---|---|
| `CpSoftReset` | **1** | único dos dois blocos de `4df3484` que faz falta |
| `Vmid0PageTable` | **0** | desnecessário, e não toca no VMID 0 do display |
| `SdmaAutoCtxSw` | **0** | alinha com o upstream; elimina a intermitência do SDMA |

Com ela: **40/40 ciclos**, CP em `0x284`, fence física, tripé do HQD fechado em 40/40, display
servido pelo mesmo dispositivo o tempo todo.

## F3. O `RPTR=15` contra `WPTR=16` tinha causa exata

O anel são três `WRITE_DATA` de 5 dwords (15) mais um dword de padding, e o padding era
`0xC0001000` — um **PACKET3 NOP**. Em cabeçalho type-3 o campo `COUNT` (bits 29:16) vale
`n-2`, então `COUNT=0` declara um pacote de **dois** dwords. Escrevíamos um só.

O CP se recusa — corretamente — a consumir um pacote incompleto, e estaciona o RPTR no dword
anterior. Não era característica do hardware nem ruído de amostragem: era um pacote malformado
por nós, determinístico em 40/40 ciclos.

Corrigido para `0x80000000` (**PACKET2**, o NOP de um dword). Se o RPTR chegar a 16, o tripé
do CP Ring 0 fecha por igualdade estrita pela primeira vez.

---

# Adendo 7 — o RPTR fecha, e o que isso vale

## G1. PACKET2 resolveu

```text
RPTR == WPTR estrito : 40
status=0 : 38    RPTR/WPTR : 16/16 (38/38)    fence : 0xBC250123 (38/38)
HQD tripé fechado : 38/38
```

Trocar o padding de `0xC0001000` (PACKET3 NOP, que declara dois dwords) por `0x80000000`
(PACKET2, um dword) levou o RPTR de 15 para 16 em **todos** os ciclos. O afrouxamento
`RPTR >= cursor-1` do gate do CP nunca foi característica do silício: era um pacote malformado
por nós.

## G2. O `FAIL-LOOP !DIVERGE` era do ensaio, não do hardware

Com o anel sendo consumido, o laço de amostragem sai na **segunda** iteração
(`PfpPcSamples=2`) em vez de esgotar 100. O `PcLast` deixou de ser o PC de repouso e passou a
ser apenas onde o PFP estava passando — daí `0x17F4/0x17F5/0x17F6`, os mesmos valores que o
corpus associava a falha.

**A regra "PC final prevê o resultado, 110/110" era válida só enquanto o laço esgotava as
amostras.** Corrigido: veredito `OK-CONSUMIU-CEDO` quando `PfpPcSamples < 8`, e a coerência
passa a ser medida contra `OK-*` em vez de `OK-SETTLED`.

## G3. O que fecha e o que não

**Fecha, 38/38**: `RPTR == WPTR == 16` estrito, e fence gravada pela GPU em VRAM
(`0x43DAFFFE` → `0xBC250123`, com verificação de veneno). Com o display servido pelo mesmo
dispositivo.

**Não fecha**: a terceira perna do tripé de `CLAUDE.md` — *dados destino conferidos byte a
byte*. O ensaio do CP Ring 0 confere a fence e o `SCRATCH_REG0`, não um buffer de destino
comparado com gabarito. Quem faz comparação byte a byte é o caminho do SDMA
(`ComparedDwords`/`MismatchCount`) e o do compute. **Enquanto o CP Ring 0 não tiver essa
conferência, o veredito honesto continua sendo `NÃO VALIDADO EM HARDWARE` pelo critério
completo** — mas agora por um motivo diferente e muito menor do que "o CP não consome o anel".

## G4. O SDMA não está resolvido

`SdmaAutoCtxSw=0` levou de 3/40 para 2/40, e os dois ciclos que falharam (7 e 21) herdaram
`EX_IDLE_POLL_TIMER_EXPIRE=1`. O ciclo 40 herdou o mesmo latch e **passou**.

Com n=3 latches e 2 falhas, não dá para afirmar que o `AUTO_CTXSW` era a causa — a corrida
anterior deu 40/40 com 1 latch, esta deu 38/40 com 3. A correlação latch→falha se mantém
(2 de 3), o `AUTO_CTXSW` continua sendo a mudança certa por alinhamento com o upstream, e a
origem do latch segue em aberto.

---

# Adendo 8 — o tripé do CP Ring 0 fechou

```text
CP TRIPE COMPLETO : 3   (RPTR==WPTR + fence + dados byte a byte)

status=0   RPTR/WPTR = 36/36   fence = 0xBC250123 (veneno 0x43DAFFFE)
dados conferidos : 4 dwords, 0 divergencia(s), 0 polls, dest[0]=BC250000
HQD tripé fechado : 3/3        display servido por bc250mcdm o tempo todo
```

As três pernas do tripé de `CLAUDE.md`, no mesmo ciclo, três vezes:

1. **`RPTR == WPTR == 36`** — igualdade estrita, sem o `>= cursor-1`
2. **Fence gravada pela GPU em VRAM**, com veneno verificado
3. **Dados conferidos byte a byte** contra gabarito, num destino envenenado antes

`0 polls` importa: os dados já estavam corretos na primeira leitura, então não houve
corrida de tempo mascarada por espera.

## H1. Era ordem no anel

O experimento que separou as hipóteses foi mover os quatro `WRITE_DATA` do destino para o
**início** do anel. Antes, com eles no fim, o destino ficava com o veneno mesmo com o anel
inteiro consumido; agora gravam, e a fence continua gravando.

**Hipótese principal, não confirmada**: o pacote nº 3 escreve uma fence via **GPUVA**
(`gpuvaFence = 0x106200` tratado como endereço virtual). Com `Vmid0PageTable=0` o VMID 0 não
tem page table nossa, então esse pacote provavelmente falha a tradução e derruba a execução
do que vem depois — o RPTR continua avançando porque o CP *busca* o anel inteiro, mas para de
*executar*. Com o destino antes dele, nada do que importa fica na sombra.

Isso também explica por que a fence via MC (pacote nº 2) sempre funcionou: vinha antes.

**Próximo passo natural**: remover ou condicionar o pacote de fence via GPUVA. Ele só faz
sentido quando `Vmid0PageTable=1`, e esse bloco está comprovadamente desnecessário.

## H2. O gate agora é o do CLAUDE.md

```c
    status = (Evidence->Rb0RptrAfter == cursor &&
              Evidence->Rb0WptrApplied == cursor &&
              Evidence->Rb0MismatchCount == 0u &&
              Evidence->Rb0FenceAfter == BC250_CP_RB0_FENCE_VALUE) ? OK : PM4_NOT_CONSUMED;
```

Sem `>= cursor-1`, sem `|| scratch`, com conferência de dados. O desvio auditado na seção 4
do STATUS está **corrigido**, não homologado.

## H3. Limite honesto

Três ciclos. Não é soak. A intermitência do SDMA (2/40 na última corrida longa) continua
aberta e bloqueia o estágio do CP quando ocorre. E `PfpDirectStatus=0` prova o tripé do anel
de teste — **não** prova dispatch de shader, que segue em 0%.

---

# Adendo 9 — soak de 40 e a hipótese da ordem confirmada

```text
CP TRIPE COMPLETO : 39 / 40      RPTR/WPTR = 31/31      fence = 0xBC250123
dados conferidos  : 4 dwords, 0 divergencia(s), 0 polls, dest[0]=BC250000
HQD tripé fechado : 39           Falhas do SDMA : 1 (ciclo 39)
```

O único ciclo que não fechou foi o 39, e por falha do SDMA — que bloqueia o estágio do CP.
**Dos 39 ciclos em que o CP chegou a rodar, 39 fecharam o tripé completo.**

## I1. A hipótese da ordem está confirmada

Removido o pacote de fence via GPUVA (agora condicionado a `Vmid0PageTable != 0`), o anel caiu
de 36 para **31 dwords** — exatamente o previsto — e o destino passou a ser escrito **sem
depender de estar no início**. Isso fecha o adendo 8: o pacote que endereça um GPUVA sem page
table falha a tradução e interrompe a execução do restante do anel, embora o RPTR continue
avançando porque o CP busca tudo.

Consequência prática: emitir um pacote que falha por desenho não é neutro — ele derruba
silenciosamente tudo que vier depois no mesmo anel.

## I2. O latch do SDMA é condição NECESSÁRIA

Somando os ciclos com telemetria de latch das corridas de 29/08:

```text
ciclos com telemetria     : 129
falhas do SDMA            :   3
ciclos com EX_IDLE herdado:   5
  desses, falharam        :   3
falhas SEM o latch        :   0
```

**Nenhuma falha ocorreu sem `EX_IDLE_POLL_TIMER_EXPIRE` aceso na entrada**, e 3 das 5
ocorrências do latch resultaram em falha. É condição **necessária, não suficiente** — o que
torna a origem do latch o alvo certo, e dá um preditor barato para um gate de preflight.

## I3. Estado ao fim do dia

| Perna | Situação |
|---|---|
| CP Ring 0 — `RPTR == WPTR` estrito | ✅ 40/40 |
| CP Ring 0 — fence física com veneno | ✅ 39/39 |
| CP Ring 0 — dados byte a byte | ✅ 39/39 |
| HQD — gate estrito próprio | ✅ 39/39 |
| Display vivo durante tudo | ✅ |
| SDMA — intermitência | ❌ 3 falhas em 129 ciclos, todas com o latch |
| Dispatch de shader | ❌ 0%, nenhum kernel executado |

Receita: `CpSoftReset=1`, `Vmid0PageTable=0`, `SdmaAutoCtxSw=0`.
