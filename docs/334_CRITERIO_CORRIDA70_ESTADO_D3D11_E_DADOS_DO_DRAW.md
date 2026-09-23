# Critério da Corrida 70 — Estado D3D11 traduzido e dados do draw medidos (faixa DX.24)

> **Escrito em 22/09/2026, ANTES do ensaio** (Lei 9). **Estado:** NÃO VALIDADO EM HARDWARE.
> **Candidato:** lab **3.231.1.0** — SYS igual (`F2A3F42C...`), UMD `1402DD97...` assinada
> (`35F72688...` fonte), INF `D46AE420...`, CAT `E90A2602...`. ICD o da DX.22 (`45F93753...`).
> **Decisão do Daniel:** medição e correções juntas numa só corrida.

---

## 0. De onde parte

Corrida 69, desfecho B: texturas de origem com conteúdo (2560x1080 com 100% das amostras
não-nulas), 46 draws com `RT0` igual à superfície apresentada, e superfície preta na grade
inteira (0/86.400). O PS do draw texturizado é `cor_do_vértice × texel`.

## 1. O que o 3.231.1.0 muda

**Medição (item 1):** `[DRAW_DATA]` em todo draw: conteúdo do CB0 do VS (não-nulos e os 8
primeiros valores como float), do CB0 do PS e do VB0 (não-nulos e os 8 primeiros dwords do
vértice inicial), lidos da sombra de CPU. O VS do DWM multiplica a posição pelo CB; CB zerado
colapsa a geometria sem erro nenhum.

**Correções (item 2), todas da classe "estado D3D11 ignorado em silêncio":**

| # | Antes | Agora |
|---|---|---|
| 1 | Só `BlendEnable` e máscara chegavam; fatores e operações nunca mapeados; `ZERO` (valor 0 no Vulkan) virava `ONE`; máscara 0 virava RGBA | Os 6 campos mapeados; `BLEND_FACTOR` via blend constante dinâmico; dual-source e valores desconhecidos **recusam** com nome |
| 2 | `inputRate` sempre por vértice | Por instância quando `InputSlotClass=1`; passo ≠ 1 **recusa** (`IA_STEP_RATE_DIFERENTE_DE_1`) |
| 3 | Slot com stride 0 recebia stride inventado | Stride 0 preservado (legítimo; RADV do fork trata, P1) |
| 4 | `location` = posição do elemento | `location = InputRegister` (vN do VS, casado pelo runtime) |
| 5 | Slot usado sem buffer recebia o buffer do slot 0 | **Recusa** (`IA_SLOT_USADO_SEM_VERTEX_BUFFER`) |
| 6 | Formato de vértice desconhecido virava BGRA8; >16 elementos truncados | **Recusa** nomeada |
| 7 | DXGI 88 (X8) sem alpha=1; 29/91/93 (sRGB) como linear; A8 amostrado em vermelho | X8 com swizzle A=ONE, sRGB em formato sRGB, A8 com (0,0,0,a) |
| 8 | Viewport com altura positiva (imagem D3D de cabeça para baixo) | Altura negativa, como o vkd3d |

Testes: `umd_state_translate_test` novo (layout real do DWM da Corrida 69 com registradores
embaralhados, slot por instância com stride 0, cada recusa nomeada, formatos e swizzles); duas
mutações reprovando (location pela ordem; ritmo sempre por vértice). Portão
`audit_dx24_d3d11_state_reaches_vulkan` com duas mutações reprovando (ternário que troca ZERO pelo
padrão; viewport sem inversão). `build-umd` e `build-contract` EXIT=0; três auditorias PASS;
`Lab package audit: PASS`; strings conferidas na UMD assinada.

**Risco declarado:** as recusas novas podem **bloquear** draws que antes rodavam com dado
inventado. Isso é o comportamento pedido pela Lei 4, e aparece no log com o motivo.

## 2. Perguntas

1. Com o estado traduzido, algum Present do DWM tem pixel não-nulo?
2. Se não, os dados que o VS lê (CB0 e VB0) têm conteúdo?

## 3. Desfechos (Lei 10 — assinatura e implicação separadas)

### A — Pixel na superfície apresentada
- **Assinatura:** pelo menos uma `INSPECAO LINEAR_BUF GRADE: NonZero=n/...` com `n > 0` num
  `dwm.exe`, seguida de `DXGI Present LEAVE cb: hr=0x00000000`.
- **Implicação:** o DWM produz pixel por GPU e ele chega ao buffer que vai ao scanout. A caixa
  envolvente diz onde (a barra de tarefas tem de ficar em `y ≥ 1040`, o que também valida a
  inversão do viewport). Qual das correções foi decisiva fica **H**; o `[DRAW_DATA]` permite
  descartar a hipótese do CB zerado. Próximo: D1.8 (atribuição) e a ponte para a alocação WDDM.

### B — Preto, e o CB do VS está zerado
- **Assinatura:** todas as `GRADE` do DWM com `NonZero=0`, **e** `[DRAW_DATA] VSCB0=... nz=0/...`
  nos draws com `RT0` igual ao `hSurfaceToPresent`.
- **Implicação:** a geometria colapsa porque o CB nunca recebe os dados do DWM. Próximo: o caminho
  de atualização de buffer constante (`UpdateSubresourceUP` de buffer, `Map` com `DISCARD`).

### C — Preto, com CB e VB com conteúdo
- **Assinatura:** `GRADE` com `NonZero=0`, **e** `VSCB0 nz > 0` **e** `VB0 nz > 0` nesses draws.
- **Implicação:** os dados existem e o resultado ainda é preto. Próximo: ler o alvo logo depois de
  um draw (readback do RT) para separar "nada rasterizado" de "rasterizado em preto".

### D — Draws do DWM passam a ser recusados
- **Assinatura:** linhas `Bc250UmdExecuteVkDraw: RECUSADO -- IA_...` ou `-- BLEND_...` em draws
  do `dwm.exe` que na Corrida 69 executavam.
- **Implicação:** o DWM depende de um recurso que agora recusamos com nome (passo de instância,
  dual-source, formato de vértice). O motivo vira a próxima tarefa; o pixel fica para depois.

### E — Algo fora do mapa
- **Assinatura:** o que não couber em A–D — inclusive a volta do replay (checker de IB saindo 1),
  fault GCVM, exceção na criação de pipeline (`EXCECAO INTERCEPTADA`, que seria a divisão por zero
  histórica com stride 0), BugCheck, regressão dos controles.
- **Implicação:** descrever antes de julgar.

**Desfecho parcial não é desfecho** (Lei 10.3). B e C podem coexistir com D em processos
diferentes; o desfecho é decidido pelo `dwm.exe` que apresentou.

**Controles a não regredir:** checker de IB = 0, zero `[TDR_GCVM_FAULT]`,
`Submetidas == Aposentadas`, `RECOVERY_OK_ONLINE`.

## 4. Execução

Boot frio, `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Anotar o que aparecer no monitor antes do
rollback.
