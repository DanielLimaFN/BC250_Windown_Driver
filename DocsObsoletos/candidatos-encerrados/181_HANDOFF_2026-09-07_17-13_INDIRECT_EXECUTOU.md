# 181 — Corrida 17:13: o IB WDDM real executou sob VMID8; e a faixa A14

**Data:** 2026-09-07 17:13–17:16 BRT
**Evidência do bind:** `docs/evidence/2026-09-07_17-13-54-DESKTOP-HQAG6T4-mcdm-display-binding/`
**Evidência do rollback:** `docs/evidence/2026-09-07_17-15-55-DESKTOP-HQAG6T4-mcdm-online-rollback/`
**Pacote:** `DriverVer 3.82.0.0`, SYS assinado `0CF9472F…`, `SdmaIndirectOnly=1` pelo INF.
**Resultado:** dez IBs reais do `dxgkrnl` buscados pela GPU sob VMID8 com duas pernas do
tripé; tela preta; `BC250 ONLINE ROLLBACK: PASS`.

Continuação de [`180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md`](180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md).

---

## 1. O que foi medido (`P0`)

```text
[StartDevice] SdmaIndirectOnly=1 (present=1)
[StartDevice] Global GFXHUB MMU initialized: Regs=26 TLB=0x00001D59 L2=0x00080E01 DummyPA=0x45FFFF000
14 CAMINHO_ESCOLHIDO  =  10 INDIRECT  +  4 RECUSADO
10 IB EXECUTADO (INDIRECT): DMA buffer de 64 bytes, VA=0x22F000 .. 0x22F1C0
10 IB_TRIPOD PASS #1..#10: WPTR==RPTR, Fence 0x1002..0x1014 sobre poison 0x43DAFFFE, Vmid=8, ~111 polls
 4 RECUSA: GPUVA do IB (0x20000 / 0x1560000) nao resolve sob VMID8; silicio nao tocado
 4 GFXHUB_FAULT_SNAPSHOT: STATUS=00000000 ADDR=0:0
 0 timeouts   0 BugCheck   Codigo 0 sustentado ate o rollback
```

**A dívida central do projeto está refutada como estava formulada.** O `INDIRECT` — o único
ramo em que a GPU busca o DMA buffer entregue pelo `dxgkrnl` — foi exercitado pela primeira
vez no caminho WDDM de produção e completou dez vezes: `RPTR` alcançou o `WPTR` e a fence
física substituiu o poison em cada uma delas.

**O risco declarado não se materializou.** As 4 recusas devolveram `STATUS_IO_DEVICE_ERROR`
e a máquina sobreviveu: nenhum `0x119`, nenhum bugcheck, dispositivo em Código 0.

### 1.1 O que essas dez linhas NÃO provam

`IB_TRIPOD PASS` é rótulo de **duas** pernas. O gate que o produzia
(`bc250_sdma_present.c`, antes da A14) exigia só `RPTR==WPTR` e fence; **nenhum byte do
destino era conferido**. Pela Lei 2 o B1 **não fecha** com isso. É o mesmo falso sucesso da
A13, num lugar que a A13 não tocou — e a A14 abaixo corrige.

### 1.2 A pergunta do tripé de boot foi respondida pela telemetria da A13

```text
TRIPE_VEREDITO: TRIPE_PROVADO (TargetWPTR=68 RPTR_Wb=52 RPTR_Reg=68 Casou=REGISTRADOR
                FenceExp=00001001 FenceObs=00001001 Poison=43DAFFFE Mismatches=0 Polls=1)
```

O `RPTR=52` que aparecia ao lado de `WPTR=68` em todas as corridas era o **writeback
atrasado**; o registrador estava em 68. O item 63 **sobrevive** ao gate estrito, e agora o
log nomeia a fonte do número. Hipótese da A13 confirmada por medição.

## 2. O display avançou e continuou preto

- 5 `CommitVidPn`, 5 `SetVidPnSourceAddress (Segment=1, Addr=0xAB0000)`, **20 `Present`**,
  10 deles com `SDMA=YES` — contra 1/1/12 na corrida 16:19.
- Frames vazios: 3 × `BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO)`, `NonZero=0/256`,
  `Hash=0x00000000`, `BlankStreak=3`. E `BLT_ENGINE_2LEGS #1` declarando
  `PERNA DE DADOS NAO CONFERIDA`.
- **7 falhas de `dwm.exe`** (`e0464645`), dois dumps preservados (244.533.098 e 203.502.034
  bytes). UMD: 384 × `OpenAdapter10`, 752 × `OpenAdapter10_2`, 2 × `OpenAdapter12`.

### 2.1 A imagem que apareceu no fim não é atribuível ao nosso driver

O relato visual foi tela preta por um longo período, imagem aparecendo em seguida, e o
watchdog reiniciando. A prova de bytes diz que nossos blits copiaram **zero** em 3/3
amostragens, e o rollback rodou às 17:15:55 removendo `oem34.inf` e restaurando o
BasicDisplay. A hipótese mais econômica é que a imagem voltou nesse momento.
**Atribuição: NÃO VALIDADA EM HARDWARE**, nas duas direções.

### 2.2 A confirmação foi recusada por defeito de ferramenta

`DISPLAY_OK` foi digitado e recusado: o binder comparava a string crua com `-ceq`, sem
`Trim()`. Espaço na borda derruba a confirmação e o watchdog dispara. Corrigido na A14.2.

## 3. Diagnóstico do `REJECT source contract` — e uma correção de enquadramento minha

Eu havia dito que isso era "a próxima parede do vídeo depois do D3D11". **Está errado, e a
evidência é minha própria mineração:** é consequência do ciclo de morte do DWM, não parede
independente. A cadeia medida:

```text
Presents #1..#15   -> processo A (hProc=0xFFFFB70D28D89340), UpdatePT com SegId=1, resolve OK
                      dwm.exe morre no fail-fast de D3D11
Presents #16..#20  -> processo B (hProc=0xFFFFB70D28D8BE90), UpdatePT com SegId=0,
                      faixas comecando em 0xC00000 -- nunca cobrindo 0xAB0000
[ResolveGpuVa] SpaceResolveAddress failed: status=9  (VA=0xAC0000)  = NOT_FOUND
[ResolveGpuVa] SpaceResolveAddress failed: status=10 (VA=0x30000)   = OWNER_MISMATCH
[Present #16..#20] REJECT source contract: SlotSeg=0, SlotWord=0xAC0000
```

Duas leituras distintas, e só uma é defeito nosso:

1. **`SlotSeg=0` e `NOT_FOUND`** — o `SegmentId` da origem é zero, isto é, a alocação do
   novo DWM **não estava residente** em segmento nenhum, e o `dxgkrnl` nunca pediu
   `UpdatePageTable` cobrindo `0xAB0000` para ele. Nosso `Present` recusou **corretamente**:
   não havia origem para ler. Downstream do DWM morto.
2. **`OWNER_MISMATCH` (status 10)** — **este é defeito nosso.** A faixa `0x30000` continuava
   `MAPPED` sob o processo A, **que já não existia**. `Bc250ActiveDestroyContextObject` já
   invalidava as faixas do contexto; `Bc250ActiveDestroyProcessObject` **não** invalidava as
   do processo. Vazamento de ciclo de vida: a resposta ao processo novo virava "ocupado por
   terceiro" em vez de "não mapeado". Corrigido na A14.3.

**A parede do vídeo continua sendo o backend D3D10/11.** Enquanto o DWM fail-fastar, ele
morre antes de tornar a primária residente, e nenhuma correção de GPUVA muda isso.

---

## 4. Faixa A14 — correções desta sessão (offline, zero corridas)

### A14.1 — a perna 3 do tripé passou a existir no caminho `INDIRECT`

O problema não é comparar bytes: é que o padrão dos fills de paginação é `0x00000000`, e
zero contra zero é inconclusivo — foi o que reprovou o blt em 63/63 amostras. A saída é
**canário**: plantar valores não-nulos no destino **antes** do disparo e conferir que a GPU
os substituiu pelo padrão declarado. **Zero sobre poison é conclusivo.**

- `driver/core/bc250_blt_proof_contract.{h,c}`: `Bc250PlanDataProofCanaries()` e
  `Bc250CountDataProofMismatches()` — fonte única do layout dos 5 canários, com duas recusas
  que faltavam (destino menor que um dword; canário igual ao padrão esperado, que anularia a
  prova). Ausência de leitura conta como **divergência total**, nunca como sucesso.
- `Bc250SdmaPresentFill` passou a consumir esse plano em vez da cópia inline. Fórmula dos
  cinco pontos e dos valores **preservada**.
- `Bc250SdmaPresentExecuteIndirect` recebeu `ExpectedEffectDestMc/Bytes/Pattern`, planta os
  canários antes do kick e classifica por `Bc250ClassifySdmaTripod`. O rótulo agora é:
  - `IB_TRIPE_VEREDITO #N: <veredito> (... Canaries=5 Divergentes=N PostGPU=[...] ...)`
  - `IB_TRIPE PROVADO NO SILICIO #N` — somente com as três pernas;
  - `IB SEM TRIPE #N` — anel e fence avançaram e os canários ficaram intactos;
  - `IB_ENGINE_2LEGS #N (… PERNA DE DADOS NAO CONFERIDA)` — quando o destino não é
    conferível pela CPU. **A string `IB_TRIPOD PASS` deixou de existir no binário.**
- O miniport declara o efeito esperado (`IB_EFEITO_DECLARADO`) a partir do **mesmo** encoder
  que montou o DMA buffer, e passou a **consumir** os parâmetros de fill no ramo `INDIRECT`
  — deixá-los pendentes reatribuía o mesmo efeito às submissões seguintes, que é o que
  imprimiu `Fill=1/11075584` idêntico nas 14 linhas da corrida 17:13.

> Decisão declarada: perna 3 reprovada **não** devolve falha ao `dxgkrnl`. A fence retirou
> fisicamente; recusar completion depois disso trocaria uma mentira por um `0x119`. O
> veredito vai para o log, não para o scheduler.

### A14.2 — `Trim()` na confirmação visual

`tools/bind-bc250mcdm-failclosed.ps1` apara espaços antes do `-ceq`. Continua
**case-sensitive** de propósito: aparar espaço não afrouxa a intenção; aceitar outra palavra
afrouxaria.

### A14.3 — `DestroyProcess` libera as faixas de GPUVA do processo

`Bc250ActiveDestroyProcessObject` chama
`Bc250GpuvaSpaceInvalidateForObject(space, Handle, NULL, NULL)` — mesma semântica que o
caminho de contexto já usava — e registra `[DestroyProcess] GPUVA: faixas do processo %p
invalidadas (Epoch=… LiveCount=…)`. Sem isso, `OWNER_MISMATCH` sobrevive ao dono morto.

---

## 5. Testes que fecham a A14

| Correção | Teste nomeado | Resultado |
|---|---|---|
| A14.1 | `blt_proof_contract_test`: 5 blocos novos — geometria medida (`0x450AB0000`, 11.075.584 B), recusa de destino não mapeado, colisão padrão/canário, contagem de divergências, e o elo que derruba um IB "perfeito" com canários intactos | PASS, `EXIT=0` |
| A14.1/3 | `tools/audit-bc250mcdm-safety.py`: `audit_ib_data_leg` (proíbe a string `IB_TRIPOD PASS`), `audit_process_gpuva_teardown` | PASS |
| todas | `driver/wdk/build-contract.bat` (suíte integral + SYS) | `EXIT=0` |

**Mutação verificada nos dois portões novos:** zerar `dataMismatches` sem chamar o contrato →
`FAILED: missing perna de dados conferida no caminho INDIRECT`; remover a invalidação do
`DestroyProcess` → `FAILED: DestroyProcess sem invalidacao das faixas de GPUVA do processo`.

O gate de staleness do empacotador também mordeu no caminho (o restore da mutação deixou o
`.c` mais novo que o `.sys` e o pacote foi recusado) — funcionando como projetado.

---

## 6. Pacote do ensaio da perna 3 — `3.83.0.0`, assinado, auditado, NÃO INSTALADO

| Item | Valor |
|---|---|
| `DriverVer` | `09/07/2026,3.83.0.0` |
| SYS fonte | `DCA7D06D349055C02E308865A45FEACC769F0BE7259E711CDF19263487C437B1` |
| SYS assinado | `FDCC5AB29F1498EF0140A87741DC8FF1BCB64F8DD2FB73BBD4FAF3BCED5D01EF` |
| Catálogo | `F7A6DD4F8D56289A3497EACAD7B0AD4BB0A68DF162EE9B7A8858A58F96E6B9B4` |
| `SdmaIndirectOnly` | `1` (INF linha 86; manifesto deriva do INF) |

Prova de frescor no SYS assinado: `IB_ENGINE_2LEGS`, `IB_TRIPE_VEREDITO`,
`IB_EFEITO_DECLARADO`, `DestroyProcess] GPUVA` presentes; **`IB_TRIPOD PASS` ausente (0
ocorrências)**. Auditorias: safety PASS, DDI matrix PASS, CP manifest OK, package audit PASS.

### 6.1 A corrida que fecha o B1

Mesma receita do doc 180 §6.2, com este pacote. O que decide, em ordem:

| Linha | O que decide |
|---|---|
| `IB_EFEITO_DECLARADO: … MC=… Size=… Pattern=…` | o efeito foi atribuído; sem ela a perna 3 não existe |
| `IB_TRIPE_VEREDITO #N: TRIPE_PROVADO … Divergentes=0` | **B1 FECHA** — as três pernas do IB real |
| `IB_TRIPE_VEREDITO #N: DADOS_DIVERGEM … Divergentes=5` | a engine consome o pacote e **não** produz o efeito: é a resposta que faltava desde 05/09, e aponta tradução/semântica do IB |
| `IB_ENGINE_2LEGS #N` | destino não conferível: nada fechou, e o log diz por quê |
| `[DestroyProcess] GPUVA: faixas … invalidadas` | o vazamento de dono morto acabou |
| `SpaceResolveAddress failed: status=10` | se **reaparecer**, a A14.3 não cobre o caminho que produz o vazamento |

**Teto: uma corrida. Boot frio.** Continua valendo: a tela deve ficar preta — a UMD não foi
recompilada e `BC250_D3D10_11_NATIVE_READY` segue `0`. Esta corrida mede a perna 3 do IB,
não o desktop.

Até ela: fechamento do B1, perna 3 do IB, submissão Node 1, desktop acelerado e jogos —
**NÃO VALIDADOS EM HARDWARE**.
