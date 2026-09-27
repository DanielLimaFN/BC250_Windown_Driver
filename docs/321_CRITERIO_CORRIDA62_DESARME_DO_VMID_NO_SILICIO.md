# Critério da Corrida 62 — devolver o slot também no silício

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.223.1.0**,
> SYS `A17258E9FC9AB4A6...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que a Corrida 61 respondeu

**Desfecho B, limpo.** O flush de HDP aconteceu — **75 de 75** linhas com
`HdpFlush=1 Ok=1`, zero `[HDP_PT_FLUSH] FALHA:` — sob carga comparável (1.819
IBs contra 1.758), e **um fault com `WalkerError=6` permaneceu**.

Pela implicação escrita antes do ensaio, a visibilidade da tabela **não** era a
causa, e o suspeito passa a ser **o que a GPU usa como raiz**.

**Não atribuo nada à queda de 3 faults para 1.** Faults já eram raros, é uma
corrida só, e `SEM FAULT LATCHADO` subiu de 4 para 6 — menos fault latchado não
é o mesmo que menos fault.

### O que a Corrida 61 acrescentou

**Nós nunca escrevemos o PDE sujo.** Somando as Corridas 60 e 61 são **208
escritas** no índice 4096, **todas limpas**, com zero `[PDE_TRIPWIRE]`, zero
`[PDE_LARGEPAGE]` e zero `[ActiveUpdatePT] RECUSA`. Ainda assim o TDR leu
`0x4526CC071` — formato de PTE num slot de PDE, a mesma assinatura da Corrida 59.

**Hipótese minha que a própria medição derrubou:** cheguei a explicar o PDE sujo
como "a caminhada lê uma raiz já reciclada", e testei a correlação com a janela
de publicação antes de afirmá-la. **Não se sustenta** — na Corrida 58, 3
caminhadas resolveram com 2 janelas `MUDOU`. Fica registrado como refutado, não
como pendência.

---

## 1. O achado, por leitura, e não é hipótese

`Bc250VmidPoolRelease` era **somente software**.

Quando o contexto morria, a entrada do pool era limpa e o VMID voltava para os
livres — e o contexto GCVM **continuava com `ENABLE_CONTEXT` ligado e `PT_BASE`
apontando para a raiz do processo morto**, uma página que o VidMm pode liberar e
reutilizar a qualquer momento.

Existia `Bc250GcvmVmid8Uninstall`, mas só para o VMID 8 fixo. Os doze slots do
pool nunca tiveram teardown.

**Isso é a Lei 5 quebrada — APPLY sem TEARDOWN.** Vale corrigir tenha ou não
relação com o fault, e eu não afirmo que tenha.

---

## 2. O que mudou

`Bc250GcvmUninstallVmid` — o gêmeo por VMID, com a ordem que importa:

1. **`CNTL` primeiro.** Enquanto `ENABLE_CONTEXT` estiver ligado, o walker pode
   nascer uma tradução nova a partir da raiz que estamos abandonando.
2. Zerar os seis registradores de endereço, já com o contexto desabilitado.
3. **Invalidar a TLB daquele VMID por último** — tradução já cacheada sobrevive
   ao desarme do `CNTL`, e sem isso o desarme seria parcial e pareceria completo.

O desarme é **provado por readback** de `ENABLE_CONTEXT`, usa `FlushVmid` (por
VMID, nunca o flush fixo) e roda **fora** do `VmidPoolLock`. Falha se declara.

Nenhuma allowlist foi ampliada: escrever o `CNTL` dos VMIDs do pool já era
permitido por `Bc250VmidPoolIsWritableGcvmRegister`.

---

## 3. Desfechos

> Assinatura e implicação em campos separados (Lei 10). Desfecho parcial não é
> desfecho.

### A — O desarme acontece e o fault acaba

**Assinatura:** (1) `[VMID_POOL] DESARMADO` com `status=0 Readback=1 Flush=1`
em todas as ocorrências e zero `FALHA NO DESARME`; (2) **zero `DRENO #`** — de
qualquer `Cid`, qualquer endereço; (3) carga comparável, 1.700–2.000 IBs.

**Implicação:** o slot armado sobre raiz morta era a causa. DX.5 avança. **Pixel
continua exigindo prova própria.**

### B — O desarme acontece e o fault permanece

**Assinatura:** desarme 100% OK **e** pelo menos um `DRENO #` com
`WalkerError=6`.

**Implicação:** o estado velho não era a causa — **e o desarme continua certo**,
porque fecha um estado que não tem direito de existir. O próximo suspeito passa
a ser o `PT_BASE` **no instante da submissão**: provar que o `CNTL`/`PT_BASE`
lidos do silício imediatamente antes do `INDIRECT_BUFFER` são os do contexto que
submete. Isso é telemetria que ainda não existe.

### C — O desarme falha

**Assinatura:** `[VMID_POOL] FALHA NO DESARME:` presente, ou `Readback=0`.

**Implicação:** **inconclusivo** quanto ao fault — proibido ler qualquer coisa
sobre a hipótese. O `passo` e o `reg` da linha dizem onde parou: 20/21 é o
`CNTL`, 22 é endereço, 23 é a TLB.

### D — O desarme nunca é exercitado

**Assinatura:** zero `[VMID_POOL] DESARMADO` na corrida inteira.

**Implicação:** nenhum contexto chegou a devolver VMID — o que por si só
contradiz as Corridas 52–61, onde houve dezenas de `LIBERADO`. Seria defeito de
ligação, não do silício.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar: BugCheck (o desarme roda em `DestroyContext`, caminho que até hoje
não tocava MMIO); `Polls` subindo no flush por VMID; exaustão de VMID aparecendo
pela primeira vez; faults migrando para VMIDs recém-desarmados; ou o
`[PDE_TRIPWIRE]` disparando, que continua sem ter disparado nenhuma vez.

---

## 4. Controles

| Controle | Corrida 61 |
|---|---:|
| `IB EXECUTADO (INDIRECT)` | 1.819 |
| TDR / restarts provados | 7 / 7 |
| Faults drenados / sem latch | 1 / 6 |
| `HdpFlush=1 Ok=1` | 75 / 75 |
| `[PDE_WRITE] ARENA` | 107, todas limpas |
| `PoolReadyMask` máximo | `0x000E` |
| GFX máximo | `Submetidas=56 Aposentadas=56 EmVoo=0` |
| BugCheck | nenhum |
| Rollback | `RECOVERY_OK_ONLINE` |

**Esta faixa não acende a tela.** Tela preta continua sendo o resultado correto;
qualquer imagem deve ser atribuída ao rollback/BasicDisplay até prova em
contrário.

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
| SYS assinado | `A17258E9FC9AB4A6...` |
| PDB | `0E42773DCDDEA026...` |
| UMD | `45814D0E115E4BAD...` |
| INF | `DE1FE0A5C0C0EBD2...` |
| CAT | `8FE7C55CB95497B0...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI** no SYS assinado:
`[VMID_POOL] DESARMADO: VMID%02u status=%u` e
`[VMID_POOL] FALHA NO DESARME:`.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias
**PASS**, `git diff --check` limpo.

**Sete mutações verificadas, todas reprovando na auditoria E na suíte:** o
desarme deixando de ser chamado; a TLB invalidada antes de o `CNTL` cair; o
desarme não provado por readback; o flush de VMID fixo no lugar do por-VMID; a
falha passando calada; a suíte parando de exigir a TLB do VMID certo; e a suíte
perdendo o contraponto do vizinho.

**Um portão meu passou — terceira faixa seguida, e a mesma classe.** Ele
procurava `CHECK(f->LastFlushVmid == target);` no arquivo inteiro, e a string
também existe no bloco de **install** — afrouxar a asserção do bloco de
**desarme** não reprovava nada. Refeito com a busca delimitada ao bloco. As três
ocorrências desta sequência foram todas "procurei a string no escopo errado";
está registrado como padrão, não como incidente isolado.

**Armadilha de ferramenta:** `bc250_gcvm_vmid8_transaction.{c,h}` estavam com
finais de linha **mistos** (228/108 e 342/115). Normalizados para CRLF antes de
editar. É o terceiro arquivo desta sequência com o problema.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
