# Checklist completo — BC-250 WDDM display+render

**Data-base:** 2026-09-02 (candidato 3.80; ver [`docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md`](docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md) e [`docs/161_PLANO_MESTRE_POS_GATE_1_5.md`](docs/161_PLANO_MESTRE_POS_GATE_1_5.md))  
**Meta final:** BC-250 funcionando no Windows como GPU normal: DisplayPort, desktop acelerado, WDDM display+render, Vulkan, jogos e LLMs com processamento real na GPU.  
**Legenda:** ✅ validado em hardware · 🧪 implementado/provado offline · 🟡 parcial · ⛔ bloqueado · ⬜ não iniciado · 🧹 corrigir/remover.

> Compilar, assinar ou retornar `STATUS_SUCCESS` nao equivale a funcionar. Onde nao existir prova fisica consta **NAO VALIDADO EM HARDWARE**.

## 1. Base, hardware e recuperacao

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 1 | Identificacao PCI | ✅ | AMD `1002:13FE`, Cyan Skillfish 2 | Manter gate exato de hardware/subsystem |
| 2 | Topologia geral | ✅ | Zen 2 + GPU GC 10.1.3; 24 CUs observadas | Consolidar topologia final no caps WDDM |
| 3 | BARs e recursos PCI | ✅ | Leitura e mapeamento usados no bring-up | Derivar todos os segmentos WDDM dos recursos reais |
| 4 | IP Discovery | 🟡 | GC/OSSSYS e blocos usados no compute | Estender parser para DCN/DMU/DIO/IH de display |
| 5 | Catalogo de registradores | 🟡 | Registradores compute catalogados; inventario DCN parcial | Promover somente offsets DCN com proveniencia e readback |
| 6 | Driver de laboratorio | ✅ | Diagnostico e telemetria isolados | Preservar separacao de producao |
| 7 | Recovery manual | ✅ | `C:\BC250_RECOVERY.cmd` funcional | Revalidar sempre que INF/servico mudar |
| 8 | Watchdog automatico | ✅ | 3.80: journal append-only, veredito PnP por 5 amostras, escalada de shutdown, final.json e limpeza. Ciclo fire-and-collect fechou sozinho em 02/09 | Manter obrigatorio em toda corrida do perfil fisico |
| 9 | Binding sem watchdog | ✅ | Aceite explicito e recovery manual | Continuar exigindo flags de risco |
| 10 | Hash instalado/pacote | ✅ | Binding compara SYS instalado e pacote | Incluir PDB GUID/age e hashes INF/CAT em cada candidato |
| 11 | Assinatura de laboratorio | ✅ | Pacotes assinados e auditados | Certificacao/assinatura de producao somente no fim |
| 12 | Boot limpo/dirty-engine | ✅ | Marcadores e bloqueio apos falhas | Manter fail-closed em toda corrida fisica |

## 2. Firmware, clocks e blocos de silicio

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 13 | PSP/GPCOM | ✅ | Caminho de carga autenticada comprovado | Integrar reset/reload de producao |
| 14 | RLC firmware | ✅ | Saiu da ROM; firmware aceito | Reset e recovery WDDM |
| 15 | PFP firmware | ✅ | Carga direta e execucao provadas | Integrar preemption/reset de producao |
| 16 | ME firmware | ✅ | Parte da triade CP comprovada | Integrar scheduler grafico completo |
| 17 | CE firmware | ✅ | Parte da triade CP comprovada | Confirmar requisitos para workloads graficos reais |
| 18 | MEC firmware | ✅ | Compute persistente fisicamente funcional | Integrar com contexts/queues WDDM |
| 19 | SDMA firmware | ✅ | Motor e trabalho reais comprovados | Usar em paging WDDM somente apos gate isolado |
| 20 | Golden registers GFX | ✅ | Sequencia derivada do upstream aplicada | Preservar mascaras SOC15 e rollback |
| 21 | Clocks GFX | ✅ | Suficientes para P1/P2 | Politica de power/DPM e estabilidade longa |
| 22 | SMU | 🟡 | Interrogacao e partes do bring-up | Power management WDDM, thermal e clocks DCN |
| 23 | IH compute | 🟡 | Contratos/enderecos e infraestrutura | Provar interrupcao real de completion WDDM |
| 24 | Reset de engine | 🟡 | Soft-reset controlado em laboratorio | Reset WDDM por node, recovery e preservacao do display |
| 25 | TDR | ⬜ | Nenhuma prova completa | Timeout, `ResetFromTimeout`, diagnostico e recuperacao |
| 26 | Suspend/resume | ⬜ | Nao validado | S3/S4/reboot sem perder fila, display ou mappings |
| 27 | Thermal/power limits | ⬜ | Nao integrado | Telemetria e limites seguros sob carga longa |

## 3. Memoria, GCVM e GPUVA

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 28 | VRAM/GDDR6 acessivel | ✅ | Buffers fisicos usados nas provas | Expor capacidade real sem anunciar memoria inexistente |
| 29 | PTE 0x10 | ✅ | Traducao usada e provada | Generalizar para mappings dinamicos |
| 30 | VMID0 page table | 🟡 | Receita fail-closed e tunable | Nao promover antes de GPUVA completa |
| 31 | GPUVA 48-bit core | 🧪 | Contratos de space/journal/snapshot | Integrar ao KMD ativo e provar no hardware |
| 32 | Journal GPUVA | 🧪 | Estruturas e testes offline | Provar generation/epoch/rollback em corrida WDDM |
| 33 | Snapshot GPUVA | 🧪 | Store e gates implementados | Publicar snapshot vivo antes de submissao |
| 34 | Gate GPUVA | 🧪 | `GpuvaLabPromotion=0`, fail-closed | Promover somente apos todos os bloqueios zerarem |
| 35 | Map/unmap GPUVA | ⛔ | Pipeline parcial | Mapear, invalidar TLB, readback e desfazer transacionalmente |
| 36 | Root page table por processo | 🟡 | ABI/objetos parciais | Criar, associar, invalidar e destruir pelo dxgkrnl |
| 37 | Allocations WDDM | ⛔ | Callbacks/contratos parciais | Create/Open/Close/Destroy com handles e ownership reais |
| 38 | Segments WDDM | ⛔ | Ainda nao consumidos pelo dxgkrnl | VRAM/aperture/system segments coerentes |
| 39 | Residency | ⬜ | Nao validada | MakeResident/Evict e accounting correto |
| 40 | Paging buffer | ⬜ | SDMA existe fora do WDDM | Paging commands reais, fence e rollback |
| 41 | Shared/cross-adapter resources | ⬜ | Nao implementado | Necessario somente para modo hibrido/render-only com outra GPU |
| 42 | Scanout allocation | ⬜ | GOP framebuffer herdado no KMDOD | Primary/scanout allocation WDDM real |

## 4. Filas, PM4 e compute fisico

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 43 | MQD/HQD | ✅ | MEC2/Pipe0/Queue0 ativada | Multiplexar contexts/queues WDDM |
| 44 | Ring persistente | ✅ | Engine online e estado transacional | Integrar lifecycle ao scheduler WDDM |
| 45 | Indirect Buffer | ✅ | Readback byte a byte antes do kick | IB proveniente de UMD/dxgkrnl |
| 46 | Shader real | ✅ | Resultado saiu do poison na GPU | Compilador/ISA e shaders arbitrarios |
| 47 | P1 | ✅ | Uma submissao; duas fences GPU; `RPTR==WPTR` | Preservar como teste de regressao |
| 48 | P2 | ✅ | 16/16 aceitas e aposentadas | Preservar como teste de regressao |
| 49 | Fence fisica direta | ✅ | Escrita pela GPU em VRAM | Associar a fence WDDM real |
| 50 | Retirement direto | ✅ | Telemetria confirma aposentadoria | Retirement reconhecido pelo dxgkrnl |
| 51 | Multi-context | ⬜ | Nao provado | Isolamento entre processos/contextos |
| 52 | Preemption | ⬜ | Nao provada | Preempt command/context com fence coerente |
| 53 | Cancel command | 🧪 | Callback fail-closed | Implementar cancelamento real quando houver submissions |
| 54 | Prioridades | ⬜ | Fila fixa atual | Nodes/engines/prioridades WDDM corretos |
| 55 | Wrap do ring | ⬜ | Nao validado no WDDM | Stress de wrap sem sobrescrever trabalho vivo |
| 56 | Concorrencia | ⬜ | Nao validada | Multithread/process, locks e teardown |

## 5. KMDOD/display herdado

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 57 | Registro KMDOD | ✅ | `DxgkInitializeDisplayOnlyDriver` Codigo 0 | Manter como baseline/recovery, nao como destino final |
| 58 | GOP framebuffer | ✅ | Endereco, pitch e modo herdados | Snapshot DCN completo que sustenta esse framebuffer |
| 59 | VidPN KMDOD | ✅ | IsSupported/Commit aprovados | Separar contratos KMDOD e full WDDM |
| 60 | `PresentDisplayOnly` | ✅ | Entrada/status e copia funcional | Nao reutilizar como se fosse `DxgkDdiPresent` full |
| 61 | VSync KMDOD | ✅ | Contadores e notificacao observados | Substituir timer/simulacao por interrupcao DCN real no full WDDM |
| 62 | Imagem apos binding | ✅ | Confirmada varias vezes | Revalidar em cada baseline candidata |
| 63 | Display acelerado | ⬜ | KMDOD e copia CPU nao sao aceleracao 3D | Scanout/flip/DCN + render WDDM completos |

## 6. DCN 2.0.1 e DisplayPort

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 64 | Existencia do DCN | ✅ | `13FE` Cyan Skillfish; referencias selecionam DCN 2.0.1 | Confirmar IP discovery da placa em runtime |
| 65 | Base DCN | 🟡 | Referencias e offsets historicos | Derivar da propria BC-250 e colocar no catalogo |
| 66 | Inventario read-only DCN | ⬜ | Registradores OTG/HUBP conhecidos em referencias | Implementar probe sem escrita e dois snapshots |
| 67 | OTG/frame counter | 🟡 | Evidencia historica de contador vivo | Reprovar no driver atual com proveniencia/readback |
| 68 | HUBP/surface-in-use | 🟡 | Enderecos conhecidos | Capturar surface, pitch, tiling, viewport e in-use |
| 69 | DCHUB/DCHUBBUB | ⬜ | Nao integrado | Watermarks/arbitragem segura para scanout |
| 70 | DPP/MPC/OPP | ⬜ | Nao integrado | Pipeline minimo de uma plane |
| 71 | DCCG/clocks display | ⬜ | Nao integrado | Clocks/readback sem derrubar GOP |
| 72 | DIG/link encoder | ⬜ | DisplayPort fisico conhecido | Estado, enable e mapeamento de encoder |
| 73 | PHY | ⬜ | Nao controlado | Power, lane mapping e sinal |
| 74 | HPD | ⬜ | Driver anuncia always-connected | Interrupcao e estado fisico reais |
| 75 | AUX | ⬜ | Nao implementado | Transacoes AUX com timeout/retry |
| 76 | DPCD | ⬜ | Nao lido pelo driver | Capabilities reais do monitor/link |
| 77 | EDID | 🟡 | Buffer/descriptor KMDOD | Ler pelo AUX e validar checksum/modos |
| 78 | Link training | ⬜ | Herdado do firmware apenas | CR/EQ, lane/rate e status reais |
| 79 | DMCU/DMUB | ⬜ | Presenca/requisito nao comprovados | Detectar, identificar firmware e handshake; nao inventar blob |
| 80 | ATOM/board tables | ⬜ | Referencias de plataforma | Extrair connector/encoder/clock info da propria placa |
| 81 | Takeover do GOP | ⬜ | Ownership KMDOD funcional | Snapshot e assumir modo sem reprogramar link |
| 82 | Master update lock | ⬜ | Nao integrado | Atualizacao atomica do pipeline |
| 83 | Flip A/B fisico | ⬜ | Nao implementado | Alternar duas surfaces e provar address-in-use |
| 84 | VBlank real | ⬜ | KMDOD tem prova, DCN full nao | IH -> ISR/DPC -> dxgkrnl |
| 85 | Mode-set | ⬜ | Apenas modo GOP | Programar um modo conhecido com rollback |
| 86 | Modos EDID | ⬜ | Nao integrados ao full WDDM | Enumerar/validar e trocar modos |
| 87 | Hotplug | ⬜ | Always-connected atual | Disconnect/reconnect sem crash/tela presa |
| 88 | Audio DisplayPort | ⬜ | Nao iniciado | Somente depois do video estavel |
| 89 | HDR/VRR/MPO/HDCP | ⬜ | Nao iniciado | Recursos tardios; nao anunciar antes de implementar |

## 7. WDDM display+render e RenderNode

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 90 | `DxgkInitialize` | 🧪 | Retorna sucesso em RenderNode | Adaptador precisa permanecer Codigo 0 |
| 91 | AddDevice | 🧪 | Retorna sucesso | Manter recursos/lifecycle corretos |
| 92 | StartDevice | 🧪 | Retorna sucesso, 1 source/1 child | Nao sofrer Stop/Remove imediato |
| 93 | WDDM 2.0/2.7 A/B | ✅ | Ambos consumidos; mesma falha stage 3 | Manter 2.0 baseline |
| 94 | QueryInterface trace | ✅ | 3.80: os 8 GUIDs identificados no WDK (I2C, OPM x4, Miracast, WDDM3_ON_VB); **todos opcionais**, recusa nao e a causa | Resta `6BA41A33`, ausente em 10.0.22621 e 10.0.26100 |
| 95 | QueryChildRelations | 🧪 | Uma saida + terminador; ChildUid agora 1 | Validar no full display sem tela preta |
| 96 | QueryDeviceDescriptor | 🧪 | ChildUid 1 e EDID path | Integrar EDID real/AUX |
| 97 | Codigo 43 | ⛔ | 3.80: recusa **imediata** no retorno do StartDevice (5 s apos o bind); `DRIVERCAPS` nunca e perguntado | Acao anterior **invalidada**: RenderOnlyLab nao fecha isto. Ver Fase 1 do plano mestre |
| 98 | StopDevice | 🧪 | Corrigido; tracker vazio e status 0 | Revalidar com trabalho WDDM pendente |
| 99 | RemoveDevice | 🧪 | Status 0 | Revalidar recursos/display ownership |
| 100 | RenderOnlyLab | ✅ | Implementado e executado no silicio (3.79 e 3.80) | Concluido; perfil aposentado como diagnostico |
| 101 | Render-only Codigo 0 | ⛔ | **REPROVADO** — estruturalmente inviavel; a recusa antecede qualquer cap de render | Encerrado. **Nao e mais marco do roteiro** |
| 102 | Display+render Codigo 0 | ⛔ | Nunca provado | Meta apos DCN/takeover e contrato coerente |
| 103 | QueryAdapterInfo caps | 🟡 | `V5QaiTypeSeenLo=0x20000000`: **so o tipo 29** e perguntado, em 12 corridas | Criterio inalcancavel ate o adaptador sobreviver ao StartDevice |
| 104 | Nodes/engines | ⬜ | Compute fisico existe | Descrever nodes WDDM e capabilities reais |
| 105 | CreateDevice | ⛔ | Callback/objetos parciais | Chamada real pelo dxgkrnl e handle valido |
| 106 | CreateContext/Virtual | ⛔ | Contratos parciais | Contexto real associado a VMID/GPUVA |
| 107 | HwQueue | ⬜ | Nao validada | Create/Destroy/Submit por fila real |
| 108 | CreateAllocation | ⛔ | Parcial | Allocation list e segmentos reais |
| 109 | Patch/Render/RenderKm | 🟡 | Ponteiros/callbacks existem | Semantica real; nao apenas `NOT_SUPPORTED` |
| 110 | SubmitCommandVirtual | 🟡 | Pipeline interno existe | Entrada originada pelo dxgkrnl |
| 111 | D3DKMT harness | ⬜ | Referencias/testes offline apenas | Abrir adaptador e submeter pelo kernel grafico |
| 112 | Fence WDDM | ⛔ | Tracker existe; fence direta existe | Vincular fence GPU ao scheduler WDDM |
| 113 | Interrupt/DPC WDDM | ⛔ | Infraestrutura parcial | Completion real e notificacao correta |
| 114 | Retirement WDDM | ⛔ | Nao provado | dxgkrnl reconhece completion fisica |
| 115 | Preemption WDDM | ⬜ | Nao provada | Preempt + fence/timeout |
| 116 | ResetFromTimeout | ⬜ | Nao implementado completamente | Recuperar engine sem perder display |
| 117 | CollectDbgInfo | 🟡 | Callback existe | Dump util para TDR/falha |
| 118 | Power callbacks | 🟡 | Alguns fail-closed | F-states/runtime power reais ou nao anunciar recurso |

## 8. Defeitos e dividas tecnicas atuais

| # | Item | Estado | Problema | Acao |
|---:|---|:---:|---|---|
| 119 | `DxgkDdiPresent` | 🧹 | Retorna `STATUS_NOT_SUPPORTED` incondicional | Nao usar no RenderOnlyLab; implementar de verdade no full display |
| 120 | `SetDisplayPrivateDriverFormat` | 🧹 | Ignora argumentos e retorna sucesso | Remover da tabela ate existir contrato real |
| 121 | Auditoria de falsos sucessos | 🟡 | 3.80: invariantes de ComputeOnly, catraca binario/schema no pacote, e audit exigindo cobertura total do enum de telemetria (pegou 4 campos orfaos que vazavam handle) | Falta o teste AST/semantico geral |
| 122 | QueryVidPnHWCapability | 🟡 | Zera caps e retorna sucesso | Auditar se zero e semanticamente correto |
| 123 | Decoder de GUIDs | ✅ | Resolvido pela fonte WDK: 7 dos 8 identificados por nome | Resta `6BA41A33`, inexistente nos headers instalados |
| 124 | Runbook WinDbg local | 🧹 | `-kl` nao permite breakpoints/stack/g | Exigir duas maquinas/KDNET |
| 125 | Breakpoint `+0x10` | 🧹 | Offset instavel e nao e retorno | Usar source breakpoint/return address controlado |
| 126 | Simbolos internos dxgkrnl | 🟡 | Podem nao existir publicamente | Validar com `x`; fallback para ETW/IFR/driver trace |
| 127 | Matriz DDI | 🟡 | Prova ponteiros, nao semantica | Renomear saida como structural-only e adicionar testes reais |
| 128 | Relatorio 3.74 | 🧹 | Chama causa da tela preta de definitiva sem trace | Rebaixar para hipotese forte/defeito estatico |
| 129 | Worktree sujo | 🟡 | Muitas mudancas historicas misturadas | Preservar tudo; criar baseline/tag somente com autorizacao |
| 130 | INF fonte vs pacote lab | 🟡 | Duas defasagens do template corrigidas: SchemaVersion 132→134 e WddmContractVersion 20→27 | Manifesto ainda nao e fonte unica |
| 131 | UMD/ICD existentes no repo | 🟡 | Esqueletos podem aparentar funcionalidade | Auditar e manter desabilitados ate KMD WDDM real |

## 9. UMD, Vulkan, DirectX e OpenGL

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 132 | ABI KMD/UMD | 🟡 | Runtime/headers iniciais | Versionar handles, memoria, queues e erros |
| 133 | UMD declarado no INF | ⛔ | **CAMINHO CRITICO.** `rdvgwddmdx11.inf` (WDDM real desta maquina) declara `InstalledDisplayDrivers`/`UserModeDriverName` numa secao `.HW` que nosso INF nao tem | Hipotese principal do Codigo 43. Ver Fase 1/2 do plano mestre |
| 134 | D3D UMD | ⛔ | **CAMINHO CRITICO.** `grep OpenAdapter` no repo = 0 ocorrencias; existe runtime de compute e esqueleto de ICD, nenhum e UMD de display | Bloqueia #133; provavelmente bloqueia o Codigo 43 |
| 135 | Vulkan ICD skeleton | 🧪 | Arquivos iniciais e JSON | Nao validado; loader/device/commands reais |
| 136 | Vulkan loader enumeration | ⬜ | Nao provada com GPU WDDM | `vulkaninfo` enumera a BC-250 sem fallback |
| 137 | Physical device properties | ⬜ | Nao confiaveis ainda | Derivar do hardware e limitar features corretamente |
| 138 | Vulkan memory | ⬜ | Nao integrado | Heaps/types/mapping/residency via WDDM |
| 139 | Vulkan queues | ⬜ | Nao integrado | Queue family ligada a context/HwQueue WDDM |
| 140 | Command buffers | ⬜ | Nao integrado | PM4/IB real a partir do ICD |
| 141 | Vulkan submit | ⬜ | Nao integrado | Submissao via dxgkrnl e fence fisica |
| 142 | Vulkan sync | ⬜ | Nao integrado | Fence/semaphore/event reais |
| 143 | Vulkan compute | ⬜ | Shader direto existe, Vulkan nao | Compute shader via ICD com poison/data |
| 144 | Vulkan graphics | ⬜ | Nao iniciado | Rasterizacao, images, renderpass/dynamic rendering |
| 145 | Vulkan WSI | ⬜ | Nao iniciado | Swapchain e present no DisplayPort/full WDDM |
| 146 | RADV winsys Windows | ⬜ | RADV Linux e referencia | Adaptar winsys/WSI/ABI para este WDDM |
| 147 | Shader compiler | ⬜ | ISA fixa/testes apenas | LLVM/ACO ou pipeline equivalente para shaders gerais |
| 148 | Zink | ⬜ | Nao iniciado | Depois de Vulkan graphics/WSI estavel |
| 149 | DXVK | ⬜ | Nao iniciado | Depois de Vulkan 1.3 funcional |
| 150 | VKD3D-Proton | ⬜ | Nao iniciado | Depois de Vulkan features/sync adequadas |
| 151 | OpenGL nativo | ⬜ | Nao planejado | Zink e o caminho preferido |
| 152 | D3D12 nativo | ⬜ | Nao existe UMD | Opcional se VKD3D-Proton cobrir a meta |

## 10. LLM, jogos e validacao de produto

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 153 | Kernels LLM iniciais | 🧪 | Arquivos/esqueletos no repo | Integrar via Vulkan/compute real |
| 154 | llama.cpp Vulkan | ⬜ | Nao validado | Offload de layers e benchmark contra CPU |
| 155 | DirectML | ⬜ | Nao iniciado | Exige D3D12 UMD funcional; nao e primeiro caminho |
| 156 | Alocacao de modelos grandes | ⬜ | 16 GB fisicos, heap WDDM ausente | Residency/OOM/fragmentacao corretas |
| 157 | Benchmark LLM | ⬜ | Ferramenta inicial no repo | Tokens/s, consumo, temperatura e verificacao de GPU |
| 158 | Triangulo Vulkan | ⬜ | Nao validado | Primeira prova graphics+WSI |
| 159 | Jogo DXVK simples | ⬜ | Nao iniciado | Frame real, sem WARP/llvmpipe |
| 160 | Jogo VKD3D | ⬜ | Nao iniciado | D3D12 traduzido e estavel |
| 161 | Compilacao de shaders | ⬜ | Nao iniciada | Cache, pipelines e ausencia de corrupcao |
| 162 | Long-run compute | ⬜ | P2 curta | Horas sem hang/regressao/fence perdida |
| 163 | Long-run display | ⬜ | KMDOD historicamente estavel | Full WDDM com flips/hotplug/mode changes |
| 164 | Jogos + desktop simultaneos | ⬜ | Nao iniciado | DWM, jogo e compute compartilhando GPU |
| 165 | Telemetria de desempenho | ⬜ | Telemetria funcional, nao perf completa | Busy, clocks, memoria, temperatura e faults |
| 166 | Sem fallback de software | ⬜ | P1/P2 provam GPU direta | Verificar ICD/DXVK/jogos por GPU counters e fences |

## 11. Qualidade, testes e entrega

| # | Item | Estado | O que ja temos | O que falta / criterio de conclusao |
|---:|---|:---:|---|---|
| 167 | Testes unitarios core | ✅ | Suite extensa | Expandir para DCN, profiles e TDR |
| 168 | Teste da init struct efetiva | ✅ | WDDM 2.0/2.7 e pointers auditados | Adicionar RenderOnlyLab/full display |
| 169 | Testes de VidPN | ✅ | Cobertura offline | Adicionar ownership/flip/DCN models |
| 170 | Testes GPUVA | 🧪 | Space/journal/snapshot offline | Integracao WDDM/hardware |
| 171 | Testes P1/P2 | ✅ | Validadores read-only e evidencia JSON | Rodar em cada novo candidato fisico |
| 172 | Safety audit | ✅ | Gate existente | Incluir DCN write whitelist/snapshots |
| 173 | Package audit | ✅ | INF/CAT/SYS/manifest | Incluir PDB identity e profile flags |
| 174 | ETW/WPP/IFR | ⬜ | Telemetria em registro | Trace de alta resolucao sem persistir em ISR/DPC |
| 175 | KDNET runbook | 🟡 | Rascunho existe | Corrigir e validar com host separado |
| 176 | Crash dump/TDR dump | ⬜ | Nao operacional | Coleta automatica e simbolizacao |
| 177 | Orçamento de corridas | ⬜ | Nao formalizado | Maximo por gate, criterio de abandono e recovery |
| 178 | Matriz de hardware | ⬜ | Uma BC-250/monitor | Diferentes monitores/cabos/modos apenas mais tarde |
| 179 | HLK | ⬜ | Nao iniciado | Depois de funcionalidade e estabilidade |
| 180 | Driver de producao | ⬜ | Apenas laboratorio | Remover gates perigosos, proteger firmware e assinar |
| 181 | Instalador/desinstalador | 🟡 | Scripts de lab/recovery | Fluxo de produto reversivel e versionado |
| 182 | Documentacao final | 🟡 | Muitos relatórios/roadmaps | Consolidar quando contratos estabilizarem |

## 12. Proxima sequencia objetiva

> **Re-sequenciado em 2026-09-02.** As ordens 2 e 3 originais estao encerradas
> com reprovacao — ver [`docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md`](docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md).
> O gargalo real subiu para os itens #133/#134 (UMD), antes enterrados na
> secao 9. Plano completo em [`docs/161_PLANO_MESTRE_POS_GATE_1_5.md`](docs/161_PLANO_MESTRE_POS_GATE_1_5.md).

| Ordem | Proximo marco | PASS obrigatorio |
|---:|---|---|
| 1 | Limpar falsos sucessos e corrigir ferramentas | Build/test/audit; nenhuma DDI vazia retornando sucesso |
| ~~2~~ | ~~Implementar `RenderOnlyLab`~~ | **Concluido** (#100) |
| ~~3~~ | ~~Corrida RenderOnlyLab → Codigo 0~~ | **REPROVADO** — inviavel; a recusa antecede qualquer cap de render |
| **2'** | **Descobrir a causa do Codigo 43** | `NTSTATUS` capturado por KDNET **ou** lista fechada de divergencias do INF contra um WDDM real |
| **3'** | **Fechar o Codigo 43** (provavelmente #133+#134) | `bc250mcdm / Codigo 0` sustentado, e o dxgkrnl avanca alem de `WDDMDEVICECAPS` |
| 4 | D3DKMT minimo | Abrir adapter, criar device/context/allocation |
| 5 | Submissao WDDM | Entrada pelo dxgkrnl, fence GPU e retirement real |
| 6 | Reset/TDR | Timeout controlado recupera sem sujar engine |
| 7 | Vulkan compute intermediario | `vulkaninfo` + compute shader fisico, ainda sem display DCN |
| 8 | Inventario DCN read-only | Bases/versao/pipes/OTG/HUBP/DP provados |
| 9 | Takeover do modo GOP | Full WDDM Codigo 0 mantendo imagem |
| 10 | Flip/VBlank real | Duas surfaces, address-in-use e interrupcao fisica |
| 11 | AUX/EDID/link training/mode-set | DisplayPort reprogramavel e recuperavel |
| 12 | Unificar display+render | Desktop e RenderNode simultaneos em Codigo 0 |
| 13 | Vulkan graphics/WSI | Triangulo apresentado pela BC-250 |
| 14 | RADV/Zink/DXVK/VKD3D | APIs traduzidas sem fallback |
| 15 | Jogos e LLMs | Carga real, estabilidade, TDR e desempenho medidos |

## Veredito atual

- **Compute direto no silicio:** VALIDADO. Reproduzido n=2 (3.79 e 3.80): P1 17/17, P2 16/16, zero mismatches.
- **Display KMDOD/GOP:** VALIDADO.
- **Render-only WDDM:** **REPROVADO** — estruturalmente inviavel neste caminho (Gate 1.5 encerrado em 2026-09-02).
- **Render WDDM:** NAO VALIDADO EM HARDWARE. Codigo 43 com causa localizada na sequencia, ainda sem causa-raiz provada.
- **Display acelerado/full WDDM:** NAO VALIDADO EM HARDWARE.
- **Vulkan, jogos e LLM via driver WDDM:** NAO VALIDADO EM HARDWARE.

