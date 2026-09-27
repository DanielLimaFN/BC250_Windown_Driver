# Critério da Corrida 53 — instalação lateral de raiz nos VMIDs do pool

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.214.1.0**,
> SYS `D2A551DC...`. Perfil `DISPLAY_RENDER_LAB` (RenderNode=1), WDDM 2.7,
> **P1 PERSISTENT SUBMISSION** — o mesmo da Corrida 52.
> **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que muda em relação à Corrida 52

A Corrida 52 foi **só observação**: o pool alocava VMIDs e ninguém tocava no
silício por causa disso.

**Esta corrida escreve registrador.** Para cada contexto GFX, o driver instala a
raiz de tabela de páginas **também** no VMID que o pool lhe deu — sete
registradores (`CNTL`, `PT_BASE`, `PT_START`, `PT_END`) em contextos GCVM
**1–6 e 10–15**, que nunca foram escritos nesta placa. Escrita, readback e flush
de TLB por VMID, com ACK.

**O pacote gráfico continua em VMID 7.** O `INDIRECT_BUFFER` não mudou. A
instalação é **lateral**: acontece em paralelo e não é usada por ninguém.

### Por que lateral, e não a troca de uma vez

Se a submissão passasse a usar o VMID do pool na mesma corrida, uma instalação
que falhasse produziria fault de tradução — **o mesmo sintoma do defeito atual**
— e a corrida não saberia dizer se o culpado foi a instalação nova ou o bug
velho. Assim, a instalação é medida isolada: ou ela funciona no silício, ou não,
e a submissão em VMID 7 continua produzindo a linha de base conhecida.

---

## 1. A pergunta única

**O silício aceita raiz de tabela de páginas nos VMIDs 1–6 e 10–15?**

Ninguém neste projeto jamais escreveu nesses contextos. O inventário da Corrida
50 os leu (`Enable=0`, raiz zero) e a Corrida 51 confirmou que os offsets são
distintos, não aliasing. Mas ler não é escrever.

---

## 2. Como ler o log

```
[VMID_POOL] INSTALACAO LATERAL: Ctx=... VMIDnn Root=0x... InstallStatus=u RecordStatus=u
                                Readback=u Flush=u Ready=u -- INDIRECT_BUFFER ainda em VMID7
[VMID_POOL] LATERAL PULADA: ...  -- a submissao SEGUE em VMID7
[VMID_POOL] LATERAL FALHOU: ...  -- entrada marcada FAILED e a submissao SEGUE em VMID7
```

Os três campos que decidem: **`Readback`** (a escrita ficou no registrador),
**`Flush`** (o ACK de invalidação de TLB voltou), **`Ready`**.

---

## 3. Desfechos

Assinatura e implicação em campos separados. **Desfecho parcial não é desfecho.**

### A — O silício aceita os VMIDs do pool

**Assinatura:** pelo menos uma `INSTALACAO LATERAL` com `InstallStatus=0`,
`RecordStatus=0`, `Readback=1`, `Flush=1` e `Ready=1`, em **pelo menos dois
VMIDs distintos**, e nenhuma `LATERAL FALHOU`.

**Implicação:** a última incógnita de hardware do alocador caiu. A próxima faixa
troca o `INDIRECT_BUFFER` para o `poolVmid` e **restaura a recusa** quando a
instalação falhar — o portão que hoje proíbe a recusa muda junto, de propósito.

### B — O readback não confere

**Assinatura:** `INSTALACAO LATERAL` com `Readback=0`.

**Implicação:** a escrita não persiste nesses contextos. Duas causas a separar
por leitura antes de gastar outra corrida: a allowlist do transporte recusou (e
aí o log do MMIO diz), ou o registrador ignora escrita com `Enable=0`. O
alocador não morre, mas a ordem de programação passa a ser o problema.

### C — O readback confere e o flush não

**Assinatura:** `Readback=1` com `Flush=0`.

**Implicação:** a raiz entra, mas a invalidação de TLB para esse VMID não
reconhece. Seria o pior desfecho para a faixa seguinte, porque a submissão
passaria a usar um VMID cuja TLB não se sabe invalidar — e o sintoma seria
indistinguível do fault atual. Nesse caso a troca do `INDIRECT_BUFFER`
**não deve** ser feita até o ACK ser entendido.

### D — A lateral nem acontece

**Assinatura:** `LATERAL PULADA` sem nenhuma `INSTALACAO LATERAL`.

**Implicação:** o `BeginInstall` recusou. A linha nomeia o status. Não é falha
de hardware — é estado do pool, e o defeito é nosso.

### E — Algo fora do mapa

**Assinatura:** qualquer coisa que não caiba em A–D.

**Instrução:** **descrever o observado antes de julgá-lo.** Pontos a observar
sem enquadrar às pressas:

- BugCheck novo. Escrever em contexto GCVM nunca tocado é a mudança mais
  invasiva desde o bring-up do anel gráfico. **É desfecho possível do ensaio.**
- O fault do VMID 7 mudar de assinatura, ou passar a nomear outro `Vmid=`.
- `Ocupados` no mapa do fault subir muito acima de 4 (pico da Corrida 52).
- A instalação lateral funcionar e o fault **desaparecer** — o que não deveria
  acontecer, já que nada usa o VMID novo; se acontecer, a explicação importa
  mais que o resultado.

---

## 4. Controles que não podem regredir

| Controle | Estado na Corrida 52 |
|---|---|
| BugCheck | zero em 8 corridas seguidas |
| TDR | 5, com 5 `TDR_RESTART PROVADO` |
| `GCVM_INVENTARIO_2` | `offsets DISTINTOS, inventario CONFIRMADO` |
| Submissões GFX | 461, anel consome |
| Rollback | `RECOVERY_OK_ONLINE` |

**Tela preta continua sendo o resultado correto.** Nada nesta faixa produz pixel.

---

## 5. Procedimento

Boot frio obrigatório. Fusível conferido e resetado se `TRIPPED`.

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 6. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `D2A551DC4F05E601...` |
| PDB | `1D4CBB0A1F528C01...` |
| UMD | `934DBD84E04F1002...` (inalterada) |
| INF | `E9651A39E6C032D3...` |
| CAT | `3677BC932F587C0A...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI** no SYS assinado:
`[VMID_POOL] INSTALACAO LATERAL`, `[VMID_POOL] LATERAL PULADA`,
`[VMID_POOL] LATERAL FALHOU`, `a submissao SEGUE em VMID`, `[VMID_POOL] RETIDO`,
`[VMID_POOL] MAPA NO FAULT`.

> Um pacote 3.214.1.0 anterior (SYS `F514C2F4...`) foi gerado e **descartado
> sem instalar**, antes da correção do escopo lateral. O artefato válido é o
> `D2A551DC...`.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, três auditorias **PASS**, `git diff --check`
limpo.

**Seis mutações verificadas nesta sessão**, todas reprovando: instalação lateral
voltando a VMID 7; flush lateral voltando a ser fixo no VMID 7; submissão
passando a usar o `poolVmid`; lateral derrubando a submissão; lateral devolvendo
a submission view; e um dos dois ramos de desistência ficando mudo.

**Dois portões fracos foram encontrados e corrigidos** — o do flush passava
porque nenhum portão exigia `FlushVmid`, e o das desistências passava porque
procurava a string em vez de **contar** os ramos.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
