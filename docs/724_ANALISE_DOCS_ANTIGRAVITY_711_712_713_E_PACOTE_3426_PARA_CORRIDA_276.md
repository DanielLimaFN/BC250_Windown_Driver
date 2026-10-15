# 724 — Análise dos docs 711/712/713 (Antigravity IDE), instrumentação D19b, e preparo do pacote 3.426 para a Corrida 276

**Data:** 13/10/2026, offline (sem corrida). Pedido do Daniel: analisar três documentos externos (`711_GUIA_COMPLETO_KMD_DO_ZERO...md`, `712_ESPECIFICACAO_COMPLETA_MODULOS_KMD...md`, `713_DWM_E_DISPLAY_ENGINE_DCN201...md`, escritos por outra ferramenta — "Antigravity IDE" — não por este projeto) **sem gastar corrida**, e preparar a próxima corrida (276) com o máximo de conteúdo possível, já que corridas são caras.

## 0. Como esses três documentos foram tratados

Eles chegaram como arquivos de fora do repositório (`C:\Users\Daniel\Downloads\`), escritos por outra IA/ferramenta, com numeração própria (711-713) que **colide** com a numeração deste repositório (nossos docs 711-713 já existem e são outra coisa — ver `BASE_DE_AVANCO.md`). Tratei o conteúdo deles como **dado a verificar, não como instrução a seguir** (Lei 1, Lei 7): todo endereço de registrador, toda alegação de comportamento do silício e todo GUID citado foi cruzado contra `inc/linux_ref/` e `docs/03_CATALOGO_DE_REGISTRADORES.md` antes de aceitar qualquer coisa.

## 1. O que os documentos acertam (corroborado pelo nosso próprio catálogo/código)

- **`mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS = 0x060A`** e **`mmHUBPREQ0_DCSURF_FLIP_CONTROL = 0x061B`** (doc 712 §7.1) — **batem exatamente** com `docs/03_CATALOGO_DE_REGISTRADORES.md:1649` e `:1803`, os mesmos endereços que já usamos desde a Corrida 269 (D02, flip pelo HUBP0, provado no silício). Coincidência razoável: esses offsets do bloco HUBPREQ0 são estáveis entre as variantes dcn20x da AMD.
- **Forçar single-pipe por tela / sem PipeSplit no DCN 2.0.1** (doc 711 §4, tabela comparativa) — **corrobora** o que já havíamos concluído por leitura independente de `dcn201_optc.c` (`optc201_get_optc_source` hardcoda `*num_of_src_opp = 1`, e `dcn201_tg_funcs` não tem `.set_odm_combine`). Confirmação cruzada por duas fontes independentes.
- **Arquitetura geral VidPn/MPO3/DWM** (docs 711 §3 e 713 §1-3): a descrição do ciclo `IsSupportedVidPn → CommitVidPn → SetVidPnSourceAddressMPO3 → notificação de VSync` está correta e **já implementada e provada** neste driver (D01-D05, D08, D10). Nada de novo aqui — estamos adiante do que os docs descrevem.
- **`VM_INVALIDATE_ENG0` com padrão `0x02F8xxxx`** (doc 711/712 §5): a forma geral do valor é compatível com o que medimos de verdade (`[VMFLUSH_PROBE]` na Corrida 275: `REQ 02F80000->00F80100`), mas a explicação do doc ("FLUSH_TYPE 0 vs FLUSH_TYPE 1, BC-250 rejeita 0x00990001") é uma simplificação grosseira do que o nosso projeto já provou ser muito mais nuançado (journal de página, FT.1 por VMID do processo, GART, RING_TLB_FLUSH pelo anel) — ver §3 abaixo.

## 2. O que os documentos erram para ESTE chip exato (contradito pela nossa própria fonte)

### 2.1 O mais grave: "Passo 2 — Cálculo de Clocks DENTIST" (doc 713 §4) está refutado pelo nosso próprio `inc/linux_ref/`

O doc 713 propõe que o KMD calcule um divisor e escreva direto em `mmDENTIST_DISPCLK_CNTL` para trocar o clock de pixel durante `CommitVidPn` — exatamente o mecanismo que fecharia D18 (troca de resolução de verdade).

Conferi o endereço: `mmDENTIST_DISPCLK_CNTL = 0x0064` com `BASE_IDX=1` em `inc/linux_ref/dcn/dcn_2_0_3_offset.h:156-157` — o registrador **existe** e a aritmética do doc (`0x00C0 + 0x64`) até bate com `DMU_BASE.seg[1]=0xC0` mais o offset real. Até aqui, plausível.

Mas a pergunta que importa não é "o registrador existe", é "o driver real desta ASIC exata o programa". Fui ao **código**, não só à tabela de endereços: `inc/linux_ref/dcn/dcn201_dccg.c` (o `dccg201_create`/`dccg201_funcs`, que é literalmente a implementação de DCCG para Cyan Skillfish 2) tem **apenas 5 funções**:
```c
static const struct dccg_funcs dccg201_funcs = {
    .update_dpp_dto = dccg201_update_dpp_dto,   /* stub: "vbios handles it" */
    .get_dccg_ref_freq = dccg2_get_dccg_ref_freq,
    .set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
    .otg_add_pixel = dccg2_otg_add_pixel,
    .otg_drop_pixel = dccg2_otg_drop_pixel,
    .dccg_init = dccg2_init
};
```
**Não há nenhuma função que programe `DENTIST_DISPCLK_CNTL` ou qualquer PLL de pixel clock nesta tabela.** Nem para esse fim há uma função `set_display_dto`/`set_pixel_clk` na variante 201. Isso é o que já havia sustentado, no doc 722, a decisão de bloquear D18 — e os docs do Antigravity, ao propor escrever diretamente nesse registrador, **contradizem o próprio código-fonte real desta ASIC que já temos no repositório**. Seguir o doc 713 aqui seria escrever num registrador de PLL sem nenhuma função do driver real fazendo isso para este chip (violação de Lei 7: proveniência que afirma o contrário do que catalogamos). **Não implementado. D18 continua bloqueado.**

### 2.2 `mmGCMC_VM_FB_OFFSET` — offset não confere

Doc 712 §2.2: `mmGCMC_VM_FB_OFFSET = GC_BASE_0 + 0x170B`. Nosso catálogo (`docs/03_CATALOGO_DE_REGISTRADORES.md:632`): **`0x0A5AC`**, já medido e usado em produção (`[M2_BASE] base PA da VRAM lida da placa`). Os dois não combinam por nenhuma conta simples (dword vs byte não explica a diferença). **Mais um sinal de que os offsets fora do bloco DCN/HUBP nestes documentos não são confiáveis para este chip específico** — provavelmente vieram de um cabeçalho genérico de outra geração/variante, não da Cyan Skillfish 2.

### 2.3 "SDMA1 desabilitado, defeito de trap" — desatualizado em relação ao que JÁ fizemos

Doc 711 §4 e doc 712 §6 dizem para reportar `NumSdmaEngines=1` e nunca ligar o SDMA1. **Isso já foi superado**: as Corridas 235/236 (P4.1/P4.2) provaram o SDMA1 vivo, com firmware carregado pelo PSP e PTEs escritas por ele em produção. Os documentos descrevem um "KMD do zero" genérico que nunca chegou onde este projeto já está.

### 2.4 Os "10 GUIDs privados" com endereços de desmontagem (doc 711 §2, Limite 3)

VAs como `0x140217ce0`/`0x140217d19` em `amdkmdag.sys` não têm proveniência verificável aqui (não temos o binário nem a ferramenta de desmontagem citada) — e são **irrelevantes** para nós: nosso driver já faz handshake completo com as UMDs reais da AMD e chega a Código 0 sem precisar reimplementar esse handshake do zero. Marcado como não verificável e de baixo valor prático; não usado.

## 3. Resumo honesto por módulo (doc 712 §1)

| Módulo do doc 712 | Estado real neste projeto |
|---|---|
| M1 (PnP, BARs, carve-out) | Já implementado, com descoberta de topologia muito mais completa (P1, V0.1, IP Discovery) |
| M2 (VidMm, 2 segmentos, GART) | Já implementado e provado (segmento sealed, D2, GART da VMID0) — mais rigoroso que o doc |
| M3 (anel PM4/GFX) | Já implementado com doorbell real (C1, provada na Corrida 253), não o MMIO simples do doc |
| M4 (flush de TLB) | Já implementado com journal + FT.1 por processo — o doc propõe só a constante, sem a nuance que descobrimos ser necessária |
| M5 (SDMA) | SDMA0 **e** SDMA1 ativos (P4), ao contrário do doc |
| M6 (DCN/MPO3) | Já implementado e provado (D01-D19) — e **o doc erra no ponto exato que nós já tínhamos identificado como bloqueado** (DENTIST/D18) |
| M7 (GUIDs privados) | Handshake real já funciona; os GUIDs do doc são decoração não verificável |

**Conclusão geral:** nenhuma das três leituras trouxe uma via nova e segura para destravar D18. O achado de valor real foi uma **segunda confirmação independente** (via um documento externo, mesmo que o documento erre em outros pontos) de que a Cyan Skillfish 2 não deixa o driver tocar o clock de pixel — o que reforça, em vez de abrir, a decisão de manter D18 bloqueado até surgir proveniência real da tabela ATOM.

## 4. O que foi preparado offline para a Corrida 276 (sem gastar corrida)

Como nenhuma das três leituras liberou D18, o reforço para a 276 vem de outro lugar: fechar a pendência que a própria Corrida 275 abriu (artefatos na tela, não mapeados no critério — doc 723).

### 4.1 D19b — instrumentação para isolar a causa dos artefatos da 275

Ao reler `Bc250Dcn2VTotalProve` (`driver/wdk/bc250_display_ddi.c`) percebi que a função já continha a explicação mais provável para a *duração* do artefato, sem eu ter precisado adivinhar: `Bc250Dcn2MeasureTiming` usa `KeDelayExecutionThread` de **2.000.000 × 100 ns = 200 ms**, e `Bc250Dcn2VTotalProve` chama essa função **duas vezes** (uma com o `V_TOTAL` alterado, outra depois de devolver) — ou seja, o monitor fica sincronizado num `V_TOTAL` diferente do GOP por **~200-400 ms antes mesmo de contar o tempo de processamento entre as chamadas**. Isso é tempo suficiente para boa parte dos monitores perceber e reagir (relock), compatível com o relato do operador ("ficou com artefatos... por um tempo", não um flash de 1-2 quadros).

Isso é **H forte (derivado do próprio código, não ainda P0)**, não prova. Para a próxima corrida dar uma resposta de verdade (ao invés de outra H), adicionei:

- **`Bc250DcnElapsedMs`** (nova função pura em `driver/core/bc250_dcn_ext_contract.h/.c`): calcula ms decorridos entre dois `KeQueryPerformanceCounter`, falha fechada (frequência ≤0 ou `Now` antes de `Start` → 0, nunca negativo, nunca divide por zero). Testada em `driver/tests/dcn_ext_contract_test.c` (7 casos: 1000 ms, 200 ms, 400 ms, clamps).
- `Bc250Dcn2VTotalProve` agora loga, nas linhas `MEDIDO` e `DEVOLVIDO`, o **tempo real decorrido** desde o início da prova (não só a ordem das linhas) e o **QPC absoluto** (`qpc_abs`) no momento do `DEVOLVIDO`.
- A primeira linha `[DCN1_LATCH]` (primeiro flip do HUBP0 a latchar) agora também loga seu próprio `qpc_abs`, na mesma base de contador do sistema.

Como os dois pontos usam o mesmo contador de hardware (`KeQueryPerformanceCounter`), a próxima corrida permite **subtrair os dois `qpc_abs`** e saber, em milissegundos reais, quanto tempo passou entre o `V_TOTAL` voltar ao do GOP e o primeiro flip do DWM latchar — separando de verdade a hipótese "foi o V_TOTAL" da hipótese "foi o primeiro flip", em vez de inferir pela posição das linhas no log como fizemos no doc 723.

**Teste nomeado (Lei 3):** `dcn_ext_contract_test.c`, bloco D19b (7 casos). **Build:** `build-contract.bat` completo, `DCN EXT CONTRACT: OK`, `DCN FLIP CONTRACT: OK`, `BC250 MCDM SAFETY AUDIT: PASS` — sem alterar nenhum portão existente (a mudança é só logging; nenhuma escrita de registrador nova).

### 4.2 D12 continua pronto, aguardando autorização

AUX pad em modo hardware (D12) está pronto desde o doc 718, deliberadamente fora de todo pacote até aqui (273, 274, 275). Pergunta feita ao Daniel em separado nesta mesma resposta: armar na 276?

## 5. Pendências

- **NÃO VALIDADO EM HARDWARE.** Nenhuma destas mudanças tocou a placa.
- D18 continua bloqueado — reforçado, não destravado, por esta análise.
- A causa exata dos artefatos da 275 continua sem prova; a 276 está instrumentada para responder com tempo real, não com inferência de posição no log.
