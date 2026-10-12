# 653 — Auditoria da trava dura (corridas 199–248b): o TMR do PSP entregue ao VidMm, o que os números dizem e um desenho experimental novo

> 08/10/2026, offline, zero corridas. Resposta ao prompt do doc 652. Li código, logs brutos de 41 corridas (`docs/evidence/2026-10-0*`), a coleta Linux (doc 552) e o `amdgpu_psp.c` upstream.
> Vocabulário: **P0** medido no silício · **P1** código nosso ou upstream · **H** hipótese.

---

## 1. Resumo em 5 linhas

1. **Achei um defeito real que ninguém tinha visto.** O `pfp_direct_load` faz `SETUP_TMR` no PSP e carrega o microcódigo do CP no TMR. Depois **devolve o TMR e a região do CP ao alocador** (`bc250_pfp_direct_load.c:1485-1487`). As duas regiões caem dentro do segmento do VidMm, e o VidMm põe **tabelas de página** exatamente ali (P0, em 40 de 40 corridas).
2. **Segundo defeito no mesmo comando:** passamos ao PSP o endereço da **BAR** (`0xC0000000+off`) como endereço físico do TMR. O amdgpu passa o endereço da **DRAM** (`0x460000000+off`, `amdgpu_gmc_vram_pa`) e mantém o TMR fixado até o fim (P1 upstream).
3. **A trava é um risco por evento, não por tempo.** Medida em FILLs de paginação, a dispersão cai de 88× para 15×. RLC rodando e P3.3 **encurtam a vida com significância estatística** (Mann-Whitney sobre FILLs: p≈0,006 e p≈0,003). Não é ruído.
4. **O sinal físico mais forte foi abandonado:** a GDDR6 decai durante a trava (doc 506, P0). O controlador de memória para, o que aponta para memória protegida, PSP/SMU ou energia, não para "mais um registrador do GFX". **Parem de bissectar o GFX.**
5. **Proposta:** primeiro corrigir o TMR como o amdgpu (uma mudança, um defeito). Depois testar na configuração que trava mais rápido (RLC rodando, 4/4 travas abaixo de 133 s), com repetição. Em paralelo, um ensaio **no Linux** (`amdgpu.dc=0`) para testar a hipótese do display sem gastar corrida do driver.

---

## 2. Hipóteses, da mais para a menos plausível

### H1 — O TMR do PSP (com o microcódigo do CP) foi entregue ao VidMm e é sobrescrito por tabelas de página e alocações

**Mecanismo (P1 + P0):**
1. `Bc250ActiveRunPfpDirectLoad` aloca no alocador do bootstrap, de cima para baixo:
   - um TMR de 4 MiB alinhado a 4 MiB (`:613-615`);
   - a região "quad" de 1,1 MiB (`:622-624`).
2. Manda `SETUP_TMR` (`:861-866`) e cinco `LOAD_IP_FW` (ME, PFP, CE, MEC, MEC_JT1) para esse TMR. No modo PSP, o CP busca o microcódigo no TMR pela I-Cache (o `IC_BASE` é do PSP: lê `0xFFFFFFFF` para nós, doc 01).
3. No rótulo `Cleanup`, **libera as duas regiões** (`:1484-1487`).
4. Na mesma partida, o `Bc250ActiveQueryRuntimeCaps` corta o segmento só nas regiões **em uso** (DX.67, `bc250_active_miniport.c:3841+`). TMR e quad já estão livres e ficam **dentro** do segmento.

**Posições:**

| Região | VRAM | Fonte |
|---|---|---|
| Segmento do VidMm | `[0xA8C000, 0xFA00000)` | P0, `[DX67_VRAM_MAPA]` |
| quad do CP | `[0xF8F0000, 0xFA00000)` | P0, `[T31_UC_MAP] PFP_DIRECT_LOAD` |
| TMR do `SETUP_TMR` | `[0xF400000, 0xF800000)` | P1: algoritmo de `Bc250VramAllocate` sobre o mapa medido (a região `[0xFB00000,+4 MiB)` ocupada empurra o candidato alinhado para 0xF400000) |
| TMR do amdgpu na mesma placa | `[0xF800000, 0xFC00000)`, fixado até o `sw_fini` | P0, doc 552: `reserve 0x400000 from 0xf40f800000 for PSP TMR` |

**O VidMm põe as tabelas de página raiz de cima para baixo no topo do segmento (P0, `McdmSetRootPageTable`):**
- em **40 de 40** corridas com raiz registrada, as raízes caem na região quad liberada (offsets de segmento `0xEE5E000`–`0xEF6A000`, ou seja VRAM `0xF8EA000`–`0xF9F6000`);
- na corrida de 06/10 10:00 (231) uma raiz desceu a `0xE7F6000` (VRAM `0xF282000`): a zona de tabelas atravessou o TMR inteiro;
- as tabelas folha, muito mais numerosas que as raízes, **não são registradas no log**. Pela mesma política de cima para baixo, crescem para baixo a partir de 0xF8F0000. O TMR começa 960 KiB abaixo (H, mensurável, §4).

**O que isso pode causar (H, dois ramos, os dois ruins):**
- **(a) O TMR é protegido** (o PSP trava a faixa no data fabric): as escritas de PTE pelo SDMA (VMID 8) e os FILLs são bloqueados ou viram erro de segurança. O GFX lê lixo como PTE e acessa endereço físico arbitrário. Ou o próprio bloqueio para o fabric. Os dois casam com "a máquina morre sem BugCheck, com o controlador de memória parado".
- **(b) O TMR não é protegido:** tabelas de página e alocações sobrescrevem o microcódigo do ME/PFP/CE/MEC. O CP executa lixo no próximo miss da I-Cache, quando aparece um caminho de código novo, por exemplo o primeiro IB de um device novo do menu (docs 629 e 632).

**A favor:**
- O risco cresce com o uso, e a zona de tabelas cresce com processos e contextos (o menu cria devices novos a cada abertura).
- O CV de FILLs-até-a-trava é 0,51 (§3), menor que o de um processo sem memória (1,0): há um componente de acúmulo.
- Parado no desktop, não há criação de contexto nem crescimento das tabelas, e a 237 passou 600 s.
- O Linux, com o mesmo firmware e a VRAM **cheia** (254/256 MB), não trava, e lá o TMR está fixado.

**Contra:**
- Travas em 8–22 s (248/248b/242) com só 3 a 6 raízes, mas a contagem de tabelas folha é desconhecida.
- **Não sabemos se o PSP aceitou o `SETUP_TMR`:** o contador `cpPspLoadedCount` é gravado em `FaultAddressLow` (`:935`) e sobrescrito pelo scratch (`:1264`, sonda `PfpDirectFaultAddrLow=0x11111111`). Se o PSP recusou, o CP roda o firmware do POST, a região liberada nunca foi TMR, e H1 cai para o ramo "o BIOS tem um TMR em outro lugar".

**Confiança:** o defeito é **P1** (código + upstream). A causalidade com a trava é **H**, a mais barata de testar e a mais bem fundamentada que resta.

**Como matar ou confirmar:**
- **Offline (sem corrida):** acrescentar ao encoder de PDE (`Bc250McdmEncodeVidMmPteWithLevelEx`, nível > 0, onde o offset da tabela filha é conhecido) um contador de tabelas que caem em `[TMR)` e em `[quad)`. Acrescentar também o log da resposta de cada comando do PSP em campos próprios.
- **Na corrida:** a correção TMR.1 (§5), na configuração que trava mais rápido.

### H2 — Uma transição de energia gerida pelo PMFW (SMU) com um participante do lado da GPU fora do estado esperado

**Mecanismo (H):**
- Rajada de GPU → o PMFW sobe e desce relógios e tensões (GFXCLK com deep sleep, possivelmente SOC, VDDNB) → falta um handshake (RLC, ou o DCN que só o GOP programou) → o PMFW trava no meio.
- O VDDNB/SOC é compartilhado com o controlador de memória e o fabric. Resultado: refresh parado e CPUs mortas.

**A favor:**
- **Decaimento da GDDR6 com a placa energizada** (doc 506/507, P0): o controlador de memória parou.
- GPU ociosa no último tique, logo depois de rajadas (doc 582, P0).
- W1 (sem driver e sem carga de GPU): 45 min sem nada.
- RLC rodando encurta a vida (p≈0,006): o RLC é o participante do handshake com o SMU.
- **No Linux (P0, `monitor.csv`, 244 amostras):** MCLK = FCLK = 450 MHz e SOCCLK = 1254 MHz **constantes** sob vkcube e 149 despejos. O SCLK varia de 4 a 100 MHz, ou seja o GFX entra em deep sleep.
- **No Linux o DCN pertence ao amdgpu** ("Display Core initialized on DCN 2.0.1", P0). É a maior diferença estrutural que resta, e não está no inventário do doc 593.

**Contra:**
- Com o RLC na ROM também trava, só que mais devagar.
- O teste de RAM pela CPU (8 GB × 2, BasicDisplay) não trava.
- Não sabemos se no Windows algum relógio de memória ou fabric muda.

**Confiança:** H.

**Teste sem corrida do driver (L1, Linux):**
- `amdgpu.dc=0`: o display fica no `simpledrm` do GOP, exatamente como nós.
- Mesma carga do doc 552 (laço de `amdgpu_evict_vram` e uma carga Vulkan headless) e mesmo `monitor.csv`.
- Assinatura e implicação:
  - **trava** → o dono do DCN é o mecanismo, e a correção passa por P9 ou por mensagem de configuração de display ao SMU, decisão do Daniel pela governança;
  - **2 × 10 min sem trava** → o display não é suficiente.
- Custa um boot de Linux, não um pacote nosso.

**Teste com corrida (instrumentação):** amostrar a tabela de métricas do SMU (GFXCLK, FCLK, MCLK, SOCCLK, VDDGFX, VDDNB) a 1 Hz pela mesma mensagem que o `amdgpu_pm_info` usa. Ela já foi exercitada 244 vezes na placa sem travar (P0, Linux), e o ID da mensagem tem de vir do `smu_v11_8_ppsmc.h` (Lei 7). Assim a próxima trava mostra o estado de energia no último segundo.

### H3 — A GPU acessando endereço físico inválido (PTE lixo ou TLB velha)

**Mecanismo:** uma PTE com SYSTEM apontando para o buraco de MMIO, para a própria BAR5 ou para RAM devolvida ao Windows. Um DMA para endereço não roteável trava o fabric sem MCE.

**Situação:**
- Só ganha força como **consequência** de H1(a).
- O encoder (`bc250_mcdm_callbacks.c:1277-1280`) só gera SYSTEM a partir do `PageAddress` do VidMm, que é confiável, e `Zero`/`Valid=0` viram PTE nula (`:1124`).
- Não achei caminho próprio que gere endereço de sistema arbitrário. A exceção é o fallback do TMR (bug 3, §4), que não roda hoje.
- **Teste:** cai junto com H1.

### H4 — CPU presa numa leitura de MMIO (bloco desligado), sem NMI

- **Fraca.** As migalhas DX.87 nunca mostraram leitura de MMIO em voo por mais de um tique (doc 582), e H4 não explica o decaimento da DRAM.
- Fica registrado: a DPC de VSync lê o `OTG0_OTG_STATUS_FRAME_COUNT` do DCN a **250 Hz** enquanto o DWM mantém o VSync ligado, ou seja só durante o uso (`bc250_display_ddi.c:1588`, `:1627`). É leitura com procedência, como o Linux faz, mas a taxa é 4× a do Linux, que lê por interrupção de vblank.

### Já descartadas, sem reabrir
SMN do Tctl (só desde a 199, e a trava é anterior), HDP, AGP/GART, doorbells, interrupções, SDMA1, goldens, C-states da CPU (T6, doc 521), calor, RAM.

---

## 3. Crítica ao método e desenho experimental

### 3.1 O que os números dizem (P0, contadores `BPG1` cumulativos do rastro, sem amostragem)

FILLs de paginação até a trava, cortados no recuo do `writer_tick`:

| Grupo | n | FILLs (média) | Mediana | CV |
|---|---:|---:|---:|---:|
| RLC na ROM, sem P3.3 (corridas 210–246b) | 24 | 1.192 | 1.210 | 0,51 |
| RLC rodando (244, 247, 248, 248b) | 4 | 467 | 423 | 0,38 |
| P3.3 ligada (242, 242b, 243) | 3 | 262 | 264 | 0,23 |

- Mann-Whitney (unilateral) RLC rodando × ROM: U=10/96, z=−2,49, **p≈0,006**. P3.3 × ROM: U=0/72, z=−2,78, **p≈0,003**.
- Em segundos a faixa vai de 6,6 a 583 s (88×). Em FILLs vai de 188 a 2.890 (15×).

**Conclusões:**
1. **A pergunta do doc 652 tem resposta:** há sinal. RLC rodando multiplica o risco por ~2,5 por evento, e a P3.3 por ~4,5.
2. **"Uma variável, n=1" não serve aqui.** Com CV de 0,5, uma corrida só não distingue um efeito de 2× com segurança. Quatro das últimas onze corridas tiraram conclusão de um ponto só.
3. **A régua é FILLs ou FlushTlb até a trava, não segundos:** a partida do desktop concentra eventos, e por isso as travas precoces parecem outliers em segundos.

### 3.2 Erros de método (sem acusação, para não repetir)

- **O dado físico mais discriminante (decaimento da DRAM, doc 506) saiu do raciocínio** depois da 199. As 40 corridas seguintes foram paridade de blocos do GFX/SDMA/IH. Elas são valiosas, mas estão a montante de um controlador de memória que para.
- **O inventário de paridade (doc 593 §4) não inclui memória reservada.** O TMR é o primeiro item do `psp_hw_start` no Linux.
- **O inventário também não inclui o dono do display:** o Linux tem o DC ativo no DCN 2.0.1.
- **Evidência sobrescrita:** o único dado sobre a aceitação do `SETUP_TMR` foi apagado pelo próprio código (bug 4, §4).
- **Duas mudanças na 248** (RLC.1b + LOGMIN.1), contra a regra do projeto.

### 3.3 Desenho proposto

1. **Ensaio na configuração de risco alto.** Com RLC rodando, 4/4 travaram antes de 133 s e todas antes de 722 FILLs. Se uma correção levar essa configuração a 600 s duas vezes, a chance de ser acaso é ≈0. Na configuração ROM, a chance de uma corrida sobreviver 600 s por acaso é ~15% (4 de ~28).
2. **Toda candidata roda 2×, e 3× para fechar.** O critério é escrito em FILLs e em segundos.
3. **O Linux como laboratório reverso:** fazer o Linux ficar parecido conosco, uma diferença por vez, até ele travar. Começar por `dc=0`. Lá há `pstore`/`ramoops`, `umr` e métricas do SMU de graça.
4. **Acelerador sintético (opcional, offline):** um harness D3D11 que cria e destrói devices e alocações de tela cheia em laço reproduz a abertura do menu sem as pausas de 30 s.

---

## 4. Defeitos concretos no código

| # | Onde | Defeito | Classe |
|---|---|---|---|
| 1 | `driver/wdk/bc250_pfp_direct_load.c:1484-1487` | `Bc250VramRelease` do TMR e do quad depois de `SETUP_TMR`/`LOAD_IP_FW`. O TMR vivo do PSP e a memória do microcódigo do CP voltam ao alocador e caem no segmento do VidMm. O amdgpu mantém o TMR fixado até o `psp_sw_fini` | **P1**, grave |
| 2 | `bc250_pfp_direct_load.c:656` (e `:659`) | `tmrPa = bar->Start + off`: o PSP recebe o endereço da BAR PCI como endereço físico do TMR. O upstream usa `amdgpu_gmc_vram_pa` (DRAM, `GCMC_VM_FB_OFFSET<<24 + off` = `0x46xxxxxxx` aqui) | **P1** |
| 3 | `bc250_pfp_direct_load.c:657-660` | Se a alocação do TMR falhar, o fallback é `VramMcStart + 0x1F800000` (era dos 512 MB). Com 256 MB isso fica fora da VRAM, e o PA cai no buraco de MMIO (`0xDF800000`). Tem de falhar fechado | **P1** latente |
| 4 | `bc250_pfp_direct_load.c:935` × `:1264` | O número de comandos aceitos pelo PSP (`cpPspLoadedCount`) é sobrescrito pelo scratch do CP. A resposta de `SETUP_TMR` e de cada `LOAD_IP_FW` nunca chega ao log. Viola a Lei 1 na prática | **P1** |
| 5 | `bc250_active_miniport.c:3841+` (DX.67) | O corte do segmento só vê regiões **em uso naquele instante**. Toda região que o hardware ou o firmware continuam usando (TMR) tem de ser persistente, ou a regra não protege nada. Falta portão de auditoria: "nada que foi entregue ao PSP pode ser liberado" | P1, de desenho |
| 6 | doc 652 §6 | "Todo acesso a MMIO passa pelo transport com allowlist" é falso para a leitura do OTG (`bc250_display_ddi.c:1907`, `MmMapIoSpace` direto). É só leitura e tem procedência; o erro é de documentação | menor |

---

## 5. Plano: no máximo 3 corridas, critério escrito antes

### Pré-trabalho offline (sem corrida)

**TMR.1 — o TMR como o amdgpu.** É uma mudança só, a correção dos bugs 1–3:
- alocar o TMR uma vez, **persistente**, e não liberar;
- mandar a DRAM PA como `system_phy_addr`;
- recusar o fallback.

A região quad só pode ser liberada se nada do hardware a referenciar depois do restore. Para não discutir, também fica persistente: são 1,1 MiB.

O segmento passa de ~239 MiB para ~233 MiB. Não é VRAM maior nem menor por escolha: é a memória que o amdgpu também não dá aos processos.

**Instrumentação junto:**
- resposta do PSP por comando: `[TMR1] SETUP_TMR resposta=… LOAD_IP_FW tipo=… resposta=…`;
- contador de PDEs para dentro de `[TMR)`/`[quad)`, que deve ser 0 depois da correção;
- mapa DX.67 listando o TMR.

**Portão:** `audit_tmr1_tmr_persistente_como_amdgpu`, com mutações:
- liberar o TMR;
- voltar à BAR;
- reativar o fallback;
- o contador sumir.

**Em paralelo, L1 no Linux (`amdgpu.dc=0`):** decisão e mãos do Daniel. Não conta como corrida do driver.

**Base:** 3.393.1.0 (248b: G.1 + RLC rodando + LOGMIN) + TMR.1. A RLC rodando entra **de propósito**: é o ensaio que trava mais rápido.

### Corrida 249 e Corrida 250 (mesmo pacote, repetição obrigatória)

**Pré-condição**, sem ela a corrida é inválida:
- `[TMR1] SETUP_TMR resposta=0x0` com `system_phy_addr=0x46F…`;
- o mapa DX.67 mostra o TMR em uso e o fim do segmento abaixo dele;
- desktop desenhado;
- o mesmo roteiro do menu iniciar até o rollback.

| Desfecho | Assinatura (o log mostra) | Implicação |
|---|---|---|
| **A** | 249 **e** 250 chegam a 600 s com rollback limpo, contador de PDEs no TMR = 0, FILLs acima de 1.500 em cada | TMR liberado era necessário para a trava (com a base RLC rodando a 4/4 abaixo de 133 s, o acaso é ≈0). 251 repete para fechar com n=3 |
| **B** | Pelo menos uma trava antes de 600 s, assinatura de sempre, pré-condição cumprida | TMR.1 não é suficiente. Fica como correção (é defeito de qualquer jeito). A 251 vira a sonda do SMU (H2) no mesmo pacote |
| **C** | `SETUP_TMR` recusado pelo PSP (resposta ≠ 0) | O PSP já tinha TMR (do BIOS) e a região liberada nunca foi TMR: H1 cai no ramo PSP. Achar onde está o TMR do BIOS vira o próximo passo (leitura offline das reservas do `e820` e do VBIOS) |
| **D** | Código 43, CP sem firmware, PSP falhando, regressão de display | Reverter TMR.1; o log do PSP diz por quê |
| **E** | Fora do mapa | Descrever antes de julgar |

"Desfecho parcial não é desfecho": A exige as **duas** corridas e o contador zerado.

### Corrida 251

- **Se A:** repetição idêntica (n=3).
- **Se B:** mesmo pacote + amostrador de métricas do SMU a 1 Hz (instrumentação, mensagem já exercitada pelo Linux nesta placa). Critério:
  - **B1:** FCLK/MCLK/SOCCLK mudam antes da trava (no Linux são constantes) → H2 no fabric;
  - **B2:** só o GFXCLK muda, com transição no último segundo → H2 no handshake GFX/RLC;
  - **B3:** tudo estático → H2 cai, e voltar a H3 com interrupções de fault ligadas;
  - **E:** fora do mapa.

---

## 6. Observabilidade da trava (sem KD, sem dump)

1. **Métricas do SMU a 1 Hz** no rastro (H2). É o que falta para ler o estado de energia no instante.
2. **Respostas do PSP** e **mapa de memória reservada** no boot: barato e decisivo.
3. **Página-canário passiva:** um padrão gravado uma vez no StartDevice, numa página reservada fora do segmento, e lido no boot seguinte. Reconfirma o decaimento na era dos 256 MB sem tráfego em regime (a DX.66 está desarmada desde a P1).
4. **Linux como espelho:** `ramoops`/`pstore` e `umr` dão o que o Windows não dá. Se o Linux travar com `dc=0`, a investigação passa para lá.
5. **Eventos WHEA/BERT** do boot seguinte já foram vistos vazios (docs 502–505). Não insistir.
6. **Não adianta:** KD/Ctrl+Break, dump, ETW (memória `bc250-kd-nao-para-a-trava-dura`).

---

## 7. Diferenças com o amdgpu que faltam no inventário do doc 593

| Item | amdgpu (P1/P0) | Nós | Prioridade |
|---|---|---|---|
| TMR do PSP | fixado até o fim, PA da DRAM, `0xF800000` com 256 MB | liberado, PA da BAR | **1** |
| Dono do DCN | DC ativo (DCN 2.0.1), watermarks e p-state do driver | herdado do GOP | **2** (teste L1 no Linux; escrita em DCN só pela governança P9) |
| `nbio_v2_3_init_registers` | `PCIE_CONFIG_CNTL.CI_SWUS_MAX_READ_REQUEST_SIZE_MODE/PRIV=1` | não escrito (P1, não achei) | 3 (barato, afeta DMA de leitura da memória de sistema) |
| Doorbell selfring aperture | ligada no `late_init` | não (doc 600) | 4 (só importa para doorbell iniciado pela GPU) |
| VM de 4 níveis, 48 bits | sim | 2 níveis, 34 bits | 5 (P5, cara) |

---

## 8. Recuperação e pendências

- Nada foi alterado no código nem em pacotes nesta auditoria. Nenhuma corrida.
- **NÃO VALIDADO EM HARDWARE:** a aceitação do `SETUP_TMR` pelo PSP, a proteção do TMR, a causalidade de H1 e H2, e o comportamento do Linux com `dc=0`.
- **Próximo passo:** o Daniel decide entre TMR.1 (recomendado) e outra rota. Se for TMR.1, eu implemento, testo offline, empacoto 3.394.1.0 e escrevo os scripts da 249/250.
