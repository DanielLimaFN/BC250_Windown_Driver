# Encerramento do Gate 1.5 — render-only reprovado

**Data:** 2026-09-02
**Candidato:** 3.80.0.0
**Evidência:** `docs/evidence/2026-09-02_08-59-42-DESKTOP-HQAG6T4-lab-binding`
**Veredito:** **REPROVADO — estruturalmente inviável.** O gate é encerrado, não adiado.

---

## 1. O que o Gate 1.5 prometia

O perfil `RENDER_ONLY_LAB` foi criado para responder barato a uma pergunta:
*a tabela render/base sobrevive em Código 0?* A justificativa, escrita no
próprio código, era que **"um FAIL não custa tela preta nem boot frio"**.

Nenhuma das duas metades se sustentou.

## 2. O custo prometido não existiu

Nas duas corridas do perfil (3.79 em 01/09 e 3.80 em 02/09) o ensaio custou
**tela preta e reboot**. Substituir o BasicDisplay na única função PCI com
saída de vídeo apaga o desktop por construção, independentemente do que o
driver faça depois. O perfil pagava o preço integral do ensaio caro sem
entregar o benefício que o justificava.

## 3. A pergunta é intestável por este caminho

O anel de 16 posições do schema 134 entregou, pela primeira vez, a sequência
completa que o dxgkrnl executa antes de desistir:

```
AddDevice → QueryInterface → QAI(29=WDDMDEVICECAPS) → QueryInterface
          → StartDevice → QueryInterface → StopDevice → RemoveDevice
```

| Medição | Valor | Consequência |
|---|---|---|
| `V5QaiTypeSeenLo` | `0x20000000` | Só o tipo 29. **`DXGKQAITYPE_DRIVERCAPS` nunca é perguntado** |
| `V5QaiCalls` | 1 | Uma única consulta de capacidade em todo o handshake |
| 1ª amostra de PnP com Código 43 | 08:59:47 (bind 08:59:42) | Recusa **imediata** no retorno do `StartDevice` |
| `V5StartDeviceStatus` | 0 | O driver devolveu sucesso |
| `AbiReadyPublishCount` | 2 | `StartDevice` alcançou seu caminho de retorno e promoveu as 3 ABIs |
| `V5ComputeOnlyDeclared` | 1 | O perfil declarou compute-only… que o dxgkrnl nunca leu |

Como `MiscCaps.ComputeOnly` só trafega por `DRIVERCAPS`, **não existe forma de
declarar compute-only antes da decisão**. O canal nunca é aberto.

A pergunta do gate — "a tabela render sobrevive?" — não pode ser respondida
aqui, porque a recusa acontece **antes de qualquer capacidade de render ser
consultada**.

## 4. Hipóteses eliminadas por medição

**As recusas de `QueryInterface` não são a causa.** As 8 chamadas foram
recusadas com `0xC00000BB`, e as 8 são de interfaces **opcionais**,
identificadas nos headers do WDK: I2C/DDC, OPM, OPM_2, OPM_2_JTP, OPM_3,
Miracast, WDDM3_ON_VB, e uma (`6BA41A33`) ausente de 10.0.22621 e 10.0.26100.
Recusá-las é legítimo e é o que drivers reais fazem.

**O número de sources/children não é o discriminador.** Mineração do corpus —
12 corridas com `RenderNode=1`, de 31/08 a 02/09:

| Perfil | sources/children | Corridas | `V5QaiCalls` | `V5QaiLastType` |
|---|---|---|---|---|
| display+render | 1 / 1 | 9 | 1 | 29 |
| render-only | 0 / 0 | 3 | 1 | 29 |

Todas param no mesmo ponto. Uma inferência inicial de que `0 sources` causava
o Código 43 foi **refutada** por esta tabela — registrada aqui porque o erro
foi cometido e corrigido dentro do mesmo dia, e o corpus foi o que o corrigiu.

## 5. O que o gate produziu de aproveitável

Reprovar não é perder. Ficam:

- **Harness fire-and-collect funcionando.** Primeiro ciclo 100% automático:
  bind → janela → retrato → rollback → reboot → `BasicDisplay / Código 0`, sem
  intervenção e sem resíduo.
- **Observabilidade do handshake** (schema 134). Sem o anel de DDI e o bitmap
  de QAI, este encerramento seria opinião.
- **Bring-up físico reproduzido n=2.** P1 17/17, P2 16/16, zero mismatches,
  idêntico entre 3.79 e 3.80.
- **Dois defeitos reais encontrados e corrigidos**: vazamento de handle de
  kernel em `Bc250ProbeRecord` (4 campos órfãos, um handle por bind cada) e
  guard de engine que passava em silêncio sem avaliar.

## 6. Impacto no roteiro

As ordens **2** e **3** da seção 12 do checklist —
*"Implementar RenderOnlyLab"* e *"Corrida RenderOnlyLab → PASS Código 0"* —
estão encerradas com reprovação. O marco "mais barato" do roteiro não existe.

Como o Código 43 atinge igualmente o perfil display+render, o gargalo subiu na
fila: aponta para os itens **#133 (UMD declarado no INF)** e **#134 (D3D UMD
não existe)**, hoje enterrados na seção 9.

Sustentação estrutural, obtida de drivers reais desta própria máquina:

| | `rdvgwddmdx11.inf` | `basicrender.inf` | `bc250mcdm.inf` |
|---|---|---|---|
| `Class` | `Display` | **`System`** | `Display` |
| Seção `.HW` | sim | — | **ausente** |
| `InstalledDisplayDrivers` | sim | — | **ausente** |
| `UserModeDriverName` | sim | — | **ausente** |

O render-only da Microsoft não é sequer classe Display — o Windows modela
adaptador de render sem display de outra forma. Isso explica, de forma
independente da medição acima, por que `RENDER_ONLY_LAB` sob `Class=Display`
nunca teve chance.

Continuação em [`161_PLANO_MESTRE_POS_GATE_1_5.md`](161_PLANO_MESTRE_POS_GATE_1_5.md).

---

**NÃO VALIDADO EM HARDWARE.** O que está provado no silício continua sendo
submissão de compute: `RPTR == WPTR`, fence física em VRAM com verificação de
poison e conferência byte a byte. O adaptador WDDM segue em Código 43.
