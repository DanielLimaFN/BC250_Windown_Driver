# 548 — M2: segmento 3 (VRAM além do BAR), com tamanho e base lidos da placa — critério da Corrida 194

> 01/10/2026, offline. Candidato **3.340.1.0**. **NÃO VALIDADO EM HARDWARE.**
> Motivação medida: [doc 547 §9](547_FRENTE_B_VKIMAGE_SOBRE_A_PROPRIA_ALOCACAO_E_CRITERIO_CORRIDA190.md).

## 1. Por quê

- **O impasse medido:** com alias nas texturas próprias houve TDR (191/192); sem alias, a memória não coube (193: o dwm.exe recriou o device 242 vezes por `vkAllocateMemory -2`, até a trava dura).
- **A memória parada:** a placa reserva `VramTotalMiB=512`, mas o segmento 1 só cobre a janela de CPU (`CpuVisibleMiB=256`, ~251 MB úteis). A outra metade era marcada como "opaca" pelo **nosso** driver, sem conteúdo conhecido.
- **A BIOS permite UMA de até 8 GB** (Daniel, 01/10). O Daniel vai alternar o valor entre corridas **sem me dizer**: o driver precisa ler o tamanho da placa a cada boot.

## 2. O que já era dinâmico (verificado no código)

| Valor | Fonte | Arquivo |
|---|---|---|
| VRAM total | `RCC_CONFIG_MEMSIZE` (MiB), o mesmo registrador do amdgpu, conferido contra `GCMC_VM_FB_LOCATION_BASE/TOP` e lido 2 vezes (instável = recusa) | `bc250_vram_topology_reader.c`, `bc250_vram_topology_contract.c` (sem teto de tamanho) |
| Janela de CPU | BAR atribuído pela BIOS (sondagem ReBAR) | sondagens `RebarAssignedMiB` |
| Base MC da SDMA | `VramMcStart` da topologia (`FB_LOCATION`), constante só sem topologia | `bc250_sdma_present.c:407` |

**O que era constante e passou a ser lido:**
- **A base PA da VRAM nas tabelas de página** (`BC250_GCVM_VRAM_MC_BASE_FALLBACK = 0x450000000`).
- Antes, uma base medida diferente **recusava** a MMU, e o driver não subia. Agora `VramMcBase` sai de `GCMC_VM_FB_OFFSET << 24`, a mesma conta do catálogo (docs/03, linha 640) e do Linux.
- Sem topologia lida, a MMU continua recusada: a base nunca é adivinhada.
- Em 512 MB a base medida é **idêntica** à constante (a guarda antiga nunca disparou, P0 por ausência da linha de recusa em todas as corridas).
- Log novo `[M2_BASE] ... IGUAL|DIFERENTE`.

## 3. O desenho

1. **Cálculo** (`Bc250ComputeHiddenSegment`, função pura em `bc250_segment_address_contract.c`):
   - `[alinhado(CpuVisible), alinhado(Total − guarda_topo))`;
   - menos toda região **em uso** dos dois alocadores internos que caia ali: a que toca o início empurra o início, e a que fica acima dele trunca o fim (mesmo critério do segmento 1). Os alocadores internos são criados com a VRAM **total**, e uma alocação top-down sem flag de CPU cairia nessa faixa;
   - sem VRAM além do BAR, ou faixa menor que 16 MiB: segmento ausente, com motivo nomeado.
2. **Guarda do topo: 16 MiB, HIPÓTESE declarada.** A tabela de descoberta não foi localizada (`DiscoveryTmrStatus=5`), e no Linux as reservas de firmware (discovery TMR, `vram_usagebyfirmware`) ficam no topo da VRAM. Troca por valor medido quando houver leitura delas.
3. **Declaração** (`QUERYSEGMENT4`):
   - `NbSegment = 3` só se o segmento existe;
   - descritor com `BaseAddress = HiddenSegmentBaseOffset`, `Size`, **`CpuVisible = 0`** e `LocalBudgetGroup = 1`;
   - tabelas de página continuam no segmento 1 (`PageTableSegmentId = 1`).
4. **Tabelas de página** (`Bc250McdmEncodeVidMmPteWithLevel`, o único codificador; os 6 sítios de escrita passam por ele):
   - folha com `Segment == 3` → `VramMcBase + HiddenSegmentBaseOffset + offset`, com faixa conferida;
   - PDE no segmento 3 é recusado;
   - segmento ausente também é recusado.
5. **Quem vai para lá** (`Bc250AllocationMayUseHiddenSegment`):
   - só alocações **da RADV** (token-ponteiro, fora das classes da UMD) **sem** `CPU_VISIBLE` nem `GPU_VA_ONLY`;
   - `SupportedSegmentSet = 0x5` (1 e 3), preferindo o 3, com `EvictionSegmentSet` em 0 (lição do 0x10E);
   - **nenhuma alocação da UMD**: o KMD copia essas por endereço MC, e o conversor só resolve o segmento 1 (fail-closed para o 3);
   - medido na 193: as imagens grandes da RADV chegam com `CpuVisible=0` (classe de token 0).
6. **Acesso da CPU:**
   - o mapeamento sob demanda de `Bc250ActiveReadVramU64` recusa offset além da janela de CPU **lida** (antes mapearia `FrameBufferPhysical + offset` fora do BAR);
   - as provas de dados por canário já eram limitadas a `FrameBufferSize`, então no segmento 3 ficam "não conferidas", sem tocar a memória.
7. **FILL e TRANSFER de paginação** usam GPUVA (`DestinationVirtualAddress`), ou seja, passam pelo mesmo codificador. O walker de tabela (`PT_RANGE`) não limita o endereço físico à janela.

## 4. Provas offline

- **`segment_address_contract_test`, caso 9:**
  - 512 MiB → 240 MiB; 4 GiB → 3.824 MiB; 8 GiB → 7.920 MiB (sem constante);
  - região no início empurra e no meio trunca; região abaixo do BAR não afeta;
  - sem VRAM além do BAR, faixa pequena demais e janela 0: recusas nomeadas.
- **`mcdm_callbacks_test`:** a folha no segmento 3 leva a base própria; fora da faixa, como PDE e com segmento ausente, recusa.
- **`allocation_abi_contract_test`:** token-ponteiro sem flags → segmento 3. `CPU_VISIBLE`, `GPU_VA_ONLY`, `RESOURCE`, `PRIMARY`, `COMMAND` e `NULL` → não.
- **Mutações verificadas:**
  - codificador usando a base do segmento 1 → falha em `mcdm_callbacks_test` linha 799;
  - política aceitando `RESOURCE` → falha em `allocation_abi_contract_test`;
  - guarda do BAR removida → portão reprova.
- **Portão** `audit_hidden_segment_is_derived_and_bounded`: cálculo a partir da topologia, log do valor lido, `NbSegment` condicional, segmento 3 nunca `CpuVisible`, faixa no codificador, guarda do BAR, política única, base PA lida e a recusa antiga proibida.
- **Builds e auditorias:** `build-contract` EXIT 0; imagem de laboratório reconstruída; as 4 auditorias passam.
- **Pacote 3.340.1.0:**
  - **Artefatos:** SYS `8E1B3319…`, INF `4F0D089F…`, CAT `4D661397…`, UMD `5CEC87A9…` (a da 193), RADV `5447A17D…`.
  - **Strings no SYS:** `[M2_SEGMENTO3] VramTotalMiB`, `[M2_BASE]`, `alem da janela de CPU`.
  - **Pré-check:** `prepare-corrida194-m2.ps1` EXIT 0.
- **Nota de registro:** o pacote 3.339.1.0 foi gerado antes de a base virar dinâmica e **nunca foi instalado**. Por isso o DriverVer subiu para 3.340.

## 5. Critério da Corrida 194 (escrito antes, Lei 10)

**Procedimento:**
1. Boot frio.
2. O Daniel escolhe a UMA na BIOS sem me dizer.
3. Rodar `tools\EXECUTAR_CORRIDA194_M2_600S.cmd`.
4. Abrir o menu várias vezes; digitar uma vez na Pesquisa.

**Leitura cega obrigatória:** antes de saber o valor escolhido, eu relato `VramTotalMiB` (sondagem `probe-registry-at-bind.txt`), a linha `[M2_SEGMENTO3]` e a linha `[M2_BASE]`. O Daniel confirma ou desmente.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `[M2_SEGMENTO3]` com o tamanho esperado para o `VramTotalMiB` lido; `QUERYSEGMENT4: 3 segments`; `[M2_SEGMENTO3_ALOC]` > 0; **zero** `vkAllocateMemory ... res=-2` no dwm.exe; zero laço de `CreateDevice`; desktop inteiro; zero TDR | A memória deixa de ser a parede; seguir para o custo por quadro e a trava residual |
| **B** | `QUERYSEGMENT4: 2 segments` ou `[M2_SEGMENTO3] status=` diferente de ok | O cálculo recusou; o status nomeia o motivo |
| **C** | Segmento 3 declarado, mas zero `[M2_SEGMENTO3_ALOC]` | A política não reconheceu as alocações da RADV (classe de token ou flags) |
| **D** | `GFXHUB_FAULT`, TDR ou dados errados logo depois das primeiras `[M2_SEGMENTO3_ALOC]` | A base do segmento 3 ou a guarda do topo estão erradas; voltar ao 3.338 |
| **D2** | `[M2_BASE] ... DIFERENTE` e o driver falha | A base mudou com a UMA e o resto da cadeia ainda supõe a antiga; investigar offline |
| **E** | Fora do mapa | Descrever antes de julgar |

## 6. Corrida 194 (01/10 09:23, 3.340.1.0) — leitura cega e segmento 3 recusado

- **Pasta:** `docs/evidence/2026-10-01_09-23-50-DESKTOP-HQAG6T4-lab-binding/`.
- **Relato do Daniel:** ao abrir o menu, tela preta.

**Leitura cega.** Relatei antes de saber o valor escolhido:
- `VramTotalMiB=512`, `CpuVisibleMiB=256`;
- `[M2_BASE] ... 0x450000000 IGUAL`;
- o Daniel informou depois que tinha escolhido **3 GB** na BIOS.

**Duas medições independentes contradizem os 3 GB:**
- o registrador da GPU (`RCC_CONFIG_MEMSIZE` conferido contra `FB_LOCATION`) dá 512 MiB;
- o Windows, por fora do nosso driver, viu **15,227 GB** de RAM no boot das 09:25, o mesmo valor de antes com 512 MB. Com 3 GB de UMA, veria ~12,8 GB.

**Leitura (H):** a BIOS não aplicou o tamanho, possivelmente por exigir um modo "UMA_SPECIFIED" junto. O Daniel vai repetir com desligamento completo.

**Segmento 3 recusado (P0):**
- a linha foi `[M2_SEGMENTO3] ... status=pequeno_demais_apos_reservas`;
- causa no mapa DX67: alocador 1 `[0x10000000,+0x10000000) papel=7`, que é o **marcador "opaco" do próprio driver** (`PLATFORM_RESERVED` sobre `[CpuVisible, Total)`), tratado como uso;
- é **desfecho B** do critério da §5;
- a tela preta no menu é compatível com a falta de memória da 193, mas não foi analisada a fundo, porque o M2 não esteve ativo.

**Correção 3.341.1.0 (M2b):**
- o cálculo ignora **só** esse marcador, reconhecido por papel e faixa exata;
- a reserva continua inteira nos alocadores internos, porque a **TMR do PSP** (`bc250_compute_bootstrap.c:228`) e o anel gráfico de nível < 3 (`bc250_active_miniport.c:464`) alocam top-down **sem** flag de CPU e só ficam abaixo do BAR por causa dela;
- encolher a reserva moveria a TMR do firmware para além do BAR, o que é rejeitado.

**Esperado na 195 com 512 MiB:** segmento 3 = `[256 MiB, 496 MiB)` = 240 MiB.

**Artefatos:** SYS `853A2CE9…`, INF `44A16FFE…`, CAT `A283AE60…`, UMD e RADV inalteradas. Portão estendido; auditorias passam; `prepare-corrida195-m2b.ps1` EXIT 0. O critério da §5 vale para a 195.

## 7. Corrida 195 (01/10 09:40, 3.341.1.0) — segmento 3 ativo, `Lock2` recusado, e bissecção na 196

**Leitura cega:**
- `VramTotalMiB=512`; o Windows viu 15,227 GB;
- o Daniel tinha salvo `UMA_SPECIFIED` + valor maior antes do boot, e mesmo assim a placa ficou em 512 MiB;
- o projeto da comunidade (`referencias/AMD-BC-250-Windows-Driver`) lê o mesmo `MC_VM_FB_LOCATION` e, como alternativa, um bloco `APCB` na CMOS (`UMA_SIZE` em 0x90+0x1A, ferramenta `fanoush/bc250_memcfg`);
- **H:** esse bloco sobrepõe o menu da BIOS.

**Resultado:**

| Fato (P0) | Fonte |
|---|---|
| `[M2_SEGMENTO3] ... [0x10000000, +0xF000000) = 240 MiB status=ok` | KMD |
| 11 alocações de 4 KB da RADV no segmento 3; a de `PA=0x460000000` (base + 256 MiB) foi preenchida pela GPU | KMD |
| Cada dwm.exe e Explorer: 4 KB (segmento 3) + 80 KB (segmento 1, `CpuVisible=1`) → `Lock2 failed: status=0xC000000D` → o processo morre | `-RADV` |
| `Lock2 failed` só aparece na 195 (em nenhuma corrida anterior) | busca no corpus |
| A sequência da RADV é idêntica à da 194 até o `Lock2` | diff |
| Zero Flips; reboot sujo às 09:41:38 (~40 s depois do bind) | KMD, Event Log |

- **Veredito:** fora do mapa (E). O M2 ativou, mas quebrou o mapeamento de CPU de uma alocação que **não** está no segmento 3. Mecanismo: H.

**Corrida 196: candidato M2-diag (3.342.1.0)**
- **Mudança:** só `BC250_M2_HIDDEN_SEGMENT_ALLOCATIONS_ENABLED = 0`. O segmento 3 continua **declarado**, mas nenhuma alocação pode usá-lo. Log `[M2_SEGMENTO3] alocacoes no segmento 3: DESLIGADAS`.
- **Artefatos:** SYS `0C47AA31…`; INF/CAT novos; UMD e RADV inalteradas. Auditorias passam; pré-check passa.

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **A** | `Lock2 failed` de novo, com zero `[M2_SEGMENTO3_ALOC]` | Basta declarar o segmento para quebrar o `Lock2`: o defeito está no descritor/QUERYSEGMENT4 |
| **B** | Zero `Lock2 failed`, e o DWM sobe como na 194 | É **colocar** alocações no segmento 3 que quebra o `Lock2` das outras; investigar o caminho de Lock do VidMm com essa colocação |
| **E** | Fora do mapa | Descrever |

## 8. Corrida 196 (01/10 10:06, 3.342.1.0) — desfecho A: só declarar o segmento 3 já quebra o `Lock2`

- **Pasta:** `docs/evidence/2026-10-01_10-06-46-DESKTOP-HQAG6T4-lab-binding/` (cauda do KMD recuperada em `bc250_display_log-tail-recovered.txt`).
- **Relato do Daniel:** "rodei, travou e reiniciei".

**Leitura cega:** `VramTotalMiB=512`, `CpuVisibleMiB=256`; o Windows viu 15,227 GB. A BIOS em `UMA_SPECIFIED` com 8 GB **não** chegou à placa de novo (H da §7: bloco `APCB` na CMOS sobrepõe o menu).

**Resultado (P0):**

| Fato | Fonte |
|---|---|
| `[M2_SEGMENTO3] ... [0x10000000, +0xF000000) = 240 MiB status=ok` e `alocacoes no segmento 3: DESLIGADAS` | KMD, linhas 306–307 |
| Zero `[M2_SEGMENTO3_ALOC]` | KMD |
| `Lock2 failed: status=0xC000000D` na alocação de 80 KB, em 3 de 3 processos com RADV (pids 1280, 13232, 6280) | `-RADV` |
| Zero TDR, zero `GFXHUB_FAULT`, 2 Flips; trava ~40 s depois e reset | KMD, relato |

**Veredito:** falha frente ao desktop; **desfecho A** do critério da §7. O defeito está no descritor do segmento 3, não na colocação.

**Correlação no corpus (P0):** `Lock2 failed` aparece em **2 de 2** corridas com o segmento 3 declarado (195, 196) e em **0 de 7** sem ele (16:18, 17:50, 22:44 de 30/09; 07:20, 07:41, 07:57 e a 194, onde o segmento saiu recusado).

**O que NÃO discrimina (medido, para não repetir):**
- o `TRANSFER` com `Dir=0` (`DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM`, d3dkmddi.h) antes do `Lock2`: a mesma alocação de 80 KB também é despejada para a RAM na 193, sem falha;
- o `Lock2` da RADV (`radv_wddm2_bo.c:1040`) não tem flags e é o mesmo nas duas;
- nenhuma DDI nossa recusou nada ligado a isso: o `0xC000000D` sai de dentro do VidMm.

**Causa provável (P1 pela documentação + P0 pela correlação; o mecanismo interno do VidMm é H):**
`referencias/windows-driver-docs/.../allocation-usage-tracking.md`, seção do `Lock2`:
- alocação com CPU num segmento **inteiramente** visível à CPU é sempre travável, sem restrição;
- com segmento local **sem** CPU, "all CPU-accessible allocations in non-CPU-accessible memory segments must contain an aperture segment in their supported segment set".

Sem o segmento 3 o VidMm trata a VRAM como inteiramente visível. Com ele declarado, passa a exigir a abertura no conjunto suportado. As alocações da RADV com CPU tinham só `SegSet=0x1`, daí o `INVALID_PARAMETER`.

## 9. Candidato M2.1 (3.343.1.0) e critério da Corrida 197 (escrito antes, Lei 10)

**Única variável nova:** alocação da RADV publicada com CPU passa a aceitar a abertura quando o segmento 3 está publicado.
- `Bc250AllocationLockNeedsApertureFallback` (`bc250_allocation_abi_contract.c`): verdadeiro só para token fora das classes da UMD **e** CPU visível. A classificação de classe virou fonte única (`Bc250AllocationTokenIsUmdClass`), usada também pelo predicado do segmento 3.
- `CreateAllocation`: `SupportedRead/WriteSegmentSet = 0x1 | 0x2`, preferido **1** depois **2**, `EvictionSegmentSet` segue 0 (lição do 0x10E). Log `[M2_LOCK_ABERTURA]` (16 primeiras + 1 em 256).
- **Fica de fora, de propósito:** as classes da UMD D3D11 (ela não chama Lock; usa shadow de CPU) e tudo que o KMD copia por endereço MC no segmento 1.
- **Inalterado:** `BC250_M2_HIDDEN_SEGMENT_ALLOCATIONS_ENABLED = 0` (nenhuma alocação no segmento 3), UMD e RADV.
- Corrigida também uma linha fundida no `CreateAllocation` (`}                if (...)`), resto de edição pelo PowerShell; agora proibida por portão.

**Provas offline:**
- `allocation_abi_contract_test`: 7 casos do predicado novo; `build-contract` EXIT 0;
- mutação (ignorar `CpuVisible`) reprova a suíte;
- `audit-bc250mcdm-safety.py` PASS com portões novos (fonte única de classe, abertura só com segmento publicado, conjunto `1|2` com preferido 1, proibição da linha fundida);
- `audit-bc250mcdm-lab-package.ps1` PASS; string `M2_LOCK_ABERTURA` conferida no SYS assinado.

**Artefatos:** SYS `157B8B05…` (fonte `E6B5C184…`), INF `EB2A58B1…`, CAT `85C31D8C…`, UMD `5CEC87A9…` (igual à 196), RADV `5447A17D…` (igual). Perfil `DISPLAY_RENDER_LAB`, WDDM 2.7, `GfxWaveProbe=1`. Scripts: `tools/prepare-corrida197-m21-lock-abertura.ps1` (EXIT 0) e `tools/EXECUTAR_CORRIDA197_M21_LOCK_600S.cmd`.

**Riscos declarados:**
- alocação da RADV com CPU pode morar na abertura (RAM do sistema); a GPU a alcança por PTE de sistema, caminho já usado pelo M1 e pelos DMA buffers;
- se alguma dessas alocações for origem de present, o resolvedor do KMD a recusa (fail-closed) e o quadro some — seria visível como `REJECT`/`RECUSADO` no Present.

**Critério da Corrida 197:**

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A — confirmada** | Linhas `[M2_LOCK_ABERTURA]` no KMD **e** zero `Lock2 failed` nos `-RADV`, **e** o DWM chega a Flips como na 193 | A regra do Lock era a causa; próximo candidato religa `BC250_M2_HIDDEN_SEGMENT_ALLOCATIONS_ENABLED` (uma variável) |
| **B — refutada** | `[M2_LOCK_ABERTURA]` presente **e** `Lock2 failed` continua | A abertura no conjunto não basta; investigar os flags do descritor do segmento 3 (`PreservedDuring*`, `LocalBudgetGroup`) um por vez |
| **C — não exercitado** | Zero `[M2_LOCK_ABERTURA]` | O ramo não rodou (segmento 3 não publicado ou classe errada); defeito nosso, ler `[M2_SEGMENTO3]` primeiro |
| **D — regressão de present** | Zero `Lock2 failed`, mas Present recusado por origem fora do segmento 1 | A abertura vazou para uma origem de present; restringir mais o predicado |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

Lei 10.3: sem os **três** sinais de A, não é A.

**NÃO VALIDADO EM HARDWARE.**

### 9.1 Adendo (coleta Linux, doc 549): a CMOS agora está em 2048 MB

A coleta Linux provou que quem manda na UMA é a CMOS APCB, não o menu da BIOS, e a deixou gravada em **2048 MB**. A tabela de valores está no [doc 549 §4](549_COLETA_LINUX_CMOS_APCB_CONTROLA_A_VRAM_E_AMDGPU_SEM_IP_DISCOVERY.md).

**Decisão do Daniel (01/10): a 197 roda com 2048 MB; a CMOS não volta para 512.** A corrida passa a testar **duas variáveis juntas**, o que fica declarado aqui:
1. a abertura no conjunto suportado (o `Lock2`);
2. a VRAM maior, com base física nova.

**Controle antes do bind (recomendado, só leitura):** rodar `tools\vram-cmos\bc250-vram.ps1 -Ler` ([doc 550](550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md)). Ele deve mostrar `ABL_SIGNATURE`, checksum `CONFERE` e `UMA_SIZE=2048`. Isso também fecha o teste V1 da ferramenta nova.

**ATUALIZAÇÃO 01/10 13:45: a CMOS agora está em 4096 MB.** O Daniel gravou 4096 com a ferramenta Windows ([doc 550](550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md)) e o firmware aplicou depois do boot frio (P0: `ABL_SIGNATURE`, `UMA_SIZE=4096`, RAM do Windows 14.739.120.128 → 12.591.636.480 bytes, exatamente 2 GiB a menos). A tabela de 2048 deixou de valer; **os valores da 197 são estes**:

| Linha do KMD | Esperado com 4096 MB |
|---|---|
| `[M2_SEGMENTO3] VramTotalMiB=... CpuVisibleMiB=...` | `4096` e `256` |
| `[M2_BASE] base PA da VRAM lida da placa ...` | `0x370000000` (topo `0x470000000` − 4 GiB, mesmo topo medido com 512 e 2048), `DIFERENTE -- usando a lida` |
| `[M2_SEGMENTO3] ... -> segmento 3 [...]` | `[0x10000000, +0xEF000000) = 3824 MiB status=ok` (já coberto pelo caso 4096 → 3824 do `segment_address_contract_test`) |
| `[McdmQAI] QUERYSEGMENT4: 3 segments filled ...` | `VRAM_SEM_CPU=0xEF000000 em 0x10000000` |
| RAM do Windows | 12.591.636.480 bytes (já medido pelo `bc250-vram.ps1`) |

Que a base física seja `0x370000000` é dedução do topo fixo medido em dois estados (H forte); a linha `[M2_BASE]` da 197 decide.

**Critério com as duas variáveis (complementa a tabela A–E acima, que continua valendo para o `Lock2`):**

| Desfecho | Assinatura | Implicação |
|---|---|---|
| **V-ok** | As cinco linhas acima com os valores esperados | A leitura dinâmica e a base nova funcionam no silício; o resultado do `Lock2` se lê pela tabela A–E |
| **V-base** | `[M2_BASE]` com outro valor, ou MMU global recusando, ou `GFXHUB_FAULT` logo no bind | A base nova quebrou algo antes do `Lock2`; o resultado A–E fica **inconclusivo** |
| **V-leitura** | `VramTotalMiB` diferente de 2048 | A ABL não aplicou, ou a leitura está errada; comparar com o `-Ler` da ferramenta |

**Se a 197 falhar de um jeito não atribuível**, a bissecção sem voltar para 512 é um candidato com 2048 e o segmento 3 **não declarado** (M2 desligado, como antes da 194). Ele isola a base nova do `Lock2`.

## 10. Corrida 197 (01/10 14:04, 3.343.1.0, CMOS em 4096 MB) — V-ok, `Lock2` sem falha no trecho coletado, e BugCheck `0x10E`/`0x34` ~2 min depois

- **Pasta:** `docs/evidence/2026-10-01_14-04-38-DESKTOP-HQAG6T4-lab-binding/`. A tela azul matou o watchdog; a cauda do KMD foi recuperada (`bc250_display_log-tail-recovered.txt`, 7.627.318 bytes, SHA-256 `D4A0E68A…`). Leitura da CMOS antes do bind em `docs/evidence/vram-cmos/2026-10-01_14-04-16-…-ler.txt` (`ABL_SIGNATURE`, `UMA_SIZE=4096`).
- **Relato do Daniel:** "deu tela azul".
- **Event Log (P0):** BugCheck **`0x10E` (`0x34`, `0xB`, `0x1`, `0x0`)** às ~14:06, `MEMORY.DMP` de 999.566.266 bytes às 14:06:58, boot seguinte às 14:06:54.

**Tabela V (P0, todos os valores escritos antes bateram):**

| Linha | Esperado | Medido |
|---|---|---|
| `[M2_BASE]` | `0x370000000`, `DIFERENTE` | `0x370000000 ... DIFERENTE -- usando a lida` |
| `[M2_SEGMENTO3] VramTotalMiB / CpuVisibleMiB` | 4096 / 256 | 4096 / 256 |
| segmento 3 | `[0x10000000, +0xEF000000) = 3824 MiB status=ok` | idêntico |
| RAM do Windows | 12.591.636.480 bytes | 12.591.636.480 bytes |

→ **V-ok.** A leitura dinâmica, a base física nova e o segmento de 3824 MiB funcionaram no silício. O bind chegou ao desktop: 725 Presents com `Flip=1`, 5 `BYTE_PROOF PASS`, zero quadro em branco, 1.946 IBs SDMA e 673 submissões gráficas, zero TDR, zero `GFXHUB_FAULT`, zero `IB_TRIPOD TIMEOUT`.

**Tabela A–E (`Lock2`):**
- `[M2_LOCK_ABERTURA]` presente (16 linhas amostradas; 182 alocações da RADV com `SegSet=0x3 Preferido=1`). ✅
- Zero `Lock2 failed` nos dois logs `-RADV` coletados (dwm.exe 1284 e Explorer 6220). ⚠️ **Só cobrem 14:04:47–14:04:50**: são os retratos iniciais, e a coleta FINAL não rodou por causa do BugCheck. O resto está em `C:\Windows\Temp`, a copiar com PowerShell elevado.
- Flips ✅.
- **Veredito do `Lock2`: A no trecho coletado; A completo pendente dos logs inteiros** (Lei 10.3).
- **Sinal forte a favor de A:** 8 `CreateDevice` e **zero** `DestroyDevice` no KMD, contra o laço de 242 recriações da 193 e as mortes do DWM ao nascer nas 195/196.

**Desfecho E (fora do mapa): BugCheck `0x10E`/`0x34`.**
- **Documentação oficial (P1,** `referencias/windows-driver-docs/.../bug-check-0x10e---video-memory-management-internal.md`**):** `0x34` = *"A heap allocation has received a state transition event incompatible with current state."* A leitura "incompatível com o conjunto suportado" do doc 535 era interpretação nossa (H), não está no texto da Microsoft.
- O log do KMD termina em atividade normal (TRANSFER `Dir=1` de 512 KB, submit gráfico aposentado). Nenhuma DDI nossa recusou ou falhou perto do fim: o `0x10E` sai de dentro do VidMm.
- **Correlação, não causa (H):** o mesmo `0x34` apareceu na 175 (Lock do Explorer sobre alocação em transição na abertura, durante um TRANSFER de 11 MB) e na 178 (`EvictionSegmentSet=2`). A 197 é a primeira corrida em que alocações **travadas pela RADV** (`Lock2`, mapeamento persistente) podem morar na abertura. Mas a combinação `SegSet=0x3` + CPU visível não é nova: o M1 já tinha 1.033 alocações assim nesta corrida e 6.049 na 193, sem `0x10E`.
- **Também mudou nesta corrida:** VRAM 4096 e base nova. As duas variáveis estavam declaradas (§9.1); por isso o BugCheck **não é atribuível** só pelo log.

**O que decide a atribuição (sem corrida nova):**
1. Pilha do `MEMORY.DMP` (`cdb` elevado): qual função do `dxgmms2` levantou o `0x34` e se há um `Lock`/`Lock2` na pilha.
2. Logs completos da UMD e da RADV de `C:\Windows\Temp` (cópia elevada): o último `Lock2`, um `Lock2 failed` tardio, e o que o processo fazia às ~14:06.
3. O que o Daniel viu na tela antes da tela azul.

**Próximo candidato:** só depois de a pilha e os logs nomearem o evento. Se não decidirem, a bissecção que separa as duas variáveis sem voltar a 512 é repetir o **mesmo** pacote 3.343.1.0 com a CMOS em 4096: se o `0x10E` voltar no mesmo tempo, é reprodutível e vale bisseccionar o M2.1; se não voltar, é intermitente e a trava/BugCheck entra na Fase 1 do `PLANO_MESTRE.md`.

**NÃO VALIDADO EM HARDWARE:** a correção do `Lock2` na corrida inteira; a estabilidade do M2.1.

### 10.1 Dump, logs completos e relato (01/10, depois da corrida)

**Relato do Daniel:** o desktop apareceu antes da tela azul.

**Pilha do `MEMORY.DMP`** (P0, `docs/evidence/dump-0x10e-2026-10-01.txt`, `FAILURE_BUCKET_ID: 0x10e_34_dxgmms2!VIDMM_RECYCLE_MULTIRANGE::Rotate`):

```
SearchApp.exe → nt!KiSystemServiceCopyEnd → dxgkrnl!DxgkLock → DXGDEVICE::Lock
→ dxgmms2!VidMmBeginCPUAccess → VIDMM_GLOBAL::BeginCPUAccess
→ VIDMM_GLOBAL::SetupAllocationForCPUAccess → VIDMM_GLOBAL::Rotate
→ VIDMM_RECYCLE_HEAP_MGR::Rotate → VIDMM_RECYCLE_MULTIRANGE::Rotate+0x77 → KeBugCheckEx
```

- O processo é a **Pesquisa** e a chamada é **`DxgkLock`**: o `pfnLockCb` da **nossa UMD D3D11**, não o `Lock2` da RADV.
- Um argumento da pilha é `0x3B000` = 241.664 bytes. É o tamanho da alocação STAGING (`Token=0xA11000031D040007`, `SegSet=0x1`) que o **último TRANSFER do log do KMD** trazia da RAM para a VRAM (`Dir=1`, FenceId 1946, executado e aposentado).
- Nenhum quadro nosso na pilha.

**Logs completos** (cópia elevada em `final-elevado/` e logs de AppContainer em `umd-logs-appcontainer/`):
- **`Lock2 failed` = 0 na corrida inteira**, no DWM e no Explorer até 14:06. → **Desfecho A do `Lock2` completo (P0).** O M2.1 fecha o defeito das 195/196.
- O log da Pesquisa (pid 7428) termina às 14:06:30.222 com `DXGI ReclaimResources: Resources=2 -> S_OK` e um `ResourceMap MapType=5` logo depois: o mapeamento que virou o `Lock` da pilha.
- `Upload LOCK falhou: hr=0x80070057` no menu (80), na Pesquisa (4) e no Explorer (2). **Não é novo:** a 193, sem segmento 3, teve 1.349 só no menu. Dívida antiga da UMD, registrada à parte.

**Leitura (H, com precedente):** um `Lock` da UMD chegou enquanto o VidMm ainda mexia na cópia em RAM ("recycle") da alocação que acabava de trazer para a VRAM. É o mesmo padrão da Corrida 175 (Lock do Explorer durante um TRANSFER de 11 MB). Entre a 179 e a 196 não houve `0x34`. A 197 mudou duas coisas: o M2.1/segmento 3 declarado e a VRAM de 4096. A pilha **não** distingue qual delas abriu essa corrida de estados.

**Dívida achada na leitura (Lei 4), não é a causa:** `Bc250DxgiOfferResources1` e `Bc250DxgiReclaimResources(1)` devolvem `S_OK` sem chamar `pfnOfferAllocationsCb`/`pfnReclaimAllocationsCb`. O kernel nem sabe da oferta, então isso não cria estado no VidMm. Mas é sucesso sem backend e entra na lista do `PLANO_MESTRE.md`.

## 11. Candidato M2 completo (3.344.1.0) e critério da Corrida 198 (escrito antes, Lei 10)

**Única variável:** `BC250_M2_HIDDEN_SEGMENT_ALLOCATIONS_ENABLED` 0 → **1**.
- Imagens da RADV sem CPU passam a `SegSet=0x5` (1|3), preferido 3. As da RADV com CPU seguem `0x3` (M2.1); as da UMD seguem no segmento 1.
- A CMOS continua em 4096 MB.
- É a meta do M2: tirar as imagens otimizadas do segmento 1 (~251 MB) e pô-las nos 3824 MiB do segmento 3.

**Por que agora, mesmo com o `0x34` sem atribuição:**
- O `Lock2` está fechado.
- O `0x34` da 197 é num `Lock` de STAGING da UMD, alocação que esta mudança **não** toca.
- Se a hipótese da corrida de estados sob pressão no segmento 1 estiver certa, tirar as imagens de lá **reduz** os TRANSFERs. Isso é previsão, não promessa, e entra no critério como sinal medido.

**Artefatos:**
- SYS `FAF7BD3E…`, INF `E31053EE…`, CAT `E76567D6…`; UMD `5CEC87A9…` e RADV `5447A17D…` inalteradas.
- Conferido no SYS assinado: a string `DESLIGADAS (M2-diag` sumiu (constante em 1) e `M2_SEGMENTO3_ALOC` está presente.
- `build-contract`, auditoria de segurança e auditoria do pacote: PASS.
- `tools/prepare-corrida198-m2-segmento3-ligado.ps1`: EXIT 0. Roteiro: `tools/EXECUTAR_CORRIDA198_M2_SEGMENTO3_600S.cmd`.

**Riscos declarados:**
- A GPU passa a ler e escrever texturas além do BAR (PA até `0x46F000000`). Isso só foi exercitado na 195, em 11 alocações de 4 KB.
- A CPU nunca acessa o segmento 3: o `Lock2` só vale para alocações com CPU, que seguem fora dele.

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A — M2 funciona** | `[M2_SEGMENTO3_ALOC]` com centenas de alocações **e** zero `GFXHUB_FAULT` **e** zero `vkAllocateMemory -2` / `OUT_OF_DEVICE_MEMORY` nos `-RADV` **e** DWM sem laço de recriação | A VRAM inteira está em uso; Fase 2 do `PLANO_MESTRE` avança (Frente B final, tabela de objetos) |
| **B — tradução além do BAR quebrada** | `GFXHUB_FAULT` com endereço numa alocação do segmento 3, ou texturas pretas/corrompidas só nas imagens da RADV | Conferir a PTE do segmento 3 (base `0x370000000` + `0x10000000` + offset) contra a caminhada da tabela |
| **C — não exercitado** | Zero `[M2_SEGMENTO3_ALOC]` | A política não pegou as imagens (classe/flags); defeito nosso, ler os `DX93_ALLOC_MAP` |
| **D — `0x34` de novo** | BugCheck `0x10E`/`0x34` com a mesma pilha (`DxgkLock` → `RECYCLE_MULTIRANGE::Rotate`) | Reprodutível com a VRAM de 4096: o próximo passo é a Fase 1 do plano para essa classe (Lock sobre alocação em transição), não o M2 |
| **E — fora do mapa** | Qualquer outra coisa | Descrever antes de julgar |

**Sinal medido em qualquer desfecho:** taxa de `TRANSFER Dir=0` (despejo da VRAM para a RAM) contra a 197 (582 em ~102 s).

Lei 10.3: sem os **quatro** sinais de A, não é A.

**NÃO VALIDADO EM HARDWARE.**

## 12. Corrida 198 e encerramento do M2 como frente (01/10)

A 198 rodou o M2 completo por ~212 s: 2.086 imagens da RADV no segmento 3, zero fault, zero TDR, DWM estável. Terminou na trava dura de sempre (tela preta, sem BugCheck). Detalhe no [doc 551 §1](551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md).

**Decisão do Daniel no mesmo dia:** trabalhar com UMA de **256 MB** e fazer tudo funcionar com o mínimo, sem VRAM grande como muleta ([doc 551 §2](551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)).

**Consequência:**
- O M2 e o M2.1 ficam **dormentes**. O código continua no driver, porque é dinâmico: com 256 MB a VRAM inteira cabe no BAR e o segmento 3 não é declarado.
- A memória para a carga real passa a ser a **M1b** (memória do sistema pelo segmento 2, como o GTT do Linux).
- O que este documento provou continua valendo:
  - leitura dinâmica da VRAM e da base física (P0, 194–198);
  - a regra do `Lock2` com VRAM sem CPU (P1 + P0 na 197);
  - o segmento 3 funcionando com 2.086 alocações (P0 parcial na 198, sem prova visual).