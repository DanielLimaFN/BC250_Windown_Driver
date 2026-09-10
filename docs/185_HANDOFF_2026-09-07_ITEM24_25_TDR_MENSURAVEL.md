# 185 — itens 24 e 25: detecção própria e hang induzido, as duas dívidas do TDR

**Data:** 2026-09-07, depois do B6
**Corridas gastas:** **zero** (offline)
**Estado:** as duas dívidas declaradas no doc 184 foram fechadas em código.
**O item 25 só fecha com a corrida.** **NÃO VALIDADO EM HARDWARE.**

Continuação de [`184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md`](184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md).

---

## 1. item 24 — `QueryEngineStatus` respondia sem medir

Terceiro falso sucesso encontrado no mesmo dia, e o pior colocado dos três:

```c
pQueryEngineStatus->EngineStatus.Responsive = 1;
return STATUS_SUCCESS;
```

O `dxgkrnl` chama essa DDI **durante o TDR** para saber se a engine está viva. Responder
"viva" sempre faria o TDR concluir que não havia o que resetar — o driver sabotando a
própria recuperação. E, como nos outros dois casos, **`mcdm_callbacks_test` exigia
`Responsive == 1`** sem probe alguma instalada.

**Agora** a resposta sai de medição: `Bc250SdmaPresentProbeResponsive()` amostra `RPTR`/`WPTR`
**duas vezes** com folga entre elas e classifica pelo contrato `Bc250TdrDetectHang`.

| Situação | Resposta |
|---|---|
| sem probe instalada | `Responsive=0`, `STATUS_NOT_SUPPORTED` |
| probe não conseguiu medir | `Responsive=0`, `STATUS_DEVICE_NOT_READY` |
| medição diz viva | `Responsive=1`, `STATUS_SUCCESS` |
| medição diz travada | `Responsive=0`, `STATUS_SUCCESS` |

*"Não sei" não pode virar "está viva".*

**Armadilha herdada, evitada por medição anterior:** os quatro ponteiros são **reduzidos a
uma volta do anel** antes da comparação. `Bc250TdrDetectHang` testa `Wptr == Rptr` para
decidir "ociosa"; com os valores crus da corrida 17:43 (`RPTR=4612`, alvo `516`, anel de
4.096 bytes) ele leria "trabalho pendente" num anel drenado e concluiria **travada** para uma
engine ociosa. O gate exige a redução nos quatro pontos.

Log: `[SdmaPresent] ENGINE_PROBE: Responsive=%u (voltas reduzidas: RPTR a->b WPTR c->d; anel=4096 bytes)`.

## 2. item 25 — o hang induzido que faltava

Não havia nada no arsenal que provocasse o timeout, e sem provocar não há como medir a cadeia
que o item 25 exige.

`Bc250SdmaPresentInduceHangForTdr()` emite um **`POLL_REGMEM` esperando `0xDEADDEAD`** no slot
de fence — valor que o driver nunca escreve —, com retry e intervalo no teto, e faz o kick do
`WPTR`. **A engine para de verdade:** não é `KeStallExecutionProcessor`, não é fence falsa,
não é delay de CPU (o gate proíbe explicitamente a variante simulada).

O ponto sutil, e deliberado: o `SubmitCommandVirtual` **aceita** a submissão e **não notifica
completion**. É a fence pendente que faz o timeout de 2 s do `dxgkrnl` disparar o TDR — que é
o objeto do ensaio. Notificar completion destruiria o próprio teste.

Fail-closed em três camadas: tunable `TdrInduceHang` com default `0`; exige
`present==1 && valor==1`; e `InterlockedCompareExchange(&TdrHangInduced, 1, 0)` garante
**disparo único por bind**, para a corrida não entrar em laço de TDR.

Ferramenta: [`tools/arm-tdr-induce-hang.ps1`](../tools/arm-tdr-induce-hang.ps1), com
`-Arm -IAcceptDeliberateHangAndPossibleBugCheck`. O manifesto do pacote declara
`TdrInduceHang`/`TdrInduceHangArmed` **derivados do INF**, e o binder avisa alto quando o
pacote vem armado.

> **Correção posterior:** a ferramenta não pode armar a primeira instalação, porque a chave
> de serviço só é criada pelo `AddReg` do INF, e o valor é lido no `StartDevice`. Para uma
> corrida única, o INF **empacotado** precisa nascer com `TdrInduceHang=1`. O script de
> registro só vale antes de um **novo** `StartDevice`; não altera o bind corrente. Correção e
> pacote em [`186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md`](186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md).

## 3. Testes

| Item | Teste nomeado | Resultado |
|---|---|---|
| item 24 | `mcdm_callbacks_test`, bloco 6 **reescrito** com os quatro casos da tabela acima, usando stubs `TestProbeAlive` / `TestProbeHung` / `TestProbeCannotMeasure` (esta escreve `1` e a DDI tem de zerar) | PASS |
| item 24 | `audit_engine_status_measures`: exige a probe no corpo da DDI, exige `Bc250TdrDetectHang` na implementação, exige **6+** usos de `Bc250TripodWrapRingPointer` no módulo, e detecta se o teste voltar a exigir `Responsive==1` sem probe | PASS |
| item 25 | `audit_induced_hang_is_fail_closed`: default desarmado, exige `present==1 && ==1`, exige disparo único, exige pacote físico `POLL_REGMEM` e **proíbe** hang simulado por delay | PASS |
| suíte | `build-contract.bat` | `EXIT=0` |

**Mutação verificada:** restaurar `Responsive = 1` fixo → dois FAILs; trocar o
`InterlockedCompareExchange` do disparo único por `1` → `FAILED: missing disparo unico do
hang induzido`.

## 4. Pacote `3.87.0.0` — assinado, auditado, NÃO INSTALADO

| Item | Valor |
|---|---|
| SYS fonte | `A4F6892AA77232311180FDB03E634BD556180A0ACE564070251182698B68D14C` |
| SYS assinado | `AC4E927645793BD56CF345DE6E5934B0546159B62ABCFEC3296B3233336E60E4` |
| Catálogo | `3FBCEDA233D90EE0BABA086E8CEAD30F789E60AA7C8A933C68082E5B3A2F2B89` |
| `SdmaPlanoAFallback` | `0` · `GpuvaLabPromotion` `0` · **`TdrInduceHang` `0`** |

Frescor no SYS assinado: `ENGINE_PROBE: Responsive=%u`,
`TDR_HANG_INDUZIDO ARMADO NO SILICIO`, `TDR_ENSAIO: hang induzido`,
`TdrInduceHang=%u (present=%u)`, `TDR_QUIESCE`.

## 5. A corrida do item 25 — decisão do dono do projeto

O comando abaixo estava na ordem errada e foi corretamente recusado quando a chave ainda não
existia:

```
powershell -ExecutionPolicy Bypass -File tools\arm-tdr-induce-hang.ps1 -Arm -IAcceptDeliberateHangAndPossibleBugCheck
```

Sequência corrigida: construir/auditar o pacote armado `3.88.0.0`, fazer stage sem bind e
então executar o binder com aceitação explícita tanto da tela preta quanto do hang/BugCheck.
O pacote já foi construído e auditado offline; comandos exatos no doc 186. **Não crie a chave
manualmente.**

Sequência esperada no log, em ordem:

| Linha | O que decide |
|---|---|
| `[StartDevice] TdrInduceHang=1 (present=1)` | o flag foi lido |
| `TDR_HANG_INDUZIDO ARMADO NO SILICIO: POLL_REGMEM …` | a engine travou de verdade |
| `TDR_ENSAIO: hang induzido … completion DELIBERADAMENTE nao notificado` | o gatilho armou |
| `[McdmResetFromTimeout] TDR: quiesce fisico do SDMA0 iniciado` | **o `dxgkrnl` entrou no TDR** |
| `TDR_QUIESCE: … STATUS=… ERR_LOG=…` | o estado da engine no hang — dado que nunca tivemos |
| `[SdmaPresent] ENGINE_PROBE: Responsive=0` | a detecção própria concordando com o hang |
| `TDR_RESTART PROVADO` | engine de volta **com fence física** → item 25 avança muito |
| `TDR_RESTART FALHOU` | engine não voltou, e o driver recusa em vez de mentir |
| **nada disso** | o timeout virou BugCheck antes de chamar o par — e isso também é resposta |

**Risco: BugCheck e reboot são desfechos possíveis, e são resultado do ensaio, não acidente.**
Fusível anti-boot-loop cobre a repetição; `tools\BC250_RECOVERY.cmd` cobre o resto; e o
watchdog agora arquiva o log vivo antes do rollback, então o dado sobrevive.

**Item 25 continua ABERTO até essa corrida.** O que ele exige por inteiro é a cadeia com
**display sobrevivendo** — e o display desta rota continua preto por falta do backend
D3D10/11. Ou seja: mesmo com o TDR funcionando, o gate de saída da P6 só fecha de verdade
junto com o B7.

TDR, Node 1 executando, desktop acelerado e jogos — **NÃO VALIDADOS EM HARDWARE**.
