# 510 — Validação da auditoria externa, DX.96-HDP e critério da Corrida 158

> 28/09/2026, offline. O Daniel confirmou que **não mexeu na memória** (tREF e demais temporizações estão como vieram).

## 1. Validação da auditoria externa, achado por achado

| # | Achado da outra IA | Veredito | Evidência |
|---|---|---|---|
| 1 | `Bc250ActiveReadVramU64` mapeia `BAR0+offset` além de 256 MB sem limite; seria o gatilho do congelamento | **Defeito confirmado; como gatilho, refutado para as corridas medidas** | O código não tem limite (P1, lido). Mas a linha `[PT_READ] VRAM U64 sob demanda` aparece **0 vezes** nas corridas 153–157, e sua classe de log (a mesma de `[PT_RANGE]`) emitiu dezenas de milhares de linhas (P2 forte). O caminho não rodou. Corrigir por segurança. |
| 2 | O segmento 2 é codificado como endereço físico cru (0–2 GB), e a GPU escreveria na RAM baixa do Windows | **Risco latente confirmado; como mecanismo, refutado** | O encoder trata Seg=2 como `physicalAddress = offset` (P1, lido). Porém as PTEs da 157 têm Seg=1 (5.321), Seg=0 (796) e Seg=65535 (22), e **nenhuma Seg=2** (P0, log amostrado). Os DMA buffers são lidos pela GPU em `PA=0x20A309000`, RAM real (P0), e os IBs fecham o tripé. Corrigir: recusar Seg=2. |
| 3 | A página dummy em `0x450000000+` seria um endereço inexistente ("17,25 GB") que causa Sync Flood | **Refutado** | `0x450000000 = GCMC_VM_FB_OFFSET(0x450) << 24`, lido do silício em 23/08 e igual à regra upstream (`gfxhub_v2_0.c:117`, catálogo docs/03:721-730). A GPU lê e escreve nessa faixa o tempo todo, com prova de bytes pela CPU (P0). O redirecionamento de fault para a dummy existe (P1) e é observável pelo `GFX_FAULT_SENTINELA` (0 ocorrências na 157). |
| 4 | Lei 4: FLUSH_TLB e UpdatePT convertem erro em SUCCESS | **Confirmado** (igual ao doc 509 §2.3) | P1. A doc oficial do `DxgkDdiBuildPagingBuffer` só admite SUCCESS, ALLOCATION_BUSY e INSUFFICIENT_DMA_BUFFER, então a correção não é "devolver o erro". É preciso não chegar ao erro e tornar a divergência observável. |
| 5 | PTE sem MTYPE e sem flush de cache (GCR) na paginação | **Refutado** | MTYPE 0 (NC) é o padrão upstream do GMC v10 (frente 1 do doc 509). O GCR existe: `Bc250SdmaPresentSubmitIndirectAsync` emite GCR e depois FENCE no anel depois de **cada** IB (`bc250_sdma_present.c:1927`, P1 lido). Ele fica no anel, não dentro do DMA buffer. |
| 6 | O fill fatiado em 0x3FFFFC desalinha o próximo pedaço de 4 KB | **Aritmética correta; impacto não sustentado** | O pedaço seguinte começa em +0x3FFFFC (P0 aritmético). A Mesa usa o mesmo limite (P1). As travas também existiam com o limite antigo de 0x400000, alinhado (135). Melhoria barata opcional: fatiar em 0x3FF000. |
| 7 | StopDevice devolvendo `STATUS_DEVICE_BUSY` viola o PnP | **A conferir (R)** | Não afeta a trava. Baixa prioridade. |
| 8 | Push lock no FLUSH_TLB arriscaria IRQL em DISPATCH | **Refutado** | A doc oficial do `DxgkDdiBuildPagingBuffer` dá IRQL **PASSIVE_LEVEL** (frente 4 do doc 509). |
| 9 | Journal abortado deixa a vista de software divergente do hardware | **Confirmado** (conhecido, docs 499/509) | P1. |

**Plano da outra IA:**
- **Nível 1 (`powercfg ... idledisable 1`):** é uma ideia **boa e barata**, porque a BIOS não tem opção de C-state: mantém os núcleos em C0 e impede os estados profundos do pacote e do fabric. É uma configuração do sistema, então **o Daniel decide e executa** e depois reverte. Fica como **segundo** ensaio, depois da DX.96-HDP, para não misturar variáveis.
- **Nível 2 (tirar o segmento 2):** perde a razão depois do achado 2 refutado.
- **Nível 3:** o limite na BAR vale como correção de segurança. Armar a interrupção de fault é útil, mas os faults já são observáveis e não houve nenhum.
- **Nível 4 (scratch registers do CP):** **rejeitado**. O reset reinicializa o ASIC (o conteúdo não sobrevive), e seriam mais escritas MMIO a cada operação.
- **Nível 5 ("Sync Flood para os clocks do UMC"):** plausível, mas **H sem fonte citada**. Não entra como fato.

## 2. DX.96-HDP (pacote 3.307.1.0)

**Mudança única de comportamento, alinhada ao upstream (P1, `hdp_v5_0.c`, `hdp_5_0_0_offset.h`, `hdp_5_0_0_sh_mask.h`):**
1. `Bc250SdmaPresentHdp` e `Bc250SdmaHdp` (ring work) fazem só flush (`0x4A80=0` + leitura) e invalidação (`0x3FC4=1` + leitura). **Não tocam mais `HDP_MISC_CNTL`.**
2. `Bc250SdmaPresentHdpInitOnce`, chamada em `Bc250SdmaPresentInit`, liga `FLUSH_INVALIDATE_CACHE` (bit 0) **uma vez** pela fórmula da Lei 6. Grava a linha `[DX96_HDP] HDP_MISC_CNTL init unica: encontrado= planejado= lido= escrito=`.
3. O rollback do tripé de boot (Lei 5) continua restaurando o `HDP_MISC` do snapshot, o que agora é neutro.

**Catálogo:** `HDP_READ_CACHE_INVALIDATE` e `HDP_MISC_CNTL` sobem de P2 para P1 (offsets `0xD1`/`0xD3` + base `0xF20` = `0x3FC4`/`0x3FCC`; bit 0 = `FLUSH_INVALIDATE_CACHE`).

**Provas offline:**
- `build-contract` EXIT 0, com "TODOS OS TESTES APROVADOS (0 falhas)";
- matriz DDI e auditoria de segurança PASS; no-op PASS;
- build de laboratório EXIT 0; auditoria do pacote PASS;
- **portão `audit_dx96_hdp_misc_written_once`**, com quatro mutações e quatro reprovações: toggle de volta no Hdp, escrita extra, init sem a chamada, toggle no ring work;
- conferido por string no SYS assinado: `[DX96_HDP]`, `[FPS3B]` e `DX95 Node`.

O anel de eventos DX.96 continua no cabeçalho das migalhas, **sem chamadores**. Fica inerte de propósito, porque escrever eventos em VRAM seria mais tráfego pela HDP no momento em que a HDP é a variável em teste.

| Artefato (3.307.1.0) | SHA-256 |
|---|---|
| `bc250mcdm.sys` assinado | `7431385A3D2A5C147BCA408C4392DC1A6BD726CF9745D1CBB2069A888B3FB037` |
| `bc250mcdm.sys` fonte lab | `4D8BB56E145CEE6AFF81FB0C29CE29C96DE67DC0C408CB67EE496A2F4B7A60F5` |
| `bc250umd.dll` (igual à 149–157) | `FAE9F89EB95B33C2A4E2A39D1688609EA4A3FF802B43492C72D45964A3406911` |
| `bc250mcdm.inf` | `15EDD730A135E18F2FEEC674AA2CD8ED94A4095DB29E900E44D36718CC030E35` |
| `bc250mcdm.cat` | `A0EC1EC5B4617E53072D386E48EFA58D4A0322455BEE626AE24B4CF4568E7468` |

## 3. Critério da Corrida 158 (escrito antes)

- **Runner:** `tools\EXECUTAR_CORRIDA158_DX96HDP_600S.cmd` (preparo `prepare-corrida158-dx96hdp.ps1`), sem KD, harness atrasado 150 s.
- **Antes:** recuperação, reset do fusível e boot frio **com o monitor ligado antes**.
- **Na trava:** botão reset em 10–15 s.
- **Linha de base:** 5 travas em 6 corridas depois da FPS.1. Uma corrida limpa sozinha é evidência fraca: com a taxa antiga, a chance de uma corrida limpa por acaso é ~17%. **Fechar pede duas corridas seguidas.**

| Desfecho | Assinatura (o que o log mostra) | Implicação |
|---|---|---|
| **A** | `[DX96_HDP]` presente; 600 s sem trava; zero `IB_TRIPOD TIMEOUT`, `Blt Timeout`, TDR e BugCheck; `IB EXECUTADO` com fence sobre poison; harness 60/60 | O toggle de `HDP_MISC` é o candidato mais forte. Repetir a corrida; duas limpas seguidas dão nome à trava. |
| **B** | Trava como antes (GPU ociosa, VRAM decaindo) | O toggle não era o gatilho. Próximo ensaio: `powercfg` sem C-states (decisão do Daniel), com o mesmo pacote. |
| **C** | Sem trava, mas a fence do SDMA deixa de ficar visível (`IB_TRIPOD TIMEOUT`, fence no poison, `Blt Timeout`) | O toggle mascarava um problema de coerência. Descobrir o flush correto (`HDP_MEM_COHERENCY_FLUSH_CNTL` ainda P2), sem voltar ao toggle. |
| **D** | Outra regressão: tela preta sem trava, pixels errados, BugCheck | Reverter a DX.96-HDP e isolar. |
| **E** | Algo fora do mapa | Descrever antes de julgar. |

Observação que não entra nos desfechos: o valor `encontrado=` da linha `[DX96_HDP]` diz em que estado o POST e o tripé de boot deixam o `FLUSH_INVALIDATE_CACHE`.

**NADA DA DX.96-HDP FOI VALIDADO EM HARDWARE.**

## 4. Adendo: a mesma corrida também mede o FPS (pedido do Daniel: corridas são o recurso caro)

- **Harness aos 30 s** (antes, 150 s), no runner. Ele leva ~35 s para os 60 quadros, então o FPS sai antes da janela em que as travas vinham acontecendo (77–183 s). Depois da FPS.1 e da FPS.3b o FPS do harness **nunca foi medido**: nas 155–157 a trava veio antes ou na abertura dele. É mudança de carga, não de driver. O desfecho A (sem trava) continua valendo; num B, a trava durante o harness tem o precedente das 152/156.
- **Divisão do quadro, offline:** `python tools/analyze-fps3-retire.py <log KMD> <log UMD do harness>` agora mostra, dentro de cada `pfnPresentCb`, quantos IBs de paginação, quanto tempo esperando a conclusão do SDMA, quanto dentro do FLUSH_TLB, quantos submits GFX e o resto (VidMm).
- **Primeiro dado da ferramenta, sobre a 153 (P0):** dos 451 ms de espera por quadro, **~100 ms estão dentro dos nossos FLUSH_TLB** (19,5 IBs de paginação por quadro, ~23 ms por IB). A 153 não tinha horário na conclusão, então o resto (351 ms) ainda mistura a espera de conclusão com o tempo do VidMm; a 158 separa os dois.
- **Mudança de comportamento continua sendo só a DX.96-HDP.** O mesmo tratamento no timer do GFX (FPS.3c) e o lote único do journal (FPS.2) ficam para depois: ambos mudam o tráfego da CPU para a memória da GPU, e confundiriam o teste do HDP.
