# Resultado físico do 3.44 e candidato 3.45 — a borda do `CP_HQD_ACTIVE`

**Data**: 30/08/2026
**Ensaio do 3.44**: `docs/evidence/2026-08-30_16-55-48-DESKTOP-HQAG6T4-lab-binding`
**Estado do 3.45**: construído e testado offline. **NÃO VALIDADO EM HARDWARE.**

---

## 1. O que o 3.44 provou

Boot frio, cadeia inteira aprovada até o compute:

| Etapa | Resultado |
|---|---|
| SDMA | `SdmaRingWorkStatus=0`, `CntlBefore=0xC2` (limpo), `RptrAfter=52` |
| CP / PFP Direct Load | `PhysicalReady=1` |
| HQD one-shot | `TriadPassed=1` |
| **Shader observável** | `ShaderPhysicalReady=1` — **terceira reprodução** |
| **Ativação persistente** | `P1ActivateStatus=0` — modo persistente do MQD funcionou no silício |
| Encenação do IB | 45 dwords, readback byte a byte aprovado |
| Submissão | aceita; `WPTR=12` publicado e **confirmado no registrador** |

O `ShaderPhysicalReady=1` também fecha uma dúvida de engenharia: a extração do
estágio de restore para `Bc250HqdRunRestoreStage()` e a migração das 48 locais
de snapshot para `BC250_HQD_PERSISTENT_STATE` **não regrediram** o caminho de
tiro único.

## 2. O que o 3.44 reprovou

```text
P1LastWptrMmioReadback = 12    (cursor está no registrador da fila)
P1LastRptrMmio         = 0     (o CP não buscou um dword sequer)
P1LastRptrReportMem    = 0
P1SelfTestPolls        = 100000 (esgotou)
```

Fila ativa, cursor publicado, doorbell tocado no offset correto — e zero fetch.

Cruzando com o que a transação de tiro único mediu **no mesmo boot, na mesma
fila**:

```text
HqdActiveDoorbellControlAfterKick = 0x40000110   (DOORBELL_EN=1, offset 0x110)
HqdActivePostActiveWptrObserved   = 45           (o CP pegou o WPTR na borda do ACTIVE)
HqdActiveWptrPollCntlWritable     = 0
HqdActiveWptrPollCntlEnLatched    = 0
```

**Hipótese**: neste silício o CP amostra o WPTR na **borda** de
`CP_HQD_ACTIVE`. O caminho de tiro único funciona porque publica o cursor
*antes* de ativar; o polling contínuo (`CP_PQ_WPTR_POLL_CNTL.EN`) não é
gravável aqui — o 3.43 já havia registrado `writable=0`, e o 3.44 mostrou que
isso não é detalhe, é o centro do problema. Uma fila persistente que recebe
WPTR depois de ativa não tem, hoje, como ser notificada.

### 2.1 Bug próprio, corrigido no 3.45

A fence do anel em `+0x2200` nunca era inicializada. Entrou com memória velha
(`0x43DAFFFE`), o retire comparou contra a janela em voo, viu delta absurdo e
marcou o motor como `FAULTED`:

```text
P1SelfTestRingFenceObserved = 0x43DAFFFE
P1FenceRegressionCount      = 1
P1EngineState               = 2 (FAULTED)
```

O mecanismo agiu certo — recusou chamar lixo de progresso. O baseline é que
faltava.

## 3. Defeito no ferramental de laboratório (vale para todos os ensaios)

O `confirm-bc250mcdm-lab.ps1` imprimia *"Watchdog desarmado"* e o watchdog
revertia assim mesmo ao fim do timeout. Os dois scripts não compartilhavam o
mesmo contrato:

```powershell
# watch-bc250mcdm-lab.ps1:50
if ($disarm.token -eq $Token -and
    $disarm.instance_id -eq $InstanceId -and
    [bool]$disarm.display_visible) { ... desarma ... }
```

O confirm escrevia `token`, `instance_id` e `disarm_reason` — **nunca**
`display_visible`. Em PowerShell `[bool]$null` é `$false`, então a condição
jamais fechava.

Consequência: **nenhum bind de laboratório deste projeto sobreviveu além dos
180 s**, o que tornava impossível soak, teste interativo pós-bind ou P1 com
múltiplas submissões. Corrigido com uma linha e um comentário explicando o
contrato.

## 4. O que o 3.45 muda

1. **Baseline da fence do anel** zerado na ativação, com readback conferido.
   Se o baseline não assentar, o `Online` falha fechado e desfaz tudo — não
   existe motor online sem base confiável para aposentadoria.
2. **Fotografia dos registradores da fila** em três momentos — após ativar,
   após submeter e após o re-arme: `CP_HQD_ACTIVE`, `HQ_STATUS0`, `PQ_CONTROL`,
   `DOORBELL_CONTROL`, `CP_PQ_STATUS`, `CPC_STATUS`, `HQD_ERROR`, `PQ_WPTR_LO`,
   `PQ_RPTR` e o dump de header do ME2. No 3.44 estávamos cegos exatamente aqui.
3. **Re-arme controlado**: se a GPU não consumir na primeira janela, o
   auto-teste faz `ACTIVE=0 → ACTIVE=1` na **mesma** fila, com o **mesmo** anel
   e o **mesmo** cursor já publicado, e mede de novo.

O re-arme é o experimento decisivo:

- **RPTR anda depois do re-arme** → a hipótese da borda do `ACTIVE` está
  provada, e P1b passa a ter alvo único: notificação de WPTR pós-ativação.
- **RPTR não anda** → a borda do `ACTIVE` não explica, o doorbell deixa de ser
  suspeito, e a investigação vai para `HQ_STATUS0` / `PQ_CONTROL` / `HQD_ERROR`,
  que agora estão capturados.

Nenhuma fence é escrita pela CPU em nenhum desses caminhos. O veredito continua
vindo exclusivamente do que a GPU gravar.

O gate imprime a conclusão na tela, sem obrigar a abrir o JSON.

## 5. Como ensaiar

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

```bash
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableShaderDispatch -EnablePersistentSubmission
```

**Desligue e ligue** — o ciclo das 16:50 foi perdido por resíduo de engine
entre binds (`SDMA0_GFX_RB_CNTL` entrou com `0x400E2`, a programação do próprio
driver de um bind anterior). Estado de engine não cai com desbind.

```bash
powershell -ExecutionPolicy Bypass -File tools\quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine
```

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableShaderDispatch -EnablePersistentSubmission
```

Confirme dentro da janela (agora desarma de verdade) e rode o aceite:

```bash
powershell -ExecutionPolicy Bypass -File tools\test-p1-submission-acceptance.ps1
```

## 6. Estado de compilação

`driver\wdk\build-contract.bat` passa limpo em `/W4 /WX` com todos os testes
offline aprovados. Candidato **3.45.0.0 / SchemaVersion 116**.

---

## 7. Resultado físico do 3.45 — hipótese refutada, causa raiz achada

**Ensaio**: `docs/evidence/2026-08-30_17-09-49-DESKTOP-HQAG6T4-lab-binding`
**Relatório**: `docs/evidence/2026-08-30_17-10-25-DESKTOP-HQAG6T4-p1-submission`

O re-arme **não destravou**: `ACTIVE` foi a 0 e voltou a 1, e o `RPTR`
continuou em zero após 20 000 polls. **A hipótese da borda do `ACTIVE` está
refutada.**

Mas as fotografias novas responderam outra coisa, mais útil:

```text
                   após ativar      após submeter
hq_status0         0x40000040   ->  0x00008040     (QUEUE_IDLE 1 -> 0)
pq_control         0xD030890D   ->  0xD030090D     (PQ_EMPTY   1 -> 0)
cpc_status         0x00000000   ->  0xA0000082     (CPC ocioso -> ocupado)
pq_wptr_lo         0            ->  12
pq_rptr            0            ->  0
hqd_error          0x00000000   ->  0x00000000
```

`PQ_EMPTY` é o bit 15 de `CP_HQD_PQ_CONTROL`. Ele caiu para zero: **o CP
registrou trabalho pendente**, saiu do ocioso e o CPC ficou ocupado. A
notificação de cursor funciona; o doorbell sai da lista de suspeitos. O que
falha é o *fetch* do conteúdo.

**Causa raiz**: o caminho de submissão não faz a invalidação de cache do lado da
GPU. A transação provada executa, imediatamente antes do kick,
`HDP_READ_CACHE_INVALIDATE`, invalidação da `GCVM_L2_CNTL2` e
`GCVM_INVALIDATE_REQ` com poll do ACK
([`bc250_compute_mqd_contract.c:2873-2952`](../driver/core/bc250_compute_mqd_contract.c)).
`Bc250SubmissionPublishWptr()` faz apenas `KeMemoryBarrier()`, que é barreira de
CPU e não afeta o HDP read cache nem a GL2.

O CP vê o cursor (registrador) e busca o anel de uma visão desatualizada da
memória — daí `RPTR=0` com `HQD_ERROR=0` e sem timeout.

A especificação da correção (candidato 3.46) está na seção 5 de
[`131_HANDOFF_IA_APOS_3.45.md`](131_HANDOFF_IA_APOS_3.45.md).

**Veredito: NÃO VALIDADO EM HARDWARE.**
