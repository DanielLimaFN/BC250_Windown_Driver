# 706 — Briefing para a busca no driver AMD do Windows (RDNA1): problemas atuais e implementações futuras

**Data:** 12/10/2026. **Natureza:** documento de passagem, offline, sem corrida.
**Para quem:** a IA que vai pesquisar o driver da AMD para Windows (Adrenalin/PRO, família RDNA1: Navi10, Navi12, Navi14) em busca de respostas para os problemas do driver WDDM da BC-250.

**Leia antes:** [`CLAUDE.md`](../CLAUDE.md) (leis e linha de estado), [doc 678](678_PASSAGEM_DE_BASTAO_2026-10-09_TRAVA_DURA_DEPOIS_DA_LISTA_C1_C4.md) (passagem de bastão), [doc 691](691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md) (auditoria), [doc 703](703_TAXAS_MEDIDAS_DO_MMIO_EM_REGIME_E_NOVAS_FORMAS_DE_IDENTIFICAR_A_TRAVA.md) e [doc 705](705_MINERACAO_DE_46_TRAVAS_PAGINACAO_ATIVA_E_A_JANELA_CEGA_DE_UM_TIQUE.md).

**Aviso:** os docs 680–690 foram escritos por outra IA e **não são fonte confiável** (doc 691). Não cite números deles.

---

## 1. Contexto em 10 linhas

| Item | Valor |
|---|---|
| Placa | AMD BC-250, PCI `1002:13FE`, APU (Cyan Skillfish 2) |
| GPU | GC 10.1.3, 24 CUs, RDNA1. Parentes no Windows: Navi10 (gfx1010), Navi12 (gfx1011), Navi14 (gfx1012) |
| Memória | 16 GB GDDR6 compartilhada CPU/GPU. VRAM (carve-out) de **256 MB** por decisão do Daniel, igual ao BAR; o resto vem da RAM do sistema (segmento 2, como o GTT do Linux) |
| SO | Windows 10 Pro 22H2, build 19045. Contrato WDDM 2.7 |
| Driver nosso | KMD `bc250mcdm.sys` (miniport WDDM display+render), UMD `bc250umd.dll` (D3D10/11 traduzido para Vulkan), RADV (Mesa, ramo `wddm2`) como backend Vulkan |
| Display | herdado do GOP do UEFI. Não programamos DCN; o Present copia por SDMA para o scanout do GOP |
| Referência que funciona | Linux `amdgpu`, mesmo firmware, 256 MB: **não trava** sob paginação pesada (docs 552, 662) |
| Driver AMD no Windows | **não suporta** o `13FE`. Serve como referência de como a AMD trata RDNA1 e APU no WDDM, não como binário a instalar |
| Pacote atual | lab **3.415.1.0** (Corrida 267, pronta, ainda não rodada; doc 704) |
| Quem roda a placa | o Daniel. Corrida física é recurso escasso (Lei 9) |

---

## 2. Problema principal: a trava dura

### 2.1 Assinatura (P0, dezenas de corridas)

- A máquina inteira congela entre ~3 s e ~10 min depois do bind do nosso driver.
- **Sem** BugCheck, TDR, fault de GCVM, timeout, WHEA ou erro de MCA. KD (WinDbg) também morre: Ctrl+Break não responde.
- No último registro a **GPU está ociosa** e tudo o que foi submetido foi aposentado (RPTR==WPTR, fences sobre poison).
- No reboot: Kernel-Power 41. A placa sobe em Código 43 pelo nosso fusível anti-boot-loop.
- A memória parece ficar sem refresh: bits trocados numa página de controle crescem com o tempo travado (memória `bc250-migalhas-decaem-na-trava`).
- **Sem o nosso driver** (BasicDisplay) a plataforma não trava (45 min, W1). Teste de RAM: 8 GB sem erro.
- **Com o driver e o desktop parado**, 600 s sem trava (Corrida 237). É preciso uso: abrir menu Iniciar, Pesquisa ou processos novos com D3D11.

### 2.2 O que a mineração diz (doc 705, 46 travas)

- **Paginação ativa no fim em 96%** das travas (FILL nos 3 s finais), contra 26–61% nos controles. Só 1 de 46 travou com a paginação parada.
- Em 19 de 46, algum processo morre esperando a **fence de paginação de uma alocação nova** (`Allocate → MapAllocation E_PENDING → MakeResident E_PENDING → WaitPaging`).
- Nenhum pacote, tamanho ou tipo de operação é exclusivo do fim.
- **Risco sem memória:** ~1 trava a cada ~1.040 FILLs (coeficiente de variação ≈ 1). Combina mais com uma corrida de ordem entre CPU, GPU e memória do que com um conteúdo inválido determinístico (H).
- A máquina morre dentro do tique seguinte ao último registro (~110 ms), janela que o rastro não grava. A DBG.1b (Corrida 267) fecha essa janela.

### 2.3 Já eliminado como causa suficiente (medido no silício; não repetir)

Cada linha foi feita igual ao `amdgpu`, rodou em regime e a trava continuou. Detalhe no doc 678 §3 e na linha de estado do `CLAUDE.md`.

| Área | Eliminado |
|---|---|
| Display | DCN herdado e escrita no scanout (L1 no Linux; 251b sem escrita no scanout); MGCG do SDMA0 |
| Tabelas de página | PTE e invalidação de TLB pela CPU (4c inteira); PTE pelo SDMA0 e pelo SDMA1 (4c.4, P4.2); tabela de 2 → 4 níveis com VA de 48 bits (P5); leitura de tabela pela BAR |
| Memória | VRAM pela BAR vs pela DRAM sem HDP (P1b); tipo WC único (C3); TMR do PSP fora do alocador (TMR.1); MTYPE da folha (UC/NC; voltou a NC, T2); falta de memória no segmento 1 (M1b.1) |
| Zeramento | por cópia ou por CONST_FILL, fatias de 256 KiB ou 4 MiB (X2b, C4) |
| Engines | SDMA1 vivo e com PTE (P4); anel/fences em GTT pela GART (D2/P7); doorbells do SDMA0 e do GFX (P2, C1); faixa de doorbell do MEC (MEC.1) |
| Interrupções | IH ligado, conclusão do SDMA0 e EOP do GFX por interrupção (P3) |
| RLC e golden | RLC rodando o firmware ou na ROM (as duas travam; rodando parece encurtar, n pequeno); 29 golden settings aplicados (G.1) |
| Leituras da CPU | invalidação de VMIDs em excesso (FT.1); sonda SQ/CP por submissão (`GfxWaveProbe=0`); seletor GRBM no poll do MEC (MEC.2); silêncio da BAR (P1) |

### 2.4 Hipóteses abertas (H)

1. **GPU acessando endereço físico que não é RAM** via PTE de memória de sistema (segmentos 0/2). O encoder não confere o endereço; a Corrida 267 conta isso.
2. **Ordem/concorrência na paginação:** invalidação de TLB pelo SDMA enquanto outra engine traduz. O `amdgpu` registra (em `amdgpu_gmc.c`) que o SDMA de Navi 1x tem um defeito de silício em que invalidar durante uma tradução pode corromper memória, e por isso evita invalidar pelo SDMA nesse cenário.
3. **Power/clock gating sem coordenação** (RLC↔SMU). Testar exige escrever em CG/PG, caro e arriscado (doc 703 §5.7). Não temos SMU ativa (sem clock management).
4. **Acessos da CPU em regime que o `amdgpu` não faz** (doc 703 §1): laço de leitura de `SDMA0_RB_RPTR` no blt do Present, poll de `CP_RB0_RPTR`/`CP_ME_CNTL`, instalação de VMID pela CPU, leitura de `OTG0_FRAME_COUNT` (DCN) no VSYNC, par SMN 0x38/0x3C para o Tctl, WPTR do SDMA1 por MMIO. A 263b travou com um blt só, então o laço do blt não é necessário.

---

## 3. Outros problemas abertos

| # | Problema | Estado | Fonte |
|---|---|---|---|
| O1 | SDMA1 às vezes não sobe (`PROOF_TIMEOUT`, 2 em 38 binds); a PTE cai para o SDMA0 | aberto | doc 702 |
| O2 | TDRs do GFX no 1º IB depois de troca de VMID; depois do nosso reset do GFX o SQ dispara erros e ondas ficam presas; RLC nunca confirma safe mode | o TDR do menu sumiu com o encadeamento upstream da RADV (241b); a GFXRST.1 (`gfx_v10_0_soft_reset` no TDR) está no pacote, mas nunca foi exercitada porque não houve TDR desde então | docs 630, 636, 642 |
| O3 | Superfície compartilhada entre processos recusada (artefatos no app Clima) | lacuna funcional | doc 601 |
| O4 | Refresh real 60,5 Hz contra 56,13 Hz declarado pelo EDID sintetizado (defeito do VidPn) | aberto | doc 666 |
| O5 | Memória em dobro por textura (alocação WDDM + imagem da RADV) | desenho (T10) | doc 691 |
| O6 | `CreateResource` não devolve `E_OUTOFMEMORY` quando a RADV não aloca | T9 | doc 691 |
| O7 | VMID instalada pela CPU (PT_BASE/CNTL + invalidação ENG17 por MMIO) e "VMID 7 legada"; o `amdgpu` faz base e invalidação no anel do job | T8 | docs 694, 691 |
| O8 | Present espera o SDMA em laço de RPTR, não por fence/interrupção | paridade pendente | doc 703 §5.6 |
| O9 | Sem SMU/clock management: clocks da GPU ficam como o POST deixou | SMU.1 proposta | doc 672 |
| O10 | Produto ainda não chega a jogos: P7 (Vulkan completo, DXVK, VKD3D) e P8 (jogos) abertos | ver `BASE_DE_AVANCO.md` P7–P9 | — |

---

## 4. Implementações futuras (ordem atual)

| Ordem | O quê | Tipo | Doc |
|---|---|---|---|
| 1 | **Corrida 267**: DBG.1 (validador de endereço físico das PTEs de sistema, só conta) + DBG.1b (rastro drenado a cada paginação) | medição | 704 |
| 2 | Bisseção pela carga com o mesmo pacote: processo novo sem alocar; um processo alocando/liberando em laço; despejo forçado | 3 corridas curtas | 703 §5.2 |
| 3 | A/B de **paginação serializada**: esperar a fence de cada FILL antes do seguinte. Se a taxa ~1/1.000 cair, o mecanismo é de ordem | corrida | 705 §3 |
| 4 | VMID pelo anel como o `amdgpu` (T8) | código + corrida | 691 |
| 5 | Paridade dos acessos frequentes: blt por interrupção, poll do GFX só com fence pendente, VSYNC sem ler DCN, Tctl fora do rastro (T11) | código | 703 §5.6 |
| 6 | Reset do GFX igual ao `amdgpu` e TDR com display sobrevivendo | código + corrida | 630, 642 |
| 7 | SMU.1: clocks e power pela SMU como o `amdgpu` | desenho | 672 |
| 8 | T9/T10 (memória da UMD), superfície compartilhada (O3), VidPn (O4) | código | — |
| 9 | P7/P8: Vulkan completo pela RADV, DXVK, VKD3D-Proton, jogos | grande | BASE P7/P8 |
| 10 | P9: DCN com escrita (flip real, modos, EDID real). Opcional, não bloqueia jogos | grande | BASE P9 |

**Regra de avaliação:** nenhuma mudança vale com n=1. A dispersão sem mudança vai de 3 s a 600 s; compara-se FILLs ou processos da T3 até a trava, com várias corridas por configuração.

---

## 5. O que pedimos à busca no driver AMD do Windows

O driver da AMD é fechado. O que dá para ler sem desmontar código: o **INF** (chaves de registro por ASIC e seus padrões), as **strings** dos binários (`amdkmdag.sys`, `atikmpag.sys`, `amdkmpfd`, UMDs `atidxx64.dll`/`amdxx64.dll`/`amdvlk64.dll`), firmwares embutidos e cabeçalhos de versão. Também vale o pacote APU que já temos: `referencias/GFX_DriverGFX Driver_48.0.8.40630 STEAM DECK` (Van Gogh, RDNA2 APU), útil para a parte de APU/UMA.

**Prioridade A — ligadas à trava dura:**

| # | Pergunta | Onde procurar |
|---|---|---|
| A1 | Em Navi1x, **qual engine faz a paginação** (FILL/TRANSFER/PTE): SDMA0, SDMA1, GFX ou compute? Há chave para escolher ou desligar paginação por SDMA? | INF (chaves com `SDMA`, `Paging`, `DMA`), strings |
| A2 | **Como e por onde invalidam a TLB** em Navi1x: MMIO direto, KIQ, pacote no anel do GFX, pelo SDMA? Usam o **semáforo** dos engines de invalidação (`VM_INVALIDATE_ENG*_SEM`)? Há workaround nomeado para o defeito de invalidação concorrente do SDMA de Navi1x? | strings com `TLB`, `Invalidate`, `Flush`, `Sem`, `WA_`, `Workaround`, `Navi10/12/14` |
| A3 | A paginação é **serializada** (espera a fence de cada operação) ou em lote? Há limite de operações de paginação em voo? | INF/strings: `Paging`, `Serialize`, `MaxPaging`, `Inflight` |
| A4 | **Onde ficam as tabelas de página** em APU: VRAM ou memória de sistema? Qual MTYPE/snoop usam para PTE de memória de sistema (UC, NC, CC)? | Van Gogh/Renoir no INF; strings `MTYPE`, `Snoop`, `PTE` |
| A5 | Como o KMD declara **segmentos ao VidMm em APU** (carve-out, abertura, memória de sistema; `CpuVisible`; `Aperture`)? O PTE de segmento de abertura recebe endereço físico ou deslocamento? | pacote Steam Deck, INF (`UMA`, `Carveout`, `Aperture`, `Segment`) |
| A6 | Estado de **clock/power gating em Navi1x** no Windows: GFXOFF, CGCG/MGCG, CGLS, RLC, ULPS, deep sleep. Quais vêm desligados por padrão e para quais ASICs | INF (`PP_*`, `EnableUlps`, `GFXOFF`, `CG`, `PG`, `DisableGfx...`) |
| A7 | Há workarounds de **UTCL2/GL2/GCR** para RDNA1 (retry fault, XNACK, `GCR_CNTL`, flush de L2 antes de paginação)? | strings `UTCL2`, `Retry`, `XNACK`, `GCR`, `GL2` |
| A8 | Lista de **chaves de registro do KMD** aplicadas a Navi10/12/14 (seção AddReg de cada device ID), com valores. É o mapa de "o que a AMD liga/desliga por padrão" em RDNA1 | INF do pacote RDNA1 |

**Prioridade B — outros problemas:**

| # | Pergunta | Problema |
|---|---|---|
| B1 | Como fazem o **reset do GFX** em Navi1x no TDR (soft reset por engine, mode1, mode2, BACO) e se o display sobrevive | O2, item 6 |
| B2 | Versões dos **firmwares** embutidos para Navi10/12/14 (SDMA, RLC, MEC, PFP, ME, CE) comparadas com o `linux-firmware` do Cyan Skillfish; há notas de correção do SDMA? | trava, O1 |
| B3 | Como o SDMA1 é inicializado e provado; ele é usado em regime? | O1 |
| B4 | Layout das **doorbells** e do IH em Navi1x no Windows (o nosso `RPTR` do IH por doorbell encurtou a vida da máquina nas 242/242b) | P3.3 |
| B5 | Como tratam **recursos compartilhados entre processos** (Shared/NT handle) no KMD e na UMD D3D11 | O3 |
| B6 | Como gerenciam **clocks pela SMU** em Navi1x/APU (mensagens, tabelas PPTable) | O9 |
| B7 | Como o UMD D3D11 da AMD responde a falta de memória no `CreateResource` | O6 |

**Prioridade C — produto, depois da trava:** caps WDDM/DXGK que a AMD declara para RDNA1 (versão WDDM, `DXGK_DRIVERCAPS`, GPU VA, preempção, HW scheduling); como fazem o flip (MPO, DirectFlip) — só referência para P9.

---

## 6. Como entregar o resultado

1. **Um doc novo** em `docs/` (próximo número livre), uma seção por pergunta (A1, A2…), com "não encontrado" quando for o caso.
2. **Cada achado com proveniência:** arquivo, versão do pacote, seção do INF ou offset/string do binário. Sem isso o achado não entra no driver (Lei 7).
3. **Classificação de confiança** (Lei 8). Achado do driver AMD do Windows é no máximo **P2** (observado em outro driver, outro ASIC); interpretação é **H**. Nunca chamar de causa da trava.
4. **Separar RDNA1 dGPU de APU:** o que vale para Navi10 pode não valer para uma APU. Marcar de qual ASIC/seção veio.
5. **Cruzar com o `amdgpu`** (`referencias/linux_upstream_2026-09-29/`, `inc/linux_ref/`): diga se o Windows faz igual ou diferente do Linux. Diferença entre os dois é o achado mais valioso, porque o Linux não trava e nós já copiamos quase tudo dele.
6. **Não propor** WARP, fallback de software, VRAM maior que 256 MB, nem escrita em DCN fora da P9 (decisões vigentes).
7. Não copiar código nem redistribuir binários da AMD: registrar fatos (nomes de chave, valores, sequências) para reimplementar.

**NÃO VALIDADO EM HARDWARE:** a causa da trava dura.
