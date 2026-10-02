# Prompt de auditoria externa — driver WDDM BC-250

> Para enviar a outras IAs junto com o repositório (branch `p7-desktop-estabilidade`). **As mudanças de 27–28/09 estão na árvore de trabalho, não commitadas.** Audite a árvore como ela está no disco.

---

Você vai auditar um driver de vídeo WDDM escrito do zero para a **AMD BC-250**, uma APU de servidor derivada do PS5 com Zen 2 e GPU RDNA1 "Cyan Skillfish".
- PCI `1002:13FE`, GC 10.1.3, SDMA 5.0, DCN 2.0.1.
- 16 GB de GDDR6 **unificada**: a mesma memória serve CPU e GPU.
- Sistema: Windows 10 22H2 (19045).

**Composição:**
- KMD miniport WDDM 2.7 (`driver/wdk/`);
- UMD D3D11 traduzida para Vulkan sobre RADV/Mesa (`driver/umd/`);
- contratos testáveis offline (`driver/core/`, `driver/tests/`);
- auditorias em Python (`tools/audit-*.py`).

**O display é herdado do GOP da UEFI:** o driver não programa o DCN, só lê um contador de quadros. A meta final é rodar jogos na placa, sem WARP nem fallback de software.

**Leia antes:**
1. `CLAUDE.md`, com as leis do projeto: sinceridade, tripé de validação no silício, zero fallback de software, endereços só com proveniência, vocabulário de confiança P0/P1/P2/H/Q.
2. `BASE_DE_AVANCO.md`, cujas primeiras entradas são as mais recentes.
3. `docs/499` a `docs/507`: corridas 152–157 e a pesquisa.

## O problema principal: trava dura intermitente
- **Sintoma:** a máquina congela inteira em 1–3 minutos de uso: tela preta, teclado morto, `Ctrl+Scroll Lock` sem efeito, nenhum BugCheck. Na Corrida 157 ficou **48 minutos** travada sem que o DPC watchdog agisse (`BugcheckCode=0`), e sem evento WHEA.
- **Estado no instante da trava:**
  - GPU ociosa no último batimento (a cada 100 ms);
  - todas as fences em dia;
  - nenhum acesso MMIO nosso pendente nas migalhas;
  - nenhum timeout do SDMA;
  - nenhum TDR.
- **Dado mais forte:** uma página de migalhas na VRAM, lida no boot seguinte (reinício pelo **botão reset**, sem corte de energia), volta com **bits trocados em quantidade proporcional ao tempo travado**: 44 s → 10 slots, ~72 s → 23–27, 81 s → cabeçalho corrompido, 48 min → página perdida. Leitura: **o controlador de memória do SoC para de fazer refresh durante a trava**. Com a GDDR6 unificada, a CPU também para.
- **Correlação:** a trava fica mais frequente com mais paginação da GPU:
  - 0/2 corridas com depurador de kernel (paginação muito lenta);
  - 0/2 com a M1 (menos tráfego de VRAM);
  - 5/6 depois da FPS.1 (paginação 5× mais rápida).
- **Já descartado (ver docs):**
  - troca do VMID8 com IB em voo (corrigida na DX.95);
  - espera de conclusão no tique de 15,6 ms (FPS.3b);
  - esgotamento do espaço de GPUVA (DX.94);
  - mensagens ao SMU (o driver não manda nenhuma em corrida).
- **Plataforma:** a BIOS P3.00 **não tem** opções de C-states nem de DF C-states.

## O que eu quero da auditoria

Classifique cada achado como **P0** (provado por evidência no repositório), **P1** (derivado de fonte upstream, que deve ser citada), **P2** ou **H** (hipótese). Não chame hipótese de causa.

### 1. O que no nosso código pode travar o fabric de dados ou o controlador de memória de uma APU AMD
Concentre-se em:
- **Codificação de PTE e endereços físicos entregues à GPU:**
  - `Bc250McdmEncodeVidMmPteWithLevel`;
  - `SetRootPageTable`;
  - segmento 1 (VRAM, base `0xF400000000` + `LocalSegmentBaseOffset`);
  - segmento 2 (memória de sistema, aberta pela M1);
  - bits de snoop, system, valid e executável;
  - a página dummy.
- **Configuração da GCVM e do GFXHUB:**
  - `CONTEXT_CNTL`;
  - redirecionamento de fault para a página dummy (`*_ENABLE_DEFAULT`);
  - invalidação de TLB;
  - flush de HDP (`docs/03_CATALOGO_DE_REGISTRADORES.md`, `inc/linux_ref/`).
- **Pacotes SDMA:**
  - `driver/core/bc250_sdma_packet_builder.*`;
  - fills de até 11 MB e cópias de paginação;
  - alinhamento e limites;
  - GCR e cache.
- **Acessos da CPU à VRAM pela BAR de 256 MB:**
  - mapeamentos `MmMapIoSpace`, write-combining e uncached;
  - leituras de "byte proof";
  - alocações `CpuVisible` mapeadas pela UMD/RADV.
- Qualquer acesso a endereço **fora** do que o hardware decodifica.

### 2. Concorrência e ciclo de vida (kernel)
- IRQL correto em cada caminho.
- Spinlocks e push locks: ordem, e se algum é tomado em DISPATCH.
- DPCs e timers: `Bc250ActiveSdmaRetirementDpcRoutine`, o timer de alta resolução novo (FPS.3b), `RetirementTimer`.
- `StopDevice` e `RemoveDevice` (há um caminho em que o `StopDevice` devolve BUSY antes de zerar `IsActive`).
- `DxgkCbSynchronizeExecution`, `DxgkCbNotifyInterrupt` e `DxgkCbNotifyDpc`.

### 3. Correção do espaço de GPUVA
- `driver/core/bc250_gpuva_space_contract.c`, `_snapshot_`, `_journal_`, depois da FPS.1 (reconstrução linear, `__movsq`) e da DX.94 (teto 4.096).
- Quando o journal é abortado com as PTEs de hardware já escritas, a cópia de software diverge do hardware. Isso é aceitável? Onde isso pode resolver um endereço errado e mandar a GPU a lugar errado?

### 4. Lei 4 (zero fallback e zero sucesso falso)
Procure DDIs que devolvem sucesso sem backend, cópias por CPU disfarçadas de GPU, e fences incrementadas pela CPU. Em particular: a falha do FLUSH_TLB é convertida em `STATUS_SUCCESS` antes de voltar ao VidMm (`bc250_mcdm_callbacks.c`, caso `DXGK_OPERATION_FLUSH_TLB`). Julgue se isso é defensável ou esconde um problema.

### 5. Lacunas de teste e de portão
Portões (`tools/audit-bc250mcdm-safety.py`) que verificam texto em vez de comportamento; testes que congelam o comportamento errado (já aconteceu cinco vezes neste projeto).

### 6. Ideias para diagnosticar a trava
Queremos ideias que **não** exijam escrever no SMU nem no DCN (há relatos de que certas mensagens ao SMU travam o SMU até o reboot):
- o que mais se pode medir de forma persistente;
- que experimentos A/B baratos separariam "transição de energia da plataforma" de "padrão de tráfego da GPU";
- se há algo conhecido sobre travas do controlador de memória em Renoir/Van Gogh/Cyan Skillfish sob carga de GPU (cite a fonte).

## Formato da resposta
Uma lista de achados, do mais grave ao menos grave, cada um com:
- **arquivo:linha**;
- o que está errado ou é suspeito;
- a evidência;
- a confiança (P0/P1/P2/H);
- o impacto possível na trava ou no produto;
- **o teste que confirmaria ou refutaria** (offline, ou uma corrida com critério escrito antes).

Depois, separado, as ideias do item 6, ordenadas por custo e informação.

**Não proponha:** WARP, renderização por CPU, "desligar a aceleração", nem trocar a meta por computação ou LLM. Demorar é aceito; fugir do escopo não.
