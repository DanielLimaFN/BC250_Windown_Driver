# 189 — Checklist até jogos na BC-250

**Atualizado em:** 07/09/2026, depois da corrida TDR 3.89  
**Meta:** jogos no Windows 10 22H2, monitor no DisplayPort da BC-250, D3D12/Vulkan,
sem WARP, llvmpipe ou fallback de software.  
**Fonte normativa:** [`../BASE_DE_AVANCO.md`](../BASE_DE_AVANCO.md)  
**Roadmap:** [`164_ROADMAP_WDDM_JOGOS.md`](164_ROADMAP_WDDM_JOGOS.md)

Este documento é a visão operacional do projeto. Ele não substitui a Tabela 1 nem a
Tabela 2 da base; transforma ambas em uma sequência verificável até a meta.

## Legenda

| Marca | Significado |
|:---:|---|
| ✅ | Fechado por medição no silício (`P0`) |
| 🟦 | Implementado e testado offline, mas **NÃO VALIDADO EM HARDWARE** |
| 🟨 | Parcial: há execução física, mas faltam pernas do contrato |
| ⬜ | Ainda precisa ser construído ou exercitado |
| ⛔ | Bloqueado por uma etapa anterior |
| ➖ | Opcional ou fora do caminho crítico |

## Foto atual

| Área | Onde estamos | O que já foi provado | Bloqueio atual |
|---|:---:|---|---|
| Hardware base / compute direto | ✅ | GPU, VRAM, PSP, RLC, CP, MEC/HQD e fences físicas exercitados | Nenhum para o caminho crítico |
| SDMA / IB WDDM | ✅ | IB real do `dxgkrnl` sob VMID8 fechou RPTR/WPTR, fence sobre poison e cinco canários | Robustez de produção ainda entra nos testes long-run |
| GCVM / paginação | 🟨 | MMU global, root física, caminhada CPU/PT, VMID8 e IB de paginação exercitados | Residency, teardown e Node 1 ainda não fecharam como conjunto |
| Miniport WDDM | ✅ | Código 0, segments, paging context, VidPn e objetos do sistema | Recursos avançados ainda parciais |
| Display por herança GOP | 🟨 | CommitVidPn, primary, source address, VSync OTG e Present SDMA ocorreram | Primária fica preta porque o DWM não consegue criar device D3D10/11 |
| TDR da SDMA0 | 🟨 | `0x117`, quiesce, reset, restart por fence e 40 submissões readmitidas | Falta provar recuperação mantendo **imagem visível**; depende do B7 |
| QueryEngineStatus | 🟦 | Lógica fail-closed e probe por RPTR/WPTR estão testados offline | A DDI não foi chamada na rota TDR medida: **NÃO VALIDADO EM HARDWARE** |
| Node 1 compute WDDM | 🟨 | Dois contextos Node 1 foram criados | Zero submissão Node 1 medida |
| UMD D3D10/11 | ⬜ | Handshake KMD/UMD e negociação do runtime foram medidos | Falta pipeline 3D real; `D3D11DDICAPS_3DPIPELINESUPPORT` não pode ser fingido |
| Vulkan / RADV | 🟨 | ICD explícito enumera e entra em `vkCreateDevice` | `vkCreateDevice` não retorna; winsys/queue/submit Node 1 incompletos |
| D3D12 / jogos | ⛔ | Arquitetura via Vulkan/VKD3D definida | Depende de Vulkan graphics + WSI estáveis |

## Caminho crítico

```text
Fundação KMD ✅
   └─> Node 1 + residency/paging completos
          └─> winsys RADV sobre BC250_ALLOCATION_PRIVATE_V1
                 ├─> Vulkan compute → graphics → WSI
                 └─> DXBC → SPIR-V → RADV/ACO → D3D10/11 → DWM visível
                                      └─> TDR com display visível
                                             └─> DXVK + VKD3D → jogos → long-run

DCN v2 (mode-set, HPD, VRR, HDR) ── opcional; não bloqueia jogos
```

## Checklist mestre

### 0 — Fundação e segurança

| Check | Entrega | Estado | Já feito | Falta / teste que fecha | Evidência |
|:---:|---|:---:|---|---|---|
| [x] | Identidade e inventário do silício | ✅ | `1002:13FE`, GC 10.1.3, 24 CUs e 16 GB GDDR6 identificados | — | Tabela 1 da base |
| [x] | Toolchain reproduzível | 🟦 | VS2026/MSVC 19.51 + SDK/WDK 28000; KMD, UMD e suites compilam | Efeito da migração no silício não precisa de corrida isolada | [`173_TOOLCHAIN_VS2026_WDK28000.md`](173_TOOLCHAIN_VS2026_WDK28000.md) |
| [x] | Ciclo fail-closed e rollback | ✅ | Preflight, fusível anti-boot-loop, watchdog, coleta do log vivo e rollback para BasicDisplay | Manter em toda corrida | [`188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md`](188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md) |
| [x] | Dumps úteis | ✅ | Dumps completos do DWM e LiveKernelDump `0x117` com símbolos | Repetir somente quando uma nova hipótese exigir | `evidence/2026-09-07_21-49-03-DESKTOP-HQAG6T4-mcdm-display-binding/` |

### 1 — KMD, memória e submissão WDDM

| Check | Entrega | Estado | Já feito | Falta / teste que fecha | Evidência |
|:---:|---|:---:|---|---|---|
| [x] | Miniport em Código 0 | ✅ | `DRIVERCAPS`, `QUERYSEGMENT4`, paging context e VidPn aceitos | — | Corrida 07/09 16:19, [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md) |
| [x] | SDMA Node 0 executa IB WDDM real | ✅ | B1: VMID8, fence sobre poison e cinco canários conferidos | — | [`182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md`](182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md) |
| [x] | Remover precedência dos Planos A | ✅ | Default usa `INDIRECT`; 32 submissões sem timeout na corrida seguinte | Manter fallback desligado em produção | [`183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md`](183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md) |
| [ ] | Residency real | ⬜ | Segments e alocações básicas já existem | Exercitar `MakeResident`/`Evict`, conferir accounting, pressão e ausência de leak | Nova pasta `docs/evidence/<data>_B5_RESIDENCY/` |
| [ ] | Paging WDDM completo | 🟨 | IB de paginação sob VMID8 fechou o tripé | Provar map, unmap, rollback e teardown do VidMm no mesmo bind | Nova pasta `docs/evidence/<data>_P6_PAGING_LIFECYCLE/` |
| [ ] | Root/journal GPUVA por processo | 🟨 | Root SegmentId 1/Offset 0, generation e snapshots Node 0 observados | Concorrência, invalidação e teardown sem faixa órfã | `gpuva_journal_contract_test` + prova física em `P6_GPUVA_LIFECYCLE` |
| [ ] | Submit Node 1 pelo scheduler | ⬜ | Contextos Node 1 existem; gate/telemetria foram corrigidos offline | Um command buffer Node 1 deve fechar RPTR/WPTR, fence sobre poison e bytes | `test-radv-wddm-compute-tripod.ps1`; `docs/evidence/<data>_B4_NODE1_TRIPOD/` |
| [ ] | Sync/retirement Node 1 | ⬜ | Caminho MEC direto existe | Fence do Node 1 deve chegar ao scheduler por ISR/DPC, sem escrita CPU | Mesma evidência de B4, com contadores Submitted/Completed |
| [ ] | Multi-context e isolamento | ⬜ | Ciclo de processos já invalida GPUVA morta | Dois processos/contexts simultâneos sem ler ou escrever a faixa do outro | `docs/evidence/<data>_P6_MULTI_CONTEXT/` |
| [ ] | Preemption/cancel/prioridades | ⬜ | Contratos parciais existem | Preempt e cancel reais com fences coerentes; prioridade observável | `docs/evidence/<data>_P6_PREEMPT_CANCEL/` |
| [ ] | Wrap e concorrência de rings | 🟨 | Bug de RPTR além de uma volta foi corrigido e uma corrida posterior teve zero timeout | Stress forçando múltiplos wraps e submissões concorrentes sem sobrescrever trabalho vivo | `docs/evidence/<data>_P6_RING_STRESS/` |

### 2 — Display herdado do GOP

| Check | Entrega | Estado | Já feito | Falta / teste que fecha | Evidência |
|:---:|---|:---:|---|---|---|
| [x] | Ownership físico / modo único | ✅ | Timing do GOP preservado; VidPn converge no perfil de produção | — | [`26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md`](26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md) |
| [x] | Primary e Present por SDMA | ✅ | CommitVidPn, SetVidPnSourceAddress e Presents `SDMA=YES` medidos | A cópia física está provada; conteúdo útil depende da UMD | [`179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md`](179_HANDOFF_2026-09-07_16-19_CODIGO0_DWM_D3D11.md) |
| [x] | VSync físico | ✅ | Frame counter do OTG ligado ao `dxgkrnl` | — | Tabela 1 da base |
| [ ] | Desktop visível | ⛔ | A tela preta foi atribuída ao fail-fast D3D11, não ao link DP | Implementar B7 e manter DWM visível por 10 minutos | `docs/evidence/<data>_B7_DWM_VISIBLE_10MIN/` |
| [ ] | DirectFlip | ⬜ | Declarado honestamente como não suportado | Implementar quando o blt + DWM estiverem estáveis; provar flip sem corrupção | `docs/evidence/<data>_DIRECTFLIP/` |
| [ ] | Convergência do perfil lab | 🟨 | Produção converge; perfil lab não chegava a primary/commit | Só investigar se continuar necessário; não bloqueia B7 | `docs/evidence/<data>_P5_LAB_VIDPN/` |

### 3 — TDR e robustez

| Check | Entrega | Estado | Já feito | Falta / teste que fecha | Evidência |
|:---:|---|:---:|---|---|---|
| [x] | Hang real detectado pelo Windows | ✅ | `POLL_REGMEM` pendente gerou LiveKernelDump `0x117` | — | [`188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md`](188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md) |
| [x] | Quiesce/reset/restart físico | ✅ | Soft reset SDMA0, reconstrução, readback e NOP/fence física | — | Mesmo handoff 188 |
| [x] | Readmissão após TDR | ✅ | 40 IBs `INDIRECT`, quatro tripés completos e Código 0 até rollback | — | Mesmo handoff 188 |
| [ ] | `QueryEngineStatus` exercitada | 🟦 | Probe de duas passagens e normalização de wrap existem offline | Criar rota controlada em que o dxgkrnl chame a DDI e arquivar `Responsive=0/1` medido | `docs/evidence/<data>_ITEM24_QUERY_ENGINE_STATUS/`; **NÃO VALIDADO EM HARDWARE** |
| [ ] | Display visível sobrevive ao TDR | ⛔ | Engine sobrevive; a tela já estava preta antes do TDR | Depois do DWM visível, repetir uma única vez: hang → reset → novo frame visível | `docs/evidence/<data>_ITEM25_TDR_DISPLAY_SURVIVES/` |
| [ ] | Tirar polling longo de spinlock | ⬜ | TDR impede falso sucesso, mas o tripé pode gastar cerca de 1 s em DISPATCH_LEVEL | Reestruturar espera/retirement e provar sem regressão | `build-contract.bat` + `docs/evidence/<data>_TDR_LATENCY/` |
| [ ] | DbgInfo completo no TDR | 🟨 | Live dump e snapshot básico existem | Fechar `CollectDbgInfo` sem success vazio e com estado útil de cada engine | Novo dump em `docs/evidence/<data>_TDR_DBGINFO/` |

**Regra de economia:** não repetir o hang induzido agora. O próximo TDR físico só tem valor
depois que o B7 produzir imagem real; antes disso ele repetiria uma prova já obtida.

### 4 — B7: backend compartilhado D3D10/11 + Vulkan

| Ordem | Check | Estado | Já feito | Falta / teste que fecha | Evidência |
|---:|---|:---:|---|---|---|
| 1 | Congelar ABI `BC250_ALLOCATION_PRIVATE_V1` | 🟦 | Layout invariante de 40 bytes; documentação formal de ownership, lifecycle e teardown no contrato; helper canônico `Bc250AllocationAbiFormat` integrado no winsys; suíte exaustiva de testes de offsets congelados, erros e bordas | Prova física de alocações no silício em corrida futura | `allocation_abi_contract_test`, `umd_private_contract_test`, `bc250_wddm_winsys_test` (PASS offline) |
| 2 | Completar winsys WDDM comum | 🟦 | Geometria GPUVA em 34 bits; telemetria ENTER/LEAVE; ciclo de vida de alocação com MakeResident/Evict e auto-evicção; primary class token; monitored fence (CPU wait/signal) com fast-path | Prova física de alocações/fences no silício | `bc250_wddm_winsys_test` (PASS offline); falta prova física Node 1 |
| 3 | Fazer `vkCreateDevice` retornar | 🟨 | Loader, instance, physical device e telemetria de fence/DDIs instrumentada; ponto exato do hang classificado como hipótese H | Identificar ponto exato de parada via telemetria na próxima corrida; queue válida, cleanup e erro fail-closed | `docs/evidence/<data>_RADV_CREATE_DEVICE/` |
| 4 | Primeiro submit da UMD no Node 1 | 🟦 | Transação offline completa; candidato lab 3.90.1 assinado/auditado; binder integrado chama o runner sob watchdog, limita o filho a 30 s e proíbe misturar RADV | Fazer boot frio; validar o dry-run elevado; instalar/bindar por decisão do dono e executar no máximo uma corrida física | `umd_node1_tripod_contract_test` + auditoria do pacote/segurança + dry-run do runner (PASS offline); binder corretamente bloqueado por `CLEAN_BOOT_REQUIRED`; `docs/evidence/<data>_B7_UMD_NODE1_TRIPOD/` |
| 5 | Memória e sincronização Vulkan | ⬜ | Heaps poderão derivar dos segments WDDM | Buffers/images, map/unmap, fences, semáforos e barreiras sob pressão | `docs/evidence/<data>_VULKAN_MEMORY_SYNC/` |
| 6 | Compute Vulkan | ⬜ | Compute direto no silício é ativo reutilizável | Shader pelo ICD escreve resultado conferido byte a byte | `bc250_radv_compute_gate` + tripé em `RADV_COMPUTE/` |
| 7 | Graphics RADV/ACO | ⬜ | Hardware GFX10.1.3 e firmware já foram exercitados diretamente | PM4, shaders, descriptors, images, render pass e rasterização reais | Render offscreen com bytes/pixels conferidos |
| 8 | WSI / swapchain | ⬜ | Present blt do KMD já funciona | Imagem Vulkan renderizada chega à primary e aparece no DP da BC-250 | `docs/evidence/<data>_P7_VULKAN_TRIANGLE/` |
| 9 | Parser e validador DXBC | 🟦 | Parser determinístico SM4/SM5, chunks FourCC, bounds-checked, rejeição fail-closed | Validado offline com bateria de testes negativos | Suite offline `dxbc_parser_test` (PASS) |
| 10 | DXBC → SPIR-V / ACO | ⬜ | RADV/ACO será o backend compartilhado | Traduzir estágios e recursos mínimos usados pelo DWM, com golden tests | Suite offline `dxbc_spirv_test` a criar |
| 11 | Device D3D10.1/D3D11 real | 🟦 | Fundação de adapter/device, winsys e 103/103 slots D3D10.1 populados; rota de prova Node 1 existe offline; gate fail-closed (`BC250_D3D10_11_NATIVE_READY=0`) | Implementar semântica real das entradas e conectar DXBC -> SPIR-V -> RADV/ACO; só então anunciar suporte | `bc250_d3d_umd_test` e `umd_node1_submit_contract_test` (PASS offline); falta pipeline 3D real |
| 12 | Pipeline mínimo do DWM | ⬜ | Estruturas de resources/views/state existem como scaffolding; `Draw*` agora rejeita explicitamente com `E_NOTIMPL` e zero progresso fabricado | Upload/lifetime de recursos, tradução DXBC -> SPIR-V, pipeline GFX10 e execução física do draw | DWM visível por 10 min em `B7_DWM_VISIBLE_10MIN` |

**Primeira entrega útil do B7:** um submit Node 1 originado pela UMD com tripé completo.
**Primeira entrega visível:** DWM real por 10 minutos. Carregar DLL, enumerar Vulkan ou
abrir uma janela não fecha nenhum dos dois marcos.

### 5 — APIs para jogos

| Check | Entrega | Estado | Teste que fecha | Evidência esperada |
|:---:|---|:---:|---|---|
| [ ] | Triângulo Vulkan no monitor da placa | ⛔ | Frame gerado pela GPU e apresentado sem cópia/fallback de software | `docs/evidence/<data>_P8_VULKAN_TRIANGLE/` |
| [ ] | DXVK (D3D9/10/11 sobre Vulkan) | ⛔ | Suite relevante e jogo simples, sem WARP/llvmpipe | `docs/evidence/<data>_P8_DXVK_GAME/` |
| [ ] | VKD3D-Proton (D3D12 sobre Vulkan) | ⛔ | Suite relevante e jogo D3D12, com GPU counters/fences | `docs/evidence/<data>_P8_VKD3D_GAME/` |
| [ ] | Zink / OpenGL | ➖ | Só depois de Vulkan graphics/WSI estáveis | `docs/evidence/<data>_P8_ZINK/` |
| [ ] | D3D12 nativo | ➖ | Opcional se VKD3D cumprir a meta de D3D12 | Evidência própria apenas se implementado |

### 6 — Produto: jogos e estabilidade

| Check | Entrega | Estado | Teste que fecha | Evidência esperada |
|:---:|---|:---:|---|---|
| [ ] | Jogo Vulkan real | ⛔ | FPS/frame-time medidos no DP da BC-250 | `docs/evidence/<data>_GAME_VULKAN/` |
| [ ] | Jogo D3D11 real | ⛔ | DXVK ou backend escolhido, sem fallback | `docs/evidence/<data>_GAME_D3D11/` |
| [ ] | Jogo D3D12 real | ⛔ | VKD3D, sem WARP, com fences/counters da GPU | `docs/evidence/<data>_GAME_D3D12/` |
| [ ] | Ausência de fallback | ⬜ | Processo, módulos, counters e fences provam execução na BC-250 | Incluída em cada evidência de jogo |
| [ ] | Compilação/cache de shaders | ⬜ | Warm/cold runs sem corrupção ou travamento | `docs/evidence/<data>_SHADER_CACHE_STRESS/` |
| [ ] | Pressão de memória | ⬜ | Residency/eviction/OOM/fragmentação nos 16 GB, sem leak | `docs/evidence/<data>_MEMORY_PRESSURE/` |
| [ ] | Desktop + jogo + compute | ⬜ | Contextos concorrentes sem starvation/corrupção | `docs/evidence/<data>_MIXED_WORKLOAD/` |
| [ ] | Long-run display | ⬜ | Horas de Present sem frame preso ou corrupção | `docs/evidence/<data>_DISPLAY_LONG_RUN/` |
| [ ] | Long-run render/compute | ⬜ | Horas sem fence perdida, fault ou reset indevido | `docs/evidence/<data>_GPU_LONG_RUN/` |
| [ ] | Suspend/resume e power | ⬜ | Retorno com allocations, display e queues coerentes | `docs/evidence/<data>_POWER_CYCLE/` |
| [ ] | TDR durante imagem/jogo | ⛔ | Hang induzido → reset → novo frame, sem perder display | `docs/evidence/<data>_FINAL_TDR_VISIBLE/` |
| [ ] | **PASS final** | ⛔ | Jogo real na BC-250, D3D12/Vulkan, sem fallback, estável e recuperável | Dossiê final referenciando todas as provas acima |

### 7 — P9 opcional: DCN v2

Nada abaixo bloqueia jogos no desenho vigente por herança do GOP.

| Check | Entrega | Estado | Prova exigida |
|:---:|---|:---:|---|
| [ ] | AUX/DPCD/EDID real | ➖ | Transação física, checksum e capabilities reais |
| [ ] | HPD | ➖ | Estado/interrupção física; a placa tem histórico de HPD quebrado até no Linux |
| [ ] | Link training / PHY | ➖ | CR/EQ, lanes/rate e rollback seguro |
| [ ] | Mode-set transacional | ➖ | Novo modo com snapshot, readback e rollback |
| [ ] | Flip A/B e VBlank por interrupção | ➖ | Surface em uso + frame counter + ISR/DPC |
| [ ] | Multi-monitor, hotplug, VRR, HDR, MPO | ➖ | Um teste físico nomeado para cada recurso |

## Próximas ações, em ordem

1. **Não gastar nova corrida TDR.** A engine já recuperou; falta uma imagem real para a
   perna restante produzir informação nova.
2. Completar o lifetime dos recursos do winsys comum; a ABI e a rota offline de command buffer Node 1 já têm contrato.
3. Por decisão do dono, instalar/bindar o candidato 3.90.1 e fazer o primeiro submit Node 1 originado pela UMD fechar o tripé físico em uma única corrida.
4. Completar `vkCreateDevice`, memória, sync e compute Vulkan.
5. Subir graphics + WSI até um triângulo visível.
6. Em paralelo sobre o mesmo backend, construir DXBC → SPIR-V e as DDIs D3D10/11 mínimas
   reais até o DWM ficar visível por 10 minutos.
7. Só então repetir o hang induzido para fechar o item 25 inteiro.
8. Levar Vulkan a DXVK/VKD3D, executar jogos e cumprir os testes de estabilidade.

## Critério de conclusão da meta

Todos os checks de **Produto** necessários ficam `[x]`, com artefatos sob `docs/evidence/`,
e a evidência conjunta demonstra:

- frame produzido pela BC-250 e exibido no DisplayPort da própria placa;
- jogo Vulkan e jogo D3D12 via VKD3D, com D3D11 coberto pelo backend/DXVK;
- ausência de WARP, llvmpipe e qualquer fallback silencioso;
- fences, counters e bytes/pixels vindos da GPU;
- TDR recuperando uma engine travada e retomando frames sem perder o display.

Até isso acontecer, a meta global permanece **ABERTA**.
