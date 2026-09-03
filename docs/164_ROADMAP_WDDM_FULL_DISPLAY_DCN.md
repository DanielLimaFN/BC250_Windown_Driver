# Roadmap 164 — WDDM Full Display + Render com DCN 2.0.1

**Data-base:** 2026-09-02
**Decisão do dono do projeto:** o destino é **WDDM Full Display + Render** — monitor no
DisplayPort da própria BC-250, desktop acelerado, Vulkan/D3D12 e jogos.
**Render-only / cross-adapter está descartado como arquitetura**, não por gosto, mas
porque a BC-250 é um APU de die único: não existe segundo adaptador de display real
para receber a apresentação.

**Consequência inescapável:** **o DCN 2.0.1 é obrigatório.** Este documento substitui a
ordem prática do `ROADMAP_BC250_WDDM_DISPLAY_RENDER_DCN.md` (seção 5) e re-sequencia as
fases do `docs/161_PLANO_MESTRE_POS_GATE_1_5.md` sob essa decisão.

> Compilar, assinar ou retornar `STATUS_SUCCESS` não equivale a funcionar.
> Onde não existir prova física, consta **NÃO VALIDADO EM HARDWARE**.

---

## 1. Onde o projeto realmente está

### 1.1 Provado no silício

| Fato | Evidência |
|---|---|
| Compute direto funciona | P1 17/17, P2 16/16, zero mismatches, fence física em VRAM, n=2 (3.79/3.80) |
| SDMA, CP/PM4, RLC, GCVM VMID0 | Tripé completo nos docs 63–99 |
| Display KMDOD sobre framebuffer do GOP | Ownership físico provado (doc 26) |
| Ciclo de ensaio automático | bind → janela → retrato → rollback → reboot → `BasicDisplay / Código 0`, sem intervenção |

### 1.2 O bloqueio atual, medido

O `dxgkrnl` abandona o adaptador **imediatamente** no retorno do `StartDevice`:

```
AddDevice → QueryInterface → QAI(29 = WDDMDEVICECAPS) → QueryInterface
          → StartDevice(SUCCESS) → QueryInterface → StopDevice → RemoveDevice
```

- `V5QaiCalls = 1`, `V5QaiTypeSeenLo = 0x20000000` → **`DXGKQAITYPE_DRIVERCAPS` nunca é
  perguntado.** Todo o `DRIVERCAPS` que temos em
  [bc250_mcdm_callbacks.c:852](../driver/wdk/bc250_mcdm_callbacks.c:852) é código morto até hoje.
- 12 corridas com `RenderNode=1` (9 com `1/1`, 3 com `0/0`) param no **mesmo** ponto.
  O número de sources/children **não** é o discriminador.
- As 8 recusas de `QueryInterface` são todas de interfaces opcionais (I2C/DDC, OPM ×4,
  Miracast, WDDM3_ON_VB) — legítimas.

### 1.3 Resultados negativos já fechados — não reabrir

| Caminho | Estado |
|---|---|
| `RENDER_ONLY_LAB` (0 sources / 0 children sob `Class=Display`) | **REPROVADO** (doc 162). Intestável: a recusa acontece antes de qualquer cap de render ser consultada, e custou tela preta + reboot nas duas corridas |
| `MiscCaps.ComputeOnly` | **Inalcançável.** Trafega só por `DRIVERCAPS`, que nunca é pedido |
| Migrar para `Class=ComputeAccelerator` / MCDM puro | **Fora do destino.** O `basicrender.inf` da própria Microsoft é `Class=System`, não Display — modelo diferente, e nenhum dos dois entrega display. Se o destino é Full Display, o INF **tem** que ser `Class=Display` |
| Contrato WDDM 2.0 vs 2.7 | Falham igual (item 93 do checklist) |

### 1.4 A hipótese viva, já implementada, ainda não ensaiada

**O adaptador é recusado por não declarar user-mode driver.** Sustentação estrutural
(comparação com `rdvgwddmdx11.inf` desta máquina): todo miniport WDDM Display-class real
declara UMD numa seção `.HW`/`SoftwareSettings`. Nós não tínhamos.

As Fases 0/1/2 do doc 163 fecharam isso **offline**:

- Seção `.HW` + `InstalledDisplayDrivers` / `UserModeDriverName` / `Wow` no
  [bc250mcdm.inf](../driver/wdk/package/bc250mcdm.inf)
- `bc250umd.dll` real, com `OpenAdapter10/10_2/11/12`, `pfnGetSupportedVersions`
  honesto e `pfnCreateDevice` retornando `E_NOTIMPL` (fail-closed)
- Pacote **3.81.0.0 assinado** em `output_active`, `BuildKind = MCDM_FAIL_CLOSED`,
  `SkipHwBringup=1`, `RenderNode=1`, `RenderOnlyLab=0`, `HardwareValidated: false`

**Este é o item mais próximo de virar informação, e ele já está pronto na bancada.**

---

## 2. Por que o DCN é obrigatório — o argumento em uma linha

O DisplayPort da placa sai do DCN 2.0.1 **dentro do mesmo die**. Hoje o UEFI GOP o
programou e o mantém aceso. No instante em que o `dxgkrnl` aceita nosso miniport como
POST device, o GOP deixa de ser dono. Se ninguém assumir HUBP/OTG/DIG/PHY, o sincronismo
cai, o monitor entra em economia, o watchdog estoura e o sistema derruba. Não existe
terceiro caminho nesse silício.

### 2.1 O que a análise da outra IA acertou e o que ela omitiu

**Acertou:** o inventário do que falta (DCHUB/DPP/MPC/OPP/DCCG/PHY/AUX/DPCD, sem takeover
do GOP) e a conclusão de que sem DCN não se mantém um DP aceso sob WDDM Full.

**Errou/omitiu três coisas:**

1. Afirmou `0x116 VIDEO_TDR_FAILURE` como fato, com cadeia causal inventada
   (`ResetFromTimeout` → `NOT_SUPPORTED` → bugcheck). Não há evidência. Preto + 15–20 s +
   reboot é igualmente compatível com `0x119`, `0x113`, ou bugcheck no próprio miniport
   durante `CommitVidPn`. Existe `C:\Windows\MEMORY.DMP` (1,2 GB, 02/09 18:00) e o
   `cdb.exe` instalado — isso é resolvível com prova (ver 4.1).
2. Recomendou render-only/cross-adapter como "imediato e recomendado" sem saber que o
   doc 162 já o reprovou por medição.
3. Não disse que aquele caminho **não entrega o objetivo**: sem segundo adaptador de
   display real, o presenting adapter seria o MSBDA — cópia por PCIe para memória de
   sistema e blit por CPU, sem flip, sem VRR. Teto de desempenho inaceitável.

### 2.2 O degrau que ela apresentou como inexistente

Ela deu um binário: render-only, ou portar o `dcn20` inteiro ("meses"). Existe um degrau
intermediário que muda o tamanho do roadmap:

| Nível | O que faz | O que **não** faz |
|---|---|---|
| **DCN inherited-mode** | Não programa timing, não faz link training, não toca no PHY. Deixa OTG rodando e link já treinado pelo GOP; só reaponta o `DCSURF_PRIMARY_SURFACE_ADDRESS` do HUBP para nosso framebuffer e habilita o VBlank do OTG no IH | Trocar resolução, hotplug, segundo monitor, HDR, VRR |
| **DCN full modeset** | DCCG, sequenciamento DPP/MPC/OPP, AUX/DPCD/EDID, link training, HPD | — |

Para jogos você precisa do segundo. Mas o primeiro é o **gate que prova que a placa
mantém um pixel aceso sob controle do `dxgkrnl`** — e é ele que separa "tem chance" de
"não tem". Ele vem antes.

### 2.3 Ativo descoberto: material de DCN já existe no repositório

O `referencias/AMD-BC-250-Windows-Driver/` traz seis ferramentas de probe de DCN
(`dcn-probe.c`, `dcn-live-scan.c`, `dcn-live-counter.c`, `dcn-active-scanout.c`,
`dcn-init-test.c`, `dcn-targeted-probe.c`) e um mapa de registradores.

**Com uma ressalva forte:** o `docs/REGISTER-MAP-BC250.md` de referência lista
`DCN base 0x5000` e `MMHUB base 0x5000` — **colisão**, o que invalida o mapa plano. O
`BC250-LINUX-IP-MAP.md` traz a forma correta: `DMU | 0x12, 0xC0, 0x34C0, 0x9000`, que são
os **quatro segmentos** da instância 0 do DMU no esquema SOC15. No DCN cada registrador
carrega seu `BASE_IDX`, então **não existe base única** — o offset é
`DMU_BASE.seg[BASE_IDX] + reg`. Os offsets planos do repositório de referência
(0x5080/0x6000/0x7000) devem ser tratados como **hipótese a reconciliar**, não catálogo.

Isso não diminui o valor: são pontos de partida e um IOCTL de leitura que já existiu.
Diminui o custo do sub-gate 2.0 de "escrever enumeração DMU do zero" para "reconciliar
segmentos conhecidos contra o IP discovery da própria placa".

---

## 3. Magnitude — a parte que não vou suavizar

O destino equivale a escrever **um driver de display AMD** mais **um driver Vulkan**. As
fases D e F abaixo são, cada uma, maiores que tudo que o projeto entregou até hoje. A
fase F (mode-set e link training) é o item mais caro do projeto inteiro.

Isso não é argumento para desistir. É argumento para que cada fase tenha valor próprio e
critério de abandono explícito:

- **Fase B** entrega o primeiro adaptador WDDM aceito pelo Windows.
- **Fase E** entrega imagem na tela sob controle do driver.
- **Fase G** entrega compute dirigido pelo Windows — já suficiente para LLM.
- **Fase I** entrega jogos.

Um projeto que para na Fase E ou G ainda é um resultado real e publicável.

---

## 4. O roadmap

Regras que valem em todas as fases:

1. **Offline antes de silício.** Corrida física é o recurso escasso.
2. **Mineração do corpus antes de cada corrida.** Já refutou inferência errada uma vez.
3. **Fire-and-collect.** Janela fixa, retrato, rollback e reboot incondicionais.
4. **Boot frio entre binds.** Estado de engine não cai com desbind.
5. **Orçamento por gate** definido *antes* de armar: teto de corridas + critério de abandono.
6. **Tripé no silício** em toda prova: `RPTR==WPTR`, fence física em VRAM com poison,
   conferência byte a byte.

---

### Fase A — Instrumentar a falha (offline, custo zero de corrida)

| # | Tarefa | Entrega |
|---|---|---|
| A.1 | Analisar `C:\Windows\MEMORY.DMP` com `cdb` **elevado** | Bugcheck real, stack, DDI de origem. Encerra a especulação do `0x116` |
| A.2 | `!analyze -v` + `lm m bc250*` + `.frame`/`dps` no frame do miniport | Se o crash for nosso, o arquivo e a linha |
| A.3 | Runbook KDNET com **segunda máquina** | Breakpoint/stack/`g` funcionando. `-kl` local não serve (doc 124) |

Comando (PowerShell **como Administrador**):

```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q" > "$env:USERPROFILE\Desktop\bugcheck.txt"
```

**PASS:** bugcheck identificado com stack. **FAIL aceitável:** dump é de outra causa —
registrar e seguir; a Fase B não depende dele.

---

### Fase B — Fechar o Código 43 · o gargalo real

O pacote 3.81 já está assinado e auditado. Falta a corrida.

| # | Tarefa | Critério |
|---|---|---|
| B.1 | Corrida 3.81 fire-and-collect, orçamento **2 corridas** | `bc250mcdm / Código 0` sustentado **e** `V5QaiTypeSeenLo` com o bit de `DRIVERCAPS` aceso |
| B.2 | Se FAIL: comparar anel de DDI 3.80 vs 3.81 | Descobrir se o handshake **avançou** — avançar e falhar em ponto novo já é informação |
| B.3 | Se FAIL sem avanço: Fase A.3 (KDNET) passa a ser pré-requisito bloqueante | Capturar o `NTSTATUS` exato com que o `dxgkrnl` desiste |

**Risco conhecido desta corrida:** 3.81 é display+render (`1/1`) **sem DCN**. Se o
`dxgkrnl` aceitar o adaptador, ele tira o GOP e a tela apaga — exatamente o cenário do
seu relato. O harness de rollback + reboot é o que torna isso aceitável. Se A.1 mostrar
bugcheck no nosso miniport, **corrigir antes** de rearmar.

**Marco:** primeiro Código 0 em `DxgkInitialize`. Destrava tudo o resto.
**Critério de abandono:** 4 corridas sem avanço no anel de DDI → parar e resolver KDNET
antes de gastar a quinta.

---

### Fase C — Inventário DCN somente leitura (paralelo, risco zero)

**Pode começar hoje, sem competir com a Fase B.** O `bc250dbg` já expõe
`IOCTL_BC250DBG_REG_READ` ([inc/bc250dbg_abi.h:49](../inc/bc250dbg_abi.h:49)), e leitura
não toca em ownership: o BasicDisplay continua dono da tela durante todo o inventário.

| # | Item checklist | Tarefa | Critério |
|---|---|---|---|
| C.1 | #4/#64 | Estender o IP Discovery para o bloco DMU. Reconciliar `0x12/0xC0/0x34C0/0x9000` contra a tabela da própria placa | Identidade, `HWID` e **os quatro segmentos** derivados da BC-250, com checksum e bounds, sob teste offline |
| C.2 | #65 | Catálogo de registradores DCN 2.0.1 por `(reg, BASE_IDX)`, não por base plana | Cada entrada citando header/fonte. Ambiguidade vai para quarentena |
| C.3 | #66 | Probe read-only sob `bc250dbg`, dois snapshots com intervalo | Zero escrita provada por auditoria; proveniência de cada leitura |
| C.4 | #67 | `OTG_STATUS_FRAME_COUNT` avançando entre snapshots | Contador vivo = display engine ativa sob o GOP |
| C.5 | #68 | Decodificar framebuffer herdado: `DCSURF_PRIMARY_SURFACE_ADDRESS`, pitch, formato, viewport, tiling, `SURFACE_INUSE` | Endereço bate com o framebuffer do GOP; timing bate com `2560x1080@60` |
| C.6 | #80 | ATOM/board tables: connector, encoder, clock source | Extraídos da própria placa |
| C.7 | #79 | Detectar presença/estado do DMCU/DMUB | Presente? rodando? **necessário** para inherited-mode? Não inventar blob |

**PASS:** mapa DCN 2.0.1 específico da BC-250, documentado, com contador vivo e
framebuffer decodificado — **tudo sem uma única escrita**.
**Custo:** zero corridas destrutivas. Isto é o melhor uso de tempo disponível agora.

---

### Fase D — Takeover inherited-mode (primeiro alvo de display)

Só depois de B (Código 0) **e** C (catálogo). Aqui começa o custo alto: cada FAIL é tela
preta + recovery + boot frio.

| # | Item | Tarefa |
|---|---|---|
| D.1 | #81 | Adquirir post-display ownership pelo contrato WDDM correto |
| D.2 | — | Snapshot completo de **todo** registrador que será tocado, antes de tocar |
| D.3 | — | Preservar exatamente modo, link, clock e timing do GOP. **Nenhum** mode-set |
| D.4 | #82 | Master update lock — reapontar `PRIMARY_SURFACE_ADDRESS` atomicamente |
| D.5 | #84 | VBlank do OTG mapeado no IH → ISR/DPC → `dxgkrnl`; `DxgkDdiControlInterrupt` real |
| D.6 | — | `GetScanLine` derivado do OTG, não sintetizado |
| D.7 | — | Rollback restaura o snapshot se qualquer readback divergir |

**PASS:** WDDM Full em Código 0, **imagem visível**, modo herdado intacto, frame counter e
VSync coerentes, 10 minutos sem TDR.
**Orçamento sugerido:** 5 corridas. Critério de abandono: 3 FAILs consecutivos com a
mesma assinatura → volta para a Fase C ampliar o catálogo.

---

### Fase E — Flip/scanout WDDM real

| # | Item | Critério |
|---|---|---|
| E.1 | #83 | Duas surfaces alternando por **flip físico**; `SURFACE_INUSE` e frame counter provam qual está no ar |
| E.2 | — | `SetVidPnSourceAddress` programando de verdade, com poison surface para detectar falso sucesso |
| E.3 | — | `DxgkDdiPresent` gerando comando/cópia válidos — **ou** anunciando apenas o modelo de flip realmente suportado. Nunca `STATUS_SUCCESS` sem efeito programado |
| E.4 | — | Retirement de flip coerente com o scheduler |

**PASS:** flip A/B provado fisicamente, desktop do Windows desenhando através do nosso
driver, no modo herdado.
**Marco:** este é o momento em que a BC-250 desenha a tela. **Ainda não:** trocar
resolução, hotplug, jogos.

---

### Fase F — Mode-set completo, AUX/DPCD e link training

A fase mais cara do projeto. Só entra depois de E estável.

`#74 HPD` → `#75 AUX` → `#76 DPCD` → `#77 EDID real` → `#78 link training (CR/EQ, lane/rate)`
→ `#71 DCCG` → `#69 DCHUB/DCHUBBUB (watermarks)` → `#70 DPP/MPC/OPP` → `#72 DIG` →
`#73 PHY` → `#85 mode-set transacional` → `#86 modos EDID` → `#87 hotplug`

Ordem obrigatória: começar por **um** modo conhecido e seguro, com rollback do link, antes
de enumerar modos do EDID. Portar os cálculos de timing/bandwidth do DC/DML apenas para
**um pipe**.

**PASS:** cold boot, reboot, troca de resolução e hotplug mantêm imagem e Código 0.

---

### Fase G — Render WDDM (pode correr em paralelo a D/E/F após a Fase B)

O compute já provado no silício passa a ser dirigido pelo Windows.

| # | Itens | Critério |
|---|---|---|
| G.1 | #103/#104 | `DRIVERCAPS` e nodes consumidos, derivados do hardware |
| G.2 | #105-#108, #111 | Harness D3DKMT: device/context/allocation com handles reais do `dxgkrnl` |
| G.3 | #107 | Segments, aperture, residency, paging; GPUVA 48-bit com journal, generation e rollback |
| G.4 | #110/#112 | `SubmitCommandVirtual` originado pelo `dxgkrnl`; fence física vinculada ao scheduler |
| G.5 | #113/#114 | IH/DPC → retirement reconhecido pelo scheduler. **Proibido** completar fence pela CPU |
| G.6 | #25/#116 | `ResetEngine` por node, `ResetFromTimeout`, `RestartFromTimeout` com quiesce real |

**G.6 não é negociável antes da Fase H.** Desenvolver UMD sobre um adaptador que não se
recupera de hang transforma todo bug de shader em reboot — e é provavelmente o que já
aconteceu com você.

**PASS:** submissão WDDM fisicamente aposentada; hang induzido é detectado, o engine é
resetado, o adaptador volta a aceitar submissão e o display sobrevive.

---

### Fase H — UMD e Vulkan

Substituir o `E_NOTIMPL` honesto de hoje por backend real, em subfases com PASS próprio:

- **H.1** ABI KMD/UMD versionada; loader, instance, physical device → `vulkaninfo` enumera a BC-250 com caps do hardware
- **H.2** Heaps e memory types coerentes com os segments da G.3; command pools/buffers → command buffer do UMD fisicamente aposentado
- **H.3** Fences, semáforos, barreiras, compute pipeline → compute shader Vulkan conferido byte a byte na VRAM
- **H.4** WSI: swapchain e present pelo DisplayPort da própria placa → triângulo na tela
- **H.5** Winsys RADV adaptado aos objetos WDDM deste driver → pipeline gráfico completo, estável em long-run
- **H.6** Camadas: DXVK (D3D9/10/11), VKD3D-Proton (D3D12), Zink (GL) → suites de conformidade sem WARP nem fallback

---

### Fase I — Jogos e LLM

Shader compilation, pressão de memória, suspend/resume, long-run; llama.cpp/DirectML via
Vulkan.

**PASS final:** desktop acelerado e jogos rodando na BC-250, sem WARP, com recuperação de
TDR funcional.

---

## 5. Caminho crítico

```
Fase A (dump/KDNET) ─┐
                     ├→ Fase B (Código 0) ─┬→ Fase D (takeover) → Fase E (flip) → Fase F (mode-set) ─┐
Fase C (DCN read-only)┘                    │                                                          ├→ Fase H → Fase I
                                           └→ Fase G (render + TDR) ─────────────────────────────────┘
```

- **Fase C é a única que pode andar hoje sem gastar corrida destrutiva.**
- Tudo depois de B está atrás de B.
- D/E/F e G podem correr em paralelo depois de B, mas H exige as duas convergidas.

---

## 6. O que fazemos agora — as três próximas ações

1. **A.1 — rodar o `cdb` elevado no `MEMORY.DMP`.** Custo: minutos. Encerra a
   especulação sobre o bugcheck e pode revelar defeito nosso antes de rearmar.
2. **C.1/C.2 — estender o IP Discovery para o DMU e montar o catálogo DCN por
   `(reg, BASE_IDX)`.** Offline, risco zero, e é o pré-requisito real de toda a Fase D.
   Começar reconciliando `0x12/0xC0/0x34C0/0x9000` contra a tabela da placa.
3. **B.1 — corrida 3.81**, com orçamento de 2 corridas, **depois** de A.1 não apontar
   defeito nosso. O pacote está assinado e auditado; falta o ensaio.

---

## Veredito

- **Compute direto no silício:** VALIDADO EM HARDWARE.
- **Display KMDOD/GOP:** VALIDADO EM HARDWARE.
- **Render-only WDDM:** REPROVADO — estruturalmente inviável e fora do destino.
- **WDDM Full Display, DCN, render dirigido pelo Windows, Vulkan, jogos:**
  **NÃO VALIDADO EM HARDWARE.**
