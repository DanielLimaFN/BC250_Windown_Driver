# Handoff — onde paramos em 21/09/2026, depois da Corrida 56

> Para a próxima IA. Leia `CLAUDE.md` primeiro — as Leis 1–10 mandam em tudo
> abaixo. Este documento substitui o [docs/307](307_HANDOFF_2026-09-21_CORRIDA52_VMID_POR_CONTEXTO.md),
> que está desatualizado: o passo que ele apontava como próximo já foi feito, e
> **não** consertou o defeito.

---

## 1. A frase curta

O alocador de VMID por contexto está **fechado no silício** e não consertou o
fault. A Corrida 56 mostrou por quê: **a assinatura do fault segue o ENDEREÇO,
não o contexto**. Três de quatro faults são no mesmo `0x200040`, mesmo cliente,
mesma leitura, variando só o VMID.

O alvo agora é esse endereço. E há um bloqueador de medição a remover antes:
a caminhada da tabela **não roda** quando o contexto que faltou já morreu.

---

## 2. O que está provado no silício (P0)

| Fato | Corrida |
|---|---|
| Vários contextos coexistem (pico de 5) | 52, 53 |
| Os 16 contextos GCVM têm offsets distintos, não aliasing | 51 |
| O silício **aceita** raiz nos VMIDs do pool — 32/32 `Ready=1` | 53 |
| O `INDIRECT_BUFFER` carrega o VMID do contexto | 54 (os faults passaram a nomear VMID 1/2/3, nunca 7) |
| O latch de fault estava **preso** (`AceitaAtualizacoes=0`) | 55 |
| O latch é **drenável e restaurável** (`CntlReadback == CntlAntes`) | 56 |

**Escopo honesto:** só os VMIDs 1–5 foram exercitados. Os 6 e 10–15 nunca
receberam escrita medida.

---

## 3. O achado da Corrida 56

Com o latch drenado a cada TDR, os quatro faults ficaram legíveis pela primeira
vez:

| Status | VMID | AddrPagina | Cid | Rw | PermFaults |
|---|---|---|---|---|---|
| `0x0014115D` | 1 | `0x2464E0` | 8 | 1 | 0x5 |
| `0x003013BC` | **3** | **`0x200040`** | **9** | **0** | **0xB** |
| `0x001013BD` | **1** | **`0x200040`** | **9** | **0** | **0xB** |
| `0x002013BD` | **2** | **`0x200040`** | **9** | **0** | **0xB** |

Os três últimos diferem **só no nibble do VMID**. Mesmo cliente (`Cid=9`), mesmo
endereço, mesma leitura.

Isso é o que derruba a leitura antiga. Se o defeito fosse disputa de contexto,
trocar o VMID teria resolvido — trocou, e o fault continua, agora seguindo o
endereço através de qualquer VMID que esteja em uso.

---

## 4. O que fazer, em ordem

### DX.10 — a caminhada tem de sobreviver à morte do contexto (offline)

**Bloqueador de medição, e é o primeiro.** Três das quatro caminhadas da Corrida
56 não aconteceram:

```
WALK <<12: raiz do VMID 7 AUSENTE (UserVmidInstalledRootPhysical=0)
```

Duas coisas erradas nessa linha:

1. **A mensagem mente.** Diz "VMID 7" e o fault era em VMID 1, 2 e 3. É a
   terceira ocorrência de telemetria descrevendo o estado anterior nesta
   investigação — corrija junto.
2. **A causa real:** `Bc250VmidPoolInstalledRootForVmid` devolveu 0 porque o
   contexto já tinha sido destruído quando o TDR rodou, e
   `Bc250VmidPoolRelease` zera o `InstallState`. O slot foi reciclado e a raiz
   sumiu.

**Conserto sugerido:** guardar, por VMID e fora da entrada do pool, a **última
raiz instalada** e o contexto a que pertencia, sobrevivendo à reciclagem. A
caminhada usa essa retenção quando a entrada viva não está `READY`, e o log
**declara qual das duas fontes usou** — sem isso, a caminhada volta a poder
mentir sobre qual tabela leu.

### DX.11 — o que é `0x200040` (offline)

`0x200040 << 12 = 0x200040000`. Resolver contra o espaço de GPUVA:
`Bc250GpuvaSpaceDescribeAddress` já devolve `BackingSegmentId`,
`BackingSegmentOffset` e distância aos vizinhos, e já é usado no discriminador
de granularidade. Faltava aplicá-lo a este endereço e **publicar o nome** da
alocação que o contém — ou declarar que não há nenhuma.

Cuidado com a granularidade: a Corrida 47 concluiu `<<20`, mas na 54 e na 56 o
`<<20` **excede 34 bits** e o `<<12` é que resolve. Essa conclusão da 47 precisa
ser relida, agora que se sabe que o endereço lido vinha de um latch preso.

### DX.5 — a tradução passa a funcionar (corrida)

Só depois das duas acima. **Ausência de fault numa corrida não prova conserto** —
pode ser ausência da condição; exija volume de `IB EXECUTADO` comparável
(1.611 na Corrida 56).

---

## 5. Armadilhas ativas, todas medidas

1. **`MoreFaults=1` não significa fila.** Com o latch drenado, nenhum TDR
   entregou um segundo fault. O bit significa outra coisa — **não invente nome**
   (Lei 7); `gc_10_1_0_sh_mask.h` não traz enumeração.
2. **`CRASH_ON_NO_RETRY_FAULT` (bit 30 do `FAULT_CNTL`) está em 1.** Medido no
   valor cru `0x3FFFFFFC`. Nunca foi investigado. Eu cheguei a rotular o bit 29
   como se fosse esse — errado, bit 29 é
   `OTHER_CLIENT_ID_NO_RETRY_FAULT_INTERRUPT`.
3. **`WALKER_ERROR` e `PERMISSION_FAULTS` não têm enumeração local.** Só shifts
   e máscaras. Os valores (6, 0x5, 0xB) permanecem **sem nome**.
4. **A PTE de `0x2464E0` é totalmente permissiva** (`flags=0x71` =
   `VALID|EXEC|READ|WRITE`) e ainda assim o acesso de escrita falhou. Mas veja o
   item 5 antes de usar isso.
5. **A caminhada é post-mortem.** A linha `JANELA_DE_PUBLICACAO` diz se a tabela
   mudou entre a submissão e o fault. Quando diz `TABELA MUDOU`, a PTE lida
   **pode ter nascido depois** e não prova nada sobre o instante do fault. Na
   Corrida 56 foi 1 `INALTERADA` e 3 `MUDOU`.
6. **A escrita em `0xA120` está aberta**, com escritor único
   (`Bc250FaultLatchDrain`) e guarda de sítio no adaptador. Não amplie sem
   critério próprio — há portão e mutação.

---

## 6. Como rodar

**Offline:**

```
driver\wdk\build-contract.bat
python tools\audit-bc250mcdm-safety.py
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-ddi-noop-callbacks.py
```

**Empacotar** (suba o `DriverVer` em `driver/wdk/package/bc250mcdm.inf` antes —
o 3.217 já foi instalado):

```
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-lab-bringup.ps1 -Execute -IUnderstandThisEnablesDestructiveBringup
powershell -ExecutionPolicy Bypass -File tools\package-bc250mcdm-lab.ps1 -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission
powershell -ExecutionPolicy Bypass -File tools\audit-bc250mcdm-lab-package.ps1
```

**Vincular** (o Daniel executa; boot frio obrigatório):

```
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

**O usuário autorizou corridas ilimitadas.** Ainda assim, minere antes: quase
todo avanço desta sessão veio de leitura de código e do catálogo, não de ensaio.

---

## 7. Armadilhas de ferramenta que custaram tempo

- **O empacotador não compila, só copia.** Pule o build de laboratório e você
  assina o binário anterior. Confira as strings novas **em ANSI** (as mensagens
  de log são `char*`); UTF-16 dá falso negativo.
- **O empacotador precisa dos três switches** (`-EnableRenderNode
  -EnableShaderDispatch -EnablePersistentSubmission`), senão sai `DISPLAY_LAB`
  READ-ONLY em vez de `DISPLAY_RENDER_LAB` P1.
- **`IOException` em `Copy-Item`** deixa o pacote inconsistente; repetir
  resolve. **Nunca aceite pacote sem auditoria PASS logo depois.**
- **O binder sem `-Execute` imprime o comando completo** com todas as flags
  derivadas. Use essa saída em vez de montar a lista à mão.
- **Heredoc do Bash corrompe backslashes.** Use a ferramenta Write para
  scripts Python; já custou várias tentativas.
- **`/O2` transforma laços de zeragem em `memset`**, que não existe no link
  offline das suites. Zere campo a campo.
- **`build-contract.bat` é CRLF.** Escrever uma linha em LF quebra o `cmd.exe`.
- **`bc250_mmio_topology_transport.c` tem finais de linha MISTOS** (540 CRLF,
  574 LF). Âncoras multi-linha falham ali; use âncora de uma linha.

---

## 8. Três hábitos que este projeto cobra

1. **Mutação em todo portão.** Nesta sessão **três portões meus passaram** e
   precisaram ser refeitos: um procurava string em vez de contar ramos, um
   errou o nome da função e avaliava fatia vazia, e um procurava o literal
   `0x0000A120u` enquanto o código usava a constante simbólica. Portão que não
   reprova não é portão.
2. **Critério antes da corrida** (Lei 10), com assinatura e implicação em campos
   separados e um desfecho `E` para o inesperado. Escreva o desfecho que você
   **não** quer primeiro — três hipóteses minhas morreram por eu só prever o
   desfecho desejado.
3. **Telemetria envelhece.** Quatro vezes nesta sessão uma linha de log
   continuou afirmando o estado anterior depois que o código mudou. Ao trocar
   comportamento, procure as mensagens que o descreviam.

---

## 9. Estado da árvore

Nada commitado. `git status` mostra ~28 modificados e 8 novos, no branch
`p6-rendernode-wddm`. Arquivos novos desta sequência:

```
driver/core/bc250_vmid_pool_contract.{c,h}
driver/core/bc250_fault_latch_contract.{c,h}
driver/tests/vmid_pool_contract_test.c
driver/tests/fault_latch_contract_test.c
docs/304 .. docs/312
```

`BASE_DE_AVANCO.md` e `PLANO_DESKTOP_ACELERADO.md` estão atualizados até a
Corrida 56. Os critérios das corridas estão em `docs/306`, `308`, `309`, `310`
e `311` — leia o da corrida mais recente antes de escrever o próximo.

**Último candidato instalado: 3.217.1.0** (SYS `9753E689...`). Fusível CLEAN.

**NADA DA PRÓXIMA FAIXA FOI VALIDADO EM HARDWARE.**
