# 674 — C3 corrigida: a VRAM do Windows já era pela DRAM; o que muda é um só tipo de memória (WC). Critério da Corrida 254

**Data:** 09/10/2026. **Natureza:** offline, sem corrida.

## 1. Correção do doc 671 (Lei 1)

A C3 dizia: "o Windows e a UMD acessam a VRAM pela BAR/HDP, inclusive ~14 mil escritas de tabela de página". **Está errado.**
- Desde a P1b (corrida 226), `Bc250ActiveQueryRuntimeCaps` publica `Caps->CpuPhysicalStart = Bc250VramCpuTranslate(BAR + localStart)` (`bc250_active_miniport.c:4031`, P1 código).
- Com isso, o segmento 1 já é entregue ao VidMm **pelo endereço da DRAM**, igual ao `aper_base` do `amdgpu` numa APU.
- Ou seja, o VidMm, os Locks da UMD e as tabelas do processo de paginação escritas pela CPU já não passam pela BAR nem pelo HDP. Pular o flush do HDP nesse caminho está certo.
- Eu tinha lido só metade do caminho. Uma primeira versão desta etapa publicava o segmento pela DRAM de novo, o que era redundante, e foi desfeita.

## 2. O que sobrava de diferença: dois tipos de memória na mesma página

| Quem mapeia a VRAM pela DRAM | Tipo até a 253 |
|---|---|
| VidMm (segmento 1, Lock da UMD) | WC |
| `FrameBufferVirtual` do display (a abertura inteira, 256 MB) | WC |
| KMD: anel/CSB do GFX (2 sítios), tabela da GART, bloco do MEC, MQD, PFP, backing do GCVM, sonda de VRAM, página do walker | **UC** (`MmNonCached`/`PAGE_NOCACHE`) |

A mesma página física aparecia como WC (pela abertura do display) e como UC (pelo mapeamento próprio do KMD). Os dois tipos são não cacheáveis, então não é o par perigoso WB/UC, mas o comportamento não é definido.

O `amdgpu` mapeia a VRAM pela CPU **só em WC**: `aper_base_kaddr` por `ioremap_wc` e os BOs de VRAM por `ttm_write_combined`.

## 3. C3 (o que mudou)

- **Helper único (`bc250_vram_cpu_path.h`):** chave `BC250_C3_VRAM_DO_WINDOWS_PELA_DRAM 1`.
  - Todo endereço **traduzido para a DRAM** sai em WC, no `Bc250VramCpuMapMemory` e no novo `Bc250VramCpuMapTranslated`, antes de considerar o tipo pedido pelo chamador.
  - Pela BAR (P1b não armada), nada muda.
- **Sítios diretos do KMD trocados:** os dois do GFX, a página do walker e a tabela da GART. Os demais já passavam pelo helper.
- **Barreiras:** todo caminho que escreve e depois avisa a GPU já tem `KeMemoryBarrier`/`_mm_mfence`, que esvaziam os buffers WC. Conferidos: MEC, SDMA0, PFP, GART (KeMemoryBarrier antes do readback) e C1.
- **Prova no log:** `[C3_VRAM_WC] chave=1 P1b=1 mapeamentos WC da DRAM pelo KMD ate aqui=N`, emitida logo depois de `[GfxEngine] ONLINE SUCESSO`.
- **Portão** `audit_c3_vram_um_so_tipo_wc`. Três mutações, todas reprovadas: chave em 0, helper honrando UC antes de WC, GART voltando a UC direto.
- `build-contract` EXIT=0; auditorias PASS.

**Pacote lab 3.400.1.0:**

| | |
|---|---|
| SYS | `452C12FD5EF1F3792D840910A73A2EC0A774FCA28885E187E4438E621FF53249` |
| INF | `6D869901…` |
| CAT | `FB508F27…` |
| UMD | `B73F9BDA` |
| RADV | `B4AE2E14` |
| Backup | `driver/wdk/package/output_lab_3.400.1.0_254` |
| Lançador | `tools/EXECUTAR_CORRIDA254_C3_VRAM_WC_MENU_600S.cmd` |

C1 e MON.1 seguem ativos.

## 4. Critério da Corrida 254 (escrito antes)

**Antes de rodar:** `tools\BC250_RECOVERY.cmd` elevado, `tools\inspect-boot-fuse.ps1 -Reset`, boot frio. Roteiro: só o menu.

**Pré-condição:**
- `[P1B_VRAM_DRAM] ATIVO`.
- `[C3_VRAM_WC] chave=1 P1b=1 ... =N` com N ≥ 4.
- `[C1_GFX_DOORBELL] ATIVA` (C1 mantida).
- Desktop visível.
- Zero `GFXHUB_FAULT`.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | Pré-condição completa e **600 s sem trava** | O tipo de memória misto era o mecanismo provável. Repetição obrigatória |
| **B** | Pré-condição completa e trava dura de sempre (GPU ociosa) | O tipo de memória sai das causas. A C3 fica (paridade). Próxima: C4 |
| **C** | `N = 0` ou falta a linha `[C3_VRAM_WC]` | A C3 não armou. A corrida não responde |
| **D** | TDR/fault ou dado errado logo após a carga: preâmbulo do GFX que não drena, MEC sem fence, GART com divergência no readback | Barreira faltando num caminho WC. Defeito meu; voltar a chave para 0 |
| **E** | Fora do mapa | Descrever antes de julgar |

**Teto:** 2 corridas.

**NÃO VALIDADO EM HARDWARE:** a C3.
