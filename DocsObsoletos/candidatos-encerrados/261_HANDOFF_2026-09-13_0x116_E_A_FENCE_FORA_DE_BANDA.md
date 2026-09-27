# Handoff 261 — O BugCheck 0x116 da corrida 22:57 não foi hang de engine: a fence monitorada era sinalizada fora de banda

**Data:** 2026-09-13
**Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2)
**Ambiente:** Windows 10 Pro 22H2 x64 | DisplayPort 2560x1080
**Corridas gastas nesta sessão:** ZERO. Tudo saiu de dump, de log recuperado e de leitura de fonte.

> **Este documento corrige o [`260`](260_HANDOFF_2026-09-13_PASSAGEM_DE_BASTAO_COMPOSICAO_DWM_E_TDR_0x116.md).**
> O 260 atribuiu o `0x116` a uma espera bloqueante de CPU estourando o watchdog de 2 s e mandou
> "tornar o Blt assíncrono". As duas coisas estão erradas, e seguir o roteiro dele não corrigiria
> nada. O que segue é o que a medição diz.

---

## 1. A evidência que não existia

A pasta da corrida 3 tinha `display-log-archive.json` com o texto
`display-log-this-bind.txt ausente no fim do bind` — o BugCheck matou o watchdog antes do
arquivamento. É a mesma classe de perda que a faixa C2 de 10/09 documentou.

O log vivo `C:\BC250_DISPLAY_LOG.txt` é cumulativo e sobrevive a reboot. O `display-log-baseline.json`
da corrida registra `start_offset = 1.203.593.958`. A cauda foi resgatada e arquivada:

| Arquivo | Bytes | SHA-256 |
|---|---|---|
| `docs/evidence/2026-09-12_22-57-30-…/display-log-this-bind-RECUPERADO.txt` | 1.308.135 | `5e938f84be558debb8179a7bdea81d4de7d9a470095de8e25ca1fdfc4b32805a` |
| `docs/evidence/dump-0x116-2026-09-12.txt` | 23.984 (UTF-16LE) | análise do `MEMORY.DMP` por `cdb` |

**Sem esses dois arquivos esta análise não existiria.** Ambos estavam em disco o tempo todo.

---

## 2. O que o dump diz — P0

```
nt!KeBugCheckEx
dxgkrnl!TdrBugcheckOnTimeout+0xfe
dxgkrnl!TdrIsRecoveryRequired+0x1b1
dxgmms2!VidSchiReportHwHang+0x62f
dxgmms2!VidSchiCheckHwProgress+0x3318b
dxgmms2!VidSchiWaitForSchedulerEvents+0x372
dxgmms2!VidSchiScheduleCommandToRun+0x2ca
dxgmms2!VidSchiRun_PriorityTable+0x35
dxgmms2!VidSchiWorkerThread+0xca
```

- `Arg3 = 0` — **nenhuma operação nossa falhou**. Contrasta com o `0x116` de 07/09 20:41, que
  trazia `STATUS_DEVICE_NOT_READY` em Arg3.
- **Nenhum quadro de `bc250mcdm` na pilha.** A thread é a do escalonador do dxgmms2.
- `Failure.Exception.IP.Offset = 0x1c950` → `bc250mcdm!Bc250McdmCollectDbgInfo`. Isto é o
  *owner tag* que o dxgkrnl publica em Arg2 para nomear o adaptador responsável. **Não é
  endereço de falha e não houve exceção** — o rótulo `Failure.Exception.IP` é invenção do
  `!analyze`. Não interpretar como "a falha é em CollectDbgInfo".

---

## 3. O que o log recuperado diz — P0

| Medição | Valor |
|---|---|
| IBs executados (`IB EXECUTADO (INDIRECT)`) | **1357** |
| `IB_TRIPOD TIMEOUT` | **0** |
| Eventos `TDR_QUIESCE` | **5** |
| `RPTR` vs `WPTR` nos 5 quiesces | **iguais nos 5** (3268/3268, 27140/27140, 15876/15876, 25988/25988, 12996/12996) |
| `STATUS1` nos 5 quiesces | **`000003FF` = idle nos 5** |
| `TDR_RESTART PROVADO` | **5 de 5** |

**A engine SDMA nunca travou.** Em todos os cinco TDRs o silício estava ocioso e com o anel
completamente drenado, e o nosso par reset/restart funcionou nas cinco vezes.

Cinco TDRs em sequência estouram o `TdrLimitCount` do Windows (padrão 5 em 60 s). Por isso
`TdrIsRecoveryRequired` chamou `TdrBugcheckOnTimeout` **em vez de** tentar a sexta recuperação.
O `0x116` é o Windows desistindo, não o nosso driver falhando em recuperar.

### 3.1 Armadilha do log — um erro meu, apanhado antes de virar veredito

Na primeira passada concluí que a aposentadoria de submissões tinha parado no `FenceId=15`,
porque só existem 16 linhas `[SdmaRetire]` para 1357 IBs. **Isso é falso.** A classe
`[SdmaRetire]` é amostrada em `bc250_display_ddi.c:574` (`BC250_VERBOSE_LOG_FIRST = 16`,
`BC250_VERBOSE_LOG_PERIOD = 1024`), enquanto `IB EXECUTADO` está na lista de exceção que
ignora amostragem. A assimetria era do logger, não do driver. A aposentadoria rodou o tempo
todo.

Fica registrado porque a próxima IA vai olhar o mesmo log e cair na mesma armadilha.

---

## 4. Causa-raiz

`Bc250UmdBltGpu` montava um pacote `SDMA_OP_FENCE` sobre a VA da **fence monitorada** e
submetia com `D3DDDICB_SUBMITCOMMAND` preenchido apenas com `Commands`, `CommandLength` e
`BroadcastContext` — **sem objeto de sincronização** (`driver/umd/bc250_wddm_winsys.c`,
`Bc250WinsysSubmitGpuVa`).

O valor chegava à memória. **O dxgkrnl nunca ficava sabendo.**

Consequência, com a intermitência exata que foi observada: `Bc250WinsysWaitForFenceFromCpu`
tem um atalho que devolve `OK` se a fence já atingiu o alvo. O 1º blt ganha a corrida contra a
GPU e passa pelo atalho — foi o `Substituidos=1` da corrida. O 2º blt (janela 590x800, 800
pacotes de scanline) não ganha, cai no `pfnWaitForSynchronizationObjectFromCpuCb` de verdade e
**bloqueia para sempre**, porque ninguém vai sinalizar. O `VidSch` vê o contexto sem progresso
e reporta hang — com a engine ociosa.

Isso casa com o log da UMD (`bc250_umd_log_1204.txt`), que termina em `22:57:36.223` no
`ResourceUnmap` anterior ao 2º draw: o trace `Draw SUBSTITUIDO` só é emitido **depois** de
`Bc250UmdBltGpu` retornar, então a DDI entrou e não voltou.

Segundo defeito no mesmo sítio: `Bc250SdmaEmitFence` é de **32 bits** e a fence monitorada do
WDDM é de **64**. Existe `Bc250SdmaEmitFence64` ao lado.

**Correção do 260:** o item A.1 ("tornar o Blt assíncrono") não tem o que fazer. A submissão do
Node 0 **já é assíncrona** desde antes desta corrida — 1974 marcadores `(async)` no log, fila
multi-slot in-flight, aposentadoria por DPC/timer com `DMA_COMPLETED` em lote, e o portão
`audit_sdma_submission_is_asynchronous` já existia e passa.

---

## 5. O que foi mudado (OFFLINE, sem corrida)

| Arquivo | Mudança |
|---|---|
| `driver/umd/bc250_wddm_winsys.c` / `.h` | Nova `Bc250WinsysSignalMonitoredFenceFromGpu`, sobre `pfnSignalSynchronizationObjectFromGpuCb`. Falha fechada se o callback faltar; recusa valor 0, fence não criada e contexto não criado. |
| `driver/umd/bc250_umd_blt.inl` | Removido o `Bc250SdmaEmitFence` do stream do blt. O `GCR` permanece — as cópias precisam estar visíveis antes do sinal. Após o submit, sinaliza pelo contrato. |
| `driver/tests/bc250_wddm_winsys_test.c` | `TestSignalMonitoredFenceFromGpu`: callback ausente, 4 formas de argumento inválido, caminho feliz com valor exato, falha do runtime propagada, e contexto destruído. |
| `driver/tests/bc250_d3d_umd_test.c` | Stream do blt passa de 128 para **96 bytes**; assere que **nenhum** pacote de fence sobrevive; novo stub `BltTestSignalGpu`; invariante `g_BltSignals == g_BltSubmitsOk` (submit que falha **não** deve sinalizar); novo modo de falha 5 (sinal recusado). |
| `tools/audit-bc250mcdm-safety.py` | Portão `audit_blt_fence_is_signalled_by_contract`. |

Quem materializa a escrita agora é o KMD, em `Bc250McdmBuildSignalMonitoredFence`
(`driver/wdk/bc250_mcdm_callbacks.c:989`) — que **já existia, já usa `Bc250SdmaEmitFence64`** e
nunca havia sido exercitado por este caminho.

### Verificação

- `driver\umd\build-umd.bat` → **EXIT=0**
- `driver\wdk\build-contract.bat` → **EXIT=0**
- `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py`,
  `audit-umd-node1-submission.py` → **PASS**

**Três mutações verificadas:**

1. Remover a chamada de sinal em `bc250_umd_blt.inl` → a suíte da UMD **reprova**.
2. Reintroduzir `Bc250SdmaEmitFence` no stream do blt → o portão **reprova** (`forbidden …`).
3. Fazer o teste voltar a exigir `CommandLength == 128u` → o portão **reprova**, com a mensagem
   de que o teste estaria exigindo a causa do `0x116`. Esta terceira mutação existe porque a
   classe "o gate congelava a mentira" já apareceu quatro vezes neste projeto (A15,
   `mcdm_callbacks_test`, C1, `sdma_blt_executed`).

### O primeiro invariante que escrevi estava errado

Escrevi `g_BltSignals == g_BltSubmits` e a suíte reprovou: submit que **falha** não é seguido
de sinal, e isso é correto — o curto-circuito do `||` é proposital. Corrigido para contar
apenas submits bem-sucedidos.

---

## 6. Estado e o que NÃO está provado

> **`NÃO VALIDADO EM HARDWARE`.** Nenhuma linha desta faixa rodou no silício. O efeito da
> correção sobre o `0x116` é **H**, não P0.

### Pacote do ensaio — assinado, auditado e **NÃO INSTALADO**

| Campo | Valor |
|---|---|
| `DriverVer` | `09/13/2026,3.162.1.0` |
| `BuildKind` | `MCDM_LAB_BRINGUP` |
| Perfil | `DISPLAY_RENDER_LAB` (`RenderNode=1`, `RenderOnlyLab=0`) |
| Contrato WDDM | 2.7 |
| Modo | P1 PERSISTENT SUBMISSION |
| Telemetry schema | 135 |
| SYS fonte | `97A48176093B4C04…` |
| SYS assinado | `59F3D9250DAF584D…` |
| UMD assinada | `6C7C70EB8615D7D4…` |
| INF | `9085671C617CD183…` |
| CAT | `A7243D41390DABB0…` |

`audit-bc250mcdm-lab-package.ps1` → **PASS**.

**Rebuild da UMD provado por string, não presumido:** o empacotador **não compila, só copia** —
pular o build assina o binário anterior. As duas linhas `SignalMonitoredFenceFromGpu ENTER` e
`… LEAVE` foram encontradas dentro da `bc250umd.dll` assinada do pacote. O SYS não mudou de
comportamento nesta faixa (a correção é toda de UMD), mas foi relinkado.

### Dívidas declaradas, não tocadas

1. **Espera síncrona no thread do DWM.** Com o sinal correto o wait passa a ser satisfeito, mas
   `Bc250UmdBltGpu` continua bloqueando dentro da DDI de `Draw`. Isso é custo de FPS, não mais
   correção. Deliberadamente fora do escopo desta faixa: uma mudança falsificável por vez.
2. **800 pacotes de scanline** para uma janela de 590x800. O `COPY_LINEAR_SUB_WINDOW` do SDMA
   v5.0 faria em um pacote, mas **não há procedência local** para o layout dele (Lei 7) —
   `inc/linux_ref/` só tem `gc_10_1_0_sh_mask.h`. Não inventar o encoding.
3. **`Journal NO_SPACE`** — 8 ocorrências, mais um
   `[ActiveFlushTlb] … -> STATUS_IO_DEVICE_ERROR`. Começam **depois** do 1º TDR, logo são
   paralelo ou consequência, **não** a origem. Classe já conhecida (doc 252).
4. **Reutilização do `BltCommand`.** Há uma única alocação de comando sobrescrita a cada blt.
   Hoje isso é seguro **porque** o wait é síncrono. Qualquer tentativa futura de tornar o Draw
   não-bloqueante precisa de anel de slots de comando antes, ou a GPU lerá um buffer já
   reescrito.

---

## 7. Critério da próxima corrida — escrito ANTES

Os dois desfechos informam:

- **Hipótese confirmada:** o ciclo do DWM passa do 2º draw, `Substituidos` cresce além de 1, e
  a corrida termina **sem** `TDR_QUIESCE` ou com no máximo 1. A parede seguinte passa a ser FPS,
  não correção.
- **Hipótese refutada:** reaparecem 5 `TDR_QUIESCE` com `RPTR==WPTR` e `STATUS1=0x3FF`. Aí a
  fence não é o único caminho pelo qual o dxgkrnl perde o progresso, e o próximo suspeito é o
  contexto de render do DWM, não o do blt.

Procurar no log do bind: linhas `[SignalMonitoredFence] Completo: FenceGpuVa=… Value=…` — se
elas **não** aparecerem, o sinal não chegou ao KMD e o problema é anterior, na winsys.

**Antes de qualquer bind:** fusível e boot frio, conforme a norma.
