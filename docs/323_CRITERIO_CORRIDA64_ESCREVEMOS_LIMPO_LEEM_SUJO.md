# Critério da Corrida 64 — escrevemos limpo, leem sujo

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.225.1.0**,
> SYS `5C2950513E5C66CE...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que a Corrida 63 respondeu

**Desfecho A para o `Cid=10`, com a telemetria já honesta:**

```
WALK VA(pagina<<12) = 0x245B000    NAO RESOLVE
WALK VA<<8(perda de 8 bits) = 0x245B00000 RESOLVEU para PA=0x4565CC000
```

O endereço que faltou é `0x245B000`; o real é `0x245B00000`. **Perda de 8
bits**, agora publicada pelo próprio log em vez de reconstruída por mim entre
corridas. O padrão soma seis ocorrências no projeto.

**E o `Cid=9` mudou de comportamento:** na Corrida 60 o `0x200040000` RESOLVIA;
nesta ele **NÃO resolve** — a caminhada parou no PDE sujo `0x451BCC071` no
índice 4096 (`reservado[5:3]=0x30`). Terceira ocorrência do PDE sujo (Corridas
59, 61, 63).

---

## 1. A contradição que sobrou, e ela é nossa

| | Medição |
|---|---|
| O que **nós escrevemos** no índice 4096 | **314 escritas** nas Corridas 60, 61 e 63 — **todas limpas**, zero `[PDE_TRIPWIRE]`, zero recusa |
| O que o **TDR lê** no índice 4096 | valor de **PTE** em três corridas: `0x450C9C071`, `0x4526CC071`, `0x451BCC071` |

As duas coisas não podem ser verdadeiras ao mesmo tempo sem que **alguém escreva
depois** — ou sem que **a nossa escrita não chegue onde acreditamos**.

**A pergunta única: qual das duas?**

---

## 2. O que mudou — e é o mínimo que separa os dois casos

**Readback imediato.** Depois de gravar `destination[4096]`, lemos de volta pelo
**mesmo ponteiro** e comparamos com o que escrevemos.

- `readback != encoded` → a escrita **não chegou** ao destino que acreditamos. O
  defeito é do nosso caminho de escrita ou do mapeamento.
- `readback == encoded` → escrevemos certo, e quem suja é **outro agente ou
  outro momento**.

**A divergência NÃO é corrigida.** Reescrever esconderia a frequência, que é
justamente o dado que falta — e há portão proibindo a reescrita nesse ramo.

**Confronto no TDR.** `[PDE_ARENA_CONFRONTO]` publica o **último valor que nós
escrevemos** e quantas escritas houve, na mesma janela em que a caminhada
imprime o que encontrou. Os dois números passam a estar no **mesmo log**, em vez
de eu compará-los à mão entre corridas. `Escritas=0` fica declarado, em vez de
virar "eram iguais".

Isto lê a memória pela CPU. **Não prova o que a GPU enxerga** — prova o que a
CPU deixou lá.

---

## 3. Desfechos

> Assinatura e implicação em campos separados (Lei 10). Desfecho parcial não é
> desfecho.

### A — A escrita não chega

**Assinatura:** pelo menos uma `[PDE_WRITE] DIVERGENCIA:`.

**Implicação:** o defeito é **nosso e anterior à GPU**. `destination` não aponta
para onde pensamos, ou o índice está errado. Vira conserto de endereçamento,
não mais investigação — e explicaria o PDE sujo sem precisar de terceiros.

### B — A escrita chega e a memória suja depois

**Assinatura:** zero `DIVERGENCIA`, **e** um `[PDE_ARENA_CONFRONTO]` cujo
`UltimoQueEscrevemos` seja limpo (termina em `001`) enquanto a linha `[PT_WALK]`
da mesma janela mostra `reservado[5:3]` diferente de zero.

**Implicação:** escrevemos certo e alguém sobrescreve. Os suspeitos passam a ser
**quem mais toca aquela página** — reuso de página pelo VidMm, ou sobreposição
entre a tabela raiz de um processo e outra estrutura. `Escritas` diz quantas
vezes passamos por lá antes.

### C — Nunca escrevemos aquele índice nesta sessão

**Assinatura:** `[PDE_ARENA_CONFRONTO]` com `Escritas=0`.

**Implicação:** o PDE sujo não tem relação com escrita nossa nenhuma, e a
contradição do §1 **se dissolve** — nunca houve duas verdades, só uma leitura.
O alvo vira inteiramente "quem monta essa raiz".

### D — Nenhum fault latchado

**Assinatura:** zero `DRENO #` com carga comparável (1.700–2.000 IBs).

**Implicação:** **inconclusivo.** A Corrida 57 teve 5 TDRs sem um único fault
latchado com 2.173 IBs. Ausência de latch não é ausência de fault, e nada desta
faixa pode ser creditado.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar: `DIVERGENCIA` aparecendo em **toda** escrita (seria defeito
grosseiro e recente, não o que três corridas mostram); o `Cid=10` deixando de
exibir o padrão `VA<<8`; um `Cid` diferente de 9 e 10 — o campo **não tem
enumeração local, não inventar nome** (Lei 7); BugCheck; ou o índice 4096 sendo
escrito por um `Lvl` diferente de 1.

---

## 4. Controles

| Controle | Corrida 63 |
|---|---:|
| `IB EXECUTADO (INDIRECT)` | 1.877 |
| TDR / restarts provados | 6 / 6 |
| Faults drenados / sem latch | 2 / 4 |
| `[PDE_WRITE] ARENA` | 106, todas limpas |
| `[VMID_POOL] DESARMADO` | 24, todos OK |
| `HdpFlush=1 Ok=1` | 75 / 75 |
| BugCheck | nenhum |
| Rollback | `RECOVERY_OK_ONLINE` |

**Esta faixa é diagnóstico e não deve mudar o fault.** Tela preta continua sendo
o resultado correto.

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
| SYS assinado | `5C2950513E5C66CE...` |
| PDB | `348052E9D5320C58...` |
| UMD | `24D604813802DC57...` |
| INF | `B77EBBDCC0EDB0F6...` |
| CAT | `10DCBDDE6C795C21...` |

`Lab package audit: PASS`. Conferido **em ANSI** no SYS assinado:
`[PDE_WRITE] DIVERGENCIA: idx=%u` e `[PDE_ARENA_CONFRONTO] UltimoQueEscrevemos`.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias
**PASS**, `git diff --check` limpo.

**Sete mutações verificadas, todas reprovando:** o readback deixando de existir;
o readback não comparado; a divergência passando calada; o último valor não
retido; o confronto sumindo do TDR; a divergência virando reescrita silenciosa;
e o confronto perdendo a isenção do amostrador.

**Nenhum portão meu passou nesta faixa** — a primeira em cinco. Nas quatro
anteriores o padrão foi sempre "procurei a string no escopo errado"; aqui os
invariantes foram escritos sobre **uso** e **ordem** desde a primeira versão.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
