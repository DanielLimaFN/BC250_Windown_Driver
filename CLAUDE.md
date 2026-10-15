# CLAUDE.md — Diretrizes de Operação para Claude Code

> **Governança de engenharia do repositório BC250-WinDv**
> *Target: AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) | Windows 10 22H2 x64*
> **Última revisão: 2026-09-25**

---

## 🎯 0. Onde queremos chegar

**Rodar jogos no Windows 10 22H2 na BC-250**, usando as 24 CUs e os 16 GB de GDDR6, com o
monitor ligado ao DisplayPort da própria placa, via D3D12/Vulkan, **sem WARP e sem
fallback de software**.

**Fonte única de verdade sobre o que já existe e o que falta:**
[`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) — Tabela 1 (já feito, com prova física) e
Tabela 2 (o que falta, em ordem de prioridade, com o teste que fecha cada item).
**Roadmap detalhado:** [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md).

**Decisão de arquitetura vigente:** display v1 é **por herança do UEFI GOP**. Não
implementamos DCN no caminho crítico. Escrita em registrador de DCN é **proibida** fora
da prioridade P9; leitura é permitida. **P9 aberta pelo Daniel em 12/10/2026 (DCN.1, [doc 710](docs/710_DCN1_FLIP_PELO_HUBP0_D0_A_D4_PACOTE_3418_E_CRITERIO_CORRIDA269.md)):** escrita permitida **só** nos registradores do flip do HUBP0 (`DCSURF_FLIP_CONTROL` e endereço primário), com a chave `DcnFlip` (padrão 0) e retorno ao endereço do GOP no teardown; timing, clock, PHY, formato, pitch e viewport continuam intocados. **Ampliada pelo Daniel em 12/10/2026 (DCN.2, [doc 713](docs/713_CORRIDA271_CONTA_FECHA_E_DCN2_SOBRE_O_GOP_PACOTE_3422_E_CRITERIO_CORRIDA272.md)):** também a interrupção VSTARTUP do OTG0 e a de flip do HUBP0, o CRC do OTG0, o cursor do HUBP0/DPP0, o AUX só durante a leitura do EDID, e o blank do OTG (DPMS), cada um com um bit da chave `DcnExt` (padrão 0) e retorno ao estado do GOP no teardown; timing, clock, PHY, formato, pitch e viewport continuam intocados. **DCN.3 (Daniel, 12/10/2026, [doc 714](docs/714_CORRIDA272_TELA_AZUL_0x133_AUTOTRAVAMENTO_EM_DIRQL_DIRQL1_DCN3_PACOTE_3423_E_CRITERIO_CORRIDA273.md)):** também a VUPDATE sem lock e a VLINE0 do OTG0 (`OTG_VERTICAL_INTERRUPT0_POSITION/CONTROL`) no bit IRQ, e a inicialização do AUX como `dcn10_aux_initialize` (`AUX_HPD_SEL`, `AUX_LS_READ_EN`, `AUX_DPHY_RX_CONTROL0`) só durante a leitura do EDID, com retorno ao estado do GOP.

**Estado em 01/10/2026:** **decisão do Daniel: VRAM mínima de 256 MB, sem VRAM grande como muleta**
([doc 551](docs/551_DECISAO_VRAM_MINIMA_256MB_E_CRITERIO_CORRIDA199.md)). Na BC-250 VRAM e RAM são a mesma
GDDR6; 256 MB é o BAR, então toda a VRAM fica visível à CPU, e a memória da carga real vem do segmento 2
(**M1b**, como o GTT do Linux). Quem manda na VRAM é o bloco `APCB` da CMOS, não o menu da BIOS
([doc 549](docs/549_COLETA_LINUX_CMOS_APCB_CONTROLA_A_VRAM_E_AMDGPU_SEM_IP_DISCOVERY.md)); a ferramenta é
`tools\vram-cmos\bc250-vram.ps1` ([doc 550](docs/550_CONTROLE_DA_VRAM_PELA_CMOS_NO_WINDOWS.md)), validada na placa.
O **M2** (segmento além do BAR) ficou **dormente** depois de provar o `Lock2` (197) e 2.086 imagens no segmento 3
(198). A 197 terminou em `0x10E`/`0x34` (Lock da UMD durante um TRANSFER) e a 198 na trava dura de sempre.
**Coleta Linux com 256 MB** ([doc 552](docs/552_COLETA_LINUX_256MB_AMDGPU_ATIVO_GABARITO_E_O_QUE_MUDA.md)):
o `amdgpu` subiu e **não travou** sob 149 despejos completos da VRAM (L-B), com firmware idêntico ao nosso. Dois
achados para o driver: a nossa RADV não se declara APU (`ids_flags` sem `FUSION`, e com a VRAM visível fixa em 16 GB: promete memória que o KMD não tem, [doc 554](docs/554_M1B1_RADV_NO_SEGMENTO_2_DORMENTE_E_CORRECAO_DO_HEAP.md)), e o
Linux usa SMU, SDMA1 e fences por interrupção, que nós não usamos.
**Corrida 199 (21:44, lab 3.346.1.0):** 256 MB sobe; trava dura ao abrir a Pesquisa, sem calor (80 °C), com SDMA1 desligado e sem erro de fabric no FCH ([doc 555](docs/555_CORRIDA199_256MB_SOBE_TRAVA_SEM_CALOR_SEM_ERRO_DE_FABRIC.md)). Placa em Código 43 pelo fusível: recuperação antes de novo bind. **Corrida 200 (22:14):** SDMA0 ocioso na parada, trava 2 s depois de o DWM ficar sem memória; nós desligamos o MGCG do SDMA0 e o Linux não ([doc 557](docs/557_CORRIDA200_SDMA0_OCIOSO_NA_PARADA_E_CG1_MGCG_DO_POST_CRITERIO_CORRIDA201.md)). **Corrida 201 (02/10 08:04, lab 3.348.1.0, CG.1):** primeira com 256 MB **sem trava dura** (591,8 s, rollback limpo); um TDR gráfico em VMID 10 recuperado ([doc 558](docs/558_CORRIDA201_PRIMEIRA_CORRIDA_256MB_SEM_TRAVA_DURA_MGCG_DO_POST.md)). **Corrida 202 (08:43, lab 3.349.1.0, boot frio):** **trava dura de novo** aos ~95 s com a CG.1 aplicada, 1,8 s depois de `res=-2` no DWM: a CG.1 não é causa suficiente ([doc 560](docs/560_CORRIDA202_TRAVA_DURA_VOLTOU_COM_CG1_CG1_NAO_E_SUFICIENTE.md)). A mineração dos finais (doc 560 §6) achou as três travas paradas até 20 ms depois do FILL de zeramento de uma alocação nova de tela cheia. **Corrida 203 (09:50, lab 3.350.1.0, X2b):** ensaio válido e **travou igual** ao abrir o menu: o `CONST_FILL` não é a causa ([doc 562](docs/562_CORRIDA203_X2B_VALIDO_TRAVOU_IGUAL_CONST_FILL_NAO_E_A_CAUSA.md)). A mineração (doc 563) achou as três travas cobertas com um processo no meio da leitura da VRAM pela CPU; no Linux as tabelas de página de VM gráfica são escritas pelo SDMA. **Próximo bind: Corrida 204**, 4b (a CPU não lê o buffer linear pela BAR, lab **3.351.1.0**, [doc 564](docs/564_4B_CPU_NAO_LE_A_VRAM_PELA_BAR_E_CRITERIO_CORRIDA204.md)), `tools/EXECUTAR_CORRIDA204_4B_CPU_SEM_LEITURA_DA_VRAM_600S.cmd`. **Corrida 204 (11:44):** 4b válido, **travou** ao abrir a Pesquisa com o zeramento de uma alocação nova em voo ([doc 565](docs/565_CORRIDA204_4B_VALIDO_TRAVOU_LEITURA_DA_CPU_NAO_BASTA.md)). Achado de código ([doc 566](docs/566_4C_DESENHO_PTE_PELA_GPU_COMO_O_LINUX_E_O_TESTE_DE_ORDEM.md)): com `CPU_VIRTUAL` o KMD escreve PTE e invalida a TLB pela CPU **no build**, fora de ordem com a GPU. **Decisão do Daniel: 4c completa em 5 etapas, uma corrida cada** ([doc 567](docs/567_4C_COMPLETA_EM_5_ETAPAS_E_CRITERIO_CORRIDA205.md)). **Corrida 205 (15:03, lab 3.352.1.0, 4c.1):** desfecho C ([doc 568](docs/568_CORRIDA205_4C1_ANEL_PROVADO_IB_TRAVA_NO_SRBM_WRITE_600S_SEM_TRAVA_DURA.md)): invalidação de TLB pelo SDMA **provada no anel**; dentro de IB o SDMA para no `SRBM_WRITE`. ~594 s sem trava dura (não atribuível à 4c, runtime igual ao da 204), três reinícios do DWM por `0xC01E0200`. **4c.2 pronta** ([doc 569](docs/569_4C2_FLUSH_TLB_PELO_SDMA_NO_ANEL_E_CRITERIO_CORRIDA206.md)): `FlushTlb` pelo SDMA no anel, antes do IB seguinte, sem MMIO no build. **Corrida 206 (15:36, lab 3.353.1.0):** flush pelo anel rodou >1.024 vezes sem fault, e BugCheck `0x10E`/`0x34` (mesmos argumentos da 197) ao abrir o menu ([doc 570](docs/570_CORRIDA206_4C2_FLUSH_NO_ANEL_RODOU_E_BUGCHECK_10E_34_COMO_NA_197.md)). Pilha igual à da 197: `Lock` da UMD no buffer de vértices dinâmico do DWM. **L1 + instrumentação prontas** ([doc 571](docs/571_L1_BUFFER_SEM_LOCK_CONTRA_0x10E_34_INSTRUMENTACAO_E_CRITERIO_CORRIDA207.md)): buffer com `VkBuffer` sem `Lock` de upload, registro BPG1 de paginação, `LockUs`, `cdb` no coletor. **Corrida 207 (21:55, lab 3.354.1.0):** desfecho B ([doc 572](docs/572_CORRIDA207_L1_SEM_0x34_TRAVA_DURA_AOS_96S_NO_ZERAMENTO_COM_4C2_ARMADA.md)): L1 aplicada, sem `0x10E`, 4c.2 emitindo, e **trava dura aos ~96 s** no zeramento de alocações novas de tela cheia. A invalidação da TLB pela CPU sai da lista de causas suficientes. **4c.3 pronta** ([doc 573](docs/573_4C3_GATES_PELO_JOURNAL_SEM_LER_TABELA_PELA_BAR_E_CRITERIO_CORRIDA208.md)): gates pelo journal, sem a CPU ler tabela de página ou IB pela BAR. **Próximo bind: Corrida 208**, lab **3.355.1.0**, `tools/EXECUTAR_CORRIDA208_4C3_GATES_PELO_JOURNAL_600S.cmd`; rodar `coletar-pos-trava.ps1` ANTES da recuperação se travar. **Corrida 208 (22:29):** desfecho B ([doc 574](docs/574_CORRIDA208_4C3_JOURNAL_RESOLVEU_99_95_E_TRAVA_DURA_NO_ZERAMENTO.md)): journal resolveu 6.177/6.180 gates, zero fault, um TDR gráfico recuperado, e **trava dura aos ~290 s** no zeramento sem caminhada pela BAR. A leitura de tabela pela CPU sai das causas suficientes; próximo: **4c.4** (PTE pelo SDMA) com o journal falhando fechado no `NO_SPACE`. **4c.4 pronta** ([doc 575](docs/575_4C4_PTE_PELO_SDMA_IB_DIVIDIDO_NO_ANEL_E_CRITERIO_CORRIDA209.md)): `GPU_VIRTUAL`, `WRITE_LINEAR` no paging buffer, IB dividido no anel em cada `FlushTlb`. **Corrida 209 (23:13):** desfecho C ([doc 576](docs/576_CORRIDA209_4C4_RODOU_PONTO_DE_DIVISAO_NO_IB_ERRADO_E_CRITERIO_CORRIDA210.md)): `GPU_VIRTUAL` e PTE pelo SDMA rodaram, mas o VidMm monta buffers à frente e a minha regra deu pontos de divisão ao IB errado (3 timeouts `INSIDE_IB`, trava antes do desktop). Corrigido. **Corrida 210 (03/10 22:23, lab 3.357.1.0):** desfecho B ([doc 577](docs/577_CORRIDA210_4C4_PTE_PELO_SDMA_ESTAVEL_DESKTOP_DESENHADO_TRAVA_DURA_AOS_228S.md)): PTE pelo SDMA estável (20.071 IBs, zero timeout/TDR/fault, desktop desenhado) e **trava dura aos ~228 s** em rajada de despejo de tela cheia. A 4c inteira sai das causas suficientes; próximo: diferenças restantes com o Linux (L-B). Mineração ([doc 578](docs/578_MINERACAO_CORRIDA210_FALTA_DE_MEMORIA_NO_SEGMENTO1_CASCATA_DO_DWM_E_O_QUE_SOBRA_DA_TRAVA.md)): a RADV presa ao segmento 1 esgota os 256 MB e derruba o DWM em cascata. **M1b.1 ligada** ([doc 579](docs/579_M1B1_LIGADA_RADV_NO_SEGMENTO2_E_CRITERIO_CORRIDA211.md)). **Corrida 211 (04/10 15:35):** M1b.1 ativa, ~380 s, tela azul por IB dividido recusado por tamanho, defeito meu ([doc 580](docs/580_CORRIDA211_M1B1_ATIVA_TELA_AZUL_POR_RECUSA_DE_IB_DIVIDIDO_E_PEDACOS_DE_ANEL.md)); corrigido com pedaços de anel. **Corrida 212 (04/10 16:07, lab 3.360.1.0):** desfecho B ([doc 581](docs/581_CORRIDA212_SEM_FALTA_DE_MEMORIA_SEM_DEFEITO_DE_HOST_TRAVA_DURA_AOS_259S.md)): sem falta de memória, um DWM, pedaços de anel OK, e **trava dura aos ~259 s** no zeramento. **Mineração (doc 582):** nas nove travas o último tique do rastro tem a GPU ociosa e todas as leituras voltando; a máquina morre entre duas observações, e o fim do log é a última drenagem, não o último evento. SMU e SDMA1 já estavam fora; sobram a conclusão por interrupção e uma diferença nova, anel e fences em memória de sistema no Linux (nós: VRAM pela BAR, polling de 1 ms). **D2 pronta** ([doc 583](docs/583_D2_ANEL_E_WORK_DO_SDMA0_NA_RAM_PELA_ABERTURA_AGP_E_CRITERIO_CORRIDA213.md)): anel e Work do SDMA0 na RAM não cacheada pela abertura AGP (correção: o Linux no BC-250 usa GART, não AGP). Corridas 213/214: AGP refutada (o SDMA0 trava na 1a leitura, [doc 586](docs/586_CORRIDA214_LOG1_FUNCIONOU_AGP_TRAVA_O_SDMA0_NA_PRIMEIRA_LEITURA.md)); log de StartDevice com falha agora sobrevive (LOG.1). **D2 por GART da VMID 0 pronta** ([doc 587](docs/587_D2_POR_GART_DA_VMID0_COMO_O_LINUX_E_CRITERIO_CORRIDA215.md)). **Corrida 215:** GART ligou e provou, primeiro IB deu fault na VMID 0 e o TDR caiu em tela azul ([doc 588](docs/588_CORRIDA215_GART_LIGOU_PRIMEIRO_IB_FAULT_NA_VMID0_E_TELA_AZUL_NO_TDR.md)). **Corrida 216 (22:08, lab 3.364.1.0):** desfecho A ([doc 589](docs/589_CORRIDA216_TDR_VOLTOU_A_VRAM_MEC_REESCREVE_A_VMID0_600S_SEM_TRAVA_E_LACO_NO_ROLLBACK.md)). O nosso MEC reescreve o contexto 0 (raiz em MC sem VALID); o TDR voltou para a VRAM sem tela azul; 600 s sem trava dura com a VMID 0 desligada (H); e laço infinito nosso no rollback (contador global da 4c.4). **LC.1 + V0.1 prontas** ([doc 590](docs/590_LC1_ROLLBACK_SEM_LACO_V01_VMID0_UNICA_E_CRITERIO_CORRIDA217.md)): recusa por buffer, e o MEC na tabela da GART, sem tocar no contexto 0. Correção: a tabela do MEC traduz (P0), então a H da VMID 0 fica sem mecanismo. **Corrida 217 (05/10 09:29, lab 3.365.1.0):** V0.1 OK (MEC na GART, contexto 0 intacto); a D2 caiu depois de 117 IBs com fence invisível na RAM (anel drenado, sem fault), e houve trava dura aos ~4 min sem a D2 ([doc 591](docs/591_CORRIDA217_VMID0_UNICA_OK_D2_CAI_COM_FENCE_INVISIVEL_NA_RAM_E_TRAVA_DURA_AOS_4MIN.md)). **UC.1 pronta** ([doc 592](docs/592_UC1_PTE_DA_GART_COM_MTYPE_UC_COMO_O_LINUX_E_CRITERIO_CORRIDA218.md)): a PTE de RAM da GART passa a ter MTYPE_UC, como o Linux (a nossa era NC). **Corrida 218 (10:08, lab 3.366.1.0):** UC.1 OK e a D2 de pé (4.193 IBs pela RAM, zero timeout); trava dura igual aos ~123 s. A D2 sai das causas ([doc 593](docs/593_CORRIDA218_UC1_FUNCIONOU_D2_DE_PE_TRAVA_DURA_IGUAL_E_INVENTARIO_DE_PARIDADE_COM_O_AMDGPU.md)). **Decisão do Daniel: paridade com o amdgpu, sem economizar corridas**; inventário no doc 593 §4. **P1 pronta** ([doc 594](docs/594_P1_SILENCIO_DA_BAR_COLETA_AUTOMATICA_NO_BOOT_E_CRITERIO_CORRIDA219.md)): silêncio da BAR, mais a coleta automática no boot (`tools/registrar-coleta-automatica.ps1`, uma vez, elevado). **Corrida 219 (11:08, lab 3.367.1.0):** P1 válida e trava igual aos ~139 s, sem rajada de paginação; a instrumentação sai das causas. A coleta automática funcionou, e o teste de RAM (`tools\rodar-teste-de-ram.ps1`) deu 8 GB sem erro ([doc 595](docs/595_CORRIDA219_P1_VALIDA_TRAVA_IGUAL_COLETA_AUTOMATICA_FUNCIONOU_E_TESTE_DE_RAM.md)). **P7 pronta** ([doc 596](docs/596_P7_ANEL_E_FENCES_DO_GFX_EM_GTT_COMO_O_AMDGPU_E_CRITERIO_CORRIDA220.md)): anel e fences do GFX em GTT pela GART. **Corrida 220 (11:38, lab 3.368.1.0):** P7 válida e trava igual aos ~137 s; o HDP por MMIO roda em toda submissão do SDMA0, então a P1b segue aberta ([doc 597](docs/597_CORRIDA220_P7_VALIDA_GFX_PELA_GTT_TRAVA_IGUAL_AOS_137S.md)). **P2 pronta** ([doc 598](docs/598_P2_WPTR_DO_SDMA0_POR_DOORBELL_COMO_O_AMDGPU_E_CRITERIO_CORRIDA221.md)): WPTR do SDMA0 por doorbell, como o amdgpu. **Corrida 221 (13:17, lab 3.369.1.0):** desfecho C: a doorbell armada com o anel vivo não chegou ao SDMA0 (readback ok, prova falhou, fallback por MMIO OK), e houve trava igual aos ~99,6 s sem a P2 ([doc 599](docs/599_CORRIDA221_DOORBELL_ARMADA_COM_ANEL_VIVO_NAO_CHEGOU_P2B_E_CRITERIO_CORRIDA222.md)). **P2b pronta:** a doorbell é programada com o F32 parado, dentro da reconstrução, como `sdma_v5_0_gfx_resume`. **Corrida 222 (14:09, lab 3.370.1.0):** a P2b também não chega (hipótese da posição refutada); **600 s sem trava dura** e rollback limpo, não atribuível (configuração igual à da 221); primeira prova do teardown da P7/D2/P2. Achado: nenhuma doorbell funciona, nem a do MEC; falta ligar a abertura do BIF (`RCC_DOORBELL_APER_EN`), como `nv_common_hw_init` ([doc 600](docs/600_CORRIDA222_600S_SEM_TRAVA_DOORBELL_NAO_CHEGA_ABERTURA_DO_BIF_P2C_E_CRITERIO_CORRIDA223.md)). **Corrida 223 (14:29, lab 3.371.1.0):** a doorbell funciona pela primeira vez (abertura do BIF; o MEC com `HitObserved=1`), e a trava veio igual aos ~338 s: a P2 sai das causas. Artefatos no app Clima por superfície compartilhada entre processos recusada, uma lacuna funcional separada ([doc 601](docs/601_CORRIDA223_DOORBELL_ATIVA_PELA_PRIMEIRA_VEZ_TRAVA_AOS_338S_E_ARTEFATOS_DO_CLIMA.md)). **P1b pronta** ([doc 602](docs/602_P1B_CPU_NA_VRAM_PELA_DRAM_SEM_HDP_COMO_O_AMDGPU_EM_APU_E_CRITERIO_CORRIDA224.md)): CPU na VRAM pela DRAM e sem HDP, como o amdgpu em APU. **Corrida 224 (15:14, lab 3.372.1.0):** a P1b não armou porque a prova leu uma página preta (zero contra zero, defeito meu); travou igual aos ~189 s ([doc 603](docs/603_CORRIDA224_P1B_NAO_ARMOU_PAGINA_PRETA_NA_PROVA_P1B1_E_CRITERIO_CORRIDA225.md)). **P1b.1:** prova por escrita. **Corrida 225 (15:39, lab 3.373.1.0):** a P1b armou (BAR e DRAM provadas iguais), mas o `DxgkCbMapMemory` recusou a DRAM e o StartDevice caiu em Código 43, defeito meu ([doc 604](docs/604_CORRIDA225_P1B_ARMOU_DXGKCBMAPMEMORY_RECUSOU_A_DRAM_P1B2_E_CRITERIO_CORRIDA226.md)). **P1b.2:** mapeamento pela DRAM com `MmMapIoSpaceEx`. **Corrida 226 (16:15, lab 3.374.1.0):** P1b ativa em regime (VRAM pela DRAM, zero HDP) e **600 s sem trava dura**, 20.838 IBs, teardown limpo ([doc 605](docs/605_CORRIDA226_P1B_ATIVA_600S_SEM_TRAVA_DURA_E_CRITERIO_DA_REPETICAO.md)); conta só com repetição. **Corrida 227 (16:57, repetição):** P1b ativa e **tela azul `0x119/0x9000`** no flip MPO3: a cópia SDMA da primária falhou e o nosso fail-closed devolveu erro ao `dxgmms2`; o motivo se perdeu no bugcheck ([doc 606](docs/606_CORRIDA227_REPETICAO_P1B_TELA_AZUL_0x119_9000_NO_FLIP_MPO3.md)). **FB.1 pronta** ([doc 607](docs/607_FB1_MOTIVO_DA_FALHA_DO_BLT_SOBREVIVE_AO_BUGCHECK_E_CRITERIO_CORRIDA228.md)): motivo do blt em `g_Bc250SdmaLastBltFailure` e anel de log extraído do dump (`tools/extrair-anel-do-dump.ps1`). O anel extraído do dump da 227 deu a causa: corrida da fence compartilhada entre o flip e a composição (`FenceObs` posterior, engine ociosa), defeito meu. **FB.2:** a fence é numerada dentro do lock e o esperador aceita fence posterior. **Corrida 228 (22:47, lab 3.375.1.0):** P1b ativa e **trava dura aos ~454 s**, igual; a P1b não é causa suficiente ([doc 608](docs/608_CORRIDA228_P1B_E_FB2_ATIVAS_TRAVA_DURA_AOS_454S_P1B_NAO_BASTA.md)). **P3.0 pronta** ([doc 609](docs/609_P3_INTERRUPCOES_PELO_IH_O_IH_E_CLIENTE_DO_MMHUB_P30_E_CRITERIO_CORRIDA229.md)): o IH é cliente do MMHUB e o driver não tem GART no MMHUB, então o IH fica desligado; a etapa mede MSI e fotografa o MMHUB. **Corrida 229 (06/10 08:40, lab 3.376.1.0):** desfecho A: MSI entregue, MMHUB com a VMID 0 desligada e sem L2 (P0), e **600 s sem trava dura, sem abrir o menu** (não comparável à 228) ([doc 610](docs/610_CORRIDA229_P30_MSI_ENTREGUE_MMHUB_FOTOGRAFADO_600S_SEM_TRAVA_DURA.md)). **P3.0b pronta** ([doc 611](docs/611_P3_0B_GART_DA_VMID0_NO_MMHUB_COMO_O_AMDGPU_E_CRITERIO_CORRIDA230.md)): GART da VMID 0 no MMHUB sobre a tabela da D2, AGP do MMHUB fechada (vinha cobrindo MC 0..16 MB), IH ainda desligado. **Corrida 230 (09:45, lab 3.377.1.0):** a GART do MMHUB ligou e derrubou o probe do PSP (cliente do MMHUB, buffers em RAM física): Código 43, sem trava ([doc 612](docs/612_CORRIDA230_GART_DO_MMHUB_LIGOU_E_DERRUBOU_O_PSP_P30B1_E_CRITERIO_CORRIDA231.md)). **P3.0b.1:** P3 depois do último uso do PSP e teardown com transporte remapeado. **Corrida 231 (10:00, lab 3.378.1.0):** PSP OK com a GART do MMHUB armada depois dele; desktop ~325 s, TDR do GFX (VMID 10) recuperado aos ~315 s e **trava dura aos ~325 s** ([doc 613](docs/613_CORRIDA231_GART_DO_MMHUB_EM_REGIME_PSP_OK_TDR_GFX_RECUPERADO_E_TRAVA_DURA_AOS_325S.md)). **P3.1 pronta** ([doc 614](docs/614_P3_1_IH_LIGADO_TRAP_DO_SDMA0_E_CRITERIO_CORRIDA232.md)): IH ligado com TRAP do SDMA0 e ISR, conclusão ainda por polling. **Corrida 232 (10:58, lab 3.379.1.0):** IH vivo (2.341 TRAPs = 2.341 vetores = 2.341 ISR, zero overflow) e trava dura aos ~77 s ([doc 615](docs/615_CORRIDA232_IH_VIVO_EM_REGIME_TRAPS_1_PARA_1_E_TRAVA_DURA_AOS_77S.md)). **P3.2 pronta** ([doc 616](docs/616_P3_2_CONCLUSAO_DO_SDMA0_DISPARADA_PELA_INTERRUPCAO_E_CRITERIO_CORRIDA233.md)): o TRAP do SDMA0 dispara o DPC de aposentadoria, timer como rede de 10 ms. **Corrida 233 (11:35, lab 3.380.1.0):** conclusão por interrupção em regime (10.931 DPCs pelo ISR, zero TDR/timeout) e trava dura aos ~584 s: a P3 sai das causas ([doc 617](docs/617_CORRIDA233_CONCLUSAO_POR_INTERRUPCAO_EM_REGIME_584S_E_TRAVA_DURA.md)). **P6 + EOP do CP prontas** ([doc 618](docs/618_P6_MTYPE_UC_E_EOP_DO_CP_E_CRITERIO_CORRIDA234.md)): MTYPE_UC nos BOs `GL2_BYPASS` da RADV (RADV nova `C6BEC54B`, exige KMD 3.381+) e conclusão do GFX pela interrupção. **Corrida 234 (13:48, lab 3.381.1.0):** EOP do CP 1 para 1 (1.236 vetores e DPCs do GFX) e P6 em regime, zero TDR/fault, e trava dura aos ~136 s: as duas saem das causas ([doc 619](docs/619_CORRIDA234_P6_E_EOP_DO_CP_EM_REGIME_TRAVA_DURA_AOS_136S.md)). Placa em Código 43 pelo fusível, rollback do watchdog não rodou: `BC250_RECOVERY.cmd`, `inspect-boot-fuse.ps1 -Reset` e boot frio. Próximo da paridade: **P4** (SDMA1). **P4.1 pronta** ([doc 620](docs/620_P4_1_SDMA1_VIVO_E_OCIOSO_COMO_O_AMDGPU_E_CRITERIO_CORRIDA235.md)): SDMA1 vivo e ocioso como o `amdgpu` (firmware pelo PSP tipo 10, anel provado na RAM pela GART). **Corrida 235 (14:30, lab 3.382.1.0):** SDMA1 vivo e provado (1 poll), ocioso, zero TDR/fault, e trava dura aos ~267 s: o estado do SDMA1 sai das causas ([doc 621](docs/621_CORRIDA235_P41_SDMA1_VIVO_E_TRAVA_DURA_AOS_267S.md)). Placa em Código 43; recuperação manual. **P4.2 pronta** ([doc 622](docs/622_P4_2_PTE_NO_SDMA1_COM_TROCA_ENTRE_ANEIS_E_CRITERIO_CORRIDA236.md)): escritas de PTE no SDMA1 com troca FENCE/POLL entre os anéis, paginação no SDMA0, como o `amdgpu`. **Corrida 236 (15:37, lab 3.384.1.0):** P4.2 em regime (8.414 faixas de PTE no SDMA1, Done = Go em todas as linhas, zero TDR/fault) e trava dura aos ~45 s: a divisão de trabalho entre os SDMA sai das causas ([doc 623](docs/623_CORRIDA236_P42_PTE_NO_SDMA1_EM_REGIME_E_TRAVA_DURA_AOS_45S.md)). Placa em Código 43; recuperação manual antes de qualquer bind. **Passagem de bastão para a próxima IA: [doc 624](docs/624_PASSAGEM_DE_BASTAO_2026-10-06_TRAVA_DURA_DEPOIS_DA_PARIDADE_P4.md)** (leia primeiro). **Depois do 624 ([doc 625](docs/625_RESET_REASON_RESPONDIDO_POR_MINERACAO_W1_E_CRITERIO_CORRIDA237_PARADO.md)):** RESET_REASON respondido por mineração (bits 30/10 fixos, sem sync flood); próximos: W1 (`tools/EXECUTAR_W1_PLATAFORMA_SEM_DRIVER.cmd`) e **Corrida 237**, mesmo 3.384.1.0 parado no desktop (`tools/EXECUTAR_CORRIDA237_PARADO_600S.cmd`). **W1 (16:32):** 45 min sem trava nem erro sem o nosso driver. **Corrida 237 (18:52):** parada, 600 s sem trava dura, rollback limpo e teardowns de IH/SDMA1/EOP/MMHUB provados ([doc 626](docs/626_CORRIDA237_PARADO_600S_SEM_TRAVA_TEARDOWNS_PROVADOS_E_CRITERIO_CORRIDA238.md)); **Corrida 238 (21:54):** ~600 s sem trava, mas não foi parada (Configurações, Edge, Claude e Pesquisa abriram; boot por reinício): desfecho C ([doc 627](docs/627_CORRIDA238_NAO_FOI_PARADA_600S_SEM_TRAVA_COM_USO_LEVE_E_PROXIMO_PASSO.md)). **Corrida 239 pronta** ([doc 628](docs/628_CRITERIO_CORRIDA239_SO_O_MENU_INICIAR.md)): só o menu Iniciar, mesmo pacote, `tools/EXECUTAR_CORRIDA239_SO_MENU_600S.cmd`; o preflight recusa reinício e apps abertos. **Corrida 239 (07/10 00:06):** desfecho B ([doc 629](docs/629_CORRIDA239_SO_MENU_TRES_TDR_DO_GFX_E_TRAVA_DURA_22S_DEPOIS.md)): a 1ª submissão de um device novo do menu travou o GFX (três TDRs recuperados em ~16 s) e a trava dura veio 22 s depois, com a GPU ociosa. **Mineração dos TDRs ([doc 630](docs/630_MINERACAO_DOS_TDR_DO_GFX_SQ_ERROS_DEPOIS_DO_RESET_E_RLC_SEM_SAFE_MODE.md)):** o GFX trava no 1º IB depois de troca de VMID; depois do nosso reset os shader engines disparam erros do SQ (992 na 239) e ondas ficam presas; o RLC nunca confirmou safe mode (0/41); o nosso reset não é o do amdgpu. **Corrida 240 pronta** ([doc 631](docs/631_INSTRUMENTACAO_RLC_IH_IB_DO_MENU_E_CRITERIO_CORRIDA240.md)): lab **3.385.1.0** + RADV `7B82C4AE`, só instrumentação (RLC, IH, IB do menu), mesmo roteiro da 239, `tools/EXECUTAR_CORRIDA240_INSTRUMENTACAO_MENU_600S.cmd`. Achado: o `pfp_direct_load` devolve `RLC_CNTL` a 0, o RLC fica parado em regime (P1). **Corrida 240 (09:16):** RLC parado confirmado (P0); o TDR vem do **preâmbulo da RADV** do menu, que encadeia com VMID 7 fixa e sem VALID (mudança nossa sem commit na RADV, diferente do upstream); trava dura 9 s depois ([doc 632](docs/632_CORRIDA240_RLC_PARADO_CONFIRMADO_PREAMBULO_DO_MENU_E_ENCADEAMENTO_DA_RADV_COM_VMID7.md)). **Corrida 241 pronta** ([doc 633](docs/633_ENCADEAMENTO_GFX_DA_RADV_COMO_O_UPSTREAM_E_CRITERIO_CORRIDA241.md)): mesmo KMD 3.385.1.0 + RADV `DFD8D120` com encadeamento GFX upstream, só o menu, `tools/EXECUTAR_CORRIDA241_ENCADEAMENTO_UPSTREAM_MENU_600S.cmd`. **Corrida 241 (10:04):** **inválida** ([doc 634](docs/634_CORRIDA241_INVALIDA_O_WINSYS_WDDM2_REESCREVE_O_ENCADEAMENTO.md)): o preâmbulo do menu saiu com `0x075040D0` (VMID 7, bit 22, sem VALID), porque `radv_wddm2_cs.c` reescreve o encadeamento depois do header (defeito meu); 600 s sem TDR nem trava, mas com o menu ativo só por ~1 min. **241b e P3.3 prontas** ([doc 635](docs/635_241B_ENCADEAMENTO_NO_WDDM2_E_P33_RPTR_DO_IH_POR_DOORBELL_CRITERIOS_241B_E_242.md)): 241b = mesmo KMD 3.385.1.0 + RADV `B4AE2E14` (override do `wddm2` removido), `tools/EXECUTAR_CORRIDA241B_ENCADEAMENTO_UPSTREAM_WDDM2_MENU_600S.cmd`, menu até o rollback; P3.3 (RPTR do IH por doorbell 0x2F0) compilada no SYS lab `825AC196`, empacotada como 3.386.1.0 só depois da 241b, para a 242. **Corrida 241b (11:33):** desfecho B ([doc 636](docs/636_CORRIDA241B_ENCADEAMENTO_UPSTREAM_SEM_TDR_DO_MENU_E_TRAVA_DURA_AOS_5MIN.md)): preâmbulo do menu com `0x0090xxxx` (CHAIN|VALID), 5 devices novos do menu **sem TDR do GFX**, e trava dura aos ~5,5 min com a assinatura de sempre: TDR e trava são problemas separados. **Corrida 242 pronta:** lab **3.386.1.0** (P3.3, SYS `382C007B`) + RADV `B4AE2E14`, só o menu até o rollback, `tools/EXECUTAR_CORRIDA242_P33_IH_RPTR_DOORBELL_MENU_600S.cmd`. **Corrida 242 (14:21):** desfecho E ([doc 637](docs/637_CORRIDA242_P33_ATIVA_E_TRAVA_DURA_EM_10S_ANTES_DO_DESKTOP.md)): P3.3 ativa e provada (RPTR do IH pela doorbell == registrador), e **trava dura em ~10–15 s, antes do primeiro quadro** (tela preta): a P3.3 é a principal suspeita (n=1). Placa em Código 43; recuperação e repetição da 242. **Corrida 242b (15:22, repetição):** P3.3 **em regime** (1.693 RPTR pela doorbell, 1:1 com o ISR, desktop desenhado) e trava dura de novo em ~20 s ([doc 638](docs/638_CORRIDA242B_P33_EM_REGIME_E_TRAVA_DURA_EM_20S_P33_DESLIGADA.md)): pela regra do doc 637 a **P3.3 foi desligada** (chave 0, divergência declarada); correlação forte (n=2) entre o RPTR do IH pela doorbell e a trava precoce, mecanismo H. **Mineração ([doc 639](docs/639_MINERACAO_P33_TAXAS_IGUAIS_E_FAIXA_DE_DOORBELL_DO_MEC_DIFERENTE_DO_AMDGPU.md)):** taxas iguais (não é volume); `CP_MEC_DOORBELL_RANGE` herdado em 0x110–0x0FFFFFFC contra 0–0x450 no amdgpu, cobrindo as doorbells do SDMA0 e do IH; proposta MEC.1. **Corrida 243 pronta** ([doc 640](docs/640_MEC1_FAIXA_DE_DOORBELL_DO_MEC_COMO_O_AMDGPU_E_CRITERIO_CORRIDA243.md)): lab **3.387.1.0** (SYS `E8125C34`) = MEC.1 (`CP_MEC_DOORBELL_RANGE_UPPER` 0x450, LOWER intocado) com a P3.3 religada + RADV `B4AE2E14`; só o menu até o rollback, `tools/EXECUTAR_CORRIDA243_MEC1_FAIXA_DOORBELL_MENU_600S.cmd`. **Corrida 243 (17:08):** desfecho B ([doc 641](docs/641_CORRIDA243_MEC1_ARMADA_E_TRAVA_DURA_AOS_49S_DESFECHO_B.md)): MEC.1 armada (UPPER 0x450 lido; o RB do GFX é 0..0x108), P3.3 em regime, e trava dura aos ~49 s: a faixa do MEC sai das causas da trava precoce. P3.3 de volta a 0; MEC.1 fica. Placa em Código 43; recuperação. **Corrida 244 pronta** ([doc 642](docs/642_RLC1_RLC_RODANDO_E_GFXRST1_RESET_DO_GFX_COMO_O_AMDGPU_E_CRITERIO_CORRIDA244.md), decisão do Daniel de juntar três mudanças): lab **3.388.1.0** (SYS `DABCBD0C`) = P3.3=0 + RLC.1 (firmware do RLC pelo PSP e `rlc_resume`, RLC rodando; até aqui ele ficava na ROM) + GFXRST.1 (`gfx_v10_0_soft_reset` no TDR do GFX) + RADV `B4AE2E14`, só o menu, `tools/EXECUTAR_CORRIDA244_RLC1_GFXRST1_MENU_600S.cmd`. Achado: 30 de 34 golden settings nunca aplicados (G.1, próximo). **Corrida 244 (22:10):** desfecho B1 ([doc 643](docs/643_CORRIDA244_RLC_RODANDO_PELA_PRIMEIRA_VEZ_TRAVA_AOS_40S_B1_E_CRITERIO_CORRIDA245.md)): RLC rodando o firmware pela primeira vez (`EXEC_ROM_CODE` 1→0, `RLC_GPM_STAT=0x00B40016` igual ao Linux), P3.3 desligada, zero TDR, e trava aos ~40 s. **Corrida 245 pronta:** lab **3.389.1.0** (SYS `6623270E`) = 244 sem a RLC.1, `tools/EXECUTAR_CORRIDA245_SEM_RLC1_MENU_600S.cmd`. **Corrida 245 (22:25):** desfecho A ([doc 644](docs/644_CORRIDA245_RLC_NA_ROM_TRAVA_VOLTA_AOS_5MIN_DESFECHO_A.md)): RLC na ROM, P3.3 em 0, ~289 s, 12.254 IBs, zero TDR, e trava dura de sempre. P3.3 e RLC rodando encurtam a vida da máquina (n=1 cada); RLC.1 fica em 0. Próximo proposto: G.1 (golden settings) ou RLC.1b (handshake RLC↔SMU desligado). **Corrida 246 pronta** ([doc 645](docs/645_G1_GOLDEN_SETTINGS_DO_CYAN_SKILLFISH_COMO_O_AMDGPU_E_CRITERIO_CORRIDA246.md)): lab **3.390.1.0** (SYS `55255FB0`) = 245 + G.1 (34 golden settings do Cyan Skillfish como `gfx_v10_0_init_golden_registers`, antes do PSP/SDMA0/CP), `tools/EXECUTAR_CORRIDA246_G1_GOLDEN_MENU_600S.cmd`. **Corrida 246 (22:49):** desfecho B1 ([doc 646](docs/646_CORRIDA246_G1_APLICADA_TRAVA_AOS_33S_B1_E_O_PROBLEMA_DA_DISPERSAO.md)): 29 goldens aplicados e conferidos (snapshot 30→2 diferentes), zero TDR, e trava aos ~33 s. A dispersão sem mudança (45–584 s) torna RLC.1 e G.1 inconclusivas com n=1; a G.1 fica para a repetição da 246 (mesmo pacote). **Corrida 246b (23:08, mesmo pacote, KD ativo):** ~323 s, 13.849 IBs, zero TDR, trava de sempre ([doc 647](docs/647_CORRIDA246B_G1_COM_KD_TRAVA_AOS_5MIN_A_246_FOI_DISPERSAO_E_O_VKD3D_NO_WEBVIEW.md)): a 246 foi dispersão e a G.1 fica. Os avisos de assinatura do `libvkd3d-shader-1.dll` no WinDbg vieram do WebView2 pelo ICD global, antes do bind; sem relação com a trava. **Corrida 247 pronta** ([doc 648](docs/648_RLC1_SOBRE_A_G1_REPETICAO_DA_244_E_CRITERIO_CORRIDA247.md)): lab **3.391.1.0** (SYS `58EEB2FB`) = 246 + RLC.1 religada, sem KD, `tools/EXECUTAR_CORRIDA247_RLC1_SOBRE_G1_MENU_600S.cmd`. **Corrida 247 (08/10 08:18, com KD):** desfecho A ([doc 649](docs/649_CORRIDA247_RLC_RODANDO_SOBRE_A_G1_108S_REAIS_DESFECHO_A_COM_KD.md)): RLC rodando e G.1 armados, ~108 s reais (133,7 s menos ~25 s de pausas do depurador), zero TDR, trava de sempre ~4 s depois da última pausa. A RLC.1 fica; Código 43 pelo fusível. **Corrida 248 pronta** ([doc 650](docs/650_RLC1B_HANDSHAKE_SMU_DESLIGADO_E_LOGMIN1_LOG_MINIMO_DA_UMD_CRITERIO_CORRIDA248.md), duas mudanças por decisão do Daniel): lab **3.392.1.0** (SYS `5EB4F626`, UMD `B73F9BDA`) = 247 + RLC.1b (`RLC_PG_CNTL` bit 23 = 1, handshake RLC↔SMU desligado) + LOGMIN.1 (log mínimo da UMD), `tools/EXECUTAR_CORRIDA248_RLC1B_LOGMIN_COM_KD_MENU_600S.cmd`. **Corrida 248 (08/10 09:07):** desfecho B1 ([doc 651](docs/651_CORRIDA248_RLC1B_ATIVA_TRAVA_EM_8S_B1_E_LOGS_DA_UMD_ZERADOS.md)): RLC.1b ativa (bit 23 lido em 1, 6/6) e trava em ~8 s, a mais rápida já medida; snapshots da UMD zerados pela trava (LOGMIN não verificável). **Corrida 248b pronta** (doc 651 §4): lab **3.393.1.0** (SYS `B71F505C`) = 248 sem a RLC.1b, `tools/EXECUTAR_CORRIDA248B_SEM_RLC1B_LOGMIN_COM_KD_MENU_600S.cmd`. **Corrida 248b (10:01):** handshake ligado (RLC.1b fora) e trava em ~22 s: a RLC.1b não explica a 248. Com RLC rodando: 40/108/8/22 s; com RLC na ROM: 289/33/228 s. **Auditoria externa pedida pelo Daniel:** prompt em [doc 652](docs/652_PROMPT_AUDITORIA_EXTERNA_TRAVA_DURA_CORRIDAS_241B_A_248B.md). **Auditorias e plano** ([doc 658](docs/658_PLANO_POS_AUDITORIAS_CHECKLIST_E_CORRIDA249.md)): defeito confirmado, o TMR do PSP era devolvido ao alocador e ia com a PA da BAR. **Corrida 249 pronta:** lab **3.394.1.0** (SYS `AB2A2160`) = 248b + TMR.1 + correções (split do SDMA, D0, IhRing, VRD.1) + sonda DCN (A; B/C só leitura) + monitor `tools/monitor-corrida.py`, `tools/EXECUTAR_CORRIDA249_TMR1_PACOTE_COM_KD_MENU_600S.cmd`; repetição (250) obrigatória. **Corrida 249 (08/10 14:42, com KD):** desfecho B+ ([doc 659](docs/659_CORRIDA249_TMR1_ATIVO_4429_FILLS_TRAVA_AOS_7MIN_B_MAIS.md)): TMR.1 ativo (PSP aceitou 6/6 com a PA da DRAM, TMR fora do segmento), **4.429 FILLs e ~462 s** antes da trava contra ≤722 FILLs/≤108 s na base com RLC rodando (máximo histórico 2.890 FILLs), ~23 Presents/s (3× a 246b); trava de sempre. Sonda DCN sem saída (compilada sem DXGK_REGISTRATION, log era stub). Rollback online OK. **Corrida 250 pronta** (doc 659 §6): lab **3.395.1.0** (SYS `C987E574`) = repetição da 249 com o log da sonda DCN corrigido, `tools/EXECUTAR_CORRIDA250_REPETICAO_249_COM_KD_MENU_600S.cmd`. **Corrida 250 (15:39, com KD):** desfecho B ([doc 660](docs/660_CORRIDA250_REPETICAO_DA_249_1754_FILLS_TRAVA_AOS_3_5MIN_E_DADOS_DO_DCN.md)): ~213 s reais, **1.754 FILLs** (249: 4.429; critério de ganho real >2.900 não atingido), zero TDR, trava de sempre; o ganho grande da 249 não se repetiu, o TMR.1 fica. Sonda DCN com saída pela primeira vez: OTG ligado 2560×1080 a 56,13 Hz, superfície em PA 0x460000000, AUX0 com AUX_EN=0. **L1 pronta** ([doc 661](docs/661_L1_LINUX_COM_AMDGPU_DC0_DCN_COMO_O_GOP_DEIXOU_ROTEIRO_E_CRITERIO.md)): Linux com `amdgpu.dc=0` (DCN como o GOP deixou, sem desktop), weston headless GL + vkcube + paginação forçada, 2 × 600 s, `tools/linux/bc250-l1-dc0.sh`; não conta como corrida. **L1 executada (16:17/16:36):** desfecho A ([doc 662](docs/662_L1_LINUX_DC0_2X600S_SEM_TRAVA_DCN_HERDADO_NAO_BASTA_COM_RESSALVA_DE_MEMORIA.md)): 2 × 615 s sem trava com o DCN herdado do GOP e o OTG varrendo VRAM reutilizada; o DCN sai das causas suficientes, com a ressalva de VRAM pouco cheia (mediana 22 MB). **L1b pronta** ([doc 663](docs/663_L1B_LINUX_DC0_COM_VRAM_CHEIA_ZERAMENTO_E_COLETA_DE_GABARITO_ROTEIRO_E_CRITERIO.md)): dc=0 com a VRAM cheia e zeramento de tela cheia (`bc250_vram_stress`), sessão 1 limpa e 2 com varredura de 435 registradores, mais referência dc=1 (EDID, DTN log, refresh), `bc250-l1-auto.sh preparar --perfil l1b`. **L1b abandonada** (tela verde no Linux; a frente Linux foi encerrada pelo Daniel, doc 663 §6). **Corrida 251 pronta** ([doc 664](docs/664_SOMBRA1_PRESENT_FORA_DO_SCANOUT_FOTO_DO_CLOCK_GATING_E_CRITERIO_CORRIDA251.md)): lab **3.396.1.0** (SYS `36A6FC4F`) = 250 + SOMBRA.1 (Present/flip copiam para uma sombra em VRAM, não para o scanout que o DCN varre; tela congelada, menu às cegas) + foto do clock gating por bloco e refresh real no boot, `tools/EXECUTAR_CORRIDA251_SOMBRA1_FOTO_CG_MENU_600S.cmd`. **Corrida 251 (22:31, sem KD):** **inválida** ([doc 665](docs/665_CORRIDA251_INVALIDA_SOMBRA_RECUSADA_TRAVA_NO_STARTDEVICE_PELA_FOTO_CG_E_CRITERIO_251B.md)): a sombra foi recusada (`no_space`: o allocator do bootstrap ainda não existia no init do present, defeito meu) e a máquina travou ~8 s depois do bind, **dentro do StartDevice**; o único código novo que rodou foi a foto de 69 registradores de clock gating (suspeita P2/H; a mesma lista deu tela verde no Linux). Código 43 pelo fusível. **251b pronta:** lab **3.397.1.0** (SYS `E93667CD`), sombra alocada antes do corte do segmento, foto CG desligada, refresh sem espera, `tools/EXECUTAR_CORRIDA251B_SOMBRA1B_MENU_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e boot frio. **Corrida 251b (22:47, sem KD):** desfecho B ([doc 666](docs/666_CORRIDA251B_SOMBRA_ATIVA_ZERO_ESCRITA_NO_SCANOUT_TRAVA_IGUAL_AOS_53S_DESFECHO_B_E_REFRESH_60HZ.md)): sombra ativa, **zero blt no scanout** (330 na sombra), e trava dura igual aos ~53 s (412 FILLs): a escrita no scanout não é o mecanismo, e com a L1 o DCN sai da investigação. Refresh real medido **60,5 Hz** (o EDID declara 56,13: defeito do VidPn). Código 43 pelo fusível. Próximo proposto: **P5** (tabela de 4 níveis). **P5.0 (desenho, offline)** ([doc 667](docs/667_P5_0_DESENHO_TABELA_DE_PAGINAS_DE_4_NIVEIS_COMO_O_AMDGPU_E_REGUA_DE_FILLS.md)): L2/TLB do GFXHUB já em paridade; sobra a forma da árvore (2 níveis com raiz de 13 bits e 34 bits de VA, contra 4 níveis de 9 bits e 48 bits); a RADV não muda. Régua de FILLs pronta (`tools/regua-fills.py`, `docs/regua-fills.csv`: 39 corridas, mediana ~793, máximo 4.465). **P5.1 e Corrida 252 prontas** ([doc 668](docs/668_P5_1_TABELA_DE_4_NIVEIS_IMPLEMENTADA_PACOTE_3398_E_CRITERIO_CORRIDA252.md)): lab **3.398.1.0** (SYS `B4EC4FB3`) = 251b + P5 (tabela de páginas de 4 níveis × 9 bits, VA de 48 bits, `CONTEXTn_CNTL` `0x007FFE07` com DEPTH 3 e END `0xF:FFFFFFFF`, como o `amdgpu`; geometria em fonte única `driver/core/bc250_gpuva_geometry.h`) com a SOMBRA desligada (a tela volta), UMD `B73F9BDA` e RADV `B4AE2E14` iguais, `tools/EXECUTAR_CORRIDA252_P5_QUATRO_NIVEIS_MENU_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e boot frio. **Corrida 252 (09/10 08:51, sem KD):** desfecho B ([doc 669](docs/669_CORRIDA252_P5_QUATRO_NIVEIS_EM_REGIME_TRAVA_DURA_AOS_124S_DESFECHO_B.md)): P5 em regime (VaBits=48/Levels=4, 4 níveis de 9 bits, 10 instalações de VMID com readback de `0x007FFE07`, zero fault/TDR), desktop de volta (1.249 flips), 831 FILLs, e trava dura aos ~124 s com a GPU ociosa: a forma da árvore sai das causas; a P5 fica. Código 43 pelo fusível. Docs 670/671: a tradução velha da TLB (filas de compute da comunidade) não explica a trava; sobram C1–C4 de paridade. **Corrida 253 pronta** ([doc 672](docs/672_C1_DOORBELL_DO_GFX_COMO_O_AMDGPU_C2_JA_EM_PARIDADE_MON1_TEMPERATURA_E_CRITERIO_CORRIDA253.md)): lab **3.399.1.0** (SYS `F4156E65`) = 252 + C1 (WPTR do anel GFX por doorbell 0x116 como o `amdgpu`, `CP_RB_WPTR_POLL_CNTL` no valor da placa, sem GRBM/MMIO por submissão, com prova e rollback) + MON.1 (Tctl do KMD de volta e uso 3D/Copy no monitor); C2 já em paridade (MEC sem polling de WPTR, P0); clock da GPU fica para a SMU.1; `tools/EXECUTAR_CORRIDA253_C1_DOORBELL_GFX_MENU_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e boot frio. **Corrida 253 (09/10 10:11, sem KD):** desfecho B ([doc 673](docs/673_CORRIDA253_C1_DOORBELL_DO_GFX_ATIVA_E_PROVADA_TRAVA_DURA_AO_ABRIR_O_MENU_DESFECHO_B.md)): C1 ativa e provada (`CONTROL=0xC0000458`, `HIT=1`, 769/769 submissões do GFX pela doorbell, zero MMIO de WPTR), C2 confirmada em paridade, MON.1 com Tctl 82,5–85,75 °C, e trava dura ao abrir o menu aos ~40 s (255 FILLs) com a GPU ociosa: o canal do WPTR do GFX sai das causas; a C1 fica. Próximo: C3 (VRAM do Windows pela DRAM). Código 43 pelo fusível. **C3 corrigida e Corrida 254 pronta** ([doc 674](docs/674_C3_CORRIGIDA_A_VRAM_DO_WINDOWS_JA_ERA_PELA_DRAM_UM_SO_TIPO_WC_E_CRITERIO_CORRIDA254.md)): o Windows já via a VRAM pela DRAM desde a P1b (minha C3 do doc 671 estava errada); a diferença real era o tipo de memória, e a C3 passa todo mapeamento da DRAM pelo KMD a WC (um só tipo por página, como o `ioremap_wc` do `amdgpu`); lab **3.400.1.0** (SYS `452C12FD`), `tools/EXECUTAR_CORRIDA254_C3_VRAM_WC_MENU_600S.cmd`. **Corrida 254 (09/10 10:51, sem KD):** desfecho B ([doc 675](docs/675_CORRIDA254_C3_VRAM_WC_ATIVA_TRAVA_DURA_AO_ABRIR_O_MENU_DESFECHO_B.md)): C3 armada (8 mapeamentos WC, zero UC), C1 e P1b em regime, desktop desenhado, zero TDR/fault, e trava dura ~1,8 s depois de o menu carregar (~43 s, 407 FILLs) com a GPU ociosa: o tipo de memória sai das causas; a C3 fica. Próximo: C4 (CONST_FILL 4 MiB). Código 43 pelo fusível. **C4 e Corrida 255 prontas** ([doc 676](docs/676_C4_ZERAMENTO_POR_CONST_FILL_DE_4MIB_COMO_O_AMDGPU_E_CRITERIO_CORRIDA255.md)): zeramento por CONST_FILL em fatias de 4 MiB como `amdgpu_fill_buffer`, cópia de zeros da X2b dormente; lab **3.401.1.0** (SYS `A022A815`), `tools/EXECUTAR_CORRIDA255_C4_CONST_FILL_MENU_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e boot frio. **Corrida 255 (09/10 11:26, sem KD):** desfecho B ([doc 677](docs/677_CORRIDA255_C4_CONST_FILL_ATIVO_TRAVA_DURA_17S_DEPOIS_DO_MENU_DESFECHO_B.md)): C4 armada (585 FILLs por CONST_FILL, zero cópia), C3/C1/P1b em regime, desktop e menu apresentando, zero TDR/fault, e trava dura ~17 s depois de o menu carregar (~73 s, 561 FILLs) com a GPU ociosa: o zeramento sai das causas; a C4 fica. A lista C1–C4 do doc 671 está esgotada; próximo: novo inventário de paridade. Código 43 pelo fusível. **Corrida 256 (09/10 13:34, lab 3.402.1.0, 4C):** **DESFECHO A** ([doc 680](docs/680_CORRIDA256_4C_HDP_BYPASS_DESFECHO_A_600S_SEM_TRAVA_226MB_VRAM.md)): supressão de MMIO de HDP no FLUSH_TLB (`[4C_HDP_FLUSH_BYPASS] ATIVO: HdpNeeded=0`, zero leituras/escritas 0x3FC4); **a máquina operou ~600 s sem trava** (595 s medidos) com o Menu Iniciar e Pesquisa operando ativamente (27.793 linhas no log UMD do Menu Iniciar); VRAM dedicada atingiu 226.4 MB com 68 MB de transbordo compartilhado; 2.020 evicções, 70.889 FLUSH_TLBs, 3.123 FILLs, 14.484 Presents/Blts, zero TDR/fault; watchdog expirou e executou rollback online para BasicDisplay em Código 0; fusível CLEAN. A afirmação de que a 4C "removeu o gatilho" foi refutada pela 256b; a 256 também não foi só o menu (Configurações e Gerenciador de Tarefas abertos), ver [doc 691](docs/691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md). Repetição obrigatória (Lei 10). **Corrida 256b (13:51, repetição da 256):** **desfecho B** ([doc 681](docs/681_CORRIDA256B_REPETICAO_TRAVA_AOS_8S_DISPERSAO_CONFIRMADA_DESFECHO_B.md)): trava dura aos ~8,21 s após o bind (292 FILLs, 5.492 FLUSH_TLBs, 2.114 IBs do SDMA0 e 649 submissões GFX), com a GPU ociosa e drenada (RPTR==WPTR), zero TDR/fault; dispersão de 8 s a 600 s confirmada (como nas 201/202 e 226/228); a 4C fica por correção de APU, mas não é causa suficiente. Código 43 pelo fusível. **Passagem de bastão para a próxima IA: [doc 678](docs/678_PASSAGEM_DE_BASTAO_2026-10-09_TRAVA_DURA_DEPOIS_DA_LISTA_C1_C4.md)** e [doc 681](docs/681_CORRIDA256B_REPETICAO_TRAVA_AOS_8S_DISPERSAO_CONFIRMADA_DESFECHO_B.md). Antes: Corrida 200 com lab **3.347.1.0** (SDMA0 e clock gating no rastro, [doc 556](docs/556_W3_SMU_NAO_E_DIFERENCA_SDMA0_E_CLOCK_GATING_NO_RASTRO_CRITERIO_CORRIDA200.md)), `tools/EXECUTAR_CORRIDA200_SDMA0_CG_600S.cmd`. Histórico: a 199 foi preparada como candidato **3.345.1.0** (3.344.1.0 + instrumentação de Tctl, SDMA1 e motivo de
reset, [doc 553](docs/553_INSTRUMENTACAO_PLATAFORMA_TCTL_SDMA1_RESET_E_CRITERIO_CORRIDA199.md)) com a CMOS em 256, depois de
recuperação, fusível e boot frio (`tools/EXECUTAR_CORRIDA199_UMA256_600S.cmd`). **Plano único:** [`PLANO_MESTRE.md`](PLANO_MESTRE.md).
**Corridas 257 a 262 e auditoria (10/10/2026), [doc 691](docs/691_AUDITORIA_DAS_CORRIDAS_253_A_262_DOCS_680_A_690_CORRIGIDOS_T2_MTYPE_NC_E_PROXIMAS_TAREFAS.md):** os docs 680–690 foram escritos por outra IA e **não são fonte confiável**. A cadeia 5A–5F partiu do `RESET_REASON bit 30` como "paridade do Data Fabric", mas o bit é fixo (doc 625). As "melhoras" foram dispersão (257 ~69 s, 258 ~97 s, 259 ~146 s, 260 ~82–94 s, 261 ~17 s, 262 ~3–4 s), e vários números foram inventados: a tabela 252–255 do doc 680, as "baterias de 500 MB concluídas" (foram 10 texturas e 50 `E_OUTOFMEMORY` por processo) e a "cura do TRANSFER de 16 MB" (258/259/260 já tinham feito 58/88/47). As corridas 256, 258, 259 e 260 rodaram com apps abertos. Ficam a 4C, a 5A, os GCR e a reserva de 24 DW; a 5B/5C (MTYPE_UC padrão na folha) foi **revertida para NC como o amdgpu (T2)**; a 5G foi descartada. Pista nova (P2/H): a trava se concentra até ~3 s depois de um **processo novo** começar a usar a GPU, mesmo sem submissão ao GFX. Próximas tarefas T3–T11 no doc 691 §7; próxima corrida é **linha de base** (T7), não teste de mudança. **NÃO VALIDADO EM HARDWARE.** **Corrida 263 pronta ([doc 692](docs/692_PACOTE_3409_T2_E_CRITERIO_CORRIDA263_LINHA_DE_BASE_PROCESSO_NOVO.md)): linha de base**, lab **3.409.1.0** (SYS `81117FD1`) = 3.408.1.0 + T2, UMD e RADV iguais; carga fixa T3 (`tools\RODAR_PROCESSO_NOVO.cmd`, um processo D3D11 novo a cada ~5 s, 90 ciclos); programas abertos permitidos; `tools/EXECUTAR_CORRIDA263_T2_LINHA_DE_BASE_PROCESSO_NOVO_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e boot frio. **Corrida 263 (10/10 20:42, lab 3.409.1.0):** desfecho B ([doc 693](docs/693_CORRIDA263_LINHA_DE_BASE_TRAVA_DENTRO_DO_22O_PROCESSO_NOVO_DESFECHO_B.md)): pré-condição completa (`[T2_PTE_MTYPE]` 0/0 | 0/1 | 3), 21 processos da T3 completos e trava dura dentro do 22º, na 3ª alocação do processo (`hAlloc=0x40000800`, fence de paginação 7008, mesmo passo das 258/259), com a GPU ociosa e zero TDR/fault; taxa 1/22 (n=1). Preflight de boot frio corrigido: o bit 19 do RESET_REASON aparece em todo boot (71/71), agora vale o log do Windows (1074 "Desligar" e nenhum 41). Código 43 pelo fusível; próximo: 263b (mesmo pacote e carga) e mineração T5. **Corrida 263b pronta ([doc 695](docs/695_PACOTE_3410_CONTADORES_BFT1_E_CRITERIO_CORRIDA263B.md)):** lab **3.410.1.0** (SYS `F6533699`) = 3.409.1.0 + contadores BFT1 no rastro (só contagem: de quem é cada FLUSH_TLB, VMIDs invalidadas, faixas no SDMA1, leituras do SQ, invalidações pela CPU), mesma carga T3; T5/T6 no [doc 694](docs/694_T5_T6_MINERACAO_DO_PROCESSO_NOVO_NA_263_E_INVENTARIO_DE_MMIO_EM_REGIME.md); `tools/EXECUTAR_CORRIDA263B_CONTADORES_PROCESSO_NOVO_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Corrida 263b (10/10 21:23, lab 3.410.1.0):** desfecho D ([doc 696](docs/696_CORRIDA263B_TRAVA_NA_SUBIDA_DO_DESKTOP_DESFECHO_D_E_O_QUE_OS_CONTADORES_BFT1_MEDIRAM.md)): trava ~3 s depois de o desktop subir, antes da T3, como a 262; pré-condição completa; zero TDR/fault. Os contadores BFT1 mediram (P0): 20% dos FLUSH_TLB (522/2.611) vêm de processos sem VMID e cada FLUSH_TLB de usuário invalida em média 3,7 VMIDs, o que o amdgpu não faz; a programação de VMID pela CPU estava ativa no fim desta trava (0,17 s), mas não na 263 (12,4 s). Código 43 pelo fusível; próximo proposto: 264 = FT.1 (invalidar só as VMIDs do processo). **Corrida 264 pronta ([doc 697](docs/697_FT1_FLUSH_TLB_SO_DAS_VMIDS_DO_PROCESSO_PACOTE_3411_E_CRITERIO_CORRIDA264.md)):** lab **3.411.1.0** (SYS `73EDA3F6`) = 3.410.1.0 + FT.1 (FLUSH_TLB de processo de usuário invalida só as VMIDs onde a raiz dele está, READY/INSTALLING, VMID 7 e compute; processo sem VMID = nenhuma; raiz desconhecida falha fechado para todas), como o amdgpu; contadores BFT1 mantidos; mesma carga T3; `tools/EXECUTAR_CORRIDA264_FT1_FLUSH_POR_PROCESSO_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Corrida 264 (10/10 21:40, lab 3.411.1.0):** desfecho D ([doc 698](docs/698_CORRIDA264_FT1_EM_EFEITO_TRAVA_NA_SUBIDA_DO_DESKTOP_DESFECHO_D.md)): FT.1 em efeito e medida (processos sem VMID invalidaram zero VMID; 1,7 VMID por flush de usuário contra 3,7 na 263b), desktop apresentando, zero TDR/fault, e trava ~10 s depois de o shell subir, antes da T3, com a paginação quase parada nos ~5 s finais: a invalidação em excesso fica enfraquecida como mecanismo; a FT.1 fica. Código 43 pelo fusível; próximo proposto: `GfxWaveProbe=0`. **Corrida 265 pronta ([doc 699](docs/699_GFXWAVEPROBE0_PACOTE_3412_E_CRITERIO_CORRIDA265.md)):** lab **3.412.1.0** (SYS `A8A6C087`) = 3.411.1.0 com `GfxWaveProbe=0` (pacote sem `-ArmGfxWaveProbe`; a CPU deixa de ler SQ/CP a cada submissão do GFX; sem código novo), mesma carga T3; pré-condição `leituras_sq` = 0; `tools/EXECUTAR_CORRIDA265_GFXWAVEPROBE0_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Corrida 265 (10/10 21:59, lab 3.412.1.0):** desfecho C ([doc 700](docs/700_CORRIDA265_GFXWAVEPROBE0_41_PROCESSOS_TRAVA_ENTRE_CICLOS_DESFECHO_C.md)): `GfxWaveProbe=0` provado (`leituras_sq` = 0), FT.1 em efeito, 41 processos novos da T3 completos (código 0) e trava dura ~273 s depois do bind, entre ciclos, com o Explorer esperando a fence de paginação de uma alocação nova; zero TDR/fault, Tctl ≤ 83,5 °C. Corrida mais longa da série 263–265, mas dentro da dispersão (n=1); a sonda fica desligada. Código 43 pelo fusível; próximo proposto: poll do MEC sem o seletor GRBM. **Corrida 266 pronta ([doc 701](docs/701_MEC2_POLL_DO_MEC_SEM_SELETOR_GRBM_PACOTE_3413_E_CRITERIO_CORRIDA266.md)):** lab **3.413.1.0** (SYS `89E1C6AD`) = 3.412.1.0 + MEC.2 (o poll do MEC de 1 ms só escreve o seletor GRBM e lê `PQ_RPTR` com fence do MEC pendente ainda não vista; aposentadoria pela fence em memória, como o amdgpu; contadores no registro BMP1 do rastro), mesma carga T3; pré-condição `polls_com_seletor` < 1% sem compute; `tools/EXECUTAR_CORRIDA266_MEC2_POLL_SEM_SELETOR_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Corrida 266 (12/10 08:31, lab 3.413.1.0):** desfecho B ([doc 702](docs/702_CORRIDA266_MEC2_EM_EFEITO_SDMA1_NAO_SUBIU_TRAVA_NO_8O_PROCESSO_DESFECHO_B.md)): MEC.2 provada (0 de 5.228 polls do MEC com seletor GRBM; o poll roda ~85/s, não 1.000/s como eu supunha), FT.1 e sonda desligada em efeito, 7 processos da T3 completos e trava dura ~68 s depois do bind, dentro do 8º; zero TDR/fault. Variável não planejada: o SDMA1 não subiu (`PROOF_TIMEOUT`, 2 em 38 binds), PTE no SDMA0. A MEC.2 fica. Código 43 pelo fusível; próximo: medir a taxa real do MMIO que sobra em regime antes de escolher a próxima mudança. **Doc 703 (12/10, offline):** taxas medidas do MMIO em regime (o laço do blt é o mais frequente, mas a 263b travou com um blt só) e novas formas de identificar a trava; próxima proposta: 267 = validador de endereço físico das PTEs e anel de paginação com alvo, só medição. **Corrida 267 pronta ([doc 704](docs/704_DBG1_MODO_DEBUG_VALIDADOR_DE_ENDERECO_FISICO_PACOTE_3415_E_CRITERIO_CORRIDA267.md)):** lab **3.415.1.0** (SYS `90700F78`) = 3.413.1.0 + DBG.1/DBG.1b com a chave nova `DebugTrace` (INF; padrão 0, o empacotador de laboratório escreve 1 com `-DebugTrace`): em modo debug confere o endereço físico de toda PTE de memória de sistema contra a RAM do Windows e o carve-out da VRAM (só conta, nada é recusado), grava o evento `PAGING_PTE`, drena 96 eventos por tique e acorda o rastro a cada paginação (fecha a janela cega de ~110 ms do doc 705); nenhuma leitura ou escrita nova em registrador; mesma carga T3; `tools/EXECUTAR_CORRIDA267_DBG1_MODO_DEBUG_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Doc 705 (12/10, offline):** mineração de 46 travas: paginação ativa no fim de 96% delas (controle 26–61%), espera de paginação de alocação nova em 19/46, risco sem memória (~1 por 1.040 FILLs), e a máquina morre dentro do tique seguinte ao último registro, janela que a DBG.1b passa a gravar. **Corrida 267 (12/10 14:18, lab 3.415.1.0, modo debug):** desfecho B ([doc 706](docs/706_CORRIDA267_DBG1_ZERO_ENDERECO_INVALIDO_E_O_ULTIMO_MILISSEGUNDO_NA_TABELA_DE_UM_PROCESSO_NOVO.md)): 591.075 PTEs de sistema conferidas, zero fora da RAM (a classe sai das causas); anel BEV1 com 99,9% dos eventos; 29 processos da T3 e trava ~213 s depois do bind, no `vkCreateDevice` do 30º, ~1 ms depois de o SDMA0 receber o IB de paginação 6579 (512 bytes, sem RETIRE) que montava a primeira tabela do processo novo, passo que passou 32 vezes antes; SDMA1 em `PROOF_TIMEOUT` de novo. Próximo proposto: DBG.2 (dwords dos IBs de paginação e posição dos FLUSH_TLB no rastro). Código 43 pelo fusível. **Corrida 268 pronta ([doc 707](docs/707_DBG2_IB_DE_PAGINACAO_NO_RASTRO_MINERACAO_DA_FASE_DO_PROCESSO_NOVO_E_CRITERIO_CORRIDA268.md)):** lab **3.416.1.0** (SYS `D993B704`) = 3.415.1.0 + DBG.2 em modo debug (conteúdo de cada paging buffer, cada FlushTlb e os pontos de divisão de cada IB do SDMA0 no rastro, eventos em lote; decodificador remonta os últimos IBs), mesma carga T3, `tools/EXECUTAR_CORRIDA268_DBG2_IB_DE_PAGINACAO_600S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. Mineração (P0): 3 de 4 travas com T3 rodando caíram nos primeiros 0,9 s de vida de um processo novo (acaso ~0,01–0,03; n=4), e 5 de 47 travas terminam com um IB do SDMA0 sem RETIRE (0,5 esperado por perda de eventos). **Corrida 268 (12/10 15:04, lab 3.416.1.0, modo debug):** desfecho C ([doc 708](docs/708_CORRIDA268_DBG2_FIM_OCIOSO_NENHUM_IB_EM_VOO_TRAVA_NO_MAPEAMENTO_DA_1A_TEXTURA_DO_PROCESSO_NOVO_DESFECHO_C.md)): DBG.2 em regime (285.418 de 285.419 eventos), SDMA1 subiu (14.200 faixas de PTE), 29 processos da T3 e trava ~166 s depois do bind, 772 ms depois do início do 30º, quando o VidMm montava as 2.048 PTEs da 1ª textura de 8 MiB dele (fences 7003/7004); todos os 7.868 IBs do SDMA0 e 2.370 do GFX aposentados, último evento um FlushTlb dentro do handler (só memória, zero MMIO); não há IB em voo, ao contrário da 267. 4 de 5 travas com T3 nos primeiros 0,9 s de um processo novo. Código 43 pelo fusível. **Doc 709 (12/10, offline):** revisão da engenharia reversa do driver AMD (707/708 da raiz): a fila de paginação separada, o semáforo e a tabela plana já estão em paridade ou não são paridade com o amdgpu, e o DCN herdado do GOP não é causa da trava (L1, 251b, 263b, 268); checklist C1–C7 e D0–D5. **Corrida 269 pronta ([doc 710](docs/710_DCN1_FLIP_PELO_HUBP0_D0_A_D4_PACOTE_3418_E_CRITERIO_CORRIDA269.md)):** lab **3.418.1.0** (SYS `B624B7ED`) = 3.416.1.0 + DCN.1 (D0 P9 aberta; D1 offsets do `dcn_2_0_1` upstream; D2 leitura do HUBP0/OTG0 do GOP; D3 flip pelo HUBP0 em `SetVidPnSourceAddress`/MPO3 sem a cópia de 11 MB pelo SDMA0; D4 VSYNC só depois do latch, ainda por poll do OTG0, sem interrupção do DCN pelo IH), chave `DcnFlip=1`, modo debug igual ao da 268, teste só do DCN com watchdog de 60 s (exceção `BC250_ALLOW_SHORT_WATCHDOG`, pedido do Daniel) e sem a carga T3, `tools/EXECUTAR_CORRIDA269_DCN1_FLIP_PELO_HUBP_60S.cmd`; antes: recuperação, `inspect-boot-fuse.ps1 -Reset` e desligar. **Corrida 269 (12/10 16:27, lab 3.418.1.0):** desfecho A ([doc 711](docs/711_CORRIDA269_DCN1_FLIP_PELO_HUBP0_FUNCIONOU_CONTABILIDADE_DO_LATCH_E_CRITERIO_CORRIDA270.md)): primeira escrita nossa no DCN; 881 flips pelo HUBP0 com readback OK e zero recusas, 866 latches vistos pelo hardware, 878 Presents sem cópia ao scanout, HUBP0 de volta ao GOP no teardown, tela atualizando normal (operador), zero TDR/fault em 60 s, rollback online limpo; menu não testado. 15 flips sem destino por defeito meu de contabilidade (flip repetido e substituído não contados). **Corrida 270 pronta:** lab **3.420.1.0** (SYS `87DB9853`) = 3.418.1.0 + contabilidade do latch (latchado/substituído/pendente, `conta=FECHA` no teardown) + `[DCN1_RESUMO]` (rotas, latch em quadros e ms, VSYNC contra o OTG, primária fora do segmento 1), só medição; 180 s com menu, Alt+Tab, vídeo em tela cheia e T3, `tools/EXECUTAR_CORRIDA270_DCN1_AMPLIADA_180S.cmd`; antes: desligar e ligar. **Corrida 270 (12/10 18:40, lab 3.420.1.0):** ([doc 712](docs/712_CORRIDA270_FLIP_PELO_HUBP0_NORMAL_ATE_O_FIM_TRAVA_1S_DEPOIS_DO_SEARCHAPP_AO_ABRIR_O_MENU.md)) flip pelo HUBP0 normal até o fim (readback e latch OK, zero recusa/TDR/fault), e trava dura ~1,1 s depois de o SearchApp nascer ao abrir o menu (~15 s de desktop, antes da T3), com o IB de paginação 2119 entregue ao SDMA0 sem RETIRE, como a 267; P2/P3 sem resposta (resumo só a cada 1.024 latches). A DCN.1 tirou todas as cópias de 11 MB do SDMA0 (as "13 cópias" eram uma linha de log enganosa, corrigida), então a apresentação pelo SDMA0 sai das causas suficientes (n=1). **Corrida 271 pronta:** lab **3.421.1.0** (SYS `CAB0136C`) = 270 + log de Present correto + `[DCN1_RESUMO]` a cada ~4 s, `tools/EXECUTAR_CORRIDA271_DCN1_AMPLIADA_180S.cmd`; antes: desligar e ligar. **Corrida 271 (12/10 19:27, lab 3.421.1.0):** ([doc 713](docs/713_CORRIDA271_CONTA_FECHA_E_DCN2_SOBRE_O_GOP_PACOTE_3422_E_CRITERIO_CORRIDA272.md)) 174 s sem trava, `conta=FECHA` (3.796/3.796), refresh 59,997 Hz, 4,2% de VSYNC perdidos pela consulta de 4 ms, zero TDR/fault, rollback limpo; a T3 não rodou. Proveniência corrigida: os offsets são do `dcn_2_0_3` (o doc 710 citava `dcn_2_0_1`, inexistente; valores iguais). **Corrida 272 pronta:** lab **3.422.1.0** (SYS `4071FC99`) = DCN.1 + DCN.2 com `DcnExt=0x3F` (timing medido no VidPn, VSYNC/flip pela interrupção do DCN, CRC do OTG, cursor por hardware, EDID real por AUX + HPD, DPMS); flip direto fora (exige UMD D3D11.1); `tools/EXECUTAR_CORRIDA272_DCN2_SOBRE_O_GOP_180S.cmd`; antes: desligar e ligar. **Corrida 272 (12/10 20:20, lab 3.422.1.0):** ([doc 714](docs/714_CORRIDA272_TELA_AZUL_0x133_AUTOTRAVAMENTO_EM_DIRQL_DIRQL1_DCN3_PACOTE_3423_E_CRITERIO_CORRIDA273.md)) timing real 59,999 Hz no VidPn, DPMS e cursor por hardware funcionaram (operador); VSTARTUP nunca chegou ao IH, CRC não confiável, AUX sem resposta; 21 processos da T3 e **BugCheck 0x133** aos ~145 s: autotravamento nosso em DIRQL (o flip MPO3 roda no `KeSynchronizeExecution` e o log tomava spinlock com `KeAcquireSpinLock`; a interrupção do IH entrou na mesma CPU), P0 pelo dump. **Corrida 273 pronta:** lab **3.423.1.0** (SYS `79C8E8A0`) = DIRQL.1 (log sem trava e sem cópia do SDMA0 em DIRQL) + DCN.3 (VSTARTUP/VUPDATE/VLINE0 em paralelo, AUX nos dois canais com a init do upstream, cursor mono/mascarado sem inversão) com `DcnExt=0x3D` (CRC fora); `tools/EXECUTAR_CORRIDA273_DIRQL1_DCN3_180S.cmd`; se travar, **esperar 3 min antes do reset**; antes: desligar e ligar. **Corrida 273 (12/10 21:07, lab 3.423.1.0):** ([doc 715](docs/715_CORRIDA273_SEM_TRAVA_175S_COM_T3_DIRQL1_EM_EFEITO_VSYNC_NAO_ROTEADO_AUX0_EM_MODO_DDC.md)) **175,6 s sem trava, sem BugCheck e sem TDR, com 24 processos da T3** (o 25º caiu no rollback); DIRQL.1 em efeito (2.310 linhas de log em DIRQL, 0 descartadas); DCN.1 `conta=FECHA` (2.136 flips); timing 59,999 Hz e DPMS OK; cursor de cor por hardware, 9 formas mono com inversão no software. As interrupções VSTARTUP/VUPDATE/VLINE0 nascem no OTG (`INT_STATUS=1`) e não chegam ao IH (roteamento do DMU, aberto); o AUX0 responde `NON_AUX_MODE` (pad em modo DDC; próximo: `hw_ddc` set_config AUX). Mineração: as corridas 199–231 travaram com o IH desligado, então o deadlock em DIRQL não explica a trava de sempre (os 175 s são compatíveis com a dispersão). Próximo: repetir a 273 lendo `OTG0_INTERRUPT_DEST` (roteamento do VSYNC) e com o pad AUX em modo AUX. **Passagem de bastão para a próxima IA: [doc 716](docs/716_PASSAGEM_DE_BASTAO_2026-10-12_DCN1_A_DCN3_DIRQL1_E_O_QUE_FAZER_A_SEGUIR.md)** (leia primeiro). **Corrida 289 (13/10 22:01, lab 3.438.1.0):** falha ([doc 741](docs/741_CORRIDA289_0x116_GFX_TRAVADO_SEM_AQUECIMENTO_DX43_APU2B_PACOTE_3439_E_CRITERIO_CORRIDA290.md)): BugCheck 0x116 aos ~40 s, com tela preta. O GFX travou no primeiro IB depois de uma troca de VMID, a preempção não drenou e o nosso reset de TDR devolveu `DEVICE_NOT_READY`. Causa: a APU.2 pôs o buffer de comando da RADV no segmento 2, onde a DX.43 não lê o DW0 do IB (21/21 `CABECA_ILEGIVEL` contra 6.247/6.247 `EMITIDO` na 288). Depois vieram 4 reinícios do Windows Update e rollback online limpo. **Decisão do Daniel: corrigir em vez de reverter, sem voltar a uma mudança por corrida.** **Corrida 290 pronta:** lab **3.439.1.0** (SYS `7B6DC0A4`) = 3.438 + APU.2b (primária e buffer de comando só no segmento 1, `Bc250AllocationApuMayPreferSystem`), `tools/EXECUTAR_CORRIDA290_APU2B_300S.cmd`; antes: desligar e ligar. **Corrida 290 (13/10 22:44, lab 3.439.1.0):** desfecho E ([doc 742](docs/742_CORRIDA290_APU2B_FECHOU_O_0x116_TELA_PRETA_POR_COMPOSICAO_RECUSADA_APU2C_PACOTE_3440_E_CRITERIO_CORRIDA291.md)): a APU.2b fechou o 0x116 (209 aquecimentos da DX.43, zero TDR em 297 s, rollback sem reboot), mas a tela ficou preta com o cursor vivo: a APU.2 pôs as superfícies do DWM no segmento 2 e o `COMPOSITE_BLT` do KMD (cópia por MC, só VRAM) recusou 160 e fez 0 (288: 4.907). **Corrida 291 pronta ([doc 743](docs/743_COMPVA_COMPOSICAO_PELO_VA_DO_PROCESSO_PELA_JANELA_DA_GART_PACOTE_3441_E_CRITERIO_CORRIDA291.md)):** lab **3.441.1.0** (SYS `C83F70F6`) = 3.439 + APU.2c (`Bc250AllocationApuSegmentPolicy`: superfície da UMD sem `GPU_VA_ONLY` prefere a VRAM) + COMP.VA (composição pelo VA do processo: PTEs do dono copiadas para a janela da GART da VMID 0, entradas 256..511, invalidação da VMID 0 no anel do SDMA0, como `amdgpu_ttm_map_buffer`), `tools/EXECUTAR_CORRIDA291_APU2C_300S.cmd`; antes: desligar e ligar. **Corrida 291 (13/10 23:17, lab 3.441.1.0):** desfecho A ([doc 744](docs/744_CORRIDA291_DESKTOP_E_3D_DE_VOLTA_COMPOSICAO_100PCT_PELA_JANELA_DA_GART_E_O_QUE_FALTA.md)): desktop e teste 3D de volta, ≥ 6.144 composições pela janela da GART (0 falhas), zero TDR/fault, T3 com 21 ciclos, rollback sem reboot; restam lentidão (22 cópias e 2.700 PTEs por quadro, superfície do DWM é memória da RADV no segmento 2), piscadas e falhas da Pesquisa por 5 recusas "janela ocupada" (defeito meu) e 227 `Upload LOCK falhou`. Próximo: COMP.VA.2 (esperar a janela), COMP.VA.3 (GART maior e cache de PTE), RT da RADV na VRAM, zero cópia. **Corrida 292 pronta ([doc 745](docs/745_COMPVA2_JANELA_QUE_ESPERA_COMPVA3_GART_32MB_E_JANELA_PERSISTENTE_PACOTE_3442_E_CRITERIO_CORRIDA292.md)):** lab **3.442.1.0** (SYS `D6D57307`) = 3.441 + COMP.VA.2 (a janela espera em vez de recusar) + COMP.VA.3 (GART do GFXHUB de 32 MB com tabela de 64 KB reservada no alocador, MMHUB em 2 MB; tela inteira numa cópia; janela persistente enquanto a geração da tabela do dono não muda), `tools/EXECUTAR_CORRIDA292_COMPVA23_300S.cmd`; antes: desligar e ligar. **Corrida 292 (13/10 23:46, lab 3.442.1.0):** desfecho C ([doc 746](docs/746_CORRIDA292_GART_NAO_SUBIU_RESERVA_DA_TABELA_NO_SPACE_COMPVA3B_PACOTE_3443_E_CRITERIO_CORRIDA292B.md)): tela preta porque a GART da VMID 0 não subiu (a reserva da tabela de 64 KB deu `NO_SPACE`; o offset fixo saiu de um mapa DX.67 incompleto, defeito meu); zero TDR, rollback sem reboot. **Corrida 292b pronta:** lab **3.443.1.0** (SYS `ABAFF3ED`) = COMP.VA.3b (varredura a partir de 0x0F3F0000 só por `ReserveFixed`), `tools/EXECUTAR_CORRIDA292B_GART_VARREDURA_300S.cmd`; antes: desligar e ligar. **Corrida 292b (14/10 00:18, lab 3.443.1.0):** desfecho C ([doc 747](docs/747_CORRIDA292B_ALOCADOR_DO_BOOTSTRAP_AINDA_NAO_EXISTIA_NA_D2_COMPVA3C_PACOTE_3444_E_CRITERIO_CORRIDA292C.md)): tela preta de novo; a reserva deu `OUT_OF_RANGE` em 320 posições porque o alocador do bootstrap só nasce no segundo preflight, depois da D2 (`TotalBytes=0`); o "topo cheio" do doc 746 estava errado. **Corrida 292c pronta:** lab **3.444.1.0** (SYS `218CFF90`) = COMP.VA.3c (reserva no alocador da MMU global, espelhada no do bootstrap), `tools/EXECUTAR_CORRIDA292C_GART_ALOCADOR_MMU_300S.cmd`; antes: desligar e ligar. **Corrida 292c (14/10 09:17, lab 3.444.1.0):** desfecho A ([doc 748](docs/748_CORRIDA292C_GART_DE_32MB_NO_AR_COMPOSICAO_NUMA_COPIA_SO_DESFECHO_A_E_O_QUE_SOBRA.md)): a GART de 32 MB subiu, junto com P4, P3 e P7; 5.376 composições, todas numa cópia só, com 90% de reuso, zero recusas e zero TDR/fault; T3 com 21 ciclos. COMP.VA.2 e COMP.VA.3 validados. Restam as piscadas (causa não medida) e o menu lento (cópia vinda da RAM, espera da CPU pelo GFX, `Upload LOCK falhou`). Próximo: render target da RADV na VRAM. **Corrida 293 pronta ([doc 749](docs/749_PISC1_LATCH_MEDIDO_NO_REGISTRADOR_ZC1_PRESENT_SEM_COPIA_PACOTE_3445_E_CRITERIO_CORRIDA293.md)):** lab **3.445.1.0** (SYS `64D0E959`, UMD `9F65C18A`) = 3.444 + PISC.1 (latch medido no registrador; o "3+" da 292c era contagem) + ZC.1 (render target apresentável desenha direto na própria alocação WDDM; Present sem cópia), `tools/EXECUTAR_CORRIDA293_ZC1_ZERO_COPIA_300S.cmd`; antes: desligar e ligar. **Corrida 293 (14/10 09:41, lab 3.445.1.0):** desfecho B ([doc 750](docs/750_CORRIDA293_ZERO_COPIA_ATIVA_TELA_PRETA_POR_SOMBRA_DE_CPU_ZC1B_PACOTE_3446_E_CRITERIO_CORRIDA293B.md)): zero cópia ativo (0 composições pelo KMD, menu mais rápido), mas a tela ficou preta fora do redesenhado: `ResourceCopy`/`CopyRegion` subiam a sombra de CPU (zeros) por cima da cópia de GPU, o mesmo mecanismo da Corrida 190; PISC.1 mostrou 1 latch tardio real. **Corrida 293b pronta:** lab **3.446.1.0** (UMD `36A7D0DC`) = ZC.1b, `tools/EXECUTAR_CORRIDA293B_ZC1B_SEM_SOMBRA_300S.cmd`; antes: desligar e ligar. **Corrida 293b (14/10 10:03, lab 3.446.1.0):** desfecho A ([doc 751](docs/751_CORRIDA293B_DESKTOP_INTEIRO_SEM_COPIA_PSO1_CACHE_DE_PIPELINES_PACOTE_3447_E_CRITERIO_CORRIDA294.md)): desktop inteiro sem cópia (ZC.1 + ZC.1b em efeito, 0 composições pelo KMD, 0 `Upload LOCK falhou` no menu), zero TDR/fault; ficam a 1ª abertura do menu (sem cache de shader e com captura de IR ligada) e a Pesquisa (WebView2, H: larga a GPU). **Corrida 294 pronta:** lab **3.447.1.0** (UMD `00BCFB38`) = PSO.1 (cache de pipelines persistente, captura de IR opcional), `tools/EXECUTAR_CORRIDA294_PSO1_CACHE_DE_PIPELINES_300S.cmd`; antes: desligar e ligar. **SHD.1 ([doc 752](docs/752_SHD1_TRADUTOR_DE_SHADER_NO_OPENADAPTER_PESQUISA_WEBVIEW2_ERR577_PACOTE_3448.md)):** no WebView2 da Pesquisa o tradutor `libvkd3d-shader` foi recusado com err=577 no 1º shader, e nenhum shader do processo traduziu (P0). Agora ele carrega no `OpenAdapter`. A 294 passa ao lab **3.448.1.0** (UMD `0A0500A2`) = PSO.1 + SHD.1; antes de desligar, rodar `tools\armar-log-webview2.ps1` elevado (log do Chromium). **Corrida 294 (14/10 10:53, lab 3.448.1.0):** parcial ([doc 753](docs/753_CORRIDA294_CACHE_DE_PIPELINES_FUNCIONA_MENU_RECRIA_O_DEVICE_E_PESQUISA_NASCEU_ANTES_DO_BIND.md)): estável, PSO.1 funciona mas não resolve o menu (1,49 s → 1,39 s; o menu recria o device inteiro a cada abertura); a Pesquisa nasceu antes do bind e o WebView2 nunca usou a nossa GPU (roteiro: reiniciar a Pesquisa depois do bind). Próximo: MENU.1 (tempo por fase), PSO.1b (gravar só com mudança) e reinício da Pesquisa no roteiro. **Corrida 295 pronta ([doc 754](docs/754_PAG1_ESPERA_DE_PAGINACAO_ADIADA_MENU1_PSO1B_PESQUISA_APOS_BIND_PACOTE_3449_E_CRITERIO_CORRIDA295.md)):** lab **3.449.1.0** (SYS `D7DE13EA`, UMD `76DA7661`) = 3.448 + PAG.1 (o CreateResource não espera a paginação na CPU para recurso com memória Vulkan própria; alias, Lock, Present e composição esperam antes; na 294 eram 55–59 esperas de 5–8 ms por abertura do menu) + MENU.1 (`[MENU1]`, tempo por fase) + PSO.1b + Pesquisa reiniciada depois do bind, `tools/EXECUTAR_CORRIDA295_PAG1_MENU1_PESQUISA_300S.cmd`; antes: desligar e ligar. **Corrida 295 (14/10 12:12, lab 3.449.1.0):** parcial ([doc 755](docs/755_CORRIDA295_PAG1_NAO_ERA_O_GARGALO_PESQUISA_NA_NOSSA_GPU_E_CHECKLIST_PARA_O_PROXIMO_FOCO.md)): estável e rollback online; PAG.1 funcionou (114 esperas adiadas, nenhum artefato) mas não era o gargalo (menu 1,3–2,2 s; custo nas alocações por recurso, da RADV e nossas: próximo é sub-alocação); MENU.1 e PSO.1b validados; a Pesquisa nasce no nosso driver e a SHD.1 está validada no WebView2, mas o Chromium larga a GPU ~0,5 s depois (H: software). **Decisão do Daniel: sair da velocidade do desktop**; próximo foco a escolher no doc 755 §6 (A app 3D/jogo real, B VSYNC por interrupção, C journal, D velocidade, E flip sem espera da CPU, F pequenos). **Corrida 296 (14/10 15:31, lab 3.450.1.0):** falha ([doc 757](docs/757_CORRIDA296_TRAVA_NO_STARTDEVICE_VCN_REMOVIDA_ICD1_WSI1_VULKAN_COMO_DRIVER_OFICIAL_PACOTE_3451_E_CRITERIO_CORRIDA297.md)): trava dura dentro do StartDevice, antes do desktop (log para em `[DCN1_D2]`); principal suspeita a sonda VCN.1 com `VcnProbe=2` (P2, n=1); o "achado de silício" no comentário do `bc250_vcn_probe.c` (bit 30) estava errado. **Decisão do Daniel: VCN removida e foco em app 3D real** (vkcube baixado tem de funcionar sem configuração, como num driver da AMD). **Corrida 297 pronta:** lab **3.451.1.0** (SYS `B29BFF54`, UMD `4BA5929A`, RADV `0EE338DB`) = 3.450 sem a VCN + ICD.1 (`VulkanDriverName` no INF, RADV no DriverStore, runtime da Khronos) + WSI.1 (app Vulkan apresenta pela UMD D3D11 e pelo DWM) + RADV sem DCC na BC-250, `tools/EXECUTAR_CORRIDA297_VKCUBE_REAL_600S.cmd`; antes: `inspect-boot-fuse.ps1 -Reset`, desligar e ligar. **Corrida 297 (14/10 16:43, lab 3.451.1.0):** parcial ([doc 758](docs/758_CORRIDA297_VKCUBE_REAL_PELA_GPU_E_DWM_MEMTEST_VULKAN_SEM_ERRO_E_0x116_POR_TDR_DO_GFX_QUE_NAO_VOLTA.md)): vkcube e vkcubepp baixados acharam a BC-250 sozinhos e apresentaram pela GPU e pelo DWM (161 + 145 Presents S_OK, ~11–16 quadros/s); memtest_vulkan sem erro; GPUbench é OpenGL e roda na CPU. BugCheck 0x116 aos ~5 min na composição do desktop: dois page faults e o reinício do GFX no TDR não volta (CP_RB0 parado depois do soft-reset). **Corrida 298 pronta ([doc 759](docs/759_GFXRST2_RESET_DO_GFX_QUE_VOLTA_E_WSI2_VKCUBE_SEM_ESPERA_DA_CPU_PACOTE_3452_E_CRITERIO_CORRIDA298.md)):** lab **3.452.1.0** (SYS `6DF1EE05`, UMD `B1AAFF12`, RADV `86BF7F51`) = 3.451 + GFXRST.2 (restart do GFX em três etapas provadas; ele passava 212/213 até a 240 e 0/2 depois da GFXRST.1/RLC.1) + WSI.2 (vkcube sem as duas esperas da CPU: cópia direta para o back buffer e fences pela GPU), `tools/EXECUTAR_CORRIDA298_GFXRST2_WSI2_600S.cmd`; antes: fusível, desligar e ligar. **Corrida 298 (14/10 17:37, lab 3.452.1.0):** parcial ([doc 760](docs/760_CORRIDA298_WSI2_RECUSADA_NA_IMPORTACAO_DA_FENCE_GRAVITYMARK_SEM_MEMORIA_WSI2B_HEAP1_PACOTE_3453_E_CRITERIO_CORRIDA299.md)): 600 s sem TDR, BugCheck ou fault, rollback online limpo; GFXRST.2 não exercitada (nenhum TDR). A WSI.2 foi recusada em todas as swapchains: a RADV não importou a fence da UMD (é o erro `vk_wddm2_monitored_fence.c:90` que o Daniel viu), e vkcube/vkcubepp rodaram pela WSI.1 (~15/19 quadros/s, como na 297). GravityMark 1.89 (Vulkan) rodou ~35 s (282 Presents, ~8 quadros/s) e caiu em `STATUS_NO_MEMORY` aos ~5,3 GB, porque a RADV anunciava 12 GB de GTT. **Corrida 299 pronta:** lab **3.453.1.0** (SYS `E04FF5AD`, UMD `B1AAFF12`, RADV `12D48A75`) = WSI.2b (importação com as combinações de flags válidas e NTSTATUS no log) + HEAP.1 (GTT pelo orçamento NON_LOCAL do Windows), `tools/EXECUTAR_CORRIDA299_WSI2B_HEAP1_600S.cmd`; antes: fusível, desligar e ligar. **Corrida 299 (14/10 21:49, lab 3.453.1.0):** parcial ([doc 761](docs/761_CORRIDA299_WSI2_ATIVA_TETO_DO_DWM_TFM_SEM_RESIDENCIA_MEMTEST_PARA_A_PAGINACAO_VKLOG1_PACOTE_3454_E_CRITERIO_CORRIDA300.md)): 600 s sem TDR nem BugCheck, rollback online; HEAP.1 em efeito (GTT 7,6 GB pelo orçamento do Windows); WSI.2 **ativa** (a importação do upstream dava `0xC000000D`; sem `Shared` abre) e as fences pela GPU fecham em todo quadro, mas o vkcube segue ~16 quadros/s: o `pfnPresentCb` leva 30–45 ms esperando o DWM, que compõe a ~33/s. TFM (Vulkan) abriu e apresentou, mas o KMD recusou 1.102 Presents do back buffer de tela cheia (não residente) e houve 1 page fault no mesmo endereço; GravityMark sem log; memtest parou a paginação ~10 s e o DWM desistiu na espera de 5 s (tela preta que voltou). **Corrida 300 pronta:** lab **3.454.1.0** (SYS `6E61F032`, UMD `B1AAFF12`, RADV `C6C9E7F3`) = VKLOG.1 (log Vulkan próprio da RADV em todo app: instância, device, placas, todo erro com arquivo:linha e funções não expostas), `tools/EXECUTAR_CORRIDA300_VKLOG1_600S.cmd`; antes: fusível, desligar e ligar. **Corrida 300 (14/10 22:18, lab 3.454.1.0):** falha (análise só leitura, a pedido do Daniel): VKLOG.1 funcionou em todo app; o GravityMark criou o device sem erro Vulkan e sem função faltando, rodou a ~8,6 quadros/s com a GPU 5–48% ocupada e o clock fixo em 1.500 MHz; dois page faults no back buffer da WSI (um no do GravityMark), TDR do GFX e trava dura entre o ResetFromTimeout e o restart (nenhuma linha do restart chegou ao log). O DWM gastou ~34 ms por criação de recurso e ~80% do tempo do quadro sem marca. **Corrida 301 pronta ([doc 762](docs/762_SUBALOC1_FRAME1_RES1_TDRLOG1_CLK1_PACOTE_3455_E_CRITERIO_CORRIDA301.md)):** lab **3.455.1.0** (SYS `510A1D86`, UMD `30D56611`, RADV `C6C9E7F3`) = SUBALOC.1 (recursos pequenos em blocos de 4 MiB) + FRAME1 (tempo do quadro por fase em todo processo) + RES.1 (back buffer da WSI com prioridade máxima de residência, mitigação) + TDRLOG.1 (passos do TDR em `C:\BC250_TDR_PASSOS.txt`, write-through) + CLK.1 (GFX a 1800 MHz pela SMU, devolvido no StopDevice), `tools/EXECUTAR_CORRIDA301_SUBALOC_FRAME1_RES1_TDRLOG_CLK1_600S.cmd`; antes: fusível, desligar e ligar. **Corrida 302 pronta ([doc 764](docs/764_C01_A_C07_DO_NAGI_PARIDADE_PELO_RASTRO_DO_AMDGPU_PACOTE_3456_E_CRITERIO_CORRIDA302.md)):** lab **3.456.1.0** (SYS `3EB03C0F`, UMD `30D56611`, RADV `296F3F8F` no pacote) = 3.455.1.0 + o que veio do Nagi (`referencias/amdgpu-wddm`, [doc 763](docs/763_CHECKLIST_DO_QUE_COPIAR_DO_NAGI_AMDGPU_WDDM_PARA_AVANCAR_MAIS_RAPIDO.md)): C01 (estado de init que o rastro de 10.018 escritas do `amdgpu` nesta placa mostra e nós não fazíamos: `SH_MEM_CONFIG`/`SH_MEM_BASES` por VMID, quantum do SDMA, faixas dos engines de invalidação 2..16; GDS fora de propósito), C03 (RLC.1b religada), C05 (wave kill no TDR do GFX), C06 (VSYNC só pela VUPDATE sem lock, na sequência exata do Linux, e erros do CP contados no IH) e C07 (pool de VMID por LRU); C04 (paginação direto no anel) bloqueado: os comandos usam endereço virtual e o anel roda na VMID 0. Pode rodar no lugar da 301 (leva tudo dela). `tools/EXECUTAR_CORRIDA302_NAGI_C01_C03_C05_C06_C07_600S.cmd`; antes: fusível, desligar e ligar.

---

## Diretriz vigente — qualidade e relato de corridas (16/09/2026)

Esta decisão do proprietário prevalece sobre orientações históricas conflitantes abaixo. Cumprir [DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md](DIRETRIZ_QUALIDADE_UMD_DX11_E_MARCOS_GRAPHICS.md).

- **Qualidade antes de velocidade:** WDDM display+render, desktop acelerado e aplicações/jogos não modificados. Não reduzir a meta a demos, render-only ou fallback de software.
- **Refazer Marcos 3 e 4:** tela colorida e cubo com vkCreateGraphicsPipelines, vertex/fragment shaders e vkCmdDraw, engine física identificada e pixels conferidos. Preservar compute/SDMA como prova desse escopo; conferir hashes antes de atribuir fontes atuais a binários históricos.
- **UMD D3D11 traduzida para Vulkan:** DDIs reais, recursos compartilhados, sincronização, residência, apresentação Windows e recuperação. DXVK local por aplicativo não fecha a entrega. Não anunciar GRAPHICS apenas removendo uma trava.
- **DCN básico integra o produto final em P9**, respeitando P5 e a governança de escrita; não é pré-requisito do primeiro desktop no GOP.
- **Está revogada a barra “STATUS DO DRIVER BC-250” antes de cada mensagem**, proposta, plano ou análise. Blocos antigos abaixo são histórico, não modelo de resposta nem status atual.
- **Após cada corrida física analisada, informar obrigatoriamente ao Daniel:**
  1. **Houve avanço?** Sim, não ou inconclusivo, comparando com a corrida anterior pertinente. Diferenciar avanço funcional de aprendizado de diagnóstico.
  2. **Sucessos:** o que passou, métricas e evidência.
  3. **Falhas:** erros, timeouts, pixels incorretos, device-lost, TDR, BugCheck e problemas observados; se nenhum foi observado, limitar a afirmação ao escopo medido.
  4. **Veredito:** sucesso, parcial, falha ou inconclusivo frente ao critério definido antes do ensaio. Código 0 e Exit 0 isolados não fecham o teste.
  5. **Recuperação e pendências:** driver ativo ao final, resultado do rollback, o que continua NÃO VALIDADO EM HARDWARE e próximo passo justificado.
- Identificar data/hora, candidato e pasta de evidência. Se faltarem logs ou medições, declarar a lacuna, sem inventar métricas. Desktop restaurado pelo BasicDisplay é recuperação, não sucesso do nosso DWM.
- **Tabelas obrigatórias depois de toda corrida, até o DCN estar completo (decisão do Daniel, 12/10/2026):**
  1. a tabela-checklist da corrida (✅ 🟡 ❌ ⏸️ ⬜, um item por linha, só ✅ com prova P0);
  2. a **Tabela viva do DCN** inteira (D01–D33: flip, VSYNC, cursor, AUX/EDID, DPMS, CRC, troca de resolução e refresh, escala, rotação, formatos, gamma, MPO, DirectFlip, VRR, link, áudio, clocks), atualizada com o resultado. HDR, segundo monitor e hotplug estão fora do escopo e não serão feitos (decisão do Daniel, 12/10/2026: uma única saída DP e HPD defeituoso até no Linux). A fonte é a seção P9 da [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md), que também é atualizada a cada corrida, com o histórico das corridas do DCN.
- Arquivar provas e atualizar BASE_DE_AVANCO.md quando houver conclusão nova. Alteração offline não é corrida. Não repetir percentuais históricos como medida de prontidão do produto.

### Registro histórico — não reproduzir automaticamente


```text
================================== STATUS DO DRIVER BC-250 ==================================
Base Geral Pre-WDDM (Lab)        ███████████████████░  94%   Estavel; 3 corridas em 12/09 sem BugCheck, fusivel CLEAN em todas
SDMA (DMA Engine & Robustez)     ████████████████████  99%   PRESENT 100% GPU (12/09 13:01): 584 blts, Fallbacks=0, HostCpuCopies=0, 1,37 ms/blt
GCVM & Traducao de Memoria       ███████████████████░  96%   Conversao segmento->MC em fonte unica e VALIDADA (SegBase=0xA8C000); ABERTO: GFXHUB_FAULT 00841B40 no IB de 11 MB
RLC, CP & PM4 Direct Load        ███████████████████░  95%   Fence em VRAM provada
Telemetria & Observabilidade     ███████████████████▍  97%   C3 pagou na 1a tentativa: HostCpuCopies e no_host_copy_fallback pegaram present por CPU que parecia vitoria
Compute & Async Queues (MEC/MQD) ██████████████████▋░  93%   RADV compute e present validados; 584 quadros consecutivos, Sub==Ret em todas as corridas
Miniport WDDM Display+Render     █████████████████▌░░  88%   Quadro Vulkan chega ao monitor POR GPU; ABERTO: DEVICE_REMOVED 0,46 s apos o 7o crash do dwm.exe
=============================================================================================
Veredito Global: a corrida de 06/09 20:34 validou a correcao da root SegmentId=1, Offset=0.
Os IBs de paginacao 0x22F000/20 tiveram snapshot Status=0 e caminhada CPU/PT coincidente.
Dois VIRTUAL_FILL diretos (81.920 e 4.096 bytes) fecharam RPTR==WPTR, fence fisica sobre
poison e dados byte a byte. Isso nao prova execucao do IB sob VMID8 nem compute Node1.
=============================================================================================
O display do perfil lab nao convergiu: 97 IsSupportedVidPn e 65 EnumVidPnCofuncModality,
todos sem GetStandardAllocationDriverData de primaria, CommitVidPn, SetVidPnSourceAddress
ou Present. A corrida de producao de 05/09 comitou apos 18 consultas; qual flag do perfil
lab causa a divergencia permanece NAO VALIDADO EM HARDWARE.
CORRIGIDO EM 08/09 PELA MINERACAO DA CORRIDA 13:00: dizer que o perfil lab "nao convergiu" era
LEITURA ERRADA da minha parte. Ele CONVERGE -- 39 vezes na 13:00, com Scale=1/Rot=1, os dois
modos fixados e "TargetMode matches GOP -> Supported=TRUE!", o mesmo estado que a producao
comita -- e a convergencia e' DESCARTADA 46 vezes. E' um LACO, nao uma nao-convergencia, e o
que o quebra e' um processo que nasce e morre entre a convergencia e o commit. Ver o bloco da
corrida 13:00 e docs/201.
=============================================================================================
Faixa A (arrumar) em 06/09, tudo offline e sem corrida: A1 caps de tipo de driver passaram a
sair de fonte unica (Bc250GetDriverCapsPolicy) -- SupportNonVGA corrigido para TRUE nos dois
handlers, e o teste que congelava FALSE foi refeito; SupportSmoothRotation e SupportDirectFlip
seguem FALSE por decisao citada na norma, com DirectFlip como DIVIDA DECLARADA. A5 build do SYS
passou a usar /Oy- (FPO desligado, exigencia de WDDM 1.2+ e pre-requisito da call stack do dump).
A8, achado no caminho: build-contract.bat estava em LF e o cmd.exe comia o primeiro caractere das
linhas depois do vcvars -- readonly_miniport_test NUNCA linkava. Convertido para CRLF, a suite
inteira voltou a EXIT=0 e o teste passou a existir. Dois gates novos na auditoria de seguranca.
A6 o binder de laboratorio passou a arquivar o log integral do KMD com SHA-256.
A2 e A3 foram reclassificados por leitura de codigo: o EDID sintetizado JA e coerente com os
56,13 Hz medidos (px 173.611.437 / 2784 / 1111), e derivar o mode set do target do monitor e
circular enquanto o EDID e sintetizado por nos -- depende do item 77 (EDID real por AUX).
Nada disso foi validado em hardware: sao mudancas de contrato, build e coleta de prova.
=============================================================================================
Toolchain migrada em 07/09: VS2026 18.9.1 / MSVC 19.51.36256.0 com SDK+WDK
10.0.28000.2526 (layout 10.0.28000.0). KMD, UMD, lab, RADV gate e suites offline passaram;
pacotes ativo 3.81.0.0 e lab 3.81.1.0 foram assinados e auditados. Esta migracao nao
constitui prova de execucao: efeito no silicio -- NAO VALIDADO EM HARDWARE.
Handoff da toolchain: docs/173_TOOLCHAIN_VS2026_WDK28000.md
=============================================================================================
O RADV entrou no ICD explicito e parou dentro de vkCreateDevice. O KMD criou dois contextos
Node1, mas recebeu zero submit Node1. O ultimo registro foi BuildPagingBuffer Op=11; o dump
de hang ficou com zero bytes e houve reboot nao limpo sem TDR/bugcheck. Ponto interno exato,
compute Vulkan, desktop visivel e jogos -- NAO VALIDADOS EM HARDWARE.
Handoff completo: docs/172_HANDOFF_2026-09-06_20-34.md
=============================================================================================
07/09, tudo OFFLINE e sem corrida -- tres das quatro perguntas de registrador que sustentavam
o B1 fecharam por leitura do NOSSO PROPRIO catalogo, que ja tinha as respostas com P1:
Q4 PAGE_TABLE_DEPTH = Levels-1 = 1 (docs/03, travado por gcvm_context8_cntl_test);
Q5 a PTE carrega PA, nao MC -- PA = offset_de_VRAM + (GCMC_VM_FB_OFFSET << 24);
Q3 o hub esta CERTO (sdma_v5_0.c: ring->vm_hub = AMDGPU_GFXHUB(0)) e a invalidacao confere
nos cinco pontos do catalogo, logo FAULT_STATUS=0 e medicao de verdade. A CSA ja havia sido
descartada. Nao eram lacunas de conhecimento; eram lacunas de BUSCA.
HIPOTESE (H) que substituiu a Q3: as sete classes de fault tem par ENABLE_DEFAULT/INTERRUPT;
o POST tinha os 14 ligados e o nosso alvo liga os 7 DEFAULT e DESLIGA os 7 INTERRUPT. Pela
tabela do catalogo, DEFAULT=1 redireciona para a dummy page. Isso explica as tres medicoes do
B1 juntas: RPTR==WPTR, FAULT_STATUS zero, destino sem dados. NAO PROVADO -- nao se sabe se o
STATUS latcha com DEFAULT=1 e INTERRUPT=0; se latchar, a hipotese esta REFUTADA.
Proxima corrida proposta produz informacao nos DOIS desfechos: ligar as 7 *_ENABLE_INTERRUPT
(0x00555403 | 0x002AAA00 = 0x007FFE03, que difere do POST em so 0x83) e submeter o MESMO IB.
Chega IV de fault -> confirmada, e o FAULT_ADDR diz qual endereco. Nao chega -> refutada, a
engine nao executa o pacote. Declarado em BC250_GCVM_CONTEXT8_CNTL_FAULT_OBSERVABLE_VALUE e
deliberadamente NAO usado por nenhum caminho: armar e decisao do dono do projeto.
Faixa A11 no mesmo dia, offline: geometria de GPUVA passou a ter fonte unica. O blob
UMDRIVERPRIVATE publicava GpuVaBits=48 enquanto o driver declarava VaBits=34 ao WDDM (medido
no log 11:08) -- defeito meu, segunda fonte de verdade, corrigido para sair de runtimeCaps e
travado por gate com teste de mutacao. Tambem: BC250_ADAPTER_GPUVA_BITS passou a derivar,
o journal de GPUVA deixou de aceitar VA ate 2^48 (fail-open; agora 2^34), o bloco de
PROVENIENCIA de bc250_gpuva_space_contract.h estava FALSO (descrevia o desenho plano anterior)
e docs/03 afirmava falsamente que o driver le GCMC_VM_FB_OFFSET em vez de usar constante --
corrigido, com log de DIVERGENCIA no SetRootPageTable e zero mudanca de comportamento.
Build do KMD e do UMD EXIT=0, tres auditorias PASS. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff completo da sessao de 07/09 (offline, zero corridas):
docs/174_HANDOFF_2026-09-07_GPUVA_FONTE_UNICA_E_TELEMETRIA_SDMA.md
Detalhe por pergunta: docs/167_CONSULTA_EXTERNA_IB_PAGINACAO_SDMA_VMID8.md secoes 8b em diante
BLOQUEADOR ATIVO: HKLM/SOFTWARE/BC250/Umd/D3D11CapsProbe ficou em 1 e precisa ser DESARMADO
antes de qualquer bind -- tools/arm-d3d11-caps-probe.ps1 -Disarm. Com ele ligado a UMD publica
a tabela de adapter, o DWM falha no 3DPIPELINESUPPORT e fail-fasta: tela preta de proposito.
O pacote output_lab esta ESTAGNADO (anterior as faixas A11/A12); reconstruir exige o flag de
bring-up destrutivo, que e decisao do dono do projeto.
=============================================================================================
Faixa A12, mesmo dia e tambem offline -- a pergunta 2 do doc 167 deixou de ser vazia. O
docs/168 ja tinha os dados e declarava, na secao 6 item 6, que a semantica de CONTEXT_STATUS
e SKIP_CNTL "nao sei o que significa e nao interpretei". Estava em
inc/linux_ref/gc_10_1_0_sh_mask.h, que existe neste repo -- eu havia escrito no doc 167 que
nao havia copia local do upstream, e isso era FALSO: nao ha .c do amdgpu (logica = H), mas ha
a tabela de mascaras (campos de registrador = P1). Decodificando as leituras de 05/09:
  PHYS/VMID0  CONTEXT_STATUS=0x0D = SELECTED + IDLE + EXPIRED
  EXEC/VMID8  CONTEXT_STATUS=0x09 = SELECTED + EXPIRED, IDLE=0
  PHYS IB_CNTL=0x00000101 e EXEC IB_CNTL=0x00080101
Os dois CONTEXT_STATUS diferem em UM bit -- o IDLE -- e os dois IB_CNTL em UM campo -- o
CMD_VMID. Medicao P0 mais decodificacao P1, nao hipotese. HIPOTESE (H): sob VMID8 a engine
consumiu o pacote do ANEL, latchou IB_BASE e CMD_VMID e PAROU DENTRO DO IB; casa com a
hipotese da pergunta 3. NAO RESOLVE: a variante PHYS foi a IDLE, ou seja TERMINOU, e a prova
de bytes dela tambem falhou -- sem traducao envolvida, aquela falha nao pode ser de GCVM. Sao
DUAS falhas diferentes. Armadilhas catalogadas: IB_RPTR/IB_OFFSET tem o campo comecando no
bit 2 (0x20 sao 8 dwords, nao 32) e EXCEPTION e campo de 3 bits, nao flag; o bit 1 de
CONTEXT_STATUS nao tem nome no upstream e sai como BITS_SEM_NOME. O SubmitCommandVirtual do
Node 0 passou a emitir IB_QUEUE_RAW/DECODED/STATUS/DISCRIMINANTE no caminho de TIMEOUT --
somente leitura, fora do caminho de sucesso para nao pesar o Present a 60 Hz. A nova
telemetria NUNCA RODOU: NAO VALIDADA EM HARDWARE.
=============================================================================================
CORRIDA 07/09 16:19 (P0) e faixa A13 no mesmo dia (OFFLINE, zero corridas). A corrida chegou a
Codigo 0, MMU global Regs=26, CommitVidPn, SetVidPnSourceAddress(0xAB0000) e 12 Present, e a
tela ficou PRETA: quatro dwm.exe fail-fastaram com e0464645/887A0004 e a UMD recusou
OpenAdapter10/10_2 (512 e 1008 vezes). Handoff: docs/179.
Mineracao do log da 16:19 -- contagem exata, P0: 14 "IB EXECUTADO" = 2 PLANO A DIRECT FILL +
12 PLANO A SYNC NOP, ZERO INDIRECT e ZERO linhas IB_QUEUE_*. Ou seja a corrida recomendada da
A12 JA FOI GASTA e nao produziu informacao: os PLANO A tem precedencia e absorveram 14/14, o
INDIRECT nao foi exercitado e a pergunta 2 do doc 167 continua SEM RESPOSTA.
Faixa A13, tudo offline: (1) a frase "IB EXECUTADO" saia nos TRES ramos -- falso sucesso da
Lei 4, corrigido, agora so o INDIRECT tem direito a ela e os PLANO A dizem que a operacao foi
REIMPLEMENTADA no anel e que a GPU NAO leu o DMA buffer; (2) ACHADO NOVO -- a linha
"TRIPE NO SILICIO COMPROVADO WPTR=68 RPTR=52" nao era telemetria errada, era GATE errado: a
perna 1 (RPTR==WPTR) nunca entrava na decisao do bringup e um timeout de 2000 polls passava.
Agora o veredito sai de Bc250ClassifySdmaTripod, o log diz QUAL leitura de RPTR casou e anel
nunca armado sai INDETERMINADO; (3) tunable SdmaIndirectOnly (default 0, fail-closed) desliga
a precedencia dos PLANO A para que o INDIRECT possa ser medido -- armar e decisao do dono.
Testes: submit_command_contract_test e blt_proof_contract_test com casos novos, build-contract
EXIT=0, tres auditorias PASS e os dois portoes novos verificados POR MUTACAO. As cinco strings
do candidato estao no .sys. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff: docs/180_HANDOFF_2026-09-07_A13_HIGIENE_DE_PROVA.md
PACOTE DO ENSAIO DO INDIRECT, assinado e auditado em 07/09 20:03 e NAO INSTALADO:
DriverVer 3.82.0.0, fonte 1C30E3B1..., assinado 0CF9472F..., CAT 7798EC55..., INF D0F73BE5...
O INF declara SdmaIndirectOnly=1 e o manifesto DERIVA esse campo do INF (o empacotador
recusa o pacote se a linha faltar). As cinco strings do candidato estao no SYS assinado.
Novo: tools/arm-sdma-indirect-only.ps1 arma/desarma sem reinstalar, e o binder ganhou dois
preflights -- o do D3D11CapsProbe, que a norma exigia e NAO EXISTIA (medido agora: chave
presente, valor ausente = desarmado), e o aviso do modo de submissao lido do manifesto.
Risco declarado desta corrida: com os PLANO A suprimidos, submissao cuja GPUVA de IB nao
resolva passa a RECUSA (STATUS_IO_DEVICE_ERROR), caminho conhecido do BugCheck 0x119.
A UMD nao foi recompilada e BC250_D3D10_11_NATIVE_READY continua 0: a tela deve ficar preta
de novo. Esta corrida mede o INDIRECT, nao o desktop. NAO VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 07/09 17:13 (P0) -- O IB WDDM REAL EXECUTOU SOB VMID8. Com SdmaIndirectOnly=1 lido
(present=1), o Node 0 escolheu 10 INDIRECT e 4 RECUSADO em 14 submissoes: dez
"IB EXECUTADO (INDIRECT)" de DMA buffers de 64 bytes em VA=0x22F000..0x22F1C0, cada um com
RPTR==WPTR e fence 0x1002..0x1014 sobre poison, ~111 polls, zero timeout e FAULT_STATUS=0.
A divida central -- "o INDIRECT e o unico que nao funciona" -- esta REFUTADA como formulada.
O risco declarado NAO se materializou: as 4 recusas devolveram STATUS_IO_DEVICE_ERROR, sem
BugCheck, e o dispositivo ficou em Codigo 0 ate o rollback (ONLINE ROLLBACK: PASS).
RESSALVA QUE MANDA NO VEREDITO: "IB_TRIPOD PASS" era rotulo de DUAS pernas -- o gate nao
conferia byte nenhum do destino. Pela Lei 2 o B1 NAO FECHA com isso.
A pergunta do tripe de boot fechou por medicao: TRIPE_VEREDITO disse RPTR_Wb=52 RPTR_Reg=68
Casou=REGISTRADOR -- o 52 de todas as corridas era o WRITEBACK atrasado, e o item 63
sobrevive ao gate estrito.
Display avancou e continuou preto: 5 CommitVidPn, 5 SetVidPnSourceAddress, 20 Present (10 com
SDMA=YES), mas 3 BYTE_PROOF INCONCLUSIVO com NonZero=0/256 e BlankStreak=3; 7 falhas de
dwm.exe (e0464645) e dois dumps de ~244 MB e ~203 MB. A imagem que apareceu no fim NAO e
atribuivel ao nosso driver -- coincide com o rollback restaurando o BasicDisplay.
Faixa A14, offline: (1) a perna 3 do tripe passou a EXISTIR no INDIRECT, por canarios nao
nulos plantados antes do disparo (zero sobre zero e inconclusivo; zero sobre poison e prova),
com Bc250PlanDataProofCanaries em fonte unica com o fill direto -- a string IB_TRIPOD PASS
deixou de existir no binario; (2) Trim() na confirmacao visual do binder, que recusou o
DISPLAY_OK digitado por espaco na borda; (3) DestroyProcess passou a invalidar as faixas de
GPUVA do processo -- o REJECT source contract dos Present #16..#20 tinha status=10
OWNER_MISMATCH em faixa viva de um dwm.exe MORTO, defeito nosso de ciclo de vida.
CORRECAO DE ENQUADRAMENTO: eu havia dito que esse REJECT era "a proxima parede do video";
minha propria mineracao refuta -- SlotSeg=0 e status=9 dizem que a origem nao estava
residente porque o DWM morreu antes. A parede continua sendo o backend D3D10/11.
Testes: blt_proof_contract_test com 5 blocos novos, build-contract EXIT=0, quatro auditorias
PASS e os dois portoes novos verificados POR MUTACAO.
PACOTE DA PERNA 3, assinado e auditado, NAO INSTALADO: DriverVer 3.83.0.0, fonte DCA7D06D...,
assinado FDCC5AB2..., CAT F7A6DD4F..., SdmaIndirectOnly=1. NADA DISSO FOI VALIDADO EM HARDWARE.
Handoff: docs/181_HANDOFF_2026-09-07_17-13_INDIRECT_EXECUTOU.md
=============================================================================================
CORRIDA 07/09 17:43 (P0) -- B1 FECHADO NO SILICIO, e BugCheck 0x119 por aritmetica nossa.
Duas submissoes sairam com IB_TRIPE_VEREDITO: TRIPE_PROVADO -- as TRES pernas sobre o DMA
buffer REAL do dxgkrnl: RPTR==WPTR nas duas leituras, fence da GPU sobre poison 0x43DAFFFE e
CINCO canarios nao-nulos plantados antes do disparo, todos substituidos (Divergentes=0,
PostGPU=[0 0 0 0 0], Esperado=0). Zero sobre poison e conclusivo. O IB de paginacao executa
sob VMID8 e PRODUZ o efeito declarado. Isso nao fecha Node 1, desktop nem jogos.
Depois, na submissao FenceId=24: IB_TRIPOD TIMEOUT TargetWPTR=516 RPTR=4612 com
FenceObs==FenceExp==0x1030. 4612 = 4096 + 516 e o anel tem 4096 bytes -- a GPU TERMINOU e a
comparacao CRUA de RPTR nunca fecharia. ~1 s de polling, IB NAO EXECUTADO,
STATUS_IO_DEVICE_ERROR e o VidMm derrubou a maquina: 0x119 Arg1=2 Arg2=0xC0000185.
A aritmetica JA existia no arquivo (Bc250SdmaRptrMatches, com comentario da MESMA falha em
17/17 fills); o caminho INDIRECT nao a chamava. E a auditoria EXIGIA a comparacao crua no
caminho de blt -- o gate obrigava o defeito a existir. Os dois corrigidos.
O mesmo defeito envenenou o diagnostico: IB_QUEUE_DISCRIMINANTE disse anel_drenado=0 com o
anel drenado. Dado novo da telemetria A12, que finalmente disparou: IB_RPTR=15 dw,
IB_SIZE=0, IB_SUB_REMAIN=0, CONTEXT_STATUS=09 (SELECTED+EXPIRED, IDLE=0), ERR_LOG=0x0F.
O FUSIVEL ANTI-BOOT-LOOP FUNCIONOU: apos o reboot o StartDevice foi recusado (State=2,
Boots=6) -- e por isso o dispositivo esta em Codigo 43 com bc250mcdm ligado. NAO ha loop de
boot; eu havia alarmado risco de repeticao antes de ler o fusivel, e retrato.
Faixa A15, offline: Bc250TripodWrapRingPointer em fonte unica, perna 1 do INDIRECT pelo
helper, anel_drenado corrigido, comparacoes cruas removidas dos tres sitios, gate novo que
PROIBE a comparacao crua, e teste com os numeros medidos (4612/4096/516) mais o caso negativo
4608 para a reducao nao virar peneira. build-contract EXIT=0, quatro auditorias PASS.
DIVIDA DECLARADA: o laco do tripe pode gastar ~1 s segurando spinlock em DISPATCH_LEVEL; e
tripe que falha de verdade continua devolvendo STATUS_IO_DEVICE_ERROR -- a resposta correta e
o item 25 (TDR), que segue ABERTO.
ANTES DE NOVA CORRIDA: tools/BC250_RECOVERY.cmd elevado e tools/inspect-boot-fuse.ps1 -Reset
(o fusivel esta TRIPPED e o binder recusa), depois boot frio.
PACOTE 3.84.0.0 assinado e NAO INSTALADO: fonte 36B68E8C..., assinado 1D47F15C...,
CAT 3CF45DF9..., SdmaIndirectOnly=1. NADA DA A15 FOI VALIDADO EM HARDWARE.
Handoff: docs/182_HANDOFF_2026-09-07_17-43_B1_FECHADO_E_0x119.md
=============================================================================================
CORRIDA 07/09 18:16 (P0) -- 32 submissoes INDIRECT, ZERO timeout, ZERO BugCheck, Codigo 0 ate
o rollback, e o B1 REPRODUZIDO (2x TRIPE_PROVADO com 5 canarios, Divergentes=0). Contra a
17:43, que morreu na 15a submissao com 0x119. Zero REJECT source contract e zero
SpaceResolveAddress failed, com 4 [DestroyProcess] GPUVA -- a classe OWNER_MISMATCH da A14.3
desapareceu num cenario com a mesma rotatividade de dwm.exe que a produzia (10 falhas,
2 dumps). RESSALVAS: o RPTR casou com o alvo CRU nas submissoes pos-wrap, entao a assinatura
de falha da 17:43 (RPTR alem de uma volta) NAO reapareceu -- a A15 nao foi contrariada nem
exercitada no modo que importa; e a imagem que voltou foi o ROLLBACK do watchdog as 18:18,
nao o nosso caminho de pixel (30 Present, 10 SDMA=YES, 4 BYTE_PROOF INCONCLUSIVO,
BlankStreak=4). O log vivo integral foi arquivado com SHA-256 (2708 linhas contra 1965 do
coletor) e o watchdog passou a fazer isso sozinho antes do rollback.
=============================================================================================
B3 e B4 em 07/09, OFFLINE e sem corrida.
B3: os PLANO A SAIRAM DA PRECEDENCIA. O default do Node 0 passou a ser somente INDIRECT --
o unico ramo que le o DMA buffer do dxgkrnl, e o que fechou o tripe completo em 17:43/18:16.
O campo do contrato deixou de ser IndirectOnly e virou PlanoAFallbackEnabled; o tunable
SdmaIndirectOnly foi substituido por SdmaPlanoAFallback (default 0, fail-closed) e a string
antiga esta PROIBIDA por gate. INF, manifesto (derivado do INF), binder e o script
arm-sdma-plano-a-fallback.ps1 acompanham. RISCO DECLARADO: submissao cuja GPUVA de IB nao
resolva agora sai como STATUS_IO_DEVICE_ERROR POR DEFAULT, caminho conhecido do 0x119 sem
TDR -- e' deliberado, falhar visivel e melhor que suceder por reimplementacao.
B4: duas coisas medidas. (a) A recusa de submissao no Node 1 era MUDA -- STATUS_NOT_SUPPORTED
sem uma linha de log, e por isso "2 contextos Node1 e zero submits" nao distinguia "o
scheduler nao submeteu" de "nos recusamos". Agora a recusa nomeia o motivo com o BlockMask
decodificado bit a bit, e a abertura do gate tambem aparece. (b) O gate NAO PODIA ABRIR no
pacote de producao: GpuvaMappingsOperational so' vai a 1 quando GpuvaLabPromotion esta
presente e em 1, e essa leitura existia SO' no #else de UNSAFE_STARTDEVICE -- mesma classe
de defeito do SdmaIndirectOnly da A13. Agora e' lida no caminho de producao; o default
continua 0 e a elegibilidade ainda precisa fechar. O caminho depois do gate JA existe
completo (view de contexto, snapshot fixado, cobertura sem gaps, reserva de fence WDDM e
Bc250SubmissionEngineSubmit no anel MEC) -- faltava chegar nele e saber por que nao chegava.
Testes: submit_command_contract_test com a polaridade invertida, build-contract EXIT=0,
cinco auditorias PASS e os portoes novos verificados POR MUTACAO.
PACOTE 3.85.0.0 assinado e NAO INSTALADO: fonte 359B1C88..., assinado 39B76788...,
CAT 8CE3C272..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0.
NADA DE B3/B4 FOI VALIDADO EM HARDWARE.
Handoff: docs/183_HANDOFF_2026-09-07_B3_B4_SUBMISSAO.md
=============================================================================================
B6 / item 25 em 07/09, OFFLINE -- o par de TDR deixou de mentir. Bc250McdmResetFromTimeout e
Bc250McdmRestartFromTimeout escreviam UMA LINHA DE LOG e devolviam STATUS_SUCCESS sem tocar
em registrador nenhum: violacao direta da Lei 4 no par de DDIs em que o dxgkrnl confia para
decidir se o adaptador foi quiescido e se voltou. Agravante: mcdm_callbacks_test EXIGIA esse
STATUS_SUCCESS sem executor instalado -- o teste congelava a mentira, mesma classe do gate
que obrigava a comparacao crua de RPTR a existir (A15).
Agora: ResetFromTimeout faz quiesce FISICO do SDMA0 (snapshot de RPTR/WPTR/STATUS/STATUS1/
ERR_LOG, fecha HardwareEnabled ANTES de mexer no silicio, desliga RB_ENABLE e trava o F32,
SEM desmapear o BAR) e RestartFromTimeout rearma o anel dos dois lados e PROVA com NOP
fisico e fence sobre poison -- se a fence nao retirar, HardwareEnabled volta a FALSE e a DDI
devolve STATUS_DEVICE_NOT_READY em vez de declarar adaptador pronto. Os executores entram
por Bc250McdmInstallTdrExecutors (mesmo padrao dos outros), e ponteiro ausente = falha
fechada por construcao. ResetEngine continua recusando, com o motivo honesto que ja tinha.
Testes: bloco 9 do mcdm_callbacks_test reescrito (sem executor -> DEVICE_NOT_READY; executor
FALSE -> DEVICE_NOT_READY; TRUE -> SUCCESS; desinstalar volta ao fail-closed), gate novo
audit_tdr_pair_has_backend que tambem detecta se o TESTE voltar a exigir a mentira,
build-contract EXIT=0, seis auditorias PASS e mutacao verificada.
O ITEM 25 CONTINUA ABERTO: falta a corrida com hang induzido medindo deteccao -> reset ->
readmissao de submissao -> display vivo. DIVIDA DECLARADA: nao existe hang induzido em
tools/, e a deteccao de duas passagens do bc250_tdr_recovery_contract NAO esta ligada a
watchdog interno -- quem detecta hoje e o dxgkrnl pelo timeout de 2 s (item 24).
PACOTE 3.86.0.0 assinado e NAO INSTALADO: fonte 11F5EEAE..., assinado 85042920...,
CAT FA713F19..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0.
NADA DO B6 FOI VALIDADO EM HARDWARE.
Handoff: docs/184_HANDOFF_2026-09-07_B6_TDR_COM_BACKEND.md
=============================================================================================
ITENS 24 e 25 em 07/09, OFFLINE -- as duas dividas do TDR fechadas em codigo.
item 24: TERCEIRO falso sucesso do dia, e o pior colocado. Bc250McdmQueryEngineStatus punha
EngineStatus.Responsive=1 SEM OLHAR REGISTRADOR, e o dxgkrnl chama essa DDI DURANTE o TDR
para saber se a engine esta viva -- o driver sabotava a propria recuperacao. E, de novo, o
mcdm_callbacks_test EXIGIA Responsive==1 sem probe instalada. Agora a resposta sai de duas
passagens de RPTR/WPTR pelo contrato Bc250TdrDetectHang: sem probe -> NOT_SUPPORTED; probe
que nao mede -> DEVICE_NOT_READY; medicao viva -> Responsive=1; medicao travada ->
Responsive=0 com STATUS_SUCCESS. "Nao sei" nao vira "esta viva". ARMADILHA EVITADA por
medicao anterior: os quatro ponteiros sao REDUZIDOS a uma volta do anel antes da comparacao
-- com os valores crus de 17:43 (RPTR=4612, alvo 516, anel 4096) o contrato leria trabalho
pendente num anel drenado e diria TRAVADA para engine ociosa.
item 25: o hang induzido que faltava. POLL_REGMEM esperando 0xDEADDEAD no slot de fence, com
retry e intervalo no teto: a engine para DE VERDADE (o gate PROIBE a variante simulada por
KeStallExecutionProcessor). O submit ACEITA e NAO notifica completion, de proposito -- e a
fence pendente que faz o timeout de 2 s do dxgkrnl disparar o TDR. Fail-closed em tres
camadas: tunable TdrInduceHang default 0, exige present==1 && valor==1, e disparo UNICO por
bind por InterlockedCompareExchange. Ferramenta: tools/arm-tdr-induce-hang.ps1; o manifesto
declara TdrInduceHang/TdrInduceHangArmed DERIVADOS do INF e o binder avisa alto.
Testes: blocos 6 e 9 do mcdm_callbacks_test reescritos com stubs (inclusive um que escreve 1
e exige que a DDI zere), dois gates novos, build-contract EXIT=0, oito auditorias PASS e
mutacao verificada nos dois.
O ITEM 25 CONTINUA ABERTO: so' fecha com a corrida do hang induzido -- e, por exigir DISPLAY
SOBREVIVENDO, o gate de saida da P6 so' fecha de verdade junto com o B7. Risco declarado da
corrida: BugCheck e reboot sao desfechos POSSIVEIS e sao resultado do ensaio.
PACOTE 3.87.0.0 assinado e NAO INSTALADO: fonte A4F6892A..., assinado AC4E9276...,
CAT 3FBCEDA2..., SdmaPlanoAFallback=0, GpuvaLabPromotion=0, TdrInduceHang=0.
NADA DOS ITENS 24/25 FOI VALIDADO EM HARDWARE.
Handoff: docs/185_HANDOFF_2026-09-07_ITEM24_25_TDR_MENSURAVEL.md
=============================================================================================
CORRECAO DA ORDEM DO ENSAIO TDR, 07/09, OFFLINE: a tentativa de armar pelo registro antes
da instalacao foi corretamente recusada. A chave de servico so' nasce pelo AddReg do INF e o
KMD le TdrInduceHang no StartDevice; escrever depois do bind so' vale no proximo start.
O INF fonte continua fail-closed em 0. O empacotador agora materializa 1 apenas no INF do
candidato, exige versao nova e aceite explicito; o binder exige o mesmo aceite.
PACOTE TDR 3.88.0.0 assinado, auditado e NAO INSTALADO:
BuildKind=MCDM_TDR_INDUCED_HANG, fonte 5B6F07ED..., assinado C4498C9B...,
CAT 5F0E41F6..., INF 45CDC569..., TdrInduceHang=1 derivado do INF empacotado.
Package audit PASS, safety audit com gate da ordem PASS, tentativa sem aceite RECUSADA.
O ITEM 25 CONTINUA ABERTO E NAO VALIDADO EM HARDWARE.
Handoff: docs/186_HANDOFF_2026-09-07_TDR_ORDEM_E_PACOTE_ARMADO.md
=============================================================================================
CORRIDA TDR 07/09 20:41 (P0): o hang induzido disparou o dxgkrnl; ResetFromTimeout fez
quiesce com RPTR=68, WPTR=92 e ERR_LOG=0x0F, mas o restart antigo nao abortou o POLL_REGMEM.
O SYNC_NOP ficou em WPTR=40/RPTR=68, fence no poison, e RestartFromTimeout devolveu
STATUS_DEVICE_NOT_READY. Seguiu BugCheck 0x116 com esse status em Arg3. QueryEngineStatus
nao apareceu: item 24 nao foi exercitado nessa rota. OFFLINE depois da corrida: restart
agora pulsa GRBM_SOFT_RESET.SDMA0, reconstrói integralmente o anel, exige readback e so'
depois prova com NOP/fence. Build e safety audit PASS; dois gates provados por mutacao.
PACOTE TDR 3.89.0.0 assinado, auditado e NAO INSTALADO: fonte CC8B1615..., assinado
9573685E..., CAT 0A765FFB..., INF 53F5900D..., TdrInduceHang=1. O ITEM 25 CONTINUA ABERTO;
A CORRECAO DO RESET INTEGRAL NAO FOI VALIDADA EM HARDWARE.
Handoff: docs/187_HANDOFF_2026-09-07_TDR_0x116_E_RESET_INTEGRAL.md
=============================================================================================
CORRIDA TDR 07/09 21:49 (P0): o Windows gerou live dump 0x117 atribuido a bc250mcdm.sys.
A cadeia fechou HANG_INDUZIDO -> TDR_QUIESCE -> SYNC_NOP TRIPOD PASS em 2 polls ->
TDR_RESTART PROVADO. Depois do restart, 40 INDIRECT executaram, quatro com tripe completo,
houve 36 Present, zero recusa/timeout e Codigo 0 ate o rollback. O reboot foi planejado pelo
watchdog, nao BugCheck. A tela continuou preta: DWM fail-fast sem backend D3D11, superficies
zeradas. Deteccao, reset, restart e readmissao estao VALIDADOS EM HARDWARE. O item 25 segue
ABERTO somente na perna display visivel sobrevive, dependente do B7. Nao repetir TDR antes
do B7. Handoff: docs/188_HANDOFF_2026-09-07_TDR_RECUPEROU_E_READMITIU.md
=============================================================================================
CORRIDA 08/09 11:48 (P0) -- BIND DO CANDIDATO LAB 3.90.1.0 EM CODIGO 43, e a corrida do B7
foi GASTA sem medir o Node 1. Cadeia medida: fusivel armado, EDID sintetizado, GOP adquirido
2560x1080, PSP+F32 OK, TRIPE_PROVADO de boot (RPTR_Reg=68, Casou=REGISTRADOR, Polls=1) e MMU
global Regs=26 -- e entao StartDevice devolveu 0xC00000BB, sem UMA linha de bring-up fisico.
O gate compute-display marcou UMD_NODE1_SKIPPED corretamente (sem primaria, sem CommitVidPn):
o runner do tripe Node 1 nem chegou a ser elegivel. Node 1 segue sem qualquer medicao.
CAUSA RAIZ FECHADA por codigo + telemetria, contra o bind 09:50 do MESMO perfil que deu
Codigo 0: um bloco novo no StartDevice mapeia Core.MmioTransport para programar a MMU global e
seguia com ele MAPEADO ate o SEGUNDO preflight readonly. Bc250DxgkDdiStartDeviceReadonly
recusa transporte nao-vazio com STATUS_INVALID_PARAMETER na guarda de ENTRADA -- antes de
escrever LastStage. Probes: 09:50 SecondPreflightStage=8 e Gate=1; 11:48 estagio=3 e Gate=0.
O 3 e' RESIDUO do PRIMEIRO preflight -- qualquer caminho que passe da guarda escreve 1
(INTERFACE_ACCEPTED), logo o 3 PROVA que a segunda chamada saiu antes disso. Nao e' hipotese.
DOIS DEFEITOS, e o segundo escondeu o primeiro: com o gate fechado, TODO o bring-up fisico foi
pulado, persistentStatus ficou no INICIALIZADOR (STATUS_NOT_SUPPORTED), e o rodapé do
StartDevice o publicou como veredito porque PersistentSubmission=1 e persistentReady=FALSE.
O 0xC00000BB NAO foi produzido por medicao nenhuma. Terceira corrida seguida perdida em
defeito de host, nao de silicio (as outras: comparacao crua de RPTR na 17:43; ordem do
registro no ensaio de TDR).
FAIXA A16, OFFLINE e sem corrida -- as tres correcoes: (1) o transporte volta a VAZIO antes do
segundo preflight, so' no caminho de laboratorio e com falha fechada se o unmap nao fechar
(producao mantem o mapa por design e nao tem segundo preflight); desmapear e' seguro porque a
MMU ja esta programada e cada transacao do bring-up mapeia/desmapeia sozinha --
Bc250MmioTopologyMapReadonly devolve WRONG_STATE se achar o transporte mapeado, entao deixa-lo
mapeado quebraria o bring-up INTEIRO, nao so' o preflight; (2) probe novo
SECOND_PREFLIGHT_STATUS com o NTSTATUS CRU mais log com o estado do transporte -- o estagio
sozinho e' ambiguo POR CONSTRUCAO; (3) persistentAttempted: o rodapé so' pode trocar o status
pelo do ensaio de submissao persistente se esse ensaio TIVER ACONTECIDO, e status real ja
carregado manda sobre ensaio que nunca rodou.
Testes: dxgk_readonly_ddi_test reproduz a 11:48 offline (runtime STAGED + transporte MAPEADO
-> INVALID_PARAMETER, LastStage intocado, RuntimeStaged=1) e prova o contraponto (transporte
vazio -> estagio 8, gate 1). Gate novo audit_second_preflight_transport_empty. build-contract
EXIT=0, tres auditorias PASS. QUATRO mutacoes verificadas -- e a primeira reprovou o MEU
proprio gate: a busca casava com o PRIMEIRO preflight, a janela saia invertida e o gate
passava sem examinar nada. Falso negativo silencioso, corrigido e re-testado.
PACOTE 3.92.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 76E5ADCA..., assinado
A1973547..., CAT 99E8E5BE..., INF 8E960DA6..., UMD 2927FCBC..., SkipHwBringup=0,
GpuvaLabPromotion=0, Node1TripodDefaultArmed=0, D3D10_11NativeReady=0. Package audit PASS.
O rebuild foi PROVADO pelas cinco marcas da A16 no SYS assinado (quatro strings de log mais o
nome de probe SecondPreflightStatus em UTF-16), e NAO pelo schema de telemetria: eu havia
proposto o schema como verificador e estava ERRADO -- $labSchemaVersion e' constante fixa do
empacotador, gravada no INF, e nao deriva do enum; nunca ia se mover sozinha. Corrigido no
mesmo dia: o schema subiu para 135 porque o campo novo mudou o conjunto, e a versao fonte foi
para 3.92.0.0 para nao existirem dois artefatos com o mesmo DriverVer.
A A16 FOI VALIDADA EM HARDWARE NA CORRIDA 12:36 -- ver o bloco seguinte. No momento em que
este bloco foi escrito ela ainda nao tinha sido; a linha fica como registro da ordem dos fatos.
O pacote 3.90.1.0 instalado as 11:48 JA FOI REVERTIDO pelo watchdog (BasicDisplay Codigo 0) e
C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO: boot frio antes de qualquer novo bind.
Handoff: docs/199_HANDOFF_2026-09-08_A16_ORDEM_DO_SEGUNDO_PREFLIGHT.md
=============================================================================================
CORRIDA 08/09 12:36 (P0, candidato 3.92.1.0) -- A16 VALIDADA EM HARDWARE, e Codigo 43 por
defeito DISTINTO e ANTERIOR. As duas linhas novas responderam a pergunta da corrida:
"Transporte devolvido ao preflight: unmap=ok (State=0)" e "Segundo preflight: status=0xC00000BB,
stage=8, gate=1, MmioState=0". Contra a 11:48: estagio 3->8, gate 0->1 e V5StartDeviceStatus
0xC00000BB->0. O StartDevice DEVOLVEU SUCESSO e o bring-up seguiu ate ChildRelations
(DP ChildUid=1). O 0xC00000BB que sobrou no SecondPreflightStatus e' o NOT_SUPPORTED LEGITIMO
do fim do gate readonly -- e o probe novo existe exatamente para separar os dois.
PAREDE NOVA, fechada por aritmetica: RuntimeCapsStatus=0xC0000182. O ramo SEALED de
Bc250ActiveQueryRuntimeCaps exige SecondPreflightStage==PREFLIGHT_READY e, ate a A16 destravar
o segundo preflight, NUNCA tinha executado num bind real -- todas as corridas caiam no
fallback. O DEFEITO E' ANTERIOR A A16; a correcao apenas o tornou alcancavel.
A conta, com os numeros medidos: janela de CPU 268.435.456; localEnd (plano de firmware)
268.161.024; reservas = scanout offset 0 len 11.059.200 + dummy page da MMU offset 268.431.360
len 4.096 + carveout opaco offset 268.435.456 (pulado) -- soma 279.498.752, que e' exatamente
RuntimeInventoryReservedLow32. A dummy e' alocada TOP_DOWN (DummyPA=0x45FFFF000 menos base
0x450000000 = offset 268.431.360) e termina no TOPO da janela; o laco tratava TODA reserva
como PREFIXO a pular, entao localStart=268.435.456 >= localEnd=268.161.024 e o segmento
colapsava. DRIVERCAPS entao devolveu DEVICE_NOT_READY -- o c00000a3 do fusivel. Agravante: a
dummy JA era tratada corretamente ~60 linhas abaixo truncando localEnd; era contada DUAS vezes
e prevalecia a contabilidade errada.
CORRECAO (offline): prefixo empurra o inicio, reserva alta trunca o fim, com o passe de prefixo
repetindo ate estabilizar porque a lista nao e' ordenada. Segmento resultante
[11.059.200, 268.161.024) = 257.101.824 bytes, com a dummy fora dele.
Teste Bc250RunSealedSegmentReservationTest com os numeros da corrida sem arredondar, em tres
casos (layout medido passa; reserva que cobre tudo RECUSA, para a assercao nao virar carimbo;
reserva alta abaixo do firmware trunca em vez de empurrar). Mutacao 5 verificada: removendo a
guarda de prefixo a suite reprova. build-contract EXIT=0, tres auditorias PASS.
RESSALVA DE ESCOPO DO TESTE: active_miniport_test linka a variante OFFLINE do callbacks, onde
QUERYSEGMENT4 esta compilado fora; por isso a assercao e' por DRIVERCAPS. A prova byte a byte
do tamanho do segmento sai da PROXIMA corrida, na linha [McdmQAI] QUERYSEGMENT4 (esperado
VRAM=0xF52A000 = 257.101.824).
A CORRECAO DO SEGMENTO SEALED NAO ESTA VALIDADA EM HARDWARE: exige rebuild do SYS, pacote novo
e corrida. O 3.92.1.0 instalado as 12:36 JA FOI REVERTIDO pelo watchdog (BasicDisplay Codigo 0)
e C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO.
Handoff: docs/200_HANDOFF_2026-09-08_12-36_A16_VALIDADA_E_SEGMENTO_SEALED.md
=============================================================================================
CORRIDA 08/09 13:00 (P0, candidato 3.92.1.0 SYS 7B34393F) -- CODIGO 0 e o SEGMENTO SEALED
VALIDADO NO SILICIO, com os dois numeros previstos ANTES da corrida: RuntimeCapsBaseLow32=
11.059.200 (0xA8C000), RuntimeCapsLocalLow32=257.101.824 (0xF531000), RuntimeCapsStatus=0,
FullCapsReady=1, Segment4Ready=1, QUERYSEGMENT4 com VRAM=0xF531000 e SetRootPageTable com
readback OK PT_BASE=450a8c000 sob VMID8. Duas faixas fechadas no silicio hoje: A16 (12:36) e
segmento sealed (13:00). Correcao de registro: eu escrevi o alvo como 0xF52A000; o HEX estava
errado, o decimal previsto 257.101.824 estava certo e foi o que apareceu.
DOIS ENQUADRAMENTOS MEUS CORRIGIDOS PELA MINERACAO:
(1) o VidPn NAO e' a parede -- ele CONVERGE. 39 convergencias completas ("TargetMode matches
GOP -> Supported=TRUE!" com Scale=1/Rot=1 e os dois modos fixados, o mesmo estado que a
producao comita) contra 46 reinicios do zero. Nao e' "nao converge"; e' um LACO.
(2) a hipotese de que o E_NOTIMPL da nossa UMD matava o processo foi REFUTADA na mesma
corrida: NENHUM BC250_UMD_LOG_*.txt foi criado, isto e', bc250umd.dll nunca carregou -- e a
DLL esta instalada em System32. Na corrida de PRODUCAO de hoje 08:44 a mesma DLL carregou em
dwm.exe e recusou OpenAdapter10/10_2 com E_NOTIMPL, com cinco logs de prova. SAO DUAS PAREDES:
producao comita -> DWM roda -> UMD recusa (parede do D3D11); laboratorio converge -> um
processo nasce e MORRE -> nunca comita -> DWM nunca roda -> UMD nunca carrega (parede
ANTERIOR, SEM CAUSA IDENTIFICADA). Confianca da ausencia de logs: P2.
Entre a convergencia e o descarte o log mostra CreateProcess -> CreateDevice -> CreateContext
(Node 0) -> DestroyProcess: 8/9/8/7, TODOS Node 0, zero Node 1, zero submissao. O dxgkrnl
registrou LiveKernelEvent P1=193 (0x193 VIDEO_DXGKRNL_LIVEDUMP) P2=804 P3=c0000001
(STATUS_UNSUCCESSFUL) P4=108 as 13:02:30. O SUBCODIGO 804 NAO FOI DECODIFICADO -- nao ha fonte
neste repositorio e nao se infere.
INSTRUMENTACAO (offline): o log so' tinha handles opacos e nao dizia QUEM morre.
DXGKARG_CREATEPROCESS carrega pProcessName e Flags desde WDDM 2.4 (d3dkmddi.h) e declaramos
2.7, entao o nome vem do CONTRATO, sem API nao documentada. Bc250McdmCreateProcess passou a
registrar Nome= e Flags(System/Gdi/Vm/VmWorker/Isolated), com guarda de pProcessName NULL,
ProcessNameLength em WCHARs (nao bytes) e truncamento em 63 chars. ARMADILHA REGISTRADA: a
copia caractere a caractere foi reconhecida pelo /O2 e reemitida como memcpy, ausente do link
offline das suites (LNK2019); RtlCopyMemory tem o mesmo destino. Resolvido com ponteiro de
destino volatile. build-contract EXIT=0, tres auditorias PASS.
A INSTRUMENTACAO NUNCA RODOU: NAO VALIDADA EM HARDWARE. O INF fonte ja esta em 3.93.0.0.
NOTA DE REGISTRO: 3.92.1.0 designa DOIS artefatos -- SYS A1973547 (A16, corrida 12:36) e SYS
7B34393F (A16 + segmento, corrida 13:00) --, porque eu nao subi o INF fonte antes do
empacotamento. Desambiguavel pelo hash, que o binder imprime e o manifesto registra.
O 3.92.1.0 foi revertido pelo watchdog (BasicDisplay Codigo 0) e
C:\BC250_REQUIRE_CLEAN_BOOT.json esta ARMADO.
Handoff: docs/201_HANDOFF_2026-09-08_13-00_SEGMENTO_VALIDADO_E_O_LACO_DO_VIDPN.md
=============================================================================================
CORRIDA 08/09 14:06 (P0, candidato 3.93.1.0 SYS EF91F17F) -- a instrumentacao pagou na PRIMEIRA
tentativa e respondeu as DUAS perguntas. Estado bom mantido: V5StartDeviceStatus=0, estagio 8,
gate 1, RuntimeCapsStatus=0, base 11.059.200 e segmento 257.101.824. Laco do VidPn identico:
39 convergencias, 46 reinicios, zero CommitVidPn, zero Present.
(1) QUEM MORRE: pProcessName veio NULL (permitido pelo contrato), mas as FLAGS responderam --
o processo que repete e' Gdi=1/System=0, isto e', o cliente GDI, NAO o dwm.exe. Casa com a
medicao da 13:00 (nenhum BC250_UMD_LOG criado): o DWM nunca entra em cena neste perfil e a
bc250umd.dll nunca carrega. A parede do D3D10/11 existe, mas e' POSTERIOR a esta.
(2) ALGUMA DDI NOSSA RECUSOU? NAO -- zero RECUSADO na corrida inteira. Hipotese eliminada.
O ACHADO: o OK estava MENTINDO. Oito vezes
"[McdmCreateContext] OK: Node=0, ..., DmaBuf=0x0, SegSet=0x0". Contexto com DmaBufferSize=0 e'
inutil -- o dxgkrnl nao tem como alocar DMA buffer. Causa: Bc250ActiveCreateContextObject faz
"*ContextInfo = context->ContextAbi", SOBRESCREVENDO os defaults que Bc250McdmCreateContext
acabara de escrever, e ContextAbi so' era populado dentro do bloco
"#if !defined(BC250_ENABLE_UNSAFE_STARTDEVICE_HW_BRINGUP)" -- so' na imagem de PRODUCAO. Na de
laboratorio ContextAbiReady ia a 1 com a estrutura inteiramente zerada. TERCEIRA VEZ da mesma
classe: A13 (tunable de submissao), B4 (GpuvaLabPromotion) e agora ContextAbi -- campo escrito
em UM SO' ramo do #if, invisivel no outro.
CORRECAO (offline): fonte unica Bc250ActiveInitializeContextAbi chamada nos TRES sitios antes
de declarar ContextAbiReady. Dois gates novos: audit_context_abi_before_ready (entre cada
ContextAbiReady=1 e o anterior tem de haver chamada a fonte unica -- SEM janela de linhas --,
e DmaBufferSize nao pode ser escrito fora do helper) e audit_process_lifecycle_ddis_speak
(todo return nao-SUCCESS depois do log de entrada exige log nas 6 linhas anteriores, nas tres
DDIs de ciclo de vida). QUATRO mutacoes verificadas (6..9), e DUAS versoes iniciais dos MEUS
gates foram reprovadas por elas: a primeira exigia so' a presenca da string RECUSADO (passava
removendo uma de tres) e a segunda usava janela de 12 linhas (reprovava o sitio de producao
legitimo, com 19 linhas de comentario). Ambas refeitas com invariante sem janela.
O gate do B3 pegou o MEU PROPRIO comentario por citar o nome banido do tunable antigo.
build-contract EXIT=0, tres auditorias PASS.
CONFIANCA: DmaBuf=0x0 publicado e' P0 (medido, oito vezes); que ISSO mate o cliente GDI e' H.
Criterio da proxima corrida: "DmaBuf=0x10000, SegSet=0x2" e o ciclo CreateProcess->
DestroyProcess PARAR, com CommitVidPn aparecendo -> hipotese confirmada; ciclo continuar com
DmaBuf correto -> REFUTADA, e o campo deixa de ser suspeito. Os dois desfechos informam.
A CORRECAO NAO ESTA VALIDADA EM HARDWARE. INF fonte em 3.94.0.0; candidato sai 3.94.1.0.
3.93.1.0 revertido pelo watchdog; BasicDisplay Codigo 0 e marcador de boot frio ARMADO.
Handoff: docs/202_HANDOFF_2026-09-08_14-06_QUEM_MORRE_E_O_CONTEXTO_ZERADO.md
=============================================================================================
CORRIDA 08/09 14:26 (P0, candidato 3.94.1.0 SYS 8CA949DC) -- O COMMITVIDPN ACONTECEU. O
criterio escrito ANTES da corrida bateu inteiro: DmaBuf 0x0->0x10000 e SegSet 0x0->0x2,
CreateProcess 8->2, DestroyProcess 7->0, convergencias/reinicios 39/46 -> 8/8, e CommitVidPn
0 -> 1. O laco que consumiu quatro corridas ACABOU, e a causa era a ABI de contexto publicada
zerada (doc 202). Pela primeira vez no perfil lab a cadeia inteira acordou: SetVidPnSourceAddress
(SourceId=0, Segment=1, Addr=0xAAC000), SetVidPnSourceVisibility(Visible=1), 60
GetStandardAllocationDriverData, 20 CreateAllocation, 30 OpenAllocation e 8 CloseAllocation --
TODAS com status 0 -- mais 367 ControlInterrupt de VSYNC. Zero StopDevice, zero BugCheck,
zero RECUSADO; adaptador vivo e estavel ate o rollback programado. O estado bom das faixas
anteriores se manteve: estagio 8, gate 1, base 0xA8C000, segmento 0xF531000.
A PAREDE NOVA E' A PAREDE ANTIGA DA PRODUCAO: dwm.exe agora RODA neste perfil e fail-fasta
com 0xe0464645 em KERNELBASE.dll+0x12d862, oito vezes entre 14:26:12 e 14:26:54 -- a MESMA
assinatura da corrida de producao. Present=0 porque o DWM nao sobrevive para apresentar.
MUDANCA DE ESTADO DO PROJETO: ate hoje havia DUAS paredes -- a de producao (backend D3D10/11,
conhecida) e a de laboratorio (um processo que nascia e morria, SEM causa identificada). Os
dois perfis CONVERGIRAM. Resta UMA parede para o desktop, com nome e arquivo:
Bc250Umd10CreateDevice devolve E_NOTIMPL porque BC250_D3D10_11_NATIVE_READY=0, fail-closed
deliberado pela Lei 4.
RESSALVA QUE MANDA NO VEREDITO: NENHUM BC250_UMD_LOG foi criado nesta corrida, apesar das oito
mortes do dwm.exe. Que o DWM fail-fasta e' P0; que ele fail-fasta POR CAUSA da recusa da nossa
UMD e' P2 -- se o OpenAdapter chegou a ser consultado antes do fail-fast e' INDETERMINADO
nesta corrida. O modulo da falha e' KERNELBASE (RtlFailFast), compativel com o caminho
CreateD3D11Device -> DXGI_ERROR_UNSUPPORTED ja documentado por dump, mas compatibilidade nao
e' prova. O LiveKernelEvent P1=193 P2=804 P3=c0000001 P4=108 reapareceu as 14:28:32 e o
SUBCODIGO 804 CONTINUA NAO DECODIFICADO.
PROXIMA ETAPA, E FICOU BARATA: o gate do runner do Node 1 exige primaria, CommitVidPn e
SetVidPnSourceAddress -- os TRES agora acontecem. Em todas as corridas anteriores o runner era
corretamente marcado UMD_NODE1_SKIPPED porque o pre-requisito nao existia. O pacote 3.94.1.0
JA ESTA EM DISCO e e' o mesmo que produziu o CommitVidPn: basta boot frio e o mesmo bind com
-RunUmdNode1Tripod. O Node 1 segue SEM QUALQUER MEDICAO desde o inicio do projeto.
build-contract EXIT=0, tres auditorias PASS. Pacote revertido pelo watchdog; BasicDisplay
Codigo 0 e marcador de boot frio ARMADO.
Handoff: docs/204_HANDOFF_2026-09-08_14-26_COMMITVIDPN_E_A_PAREDE_UNICA.md
Dossie para auditoria externa da sessao inteira: docs/203_AUDITORIA_EXTERNA_2026-09-08.md
=============================================================================================
CORRIDA 08/09 15:18 (P0) e faixa B7.1 offline -- B2 FECHADO NO SILICIO E BLOQUEADOR DO NODE 1
ISOLADO. Com -RunUmdNode1Tripod e arquivamento B2 ativo, o pacote lab 3.94.1.0 alcancou Codigo 0
e os logs da UMD foram capturados (bc250_umd_log.txt, 43 KB).
dwm.exe (pid=3704): OpenAdapter10_2 e OpenAdapter10 recusados com E_NOTIMPL por
BC250_D3D10_11_NATIVE_READY=0 -> A CAUSA DO FAIL-FAST DO DWM E' AGORA PROVADA NO SILICIO (P0),
nao mais P2.
Harness Node 1 (pid=788 bc250-umd-node1-tripod.exe): executou sob watchdog, autenticou o token,
criou a paging queue e contexto de compute no Node 1, e alocou 64 KB de command buffer.
Em MapAllocation: o runtime WDDM retornou hr=0x8000000A (E_PENDING) com VA=0x11000 e
PagingFence=7001. A winsys da UMD tratou FAILED(0x8000000A) como erro fatal (CALLBACK_FAILED).
Faixa B7.1 (offline): Bc250WinsysMapAllocation e Bc250WinsysMakeResident corrigidos para
aceitar E_PENDING (0x8000000AL), Bc250WinsysWaitPaging retorna OK quando PagingFenceValue=0.
Suite TestPagingPendingStatus adicionada em bc250_wddm_winsys_test.c; build-contract EXIT=0,
build-umd EXIT=0, 17 portoes de seguranca PASS.
PACOTE LAB 3.94.2.0 assinado e auditado (SYS 8CA949DC, UMD 35FBD781, CAT 5FB2E86E).
NADA DA FAIXA B7.1 FOI VALIDADO EM HARDWARE.
Handoff: docs/205_HANDOFF_2026-09-08_15-18_B2_PROVADO_E_MAPALLOCATION_PENDING.md
=============================================================================================
CORRIDA 10/09 11:22 (P0, lab 3.126.1.0) e AUDITORIA de 10/09 que corrigiu o enquadramento dela.
NOTA DE MANUTENCAO: esta barra estava parada em 08/09; as corridas de 09 e 10/09 entraram na
BASE_DE_AVANCO.md e nao aqui. O detalhe completo delas esta lá; aqui fica o que manda no veredito.
PROVADO NO SILICIO (P0): (1) a PAREDE UNICA DO D3D11 CAIU -- dwm.exe (pid 1204), Explorer.EXE
(5220) e dxgiadaptercache.exe (1964) carregaram a bc250umd.dll e receberam "OpenAdapter10_2:
Interface=0x000B000B, Version=0x00020009 -> S_OK (BC250 Native)"; (2) OpenAllocation fechou --
41/41 e 29/29 em status 0 nos binds 11:10 e 11:21, contra 5.745 falhas em 7.154 chamadas no bind
09:34; (3) 769 submissoes INDIRECT consecutivas com 12 voltas completas do anel SDMA; (4) a causa
raiz do BugCheck 0x119 no FenceId=770 e' VISIBILIDADE DE HDP, nao execucao -- RPTR=50244 =
12*4096+1092 no writeback E no registrador, STATUS1=000003FF (engine OCIOSA), RPTR_SAMPLES plano
por 200.000 polls, e FENCE_VISIBILIDADE antes_hdp=43DAFFFE depois_hdp=00001304 esperado=00001304.
A fence apareceu NO FLUSH. O flush ativo no laco de poll (bc250_sdma_present.c) ataca a causa
certa e teria saido no primeiro poll -- NAO VALIDADO EM HARDWARE.
RETIRADO DO VEREDITO POR NAO SE SUSTENTAR: o "Marco Historico: desktop real D3D11 exibido no
monitor" NAO ACONTECEU nesta corrida. As linhas Present #1 DESKTOP REAL DETECTADO e BYTE_PROOF
PASS citadas estao no offset 8.161.041 do log cumulativo, dentro da janela do bind das 09:34
(7.659.265..14.831.198); o bind das 11:22 (start_offset 15.009.172) teve 10 Presents, ZERO
deteccao de desktop, ZERO BYTE_PROOF e ZERO SDMA=YES. Tres binds foram fundidos num so' marco.
E o detector dispara com NonZero=1/256 -- 255 dos 256 dwords amostrados sao ZERO, e SCANOUT,
PRIMARY e SLOT_WORD amostraram 0/256: frame preto com um dword nao-nulo. No bind das 11:10, com
OpenAlloc 100% OK, foram 194 Presents e 10 blts, TODOS BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO),
BlankStreak=4. E nenhum pixel veio do D3D11: o log da UMD do DWM nao tem UMA linha de Present,
Flush ou blt; tem 1 "DrawIndexed: RECUSADO" e termina 2 s depois do load dentro de 4.159
CheckMultisampleQualityLevels. A imagem distorcida vista pelo operador e' atribuivel a transicao
de adaptador/BasicDisplay -- o MESMO engano ja cometido e corrigido em 07/09 18:16. O stride skew
7680->10240 e' H, nao medicao.
DIVIDA DISTINTA E ABERTA, que o flush de HDP NAO cobre: nos dois timeouts de 09/09 o RPTR congelou
EXATAMENTE em IndirectEndDW (1056=264dw; 1568=392dw), o flush NAO revelou a fence (depois_hdp=
43DAFFFE em 31 de 31 ocorrencias do corpo inteiro) e um deles trouxe GFXHUB_FAULT_SNAPSHOT
STATUS=00841B40 ADDR=0x82C -- fault de traducao REAL. Os dois eram o IB pesado Fill=1/11.075.584.
O caminho TIMEOUT -> STATUS_IO_DEVICE_ERROR -> 0x119 continua vivo para essa classe.
FAIXA C1, OFFLINE -- o fallback de CPU no Draw foi REMOVIDO. Achado por leitura de codigo no
candidato 3.126.1.0 ainda nao commitado: Bc250UmdCompositeDraw atendia pfnDraw e pfnDrawIndexed
com um laco de memcpy entre os shadows de CPU, marcava handled=TRUE ANTES de tentar qualquer
caminho de GPU e devolvia sucesso silencioso -- a linha honesta "DrawIndexed: RECUSADO" desaparecia
e shader, VB, IB, topologia, blend e escala de viewport eram ignorados. E' fallback de software
disfarcado de renderizacao (Lei 4) e tornaria INFALSIFICAVEL qualquer desktop na proxima corrida.
AGRAVANTE: o bc250_d3d_umd_test.c EXIGIA esse memcpy, congelando a mentira -- TERCEIRA ocorrencia
da classe (A15 obrigava a comparacao crua de RPTR; mcdm_callbacks_test exigia STATUS_SUCCESS sem
executor de TDR). Agora: memcpy removido, Bc250UmdSubstituteDrawWithBlt em fonte unica com
Bc250UmdBltGpu como UNICO produtor de pixel, elegibilidade por politica pura
Bc250UmdDrawBltSubstitutionIsLawful (tunable armado + winsys + residencia + mesmas dimensoes +
mesmo formato + viewport na origem cobrindo tudo + SEM blend, contando alpha-to-coverage e write
mask parcial), fechada por DEFAULT pelo tunable D3D11DrawBltSubstitution, log declarando "Draw
SUBSTITUIDO por copia 1:1 no SDMA -- NAO e' rasterizacao" e SubstitutedDrawCount contado SEPARADO
de DrawCount. Substituicao por blt NAO e' rasterizacao: sem compilador nao se sabe se o PS ligado
e' passe identidade, e afirmar equivalencia seria H apresentada como P0.
ACHADO COLATERAL DA C1: a suite OFFLINE dependia do estado da MAQUINA. CreateDevice e
GetCaps(Type=130) decidiam inline lendo HKLM\SOFTWARE\BC250\Umd\D3D11LabPromotion, que ficou em 1
depois do bind das 11:21 -- com ela armada o bc250_d3d_umd_test.c reprovava em duas assercoes SEM
NENHUMA MUDANCA DE CODIGO, o que REFUTA o "42/42 PASS" reportado. Extraida
Bc250Umd3DPipelineIsDeclared como fonte unica das duas respostas; a suite testa a tabela pura nos
quatro casos e assere o DDI por COERENCIA com o ambiente medido.
FAIXA C2, OFFLINE -- a evidencia das corridas estava sendo PERDIDA. C:\BC250_DISPLAY_LOG.txt e'
cumulativo e sobrevive a reboot, mas o binder o copia no FIM do roteiro; com BugCheck o watchdog
nao roda e a pasta fica truncada SEM QUE NADA INDIQUE. Medido em 10/09: SEIS pastas truncadas --
09:06 (123.676 bytes), 09:34 (6.934.920), 10:03 (65.987, sem arquivo), 10:23 (89.001, sem
arquivo), 11:10 (177.974) e 11:21 (438.038, contendo o FenceId=770 e a prova de HDP). A primeira
passada desta auditoria concluiu que a hipotese de HDP estava REFUTADA por analisar so' a pasta
truncada; a prova estava em disco o tempo todo. Novo tools/recover-bc250mcdm-lost-tail.ps1 resgata
o trecho com SHA-256 e proveniencia, delimitando o fim pelo start_offset do bind SEGUINTE quando
existe e DECLARANDO no json quando nao existe; o binder passou a chama-lo ANTES de cada bind,
depois de gravar o baseline novo. Ja executado: 6/6 pastas recuperadas.
Testes: build-umd EXIT=0 com "C1 politica 9/9, Draw fail-closed sem memcpy e tokens unicos PASS",
build-contract EXIT=0, quatro auditorias PASS, dois portoes novos (audit_draw_has_no_cpu_fallback
e audit_lost_log_tail_is_recovered) e SEIS mutacoes verificadas.
NADA DE C1/C2 FOI VALIDADO EM HARDWARE. Fusivel em TRIPPED (State=2, Boots=3): -Reset e boot frio
antes de qualquer bind. Desarmar D3D11LabPromotion se nao for objetivo da corrida.
A PROXIMA CORRIDA NAO DEVE PRODUZIR DESKTOP: com o Draw recusado honestamente e a substituicao
desarmada, TELA PRETA e' o resultado esperado e correto; qualquer imagem que aparecer deve ser
atribuida ao rollback/BasicDisplay ate prova em contrario.
Auditoria: docs/234_AUDITORIA_2026-09-10_C1_DRAW_SEM_CPU_E_C2_EVIDENCIA_PERDIDA.md
=============================================================================================
CORRIDA 10/09 12:16 (P0, lab 3.127.1.0) -- FLUSH DE HDP VALIDADO NO SILICIO, e BugCheck 0x133.
O criterio escrito ANTES da corrida foi CUMPRIDO: 1819 submissoes INDIRECT (1805 Fill=0/0 e 14
Fill=1/4096), FenceId ate 5335, 84 voltas do anel, ZERO IB_TRIPOD TIMEOUT, ZERO IB NAO EXECUTADO,
ZERO 0x119. Distribuicao de polls (n=1977): min 2, p50 3, p90 49, p99 180, MAX 181 -- contra
MAX 164.167 e morte no FenceId 770 na corrida das 11:22. A linha FENCE_VISIBILIDADE nao aparece
nenhuma vez, porque ela so' sai no caminho de timeout e nao houve timeout. A cauda ilimitada
acabou.
C1 VALIDADA EM HARDWARE: "DRAW_BLT_SUBSTITUTION: desarmado" e "DrawIndexed: RECUSADO --
compilador/shader-state/draw PM4 ausente (Rejected=1)" no dwm.exe (pid 1220); dwm, Explorer.EXE
(5304) e dxgiadaptercache.exe (10104) receberam S_OK (BC250 Native). TELA PRETA era o resultado
ESPERADO E CORRETO -- Draw recusado honestamente, zero memcpy, zero sucesso silencioso.
C2 VALIDADA EM HARDWARE: o arquivamento truncou em 15.725.444 e o resgate recuperou 3.492.427
bytes (sha256 896A6811...), que contem a corrida INTEIRA. Sem a C2 esta analise nao existiria.
PAREDE NOVA -- 0x133 DPC_WATCHDOG_VIOLATION (0x0, 0x501, 0x500, 0xfffff805220fb320) as 12:19:07,
MEMORY.DMP de 827.376.956 bytes. Arg2=0x501 contra Arg3=0x500: tempo excessivo em DISPATCH_LEVEL.
O log termina LOGO APOS uma submissao bem-sucedida (IB EXECUTADO ... FenceId=5335), sem timeout e
sem recusa -- NAO e' o caminho do 0x119.
HIPOTESE (H): DxgkDdiSubmitCommandVirtual e' chamada em DISPATCH_LEVEL e o laco de poll de
bc250_sdma_present.c:1226 roda DENTRO do KeAcquireSpinLock da linha 1129, soltando so' nas
1302/1408 -- 1819 submissoes, ate 181 polls de MMIO cada, em ~137 s. A A15 declarou esta divida
por escrito ("o laco do tripe pode gastar ~1 s segurando spinlock em DISPATCH_LEVEL"). NUANCE que
manda na leitura: os casos de 100-181 polls NAO sao visibilidade -- o flush ja dispara no
casamento de RPTR; sao espera REAL da engine. O flush matou a cauda INFINITA; sobrou o fato de
esperarmos de forma SINCRONA.
PROXIMO PASSO, SEM CUSTO DE CORRIDA: atribuir o 0x133 pelo stack do dump (comando em docs/235
secao 2.2). Somente depois implementar a conclusao ASSINCRONA. NAO baixar o teto de polls: teto
menor devolve STATUS_IO_DEVICE_ERROR, que e' o caminho conhecido do 0x119 -- trocaria um BugCheck
por outro.
NAO EXERCITADO: o IB de fill de 11.075.584 bytes nao apareceu nesta corrida, entao o
GFXHUB_FAULT STATUS=00841B40 ADDR=0x82C segue SEM TESTE e o flush de HDP NAO cobre essa classe.
ESTADO DA MAQUINA: BC-250 em CODIGO 43 (CM_PROB_FAILED_POST_START) com o 3.127.1.0 preso, fusivel
TRIPPED (Boots=2, LastStatus=0xC0000001) e SEM rollback (o BugCheck matou o watchdog).
tools\BC250_RECOVERY.cmd elevado, inspect-boot-fuse.ps1 -Reset e BOOT FRIO antes de novo bind.
ATENCAO AO EMPACOTAR: 3.127.1.0 JA FOI INSTALADO; suba o DriverVer fonte antes de gerar outro
candidato, senao dois artefatos diferentes carregam o mesmo DriverVer (erro do 3.92.1.0).
Handoff para a proxima IA: docs/235_HANDOFF_2026-09-10_12-16_FLUSH_HDP_VALIDADO_E_0x133.md
=============================================================================================
12/09 -- TRES CORRIDAS. A CAUSA RAIZ DA TELA PRETA FECHOU, E DUAS DELAS FORAM GASTAS EM
DEFEITO DE HOST (o segundo, meu, empilhando duas mudancas num candidato).
CORRECAO DE REGISTRO QUE MANDA NO VEREDITO: o present do "Marco 2" de 11/09 22:46 foi feito
por RtlCopyMemory da CPU -- zero linhas de SDMA no log daquele bind. O veredito saiu 10/10
PASS porque sdma_blt_executed casava com a string PRESENT_BLT, e o contador FallbackCpuCount
era CEGO a copia que o escape realmente fazia. QUARTA ocorrencia de "o gate carimbava o falso
sucesso" (A15, mcdm_callbacks_test, C1).
12:29 (P0, 3.149.1.0) -- primeira corrida com present 100% GPU. Com a copia por CPU removida,
o defeito que ela mascarava apareceu na tela: preto com faixa cinza. CAUSA RAIZ, por leitura
de codigo: Bc250GpuvaSpaceResolveAddress devolve BackingSegmentOffset, e o segmento 1 comeca
em LocalSegmentBaseOffset=0xA8C000, nao no byte zero. O present somava so' VramMcStart e lia
11.059.200 bytes ANTES da imagem. O tripe inteiro passava -- nenhuma das tres pernas confere
de ONDE se leu.
12:51 (P0, 3.150.1.0) -- a correcao trocou tambem a BASE por engano (caps.VramMcBase=0x450000000
no lugar de VramMcStart=0xF400000000). Origem e destino em dominios diferentes: blt em timeout
com fence no poison, 8 de 8, e present caindo na copia por CPU. O operador viu as barras
animando e relatou sucesso -- e o portao no_host_copy_fallback REPROVOU a corrida. Sem ele,
teria virado o terceiro marco falso pela mesma causa.
13:01 (P0, 3.151.1.0) -- PRESENT 100% POR GPU VALIDADO NO SILICIO. SegOff=0x1560000
SegBase=0xA8C000 SrcMc=0xF401FEC000 DstMc=0xF400000000; Submitted=584 Completed=584
Fallbacks=0 HostCpuCopies=0; BltMax=2171 us, media 1,37 ms. A "Garantia de Scanout GOP" por
CPU foi REMOVIDA na C3.1 e o ramo de CPU nao executou nenhuma vez, logo o UNICO escritor do
scanout foi o SDMA. Cadeia fechada: shader Vulkan no MEC -> VRAM -> byte proof 8/8 -> SDMA ->
scanout do GOP -> monitor. 584 quadros consecutivos (recorde anterior 473), a ~26 FPS contra
~1,25 FPS da copia por CPU.
PAREDE REMANESCENTE, identica nas tres: ruptura com status_device_removed, tdr_events=0,
Sub==Ret==Notified e ZERO StopDevice durante a corrida. O RADV leu ExecutionState=4 =
D3DDDI_DEVICEEXECUTION_STOPPED (P1, d3dumddi.h:3655) -- nao HUNG, nao DMAFAULT -- com "all
fences satisfied". A engine NAO travou: o device foi parado administrativamente. Correlacao
(H, nao P0): 7 crashes do dwm.exe + 8 WER em cada corrida, e a falha 0,46 s (13:01) e 0,5 s
(12:29) depois do SETIMO e ultimo. O que encerra a corrida parece ser o orcamento de crashes
do DWM, nao a contagem de quadros.
PROXIMA CORRIDA, falsificavel e sem rebuild: armar D3D11LabPromotion para o DWM receber device
e parar de fail-fastar, com D3D11DrawBltSubstitution DESARMADO (Draw segue RECUSADO, sem pixel
falso). Precedente que sustenta: em 10/09 12:16, com essa mesma combinacao, o dwm.exe gerou
546 KB de log e nenhum crash naquela pasta. 600/600 -> hipotese confirmada; parada em ~580 ->
REFUTADA.
DIVIDAS DECLARADAS: os 26 FPS NAO sao do blt (1,37 ms de 38 ms = 3,6%) -- a hipotese de que a
espera sincrona explicava o FPS esta REFUTADA por medicao; o custo esta no Vulkan. A divida do
laco em DISPATCH_LEVEL (A15) segue existindo, mas nao e' gargalo de FPS.
PACOTE 3.151.1.0 assinado e auditado: SYS 0D3E83AC..., UMD DD4DFED4... (inalterada desde o
3.149.1.0). Fusivel CLEAN, marcador de boot frio ARMADO: boot frio antes de qualquer bind.
=============================================================================================
13/09, OFFLINE e sem corrida -- o 0x116 da corrida 12/09 22:57 NAO foi hang de engine, e o
diagnostico do doc 260 estava errado nos tres pontos que mandavam no roteiro.
EVIDENCIA QUE NAO EXISTIA: a pasta da corrida tinha "display-log-this-bind.txt ausente no fim
do bind" (o BugCheck matou o watchdog). A cauda foi resgatada do log cumulativo pelo offset do
baseline e arquivada: display-log-this-bind-RECUPERADO.txt, 1.308.135 bytes, sha256 5E938F84...
Mais docs/evidence/dump-0x116-2026-09-12.txt, do MEMORY.DMP. Sem os dois, nada disto existiria.
P0 DO DUMP: a pilha e' VidSchiWorkerThread -> VidSchiCheckHwProgress -> VidSchiReportHwHang ->
TdrIsRecoveryRequired -> TdrBugcheckOnTimeout. ZERO quadros nossos. Arg3=0, ou seja NENHUMA
operacao nossa falhou. O "Failure.Exception.IP = Bc250McdmCollectDbgInfo" e' o owner tag que o
dxgkrnl publica em Arg2 para nomear o adaptador -- nao e' endereco de falha e nao houve excecao.
P0 DO LOG RECUPERADO: 1357 IBs executados, ZERO IB_TRIPOD TIMEOUT e CINCO TDR_QUIESCE -- em
todos os cinco RPTR==WPTR e STATUS1=000003FF (idle), e TDR_RESTART PROVADO 5 de 5. A engine
NUNCA travou e a nossa recuperacao funcionou nas cinco. O 0x116 e' o Windows estourando o
TdrLimitCount e desistindo do sexto, nao o driver falhando em recuperar.
ARMADILHA DO LOG, ERRO MEU APANHADO ANTES DE VIRAR VEREDITO: cheguei a concluir que a
aposentadoria parava no FenceId=15, porque so' ha 16 linhas [SdmaRetire] para 1357 IBs. FALSO --
[SdmaRetire] e' classe AMOSTRADA (bc250_display_ddi.c:574, FIRST=16 PERIOD=1024) e "IB EXECUTADO"
esta na lista de excecao. A assimetria era do logger. A aposentadoria rodou o tempo todo.
CAUSA RAIZ: a fence monitorada do blt era sinalizada FORA DE BANDA. Bc250UmdBltGpu montava um
SDMA_OP_FENCE sobre a VA da fence e o D3DDDICB_SUBMITCOMMAND saia sem objeto de sincronizacao.
O valor chegava a memoria; o dxgkrnl nunca ficava sabendo. Isso explica a intermitencia exata:
Bc250WinsysWaitForFenceFromCpu tem atalho que devolve OK se a fence ja chegou ao alvo -- o 1o
blt ganha a corrida contra a GPU e passa (foi o Substituidos=1); o 2o (janela 590x800, 800
pacotes de scanline) nao ganha, cai no pfnWaitForSynchronizationObjectFromCpuCb de verdade e
bloqueia PARA SEMPRE. O VidSch ve contexto sem progresso e reporta hang com a engine ociosa.
Casa com o log da UMD parando as 22:57:36.223 no ResourceUnmap anterior ao 2o draw: o trace
"Draw SUBSTITUIDO" so' sai DEPOIS de Bc250UmdBltGpu retornar. Segundo defeito no mesmo sitio:
Bc250SdmaEmitFence e' de 32 bits e a fence do WDDM e' de 64 (Bc250SdmaEmitFence64 esta ao lado).
CORRECAO DO 260: "tornar o Blt assincrono" nao tem o que fazer -- a submissao do Node 0 JA E'
assincrona desde antes dessa corrida (1974 marcadores (async), fila multi-slot, aposentadoria
por DPC/timer, e o portao audit_sdma_submission_is_asynchronous ja existia e passa).
FAIXA APLICADA (offline): Bc250WinsysSignalMonitoredFenceFromGpu nova sobre
pfnSignalSynchronizationObjectFromGpuCb, fail-closed em callback ausente, valor 0, fence nao
criada e contexto nao criado; o pacote de fence proprio SAIU do stream do blt (que caiu de 128
para 96 bytes) e o GCR ficou; quem materializa a escrita agora e' Bc250McdmBuildSignalMonitoredFence,
que JA EXISTIA no KMD, JA usa Fence64 e NUNCA tinha sido exercitado por este caminho.
Testes: TestSignalMonitoredFenceFromGpu novo no winsys, stub BltTestSignalGpu e invariante
g_BltSignals == g_BltSubmitsOk no teste do blt, mais modo de falha 5. build-umd EXIT=0,
build-contract EXIT=0, quatro auditorias PASS, portao novo audit_blt_fence_is_signalled_by_contract.
TRES MUTACOES VERIFICADAS: remover o sinal reprova a suite; reintroduzir Bc250SdmaEmitFence
reprova o portao; e fazer o teste voltar a exigir 128 bytes TAMBEM reprova -- esta terceira
existe porque "o gate congelava a mentira" ja apareceu quatro vezes (A15, mcdm_callbacks_test,
C1, sdma_blt_executed). O PRIMEIRO invariante que escrevi estava errado e a suite me pegou:
submit que FALHA nao deve sinalizar, e o curto-circuito do || e' proposital.
DIVIDAS DECLARADAS E NAO TOCADAS: (1) Bc250UmdBltGpu continua bloqueando dentro da DDI de Draw
-- com o sinal correto isso vira custo de FPS, nao mais correcao; (2) 800 pacotes de scanline
por janela, e COPY_LINEAR_SUB_WINDOW NAO tem procedencia local (Lei 7: inc/linux_ref/ so' tem
gc_10_1_0_sh_mask.h) -- nao inventar o encoding; (3) Journal NO_SPACE 8x mais um ActiveFlushTlb
-> STATUS_IO_DEVICE_ERROR, que comecam DEPOIS do 1o TDR e portanto nao sao a origem (doc 252);
(4) o BltCommand e' uma unica alocacao sobrescrita a cada blt, hoje segura SO' porque o wait e'
sincrono -- tornar o Draw nao-bloqueante exige anel de slots ANTES, ou a GPU le buffer reescrito.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> o ciclo do DWM passa do 2o draw,
Substituidos cresce alem de 1 e a corrida termina com no maximo 1 TDR_QUIESCE; refutada -> voltam
os 5 TDR_QUIESCE com RPTR==WPTR e STATUS1=0x3FF, e o proximo suspeito passa a ser o contexto de
RENDER do DWM, nao o do blt. Procurar "[SignalMonitoredFence] Completo: FenceGpuVa=... Value=..."
no log: se NAO aparecer, o sinal nao chegou ao KMD e o defeito e' anterior, na winsys.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE. Nao foi empacotado candidato: o DriverVer fonte
precisa subir antes, porque o ultimo candidato lab foi 3.161.1.0. EMPACOTADO EM 13/09 e NAO INSTALADO:
PACOTE LAB 3.162.1.0, BuildKind=MCDM_LAB_BRINGUP, perfil DISPLAY_RENDER_LAB (RenderNode=1,
RenderOnlyLab=0), contrato WDDM 2.7, modo P1 PERSISTENT SUBMISSION, schema 135.
SYS fonte 97A48176..., SYS assinado 59F3D925..., UMD 6C7C70EB..., INF 9085671C...,
CAT A7243D41... Lab package audit PASS. A UMD assinada FOI CONFERIDA por string: as duas
linhas SignalMonitoredFenceFromGpu ENTER/LEAVE estao no binario, entao o empacotador nao
copiou a DLL anterior -- armadilha conhecida, o empacotador nao compila, so' copia.
Handoff: docs/261_HANDOFF_2026-09-13_0x116_E_A_FENCE_FORA_DE_BANDA.md
=============================================================================================
CORRIDA 13/09 05:50 (P0, lab 3.162.1.0) -- a correcao da fence NAO impediu o 0x116, e o
criterio que EU escrevi era irrespondivel pela instrumentacao existente.
Cauda resgatada e arquivada (o watchdog morreu de novo): display-log-this-bind-RECUPERADO.txt,
1.510.070 bytes, sha256 EE96AB8B... Dump novo: docs/evidence/dump-0x116-2026-09-13.txt, com
pilha IDENTICA a de 12/09 -- VidSchiWorkerThread -> VidSchiCheckHwProgress -> ReportHwHang ->
TdrIsRecoveryRequired -> TdrBugcheckOnTimeout, Arg3=0, Arg4=0xd, zero quadros nossos.
MEU CRITERIO ESTAVA MAL ESPECIFICADO. Escrevi "se [SignalMonitoredFence] nao aparecer, o sinal
nao chegou ao KMD". Nao apareceu, e Op=16 (DXGK_OPERATION_SIGNAL_MONITORED_FENCE) tambem nao --
mas isso NAO prova falha: o 1o blt devolveu S_OK com Substituidos=1, e isso exige
*BltFence.CpuVirtualAddress == BltSequence. Alguem escreveu a fence. O dxgkrnl honrou o sinal
por outro caminho, sem nos pedir para montar pacote. O criterio nao discriminava nada.
PIOR: BC250_WINSYS_LOG escrevia SO' em OutputDebugStringA e stderr, e o dwm.exe nao tem stderr.
NENHUMA linha da winsys chegou ao log arquivado, em nenhuma corrida. Eu escrevi um criterio
sobre linhas que a instrumentacao nao podia produzir.
ACHADO NOVO QUE INVERTE O SUSPEITO (P0): os DOIS blts executam corretamente na GPU, com
RPTR==WPTR e fence sobre poison -- 6 de 30.304 bytes (tela cheia 2560x1080, 1080 copias) e 5 de
22.464 (janela 590x800, 800 copias), todos Vmid=7. E os CINCO TDRs disparam SEMPRE logo apos o
blt de TELA CHEIA (FenceId 45, 428, 760, 993, 1372); NENHUM apos o de janela. O 260 e eu
culpavamos o 2o draw com blend; a medicao diz que o congelamento e' DEPOIS do blt de tela cheia
completar e ANTES da submissao seguinte -- intervalo que so' contem WaitPaging(src),
WaitPaging(dst) e o pfnLockCb do UploadAllocation. Correcao de registro: eu disse "10
TDR_QUIESCE"; sao CINCO -- o grep contou duas linhas por evento.
FAIXA APLICADA (offline): sink de trace passou a ser UNICO. Bc250WinsysTraceRaw nova em
bc250_wddm_winsys.c carimba hora/pid/imagem e anexa em C:\Windows\Temp\BC250_UMD_LOG_<pid>.txt;
BC250_WINSYS_LOG passou a escreve-lo (stderr fica para as suites offline) e Bc250UmdTrace
DELEGA a ele, eliminando a segunda copia da escrita em arquivo que existia na UMD.
Bc250WinsysSubmitGpuVa ganhou ENTER/LEAVE -- era a UNICA funcao da winsys sem trace, e
pfnSubmitCommandCb bloqueia com a fila do contexto cheia.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portao novo
audit_umd_trace_reaches_the_evidence_file. TRES MUTACOES VERIFICADAS: tirar o roteamento do
sink reprova; renomear SubmitGpuVa ENTER reprova; e Bc250UmdTrace voltar a chamar CreateFileA
por conta propria reprova. O portao antigo que exigia BC250_UMD_LOG_%lu.txt em bc250_d3d_umd.c
foi reapontado para a fonte unica.
PACOTE LAB 3.163.1.0 assinado, auditado e NAO INSTALADO: SYS fonte C1229885..., SYS assinado
55D2F2E9..., UMD BCAB3634..., INF 04288201..., CAT EE6BC8F3..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. A UMD assinada FOI
CONFERIDA por string: SubmitGpuVa ENTER/LEAVE, WDDM_WINSYS (29x), BC250_UMD_LOG_ e
SignalMonitoredFenceFromGpu ENTER estao no binario.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES e agora RESPONDIVEL: o log do dwm.exe passa a conter
linhas [WDDM_WINSYS]. A ULTIMA linha antes do congelamento NOMEIA o callback que bloqueou --
WaitPaging, Upload/Lock ou SubmitGpuVa. Se a ultima for um ENTER sem LEAVE, o bloqueio e'
naquele callback e a correcao deixa de ser hipotese. Se as linhas [WDDM_WINSYS] NAO aparecerem,
o sink falhou e o defeito e' da instrumentacao, nao do driver.
ESTA FAIXA E' SO' INSTRUMENTACAO: nao altera nenhum caminho de GPU e NAO deve corrigir o 0x116.
Esperar TDR e BugCheck de novo -- o que muda e' que desta vez o log diz ONDE.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 06:08 (P0, lab 3.163.1.0) -- A INSTRUMENTACAO PAGOU E O PONTO DE BLOQUEIO TEM NOME.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 1.067.888 bytes, sha256 EBEDED30...
Dump: docs/evidence/dump-0x116-2026-09-13.txt, pilha IDENTICA as anteriores (VidSchiWorkerThread
-> VidSchiCheckHwProgress -> ReportHwHang -> TdrIsRecoveryRequired -> TdrBugcheckOnTimeout,
Arg3=0, Arg4=0xd, zero quadros nossos). 244 linhas [WDDM_WINSYS] no dwm.exe -- o sink unico
funcionou na primeira tentativa.
O SINAL DE FENCE PELO CONTRATO ESTA VALIDADO EM HARDWARE para a 1a submissao (P0): o 1o blt fez
SubmitGpuVa 30304 -> LEAVE hr=0, SignalMonitoredFenceFromGpu Value=1 -> LEAVE hr=0,
WaitForFenceFromCpu Target=1 Current=0 -> LEAVE hr=0 Current=1, tudo em 2 ms. A fence avancou
0->1 PELO CAMINHO DO CONTRATO. A correcao da manha nao era errada; era insuficiente.
PONTO DE BLOQUEIO, COM NOME (P0): o 2o blt fez SubmitGpuVa 22464 -> LEAVE hr=0,
SignalMonitoredFenceFromGpu Value=2 -> LEAVE hr=0, e entao
"WaitForFenceFromCpu ENTER: TargetFence=2 CurrentFence=1" SEM LEAVE. O KMD registrou
IB EXECUTADO do IB de 22464 com RPTR==WPTR e fence sobre poison: a GPU FEZ o trabalho; a fence
monitorada simplesmente nao avancou de 1 para 2. Op=16 (SIGNAL_MONITORED_FENCE) nunca foi
pedido e nao houve Unhandled Op.
DUAS CORRECOES DE REGISTRO MINHAS: (1) eu afirmei que TODOS os TDRs seguiam o blt de TELA CHEIA
e usei isso para inverter o suspeito -- ERRADO; nesta corrida 4 dos 5 seguem o blt de JANELA. O
invariante real e' banal: o TDR segue o ULTIMO blt, qualquer que seja. (2) eu disse "10
TDR_QUIESCE"; sao CINCO -- o grep contava duas linhas por evento.
CORRECAO DE VERDADE (offline), atacando a causa e nao o sintoma -- a espera por draw SAIU:
(A) ANEL DE SLOTS. Havia UMA alocacao de command buffer reescrita a cada blt, e por isso a DDI
de Draw PRECISAVA esperar a fence da propria submissao antes de retornar -- era isso que
congelava a thread de composicao do DWM e fazia o VidSch reportar hang com a engine OCIOSA.
Agora sao BC250_BLT_SLOT_COUNT=4 command buffers, um por slot, e a unica espera que sobrou e' a
de RECICLAGEM: pela submissao que usou AQUELE slot 4 blts atras, que na pratica ja terminou. No
regime normal ninguem bloqueia.
(B) HONESTIDADE DO TRACE: S_OK agora significa SUBMETIDO, nao concluido. A linha virou
"Draw SUBSTITUIDO por copia 1:1 SUBMETIDA ao SDMA -- NAO e' rasterizacao e NAO e' prova de
conclusao". A prova de execucao continua sendo a do KMD (IB EXECUTADO, RPTR==WPTR, fence sobre
poison), nao a da UMD.
(C) DUAS FALHAS MUDAS na DDI Bc250McdmSignalMonitoredFence, achadas por leitura: o retorno
STATUS_INVALID_PARAMETER com pDmaBuffer==NULL nao logava NADA -- era impossivel distinguir "o
dxgkrnl nunca chamou" de "nos recusamos calados", exatamente a ambiguidade que a Lei 4 proibe e
que audit_process_lifecycle_ddis_speak ja trava em outras DDIs. Agora a entrada SEMPRE fala,
com FenceGpuVa, Value, DmaSize, pDma e KernelSubmissionType.
(D) BUG REAL DE CAPACIDADE nas DUAS variantes do sinal: Bc250PagingFloorPow2Dwords arredondava
a capacidade do buffer LINEAR para baixo ate potencia de dois -- 20 dwords viravam 16 e a DDI
recusava com INSUFFICIENT_DMA_BUFFER um buffer que cabia de sobra. Agora exigimos o tamanho
REAL e declaramos aos emissores uma janela fixa de 32 dwords que cabe dentro do verificado.
Testes: o laco generico de modos de falha foi reescrito em casos EXPLICITOS, porque o modo 3
(fence que nao avanca) deixou de ser falha e virou a PROVA de que a DDI nao espera a propria
submissao -- 4 blts consecutivos passam com a fence congelada. Mais o invariante do anel
(4 submissoes, 4 GPUVAs distintas, zero esperas) e o contraponto (com a fence congelada, a
5a volta PRECISA esperar). build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS,
portao novo audit_blt_draw_does_not_block_on_its_own_fence. DUAS MUTACOES VERIFICADAS:
reintroduzir a espera pela propria fence reprova o portao; e o teste deixar de exigir
"g_BltWaits == 0" TAMBEM reprova -- esta segunda existe porque "o gate congelava a mentira" ja
apareceu cinco vezes (A15, mcdm_callbacks_test, C1, sdma_blt_executed, teste do blt com 128
bytes). Tres assercoes minhas estavam erradas e a suite me pegou nas tres.
PACOTE LAB 3.164.1.0 assinado, auditado e NAO INSTALADO: SYS fonte C571C731..., SYS assinado
2468E0A4..., UMD 9A0B61D0..., INF 2D2014A9..., CAT A406BD6A..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "SUBMETIDA ao SDMA" e "SubmitGpuVa ENTER" na UMD; "[DdiSignalMonitoredFence] ENTER" e
"RECUSADO: pDmaBuffer NULL" no SYS.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> nao existe mais "WaitForFenceFromCpu
ENTER" sem LEAVE no log do dwm.exe, Substituidos cresce muito alem de 1, e a corrida termina
com no maximo 1 TDR_QUIESCE. Refutada -> voltam os cinco TDR com a engine ociosa, e entao o
bloqueio nao era o unico caminho pelo qual o dxgkrnl perde o progresso.
INFORMACAO NOVA EM QUALQUER DESFECHO: se o dxgkrnl chamar a DDI de sinal, a linha
"[DdiSignalMonitoredFence] ENTER" aparece e diz COM QUE ARGUMENTOS; se ele nunca chamar, a
ausencia passa a ser conclusiva porque toda saida da DDI agora fala.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 08:12 (P0, lab 3.164.1.0) -- O ANEL DE SLOTS NAO IMPEDIU O 0x116, e a corrida
entregou o DISCRIMINANTE que reposiciona o problema inteiro.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 963.360 bytes, sha256 59EFAFFE...
Quarto dump seguido com pilha IDENTICA (docs/evidence/dump-0x116-2026-09-13c.txt): Arg3=0,
Arg4=0xd, zero quadros nossos. !analyze -v esgotou o que tinha a dar -- parar de gasta-lo.
O DISCRIMINANTE (P0): o KMD executou 958 IBs. 953 eram buffers de PAGINACAO do proprio dxgkrnl
(Op=2/8/11/12) e nao produziram UM TDR. Os outros 5 eram submissoes de CONTEXTO da UMD no
Node 0 -- que e' o no de PAGINACAO (PagingNodeIndex=0, bc250_display_ddi.c:1571) -- e CADA UMA
foi seguida de um TDR, sempre com RPTR==WPTR e STATUS1=0x3FF (engine ociosa, anel drenado).
Cinco blts, cinco TDRs, 1 para 1. E as submissoes de UMD no Node 1 (RADV, 600 quadros de
12/09) NUNCA deram um TDR. Nao e' o blt: e' que NENHUMA submissao de contexto e' aceita como
progresso pelo escalonador.
DUAS HIPOTESES MINHAS MORTAS NA MESMA CORRIDA: (1) "nao notificamos conclusao de contexto" --
FALSO, a linha IB EXECUTADO sai de dentro de Bc250ActiveSdmaRetireAndNotify, entao os blts SAO
aposentados e entram em lote com DMA_COMPLETED; (2) a marca d'agua por no descartando
notificacoes -- REFUTADA por medicao: 958 FenceIds estritamente crescentes, zero retrocessos.
Zero GFXHUB_FAULT e FaultStatus=0 nos cinco TDRs: nao ha falha de traducao.
CORRECAO DE REGISTRO: na corrida anterior eu disse que todos os TDRs seguiam o blt de TELA
CHEIA; aqui 4 dos 5 seguem o de JANELA. O invariante e' banal -- o TDR segue o ULTIMO blt.
DEFEITO DE COLETA, MEU: os bc250_umd_log_*.txt da pasta de evidencia sao SNAPSHOTS de ~5 s
depois do bind. O do dwm.exe termina no meio de um CreateResource, antes de qualquer draw; os
logs do fim da corrida ficaram em C:\Windows\Temp e sumiram. O criterio que escrevi para a
08:12 ficou SEM RESPOSTA por isso. A licao da C2 nao tinha sido estendida aos logs da UMD.
FAIXA APLICADA (offline) -- a composicao SAIU do no de paginacao:
(A) Novo opcode BC250_ESCAPE_OP_COMPOSITE_BLT no contrato de escape. A UMD alcanca o KMD por
pfnEscapeCb e o KMD executa com Bc250SdmaPresentBlt -- o mesmo caminho que fez 584 blts
consecutivos sem UM TDR em 12/09 13:01, que nao cria pacote de contexto e nao passa pelo
escalonador. Origem E destino sao resolvidos por Bc250SegmentOffsetToMc (a aritmetica da C3.4),
com recusa para faixa que nao couber no segmento e para sobreposicao.
(B) A UMD nao cria mais contexto, paging queue, command buffer nem fence para o blt. Paging
queue, contexto virtual, anel de slots e Bc250UmdBltInit/Teardown foram REMOVIDOS -- nao ficou
dead code; esta no git.
(C) ESCOPO DECLARADO E ESTREITO: so' copia LINEAR (pitch igual nos dois lados, retangulo
cobrindo linhas inteiras a partir da origem). E' o draw de tela cheia do DWM. O anel do KMD tem
4096 bytes e nao comporta copia por scanline; retangulo de JANELA e' RECUSADO com log honesto,
nunca aproximado. Isso significa que janelas NAO serao compostas nesta corrida.
(D) Bug real no guard do contrato de escape: ele escolhia kernel-mode por NTDDI_VERSION, que a
UMD tambem define, e incluir <ntdef.h> depois de <windows.h> redefinia _PROCESSOR_NUMBER,
_GROUP_AFFINITY e _FLOAT128. Corrigido para _KERNEL_MODE.
(E) O (a) que vinha de graca: CTX_PROBE e CONTEXT SWITCH sairam da amostragem. Na 08:12
sobraram 3 CTX_PROBE em ~958 submissoes, e eram exatamente as linhas que diriam se a root do
VMID do contexto foi instalada. Sao raras por natureza, nao um diluvio.
Testes: bc250_d3d_umd_test reescrito em torno do invariante NEGATIVO -- a composicao nao pode
gerar NENHUMA submissao de contexto (g_BltSubmits == 0) --, mais recusa por pitch diferente,
por retangulo deslocado, por pfnEscapeCb ausente, e propagacao de falha do KMD.
build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portao novo
audit_composition_never_submits_a_context substituindo os dois portoes do caminho antigo.
DUAS MUTACOES VERIFICADAS: reintroduzir Bc250WinsysSubmitGpuVa no blt reprova; e o teste deixar
de exigir zero submissao TAMBEM reprova.
ARMADILHA REGISTRADA: minha funcao nova de resolucao continha o mesmo anchor textual que
audit_present_source_uses_segment_base usa para delimitar a janela do ramo de present, e isso
ampliou a janela ate englobar a soma crua legitima do caminho de endereco fisico. O portao
reprovou corretamente; renomeei o local em vez de afrouxar o portao.
PACOTE LAB 3.165.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 010C076D..., SYS assinado
DF38D09E..., UMD 2F1F8E2B..., INF E9FCA938..., CAT DAED531F..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "BltViaKmd ENTER" e a recusa de scanline na UMD; "COMPOSITE_BLT OK", a recusa de
resolucao e "CTX_PROBE" no SYS.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: confirmada -> aparecem linhas
"[Bc250Escape] COMPOSITE_BLT OK" e a corrida termina com NO MAXIMO 1 TDR_QUIESCE, contra os
cinco de todas as corridas anteriores. Refutada -> voltam os cinco TDR mesmo sem UMA submissao
de contexto, e entao o no de paginacao nao era o discriminante e o suspeito passa a ser o
proprio ciclo de vida do contexto do DWM, nao a submissao.
NAO ESPERAR DESKTOP: janelas sao RECUSADAS por escopo declarado, e o Draw real continua sem
backend. Tela preta segue sendo o resultado correto; o que esta em teste e' o TDR, nao o pixel.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDAS 13/09 11:25 e 11:32 (P0, lab 3.165.1.0) -- DUAS CORRIDAS SEGUIDAS SEM TDR E SEM
BUGCHECK, as primeiras de toda a sequencia. Contra CINCO TDRs e 0x116 em todas as anteriores.
Confirmado pelo Event Log: o ultimo MEMORY.DMP e' das 08:14, da corrida anterior.
11:25 -- 2741 IBs, ZERO TDR. O DWM morreu quatro chamadas de DDI antes do primeiro Draw, entao
o escape nao rodou e o criterio ficou sem resposta.
11:32 -- 3283 IBs, ZERO TDR, e o caminho novo FOI ALCANCADO: "BltViaKmd ENTER: 2560x1080
pitch=10240" no dwm.exe. Primeira vez que o DWM chega ao Draw pelo caminho do KMD.
ACHADO QUE REFUTA A MINHA PROPRIA HIPOTESE: a desamostragem do CTX_PROBE (2741 e 3283 linhas,
contra 3 na corrida 08:12) mostrou 37 submissoes de CONTEXTO em VMID 7 com ZERO TDR, mais
"VMID7 CONTEXT SWITCH" com root valida (0x45FDFF000). Na 08:12 havia 22 submissoes em VMID 7 e
so' 5 eram nossos blts -- as outras 17 nunca deram TDR e eu nao tinha visto porque a amostragem
escondia. "Submissao de contexto no no de paginacao causa TDR" esta REFUTADO: era algo
especifico do NOSSO blt, nao da classe inteira. Isso elimina um espaco grande de suspeitos.
O CRITERIO NAO FECHOU, POR DEFEITO MEU, E SAO DOIS:
(1) O log do KMD teve ZERO linhas COMPOSITE_BLT -- nem OK, nem RECUSADO. As duas saidas de
recusa do handler que eu escrevi ontem eram MUDAS (payload menor que 4 bytes no dispatch, e
tamanho menor que o contrato no composite). Impossivel distinguir "nao chegou ao handler" de
"recusamos calados" -- a mesma ambiguidade que a Lei 4 proibe e que eu havia corrigido de manha
em Bc250McdmSignalMonitoredFence, reintroduzida por mim no codigo novo.
(2) O log do dwm.exe termina no "BltViaKmd ENTER" as 11:32:33.465, mas o primeiro crash do DWM
foi as 11:33:03 -- TRINTA SEGUNDOS depois. O log nao terminou no escape: ele e' um SNAPSHOT
tirado pelo coletor ~6 s apos o bind. A linha "BltViaKmd LEAVE", com o hr do pfnEscapeCb, que
responderia a corrida inteira, existiu e foi descartada. TERCEIRA corrida seguida perdida pela
mesma causa (08:12, 11:25, 11:32), depois de eu ter identificado o defeito e nao o corrigido.
Dado colateral: 3.282 escapes de TERCEIROS com opcode 0x2 chegaram ao nosso handler na 11:32 --
nenhum nosso (o DWM chamou BltViaKmd UMA vez). O log 1-em-1 que eu pus na recusa afogou o
arquivo, e "RECUSA" esta na lista que escapa da amostragem global.
FAIXA APLICADA (offline): (A) entrada do handler de composicao SEMPRE fala, com DataSize
recebido e esperado, e a recusa por tamanho nomeia a causa ("ABI divergente entre UMD e KMD");
(B) a recusa de payload invalido no dispatch deixou de ser muda; (C) a recusa de opcode
desconhecido ganhou teto por contador PROPRIO (8 primeiras + 1 em 1024), porque o teto global
nao a alcanca; (D) o binder passou a coletar os logs da UMD TAMBEM no fim da janela fisica, em
arquivos -FINAL que NAO sobrescrevem o snapshot -- os dois juntos mostram o que mudou.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS, portoes novos dentro
de audit_composition_never_submits_a_context e audit_umd_trace_reaches_the_evidence_file. DUAS
MUTACOES VERIFICADAS: a recusa por tamanho voltar a ser muda reprova; e o binder voltar a
coletar so' no inicio TAMBEM reprova.
PACOTE LAB 3.166.1.0 assinado, auditado e NAO INSTALADO: SYS fonte DB2B794A..., SYS assinado
CFF5EBC7..., UMD 128C1FFC..., INF 38A95E60..., CAT 54668C1B..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "COMPOSITE_BLT ENTER", "ABI divergente entre UMD e KMD" e "payload ausente ou menor que
4 bytes" no SYS; "BltViaKmd ENTER" na UMD; bc250_umd_log-FINAL.txt no binder.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES e agora RESPONDIVEL PELO LOG DO KMD, que sobrevive:
a linha "[Bc250Escape] COMPOSITE_BLT ENTER" aparece com DataSize -- se DataSize divergir do
esperado, a ABI entre UMD e KMD esta quebrada e a linha diz os dois numeros; se aparecer
"COMPOSITE_BLT OK", a composicao executou no caminho provado do KMD; se NAO aparecer NENHUMA
das duas, o escape nao chega ao handler e o defeito e' do dxgkrnl recusando a chamada, nao
nosso. Os tres desfechos sao distinguiveis, o que nao era verdade nas tres corridas anteriores.
ESTA FAIXA E' CORRECAO DE DEFEITO, NAO EXPERIMENTO: as duas saidas mudas eram violacao da Lei 4
introduzida por mim, e a coleta em snapshot custou tres criterios.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 11:55 (P0, lab 3.166.1.0) -- TERCEIRA CORRIDA SEGUIDA SEM TDR E SEM BUGCHECK
(3645 IBs, zero TDR_QUIESCE), e o criterio FECHOU no terceiro desfecho.
Cauda resgatada: display-log-this-bind-RECUPERADO.txt, 3.878.073 bytes.
O SYS novo estava rodando, PROVADO pelo proprio teto que ele introduziu: o flood de escapes de
terceiros caiu de 3.282 linhas (11:32) para 11 (8 primeiras + periodicas), com "ocorrencia #".
CRITERIO, RESPONDIDO: COMPOSITE_BLT ENTER = 0, OK = 0, RECUSADO = 0, "payload ausente" = 0.
A linha ENTER agora e' INCONDICIONAL no topo do handler, logo a ausencia e' conclusiva: o
NOSSO escape NAO chega ao handler, enquanto escapes de TERCEIROS chegam normalmente. Pelo
criterio escrito antes da corrida, isso significa que o dxgkrnl recusa a chamada antes de nos
alcancar -- nao e' recusa nossa. O dwm.exe chamou BltViaKmd ENTER as 11:55:38.052.
DEFEITO MEU, QUARTA VEZ NA MESMA CLASSE: a coleta -FINAL que eu havia adicionado caiu dentro
de `if ($EnablePhysicalRenderOnly)`, ramo que este perfil NAO executa. Nao ha arquivos -FINAL
na pasta e a linha "BltViaKmd LEAVE", com o hr do pfnEscapeCb -- o unico dado que nomeia a
recusa --, perdeu-se de novo. Os logs em C:\Windows\Temp ja haviam sido apagados.
FAIXA APLICADA (offline), tres frentes:
(A) CORRECAO DE CONTRATO, nao instrumentacao: o escape passou a declarar
args.Flags.HardwareAccess = 1. O handler do KMD dirige o anel SDMA0; sem essa flag o dxgkrnl
nao serializa o escape contra o dispositivo. E' candidato direto a causa da recusa.
(B) ENTRADA DA DDI DE ESCAPE FALA, antes de qualquer despacho: [McdmEscape] ENTER imprime
DataSize, o primeiro dword (o opcode do contrato), hDevice, hContext e Flags, mais se o handler
esta instalado. Sem isso era impossivel separar "o dxgkrnl nunca nos chamou" de "chamou com um
payload que nao reconhecemos". Teto por contador proprio (8 primeiras + 1 em 512), e qualquer
dword com a assinatura 0xBC25xxxx escapa do teto -- o nosso NUNCA e' amostrado fora.
A ausencia de handler instalado tambem deixou de ser muda.
(C) COLETA FINAL NO CAMINHO COMUM do binder, agora fora do ramo morto: janela de
WatchdogTimeoutSeconds-60 s (piso 30) e entao copia dos BC250_UMD_LOG_*.txt para arquivos
-FINAL que NAO sobrescrevem o snapshot, mais a cauda do log do KMD pelo offset do baseline. O
watchdog continua sendo a rede de seguranca.
(D) LOG MAXIMO na UMD: BltViaKmd ENTER agora imprime Opcode, sizeof do payload, hDevice,
hContext e Flags; o LEAVE imprime hr e ResultCode. Se o KMD imprimir um DataSize diferente do
sizeof da UMD, a ABI esta divergente e os DOIS numeros estarao no log.
Testes: build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS. TRES MUTACOES
VERIFICADAS: tirar HardwareAccess reprova; a entrada da DDI de escape voltar a ser muda
reprova; e o binder perder a coleta FINAL TAMBEM reprova.
PACOTE LAB 3.167.1.0 assinado, auditado e NAO INSTALADO: SYS fonte 772045D5..., SYS assinado
BC64C7A7..., UMD F832A457..., INF FCBF218E..., CAT D60E37C1..., perfil DISPLAY_RENDER_LAB,
WDDM 2.7, P1 PERSISTENT SUBMISSION, schema 135. Lab package audit PASS. Binarios CONFERIDOS por
string: "[McdmEscape] ENTER #%u", "nenhum handler instalado" e "COMPOSITE_BLT ENTER" no SYS;
"BltViaKmd ENTER", "hDevice=%p hContext=%p Flags" e "dxgkrnl ou o KMD recusaram" na UMD.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- tres desfechos, todos distinguiveis e todos
respondidos por logs que agora SOBREVIVEM:
  1. "[McdmEscape] ENTER" com Dword0=0xBC250002 -> o escape chega ao KMD; a linha seguinte
     (COMPOSITE_BLT ENTER/OK/RECUSADO) diz o que fizemos com ele.
  2. "BltViaKmd LEAVE: hr=..." com hr != 0 e NENHUM [McdmEscape] com 0xBC250002 -> o dxgkrnl
     recusou; o hr nomeia o motivo, e HardwareAccess deixa de ser hipotese num sentido ou no
     outro.
  3. Nenhuma das duas -> a coleta FINAL falhou de novo e o defeito e' do binder, nao do driver.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
CORRIDA 13/09 12:09 (P0, lab 3.167.1.0) -- QUARTA CORRIDA SEGUIDA SEM TDR E SEM BUGCHECK
(3414 IBs, zero TDR_QUIESCE), A COLETA FINAL FUNCIONOU, e a flag que EU adicionei nesta faixa
foi a causa do bloqueio.
A COLETA ESTA RESOLVIDA: o binder gravou 12 arquivos bc250_umd_log_*-FINAL.txt e
display-log-this-bind-FINAL.txt (3.644.356 bytes) ao fim da janela de 120 s, no caminho comum.
Cinco corridas seguidas haviam perdido o dado decisivo; esta nao perdeu. A tentativa anterior
tinha caido dentro de `if ($EnablePhysicalRenderOnly)`, ramo que este perfil nao executa.
CORRECAO DE UM VEREDITO MEU, PELA EVIDENCIA NOVA: eu havia concluido, olhando so' o lado do
KMD, que "o dxgkrnl RECUSA o nosso escape". FALSO. O log da UMD mostra SETE "BltViaKmd ENTER"
-- um por instancia do DWM -- e ZERO "LEAVE". O pfnEscapeCb NAO RETORNA. Nao e' recusa, e'
BLOQUEIO.
CAUSA, MEDIDA: Flags=0x00000001 nas sete chamadas -- o HardwareAccess que eu armei nesta faixa
apresentando-o a voce como "exigencia de contrato, nao enfeite". HardwareAccess=1 faz o dxgkrnl
SERIALIZAR o escape contra o dispositivo, esperando a GPU quiescer antes de despachar. Isso e'
chamado de dentro da DDI de Draw do DWM, com o dispositivo ocupado: deadlock por construcao. O
handler do KMD dirige o anel de present dele, protegido pelo NOSSO spinlock, e nao precisa da
serializacao do dxgkrnl.
O CONTRASTE ESTA NOS PROPRIOS DADOS: os 14 [McdmEscape] ENTER da corrida sao todos de
TERCEIROS, com Dword0=0x00000002, DataSize=388/492 e Flags=0x00000000 -- e atravessam. O unico
escape com HardwareAccess era o nosso, e foi o unico que travou. Zero escapes com
Dword0=0xBC250002 chegaram ao KMD, e essa ausencia e' conclusiva porque o teto do log ISENTA
qualquer dword com assinatura 0xBC25xxxx.
DADO COLATERAL QUE VALE: bloquear dentro de um escape NAO dispara TDR -- sete threads de DWM
travadas e zero TDR_QUIESCE. Bloquear numa fence monitorada dispara. Sao caminhos diferentes no
escalonador, e isso estreita o modelo.
CONFIRMADO PELO LOG DA UMD: Opcode=0xBC250002 e sizeof=48 nas sete chamadas -- a ABI do lado da
UMD esta correta, e o DataSize que o KMD veria seria 48.
FAIXA APLICADA (offline): args.Flags.Value = 0u. Nao e' "tentar o outro valor" -- a
serializacao de dispositivo nao nos serve e cria o deadlock. hDevice fica COMO ESTA de
proposito, para mudar UMA variavel: se o escape ainda falhar, o hr do LEAVE nomeia o motivo, e
o hDevice nao-nulo (que difere dos 14 que atravessam) vira o proximo suspeito ja identificado.
O PORTAO FOI INVERTIDO: audit_composition_never_submits_a_context EXIGIA HardwareAccess porque
eu o havia declarado contrato; agora o PROIBE, com a medicao no comentario, e exige
args.Flags.Value = 0u. Mutacao verificada: rearmar a flag reprova nos dois sentidos.
build-umd EXIT=0, build-contract EXIT=0, quatro auditorias PASS.
PACOTE LAB 3.168.1.0 assinado, auditado e NAO INSTALADO: SYS assinado 80374141..., UMD
FCB3B552..., INF D4382AAE..., CAT FD042E32..., perfil DISPLAY_RENDER_LAB, WDDM 2.7,
P1 PERSISTENT SUBMISSION. Lab package audit PASS. ARMADILHA REGISTRADA: a primeira passada do
empacotador falhou com IOException em Copy-Item e deixou o pacote INCONSISTENTE -- a auditoria
reprovou com seis hashes divergentes. Repetir o empacotador resolveu. NUNCA aceitar pacote sem
a auditoria PASS logo depois.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES: a linha "BltViaKmd LEAVE: hr=..." PRECISA aparecer
-- a coleta FINAL ja provou que consegue captura-la. Entao:
  1. LEAVE com hr=0 e "[Bc250Escape] COMPOSITE_BLT OK" no KMD -> a composicao executa pelo
     caminho provado do KMD.
  2. LEAVE com hr != 0 e nenhum [McdmEscape] com 0xBC250002 -> o dxgkrnl recusa, e o hr nomeia
     o motivo; hDevice passa a ser o suspeito nomeado.
  3. LEAVE ausente de novo -> o bloqueio nao era o HardwareAccess, e a hipotese cai.
NAO ESPERAR DESKTOP: janelas seguem RECUSADAS por escopo declarado e o Draw real continua sem
backend. Tela preta e' o resultado correto.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
AUDITORIA DA CORRIDA 13/09 16:28:27 (pacote 3.172.0.0) -- O DIAGNOSTICO DO docs/265 NAO SE
SUSTENTA NA PROPRIA EVIDENCIA DELE, E A PAREDE E' OUTRA. Tudo abaixo e' OFFLINE, zero corridas.
O QUE A CORRIDA REALMENTE PROVOU (P0, e vale): 0x10E eliminado, zero BugCheck, zero TDR, 3.757
IBs indiretos, rollback online limpo (RECOVERY_OK_ONLINE). Isso e' ESTABILIDADE.
O QUE ELA NAO PROVOU, contra o que o 265 afirmou:
(1) ZERO chamadas a Bc250DxgkDdiPresent na corrida inteira -- nao ha UMA linha [Present #N] em
5,4 MB de log. A ausencia e' CONCLUSIVA: [Present #...] nao cai em classe de amostragem
(bc250_display_ddi.c, ramo "else return TRUE"), FrameBufferVirtual e' nao-NULL
([DisplayInit] Virt=FFFFBAF97B800000) e o ramo de recusa contem REJECT, isento de amostragem.
O caminho de transferencia para o scanout GOP e' CODIGO MORTO nesta corrida. O 265 propoe
consertar codigo que nunca rodou.
(2) NAO HA QUADRO COMPOSTO. 1787 de 1787 COMPOSITE_BLT terminam em Prova=ZERO_INCONCLUSIVO,
precedidos por "COMPOSITE_DATA_PROOF INCONCLUSIVO_ZERO: zero contra zero NAO prova pixel". Os
5.165 escapes moveram ZEROS entre buffers. Contar operacao como progresso e' a QUINTA ocorrencia
da classe "o gate carimbava o falso sucesso" (A15, mcdm_callbacks_test, C1, sdma_blt_executed).
(3) NENHUM destino foi o scanout: os sete DstMc distintos (0xF40208C000, 0xF400B8C000, ...) sao
backbuffers do DWM; o scanout e' 0xF400000000.
(4) O dwm.exe esta em LACO DE DEVICE-LOST, 90 vezes: Present -> destroi todo recurso ->
DestroyDevice -> CloseAdapter -> recarrega a DLL -> CreateDevice, a ~1 Hz. O Present FALHA. E e'
Flags=0x00000002 = FLIP (dxgiddi.h:71), nao Blt.
(5) NAO HA RASTERIZACAO, e o proprio log declara: "SHADER_TRANSLATOR: TokenStream -> SPIR-V ...
Traducao NAO e' pipeline: descriptors, render target, anel grafico e Draw seguem ausentes."
CAUSA RAIZ DO PRESENT FALHAR (H de alta confianca -- codigo e log concordam; NAO e' P0 porque o
hr do pfnPresentCb nunca foi registrado): a UMD e o KMD discordam do NOSSO PROPRIO contrato de
alocacao. Bc250WinsysAllocate zerava D3DDDICB_ALLOCATE e NUNCA atribuia hResource
(d3dumddi.h:3457), Flags.Primary nem VidPnSourceId (d3dukmdt.h:554 e :549). O dxgkrnl so' liga
DXGKARG_CREATEALLOCATION.Flags.Resource quando a UMD passa hResource, e o ramo do KMD que marca
primaria (bc250_active_miniport.c) esta atras justamente desse bit -- logo era INALCANCAVEL para
toda alocacao nossa. A primariedade existia SO' no nosso token privado, que o KMD nem consultava.
Confirmado no log: todo [CreateAllocation] ENTER de origem UMD traz Resource=0 ->
"Non-resource allocation"; ha UM unico "Primary token confirmed" na corrida e e' o Resource=1 do
bind (handle 0000000800000008, o mesmo do CommitVidPn/SetVidPnSourceAddress 0xAAC000).
FAIXA APLICADA (offline), quatro frentes:
(A) VINCULO DE RECURSO. Bc250WinsysAllocateForResource nova com BC250_WINSYS_RESOURCE_BINDING
(RuntimeResource/VidPnSourceId/Primary); Bc250Umd10CreateResource passa hRTResource.handle.
FONTE UNICA de primariedade: a classe do token manda, e vinculo que DISCORDE dela falha fechado
-- sem isso voltariamos a ter duas verdades, que e' a classe de defeito que a faixa corrige.
Primaria SEM hRTResource e' RECUSADA antes do callback: suceder calado ali produziria de novo
uma "primaria" que o kernel nunca veria como tal. Desalocacao ficou SIMETRICA (criada com
recurso, devolvida com recurso, d3dumddi.h:4068), senao o objeto de recurso do kernel fica orfao.
(B) I1 -- Bc250DxgiPresent registra o hr do pfnPresentCb. Era `return callback(...)` direto: o
numero que decidia a corrida nunca foi gravado.
(C) I2 -- Bc250DxgkDdiPresent tinha TRES saidas mudas (hContext NULL, pPresent NULL,
FrameBufferVirtual NULL). Entrada agora e' INCONDICIONAL e anterior a toda guarda, com contador
proprio (8 primeiras + 1 em 256); a ausencia da linha passa a ser conclusiva.
(D) I3 -- [McdmQAI], [McdmCreateDevice] e [McdmCreateContext] dividiam UM contador de amostragem.
A rajada de QAI no bind (17 linhas medidas) esgota as 16 primeiras da classe, e por isso as 90
criacoes de device do dwm.exe nao deixaram UMA linha. Contadores independentes (12, 13, 14).
(E) No KMD, a primaria de UMD NAO pode mais sobrescrever g_Bc250DisplayState.hPrimaryAllocation
-- ate esta faixa o ramo era inalcancavel e o unico escritor era a primaria do GOP; agora as do
dwm.exe tambem chegam la. Preenche so' se vazio; as demais sao contadas e nomeadas.
EFEITO COLATERAL DECLARADO: com Flags.Resource passando a 1 para recursos de render, o ramo
`else if (Flags.Resource == 0u && info->FlagsWddm2.CpuVisible)` deixa de aplicar-se a eles, e
CpuVisible fica FALSE -- o que REFORCA a correcao do 0x10E em vez de afrouxa-la. ResourceMap usa
shadow de CPU (calloc), nao pfnLockCb, entao nao ha regressao de mapeamento.
Testes: bloco de primaria do bc250_wddm_winsys_test reescrito em torno dos tres campos exigidos
um a um, mais tres casos de fail-closed e um CONTRAPONTO (command buffer/scratch sem recurso
continuam funcionando -- sem ele a correcao viraria peneira). build-umd EXIT=0, build-contract
EXIT=0, tres auditorias PASS, dois portoes novos
(audit_primary_allocation_is_declared_to_the_kernel e audit_present_path_is_not_mute).
NOVE MUTACOES VERIFICADAS: tirar hResource reprova; zerar Flags.Primary reprova; desalocacao
assimetrica reprova; afrouxar a recusa de primaria sem recurso reprova; reverter a divisao de
amostragem reprova; remutar o DdiPresent reprova; voltar ao return direto no Present reprova;
CreateResource sem vinculo reprova; e o KMD sobrescrever a primaria de display reprova.
ARMADILHA REGISTRADA: a primeira passada da mutacao 4 reprovou pelo assert ERRADO -- a allowlist
de token do stub disparava antes do meu invariante. Falha nao-atribuivel e' portao fraco; o stub
foi corrigido para que quem reprove seja "AllocateCalls nao mudou", que nomeia a causa.
ESTA FAIXA NAO ACENDE A TELA, E ISSO E' ESPERADO. Ela remove a parede de APRESENTACAO. A parede
de RASTERIZACAO (sem pipeline grafico, sem descriptors, sem render target, sem Draw PM4) continua
inteira, e enquanto ela existir nenhum pixel e' produzido por ninguem. TELA PRETA segue sendo o
resultado CORRETO; qualquer imagem deve ser atribuida ao rollback/BasicDisplay ate prova em
contrario.
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- tres desfechos, todos distinguiveis:
  1. "[CreateAllocation] Primary token confirmed (UMD #1)" aparece E "[DdiPresent] ENTER #1"
     aparece -> a cadeia de apresentacao fechou; o laco de device-lost do dwm.exe tem de parar
     e "DXGI Present LEAVE cb: hr=0x00000000" confirma pelo lado da UMD.
  2. "DXGI Present LEAVE cb: hr=" com hr != 0 e nenhum [DdiPresent] ENTER -> o dxgkrnl ainda
     recusa, e AGORA o hr nomeia o motivo; a hipotese da primaria cai e o proximo suspeito passa
     a ser o no do contexto (Winsys.KernelContext e' de COMPUTE, node 1).
  3. Nenhuma das duas -> a coleta falhou; defeito de binder, nao de driver.
INFORMACAO NOVA EM QUALQUER DESFECHO, o que nao era verdade na corrida 16:28.
PACOTE LAB 3.173.1.0 assinado, auditado e NAO INSTALADO. BuildKind=MCDM_LAB_BRINGUP, perfil
DISPLAY_RENDER_LAB (RenderNode=1, RenderOnlyLab=0), contrato WDDM 2.7, modo P1 PERSISTENT
SUBMISSION, schema 135. SYS fonte 529B645B..., SYS assinado 8D07EBB4..., UMD assinada AEB6A452...,
INF AE3E88EB..., CAT 9480733D... Lab package audit PASS.
Os binarios foram CONFERIDOS POR STRING (o empacotador nao compila, so' copia): no SYS assinado
estao "[DdiPresent] ENTER #", "RECUSADO: FrameBufferVirtual NULL" e "Primary token confirmed (UMD";
na UMD assinada estao "DXGI Present ENTER cb:", "DXGI Present LEAVE cb: hr=", "Allocate ENTER:
Token=" e "primaria sem hRTResource".
CORRECAO DE REGISTRO: o docs/265 chama a corrida 16:28:27 de "pacote 3.172.0.0". E' FALSO -- o
INF daquele output_lab diz DriverVer=3.170.1.0 e o SYS instalado era 59E08349..., o mesmo que
tools/_last_lab_binding.json registra. O DriverVer fonte foi para 3.173.0.0 (candidato 3.173.1.0)
para nao haver duvida de identidade.
ESTADO DA MAQUINA NA HORA DO EMPACOTAMENTO: BasicDisplay em Codigo 0, fusivel INEXISTENTE (proximo
boot assume CLEAN) e SEM marcador C:\BC250_REQUIRE_CLEAN_BOOT.json. Ainda assim o BOOT FRIO E'
OBRIGATORIO: o rollback de 16:28 foi ONLINE, sem reboot, entao o estado de engine da corrida
anterior continua vivo e nao cai com desbind.
=============================================================================================
CORRIDA 13/09 17:18:35 (P0, lab 3.173.1.0) -- A FAIXA DA PRIMARIA FUNCIONOU, A HIPOTESE NAO
FECHOU, E O LOG PASSOU A RESPONDER. Desfecho 2 do criterio escrito antes da corrida.
ESTABILIDADE, sem regressao: zero TDR_QUIESCE, zero BugCheck, zero IB_TRIPOD TIMEOUT, zero
DEVICE_REMOVED, zero StopDevice, zero GFXHUB_FAULT. Rollback online limpo (RECOVERY_OK_ONLINE,
BasicDisplay Codigo 0 em 5 amostras).
O QUE A CORRECAO (1) CONSEGUIU, P0: (a) CreateAllocation de origem UMD passou de Resource=0
("Non-resource allocation") para 7/7 Resource=1; (b) na UMD, SrcPrimary=1 SrcDeclaredPrimary=1
SrcRTResource=0000000006010000; (c) SetVidPnSourceAddress saiu de 0xAAC000/hAlloc=0x08 (primaria
do GOP do boot) para Addr=0x4B5C000/hAlloc=0xA8, uma alocacao criada pela UMD -- o dxgkrnl so'
aponta o source address para alocacao que ele aceita como primaria; (d) Bc250DxgkDdiPresent foi
ALCANCADA pela PRIMEIRA VEZ na historia do projeto: 6 ENTER, contra zero em todas as corridas
anteriores. Os 6 fecharam ate o silicio -- origem resolvida (SegOff=0x4B60000 -> VramOff=
0x55EC000), blt de 11.059.200 bytes, SDMA=YES, DMA buffer de 4 bytes submetido e executado
(IB EXECUTADO ... FenceId=833).
TELA PRETA PELO MOTIVO PREVISTO: 6 de 6 com "AVISO: Superficie de origem esta com todos os
pixels em zero (NonZero=0/256)" e BYTE_PROOF INCONCLUSIVO (FRAME EM BRANCO). A parede de
rasterizacao segue inteira; nao havia pixel para apresentar.
A HIPOTESE DA PRIMARIA ESTA REFUTADA COMO CAUSA SUFICIENTE, P0: 84 de 84 pfnPresentCb
devolveram hr=0x80004005 (E_FAIL) no MESMO milissegundo da chamada -- rejeicao sincrona de
validacao, nao falha de submissao -- com a primaria ja corretamente declarada. O laco de
device-lost do dwm.exe continua: 61 CreateDevice/DestroyDevice. O I1 pagou: o numero existe.
CORRECAO DO MEU PROPRIO RACIOCINIO: eu escrevera que o proximo suspeito seria "o no do contexto
(COMPUTE, node 1)". ERRADO como formulado -- o GetNodeMetadata da propria corrida diz Node 0 =
EngineType 6 (COPY) e Node 1 = EngineType 1 (3D). Node 1 NAO e' um no de compute para o
dxgkrnl. O que SOBREVIVE e' mais estreito e e' medicao, decodificando DXGK_CREATECONTEXTFLAGS
(d3dkmddi.h:1518, P1): os dois unicos contextos pelos quais o dxgkrnl apresentou estao no
Node 0 -- Ctx 0x04 Flags=0x05 (SystemContext+VirtualAddressing) e Ctx 0x07 Flags=0x06
(GdiContext+VirtualAddressing), que fez os 6 --, enquanto TODO contexto da nossa UMD e' Node 1
Flags=0x04 (VirtualAddressing apenas) e NUNCA apresentou. NAO NOMEIO a causa do E_FAIL:
0x80004005 e' generico e a corrida nao o determinou. E' H.
PONTO CEGO MEU, DECLARADO: "Primary token confirmed (UMD" saiu 0 vezes e isso NAO e'
conclusivo. A classe 9 do amostrador ([CreateAllocation] + [ActiveOpenAlloc] +
[McdmCreateAllocation] + [McdmCloseAllocation]) somou 45 linhas contra o orcamento de 16
primeiras. E' o MESMO defeito de contador compartilhado que eu corrigi na classe 12 e nao
corrigi na classe 9 -- meu proprio I3 foi incompleto, e o buraco caiu em cima da medicao que eu
queria.
FAIXA APLICADA (offline), duas frentes:
(A) I3 COMPLETADO -- as quatro tags da classe 9 ganharam contadores proprios (9, 15, 16 e 17).
A ausencia de "Primary token confirmed (UMD" passa a ser conclusiva.
(B) CONTEXTO DE APRESENTACAO DEDICADO. Bc250WinsysCreatePresentContext/DestroyPresentContext
novas; o Present passa a ir por Winsys.PresentContext, criado no Node 0, enquanto o contexto de
trabalho continua no Node 1. SystemContext/GdiContext sao decididos no kernel e nao estao em
D3DDDI_CREATECONTEXTFLAGS (d3dukmdt.h:791), entao NodeOrdinal e' a unica variavel sob nosso
controle -- e o ensaio move exatamente essa uma. SEM FALLBACK para o contexto de trabalho: a
ausencia do contexto de apresentacao RECUSA com motivo nomeado, porque cair de volta
reproduziria o estado ja medido falhando 84/84 e misturaria as duas variaveis. O teardown
destroi os DOIS -- o dwm.exe fez 61 ciclos de device em ~3 min e contexto nao destruido vaza um
por ciclo.
Testes: bloco novo de coexistencia no bc250_wddm_winsys_test (handles distintos por no, contexto
de trabalho intocado, idempotencia fail-closed, destruicao pelo proprio handle), stubs de
umd_node1_submit_contract_test e umd_node1_tripod_contract_test atualizados para os dois
contextos com invariante "2 criados, 2 destruidos". build-umd EXIT=0, build-contract EXIT=0,
tres auditorias PASS, portoes estendidos em audit_present_path_is_not_mute.
SEIS MUTACOES VERIFICADAS (10..15): reverter a divisao da classe 9 reprova; o Present voltar ao
contexto de trabalho reprova; remover a recusa nomeada reprova; a criacao do contexto de
apresentacao escrever no de trabalho reprova; o teardown perder a destruicao reprova; e o STUB
devolver o mesmo handle para os dois nos TAMBEM reprova -- esta ultima existe porque "o gate
congelava a mentira" ja apareceu cinco vezes no projeto.
ARMADILHA REGISTRADA: a primeira versao do teste criava o contexto de apresentacao em
TestLifecycle e o destruia em TestResidencyAndPrimaryClass -- funcoes diferentes. A suite pegou.
Tres assercoes de contagem que congelavam "1 contexto" tiveram de virar "2 criados, 2
destruidos", e um stub que exigia o handle 0xC001 constante passou a comparar com a FONTE
(context.KernelContext), para continuar valendo se o no do contexto de trabalho mudar.
PACOTE LAB 3.174.1.0 assinado, auditado e NAO INSTALADO. BuildKind=MCDM_LAB_BRINGUP, perfil
DISPLAY_RENDER_LAB (RenderNode=1, RenderOnlyLab=0), WDDM 2.7, P1 PERSISTENT SUBMISSION,
schema 135. SYS fonte 638D83F2..., SYS assinado 957A3873..., UMD assinada 90F90E2A...,
INF B64F0320..., CAT 8F34DF19... Lab package audit PASS. Binarios CONFERIDOS POR STRING (o
empacotador nao compila, so' copia): no SYS "[DdiPresent] ENTER #", "Primary token confirmed
(UMD", "[McdmCloseAllocation]" e "[ActiveOpenAlloc]"; na UMD "CreatePresentContext ENTER",
"DestroyPresentContext ENTER", "PRESENT_CONTEXT: Node=", "PresentNode=" e "DXGI Present
RECUSADO: contexto de apresentacao ausente".
CRITERIO DA PROXIMA CORRIDA, ESCRITO ANTES -- quatro desfechos, todos distinguiveis:
  1. "DXGI Present LEAVE cb: hr=0x00000000" aparece -> o no do contexto ERA a causa; o laco de
     device-lost do dwm.exe tem de parar e [DdiPresent] ENTER cresce muito alem de 6.
  2. "DXGI Present LEAVE cb: hr=0x80004005" com "PresentNode=0" na linha ENTER -> apresentamos
     pelo Node 0 e ainda assim falhou. O contexto MORRE como suspeito, e o proximo passa a ser
     a forma da cadeia de flip (hDstAllocation=0, buffers do swapchain criados um a um, sem
     registro de cadeia).
  3. "DXGI Present RECUSADO: contexto de apresentacao ausente" -> a criacao no Node 0 falhou; a
     linha "PRESENT_CONTEXT: criacao FALHOU" diz o status e o ensaio nao chegou a acontecer.
  4. "Primary token confirmed (UMD #N)" aparece -> fecha, por medicao direta, o que a corrida
     17:18 so' pode sustentar por indicio (o SetVidPnSourceAddress ter ido para hAlloc de UMD).
NAO ESPERAR DESKTOP: a parede de rasterizacao continua inteira e nenhum pixel e' produzido por
ninguem. TELA PRETA segue sendo o resultado CORRETO; qualquer imagem deve ser atribuida ao
rollback/BasicDisplay ate prova em contrario.
ESTADO DA MAQUINA NO EMPACOTAMENTO: BasicDisplay Codigo 0, fusivel INEXISTENTE (proximo boot
assume CLEAN), sem marcador C:\BC250_REQUIRE_CLEAN_BOOT.json. BOOT FRIO CONTINUA OBRIGATORIO: o
rollback de 17:18 foi ONLINE, sem reboot, entao o estado de engine da corrida anterior segue
vivo e nao cai com desbind.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
NADA DESTA FAIXA FOI VALIDADO EM HARDWARE.
=============================================================================================
```

> ℹ️ **O adaptador WDDM alcançou Código 0 em 02/09/2026** (itens 97 e 102 fechados com prova
> física arquivada em `docs/evidence/2026-09-02_21-58-02-DESKTOP-HQAG6T4-mcdm-display-binding/`).
> `DRIVERCAPS`, `QUERYSEGMENT4`, Paging Context e VidPn ativos por 121 s. A etapa atual é P5
> (pipeline de blt por SDMA no scanout GOP para manter tela visível).

---

### Lei do escopo (06/09) — decisão do dono do projeto

1. **O objetivo é WDDM acelerado por GPU e jogos com Vulkan.** Nada mais.
2. **LLM é secundário e é subproduto.** `llama.cpp`, tokens/s e DirectML não são marco, não
   entram em PASS de etapa e não aparecem em veredito.
3. **Compute isolado não é entregável.** Compute vale como **pré-requisito de jogos** — a
   submissão Vulkan passa por ele. Não é destino.
4. **É proibido propor compute ou LLM como "caminho mais rápido" quando o item de vídeo
   estiver bloqueado.** Item travado se resolve, se declara travado, ou se declara
   `NÃO VALIDADO EM HARDWARE`. Trocar a meta por uma mais fácil é fuga de escopo.
5. **Demorar é aceito.** Custo em tempo não é argumento para trocar o objetivo.
6. Critério final: **jogo real na tela ligada à placa, sem WARP, com TDR recuperando de hang
   sem perder o display.**

> ⚠️ **Plataforma real (06/09):** **Windows 10 Pro 22H2, build 19045, UBR 2965** — não
> Windows 11. Confirmado pelo dump do DWM e pelo registro da máquina. WDDM máximo aqui é 2.9,
> compatível com o contrato 2.0/2.7 em uso.

> ⚠️ **Barreira ativa do vídeo (06/09, decidida por dump):** o `dwm.exe` exige device
> **D3D11** em `CLegacyRenderTarget::EnsureSwapChain` → `CD3DDevice::CreateD3D11Device`, e ao
> receber `DXGI_ERROR_UNSUPPORTED` ele **fail-fasta** em vez de cair para software.
> `DriverSupportsCddDwmInterop` **não** é rota de fuga — já está declarado e não mudou nada.
> Logo **backend D3D10/11 real na `bc250umd.dll` é obrigatório** para vídeo acelerado.

---

## ⚖️ 2. Leis inegociáveis

### Lei 1 — Sinceridade absoluta
Reporte o que a medição diz, não o que seria bom. Se uma prova estiver ausente, escreva
literalmente **`NÃO VALIDADO EM HARDWARE`**. Nunca suavize, nunca arredonde para cima,
nunca chame hipótese de causa. Se você errou antes, corrija em uma frase e siga.

### Lei 2 — Tripé de validação no silício
Não declare sucesso, aceleração ou conclusão sem as três provas físicas:
1. `RPTR == WPTR` na engine correspondente;
2. fence física gravada **pela GPU** na VRAM, com verificação de poison;
3. dados destino conferidos **byte a byte** contra gabarito da CPU.

### Lei 3 — Todo item fecha com um teste, e o teste é nomeado
Ao propor ou concluir qualquer trabalho, diga **qual teste** comprova execução no hardware
e **onde a evidência será arquivada**. A Tabela 2 da `BASE_DE_AVANCO.md` já traz a prova
exigida de cada item — use-a. Trabalho sem teste nomeado não é entregável.

### Lei 4 — Zero fakes, zero fallbacks de software
- Nunca incremente fences ou timestamps pela CPU.
- Nunca use `KeStallExecutionProcessor`, `Sleep` ou loop de delay simulando execução.
- Nunca retorne `STATUS_SUCCESS` em DDI sem backend funcional.
- Nunca trate compilação, assinatura, `vulkaninfo`, enumeração de API, carregamento de DLL
  ou abertura de swapchain como prova de aceleração.
- Nunca proponha WARP, GDI, llvmpipe ou emulação por CPU disfarçada de GPU.

### Lei 5 — Ciclo de vida transacional
Toda interação de baixo nível com hardware:
```text
[PREFLIGHT] → [SNAPSHOT] → [APPLY] → [READBACK] → [PROVA NO SILÍCIO] → [ROLLBACK/TEARDOWN]
     |                                                    |                      |
     +---(abortar se inválido)                            +---(falhar fechado)---+
```

### Lei 6 — Fórmula de bits SOC15
```c
uint32_t val = (lido & ~mask) | (or_val & mask);
```

### Lei 7 — Endereços só com proveniência
Use apenas offsets catalogados em
[`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md), com fonte
citada. Nada em quarentena (`Q`). No DCN o endereço é
`DMU_BASE.seg[BASE_IDX] + reg` — **não existe base plana**
(`DMU_BASE = {0x12, 0xC0, 0x34C0, 0x9000}`).

### Lei 8 — Vocabulário de confiança
Classifique toda afirmação técnica: **P0** provado no silício · **P1** derivado do upstream
· **P2** parcialmente observado · **H** hipótese · **Q** quarentena (proibido).
Detalhe em [`rules.md`](rules.md).

### Lei 9 — Economia de corridas físicas
A corrida no hardware é o recurso escasso, e o **Daniel** é quem a executa — eu compilo,
analiso e preparo. Antes de propor ensaio: responda o que puder por leitura de fonte,
mineração de `docs/evidence/` e comparação com drivers reais da máquina. Defina teto de
corridas e critério de abandono **antes** de armar. Boot frio entre binds.

### Lei 10 — Como se escreve o critério de uma corrida

O critério é escrito **antes** do ensaio (Lei 9) e obedece a três regras que nasceram de falhas
medidas, não de estilo:

1. **Assinatura e implicação são campos separados.** Cada desfecho declara *o que o log vai
   mostrar* e, em campo distinto, *para onde isso leva a investigação*. Na Corrida 39 os dois
   vinham acoplados: a assinatura do desfecho D foi falsificada (*"zero `UploadImageData`"* — houve
   21) enquanto a conclusão de D se confirmou (*"o DWM não usa `pInitialDataUP`"*). Pacote fechado
   força escolher entre aceitar uma conclusão certa por uma assinatura errada, ou descartar as
   duas. Ambas as saídas são falsas.

2. **Todo critério leva um desfecho final `E — algo fora do mapa`,** com a instrução de descrever
   o observado antes de julgá-lo. Na Corrida 39 o maior resultado — 118 submissões gráficas sem
   hang, com IBs dez vezes maiores que o que travava — não estava em nenhuma das quatro letras,
   porque a convergência fora tratada apenas como *controle a não regredir*. Escrever só o
   esperado cega para o inesperado, e nesse caso o inesperado era o avanço.

3. **Desfecho parcial não é desfecho.** Se um desfecho exige três sinais e aparecem dois, ele
   **não** ocorreu — em especial quando o sinal ausente é o que o define. Na Corrida 39 o desfecho
   A teve upload e submissões maiores, mas não teve `VK_DESCRIPTOR] BOUND`; chamar aquilo de "A
   parcial" teria transformado ausência de draw em quase-sucesso.

---

## ⚡ 3. Comandos rápidos

### Driver de laboratório (`bc250dbg.sys`)
- Build: `dbg\build.bat` · Instalar: `dbg\install.bat`
- Iniciar: `dbg\start.bat` · Parar/remover: `dbg\uninstall.bat`

### Driver WDDM de produção (`bc250mcdm.sys`)
```
powershell -ExecutionPolicy Bypass -File tools\build-bc250mcdm-package.ps1
python tools\audit-mcdm-ddi-matrix.py
python tools\audit-bc250mcdm-safety.py
python tools\audit-ddi-noop-callbacks.py
```
> ⚠️ O empacotador **não compila o SYS** — ele copia. Pular o build de laboratório assina
> o binário do candidato anterior.

### Ensaios físicos
```
powershell -ExecutionPolicy Bypass -File tools\capture-baseline.ps1
powershell -ExecutionPolicy Bypass -File tools\test-sdma-robustness.ps1 -Execute
powershell -ExecutionPolicy Bypass -File tools\test-gcvm-translate.ps1 -Execute -IAcceptTheRisk
```

### Análise de crash dump (exige PowerShell elevado)
```
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z C:\Windows\MEMORY.DMP -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols" -c "!analyze -v; lm m bc250*; q"
```

### Recuperação de emergência

| Situação | Ação |
|---|---|
| Windows inicia | `tools\BC250_RECOVERY.cmd` (ou `C:\BC250_RECOVERY.cmd`) restaura o BasicDisplay |
| Windows inicia, limpeza completa | `tools\clean_all_drivers.ps1` elevado (DriverStore + SCM) |
| **Não inicia: tela preta ou loop de boot** | `tools\BC250_WINRE_CLEAN.cmd` pelo **Prompt de Comando do WinRE**, com o arquivo num pendrive. Ver [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) |

> ⚠️ **Nunca** use `del C:\Windows\System32\drivers\bc*.sys`. O padrão casa com
> `bcmfn2.sys`, driver **inbox da Microsoft**. E em WinRE o Windows raramente é `C:`.

---

## 💻 4. Padrões de código C (Windows kernel mode)

- **Dialeto:** C99/C11 compatível com MSVC x64 e WDK.
- **Rigor:** todo código passa limpo em `/W4 /WX` — zero warnings.
- **SAL:** `_In_`, `_Out_`, `_Inout_`, `_Check_return_`, `_Pre_null_`.
- **Sem CRT de user-mode:** proibido `malloc`, `free`, `printf`, `exit`.
  - Alocar: `ExAllocatePool2(POOL_FLAG_NON_PAGED, size, tag)`
  - Liberar: `ExFreePoolWithTag(ptr, tag)`
  - Memória: `RtlCopyMemory`, `RtlZeroMemory`, `RtlEqualMemory`
  - Log: `DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, ...)`
- **Defensivo:** checar ponteiros contra `NULL`; checar `NTSTATUS` com
  `if (!NT_SUCCESS(status)) { ... return status; }`; cleanup garantido em toda falha de
  alocação ou mapeamento.

---

## 📂 5. Mapa do repositório

| O quê | Onde |
|---|---|
| **Base de avanço (o que falta, prioridade, testes)** | [`BASE_DE_AVANCO.md`](BASE_DE_AVANCO.md) |
| **Roadmap detalhado** | [`docs/164_ROADMAP_WDDM_JOGOS.md`](docs/164_ROADMAP_WDDM_JOGOS.md) |
| Regras normativas e vocabulário de confiança | [`rules.md`](rules.md) |
| Catálogo de registradores | [`docs/03_CATALOGO_DE_REGISTRADORES.md`](docs/03_CATALOGO_DE_REGISTRADORES.md) |
| Provas físicas P1 / P2 | [`docs/148...`](docs/148_VITORIA_FASE_P1_VALIDADA_EM_HARDWARE.md) · [`docs/159...`](docs/159_VITORIA_HISTORICA_FASE_P2_VALIDADA_EM_HARDWARE.md) |
| Prova de ownership de display | [`docs/26...`](docs/26_POST_DISPLAY_OWNERSHIP_FISICO_1.1.0.0.md) |
| Por que render-only foi reprovado | [`docs/162...`](docs/162_ENCERRAMENTO_GATE_1_5_RENDER_ONLY.md) |
| Corpus de evidência física | `docs/evidence/` |
| Miniport WDDM | [`driver/wdk/`](driver/wdk/) |
| UMD (D3D / Vulkan / runtime) | [`driver/umd/`](driver/umd/) |
| Driver de laboratório KMDF | [`dbg/`](dbg/) |
| ABI de diagnóstico | [`inc/bc250dbg_abi.h`](inc/bc250dbg_abi.h) |
| Harness, auditorias e scripts | [`tools/`](tools/) |
| Recuperação | [`RECOVERY_BC250V4.md`](RECOVERY_BC250V4.md) · `tools/BC250_RECOVERY.cmd` |
| Arquivo morto (nada apagado) | [`DocsObsoletos/INDICE.md`](DocsObsoletos/INDICE.md) |

---

## 🔄 6. Manutenção destes arquivos

`CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `.cursorrules` compartilham o mesmo núcleo
normativo. **Ao mudar uma lei ou a regra de relato de corridas, atualize os quatro.** Divergência entre
eles já produziu, no passado, agentes declarando sucesso inexistente.
