# 709 — Revisão dos achados do driver AMD Windows (707/708 da raiz), checklist e a questão do DCN

**Data:** 12/10/2026. Offline, sem corrida.

**Fontes revisadas:**
- `707_PESQUISA_DRIVER_AMD_WINDOWS_RDNA1_REA_NIMEZ.md` e `708_FUNCIONAMENTO_COMPLETO_DRIVER_AMD_WINDOWS_WDDM_RDNA1.md`, na raiz do repositório. Os números colidem com os docs 707/708 em `docs/`.
- O pacote `C:\AMD\NimeZ-25.3.1`.

**Como foi conferido:**
- contra o upstream que já está no repositório: `inc/linux_ref/sdma_v5_0.c`, `gmc_v10_0.c`, `gfxhub_v2_0.c`, `mmhub_v2_0.c` e os cabeçalhos de offset;
- contra o nosso código;
- contra a evidência das corridas.

**Regra (Leis 7 e 8):** o que vem só do binário da AMD é **P2 no máximo**. Vira código apenas com proveniência cruzada no upstream ou no catálogo (docs/03).

---

## 1. Ressalvas sobre os dois documentos

1. **Misturam o nosso vocabulário com o da AMD.**
   - O B3 diz que o driver da AMD trata `PROOF_TIMEOUT` do SDMA1, mas `PROOF_TIMEOUT` é um termo nosso (P4.1).
   - A matriz cita "T9" e "P5" como se fossem achados no binário.
   - Quando isso aparece, a afirmação sobre a AMD é **H**, não P2.
2. **Afirmam causa sem medição.**
   - O 708 §4.1 diz "erro fatal na BC-250: condição de corrida no WPTR". O nosso anel do SDMA0 é serializado por um spinlock, e nenhuma corrida mediu corrida de WPTR.
   - O 708 §4.3 diz que árvores multinível no domínio de sistema "corrompem o MMHUB". Não há medição disso.
3. **O pacote NimeZ não suporta a BC-250 de fato.**
   - O `13FE` só aparece na base de comparação do NimeZ (`MultiParse`/`MSFTParse`), mapeado para `ati2mtag_Navi12` com o nome "Cyan Skillfish / Navi12 Lite".
   - Os INFs instaláveis (`u0412788.inf.bak`, `u0412791.inf.bak`) não têm o `13FE` nem a seção Navi12.
   - As chaves citadas no A6 são de **outras** famílias: Navi10/14 e APUs Renoir/Rembrandt/Phoenix.
4. **Versão do DCN.**
   - Os docs dizem DCN 2.1 (Renoir, com `dcn21_dchvm_init`).
   - A nossa sonda já identifica **DCN 2.0.1** (`[DCN_PROBE] ... DCN 2.0.1`), e é o `dcn201` que o amdgpu usa para a Cyan Skillfish (P1, a conferir no fonte).
   - O roteiro de DCN dos docs aponta o bloco errado.

## 2. Achado por achado

| # | Achado dos docs | Conferência | Situação |
|---|---|---|---|
| A1 | Paginação deveria ir para a fila `PAGE_RB` (0xE0), não para `GFX_RB` (0x80) | O offset existe (`mmSDMA0_PAGE_RB_CNTL 0x00e0`, P1). O `sdma_v5_0.c` do amdgpu **não usa** fila de paginação em Navi10: só o anel GFX (P1). O Linux usa o anel GFX para a paginação e **não travou** sob 149 despejos (L-B, doc 552) | **Não é paridade nem causa provada.** Fica como experimento opcional (H) |
| A2 | Semáforo `VM_INVALIDATE_ENG*_SEM` na invalidação | O amdgpu usa o semáforo **só no MMHUB** (`gmc_v10_0_use_invalidate_semaphore`, P1); no GFXHUB há um TODO. Nós já usamos o `ENG17_SEM` no MMHUB (`bc250_active_miniport.c:20132`) | **Já em paridade** |
| A2 | `lfence`/barreiras | O handler do FlushTlb faz `_mm_mfence()` (P1b) | Já coberto |
| A3 | Paginação serializada (`KMD_EnablePageMigration=0`) | Nós não migramos páginas por conta própria; quem decide é o VidMm | Nada a fazer; o A/B de serializar FILLs (doc 705) segue aberto |
| A4 | "GVM não suporta multinível no domínio de sistema" | O domínio de sistema é a VMID 0 (GART). O amdgpu usa `CONTEXT0 PAGE_TABLE_DEPTH=0` e multinível nas VMIDs de usuário (P1). A nossa VMID 0 já é plana (`0x007FFE01`, profundidade 0); a P5 de 4 níveis é das VMIDs de usuário | **Já em paridade.** O doc confundiu os dois domínios |
| A5 | Segmentos: carve-out `CpuVisible=1` e abertura `Aperture=1` com PA real | Bate com o nosso desenho (segmento 1 local, segmento 2 abertura; doc 704: PA crua) | Já em paridade |
| A6 | Chaves de power gating no INF | São chaves do PowerPlay **do driver da AMD** para outros chips; não são registradores. O nosso driver não liga power gating | Sobra uma pergunta legítima (C3): em que estado o POST/SMU deixa o PG do MMHUB e o MGPG do GFX |
| A7 | `WaitForUtcl1AlogClean` no SDMA | Nome sem semântica e sem equivalente achado no `sdma_v5_0.c` | H; não implementar sem fonte |
| B1 | Reset em níveis: engine/KIQ, BACO, Mode1 | Plausível e útil para o TDR do produto (item 25) | Roadmap do TDR, não a trava |
| B2 | Firmware pelo PSP | Já fazemos (PSP, TMR.1, RLC.1) | Já em paridade |
| B3 | SDMA1 com timeout → tudo no SDMA0 | Nós fazemos o mesmo fail-closed. O `PROOF_TIMEOUT` intermitente (266, 267; subiu na 268) é nosso | Investigar a intermitência (C5) |
| B4 | IH: cair para polling se a doorbell do RPTR falhar | Bate com a nossa decisão da P3.3 (desligada) | Já coberto |
| B7 | `E_OUTOFMEMORY` em toda falha de `CreateResource` | T9 do doc 691 | Conferir o estado (C6) |
| §4.4 / §5 | Flip pelo HUBP com `OTG_GLOBAL_SYNC_LOCK` | Correto como desenho de produto (P9). Exige offsets do DCN 2.0.1 com proveniência | Ver §3 |

## 3. A questão do DCN

**Proposta do Daniel:** parar a investigação da trava e implementar o DCN, porque ele roda em clocks e domínios de energia próprios, e os resets de fila e de CU não o travariam.

**O que a evidência diz sobre a trava (P0):**
1. **Não há reset no momento da trava.** Nas corridas 263–268: zero TDR, zero `ResetFromTimeout`, zero reset de fila ou CU. A 268 morreu com a GPU sem trabalho registrado.
2. **Quem morre é a máquina inteira, não só a imagem.**
   - O rastro para de ser escrito pela CPU.
   - O KD não consegue entrar (memória "KD não para a trava dura").
   - Uma parada só do display deixaria a CPU viva escrevendo o rastro.
3. **Mesmo DCN do GOP, outro driver, sem trava.** No L1 (doc 662), o Linux com `amdgpu.dc=0`, ou seja o DCN exatamente como o GOP o deixou, rodou 2 × 615 s sob paginação sem trava. Ressalva: VRAM pouco cheia.
4. **Escrita no scanout não é necessária.** A 251b fez zero blt no scanout e travou igual (doc 666).
5. **Cópias de apresentação não são necessárias.** A 263b travou com um blt só (doc 703).

**Leitura:**
- O DCN, no estado em que o GOP o deixa, **não é a causa** da trava (P0 pelos pontos 3 e 4).
- Implementar o DCN **não deve resolver a trava** (H forte).
- O que o DCN muda de verdade: tira do SDMA0 as cópias de apresentação (~23 por segundo de blt de tela cheia). Isso remove uma variável, mas a 263b mostra que essa variável não é necessária.

**Recomendação:**
- **Não parar a investigação da trava.** O DCN (P9) é parte do produto e pode começar; abrir a P9 é decisão do Daniel, porque a escrita em DCN hoje é proibida fora dela.
- **Primeiro passo de DCN que também serve de experimento:** flip pelo HUBP0 (endereço da superfície + `OTG_GLOBAL_SYNC_LOCK`), sobre o pipeline que o GOP já deixou ligado, sem treinar link nem mexer em clock.
  - Isso zera as cópias de Present no SDMA0.
  - Se a trava continuar, fica provado que as cópias de Present não entram nela.

**Pré-requisitos (Lei 7):**
- **Cabeçalhos de offset e máscara do DCN 2.0.1 em `inc/linux_ref`.** Hoje só existe o `DMU_BASE` (em `cyan_skillfish_ip_offset.h`). Os registradores HUBP/OPTC precisam vir do upstream (`dcn_2_0_0_offset.h` e o código do `dcn201`), não do binário da AMD.
- **Leitura antes de escrita.** A sonda DCN já lê OTG e superfície (doc 660: OTG 2560×1080, superfície em PA 0x460000000). Lembrete: leitura de registrador não é inócua (memória: a 251 travou numa foto de clock gating). Só ler o que já foi lido sem dano.

## 4. Checklist

**Legenda:** ✅ feito ou em paridade · 🔲 a fazer · 🟡 decisão do Daniel.

### Trava dura (continua sendo a prioridade)

- [ ] **C1. DBG.3: fechar o ponto cego da 268.** Eventos no rastro para:
  - `COMPOSITE_BLT` e Present/flip no SDMA0 (início e fim);
  - começo de `SubmitCommandVirtual` (antes do `PT_WALK`);
  - programação de VMID e instalação de raiz (`SetRootPageTable`, pool de VMID);
  - migalhas de MMIO também no dreno, não só no tique.

  *Teste:* próxima trava com o último evento dentro de ~1 ms do fim e com essas classes presentes.
- [ ] **C2. A/B de serializar a paginação** (doc 705): esperar a fence de cada envio de paginação antes do seguinte. *Medida:* processos do T3 até a trava, várias corridas.
- [ ] **C3. Estado de power gating do MMHUB e do GFX depois do POST, só leitura.**
  - Primeiro procurar nas fotos que já existem (BCG1, doc 556; varredura do doc 663).
  - Só ler registrador novo com proveniência e risco declarado.
- [ ] **C4. Comparar a sequência "processo novo"** (CreateProcess → SetRootPageTable → instalação de VMID → primeira PTE) com o amdgpu (`amdgpu_vm_init` / `vm_flush`). *Pergunta:* invalidamos a TLB e o L2 na troca de raiz como ele faz?
- [ ] **C5. SDMA1 `PROOF_TIMEOUT` intermitente** (266 e 267 falharam, 268 subiu): ler o motivo nos logs das três.
- [ ] **C6. T9 do doc 691: `E_OUTOFMEMORY`** em toda falha de `CreateResource`. Conferir o estado do código.

### Já em paridade (não refazer)

- [x] Semáforo de invalidação no MMHUB (`ENG17_SEM`).
- [x] GART da VMID 0 plana (profundidade 0); multinível só nas VMIDs de usuário.
- [x] Firmware pelo PSP (TMR.1, RLC.1).
- [x] Segmentos carve-out e abertura com PA real.
- [x] Barreira de memória no FlushTlb.

### Fila de paginação dedicada (opcional, H)

- [ ] **C7. Paginação na `PAGE_RB` do SDMA0** (`0x00E0`, P1 de offset). Não é paridade com o amdgpu. Só depois de C1/C2, se a trava continuar ligada ao SDMA0.

### DCN / P9 (decisão do Daniel)

- [ ] **D0. Abrir a P9:** liberar escrita em DCN (governança do CLAUDE.md).
- [ ] **D1. Trazer para `inc/linux_ref` os offsets e máscaras do DCN 2.0.1** (HUBP, OPTC/OTG, DCSURF) com proveniência. Catalogar em docs/03.
- [ ] **D2. Ler (só ler) o pipeline do GOP:** HUBP0 (endereço, pitch, formato) e OTG0 (lock, totais), com a sonda que já existe.
- [ ] **D3. Flip pelo HUBP0** no `SetVidPnSourceAddress`: lock → endereço → unlock, sem cópia SDMA.
  - *Teste:* frame novo na tela, zero blt de Present, contagem de flips igual à de Presents.
  - *Lei 2:* readback do endereço e pixels conferidos na superfície.
- [ ] **D4. Interrupção de VBLANK/flip-done** pelo DCN para o `DXGK_INTERRUPT_CRTC_VSYNC`, no lugar do temporizador.
- [ ] **D5. Depois:** cursor pelo HUBP, MPO3, EDID real por AUX, mode set.

**NÃO VALIDADO EM HARDWARE:** todos os itens acima.
