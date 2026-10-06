# 582 — Mineração das travas com 256 MB: GPU ociosa, máquina morre entre duas observações, e o que o Linux faz diferente

**Data:** 04/10/2026 · **Tipo:** mineração offline, zero corridas · **Pedido:** Daniel, depois da Corrida 212 ([doc 581](581_CORRIDA212_SEM_FALTA_DE_MEMORIA_SEM_DEFEITO_DE_HOST_TRAVA_DURA_AOS_259S.md))
**Corpus:** `docs/evidence/2026-10-0[1-4]_*-lab-binding/`, rastro durável `paging-trace-this-bind*.bin` e log `display-log-this-bind*.txt`.
**Script:** `mine_trace.py` (scratchpad da sessão). Ele corta o rastro no primeiro recuo do `writer_tick`, que é o reboot de recuperação. Sem esse corte, o decodificador resume os registros do boot seguinte: o JSON da 212 mostra `writer_tick 12..16` com `measured=0`, e isso é o boot de recuperação, não a trava.

## 1. Medido (P0)

### 1.1 O último tique do rastro, antes do reboot, em cada trava dura

| Corrida | Hora | GFX (HB, último DEPOIS) | SDMA0 (BSQ1, último DEPOIS) | Leitura que não voltou? | Migalha presa por mais de 1 tique? |
|---|---|---|---|---|---|
| 199 | 10-01 21:44 | ocioso, medido | (sem BSQ1 nesse binário) | não | não |
| 200 | 10-01 22:14 | ocioso | ocioso, anel vazio | não | não |
| 202 | 10-02 08:43 | ocioso | ocioso, anel vazio | não | não |
| 203 | 10-02 09:50 | ocioso | ocioso, anel vazio | não | não |
| 204 | 10-02 11:44 | ocioso | ocioso, anel vazio | não | não |
| 207 | 10-02 21:55 | ocioso | ocioso, anel vazio | não | não |
| 208 | 10-02 22:29 | ocioso | ocioso, anel vazio | não | não |
| 210 | 10-03 22:23 | ocioso | **ocupado**, dentro de IB (`STATUS1=0x28380`, `CTX_STATUS=9`) | não | não |
| 212 | 10-04 16:07 | ocioso | ocioso, anel vazio | não | não |

- **No último tique, todas as leituras de registrador voltaram** (`measured=1`): não há `ANTES` sem `DEPOIS` em nenhuma das nove.
- **O GFX estava ocioso em 9 de 9.** O SDMA0 estava ocioso em 7 de 8 medidas; a exceção é a 210.
- Nenhuma migalha de MMIO ficou em voo por mais de um tique. As que aparecem no último registro (`PAGING_DDI`, `SUBMIT_DDI`) têm sequência de 1, ou seja, são atividade normal.
- **O registro do tique seguinte nunca foi gravado.** A thread do rastro roda em PASSIVE a cada 100 ms, com `ZwFlushBuffersFile`.
- **Conclusão P0:** a máquina inteira parou em até um tique (cerca de 100 ms mais a gravação) depois de uma observação em que a GPU respondia e estava parada. A morte acontece **entre** duas observações, não dentro de uma leitura nossa.

### 1.2 O fim do log não é o fim da execução

- Em `Bc250DisplayLog` (`bc250_display_ddi.c`), a linha só vai direto para o arquivo em PASSIVE, com APCs habilitadas, sem paginação ativa e com o arquivo livre.
- Em qualquer outro caso, a linha vai para um anel em RAM. Esse anel só é drenado quando uma thread PASSIVE escreve a linha seguinte.
- **Consequência (P0, leitura de código):** a última linha do arquivo é a última **drenagem**, não o último evento. Linhas de DISPATCH e de paginação que estavam no anel na hora da trava se perderam.
- **A diferença entre o último `qpc` do log e o último tique do rastro** fica entre 0,15 e 0,25 s nas nove travas. Na 211, que terminou em tela azul e não em trava, ela foi de 0,16 s, a mesma ordem.
  - Portanto não dá para localizar a trava com precisão melhor que cerca de um tique.
  - A "assinatura" das minerações anteriores (zeramento de alocação nova, CPU iniciando o próximo envio do Node 0, docs 560, 563 e 581) é o **contexto de atividade alta** em volta da trava. **Não é** a operação que travou.
  - Isso rebaixa essas assinaturas de "último ato" para "vizinhança". As conclusões de eliminação continuam válidas, porque cada uma foi tirada pelo desfecho, não pela linha final.

### 1.3 Onde fica o que a CPU consulta o tempo todo

- **Anel e Work do SDMA0** (fences dos slots e writeback do RPTR) estão na **VRAM**, em `[0xFF00000, +0x2000)`, mapeados UC e acessados pela CPU pela BAR. Evidência no log da 212: `[T31_UC_MAP] SDMA_PRESENT_RING_WORK` e `RingGpu=0xF40FF00000 WorkGpu=0xF40FF01000`.
  - Essa faixa fica **fora** do segmento 1 `[0xA8C000, 0xFA00000)`. Por isso a hipótese "zeramento sobrescreve o anel" está **refutada** (P0).
- **A aposentadoria é por polling:** um timer de 1 ms para o GFX e um timer de alta resolução de 1 ms para o SDMA (`bc250_active_miniport.c:12160` e `:12193`). Cada passada lê fence na VRAM pela BAR e registradores pela MMIO, enquanto houver trabalho em voo.
- **A abertura AGP está desligada** (`bc250_gcvm_global_mmu.c:98-100`: BOT `0xFFFFFF` maior que TOP 0). Então o kernel não tem hoje como pôr um anel em memória de sistema sem GART na VMID 0.

## 2. O que o Linux faz (P1)

| Item | Linux (`amdgpu`) | Nós |
|---|---|---|
| Anel do kernel (SDMA, GFX) | `amdgpu_ring_init`: `amdgpu_bo_create_kernel(..., AMDGPU_GEM_DOMAIN_GTT, &ring->ring_obj, ...)` (upstream `amdgpu_ring.c`, consultado hoje) | VRAM pela BAR |
| Writeback (RPTR, WPTR, fences) | `amdgpu_wb_init`: `AMDGPU_GEM_DOMAIN_GTT` (upstream `amdgpu_wb.c`, consultado hoje); `sdma_v5_0.c:1028/1092` usa `adev->wb.gpu_addr` | VRAM pela BAR |
| Conclusão | interrupção (`amdgpu_ring_init(..., &adev->sdma.trap_irq, ...)`, `sdma_v5_0.c:1422`; 37.701 interrupções na coleta L-B, doc 552) | polling de 1 ms |
| Clock e power gating no GC 10.1.3 | `cg_flags = 0; pg_flags = 0` (`nv.c:828-831`): nada é alterado, fica o estado do POST | já alinhado pela CG.1 |
| SMU | só tabela de métricas; o firmware gerencia (doc 556) | igual, já eliminada |
| SDMA1 | usado | desligado; eliminado como causa suficiente (doc 555) |

**Ressalva:** os dois arquivos upstream não estão em `referencias/linux_upstream_2026-09-29/`. A leitura foi do `master` do GitHub hoje. É P1, mas sem cópia local: deve ser arquivada antes de virar base de código.

## 3. O que isso muda (H, declarado)

- Das diferenças do doc 552, **sobra uma inteira e uma nova**, e as duas têm a mesma natureza:
  - **D1:** conclusão por interrupção, contra o nosso polling de 1 ms.
  - **D2 (nova):** anel e fences em memória de sistema, contra os nossos na VRAM pela BAR.
- **Hipótese H-BAR:** tráfego contínuo da CPU pela BAR (leituras UC de fence, escritas de anel, MMIO) durante rajadas de DMA do SDMA trava a malha. Na BC-250, CPU e GPU dividem a mesma GDDR6, então uma malha travada congela tudo, sem tela azul e sem KD (memória `bc250-kd-nao-para-a-trava-dura`).
  - Casa com o §1.1: a GPU está parada e a máquina morre fora de uma leitura instrumentada.
  - Casa com a vizinhança: as travas aparecem em rajada de paginação.
  - **Não está provada.** A 4b tirou só a leitura do buffer linear (uma fonte de tráfego pela BAR) e não tirou a trava. Isso enfraquece a H-BAR, mas não a refuta: o polling de fence e anel continuou.
- **O que a H-BAR não explica:** por que o Linux, com a mesma janela de 256 MB e aplicativos escrevendo em VRAM visível pela BAR, não travou sob 149 despejos. A diferença que sobra é justamente que o kernel do Linux **não** faz polling pela BAR.

## 4. Próximo passo proposto (decisão do Daniel)

Uma variável por corrida, na ordem do menor risco para o maior.

1. **D2: anel e Work do SDMA0 em memória de sistema**, mantendo o polling.
   - Memória contígua não paginada e não cacheada, ligada à GPU pela abertura AGP (`AGP_BOT`/`AGP_TOP`/`AGP_BASE`, registradores do GCMC já catalogados e escritos hoje com a abertura desligada). ~~É o mesmo mecanismo que o Linux usa para buffers de kernel em GTT.~~ **Corrigido no doc 583:** no BC-250 o Linux **não** liga a AGP; ele usa uma GART de 512 MB na VMID 0 (P0, coleta L-B: `GART: 512M 0x0 - 0x1FFFFFFF`, sem linha `AGP:`). A AGP é escolha nossa, por ser menor.
   - O polling passa a ler a RAM da CPU, não a BAR. Muda um só fator: **onde** a CPU consulta.
   - Depois disso o GFX entra numa segunda etapa, se a primeira não fechar.
   - **Risco:** endereço de MC errado deixa o SDMA lendo lixo, o que dá timeout e TDR, não corrupção silenciosa. A Lei 5 se aplica: ler de volta a abertura e conferir com NOP e fence sobre poison antes do primeiro IB.
2. **D1: interrupção do SDMA0** (anel IH e MSI). É maior: anel IH, habilitação e o handler já existente (`Bc250McdmInterruptRoutine` sem backend). Fica para depois da D2, ou para o caso de a D2 sair sem trava e for preciso confirmar.
3. **Instrumentação passiva junto com a D2:** um contador por tique de acessos da CPU pela BAR (leituras UC da VRAM, escritas de anel, leituras MMIO) num registro novo do rastro. Mesmo que a D2 trave, ele mostra se houve pico de tráfego no último segundo.

**Critério, que será escrito antes (Lei 10), quando a D2 estiver pronta:** desfecho A = 600 s sem trava com a D2 ativa; B = trava com a D2 ativa (a H-BAR do anel cai e sobra a D1); C = a D2 não ligou; D = defeito de host; E = fora do mapa.

## 5. Pendências

- **Arquivar** `amdgpu_ring.c` e `amdgpu_wb.c` em `referencias/` para a P1 ficar local.
- **NÃO VALIDADO EM HARDWARE:** a causa da trava, a H-BAR, e 600 s sem trava com 256 MB.
- Recuperação e boot frio continuam obrigatórios antes de qualquer bind.
