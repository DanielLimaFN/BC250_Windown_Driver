# 184 — B6 / item 25: o par de TDR deixou de devolver sucesso sem silício

**Data:** 2026-09-07, depois de B3/B4
**Corridas gastas:** **zero** (offline)
**Estado:** quiesce e reinício físicos implementados e travados por teste e gate.
**Item 25 (TDR) continua ABERTO:** falta a corrida com hang induzido.
**NÃO VALIDADO EM HARDWARE.**

Continuação de [`183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md`](183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md).

---

## 1. O defeito encontrado por leitura

`bc250_mcdm_callbacks.c`, antes desta faixa:

```c
NTSTATUS APIENTRY Bc250McdmResetFromTimeout(CONST HANDLE hAdapter)
{
    Bc250DisplayLog("[McdmResetFromTimeout] Called - recovering adapter without BSOD\n");
    if (hAdapter == NULL) return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;      /* <-- nada foi tocado no silicio */
}
```

O mesmo em `RestartFromTimeout`. As duas escreviam uma linha de log e declaravam sucesso
**sem tocar em um único registrador** — violação direta da Lei 4, e no par de DDIs em que o
`dxgkrnl` confia para decidir se o adaptador foi quiescido e se voltou. A consequência
prática de mentir aqui é o sistema seguir submetendo para uma engine parada.

**Agravante:** `mcdm_callbacks_test` **exigia** esse `STATUS_SUCCESS`
(`CHECK(Bc250McdmResetFromTimeout((HANDLE)0x250) == STATUS_SUCCESS)`) sem executor algum
instalado. O teste congelava a mentira — mesma classe do gate que **obrigava** a comparação
crua de RPTR a existir (A15).

Isso ficou urgente por causa do B3: com o default em somente `INDIRECT`, submissão que não
resolve devolve `STATUS_IO_DEVICE_ERROR`, e o caminho conhecido dali é o `0x119` que já
derrubou a máquina às 17:45.

## 2. O que foi implementado

### 2.1 Quiesce físico — `Bc250SdmaPresentQuiesceForTdr()`

- Snapshot antes de tocar: `RPTR`, `WPTR`, `STATUS`, `STATUS1`, `ERR_LOG`, `RB_CNTL`,
  `F32_CNTL` e se o hardware estava habilitado.
- Fecha a porta para submissão nova **antes** de mexer no silício (`HardwareEnabled=FALSE`),
  para que quem checa o flag falhe fechado em vez de escrever num anel sendo desligado.
- Desliga `RB_ENABLE` e **trava o F32** (`F_F32_HALT`) para parar o fetch em andamento.
- **Não desmapeia o BAR.** `Bc250SdmaPresentTeardown` faz isso e serve para `StopDevice`;
  usar teardown aqui tornaria o reinício impossível na mesma sessão de TDR.
- Sem transporte (`Initialized=0` ou `MmioVirtual=NULL`) devolve **FALSE** — não houve
  quiesce, e quem chama não pode declarar sucesso.

Log: `[SdmaPresent] TDR_QUIESCE: RB_ENABLE desligado e F32 travado (… RPTR=… STATUS=… ERR_LOG=… RB_CNTL=X->Y F32_CNTL=X->Y)`.

### 2.2 Reinício com prova — `Bc250SdmaPresentRestartAfterTdr()`

1. zera cursor e os dois lados do ponteiro (registrador e writeback), com `HDP` flush;
2. destrava o F32; religa `RB_ENABLE` e `IB_ENABLE`;
3. habilita **provisoriamente** o hardware — é o único jeito de usar o próprio caminho de
   submissão como prova;
4. **prova física:** um NOP com GCR flush e fence sobre poison (`Bc250SdmaPresentSyncNop`).
   Se a fence não for retirada, `HardwareEnabled` e `TripodValidated` voltam a `FALSE` e a
   função devolve **FALSE**.

Log: `TDR_RESTART PROVADO` ou `TDR_RESTART FALHOU: … HardwareEnabled volta a FALSE; adaptador continua recusando submissao`.

### 2.3 As DDIs

| DDI | Antes | Agora |
|---|---|---|
| `ResetFromTimeout` | log + `STATUS_SUCCESS` | executor de quiesce; sem ele ou em falha → `STATUS_DEVICE_NOT_READY` |
| `RestartFromTimeout` | log + `STATUS_SUCCESS` | executor de restart **com prova de fence**; falha → `STATUS_DEVICE_NOT_READY` |

Os executores entram por `Bc250McdmInstallTdrExecutors()`, o mesmo padrão de
`InstallDebugLifecycle`/`InstallSubmissionExecutor` — existe porque as suítes offline linkam
`bc250_mcdm_callbacks.c` **sem** o módulo de transporte físico. Ponteiro ausente = DDI falha
fechada, por construção.

`ResetEngine` continua recusando (`STATUS_NOT_SUPPORTED`) com o comentário honesto que já
tinha: sem devolução dos pins das fences abortadas, sucesso por node seria falso.

## 3. Testes

| Item | Teste nomeado | Resultado |
|---|---|---|
| contrato das DDIs | `mcdm_callbacks_test`, bloco 9 **reescrito**: sem executor → `DEVICE_NOT_READY`; executor que devolve `FALSE` → `DEVICE_NOT_READY`; executor `TRUE` → `SUCCESS`; desinstalar volta ao fail-closed (sem sucesso residual), com contadores conferindo quantas vezes cada lado foi chamado | PASS |
| gate | `audit_tdr_pair_has_backend`: exige o executor nos dois corpos, exige caminho de recusa, exige `proven = Bc250SdmaPresentSyncNop();` no reinício, e **detecta se o teste voltar a exigir `STATUS_SUCCESS` sem executor** | PASS |
| suíte | `build-contract.bat` | `EXIT=0` |

**Mutação verificada:** trocar a guarda do quiesce por `if (0)` e a prova por `proven = TRUE`
→ `FAILED: ResetFromTimeout sem executor fisico` + `missing prova fisica no reinicio pos-TDR`.

## 4. Pacote `3.86.0.0` — assinado, auditado, NÃO INSTALADO

| Item | Valor |
|---|---|
| SYS fonte | `11F5EEAE26ABAF43CDBFC66758F59B7715097E769AD11325D97A8E2898EFE441` |
| SYS assinado | `85042920BE388CD718591FE21E052B33C795F8DFC2E57BCBAED9C88275290337` |
| Catálogo | `FA713F1942574048DF880A04F5A2552EB35FFD84B99F3BDAE9D5D8C80B1B1D42` |
| `SdmaPlanoAFallback` | `0` · `GpuvaLabPromotion` `0` |

Frescor no SYS assinado: `TDR_QUIESCE`, `TDR_RESTART PROVADO`, `TDR_RESTART FALHOU`,
`McdmResetFromTimeout] TDR`, `McdmRestartFromTimeout] TDR`.

## 5. O que falta para fechar o item 25 — e é corrida, não código

O item 25 exige a cadeia inteira **medida**: hang induzido → detectado pelo `dxgkrnl` →
engine resetada → adaptador volta a aceitar submissão → **display sobrevive**. O que existe
agora é o backend das duas DDIs que essa cadeia usa.

O que ler numa corrida que provoque TDR:

| Linha | O que decide |
|---|---|
| `[McdmResetFromTimeout] TDR: quiesce fisico do SDMA0 iniciado` | o `dxgkrnl` chamou o par |
| `TDR_QUIESCE: … STATUS=… ERR_LOG=…` | o estado da engine no momento do hang, que é o dado que nunca tivemos |
| `TDR_RESTART PROVADO` | a engine voltou **com fence física** |
| `TDR_RESTART FALHOU` | a engine não voltou, e o adaptador recusa em vez de mentir |
| ausência das duas | o `dxgkrnl` não chamou — o timeout virou bugcheck antes |

**Dívida declarada:** ainda não há hang induzido de propósito no arsenal (`tools/`), e a
detecção de duas passagens do `bc250_tdr_recovery_contract` **não está ligada** a um
watchdog interno do driver — quem detecta hoje é o `dxgkrnl`, pelo timeout de 2 s. Ligar a
detecção própria é o passo seguinte do item 24.

Até essa corrida: TDR, Node 1 executando, desktop acelerado e jogos —
**NÃO VALIDADOS EM HARDWARE**.
