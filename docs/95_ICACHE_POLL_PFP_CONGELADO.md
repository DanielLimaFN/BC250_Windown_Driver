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

### Diferencial contra as 140 capturas de sucesso

Comparando os 487 campos deste boot com as 140 capturas `PfpDirectStatus=0`, sobraram poucos
discriminadores — e eles são específicos:

| Campo | Nas 140 OK | Neste boot |
|---|---|---|
| `PfpDirectFaultAddrHigh` (JT_STAT empacotado `MEC/CE/PFP`) | `0x00FF0303` | `0x00FE0303` |
| `PfpDirectMePcLast` | `0x49F` / `0x4A0` / `0x4A1` | `0x00000E19` |
| `PfpDirectCePcLast` | `0x41E` / `0x5CB` | `0x0000004D` |

`PFP=0x03` e `CE=0x03` batem com todos os sucessos. **Só o `CP_MEC_JT_STAT` difere: `0xFE`
contra `0xFF` — um bit.** E ME e CE terminam em regiões de código completamente diferentes das
que os sucessos mostravam.

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
