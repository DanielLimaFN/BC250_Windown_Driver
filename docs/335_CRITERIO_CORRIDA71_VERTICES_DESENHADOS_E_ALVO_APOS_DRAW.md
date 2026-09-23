# Critério da Corrida 71 — Vértices desenhados e alvo logo após o draw (faixa DX.25)

> **Escrito em 22/09/2026, ANTES do ensaio** (Lei 9). **Estado:** NÃO VALIDADO EM HARDWARE.
> **Candidato:** lab **3.232.1.0** — SYS igual (`F2A3F42C...`), UMD `A757ACED...` assinada
> (`2AC36A00...` fonte), INF `AC2651E4...`, CAT `9F90AF08...`. ICD o da DX.22 (`45F93753...`).
> **Só instrumentação** sobre o 3.231.1.0.

---

## 0. De onde parte

Corrida 70, desfecho C: superfície apresentada preta (0/86.400), CB do VS com a transformação
tela→NDC correta (`[0,001 −0,002 −1 1]`), VB com ~20% não-nulo. O vértice amostrado (offset 0)
era zero, mas **não era o desenhado**: os draws são indexados e `StartIndexLocation`,
`BaseVertexLocation` e o index buffer não estavam no log.

Restam três explicações: (1) nada é rasterizado; (2) é rasterizado em preto; (3) é desenhado e
depois sobrescrito.

## 1. O que o 3.232.1.0 mede

1. `[DRAW_VERTS]` em todo draw: `Indexed`, `Start`, `Base`, formato e offset do IB, os 6
   primeiros índices lidos da sombra do IB, e os 3 primeiros vértices que eles apontam, com os 3
   primeiros dwords como float (posição no layout do DWM) e o 4º em hex (cor R8G8B8A8). Estados
   nomeados: `IB_SEM_SOMBRA`, `IB_FORA_DO_BUFFER`, `FORA_DO_BUFFER`, `INDISPONIVEL`.
2. `[DRAW_READBACK] #n` logo após cada draw em alvo ≥ 1024x512: cópia do alvo inteiro para o
   buffer linear e a mesma `GRADE` do Present. Teto de 40 por device, declarado no log. Nada é
   escrito no alvo.

Testes: portão `audit_dx25_drawn_vertices_and_target_readback` (índice a partir de
`IndexOffset + Start`, `BaseVertex` somado, teto declarado, nenhuma escrita no alvo); mutação
removendo o `BaseVertex` **reprova**. Builds EXIT=0; três auditorias PASS; a primeira passada do
empacotador falhou e a auditoria **bloqueou** o pacote inconsistente; a segunda passou e a auditoria
deu PASS.

**Custo declarado:** até 40 cópias de 11 MB por DWM. Pode deixar o DWM mais lento no começo.

## 2. Desfechos (Lei 10 — assinatura e implicação separadas)

As linhas relevantes são do `dwm.exe` que apresentou, nos draws com `RT0` igual ao
`hSurfaceToPresent`.

### A — Pixel no Present
- **Assinatura:** alguma `GRADE: NonZero=n` com `n > 0` logo após um `DXGI Present` do `dwm.exe`
  (não após `[DRAW_READBACK]`), seguida de `DXGI Present LEAVE cb: hr=0x00000000`.
- **Implicação:** D1.6 passa a depender de atribuição (D1.8) e da ponte para a alocação WDDM.

### B — Nada é rasterizado
- **Assinatura:** todas as `GRADE` de `[DRAW_READBACK]` com `NonZero=0`, **e** `[DRAW_VERTS]`
  com vértices zerados, fora do buffer ou sem sombra do IB nesses draws.
- **Implicação:** o dado que o VS lê nos índices desenhados não é o que o DWM escreveu. Próximo: o
  caminho de atualização do VB/IB dinâmico (`Map` com `NO_OVERWRITE`/`DISCARD`) e o offset.

### C — Vértices válidos, alvo preto depois de cada draw
- **Assinatura:** todas as `GRADE` de `[DRAW_READBACK]` com `NonZero=0`, **e** `[DRAW_VERTS]` com
  posições dentro da tela (0–2560, 0–1080, espaço que o CB converte para NDC) e cor `w3` não-nula.
- **Implicação:** os dados estão certos e mesmo assim nada aparece no alvo. O problema está na
  rasterização ou na saída (viewport/scissor efetivos, estado de profundidade, saída do PS,
  amostragem). Próximo: estado efetivo do pipeline desse draw e o valor que o PS escreve.

### D — Desenhado e depois sobrescrito
- **Assinatura:** alguma `GRADE` de `[DRAW_READBACK]` com `NonZero > 0`, **e** as `GRADE` do
  Present seguinte com `NonZero=0`.
- **Implicação:** o pixel existe e se perde entre o draw e o Present. A ordem das linhas diz depois
  de qual draw (ou clear, ou cópia) a grade volta a zero.

### E — Algo fora do mapa
- **Assinatura:** o que não couber em A–D — inclusive `[DRAW_READBACK] ... FALHOU`, regressão dos
  controles, TDR novo, replay de volta.
- **Implicação:** descrever antes de julgar.

**Desfecho parcial não é desfecho** (Lei 10.3). **Controles a não regredir:** checker de IB = 0,
zero `[TDR_GCVM_FAULT]`, `Submetidas == Aposentadas`, `RECOVERY_OK_ONLINE`.

## 3. Execução

Boot frio, `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Anotar o que aparecer no monitor.
