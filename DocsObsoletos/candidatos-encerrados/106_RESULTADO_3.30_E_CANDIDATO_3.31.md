# Resultado físico do 3.30 e candidato 3.31

Data local: 30/08/2026
Ensaiado: **3.30.0.0 / SchemaVersion 101** — **NÃO VALIDADO EM HARDWARE**
Preparado: **3.31.0.0 / SchemaVersion 102** (offline)

Continuação de [`105_CANDIDATO_3.30_WPTR_ONESHOT_QUEUE_MASK.md`](105_CANDIDATO_3.30_WPTR_ONESHOT_QUEUE_MASK.md).

---

## 1. O binding do 3.30 funcionou; a transação não

O pacote entrou em `bc250mcdm / Código 0` com o hash correto
(`66D5929C…` instalado == empacotado) e o display preservado. A telemetria
confirma que o binário 3.30 — não o anterior — rodou:

```text
SchemaVersion                  0x00000065  (101)
HqdActiveStatus                0x00000005  (BC250_HQD_ACTIVE_WRITE_FAILED)
```

## 2. O achado: `CP_PQ_WPTR_POLL_CNTL.EN` não é retido

| Campo | Valor |
|---|---:|
| `HqdActiveWptrPollCntlBefore` | `0x00000000` |
| `HqdActiveWptrPollCntlApplied` | `0x80000000` |
| `HqdActiveWptrPollCntlReadback` | `0x00000000` |
| `HqdActiveWptrPollCntl1Applied` | `0x00000000` (nunca alcançado) |
| `HqdActivePreActiveWptrApplied` | `0x00000000` (nunca alcançado) |
| `HqdActiveEopRptrApplied` | `0x00000000` (nunca alcançado) |
| `HqdActiveRestorePassed` | `1` |
| `HqdActiveRestoreFailureCount` | `0` |
| `HqdActiveFirstRestoreFailureStage` | `0` (`NONE`) |

A transação abortou fail-closed exatamente na primeira etapa nova do 3.30.
Todos os passos seguintes ficaram em zero, de forma coerente.

**Este ensaio produziu um fato que nenhum ciclo anterior havia produzido.**
Até o 3.29 o código seguia o caminho KIQ e escrevia `orig & ~EN`. Com
`orig = 0`, isso é escrever zero num registrador que já vale zero — passa em
qualquer readback sem provar nada. O 3.30 foi a **primeira escrita real
verificada** em `0xC20C`, e o bit `EN` não gruda.

### 2.1 O offset não é o culpado

`CP_PQ_STATUS` (`dw 0x1E58` → `0xC2E0`) usa a mesma base `+0x1260` dwords e,
no ensaio 3.29, aceitou `DOORBELL_ENABLE` e releu `0x2`. Isso ancora a base de
forma independente do próprio `0xC20C`. O registrador é o certo; o que não é
retido é o bit.

### 2.2 A telemetria nova funcionou

`RestorePassed=1` com `RestoreFailureCount=0` e estágio `NONE`: o teardown foi
íntegro e a instrumentação de rollback do 3.30 se comportou como projetada —
ela simplesmente não teve falha para reportar.

## 3. Dois defeitos do 3.30, corrigidos no 3.31

### 3.1 O `EN` não deveria ser fatal

Tratar `EN` como pré-requisito era uma hipótese, não um fato demonstrado. Ao
abortar ali, o ensaio nunca chegou a `CP_PQ_WPTR_POLL_CNTL1` — que é o lever de
verdade e continua sem nenhuma medição física — nem ao WPTR pré-`ACTIVE`, ao
`INIT_FETCHER` ou ao `ACTIVE`. Um boot inteiro sem medir a peça principal.

O comentário upstream em `kgd_gfx_v9_hqd_load` atribui à `QUEUE_MASK` a
ativação da lógica de **doorbell** para monitorar mudanças; nada garante que
isso dependa de `EN`.

No 3.31, `CP_PQ_WPTR_POLL_CNTL` vira observação registrada e não-fatal.
**Isto não afrouxa nada**: o gate do tripé segue exigindo `RPTR==WPTR`, fence
física com veneno verificado e dado conferido byte a byte. O que sai é apenas
um abort por etapa cuja necessidade nunca foi demonstrada.

### 3.2 Sonda de gravabilidade

Duas hipóteses restam para o `EN`, e elas se separam com duas operações MMIO:
escrever `PERIOD=4` (campo comum, sem efeito de habilitação) e reler.

| `PERIOD` gruda? | `EN` gruda? | Leitura |
|---|---|---|
| sim | não | registrador gravável; `EN` especificamente bloqueado (CP/RLC dono do bit, ou GFXOFF) |
| não | não | registrador read-only neste contexto |

Publicado em `HqdActiveWptrPollCntlProbeApplied`, `…ProbeReadback`,
`…Writable` e `…EnLatched`.

### 3.3 Falso positivo no gate de aceite

O aceite do 3.30 reprovou com `rollback EOP_RPTR divergiu: before=0x40000000,
restored=0x00000000` apesar de o `EOP_RPTR` **nunca** ter sido escrito — a
comparação não checava o flag de execução da etapa. Corrigido: só compara
quando `HqdActiveEopRptrApplied != 0`.

## 4. Testes que travam a regressão

- **9.2.3** — o fake recusa `EN[31]`, reproduzindo o silício do 3.30. Exige que
  a transação **prossiga**: `WptrPollCntl1Applied=1`, `PreActiveWptrPublished=1`,
  `EopRptrApplied=0x80000000`, `ActiveConfirmed=1`. E exige que, sem execução
  real, `TriadPassed=0` e `PhysicalReady=0` — prova de que nada foi afrouxado.
- **9.2.4** — registrador inteiramente read-only: `Writable=0`, `EnLatched=0`,
  e a sequência segue mesmo assim.
- 9.1, 9.2.1 e 9.2.2 (máscara pela topologia, estágio de rollback) seguem
  valendo.

`build-contract.bat` sai com `EXIT=0` e sem warning no código de kernel.

## 4.1 A evidência canônica do 3.30 se perdeu — e a causa foi corrigida

O `confirm-bc250mcdm-lab.ps1` nunca chegou a rodar neste ciclo: o watchdog
expirou e fez rollback automático às `10:23:42`, três minutos após armar,
removendo o serviço `bc250mcdm` e com ele a chave `Parameters\Probe`. O
diretório de evidência ficou só com `watchdog-state.json` e
`watchdog-rollback.txt`; não existe `probe-registry.json` para o 3.30.

Os valores da seção 2 vêm de uma leitura ao vivo do registro feita enquanto o
driver ainda estava vinculado, preservada com procedência explícita em
`docs/evidence/2026-08-30_10-20-40-DESKTOP-HQAG6T4-lab-binding/probe-registry-manual-capture.md`.
Esse arquivo **não** é saída do coletor e diz isso na primeira linha.

Causa raiz: a captura dependia de uma ação humana dentro da janela do watchdog.
Num ciclo caro, com uma única submissão por boot, isso é frágil por construção.
O `bind-bc250mcdm-lab.ps1` agora despeja a telemetria em
`probe-registry-at-bind.json` e `probe-registry-at-bind.txt` **imediatamente
após o binding**, antes de o watchdog poder disparar. A captura é best-effort e
envolvida em `try/catch`: ela nunca pode derrubar o ensaio nem o watchdog.
Os dois caminhos foram exercitados fora do hardware — chave ausente (só emite
aviso) e chave presente (gera JSON e `reg export`).

## 5. Artefatos do pacote 3.31

```text
DriverVer              08/30/2026,3.31.0.0
Schema                 102
recipe cp/vmid0/sdma   1 / 0 / 0
Package SYS SHA-256    884BE1FF210AECBFC0A8DE8B568F6AF8725D32414C196C2DCCC8CE20FE7DBB4C
Package INF SHA-256    BCFABAE1129418014010D8DC7395A560F863158A54FAA4D472C267E43FEAA268
audit-bc250mcdm-lab-package.ps1   PASS
```

Símbolos novos confirmados como strings UTF-16 dentro do `.sys` assinado.
O hash difere do 3.30 (`66D5929C…`), como tem de ser.

## 6. Procedimento do próximo ensaio

O ciclo 3.30 já foi encerrado: o watchdog fez o rollback automático e o
`rollback-bc250mcdm-lab.ps1` confirmou `BasicDisplay / Código 0`, com serviço,
chave de telemetria e marcador OEM ausentes. Restou o `bc250mcdm.sys` residual
em `System32\drivers`, que a quarentena remove no passo 2.

Em boot limpo:

1. Reboot manual.
2. `quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine`.
3. `audit-bc250mcdm-prebinding.ps1 -LabPackage` → PASS.
4. `bind-bc250mcdm-lab.ps1 -EnableShaderDispatch -Execute -IAcceptBlackScreenRisk`.
5. `confirm-bc250mcdm-lab.ps1` com o token impresso, **dentro do watchdog**.
   A telemetria já terá sido salva no passo 4 (seção 4.1), mas o `confirm` é o
   que desarma o rollback automático e fecha a evidência canônica.
6. `test-shader-dispatch-acceptance.ps1`.

Uma só submissão física por boot.

## 7. Como ler o resultado do 3.31

| Observação | Leitura |
|---|---|
| `WptrPollCntlWritable=1`, `EnLatched=0` | `EN` é bloqueado por dono externo (CP/RLC/GFXOFF) — investigar quem |
| `WptrPollCntlWritable=0` | `0xC20C` é read-only neste contexto — rever precondição, não o offset |
| `WptrPollCntl1Readback` sem o bit da fila | `CNTL1` também não é gravável aqui; a hipótese do doorbell cai |
| Máscara pega, `PQ_EMPTY` cai, `RPTR` avança | Hipótese confirmada; seguir para resultado e fence |
| Máscara pega, `PQ_EMPTY` permanece | Bloqueio a jusante do fetch, não na publicação do WPTR |
| `FirstRestoreFailureStage != 0` | A falha de rollback do 3.29 fica localizada |

**NÃO VALIDADO EM HARDWARE.**
