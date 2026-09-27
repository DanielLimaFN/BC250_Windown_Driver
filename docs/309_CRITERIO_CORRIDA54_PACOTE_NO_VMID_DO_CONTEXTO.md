# Critério da Corrida 54 — o pacote gráfico passa a carregar o VMID do contexto

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.215.1.0**,
> SYS `718809CF...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> **P1 PERSISTENT SUBMISSION**. **NÃO VALIDADO EM HARDWARE.**

---

## 0. Esta é a corrida que o alocador inteiro existe para permitir

Até a Corrida 53 o `INDIRECT_BUFFER` ia em **VMID 7** e vários contextos
disputavam essa única raiz — 5 de 5 comparações divergentes na Corrida 49, até
**5 contextos vivos ao mesmo tempo** na Corrida 53. Com um VMID por contexto
**não há raiz para disputar**.

As quatro incógnitas caíram uma a uma, todas por medição:

| Pergunta | Fechada em |
|---|---|
| Vários contextos disputam mesmo o VMID 7? | Corrida 52 — `Ocupados` 2–3 por fault |
| Os 16 contextos GCVM são distintos, não aliasing? | Corrida 51 — `Mudaram=3 de 16` |
| A allowlist e o ciclo de vida aguentam? | offline, com mutação |
| **O silício aceita raiz nesses VMIDs?** | **Corrida 53 — 32/32, cinco VMIDs** |

---

## 1. O que mudou no código

1. **O pacote carrega o `poolVmid`.** `Bc250ActiveGfxEngineSubmit` recebe o VMID
   do contexto; `Bc250Pm4EmitIndirectBuffer` já o codificava por pacote.
2. **A recusa voltou a ser obrigatória.** Enquanto a instalação era lateral,
   falhar e seguir era o certo — o slot não era usado por ninguém. Agora partir
   com a tabela do próprio VMID ausente produziria exatamente o fault que a
   faixa elimina, com o log dizendo que tudo correu bem. **Os três ramos de
   tropeço recusam**, cada um nomeando o motivo.
3. **A caminhada do fault segue o VMID que o fault nomeia.** Caminhar um fault
   de VMID 3 com a raiz do VMID 7 leria a tabela errada, e "a PTE não existe"
   viraria conclusão falsa. A raiz vem da entrada do pool, e **só `READY`
   entrega raiz** — `INSTALLING` e `FAILED` devolvem zero, e a ausência é
   declarada.
4. **Telemetria vencida corrigida.** A linha de sucesso dizia "INDIRECT_BUFFER
   ainda em VMID7", o que deixou de ser verdade. Agora nomeia o VMID que o
   pacote realmente carrega, e um portão proíbe a frase antiga de voltar.

### Ruído esperado no log

O bloco de instalação no VMID 7 legado **continua rodando** — de propósito, para
mudar uma variável só. As linhas `VMID7 CONTEXT SWITCH` vão continuar
aparecendo. **Elas agora são ruído**, não o caminho do pacote. Removê-las é
trabalho da faixa seguinte.

---

## 2. A pergunta única

**O fault de tradução para?**

---

## 3. Desfechos

### A — O conserto funcionou

**Assinatura:** zero `[TDR_GCVM_FAULT] DECODIFICADO` com `WalkerError`, **e**
submissões GFX com `IB EXECUTADO` em número comparável às corridas anteriores
(2.294 na Corrida 53), **e** `[VMID_POOL] INSTALACAO` com `Ready=1` em ao menos
dois VMIDs distintos.

**Implicação:** a fase DX fecha. O próximo bloqueador passa a ser **D0.7** — a
perna 3 byte a byte —, e o caminho para o primeiro pixel abre.

**Cuidado que manda no veredito:** ausência de fault **não é, sozinha, prova de
conserto** — pode ser ausência da condição. Por isso a assinatura exige
submissões em número comparável: poucas submissões com zero fault é o desfecho
D, não o A.

### B — O fault continua, no VMID novo

**Assinatura:** `DECODIFICADO` com `Vmid=` diferente de 7, e
`ORIGEM_DA_RAIZ` dizendo `entrada READY do pool`.

**Implicação:** a disputa de raiz **não era a causa suficiente**. A caminhada
agora lê a tabela certa, então a linha `WALK` passa a responder de verdade se a
PTE existe naquele VMID. O suspeito volta para publicação/TLB, e a Corrida 49
passa a ser lida como correlação, não causa.

### C — O fault continua, ainda no VMID 7

**Assinatura:** `DECODIFICADO` com `Vmid=7`.

**Implicação:** algo ainda submete em VMID 7. Candidato imediato: o bloco legado
de instalação, ou um caminho de submissão que não passa pelo pool. É defeito
nosso, localizável por leitura, **sem gastar corrida**.

### D — As submissões param

**Assinatura:** `[VMID_POOL] RECUSA:` em volume, com `IB EXECUTADO` despencando.

**Implicação:** a instalação que funcionou 32/32 na Corrida 53 deixou de
funcionar quando virou pré-requisito. A linha de recusa nomeia qual dos três
ramos disparou. **Este é o custo declarado de ter restaurado o fail-closed** — e
é preferível a submeter com tabela ausente.

### E — Algo fora do mapa

**Assinatura:** qualquer coisa que não caiba em A–D.

**Instrução:** descrever o observado antes de julgá-lo. A observar sem enquadrar
às pressas:

- BugCheck. Nove corridas seguidas sem um; esta muda o VMID de **todo** pacote
  gráfico, que é a mudança mais invasiva desde o bring-up do anel.
- Pixel aparecer. Não deveria — a parede de rasterização continua inteira —, e
  se aparecer a explicação importa mais que o resultado.
- `Exaustoes > 0`. Nunca aconteceu; agora a exaustão **recusa** a submissão.
- O fault mudar de `WalkerError=6` para outro código.

---

## 4. Controles

| Controle | Corrida 53 |
|---|---|
| BugCheck | zero em 9 corridas |
| IBs executados | 2.294 |
| Instalações OK | 32 de 32, cinco VMIDs |
| Exaustões | 0 |
| Rollback | `RECOVERY_OK_ONLINE` |

**Tela preta continua correta.** Nenhum pixel vem desta faixa.

---

## 5. Procedimento

Boot frio obrigatório; fusível conferido.

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 6. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `718809CF28FBCAAF...` |
| PDB | `F6EFD8ADBEC6B2C0...` |
| UMD | `934DBD84E04F1002...` (inalterada) |
| INF | `F3B01A64530ADDA9...` |
| CAT | `AFCF8B730E714956...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI** no SYS assinado:
`[TDR_GCVM_FAULT] ORIGEM_DA_RAIZ`, `[VMID_POOL] INSTALACAO: Ctx=`,
`o INDIRECT_BUFFER desta submissao carrega VMID`,
`[VMID_POOL] RECUSA: instalacao do VMID`. Conferido também que
`INDIRECT_BUFFER ainda em VMID` e `a submissao SEGUE em VMID` **saíram** do
binário — telemetria da faixa anterior não pode sobreviver.

> A primeira passada do empacotador falhou com `IOException` em `Copy-Item` e a
> auditoria reprovou com cinco hashes divergentes. Repetir resolveu — armadilha
> já registrada. **Nunca aceitar pacote sem auditoria PASS logo depois.**

---

## 7. Estado offline

`build-contract.bat` **EXIT=0**, três auditorias **PASS**, `git diff --check`
limpo.

**Sete mutações verificadas, todas reprovando:** pacote voltando ao VMID 7;
falha de instalação deixando de recusar; `BUSY` seguindo em vez de adiar;
caminhada voltando à raiz fixa do VMID 7; leitor entregando raiz de slot
não-`READY`; origem da raiz deixando de ser declarada; telemetria voltando a
afirmar o estado anterior.

**Oito portões foram invertidos de propósito** nesta faixa — os que exigiam
`BC250_GCVM_USER_VMID` no pacote e proibiam a recusa agora exigem o oposto. Isso
estava previsto por escrito desde a faixa que os criou.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
