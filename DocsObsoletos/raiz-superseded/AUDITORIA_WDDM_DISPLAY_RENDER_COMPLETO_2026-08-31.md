# Auditoria e plano mestre — BC-250 WDDM 2.x display + render

**Data da auditoria:** 31/08/2026  
**Alvo:** AMD BC-250 `1002:13FE`, GC 10.1.3 / Cyan Skillfish 2, Windows 11 x64  
**Meta definida pelo proprietário:** um adaptador WDDM display+render de uso geral, com display acelerado, Vulkan real, jogos via DXVK/VKD3D-Proton/Zink e computação real para LLMs.

> Este documento substitui, para planejamento de produto, a antiga suposição de que MCDM compute-only seria o destino final. As regras de prova física, fail-closed, snapshot/readback/rollback e proibição de resultados simulados continuam válidas porque são requisitos de segurança e de qualidade, não limitações de produto.

## 1. Veredito executivo

O repositório contém uma base de laboratório excepcionalmente útil e uma prova real de execução compute no MEC/HQD, mas **não contém hoje um driver gráfico utilizável**. O salto restante não é “ligar o RADV” nem completar algumas entrypoints: é construir as partes de produção que, no Linux, são divididas entre `amdgpu`/DRM, scheduler, TTM, VM, KFD, DC/DCN/DMUB, firmware, Mesa/RADV/ACO e WSI.

Estado honesto em uma frase:

> Há submissão compute controlada e repetível no silício, e há um caminho display-only por framebuffer GOP; ainda não há submissão de aplicações pelo WDDM, UMD Vulkan funcional, graphics queue, apresentação acelerada, display engine owned, isolamento multiprocesso, paging robusto, interrupções de conclusão ou TDR real. Tudo isso permanece **NÃO VALIDADO EM HARDWARE**.

Consequências imediatas:

- DXVK, VKD3D-Proton e Zink **não podem funcionar** sobre o ICD atual.
- O ICD local não é RADV e não executa Vulkan: `vkQueueSubmit` devolve `VK_ERROR_DEVICE_LOST` deliberadamente.
- O RADV externo testado não chegou ao miniport: as evidências `v2-wddm-submission` mostram delta zero de submissions/fences/interrupts.
- O “display” atual não é acelerado: é framebuffer herdado do GOP, com present display-only copiado pela CPU.
- A prova P2 de 16 submissões é válida para o harness e o estado controlado correspondente; ela não prova WDDM, Vulkan, LLM, graphics, concorrência, paginação ou display acelerado.
- A meta é tecnicamente possível como programa de engenharia, mas o esforço remanescente é comparável a portar/adaptar uma pilha de driver, não a finalizar um protótipo quase pronto.

## 2. Método e regra de evidência

Foram auditados os diretórios `driver/core`, `driver/wdk`, `driver/umd`, `driver/tests`, `dbg`, `inc`, `tools`, a documentação histórica, os manifests/INFs, as evidências recentes e o build atual. `Vulkan-Samples`, `studio` e árvores de referência foram tratados como dependências/referências, não como implementação do driver.

Cada item usa uma destas classes:

| Classe | Significado |
|---|---|
| **HWP** | Provado no silício com RPTR/WPTR, fence GPU com poison check, dados e teardown coerentes. |
| **HW parcial** | Houve observação física útil, mas falta parte do gate completo. |
| **Offline** | Código/teste estrutural compila ou passa com transporte fake; não prova hardware. |
| **Stub/fail-closed** | Interface existe, mas recusa ou não executa a operação. |
| **Ausente** | Não existe implementação relevante no repositório. |

Build executado nesta auditoria: `driver/wdk/build-contract.bat`, retorno 0. O próprio build reportou `semantic/hardware validation: NOT PROVEN`, `STRUCTURAL ONLY` e `NÃO VALIDADO EM HARDWARE`. Foram observados warnings em ferramentas e no link do teste UMD, listados como dívida técnica.

## 3. O que foi realmente conquistado

### 3.1 Linha do tempo técnica condensada

| Marco | Evidência/implementação | Estado honesto |
|---|---|---|
| Recuperação e bind transacional | Scripts de bind, quarentena, watchdog, rollback e retorno ao BasicDisplay | Forte base operacional; ainda não elimina risco de tela preta durante ownership real de DCN. |
| Inventário BAR/VRAM/IP/TMR | Contratos, probes e leituras físicas documentadas | HW parcial/HWP por subcomponente. |
| GCVM inicial | VMID 8 inerte, PTE, TLB e PTE `0x10` para fila | HWP no escopo estreito; sem GPUVA dinâmica por processo. |
| PSP/firmware | Ring GPCOM, ACK estrito, staging, SDMA/RLC e direct-load CP | HWP nos fluxos documentados. |
| SDMA | Ring, RPTR==WPTR, fence física, dados e rollback | HWP para operação controlada. Não é ainda DMA WDDM multiprocesso. |
| CP/PM4 | Ring e pacotes controlados, fence e conferência de dados | HWP no harness. |
| MEC/HQD one-shot | Shader observável, resultado e fence físicos | HWP. |
| Fila persistente P1 | `RPTR=WPTR=12`, shader/fences e zero mismatch | HWP no candidato 3.54. |
| Multi-submissão P2 | 16/16 aceitas e aposentadas; `RPTR=WPTR=204`; 17 fences; zero mismatch | HWP no pacote/evidência válida. |
| Callback WDDM | Tabelas estruturais e contratos para DDIs | Offline; muitas semânticas ainda recusam ou são placeholders. |
| Display WDDM | VidPn de um modo GOP e `PresentDisplayOnly` | Display-only por CPU; não é render/full WDDM acelerado. |
| Runtime privado | DLL/IOCTLs para alloc/copy/dispatch | Compila; não há aceite end-to-end de aplicação real. |
| ICD Vulkan local | Loader/handles e subconjunto de entrypoints | Stub/fail-closed; nenhuma submissão Vulkan. |
| RADV/LLM externo | Tentativas registradas | Reprovado: RADV não submete pelo WDDM; benchmark não prova uso de GPU. |

### 3.2 Prova P2 que pode ser reutilizada

A evidência canônica válida em `docs/evidence/2026-08-31_12-07-15-DESKTOP-HQAG6T4-p2-multi-submission/` registra:

- `engine_state=1`;
- 16 submissions aceitas e 16 aposentadas;
- `WPTR=RPTR report=RPTR MMIO=204`;
- zero divergências;
- fence final 17;
- `p2_triad_passed=1` e `p2_physical_ready=1`.

Isso prova que a receita de firmware, fila MEC, PM4 compute, endereçamento físico usado naquele ensaio e coerência são uma semente funcional. Ela deve virar backend do scheduler WDDM, não permanecer como self-test de `StartDevice` ou IOCTL privilegiado.

### 3.3 Evidências que não devem ser promovidas

Existem capturas posteriores de P2 com contadores antigos ainda visíveis, mas `engine_state=0`, dispositivo fora de `bc250mcdm / Código 0` e veredito `NÃO VALIDADO EM HARDWARE`. Elas não são novas execuções válidas.

As capturas `v2-wddm-submission` de 12:16, 12:50 e 13:06 mostram:

- benchmark terminando ou falhando sem qualquer delta de submission;
- `V2TrackerPushedTotal`, `V2NotifiedTotal`, DPC, interrupts e fences todos com delta zero;
- veredito explícito `RADV NAO SUBMETE PELO WDDM`.

Portanto, nenhum resultado de `llama-bench` dessas rodadas pode ser atribuído à BC-250.

## 4. Matriz completa: temos × falta

### 4.1 Kernel, WDDM e confiabilidade

| Subsistema | O que existe | O que falta para produção | Estado |
|---|---|---|---|
| PnP/Start/Stop | Add/Start/Stop, BAR mapping, inventário e teardown | remover self-tests destrutivos do start; suportar restart, surprise removal e concorrência real | HW parcial |
| Modelo WDDM | registration e matriz de callbacks | escolher e fixar WDDM alvo; capabilities verdadeiras; todos os contratos exigidos pelo perfil display+render | Offline |
| Objetos | processos/devices/contexts/allocations modelados | ownership por processo, refcount/rundown, handles resistentes a races, quotas e cleanup após crash | Offline |
| Segmentos/VidMm | contratos local/GART e PTE encoder | allocations reais do VidMm, residency, eviction, paging buffers, map/unmap GPUVA, budget e overcommit | Offline/HW estreito |
| GPUVA | PTE isolada e VMID fixo/provas controladas | VA 48-bit por processo, VMID allocator, page faults, invalidation por range, proteção entre processos | Ausente em produção |
| Scheduler | fila persistente de laboratório | queues por contexto, prioridades, fair scheduling, dependencies, backpressure e retirement WDDM | HW estreito |
| Fences | fences físicas no harness | monitored fences, `DxgkCbNotifyInterrupt`, DPC retirement, ordering e rollover | Ausente em produção |
| IH/interrupts | parser/contrato offline | ring IH real, MSI/MSI-X/line interrupt, ISR mínima, DPC, storm control e source validation | Offline |
| Preempção | contrato offline | preempção física SDMA/compute/GFX com fence e timeout | Offline |
| TDR/reset | contrato e funções expostas | reset de queue/engine/adapter real; state capture; restart seguro; fault injection | Stub/offline |
| Power | contrato | D0/D3, idle, GFXOFF, clocks, thermal limits, suspend/resume e hibernate validados | Offline |
| Segurança | allowlists e ISA validator inicial | validar todo command stream, relocations, register classes, addresses, descriptors e shader resources | Parcial |
| Telemetria | schema amplo e provas | ETW/WPP, correlação por PID/context/fence, dumps WinDbg e ABI versionada | Parcial |

### 4.2 Display e apresentação

| Item | Estado atual | Necessário para “como uma GPU de PC” |
|---|---|---|
| POST display ownership | framebuffer físico do GOP é mapeado | inventário/ownership transacional de DCN/DCE/DMUB e estado ATOM/firmware |
| VidPn | uma source/target, um modo sintetizado | EDID real, hotplug, vários modos/refresh, topology, clone/extend e mode set |
| `SetVidPnSourceAddress` | `STATUS_NOT_SUPPORTED` | flip real para allocation scanout, pin/residency e sincronização no vblank |
| `Present` render path | `STATUS_NOT_SUPPORTED` | patch/render/submit, flip/blit e fence real |
| `PresentDisplayOnly` | cópia CPU para framebuffer GOP | apenas fallback/recovery; não deve ser chamado “aceleração” |
| VSync | timer/DPC sintético do caminho display-only | interrupção vertical real da display engine |
| Cursor | cursor visível/shape recusados | cursor plane de hardware, hotspot, formatos e atualização atômica |
| Planes/overlays | ausentes | primary/overlay planes, scaling, color space, rotation e protected paths conforme escopo |
| Áudio HDMI/DP | ausente | codec/endpoint, ELD e sincronização se desejado |
| HDR/color | ausente | gamma/LUT, colorspaces/HDR metadata; pode ser fase tardia |
| Multi-monitor/hotplug | ausente | HPD/AUX/DDC, link training, MST se o hardware exigir |

**Conclusão de display:** manter o GOP aceso não é possuir a display engine. A tomada real do display exigirá estudar o IP físico presente na BC-250, o VBIOS/ATOM e o estado deixado pelo firmware/Linux. Não há hoje no código uma implementação DCN/DMUB/DCE suficiente.

### 4.3 Graphics core

| Item | Temos | Falta |
|---|---|---|
| Firmware/RLC/CP | bring-up e PM4 controlado | lifecycle permanente, várias rings e reset robusto |
| Compute shaders | blob allowlisted conhecido | compilação geral, ABI de shader, user data, descriptors, scratch e traps |
| GFX ring | PM4 controlado em laboratório | graphics queue WDDM real e isolamento por contexto |
| Pipeline graphics | nenhum pipeline completo | VS/PS/GS/HS/DS quando aplicável, state objects, draws e indirect draws |
| Raster/backend | apenas headers de referência | CB/DB, render targets, depth/stencil, blending, MSAA, compression/decompression |
| Recursos | buffers experimentais | images, tiling, swizzle, metadata, DCC/HTILE/CMASK/FMASK e format table |
| Cache/coerência | sequências provadas no harness | modelo completo de barriers, acquire/release, aliases e ownership transitions |
| Queries | ausentes | timestamp, occlusion, pipeline stats e calibrated timestamps |

### 4.4 Vulkan/UMD

| Bloco Vulkan | Estado atual | Necessário |
|---|---|---|
| Loader/manifest | esqueleto local | conformance de loader, dispatch tables completas, versionamento 32/64-bit |
| Instance/device | handles locais simplificados | validação completa de `pNext`, extensions, alloc callbacks e threading |
| Memory | malloc/espelho host e IOCTL experimental | BOs VidMm/D3DKMT, heaps/types reais, map/flush/invalidate e residency |
| SPIR-V | bytes guardados | frontend/validator + compilador para ISA GFX10.1 (preferencialmente ACO/RADV) |
| Descriptors | ausentes | layouts, pools, sets, descriptor buffers/tables e user SGPR ABI |
| Pipelines | objetos simplificados | pipeline layouts/cache, compute e graphics completos |
| Commands | grava apenas bind/grid | command graph real, barriers, copies, draws, dispatches e secondary buffers |
| Sync | fences host não ligadas à GPU | timeline/binary semaphores, monitored fences e external handles conforme necessidade |
| Submit | sempre `VK_ERROR_DEVICE_LOST` | submissão WDDM real, retirement e propagação de device loss |
| Images/formats | recusados | formatos, layouts, tiling/modifiers internos, views, samplers e render targets |
| WSI | surface support recusado | Win32 surface, swapchain, acquire/present, present modes e resize/out-of-date |
| Features/extensions | quase nenhuma | anunciar somente o que passou CTS; matriz por consumidor e versão pinada |

## 5. Estratégia recomendada para RADV no Windows

### 5.1 O que “usar RADV” realmente significa

RADV fornece uma parte enorme do que falta no UMD: Vulkan, ACO/NIR, descriptors, pipelines, command buffers, images, barriers e conhecimento GFX10. Porém seu caminho usual pressupõe interfaces Linux/DRM/amdgpu para BO, VM, command submission e sync. Colocar `radv*.dll` ao lado do executável não cria essa camada no Windows.

A estratégia de maior reaproveitamento é:

1. Fixar uma revisão Mesa/RADV conhecida e importar apenas a dependência necessária, com licença e provenance preservadas.
2. Criar um **winsys Windows BC-250** que traduza as operações RADV de buffer object, VA, residency, CS submit, syncobj/fence, query/reset e device info para D3DKMT/WDDM.
3. Manter compilação NIR/ACO e command generation do RADV o mais upstream possível.
4. Adaptar WSI Win32 para swapchain/present do novo KMD, sem atalhos Linux/Wine falsos.
5. Alimentar o winsys apenas com capabilities fisicamente verdadeiras do KMD.

Alternativa — escrever um ICD do zero — deve ser reservada a um ICD compute mínimo de diagnóstico. Para jogos, replicaria anos de trabalho do RADV e aumenta muito o risco de bugs de shader, formatos e sincronização.

### 5.2 Contrato KMD ↔ winsys que deve existir

- descoberta do adapter/LUID e abertura D3DKMT;
- criação de device/context por engine;
- criação/import/export de allocations;
- reserva/map/unmap de GPUVA e atualização de page tables pelo fluxo WDDM correto;
- make resident/evict e budget notifications;
- command allocation/patch/submit ou submit virtual, conforme o modelo escolhido;
- monitored fence por context/node e wait eficiente;
- query de reset/device removed e fault info;
- calibração de timestamp;
- present/flip de allocations scanout;
- ABI versionada, tamanho de structs, feature bits e rejeição fail-closed.

## 6. DXVK, VKD3D-Proton e Zink

Essas camadas são consumidores do Vulkan; não substituem o driver Vulkan.

| Camada | Valor | Pré-condição real |
|---|---|---|
| DXVK | D3D8/9/10/11 sobre Vulkan, conforme build/configuração | Vulkan graphics robusto, WSI, descriptors, formats, sync e extensões exigidas pela versão pinada |
| VKD3D-Proton | D3D12 sobre Vulkan | conjunto ainda mais exigente de features, descriptors, memory model, sync e shader compiler |
| Zink | OpenGL sobre Vulkan | Vulkan graphics/WSI estável e features/extensões requeridas pela revisão Mesa escolhida |

Ordem recomendada de habilitação:

1. Vulkan compute próprio e CTS do subconjunto;
2. Vulkan graphics: triangle → textures → depth/blend → render targets;
3. WSI/swapchain e aplicações Vulkan nativas simples;
4. Zink básico e suíte OpenGL;
5. DXVK com D3D9 simples, depois D3D11;
6. VKD3D-Proton somente após Vulkan moderno e sincronização madura.

Cada projeto deve ter versão/commit pinado e uma matriz automática das features/extensões realmente consultadas. “Abrir o jogo” não basta: capturar submissions, fences, pixels/resultados e ausência de fallback.

## 7. Plano mestre por gates

### Fase 0 — Congelar a verdade e limpar falsos positivos

**Objetivo:** uma baseline reproduzível antes de novas mudanças.

- versionar separadamente código, evidências válidas e artefatos gerados;
- definir a evidência P2 canônica e marcar capturas stale/inválidas;
- corrigir documentos que chamam ICD/LLM de implementados;
- adicionar `capability_status.json`: `offline`, `hardware_partial`, `hardware_proven`, `disabled`;
- proibir capabilities positivas sem teste físico correspondente;
- tornar build `/W4 /WX` também para ferramentas/UMD;
- fixar versões WDK, Windows, firmware, Mesa/RADV e toolchain.

**Gate:** clean clone constrói os mesmos hashes, todos os testes offline passam e nenhuma feature não provada é anunciada.

### Fase 1 — Recovery para testes sem medo razoável de tela preta

Risco zero não pode ser prometido quando um KMD controla GPU/display. O objetivo correto é recuperação determinística.

- segundo adaptador/saída independente ou acesso remoto/serial obrigatório durante bring-up DCN;
- duas entradas de boot: laboratório e estável;
- watchdog externo que detecta ausência de heartbeat e restaura BasicDisplay/driver anterior;
- pacote last-known-good assinado e offline;
- bind por boot, nunca hot-swap arbitrário com engine suja;
- crash dump kernel completo, Driver Verifier seletivo e símbolos;
- fail injection em cada estágio e restore verificado;
- desligar automaticamente o caminho novo após N falhas de boot.

**Gate:** 100 ciclos bind/reboot/rollback, falhas injetadas em cada estágio, sempre recuperáveis sem reinstalar o Windows.

### Fase 2 — Transformar o MEC provado em scheduler WDDM real

- retirar o batch P2 do self-test e encapsular ring/queue como objeto por adapter/context;
- implementar monitored fences e retirement por IH→ISR→DPC→Dxgkrnl;
- `CreateProcess/Device/Context`, submit, destroy e cancel realmente ligados ao backend;
- backpressure, wraparound, rollover e ordenação de fences;
- preempção e reset físico da queue;
- zero polling de user mode como mecanismo de conclusão de produção.

**Gate:** 10.000 dispatches via D3DKMT/WDDM, 2+ processos, wrap, prioridades, cancel/crash de processo e fences físicas corretas.

### Fase 3 — VidMm/GPUVA/residency de produção

- allocations reais local/system, GPUVA por processo e VMIDs isolados;
- paging engine SDMA, transfer/fill/discard e page-table update;
- make resident/evict, budgets e pressão de memória;
- page fault capture/recovery e acesso inválido bloqueado;
- staging H2D/D2H sem tratar ponteiro de user mode como endereço GPU;
- teardown sem DMA-after-free.

**Gate:** stress de alocação acima da memória disponível, eviction/re-residency, processos hostis e Driver Verifier sem corrupção.

### Fase 4 — KMD render completo mínimo

- node GFX e command submission real;
- parser/validator/patching seguro ou submit virtual devidamente protegido;
- contexts de GFX, compute e SDMA;
- preemption/reset por node;
- timestamp/query e faults;
- primeiro draw PM4 controlado em render target offscreen.

**Gate:** triângulo offscreen produzido pela GPU, hash/pixels conferidos, fence física, múltiplos contexts e reset após hang injetado.

### Fase 5 — Winsys RADV/ACO para Windows

- portar device info correto de GC 10.1.3/Cyan Skillfish;
- implementar BO/VA/residency/CS/fence/reset no winsys WDDM;
- validar ACO ISA e ABI contra shaders conhecidos;
- compute Vulkan real primeiro, depois graphics offscreen;
- remover/desativar o ICD local de fachada para não mascarar testes.

**Gate:** `vulkaninfo` coerente, compute sample com prova física, triangle offscreen e validation layers limpas; nenhum fallback.

### Fase 6 — Vulkan graphics funcional

- descriptors, samplers, images/views, formats e tiling;
- pipeline state e shader stages;
- color/depth targets, blend, depth/stencil, MSAA básico;
- barriers/cache flushes, copies/blits/resolves;
- pipeline cache e shader cache;
- CTS incremental por feature anunciada.

**Gate:** conjunto crescente de Vulkan CTS com allowlist explícita; renderdoc/apitrace se suportável; testes negativos de corrupção e device loss.

### Fase 7 — WSI e apresentação acelerada mantendo GOP

- Win32 surface/swapchain;
- allocations scanout/presentable;
- acquire/present sem cópia CPU;
- inicialmente manter mode set do GOP e trocar apenas scanout/flip se o hardware permitir;
- vblank real e frame pacing;
- resize, fullscreen/windowed, alt-tab e device lost.

**Gate:** aplicação Vulkan nativa apresenta milhares de frames com flip GPU, captura de vblank/fence e sem `PresentDisplayOnly` CPU.

### Fase 8 — Ownership real da display engine

- identificar exatamente DCN/DCE/DMUB/SMU/ATOM presentes;
- importar/adaptar somente registradores e sequências comprovadas do Linux/firmware;
- DDC/AUX/EDID/HPD, link training e mode set;
- source address, vblank, cursor e planes;
- power transitions e restore do estado de boot;
- depois multi-mode, multi-monitor, áudio e HDR.

**Gate:** boot frio, reboot, monitor disconnect/reconnect, mudanças de modo/refresh, sleep/resume e 24h de scanout sem tela preta não recuperável.

### Fase 9 — Camadas de compatibilidade para jogos

- pin de DXVK e jogos D3D9 simples;
- DXVK D3D11 e suites de conformidade;
- Zink + Piglit/dEQP OpenGL;
- VKD3D-Proton + testes D3D12 apenas quando as features Vulkan existirem;
- profiles por jogo só após corrigir o comportamento no driver, sem mentir capabilities.

**Gate:** jogos e testes selecionados com frame output correto, estabilidade, frametime, ausência de fallback e captura de submissions/fences.

### Fase 10 — LLMs reais

- primeiro usar Vulkan compute já provado;
- rodar backend Vulkan upstream do `llama.cpp` em commit pinado;
- validar kernels, descriptors, push constants e memory pressure;
- correlacionar cada token a submissions/fences e atividade física;
- comparar logits/tokens/tolerância e medir throughput/energia;
- proibir fallback silencioso nas camadas declaradas offloaded.

**Gate:** modelo pequeno e depois modelos maiores, `-ngl` efetivo, resultados comparáveis ao CPU e teste negativo que falha quando a queue é bloqueada.

### Fase 11 — Produto

- 32/64-bit conforme consumidores;
- instalador, upgrade/downgrade e cleanup de ICDs;
- ETW, dumps, símbolos e diagnóstico de campo;
- HLK/WHQL/attestation conforme distribuição pretendida;
- soak 24/72h, suspend/resume, multi-app e thermal throttling;
- política de compatibilidade KMD/UMD e rollback automático.

## 8. Débitos técnicos encontrados antes de começar

### P0 — bloqueadores/falsos positivos

1. **Documentação contraditória:** `STATUS_E_ROADMAP_CONSOLIDADO.md` declara ICD Vulkan e kernels LLM como implementados, mas o código recusa `vkQueueSubmit` e a evidência diz que RADV não submete. Corrigir antes de usar o documento como status.
2. **ICD local é fachada de objetos host:** várias funções apenas alocam structs/memória CPU; begin/end retornam sucesso sem gravar comandos. Ele deve permanecer desinstalado ou anunciar incompatibilidade até haver backend real.
3. **`bc250_submit_ib` ignora `ib_gpu_va`:** a função recebe VA/quantidade, mas envia um request com `ShaderCodeSize` e não transporta a VA do IB. O nome/contrato é enganoso.
4. **Cópias H2D/D2H usam ponteiro de processo como `SourceGpuVa`/`DestinationGpuVa`:** isso não é um contrato seguro de DMA. É obrigatório pin/map/probe ou staging controlado pelo KMD.
5. **Polling de fence usa o mesmo IOCTL de dispatch com request vazio:** pode ter efeitos laterais e não é um wait/query ABI confiável.
6. **Evidência stale:** contadores persistidos podem sobreviver ao driver ativo e produzir aparência de sucesso. Todo aceite deve exigir hash, device/code, boot/session ID, monotonic submission nonce e deltas positivos.
7. **INF fonte está desatualizado:** versão/schema/tunables do `driver/wdk/package/bc250mcdm.inf` não representam os pacotes/evidências recentes. Há risco de build/pacote híbrido.

### P1 — kernel/display

1. `DxgkDdiSetVidPnSourceAddress`, `DxgkDdiPresent`, cursor shape e vários caminhos retornam `STATUS_NOT_SUPPORTED`.
2. `PresentDisplayOnly` faz `RtlCopyMemory` da CPU para o framebuffer; isso é fallback, não aceleração.
3. EDID é sintetizado como “BC-250 GOP”; não há leitura real DDC/AUX/hotplug.
4. VSync do display-only é baseado em timer, não interrupção física.
5. `ResetFromTimeout`/`RestartFromTimeout` e caminhos de reset/preempção não têm backend físico completo.
6. IH, monitored fence e notification WDDM ainda não aposentam submissions de aplicações.
7. Muitos testes unitários verificam intencionalmente `STATUS_NOT_SUPPORTED`; eles garantem fail-closed, não prontidão.
8. O serviço compute paralelo por IOCTL direto não é substituto seguro de um render UMD/WDDM multiprocesso.

### P1 — UMD/Vulkan

1. Header Vulkan local reimplementado manualmente aumenta risco de ABI/struct/enum incorretos; usar headers oficiais pinados.
2. Falta validação de handles, lifetime, threading e callbacks de alocação.
3. Falta SPIR-V→ISA; palavras SPIR-V não são ISA AMD.
4. Faltam descriptors, pipeline layouts, resource binding e a maior parte do core Vulkan.
5. Fences/semaphores não estão conectados a monitored fences da GPU.
6. O manifest/installer pode interferir com outros ICDs; testes devem usar ambiente isolado e rollback.
7. `vkQueueWaitIdle`/`vkDeviceWaitIdle` retornam sucesso porque nenhum trabalho é aceito; isso é coerente com fail-closed, mas não é funcionalidade.

### P2 — qualidade/build/repositório

1. Worktree contém grande volume de alterações e evidências não versionadas; impossível reproduzir uma release sem congelamento.
2. O build atual emite warnings: variável não usada em `bc250-compute-cli.c`, signed/unsigned em `bc250-gpuva-sdma-gate.c`, `fopen` inseguro e warnings LNK4217 no teste UMD.
3. Artefatos binários, logs e fontes grandes estão misturados; separar source, generated, third-party e evidence.
4. Não há CI limpa demonstrada para KMD, UMD, packaging, static analysis e tests.
5. Percentuais históricos misturam “contrato escrito”, “compila”, “observado” e “provado”; substituí-los por gates binários e cobertura.

## 9. Arquitetura-alvo proposta

```text
Aplicações Vulkan / DXVK / VKD3D-Proton / Zink / llama.cpp
                         |
               RADV + ACO/NIR (UMD Windows)
                         |
               BC-250 RADV winsys WDDM
                         |
       D3DKMT / Vulkan WSI Win32 / monitored fences
                         |
          KMD WDDM display + render BC-250
       +-----------+-----------+-----------+
       | GFX/CP    | MEC       | SDMA      |
       | scheduler | scheduler | paging    |
       +-----------+-----------+-----------+
              VidMm/GPUVA/IH/TDR/power
                         |
             DCN/DCE/DMUB + scanout/HPD
                         |
                     BC-250
```

O `bc250dbg.sys` continua como laboratório separado e somente é usado em boots de diagnóstico. O produto não deve depender dele nem do compute service de IOCTL direto para funcionar.

## 10. Gates obrigatórios e definição de “100%”

Não usar um percentual único. “Driver similar ao Linux para jogos” só pode ser declarado quando todos os gates abaixo estiverem verdes:

| Gate | Critério mínimo |
|---|---|
| Compute físico | dispatches gerais via WDDM, não self-test, com dados/fence/isolamento |
| Graphics físico | draw offscreen completo e conferido |
| Memória | residency/eviction/GPUVA/page faults multiprocesso |
| Sync | IH + monitored fences + preemption/TDR |
| Vulkan | CTS por todas as capabilities anunciadas |
| WSI | swapchain/present/vblank real sem CPU-copy |
| Display | modeset/hotplug/suspend/recovery físicos |
| Compatibilidade | DXVK/Zink/VKD3D em matrizes pinadas |
| LLM | offload real, resultados e prova negativa anti-fallback |
| Robustez | soak, verifier, crash/recovery e upgrade/downgrade |
| Distribuição | pacote assinado, instalável e recuperável |

## 11. Próxima sequência concreta

Antes de escrever mais Vulkan ou display:

1. corrigir os sete débitos P0;
2. congelar um pacote P2 canônico reproduzível;
3. implementar IH + monitored fence + retirement WDDM;
4. submeter o mesmo shader comprovado por um cliente D3DKMT real e obter delta físico;
5. implementar GPUVA/residency por processo;
6. só então iniciar o winsys RADV.

Em paralelo de planejamento, deve-se capturar no Linux a topologia completa de display, ATOM/VBIOS, DCN/DCE/DMUB, IRQs, clocks e transições de mode set — apenas leitura — para dimensionar a Fase 8. Nenhum endereço novo deve ser escrito no Windows antes de entrar no catálogo e passar pelo ciclo:

```text
[PREFLIGHT] -> [SNAPSHOT] -> [APPLY/RMW] -> [READBACK]
            -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]
```

## 12. Conclusão

A outra IA não inventou toda a base: SDMA, CP e compute MEC têm evidências físicas valiosas. O erro foi projetar essa vitória estreita como se UMD, Vulkan, LLM e WDDM de produção já estivessem quase concluídos. Eles não estão.

O caminho mais realista para o objetivo do proprietário é:

> preservar o laboratório comprovado → transformar submission/fence/memória em serviços WDDM reais → portar o winsys do RADV/ACO → fechar Vulkan graphics/WSI → acelerar present sobre o display herdado → assumir DCN/display de forma transacional → habilitar DXVK/Zink/VKD3D e LLMs.

Até a conclusão dos gates correspondentes, display acelerado, jogos Vulkan/DXVK/VKD3D/Zink e LLMs permanecem **NÃO VALIDADOS EM HARDWARE**.

## 13. Registro de início da execução — 31/08/2026

Primeiro incremento P0 aplicado:

- criado `capability_status.json` com estados explícitos e machine-readable;
- corrigido o topo de `STATUS_E_ROADMAP_CONSOLIDADO.md` para retirar as alegações falsas de runtime, Vulkan e LLM funcionais;
- runtime público agora falha fechado para allocator/free sem ownership, cópias CPU disfarçadas de SDMA, IB cuja GPUVA era descartada e polling que reutilizava o IOCTL de dispatch;
- os IOCTLs legacy de allocator/free/copy do serviço também falham fechado;
- INF fonte sincronizado com `3.61.0.0 / Schema 127`, e packager tornado independente do schema antigo 98;
- teste UMD ampliado para 21 invariantes de segurança;
- `build-contract.bat` e `build-umd.bat` passaram; isto é validação offline, não hardware.

Próximo gate ativo: projetar a ABI de monitored fence/retirement e ligar a fila MEC P2 comprovada ao fluxo de submissão WDDM sem polling ou fence produzida pela CPU. **NÃO VALIDADO EM HARDWARE.**

### Correção V2 inicial

A primeira revisão do caminho de retirement encontrou e corrigiu dois defeitos:

- `DxgkDdiQueryCurrentFence` devolvia a fence privada do ring convertida para `UINT`; agora devolve exclusivamente o último `SubmissionFenceId` efetivamente notificado ao Dxgkrnl;
- se o trabalho já tivesse sido publicado no ring e o registro no tracker WDDM falhasse, o código retornava erro mas mantinha a engine online; agora a engine passa obrigatoriamente a `FAULTED`, impedindo submissões posteriores sobre estado órfão.

Validação desta correção é offline. IH e monitored fences continuam **NÃO VALIDADOS EM HARDWARE**.

### Auditoria IH inicial

O contrato IH antigo foi corrigido antes de qualquer tentativa física:

- o vetor Navi/GFX10 é tratado como 32 bytes/8 DWORDs, não 16 bytes/4 DWORDs;
- RPTR/WPTR são tratados em bytes;
- decode inclui `ring_id` de 8 bits, VMID na posição correta, origem do VMID, timestamp de 48 bits, origem do timestamp, node ID e quatro DWORDs de source data;
- foi adicionado `Bc250IhValidateProvenance`, que exige snapshot, layout de registradores provado, vector layout V4/8 DWORDs e mapa client/source provado;
- offsets duplicados, desalinhados ou provenance ausente falham fechado.

O inventário bruto contém famílias conflitantes de offsets IH (`0x3800...` e
`0x4480...`) vindas de árvores de referência que não constituem prova para esta
unidade. Nenhuma delas foi promovida ao catálogo nem usada em escrita. O IH físico
permanece desabilitado e **NÃO VALIDADO EM HARDWARE**.

### Proveniência OSSSYS via IP Discovery

O parser de IP Discovery foi generalizado para extrair uma identidade alvo por
`hardware_id` e instância. O bloco `OSSSYS` (`HWID 30`, usado pelo AMDGPU como
fonte do `OSSSYS_HWIP`) agora pode ser extraído com validação de checksum,
limites, unicidade e estado de harvest. O script
`tools/audit-linux-ih-provenance.ps1` converte a árvore sysfs já coletada no
Linux em um candidato JSON com hash SHA-256 da fonte.

Essa evidência prova apenas versão, instância e bases publicadas pelo IP
Discovery. Ela **não autoriza offsets de registradores**, **não autoriza o mapa
client/source** e **não habilita IH**. A composição `base + offset` ainda precisa
ser fechada contra headers oficiais da versão exata e depois validada por
snapshot, readback e prova no silício seguindo a máquina transacional.

### Classificação das famílias de offsets IH

O header oficial `osssys_5_0_0_offset.h` define o bloco com base byte
`0x4280` e os registradores como índices DWORD: `IH_RB_CNTL=0x80`,
`IH_RB_BASE=0x81`, `IH_RB_BASE_HI=0x82`, `IH_RB_RPTR=0x83`,
`IH_RB_WPTR=0x84`, `IH_DOORBELL_RPTR=0x87`, `IH_CNTL=0xC0`,
`IH_CNTL2=0xC1` e `IH_STATUS=0xC2`. A composição correta em BAR é:

`byte_offset = (osssys_base_dwords + register_dwords) * 4`

Para `osssys_base_dwords=0x10A0`, equivalente à base byte `0x4280`, a
composição produz exatamente `RB_CNTL=0x4480`, `RB_BASE=0x4484`,
`RB_BASE_HI=0x4488`, `RPTR=0x448C`, `WPTR=0x4490`,
`DOORBELL_RPTR=0x449C`, `IH_CNTL=0x4580`, `IH_CNTL2=0x4584` e
`IH_STATUS=0x4588`.

Assim, a família `0x4480` é **estruturalmente coerente** com OSSSYS 5.0. A
família `0x3800` é rejeitada porque não preserva nem a ordem nem os
espaçamentos oficiais — ela posiciona `RB_BASE` antes de `RB_CNTL` e diverge
também nos registradores globais.

Essa classificação está codificada em
`driver/core/bc250_ih_register_layout_contract.{h,c}` com testes de
alinhamento, overflow, versão e mismatch. O resultado ainda é somente um
**candidato de catálogo**: falta capturar na unidade BC-250 a base OSSSYS do
IP Discovery e realizar snapshot/readback físico. Nenhum endereço foi
promovido a `docs/03_CATALOGO_DE_REGISTRADORES.md` nesta etapa.

### Regressão física pós-auditoria — 31/08/2026 16:44

Depois de corte físico de energia, o pacote 3.61/schema 127 foi vinculado em
modo de laboratório com `RenderNode=0`, shader e fila persistente habilitados,
watchdog de 180 segundos e engine inicialmente limpa. A evidência está em
`docs/evidence/2026-08-31_16-44-22-DESKTOP-HQAG6T4-lab-binding`.

- preflight físico: `HqdActiveBefore=0` e baseline WPTR `0`;
- P1: IB de 45 DWORDs relido, `WPTR=RPTR memória=RPTR MMIO=12`;
- fence do dispatch: `0xA55A0FF0 -> 0xBC250D15`;
- resultado: `0x43DAFFFE -> 0xBC250C01`, zero mismatches;
- P1: uma submissão específica aposentada e triad/physical ready iguais a 1;
- P2: 16/16 submissões aceitas e aposentadas;
- P2: `WPTR=RPTR memória=RPTR MMIO=204`, fence final 17;
- P2: zero mismatches, zero falhas de doorbell e triad/physical ready iguais a 1;
- teardown: watchdog restaurou BasicDisplay com Código 0.

O validador P1 tinha um débito: exigia que os contadores acumulados do motor
fossem `1`, embora o mesmo StartDevice execute depois os 16 lotes P2 e termine
em `17`. O harness foi corrigido para exigir `1 + P2SubmissionsAccepted` e
`1 + P2SubmissionsRetired`; as provas específicas de P1 continuam exigindo
exatamente uma submissão, duas fences físicas, resultado correto e cursores
drenados. P1 e P2 passaram novamente sobre a mesma captura.

Esta rodada confirma que as mudanças recentes não regrediram o caminho MEC
P1/P2 já provado. Ela não valida WDDM de aplicação, IH, Vulkan ou display
acelerado.

### Correções pós-regressão — contrato seguro de bind e submissão WDDM

A revisão seguinte encontrou dois débitos que poderiam produzir resultados
enganosos antes do próximo ensaio:

1. `RenderNode` era gravado pelo binder somente depois do carregamento do
   driver. Um valor `1` persistido por uma tentativa anterior podia, portanto,
   selecionar inesperadamente o registro display+render no bind seguinte. O
   INF fonte agora define explicitamente `RenderNode=0`; o empacotador e o
   binder recusam pacotes que não preservem esse padrão seguro.
2. `Bc250McdmSubmitCommandVirtual` retornava `STATUS_SUCCESS` quando não havia
   executor de submissão instalado. O callback também montava uma validação
   intermediária com endereços GPU constantes que não descreviam o contexto
   real. Esse caminho foi removido: sem executor do miniport ativo a DDI falha
   com `STATUS_NOT_SUPPORTED`; com executor, toda validação e publicação ficam
   no proprietário da fila MEC real.

A suíte integral `driver/wdk/build-contract.bat` passou após as correções,
incluindo callbacks MCDM, miniport ativo, GPUVA, anel persistente, VidPn, matriz
DDI e auditoria de segurança. Esse resultado é **VALIDAÇÃO OFFLINE APENAS**.
O novo binário ainda não foi empacotado, instalado ou ensaiado no silício.

### Fonte WDDM correta para reconstrução de GPUVA

Os headers WDK 10.0.26100 e a documentação oficial confirmam que
`DXGK_OPERATION_UPDATE_PAGE_TABLE` fornece `hProcess` (o handle KMD retornado
por `DxgkDdiCreateProcess`), `hAllocation`, `FirstPteVirtualAddress`, nível da
page table e o vetor de `DXGK_PTE`. `DxgkDdiBuildPagingBuffer` é chamado em
`PASSIVE_LEVEL`. Logo, essa operação — limitada a atualizações de folha — é a
fonte correta para preparar mudanças de cobertura GPUVA. `VirtualTransfer` e
`VirtualFill` apenas consomem endereços já mapeados e não podem criar ownership.

O commit não pode ocorrer simplesmente no retorno de `UPDATE_PAGE_TABLE`: a
sequência WDDM inclui atualização de PTE, `FLUSH_TLB` da root page table e
submissão do paging buffer. O desenho seguinte deve manter um journal pendente
por `hProcess`/root page table, aplicar map/unmap de intervalos (inclusive
split/merge parcial) e publicar um snapshot somente após flush confirmado.
Atualizações não-leaf, 64 KiB, copy-page-table e residency permanecem
fail-closed até possuírem contratos próprios. Também foi removido o último
fallback que retornava sucesso para `FLUSH_TLB` sem handler instalado.

Como primeira peça do journal, `Bc250GpuvaSpaceReplaceRangeTransactional` foi
adicionado ao contrato GPUVA. Ele reconstrói o resultado em um espaço scratch,
faz split das porções esquerda/direita atingidas por map/unmap parcial, ajusta
o offset de backing da porção direita e só copia o resultado ao snapshot
inativo se todas as inserções passarem. Falha de permissão, capacidade,
alinhamento ou overflow deixa o estado anterior intacto. Testes cobrem unmap
parcial, preservação dos dois lados, remap com novas permissões e rollback de
remap sem permissões.

O primeiro build revelou dependência indesejada de `memcpy` na cópia grande do
snapshot e falhou no link. A cópia foi tornada interna e explícita; a suíte
integral subsequente passou com exit code 0. Este contrato ainda não publica
snapshots concorrentes nem recebe `UPDATE_PAGE_TABLE`: permanece offline e o
gate `GpuvaMappingsOperational` continua zero.

### Snapshot GPUVA concorrente de dois slots (continuação de 31/08/2026)

Foi adicionado `BC250_GPUVA_SNAPSHOT_STORE`, mantendo dois snapshots completos
fora da pilha. Leitores adquirem uma referência no slot ativo, confirmam o slot
novamente e liberam explicitamente após validar o IB. O escritor em
`PASSIVE_LEVEL` tenta bloquear exclusivamente o slot inativo, reconstrói nele a
transação completa e somente então publica o índice ativo.

O contador de leitores usa um valor reservado de writer-lock. Isso fecha a
janela ABA em que um leitor poderia ter observado um slot ativo antigo, atrasar
o incremento e alcançar o mesmo slot enquanto ele já estivesse sendo
reutilizado pelo escritor. A geração passou a ser registrada por slot, evitando
associar ao snapshot antigo a geração global de uma publicação concorrente.

O teste `gpuva_snapshot_contract_test` cobre:

- leitor antigo permanecendo válido após a primeira publicação;
- nova aquisição observando apenas o snapshot integralmente publicado;
- recusa `BC250_GPUVA_BUSY` ao tentar reutilizar um slot ainda referenciado;
- publicação posterior após a liberação do leitor;
- gerações monotônicas por snapshot.

A primeira execução integral encontrou `LNK2019` porque o novo teste
freestanding não fornecia `memset`. O harness foi corrigido e uma segunda
execução de `driver\\wdk\\build-contract.bat` terminou com código zero. Esse é
um resultado estrutural/offline: não houve instalação, binding, MMIO ou execução
na BC-250. O store ainda não está conectado aos callbacks `UpdatePageTable` e
`FlushTlb`; portanto `GpuvaMappingsOperational` permanece deliberadamente zero
e esta etapa está **NÃO VALIDADA EM HARDWARE**.

### Journal `UpdatePageTable -> FlushTlb` (fundação offline)

Foi implementado o contrato `BC250_GPUVA_JOURNAL`. Atualizações leaf de 4 KiB
são acumuladas por processo e pela root page table esperada. Cada PTE válida
vira uma alteração de map com permissões e backing explícitos; PTE inválida vira
unmap. Um segundo processo/root não pode reutilizar o journal pendente.

O commit exige que o `FlushTlb` corresponda ao mesmo processo e root e que sua
faixa cubra todas as alterações pendentes (ou represente flush global). Flush
incorreto não descarta o journal. No commit válido, todas as mudanças são
aplicadas ao slot inativo e publicadas por uma única troca de snapshot, evitando
visibilidade parcial de um lote.

O teste `gpuva_journal_contract_test` cobre root divergente, faixa de flush
insuficiente, commit correto de duas páginas, validação de cobertura no snapshot
novo e unmap de uma página preservando a página adjacente. A suíte integral
`driver\\wdk\\build-contract.bat` terminou com código zero.

Esta implementação ainda não está registrada como observador do
`DXGK_OPERATION_UPDATE_PAGE_TABLE`, nem o commit foi ligado ao handler físico de
`DXGK_OPERATION_FLUSH_TLB`. Consequentemente o gate operacional continua zero.
Resultado exclusivamente estrutural/offline, **NÃO VALIDADO EM HARDWARE**.

### Integração do journal nas DDIs WDDM (offline)

O miniport ativo passou a possuir `GpuvaSnapshots`, scratch persistente, journal
e staging fixo no contexto do adaptador. Não há alocação temporária no caminho
protegido e as estruturas grandes não são colocadas na pilha do kernel.

Para updates leaf (`PageTableLevel == 0`), o callback comum conclui primeiro
toda a validação/encoding. Em seguida o handler ativo, sob exclusão, valida o
`hProcess` no registro de objetos e captura a root page table real configurada
por `SetRootPageTable`; somente depois prepara o journal e escreve as PTEs CPU
virtual. Updates de níveis superiores continuam sendo codificados normalmente,
mas não são apresentados falsamente como ranges GPUVA leaf.

No `FlushTlb`, se houver journal pendente, processo, segmento e offset da root
devem corresponder. A invalidação GFXHUB com readback/ACK ocorre antes do commit.
Somente após sucesso físico o lote é publicado no snapshot. Falha de ACK não
publica. Falha de publicação após o ACK fecha `GpuvaMappingsOperational`.
Flush sem alteração leaf pendente continua permitido e ainda exige a operação
física, evitando quebrar invalidações legítimas de níveis superiores.

`StopDevice` fecha o gate, desabilita o journal e aborta qualquer lote pendente
sob a mesma exclusão usada pelo teardown de objetos. `Unload` agora também
desconecta explicitamente submission, fence, update-page-table, flush e ciclo de
interrupção, removendo callbacks globais residuais.

A primeira compilação desta integração falhou por conversão implícita do
bitfield `DXGK_PTE.Segment`; o cast explícito foi aplicado. Depois da correção e
da revisão de updates não-leaf/flush sem journal, a suíte integral terminou com
código zero. O gate permanece zero porque o caminho de submissão ainda consulta
o antigo `GpuvaSpace`, e a sequência integrada ainda não foi executada na
BC-250. **NÃO VALIDADO EM HARDWARE**.

### Validação de IB migrada para snapshots

`SubmitCommandVirtual` deixou de consultar o `GpuvaSpace` mutável legado. Após
adquirir a visão rundown do contexto, ele adquire uma referência do slot GPUVA
ativo, valida integralmente o IB nesse snapshot e mantém a referência até a
publicação e o commit da fence. Todos os retornos de erro após a aquisição
liberam tanto o snapshot quanto a visão do contexto.

O epoch usado na cobertura agora é `snapshot->PageTableEpoch`. O
`PageTableEpoch` do registro de processo não é mais comparado com mappings,
pois ele representa mudanças de identidade/configuração da root page table, não
cada publicação leaf. Misturar os dois domínios poderia rejeitar mappings
válidos ou autorizar um snapshot com geração errada.

Ao preparar qualquer update leaf, o gate operacional é fechado antes da escrita
das PTEs; durante o intervalo anterior ao ACK, page table e snapshot pertencem a
gerações diferentes. A suíte integral passou novamente com código zero.

O gate ainda não é reaberto automaticamente. Faltam dois contratos antes disso:

- rastrear invalidações/updates de níveis superiores da hierarquia;
- garantir a vida do mapping até a retirada física da fence, não apenas até a
  publicação do IB no anel.

Portanto a migração elimina o uso concorrente do espaço legado, mas não declara
submissão GPUVA pronta. **NÃO VALIDADO EM HARDWARE**.

### Pin do snapshot transferido para a fence física

O rastreador WDDM passou a transportar metadados opacos de pin GPUVA: slot,
geração e indicador de validade. A reserva recebe o pin já adquirido pela
submissão; o commit transfere os metadados para a entrada FIFO associada à fence
física. Somente após `Bc250SubmissionEnginePoll` observar a fence escrita pela
GPU e o drain retirar a entrada é que o miniport valida slot/geração e decrementa
a referência do snapshot.

Falha antes do commit mantém a referência sob ownership local e a libera no
caminho de erro. Após commit bem-sucedido, a função de submissão invalida seu
token local sem decrementar, pois o ownership pertence à fence. Tentativa de
anexar dois pins à mesma reserva é recusada.

O teste do tracker cobre explicitamente `reserve -> attach -> commit -> drain` e
confirma preservação de slot e geração. A primeira execução da suíte revelou
`__chkstk` apenas no executável freestanding de teste, devido ao crescimento de
uma estrutura local; tracker e saída foram movidos para armazenamento estático.
A execução integral seguinte terminou com código zero.

Ainda falta definir teardown/reset de pins quando há fences não retiradas. O
driver não pode simplesmente zerar o tracker nem decrementar pins enquanto a GPU
pode continuar lendo o mapping. Esse caso deve falhar ocupado, concluir/resetar
fisicamente a engine e só então liberar as referências. Até esse contrato e o
rastreamento de níveis superiores existirem, o gate continua fechado.
**NÃO VALIDADO EM HARDWARE**.

### Stop gracioso e correção de falso reset

`StopDevice` agora consulta `Count` e `ReservedCount` do tracker dentro de
`DxgkCbSynchronizeExecution`. Se houver reserva transitória ou fence física em
voo, retorna `STATUS_DEVICE_BUSY` antes de mudar `IsActive`, cancelar o timer ou
iniciar teardown. Assim o polling continua capaz de retirar a fence e devolver
o pin; o stop não destrói a única autoridade que mantém o mapping vivo.

O fixture cobre reserva pendente, recusa do stop preservando `IsActive`,
cancelamento e stop posterior bem-sucedido. A primeira posição escolhida para o
teste já estava depois de um stop anterior e foi corrigida; a suíte integral
seguinte terminou com código zero.

A auditoria dos resets encontrou um falso positivo preexistente:
`Bc250McdmResetEngine` retornava `STATUS_SUCCESS` e `LastAbortedFenceId = 0` sem
executar reset de HQD/SDMA e sem tratar pins. Esse retorno foi substituído por
`STATUS_NOT_SUPPORTED`. Caps estruturais de TDR não constituem prova de reset
físico. Um futuro executor só poderá retornar sucesso após parar a engine,
confirmar quiescência/reset no silício, identificar a última fence abortada e
liberar de maneira controlada os pins correspondentes.

O stop gracioso está protegido, mas reset/timeout funcional permanece ausente e
o gate GPUVA continua fechado. **NÃO VALIDADO EM HARDWARE**.

Débitos técnicos observados e ainda abertos: três ferramentas auxiliares
compilam com warnings (`bc250-compute-cli.c`, `bc250-gpuva-sdma-gate.c` e
`bc250-hardware-diag.c`), e o link de `bc250-mcdm-submit-test` emite avisos de
importação. Eles não invalidaram os contratos do kernel, mas devem ser
eliminados antes de transformar o harness de aplicação em critério de aceite.

### Próximo bloqueio real: ownership do contexto e GPUVA dinâmica

O executor ativo publica `DmaBufferVirtualAddress` como IB na MEC e correlaciona
a fence física do anel com `SubmissionFenceId`, mas ainda não existia uma prova
completa de que, no instante da DDI, o contexto pertencesse ao adaptador/nó correto
e de que toda a faixa do IB estivesse mapeada nas page tables daquele processo. O
registro de objetos anterior não conservava `NodeOrdinal` no objeto de contexto, e
seu lock não podia ser adquirido ingenuamente em uma DDI que pode chegar em
`DISPATCH_LEVEL`.

### Resolução do Bloqueio: Contexto Seguro, GPUVA e Submissão Transacional (31/08/2026)

Para eliminar definitivamente esses riscos sem regredir o motor físico MEC P1/P2:

1. **Contexto WDDM Seguro (`BC250_CONTEXT_SUBMISSION_VIEW`)**:
   - O registro de objetos agora rastreia `OwnerAdapter`, `NodeOrdinal`, `EngineOrdinal`, `State` (`LIVE`/`DESTROYING`/`DEAD`), `InFlightSubmissions`, `PageTableEpoch` e `TeardownEpoch`.
   - `Bc250McdmContextAcquireSubmissionView` opera em `DISPATCH_LEVEL` com zero alocações e sem locks exclusivos, usando `InterlockedIncrement(&contextRec->InFlightSubmissions)` e snapshot de campos imutáveis.
   - Handles nulos, estáticos, com mismatch de nó/adaptador/processo ou sem root page table configurada são rejeitados de forma estrita *fail-closed*.
   - Destruição de contextos com submissões em voo é bloqueada (transiciona para `DESTROYING` e retorna `DEVICE_BUSY`).

2. **Rastreamento de Espaço de GPUVA por Processo/Contexto (`BC250_GPUVA_SPACE`)**:
   - Contrato determinístico (`driver/core/bc250_gpuva_space_contract.{h,c}`) com validação estrita de 48 bits, alinhamento DWORD/4KB, proteção contra wraparound/overflow, controle de permissões (`READABLE`, `WRITEABLE`, `EXECUTABLE`), época de page tables e isolamento entre processos.
   - `Bc250GpuvaSpaceValidateRangeCoverage` percorre integralmente o intervalo `[BaseGpuVa, BaseGpuVa + Size)` exigindo cobertura 100% contínua e sem lacunas (gaps) ou desmapeamentos.

3. **Ordem Transacional de Submissão e Rastreamento de Fences WDDM**:
   - `DxgkDdiSubmitCommandVirtual` segue a máquina de estados:
     1. Validação básica de argumentos.
     2. Aquisição de visão do contexto (`InFlightSubmissions++`).
     3. Validação integral da faixa GPUVA do IB (`Bc250GpuvaSpaceValidateRangeCoverage`).
     4. Reserva sincronizada de slot no rastreador de fences (`Bc250WddmFenceTrackerReserve`).
     5. Publicação do Indirect Buffer no anel físico persistente (`Bc250SubmissionEngineSubmit`).
     6. Em caso de falha de publicação: cancelamento da reserva (`Bc250WddmFenceTrackerCancel`).
     7. Commit da fence física ao `SubmissionFenceId` (`Bc250WddmFenceTrackerCommit`). Em caso de falha pós-publicação: isolamento do motor (`FAULTED`).
     8. Liberação da visão do contexto (`InFlightSubmissions--`).

4. **Auditoria e Validação Offline Integral**:
   - Suíte `build-contract.bat`, `audit-mcdm-ddi-matrix.py` e `audit-bc250mcdm-safety.py` aprovadas com código de saída 0.
   - Veredito rigoroso: **CONTRATO OFFLINE APROVADO — NÃO VALIDADO EM HARDWARE**. Nenhum driver foi instalado, nenhum MMIO foi gravado e nenhuma prova física foi alterada nesta etapa.

### Melhorias de Engenharia Pós-Auditoria (31/08/2026)

Em resposta à auditoria estática e análise de gargalos operacionais em `DISPATCH_LEVEL`, foram implementadas e aprovadas três melhorias fundamentais:

1. **Expansão do Registro de Faixas GPUVA e Busca Binária $O(\log N)$**:
   - Capacidade expandida de 64 para 256 faixas ativas por espaço de GPUVA (`BC250_GPUVA_SPACE_MAX_RANGES = 256`), acomodando heaps complexos de buffers/texturas.
   - Tabela de índices ordenados `SortedIndices` mantida por `BaseGpuVa` no momento da inserção (`Bc250GpuvaSpaceInsertRange`) e remoção (`Bc250GpuvaSpaceRemoveRange`) em `PASSIVE_LEVEL`.
   - `Bc250GpuvaSpaceValidateRangeCoverage` agora realiza busca binária determinística $O(\log N)$ para localizar faixas em `DISPATCH_LEVEL`, eliminando varreduras lineares $O(N)$ no caminho crítico de submissão de comandos virtuais.

2. **Otimização da Latência de Retirada de Fence (Dreno Oportunístico e Provedor Físico)**:
   - Implementado o provedor `Bc250ActiveCurrentFenceProvider` conectado a `DxgkDdiQueryCurrentFence` via `Bc250McdmInstallCurrentFenceProvider`.
   - Leitura direta da fence física da GPU na VRAM (`ComputeFenceVirtual`) com barreira de memória (`KeMemoryBarrier()`), acoplada ao dreno de fences correlacionadas `Bc250WddmFenceTrackerDrain`.
   - Dreno oportunista executado na entrada de submissões e ciclo periódico de alta frequência via `KTIMER`/`KDPC`, minimizando a latência de devolução de fences ao `dxgkrnl`.

3. **Coerência de Cache GCMC e Invalidação de TLB no GFXHUB (`DXGK_OPERATION_FLUSH_TLB`)**:
   - `Bc250McdmBuildPagingBuffer` conectado ao manipulador ativo `Bc250ActiveFlushTlbHandler` para a operação `DXGK_OPERATION_FLUSH_TLB`.
   - A cada flush solicitado pelo subsistema de paginação do WDDM:
     - Incrementa deterministicamente `PageTableEpoch` no espaço GPUVA do adaptador.
     - Executa `Bc250ActiveFlushMappedGfxhubTlb`, acionando a transação física de invalidação de TLB no GFXHUB com verificação de ACK e proteção contra TLBs obsoletas.

Todos os 41 testes unitários, testes de regressão de submissão unificada e auditorias estruturais foram reexecutados com sucesso (código de saída 0).

### Revisão independente e correção do protótipo GPUVA

A implementação acima foi reprovada em revisão semântica apesar do build verde.
As faixas eram inseridas apenas pelos testes; nenhum fluxo de produção do VidMm
chamava `Bc250GpuvaSpaceInsertRange`/`RemoveRange`. Também havia corrida entre
Acquire/DestroyContext, drenagem do tracker fora de
`DxgkCbSynchronizeExecution`, leitura direta da fence VRAM em
`QueryCurrentFence`, falso sucesso no flush de TLB e teardown de timer sem
aguardar DPC já enfileirado.

Foram aplicadas as seguintes correções fail-closed:

- gate de rundown atômico por contexto, fechando novas aquisições antes de
  Destroy/teardown e mantendo as referências em voo contadas;
- teardown do registro recusa conclusão enquanto houver submissão em voo;
- Stop/Remove não libera o adapter se o teardown de objetos falhar;
- `QueryCurrentFence` voltou a consultar somente a última fence WDDM já
  notificada, sob `DxgkCbSynchronizeExecution`, sem drenar o tracker e sem ler
  a fence física bruta;
- `KeFlushQueuedDpcs` foi restaurado depois do cancelamento do timer;
- flush de TLB só incrementa a epoch e retorna sucesso após ACK físico;
- `DxgkDdiSubmitCommandVirtual` agora exige
  `GpuvaMappingsOperational=1`. Esse gate permanece zero porque map/unmap real
  do VidMm ainda não foi integrado. Faixas sintéticas dos testes não podem mais
  autorizar trabalho no motor de produção.

A suíte integral passou depois dessas correções. O rastreador de intervalos e a
reserva transacional de fence continuam aproveitáveis como contratos offline,
mas o estado correto é **PROTÓTIPO OFFLINE INCOMPLETO — NÃO VALIDADO EM
HARDWARE**. O próximo trabalho é identificar no WDK o fluxo real de atualização
da GPUVA/page tables e publicar snapshots de intervalos seguros para leitura em
DISPATCH_LEVEL; até lá, submissão WDDM de aplicação permanece fail-closed.

### Integração do journal GPUVA, snapshots e hierarquia não-leaf (01/09/2026)

O fluxo real de `DXGK_OPERATION_UPDATE_PAGE_TABLE` passou a alimentar um journal
transacional associado ao par processo/root page table. Atualizações de nível 0
(leaf PTEs de 4 KiB) produzem operações map/unmap no journal; atualizações dos
níveis 1–3 são rastreadas separadamente como mutações de hierarquia, sem inferir
faixas GPUVA inexistentes a partir de PDEs.

- Antes de qualquer escrita de page table, `GpuvaMappingsOperational` é fechado.
- Leaf e hierarquia pendentes devem pertencer ao mesmo processo/root informado
  pelo flush; qualquer divergência falha fechada.
- O snapshot imutável de intervalos só é publicado depois do ACK físico do flush
  do GFXHUB. Falha de ACK não publica estado e não abre o gate.
- A submissão virtual fixa uma referência ao slot/geração do snapshot; essa
  referência é transferida para o fence tracker e somente liberada após a fence
  física correspondente ser retirada.
- O duplo buffer rejeita nova publicação se ambos os slots ainda estiverem
  fixados por trabalho em voo, fornecendo backpressure em vez de sobrescrever
  memória observável pela GPU.
- `StopDevice` recusa teardown gracioso com fences comprometidas ou reservas
  pendentes; `ResetEngine` retorna `STATUS_NOT_SUPPORTED` porque ainda não existe
  executor físico de reset/TDR auditado.

O teste de callbacks comprova offline que atualizações não-leaf chegam ao handler
ativo, e a suíte completa `driver/wdk/build-contract.bat` terminou com código 0.
Isto não autoriza submissão de produção: o gate continua deliberadamente zero
até existir uma política de ativação comprovável, telemetria correspondente e
um ensaio seguindo
`[PREFLIGHT] -> [SNAPSHOT] -> [APPLY] -> [READBACK] -> [PROVA NO SILÍCIO] -> [ROLLBACK/TEARDOWN]`.

Veredito desta etapa: **RASTREAMENTO GPUVA LEAF/NÃO-LEAF INTEGRADO OFFLINE — NÃO
VALIDADO EM HARDWARE**.

### Gate GPUVA: elegibilidade separada de autorização (01/09/2026)

Foi acrescentado `GpuvaActivationEligible` para registrar que o estado candidato
satisfaz todos os pré-requisitos observáveis sem confundi-lo com
`GpuvaMappingsOperational`, que continua sendo a autorização efetiva. A
elegibilidade só é calculada após commit leaf e exige, ao mesmo tempo:

- miniport ativo;
- ABIs de contexto, alocação e root page table prontas;
- submission engine fisicamente online;
- journal e hierarquia sem mutações pendentes;
- slot de snapshot válido, com geração/epoch publicada e ao menos uma faixa viva.

Qualquer nova atualização de page table zera elegibilidade e autorização. Flush
apenas de hierarquia, falha de publicação, snapshot vazio ou qualquer pré-requisito
ausente mantém ambos fechados. Mesmo quando a elegibilidade se torna 1, o código
**não** promove automaticamente `GpuvaMappingsOperational`: essa promoção será
uma etapa distinta do ensaio integrado e deverá ter rollback explícito.

A suíte completa foi recompilada e executada com sucesso após a mudança. Os três
warnings antigos dos utilitários e os avisos de importação do harness MCDM seguem
como dívida técnica. Nenhum driver foi instalado e nenhum acesso ao hardware foi
feito nesta rodada: **NÃO VALIDADO EM HARDWARE**.

### Telemetria explicável do gate GPUVA (01/09/2026)

O gate passou a publicar seis valores persistentes: elegibilidade, autorização
operacional, máscara de bloqueios, geração do snapshot, epoch e quantidade de
faixas vivas. A máscara distingue journal/hierarquia pendentes, adaptador ou
engine offline, ABIs ausentes, slot/assinatura inválidos, snapshot vazio, epoch
zero e flush sem commit leaf. Os valores são fotografados sob `ObjectLock` e só
depois gravados na telemetria, evitando I/O de Registry dentro da região crítica
e combinações provenientes de gerações diferentes.

O gate operacional não foi aberto. A suíte integral passou novamente, sem
instalação e sem acesso físico: **TELEMETRIA APROVADA OFFLINE — NÃO VALIDADA EM
HARDWARE**.

O watchdog do boot V5 terminou às `09:30:18 -03:00` em `ROLLED_BACK` e
restaurou `BasicDisplay` Código 0. O pacote `oem20.inf` foi removido e o
marcador de boot frio foi publicado.

### Candidato 3.65 — rastro V5 e teardown idempotente (01/09/2026)

O 3.65 contém somente correções dirigidas pela evidência do boot anterior:

- snapshot de `V5LastDdi`, `V5PrevDdi`, `V5Prev2Ddi` e contagem no início do
  `StopDevice`, antes de destruir qualquer estado;
- `QueryInterface` incluído no mesmo rastro DDI;
- `RemoveDevice` não repete `StopDevice` quando `IsActive` já está zero, mas
  ainda executa Stop quando recebe remoção direta;
- o teste existente `StopDevice -> RemoveDevice` passa e protege a
  idempotência.

Build integral, build destrutivo de laboratório, testes contratuais, matriz
DDI, `Inf2Cat`, assinaturas, auditor do pacote e auditor de segurança passaram.
Pacote `09/01/2026,3.65.0.0`, schema 128:

- SYS: `1962DC03F277C681595C607E6E01EC266F3C7B83A1E2E138723AD006DF27F874`;
- INF: `30CFB124853598A831621832740DD2A32F0A48C58BBE2C3EA4F95321780E62F1`;
- CAT: `89DFBEAB6820FEE3CA41D2D77EB4B44A7397ACD3537CDE3F5B68486D7A3EA1E4`.

Nenhum binding foi feito. O próximo ciclo exige boot frio em BasicDisplay,
binding 3.65 com `RenderNode=0`, prova display/P1/P2 e somente então nova
promoção protegida para `RenderNode=1`. **3.65 APROVADO OFFLINE — NÃO VALIDADO
EM HARDWARE**.

### Validação física 3.65 em KMDOD (01/09/2026)

Após boot frio, o 3.65 foi vinculado com `RenderNode=0`, shader e submissão
persistente habilitados. O adaptador permaneceu `bc250mcdm` Código 0, o hash do
SYS instalado coincidiu com o pacote e o display foi confirmado com VidPN,
commit, present, VSync e interrupções. Evidência:
`docs/evidence/2026-09-01_09-36-52-DESKTOP-HQAG6T4-lab-binding`.

Os harnesses independentes terminaram com código zero:

- P1: fences físicas, resultado byte a byte e `WPTR=RPTR=12`;
- P2: 16/16 aceitas/aposentadas, `WPTR=RPTR MMIO=RPTR report=204`, zero
  divergências;
- one-shot, shader, P1 e P2 em `PhysicalReady=1`;
- `V5RegistrationMode=0`, confirmando o controle KMDOD.

**3.65 KMDOD, DISPLAY E P1/P2 VALIDADOS EM HARDWARE; RASTRO V5 AINDA NÃO
EXERCITADO**.

### Decisão operacional: watchdog dispensado (01/09/2026)

O operador determinou explicitamente que os próximos testes não usarão reboot
guard/watchdog. A partir deste ponto não haverá rollback automático. A
recuperação manual `C:\BC250_RECOVERY.cmd` foi reauditada e passou; nenhuma
tarefa `BC250-RebootGuard` estava registrada antes do reboot 3.65. Foi criado
`inspect-bc250mcdm-rendernode.ps1`, somente para captura pós-boot de dispositivo,
V5, P1/P2 e rastro DDI, sem confirmação ou mutação de hardware.

### Promoção opt-in do gate para candidato de laboratório (01/09/2026)

Foi adicionado o tunable `GpuvaLabPromotion`, lido no `StartDevice` em
`PASSIVE_LEVEL` e definido como `0` por padrão no INF. O driver também registra
se o valor existia de fato, impedindo que falha de leitura seja confundida com
autorização. A promoção para `GpuvaMappingsOperational=1` exige valor presente e
exatamente igual a 1, além de todos os critérios de elegibilidade, e só ocorre
depois de commit leaf e ACK físico de TLB bem-sucedidos.

Uma nova atualização de page table, falha de commit/flush, teardown ou
inconsistência de fence/snapshot fecha novamente o gate. Flush somente de
hierarquia nunca o promove. O pacote distribuído permanece seguro por padrão;
nenhuma chave foi alterada nesta máquina e nenhum driver foi instalado.

A serialização efetiva entre paging do VidMm e publicação no ring ainda precisa
ser observada no ensaio integrado. Portanto esta mudança cria apenas o candidato
controlável: **PROMOÇÃO OPT-IN APROVADA OFFLINE — NÃO VALIDADA EM HARDWARE**.

### Harness transacional da promoção GPUVA (01/09/2026)

O script `tools/test-gpuva-lab-promotion.ps1` implementa três ações separadas:
`Prepare`, `Inspect` e `Rollback`. `Prepare` exige administrador, uma única
BC-250 `1002:13FE`, o recovery `C:\BC250_RECOVERY.cmd`, salva valor original,
identidade PnP e hash do driver instalado, aplica somente o tunable e confirma
readback. Ele não instala, não troca binding, não reinicia e não submete.

Após boot separado, `Inspect` exige a mesma transação, confere Código 0, serviço,
política lida pelo driver, elegibilidade, máscara zero, snapshot vivo, engine
online e contadores de erro. Mesmo aprovado, o resultado é apenas “pronto para
submissão controlada”. `Rollback` restaura ou remove o DWORD conforme o snapshot
original e verifica readback; a reinicialização fica explícita.

Como a telemetria ganhou campos, a fonte do pacote e o empacotador passaram de
schema 127 para **128**. Naquele ponto, o manifesto de `output_lab` permaneceu
127 por ainda representar o pacote antigo; ele só foi atualizado pela
regeneração descrita a seguir. O script passou na análise sintática e ainda não
foi executado. **NÃO VALIDADO EM HARDWARE**.

### Pacote de laboratório 3.62 / schema 128 (01/09/2026)

A imagem de laboratório foi reconstruída integralmente, substituindo apenas o
objeto do miniport pela variante com bring-up ativo. A verificação pós-link
confirmou que ela não é byte-idêntica à produção e contém a instrumentação
obrigatória. Em seguida, o pacote foi regenerado com a receita já usada na prova
P1/P2: shader dispatch e submissão persistente habilitados, `CpSoftReset=1`,
`Vmid0PageTable=0` e `SdmaAutoCtxSw=0`.

O candidato foi identificado como `09/01/2026,3.62.0.0`, schema 128. O manifesto
agora declara explicitamente `GpuvaLabPromotionEnabled=false`, o auditor exige o
DWORD correspondente igual a zero no INF e confirma que os oito novos nomes de
telemetria estão realmente presentes no SYS. Isso impede distribuir por engano
um pacote com promoção GPUVA aberta.

Resultado do auditor de pacote:

- SYS: `6260AA8948598DF270481D80DFEF961662AE201D2606861199FB3658D0BC3C8C`;
- INF: `CC3668174B791B330D1C169BE1EB8F7747FACC57CE25484D50DE21D2C5C0BF7A`;
- CAT: `51EF18C510216697B849C7FF9CE8D9AB93854448CE502830193CE9A491B55D8A`;
- assinatura SYS/CAT válida;
- `Inf2Cat`: zero erros e zero warnings;
- auditoria: `PASS`.

Nenhuma instalação ou troca de binding foi realizada. **PACOTE APROVADO
OFFLINE — NÃO VALIDADO EM HARDWARE**.

### Preflight do ensaio físico 3.62 (01/09/2026)

O preflight somente leitura encontrou exatamente uma BC-250
`PCI\VEN_1002&DEV_13FE`, vinculada a `BasicDisplay`, Código 0 e status OK.
`C:\BC250_RECOVERY.cmd` está presente, não há reboot pendente por CBS, Windows
Update ou `PendingFileRenameOperations`, e o auditor confirmou novamente os
hashes/assinaturas do pacote 3.62.

O binder foi endurecido para exigir `GpuvaLabPromotion=0` tanto no INF quanto no
manifesto e para regravar zero durante o binding. A análise sintática passou. O
dry-run operacional foi recusado antes de qualquer alteração porque a sessão
atual não possui elevação administrativa, exatamente como exige o guard-rail.
Nenhum pacote foi estagiado e nenhum binding ocorreu.

Ponto de retomada: executar o binder em PowerShell elevado com shader, fila
persistente, render node para o próximo boot e watchdog de 600 segundos; confirmar
visibilidade antes de o watchdog expirar. Somente depois executar `Prepare` do
harness GPUVA. **NÃO VALIDADO EM HARDWARE**.

### Bind confirmado e reboot guard generalizado (01/09/2026)

O operador vinculou `oem20.inf`: `UpdateDriverForPlugAndPlayDevicesW` retornou
sucesso, o dispositivo ficou em `bc250mcdm` Código 0 e o SYS instalado casou com
o hash 3.62. O confirm registrou schema 128, VSync 8, commit/present aprovados e
desarmou corretamente o watchdog. Isso valida presença do display neste ciclo,
mas não compute nem GPUVA. `RenderNode=1` está gravado para o próximo boot e
`GpuvaLabPromotion=0` foi confirmado pelo driver.

O watchdog persistente de reboot existente estava preso a 3.24/schema 98. Ele
foi generalizado sem remover os gates de identidade: versão, schema e
`SkipHwBringup` agora vêm do manifesto autorizado; SYS/INF/recovery continuam
presos por SHA-256 e o watcher aceita somente versão numérica válida. O armador
ganhou `-LabCandidate`, selecionando manifesto `output_lab` e rollback de
laboratório. A confirmação pós-boot usa os valores esperados gravados na
transação, em vez de literais antigos.

Análise sintática dos três scripts e dry-run do armamento passaram. O dry-run
identificou 3.62.0.0, `oem20.inf`, serviço Código 0, recovery íntegro e timeout
600 s; nenhuma tarefa foi criada e nenhum reboot ocorreu. **PRONTO PARA ARMAR;
NÃO VALIDADO EM HARDWARE**.

### Vitória física 3.64 KMDOD e gate reforçado para RenderNode (01/09/2026)

O segundo binding limpo do 3.64, evidência
`docs/evidence/2026-09-01_09-12-28-DESKTOP-HQAG6T4-lab-binding`, terminou em
`bc250mcdm` Código 0. A política pré e pós-bind coincidiu:
`RenderNode=0`, `ShaderDispatch=1`, `PersistentSubmission=1` e GPUVA fechada.
O display foi confirmado com commit, present, VSync e interrupções reais.

Os harnesses independentes fecharam o tripé físico:

- P1: IB de 45 DWORDs, `WPTR=RPTR=12`, duas fences escritas pela GPU,
  resultado do shader conferido byte a byte, zero divergências;
- P2: 16/16 submissões aceitas e aposentadas, fence final 17,
  `WPTR=RPTR MMIO=RPTR report=204`, zero divergências;
- one-shot, shader, coerência, P1 e P2 publicaram `PhysicalReady=1`;
- `V5RegistrationMode=0`, provando que este ciclo permaneceu corretamente em
  KMDOD e não simulou display+render.

**CANDIDATO 3.64 KMDOD, DISPLAY, ONE-SHOT, P1 E P2 VALIDADOS EM HARDWARE;
RENDERNODE AINDA NÃO VALIDADO**.

Antes da promoção, o reboot guard foi reforçado. No modo
`-RequireRenderNode`, ele só aceita confirmação se o novo boot provar
`V5RegistrationMode=1`, tunable presente/solicitado, `V5StartDeviceStatus=0`,
uma source, um child, display funcional e novamente one-shot/P1/P2 físicos. O
watchdog também exige que a confirmação declare o RenderNode validado. O novo
`prepare-bc250mcdm-rendernode.ps1` executa preflight, snapshot, apply e readback
de `RenderNode=1`, com restauração do snapshot em qualquer falha. Parser,
auditoria estática e dry-run readonly passaram.

### Primeiro boot V5 real do 3.64: Start completo, remoção posterior (01/09/2026)

O boot protegido ocorreu às `09:20:04 -03:00`. Houve imagem, mas a confirmação
foi corretamente recusada porque o adaptador terminou em `bc250mcdm` Código
43. O watchdog permaneceu `OBSERVING`; nenhuma confirmação foi forçada.

A captura preservada em
`docs/evidence/2026-09-01_09-18-51-DESKTOP-HQAG6T4-mcdm-reboot-watchdog`
prova avanço substancial:

- `V5RenderNodeRequested=1`, tunable presente e `V5RegistrationMode=1`;
- `DxgkInitialize`, `AddDevice` e `V5StartDeviceStatus` iguais a zero;
- uma source e um child publicados;
- display chegou a commit, present, VSync e 487 interrupções de controle;
- one-shot e shader físicos passaram;
- P1 passou e P2 aposentou novamente 16/16 submissões;
- `QueryAdapterInfo` respondeu `DXGKQAITYPE_WDDMDEVICECAPS` com sucesso;
- depois disso o dxgkrnl chamou Stop/Remove e o dispositivo ficou em Código
  43, sem evento relevante no log do Sistema.

Logo o driver realmente entrou no contrato WDDM display+render e executou GPU,
mas o adaptador não permaneceu operacional. **REGISTRO V5/START/P1/P2 PROVADOS
NO SILÍCIO; RENDERNODE OPERACIONAL REPROVADO POR CÓDIGO 43**.

Dois débitos de diagnóstico/teardown foram encontrados no fonte:

1. `Bc250ProbeSnapshotDdiTrace` existia, mas não era chamado; por isso
   `V5LastDdi/PrevDdi` não foram publicados antes do Stop;
2. `RemoveDevice` chamava `StopDevice` pela segunda vez, devolvendo
   `STATUS_DEVICE_BUSY` (`0x80000011`) e mascarando o teardown.

O próximo candidato publica o rastro DDI no início do Stop, marca também
`QueryInterface` e só executa Stop a partir de Remove quando o contexto ainda
está ativo. A compilação integral, todos os testes contratuais, matriz DDI e
auditoria de segurança passaram. **CORREÇÕES OFFLINE; NÃO VALIDADAS EM
HARDWARE**.

### Transação GPUVA e reboot guard armados (01/09/2026)

O `Prepare` salvou o valor original `GpuvaLabPromotion=0`, hash do SYS 3.62 e
identidade exata do dispositivo; aplicou 1 e confirmou readback. O reboot guard
foi então armado como tarefa SYSTEM `BC250-RebootGuard`, estado `Ready`, trigger
de startup e timeout 600 s. A configuração fixa 3.62.0.0, schema 128,
`SkipHwBringup=0`, `oem20.inf` e hashes SYS/INF/recovery.

Os scripts instalados em `C:\BC250_REBOOT_GUARD` casam byte a byte com as fontes.
A sessão não elevada não conseguiu enumerar a tarefa por ACL, mas o readback
elevado exportado pelo próprio armador comprova `enabled=true`, estado `Ready`,
ação PowerShell SYSTEM correta e `reboot_executed=false`. O candidato está pronto
para desligamento completo e boot frio protegido. **NÃO VALIDADO EM HARDWARE**.

### Desligamento híbrido não contou como novo boot (01/09/2026)

Após desligar e retirar a energia, a confirmação foi corretamente recusada como
“ainda no boot de armamento”. O diagnóstico mostrou `HiberbootEnabled=1` e
`LastBootUpTime=31/08/2026 14:33:45`, com ticks exatamente iguais aos salvos no
guard. Portanto o Windows restaurou o kernel pelo hiberfile: a tarefa `AtStartup`
não rodou e o driver ativo ainda reporta `GpuvaLabPromotionRequested=0`, apesar
do DWORD preparado estar em 1.

O dispositivo continua em 3.62.0.0, `bc250mcdm`, Código 0. A transação e o guard
não foram consumidos. A retomada correta é um **Restart** do Windows, que força
boot completo mesmo com Fast Startup habilitado; depois confirmar o guard e usar
caminho absoluto no `Inspect`. **NÃO VALIDADO EM HARDWARE**.

### Resultado pós-reboot do gate GPUVA 3.62 (01/09/2026)

O `Restart` produziu um boot real em `01/09/2026 04:37:41 -03:00`. O reboot
guard aceitou a confirmação visual e foi desarmado. O dispositivo permaneceu em
`bc250mcdm`, Código 0, versão 3.62, schema 128; commit, present e VSync do display
continuaram funcionais.

O `Inspect` da transação GPUVA reprovou corretamente e não submeteu trabalho:

- política solicitada/presente igual a 1;
- nenhum gate elegível ou operacional publicado;
- nenhuma geração, epoch ou faixa viva de snapshot;
- submission engine ausente/offline;
- `HqdActiveStatus=1`, decodificado como `BC250_HQD_ACTIVE_BAD_ARGUMENT`.

Portanto houve prova física de display e prova física do comportamento
fail-closed, mas não de compute, GPUVA ou render. O DWORD
`GpuvaLabPromotion` continua observado como 1 no Registry e deve ser restaurado
pela ação `Rollback` da transação antes do próximo ciclo. **GPUVA REPROVADO COM
SEGURANÇA — NÃO VALIDADO EM HARDWARE**.

### Débito crítico encontrado no candidato 3.62 (01/09/2026)

A análise do caminho executável encontrou uma regressão de integração, não uma
limitação do silício:

1. A transação HQD one-shot zerava `BC250_COMPUTE_MQD_PROPERTIES`, mas não
   preenchia `SchedulerMe`, `SchedulerPipe` e `SchedulerQueue`. O contrato
   rejeitava `SchedulerMe=0` antes de qualquer acesso à fila, explicando
   exatamente o status físico observado.
2. O tunable `PersistentSubmission` existia no INF e no contexto, mas nunca era
   lido no `StartDevice`.
3. `Bc250SubmissionEngineOnline`, `Bc250SubmissionEngineSelfTest` e
   `Bc250SubmissionEngineP2StreamTest` estavam implementados, mas não possuíam
   chamada no miniport ativo.
4. O sucesso do `StartDevice` exigia PSP e SDMA, mas não exigia o gate one-shot,
   P1 ou P2 mesmo quando o pacote declarava shader e submissão persistente.
5. `StopDevice` não chamava `Bc250SubmissionEngineOffline` antes do teardown do
   core.

Assim, a declaração `PersistentSubmission=1` do pacote 3.62 era apenas
declarativa. As provas históricas P1/P2 continuam válidas para os respectivos
binários antigos, mas não podem ser transferidas ao 3.62. Este é débito técnico
crítico e invalida qualquer alegação de render no candidato 3.62.

### Candidato 3.63: reintegração transacional P1/P2 (01/09/2026)

O miniport ativo foi corrigido para executar a sequência física completa:

`[PREFLIGHT one-shot] -> [HQD Online] -> [P1 SelfTest] -> [P2 16 lotes] ->`
`[READBACK/TELEMETRIA] -> [HQD persistente viva ou Offline em falha]`.

Mudanças efetivas:

- one-shot e pré-programação usam MEC2/Pipe0/Queue0;
- `PersistentSubmission` é lido em `PASSIVE_LEVEL`, com presença registrada;
- opt-ins aceitam somente os valores exatos 0 ou 1;
- P1 e P2 são chamados somente após o one-shot provar `PhysicalReady`;
- `StartDevice` só retorna sucesso com P1 e P2 fisicamente prontos quando a
  submissão persistente foi solicitada;
- qualquer falha executa `Offline`, mantém render fechado e devolve erro;
- `StopDevice` desativa a engine antes do teardown readonly;
- o auditor estático agora reprova pacote que anuncie a funcionalidade sem as
  chamadas `Online`, P1, P2, telemetria, `Offline`, seletores BC-250 e gates
  `PhysicalReady`.

A suíte contratual integral, a compilação específica do caminho destrutivo de
laboratório, `Inf2Cat`, assinaturas, auditor do pacote e auditor de segurança
passaram. O novo pacote é `09/01/2026,3.63.0.0`, schema 128, com
`GpuvaLabPromotion=0`:

- SYS: `A436F65712203B8898EA826A0E272F4FBF7F54A5795DFFFBE7DC131D726FBDE8`;
- INF: `55F1257D565D59D75BC757B9E6DDED4CB4D5D0D09105E19D4EA152E9620D4195`;
- CAT: `C22851C5B729377C1F948C810EEC0753ED869144F515587D8F0C837FBE4A63C8`.

Nenhuma instalação ou troca de binding foi feita. O próximo ensaio deve primeiro
restaurar a transação GPUVA antiga, depois vincular 3.63 com watchdog e validar,
na mesma captura, display, one-shot, P1 e P2. Somente após essa prova deve-se
preparar novamente a promoção GPUVA. **CANDIDATO 3.63 APROVADO OFFLINE — NÃO
VALIDADO EM HARDWARE**.

### Rollback da promoção GPUVA confirmado (01/09/2026)

O operador executou `Rollback` usando a transação original. O harness restaurou
e verificou o snapshot `GpuvaLabPromotion=0`; uma segunda leitura independente
do Registry confirmou zero. O pacote 3.63 também foi reauditado imediatamente
antes do ensaio: hashes SYS/INF coincidem com o manifesto, schema 128, shader e
P1 habilitados, promoção GPUVA desabilitada. O 3.62 ainda carregado só descarta a
política antiga em memória no próximo `StartDevice`; nenhuma submissão deve ser
feita antes disso. **ROLLBACK CONFIGURACIONAL CONCLUÍDO — NÃO VALIDADO EM
HARDWARE**.

### Vitória física do candidato 3.63 — integração P1/P2 restaurada (01/09/2026)

O candidato 3.63 foi vinculado como `oem20.inf` com watchdog de 600 segundos.
`UpdateDriverForPlugAndPlayDevicesW` retornou sucesso sem exigir reboot; o
dispositivo permaneceu em `bc250mcdm`, Código 0, com display visível. O SYS
instalado corresponde ao pacote:
`A436F65712203B8898EA826A0E272F4FBF7F54A5795DFFFBE7DC131D726FBDE8`.
O watchdog foi desarmado por confirmação visual às `04:55:42 -03:00`.

A captura imutável do binding contém 849 valores e prova, no mesmo ciclo:

- schema 128, `SkipHwBringup=0` e `GpuvaLabPromotionRequested=0`;
- one-shot: `HqdActiveStatus=0`, `HqdActivePhysicalReady=1` e
  `ShaderPhysicalReady=1`;
- engine P1 online, ativação zero, self-test tentado e `PhysicalReady=1`;
- P1: `WPTR=12`, `RPTR MMIO=12`, `RPTR report=12`, tripé aprovado;
- P2: 16 submissões aceitas e 16 aposentadas, zero divergências;
- P2: `WPTR=204`, `RPTR MMIO=204`, `RPTR report=204`, tripé aprovado.

Os harnesses independentes `test-p1-submission-acceptance.ps1` e
`test-p2-multi-submission.ps1` terminaram ambos com código 0 e publicaram
`p1-submission-acceptance.json` e `p2-multi-submission-acceptance.json` no
diretório de evidência
`docs/evidence/2026-09-01_04-54-39-DESKTOP-HQAG6T4-lab-binding`.

Isto valida no silício a reintegração one-shot/P1/P2 do binário 3.63 e elimina a
regressão observada no 3.62. Ainda não valida GPUVA dinâmica, submissão originada
por aplicação WDDM, Vulkan ou jogos. `RenderNode=1` só será consumido por
`DriverEntry` no próximo boot protegido. **FASE P1/P2 DO CANDIDATO 3.63 VALIDADA
EM HARDWARE; GPUVA/RENDER AINDA NÃO VALIDADOS**.

### Boot protegido do 3.63 e falsa ativação do RenderNode (01/09/2026)

O reboot guard do 3.63 foi armado, o Windows reiniciou de fato às `04:58:06` e
a confirmação visual atual foi aceita. O guard terminou `DISARMED`, o dispositivo
permaneceu `bc250mcdm` Código 0 e display/commit/present/VSync continuaram
funcionais. P1 e P2 foram revalidados no novo boot por harnesses independentes:
tripés aprovados, P1 `12/12` e P2 `16/16`, `204/204`, zero divergências.

Porém `V5RegistrationMode` e os campos de seleção do RenderNode não foram
publicados, apesar de `RenderNode=1` estar presente no Registry. A inspeção do
`DriverEntry` demonstrou que o binário 3.63 sempre chamava
`DxgkInitializeDisplayOnlyDriver`. A única referência a `DxgkInitialize` estava
protegida por `DriverObject == 0xDEADBEEF`, condição impossível em execução real
e mantida apenas para satisfazer o link/auditoria estrutural.

Logo o reboot 3.63 valida estabilidade de display e repetibilidade P1/P2, mas
**não exercitou WDDM display+render**. A ausência de Código 43 não pode ser usada
como prova do RenderNode. Este achado foi classificado como regressão crítica de
integração e falso positivo de auditoria.

### Candidato 3.64 — seleção WDDM real e telemetria V5 (01/09/2026)

O caminho morto foi removido. `DriverEntry` agora lê `RenderNode` em
`PASSIVE_LEVEL` e aplica política estrita:

- zero ou ausente registra por `DxgkInitializeDisplayOnlyDriver`;
- presente e exatamente igual a 1 registra por `DxgkInitialize` usando a tabela
  WDDM 2.7 completa;
- qualquer valor diferente de 0/1 falha com erro de configuração.

Um wrapper real de `StartDevice` registra status, quantidade de fontes e filhos
para os dois modos. O carregamento zera o rastro V5 antes do registro para que
resíduo de boot anterior não seja aceito. O auditor estático passou a exigir a
leitura do tunable, os dois caminhos alcançáveis, a telemetria V5 e a ausência de
`0xDEADBEEF` no `DriverEntry`.

A suíte integral, o objeto de laboratório, o link, `Inf2Cat`, assinaturas e os
dois auditores passaram. Pacote `09/01/2026,3.64.0.0`, schema 128:

- SYS: `7CF0AEB87D91204508D384E87970DE7B97BF8BDDA950159185A5BCC716AE4DCF`;
- INF: `A5CC94EFACDD37AE096C35F2243DCFA375C28CE3CF6E1567B31F727367F69A9E`;
- CAT: `87DC2E4128FA99F7BC7C61EFE490099E9F1D9BC6264D403BFDB1369A2BB5547D`.

O pacote mantém `RenderNode=0` e `GpuvaLabPromotion=0` por padrão. O ensaio será
transacional em duas fases: binding KMDOD para validar o novo binário e P1/P2;
depois `RenderNode=1` e reboot guard para exercer o registro WDDM real. **3.64
APROVADO OFFLINE — NÃO VALIDADO EM HARDWARE**.

O binder de laboratório também foi corrigido antes do ensaio. Todos os tunables
lidos por `DriverEntry`/`StartDevice` — incluindo `RenderNode`, shader, receita
F8b, submissão persistente e promoção GPUVA — agora são fotografados, aplicados
e verificados **antes** de `UpdateDriverForPlugAndPlayDevicesW`. A política é
relida depois do binding e a transação publica `prebind-policy.json`. Isso evita
que o valor residual `RenderNode=1` do teste anterior faça o 3.64 entrar em WDDM
completo durante uma fase anunciada como KMDOD. O auditor passou a verificar
também essa ordem.

### Ensaio 3.64 fase KMDOD interrompido por HQD residual (01/09/2026)

O primeiro binding físico do 3.64 foi deliberadamente solicitado sem
`-EnableRenderNode`. A política foi aplicada e relida antes do carregamento:
`RenderNode=0`, `ShaderDispatch=1`, `PersistentSubmission=1` e
`GpuvaLabPromotion=0`. O SYS instalado correspondeu ao hash do pacote 3.64.
`DriverEntry`, `DxgkInitialize` do modo display-only, `AddDevice` e o wrapper de
`StartDevice` foram alcançados, mas o dispositivo terminou em Código 43.

A evidência
`docs/evidence/2026-09-01_08-54-32-DESKTOP-HQAG6T4-lab-binding` mostra:

- `V5RenderNodeRequested=0`, presente e `V5RegistrationMode=0`: o caminho
  display+render não foi solicitado nem testado;
- `V5StartDeviceStatus=0xC00000A3` (`STATUS_DEVICE_NOT_READY`);
- `HqdActiveStatus=7`, primeira falha no estágio de restauração
  `ACTIVE_DEACTIVATE`, offset catalogado `0xC82C`, esperado 0 e observado 1;
- P1 e P2 não foram tentados no novo ciclo;
- `StopDevice` foi alcançado e `RemoveDeviceStatus=0x80000011`
  (`STATUS_DEVICE_BUSY`).

O teste começou imediatamente após uma sessão 3.63 que mantinha a fila
persistente fisicamente online. Portanto esse resultado mede uma transição de
binário sobre HQD residual, não a sanidade do 3.64 em boot limpo e muito menos o
RenderNode. A confirmação visual foi corretamente recusada porque o serviço não
estava em Código 0. **ENSAIO INVÁLIDO PARA RENDER; CÓDIGO 43 REAL E
FAIL-CLOSED; NÃO VALIDADO EM HARDWARE PARA DISPLAY+RENDER**.

Dois débitos do harness foram corrigidos após o diagnóstico:

1. o preflight de binding passou a considerar também `P1EngineState`,
   `P1SelfTestPhysicalReady` e `P2PhysicalReady`, além de HQD ACTIVE e WPTR;
2. o rollback só declara sucesso depois de reler `BasicDisplay` com Código 0,
   inclusive após um eventual `restart-device`; caso contrário falha e aponta
   `C:\BC250_RECOVERY.cmd`.
3. o rollback publica `C:\BC250_REQUIRE_CLEAN_BOOT.json` com a identidade do
   boot; o binder recusa novo ensaio no mesmo boot e só remove o marcador após
   `LastBootUpTime` mudar.

Sequência obrigatória para repetir: deixar o watchdog restaurar BasicDisplay,
confirmar BasicDisplay/Código 0, executar boot frio com o miniport BC250 fora do
binding e somente então testar 3.64 em `RenderNode=0`. Reiniciar enquanto o
3.63/3.64 com `PersistentSubmission=1` ainda está instalado não limpa o ensaio,
pois o próximo `StartDevice` religa a fila.

Às `09:04:34 -03:00`, o watchdog do ensaio 3.64 expirou e executou o rollback
automaticamente. O estado final independente foi `BasicDisplay`, Código 0;
`oem20.inf` foi desinstalado e excluído, o processo do watchdog terminou e o
estado transacional foi publicado como `ROLLED_BACK`. O marcador
`C:\BC250_REQUIRE_CLEAN_BOOT.json` contém a identidade do boot do rollback e
impede novo binding nesta sessão. **ROLLBACK/TEARDOWN EXTERNO VERIFICADO;
COMPUTE/RENDER NÃO VALIDADOS NESTE CICLO**.

### Aborto pré-bind do segundo ensaio 3.64 (01/09/2026)

Após o boot frio, o preflight confirmou `BasicDisplay` Código 0, identidade de
boot nova, ausência de watchdog concorrente e pacote 3.64 íntegro. O binder
estagiou `oem20.inf` e armou o watchdog, mas abortou antes de aplicar a política
e antes de `UpdateDriverForPlugAndPlayDevicesW`: `Get-ItemPropertyValue` lançou
exceção ao fotografar `SkipHwBringup`, que legitimamente ainda não existia na
chave recém-criada. O dispositivo permaneceu em `BasicDisplay` Código 0; nenhum
MMIO, P1/P2 ou RenderNode foi exercitado.

O snapshot foi corrigido para consultar a coleção `PSObject.Properties` e
representar propriedades ausentes como `null`. Parser, auditoria de segurança e
teste readonly com a propriedade realmente ausente passaram. O rollback também
distingue agora aborto pré-bind de teardown real: só exige novo boot quando o
estado anterior não era `BasicDisplay` Código 0. **FALHA DE HARNESS CORRIGIDA;
NÃO VALIDADO EM HARDWARE**.

### Candidato 3.65 — KMDOD limpo e isolamento do Código 43 V5 (01/09/2026)

Depois do rollback e boot frio, o candidato 3.65 foi vinculado com
`RenderNode=0`, `ShaderDispatch=1`, `PersistentSubmission=1` e promoção GPUVA
desligada. A evidência
`docs/evidence/2026-09-01_09-36-52-DESKTOP-HQAG6T4-lab-binding` confirmou
`bc250mcdm` Código 0, display/commit/present/VSync e novamente os tripés físicos
P1 (`12/12`) e P2 (16/16; `204/204/204`; zero divergências). Isso prova que o
novo binário não regrediu o caminho KMDOD.

No boot seguinte com `RenderNode=1`, a evidência
`docs/evidence/2026-09-01_09-42-06-DESKTOP-HQAG6T4-rendernode-inspect` mostrou
registro V5 real, `DxgkInitialize`, `AddDevice` e `StartDevice` com sucesso,
uma fonte/um filho, display ativo e P1/P2 fisicamente prontos. Mesmo assim o
dxgkrnl chamou `StopDevice` e `RemoveDevice`; o dispositivo terminou em Código
43 e `RemoveDeviceStatus=0x80000011` (`STATUS_DEVICE_BUSY`). O rastro final foi
`QueryInterface -> QueryChildRelations -> QueryInterface -> StopDevice`.

As interfaces com Data1 `AF03F190` e `E922004D` foram identificadas nos headers
locais do WDK como interface Miracast/UMDF e `GUID_DEVINTERFACE_WDDM3_ON_VB`.
`6BA41A33` continua sem identificação confiável e não será implementada por
suposição. O autoteste P1/P2 não alimenta o rastreador de fences WDDM; somente o
caminho de submissão do dxgkrnl o faz. Portanto o `DEVICE_BUSY` pode representar
uma contagem real ou uma falha na consulta sincronizada, causas que o schema 128
não separa. **START V5 E SILÍCIO P1/P2 PROVADOS; VIDA ÚTIL DO RENDERNODE FALHOU;
JOGOS/VULKAN NÃO VALIDADOS**.

### Candidato 3.66 — telemetria causal do StopDevice (01/09/2026)

O candidato 3.66 mantém integralmente o comportamento fail-closed e adiciona
telemetria somente diagnóstica:

- `StopDeviceStatus` agora é publicado em todas as saídas do caminho ativo;
- `V5StopFenceQueryMode` distingue leitura direta de consulta serializada;
- `V5StopFenceSyncStatus` e `V5StopFenceSyncResult` registram o contrato de
  `DxgkCbSynchronizeExecution`;
- `V5StopFencePendingCount` e `V5StopFenceReservedCount` registram as contagens
  observadas dentro da rotina sincronizada.

O schema foi elevado para 129. Build integral, contratos offline, matriz DDI,
auditoria fail-closed, parser dos harnesses, Inf2Cat e assinatura passaram.
Pacote `09/01/2026,3.66.0.0`:

- SYS: `A546F8BA2A26ECDB4BDE13FC23C4EAB401EEC7B7E49D76CF56A1A02D0E60140E`;
- INF: `B624F23E0E4664B9087E52D60A8AE8E72A23A2CFD05D724849C3B5DA0326DE01`;
- CAT inicial: `6BBD343B1468973BC2EFFF6BE3B501853F6F11194850CE1B1908042DF79344D8`.

Após a correção final do recovery, o pacote foi novamente assinado e auditado
com `RecoverySha256=B61342F920B60C94C083D5B7FA14B56238A683245C1CD5D5BC264C8167A026D7`.
SYS e INF permaneceram idênticos; o CAT vigente passou a
`CC8EAEBC38D9FE470F9AB2B2009C31506A56D810170197CAE3DF39A6EABE4C6C`.

Por decisão explícita do operador, os próximos ensaios não usarão watchdog. O
binder agora oferece `-DisableWatchdog -IAcceptNoAutomaticRollback`, recusa esse
modo sem aceite explícito e preserva a exigência de
`C:\BC250_RECOVERY.cmd`. Ele também continua recusando qualquer watchdog antigo
concorrente, evitando rollback inesperado. A máquina de estados e o snapshot de
evidência permanecem obrigatórios; apenas o temporizador automático foi
retirado. **3.66 APROVADO OFFLINE — NÃO VALIDADO EM HARDWARE**.

### Recovery elevado no Windows normal (01/09/2026)

Por autorização explícita do operador, `tools/BC250_RECOVERY.cmd` não exige
mais Modo de Segurança. O fluxo elevado agora desabilita a instância PCI exata,
remove somente pacotes com a identidade BC-250 estrita, exclui os serviços
experimentais, apaga os três binários de nomes exatos, reabilita o dispositivo
e exige `BasicDisplay` Código 0 por CIM. Se um binário continuar carregado, o
script falha fechado e pede reboot/reexecução. A fonte passou em `/DRY-RUN` e na
auditoria de segurança; a cópia em `C:\` ainda precisa ser atualizada por um
terminal elevado antes da execução real. **RECOVERY ATUALIZADO OFFLINE; AINDA
NÃO EXECUTADO NO HOST**.

### Resultado físico 3.66 — schema 129 responde: falha de sincronização (01/09/2026)

O ensaio sem watchdog foi executado. `RenderNode=1` foi promovido, o boot
ocorreu e a captura saiu no primeiro `StartDevice`. Evidência:
`docs/evidence/2026-09-01_11-34-36-DESKTOP-HQAG6T4-rendernode-inspect`.

Registro V5 real e silício preservado: `V5RegistrationMode=1`,
`DxgkInitializeStatus=0`, `AddDeviceStatus=0`, `V5StartDeviceStatus=0` com uma
fonte e um filho, `HqdActiveStatus=0`, `ShaderPhysicalReady=1`,
`P1EngineState=1`, `P1SelfTestPhysicalReady=1`, `P2PhysicalReady=1`. O
dispositivo terminou em Código 43.

O bloco causal do schema 129 fechou a pergunta para a qual foi criado:

| Campo | Valor |
|---|---|
| `V5StopFenceQueryMode` | 2 (consulta serializada tentada) |
| `V5StopFenceSyncStatus` | `0xC0000001` (`STATUS_UNSUCCESSFUL`) |
| `V5StopFenceSyncResult` | 0 (`queried == FALSE`) |
| `V5StopFencePendingCount` | 0 |
| `V5StopFenceReservedCount` | 0 |
| `StopDeviceStatus` / `RemoveDeviceStatus` | `0x80000011` (`STATUS_DEVICE_BUSY`) |

Dos cinco casos possíveis, quatro ficam **descartados por medição**: não havia
contagem pendente, não havia reserva não cancelada, e o callback nunca chegou a
executar — logo não pode ter retornado falso. O caso confirmado é **falha em
`DxgkCbSynchronizeExecution`**. O `STATUS_DEVICE_BUSY` foi autoinfligido pelo
ramo fail-closed de `Bc250ActiveFenceTrackerHasPending`, com o rastreador
comprovadamente vazio.

Isto **não** é a causa raiz do Código 43. O teardown já estava em curso quando o
`DEVICE_BUSY` apareceu; ele é consequência. A causa continua aberta.

Achado novo e independente: `V5QaiCalls=1`, tipo `0x1D`
(`DXGKQAITYPE_WDDMDEVICECAPS`), status `0`. A correção anterior de
WDDMDEVICECAPS funcionou — aquele tipo devolvia `0xC00000BB`. Mas
`DXGKQAITYPE_DRIVERCAPS` **nunca foi solicitado**. A contabilidade dos 18 marks
fecha exata: `QueryAdapterInfo` ×1 + `QueryChildRelations` ×1 +
`QueryInterface` ×8 marcada em dobro (defeito de instrumentação) = 18, ou seja
10 DDIs reais. Nenhuma DDI de render foi tocada: sem `CreateDevice`, sem
`CreateContext`, sem `CreateAllocation`, sem `QUERYSEGMENT`.

Interfaces pedidas, pelos três `Data1` que sobreviveram: `AF03F190` é
`GUID_DEVINTERFACE_MIRACAST_DISPLAY` (`dispmprt.h`), `E922004D` é
`GUID_DEVINTERFACE_WDDM3_ON_VB` (`d3dkmddi.h`, `Version=1`, `Size=56`), e
`6BA41A33` **não existe** em 10.0.22621 nem em 10.0.26100 — permanece sem
identificação e não será implementada por suposição. As cinco primeiras
chamadas de oito foram perdidas: só três `Data1` eram guardados, e sem os 16
bytes não há identificação possível.

**CASO DO `DEVICE_BUSY` IDENTIFICADO; CAUSA DO CÓDIGO 43 AINDA ABERTA; NÃO
VALIDADO EM HARDWARE PARA RENDER WDDM**.

### Candidato 3.67 — quiescência do teardown e rastro completo (01/09/2026)

Duas frentes, um único candidato. Schema elevado para 130.

**A. O rastreador passou a ter serialização própria e o teardown, quiescência.**

A correção deliberadamente **não** é ler `Count`/`ReservedCount` direto. Não
está provado que a interrupção já foi desconectada nesse ponto do ciclo PnP, e
mesmo que estivesse restariam o timer de retirement (armado em 1 ms), o DPC,
submissões concorrentes e cancelamentos de reserva. Trocar uma leitura errada
por outra leitura errada não seria correção.

`BC250_WDDM_FENCE_TRACKER` ganhou lock próprio, usado em todos os caminhos —
reserve, commit, cancel, attach-pin, push, drain, query e a fotografia do
`StopDevice`. A aquisição é válida em `PASSIVE_LEVEL`, `DISPATCH_LEVEL` e
DIRQL: abaixo de `DISPATCH` sobe o IRQL antes de girar, a partir dele apenas
gira. O giro é **limitado** — sem watchdog no laboratório, travar o processador
em giro infinito custa um boot frio às cegas; estourar o limite devolve falha,
é registrado e faz o chamador falhar fechado. Perder o lock nunca é
interpretado como "rastreador vazio". `Push` toma o lock uma única vez para
reserve+commit, para que o atalho não seja menos atômico que o caminho longo.

O contexto ganhou `LifecycleState` (`NOT_STARTED=0`, `RUNNING`, `STOPPING`,
`STOPPED`) e `SubmitInFlight`. `NOT_STARTED` vale zero de propósito: memória
zerada tem de ser o estado que **recusa** trabalho. `SubmitCommandVirtual`
passou a ter um invólucro com ponto único de saída — espalhar
`InterlockedDecrement` pelos dezoito retornos do corpo seria a forma clássica
de vazar a contagem em um caminho de erro e travar o teardown para sempre.

`Bc250ActiveQuiesceAndSnapshotTracker` executa, nesta ordem: `STOPPING` e
portão de admissão fechado; timer de retirement parado; espera das submissões
em voo com dreno de DPC (limite de 400 × 5 ms); fotografia sob o lock. Só então
o veredito. A consulta serializada continua sendo feita, mas como **observação**
— o próximo ensaio ainda mostra se `DxgkCbSynchronizeExecution` funciona no
`StopDevice`, sem que a resposta dele decida o teardown.

O critério fail-closed não foi afrouxado: contagem diferente de zero continua
devolvendo `STATUS_DEVICE_BUSY`, e falha de lock ou dreno incompleto também.

**B. Telemetria ampliada.**

`DxgkDdiQueryInterface` passou a gravar o GUID inteiro — `Data1`, `Data2`,
`Data3`, os oito bytes de `Data4`, `Version`, `Size`, status e ordem da chamada
— em buffer circular de 16 slots em memória, publicado uma única vez no
teardown como `REG_BINARY` (`V5QiSlot00`..`V5QiSlot15`). A DDI não paga
`ZwOpenKey` por chamada. Nenhuma interface é aceita: a recusa continua sendo o
único retorno.

Campos novos: `V5StopReason`, `V5StopTimerDrained`, `V5StopDpcDrained`,
`V5StopSubmitInFlight`, `V5StopDrainIterations`, `V5StopSnapshotOk`,
`V5StopLockFailures`, `V5StopRejectedWhileStopping`, `V5StopFirstStatus`,
`V5LifecycleState`, `V5ChildRelationsStatus/Count/Calls`, `V5PostStartStage`
(marco monotônico do ponto mais avançado alcançado após o `StartDevice`) e
`V5QiSlotsCaptured`.

**Higiene, com dois defeitos reais corrigidos.**

1. A marcação duplicada de `Bc250ProbeMarkDdi(BC250_V5_DDI_QUERY_INTERFACE)`
   foi removida — era ela que inflava `V5DdiMarks`.
2. `Bc250ProbeResetSubmissionEvidence` estava declarada, implementada e
   documentada com "chamar em DriverEntry", mas **não possuía um único chamador
   em todo o repositório**: o reset de evidência P1/P2 que a documentação
   afirmava existir nunca executou. Agora executa, antes do bring-up e seguido
   por publicação física nova.
3. `Bc250ProbeResetRenderTrace` zera o bloco V5 completo e os campos DMM. O
   bloco DMM é enumerado à mão porque a faixa do enum não é limpa
   (`BC250_PROBE_SKIP_HW_BRINGUP` mora no meio dela e ecoa um tunable, não uma
   medição). O bloco P1/P2 mantém reset próprio e não é arrastado.
4. `function_body` em `audit-bc250mcdm-safety.py` não pulava declarações
   antecipadas: uma função com protótipo devolvia o corpo da função **seguinte**,
   e toda exigência sobre ela passava pelo motivo errado — falso negativo
   silencioso em um gate fail-closed.

**Testes offline novos** (`driver/tests/active_miniport_test.c`):

- `Bc250RunStopQuiescenceTest` — reproduz a falha do silício com um stub de
  `DxgkCbSynchronizeExecution` que devolve `STATUS_UNSUCCESSFUL` sem invocar a
  rotina. Quatro casos: rastreador vazio conclui o teardown; reserva pendente
  continua `STATUS_DEVICE_BUSY`; fence em voo continua `STATUS_DEVICE_BUSY`;
  `RemoveDevice` direto conclui. Confere que o rastreador **não** foi limpo
  para fabricar sucesso e que nenhuma reserva nova entra após `STOPPING`.
- `Bc250RunTrackerRaceTest` — threads reais de host disputando
  reserve/commit/cancel e drain enquanto o `StopDevice` fecha o portão e
  fotografa, 16 rodadas. Invariantes: aceitas = aposentadas + em voo, nenhuma
  reserva órfã, e sucesso do `StopDevice` exige rastreador vazio.
- `Bc250RunRenderTraceResetTest` — prova que o reset zera rastro de DDI, slots
  de `QueryInterface` e campos DMM, e que o bloco P1/P2 sobrevive a ele.

**Resultados offline**: build contratual completo e toda a suíte de testes
`PASS`; `audit-mcdm-ddi-matrix.py` `PASS (STRUCTURAL ONLY)`;
`audit-bc250mcdm-safety.py` `PASS`; `audit-bc250mcdm-lab-package.ps1` `PASS`
com `mode: P1 PERSISTENT SUBMISSION`.

Pacote `09/01/2026,3.67.0.0`, schema 130, receita idêntica à do 3.66
(`SkipHwBringup=0`, `ShaderDispatch=1`, `CpSoftReset=1`, `Vmid0=0`,
`SdmaAutoCtxSw=0`, `PersistentSubmission=1`, `GpuvaLabPromotion=0`,
`RenderNode=0`):

- source SYS: `24C5FEF61CA8667C25338CD969B546F8A39502BBD848FE00236298DA0A130021`;
- SYS: `E99FC3043D43EFA473F17F486A3844AD484069A6105F975D68DB2E239692E8D8`;
- INF: `C192D3A208FB751466CD3206C0254D40591E927513F5483AAAEA9D504355D059`;
- CAT: `72B0FC0F06301BA126248E8BB1DA2FE052E5D4098FE14B22F74BB884E4E657F4`;
- recovery: `B61342F920B60C94C083D5B7FA14B56238A683245C1CD5D5BC264C8167A026D7`.

**Débito registrado**: `package-bc250mcdm-lab.ps1` tem `-EnableShaderDispatch` e
`-EnablePersistentSubmission` desligados por padrão. Empacotar sem as flags
produz um candidato silenciosamente sem P1/P2, com o mesmo número de versão e
sem nenhum aviso — aconteceu na primeira tentativa de empacotar o 3.67 e só foi
detectado pela conferência do manifesto contra o do 3.66. O empacotador
deveria exigir a receita explicitamente ou herdá-la do manifesto anterior.

Sobre `DRIVERCAPS`: nada foi respondido artificialmente e nenhuma capability
foi alterada por tentativa. A ausência da consulta permanece registrada como
achado, não como causa. O próximo passo é identificar os oito
`QueryInterface` completos, em especial o GUID iniciado em `6BA41A33`.

**3.67 APROVADO OFFLINE — NÃO VALIDADO EM HARDWARE PARA RENDER WDDM**.

#### Revisão final de concorrência do 3.67

Antes de qualquer instalação, uma segunda auditoria encontrou duas janelas no
teardown: submissão já admitida podia rearmar o timer depois da primeira parada;
e um retorno busy deixava `STOPPING` com retirement desligado, impedindo uma
fence real de progredir. A revisão final corrige ambas sem reabrir admissões:

- o fim de `SubmitCommandVirtual` só rearma o timer em `RUNNING`;
- depois de `SubmitInFlight == 0`, a quiescência para o timer novamente e drena
  DPCs antes do snapshot;
- um Stop busy mantém o portão fechado, mas rearma retirement para trabalhos já
  aceitos; uma nova chamada pode progredir depois da aposentadoria/cancelamento;
- o caminho observacional `PENDING` também fotografa pelo lock próprio;
- o portão virou função testável, com teste concorrente que entra antes de
  `STOPPING` e prova saída com timer parado.

Build integral, teste ativo, matriz DDI e auditoria de segurança passaram
novamente. Pacote final 3.67/schema 130, ainda não instalado:

- source SYS: `76DC250B83A32707A5B422E137A41CE908897C5D5E09CECCF5EE91C9B7BEDBCF`;
- SYS: `BBEAB17FE4276B3E6F71BE13981BEA640E5B74CB339750B154A716E120D8085F`;
- INF: `C192D3A208FB751466CD3206C0254D40591E927513F5483AAAEA9D504355D059`;
- CAT: `0FC2B1FF5A7BD984293ED7ED081358A7B7800CC4031B85960CBD6887C712A47E`.

**REVISÃO FINAL 3.67 APROVADA OFFLINE — NÃO VALIDADO EM HARDWARE PARA RENDER
WDDM**.
