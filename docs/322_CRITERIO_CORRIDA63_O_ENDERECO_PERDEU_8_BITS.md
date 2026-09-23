# Critério da Corrida 63 — o endereço que falta perdeu 8 bits

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.224.1.0**,
> SYS `6FCFB6EC0F0456E2...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. A Corrida 62, e a correção que ela obrigou

**Desfecho B.** O desarme aconteceu — **24 de 24** `[VMID_POOL] DESARMADO` com
`status=0 Readback=1 Flush=1`, zero falhas — e **dois faults permaneceram**. O
desarme continua certo (Lei 5), e não era a causa.

O risco que declarei — MMIO em `DestroyContext`, caminho que nunca tinha tocado
registrador — **não se materializou**: zero BugCheck.

### A correção, e ela é grande

**A granularidade está resolvida, e nunca foi granularidade.**

`gc_10_1_0_sh_mask.h:12163-12168` (P1, local): `LOGICAL_PAGE_ADDR_LO32` tem **32
bits** e `LOGICAL_PAGE_ADDR_HI4` tem **4 bits** — um número de página de **36
bits**. Com página de 4 KiB isso cobre 48 bits de VA, que é o espaço de uma GPU.
Com página de 1 MiB seriam 56 bits, e vinte dos trinta e seis ficariam
permanentemente zerados.

**O endereço é `página << 12`.**

Corroboração independente (P0): a página `0x200040` sob `<<12` dá
`0x200040000`, que a nossa própria linha `[ALVO 0x200040]` reporta como a **base
exata** de uma alocação viva (`Base=0x200040000 Size=0x40000`).

**Portanto a conclusão da Corrida 47 era uma leitura errada.** `<<20` é
identicamente `(página<<12)<<8`. O que aquele ensaio mediu não foi o tamanho da
página — foi que **o endereço real é 256 vezes o endereço que faltou**. Isso é
assinatura de **perda de 8 bits**, e vale para todos os faults `Cid=10` do
projeto:

| Corridas | Página | VA (`<<12`) | VA `<<8` |
|---|---|---|---|
| 45, 46, 48, 59 | `0x2431` | `0x2431000` | `0x243100000` |
| 45, 62 | `0x245B` | `0x245B000` | `0x245B00000` |

### O que isso corrige no meu próprio relato

Eu disse, por três corridas, que **"a tabela vista pela CPU está certa e a GPU
falta no mesmo endereço"**. Para os faults `Cid=10` — que são **2 de 2** nesta
corrida — isso é **falso**: no endereço que realmente faltou (`0x245B000`) a
tabela diz `PDE=0` no índice 18, ou seja **não mapeado**, e a GPU concorda. Não
há paradoxo. As linhas `RESOLVEU em <<20` estavam caminhando **outro endereço** e
não provavam nada.

O paradoxo do `Cid=9` (`0x200040000`, mapeado e mesmo assim faltou) **continua de
pé**, mas não ocorreu nesta corrida.

---

## 1. A pergunta única

**Quem dereferencia o endereço com 8 bits a menos?**

---

## 2. O que mudou nesta faixa

Só telemetria, e é **correção de honestidade**, não experimento:

- O segundo candidato do discriminador deixou de se chamar `<<20` e passou a se
  chamar `VA<<8(perda de 8 bits)`; o primeiro, `VA(pagina<<12)`.
- A linha de sucesso do resolver dizia *"a PTE EXISTE, logo o defeito NÃO é PTE
  ausente"*. Passou a dizer que, **se o shift for `VA<<8`, isso significa que o
  endereço que faltou perdeu 8 bits, e NÃO que a tabela esteja certa**.

**Um portão meu estava congelando a conclusão errada.** Ele *exigia* a frase
antiga. É a classe "o gate carimbava a mentira", desta vez aplicada a uma frase
de veredito em vez de a um contador — a primeira ocorrência dessa classe sobre
uma **conclusão** neste projeto. Corrigido junto.

Os encodings `>>8` que consegui conferir por leitura estão **corretos**:
`CP_RB0_BASE` (provado na Corrida 44), `SPI_SHADER_PGM_LO/HI` (`>>8` e `>>40`,
par coerente) e `CB_COLOR0_BASE`. O sítio defeituoso **ainda não tem nome**, e
eu não invento um.

---

## 3. Desfechos

> Assinatura e implicação em campos separados (Lei 10). Desfecho parcial não é
> desfecho.

### A — A perda de 8 bits se confirma e se localiza

**Assinatura:** pelo menos um fault com `WALK VA<<8(perda de 8 bits) = 0x…
RESOLVEU` **e** `WALK VA(pagina<<12) … NAO RESOLVE` no mesmo fault.

**Implicação:** confirma o padrão com a telemetria já honesta, e o endereço
resolvido nomeia a **alocação** de destino — que é a pista para o sítio. Não
conserta nada: é o alvo ficando localizável.

### B — O padrão não se repete

**Assinatura:** faults em que o `VA(pagina<<12)` **resolve** (como o `Cid=9` de
`0x200040000`), ou em que **nenhum** dos dois resolve.

**Implicação:** há mais de uma classe de defeito, e a perda de 8 bits explica só
a `Cid=10`. O `Cid=9` volta a ser o alvo, e ele é o paradoxo real — endereço
mapeado, permissivo, contemporâneo, e a GPU falta.

### C — Nenhum fault latchado

**Assinatura:** zero `DRENO #` com carga comparável (1.700–2.000 IBs).

**Implicação:** **inconclusivo.** A Corrida 57 já teve 5 TDRs sem um único
fault latchado com 2.173 IBs — ausência de latch não é ausência de fault.
Proibido atribuir a qualquer faixa desta sequência.

### D — O desarme regride

**Assinatura:** `[VMID_POOL] FALHA NO DESARME:` presente, ou `DESARMADO` com
`Readback=0`.

**Implicação:** regressão da faixa anterior; o resto da corrida não é comparável
com a 62 e a análise para aqui.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar: um fault cujo `VA<<8` **também** não resolva; `Cid` diferente de 9 e
10 (o campo não tem enumeração local — **não inventar nome**, Lei 7); BugCheck;
`[PDE_TRIPWIRE]` disparando pela primeira vez; ou o `VA(pagina<<12)` resolvendo
para um PA que não pertence a nenhuma alocação nomeada.

---

## 4. Controles

| Controle | Corrida 62 |
|---|---:|
| `IB EXECUTADO (INDIRECT)` | 1.755 |
| TDR / restarts provados | 7 / 7 |
| Faults drenados / sem latch | 2 / 5 |
| `[VMID_POOL] DESARMADO` | 24, todos `status=0 Readback=1 Flush=1` |
| `HdpFlush=1 Ok=1` | 73 / 73 |
| GFX máximo | `Submetidas=70 Aposentadas=70 EmVoo=0` |
| BugCheck | nenhum |
| Rollback | `RECOVERY_OK_ONLINE` |

**Esta faixa é só telemetria: não muda nenhum caminho de GPU e não deve mudar o
fault.** Tela preta continua sendo o resultado correto.

---

## 5. Procedimento

Conferir o fusível e fazer **boot frio** antes do bind.

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 6. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `6FCFB6EC0F0456E2...` |
| PDB | `63C59F930E056B4C...` |
| UMD | `DA6CFC4222C9C54C...` |
| INF | `87AA2A642AA89F92...` |
| CAT | `C7F49EF24C616F0F...` |

`Lab package audit: PASS`. Conferido **em ANSI** no SYS assinado:
`VA<<8(perda de 8 bits)`, `VA(pagina<<12)` e `NAO que a tabela esteja certa`
presentes; a frase antiga `a PTE EXISTE, logo o defeito NAO e' PTE ausente`
conferida **ausente** — prova de que o empacotador não copiou o candidato
anterior.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias
**PASS**, `git diff --check` limpo.

**Três mutações verificadas, todas reprovando:** a conclusão errada voltando ao
resolver; o candidato voltando a se chamar granularidade; e o rótulo voltando a
ser o shift cru.

**Um portão meu passou — quarta vez nesta sequência, e de novo a mesma família.**
Ele provava que o **array** de rótulos existe, não que as duas linhas o
**consumam**; a mutação que fixava `"<<20"` na chamada passava. As quatro
ocorrências foram: procurar a string no arquivo em vez do bloco (duas vezes),
procurar o argumento em vez da chamada, e agora procurar a existência em vez do
uso. **É um padrão meu, não azar.**

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
