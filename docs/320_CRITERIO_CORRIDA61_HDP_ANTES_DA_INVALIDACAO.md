# Critério da Corrida 61 — drenar o HDP antes de invalidar a TLB

> **Escrito ANTES do ensaio** (Lei 9). Candidato **lab 3.222.1.0**,
> SYS `E12F2375BA9EA172...`. Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7,
> P1 PERSISTENT SUBMISSION, schema 135. **NÃO VALIDADO EM HARDWARE.**

---

## 0. O que a Corrida 60 fechou, e o que ela abriu

A Corrida 60 respondeu a pergunta que eu tinha feito e **eliminou o suspeito que
eu havia anunciado**:

- **Nós escrevemos o índice 4096** — 101 linhas `[PDE_WRITE] ARENA`, das quais
  23 com `V=1`. **Todas limpas**: `Encoded=0x…001`, `Reservado[5:3]=0x0`.
- **Zero** `[PDE_TRIPWIRE]`, **zero** `[PDE_LARGEPAGE]`, **zero**
  `[ActiveUpdatePT] RECUSA`.
- O PDE sujo `0x…071` da Corrida 59 **não reproduziu**: o TDR leu `0x45FDFE001`,
  limpo, no mesmo índice 4096.

E entregou a medição mais forte da sequência: **duas caminhadas
contemporâneas** (`JANELA_DE_PUBLICACAO: TABELA INALTERADA` — a PTE lida já
valia no instante do fault) que **RESOLVERAM até a folha**:

```
L1 idx=4096 PDE=0x45FDFE001 limpo -> L0 idx=64 PTE=0x4520CC031 -> RESOLVIDO 0x200040000 -> 0x4520CC000
```

`0x31` = `VALID|EXEC|READ`. O fault é `Rw=0` — **leitura de página legível**.

Ou seja, no instante do fault a imagem vista pela CPU estava **completa,
correta e com permissão**, e a GPU mesmo assim reportou `WALKER_ERROR=6` e
`MAPPING_ERROR=1` no mesmo endereço.

Isso refuta, agora **contemporaneamente**, as três explicações que sobravam:
PTE ausente, PDE corrompido e permissão.

---

## 1. A pergunta única

**A GPU está lendo a tabela que a CPU escreveu?**

---

## 2. A hipótese, e por que ela é forte sem ser prova

As page tables são escritas pela CPU em VRAM através de
`PageTableAddress.CpuVirtual`. Essas escritas ficam na **cache de escrita do
HDP**. Nada no caminho de paginação drenava o HDP antes de o walker buscar.

**Este projeto já mediu exatamente este mecanismo neste silício.** Corrida
10/09 12:16:

```
FENCE_VISIBILIDADE antes_hdp=43DAFFFE depois_hdp=00001304 esperado=00001304
```

O valor **apareceu no flush**. Era outra consumidora — uma fence —, mas a mesma
memória, a mesma cache e o mesmo sentido: CPU escreve, GPU lê.

Casa também com a intermitência: 3 faults em 1.758 IBs é o perfil de uma corrida
de visibilidade, não de uma tabela estruturalmente errada.

**Continua H, não P0.** Se o fault sobreviver ao flush, a hipótese cai.

---

## 3. O que mudou

**A ordem é o conteúdo da faixa.** `HDP_MEM_COHERENCY_FLUSH_CNTL` é escrito com
readback **antes** do laço de invalidação por VMID. Invalidar a TLB antes de
drenar o HDP mandaria o walker **rebuscar** bytes que ainda não chegaram — isso
pioraria a janela em vez de fechá-la. O portão trava a ordem, não a presença.

O offset `0x3FC4` **não é novo**: já está catalogado em
`bc250_gfx_ring_transaction.h`, já é usado por quatro caminhos deste driver e já
está nas allowlists de leitura **e** escrita. Nenhuma allowlist foi ampliada.

`[FLUSH_POOL_VMIDS]` passou a publicar `HdpFlush=`, e a falha de drenagem se
declara.

---

## 4. Desfechos

> Assinatura e implicação em campos separados (Lei 10). Desfecho parcial não é
> desfecho.

### A — Hipótese confirmada

**Assinatura:** (1) `HdpFlush=1` nas linhas da máscara e zero `[HDP_PT_FLUSH]
FALHA:`; (2) **zero faults de tradução no GFX** — nenhum `DRENO #`, de qualquer
`Cid` e qualquer endereço; (3) carga comparável, `IB EXECUTADO (INDIRECT)` da
ordem de 1.700–2.000.

**Implicação:** a GPU lia tabela velha. DX.5 avança. **Pixel continua exigindo
prova própria** — a parede de rasterização não foi tocada e nenhuma imagem é
esperada.

> Escrevi (2) como **qualquer** fault, deliberadamente. Na Corrida 59 amarrei a
> assinatura a um endereço específico, ele sumiu, outro apareceu e a implicação
> não se sustentou. A assinatura tem de ser a pergunta inteira.

### B — Flush exercitado, fault permanece

**Assinatura:** `HdpFlush=1`, zero falhas de drenagem, **e** pelo menos um
`DRENO #` com `WalkerError=6`.

**Implicação:** a visibilidade da tabela **não** era a causa. O suspeito passa a
ser **o que a GPU usa como raiz**, não o que a tabela contém: o `PT_BASE` do
VMID no instante da submissão, ou o `CNTL` do contexto. A caminhada já prova que
o conteúdo está certo; falta provar que a GPU está lendo a partir do endereço
certo.

### C — O flush não aconteceu

**Assinatura:** `HdpFlush=0`, ou `[HDP_PT_FLUSH] FALHA:` presente.

**Implicação:** a escrita MMIO não passou. **Inconclusivo** quanto à hipótese —
proibido ler qualquer coisa sobre visibilidade neste desfecho. A linha diz se
foi o write ou o read que recusou.

### D — O fault muda de forma

**Assinatura:** os faults continuam, mas **sem** `WalkerError=6` — por exemplo
só permissão, ou `MappingError=0`.

**Implicação:** o flush mudou o que a GPU enxerga sem eliminar o problema.
Isso seria **evidência parcial a favor** da hipótese e mudaria o alvo para a
classe nova, que a linha `DRENO #` nomeia por campo.

### E — Algo fora do mapa

**Instrução: descrever o observado antes de julgá-lo.**

A observar: a taxa de TDR subindo (o flush por `FlushTlb` custa uma escrita e
uma leitura MMIO a mais, e `FlushTlb` é frequente); BugCheck `0x133`;
`[PDE_TRIPWIRE]` disparando agora que não disparou antes; faults num endereço
fora de `0x200040000`/`0x245B000`; ou `PoolReadyMask` ficando em zero a corrida
inteira, o que tornaria a comparação com a 60 inválida.

---

## 5. Controles

| Controle | Corrida 60 |
|---|---:|
| `IB EXECUTADO (INDIRECT)` | 1.758 |
| TDR / restarts provados | 7 / 7 |
| Faults drenados / sem latch | 3 / 4 |
| `[PDE_WRITE] ARENA` | 101 (23 com `V=1`, todas limpas) |
| Caminhadas contemporâneas que resolveram | 2 |
| `PoolReadyMask` máximo | `0x000E` |
| BugCheck | nenhum |
| Rollback | `RECOVERY_OK_ONLINE` |

**Esta faixa não acende a tela.** Tela preta continua sendo o resultado correto;
qualquer imagem deve ser atribuída ao rollback/BasicDisplay até prova em
contrário.

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
| SYS assinado | `E12F2375BA9EA172...` |
| PDB | `B0CB5D0D5E9FBC64...` |
| UMD | `401C3FBBBC958411...` |
| INF | `ABCC183C6B0CBF35...` |
| CAT | `B3B11630E97889E4...` |

`Lab package audit: PASS`. Strings conferidas **em ANSI** no SYS assinado:
`[HDP_PT_FLUSH] FALHA:` e `HdpFlush=%u Ok=%u` presentes; a linha antiga sem
`HdpFlush` conferida **ausente** — prova de que o empacotador não copiou o
candidato anterior.

---

## 8. Estado offline

`build-contract.bat` **EXIT=0**, `build-umd.bat` **EXIT=0**, três auditorias
**PASS**.

**Seis mutações verificadas, todas reprovando:** o HDP deixando de ser drenado;
o HDP drenado **depois** da invalidação; a falha de drenagem passando calada; a
máscara parando de publicar `HdpFlush`; o offset virando número solto; e o
offset saindo da allowlist do transporte.

**Um portão meu passou, e a mutação o pegou** — de novo a mesma classe. Ele
procurava o **argumento** (`..._FLUSH_CNTL, 1u)`) e passava quando a **chamada**
de escrita era removida e o argumento ficava para trás. Refeito com invariante
sobre a chamada inteira, comparada sem espaços. É a segunda faixa seguida em que
um portão meu precisou ser refeito pela própria mutação.

**Correção de enquadramento meu:** na entrega anterior eu apresentei o PDE sujo
como "o alvo agora". A Corrida 60 não o reproduziu, e as três caminhadas da
Corrida 59 diziam `TABELA MUDOU` — ou seja, aquele achado era **mais fraco do
que eu o apresentei**. O que sobrevive dele são os dois defeitos reais que a
busca revelou, e eles continuam corrigidos.

**NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.**
