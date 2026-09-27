# Passagem de bastão — 22/09/2026, depois das Corridas 59 a 63

> Para a próxima IA. Leia `CLAUDE.md` primeiro: as Leis 1–10 mandam em tudo
> abaixo. Este documento substitui o
> [docs/317](317_PASSAGEM_DE_BASTAO_POS_CORRIDA58_FLUSH_DOS_VMIDS_DO_POOL.md),
> cujo passo seguinte já foi executado, medido e **refutado como causa**.

---

## 1. A frase curta

Cinco corridas fecharam cinco hipóteses e **nenhuma acendeu a tela**. O que
sobrou não é mais "por que a tradução falha", e sim **duas** perguntas separadas,
ambas com telemetria pronta e nenhuma medida ainda:

1. **`Cid=10`** — o endereço que falta **perdeu 8 bits**. Confirmado seis vezes.
   Falta achar **quem** dereferencia `VA>>8`.
2. **`Cid=9`** — nós escrevemos o PDE do índice 4096 **limpo 314 vezes**, e o
   TDR o lê **sujo** em três corridas. A Corrida 64 separa "a escrita não chega"
   de "alguém suja depois".

**O candidato 3.225.1.0 está empacotado, auditado e NÃO instalado.** O critério
da Corrida 64 está congelado em
[docs/323](323_CRITERIO_CORRIDA64_ESCREVEMOS_LIMPO_LEEM_SUJO.md). **Comece
rodando essa corrida** — todo o trabalho offline já foi feito.

---

## 2. O que estas cinco corridas fecharam

| Corrida | Faixa testada | Desfecho | O que ficou provado |
|---|---|---|---|
| **59** | DX.12 — flush cobre os VMIDs do pool | **B** | `PoolReadyMask` até `0x001E`, ACK em todos, `Polls=1`. **Não era a causa** |
| **60** | DX.13 — escrita de PDE observável | **E** | **Nós escrevemos o índice 4096, 101×, sempre limpo.** Duas caminhadas contemporâneas resolveram |
| **61** | DX.14 — HDP drenado antes da TLB | **B** | 75/75 `HdpFlush=1`. **Visibilidade não era a causa** |
| **62** | DX.15 — desarme do VMID no silício | **B** | 24/24 desarmes OK, zero BugCheck. **Lei 5 fechada, não era a causa** |
| **63** | DX.16 — granularidade corrigida | **A** (`Cid=10`) | `VA<<8` resolve, `VA(página<<12)` não. **Perda de 8 bits** |

**Quatro hipóteses minhas morreram por medição.** Isso é o processo funcionando:
cada corrida produziu informação nos dois desfechos, como a Lei 9 exige.

---

## 3. A correção que manda em tudo que você vai ler

**A granularidade do endereço de fault é `página << 12`, e a conclusão da
Corrida 47 (`<<20`) era uma leitura errada.**

Proveniência (P1, local): `inc/linux_ref/gc_10_1_0_sh_mask.h:12163-12168` declara
`LOGICAL_PAGE_ADDR_LO32` com **32 bits** e `_HI4` com **4 bits** — página de
**36 bits**. Com 4 KiB isso cobre 48 bits de VA. Com 1 MiB seriam 56, e vinte
dos trinta e seis ficariam permanentemente zerados.

Corroboração independente (P0): a página `0x200040` sob `<<12` dá
`0x200040000`, que a nossa própria linha `[ALVO 0x200040]` reporta como a **base
exata** de uma alocação viva.

**Consequência:** `<<20` é identicamente `(página<<12)<<8`. O que a Corrida 47
mediu não foi tamanho de página — foi que **o endereço real é 256× o que
faltou**.

**E isso corrige o meu próprio relato de três corridas.** Eu escrevi que "a
tabela vista pela CPU está certa e a GPU falta no mesmo endereço". Para os faults
`Cid=10` isso é **falso**: no endereço que realmente faltou a tabela diz
`PDE=0` — **não mapeado** — e a GPU concorda. **Não havia paradoxo.** As linhas
`RESOLVEU em <<20` caminhavam outro endereço.

> Qualquer documento anterior a `docs/322` que fale em "granularidade `<<20`"
> está lendo um defeito de shift como se fosse tamanho de página.

---

## 4. As duas perguntas abertas

### 4.1 `Cid=10` — quem dereferencia `VA>>8`

| Corridas | Página | VA real (`<<12`) | O que a GPU acessou |
|---|---|---|---|
| 45, 46, 48, 59 | `0x2431` | `0x243100000` | `0x2431000` |
| 45, 62, 63 | `0x245B` | `0x245B00000` | `0x245B000` |

Os encodings `>>8` que **conferi por leitura estão corretos**: `CP_RB0_BASE`
(provado na Corrida 44), `SPI_SHADER_PGM_LO/HI` (`>>8` e `>>40`, par coerente) e
`CB_COLOR0_BASE`. **O sítio defeituoso ainda não tem nome.**

O campo `Cid` **não tem enumeração local** — `gc_10_1_0_sh_mask.h` traz só shift
e máscara. **Não invente um nome de cliente** (Lei 7).

### 4.2 `Cid=9` — o PDE sujo do índice 4096

| | Medição |
|---|---|
| O que **nós escrevemos** | **314 escritas** (Corridas 60, 61, 63), **todas limpas**, zero `[PDE_TRIPWIRE]`, zero recusa |
| O que o **TDR lê** | `0x450C9C071` (C59), `0x4526CC071` (C61), `0x451BCC071` (C63) — formato de **PTE** em slot de **PDE** |

`Bc250GcvmEncodePde` **não consegue** produzir esses valores: recusa qualquer
flag fora de `VALID/SYSTEM/SNOOPED`.

**A Corrida 64 é exatamente este teste** e tem três desfechos distinguíveis:
`DIVERGENCIA` → o defeito é nosso e anterior à GPU; confronto limpo contra
leitura suja → alguém sobrescreve; `Escritas=0` → nunca houve duas verdades.

---

## 5. Hipóteses mortas — não ressuscite sem medição nova

1. **Disputa de contexto pelo VMID 7** — o alocador fechou (Corridas 52–54) e o
   fault continuou.
2. **TLB velha nos VMIDs do pool** — cobertura corrigida e com ACK (59), taxa de
   fault inalterada.
3. **Visibilidade da tabela (HDP)** — drenado 75/75 (61), fault permaneceu.
4. **VMID armado sobre raiz morta** — desarmado 24/24 (62), fault permaneceu.
5. **"A caminhada lê uma raiz reciclada"** — eu testei a correlação com a janela
   de publicação **antes** de afirmá-la: na Corrida 58, 3 caminhadas resolveram
   com 2 janelas `MUDOU`. **Refutada, não pendente.**
6. **Granularidade `<<20`** — ver §3.

---

## 6. Armadilhas ativas, todas medidas

1. **`MoreFaults=1` não significa fila.** Com o latch drenado, nenhum TDR
   entregou um segundo fault. **Não invente o significado** (Lei 7).
2. **`CRASH_ON_NO_RETRY_FAULT` (bit 30 do `FAULT_CNTL`) está em 1**, medido no
   valor cru `0x3FFFFFFC`. Nunca investigado.
3. **`WALKER_ERROR` e `PERMISSION_FAULTS` não têm enumeração local.** Os valores
   (6, 0x3, 0xB) permanecem **sem nome**.
4. **Ausência de fault latchado não é ausência de fault.** A Corrida 57 teve 5
   TDRs sem um único fault com 2.173 IBs.
5. **A caminhada é post-mortem.** `JANELA_DE_PUBLICACAO` diz se a tabela mudou
   entre a submissão e o fault. `TABELA MUDOU` ⇒ a PTE lida pode ter nascido
   depois e **não fala do instante do fault**.
6. **A escrita em `0xA120` está aberta**, com escritor único
   (`Bc250FaultLatchDrain`) e guarda de sítio. Não amplie sem critério próprio.

---

## 7. Um padrão meu que você vai repetir se não souber dele

**Quatro portões meus passaram nesta sequência, e os quatro pela mesma família:
procurei a existência de uma string, não o seu uso no escopo certo.**

| Faixa | Como passou | Como ficou |
|---|---|---|
| DX.12 | procurava string em vez de contar ramos | invariante de contagem |
| DX.13 | `InterlockedIncrement(` solto — o arquivo tem 7 ocorrências | contador próprio, sem espaços |
| DX.14 | procurava o **argumento**, não a **chamada** | chamada inteira, sem espaços |
| DX.15 | string no arquivo, existindo também no bloco de *install* | busca **delimitada** ao bloco |

Na DX.17 nenhum portão passou — foi a primeira em cinco, porque escrevi os
invariantes sobre **uso** e **ordem** desde a primeira versão. **Faça isso desde
o começo.**

E um caso distinto, que vale registrar sozinho: **um portão meu estava
congelando uma conclusão errada.** Ele *exigia* a frase *"a PTE EXISTE, logo o
defeito NÃO é PTE ausente"* — que a §3 refutou. É a classe "o gate carimbava a
mentira", pela primeira vez neste projeto aplicada a uma **conclusão** em vez de
a um contador.

---

## 8. Como rodar

**Offline:**

```bash
driver\wdk\build-contract.bat
```

```bash
python tools\audit-bc250mcdm-safety.py
```

**Empacotar** (suba o `DriverVer` em `driver/wdk/package/bc250mcdm.inf` antes —
o 3.225 já foi gerado):

```bash
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
```

```bash
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission
```

```bash
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

**Vincular** — o Daniel executa, **boot frio obrigatório**:

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

**Corridas ilimitadas estão autorizadas.** Ainda assim, minere antes: o maior
achado desta sequência (§3) veio de **ler um header**, não de um ensaio.

---

## 9. Armadilhas de ferramenta que custaram tempo

- **O empacotador não compila, só copia.** Confira as strings novas **em ANSI**
  no SYS **assinado** — as mensagens de log são `char*`, e UTF-16 dá falso
  negativo. Confira também que a string **antiga sumiu**: é a única prova de que
  o candidato anterior não foi recopiado.
- **O empacotador precisa dos três switches** (`-EnableRenderNode
  -EnableShaderDispatch -EnablePersistentSubmission`), senão sai `DISPLAY_LAB`
  READ-ONLY.
- **`sed -i` converte o arquivo inteiro de CRLF para LF.** Aconteceu com
  `bc250_mcdm_callbacks.c`. Use Python com `newline=''`.
- **Finais de linha MISTOS** em `bc250_display_ddi.c`,
  `bc250_gcvm_vmid8_transaction.{c,h}` e `bc250_mmio_topology_transport.c`.
  Âncoras multi-linha falham. Normalize antes de editar.
- **Heredoc do Bash corrompe backslashes.** Escreva os scripts Python com a
  ferramenta Write; custou quatro tentativas nesta sessão.
- **Toda linha de log nova em caminho raro precisa de isenção no amostrador**
  (`bc250_display_ddi.c`), **e** de teto próprio se rodar em caminho quente. A
  armadilha 16 já queimou uma medição inteira.

---

## 10. Estado da árvore

Nada commitado. `git status --short` mostra **60 arquivos**, no branch
`p6-rendernode-wddm`. Documentos desta sequência: `docs/318` a `docs/323`, mais
este.

**Último candidato instalado: 3.224.1.0** (Corrida 63).
**Empacotado e NÃO instalado: 3.225.1.0**, SYS
`5C2950513E5C66CEE312A2535F773C2093CA1B9075465A63F21DB3B289236072`.
Fusível **CLEAN**. Boot frio obrigatório antes de qualquer bind.

`BASE_DE_AVANCO.md` e `PLANO_DESKTOP_ACELERADO.md` estão atualizados até a
Corrida 63, com DX.12 a DX.17 registradas.

---

## 11. O que continua ABERTO, e não se engane com o progresso

Nada desta sequência produziu **um pixel**. O que avançou foi o diagnóstico da
tradução de memória (fase DX). Continuam abertos:

- **DX.5** — a tradução passar a funcionar.
- **D0.7** — perna 3 do tripé (byte a byte). **Nunca executou.**
- **D1.6–D1.8** — primeiro pixel não-nulo.
- **D2.2/D2.5** — o tradutor de shader ainda carrega de `C:\BC250_LIB`.
- **D3 (0 de 8)** — **o maior bloco do plano, e ninguém tocou nele.**
- **D4, D5** — desktop acelerado e estável.

**Tela preta é o resultado CORRETO** enquanto a parede de rasterização existir.
Qualquer imagem deve ser atribuída ao rollback/BasicDisplay até prova em
contrário — esse engano já foi cometido em 07/09, 10/09 e 12/09.

**NADA DA PRÓXIMA FAIXA FOI VALIDADO EM HARDWARE.**
