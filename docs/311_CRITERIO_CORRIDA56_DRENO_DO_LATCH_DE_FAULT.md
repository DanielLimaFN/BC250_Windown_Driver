# Critério da Corrida 56 — o dreno do latch de fault

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.217.1.0**,
> SYS `9753E689...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, P1 PERSISTENT
> SUBMISSION. **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que muda

A Corrida 55 mediu `LATCH_CNTL=0x3FFFFFFC` — **`Clear=0` e
`AceitaAtualizacoes=0`**. O par `STATUS`/`ADDR` trava no primeiro fault e só se
solta quando alguém escreve o bit 0. Nunca escrevemos.

Esta faixa **escreve** esse bit. É a primeira vez que este driver toca
`GCVM_L2_PROTECTION_FAULT_CNTL`, e a allowlist de escrita foi aberta
especificamente para ele, com escritor único e guarda de sítio.

O dreno lê, pulsa o `CLEAR`, lê de novo, até o `STATUS` sair zero ou bater o
teto de 8. O `CNTL` é restaurado e **provado por readback**.

---

## 1. A pergunta única

**Quantos faults saem da fila?**

Em onze corridas todo `Status` decodificado trouxe `MoreFaults=1` — havia mais,
e nunca vimos nenhum.

---

## 2. Linhas novas

```
[TDR_GCVM_FAULT] DRENO: status=u Drenados=u ClearAcked=u ClearIneficaz=u Teto=u
                        MoreFaults=u CntlAntes=0x........ CntlReadback=0x........ Restaurado=u
[TDR_GCVM_FAULT] DRENO #n: Status=0x........ -> Vmid=u WalkerError=u ... AddrPagina=0x........
```

---

## 3. Desfechos

### A — A fila aparece

**Assinatura:** `Drenados >= 2`, com `ClearAcked=1`, `ClearIneficaz=0` e
`Restaurado=1`.

**Implicação:** pela primeira vez vemos o que `MoreFaults` escondia. As linhas
`DRENO #n` dizem se os faults seguintes são do **mesmo** VMID e endereço
(cascata de um só acesso) ou de VMIDs/endereços **diferentes** (falhas
independentes). Isso decide se o alvo é um endereço ou um padrão.

### B — A fila tem um só

**Assinatura:** `Drenados == 1` com `ClearAcked=1` e `Restaurado=1`.

**Implicação:** o `CLEAR` funciona e **não havia** fila — `MoreFaults=1`
significava outra coisa que ainda não sabemos nomear (Lei 7: não inventar).
O fault é único por janela, e a análise anterior, uma vez descontado o resíduo
entre TDRs, volta a valer.

### C — O CLEAR não pega

**Assinatura:** `ClearIneficaz=1`.

**Implicação:** escrever o bit 0 não solta o latch nesta peça. O caminho passa
a ser `ALLOW_SUBSEQUENT_..._UPDATES` (bit 1), que tem o custo já declarado de
sobrescrever o primeiro fault — e exigiria critério próprio.

### D — A restauração falha

**Assinatura:** `Restaurado=0` ou `status=3` (`RESTORE_FAILED`), ou
`CntlReadback` diferente de `CntlAntes`.

**Implicação:** deixamos o `CNTL` alterado, e os 11 enables que o POST ligou
podem ter mudado. **É o desfecho mais grave desta faixa** e exige parar o
ensaio: qualquer fault medido depois disso é de uma configuração que não é a do
POST.

### E — Algo fora do mapa

**Instrução:** descrever o observado antes de julgá-lo. A observar:
`Teto=1` (a fila é maior que 8, e o teto é de tempo, não estimativa de fila);
`Drenados=0` com fault decodificado acima (contradição interna, defeito meu);
BugCheck; o `CntlAntes` divergir de `0x3FFFFFFC`.

---

## 4. Controles

| Controle | Corrida 55 |
|---|---|
| IBs executados | 1.370 |
| TDR / restarts provados | 6 / 6 |
| Faults latchados | 2 (4 × `SEM FAULT LATCHADO`) |
| BugCheck | **0x116** — a sequência de 10 corridas limpas acabou |
| Rollback | `RECOVERY_OK_ONLINE` |

**Risco desta faixa: médio.** É a primeira escrita em `0xA120`. O pulso preserva
os outros 31 bits e restaura o valor de entrada, mas isso é contrato verificado
offline, não medição no silício. **BugCheck é desfecho possível.**

**Tela preta continua correta.**

---

## 5. Procedimento

Fusível conferido antes deste documento: **CLEAN (0), Boot Count 0** — o reset
foi feito e o boot frio aconteceu.

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 6. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `9753E689A9BE4F1D...` |
| PDB | `53CB865959371FE9...` |
| UMD | `934DBD84E04F1002...` (inalterada) |
| INF | `812FC2A4596206F1...` |
| CAT | `C373D2D2A9F3DAE7...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI**:
`[TDR_GCVM_FAULT] DRENO: status=`, `[TDR_GCVM_FAULT] DRENO #`,
`nenhum fault na fila -- a ausencia esta DECLARADA`, `LATCH_CNTL=`,
`JANELA_DE_PUBLICACAO:`.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0** com `FAULT LATCH DRAIN: OK`, três auditorias
**PASS**, `git diff --check` limpo.

**Sete mutações verificadas, todas reprovando:** `0xA120` saindo da allowlist de
escrita; adaptador aceitando qualquer offset; pulso escrevendo constante (o que
perderia os 11 enables do POST); readback deixando de provar a restauração;
repetição voltando a contar como fault novo; fila vazia virando silenciosa;
faults drenados deixando de ser decodificados.

**Um portão meu foi atravessado e corrigido no mesmo dia.** O portão que
*proibia* escrita em `0xA120` procurava o literal `0x0000A120u`; quando a
allowlist passou a usar a constante simbólica, ele passou sem reprovar. Agora as
duas grafias contam, e o adaptador do miniport recusa qualquer offset diferente
do `FAULT_CNTL` **antes** da allowlist do transporte.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
