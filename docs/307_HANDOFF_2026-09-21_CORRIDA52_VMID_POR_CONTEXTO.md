# Handoff — onde paramos em 21/09/2026, depois da Corrida 52

> **DESATUALIZADO (21/09, apos a Corrida 56).** O passo que este documento aponta
> como proximo -- ligar a submissao ao VMID do pool -- JA FOI FEITO nas Corridas
> 54 a 56, e **nao consertou o fault**: a assinatura segue o ENDERECO, nao o
> contexto. Use
> [docs/312](312_HANDOFF_2026-09-21_CORRIDA56_LATCH_DRENADO_E_O_ENDERECO.md).
> O que continua valendo aqui e' o historico e a lista de armadilhas.

> Para a próxima IA. Leia `CLAUDE.md` primeiro (as Leis 1–10 mandam em tudo
> abaixo). Este documento diz **em que ponto exato** o trabalho parou, o que já
> está provado, e qual é o próximo passo com os sítios de código nomeados.

---

## 1. A frase curta

A causa raiz do travamento do anel gráfico **está provada no silício**: vários
contextos do Windows disputam um único VMID (o 7), e o hardware só tem **uma**
raiz de tabela de páginas por VMID. Quando o IB do contexto A executa com a raiz
do contexto B instalada, os endereços de A não traduzem — e aparece o fault
`WalkerError=6 / MappingError=1` que persegue este projeto desde a Corrida 45.

**O conserto é dar um VMID para cada contexto.** Metade dele já está pronta e
validada; a outra metade é o próximo passo e está descrita na seção 5.

---

## 2. O que a Corrida 52 provou (P0, medido)

Candidato **lab 3.213.1.0**, SYS `831C79B7...`, pasta
`docs/evidence/2026-09-21_11-24-16-DESKTOP-HQAG6T4-lab-binding`.

Em cada um dos 4 faults o driver imprimiu o mapa de contextos vivos:

| Fault | Contextos vivos | Raiz instalada no VMID 7 naquele instante |
|---|---|---|
| 1 | 2 (raízes `0x45FDDA000`, `0x45FDE4000`) | `0x45FDE4000` |
| 2 | 3 (`0x45FDD5000` ×2, `0x45FDF6000`) | `0x45FDD5000` |
| 3 | 3 (`0x45FDD5000`, `0x45FDEB000`, `0x45FDD3000`) | `0x45FDEB000` |
| 4 | 3 (`0x45FDD5000`, `0x45FDEB000`, `0x45FDF6000`) | `0x45FDEB000` |

A raiz instalada é sempre a de **um** dos contextos vivos. Os outros estavam
submetendo com raízes diferentes. Isso é o mecanismo, não uma inferência.

Outros números da mesma corrida:

- **Pico de 4 contextos simultâneos** (55 amostras; distribuição `0:6 1:17 2:19 3:12 4:1`)
- 27 contextos ao longo da corrida, 24 devolvidos, **zero exaustões**
- Nenhum contexto recebeu dois VMIDs; nenhum VMID reservado foi entregue
- 5 TDR, 5 `TDR_RESTART PROVADO`, **zero BugCheck** (oitava corrida seguida),
  rollback `RECOVERY_OK_ONLINE`

**Correção de premissa registrada:** eu havia escrito que "a Corrida 49 mediu 6
raízes simultâneas". Era falso — as 6 raízes foram contadas **no tempo**, ao
longo de 82 submissões. A coexistência real é 4. A conclusão (12 slots sobram)
sobrevive; a premissa não. Já corrigido no header do contrato e na assertiva do
teste.

---

## 3. O que já está pronto e verificado (offline)

Arquivos **novos**, ainda não commitados:

| Arquivo | O que é |
|---|---|
| `driver/core/bc250_vmid_pool_contract.h` / `.c` | o pool: aritmética pura, sem hardware, sem lock |
| `driver/tests/vmid_pool_contract_test.c` | suíte offline |
| `docs/306_CRITERIO_CORRIDA52_...md` | critério escrito antes da Corrida 52 |

Reservados (medidos, não convenção): **0** = microengine MEC, **7** = usuário,
**8** = paginação, **9** = compute. Livres: 1–6 e 10–15, **doze slots**.

Ligação já feita em `driver/wdk/bc250_active_miniport.c`, em cinco pontos:
inicialização no bring-up, aquisição na submissão GFX, registro da raiz no mesmo
instante da instalação no silício, devolução no `DestroyContext`, e o mapa
impresso no fault.

Estado: `build-contract.bat` **EXIT=0**, três auditorias **PASS**, **nove
mutações verificadas** (todas reprovam).

---

## 4. O limite deliberado da faixa atual

**A submissão ainda vai em VMID 7.** O pool aloca e registra, mas ninguém usa o
VMID alocado.

Isso foi de propósito, e a razão importa: se a submissão passasse a usar o VMID
do pool na mesma faixa, um **mapa errado** produziria exatamente o mesmo sintoma
do defeito antigo — fault de tradução — e a corrida não saberia dizer se o
culpado era o conserto ou o bug. Primeiro a medição provou o mapa (feito, Corrida
52); agora a submissão pode passar a usá-lo.

Há um portão na auditoria que **reprova** se alguém mudar isso sem revisar o
escopo. Ele deve ser alterado junto com o código, não contornado.

---

## 5. O próximo passo, com os sítios nomeados

Trocar `BC250_GCVM_USER_VMID` pelo `poolVmid` em **dois** lugares de
`driver/wdk/bc250_active_miniport.c`:

1. **Linha ~11546** — a instalação da raiz:
   `Bc250GcvmInstallVmid(BC250_GCVM_USER_VMID, physicalRoot, &io, ...)`
2. **Linha ~11672** — a submissão ao anel:
   `Bc250ActiveGfxEngineSubmit(context, ..., BC250_GCVM_USER_VMID, &assignedFence)`

O `Bc250Pm4EmitIndirectBuffer` (linha ~1236) **já recebe o VMID por parâmetro** e
o codifica por pacote — não precisa mudar nada lá.

### Quatro armadilhas, todas reais, todas já localizadas

**(a) A allowlist de ESCRITA do MMIO só libera o VMID 7.**
`driver/wdk/bc250_mmio_topology_transport.c` chama `Bc250GcvmIsUserRegister()`,
que em `bc250_gcvm_vmid8_transaction.h:230` aceita **apenas** os sete
registradores do VMID 7. Escrever a raiz em VMID 1–6/10–15 vai ser **recusado
pelo transporte**. A allowlist de **leitura** já foi aberta para as faixas
`[0xA200,0xA240)` e `[0xA3AC,0xA42C)`; a de escrita **não**, deliberadamente.
Abri-la é decisão consciente e precisa de portão próprio.

**(b) O flush de TLB é fixo no VMID 7.** `Bc250ActiveFlushUserVmidOnly`
(linha ~14108) chama `Bc250ActiveFlushVmidTlb(Context, BC250_GCVM_USER_VMID)`.
Ele é passado como `io.Flush` na instalação. Com VMID por contexto ele precisa
invalidar **o VMID que está sendo instalado**, senão a TLB guarda a tradução
antiga e o fault continua — com a causa parecendo a mesma.

**(c) A escrituração `UserVmidInstalled*` é de um VMID só.** Os campos
`UserVmidInstalledContext`, `UserVmidInstalledRootPhysical` e
`UserVmidInstallStatus` em `bc250_active_miniport.h` assumem um único VMID vivo.
Com o pool, o par (contexto, raiz) já está em `VmidPool.Entries[]` — a decisão de
"preciso reinstalar?" deve sair de lá, não desses três campos.

**(d) A caminhada do fault usa a raiz do VMID 7.** Em `Bc250ActiveWalkGpuVa(...)`
o `walkRoot` vem de `activeCtx->UserVmidInstalledRootPhysical`. Com VMID por
contexto ela precisa caminhar a raiz **do VMID que o fault nomeia**
(`Vmid=` já sai decodificado no `[TDR_GCVM_FAULT] DECODIFICADO`).

### Como saber se funcionou

O fault `Vmid=7 WalkerError=6 MappingError=1` deve **parar**. Se ele reaparecer
com um `Vmid=` diferente, o mecanismo foi confirmado mas a instalação daquele
VMID falhou — e aí a armadilha (a) ou (b) é a primeira suspeita. Escreva o
critério **antes** da corrida (Lei 10) e inclua o desfecho `E — algo fora do mapa`.

---

## 6. Como rodar

**Compilar e verificar (offline, sem hardware):**

```
driver\wdk\build-contract.bat
python tools\audit-bc250mcdm-safety.py
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-ddi-noop-callbacks.py
```

**Empacotar** (suba o `DriverVer` em `driver/wdk/package/bc250mcdm.inf` antes —
o 3.213 já foi instalado, e dois artefatos com o mesmo `DriverVer` já causaram
confusão neste projeto):

```
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

> O empacotador **não compila** — ele copia. Pular o build de laboratório assina
> o binário do candidato anterior. Confira o perfil na auditoria: tem de sair
> `DISPLAY_RENDER_LAB (RenderNode=1)` e `P1 PERSISTENT SUBMISSION`. Se sair
> `DISPLAY_LAB` / `READ-ONLY`, faltaram os três switches.
>
> Confira as strings novas **em ANSI** dentro do SYS assinado, não em UTF-16 —
> as mensagens de log são `char*`.

**Vincular** (o Daniel executa; boot frio obrigatório antes):

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

> Rodar o binder **sem** `-Execute` imprime o comando completo com todas as
> flags derivadas. Use essa saída em vez de reconstruir a lista à mão — eu errei
> duas vezes montando-a de memória.

---

## 7. Estado da árvore

Nada desta sequência foi commitado. Há 25 arquivos modificados e 6 novos
(`git status`). O branch é `p6-rendernode-wddm`.

Os documentos `304`, `305` e `306` são os critérios das corridas 45, 46 e 52 e
explicam o raciocínio que levou até aqui. `BASE_DE_AVANCO.md` e
`PLANO_DESKTOP_ACELERADO.md` estão atualizados até a Corrida 51 — **a Corrida 52
ainda não foi escrita neles**, e essa é a primeira tarefa pendente.

---

## 8. Três hábitos que este projeto cobra

1. **Mutação em todo portão.** Um portão que não reprova quando o defeito é
   reintroduzido não é portão. Já aconteceu cinco vezes de um teste *exigir* a
   mentira que deveria impedir.
2. **Critério antes da corrida**, com assinatura e implicação em campos separados
   e um desfecho `E` para o inesperado (Lei 10). Corrida é o recurso escasso.
3. **Ausência de linha não prova ausência de evento** — o log é amostrado por
   classe. Antes de concluir "não aconteceu", confirme que a string está na lista
   de isenção do amostrador em `driver/wdk/bc250_display_ddi.c`.

**NADA DA FAIXA SEGUINTE FOI VALIDADO EM HARDWARE.**
