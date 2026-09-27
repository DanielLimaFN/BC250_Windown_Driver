# Critério da Corrida 69 — Onde o pixel se perde (faixa DX.23)

> **Escrito em 22/09/2026, ANTES do ensaio** (Lei 9). **Estado:** NÃO VALIDADO EM HARDWARE.
> **Candidato:** lab **3.230.1.0** — SYS **igual** ao 3.229.1.0 (`F2A3F42C...`), UMD nova
> (`D2761B8C...` assinada, `366E7BB8...` fonte), INF `FFD5BCA8...`, CAT `B3B6EE01...`.
> **ICD:** o mesmo da Corrida 68 (`45F93753...`, DX.22), copiado pelo executor.

---

## 0. O que a Corrida 68 deixou em aberto

- Os 16 Presents aceitos do DWM sairam com `NonZeroSamples=0/5`. **Essa medição não sustenta
  "tudo preto":** as 5 amostras são 4 pixels do canto superior esquerdo e o centro, e o draw do
  quadro lido tinha scissor `0,1040..2560,1080` — as 40 linhas da barra de tarefas, onde nenhuma
  amostra caía, com uma textura 2560x40 enviada pela CPU logo antes.
- O zero do KMD (`NonZero=0/256` na alocação WDDM) também não prova nada sobre o que o DWM
  desenhou: a ponte Vulkan→WDDM falhou em 16 de 22 tentativas (12 por `DstVa` não resolvida no
  KMD, 6 "OK" que copiaram o que havia no buffer linear), então a alocação WDDM nunca recebeu
  conteúdo confiável.
- O que chegou ao monitor foi o buffer linear, via `radv_bc250_present_scanout` → escape
  `PRESENT_BLT` → SDMA para `0xF400000000`. **Pergunta ao operador sobre a 68:** apareceu alguma
  faixa na base da tela? A resposta vale como observação, não como prova.
- Descartado por leitura: `RotateResourceIdentities` troca alocação WDDM e sombra de CPU, **não**
  a `VkImg`; RTV e Present usam a mesma imagem Vulkan do objeto.

## 1. O que o 3.230.1.0 mede (só instrumentação)

1. `[VK_BRIDGE] INSPECAO LINEAR_BUF GRADE`: 1 linha em 4 e 1 pixel em 8 sobre a superfície
   inteira, com `NonZero=n/total`, caixa envolvente, primeiro valor não-nulo e a **faixa das 40
   linhas inferiores** contada à parte. O Present passa a imprimir `NonZeroGrade=`.
2. `[VK_RES] UploadImageData ... res=%p NonZeroSrc=n/m`: quanto da **origem** que o DWM nos
   entregou já é não-nula (medido na memória da CPU).
3. `SetRenderTargets ... RTV0=%p res=%p WxH` e `DRAW fence concluida ... RT0=%p WxH img=%p`: qual
   recurso cada draw teve como alvo, para cruzar com `hSurfaceToPresent`.

Testes: `umd_present_grid_test` novo (preto → 0; barra só nas 40 linhas → 3.200 na grade,
invisível às 5 amostras antigas); mutação que devolve a contagem antiga **reprova**.
`build-umd` e `build-contract` EXIT=0; três auditorias PASS; `Lab package audit: PASS`; strings
novas conferidas na UMD assinada.

## 2. Pergunta

Num Present aceito do `dwm.exe`, o buffer linear tem pixel não-nulo em algum lugar? Se não, em que
elo a cadeia **origem → draw → superfície apresentada** perde o conteúdo?

## 3. Desfechos (Lei 10 — assinatura e implicação separadas)

### A — Há pixel na superfície apresentada
- **Assinatura:** pelo menos uma linha `INSPECAO LINEAR_BUF GRADE: NonZero=n/...` com `n > 0` num
  processo `dwm.exe`, seguida de `DXGI Present LEAVE cb: hr=0x00000000` do mesmo quadro.
- **Implicação:** o DWM produz conteúdo por GPU e ele chega ao buffer que vai ao scanout. D1.6
  passa a depender só da atribuição (D1.8: foto/observação antes do `RECOVERY_OK_ONLINE`) e da
  cópia para a alocação WDDM. A caixa envolvente diz se é só a barra ou mais.

### B — Origem com conteúdo, draws no alvo certo, superfície preta
- **Assinatura:** todas as `GRADE` do DWM com `NonZero=0`, **e** algum `UploadImageData` com
  `NonZeroSrc > 0`, **e** algum `DRAW ... RT0=` igual ao `hSurfaceToPresent` do Present seguinte.
- **Implicação:** o draw executa e escreve preto — suspeitos: saída do pixel shader, blend/write
  mask, amostragem (view/formato/sampler). Próximo passo pelo `[VK_PIPELINE_IR]` do PS desse draw.

### C — Os draws não miram a superfície apresentada
- **Assinatura:** `GRADE` com `NonZero=0`, **e** nenhum `DRAW ... RT0=` igual ao
  `hSurfaceToPresent`.
- **Implicação:** o DWM compõe em alvo intermediário e leva à superfície final por um caminho que
  não executamos (cópia de recurso, resolve, etc.). Procurar a DDI de cópia entre o último draw e o
  Present.

### D — A origem já é preta
- **Assinatura:** `GRADE` com `NonZero=0` **e** todos os `UploadImageData` do DWM com
  `NonZeroSrc=0/...`.
- **Implicação:** o preto vem de cima (conteúdo que nos entregaram). A parede passa ao Explorer
  (`CreateResource CALLBACK_FAILED`) e aos clientes que desenham a área de trabalho.

### E — Algo fora do mapa
- **Assinatura:** o que não couber em A–D — inclusive a volta do replay (`check-gfx-ib-dump-cumulative.py`
  saindo 1), faults GCVM, TDR repetido, BugCheck, regressão dos controles.
- **Implicação:** descrever antes de julgar.

**Desfecho parcial não é desfecho** (Lei 10.3). **Controles a não regredir:** checker de IB = 0,
zero `[TDR_GCVM_FAULT]`, GFX `Submetidas == Aposentadas`, `RECOVERY_OK_ONLINE`.

## 4. Execução

Boot frio, `tools\EXECUTAR_D3D11_NATIVO.cmd -Force`. Ao terminar, anotar o que se viu no monitor
**antes** do rollback (em especial a base da tela).
