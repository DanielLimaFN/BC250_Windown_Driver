# Critério da Corrida 59 — a invalidação segue os VMIDs que a GPU usa

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.220.1.0**,
> SYS `890AA873A556225F...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que muda, e por que

Achado **por leitura de código**, depois da Corrida 58:
`Bc250ActiveFlushMappedGfxhubTlb` invalidava **três VMIDs fixos** — 8
(paginação), 7 (usuário legado) e 9 (compute). Isso estava certo enquanto o anel
gráfico submetia em VMID 7. **Deixou de estar na Corrida 54**, quando o
`PACKET3_INDIRECT_BUFFER` passou a carregar o VMID dinâmico do pool; na Corrida
58 foram os VMIDs 1 a 4.

`Bc250GcvmInstallVmid` invalida o slot **na instalação**. Uma
`UpdatePageTable`/`FlushTlb` posterior não invalidava nenhum slot dinâmico já
instalado.

Isso **casa** com o que a Corrida 58 mediu — escrita em `0x2469A6000` falhando
com a janela de publicação `INALTERADA` e a PTE contemporânea `0x458F72071`
válida e R/W/X na visão da CPU. **Casar não é provar.** A relação causal é
**HIPÓTESE** e é exatamente o que esta corrida decide.

A faixa corrige a **cobertura**, não a conclusão: a máscara agora soma os slots
do pool que estão `READY` com raiz instalada.

---

## 1. A pergunta única

**A GPU estava traduzindo com TLB velha nos VMIDs do pool?**

---

## 2. Linhas novas

```
[FLUSH_POOL_VMIDS] #n FixedMask=0x0380 PoolReadyMask=0x.... RequestedMask=0x....
                      RejeitadaMask=0x.... FalhouMask=0x.... Ok=u
[FLUSH_POOL_VMIDS] FALHA: mascara 0x.... nao invalidou; o flush PARCIAL nao e contado como sucesso
[FlushMappedGfxhubTlb] VMIDnn status=u, PhysReady=u, AckObserved=u, Polls=u
```

A linha da máscara tem **teto próprio** (muda de máscara, 8 primeiras, 1 em 512,
mais toda falha) e está **isenta da amostragem global** — armadilha 16, já paga
uma vez pelo `PUBLICACAO_COMPOSICAO`.

A linha por VMID **passou a dizer qual VMID**. Até esta faixa ela relatava o
resultado da invalidação sem dizer de quem, o que com três VMIDs fixos era só
inconveniente e com máscara dinâmica seria inútil.

---

## 3. Desfechos

> Assinatura e implicação são campos separados (Lei 10). Desfecho parcial **não
> é desfecho**: se um desfecho exige três sinais e aparecem dois, ele não
> ocorreu.

### A — Hipótese confirmada funcionalmente

**Assinatura:** (1) pelo menos um `PoolReadyMask` diferente de zero; (2) cada
VMID da máscara com `AckObserved=1` numa linha `[FlushMappedGfxhubTlb] VMIDnn`;
(3) o fault de escrita em `0x2469A6000` **desaparece** sob carga comparável —
`IB EXECUTADO (INDIRECT)` da ordem de 1.900, como na Corrida 58.

**Implicação:** a TLB velha nos VMIDs do pool era a causa do fault restante.
DX.5 avança. **Pixel continua exigindo prova própria** — ausência de fault não
produz imagem, e a parede de rasterização segue inteira.

### B — Flush exercitado, mesmo fault

**Assinatura:** `PoolReadyMask` não zero, ACK observado em todos os VMIDs da
máscara, **e** o fault `Cid=8 Rw=1` reaparece — em `0x2469A6000` ou em outra
GPUVA da mesma faixa `0x246400000`.

**Implicação:** a hipótese cai **como causa suficiente**. A cobertura do flush
deixa de ser suspeita e o próximo alvo passa a ser a hierarquia (o PDE, não a
folha) ou o consumidor `Cid=8` — quem escreve, e o quê.

### C — O flush dinâmico não foi exercitado

**Assinatura:** `[FLUSH_POOL_VMIDS]` aparece com `PoolReadyMask=0x0000` em todas
as ocorrências.

**Implicação:** **INCONCLUSIVO.** Nenhum slot estava `READY` no instante do
flush, e a correção não foi posta à prova. Proibido atribuir à correção
qualquer ausência de fault neste desfecho. Investigar por que o pool estava
vazio — a Corrida 58 teve 26 instalações, então máscara vazia o tempo todo é ela
própria um achado.

### D — O flush falhou

**Assinatura:** `FalhouMask` diferente de zero, ou `[FLUSH_POOL_VMIDS] FALHA:`.

**Implicação:** falha da implementação ou do caminho físico. `Ok=0` fecha
`GpuvaMappingsOperational`, o que deve produzir recusa de submissão em vez de
avanço — e isso é o comportamento **correto**. Não ler nada sobre a hipótese
neste desfecho: o ensaio não aconteceu.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar, sem que nenhum destes seja esperado: `RejeitadaMask` diferente de
zero (o bit 0 chegou a ser pedido, e o pool deveria tê-lo excluído);
`PoolReadyMask` com bit reservado; BugCheck `0x133` (o flush passou a gastar
mais polls por chamada — a máscara pode ter até 15 VMIDs, contra 3);
`Polls` subindo para a ordem do teto de 1000; fault numa GPUVA **fora** da faixa
`0x246400000`; ou o fault mudar de `Rw=1` para `Rw=0`.

**O risco novo desta faixa é tempo em `DISPATCH_LEVEL`**, e ele é declarado: o
flush passou de 3 para até 3 + N VMIDs por chamada. O lock do pool é solto antes
de qualquer MMIO, mas o custo total por `FlushTlb` cresceu.

---

## 4. Controles

| Controle | Corrida 58 |
|---|---|
| `IB EXECUTADO (INDIRECT)` | 1.900 |
| TDR / restarts provados | 6 / 6 |
| Instalações do pool / exaustões | 26 / 0 |
| GFX máximo | `Submetidas=83 Aposentadas=81 EmVoo=2` |
| Fences pela GPU / perna 3 aberta | 107 / 107 |
| BugCheck | nenhum observado |
| Rollback | `RECOVERY_OK_ONLINE` |

**Carga comparável é requisito do desfecho A.** Ausência de fault com 200 IBs
não refuta nada — pode ser ausência da condição.

**Tela preta continua sendo o resultado correto.** O `Draw` real segue sem
backend e a parede de rasterização não foi tocada. Qualquer imagem deve ser
atribuída ao rollback/BasicDisplay até prova em contrário.

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
| SYS assinado | `890AA873A556225F...` |
| PDB | `015AE6FA78BB01E3...` |
| UMD | `3D4322E8B2544919...` |
| INF | `B7AC565A58E75190...` |
| CAT | `B8EE192B5884AB02...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI** no SYS **assinado**:
`[FLUSH_POOL_VMIDS] #%u FixedMask=`, `RejeitadaMask=`,
`[FLUSH_POOL_VMIDS] FALHA:` e `[FlushMappedGfxhubTlb] VMID%02u status=`.
A string antiga sem VMID (`[FlushMappedGfxhubTlb] Transaction status=`) foi
conferida **ausente** — é a prova de que o empacotador não copiou o candidato
anterior.

---

## 7. Estado offline

`build-contract.bat` **EXIT=0** com `VMID POOL: OK`, `build-umd.bat` **EXIT=0**,
três auditorias **PASS**, `git diff --check` limpo (só avisos de normalização
LF/CRLF, pré-existentes).

**Onze mutações verificadas, todas reprovando.** Nove contra a auditoria: o
flush voltando a cobrir só 7/8/9; o flush por VMID acontecendo antes de soltar o
`VmidPoolLock`; flush parcial virando sucesso; o bit 0 deixando de ser recusado;
a máscara sumindo do log; `INSTALLING`/`FAILED` entrando na máscara; os
reservados deixando de ser cruzados fora; a linha perdendo a isenção do
amostrador; e a suíte deixando de exigir dois slots juntos. Duas contra a
**suíte**: `INSTALLING`/`FAILED` na máscara e contexto morto na máscara.

**Duas guardas minhas eram carimbo e a mutação mostrou isso.** Pelo caminho
vivo, `RecordInstall` zera `InstalledRootPhysical` em toda saída que não seja
`READY` — então a guarda de estado e a de raiz se cobriam e **nenhuma das duas
conseguia falhar sozinha**. A suíte passava com qualquer uma removida. Os casos
foram refeitos forçando estados que a API não produz, e cada guarda passou a ser
load-bearing.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
