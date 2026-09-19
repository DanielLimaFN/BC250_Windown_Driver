# 235 — Handoff 2026-09-10 12:16 · Flush de HDP validado no silício e nova parede `0x133`

> **Para a próxima IA.** Leia este arquivo inteiro antes de propor qualquer coisa.
> Normas: `CLAUDE.md` / `AGENTS.md` / `GEMINI.md` / `.cursorrules` (núcleo idêntico).
> Prioridades: `BASE_DE_AVANCO.md`. Auditoria anterior: `docs/234`.
> **O Daniel é quem executa corridas físicas. Você compila, analisa e prepara comandos.**

---

## 0. Estado da máquina AGORA (medido em 10/09 ~12:25)

| Fato | Valor |
|---|---|
| BC-250 | `Status=Error`, `CM_PROB_FAILED_POST_START` (**Código 43**) |
| Driver preso | `oem34.inf`, **3.127.1.0**, service `bc250mcdm` |
| Fusível anti-boot-loop | **`TRIPPED (2)`, Boots=2, LastStatus=`0xC0000001`** |
| Rollback do watchdog | **NÃO ocorreu** (BugCheck matou o watchdog) |
| Último boot | 12:19:07 (reboot do BugCheck) |
| Dump | `C:\Windows\MEMORY.DMP`, 827.376.956 bytes, 12:19:09 |

**Antes de qualquer coisa, o Daniel precisa rodar, elevado:**

```
tools\BC250_RECOVERY.cmd
```

E, para destravar um bind futuro: `tools\inspect-boot-fuse.ps1 -Reset` + **boot frio**.

---

## 1. O que a corrida 12:16 PROVOU (P0)

Pacote lab **3.127.1.0** (`-EnableRenderNode -EnableD3D11LabPromotion`).
Evidência: `docs/evidence/2026-09-10_12-16-46-DESKTOP-HQAG6T4-lab-binding/`.

### 1.1 O flush de HDP funcionou — critério escrito antes da corrida, cumprido

| | 11:22 (3.125.1.0) | 12:16 (3.127.1.0) |
|---|---|---|
| Submissões INDIRECT | morreu no FenceId **770** | **1819**, FenceId até **5335** |
| Voltas do anel SDMA | 12 | **84** |
| `IB_TRIPOD TIMEOUT` | 1 (fatal) | **0** |
| `IB NAO EXECUTADO` | 1 | **0** |
| Máximo de polls | **164.167** | **181** |
| BugCheck `0x119` | sim | **não** |

Distribuição de polls em 12:16 (n=1977): min 2, p50 **3**, p90 49, p99 180, max **181**.
A cauda ilimitada desapareceu. `FENCE_VISIBILIDADE` **não aparece nenhuma vez** — essa linha só
é emitida no caminho de timeout, e não houve timeout.

Caminhos escolhidos: 1805 `INDIRECT (Fill=0/0)` + 14 `INDIRECT (Fill=1/4096)`.

### 1.2 C1 (Draw sem CPU) validada no silício

```
12:16:51.095 [dwm.exe] DRAW_BLT_SUBSTITUTION: desarmado (HKLM\SOFTWARE\BC250\Umd\D3D11DrawBltSubstitution)
12:16:51.096 [dwm.exe] DrawIndexed: RECUSADO -- compilador/shader-state/draw PM4 ausente (Rejected=1)
```

`dwm.exe` (1220), `Explorer.EXE` (5304) e `dxgiadaptercache.exe` (10104) receberam
`S_OK (BC250 Native)` no `OpenAdapter10_2`. **Tela preta era o resultado esperado e correto** —
o `Draw` foi recusado honestamente, sem `memcpy`, sem sucesso silencioso. Confirmado.

### 1.3 C2 (resgate de evidência) validada no silício

O arquivamento truncou em 15.725.444 e o resgate recuperou **3.492.427 bytes**
(`sha256 896A681192D4D8726FAA20C31BBA37C14900150B5CD360E76E2B3CF10A9C3BA7`), que contêm
**a corrida inteira**. Sem a C2 esta análise não existiria. O arquivo está em
`bc250_display_log-tail-recovered.txt` com proveniência em `display-log-tail-recovered.json`.

> **Lição operacional, e ela custou uma auditoria errada:** a pasta em `docs/evidence/` pode
> estar truncada. Sempre compare `bytes` do `display-log-archive.json` com o tamanho de
> `C:\BC250_DISPLAY_LOG.txt` e atribua cada linha ao bind certo pelas janelas de `start_offset`
> dos `display-log-baseline.json` — o log é cumulativo e contém vários binds.

---

## 2. A PAREDE NOVA: `0x133 DPC_WATCHDOG_VIOLATION`

```
BugCheck 0x00000133 (0x0000000000000000, 0x0000000000000501, 0x0000000000000500, 0xfffff805220fb320)
12:19:07 — Kernel-Power 41 + WER 1001
```

`Arg2=0x501` contra `Arg3=0x500`: tempo excessivo em `DISPATCH_LEVEL`.
O log termina **logo após uma submissão bem-sucedida** (`IB EXECUTADO (INDIRECT) ... FenceId=5335`),
sem timeout e sem recusa — **não é o caminho do `0x119`**.

### 2.1 Hipótese (H), e é dívida que a faixa A15 declarou por escrito

`DxgkDdiSubmitCommandVirtual` é chamada em `DISPATCH_LEVEL`. O laço de poll em
[`bc250_sdma_present.c:1226`](../driver/wdk/bc250_sdma_present.c) roda **dentro** do
`KeAcquireSpinLock` da linha 1129 e só solta nas linhas 1302/1408. São 1819 submissões, até
181 polls de MMIO cada, em ~137 s de corrida (dwm carregou 12:16:50, reboot 12:19:07).

A A15 registrou isto literalmente: *"o laço do tripé pode gastar ~1 s segurando spinlock em
DISPATCH_LEVEL"*. A corrida cobrou a dívida.

**Nuance que manda na leitura:** os casos de 100–181 polls **não são visibilidade**. O flush já
dispara no casamento de RPTR; esses polls são espera real da engine. O flush eliminou a cauda
*infinita*; o que sobrou é o fato de esperarmos de forma **síncrona**.

### 2.2 PRIMEIRA COISA A FAZER — atribuir o `0x133` ao stack

Isto separa H de P0 e **não custa corrida**. Pedir ao Daniel, elevado:

```
powershell -Command "& 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe' -z C:\Windows\MEMORY.DMP -y 'srv*C:\Symbols*https://msdl.microsoft.com/download/symbols' -c '!analyze -v; !dpcwatchdog; lm m bc250*; q'"
```

- Se o stack apontar `bc250mcdm!Bc250SdmaPresent*` / `SubmitCommandVirtual` → **hipótese
  confirmada (P0)**, siga para §3.
- Se apontar outro módulo → a hipótese está **refutada**, e a §3 não é o conserto certo.
  Não implemente §3 sem essa leitura.

---

## 3. Conserto proposto (NÃO implementado): completar de forma assíncrona

**Não** baixe o teto de polls. Teto menor devolve `STATUS_IO_DEVICE_ERROR`, e esse é o caminho
conhecido do `0x119` — trocaria um BugCheck por outro.

O conserto certo é o contrato que o `dxgkrnl` espera:

1. `SubmitCommandVirtual` monta o pacote, publica o WPTR e **retorna** sem esperar a fence.
2. A observação da fence sai do `DISPATCH_LEVEL` (interrupção/DPC próprio ou item de trabalho).
3. `DMA_COMPLETED` é notificado de lá.

Restrições a respeitar:

- **Lei 4**: proibido `KeStallExecutionProcessor` / delay simulando execução; a fence continua
  tendo de ser escrita **pela GPU** e conferida sobre poison.
- **Lei 2**: o tripé continua obrigatório onde há efeito declarado — `RPTR==WPTR`, fence física
  sobre poison, e dados byte a byte. A conclusão assíncrona **não pode** enfraquecer a perna 3.
- O flush de HDP deve permanecer: foi ele que tornou a fence visível (P0, ver `docs/234` §2).
- O portão `audit_ring_wrap_arithmetic` proíbe comparação crua de RPTR; use
  `Bc250SdmaRptrMatches` / `Bc250TripodWrapRingPointer`.
- Escreva o **portão de auditoria** e prove-o por **mutação**, como manda a prática do repo.

---

## 4. Itens abertos, em ordem

1. **`0x133`** — atribuir pelo dump (§2.2) e então implementar a conclusão assíncrona (§3).
   Fecha com corrida de ≥2000 submissões sem BugCheck e sem timeout.
2. **`GFXHUB_FAULT STATUS=00841B40 ADDR=0x82C`** — fault de tradução real no IB de fill de
   **11.075.584 bytes**. **Não foi exercitado em 12:16** (só 14 fills de 4096 bytes), então
   segue sem teste. O flush de HDP **não** cobre esta classe: nos dois timeouts de 09/09 o RPTR
   congelou exatamente em `IndirectEndDW` e `depois_hdp` permaneceu no poison.
3. **Desktop acelerado** — exige compilador de shader, estado de pipeline e PM4 de draw no
   Node 1. `DrawIndexed: RECUSADO` é a resposta honesta hoje. A substituição por blt
   (`D3D11DrawBltSubstitution`) é paliativo **declarado e fechado por default**, não fecha etapa
   3D, e com ela armada pixel na tela deixa de ser prova de rasterização.
4. Limpeza: 11 `.txt` de análise soltos na raiz do repo (`call_ddi*.txt`, `dump_analysis.txt`,
   `disasm_*.txt`, `paging_stages*.txt`, `frame_analysis.txt`, `dbgprint_output.txt`).

---

## 5. O que existe hoje e você não deve reinventar

### Faixa C1 (10/09, offline — validada em hardware em 12:16)

- `Bc250UmdSubstituteDrawWithBlt` (`driver/umd/bc250_d3d_umd.c:1622`) — fonte única; único
  produtor de pixel é `Bc250UmdBltGpu` (SDMA). **Zero `memcpy`.**
- `Bc250UmdDrawBltSubstitutionIsLawful` (`driver/umd/bc250_umd_caps_probe.c`) — política pura,
  9 casos testados: tunable armado + winsys + residência + mesmas dimensões + mesmo formato +
  viewport na origem cobrindo tudo + **sem** blend (conta alpha-to-coverage e write mask parcial).
- `Bc250Umd3DPipelineIsDeclared` — fonte única de `CreateDevice` e `GetCaps Type=130`.
  Existe porque a suite offline **dependia do registro da máquina**: com `D3D11LabPromotion=1`
  deixado armado por um bind, `bc250_d3d_umd_test.c` reprovava sem mudança de código.
- `SubstitutedDrawCount` contado **separado** de `DrawCount`, de propósito.
- Tunable `D3D11DrawBltSubstitution` em `HKLM\SOFTWARE\BC250\Umd`; ferramentas
  `tools/arm-d3d11-draw-blt-substitution.ps1` e switch `-EnableD3D11DrawBltSubstitution`
  no binder (exige `-EnableD3D11LabPromotion`).

### Faixa C2 (10/09, offline — validada em hardware em 12:16)

- `tools/recover-bc250mcdm-lost-tail.ps1` — somente leitura sobre o log vivo, SHA-256 e
  proveniência, fim delimitado pelo `start_offset` do bind seguinte quando existe.
- O binder o chama **antes** de cada bind, depois de gravar o baseline novo.

### Portões novos em `tools/audit-bc250mcdm-safety.py`

- `audit_draw_has_no_cpu_fallback` — proíbe `memcpy`/`RtlCopyMemory`/`CopyMemory`/`memmove` e
  upload por CPU **dentro do recorte exato** da função de substituição; exige `Bc250UmdBltGpu`,
  a política pura, a contagem separada e o log honesto; **e detecta se o teste voltar a exigir
  a mentira**.
- `audit_lost_log_tail_is_recovered` — exige o resgate, exige que o binder o chame antes do
  arquivamento e depois do baseline, e proíbe qualquer escrita no log cumulativo.

Ambos verificados por mutação (6 mutações no total — ver `docs/234` §5.1).

---

## 6. Comandos de verificação offline (rodam sem hardware)

```
cmd /c driver\umd\build-umd.bat
```
Esperado: `EXIT=0` e `-> C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos PASS`.

```
cmd /c driver\wdk\build-contract.bat
```
Esperado: `EXIT=0`.

```
python tools\audit-bc250mcdm-safety.py
```
```
python tools\audit-mcdm-ddi-matrix.py
```
```
python tools\audit-ddi-noop-callbacks.py
```
```
python tools\audit-umd-node1-submission.py
```

### Empacotar um candidato novo

O `DriverVer` fonte está em **3.127.0.0** (`driver/wdk/package/bc250mcdm.inf`), e o pacote lab
soma +1 no build → **3.127.1.0, que JÁ FOI INSTALADO**. **Suba o INF fonte antes de empacotar
de novo**, senão dois artefatos diferentes carregarão o mesmo `DriverVer` — erro já cometido
com o 3.92.1.0.

O empacotador **não compila o SYS**; ele copia de `driver\wdk\output_lab\bc250mcdm.sys` e recusa
se alguma fonte de `driver\core` ou `driver\wdk` for mais nova. Se você mexer no KMD, o rebuild
exige `tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup`
— **decisão do dono do projeto**.

```
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode
```
```
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

### Corrida (só o Daniel executa, elevado)

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -EnableRenderNode -EnableD3D11LabPromotion -Execute -IAcceptBlackScreenRisk
```

Pré-requisitos: recovery feito, fusível `CLEAN`, BasicDisplay em Código 0, **boot frio**.
**Escreva o critério de PASS/FAIL antes de armar**, e lembre: enquanto o `Draw` for recusado,
**tela preta é o resultado esperado**, e qualquer imagem que apareça deve ser atribuída ao
rollback/BasicDisplay até prova em contrário.

---

## 7. Armadilhas já pagas neste projeto — não repita

1. **Fundir binds.** O log é cumulativo; recorte pelo `start_offset` antes de citar qualquer linha.
2. **`NonZero=1/256` não é desktop.** 255 dos 256 dwords amostrados são zero.
3. **Imagem na tela após rollback não é nosso pixel.** Já erramos isso duas vezes (07/09 18:16
   e 10/09 11:22).
4. **Teste que congela a mentira.** Três ocorrências: A15 (comparação crua de RPTR),
   `mcdm_callbacks_test` (STATUS_SUCCESS sem executor) e C1 (`memcpy` no draw). Ao consertar um
   falso sucesso, **conserte o teste também** e prove por mutação.
5. **Campo escrito em um só ramo do `#if`.** Três ocorrências (A13, B4, ContextAbi).
6. **Suite que lê o registro da máquina.** Política pura no `.c`, leitura de registro no chamador.
7. **`STATUS_SUCCESS` em DDI sem backend.** Lei 4; o `audit-ddi-noop-callbacks.py` existe por isso.
