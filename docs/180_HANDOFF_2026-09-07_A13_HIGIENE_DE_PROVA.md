# 180 — Faixa A13 (07/09, OFFLINE): higiene de prova antes de gastar a próxima corrida

**Data:** 2026-09-07, após a corrida 16:19
**Corridas gastas nesta sessão:** **zero**
**Estado desta faixa:** mudança de contrato, telemetria e gate. **NÃO VALIDADO EM HARDWARE.**

Continuação de [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md).

---

## 1. O que a mineração da corrida 16:19 revelou, além do que o doc 179 já dizia

### 1.1 A corrida recomendada da `BASE_DE_AVANCO.md` já foi gasta — e não produziu informação

Ela existia para responder a **pergunta 2** do doc 167 (*que estado faz o `RPTR` alcançar
o `WPTR` sem executar*) pelas linhas `IB_QUEUE_*` da faixa A12. Contagem exata no
`bc250_display_log.txt` de
`docs/evidence/2026-09-07_16-19-06-DESKTOP-HQAG6T4-mcdm-display-binding/`:

```text
14  [SubmitVirtual Node0 SDMA] IB EXECUTADO
14  [SubmitVirtual Node0 SDMA] IB_GPUVA_SNAPSHOT
12  [SubmitVirtual Node0 SDMA] PLANO A SYNC NOP
 2  [SubmitVirtual Node0 SDMA] PLANO A DIRECT FILL
 0  linhas IB_QUEUE_*  ·  0 INDIRECT  ·  0 RECUSA
```

`14 = 2 + 12`. O desfecho fraco que o próprio documento antecipou é o que aconteceu: os
dois `PLANO A` absorveram **100 %** das submissões, o ramo `INDIRECT` não foi alcançado e
a telemetria da A12 **não escreveu uma linha**. Classificação: `P0` para a contagem;
a pergunta 2 continua **sem resposta**.

Consequência normativa: **reagendar aquela corrida sem mudança material é gastar boot frio
para reler o mesmo nada.**

### 1.2 A linha de sucesso do submit era comum aos três ramos (defeito nº 1 do doc 179)

`bc250_active_miniport.c` imprimia `IB EXECUTADO: DMA buffer de %u bytes em VA=...` no
caminho de sucesso compartilhado. Como as 14 submissões foram `PLANO A`, o log afirmou
execução de IB **14 vezes sem que a GPU tenha lido um único DMA buffer** — os `PLANO A`
reimplementam a operação em pacotes do **anel**. Falso sucesso, Lei 4.

### 1.3 Achado novo, não registrado em nenhum handoff: o tripé de boot não avaliava a perna 1

A linha `TRIPÉ NO SILÍCIO COMPROVADO (Item 63)! WPTR=68 RPTR=52` **não era telemetria
errada — era gate errado.** O gate de `Bc250SdmaPresentBringup` era:

```c
if (TripodFenceObserved == (FENCE_INIT + 1u) && TripodMismatches == 0u)
```

A igualdade `RPTR == WPTR` **nunca entrava na decisão**. Dois efeitos:

1. o poll aceitava writeback **ou** registrador sem registrar qual casou, e o log publicava
   o writeback atrasado ao lado da palavra `COMPROVADO`;
2. um timeout de 2000 polls caía no mesmo `COMPROVADO` desde que fence e bytes estivessem
   certos.

Isto é violação da Lei 2 **no código**, e é o carimbo que promoveu o item 63.

---

## 2. Correções desta faixa (todas offline)

### A13.1 — `IB EXECUTADO` restrito ao ramo `INDIRECT`

Fonte única nova em `driver/core/bc250_submit_command_contract.{h,c}`:

- `BC250_SUBMIT_EXEC_PATH` — `REFUSED` / `INDIRECT` / `RING_DIRECT_FILL` / `RING_SYNC_NOP`;
- `Bc250SelectSubmitExecPath()` — reproduz **exatamente** a precedência que rodou em
  produção quando `IndirectOnly == 0`;
- `Bc250SubmitPathExecutesDmaBuffer()` — `TRUE` só para `INDIRECT`;
- `Bc250SubmitPathName()` — nome estável para log.

No `SubmitCommandVirtual` do Node 0 os três `if` passaram a decidir por `execPath`, e o log
ganhou duas linhas honestas:

```text
[SubmitVirtual Node0 SDMA] CAMINHO_ESCOLHIDO: <nome> (Fill=… DestPA=… DmaBytes=… IbResolve=… IndirectOnly=…)
[SubmitVirtual Node0 SDMA] IB EXECUTADO (INDIRECT): a GPU leu o DMA buffer de … bytes em VA=…
[SubmitVirtual Node0 SDMA] PLANO A CONCLUIDO (<nome>): operacao REIMPLEMENTADA no anel; o DMA
                           buffer … NAO foi lido pela GPU. Isto NAO e execucao de IB.
```

### A13.2 — a perna 1 do tripé entrou na decisão

Contrato novo em `driver/core/bc250_blt_proof_contract.{h,c}`:

- `BC250_TRIPOD_VERDICT` — `INDETERMINATE` / `PROVEN` / `RPTR_NOT_RETIRED` /
  `FENCE_NOT_WRITTEN` / `DATA_MISMATCH`;
- `Bc250ClassifySdmaTripod()` — ordem da Lei 2: `RPTR==WPTR`, depois fence, depois bytes;
- `BC250_TRIPOD_RPTR_SOURCE` — o log passa a dizer **qual** leitura casou
  (`WRITEBACK`, `REGISTRADOR`, `WRITEBACK+REGISTRADOR`, `NENHUMA`);
- `TargetWptrBytes == 0` sai **`INDETERMINADO`**, para que um anel que nunca armou não
  passe por provado por vacuidade.

`Bc250SdmaPresentBringup` agora zera `TripodValidated`, classifica pelo contrato, emite
`TRIPE_VEREDITO: …` com as duas leituras de RPTR e só usa a frase `TRIPÉ NO SILÍCIO
COMPROVADO` quando `Bc250TripodAllowsClaim()` autoriza. Dois campos novos guardam o
veredito e a fonte do RPTR para auditoria posterior.

> ⚠️ Efeito possível na próxima corrida, declarado por antecipação: se o boot do SDMA
> passava só por fence + bytes, o gate mais estrito pode **reprovar** o bringup e deixar
> `HardwareEnabled = FALSE`. Isso não é regressão de silício — é a medição que sempre
> deveria ter sido feita. Se acontecer, o `TRIPE_VEREDITO` diz qual perna faltou.

### A13.3 — `SdmaIndirectOnly`: desligar a precedência dos `PLANO A`

Tunable novo, lido **no caminho compilado no pacote de produção** (o bloco de tunables de
laboratório vive no `#else` de `BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP` — foi por isso
que `GpuvaLabPromotion` saiu `present=0` em todas as corridas):

```text
HKLM\SYSTEM\CurrentControlSet\Services\bc250mcdm\…  SdmaIndirectOnly  (REG_DWORD)
0 ou ausente  -> comportamento medido, preservado
1             -> os dois PLANO A são suprimidos; o INDIRECT é exercitado
```

Fail-closed: só liga com `Present == 1` **e** valor `1`. Com o tunable ligado e a GPUVA do
IB sem resolver, o resultado é **RECUSA** — nunca queda de volta no `PLANO A`.

**Armar é decisão do dono do projeto.** Compilar isto não liga nada.

---

## 3. Testes que fecham esta faixa

| Correção | Teste nomeado | Resultado |
|---|---|---|
| A13.1 | `submit_command_contract_test` (3 blocos novos: precedência de produção, `IndirectOnly`, rótulo) | PASS, `EXIT=0` |
| A13.2 | `blt_proof_contract_test` (6 casos novos, incluindo o gabarito `WPTR=68/RPTR=52`) | PASS, `EXIT=0` |
| A13.1–3 | `tools/audit-bc250mcdm-safety.py`: `audit_submit_path_labels`, `audit_tripod_leg_one`, `audit_indirect_only_tunable` | PASS |
| todas | `driver/wdk/build-contract.bat` (suíte integral + SYS) | `EXIT=0` |

**Mutação verificada nos dois portões novos** (não é presença decorativa):

- trocar `Bc250TripodAllowsClaim(tripodVerdict)` por um teste frouxo →
  `FAILED: missing guarda da frase TRIPE no bringup de SDMA`;
- restaurar a frase antiga `IB EXECUTADO: DMA buffer …` →
  `FAILED: 'IB EXECUTADO' sem qualificar INDIRECT`.

Auditorias `audit-mcdm-ddi-matrix.py` e `audit-ddi-noop-callbacks.py`: `EXIT=0`.

Prova de frescor do binário (a norma manda procurar string ASCII do candidato dentro do
`.sys`, porque o empacotador assina e o hash do pacote ≠ hash do build) — as cinco presentes
em `driver/wdk/output/bc250mcdm.sys`: `CAMINHO_ESCOLHIDO`, `TRIPE_VEREDITO`,
`PLANO A CONCLUIDO`, `SdmaIndirectOnly`, `IB EXECUTADO (INDIRECT)`.

Correção de higiene achada no caminho: em `submit_command_contract_test.c` o `CHECK` do
ramo **modo usuário** fazia `return 1`, e neste arquivo `1` significa **sucesso**
(`main` devolve `run() ? 0 : 1`) — um `CHECK` falho seria lido como PASS. A suíte compila
com `/DBC250_KERNEL_MODE`, então isso **nunca mascarou corrida**; foi corrigido para não
ficar de pé.

---

## 4. O que esta faixa NÃO fez

- **Nenhuma corrida física.** Nada aqui é `P0`.
- Não implementou backend D3D10/11 — `BC250_D3D10_11_NATIVE_READY` continua `0`, e as
  recusas `OpenAdapter10/10_2` continuam corretas (na 16:19: 512 + 1008, mais 2 de
  `OpenAdapter12`).
- Não aposentou os `PLANO A` (isso é o B3, e só depois de o B1 fechar).
- Não reconstruiu `output_lab` (estagnado; exige o flag de bring-up destrutivo, decisão do
  dono).
- Não instalou nada. (O candidato **foi** empacotado e assinado depois — ver §6 —, mas
  permanece fora da máquina.)

---

## 5. Próxima corrida — agora ela produz informação nos dois desfechos

**Uma** corrida, boot frio, pacote de produção reconstruído a partir deste código,
watchdog armado, probe `D3D11CapsProbe` desarmado, **com `SdmaIndirectOnly=1`**.

Sem o tunable, a corrida repete a 16:19 e não mede o `INDIRECT`. Com ele:

| O que sair no log | O que decide |
|---|---|
| `CAMINHO_ESCOLHIDO: INDIRECT` | o ramo finalmente é exercitado — pré-requisito de tudo |
| `IB_QUEUE_STATUS: … IDLE=?` | `IDLE=0` confirma "parou dentro do IB"; `IDLE=1` refuta |
| `IB_QUEUE_DECODED: IB_RPTR/IB_OFFSET` | se diferirem de 8/0, o `0x20` de 05/09 não era constante |
| `IB_QUEUE_STATUS: EXCEPTION=?` | não-zero é resposta direta, e ninguém tinha olhado |
| `TRIPE_VEREDITO: …` no boot | diz se o item 63 sobrevive ao gate honesto, e qual perna falta |
| `PLANO A CONCLUIDO (…)` | se ainda aparecer, o tunable não pegou — ler `SdmaIndirectOnly=… (present=…)` |

**Risco declarado:** com os `PLANO A` desligados, submissões que hoje "concluem" passam a
**RECUSA** (`STATUS_IO_DEVICE_ERROR`), que é o caminho conhecido de `0x119`. Ou seja: esta
corrida pode terminar em bugcheck ou tela preta com recovery. É o preço de medir o
`INDIRECT`, e a decisão de pagá-lo é do dono do projeto — não um efeito de compilar.

**Critério de abandono:** primeiro timeout, fault, Código 43 ou ausência de confirmação
visual deixa o watchdog restaurar BasicDisplay. Evidência em
`docs/evidence/<timestamp>-mcdm-display-binding/`.

Até essa corrida: execução do IB WDDM real, veredito do tripé sob o gate novo, submissão
Node 1, desktop acelerado e jogos — **NÃO VALIDADOS EM HARDWARE**.

---

## 6. Pacote assinado do ensaio do INDIRECT — `3.82.0.0`

Construído e assinado em 07/09 20:03 UTC, **não instalado**.

| Item | Valor |
|---|---|
| `DriverVer` | `09/07/2026,3.82.0.0` |
| SYS fonte | `1C30E3B1DF9B6AB29FB53B49A761B784D5E0C36AB617EE2CF570B3731CC6E0CF` |
| SYS assinado | `0CF9472F72789ACF9603248C5FF113EB3F0FF73C0DA669935CDB6237B9C0504D` |
| INF | `D0F73BE58EEC4DEC330A9F1D37510B71EEE0EC9BCB4E088F048912E9747E4533` |
| Catálogo | `7798EC55E7D10FE866E0DE4B762BE80BA5F9FD02B2E3A823B328739D5EC055F0` |
| Assinante | `8D14413F3B97AEF9D7F78A1CA8D9EA99A3FBAD3B` |
| Diretório | `driver/wdk/package/output_active/` |
| Manifesto | `driver/wdk/package/output_active.manifest.json` |

O INF declara **`HKR,Parameters,SdmaIndirectOnly,0x00010001,1`** (linha 86), e o manifesto
registra `"SdmaIndirectOnly": 1` / `"SdmaIndirectOnlyEnabled": true`. Esse campo é
**derivado do INF empacotado**, não escrito por literal: o empacotador agora **recusa o
pacote** se a linha não existir, para não repetir a segunda fonte de verdade que já mordeu
a geometria de GPUVA na faixa A11.

Prova de frescor do binário — as cinco strings do candidato presentes no **SYS assinado**:
`CAMINHO_ESCOLHIDO`, `TRIPE_VEREDITO`, `PLANO A CONCLUIDO`, `SdmaIndirectOnly`,
`IB EXECUTADO (INDIRECT)`.

Auditorias no build: `BC250 DDI MATRIX: PASS`, `BC250 MCDM SAFETY AUDIT: PASS`,
`CP manifest: OK`, `BC250 MCDM PACKAGE AUDIT: PASS (INTEGRITY ONLY)`.

### 6.1 Novo no ferramental

- **`tools/arm-sdma-indirect-only.ps1`** — inspeciona, arma (`-Arm
  -IAcceptSubmissionRefusalRisk`) ou desarma (`-Disarm`) o flag **sem reinstalar o pacote**.
  Sem argumento, só relata o estado. Testado em modo inspeção: relata corretamente
  `chave de servico ausente` com BasicDisplay ligado.
- **`tools/bind-bc250mcdm-failclosed.ps1`** ganhou dois preflights:
  1. **`D3D11CapsProbe`** — a `BASE_DE_AVANCO.md` mandava todo binder exigir o probe
     ausente/zero, e **esse gate não existia**. Agora o binding é bloqueado se o valor
     estiver diferente de zero. Estado medido agora: chave presente, **valor ausente =
     desarmado**;
  2. **modo de submissão declarado** — o binder lê `SdmaIndirectOnlyEnabled` do manifesto e
     avisa, antes de mutar, que os `PLANO A` estão suprimidos e que `RECUSA` com risco de
     `0x119` é desfecho possível. Não bloqueia; impede a surpresa.

### 6.2 Receita da corrida (executada pelo Daniel, em PowerShell elevado)

```
powershell -ExecutionPolicy Bypass -File tools\arm-d3d11-caps-probe.ps1 -Disarm
powershell -ExecutionPolicy Bypass -File tools\stage-bc250mcdm-package.ps1 -Execute
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-failclosed.ps1 -Execute -IAcceptBlackScreenRisk
```

**Boot frio antes.** A terceira linha é dry-run e não escreve nada — ela imprime o
preflight, o aviso do modo de submissão e sai. O `SdmaIndirectOnly` já vem em 1 pelo INF;
`arm-sdma-indirect-only.ps1` só é necessário para **desarmar** depois do bind, se você
quiser repetir o cenário antigo sem reinstalar.

**Teto: uma corrida.** Critério de abandono inalterado: primeiro timeout, fault, Código 43
ou ausência de confirmação visual deixa o watchdog restaurar BasicDisplay.

### 6.3 Duas dívidas declaradas deste pacote

1. **O `output_lab` (3.81.1.0) agora ranqueia ABAIXO do ativo (3.82.0.0).** Ele já estava
   estagnado e marcado como não-usável; se algum dia for rebindado sem rebuild, o PnP pode
   preferir o ativo. O `package-bc250mcdm-lab.ps1` deriva a versão do INF fonte
   (`Build + 1`), então um rebuild o coloca em 3.82.1.0 e resolve — mas rebuild exige o flag
   de bring-up destrutivo, que é decisão do dono.
2. **A UMD não foi recompilada** — `bc250umd.dll` é a do build anterior, apenas reassinada.
   Não houve mudança de código de UMD nesta faixa, e `BC250_D3D10_11_NATIVE_READY` continua
   `0`; logo o DWM continuará fail-fastando e **a tela desta corrida deve ficar preta de
   novo**. Esta corrida mede o `INDIRECT`, **não** o desktop.

Pacote assinado, auditado e **NÃO INSTALADO**: efeito no silício —
**NÃO VALIDADO EM HARDWARE.**
