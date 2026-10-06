# 578 — Mineração da Corrida 210: falta de memória no segmento 1, a cascata do DWM e o que sobra da trava

**Data:** 03/10/2026 · **Tipo:** offline, sem corrida (Lei 9)
**Fonte:** `docs/evidence/2026-10-03_22-23-42-DESKTOP-HQAG6T4-lab-binding/`
- log do KMD recuperado;
- `umd-logs-pos-travamento/` (logs da UMD e da RADV de cada processo).

Comparação com a 207 (`2026-10-02_21-55-18`) e a 208 (`2026-10-02_22-29-44`).

---

## 1. Linha do tempo da 210 (P0, horários dos logs da UMD)

| Hora | Evento |
|---|---|
| 22:23:51 | Sobe o `dwm.exe` pid 1296 (Explorer pid 6168) |
| **22:26:08** | Primeiro `vkAllocateMemory … res=-2` no **Explorer**; a RADV registra `MakeResident: status=0xC0000017` (**`STATUS_NO_MEMORY`**) para uma alocação de **64 KB** |
| 22:26:11 | A mesma falha na **Pesquisa** |
| 22:26:24 | A mesma falha no **DWM 1296** (imagem de 16×16, 4 KB), seguida de `vkQueueSubmit res=-4` (device perdido) |
| 22:26:39 | O DWM 1296 desmonta o device e chama `CloseAdapter`, depois de **732 Present** |
| 22:26:43–22:27:03 | DWM 3500 (`res=-2` ×4, `-4` ×4), desmonta |
| 22:27:07–22:27:26 | DWM 12692 (`res=-2` ×10), desmonta |
| 22:27:30 | DWM 5920 sobe, já com `res=-2` ×7 |
| ~22:27:40 | **Trava dura**, com o DWM 5920 destruindo recursos e a paginação em rajada (25 despejos e 65 MB no último segundo, BPG1) |

**Totais:**
- 480 `res=-2` e 211 `MakeResident NO_MEMORY` nos logs da RADV;
- 4 instâncias de `dwm.exe`.

## 2. Por que falta memória (P0 do log do KMD + P1 do código)

**Distribuição das alocações (`DX93_ALLOC_MAP`):**

| Classe | SegSet | Quantidade | Total |
|---|---|---|---|
| **RADV** (classe 0) | **`0x1`, só a VRAM** | 2.550 | **1.881 MiB** acumulados (1.943 de 64 KB, 97 de 11,8 MB) |
| Recursos da UMD (`0x52455300`) | `0x3`, preferido 2 | 2.014 | — |

- O segmento 1 tem ~245 MB.
- A RADV não tem para onde transbordar. Quando o conjunto em uso passa disso, o VidMm recusa a residência (`STATUS_NO_MEMORY`), a RADV devolve `VK_ERROR_OUT_OF_DEVICE_MEMORY` e o device se perde.
- **No Linux** (L-B, doc 552), o excedente vai para o GTT, e o `amdgpu` não travou com 256 MB.
- **A correção já existe e está dormente:** é a **M1b.1** ([doc 554](554_M1B1_RADV_NO_SEGMENTO_2_DORMENTE_E_CORRECAO_DO_HEAP.md)). Ela põe a RADV no segmento 2 e corrige o heap fictício de 16 GB e o `FUSION`.

## 3. Os 109 `Present RECUSADO 0xC01E0200` não são cópia da GPU falhando (P0)

- **Toda recusa** vem precedida de `BLT_DESTINO RECUSADO: motivo=GEOMETRIA_DA_ORIGEM_DESCONHECIDA` (109 de 109).
- O caso: um Present em modo **Blt** (`Flip=0, Blt=1`) para um destino que não é o scanout. O KMD recusa e devolve `STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE`, que põe o contexto em estado perdido.
- **Quando começam:** no Present #733, isto é, logo depois de o DWM 1296 morrer com 732. Só aparecem **depois** da cascata.
- **De onde vêm:** a UMD dos DWMs reiniciados registrou só 4 `DXGI Present`. Logo, a maior parte das recusas vem de outro contexto. Hipótese: a composição pelo GDI enquanto o DWM está fora. Isso é **H**.
- **O que se pode afirmar:** que alimentam a cascata é H. O P0 é que elas são **consequência** da primeira morte do DWM, não a causa.

## 4. O que sobra da trava (o ponto que manda)

| Corrida | `res=-2` | `MakeResident NO_MEMORY` | DWMs | Trava |
|---|---|---|---|---|
| 207 | **0** | **0** | 1 | ~96 s |
| 208 | 9 | 0 | 2 | ~290 s |
| 210 | 480 | 211 | 4 | ~228 s |

- **A falta de memória não é necessária para a trava:** a 207 travou sem nenhuma (P0).
- **O fator comum de 202 a 210** é a **rajada de alocação e paginação de superfícies de tela cheia**: abrir menu ou Pesquisa, recriar o DWM. A falta de memória é **um gerador de rajadas**, porque cada morte do DWM recria tudo, mas não a causa.
- **São dois defeitos distintos:**
  1. **Funcional (P0):** a RADV presa ao segmento 1 esgota a memória e derruba o DWM em cascata. Mesmo sem trava, o desktop morreria.
  2. **A trava dura:** continua sem causa. Ela sobrevive à 4c inteira (PTE, TLB e leitura de tabela já sem a CPU).

## 5. Recomendação

1. **Ligar a M1b.1** (segmento 2 para a RADV), que é a estratégia já decidida pelo Daniel: "VRAM mínima, memória extra pelo segmento 2" (doc 551). Ela ataca o defeito 1 e, como efeito esperado (**H**), reduz as rajadas de despejo no segmento 1. Pré-requisitos do doc 554 §5, revistos:
   - **Composição de fonte no segmento 2:** hoje é recusada com log (falha fechada). Medir a contagem na corrida em vez de bloquear.
   - **Robustez da paginação:** o `0x34` foi tratado pela L1 (207–210, zero `0x10E`). `Upload LOCK 0x80070057` e `Offer/ReclaimResources` sem backend continuam abertos.
   - **Os três interruptores juntos:** DriverVer novo, **RADV recompilada** (cuidado com o ninja parcial: apagar os `.obj`), hashes conferidos e critério A–E.
2. **Corrigir a resposta ao Present em Blt de geometria desconhecida.** Hoje é `GPU_EXCEPTION`, que perde o contexto. Fica atrás da M1b, porque só aparece depois da cascata.
3. **A trava** continua como pergunta aberta. Com a M1b ligada, a corrida mede também se as rajadas ficam menores e se a trava se desloca. Os candidatos a seguir, se ela persistir: fences por interrupção, SMU, SDMA1.

**NÃO VALIDADO EM HARDWARE:** a M1b.1, a correção do Present em Blt e a causa da trava.
