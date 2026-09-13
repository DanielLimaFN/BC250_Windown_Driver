# 236 — Resposta à consulta técnica: micro-tiling, `DrawIndexed` e o `0x119`

> **Base desta resposta:** log cumulativo `C:\BC250_DISPLAY_LOG.txt` (38.386.654 bytes),
> recortado por bind pelas janelas de `start_offset` dos `display-log-baseline.json`, mais
> leitura do código em `driver/umd/bc250_d3d_umd.c`, `driver/wdk/bc250_sdma_present.c` e
> `driver/wdk/bc250_active_miniport.c`.
> **Nenhuma corrida foi gasta para produzir esta resposta.**

---

## 0. Sumário executivo

| Item | Veredito |
|---|---|
| "O desktop apareceu e animou" | **CONFIRMADO (P0)** — e é diferente do falso positivo de 11:22 |
| Bloqueador A (micro-tiling Morton 8×8) | **HIPÓTESE (H)** — a premissa "o D3D aloca tiled" é refutada pelo nosso próprio código |
| Bloqueador B (cadeia `E_NOTIMPL` → `DEVICE_REMOVED` → `StopDevice` → `0x119`) | **REFUTADO pelo log** — a causa é *back-pressure* de um único slot in-flight |
| Pergunta 2 (sucesso silencioso no `DrawIndexed`) | **Não fazer** — viola a Lei 4, é o defeito C1 removido hoje, e **não conserta o `0x119`** |
| Pergunta 3 (qual NTSTATUS devolver) | **Nenhum** — não existe status que faça o VidSch descartar submissão |
| Ordem correta | **B primeiro.** Com `0x119` em ~10 s não há como iterar em pixel |

---

## 1. O marco é real, e isso importa

Corrida das 15:38 (recorte `[37.740.388 .. 38.386.654)`), 199 Presents:

```
[Present #1] BYTE_PROOF PASS (dados conferidos, frame com conteudo):
             Source=0x15D0000 Dest=0x0 Bytes=11059200 (PostScanout: NonZero=199/256, Hash=0xFFD016DF)
[Present #3] *** DESKTOP REAL DETECTADO EM SLOT_DST *** NonZero=199/256, Hash=0xFFD3E607
[Present #5] *** DESKTOP REAL DETECTADO EM SLOT_DST *** NonZero=127/256, Hash=0xFF76EFDC
[Present #6] *** DESKTOP REAL DETECTADO EM SLOT_DST *** NonZero=89/256,  Hash=0x00372FC1
```

Amostragem do scanout: `SCANOUT NonZero=127/256` e `164/256`.

**Por que isto vale, quando o de 11:22 não valia:** em 11:22 o detector disparava com
`NonZero=1/256` — 255 dos 256 dwords amostrados eram zero, e `SCANOUT`/`PRIMARY` amostravam
`0/256`. Era frame preto com um dword não-nulo. Agora são **199, 127 e 89 de 256**, com o
**scanout** também cheio, e os **hashes mudam entre Presents** — que é literalmente a definição
de "animou". Isto é conteúdo real chegando à abertura de scanout pelo SDMA. Parabéns; é o
primeiro pixel do projeto que se sustenta na medição.

Duas ressalvas que continuam valendo:

1. `[Present #5] BYTE_PROOF FAIL: Source=0x190000 Dest=0x0 Bytes=11059200 FirstMismatch=0x1D0;
   SDMA disabled` — o destino divergiu da origem no byte **464**. Ou a cópia não é fiel, ou a
   origem está sendo escrita pelo DWM enquanto lemos (tearing). Isso aponta para o **blt**, não
   para swizzle. Ver §2.3.
2. 199 Presents com apenas **3** `SDMA=YES`: a esmagadora maioria dos Presents não está blitando.

---

## 2. Bloqueador A — a premissa do micro-tiling não se sustenta como está escrita

### 2.1 Em WDDM, quem escolhe o layout é o driver, não o D3D

Não existe swizzle Morton imposto pelo runtime do Direct3D. Em GFX10 o *driver de modo usuário*
escolhe o swizzle mode (`SW_LINEAR`, `SW_4KB_*`, `SW_64KB_*`, `_S`/`_D`/`_R`) ao criar a
alocação, e o publica ao runtime via `RowPitch` e ao KMD via *private driver data*. A frase
"recursos de textura 2D criados pelo Direct3D são alocados no formato de micro-tiling 8×8" está
invertida: o D3D aceita o que o driver declara.

### 2.2 E o nosso driver declara LINEAR — está no código

`driver/umd/bc250_d3d_umd.c:641`:

```c
/* AMD GFX10 256-byte linear row pitch alignment */
rowPitch   = ((width * bpp) + 255u) & ~255u;
slicePitch = rowPitch * height;
totalSize  = slicePitch * depth;
```

Para 2560 × 4 bytes = 10240, que já é múltiplo de 256 → **`RowPitch = 10240`, idêntico ao pitch
do scanout do GOP**. E uma busca por `Swizzle|swizzle|Tiled|TILED|Morton|TileMode|tile_split`
em `driver/umd/*.c`, `driver/wdk/*.c` e `driver/core/*.c` retorna **zero ocorrências**. Não há
tiling em nenhum lugar do driver.

Conclusão: se o conteúdo no scanout está entrelaçado, **não foi o D3D que entrelaçou** — porque
o D3D nunca escolhe isso, e nós escolhemos linear.

### 2.3 O que 8 pixels realmente sugerem

8 px a 32 bpp = **32 bytes**. Um artefato com granularidade de 32 bytes é muito mais compatível
com **desalinhamento de geometria/stride na cópia** do que com tiling de 8×8, que tem grão de
**256 bytes** (o tile inteiro) e produziria blocos quadrados embaralhados, não colunas verticais
limpas e regulares.

Somando ao `FirstMismatch=0x1D0` do Present #5: 464 bytes não é múltiplo de 256 nem de 10240.
Isso é assinatura de cópia infiel ou de origem mutante, não de swizzle — swizzle daria divergência
**estruturada** em fronteiras de 256 bytes.

**Isto é H, não P0, dos dois lados.** Não implemente de-tiling antes de discriminar.

### 2.4 O discriminador, e ele é barato

Instrumentação somente-leitura no `DxgkDdiPresent`, antes do blt. Uma tela de logon tem grandes
áreas planas, e isso basta:

```c
/* Discriminador de layout: superficie linear vs tiled 8x8.
 * Somente leitura. Nao altera pixel nenhum. */
const uint32_t *p = (const uint32_t *)srcVirtual;
Bc250DisplayLog("[LAYOUT] off0     : %08X %08X %08X %08X %08X %08X %08X %08X\n",
    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
Bc250DisplayLog("[LAYOUT] off+32B  : %08X %08X %08X %08X\n",
    p[8], p[9], p[10], p[11]);                       /* proximo grupo de 8 px */
Bc250DisplayLog("[LAYOUT] off+256B : %08X %08X %08X %08X\n",
    p[64], p[65], p[66], p[67]);                     /* proximo tile 8x8 */
Bc250DisplayLog("[LAYOUT] off+pitch: %08X %08X %08X %08X\n",
    p[2560], p[2561], p[2562], p[2563]);             /* proxima LINHA se linear */
```

Leitura do resultado:

- **Linear** → `off+pitch` é praticamente igual a `off0` (mesma cor de fundo, linha adjacente),
  e `off+32B` também continua a mesma cor.
- **Tiled 8×8 Morton** → `off+pitch` cai em outro *tile row* e não tem relação com `off0`,
  enquanto `off+256B` é que exibe continuidade horizontal.

Custo: quatro linhas de log em uma corrida que você já vai fazer para o Bloqueador B. **Não gaste
uma corrida só para isso.**

### 2.5 Respostas diretas às três opções

**Opção 1 — "forçar linear via flag do D3D/WDDM":** não existe flag porque não é decisão do D3D,
e vocês **já estão em linear**. O que merece atenção é o caminho da *primária compartilhada*:
`Bc250McdmGetStandardAllocationDriverData` (`driver/wdk/bc250_mcdm_callbacks.c:1616`) é onde vocês
declaram a geometria da alocação criada pelo SO. Mantenham `Pitch=10240` lá e **recusem alto**
qualquer divergência de pitch/largura em vez de copiar silenciosamente — hoje um mismatch passaria
sem uma linha de log.

**Opção 2 — SDMA 5.0 faz de-tile em hardware?** Sim, a família `SDMA_OP_COPY` tem sub-opcodes
para superfície tiled. Mas o pacote que vocês precisam **primeiro** não é o tiled: é o
**`COPY_LINEAR_SUB_WINDOW`**, porque ele aceita retângulo com **pitch e slice-pitch independentes
de origem e destino** — ou seja, resolve stride skew em geral, tiling ou não. O builder de vocês
hoje só tem o linear plano:

```c
/* driver/core/bc250_sdma_packet_builder.h */
#define BC250_SDMA_OP_COPY            1u
#define BC250_SDMA_SUBOP_COPY_LINEAR  0u
#define BC250_SDMA_DWORDS_COPY_LINEAR 7u
```

A família de sub-opcodes do SDMA 4/5 (**P1, de memória do upstream — NÃO codifiquem sem conferir**)
é: `COPY_LINEAR=0`, `COPY_TILED=1`, `COPY_SOA=3`, `COPY_LINEAR_SUB_WIN=4`,
`COPY_TILED_SUB_WIN=5`, `COPY_T2T_SUB_WIN=6`, `COPY_DIRTY_PAGE=7`, `COPY_LINEAR_PHY=8`.

**Aqui vale a lição do docs/03 outra vez:** o layout exato dos dwords desses pacotes está em
`sdma_pkt_open.h` do upstream, e esse header **não está neste repositório** — `inc/linux_ref/` só
tem `gc_10_1_0_sh_mask.h`. Antes de desenhar o pacote, **traga o header**. Três das quatro
perguntas de registrador do B1 já estavam respondidas no catálogo local; não eram lacunas de
conhecimento, eram lacunas de busca. Codificar um pacote de sub-window por memória alheia é
exatamente o tipo de chute que a Lei 7 proíbe.

**Opção 3 — de-tile por CPU:** 11 MB por quadro a 56 Hz são ~620 MB/s de leitura mais escrita
sobre a janela de VRAM (que é *uncached* pelo BAR) — isso não fecha orçamento de quadro e é, por
construção, fallback de software no caminho de pixel. Não recomendo nem como paliativo. Se o
discriminador de §2.4 provar tiling, a resposta certa é o pacote de sub-window do SDMA (hardware),
não a CPU.

---

## 3. Bloqueador B — o diagnóstico está errado, e o log da própria corrida prova

### 3.1 O que o recorte da corrida das 15:38 mostra

```
[SubmitVirtual Node0 SDMA] CAMINHO_ESCOLHIDO: INDIRECT (Fill=0/0 bytes DestPA=0x0 DmaBytes=32 IbResolve=1 PlanoAFallback=0)
[SubmitVirtual Node0 SDMA] AVISO: InFlight anterior (FenceId=1002) retido apos 2000 polls
[SubmitVirtual Node0 SDMA] IB_EFEITO_DECLARADO: AUSENTE (hasFill=0 destPA=0x0 bytes=0)
[SubmitVirtual Node0 SDMA] GFXHUB_FAULT_SNAPSHOT: Read=1/1/1 STATUS=00000000 ADDR=00000000:00000000
[SubmitVirtual Node0 SDMA] IB NAO EXECUTADO #1: FenceId=1003; completion NAO notificado
```

E, nesse mesmo recorte:

- `StopDevice` → **0 ocorrências**
- `IB_TRIPOD TIMEOUT` → **0**
- `GFXHUB_FAULT` → `STATUS=00000000` (sem falha de tradução)

**O `StopDevice Reason=2` citado no item 3 da consulta não é desta corrida.** Ele tem 3
ocorrências no bind das **15:28** (`[28.671.164 .. 37.740.388)`), que terminou limpo:
`StopDevice: WDDM submissions: Accepted=18937, NotExecuted=0`. Dois binds diferentes foram
fundidos numa só cadeia causal — **a mesma classe de erro já corrigida duas vezes** (07/09 18:16
e 10/09 11:22). O log é cumulativo; recorte pelo `start_offset` antes de citar qualquer linha.

### 3.2 A causa real, em duas linhas de código

`driver/wdk/bc250_sdma_present.c:1569`:

```c
if (g_Bc250SdmaPresent.InFlight.Active) {
    KeReleaseSpinLock(&g_Bc250SdmaPresent.RingLock, oldIrql);
    return FALSE;                 /* <-- UM UNICO slot in-flight */
}
```

`driver/wdk/bc250_active_miniport.c:7564`:

```c
if (g_Bc250SdmaPresent.InFlight.Active) {
    ULONG waitPoll;
    for (waitPoll = 0u; g_Bc250SdmaPresent.InFlight.Active && waitPoll < 2000u; ++waitPoll) {
        Bc250ActiveSdmaRetireAndNotify(context, FALSE);
        ...
        YieldProcessor();
    }
    ...  /* desiste e segue */
}
```

… e o `FALSE` termina em `driver/wdk/bc250_active_miniport.c:7711`:

```c
return STATUS_IO_DEVICE_ERROR;
```

Cadeia verdadeira: **o DWM submeteu mais rápido do que a retirada retira → o slot único estava
ocupado → giramos 2000 polls em `DISPATCH_LEVEL` → desistimos → `STATUS_IO_DEVICE_ERROR` →
`VidSch` → `0x119` Arg1=2, Arg2=`0xC0000185`.**

`E_NOTIMPL` no `DrawIndexed` **não participa disso**. Nem `DEVICE_REMOVED`, nem `StopDevice`.

Reforço pelo histórico de BugChecks do dia, que é coerente com a mesma raiz:

| Hora | BugCheck | Leitura |
|---|---|---|
| 13:20 | `0x119` (Arg2 `0xC01E0102`) | submissão recusada |
| 14:25 | `0x133` DPC_WATCHDOG | espera em `DISPATCH_LEVEL` |
| 14:43 | `0x116` VIDEO_TDR_FAILURE | *esta* sim é fonte legítima de `DEVICE_REMOVED` |
| 15:39 | `0x119` (Arg2 `0xC0000185`) | back-pressure do slot único |

O `0x133` e o `0x119` têm a **mesma origem**: esperar por trabalho de GPU dentro do
`SubmitCommandVirtual`, que corre em `DISPATCH_LEVEL`. Se a espera é longa, dá `0x133`; se
desiste, dá `0x119`. Trocar o teto de polls só troca de BugCheck.

---

## 4. Pergunta 2 — sucesso silencioso no `DrawIndexed`: não

Quatro motivos, em ordem de peso:

1. **Não conserta o `0x119`.** Está provado em §3: a submissão que morreu foi `Fill=0/0 DmaBytes=32`
   de paginação, com `StopDevice=0` na corrida. Você trocaria honestidade por nada.
2. **É o defeito C1, removido hoje.** `Bc250UmdCompositeDraw` fazia exatamente isso — atendia
   `pfnDraw`/`pfnDrawIndexed` com `memcpy` entre shadows de CPU e devolvia sucesso silencioso.
   Foi removido em 10/09 (`docs/234` §4), e o `bc250_d3d_umd_test.c` **exigia** aquele `memcpy`,
   congelando a mentira — terceira ocorrência dessa classe no projeto. O portão
   `audit_draw_has_no_cpu_fallback` em `tools/audit-bc250mcdm-safety.py` **reprova o build** se
   voltar, e foi verificado por mutação.
3. **Torna o objetivo infalsificável.** Com sucesso silencioso, um desktop na tela deixa de
   distinguir composição por GPU de `memcpy` de CPU. Vocês perderiam justamente a capacidade de
   provar o marco que acabaram de conquistar.
4. **A pergunta está no lugar errado.** `DrawIndexed` só chega à UMD porque **nós declaramos**
   `D3D11DDICAPS_3DPIPELINESUPPORT` (via `Bc250Umd3DPipelineIsDeclared`, hoje ligado por
   `D3D11LabPromotion`). A alavanca honesta é a **capability**, não o draw. Duas opções legítimas:
   - manter a promoção e aceitar que `Draw*` recusa — é o estado atual, e foi com ele que o
     desktop apareceu e animou;
   - não declarar pipeline 3D e deixar o DWM escolher outro caminho de composição.

   Se quiserem medir o caminho de pixel com o draw "atendido", já existe o mecanismo **declarado**:
   `D3D11DrawBltSubstitution` (`tools/arm-d3d11-draw-blt-substitution.ps1`), que substitui o draw
   por cópia 1:1 **no SDMA** (nunca CPU), só na janela onde a substituição é plausível, loga
   `Draw SUBSTITUIDO por copia 1:1 no SDMA -- NAO e' rasterizacao` e conta em
   `SubstitutedDrawCount`, separado de `DrawCount`. Ele hoje cobre `pfnDraw` **e**
   `pfnDrawIndexed` — os quatro pontos de entrada de draw chamam
   `Bc250UmdSubstituteDrawWithBlt`. Se o `DrawIndexed` está caindo em recusa, é porque a
   **elegibilidade** falhou (tunable desarmado, blend ligado, viewport parcial, dimensões ou
   formato diferentes), não porque o `DrawIndexed` não seja atendido. Ligue o tunable e leia qual
   perna reprovou — isso é informação, sucesso silencioso não é.

**Correção factual, de passagem:** `pfnSetErrorCb(E_NOTIMPL)` não produz `DXGI_ERROR_DEVICE_REMOVED`.
Device removal vem do KMD/scheduler — TDR, `DXGK_INTERRUPT_DMA_FAULTED`, reset de adaptador. Vocês
têm um `0x116` VIDEO_TDR_FAILURE às 14:43, que é fonte legítima. Atribuir a remoção ao `SetErrorCb`
é hipótese apresentada como fato.

---

## 5. Pergunta 3 — qual NTSTATUS devolver: nenhum

**Não existe status que faça o `VidSch` descartar ou postergar uma submissão.**
`DxgkDdiSubmitCommandVirtual` é contrato de aceitação: o scheduler assume que o driver enfileirou.
Qualquer falha é fatal.

E vocês **já mediram isso**. O comentário em `bc250_active_miniport.c:7690` documenta a tentativa
anterior:

> Reportar a falta por `DXGK_INTERRUPT_DMA_FAULTED` daqui NÃO evita o BugCheck. […] BugCheck
> `0x119`, Arg1=`0xD`. […] Trocamos um `0x119` Arg1=2 por um `0x119` Arg1=0xD sem ganhar nada.

Ou seja: a pergunta já foi respondida por medição própria. **A resposta é não falhar.**

### 5.1 O conserto: fila de software, e retirada fora do `DISPATCH`

O slot único não tem base em hardware. O anel tem 4096 bytes e vocês observaram **84 voltas** numa
corrida, com IBs de 8–16 dwords — cabem **dezenas** de IBs enfileirados simultaneamente. Um anel
de SDMA existe precisamente para ter várias submissões pendentes.

1. **Trocar `InFlight` (1 registro) por um array circular** de, digamos, 32 registros, indexado
   pela fence de hardware. `Bc250ActiveSdmaRetireAndNotify` já sabe retirar; passa a retirar
   *todos* os que completaram, não um.
2. **Admissão pelo espaço do ANEL, não pelo slot.** Só há motivo para barrar se faltarem dwords no
   anel. Antes de barrar, retire o que já completou.
3. **Tirar a espera do `SubmitCommandVirtual`.** O laço de 2000 polls da linha 7564 é o que produz
   o `0x133`. A retirada pertence ao DPC/timer que vocês já têm
   (`Bc250ActiveSdmaRetirementTimerStart`) e à interrupção. `SubmitCommandVirtual` publica o WPTR
   e **retorna**.
4. **Se o anel realmente encher**, complete o mais antigo de forma **limitada** (o anel é seu, você
   sabe o alvo) e registre como dívida declarada. Com 32 slots e 4 KB de anel isso deve ser
   inalcançável — se acontecer, é informação, não motivo para falhar.

Restrições que continuam valendo, e que a versão assíncrona **não pode** enfraquecer:

- **Lei 2** — onde há efeito declarado, o tripé continua obrigatório: `RPTR==WPTR`, fence física
  escrita **pela GPU** sobre poison, e dados conferidos byte a byte. A perna 3 não pode virar
  "não conferida" por conveniência da assincronia.
- **Lei 4** — nenhum `KeStallExecutionProcessor`, nenhuma fence incrementada por CPU.
- Manter o **flush de HDP** — foi ele que tornou a fence visível (validado no silício em 12:16:
  1819 submissões, 84 voltas, zero timeout, máximo de 181 polls contra 164.167 antes).
- Usar `Bc250SdmaRptrMatches` / `Bc250TripodWrapRingPointer`; o portão
  `audit_ring_wrap_arithmetic` proíbe comparação crua de RPTR.
- Escrever o **portão de auditoria** do novo invariante ("submit nunca devolve falha por
  back-pressure") e **provar por mutação**.

---

## 6. Ordem recomendada

1. **Fila de software + retirada assíncrona (§5.1).** Sem isso, toda corrida morre em ~10 s e não
   há como iterar em pixel. É também o conserto do `0x133`.
2. **Na mesma corrida**, levar o discriminador de layout de §2.4 (quatro linhas de log,
   somente leitura, custo zero).
3. **Só então** decidir sobre de-tiling — e, se o discriminador provar tiling, ir pelo pacote de
   sub-window do SDMA **depois de trazer o `sdma_pkt_open.h`** para `inc/linux_ref/`.
4. Investigar por que 199 Presents produziram apenas 3 `SDMA=YES`, e o
   `BYTE_PROOF FAIL FirstMismatch=0x1D0` — pode ser tearing (origem escrita durante a cópia), que
   se resolve com sincronização, não com de-tiling.

**Antes da corrida, escrevam o critério de PASS/FAIL.** Sugestão para a etapa 1: ≥5000 submissões
com o DWM ativo, zero `IB NAO EXECUTADO`, zero `AVISO: InFlight anterior`, zero BugCheck. E
lembrem: qualquer imagem que apareça **depois** do rollback deve ser atribuída ao BasicDisplay até
prova em contrário — esse engano já custou duas leituras erradas neste projeto.
