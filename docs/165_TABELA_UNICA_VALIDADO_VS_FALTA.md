# Tabela única — o que já está validado no hardware e o que falta para WDDM com jogos

**Data-base:** 2026-09-02 · **Candidato atual:** 3.81.0.0 (assinado, ensaio pendente)
**Fonte:** consolidação de `CHECKLIST_COMPLETO_BC250_WDDM_DISPLAY_RENDER.md`, `docs/161`, `docs/162`, `docs/163`, `docs/164`
**Numeração `#`:** preservada do checklist original, para rastreabilidade.

**Estado:** ✅ validado no silício · 🧪 provado só offline · 🟡 parcial · ⛔ bloqueado · ⬜ não iniciado · ❌ reprovado
**Fase:** ordem de execução do roadmap [164](164_ROADMAP_WDDM_FULL_DISPLAY_DCN.md). `—` = já feito. `T` = transversal.

| # | Fase | Bloco | Item | Estado | Situação real / critério de conclusão |
|---:|:---:|---|---|:---:|---|
| 1 | — | Base | Identificação PCI | ✅ | `1002:13FE` Cyan Skillfish 2 confirmado |
| 2 | — | Base | Topologia Zen 2 + GC 10.1.3 | ✅ | 24 CUs observadas |
| 3 | — | Base | BARs e recursos PCI | ✅ | Lidos e mapeados no bring-up |
| 6 | — | Base | Driver de laboratório `bc250dbg` | ✅ | Diagnóstico e telemetria isolados |
| 7 | — | Base | Recovery manual | ✅ | `C:\BC250_RECOVERY.cmd` funcional |
| 8 | — | Base | Watchdog automático fire-and-collect | ✅ | 3.80 fechou o ciclo sozinho: bind → retrato → rollback → reboot |
| 9 | — | Base | Binding sem watchdog | ✅ | Aceite explícito + recovery manual |
| 10 | — | Base | Hash instalado vs pacote | ✅ | SYS comparado a cada bind |
| 11 | — | Base | Assinatura de laboratório | ✅ | Pacotes assinados e auditados |
| 12 | — | Base | Gate de boot limpo / dirty-engine | ✅ | Fail-closed em toda corrida física |
| 13 | — | Firmware | PSP / GPCOM | ✅ | Carga autenticada comprovada |
| 14 | — | Firmware | RLC | ✅ | Saiu da ROM; firmware aceito |
| 15 | — | Firmware | PFP | ✅ | Carga direta e execução provadas |
| 16 | — | Firmware | ME | ✅ | Tríade CP comprovada |
| 17 | — | Firmware | CE | ✅ | Tríade CP comprovada |
| 18 | — | Firmware | MEC | ✅ | Compute persistente fisicamente funcional |
| 19 | — | Firmware | SDMA | ✅ | Motor e trabalho reais comprovados |
| 20 | — | Silício | Golden registers GFX | ✅ | Sequência upstream aplicada com máscaras SOC15 |
| 21 | — | Silício | Clocks GFX | ✅ | Suficientes para P1/P2 |
| 28 | — | Memória | VRAM GDDR6 acessível | ✅ | Buffers físicos usados nas provas |
| 29 | — | Memória | PTE 0x10 / tradução GCVM | ✅ | Tradução provada no silício |
| 43 | — | Filas | MQD / HQD | ✅ | MEC2 / Pipe0 / Queue0 ativada |
| 44 | — | Filas | Ring persistente | ✅ | Engine online, estado transacional |
| 45 | — | Filas | Indirect Buffer | ✅ | Readback byte a byte antes do kick |
| 46 | — | Filas | Shader real na GPU | ✅ | Resultado saiu do poison |
| 47 | — | Filas | Prova P1 | ✅ | 17/17, duas fences GPU, `RPTR==WPTR` |
| 48 | — | Filas | Prova P2 | ✅ | 16/16 aceitas e aposentadas, n=2 |
| 49 | — | Filas | Fence física direta | ✅ | Escrita pela GPU em VRAM, poison verificado |
| 50 | — | Filas | Retirement direto | ✅ | Telemetria confirma aposentadoria |
| 57 | — | KMDOD | Registro `DxgkInitializeDisplayOnlyDriver` | ✅ | Código 0 — baseline e recovery |
| 58 | — | KMDOD | Framebuffer do GOP | ✅ | Endereço, pitch e modo herdados |
| 59 | — | KMDOD | VidPN KMDOD | ✅ | `IsSupported` / `Commit` aprovados |
| 60 | — | KMDOD | `PresentDisplayOnly` | ✅ | Entrada, status e cópia funcionais |
| 61 | — | KMDOD | VSync KMDOD | ✅ | Contadores e notificação observados (timer, não DCN) |
| 62 | — | KMDOD | Imagem após binding | ✅ | Confirmada várias vezes |
| 93 | — | WDDM | A/B contrato 2.0 vs 2.7 | ✅ | Ambos consumidos; **mesma** falha no estágio 3 |
| 94 | — | WDDM | Trace de `QueryInterface` | ✅ | 7 dos 8 GUIDs identificados; todos opcionais — **não** são a causa |
| 100 | — | WDDM | Perfil `RenderOnlyLab` | ✅ | Implementado e executado (3.79/3.80); perfil aposentado |
| 123 | — | Ferramentas | Decoder de GUIDs | ✅ | Resolvido pela fonte WDK; resta só `6BA41A33` |
| 167 | — | Testes | Suíte unitária core | ✅ | 37 testes de kernel + 21 invariantes UMD, 0 falhas |
| 168 | — | Testes | Teste da init struct efetiva | ✅ | WDDM 2.0/2.7 e ponteiros auditados |
| 169 | — | Testes | Testes de VidPN | ✅ | Cobertura offline |
| 171 | — | Testes | Validadores P1/P2 | ✅ | Read-only, evidência JSON |
| 172 | — | Testes | Safety audit | ✅ | Gate no build |
| 173 | — | Testes | Package audit | ✅ | INF / CAT / SYS / manifesto |
| 176 | **A** | Debug | Crash dump / TDR dump | ⬜ | `C:\Windows\MEMORY.DMP` (1,2 GB, 02/09 18:00) existe e **nunca foi analisado**. `cdb` instalado; exige elevação |
| 175 | **A** | Debug | Runbook KDNET | 🟡 | Rascunho. Exige **segunda máquina** — `-kl` local não dá breakpoint/stack/`g` |
| 126 | **A** | Debug | Símbolos internos do `dxgkrnl` | 🟡 | Validar com `x`; fallback ETW/IFR |
| 174 | **A** | Debug | ETW / WPP / IFR | ⬜ | Trace de alta resolução sem persistir em ISR/DPC |
| 97 | **B** | WDDM | **Código 43** | ⛔ | **GARGALO ÚNICO.** Recusa imediata no retorno do `StartDevice`, 5 s após o bind. `DRIVERCAPS` nunca é perguntado |
| 133 | **B** | UMD | UMD declarado no INF | 🧪 | Seção `.HW` + `InstalledDisplayDrivers` / `UserModeDriverName` / `Wow` implementadas no 3.81. **Não ensaiado** |
| 134 | **B** | UMD | D3D UMD `bc250umd.dll` | 🧪 | `OpenAdapter10/10_2/11/12` reais, `pfnCreateDevice` = `E_NOTIMPL` fail-closed. Assinado. **Não ensaiado** |
| 90 | **B** | WDDM | `DxgkInitialize` | 🧪 | Retorna sucesso; adaptador precisa **permanecer** Código 0 |
| 91 | **B** | WDDM | `AddDevice` | 🧪 | Sucesso; validar lifecycle de recursos |
| 92 | **B** | WDDM | `StartDevice` (1 source / 1 child) | 🧪 | Sucesso; não sofrer `Stop`/`Remove` imediato |
| 103 | **B** | WDDM | `QueryAdapterInfo` caps | 🟡 | `V5QaiTypeSeenLo=0x20000000` — só o tipo 29 em 12 corridas. Inalcançável até B passar |
| 102 | **B** | WDDM | Display+render em Código 0 | ⛔ | Nunca provado. **Marco que destrava todo o resto** |
| 98 | **B** | WDDM | `StopDevice` | 🧪 | Corrigido; revalidar com trabalho WDDM pendente |
| 99 | **B** | WDDM | `RemoveDevice` | 🧪 | Status 0; revalidar ownership de display |
| 95 | **B** | WDDM | `QueryChildRelations` | 🧪 | Uma saída + terminador, `ChildUid=1`. Validar sem tela preta |
| 96 | **B** | WDDM | `QueryDeviceDescriptor` | 🧪 | EDID path presente; integrar EDID real via AUX |
| 119 | **B** | Dívida | `DxgkDdiPresent` retorna `NOT_SUPPORTED` | 🧹 | Implementar de verdade na Fase E, não antes |
| 120 | **B** | Dívida | `SetDisplayPrivateDriverFormat` sucesso vazio | 🧹 | Remover da tabela até existir contrato real |
| 122 | **B** | Dívida | `QueryVidPnHWCapability` zera caps | 🟡 | Auditar se zero é semanticamente correto |
| 127 | **B** | Dívida | Matriz DDI | 🟡 | Prova ponteiros, não semântica. Renomear como structural-only |
| 130 | **B** | Dívida | INF fonte vs pacote | 🟡 | Manifesto ainda não é fonte única |
| 4 | **C** | DCN | IP Discovery do bloco de display | 🟡 | Só GC/OSSSYS. Estender ao DMU. Pista offline: `DMU = 0x12, 0xC0, 0x34C0, 0x9000` (4 segmentos SOC15) |
| 5 | **C** | DCN | Catálogo de registradores DCN | 🟡 | Referência do repo lista **DCN e MMHUB ambos em 0x5000** — colisão. Catalogar por `(reg, BASE_IDX)`, não base plana |
| 64 | **C** | DCN | Existência do DCN 2.0.1 | 🟡 | Referências selecionam DCN 2.0.1; confirmar por IP discovery da própria placa |
| 65 | **C** | DCN | Base real do DCN | 🟡 | Derivar da BC-250, com checksum e bounds |
| 66 | **C** | DCN | Inventário read-only | ⬜ | **Risco zero** — `IOCTL_BC250DBG_REG_READ` já existe; BasicDisplay segue dono da tela |
| 67 | **C** | DCN | OTG / frame counter vivo | 🟡 | `OTG_STATUS_FRAME_COUNT` avançando entre dois snapshots |
| 68 | **C** | DCN | HUBP / surface-in-use | 🟡 | Decodificar `DCSURF_PRIMARY_SURFACE_ADDRESS`, pitch, formato, viewport, tiling |
| 79 | **C** | DCN | DMCU / DMUB | ⬜ | Presente? rodando? necessário para inherited-mode? **Não inventar blob** |
| 80 | **C** | DCN | ATOM / board tables | ⬜ | Connector, encoder e clock source extraídos da placa |
| 81 | **D** | Display | Takeover do GOP (inherited-mode) | ⬜ | Assumir ownership **preservando** modo, link e clock. Snapshot antes de tocar |
| 82 | **D** | Display | Master update lock | ⬜ | Reapontar `PRIMARY_SURFACE_ADDRESS` atomicamente |
| 84 | **D** | Display | VBlank real (IH → ISR/DPC → dxgkrnl) | ⬜ | Substituir o timer do KMDOD por interrupção DCN física |
| 42 | **D** | Memória | Scanout / primary allocation | ⬜ | Allocation WDDM real, não framebuffer herdado |
| 83 | **E** | Display | Flip A/B físico | ⬜ | Duas surfaces alternando; `SURFACE_INUSE` + frame counter provam qual está no ar |
| 63 | **E** | Display | Display acelerado | ⬜ | Desktop desenhado pelo nosso driver; cópia por CPU não conta |
| 74 | **F** | DisplayPort | HPD | ⬜ | Hoje o driver anuncia always-connected. Interrupção e estado reais |
| 75 | **F** | DisplayPort | AUX | ⬜ | Transações com timeout e retry |
| 76 | **F** | DisplayPort | DPCD | ⬜ | Capabilities reais do monitor e do link |
| 77 | **F** | DisplayPort | EDID real | 🟡 | Hoje é buffer do KMDOD. Ler por AUX e validar checksum |
| 78 | **F** | DisplayPort | Link training | ⬜ | CR/EQ, lane count, rate e status reais |
| 71 | **F** | DCN | DCCG / clocks de display | ⬜ | Não integrado |
| 69 | **F** | DCN | DCHUB / DCHUBBUB | ⬜ | Watermarks e arbitragem de scanout |
| 70 | **F** | DCN | DPP / MPC / OPP | ⬜ | Pipeline mínimo de uma plane |
| 72 | **F** | DCN | DIG / link encoder | ⬜ | Estado, enable e mapeamento |
| 73 | **F** | DCN | PHY | ⬜ | Power, lane mapping e sinal elétrico |
| 85 | **F** | Display | Mode-set transacional | ⬜ | Um modo conhecido, com rollback do link |
| 86 | **F** | Display | Modos do EDID | ⬜ | Enumerar, validar e trocar |
| 87 | **F** | Display | Hotplug | ⬜ | Disconnect/reconnect sem crash nem tela presa |
| 104 | **G** | Render | Nodes / engines WDDM | ⬜ | Descrever o compute que já existe fisicamente |
| 111 | **G** | Render | Harness D3DKMT | ⬜ | Abrir adaptador e submeter pelo kernel gráfico |
| 105 | **G** | Render | `CreateDevice` | ⛔ | Chamada real do `dxgkrnl` com handle válido |
| 106 | **G** | Render | `CreateContext` / Virtual | ⛔ | Contexto real associado a VMID/GPUVA |
| 107 | **G** | Render | `HwQueue` | ⬜ | Create / Destroy / Submit por fila real |
| 108 | **G** | Render | `CreateAllocation` | ⛔ | Allocation list e segmentos reais |
| 37 | **G** | Memória | Allocations WDDM | ⛔ | Create/Open/Close/Destroy com handles e ownership reais |
| 38 | **G** | Memória | Segments WDDM | ⛔ | VRAM / aperture / system coerentes, consumidos pelo `dxgkrnl` |
| 39 | **G** | Memória | Residency | ⬜ | `MakeResident` / `Evict` e accounting correto |
| 40 | **G** | Memória | Paging buffer | ⬜ | Paging via SDMA com fence e rollback |
| 30 | **G** | Memória | VMID0 page table | 🟡 | Receita fail-closed; não promover antes da GPUVA completa |
| 31 | **G** | Memória | GPUVA 48-bit core | 🧪 | Contratos de space/journal/snapshot; integrar ao KMD ativo |
| 32 | **G** | Memória | Journal GPUVA | 🧪 | Provar generation/epoch/rollback em corrida WDDM |
| 33 | **G** | Memória | Snapshot GPUVA | 🧪 | Publicar snapshot vivo antes de submeter |
| 34 | **G** | Memória | Gate GPUVA | 🧪 | `GpuvaLabPromotion=0`; promover só com todos os bloqueios zerados |
| 35 | **G** | Memória | Map / unmap GPUVA | ⛔ | Mapear, invalidar TLB, readback e desfazer transacionalmente |
| 36 | **G** | Memória | Root page table por processo | 🟡 | Criar, associar, invalidar e destruir pelo `dxgkrnl` |
| 109 | **G** | Render | Patch / Render / RenderKm | 🟡 | Semântica real, não `NOT_SUPPORTED` |
| 110 | **G** | Render | `SubmitCommandVirtual` | 🟡 | Pipeline interno existe; falta **entrada originada pelo `dxgkrnl`** |
| 112 | **G** | Render | Fence WDDM | ⛔ | Vincular a fence física da GPU ao scheduler |
| 113 | **G** | Render | Interrupt / DPC WDDM | ⛔ | Completion real e notificação correta |
| 23 | **G** | Silício | IH de compute | 🟡 | Provar interrupção real de completion WDDM |
| 114 | **G** | Render | Retirement WDDM | ⛔ | `dxgkrnl` reconhece completion **física**. Proibido completar por CPU |
| 51 | **G** | Filas | Multi-context | ⬜ | Isolamento entre processos e contextos |
| 52 | **G** | Filas | Preemption | ⬜ | Preempt command/context com fence coerente |
| 115 | **G** | Render | Preemption WDDM | ⬜ | Preempt + fence/timeout |
| 53 | **G** | Filas | Cancel command | 🧪 | Callback fail-closed; implementar quando houver submissões |
| 54 | **G** | Filas | Prioridades | ⬜ | Nodes/engines/prioridades WDDM corretos |
| 55 | **G** | Filas | Wrap do ring | ⬜ | Stress sem sobrescrever trabalho vivo |
| 56 | **G** | Filas | Concorrência | ⬜ | Multithread/process, locks e teardown |
| 24 | **G** | Silício | Reset de engine | 🟡 | Soft-reset em lab existe. Falta reset WDDM por node preservando o display |
| 25 | **G** | Silício | TDR | ⬜ | **Bloqueante antes da Fase H.** Sem isto, todo bug de shader vira reboot |
| 116 | **G** | Render | `ResetFromTimeout` / `RestartFromTimeout` | ⬜ | Recuperar engine sem perder o display |
| 117 | **G** | Render | `CollectDbgInfo` | 🟡 | Dump útil para TDR |
| 118 | **G** | Render | Power callbacks | 🟡 | F-states/runtime power reais, ou não anunciar |
| 132 | **H** | UMD | ABI KMD/UMD versionada | 🟡 | Versionar handles, memória, queues e erros |
| 135 | **H** | Vulkan | ICD skeleton | 🧪 | Arquivos e JSON; nada validado |
| 136 | **H** | Vulkan | Enumeração pelo loader | ⬜ | `vulkaninfo` lista a BC-250 sem fallback |
| 137 | **H** | Vulkan | Physical device properties | ⬜ | Derivar do hardware, limitar features corretamente |
| 138 | **H** | Vulkan | Memória | ⬜ | Heaps/types/mapping/residency via WDDM |
| 139 | **H** | Vulkan | Queues | ⬜ | Queue family ligada a context/HwQueue WDDM |
| 140 | **H** | Vulkan | Command buffers | ⬜ | PM4/IB real gerado pelo ICD |
| 141 | **H** | Vulkan | Submit | ⬜ | Submissão via `dxgkrnl` com fence física |
| 142 | **H** | Vulkan | Sync | ⬜ | Fence / semaphore / event reais |
| 143 | **H** | Vulkan | Compute | ⬜ | Compute shader via ICD, conferido byte a byte |
| 144 | **H** | Vulkan | Graphics | ⬜ | Rasterização, images, renderpass |
| 145 | **H** | Vulkan | WSI | ⬜ | Swapchain e present pelo DisplayPort da própria placa |
| 146 | **H** | Vulkan | RADV winsys para Windows | ⬜ | Adaptar winsys/WSI/ABI. Não roda inalterado |
| 147 | **H** | Vulkan | Shader compiler | ⬜ | LLVM/ACO ou equivalente para shaders arbitrários |
| 148 | **H** | APIs | Zink (OpenGL) | ⬜ | Depois de Vulkan graphics/WSI estável |
| 149 | **H** | APIs | DXVK (D3D9/10/11) | ⬜ | Depois de Vulkan 1.3 funcional |
| 150 | **H** | APIs | VKD3D-Proton (D3D12) | ⬜ | Depois de Vulkan features/sync adequadas |
| 151 | **H** | APIs | OpenGL nativo | ⬜ | Não planejado; Zink é o caminho |
| 152 | **H** | APIs | D3D12 nativo | ⬜ | Opcional se VKD3D cobrir a meta |
| 158 | **I** | Produto | Triângulo Vulkan | ⬜ | Primeira prova graphics + WSI |
| 159 | **I** | Produto | Jogo DXVK simples | ⬜ | Frame real, sem WARP nem llvmpipe |
| 160 | **I** | Produto | Jogo VKD3D | ⬜ | D3D12 traduzido e estável |
| 161 | **I** | Produto | Compilação de shaders | ⬜ | Cache, pipelines, ausência de corrupção |
| 164 | **I** | Produto | Jogos + desktop simultâneos | ⬜ | DWM, jogo e compute compartilhando a GPU |
| 166 | **I** | Produto | Ausência de fallback de software | ⬜ | Verificar por GPU counters e fences |
| 153 | **I** | LLM | Kernels LLM | 🧪 | Esqueletos no repo; integrar via compute real |
| 154 | **I** | LLM | llama.cpp Vulkan | ⬜ | Offload de layers e benchmark contra CPU |
| 155 | **I** | LLM | DirectML | ⬜ | Exige D3D12 UMD funcional; não é o primeiro caminho |
| 156 | **I** | LLM | Alocação de modelos grandes | ⬜ | 16 GB físicos existem; heap WDDM não |
| 157 | **I** | LLM | Benchmark LLM | ⬜ | Tokens/s, consumo, temperatura, verificação de GPU |
| 162 | **I** | Estabilidade | Long-run compute | ⬜ | Horas sem hang, regressão ou fence perdida |
| 163 | **I** | Estabilidade | Long-run display | ⬜ | Full WDDM com flips, hotplug e mode changes |
| 165 | **I** | Estabilidade | Telemetria de desempenho | ⬜ | Busy, clocks, memória, temperatura, faults |
| 22 | **T** | Silício | SMU | 🟡 | Power management WDDM, thermal e clocks de DCN |
| 26 | **T** | Silício | Suspend / resume | ⬜ | S3/S4/reboot sem perder fila, display ou mappings |
| 27 | **T** | Silício | Thermal / power limits | ⬜ | Limites seguros sob carga longa |
| 41 | **T** | Memória | Shared / cross-adapter | ⬜ | Só necessário no modo híbrido — **fora do destino** |
| 88 | **T** | DisplayPort | Áudio DisplayPort | ⬜ | Só depois do vídeo estável |
| 89 | **T** | DisplayPort | HDR / VRR / MPO / HDCP | ⬜ | Tardio. Não anunciar antes de implementar |
| 121 | **T** | Qualidade | Auditoria de falsos sucessos | 🟡 | Falta o teste AST/semântico geral |
| 124 | **T** | Qualidade | Runbook WinDbg local | 🧹 | `-kl` não serve. Exigir KDNET com duas máquinas |
| 125 | **T** | Qualidade | Breakpoint `+0x10` | 🧹 | Offset instável e não é ponto de retorno |
| 128 | **T** | Qualidade | Relatório 3.74 | 🧹 | Rebaixar "causa definitiva" para hipótese |
| 129 | **T** | Qualidade | Worktree sujo | 🟡 | Criar baseline/tag só com autorização |
| 131 | **T** | Qualidade | UMD/ICD esqueletos | 🟡 | Manter desabilitados até o KMD WDDM ser real |
| 170 | **T** | Testes | Testes GPUVA | 🧪 | Integração WDDM/hardware |
| 177 | **T** | Processo | Orçamento de corridas | ⬜ | Máximo por gate, critério de abandono, recovery |
| 178 | **T** | Processo | Matriz de hardware | ⬜ | Uma BC-250 e um monitor hoje |
| 179 | **T** | Entrega | HLK | ⬜ | Depois de funcionalidade e estabilidade |
| 180 | **T** | Entrega | Driver de produção | ⬜ | Remover gates perigosos, proteger firmware, assinar |
| 181 | **T** | Entrega | Instalador / desinstalador | 🟡 | Hoje só scripts de lab e recovery |
| 182 | **T** | Entrega | Documentação final | 🟡 | Consolidar quando os contratos estabilizarem |
| 101 | ❌ | Encerrado | Render-only em Código 0 | ❌ | **REPROVADO** (doc 162). A recusa antecede qualquer cap de render; custou tela preta nas duas corridas |
| — | ❌ | Encerrado | `MiscCaps.ComputeOnly` | ❌ | Inalcançável: trafega só por `DRIVERCAPS`, que nunca é perguntado |
| — | ❌ | Encerrado | Cross-adapter / MCDM como destino | ❌ | APU de die único: não há segundo adaptador de display real. Teto de desempenho inaceitável |

---

## Contagem

| Estado | Itens |
|---|---:|
| ✅ Validado no silício | 45 |
| 🧪 Provado só offline | 17 |
| 🟡 Parcial | 28 |
| ⛔ Bloqueado | 11 |
| ⬜ Não iniciado | 75 |
| 🧹 Dívida a remover | 5 |
| ❌ Reprovado / encerrado | 3 |
| **Total de itens** | **184** |

## Veredito

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD / GOP:** VALIDADO EM HARDWARE.
- **Render-only WDDM:** REPROVADO e encerrado.
- **Render WDDM, display acelerado, Vulkan, jogos e LLM via driver:** **NÃO VALIDADO EM HARDWARE.**

O gargalo é **um só**: item **97 — Código 43**. Nada da Fase C para frente pode ser
concluído antes dele, e o candidato que testa a única hipótese viva (**3.81**, itens 133
e 134) está assinado na bancada, aguardando ensaio.
