# 199 — Handoff 2026-09-08 · Faixa A16 · a ordem do segundo preflight

> **Corrida analisada:** 08/09 11:48 (P0, bind do candidato lab 3.90.1.0)
> **Trabalho desta faixa:** OFFLINE, zero corridas · pacote 3.92.1.0 assinado e **NAO INSTALADO**
> **Veredito:** Node 1 **NÃO VALIDADO EM HARDWARE**

---

## 1. O que a corrida 11:48 mediu

| Etapa | Resultado |
|---|---|
| Fusível anti-boot-loop | ARMADO (Boots=4) |
| EDID sintetizado / GOP | 2560x1080, Pitch=10240, Phys=0xC0000000 |
| PSP Load SDMA0 FW / F32 | Status=0, PhysicalReady=1 |
| Tripé de boot | `TRIPE_PROVADO` — RPTR_Reg=68, Casou=REGISTRADOR, Fence=0x1001, Polls=1 |
| MMU global GFXHUB | Regs=26, TLB=0x1D59, L2=0x80E01, ACK em 1 poll |
| **StartDevice** | **0xC00000BB** — Código 43 |
| Bring-up físico (CP/HQD/submissão) | **zero linhas de log** |
| `compute-display-prerequisite-gate.json` | `UMD_NODE1_SKIPPED` |

O gate do runner agiu certo: sem primária, sem `CommitVidPn` e sem
`SetVidPnSourceAddress`, o tripé Node 1 não era elegível. **A corrida do B7 foi
gasta sem produzir nenhum dado sobre o Node 1.**

Evidência: `docs/evidence/2026-09-08_11-48-12-DESKTOP-HQAG6T4-lab-binding/`.

---

## 2. Causa raiz (P1 — código + telemetria, não hipótese)

O bind das **09:50**, no mesmo perfil `DISPLAY_RENDER_LAB` e com o pacote
anterior, fechou em Código 0. A comparação dos probes isola o ponto:

| probe | 09:50 (Código 0) | 11:48 (Código 43) |
|---|---|---|
| `RuntimeStageStatus` | 0 | 0 |
| `SecondPreflightStage` | **8** (`PREFLIGHT_READY`) | **3** (`RUNTIME_MISSING`) |
| `SecondReadonlyGatePassed` | 1 | 0 |
| `V5StartDeviceStatus` | 0 | 0xC00000BB |

A cadeia:

1. Um bloco novo em `Bc250DxgkDdiStartDeviceActive` mapeia
   `Core.MmioTransport` e chama `Bc250ActiveEnsureGlobalMmu`. Ele fica **antes**
   do segundo preflight readonly, no caminho comum. É novo: as duas linhas de
   log da MMU global não existem no log das 09:50.
2. O bloco deixa o transporte **mapeado**.
3. `Bc250DxgkDdiStartDeviceReadonly` exige, na **guarda de entrada**,
   `MmioTransport.State == BC250_MMIO_TOPOLOGY_EMPTY`; caso contrário devolve
   `STATUS_INVALID_PARAMETER` — **antes** de escrever `LastStage`.
4. Por isso `SecondPreflightStage=3` é **resíduo do primeiro preflight**.
   Qualquer caminho que passe da guarda escreve `INTERFACE_ACCEPTED (1)` no
   mínimo; o 3 **prova** que a segunda chamada saiu antes disso.
5. Sem `ReadonlyGatePassed==1 && LastStage==PREFLIGHT_READY`, todo o bring-up
   físico foi pulado — HQD, PFP, submissão persistente.

A guarda está **certa**: ela existe para que o preflight rode sobre transporte
limpo. Errada era a ordem.

### 2.1 O segundo defeito, que escondeu o primeiro

`persistentStatus`, `selfTestStatus` e `p2Status` nascem em
`STATUS_NOT_SUPPORTED` e só são escritos **dentro** do ensaio de submissão
persistente. Como o bind pediu `PersistentSubmission=1` e `persistentReady`
ficou `FALSE`, o rodapé do StartDevice publicou o **inicializador** de
`persistentStatus` como veredito.

**O `0xC00000BB` não foi produzido por medição nenhuma.** A falha real —
`STATUS_INVALID_PARAMETER` no segundo preflight — ficou invisível.

---

## 3. O que foi corrigido (offline)

### 3.1 Ordem — `bc250_active_miniport.c`

O transporte volta a **vazio** antes do segundo preflight, com falha fechada se
o unmap não fechar. Guardado por `#if defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)`:
o caminho de **produção** mantém o mapa durante toda a vida do dispositivo por
design e não executa segundo preflight — comportamento inalterado.

Desmapear ali é seguro e não desfaz nada:

- os registradores da MMU global **já** foram programados (`GlobalMmuReady=TRUE`,
  e o sítio preguiçoso em `SetRootPageTable` só reprograma quando é `FALSE`);
- cada transação do bring-up físico mapeia e desmapeia o transporte por conta
  própria. `Bc250MmioTopologyMapReadonly` devolve `WRONG_STATE` se encontrar o
  transporte já mapeado — **deixá-lo mapeado quebraria o bring-up inteiro**, não
  apenas o preflight.

### 3.2 Telemetria — o estágio sozinho é ambíguo por construção

Probe novo `BC250_PROBE_SECOND_PREFLIGHT_STATUS` (`SecondPreflightStatus`) com o
`NTSTATUS` **cru** da segunda chamada, mais log com estágio, gate e estado do
transporte. Acrescentado no **fim** do enum, para não deslocar índice de nenhum
campo existente.

### 3.3 O rodapé parou de mascarar

`persistentAttempted` marca que o ensaio de submissão persistente **aconteceu**.
Sem ele, um bring-up que parou antes publicava o inicializador. Falha real já
carregada em `preflightStatus` manda; e submissão persistente pedida e não
ensaiada nunca vira `STATUS_SUCCESS`.

---

## 4. Como isso fica travado

**Teste** — `driver/tests/dxgk_readonly_ddi_test.c` reproduz a corrida offline:

- runtime **STAGED** + transporte **MAPEADO** → `STATUS_INVALID_PARAMETER`
  (explicitamente **não** `STATUS_NOT_SUPPORTED`), `LastStage` intocado,
  `RuntimeStaged` continua 1;
- contraponto: transporte vazio, mesmo runtime → estágio 8, gate 1. Sem essa
  metade, a asserção anterior seria satisfeita por um preflight quebrado de
  qualquer outra forma.

**Gate** — `audit_second_preflight_transport_empty` em
`tools/audit-bc250mcdm-safety.py`: exige o unmap na janela entre a MMU global e
o segundo preflight, exige o probe do status cru, exige `persistentAttempted`, e
detecta se o **teste** perder as asserções.

### 4.1 Mutações verificadas — quatro, e a primeira reprovou o gate novo

| # | Mutação | Esperado | Medido |
|---|---|---|---|
| 1 | remove o unmap antes do preflight | FAIL da auditoria | **PASSOU** → gate defeituoso |
| 2 | rodapé volta a mascarar | FAIL | FAIL, 2 erros |
| 3 | teste perde a prova de `RuntimeStaged` | FAIL | FAIL |
| 4 | remove a guarda do transporte no readonly | FAIL do teste | `TESTE FALHOU (dxgk_readonly_ddi_test)` |

A mutação 1 expôs um **falso negativo silencioso no meu próprio gate**: a busca
por `preflightStatus = Bc250DxgkDdiStartDeviceReadonly(` casava com o
**primeiro** preflight, no topo da função; a janela saía invertida e o gate
retornava sem examinar nada. Corrigido — a busca começa depois da MMU global, e
o retorno silencioso virou erro. Re-testado: mutação 1 agora reprova.

---

## 5. Estado de build

```
driver/wdk/build-contract.bat          EXIT=0
tools/audit-bc250mcdm-safety.py        PASS
tools/audit-mcdm-ddi-matrix.py         PASS (STRUCTURAL ONLY)
tools/audit-ddi-noop-callbacks.py      98 anunciadas, 3 dívidas, 1 aviso, 0 falhas
```

### 5.1 Pacote 3.92.1.0 — construído, assinado, auditado, **não instalado**

| campo | valor |
|---|---|
| DriverVer | 09/08/2026, **3.92.1.0** |
| BuildKind | `MCDM_LAB_BRINGUP` |
| SYS fonte | `76E5ADCA2141F6DD...` |
| SYS assinado | `A19735479D31536E...` |
| CAT | `99E8E5BEC7CBB483...` |
| INF | `8E960DA602C06F50...` |
| UMD | `2927FCBCE19AA1BE...` (inalterada) |
| TelemetrySchemaVersion | 135 |
| SkipHwBringup / GpuvaLabPromotion | 0 / 0 |
| Node1TripodDefaultArmed / D3D10_11NativeReady | 0 / 0 |

`BC250 MCDM LAB PACKAGE AUDIT: PASS`.

**Prova de que o rebuild pegou** — as cinco marcas da A16 no SYS **assinado**:

```
[StartDevice] Transporte devolvido ao preflight: unmap=%s (State=%u)   PRESENTE
[StartDevice] Segundo preflight: status=0x%08X, ...                    PRESENTE
[StartDevice] RECUSADO fail-closed: transporte MMIO nao voltou ...     PRESENTE
[StartDevice] Submissao persistente pedida e NAO ENSAIADA ...          PRESENTE
SecondPreflightStatus  (UTF-16LE, nome de probe)                       PRESENTE
```

#### Correção de um verificador que eu propus errado

Eu havia dito que `TelemetrySchemaVersion` passaria de 134 para 135 sozinha se o
rebuild pegasse, e que 134 provaria binário velho. **Falso.** É constante fixa em
[`package-bc250mcdm-lab.ps1`](../tools/package-bc250mcdm-lab.ps1) gravada no INF —
não deriva do enum e nunca ia se mover. O sinal válido é o par
`SourceSysSha256` + strings no binário assinado, que é o método que este projeto
já usava.

Corrigido no mesmo dia: `$labSchemaVersion` subiu para **135**, porque o campo
apendado mudou o conjunto de campos e declarar 134 para 135 campos é duas fontes
de verdade; e o INF fonte foi para 3.92.0.0 para não existirem dois artefatos
diferentes sob o mesmo `DriverVer`. Repacotado com o **mesmo** `SourceSysSha256`.

**Nenhuma instalação ou bind foi realizado nesta faixa.**

---

## 6. Estado da máquina

- O pacote 3.90.1.0 instalado às 11:48 **já foi revertido** pelo watchdog;
  BasicDisplay em Código 0.
- `C:\BC250_REQUIRE_CLEAN_BOOT.json` está **ARMADO**. O binder recusa novo bind
  até um boot frio. **Não apagar o marcador manualmente** — o binder o libera
  ao detectar o novo boot.

---

## 7. Próximo passo, e ele mede **uma** coisa

Boot frio, depois bind **sem** `-RunUmdNode1Tripod`. A pergunta é única:

> o StartDevice volta a Código 0 com a MMU global ligada?

- **PASS:** `SecondPreflightStage=8`, `SecondReadonlyGatePassed=1`,
  `SecondPreflightStatus=0xC00000BB` (o `STATUS_NOT_SUPPORTED` **legítimo** do
  fim do gate readonly, não o mascarado), `V5StartDeviceStatus=0`, Código 0, e as
  linhas de MMU global presentes — que é o que 09:50 **não** tinha.
- **FAIL:** qualquer outro desfecho. **Critério de abandono:** não repetir no
  mesmo boot. O `probe-registry-at-bind.json` já carrega o dado; a análise é
  offline.

Só depois disso a corrida do `B7_UMD_NODE1_TRIPOD`. Todo o trabalho anterior
(runner, token de 128 bits, gate de hash da UMD, watchdog) continua válido — só
ficou inalcançável.

**Node 1: NÃO VALIDADO EM HARDWARE.**

---

## 8. Observação de processo

Três das últimas quatro corridas morreram em **defeito de host**, não de
silício: comparação crua de RPTR (17:43), ordem do registro no ensaio de TDR, e
agora a ordem do preflight. O padrão é o mesmo — código novo entra no caminho
crítico sem um teste que fixe a **sequência**, e a corrida física paga a conta.
As seções 4 e 4.1 existem para quebrar esse ciclo.
