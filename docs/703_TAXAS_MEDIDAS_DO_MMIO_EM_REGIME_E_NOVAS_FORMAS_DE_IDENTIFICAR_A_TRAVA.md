# 703 — Taxas medidas do MMIO em regime e novas formas de identificar a trava

**Data:** 12/10/2026. **Natureza:** análise offline, sem corrida.

**Fontes:**
- código: inventário completo dos acessos da CPU a registradores depois do StartDevice;
- evidência das corridas 263, 263b, 264, 265 e 266: logs recuperados e rastro (BEV1, BFT1, BMP1, BPS1).

**Confiança (Lei 8):** contagens de log e de rastro são P0; caminhos e períodos lidos no código são P1; taxas por multiplicação são H.

---

## 1. Inventário: o que a CPU ainda toca em regime (P1)

| # | Caminho | Registradores | Disparo |
|---|---|---|---|
| A | Blt síncrono do flip/Present (`Bc250SdmaPresentBltCore`, `sdma_present.c:997`) | lê `SDMA0_RB_RPTR` em laço com `KeStallExecutionProcessor(5)` | cada cópia de 11 MB |
| B | Poll do GFX (`Bc250ActiveGfxEnginePoll`, `active:2762`) | lê `CP_RB0_RPTR` e `CP_ME_CNTL` (+`FAULT_STATUS` se aposentou) | cada `RetireAndNotify`, ocioso ou não |
| C | Poll do MEC | seletor GRBM + `PQ_RPTR` só com fence pendente (MEC.2) | idem; medido 0 |
| D | Aposentadoria do SDMA0 (`sdma_present.c:3326`) | lê `SDMA0_RB_RPTR` 1 + 1 por IB | TRAP + timer de 10 ms com IB em voo |
| E | ISR do IH | escreve `IH_RB_RPTR` (OSSSYS) | cada MSI |
| F | Submit do SDMA0 | lê `SDMA0_RB_RPTR` | cada IB/pedaço |
| G | Rota do SDMA1 (P4.2) | lê `SDMA1_RB_RPTR`; **escreve WPTR por MMIO** (4 escritas, sem doorbell) | cada pedaço com PTE |
| H | Submit do GFX | lê `CP_RB0_RPTR`, `CP_ME_CNTL` (+`FAULT_STATUS` na troca de VMID) | cada submit |
| I | VMID pela CPU (`Bc250GcvmInstallVmid`) | ~18–20 acessos: PT_BASE/START/END, CNTL, engine 17 (REQ/ACK) | troca de contexto na VMID 7, contexto novo no pool |
| J | Amostra de plataforma | 5 leituras do SDMA1 + par SMN (Tctl) | ~1,2 s |
| K | DPC do VSYNC | lê `OTG0_FRAME_COUNT` (**DCN**) | tique do relógio |

**Sem MMIO em regime** (P1):
- HDP (4C);
- FLUSH_TLB (4c.4);
- WPTR do GFX e do SDMA0 (doorbell);
- batimentos BGH1/BSQ1/BCG1 (silêncio da P1);
- `GetScanLine`, `QueryCurrentFence`, BuildPagingBuffer;
- MMHUB, MP1/SMU, PCI config.

**Correção do timer (P1):** o `RetirementTimer` é um KTIMER de período 1 ms. Ele vence no tique do relógio do Windows: 15,6 ms por padrão (64/s), ou 1 ms se algum processo pediu `timeBeginPeriod(1)`. Os ~85 polls/s do MEC medidos na 266 são 64 tiques mais as entradas por submit e EOP do GFX. A taxa pode subir até ~1.000/s conforme o que estiver aberto na máquina. Isso também é uma fonte de variação entre corridas.

## 2. Taxas medidas por corrida

Janela da corrida = do bind ao último registro do rastro.

| Corrida | Janela | Cópias primárias (A) | Blts totais (A) | IBs do SDMA0 (D, F) | ISR do IH (E) | Submits GFX (H) | VSYNC (K) | Trocas de VMID 7 (I) |
|---|---|---|---|---|---|---|---|---|
| 263 | ~150 s | 1.768 | ≥5.280 (≥35/s) | 8.157 (54/s) | — | — | — | 90 |
| 263b | ~3,5 s | 18 | **~1** | 1.065 | — | 5 (BEV1) | — | 6 |
| 264 | ~10 s | 152 | ~420 (44/s) | 1.164 (~120/s) | — | 164 (BEV1) | — | 12 |
| 265 | ~264 s | 1.762 (6,7/s) | ≥5.280 (≥20/s) | 9.307 (35/s) | 11.177 (~43/s) | 1.953 (7,4/s) | ~14.580 (55/s) | 34 (0,13/s) |
| 266 | ~58,5 s | 780 (13/s) | ~2.340 (40/s) | 3.657 (62/s) | 4.300 (~86/s) | 950 (16/s) | ~3.180 (54/s) | 29 (0,5/s) |

**Origem dos números:**
- Blts: o contador `BLT_ENGINE_2LEGS #N`, que sai a cada 60.
- VSYNC: o número `#N` da última linha `[Vsync #]`.
- ISR: `[P3_IH] regime`, relatório a cada 10 s; o último pode estar até 10 s antes do fim.

**Ressalvas:**
- O 263 e o 265 terminam os dois em `#5280`, com exatamente 89 linhas `BLT_ENGINE_2LEGS` cada: é um **teto do log**, não a contagem. Nessas duas corridas o total de blts é **pelo menos** 5.280, e as taxas de 35/s e 20/s são mínimos.
- **Lacuna:** as migalhas de MMIO (BCR1 `LastSequence`) estão em 0 em todas as corridas desde o silêncio da P1. Não há contagem direta do total de acessos, nem das voltas do laço do blt (`g_Bc250X1GatedPolls` só sai no log em 4 linhas).

**Taxa de leitura por caminho (H, a partir do P0 acima):**

| Caminho | Taxa estimada | Base |
|---|---|---|
| A — laço do blt | **~5.000–11.000 leituras/s** | ~270 voltas por blt de 11 MB (blt de ~1,37 ms, medida de 12/09) |
| B — poll do GFX | ~170 leituras/s | 85 chamadas/s × 2 |
| D+F — SDMA0 | ~100–200 leituras/s | IBs/s × 2–3 |
| E — IH | 40–90 escritas/s | ISR/s |
| K — DCN | ~55 leituras/s | VSYNC/s |
| I — VMID pela CPU | ~2–10 acessos/s | trocas × 20 |
| J — plataforma | ~6 acessos/s | 7 por 1,2 s |

## 3. O que as taxas dizem

1. **O acesso mais frequente (laço do blt) não é necessário para a trava (P0).** A 263b travou em ~3,5 s com **um** blt. As taxas de blt da 263 e da 265 são só mínimos (teto do log), então a comparação de dose entre corridas longas não é possível com o log atual; a 263b basta para dizer que o laço não é necessário.
2. **A troca de VMID pela CPU é rara:** 0,1 a 2 por segundo. Na 265 e na 263 a última foi segundos antes da trava (docs 693/696). Não é candidata por frequência.
3. **O que se repete no fim das cinco travas é trabalho de paginação do SDMA0 (P0 pelo BEV1).** Em todas, os últimos eventos gravados são pares SUBMIT/RETIRE do SDMA0 que terminaram 0 a 10 ms antes do fim do rastro. Em quatro das cinco há FILL ou TRANSFER de paginação nos ~25 ms finais:
   - 263b: FILL de 64 KB a −5,5 ms;
   - 264: TRANSFER a −24,7 ms;
   - 265: FILL a −8,2 ms;
   - 266: TRANSFER de 8 MB a −6,7 ms.
   
   A GPU fica ociosa e a máquina morre antes da drenagem seguinte. Isso repete o achado antigo (docs 560 e 582: "FILL de paginação em voo") com a instrumentação atual.
4. **O anel BEV1 perde ~60% dos eventos.** Grava 16 por tique de um anel de 48: na 266 foram 3.861 de 9.755. As contagens por tipo são amostra. A sequência final é completa, porque o último tique drena tudo o que coube.

## 4. Achado de código: a PTE de memória de sistema não confere o endereço físico (P1)

Em `Bc250McdmEncodeVidMmPteWithLevelEx` (`bc250_mcdm_callbacks.c`, ramo `Source->Segment == 0u || == 2u`), a folha recebe `physicalAddress = PageAddress << 12` sem nenhuma conferência. O segmento 1 e o segmento 3 têm checagem de faixa; os segmentos 0 e 2 não.

O segmento 2 é declarado como **abertura** (`Flags.Aperture = 1`, base lógica `0x100000000`), e `MAP_APERTURE_SEGMENT` é tratado como nada a fazer. Se, para PTE de abertura, o VidMm entregasse deslocamento na abertura em vez de endereço físico de RAM, a GPU leria e escreveria endereços físicos baixos. Isso inclui a faixa legada e, conforme o deslocamento, o buraco de MMIO abaixo de 4 GB.

Que a renderização pelo segmento 2 funcionou (M1b.1, corridas 211–212) indica que o endereço é físico (P2), mas nunca foi medido.

**Por que importa:** um acesso da GPU (SDMA ou GFX) a endereço físico que não é RAM é uma forma conhecida de travar o data fabric de uma APU sem deixar erro de MCA. Isso combina com a assinatura das travas (H, não provado):
- GPU ociosa;
- CPUs paradas;
- a memória sem refresh;
- zero WHEA.

## 5. Novas formas de identificar a trava, em ordem de custo e retorno

### 5.1 Validador de endereço físico (só software, sem registrador novo)

**O que faz:**
1. No StartDevice, guardar as faixas de RAM do Windows (`MmGetPhysicalMemoryRanges`, já usado pela P1b) e as faixas proibidas conhecidas: carve-out de VRAM, TMR e a página dummy.
2. Em toda folha de segmento 0/2 e em todo endereço físico que uma operação de paginação entregar ao SDMA, conferir se o endereço cai inteiro numa faixa de RAM. Em toda folha de segmento 1/3, conferir se cai no carve-out.
3. Contar no rastro (registro novo, como o BMP1): PTEs de sistema validadas, fora de RAM e dentro do buraco de MMIO. Registrar as 8 primeiras violações com segmento, PFN e VA.
4. Registrar também as primeiras PTEs de segmento 2 com o PFN cru, para fechar de vez se o VidMm entrega endereço físico ou deslocamento.

**Comportamento:** só mede. Nada é recusado nesta etapa, porque recusar mudaria o caminho e misturaria variáveis.

**Desfechos:**
- **Violação aparece:** é causa candidata forte, com endereço.
- **Zero violações em milhares de PTEs:** a classe "GPU em endereço inválido por PTE" sai da lista.

### 5.2 Bisseção pela carga, com o mesmo driver

Hoje toda corrida mistura o desktop, o menu e processos novos.

**Variantes da T3**, com o mesmo pacote e sem tocar no driver:
- (a) processo novo que só cria o device, sem alocar;
- (b) **um só** processo que aloca e libera texturas em laço (paginação sem processo novo);
- (c) um processo que força despejo: aloca acima do orçamento local e lê de volta;
- (d) desktop parado. Já medido: 600 s sem trava na 237.

Se (b) ou (c) travarem e (a) não, o gatilho é paginação/PTE e não o ciclo de vida do processo. Se só (a) travar, é o ciclo de vida (VMID, contexto). É a forma mais barata de localizar o gatilho por tipo de trabalho.

### 5.3 Anel de paginação com alvo no rastro

**O que faz:** gravar no rastro, a cada tique, as últimas N operações do SDMA0 com tipo (FILL, TRANSFER, PTE), segmento, faixa de endereço físico e bytes. Hoje o BEV1 tem tipo e bytes, mas o endereço é a VA do IB, não o alvo.

**Por que vale:** em todas as travas o último trabalho é paginação do SDMA0. Este anel mostra **para onde** o SDMA estava escrevendo nos ~10 ms finais.

**Junto:** aumentar o anel BEV1 (48 → 256) ou drenar mais por tique, para parar de perder 60% dos eventos.

### 5.4 Estatística em vez de n=1

A dispersão vai de 3 s a 600 s, e a série 263–266 mostrou isso de novo: a 265 "melhorou" e a 266 desfez.

**Proposta:**
- trocar a métrica para "processos da T3 até a trava", com a carga fixa;
- decidir uma mudança só com 3 corridas por configuração, comparando as medianas;
- usar a T3 desde o primeiro segundo, para a subida do desktop não dominar (264 e 263b travaram antes).

### 5.5 Observador externo (precisa de um segundo PC)

**O que faz:** um programa na BC-250 envia UDP a cada 5 ms com o passo da T3 para outro computador, que grava com carimbo de hora.

**Por que vale:** dá o instante exato da morte, independente da drenagem de 115 ms do rastro. Separa "morreu durante a paginação" de "morreu entre operações".

**Custo:** baixo, mas exige outra máquina na rede.

### 5.6 Paridade nos acessos mais frequentes (mudança, não instrumento)

- **Laço do blt (A):** esperar a fence por interrupção, como o amdgpu, em vez de girar em `RPTR`.
- **Poll do GFX ocioso (B):** só ler com fence pendente, como a MEC.2.
- **VSYNC lendo DCN (K).**

A medida acima (263b) diz que isso não é necessário para a trava. Então essas mudanças vêm depois de 5.1–5.3, por paridade, não como teste da causa.

### 5.7 Hipótese de power/clock gating (doc 653)

A auditoria externa sugeriu que o RLC rodando faz gating sem coordenação e que um acesso a bloco gateado trava. A pista é que o RLC rodando encurtou a vida da máquina (n pequeno).

O teste exigiria **escrever** em registradores de CG/PG. Tem custo e risco: a RLC.1b sozinha travou em 8 s, e a leitura de 69 registradores de CG travou a 251. Fica depois da 5.1 e da 5.2.

## 6. Recomendação

1. **Próxima corrida (267): 5.1 + 5.3 juntos.** Os dois só medem e se complementam. O desfecho da 5.1 é binário e decide se a classe "endereço inválido" fica ou sai.
2. **Depois:** a bisseção 5.2, em três corridas curtas, com o mesmo pacote.
3. **Pendência separada:** o PROOF_TIMEOUT intermitente do SDMA1 (2 em 38).

**NÃO VALIDADO EM HARDWARE:** a causa da trava.
