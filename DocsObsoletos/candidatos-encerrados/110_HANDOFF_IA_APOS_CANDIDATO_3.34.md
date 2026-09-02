# Handoff para a próxima IA — estado após o candidato 3.34

Data local: 30/08/2026
Último pacote ensaiado: **3.34.0.0 / SchemaVersion 105**
Veredito: **NÃO VALIDADO EM HARDWARE**

Este documento é autocontido. Leia-o antes de `docs/104` a `docs/109`, que
contêm o detalhe ciclo a ciclo.

---

## 1. Objetivo inegociável

Um shader realmente executado nos CUs com, no mesmo ciclo físico:

1. resultado saindo do buffer envenenado e conferido byte a byte;
2. `RPTR == WPTR == ShaderDispatchDwords`;
3. fence física posterior ao `DISPATCH_DIRECT` saindo do poison;
4. rollback de memória e registradores comprovado;
5. nenhum avanço de ponteiro, fence ou resultado simulado pela CPU.

Faltando qualquer item: **NÃO VALIDADO EM HARDWARE**.

## 2. Onde o bloqueio está AGORA

Não é mais publicação de cursor, nem fila vazia, nem tradução de memória.

**O MEC2 busca o primeiro pacote PM4, avança o RPTR em 1 dword, e trava
decodificando.**

Evidência do 3.34, decodificada de `gc_10_1_0_sh_mask.h`:

```text
CP_CPC_STATUS        0xA0000082
  bit31 CPC_BUSY=1  bit29 CPG_CPC_BUSY=1  bit7 ROQ2_BUSY=1  bit1 MEC2_BUSY=1
CP_CPC_BUSY_STAT     0x04000000
  bit26 MEC2_PIPE0_BUSY=1
CP_CPC_STALLED_STAT1 0x00010000
  bit16 MEC2_DECODING_PACKET=1     <-- travado decodificando
CP_STAT              0x00000000    (CP grafico ocioso, esperado)
```

| Campo | Valor |
|---|---:|
| `RptrAfterKick` / `RptrMmioLast` / `RptrAfter` | `1` / `1` / `1` |
| `RptrNonZeroSamples` | `196` de 196 |
| `RptrReportMemLast` | `0` |
| `WptrMmioLast` | `40` |
| `PQ_CONTROL` | `0xD030090D` (`PQ_EMPTY=0`) |
| `HQ_STATUS0` | `0x00008000` (`QUEUE_IDLE=0`) |
| `MecPcFirst` / `MecPcLast` / transições | `0x35CB` / `0x35CB` / `134` |
| `HqdErrorPollLast`, UTCL1 CPC/CPF | `0` |
| `ShaderResultAfter` / `ShaderFenceAfter` | `0x43DAFFFE` / `0xA55A0FF0` (veneno) |

RPTR = 1 significa que o CP leu exatamente um dword — o cabeçalho do primeiro
pacote — e parou ali. Coerente com `MEC2_DECODING_PACKET` preso em 1.

**Diferença entre ciclos a investigar:** no 3.33 (mesmo código exceto pelas
leituras novas) `RptrNonZeroSamples` foi `0` e `RptrAfter` foi `0`; no 3.34 foi
`196` e `1`. Os dois ciclos foram boot frio. Pode ser variabilidade real ou
efeito de temporização das leituras adicionais. **Não trate RPTR=1 como
determinístico sem repetir o ensaio.**

## 3. DEFEITO CONHECIDO — corrija isto primeiro

O 3.34 lê `CP_MEC_ME1_HEADER_DUMP` (`0x08238`). **Está errado.** A topologia
real lida em `RLC_CP_SCHEDULERS = 0x58504840` dá `me = 2`, e o `CP_CPC_STATUS`
confirma `MEC2_BUSY` + `MEC2_PIPE0_BUSY`. Estamos usando o **ME2**.

Por isso o dump veio `0xDEF0DEF0 / 0xDEF1DEF1 / 0xDEF2DEF2 / 0xDEF3DEF3` —
padrão de preenchimento de FIFO vazia, do motor errado. O ponteiro da FIFO
avança (os valores incrementam), então o mecanismo funciona; só o alvo está
trocado.

Registrador correto, derivado da mesma base já validada:

```text
mmCP_MEC_ME2_HEADER_DUMP = dw 0x0E2F  ->  (0x0E2F + 0x1260) * 4 = BAR5 0x0823C
```

`CP_MEC_ME1_HEADER_DUMP` (`0x0E2E` → `0x08238`) já está catalogado e
allowlistado só para leitura; acrescente o `0x0823C` do mesmo jeito.

## 4. Próximo candidato sugerido (3.35) — ainda só observação

Não introduza escrita nova. O objetivo é ver **qual** pacote trava o decoder.

1. Ler `CP_MEC_ME2_HEADER_DUMP` (`0x0823C`) na mesma rajada única pós-polling
   que hoje drena o ME1. Mantenha o ME1 também, como controle.
2. Publicar os primeiros dwords do ring na telemetria (ex.: `RingDword0..7`),
   lidos da memória, para cruzar offline com o header dump.
3. Revisar o construtor PM4 do primeiro pacote em
   `driver/core/` — um opcode inválido ou não suportado pelo MEC trava o
   decodificador exatamente assim. O `ShaderDispatchPacketIndex = 25` e o
   `ShaderFencePacketIndex = 32` num plano de 40 dwords indicam que há pacotes
   antes do `DISPATCH_DIRECT`; é um deles que está sendo decodificado.

## 5. Regra que custou três ensaios — não repita

**Toda etapa nova entra como observação registrada, nunca como `abort`, até o
silício provar que ela é obrigatória.**

- 3.30 tratou `CP_PQ_WPTR_POLL_CNTL.EN` como pré-requisito → abortou em
  `WRITE_FAILED` e queimou um boot sem medir o `CNTL1`.
- 3.31 tratou a publicação pré-`ACTIVE` do WPTR como obrigatória → abortou em
  `READBACK_FAILED` e queimou outro boot.
- Só o 3.32, com as duas rebaixadas a observação, percorreu a sequência inteira
  e revelou que a fila deixou de estar vazia e ociosa.

Isto **não** afrouxa o gate: o tripé continua exigindo `RPTR==WPTR`, fence
física com veneno verificado e dado byte a byte. O que muda é não abortar por
hipótese não demonstrada.

## 6. O que já está PROVADO no silício

| Fato | Ciclo |
|---|---|
| `CP_PQ_WPTR_POLL_CNTL` (`0xC20C`) é **read-only** neste contexto — nem `PERIOD` nem `EN` gravam | 3.31 |
| `CP_PQ_WPTR_POLL_CNTL1` (`0xC210`) **é gravável**; `QUEUE_MASK` latcheia | 3.31 |
| `CP_HQD_PQ_WPTR_LO` **não aceita** cursor com a fila inativa | 3.31 |
| Com a `QUEUE_MASK` ligada, o **hardware publica o cursor sozinho**: `PostActiveWptrObserved=40`, `DirectWptrKickApplied=0`, e o WPTR **se mantém** (no 3.29 decaía a 0) | 3.32, 3.33, 3.34 |
| `PQ_EMPTY` 1→0 e `QUEUE_IDLE` 1→0 — o silício reconhece trabalho pendente | 3.32+ |
| O próprio CP altera `CP_HQD_PERSISTENT_STATE` (`PRELOAD_SIZE` `0x053`→`0xC53`) | 3.33+ |
| MEC2/pipe0 ocupado e travado em `DECODING_PACKET` | 3.34 |
| Ring em GPUVA `0x10000`, `PTE=0xF400AA00E1`, tradução e readback com **zero** mismatches | 3.28+ |
| SDMA0, CP/PM4 Ring 0 e o gate estrito do MEC/HQD fecham o tripé com display vivo (59/59) | anterior |

## 7. O que foi ELIMINADO como causa

| Hipótese | Como caiu |
|---|---|
| Writeback de RPTR (`RPTR_REPORT_ADDR`) | 3.33: registrador **e** memória em zero; não era problema de reporte |
| `CP_HQD_VMID` divergente | 3.33: `VmidPollLast=0`, igual ao Contexto 0 |
| Dequeue espontâneo | 3.33: `DequeueRequestPollLast=0` |
| Offset do `CNTL1` errado | 3.31: `0xC210` grava e relê enquanto `0xC20C` recusa — endereço não mapeado não latcheia |
| `QUEUE_MASK` "sticky" | 3.32: limpa na primeira tentativa quando a fila completa o ciclo de vida; no 3.31 parecia sticky só porque a transação abortou antes de `ACTIVE` |
| Literal `0x00403004` no CP | É do SDMA (`SdmaRingPollCntl`); marcado como **proibido** no catálogo |

## 8. Estado da máquina AGORA

```text
device      bc250mcdm / Codigo 0   (3.34 ainda VINCULADO)
watchdog    DISARMED (confirm executado)
display     vivo
```

**Antes de qualquer novo ensaio:**

1. `powershell -ExecutionPolicy Bypass -File tools\rollback-bc250mcdm-lab.ps1`
2. **Reboot manual** — obrigatório: sem o ciclo de vida completo da fila, a
   `QUEUE_MASK` do `CNTL1` só cai com reset, e um boot morno faz o rollback
   comparar `1` com `1` e dar falso verde. O campo
   `WptrPollCntl1DirtyEntry` existe para denunciar isso e **reprova o ensaio**.
3. `quarantine-bc250mcdm-lab-residue.ps1 -Execute -IAcceptExactResidueQuarantine`
4. `audit-bc250mcdm-prebinding.ps1 -LabPackage` → tem de dar **PASS**
5. `bind-bc250mcdm-lab.ps1 -EnableShaderDispatch -Execute -IAcceptBlackScreenRisk`
6. `confirm-bc250mcdm-lab.ps1` com o token impresso
7. `test-shader-dispatch-acceptance.ps1`

Uma só submissão física por boot.

## 9. Armadilhas de ferramenta (todas já corrigidas — não reintroduza)

**O empacotador não compila o SYS.** `tools\package-bc250mcdm-lab.ps1` só
copia `driver\wdk\output_lab\bc250mcdm.sys`. Quem compila é
`tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`.
Pular esse passo assinou uma vez o binário do candidato **anterior** com o
`DriverVer` novo. Hoje o empacotador falha fechado se qualquer `*.c`/`*.h` de
`driver\core` ou `driver\wdk` for mais novo que o SYS.

**Sempre confira o `PackageSysSha256`** do manifesto contra o hash do candidato
anterior. Prova adicional barata: procurar os nomes de telemetria novos como
strings UTF-16 dentro do `.sys` assinado.

**Ao subir o `SchemaVersion`**, quatro scripts têm o número em literal e
bloqueiam o ensaio se ficarem para trás: `package-bc250mcdm-lab.ps1`,
`audit-bc250mcdm-lab-package.ps1`, `bind-bc250mcdm-lab.ps1` e
`confirm-bc250mcdm-lab.ps1` (mais o `test-shader-dispatch-acceptance.ps1`).

**`audit-bc250mcdm-prebinding.ps1` exige `-LabPackage`.** Sem a flag ele audita
o pacote de **produção** (3.24) e bloqueia sempre.

**`bind-bc250mcdm-lab.ps1` exige `-EnableShaderDispatch`** para casar com o INF
empacotado; sem ela o contrato espera `ShaderDispatch=0` e o binding falha
antes do dry-run.

**A telemetria é capturada automaticamente no bind** (`probe-registry-at-bind.json`
e `.txt`), antes de o watchdog poder disparar. Antes disso dependia do
`confirm` dentro da janela de 180 s, e um ciclo inteiro de evidência se perdeu.

**A quarentena autoriza o resíduo pelo `package_sys_sha256`** gravado em
`tools\_last_lab_binding.json` pelo bind. Antes ela dependia do manifesto
atual, e reempacotar antes de limpar o resíduo destruía a referência.

**Em PowerShell 5.1, `0x80000000` sem sufixo é `Int32` negativo** e sofre
extensão de sinal em `[int64]`. Use `0x80000000L` e normalize com `To-U32`.

## 10. Conformidade

- Build: `driver\wdk\build-contract.bat` → `EXIT=0`, zero warnings em
  `driver/core`, `driver/wdk` e `driver/tests` sob `/W4 /WX`. Os warnings que
  aparecem são pré-existentes em `tools/` e `driver/umd/` (user-mode).
- Registradores de diagnóstico do CP/CPC entram **somente na allowlist de
  leitura**; há teste dedicado exigindo que toda escrita seja recusada.
- O worktree está sujo e contém todo o desenvolvimento, documentos e
  evidências. **Não apagar, resetar ou sobrescrever.**

## 11. Onde está a evidência

```text
docs/evidence/2026-08-30_11-26-55-DESKTOP-HQAG6T4-lab-binding/   3.34 (Schema 105)
docs/evidence/2026-08-30_11-05-15-DESKTOP-HQAG6T4-lab-binding/   3.33 (Schema 104)
docs/evidence/2026-08-30_10-52-23-DESKTOP-HQAG6T4-lab-binding/   3.32 (Schema 103)
docs/evidence/2026-08-30_10-41-28-DESKTOP-HQAG6T4-lab-binding/   3.31 (Schema 102)
docs/evidence/2026-08-30_10-20-40-DESKTOP-HQAG6T4-lab-binding/   3.30 — SEM dump canonico
```

O diretório do 3.30 contém `probe-registry-manual-capture.md`, que **não** é
saída do coletor e diz isso na primeira linha: o watchdog reverteu antes do
`confirm` e os valores foram lidos ao vivo do registro. Trate-o como
transcrição, não como evidência canônica.

## 12. O que este documento NÃO afirma

Nenhuma wave executou em nenhum ciclo. `TriadPassed = 0`, `PhysicalReady = 0`,
resultado e fence permanecem no veneno em todos os ensaios. O tripé nunca
fechou para dispatch de shader.

**NÃO VALIDADO EM HARDWARE.**
