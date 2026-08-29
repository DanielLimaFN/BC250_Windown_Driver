# BC-250 Windows Driver — Status Consolidado e Roadmap

> **Revisão 3.1 — 2026-08-29.** Consolida: validação física do CP PM4, do compute MEC/HQD e do
> caminho de display WDDM, comparação com o projeto de referência `AMD-BC-250-Windows-Driver`
> e o roadmap de fases. Os percentuais estão separados em dois eixos (implementado × provado),
> os números vêm de auditoria direta do corpus em `docs/evidence/` cruzada com o código-fonte.
>
> **Duas mudanças de status nesta revisão**: (1) o compute MEC/HQD **fechou o tripé físico 26
> vezes** — não é mais 0% provado (seção 3.4); (2) foi descoberto um **modo de falha
> intermitente do PFP** que hoje é o gargalo único do projeto (seção 2).
>
> Não substitui os documentos detalhados em `docs/` — é o ponto de entrada rápido para retomar.

---

## 1. Status Geral Consolidado

Duas colunas, propositalmente. **Implementado** mede código escrito e funcional em bancada.
**Provado** mede evidência física fechada segundo o tripé de `CLAUDE.md`. Misturar os dois numa
barra só cria a ilusão de que algo está "quase lá" quando a prova está em zero.

```text
======================================== STATUS DO DRIVER BC-250 ========================================
Área                              Implementado           Provado (tripé)        Escopo real da prova
---------------------------------------------------------------------------------------------------------
Base Geral Pré-WDDM (Lab)         ██████████████████▋░ 93%  █████████████████▋░░ 88%  Bancada isolada
SDMA0 (engine + display vivo)     ███████████████████░ 95%  ███████████████████░ 95%  Tripe COM display ativo (2.0)
GCVM/VMID8 (mecanismo MMIO)       ██████████████████░░ 90%  ██████████████████░░ 90%  Invalidate/ACK + PTE rollback
GCVM sob VidMm (F3, produção)     ███████░░░░░░░░░░░░░ 35%  ░░░░░░░░░░░░░░░░░░░░  0%  Sem paging sob pressão
CP / PM4 Ring 0 (bancada)         █████████████████░░░ 85%  ███████████████░░░░░ 75%  110/167; falha intermitente
RLC (fora do StartDevice)         ████████░░░░░░░░░░░░ 40%  ░░░░░░░░░░░░░░░░░░░░  0%  Proibido no arranque normal
Telemetria & Observabilidade      ███████████████░░░░░ 75%  ████████████░░░░░░░░ 60%  Gate visual aprovado
Compute MEC / HQD (PM4 na fila)   ████████████████░░░░ 80%  ██████████████░░░░░░ 70%  Tripé fechado 26x (seção 3.4)
Compute — dispatch de shader      ██░░░░░░░░░░░░░░░░░░ 10%  ░░░░░░░░░░░░░░░░░░░░  0%  Nenhum kernel executado
Display WDDM (candidato 3.24)     █████████████████░░░ 85%  ████████░░░░░░░░░░░░ 40%  Smoke test, 1 sessão só
IH / Monitored Fences / TDR       ░░░░░░░░░░░░░░░░░░░░  0%  ░░░░░░░░░░░░░░░░░░░░  0%  Não iniciado
=========================================================================================================
Veredito Global: NÃO VALIDADO EM HARDWARE (produção)
  Provados em bancada: SDMA, CP Ring 0, GCVM/VMID8 e a fila de compute MEC/HQD (tripé, 26x)
  CONVERGENCIA PARCIAL: SDMA fecha o tripé COM o display vivo, sem tela preta (seção 2.0)
  Bloqueio atual: carregar o firmware do CP sem invalidar o Engine 0
  Não provados: dispatch de shader, CP com display vivo, IH/TDR, robustez do 3.24
=========================================================================================================
```

**Regra de ouro do projeto (`CLAUDE.md`)**: nada é declarado validado sem o tripé físico —
`RPTR == WPTR`, fence gravada pela GPU em VRAM (com verificação de poison), e dados destino
conferidos byte a byte. Zero fakes, zero fallback por software, zero `STATUS_SUCCESS` sem
backend funcional. **Ver seção 4: o gate do CP hoje opera com um afrouxamento desse critério,
e isso precisa ser homologado ou corrigido.**

---

## 2. Próxima ação: caracterizar a intermitência do PFP/CP

> **Esta seção substitui a recomendação anterior (soak do display).** A auditoria do corpus
> revelou uma falha intermitente no PFP que é pré-requisito de tudo o mais — inclusive do
> compute, que **já fecha o tripé** quando o PFP sobe limpo (seção 3.4).

### 2.0 CONVERGÊNCIA PARCIAL DEMONSTRADA — SDMA com o display vivo (2026-08-29)

> **Isto revisa a premissa da seção 6.** Display e compute **não são** globalmente
> incompatíveis. O SDMA já convive com o display servido pelo `bc250mcdm`.

Ensaio executado em 2026-08-29, com `bc250mcdm` em Código 0 servindo o display
(`SkipHwBringup=1`) e o serviço de kernel `bc250compute` carregado simultaneamente.
Ferramenta: `tools\monitor\bc250_silicon_runner.exe` (IOCTL sobre `\\.\Bc250Compute`).

**TESTE 1 — SDMA0: tripé completo, com o monitor ligado**

```text
UCode Checksum : 0xAB6305C6 (valido)
WPTR Ring      : 0 -> 68
RPTR Ring      : 0 -> 68          <- RPTR == WPTR ESTRITO
Fence Fisica   : 0xBC250123       <- gravada pela GPU em VRAM
Bytes Copiados : 16 bytes em 0.095 ms, integridade conferida
```

Estado do display **depois** do ensaio: `servico=bc250mcdm`, `ConfigManagerErrorCode=0`,
`DmmPresentStatus=0`, `DmmCommitStatus=0`, **zero eventos de display/TDR desde o boot**.
A tela não apagou.

**Por que funcionou**: o caminho do `bc250compute` **não invalida o Engine 0** — não há
nenhuma ocorrência de `GCVM_INVALIDATE` em `bc250_compute_service.c`. A causa-raiz nº 2 da
seção 6 está ausente desse caminho. Note também que ele **escreve em `R_RLC_CNTL`**
([`bc250_compute_service.c:713`](driver/wdk/bc250_compute_service.c:713)) e ainda assim o
display sobreviveu — a proibição da causa-raiz nº 3 vale para o `StartDevice`, não para
escrita em runtime.

**TESTE 2 — CP PM4: não consumiu**

```text
WPTR Ring : 0 -> 16
RPTR Ring : 0 -> 0          <- ring nao consumido
Fence EOP : 0x43DAFFFE      <- permaneceu envenenada
Tempo     : 198.8 ms (polling esgotado)
```

**Cuidado ao interpretar**: essa assinatura (`RPTR=0/16`, fence envenenada) é idêntica à do
modo de falha do PFP da seção 2.1 — **mas quase certamente não é a mesma causa**. Com
`SkipHwBringup=1` o `StartDevice` retorna cedo e o **PFP direct load nunca rodou neste boot**:
o CP está sem ucode. Ring sem firmware não consome pacotes. O SDMA funcionou justamente por
ter ucode próprio válido (`checksum=0xAB6305C6`).

Atribuir essa falha à intermitência seria erro de diagnóstico. São hipóteses distintas e o
ensaio ainda não as separa.

**Consequência para o F7 — o problema é mais específico do que o documento dizia**

Não é "display e compute não convergem". É: **o caminho que carrega o firmware do CP (PFP
direct load) é que derruba o display** — porque é ele que invalida o ENG0 e escreve em
aperturas proibidas durante o arranque. O compute em si já convive.

F7 se reduz a um problema bem menor: **carregar o firmware do CP sem invalidar o Engine 0**.
O SDMA é a prova de existência de que a coexistência é possível.

### 2.1 A assinatura é binária e determinística

Correlacionando `PFP physical:` com `CP PM4 ring:` nas 167 capturas:

| PC final do PFP | `transitions` | Resultado do CP | Ocorrências |
|---|---|---|---|
| `0x00000284` | 2 | **OK** | **110 / 110 — sem exceção** |
| `0x0000041E` | 1 | FAIL | 44 (modo antigo, 27/08, já superado) |
| `≈ 0x000017F5 / 0x000017F6` | **26 a 48** | FAIL | 7 (**intermitência nova, 28/08 18h+**) |
| `0x00000000` | 0 | FAIL | 3 (PFP nunca saiu do reset) |

**O PC final do PFP prevê o resultado do CP com 100% de acurácia no corpus inteiro.** Isso é um
discriminador barato e confiável — pode virar gate de preflight imediatamente.

### 2.2 O que a assinatura nova significa

O modo de falha de 28/08 **não é travamento**: `transitions=26..48` contra `transitions=2` nos
sucessos significa que o PFP está **executando em loop ativo** numa região alta do ucode
(`0x17F5`) em vez de assentar em `0x284`. Ele nunca chega a consumir o Ring 0 — daí
`RPTR=0->0` e `status=14` (`PM4_NOT_CONSUMED`).

Comparação direta de duas execuções do **mesmo binário**, com mesmo `version=94`, mesmo
`mc=000000F40FAAE000`, mesmo `staged=65792dw/0mismatch` e mesmo `JT_STAT=FF0303`:

```text
17:46  PC=00000000->000018EF->00000284  transitions=2/4   -> status=0   RPTR/WPTR=15/16  fence OK
18:17  PC=00000000->000017F6->000017F5  transitions=26/4  -> status=14  RPTR/WPTR=0/0    fence envenenada
```

Não é regressão de código. É **estado residual da GPU dentro do mesmo boot**: seis falhas
consecutivas a partir de 18:17, e recuperação às 21:14 (após reboot).

### 2.3 Estado atual da máquina — o ensaio exige reconfiguração

Preflight executado em 2026-08-29 (`tools\ensaio-pfp-intermitencia.ps1`, modo dry-run):

```text
Dispositivo    : PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\4&19CAF403&0&0041
Servico        : bc250mcdm      ConfigMgrError : 0      SchemaVersion : 96
SkipHwBringup  : 1
PFP            : telemetria nao populada nesta sessao
```

**A máquina está em modo display-only.** `SkipHwBringup=1` vem de dois lugares, ambos com
default ligado: [`bc250mcdm.inf:51`](driver/wdk/package/bc250mcdm.inf:51)
(`HKR,Parameters,SkipHwBringup,0x00010001,1`) e o próprio código,
`Bc250ActiveReadTunable(L"SkipHwBringup", 1)`.

E a exclusão entre display e compute está implementada numa linha só —
[`bc250_active_miniport.c:2364-2370`](driver/wdk/bc250_active_miniport.c:2364): quando
`skipHw != 0`, o `StartDevice` **retorna cedo** publicando apenas
`NumberOfVideoPresentSources=1`, sem tocar em PFP, CP ou GCVM. É por isso que o 3.24 dá
Código 0 sem tela preta, e é por isso que a telemetria do PFP está vazia neste boot.

> **Consequência operacional**: rodar o ensaio F4d exige `SkipHwBringup=0` **no mesmo
> dispositivo que hoje serve o display**. Isso religa o bring-up que invalida o Engine 0
> (seção 6) — ou seja, é exatamente o cenário de tela preta, com o watchdog de rollback como
> rede. **Decisão de bancada, não automatizável.**

### 2.4 Ensaio a executar (após a reconfiguração)

1. **N execuções consecutivas sem reboot**, registrando em qual iteração o PC final migra de
   `0x284` para `0x17F5`. Confirma ou refuta a hipótese de acumulação.
2. **Reboot e repetir** — confirma que o reboot é o que recupera.
3. **Bissecção do estado residual**: o `Restore:` de
   [`bc250_pfp_direct_load.c:967-1030`](driver/wdk/bc250_pfp_direct_load.c:967) devolve um
   conjunto grande de registradores, mas **não** cobre o estado interno do PFP/icache nem o
   TMR do PSP. Suspeitos primários, nessa ordem: icache do PFP não invalidado entre cargas;
   TMR/PSP não reciclado (o `DESTROY` prossegue mesmo sem ACK após 500 polls,
   [linha 972](driver/wdk/bc250_pfp_direct_load.c:972)); `R_CP_ME_CNTL` restaurado para um
   valor já contaminado pela execução anterior.
4. **Gate de preflight barato**: abortar antes de submeter se o PC final do PFP não for
   `0x284` — transforma uma falha silenciosa em recusa explícita, alinhado ao fail-closed.

> **Nota sobre GFXOFF**: a regra do doc 92 manda envolver toda interação com anéis em
> `DisallowGfxOff/AllowGfxOff` (mailbox `0x15`/`0x14`). O caminho do PFP/CP **não faz isso** —
> não há nenhuma ocorrência de GFXOFF em `bc250_pfp_direct_load.c`. O caminho de compute
> declara `GfxOffApplicable=0` com justificativa documentada
> ([`bc250_active_miniport.c:565-573`](driver/wdk/bc250_active_miniport.c:565): SMU 11.8 do
> Cyan Skillfish2 não expõe `AllowGfxOff`/`DisallowGfxOff` no ABI oficial). **Doc 92 e código
> estão em desacordo** — resolver antes de culpar GFXOFF pela intermitência.

### 2.5 Em paralelo: robustez do display 3.24 (F4c) — executável AGORA

A máquina já está exatamente na configuração que o F4c precisa (display-only, Código 0), e o
ensaio é **read-only**. Ferramenta: `tools\soak-bc250mcdm-display.ps1`.

| Ensaio | Estado | Por que importa |
|---|---|---|
| Soak prolongado (>= 4 h com desktop ativo) | 🟡 instrumento validado, 2 min sem regressão | Smoke test não é estabilidade |
| Reboot do próprio 3.24 | ⬜ | Sem isso, "sobreviveu a reboot" é herança do 3.23 |
| Suspend/resume (S3/S0ix) | ⬜ | Caminho onde o scanout costuma morrer silenciosamente |
| Mudança de modo em runtime | ⬜ | `CommitVidPn` só foi exercitado no modo nativo |
| Watchdog de rollback ainda armado | ⬜ | Confirmar que a reversão para `BasicDisplay` continua viva |

### 2.6 ACHADO P0 — bugcheck `0x124` encerrou a sessão mais longa com o driver

Levantado em 2026-08-29 ao tentar creditar tempo de uptime já decorrido para o F4c.
**A sessão mais longa com o driver carregado não terminou limpa — terminou em erro fatal de
hardware.**

Histórico de boots (`Kernel-General` 12/13) e desligamentos sujos (`Kernel-Power` 41):

| Janela | Duração | Fim |
|---|---|---|
| 28/08 23:41 → 29/08 ~08:35 | **~8,9 h** (driver gravado 23:50) | **`Kernel-Power` 41, BugcheckCode 292 = `0x124`** |
| 29/08 08:35 → agora | 1,8 h | em curso, limpa |

Detalhes do evento (`29/08/2026 08:35:23`):

```text
BugcheckCode         = 292  (0x124  WHEA_UNCORRECTABLE_ERROR)
BugcheckParameter1   = 0x10
BugcheckInfoFromEFI  = true
PowerButtonTimestamp = 0
LongPowerButtonPressDetected = false
```

**Por que isso importa:**

1. **É inédito na janela de 7 dias.** Há 54 registros de `Kernel-Power` 41 em 7 dias, mas
   51 deles têm `BugcheckCode=0` — reset físico/queda de energia, coerentes com o fluxo de
   desenvolvimento de dar hard reset após tela preta. Os outros são três `0xC4`
   (`DRIVER_VERIFIER_DETECTED_VIOLATION`) de 22/08, já registrados em `docs/evidence/`.
   **O `0x124` aparece uma única vez, e é o mais recente.**
2. **Não há dump.** `BugcheckInfoFromEFI=true` indica que o registro veio do firmware; o
   único `MEMORY.DMP` na máquina é de 22/08. Sem dump, não há análise de causa raiz.
3. **Não houve aviso no nível de display.** A varredura da janela 23:41→08:35 não achou
   nenhum evento de `Display`/`dxgkrnl`/TDR. Foi de saudável a fatal sem degradação visível.
4. **`WHEA_UNCORRECTABLE_ERROR` é erro de hardware**, não de software — MCE de CPU, erro
   fatal de PCIe, ou similar. **Não é atribuível ao driver com a evidência disponível**, e
   seria desonesto afirmar que é. Mas ocorreu ao fim da sessão mais longa com o driver
   carregado, num alvo que é uma placa de mineração em plataforma modificada.

**Consequência direta para o F4c**: não é possível creditar as ~8,9 h como soak aprovado.
A janela terminou em erro fatal. O critério de soak passa a exigir **também** ausência de
`Kernel-Power` 41 com `BugcheckCode != 0` na janela.

**Ações**: (a) verificar se o `0x124` reincide — só a repetição distingue evento isolado de
padrão; (b) conferir se a captura de dump completo está armada, para que uma reincidência
renda material de análise; (c) não tratar como bug do driver até haver evidência que ligue
os dois.

### 2.7 Descoberta: não existe instrumento de runtime no driver

Verificado empiricamente em 2026-08-29: amostrando os **100 campos** de
`Parameters\Probe` com 20 s de intervalo e o driver ativo, **nenhum campo muda**. A telemetria
inteira é **retrato de `StartDevice`**, não monitor de runtime.

Em particular, **`DmmVsyncCount` não é contador vivo**. É um latch de prova única, em
[`bc250_display_ddi.c`](driver/wdk/bc250_display_ddi.c:32),
`Bc250RecordVsyncProofAfterSuccessfulPresent`:

```c
InterlockedCompareExchange(&g_Bc250DisplayState.VsyncProofRecorded, 1, 0) == 0
```

Depois do primeiro Present bem-sucedido o valor congela. O `DmmVsyncCount=7` citado na seção
3.5 continua sendo **prova válida de que houve VSync real seguido de Present** — mas **não**
prova continuidade, e ler "delta 0" nesse campo é comportamento correto, não regressão.

**Impacto**: o soak F4c consegue provar **ausência de regressão** (Código 0 mantido, sem
rollback para `BasicDisplay`, sem evento de display/TDR no Event Log), mas **não** consegue
provar que o VSync continua sendo entregue. Fechar essa lacuna exige um contador vivo no
driver — código novo, e pré-requisito natural de F5b (monitored fences).

---

## 3. O que está de fato provado no silício

### 3.1 CP Ring 0 (PFP/ME/CE) — PM4 real executado pelo hardware

Local: [`driver/wdk/bc250_pfp_direct_load.c:893-965`](driver/wdk/bc250_pfp_direct_load.c:893)

- `RPTR` lido via MMIO real (`R_CP_RB0_RPTR`) — polling puro sobre registrador de hardware,
  nunca incrementado por CPU.
- Fence pré-envenenada com `0x43DAFFFE` em VRAM, virando `0xBC250123` — valor gravado pelo
  pacote PM4 `WRITE_DATA` executado pelo próprio CP.
- **Contagem real do corpus** (`docs/evidence/`), recontada nesta revisão: **167 capturas de
  `CP PM4 ring:`**, das quais **110 trazem a assinatura de sucesso canônica**
  `active=0->1->0 RPTR/WPTR=0->15/16 fence=43DAFFFE->BC250123`.

  **A taxa agregada (65,9%) é enganosa** — ela mistura o bring-up com o candidato homologado.
  A distribuição temporal separa três regimes distintos:

  | Janela | ok | fail | Leitura |
  |---|---|---|---|
  | 27/08 10h–18h | 1 | 47 | Bring-up. Falha esperada. `PC=0x41E` |
  | 27/08 19h → 28/08 17h | ~101 | 1 | **Candidato homologado — ~99% de sucesso** |
  | 28/08 18h–21h | 3 | 6 | **Intermitência nova no mesmo binário** (seção 2) |

  > A revisão 1 dizia "~150 capturas idênticas"; a revisão 2 corrigiu para "110/167 = 65,9%".
  > Ambas estavam mal enquadradas. O correto: **o candidato homologado acerta ~99%**, e existe
  > um **regime de falha intermitente separado**, aberto nas últimas horas de trabalho.

- Candidato homologado: **1.83.0.0 / SchemaVersion 90**
  (doc [88_HANDOFF_IA_CP_HOMOLOGADO_MEC_NEXT.md](docs/88_HANDOFF_IA_CP_HOMOLOGADO_MEC_NEXT.md)).

**Limite importante**: isso prova o Command Processor / Ring 0. **Não prova compute**
(MEC/HQD/dispatch de shader) — engines independentes. O próprio log de auditoria termina com:
`CP TRIADE & PM4 RING 0 ENSAIADOS; COMPUTE NAO EXECUTADO; NAO VALIDADO EM HARDWARE`.

### 3.2 SDMA0 — cópia física provada

Assinatura de sucesso no corpus:
`SDMA ring: status=0 RPTR-mmio/wb/WPTR=68/52/68 fence=43DAFFFE->BC250123 data=4dw/0mismatch`

- `RPTR` **MMIO** alcança `WPTR` (68/68); o `RPTR` de **writeback** fica em 52. Ou seja:
  `RPTR == WPTR` vale para o registrador, não para o writeback. Registrar isso é importante
  porque o tripé não diz qual das duas fontes conta.
- Fence física `0x43DAFFFE -> 0xBC250123`, 4 DWORDs copiados, `0mismatch`, rollback íntegro.
- Marco: candidato 1.28.0.0
  (doc [53_SDMA_GATE_RPTR_MMIO_DADOS_1.28.0.0.md](docs/53_SDMA_GATE_RPTR_MMIO_DADOS_1.28.0.0.md)).

### 3.3 GCVM / TLB / VMID 8

Invalidate/ACK provado, PTE escrita/relida/restaurada, rollback físico da transação medida
(doc [41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md](docs/41_GCVM_BACKING_OWNED_VMID8_INERTE_1.16.0.0.md)).

> **Nota sobre a divergência de percentual**: a barra marca GCVM em 90% e a fase F3 em 35%.
> Não é contradição — são métricas diferentes. **90%** é o *mecanismo MMIO* (invalidate, ACK,
> escrita de PTE, rollback). **35%** é a *integração com VidMm/GPUVA* sob gerenciamento real de
> memória e pressão de paging, que não foi iniciada. A barra agora lista as duas linhas
> separadamente para eliminar a leitura ambígua.

### 3.4 Compute MEC/HQD — o tripé FOI fechado (mudança de status)

> **Correção de status.** A revisão 1 e a revisão 2 desta consolidação afirmavam
> *"Compute: 0% provado, `status != 0`"*, herdando o doc 89
> ([89_HANDOFF_HQD_MEC_2026-08-28.md](docs/89_HANDOFF_HQD_MEC_2026-08-28.md)). **O corpus
> contradiz isso.** O doc 89 é anterior às capturas de sucesso.

**26 capturas** trazem o tripé de compute fechado, a partir de 2026-08-28 15h — exatamente a
janela do commit `4df3484 "100% silicio"` (28/08 15:43):

```text
HQD active: status=0 selector=00000008 applied=1->0 RPTR/WPTR=0->11/11
            fence=43DAFFFE->BC250123 db=1:272:11 triad=1
HQD live bank:  RPTR/WPTR=11/11  HQ_STATUS0/HQD_ERROR=40008040/00000000
HQD coherency:  HDP=1 L2=...->00000003 REQ=00F80001/00F80001 ACK=00000100->00000001 passed=1
HQD faults:     todos 00000000
```

Distribuição: 8 capturas às 15h, 6 às 16h, 9 às 17h, 2 às 18h, 1 às 21h.

**O gate de compute é mais rigoroso que o do CP.** Em
[`bc250_compute_mqd_contract.c:2244-2247`](driver/core/bc250_compute_mqd_contract.c:2244) o
`TriadPassed` exige as três condições **em AND**, sem escape hatch:

```text
rptrPoll == submittedDwords    <- RPTR ESTRITO == WPTR (11/11), nao "-1"
scratchPoll == 0x22222222      <- marcador MMIO escrito por PM4
fencePoll  == 0xBC250123       <- fence em VRAM, pre-envenenada com 43DAFFFE
```

Compare com a seção 4: o gate do CP aceita `RPTR >= cursor-1` e usa **OR** entre fence e
scratch. **O caminho de compute já implementa o que a seção 4 pede para o caminho do CP** — a
correção das pendências 2/3/4 tem implementação de referência dentro do próprio repositório.

**Limite honesto — o que isso NÃO é:** prova de que a fila de compute (MEC/HQD/MQD) aceita
submissão via doorbell, executa PM4 e grava fence em VRAM. **Não é dispatch de compute shader.**
Não há kernel executado nem buffer de resultado conferido byte a byte contra gabarito — o
terceiro pé do tripé de `CLAUDE.md` é satisfeito aqui pela fence (dado destino com gabarito e
anti-poison), não por um payload de saída de shader. Por isso a barra da seção 1 agora tem
**duas linhas de compute**: `PM4 na fila` (70% provado) e `dispatch de shader` (0%).

### 3.5 Display WDDM (candidato 3.24.0.0) — smoke test físico aprovado

- Código 0 real (`ConfigManagerErrorCode: 0`), sem triângulo amarelo.
- Cadeia **VidPN → CommitVidPn (status 0) → PresentDisplayOnly (status 0) → VSync
  (`DmmVsyncCount=7`, gravado só após notificação real de VSync + Present concluído)**.
- Imagem real em **2560x1080@60**, confirmada visualmente, sobreviveu a 1 reboot manual
  (candidato **3.23**) e a uma carga interativa de desktop.
- Watchdog de rollback funcionando: quando a tela ficava preta (candidatos 3.20/3.21), o
  sistema reverteu sozinho para `BasicDisplay`, Código 0.
- Detalhes: [91_HANDOFF_3.24_DISPLAY.md](docs/91_HANDOFF_3.24_DISPLAY.md),
  [90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md](docs/90_TELA_PRETA_VIDPN_DISPLAY_3.22.0.0.md).

**Limite importante**: é smoke test, não produção — ver a lista de pendências na **seção 2**.
E o `StartDevice` do display é deliberadamente "burro" (sem PFP/RLC/GCVM) — ver seção 6.

---

## 4. Critério de aceite do gate CP — desvio auditado nesta revisão

A revisão 1 afirmava que o gate *"só passa se a fence virar `0xBC250123`"*. **A auditoria do
código mostra que essa afirmação é imprecisa.** O critério real, em
[`bc250_pfp_direct_load.c:956-960`](driver/wdk/bc250_pfp_direct_load.c:956), é:

```text
status = OK  se  (Rb0RptrAfter >= cursor - 1)
             E   (Rb0WptrApplied == cursor)
             E   ( Rb0FenceAfter == 0xBC250123      <- fence em VRAM
                 OU Rb0FenceAfter == 0x22222222      <- marcador em SCRATCH_REG0
                 OU scratchAfter  == 0x22222222 )
```

Três desvios em relação ao tripé de `CLAUDE.md`, em ordem de gravidade:

**4.1 — `RPTR >= cursor - 1`, não `RPTR == WPTR`.**
Não é arbitrário: nas **110 capturas de sucesso**, a assinatura é sempre `RPTR/WPTR=0->15/16`,
isto é, `RPTR == WPTR - 1`. **O `RPTR` estrito nunca coincidiu com `WPTR` numa captura que
também tivesse fence válida.** Houve capturas com `RPTR==WPTR` (`16/16`, `12/12`, `32/32`), mas
todas com a fence envenenada — ou seja, ring drenado sem execução do `WRITE_DATA`. O
afrouxamento parece tecnicamente correto (o `RPTR` do CP para uma posição antes do `NOP` de
padding final), **mas hoje é um desvio não homologado da lei de ouro do projeto.**
*Ação pendente: documentar formalmente como exceção justificada em `CLAUDE.md`, ou ajustar o
preâmbulo PM4 para que o ring drene até `RPTR == WPTR`.*

**4.2 — A fence pode ser satisfeita por MMIO, não por VRAM.**
`0x22222222` é escrito em `R_CP_SCRATCH_REG0` por um `WRITE_DATA` PM4
([linhas 740-745](driver/wdk/bc250_pfp_direct_load.c:740)), com pré-envenenamento em
`0x11111111` ([linha 852](driver/wdk/bc250_pfp_direct_load.c:852)) e restauração
([linha 1007](driver/wdk/bc250_pfp_direct_load.c:1007)). **É prova legítima e anti-stale de que
o CP executou PM4** — mas é escrita em *registrador*, não em *VRAM*. O tripé de `CLAUDE.md`
exige explicitamente fence **em VRAM**. Uma execução que passe apenas por esse caminho prova
"PM4 executado", **não** fecha o tripé literal.
*Ação pendente: separar os dois veredictos na evidência — `PM4_EXECUTED` (scratch) vs
`TRIPE_CLOSED` (fence VRAM) — em vez de colapsá-los num único `OK`.*

**4.3 — O campo de evidência chamado "fence" pode conter o valor do scratch.**
Em [linhas 902-906](driver/wdk/bc250_pfp_direct_load.c:902), `Evidence->Rb0FenceAfter` recebe
`scratchAfter` quando este vale `0x22222222`. Isso significa que um relatório com
`fence=...->22222222` está reportando conteúdo de registrador sob o nome de fence.
*Ação pendente: usar campos distintos para fence-VRAM e scratch-MMIO no bloco de evidência.*

**Impacto prático nas 110 capturas de sucesso**: nenhuma delas dependeu do escape hatch — todas
mostram `fence=43DAFFFE->BC250123`, isto é, **fence real em VRAM**. A prova histórica continua
de pé. O problema é de *gate*, não de *resultado*: o critério aceita menos do que a lei exige,
e uma regressão futura poderia passar despercebida.

---

## 5. O que ainda NÃO está provado

| Área | Evidência física | Estado |
|---|---|---|
| **Dispatch de compute shader** | A fila MEC/HQD executa PM4 e fecha o tripé (seção 3.4), mas **nenhum kernel foi despachado** e nenhum buffer de saída foi conferido byte a byte | **Não provado** |
| Estabilidade do PFP/CP | 110/110 sucessos com `PC=0x284`, mas regime de falha intermitente aberto em 28/08 18h+ (seção 2) | **Não caracterizado** |
| SDMA via Dxgkrnl (produção) | Motor provado em bancada, mas `Bc250ActiveRunSdmaRingWork` usa offsets MC fixos e está proibido de reaproveitamento no backend GPUVA real (doc 92) | **Não provado como engine de produção** |
| Convergência Display + Compute | Bring-up de compute (PFP/RLC/GCVM) corrompia o framebuffer e desligava o Engine 0 de display — hoje rodam em caminhos separados | **Não resolvido** |
| IH / monitored fences / TDR | Sem ring de interrupção real, sem preempção, sem reset de queue | **0% físico** |
| Robustez do display 3.24 | Reboot próprio, soak, suspend/resume, mode change — nenhum executado | **0%** (ver seção 2) |
| Vulkan / LLM | Fora do escopo até compute fechar o tripé físico | **0%** |

---

## 6. Causa raiz da "tela preta" (por que display e compute não convergem ainda)

Documentado em [21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md:74-96](docs/21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md:74):

1. **Invasão do framebuffer**: o PFP Direct Load gravava uma tabela de páginas de teste no
   offset 1 MB da VRAM — exatamente sobre os pixels ativos (scanout GOP em `0xC0000000`,
   2560x1080@60 = ~11 MB contíguos).
2. **Invalidação do Engine 0**: `R_GCVM_INVALIDATE_ENG0_REQ` invalida o Engine 0, que no
   BC-250 é também o **Display Engine / VMID 0** (não há DCN dedicado com CRTCs autônomos) —
   invalidar isso desliga o scanout imediatamente.
   > **Confirmado no código nesta revisão**: o caminho do CP escreve nesse registrador em
   > [`bc250_pfp_direct_load.c:886`](driver/wdk/bc250_pfp_direct_load.c:886), imediatamente
   > antes de armar o `WPTR`. Não é hipótese — é a linha exata que impede a coexistência.
3. **Escrita em `R_RLC_CNTL` durante StartDevice** — proibido pela base de conhecimento do
   projeto de referência: qualquer escrita nessas aperturas durante o arranque normal causa
   tela preta.
4. **VSync fora de DIRQL**: `DxgkCbNotifyInterrupt` precisa rodar em DIRQL ou sincronizado via
   `DxgkCbSynchronizeExecution` — chamado de um DPC comum pode travar o subsistema gráfico
   silenciosamente.

**Regra de isolamento adotada** (doc [92_COMPUTE_GPUVA_SDMA_GATE.md](docs/92_COMPUTE_GPUVA_SDMA_GATE.md)):
qualquer alocação/tabela de páginas de compute deve residir **acima de 16 MB da VRAM**
(`>= 0x01000000`), operar em **VMID 8 ou superior**, nunca invalidar o Engine 0, e sempre
desativar/reativar GFXOFF via mailbox SMU (`0x15`/`0x14`) ao redor de qualquer interação com
os anéis de hardware.

### 6.1 F7 é engenharia, não pesquisa

As quatro causas-raiz acima **já têm mitigação escrita** na regra de isolamento do doc 92. O que
falta não é descoberta — é transformar disciplina em **contrato de mapa de memória VRAM** que
torne a violação impossível por construção:

- Faixa de scanout declarada e **reservada** (`0xC0000000` + ~11 MB), inacessível ao alocador de compute.
- Arena de compute com base fixa `>= 0x01000000` acima do topo do framebuffer, validada em tempo de alocação.
- `R_GCVM_INVALIDATE_ENG0_REQ` **removido** do caminho de compute (substituído por invalidação do engine do VMID 8).
- `R_RLC_CNTL` marcado como registrador proibido durante `StartDevice`, com asserção de build.

Isso rebaixa F7 de "desafio de pesquisa" para "refatoração com contrato" — notícia melhor do
que a revisão 1 sugeria.

---

## 7. Comparação com o projeto de referência (`referencias/AMD-BC-250-Windows-Driver`)

Projeto comunitário "Keshas" (`atikmdag.sys`), mais antigo e mais amplo (mira também
Linux/Vulkan/Mesa), mas com modelo de driver fundamentalmente diferente.

| Dimensão | Referência (Keshas) | Nosso driver (`bc250mcdm.sys`) |
|---|---|---|
| Modelo de driver | **WDM legado**; DDIs WDDM só como stubs | **WDDM MCDM real**, DDIs KMDOD implementadas |
| CP / PM4 | **Executor PM4 por software** — CPU traduz pacotes em MMIO direto, não é execução real | **PM4 Ring 0 real**: fence gravada pela GPU, 110/167 capturas |
| SDMA | Quebrado: `RB_BASE_LO` inválido, self-test falha | Provado: RPTR-MMIO=WPTR=68, fence, 4dw/0mismatch |
| PSP / firmware | GPCOM ring funciona (`0x58000`), `LOAD_IP_FW`/`SETUP_TMR` verificados | Ring PSP, `LOAD_IP_FW`, TMR também provados — trajetória paralela, resultado equivalente |
| GCVM / Page Tables | `PT_BASE` hardware-locked, sempre lê 0 — *"não dá pra corrigir"* | VMID 8 com rollback físico provado — **bloqueio superado** |
| Compute (HQD/MEC/KIQ) | Bloqueado: `KIQ_SIZE=0` read-only, `CP_HQD_*` NBIO-blocked | HQD programa e ativa, mas `status != 0` — mais avançado, ainda bloqueado |
| Display / WDDM | KMDOD básico + `wddm-ps5` esqueleto (*"not yet display-verified"* no próprio README) | Cadeia VidPN→Commit→Present→VSync provada em smoke test |
| Disciplina de evidência | "✅ funciona" baseado em teste isolado de ferramenta user-mode | Gate fail-closed + auditoria automatizada (com os desvios da seção 4 a corrigir) |

### 7.1 Achados importáveis — com peso ajustado

- **`navi12_sdma.bin` (v0x2c) x `cyan_skillfish2_sdma.bin` (v0x34)**: único achado de baixo
  custo, mas mexe no **único motor 95% provado**. Só depois de F6, e com rollback transacional.
- **Offsets do GPCOM PSP (`0x58000`)**: confirmação independente, sem risco. Aceitar.
- **Alerta `CP_HQD_*` NBIO-blocked / `KIQ_SIZE=0` "sem correção possível"**: **peso invertido em
  relação à revisão 1.** O mesmo projeto declarou `PT_BASE` *hardware-locked, impossível de
  corrigir* — e esse bloqueio **foi superado aqui** (VMID 8 com rollback físico, doc 41). O
  histórico deles em declarações de impossibilidade está **0/1**. Usar essa conclusão como razão
  para desistir do HQD seria aceitar o veredito de quem já errou exatamente o mesmo tipo de
  chamada. Continuar F8 com base em evidência própria.

---

## 8. Roadmap de Fases (consolidado a partir de `docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md`)

| Fase | Objetivo | Status real hoje | Evidência-chave |
|---|---|---|---|
| **F0** — Fail-closed & recovery | Nunca instalar nada que minta sucesso | ✅ 99% | Recovery, auditor de regressão, pacote assinado |
| **F1** — Lifecycle MCDM (ABI) | Tabela DDI mínima + objetos com owner/geração | ✅ 100% estrutural | 36/36 DDIs, testes de handle/teardown |
| **F2** — Ownership PnP & inventário | BARs, VRAM, TMR, scanout reais | ✅ 100% (escopo conservador) | Inventário físico completo, seal de geração |
| **F3** — VidMm/GPUVA/page tables | Substituir endereço fixo por memória gerenciada | 🟡 ~35% | Mecanismo GCVM/VMID8 provado; falta paging sob pressão |
| **F4** — PSP/firmware/CP Ring 0 | Firmware autenticado + PM4 real | ✅ **Ring 0 fechado em bancada** | Docs 54–80; 110/167 capturas |
| **F4b** — Display WDDM *(fora do roadmap original)* | VidPN→Commit→Present→VSync no silício | ✅ **Smoke test aprovado** (3.24) | Doc 91, Código 0, 2560x1080@60 |
| **F4d** — **Caracterizar a intermitência do PFP** *(PRÓXIMA AÇÃO)* | Descobrir por que o PFP entra em loop em `0x17F5` em vez de assentar em `0x284` | ⬜ 0% — **sem código novo, só ensaio** | Seção 2; assinatura 110/110 |
| **F4c** — Robustez do display 3.24 *(em paralelo)* | Reboot próprio, soak, suspend/resume, mode change | ⬜ 0% — **sem código novo, só ensaio** | Seção 2.4 |
| **F5a** — **Detecção de hang + reset mínimo** *(cindida de F5)* | Não travar a máquina quando o compute falhar com display vivo | ⬜ 0% — **paralela a F7** | — |
| **F7** — Convergência Display + Compute | Contrato de mapa VRAM; bring-up sem tela preta | 🟡 **SDMA convergiu** (seção 2.0); falta o CP | Causa raiz mapeada (doc 21 + `bc250_pfp_direct_load.c:886`) |
| **F5b** — IH ring real + monitored fences | Conclusão assíncrona real, sem polling de user mode | ⬜ 0% físico | Não iniciado |
| **F6** — SDMA pelo Dxgkrnl | SDMA como engine real do miniport | ⬜ 0% (motor provado, integração não) | Doc 92: broker/lease ainda ABI-only |
| **F8a** — MEC/HQD: PM4 na fila de compute | Submissão por doorbell + fence VRAM | ✅ **Tripé fechado 26x** | `status=0 RPTR/WPTR=11/11 fence=BC250123 triad=1` (seção 3.4) |
| **F8b** — Dispatch de compute shader | Kernel executando, buffer de saída conferido byte a byte | ⬜ 0% | Nenhum kernel despachado |
| **F9** — Runtime/kernels LLM | UMD privado + kernels quantize/matmul/attention | ⬜ 0% | — |
| **F10** — Vulkan ICD compute-only | API mínima para `llama.cpp` | ⬜ 0% | — |
| **F11** — llama.cpp via Vulkan | LLM real rodando na BC-250 | ⬜ 0% | — |
| **F12** — Robustez/distribuição | HLK, Verifier 24h, instalador transacional | ⬜ 0% | — |
| **F13** (opcional) — 3D/full display | Aceleração gráfica completa | ⬜ 0% — fora do critério de sucesso do LLM | — |

### 8.1 Caminho crítico revisado

```text
F4d  Caracterizar a intermitência do PFP   <- PRÓXIMA AÇÃO: ensaio físico, zero código novo
       |    (gate de preflight PC==0x284 sai de graça deste trabalho)
       |
       +--> F4c  Soak/reboot/suspend do display 3.24  ]
       |                                              }-- EM PARALELO (limitado por bancada)
       +--> Pendências 3 e 4: separar PM4_EXECUTED    ]   (limitado por engenheiro)
       |    de TRIPE_CLOSED, usando o gate de compute
       |    como implementação de referência
       |
       v
     F5a  Detecção de hang + reset mínimo  ]
                                           }-- EM PARALELO
     F7   Convergência via contrato VRAM   ]
                    |
                    v
              F5b  IH ring real + monitored fences
                    |
                    v
              F6   SDMA via Dxgkrnl (backend físico do broker GPUVA)
                    |
                    v
              F8b  Dispatch de shader com resultado conferido
                    |
                    v
              F9 -> F10 -> F11  (pilha LLM/Vulkan)
                    |
                    v
              F12  Produção
```

**Por que F4d vem antes de tudo**: o compute já fecha o tripé (F8a) quando o PFP sobe limpo, e
falha junto quando o PFP entra em loop — nas capturas de falha o HQD aparece como
`status=1 applied=0->0 triad=0`, ou seja, nem chega a ser exercitado. **A intermitência do PFP é
o gargalo único que hoje limita tanto o CP quanto o compute.** Depurar F7 ou F8b sobre uma base
que falha ~1 em 3 em certos regimes significa perseguir sintomas não reproduzíveis.

### 8.2 Por que F5 foi cindido

A revisão 1 colocava F5 (IH/fences/TDR) **depois** de F7, ao mesmo tempo em que o descrevia como
*"pré-requisito de segurança antes de qualquer submit real"*. As duas coisas não podem ser
verdade simultaneamente, porque **F7 é exatamente ligar bring-up de hardware com o monitor
ativo**.

Hoje, quando o compute quebra, o sintoma é tela preta e o watchdog reverte para `BasicDisplay`
(observado nos candidatos 3.20/3.21) — falha recuperável. **Depois de F7, com o display servido
pelo próprio driver, um hang de engine sem detecção não vira tela preta recuperável: vira
máquina travada sem caminho de rollback automático.** Por isso a parte de **detecção e reset
(F5a)** foi separada e movida para paralela a F7; o ring de interrupção completo e monitored
fences (F5b) permanecem depois.

### 8.3 Prioridade F7 antes de F8 — mantida

Sem a convergência, mesmo que o compute feche o tripé físico amanhã, não há como demonstrar
nada com o monitor ligado — e essa é a demo que normalmente se espera ver funcionando.

---

## 9. Pendências abertas por esta revisão

| # | Pendência | Prioridade | Onde |
|---|---|---|---|
| 1 | **Caracterizar a intermitência do PFP** (`PC=0x17F5` em loop vs `0x284`) | **P0** | seção 2 |
| 2 | **Doc 89 está desatualizado** — afirma `status != 0` no HQD; o corpus tem 26 tripés fechados desde 28/08 15h. Atualizar ou marcar como superado. | **P0** | [doc 89](docs/89_HANDOFF_HQD_MEC_2026-08-28.md) |
| 3 | Adicionar gate de preflight `PC final == 0x284` (fail-closed, sai de graça do ensaio P0) | P1 | seção 2.3 |
| 4 | Separar veredicto `PM4_EXECUTED` (scratch MMIO) de `TRIPE_CLOSED` (fence VRAM) no caminho do CP — **implementação de referência em [`bc250_compute_mqd_contract.c:2244`](driver/core/bc250_compute_mqd_contract.c:2244)** | P1 | seção 4.2 |
| 5 | Usar campos distintos para fence-VRAM e scratch-MMIO no bloco de evidência do CP | P1 | seção 4.3 |
| 6 | **`CLAUDE.md` ainda carrega a barra de status antiga** (`RLC/CP/PM4 45%`, `Compute 15%`, `Miniport 20%`). Como a regra nº 1 manda sempre apresentar a barra, duas versões divergentes é risco de relato incorreto na próxima sessão. | P1 | [`CLAUDE.md`](CLAUDE.md) |
| 7 | **Doc 92 e código em desacordo sobre GFXOFF** — doc manda mailbox `0x15`/`0x14`, código declara `GfxOffApplicable=0` com justificativa SMU 11.8 | P2 | seção 2.3 |
| 8 | Homologar ou corrigir o critério `RPTR >= cursor - 1` do CP — **fazer depois de P0**, apertar o gate antes do diagnóstico gera falhas inseparáveis | P2 | seção 4.1 |
| 9 | **1.152 linhas não commitadas** em `bc250_compute_service.c` (+784), `bc250_active_miniport.c` (+77) e outros, sobre `771d108`. Commitar ou stashar antes dos ensaios, para que o resultado tenha um SHA de referência. | P1 | `git status` |
| 10 | **Não existe instrumento de runtime** — os 100 campos de `Parameters\Probe` são retrato de `StartDevice` e nenhum muda com o driver ativo. Sem contador vivo não há como provar liveness de VSync nem de fence. Pré-requisito natural de F5b. | P1 | seção 2.7 |
| 11 | **Bugcheck `0x124` (WHEA_UNCORRECTABLE_ERROR) encerrou a sessão de ~8,9 h em 29/08 08:35.** Inédito em 7 dias, sem dump, sem aviso de display. Verificar reincidência antes de creditar qualquer soak. | **P0** | seção 2.6 |
| 12 | Ensaio F4d exige `SkipHwBringup=0`, o que religa o bring-up no dispositivo que serve o display — decisão de bancada com risco de tela preta | **P0** | seção 2.3 |

### 9.1 Ferramentas criadas para estas pendências

| Script | Papel | Risco |
|---|---|---|
| [`tools/ensaio-pfp-intermitencia.ps1`](tools/ensaio-pfp-intermitencia.ps1) | F4d — N ciclos de `StartDevice` sem reboot, classificando o PC final do PFP (`OK-SETTLED` / `FAIL-LOOP` / `FAIL-STALL-OLD` / `FAIL-NO-START`) e cruzando com `PfpDirectStatus`. Sinaliza `!DIVERGE` se a regra 110/110 for quebrada. | **Alto** — exige `-Execute -IAcceptTheRisk`; faz Disable/Enable do dispositivo. Preflight recusa se `SkipHwBringup=1`. |
| [`tools/soak-bc250mcdm-display.ps1`](tools/soak-bc250mcdm-display.ps1) | F4c — soak de regressão do display: binding, Código 0, status de Present/Commit e eventos de display/TDR no Event Log. CSV incremental. | **Nenhum** — 100% read-only. Validado em 2026-08-29, 2 min sem regressão. |

---

## 10. Referências rápidas

- Regras inegociáveis do projeto: [`CLAUDE.md`](CLAUDE.md)
- Roadmap detalhado original (14 fases, histórico completo de candidatos): [`docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md`](docs/20_ROADMAP_WDDM_MCDM_VULKAN_LLM.md)
- Handoff mais recente de compute (MEC/HQD): [`docs/89_HANDOFF_HQD_MEC_2026-08-28.md`](docs/89_HANDOFF_HQD_MEC_2026-08-28.md)
- Handoff mais recente de display: [`docs/91_HANDOFF_3.24_DISPLAY.md`](docs/91_HANDOFF_3.24_DISPLAY.md)
- **Runbook do ensaio F4d** (procedimento, riscos, reversao): [`docs/93_RUNBOOK_F4D_INTERMITENCIA_PFP.md`](docs/93_RUNBOOK_F4D_INTERMITENCIA_PFP.md)
- Ponto de retomada mestre WDDM+Display: [`docs/21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md`](docs/21_PONTO_DE_RETOMADA_WDDM_DISPLAY.md)
- Gate GPUVA+SDMA (compute isolado, ABI-only ainda): [`docs/92_COMPUTE_GPUVA_SDMA_GATE.md`](docs/92_COMPUTE_GPUVA_SDMA_GATE.md)
- Teste de aceitação integral no silício: [`docs/21_TESTE_DE_ACEITACAO_SILICIO.md`](docs/21_TESTE_DE_ACEITACAO_SILICIO.md)
- Corpus de evidências físicas (338 sessões): `docs/evidence/`
- Projeto de referência comparado: [`referencias/AMD-BC-250-Windows-Driver/README.md`](referencias/AMD-BC-250-Windows-Driver/README.md)

---

## 11. Changelog

**Revisão 3 — 2026-08-29** *(auditoria cruzada código × corpus de evidências)*
- **Compute deixou de ser 0% provado.** 26 capturas com o tripé fechado na fila MEC/HQD
  (`status=0 RPTR/WPTR=11/11 fence=43DAFFFE->BC250123 triad=1`), a partir de 28/08 15h. As
  revisões 1 e 2 herdavam o doc 89, que é anterior a essas capturas. Nova seção 3.4.
- **F8 cindido em F8a (PM4 na fila — ✅ fechado) e F8b (dispatch de shader — 0%)**, e a barra
  ganhou duas linhas de compute. O tripé fechado prova execução de PM4 pela fila de compute,
  **não** dispatch de kernel com resultado conferido byte a byte.
- **Descoberto o discriminador do PFP**: o PC final prevê o resultado do CP com 100% de
  acurácia em 167 capturas (`0x284`→OK 110/110; `0x41E`/`0x17F5`/`0x0`→FAIL). Nova seção 2.
- **Caracterizado um segundo modo de falha**, aberto em 28/08 18h: PFP em *loop ativo*
  (`transitions=26..48`) em `0x17F5` em vez de assentar em `0x284`, no mesmo binário, mesma
  `version=94`, mesmo staging. Não é regressão de código — é estado residual dentro do boot.
- **Próxima ação trocada de F4c (soak do display) para F4d (caracterizar o PFP)**: o compute já
  funciona quando o PFP sobe limpo e falha junto quando não sobe, logo a intermitência do PFP é
  o gargalo único. F4c passa a rodar em paralelo.
- Taxa do CP reenquadrada pela terceira vez: nem "~150 idênticas" (rev. 1) nem "65,9%" (rev. 2)
  — são **três regimes** distintos, com o candidato homologado em **~99%**.
- Registrada a assimetria de gates: o caminho de compute exige as três condições do tripé em
  **AND** com `RPTR` estrito; o do CP usa **OR** com `RPTR >= cursor-1`. A correção pedida na
  seção 4 já tem implementação de referência no próprio repositório.
- Registrado o desacordo doc 92 × código sobre GFXOFF (mailbox SMU vs `GfxOffApplicable=0`).
- Pendências repriorizadas em P0/P1/P2; doc 89 marcado como desatualizado.

**Revisão 3.1 — 2026-08-29** *(ensaios instrumentados; verificação em máquina real)*
- **Estado da máquina levantado**: display-only, `SkipHwBringup=1`, SchemaVersion 96, Código 0.
  A exclusão display × compute está numa linha só (`bc250_active_miniport.c:2364`) — o
  `StartDevice` retorna cedo sem tocar em PFP/CP/GCVM. Nova seção 2.3.
- **Descoberto que não há instrumento de runtime**: os 100 campos de `Parameters\Probe` não
  mudam com o driver ativo. `DmmVsyncCount` é latch one-shot
  (`Bc250RecordVsyncProofAfterSuccessfulPresent`), não contador vivo. Nova seção 2.6, pendência 10.
- Criados e validados dois scripts de ensaio (seção 9.1): `ensaio-pfp-intermitencia.ps1` (F4d,
  fail-closed com preflight que recusa `SkipHwBringup=1`) e `soak-bc250mcdm-display.ps1`
  (F4c, read-only).
- F4c reclassificado como **executável agora** — a máquina já está na configuração certa.
- F4d ganhou pré-requisito explícito de reconfiguração, com risco de tela preta (pendência 11).

**Revisão 2 — 2026-08-29**
- Barra de status separada em dois eixos: **implementado** × **provado (tripé)**, com escopo
  explícito de cada prova. Elimina a leitura de que compute está "70% pronto" quando a prova
  física está em 0%.
- `RLC, CP & PM4 Direct Load: 100%` desmembrado e rebaixado: nada pode estar em 100% enquanto o
  veredito global é `NÃO VALIDADO EM HARDWARE`. RLC nem participa do `StartDevice`.
- Contagem de evidências recontada do corpus: **110 sucessos em 167 capturas** (era "~150
  capturas idênticas").
- **Nova seção 4**: auditoria do critério de aceite real do gate CP, com três desvios
  documentados em relação ao tripé de `CLAUDE.md`.
- Causa-raiz nº 2 da tela preta confirmada na linha exata do código
  (`bc250_pfp_direct_load.c:886`).
- **Nova seção 2 / fase F4c**: robustez do display 3.24 promovida a próxima ação.
- **F5 cindido em F5a (detecção/reset, paralela a F7) e F5b (IH/monitored fences, posterior)**,
  resolvendo a contradição de ordenação da revisão 1.
- Seção 6.1: F7 reclassificada de pesquisa para engenharia (contrato de mapa VRAM).
- Seção 7.1: peso invertido no alerta `CP_HQD_*` NBIO-blocked do projeto de referência.
- Divergência GCVM 90% × F3 35% explicada e separada em duas linhas da barra.
- **Nova seção 9**: pendências abertas, incluindo a dessincronização da barra em `CLAUDE.md`.

**Revisão 1 — 2026-08-29** — versão inicial consolidada.
