# 234 — Auditoria de 10/09/2026: C1 (Draw sem CPU) e C2 (evidência perdida)

> **Escopo:** auditoria das corridas de 10/09 e duas faixas de correção **offline, sem corrida**.
> **Veredito global:** houve avanço real e mensurável, e houve um marco reportado que a evidência
> arquivada não sustentava. As duas coisas são verdade ao mesmo tempo.
> **NADA DE C1/C2 FOI VALIDADO EM HARDWARE.**

---

## 1. O que estava reportado e o que a medição diz

O handoff `docs/233` e a entrada de 10/09 da `BASE_DE_AVANCO.md` declaravam um
**"Marco Histórico: desktop real D3D11 exibido no monitor físico"** na corrida das 11:22.

### 1.1 Confirmado (P0)

| Afirmação | Evidência |
|---|---|
| DWM e Explorer aceitos no D3D11 | `bc250_umd_log_1204.txt:10` e `_5220.txt:10` — `OpenAdapter10_2: Interface=0x000B000B, Version=0x00020009 -> S_OK (BC250 Native)`; também `dxgiadaptercache.exe` (1964) |
| `OpenAllocation` fechou | binds 11:10 e 11:21: 41/41 e 29/29 em `0x00000000`, contra 5.745 falhas / 7.154 chamadas no bind 09:34 |
| SDMA robusto sob carga | 769 submissões INDIRECT consecutivas, 12 voltas completas do anel |
| **Causa-raiz do `0x119` = visibilidade de HDP** | ver §2 — as quatro leituras concordam |

### 1.2 Não sustentado pela evidência

1. **O "desktop real" é de OUTRO bind.** As linhas citadas
   (`[Present #1] *** DESKTOP REAL DETECTADO ***`, `BYTE_PROOF PASS`) estão no offset **8.161.041**
   do log cumulativo, dentro da janela do bind das **09:34** (`start_offset` 7.659.265 →
   14.831.198). O bind das 11:22 (`start_offset` 15.009.172) teve **10 Presents, zero detecção de
   desktop, zero `BYTE_PROOF`, zero `SDMA=YES`**. Três binds foram fundidos num só marco.
2. **O detector dispara com `NonZero=1/256`** — 255 dos 256 dwords amostrados são zero, e
   `SCANOUT`, `PRIMARY` e `SLOT_WORD` amostraram `0/256`. É frame preto com um dword não-nulo.
   No bind das 11:10, com `OpenAlloc` 100% OK, foram 194 Presents e 10 blts, **todos**
   `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`, `BlankStreak=4`.
3. **Nenhum pixel veio do D3D11.** O log da UMD do DWM não tem uma linha de `Present`, `Flush` ou
   blt; tem **1** `DrawIndexed: RECUSADO` e termina 2 s após o load dentro de 4.159
   `CheckMultisampleQualityLevels`. A imagem distorcida vista pelo operador é atribuível à
   transição de adaptador / BasicDisplay — o mesmo engano já cometido e corrigido em 07/09 18:16.
4. **O *stride skew* (7680 → 10240) é H**, não medição: nenhuma linha de `Present` desta corrida
   registra pitch de origem 7680 no caminho de blt.

### 1.3 Correção de rota da própria auditoria

A primeira passada desta auditoria concluiu que a hipótese de HDP estava **refutada**, porque na
pasta `docs/evidence/2026-09-10_11-21-16-.../` as 31 linhas `FENCE_VISIBILIDADE` do corpo inteiro
davam `depois_hdp=43DAFFFE` e `RPTR=50244` não existia em lugar nenhum. **Estava errado**: aquela
pasta estava truncada. Ver §3 — o defeito de coleta é, ele próprio, um dos achados.

---

## 2. `0x119` no FenceId=770: visibilidade, não execução (P0)

Trecho resgatado do log cumulativo (linhas 4107–4115 do tail de 438.038 bytes):

```
[SubmitVirtual Node0 SDMA] IB_TRIPOD TIMEOUT: TargetWPTR=1092, WPTR=1092, RPTR=50244,
    STATUS=46DC6452, ERR_LOG=0000000F, FenceExp=00001304, FenceObs=43DAFFFE
[SubmitVirtual Node0 SDMA] ENGINE_STATE: STATUS1=000003FF (idle se &0x3FF==0x3FF) FREEZE=00000000
[SubmitVirtual Node0 SDMA] FENCE_VISIBILIDADE: antes_hdp=43DAFFFE depois_hdp=00001304
    esperado=00001304 | RPTR_writeback=50244 RPTR_reg=50244
[SubmitVirtual Node0 SDMA] RPTR_SAMPLES (n=8, stride=25000 polls): 50244 x8
[SubmitVirtual Node0 SDMA] IB_QUEUE_DECODED: veredito=BUSY_OR_STALLED IB_ENABLE=1
    SWITCH_INSIDE_IB=1 CMD_VMID=8 IB_RPTR=8 dw IB_SIZE=0 dw IB_SUB_REMAIN=0 dw
```

- `50244 = 12·4096 + 1092` — reduzido a uma volta do anel, **igual ao alvo**.
- Writeback e registrador **concordam**: não é atraso de writeback.
- `STATUS1=0x3FF` — engine **ociosa**. `RPTR_SAMPLES` plano por 200.000 polls.
- A fence apareceu **no flush**: `antes_hdp` poison → `depois_hdp` valor exato esperado.

O flush ativo já implementado em `driver/wdk/bc250_sdma_present.c`
(`Bc250SdmaPresentBlt` e `Bc250SdmaPresentExecuteIndirect`) ataca a causa certa e teria saído do
laço no primeiro poll. **Ainda não validado em hardware.**

### 2.1 Dívida distinta, ABERTA, que o flush NÃO cobre

Nos dois timeouts de 09/09 o comportamento é **outro**:

```
IB_TRIPOD TIMEOUT: TargetWPTR=1092, WPTR=1092, RPTR=1056  (IndirectEndDW=264 -> 1056)
FENCE_VISIBILIDADE: antes_hdp=43DAFFFE depois_hdp=43DAFFFE
RPTR_SAMPLES: 1056 x8
GFXHUB_FAULT_SNAPSHOT: STATUS=00841B40 ADDR=00000000:0000082C     <-- fault de tradução REAL
```

O `RPTR` congela **exatamente em `IndirectEndDW`**, o flush **não** revela a fence, e os dois casos
eram o IB pesado `Fill=1/11.075.584`. O caminho
`TIMEOUT → STATUS_IO_DEVICE_ERROR → 0x119` continua vivo para essa classe. **Item novo, aberto.**

---

## 3. Faixa C2 — a evidência das corridas estava sendo perdida

`C:\BC250_DISPLAY_LOG.txt` é **cumulativo** e sobrevive a reboot. O binder o copia integral para a
pasta de evidência, mas **no fim do roteiro**. Com BugCheck, o watchdog não roda, a cópia não é
refeita e a pasta fica truncada **sem que nada indique**.

Medição de 10/09 — `bytes` do `display-log-archive.json` contra o log vivo (15.584.374 bytes):

| Pasta | Faltavam |
|---|---|
| 09:06:23 | 123.676 bytes |
| 09:34:41 | **6.934.920 bytes** |
| 10:03:46 | 65.987 bytes (nenhum arquivo) |
| 10:23:34 | 89.001 bytes (nenhum arquivo) |
| 11:10:59 | 177.974 bytes |
| **11:21:16** | **438.038 bytes** — continha o `FenceId=770` e a prova de HDP |

**Correção:** `tools/recover-bc250mcdm-lost-tail.ps1` — somente leitura sobre o log vivo, grava
`bc250_display_log-tail-recovered.txt` com SHA-256 e `display-log-tail-recovered.json` com
proveniência. O fim do trecho é delimitado pelo `start_offset` do bind **seguinte** quando existe;
quando não existe, o json **declara** que o trecho pode conter atividade posterior. O binder passou
a chamá-lo **antes** de cada bind, depois de gravar o baseline novo (que é o que delimita o trecho
anterior). Já executado: **6/6 pastas recuperadas**.

---

## 4. Faixa C1 — o fallback de CPU no `Draw` foi removido

Encontrado por leitura de código no candidato **3.126.1.0, ainda não commitado**.

`Bc250UmdCompositeDraw` atendia `pfnDraw` e `pfnDrawIndexed` com um laço de `memcpy` entre os
shadows de CPU de origem e destino, marcava `handled = TRUE` **antes** de tentar qualquer caminho de
GPU, e devolvia sucesso silencioso — a linha honesta `DrawIndexed: RECUSADO` desaparecia. Ignorava
shader, vertex buffer, index buffer, topologia, blend e escala de viewport.

Duas consequências:

1. É **fallback de software disfarçado de renderização** — proibição nominal da Lei 4.
2. Tornaria **infalsificável** qualquer desktop que aparecesse na próxima corrida: não haveria como
   distinguir composição por GPU de `memcpy`.

**Agravante:** `TestCompositeDrawAndUniqueTokens` em `bc250_d3d_umd_test.c` **exigia** esse `memcpy`
(`assert(((uint8_t*)resDst.pCpuShadow)[0] == 0x5A)` e recusa proibida), congelando a mentira. É a
**terceira** ocorrência da classe: A15 obrigava a comparação crua de RPTR a existir, e
`mcdm_callbacks_test` exigia `STATUS_SUCCESS` sem executor de TDR instalado.

### 4.1 O que passou a valer

- `memcpy` **removido**. `Bc250UmdSubstituteDrawWithBlt` é fonte única e o **único** produtor de
  pixel dentro dela é `Bc250UmdBltGpu` (SDMA).
- Elegibilidade em política pura testável, `Bc250UmdDrawBltSubstitutionIsLawful`: exige tunable
  armado, winsys pronto, origem e destino residentes, mesmas dimensões, mesmo formato, viewport na
  origem cobrindo a superfície inteira, e **sem** blend (alpha-to-coverage, blend em qualquer RT
  ativo ou write mask parcial também recusam).
- **Fechado por default**: tunable `D3D11DrawBltSubstitution` (`HKLM\SOFTWARE\BC250\Umd`), ausente =
  desarmado = `E_NOTIMPL` honesto, idêntico ao comportamento anterior à C1.
- O log **declara** o que fez:
  `Draw SUBSTITUIDO por copia 1:1 no SDMA -- NAO e' rasterizacao (shader/VB/IB/topologia ignorados)`.
- `SubstitutedDrawCount` é contado **separado** de `DrawCount`, para que nenhuma corrida futura
  confunda substituição com rasterização.

Substituição por blt **não é** rasterização, e afirmar equivalência seria H apresentada como P0: sem
compilador não se sabe se o PS ligado é um passe identidade. Por isso a janela é estreita e o
default é recusar.

### 4.2 Achado colateral: a suite offline dependia do estado da máquina

`Bc250Umd10CreateDevice` e `Bc250Umd10GetCaps(Type=130)` decidiam inline lendo
`HKLM\SOFTWARE\BC250\Umd\D3D11LabPromotion`. Essa chave ficou em **1** depois do bind das 11:21, e
com ela armada `bc250_d3d_umd_test.c` reprovava em duas asserções **sem nenhuma mudança de código**
— o que refuta o "42/42 PASS" reportado no handoff.

Correção: `Bc250Umd3DPipelineIsDeclared(NativeReady, LabPromotionArmed)` em
`bc250_umd_caps_probe.c` é agora fonte única das duas respostas; a suite testa a tabela pura nos
quatro casos e assere o DDI **por coerência** com o ambiente medido, imprimindo o aviso.

---

## 5. Testes e portões

- `driver\umd\build-umd.bat` → EXIT=0.
  Bloco novo: `C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos PASS`.
- `driver\wdk\build-contract.bat` → EXIT=0.
- `audit-bc250mcdm-safety.py`, `audit-mcdm-ddi-matrix.py`, `audit-ddi-noop-callbacks.py`,
  `audit-umd-node1-submission.py` → **PASS**.
- Portões novos em `tools/audit-bc250mcdm-safety.py`:
  - `audit_draw_has_no_cpu_fallback` — proíbe `memcpy`/`RtlCopyMemory`/`CopyMemory`/`memmove` e
    upload por CPU **dentro** do recorte exato da função de substituição; exige `Bc250UmdBltGpu`,
    a política pura, a contagem separada e o log honesto; e detecta se o **teste** voltar a exigir
    a mentira.
  - `audit_lost_log_tail_is_recovered` — exige o resgate, exige que o binder o chame **antes** do
    arquivamento e **depois** do baseline, e proíbe qualquer escrita no log cumulativo.

### 5.1 Mutações verificadas (6)

| # | Mutação | Detectada por |
|---|---|---|
| 1 | `Bc250UmdDrawBltSubstitutionIsLawful` retorna `TRUE` com tunable desarmado | teste C1, linha da tabela pura |
| 2 | `memcpy` reintroduzido no início da substituição | teste C1 (`RejectedDrawCount` não subiu) |
| 3 | `memcpy(` presente no corpo da substituição | `audit_draw_has_no_cpu_fallback` |
| 4 | teste renomeado de volta para `TestCompositeDrawAndUniqueTokens` | `audit_draw_has_no_cpu_fallback` |
| 5 | chamada do resgate desligada no binder | `audit_lost_log_tail_is_recovered` |
| 6 | resgate abrindo o log em `FileAccess::ReadWrite` | `audit_lost_log_tail_is_recovered` |

---

## 6. Ordem proposta para a próxima corrida

1. **Empacotar 3.127.0.0** com C1 e C2 (o empacotador **não** compila o SYS — build de laboratório
   primeiro).
2. Limpar o fusível e fazer boot frio:
   `tools\inspect-boot-fuse.ps1 -Reset` (o log recuperado termina em
   `[FUSE] STARTDEVICE RECUSADO: State=2, Boots=3`).
3. **Desarmar `D3D11LabPromotion` se não for objetivo da corrida** — deixá-lo armado altera o
   comportamento da UMD e contaminou a suite offline.
4. Critério escrito **antes** da corrida, para o flush de HDP:
   - **Confirmado** se as submissões passarem de `FenceId` 770 com voltas do anel, com zero
     `IB_TRIPOD TIMEOUT` e zero `0x119`, e alguma linha mostrar a fence resolvida logo após flush.
   - **Refutado** se reaparecer timeout com `depois_hdp` igual ao poison — nesse caso o caso é o da
     §2.1 (fault de tradução no IB de 11 MB), não visibilidade.
5. **Esta corrida não deve produzir desktop.** Com o `Draw` recusado honestamente e
   `D3D11DrawBltSubstitution` desarmado, tela preta é o resultado **esperado e correto**. Qualquer
   imagem que apareça deve ser atribuída ao rollback/BasicDisplay até prova em contrário.
6. Item novo, separado: fault de tradução no IB de fill de 11 MB (`GFXHUB STATUS=00841B40`,
   `ADDR=0x82C`).

---

## 7. Barreira que continua de pé

O desktop acelerado exige **rasterização real**: compilador de shader, estado de pipeline e pacotes
PM4 de draw no Node 1. `DrawIndexed: RECUSADO` é a resposta honesta hoje, e é o próximo item de
funcionalidade — não a substituição por blt, que é paliativo declarado e fechado por default.
