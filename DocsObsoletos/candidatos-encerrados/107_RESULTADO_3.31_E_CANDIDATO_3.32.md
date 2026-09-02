# Resultado físico do 3.31 e candidato 3.32

Data local: 30/08/2026
Ensaiado: **3.31.0.0 / SchemaVersion 102** — **NÃO VALIDADO EM HARDWARE**
Preparado: **3.32.0.0 / SchemaVersion 103** (offline)

Continuação de [`106_RESULTADO_3.30_E_CANDIDATO_3.31.md`](106_RESULTADO_3.30_E_CANDIDATO_3.31.md).

---

## 1. O ensaio mais informativo até agora

O binding entrou em `bc250mcdm / Código 0` com `884BE1FF…` e a captura no bind
funcionou: **596 valores salvos antes de o watchdog poder disparar**. A
evidência não dependeu mais de ação humana — foi o conserto do ciclo anterior
provando seu valor no primeiro uso.

```text
SchemaVersion                        0x66 (102)
HqdActiveStatus                      0x06 (BC250_HQD_ACTIVE_READBACK_FAILED)
```

## 2. Quatro fatos físicos novos

### 2.1 `CP_PQ_WPTR_POLL_CNTL` (`0xC20C`) é read-only neste contexto

| Campo | Valor |
|---|---:|
| `ProbeApplied` (PERIOD=4) | `0x00000004` |
| `ProbeReadback` | `0x00000000` |
| `WptrPollCntlWritable` | `0` |
| `Applied` (EN=1) | `0x80000000` |
| `Readback` | `0x00000000` |
| `EnLatched` | `0` |

A sonda respondeu exatamente o que foi construída para responder: nem `PERIOD`
gruda. Não é o bit `EN` que está bloqueado — o registrador inteiro recusa
escrita aqui. Das duas hipóteses do doc 106 §3.2, sobra a segunda.

### 2.2 `CP_PQ_WPTR_POLL_CNTL1` (`0xC210`) É gravável — primeira medição

| Campo | Valor |
|---|---:|
| `QueueMaskBit` | `0` |
| `WptrPollCntl1Applied` | `0x00000001` |
| `WptrPollCntl1Readback` | `0x00000001` |

A máscara de fila entrou no silício. Esta é a primeira medição física do
`CNTL1` em todo o projeto.

Também encerra qualquer dúvida sobre o offset: `0xC210` aceita escrita enquanto
`0xC20C`, quatro bytes antes, não aceita. Endereço não mapeado não latcheia. A
derivação `dw 0x1E24` + base `+0x1260` estava correta.

### 2.3 `CP_HQD_PQ_WPTR_LO` não aceita o cursor com a fila inativa

| Campo | Valor |
|---|---:|
| `PreActiveWptrApplied` | `40` |
| `PreActiveWptrReadback` | `0` |
| `PreActiveWptrPublished` | `0` |

Foi o que abortou o ensaio (`READBACK_FAILED`), antes de `INIT_FETCHER` e de
`ACTIVE`. Cruzando com o 3.29 — onde a escrita **depois** de `ACTIVE=1` releu
`40` — a regra observada é: o registrador só aceita o cursor com a fila ativa.
Upstream publica antes de `ACTIVE`; este silício não permite.

### 2.4 A `QUEUE_MASK` acende e não apaga

| Campo | Valor |
|---|---:|
| `WptrPollCntl1Restored` (escrevi `0`) | `0x00000001` |
| `FirstRestoreFailureStage` | `13` (`WPTR_POLL_CNTL1`) |
| `FirstRestoreFailureOffset` | `0x0000C210` |
| `FirstRestoreFailureExpected` | `0x00000000` |
| `FirstRestoreFailureObserved` | `0x00000001` |
| `FirstRestoreFailureTransport` | `0` (divergência de valor, não falha de MMIO) |

A telemetria de rollback localizou a falha com precisão cirúrgica — o que o
3.29 não conseguia dizer. **Consequência real: a GPU ficou com o bit ligado, e
só um reset o limpa.** Não é cosmético: num boot morno o snapshot entraria com
o bit aceso e o rollback compararia `1` com `1`, declarando aprovação — um
falso verde.

### 2.5 O que continua sem acontecer

`ActiveConfirmed=0`, doorbell nunca tocado, `RptrAfter=0`, resultado
`0x00000000` e fence `0x00000000` (buffers restaurados). Nenhuma wave.

## 3. O que o 3.32 muda

### 3.1 A publicação pré-`ACTIVE` vira observação

Mesmo erro do `EN` no 3.30: tratei como obrigatória uma etapa que o silício
recusa. O 3.32 registra a tentativa (`PreActiveWptrApplied/Readback/Published`)
e **segue**. Quem publica o cursor de verdade é o caminho pós-`ACTIVE`, que já
existe — agora com a fila dentro da `QUEUE_MASK`, combinação nunca ensaiada:
no 3.29 houve WPTR pós-`ACTIVE` sem `CNTL1`; no 3.31, `CNTL1` sem WPTR.

O gate do tripé não muda: `RPTR==WPTR`, fence física com veneno e dado byte a
byte continuam obrigatórios.

### 3.2 `PostActiveWptrObserved`

Leitura do WPTR imediatamente após `ACTIVE=1` e **antes** de qualquer doorbell
ou escrita direta. Responde sozinha a pergunta que sobrou: com a fila na
`QUEUE_MASK`, o CP busca o cursor do shadow por conta própria? É leitura pura,
sem escrita — nada simulado.

### 3.3 `WptrPollCntl1DirtyEntry` — proteção contra o falso verde

Se a `QUEUE_MASK` já entrar acesa, o campo acende e o aceite **reprova o
ensaio** com a mensagem de que não partiu de boot frio. Sem isso, o item 2.4
viraria uma aprovação silenciosa a partir do segundo ensaio.

### 3.4 Tentativas de limpeza registradas

O rollback do `CNTL1` tenta três vezes e publica `WptrPollCntl1ClearAttempts`.
Se não cair, o gate continua reprovando — não se mascara estado deixado no
hardware.

## 4. Testes

- **9.2.5** — o fake reproduz os três fatos do 3.31 juntos (`CNTL` read-only,
  WPTR recusado com fila inativa, `QUEUE_MASK` que não apaga). Exige que a
  transação **chegue ao fim**: `WptrPollCntl1Applied=1`,
  `EopRptrApplied=0x80000000`, `ActiveConfirmed=1`; que o rollback reprove no
  estágio 13 com `ClearAttempts=3`; e que `TriadPassed=0` e `PhysicalReady=0`
  sem execução real.
- **9.2.6** — boot morno: `WptrPollCntl1Before=1`, `DirtyEntry=1`.
- 9.1 a 9.2.4 seguem valendo.

`build-contract.bat` sai com `EXIT=0`, sem warning no código de kernel.

## 5. Correção de ferramenta validada no ciclo

A quarentena do resíduo falhou no início deste ciclo porque só autorizava o
hash do manifesto atual e o de um relatório de aceite que nunca está onde ela
procura (o script de aceite cria o próprio diretório `-shader-f8b`). Reempacotar
antes de limpar o resíduo destruía a única referência.

O `bind` passou a gravar `package_sys_sha256` em `_last_lab_binding.json` —
registro durável, imune a reempacotamento — e a quarentena autoriza a partir
dele. **Verificado na condição exata que quebrou:** com o manifesto já em
3.32 (`BD28BDD8…`), a quarentena continua autorizando o resíduo `884BE1FF…`.

## 6. Artefatos do pacote 3.32

```text
DriverVer              08/30/2026,3.32.0.0
Schema                 103
recipe cp/vmid0/sdma   1 / 0 / 0
Package SYS SHA-256    BD28BDD88E17862A58DECDD54685031194029399108831CA84073E8AD7985B37
Package INF SHA-256    8BFD2B851A3BC7656E451477DCC792B54A0370BCC8CB7B16AC4A3848542D8D26
audit-bc250mcdm-lab-package.ps1   PASS
```

Símbolos novos confirmados como strings UTF-16 dentro do `.sys` assinado.

## 7. Procedimento

Máquina já reiniciada e em `BasicDisplay / Código 0`, sem serviço e sem
marcador; resta o `bc250mcdm.sys` residual.

1. `quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine`
2. `audit-bc250mcdm-prebinding.ps1 -LabPackage` → PASS
3. `bind-bc250mcdm-lab.ps1 -EnableShaderDispatch -Execute -IAcceptBlackScreenRisk`
4. `confirm-bc250mcdm-lab.ps1` com o token impresso
5. `test-shader-dispatch-acceptance.ps1`

Uma só submissão física por boot. **Se este ensaio rodar, o próximo exige
reboot**, por causa do item 2.4.

## 8. Como ler o resultado

| Observação | Leitura |
|---|---|
| `PostActiveWptrObserved == 40` | O CP buscou o cursor sozinho pela `QUEUE_MASK` — mecanismo upstream funcionando |
| `PostActiveWptrObserved == 0` e `DirectWptrReadback == 40` | Só a escrita direta publica; a `QUEUE_MASK` não basta |
| `PQ_EMPTY` cai e `RPTR` avança | A fila fez fetch — seguir para resultado e fence |
| `PQ_EMPTY` permanece com WPTR==40 | Bloqueio a jusante do fetch, não na publicação do cursor |
| `WptrPollCntl1DirtyEntry == 1` | Ensaio inválido: não partiu de boot frio |

**NÃO VALIDADO EM HARDWARE.**
