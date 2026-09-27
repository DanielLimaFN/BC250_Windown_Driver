# Critério da Corrida 60 — quem escreve o PDE da arena

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.221.1.0**,
> SYS `783E3CD392DD4F97...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. De onde vem a pergunta

A Corrida 59 fechou a cobertura do flush de TLB — `PoolReadyMask` até `0x001E`,
ACK em todos os VMIDs, zero falhas, `Polls=1` — e **a taxa de fault não mudou**
(3 em 7 TDRs, contra 3 em 6 e 4 em 6). A hipótese da TLB velha caiu como causa
suficiente.

O que a mesma corrida entregou foi um alvo novo, e ele é do **nível de
diretório**, não da folha:

```
L1 idx=4096 PDE=0000000450C9C071 -> leafPA=0x450C9C040 flags=0x1 reservado[5:3]=0x30
FALHA L1: PDE com bits reservados [5:3] ligados
```

`0x…071` é formato de **PTE** (`VALID|EXE|READ|WRITE`) num slot de **PDE**. Pelo
layout do Navi 10 (P1, `gmc_v10_0.c`, já no `bc250_gcvm_contract.h`), o endereço
do PDE começa no **bit 6** — então o bit 6 é endereço e os bits [5:3] são
reservados. A GPU caminharia 64 bytes adiante. É a assinatura do defeito de
05/09 (`0x61`), que ficou invisível por seis corridas.

Três coisas sustentam que a leitura é boa: o índice 4096 é exatamente o da arena
`0x200040000` que falha; o índice **4632 da mesma raiz** tinha PDE limpo e
resolveu até a folha, logo a caminhada leu a tabela certa; e o portão que
disparou **já existia** e nunca havia disparado em corrida nenhuma.

---

## 1. A pergunta única

**Fomos nós que escrevemos aquele valor?**

`Bc250GcvmEncodePde` **não consegue** produzi-lo — recusa qualquer flag fora de
`VALID/SYSTEM/SNOOPED`. Então ou existe um caminho nosso que eu não conheço, ou
quem escreve não somos nós. O log não sabia dizer qual.

---

## 2. O que mudou, e o que é correção de verdade

**Dois defeitos reais, independentes da hipótese:**

1. **O status do encode era descartado com `(void)`.** Quando
   `Bc250McdmEncodeVidMmPteWithLevel` recusa, deixa `*Encoded` em **zero** — e o
   laço gravava esse zero numa page table **viva**, sem uma linha de log. Lei 4
   no caminho mais caro do driver. Agora a recusa **não grava** e se declara.
2. **`LargePage` nunca era olhado no nível de diretório.** O nível 0 já o
   recusava; o nível > 0 sequer lia o campo.

**Um discriminante:** `[PDE_WRITE] ARENA` publica, sempre que uma atualização de
diretório cobre o índice 4096, a `DXGK_PTE` crua e o valor codificado.

**Um tripwire:** entrada de nível ≥ 1 com bits [5:3] no valor codificado é
recusada e nomeada. Por construção impossível — existe para dizer que estou
errado sobre a construção.

---

## 3. Por que `LargePage` é medido e não recusado

Recusar seria o lado seguro **se soubéssemos a frequência**. Não sabemos:
nenhuma corrida registrou `LargePage` no nível de diretório, porque o campo
nunca foi lido ali. Se o VidMm o usar de rotina, recusar faria a
`UpdatePageTable` falhar em série e a corrida **não mediria nada** — que é o
preço já pago na Corrida 53, quando a instalação lateral recusava a submissão
antes de existir medição.

A linha sai, o caso fica contado, a recusa vem depois do número. Isso **não é
sucesso silencioso**: o log nomeia a aproximação e diz que ela não está provada.

---

## 4. Desfechos

> Assinatura e implicação em campos separados (Lei 10). Desfecho parcial não é
> desfecho.

### A — Fomos nós

**Assinatura:** `[PDE_TRIPWIRE] FALHA:` aparece, **ou** `[PDE_WRITE] ARENA` sai
com `Reservado[5:3]` diferente de zero.

**Implicação:** o valor de PTE no slot de PDE sai de um caminho nosso, e o
`Lvl`/`Start`/`Num` da linha dizem qual. Vira conserto de codificação, não mais
investigação. **Não** declara o fault resolvido.

### B — Não fomos nós

**Assinatura:** `[PDE_WRITE] ARENA` aparece pelo menos uma vez com
`Reservado[5:3]=0x0` e `Status=0x00000000`, zero `[PDE_TRIPWIRE]`, **e** o TDR
volta a ler `reservado[5:3]` não-zero no índice 4096.

**Implicação:** escrevemos limpo e a tabela é encontrada suja depois. O suspeito
passa a ser **quem mais toca essa página** — sobreposição de alocação, ou a
raiz do VMID apontando para memória reutilizada. Discriminar exigiria carimbar a
página antes e depois.

### C — O índice nunca é escrito por nós

**Assinatura:** zero `[PDE_WRITE] ARENA` na corrida inteira, com atualizações de
nível ≥ 1 acontecendo (a ausência é conclusiva: a linha é isenta da amostragem).

**Implicação:** a entrada de raiz da arena não passa pelo nosso handler. Isso
sozinho já explicaria um PDE que não reconhecemos, e muda o alvo para **como
essa raiz é montada**, não para como a escrevemos.

### D — O encode estava recusando calado

**Assinatura:** `[ActiveUpdatePT] RECUSA:` aparece.

**Implicação:** o defeito (1) era **alcançável**, e entradas viravam zero numa
tabela viva sem ninguém saber. Isso explicaria os `PDE=0` em L1 que a Corrida 59
viu nas outras duas caminhadas — embora `PDE=0` também seja o resultado legítimo
de um unmap (`Valid=0` codifica zero por contrato), então a linha é o que separa
os dois casos.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar: `[PDE_LARGEPAGE]` aparecendo (e com que frequência — o contador diz,
mesmo com o teto); o fault mudando de endereço ou de `Cid`; `[PDE_WRITE]` saindo
com `Status` diferente de zero; BugCheck; ou a taxa de fault mudando sem que
nenhuma linha nova explique por quê.

---

## 5. Controles

| Controle | Corrida 59 |
|---|---:|
| `IB EXECUTADO (INDIRECT)` | 1.969 |
| TDR / restarts provados | 7 / 7 |
| Faults drenados / sem latch | 3 / 4 |
| `PoolReadyMask` máximo | `0x001E` |
| GFX máximo | `Submetidas=99 Aposentadas=99 EmVoo=0` |
| BugCheck | nenhum |
| Rollback | `RECOVERY_OK_ONLINE` |

**Esta faixa é diagnóstico.** Não espere o fault sumir, não espere pixel. A
parede de rasterização não foi tocada e **tela preta continua sendo o resultado
correto**; qualquer imagem deve ser atribuída ao rollback/BasicDisplay até prova
em contrário.

---

## 6. Procedimento

Conferir o fusível e fazer **boot frio** antes do bind.

```bash
powershell -ExecutionPolicy Bypass -File tools\bind-bc250mcdm-lab.ps1 -Execute -IAcceptBlackScreenRisk -EnableRenderNode -EnableShaderDispatch -EnablePersistentSubmission -EnableGpuvaLabPromotion -IAcceptGpuvaLabPromotion -EnableNode1JournalGatePerProcess -EnableD3D11LabPromotion -EnableD3D11ShaderTranslator -EnableDwmCrashCapture -RunD3D11Native -WatchdogTimeoutSeconds 120 -IAcceptAutomaticRecoveryReboot
```

---

## 7. Proveniência

| Artefato | SHA-256 (início) |
|---|---|
| SYS assinado | `783E3CD392DD4F97...` |
| PDB | `83429C5866E95D91...` |
| UMD | `B4D07A72C1B70BAA...` |
| INF | `E4C6C97AB51F3799...` |
| CAT | `1833BA4CD3CA59C7...` |

`Lab package audit: PASS`. Seis strings conferidas **em ANSI** no SYS assinado,
incluindo `[PDE_WRITE] ARENA idx=`, `[PDE_TRIPWIRE] FALHA:`,
`[PDE_LARGEPAGE] Lvl=` e `[ActiveUpdatePT] RECUSA:`.

---

## 8. Estado offline

`build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias
**PASS**, `git diff --check` limpo.

**Oito mutações verificadas, todas reprovando:** o status do encode voltando a
ser descartado; o tripwire gravando assim mesmo; o índice da arena virando
número solto; a máscara de reservados divergindo do decodificador; `PDE_WRITE`
sumindo; `LargePage` voltando a ser invisível; `PDE_LARGEPAGE` perdendo o teto;
e `PDE_TRIPWIRE` perdendo a isenção do amostrador.

**Um portão meu passou, e a mutação o pegou.** Ele exigia a string
`InterlockedIncrement(` no arquivo — que já tem **sete** ocorrências em outros
caminhos, então tirar o teto desta linha não reprovava nada. Refeito com
invariante sobre o contador próprio, comparado sem espaços.

**Duas armadilhas de ferramenta nesta faixa.** Um `sed -i` converteu
`bc250_mcdm_callbacks.c` inteiro de CRLF para LF (restaurado), e
`bc250_display_ddi.c` ficou com finais de linha **mistos** — a mesma armadilha
já registrada para `bc250_mmio_topology_transport.c`. Foi o que fez uma âncora
de mutação não casar. Normalizado.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
