# Handoff para a próxima IA — Fase P1 após o candidato 3.45

**Data**: 30/08/2026
**Último pacote ensaiado**: **3.45.0.0 / SchemaVersion 116** (`46A158CA…`)
**Veredito**: **NÃO VALIDADO EM HARDWARE** — mas o ciclo respondeu a pergunta que
foi feito para responder, e a causa raiz está isolada com evidência.

> Este documento é autocontido. Leia-o inteiro antes de tocar em código.

---

## 1. Onde o projeto está

A fundação de baixo nível está provada e **reproduzida três vezes** no silício:
SDMA, CP Direct Load, MEC/HQD e o dispatch do shader observável
(`0xBC250C01` gravado pela GPU, conferido byte a byte).

A Fase P1 — fila de computação **persistente** entre submissões, que é o que o
`DxgkDdiSubmitCommandVirtual` do Windows exige — está **implementada e ativando
no silício**, mas o CP **não consome o anel**. A causa raiz foi identificada no
ensaio 3.45 e a correção está especificada na seção 5. Ela ainda não foi
escrita: é o próximo passo.

### 1.1 Estado do repositório

47 arquivos modificados/não rastreados, **nada commitado**. Os artefatos novos
desta linha de trabalho:

| Arquivo | Papel |
|---|---|
| [`driver/core/bc250_persistent_ring_contract.{c,h}`](../driver/core/bc250_persistent_ring_contract.c) | máquina de estados do anel persistente (offline, 9 grupos de teste) |
| [`driver/tests/persistent_ring_contract_test.c`](../driver/tests/persistent_ring_contract_test.c) | teste do contrato acima |
| [`driver/wdk/bc250_submission_engine.{c,h}`](../driver/wdk/bc250_submission_engine.c) | dono do estado persistente: memória, mapeamento, fila ativa, cursores, auto-teste |
| [`tools/test-p1-submission-acceptance.ps1`](../tools/test-p1-submission-acceptance.ps1) | gate estrito da P1 |
| [`docs/129_…`](129_CANDIDATO_3.44_FASE_P1_SUBMISSAO_PERSISTENTE.md), [`docs/130_…`](130_RESULTADO_3.44_E_CANDIDATO_3.45.md) | candidato 3.44 e resultado 3.44 + candidato 3.45 |

Alterados em arquivos existentes:

- `bc250_compute_mqd_contract.{c,h}` — modo persistente aditivo (seção 3.1)
- `bc250_pm4_packet_builder.{c,h}` — `Bc250Pm4EmitReleaseMemEop32` com os
  literais provados e `INT_SEL` parametrizável
- `bc250_sdma_packet_builder.{c,h}` — `Bc250SdmaEmitIndirect` (aditivo, 56
  inserções e **zero** remoções)
- `bc250_active_miniport.{c,h}` — campo do motor, tunable `PersistentSubmission`,
  executor real, `Online`/`SelfTest` no StartDevice, `Offline` no StopDevice
- `bc250_probe_telemetry.{c,h}` — 93 campos novos de telemetria P1
- `driver/tests/active_miniport_test.c` — stubs de spinlock, pool 64 KiB,
  asserções de submissão fail-closed
- `driver/wdk/build-contract.bat` — compila e linka os módulos novos
- `tools/{package,bind,confirm,audit}-bc250mcdm-lab*.ps1` — correções da seção 6

**O build está verde**: `driver\wdk\build-contract.bat` passa limpo em `/W4 /WX`
com todos os testes offline aprovados.

---

## 2. O que o ensaio 3.45 provou e refutou

Evidência: `docs/evidence/2026-08-30_17-09-49-DESKTOP-HQAG6T4-lab-binding` e o
relatório `docs/evidence/2026-08-30_17-10-25-DESKTOP-HQAG6T4-p1-submission/p1-submission-acceptance.json`.

### 2.1 Funcionou

| Etapa | Evidência |
|---|---|
| SDMA / CP / HQD / shader | toda a cadeia aprovada, `ShaderPhysicalReady=1` (3ª reprodução) |
| **Ativação persistente** | `active=1`, `hq_status0=0x40000040` (QUEUE_IDLE=1), `pq_control=0xD030890D` (PQ_EMPTY=1), `doorbell_control=0x40000110` (DOORBELL_EN=1, offset 0x110), `cp_pq_status=0x2` |
| Encenação do IB | 45 dwords, readback byte a byte aprovado, GPUVA `0x00A96000` |
| Baseline da fence | `fence_baseline_written=1`, readback `0` |
| Publicação do cursor | `wptr_shadow_readback=12`, `wptr_mmio_readback=12`, doorbell tocado |

O `pq_control` após a ativação persistente é **idêntico** ao do caminho provado
(`0xD030890D`): a fila está programada exatamente igual à do 3.43.

### 2.2 O CP percebeu a submissão

Comparando as fotografias antes e depois do submit:

```text
                   após ativar      após submeter
hq_status0         0x40000040   ->  0x00008040     (QUEUE_IDLE 1 -> 0)
pq_control         0xD030890D   ->  0xD030090D     (PQ_EMPTY   1 -> 0)
cpc_status         0x00000000   ->  0xA0000082     (CPC ocioso -> ocupado)
pq_wptr_lo         0            ->  12
pq_rptr            0            ->  0
hqd_error          0x00000000   ->  0x00000000     (sem erro)
```

`PQ_EMPTY` é o bit 15 de `CP_HQD_PQ_CONTROL`
(`inc/linux_ref/gc_10_1_0_sh_mask.h:20323`). Ele caiu para 0: **o CP registrou
que há trabalho pendente**, saiu do estado ocioso e o CPC ficou ocupado. A
notificação de cursor **funciona**. O que não acontece é o *fetch*: `RPTR`
permanece em 0 e nada completa.

### 2.3 Hipótese refutada

O 3.45 testou a hipótese do 3.44 — "o CP só amostra o WPTR na borda de
`CP_HQD_ACTIVE`" — com um re-arme controlado (`ACTIVE=0 → ACTIVE=1`, mesmo anel,
mesmo cursor):

```text
rearm.attempted        1
rearm.active_after_off 0x00000000     (desativou)
rearm.active_after_on  0x00000001     (reativou)
rearm.polls            20000
rearm.rptr_mmio        0              (não destravou)
rearm.completed        0
```

**Hipótese da borda do ACTIVE: REFUTADA.** E o doorbell sai da lista de
suspeitos, porque a seção 2.2 mostra que o CP reagiu ao cursor.

---

## 3. Arquitetura implementada (para não reinventar)

### 3.1 Modo persistente aditivo no MQD contract

`bc250_compute_mqd_contract.c` ganhou `BC250_HQD_TRANSACTION_PERSISTENT_ACTIVATE`,
que sobe a fila **vazia** e **não** executa o rollback — quem chama vira dono do
teardown via `Bc250ComputeDeactivatePersistentHqd()`.

Para não existirem duas implementações de restore, o estágio 10 (dequeue +
rollback LIFO) foi extraído para `Bc250HqdRunRestoreStage()` e as ~48 variáveis
locais de snapshot viraram `BC250_HQD_PERSISTENT_STATE`. **Os dois caminhos usam
a mesma função, na mesma ordem.**

Com `PersistentSubmission=0` o comportamento é o do 3.43, sem desvio — e o
`ShaderPhysicalReady=1` nos ensaios 3.44 e 3.45 comprova isso no silício.

### 3.2 Anel persistente

`bc250_persistent_ring_contract.c`. Duas invariantes que **não** devem ser
relaxadas:

- **Nenhum pacote cruza o fim do anel.** Quando o lote não cabe, o rabo é
  preenchido com `PACKET2` (`0x80000000`), o NOP de **um** dword. Um `PACKET3`
  NOP declara `COUNT=n-2`, ou seja dois dwords, e o CP estaciona o RPTR no dword
  anterior — foi a causa determinística do `RPTR=15` contra `WPTR=16` em 40/40
  ciclos (vide [`bc250_pfp_direct_load.c:1027`](../driver/wdk/bc250_pfp_direct_load.c:1027)).
- **A fence avança exatamente 1 por submissão.** É isso que torna a
  aposentadoria não ambígua a partir dos 32 bits que a GPU grava. Valor fora da
  janela em voo é classificado como **corrupção**, não progresso.

### 3.3 Motor de submissão

`bc250_submission_engine.c`. Mapa de memória **idêntico ao do 3.43**, de
propósito: MQD `+0x0000`, EOP `+0x1000`, RPTR `+0x2000`, WPTR `+0x2100`, fence do
anel `+0x2200`, PT do Contexto 0 `+0x3000`, shader `+0x4000`, resultado
`+0x5000`, **IB do auto-teste `+0x6000`**, fence do dispatch `+0x2300`, anel
`+0x10000` (64 KiB, 16384 dwords).

As duas fences vivem em endereços distintos de propósito: no mesmo endereço uma
sobrescreve a outra e a aposentadoria do anel lê lixo.

### 3.4 Auto-teste

Um `SubmitCommandVirtual` real exige cliente de modo usuário, que só chega com o
UMD (P4). O auto-teste submete **um** Indirect Buffer pelo mesmo caminho
persistente, e o conteúdo do IB é a sequência de dispatch aprovada no 3.43.
Tripé exigido: `RPTR==WPTR`, fence do dispatch `0xA55A0FF0→0xBC250D15`, fence de
sequência do anel `0→1`, resultado `0x43DAFFFE→0xBC250C01` byte a byte.

---

## 4. Regras que este código NÃO relaxa

- Nenhuma fence é incrementada pela CPU. A única escrita de fence após a
  submissão é a do `RELEASE_MEM` executado pela GPU.
- `Submit` só devolve `STATUS_SUCCESS` quando o lote entrou no anel e o cursor
  foi publicado. Falha vira NTSTATUS de erro.
- Nó SDMA (ordinal 0) devolve `STATUS_NOT_SUPPORTED` — recusa honesta, sem anel
  persistente ainda.
- O motor vai para `FAULTED` se a fence lida estiver fora da janela em voo.

---

## 5. ⭐ PRÓXIMO PASSO — candidato 3.46, causa raiz identificada

**O caminho de submissão não faz a invalidação de cache do lado da GPU.**

A transação provada, em [`bc250_compute_mqd_contract.c:2873-2952`](../driver/core/bc250_compute_mqd_contract.c), executa
esta sequência **imediatamente antes do kick**, com o comentário
*"Torna as escritas de CPU visiveis ao MEC antes do kick"*:

1. publica o cursor na shadow memory e faz barrier;
2. `HDP_READ_CACHE_INVALIDATE` (`0x00003FC4`) = `1`;
3. `GCVM_L2_CNTL2` (`0x0000A104`) = `(valor & ~0x3) | 0x3`, com readback
   conferindo que os bits de invalidação voltaram a zero;
4. `GCVM_INVALIDATE_REQ` (`0x0000A2D0`) = `0x00F80001`, **dummy-read do REQ**
   (GC10.1 exige, senão dá falso ACK), e poll de `GCVM_INVALIDATE_ACK`
   (`0x0000A318`) pelo bit 0, até 1000 iterações.

`Bc250SubmissionPublishWptr()` em `bc250_submission_engine.c` faz **apenas**
`KeMemoryBarrier()`, que é barreira de CPU e não faz nada pelo HDP read cache
nem pela GL2 da GPU.

Isso explica exatamente o observado: o CP enxerga o cursor (registrador,
não memória) e sai do ocioso, mas busca o conteúdo do anel de uma visão
desatualizada e nunca obtém pacotes válidos — `RPTR` fica em 0, `HQD_ERROR=0`,
sem timeout, sem erro.

### 5.1 O que fazer

Em `Bc250SubmissionPublishWptr()`, entre a escrita do shadow e o kick
(doorbell + escrita direta do WPTR), replicar os passos 2–4 acima na **mesma
ordem**. Constantes já públicas em `bc250_compute_mqd_contract.h`:

```c
#define BC250_HQD_MMIO_HDP_READ_CACHE_INVALIDATE 0x00003FC4u
#define BC250_HQD_MMIO_GCVM_L2_CNTL2             0x0000A104u
#define BC250_HQD_MMIO_GCVM_INVALIDATE_REQ       0x0000A2D0u
#define BC250_HQD_MMIO_GCVM_INVALIDATE_ACK       0x0000A318u
#define BC250_HQD_GCVM_INVALIDATE_VMID0          0x00000001u
#define BC250_HQD_GCVM_INVALIDATE_REQ_VMID0      0x00F80001u
#define BC250_HQD_GCVM_L2_INVALIDATE_MASK        0x00000003u
```

Use a fórmula de bits do `CLAUDE.md` para o `GCVM_L2_CNTL2`:
`val = (lido & ~mask) | (or_val & mask)`.

Acrescente telemetria por submissão — `P1CoherHdpApplied`,
`P1CoherL2Readback`, `P1CoherInvalidateAck`, `P1CoherInvalidatePolls`,
`P1CoherencyPassed` — e faça o gate exigir `P1CoherencyPassed=1`. Se a
invalidação não fechar, a submissão deve **falhar**, não seguir em frente.

### 5.2 Se isso não resolver

O próximo suspeito é o `CP_MEC_CNTL`: o teardown da transação de tiro único roda
**antes** do `Online` e restaura o registrador ao valor de entrada. Capture
`CP_MEC_CNTL` (`0x00008234`) e `CP_ME_CNTL` (`0x000086D8`) na fotografia da fila
e confira se o MEC continua *unhalted* depois que o motor sobe.

---

## 5.3 Critérios para declarar a P1 concluída — leia antes de comemorar

O 3.46 verde **não** encerra a P1. Ele fecha apenas *um* dos três critérios:

| Critério | Estado após um 3.46 verde |
|---|---|
| Motor persistente consome o anel no nó de computação | ✅ seria fechado |
| **Nó SDMA (ordinal 0)** | ❌ sem anel persistente; `Submit` devolve `STATUS_NOT_SUPPORTED`. Metade da superfície de submissão do adaptador continua descoberta |
| **Caminho fim-a-fim do Dxgkrnl** | ❌ o DDI está ligado ao motor, mas nada no Windows submete até existir UMD (P4). O auto-teste prova o **motor**, não o pipeline |

Ou seja: mesmo com o tripé fechando no 3.46, o correto é registrar
**"P1 — motor de submissão persistente validado no silício"**, não "P1
concluída". Declarar a fase inteira fecharia o mesmo tipo de buraco que o
`SubmitCommandVirtual` devolvendo `STATUS_SUCCESS` sem backend criou até o 3.43.

## 6. Defeitos de ferramental encontrados e corrigidos

1. **Watchdog nunca desarmava.** `watch-bc250mcdm-lab.ps1:50` exige
   `[bool]$disarm.display_visible`; o `confirm-bc250mcdm-lab.ps1` nunca escrevia
   esse campo. Em PowerShell `[bool]$null` é `$false`, então o rollback caía ao
   fim dos 180 s mesmo com a confirmação impressa como bem-sucedida. **Nenhum
   bind de laboratório deste projeto havia sobrevivido além do timeout.**
   Corrigido.
2. **Schema fixo em literal.** `bind`, `audit-lab-package`, `confirm` e
   `test-shader-dispatch-acceptance` travavam em `114` e recusavam o 3.44/115.
   Agora derivam de `TelemetrySchemaVersion` do manifesto, com piso em 114.
3. **`-EnablePersistentSubmission`** adicionado ao `package` e ao `bind`, com
   conferência do flag no manifesto e no INF.

---

## 7. Armadilhas de operação (custaram ciclos nesta sessão)

- **Estado de engine não cai com desbind.** O ciclo das 16:50 falhou no SDMA
  (`SdmaRingWorkStatus=11`, `FENCE_FAILED`) porque `SDMA0_GFX_RB_CNTL` entrou com
  `0x400E2` — a programação do próprio driver de um bind anterior. Em boot frio
  entra `0xC2` e passa. **Desligue e ligue entre binds.** Não é a intermitência
  documentada do `EX_IDLE_POLL_TIMER_EXPIRE`: o bit 11 estava apagado.
- **O empacotador não compila o SYS.** Ele só copia de
  `driver\wdk\output_lab\bc250mcdm.sys`. Já existe guarda de frescor que recusa
  fonte mais nova que a imagem — respeite-a.
- **Quarentene o SYS residual** antes de cada bind.

---

## 8. Cadeia de ensaio completa

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

```bash
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission
```

**Desligar e ligar.** Depois:

```bash
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine
```

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission
```

O bind imprime o `confirm-bc250mcdm-lab.ps1` com `EvidenceDir`, `InstanceId` e
`Token` — rode-o dentro dos 180 s. Depois:

```bash
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

O gate imprime o diagnóstico na tela, sem obrigar a abrir o JSON.

> **Os passos de hardware são do Daniel.** A IA compila, analisa e propõe; ele
> executa. Cada ciclo é caro: prefira leitura de evidência e bisseção a gastar
> corrida.

---

## 9. Depois de P1: o que ainda é ficção

Não confunda com fase concluída. Hoje, no código:

| DDI | Estado real |
|---|---|
| `Bc250McdmPreemptCommand` | devolve `STATUS_SUCCESS` sem tocar em registrador |
| `Bc250McdmResetEngine` | devolve `STATUS_SUCCESS`, `LastAbortedFenceId=0` |
| `Bc250McdmResetFromTimeout` / `RestartFromTimeout` | `STATUS_NOT_SUPPORTED` |
| IH ring | nunca ligado; as interrupções vistas são VSync de display |
| GPUVA dinâmica por processo | inexistente; há um mapeamento estático do Contexto 0 |

A ordem recomendada, e o porquê, está na seção 8.1 do
[`STATUS_E_ROADMAP_CONSOLIDADO.md`](../STATUS_E_ROADMAP_CONSOLIDADO.md) e na
crítica registrada nesta sessão: **detecção de hang e TDR (P1a) precisam vir
antes de submissão real com o display vivo**, porque depois da convergência um
hang de engine sem detecção não vira tela preta recuperável — vira máquina
travada.

**Veredito global: NÃO VALIDADO EM HARDWARE.**
