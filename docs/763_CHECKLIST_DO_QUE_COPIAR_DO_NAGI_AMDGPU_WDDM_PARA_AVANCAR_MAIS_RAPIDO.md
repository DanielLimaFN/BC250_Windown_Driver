# 763 — Checklist: o que copiar do Nagi (amdgpu-wddm) para avançar mais rápido

> **Data:** 15/10/2026. **Tipo:** análise offline, sem corrida e sem pacote novo.
> **Fonte:** `referencias/amdgpu-wddm` (projeto Nagi, autor D-Ogi, 2.646 commits desde 21/09/2026).
> **Confiança:** tudo o que vem do repositório deles é **relato de terceiros**, não P0 para nós. Um item só
> vira fato nosso depois da corrida que o fecha. **NADA DESTE CHECKLIST FOI VALIDADO EM HARDWARE.**

## 0. Regras de cópia (licença)

| Origem | Licença | Como usamos |
|---|---|---|
| Código do `amdgpu` que eles importaram (`driver/amdgpu-import`, partes do `driver/shim` marcadas com PROVENANCE) | MIT (AMD) | Copiar **do upstream**, com procedência no comentário, não do repositório deles |
| Dados de evidência (`evidence/linux/...`: rastro MMIO, dumps de anel, sweeps) | São medições, não código | Usar como gabarito de comparação, citando o caminho. Não vira fato nosso sem medição na nossa placa |
| Código próprio deles (`driver/kmd`, o resto do `driver/shim`, ferramentas) | PolyForm Noncommercial 1.0.0 | Ler e reescrever. Copiar só mantendo as linhas do `NOTICE` deles e sem uso comercial |
| Forks deles (Mesa, DXVK, vkd3d-proton) | A licença de cada projeto (MIT/zlib/LGPL) | Os patches seguem a licença do projeto original |

Legenda: ⬜ a fazer · 🟡 em andamento · ✅ feito com prova P0 · ❌ descartado

> **Atualização 15/10 ([doc 764](764_C01_A_C07_DO_NAGI_PARIDADE_PELO_RASTRO_DO_AMDGPU_PACOTE_3456_E_CRITERIO_CORRIDA302.md)):**
> C01, C03, C05, C06 e C07 em código no pacote 3.456.1.0 (🟡 = pronto, não validado em hardware); C02 feito offline
> (dumps de anel do Linux vazios); C04 bloqueado: os comandos do paging buffer usam endereço virtual da VMID de
> paginação, e o que roda no anel roda na VMID 0. Exige refazer o builder para emitir endereço MC.

## 1. Prioridade 1 — atacar a trava dura (offline primeiro)

- [x] 🟡 **C01 — Comparar a nossa sequência de bring-up com o rastro do `amdgpu` nesta placa.**
  - Fonte: `referencias/amdgpu-wddm/evidence/linux/2026-09-21-E03-init-trace/amdgpu-events.txt` (18 MB,
    10.018 escritas e 136.117 leituras em 82 fases), resumo em `docs/init-sequence-unitA.generated.md`,
    gerador `tools/trace/summarize_init.py`.
  - Como: extrair do nosso log de StartDevice cada escrita (registrador e valor) e alinhar por bloco (NBIO,
    GCVM, MMHUB, HDP, IH, GC golden, CP, SDMA). Toda diferença vai para uma tabela com o motivo, ou vira
    candidata a corrida.
  - Ressalva: a placa deles tem UMA de 8 GB. Endereços de VRAM, GART e aperture mudam; comparar campos, não
    endereços absolutos.
  - Teste que fecha: tabela de diferenças completa, arquivada em `docs/evidence/` (offline). As diferenças
    com efeito viram itens de corrida.
- [x] 🟡 **C02 — Comparar os nossos anéis com os dumps do Linux.**
  - Fonte: `.../2026-09-21-E03-init-trace/rings/` (`amdgpu_ring_gfx_0.0.0`, `ring_sdma0/1`, `ring_kiq`,
    MQDs de compute e do GFX).
  - Como: conferir MQD do GFX/KIQ e os primeiros pacotes de cada anel contra os nossos.
  - Teste que fecha: diferenças listadas campo a campo (offline).
- [x] 🟡 **C03 — Religar a RLC.1b (handshake RLC↔SMU desligado, `RLC_PG_CNTL` bit 23).**
  - Fonte: upstream `gfx_v10_0_rlc_start` com `pp_gfxoff=0`. Eles rodam estáveis assim
    (`driver/kmd/gfx.c:361-365`, `driver/shim/bc250_gfx.c:697-745`), com CG e PG em 0 antes do start.
  - Por quê: a 248 (com) e a 248b (sem) foram n=1 cada, inconclusivas.
  - Teste que fecha: corrida com o bit lido em 1 e o tempo até a trava contra a régua de FILLs
    (`tools/regua-fills.py`).
- [ ] ❌ **C04 — Paginação pelo anel direto, como bissecção.**
  - Fonte: desenho deles (`docs/design/paging-node.md`, `sdma-virtual-paging.md`): comandos do paging buffer
    copiados **dentro do anel do SDMA0**, VMID 2 só para paginação, IB nativo só depois.
  - Por quê: 5 de 47 travas terminam com um IB do SDMA0 sem RETIRE (doc 707) e a 267/270 pararam logo depois
    de um IB de paginação. Tirar o IB do caminho separa "IB do paging buffer" do resto.
  - Teste que fecha: corrida com a carga T3; contador de pacotes pelo anel = total de paging buffers, zero
    IB de paginação, e o tempo até a trava contra a régua.

## 2. Prioridade 2 — recuperação e interrupções

- [x] 🟡 **C05 — Wave kill como 1º passo do TDR do GFX.**
  - Fonte: upstream `gfx_v10_0_ring_soft_recovery` (MIT; removido do upstream em maio/2025, eles mantêm).
    Eles: `driver/shim/bc250_gfx.c`, `driver/kmd/hang.c`, `docs/design/hang-recovery.md`.
  - Teste que fecha: hang induzido (já existe o `TdrInduceHang`); ondas da VMID mortas, anel drenado,
    submissão seguinte aposentada, desktop vivo.
- [x] 🟡 **C06 — VSYNC pela interrupção (D-item do DCN aberto na 273).**
  - Fonte: `docs/design/vsync-interrupt-route.md` deles. A rota inteira é `OTG0_OTG_GLOBAL_SYNC_STATUS` bit 12
    (arma), bit 16 (ack), entrega como IH client 4 / src 0x57. Eles dizem que não há registrador de
    roteamento no DMU, e que o `0` deles era falta de controle positivo.
  - Nós já armamos o bit 12 (`driver/wdk/bc250_display_ddi.c:1883`). Procurar o defeito do nosso lado do IH:
    filtro por client no ISR, `IH_RB_CNTL` e se a leitura foi feita com o IH já recebendo outra fonte.
  - Teste que fecha: contador de vetores client 4 / src 0x57 no ISR ≈ refresh × tempo, na Tabela viva do DCN.
- [x] 🟡 **C07 — Regra do pool de VMID.**
  - Fonte: `driver/kmd/vmid_pool.h` (cabeçalho explica a regra): a raiz de uma VMID só muda depois que a
    última tarefa daquela VMID aposentou; LRU por uso, não por posse.
  - Como: conferir o nosso pool contra essa regra (offline). Só vira mudança se houver diferença.
  - Teste que fecha: leitura de código com a tabela de diferenças; se mudar, corrida com contadores de
    troca de raiz.

## 3. Prioridade 3 — desempenho e display

- [ ] **C08 — DPM pela SMU (clock dinâmico até 2000 MHz).**
  - Fonte: upstream `smu_v11_8` / `cyan_skillfish_ppt.c` (MIT). Eles: `driver/shim/bc250_dpm.c`,
    `docs/design/dpm.md`. Hoje temos clock fixo (CLK.1, 1800 MHz).
  - Respeitar `docs/hardware.md` deles sobre a proteção de sobrecorrente da placa acima de ~1850–2200 MHz em
    40 CUs (relato da comunidade).
  - Teste que fecha: clock lido da SMU sobe com carga e volta no ocioso; Tctl dentro do limite.
- [ ] **C09 — EDID por AUX.**
  - Fonte: DC do upstream (`dce_aux`, `dcn10_aux_initialize`). Eles: `driver/kmd/dpaux.c`, `edid.c`,
    `docs/design/display-modes.md`. Nosso AUX0 responde `NON_AUX_MODE` (doc 715).
  - Teste que fecha: 128 bytes de EDID com checksum válido e o nome do monitor no Windows.
- [ ] **C10 — Escala de modos menores sobre o timing nativo.**
  - Fonte: `driver/kmd/dcn_scale.c` deles (código próprio: reescrever). Exige ampliar a P9 (escala) por
    decisão do Daniel.
  - Teste que fecha: 1920×1080 pedido pelo Windows, CRC/foto da tela, sem troca de timing no cabo.

## 4. Decisões de arquitetura (do Daniel, não minhas)

- [ ] **C11 — DXVK como engine D3D11 dentro da nossa UMD.**
  - O que eles fazem: o runtime D3D11 do System32 carrega a UMD registrada, e ela carrega o DXVK como
    biblioteca interna (`driver/umd/dxvk`, `docs/design/d3d11-ddi-engine.md`). Nada fica ao lado do
    aplicativo. É diferente do "DXVK local por aplicativo" que a diretriz de 16/09 recusa.
  - Ganho: o tradutor D3D11 → Vulkan deixa de ser nosso. É o caminho que levou eles a jogos reais.
- [ ] **C12 — vkd3d-proton como engine D3D12 dentro da UMD.**
  - Eles: `driver/umd/d3d12`, `docs/design/d3d12-ddi-engine.md`. Abre D3D12, que hoje não temos.
- [ ] **C13 — Patches do winsys WDDM da RADV.**
  - Eles: `driver/icd/mesa-wddm2-*.patch` (budget, contabilidade de memória, cache intent, filas de runtime)
    e o fork `D-Ogi/mesa-amdgpu-wddm`. Comparar com o nosso `radv_wddm2_cs.c` e com a HEAP.1/WSI.2.
- [ ] **C14 — Registro de fatos com gate.** (opcional)
  - Eles: `docs/facts/data/*.yaml` e `tools/facts/gen_facts.py --check`. Útil para a BASE_DE_AVANCO, não muda
    o driver.

## 5. O que **não** copiar

| Item deles | Por quê |
|---|---|
| UMA de 8 GB | A Corrida 198 travou com 4 GB; decisão do doc 551 continua |
| Invalidação de TLB de todas as VMIDs | Não é causa da trava nos dois lados; a FT.1 fica |
| PTE escrita pela CPU fora do processo de paginação | Já estamos em paridade com o Linux (4c.4) |
| Alvo Windows 11 / WDDM 2.9 | A nossa plataforma é Windows 10 22H2 |
| Desbloqueio de 40 CUs | Fora da prioridade agora; risco de sobrecorrente |

## 6. Ordem proposta

1. C01 e C02 (offline, não gastam corrida).
2. Uma corrida com C03 + C05, mais o que C01 achar (várias mudanças por corrida, decisão do Daniel de 13/10).
3. C04 como bissecção da paginação.
4. C06 e C07.
5. Decisão do Daniel sobre C11–C13.
