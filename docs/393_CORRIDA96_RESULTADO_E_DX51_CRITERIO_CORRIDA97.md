# Corrida 96 (DX.50) — resultado; DX.51 e critério da Corrida 97

> 24/09/2026.
> **Corrida 96:** bind às 23:52:12, lab **3.256.1.0** SYS `041E71DC…`, UMD
> `725279BA…`.
> **Evidência:** `docs/evidence/2026-09-23_23-52-12-DESKTOP-HQAG6T4-lab-binding/`.
> **Critério:** [doc 392](392_DX50_BISSECCAO_DIRTY_RECTS_E_CRITERIO_CORRIDA96.md).

## 1. Corrida 96: resultado

**Relato do Daniel:** desktop "piscando todo bugado"; ele reiniciou depois de
5 min.

**Máquina (P0):**
- Kernel-Power 41 às 23:59:12;
- fusível recusou o boot seguinte;
- rollback `RECOVERY_OK_ONLINE` às 23:59:32.

**Controle (P0):**
- 20 `DIRTY_RECTS_RECUSADOS … motivo=DX50_BISSECCAO_DESLIGADO`;
- zero `DIRTY_RECTS n=`.

A bissecção estava ativa.

**Medido (P0):**
- harness 60/60;
- KMD (log resgatado, 202.035 linhas, sha256 `E42DED07…3A282`): 684 Presents
  e 246 `COMPOSITE_BLT OK`;
- zero TDR, zero `GFXHUB_FAULT`, zero `CP_RESET_DETECTED`, zero timeout;
- DX.48: nenhum `AddDevice #2`.

**Travamento:**
- o log do Explorer (sink durável) para às **23:56:38**;
- o log do KMD termina no meio do registro de uma submissão SDMA de paginação,
  igual às 94 e 95.

**Arquivamento novo (P0):**
- o bind da 96 arquivou os logs residuais da 95, incluindo o do dwm.exe da 95
  (pid 1200, 10,9 MB, última escrita às **23:34:05**, no mesmo segundo que o
  Explorer da 95);
- isso data o travamento da 95;
- **defeito da coleta:** o arquivo foi *movido* e levou a ACL de
  `C:\Windows\Temp`, então a sessão não elevada não consegue lê-lo. Corrigido
  abaixo.

**Desfecho: C.** Travou com os dirty rects desligados. **Eles não são a causa do
travamento.** A tela piscando aconteceu com cópias de tela cheia, então o
defeito visual também não vem só deles.

**Contagem:** 3 travamentos em 3 corridas com a DX.49 (94, 95 e 96). O que
sobra da DX.49 é:
- o lote de draws;
- o staging persistente;
- o trace (inócuo para a GPU).

## 2. DX.51 (uma variável)

**`bc250_d3d_umd.c`:** `static const BOOL s_Bc250Dx51DrawBatchEnabled = FALSE;`.
- Todo draw vai pelo caminho direto: dreno antes, dados no próprio recurso e
  flush depois. É o comportamento da Corrida 93.
- O anel de 16 MB **não é criado**.

**Continuam:**
- staging persistente;
- trace durável;
- dirty rects desligados (DX.50).

**Coleta (sem efeito na GPU):**
- o binder passou a **copiar** os logs residuais, conferir o hash e só então
  apagar a origem;
- a cópia herda a ACL da evidência;
- o wrapper da 97 roda `icacls /reset` na pasta arquivada da 96, para liberar a
  leitura do log do dwm.exe da 95.

**Verificação offline:**
- build-umd e build lab EXIT=0;
- portão `audit_dx51_draw_batch_bisection` **3/3** mutações;
- DX.50 **6/6** (inclui a cópia com hash antes de apagar);
- DX.49 **19/19**;
- auditoria de segurança PASS.

**Candidato:**

| Componente | Valor |
|---|---|
| KMD | lab `3.257.1.0` SYS `94DFDF76A85EC53734E18A6B20AC93DC646129D81B0D9A1BD80AE38C3C48B8C3`, INF `55E0A107…D812`, CAT `7874BE52…6855`. Auditoria do pacote PASS |
| UMD | `7226ABECAA2CA8440703B8B33284950EAE404F6672470D042069394E126C831F` |
| Pré-voo | `tools/prepare-corrida97-dx51.ps1`: PASS |
| Execução | `tools\EXECUTAR_CORRIDA97_DX51.cmd`, elevado, 300 s, depois de **desligamento completo** (marcador do rollback de 23:59:32) |

## 3. Critério da Corrida 97, escrito antes

**Controle:**
- zero `[DX49_BATCH] ANEL … criado` no log do dwm.exe;
- todo `DRAW GRAVADO NO LOTE` com `Anel=0 FlushAgora=1`;
- `motivo=DX50_BISSECCAO_DESLIGADO` presente.

| | Assinatura | Implicação |
|---|---|---|
| **A** | Janela inteira **sem travamento** e **sem piscar**, 60/60 | O lote de draws é a causa. O próximo passo é achar qual garantia de vida útil ou ordem ele quebra, antes de religá-lo. Vazão esperada perto da 93 (~40/min), um pouco acima pelo staging. |
| **B** | Sem travamento, **mas piscando** | O travamento vem do lote; o defeito visual tem outra causa (staging ou algo anterior à DX.49). |
| **C** | **Travamento** de novo | Nem dirty rects nem lote. Fica o staging persistente (11 MB host-visible mapeado permanentemente, barreira nova do upload). Próximo corte: staging por chamada de volta. |
| **D** | Sem travamento, sem piscar, e desktop tão lento quanto na 93 | Igual a A, com a vazão da DX.49 perdida junto com o lote, como previsto. |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar. |

**Depois da corrida:**
- ler o log do dwm.exe da 95 (liberado pelo `icacls`) e o da 96 (arquivado pelo
  bind da 97);
- as 5 seções e a tabela-checklist.

## 4. Adendo, antes da corrida (mineração offline de 24/09)

**Achado (P0):** o Explorer está num laço de falha:
1. tenta uma alocação de 3.407.872 bytes (`Token=0x52455300…`, com
   `hRTResource`);
2. o `pfnAllocateCb` devolve `E_INVALIDARG`, sem nenhuma recusa no log do KMD;
3. ele **destrói o device inteiro**: `Bc250UmdVkBridgeShutdown`, `VkDevice` e
   contextos WDDM;
4. recomeça ~1,5 s depois.

| Corrida | Ciclos do laço (Explorer) | `KMD_CONTEXT_CREATE` | `VMID_POOL LIBERADO` |
|---|---|---|---|
| 93 | 21 em ~5 min | 492 em ~5 min (~100/min) | 70 |
| 94 | — | 228 em ~35 s (~390/min) | 22 |
| 95 | 126 | 973 em ~3,4 min (~290/min) | 98 |
| 96 | 173 em ~4 min | 1302 em ~4,4 min (~300/min) | 136 |

**Leitura:**
- a falha já existia na 93; a DX.49 só deixou tudo mais rápido, e com isso o
  laço;
- na 96 o travamento coincide com um `BridgeShutdown ENTER` do Explorer sem
  LEAVE, às 23:56:38;
- na 95, com uma rajada de `CheckMultisampleQualityLevels`, logo depois de um
  recomeço.

**H nova, concorrente:** o travamento acompanha a **taxa de criação e destruição
de contextos/VMIDs** (dreno e desarme de VMID com escrita de registrador
enquanto a GPU trabalha), e não um defeito do lote em si.

**Consequência para a 97:** com o lote desligado a vazão volta à da 93, e o laço
e a rotatividade também. Por isso **o desfecho A não separa as duas hipóteses**.

**Métrica obrigatória na análise:** `KMD_CONTEXT_CREATE`/min e ciclos do laço do
Explorer por minuto.

**Se der A (ou D):** a Corrida 98 religa o lote e **corrige o laço do Explorer**
(descobrir por que o `pfnAllocateCb` recusa essa alocação). Se a 98 não travar,
a causa era a rotatividade; se travar, era o lote.
