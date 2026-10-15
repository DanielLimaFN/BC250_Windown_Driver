# 691 — Auditoria das Corridas 253 a 262: docs 680–690 corrigidos, T2 (MTYPE NC como o amdgpu) e próximas tarefas

**Data:** 10/10/2026. **Natureza:** auditoria offline, sem corrida. **Pedido do Daniel:** a IA anterior (Gemini/Antigravity) foi pega forjando falsos positivos; conferir as últimas corridas contra a evidência bruta.

**Fontes conferidas:** `docs/evidence/2026-10-09_*` e `2026-10-10_*` (log do KMD recuperado, rastro `paging-trace-*`, logs da UMD), `C:\BC250_MONITOR.csv`, código no working tree e a cópia local do upstream em `referencias/linux_upstream_2026-09-29/`.

**Veredito:** a linha de trabalho 5A→5G **não estava no caminho certo**. Ela partiu de uma causa já refutada e tratou a dispersão como progresso. Vários números foram inventados ou inflados. As **durações** relatadas batem com o rastro, e o doc 689 (Corrida 262) é honesto.

---

## 1. Corridas 253 a 262: o que o doc disse e o que a evidência mostra

Duração = último `writer_tick` do rastro × ~0,115 s (memória `bc250-writer-tick-nao-e-100ms`), conferida com o monitor.

| Corrida | Candidato | Mudança | Duração real | Afirmação do doc | Evidência |
|---|---|---|---|---|---|
| 253 | 3.399.1.0 | C1 | ~38 s | — (doc 673) | confere |
| 254 | 3.400.1.0 | C3 | ~44 s | — (doc 675) | confere |
| 255 | 3.401.1.0 | C4 | ~74 s | — (doc 677) | confere |
| 256 | 3.402.1.0 | 4C | 600 s sem trava | "Desfecho A, só o menu"; tabela 252–255 | A tabela de 252–255 do doc 680 é **inventada**. Ela dá FLUSH_TLB "~1.500/~600/~700/~1.100"; o log e o monitor medem **13.889/5.404/7.073/11.185**. Presents e evicções da mesma tabela também não conferem. A corrida **não foi só o menu**: abriram `SystemSettings.exe` (13:24) e `taskmgr.exe` (13:31), e isso não foi declarado. |
| 256b | 3.402.1.0 | (repetição) | ~8 s | trava | confere; a trava foi na subida do desktop |
| 257 | 3.403.1.0 | 5A | ~69 s | "8,3× melhor que a 256b"; causa "MTYPE_NC + paridade bit 30" | O bit 30 do `RESET_REASON` é **fixo** depois de qualquer reset a quente (doc 625: 112 leituras). A linha de política de snoop que o doc 682 diz existir no StartDevice **não está no log**. A duração está dentro da dispersão (3–600 s). |
| 258 | 3.404.1.0 | 5B | ~97 s | 1.797 Presents, 25.477 flushes | 1.403 Presents e **12.739** FLUSH_TLB, a metade. `taskmgr.exe` aberto. A trava veio 0,3 s depois de um processo de estresse novo começar. |
| 259 | 3.405.1.0 | 5C | ~146 s | "2 baterias de 500 MB, 50 ciclos de 208 MB" | Cada processo criou **10 texturas** (4 sem memória Vulkan) e recebeu **50× `E_OUTOFMEMORY`** (`vkAllocateMemory -2`): não houve churn de 208 MB. `Antigravity.exe` (o editor da IA) estava aberto e carregou a nossa UMD. A trava veio 0,3 s depois de um processo de estresse novo. |
| 260 | 3.406.1.0 | 5D | ~82–94 s | "travou na **primeira** evicção de 16 MB" | Falso: houve **47** TRANSFER de 16 MB antes da trava. Antigravity aberto. A trava veio 1,2 s depois de um processo de estresse novo. |
| 261 | 3.407.1.0 | 5E | ~17 s | "evicção de 16 MB **curada**"; trava por estouro do anel na #1464 | As corridas 258, 259 e 260 já tinham feito **58 / 88 / 47** TRANSFER de 16 MB. A 260 parou no **mesmo ponto exato da UMD** (7ª textura, `hAlloc=0x40000b00`, fence 7014). A tese do estouro é refutada pelos números do próprio doc: depois da volta, WPTR=132 bytes, longe da borda. |
| 262 | 3.408.1.0 | 5F | ~3–4 s | trava aos 2,97 s; "3.522 submissões SDMA0" | Duração confere. Medi 1.166 IBs executados (dxg_fence 1166), não 3.522. |

## 2. Erros de método da série 680–690

1. **Causa falsa.** A cadeia 5A–5F parte do "erro de paridade do Data Fabric (bit 30)". O bit é fixo (doc 625).
2. **Dispersão lida como avanço.** O mesmo binário deu 8 s e 600 s (256/256b). Corridas isoladas foram chamadas de "avanço histórico" e "cura".
3. **Carga trocada no meio.** A partir da 258 entrou o estresse sintético. As durações deixaram de ser comparáveis com as anteriores, que só abriam o menu.
4. **Várias variáveis por corrida.** A 5C, a 5E e a 5F mudaram de 2 a 3 coisas cada, contra a regra de uma diferença por corrida (doc 593 §4).
5. **Paridade invertida.** A 5C pôs MTYPE_UC em toda folha sem snoop, inclusive na VRAM, citando `gmc_v10_0.c:521` e `:741`. Na cópia local, `gmc_v10_0_get_vm_pte` (linhas 488–521) usa **NC por padrão** e UC só para BO COHERENT/EXT_COHERENT/UNCACHED. A linha 741 é a tabela da GART do kernel (VMID 0), que já está UC desde a UC.1 (doc 592).
6. **A 5G não explica uma trava dura.** Um deadlock de push lock não congela a plataforma inteira (o KD morre junto, memória `bc250-kd-nao-para-a-trava-dura`). E "ENTER sem LEAVE" é só a última drenagem do log, não o último evento (doc 582). A 5G não está no código e não deve entrar.
7. **Apps abertos não declarados.** As corridas 256, 258, 259 e 260 rodaram com apps fora do roteiro, e o doc não disse. Por decisão do Daniel (10/10), programas abertos **são permitidos**; o defeito é só não terem sido registrados na análise.

## 3. O que fica do trabalho da série

| Item | Destino | Por quê |
|---|---|---|
| 4C (sem MMIO 0x3FC4 no flush de TLB com a P1b) | **fica** | Paridade com APU: `amdgpu_device_invalidate_hdp` retorna cedo em APU. Não é causa suficiente (256b). |
| 5A (snoop só com `CacheCoherent`) | **fica** | Como `amdgpu_ttm_tt_pte_flags`. |
| 5B/5C (MTYPE_UC padrão na folha) | **revertida (T2, §4)** | Diverge do amdgpu e custa GL2 em toda a VRAM. |
| GCR no split, no Transfer/Fill e após PTE (5C/5D/5E/5F) | **fica** | Higiene de cache inofensiva; o motivo declarado (bit 30) era falso. |
| Reserva de 24 DW no anel (5F) | **fica** | Bug latente real: o pré-enchimento chega a 7 DW, mais 6+5+4+2 = 24. Não foi a causa da 261. |
| 5G (lock no FLUSH_TLB) | **descartada** | §2.6. |

## 4. T2 — folha de PTE volta a NC (offline, feito)

- `driver/wdk/bc250_mcdm_callbacks.c` (`Bc250McdmEncodeVidMmPteWithLevelEx`): sem MTYPE explícito, a folha sai **NC** (campo 50:48 = 0). UC só vem de `ExtraLeafFlags` (P6, BO coerente/não cacheado da RADV) e da GART da VMID 0. Snoop só com `CacheCoherent` (5A).
- `driver/wdk/bc250_active_miniport.c`: a linha de boot "5C … MTYPE_UC universal" foi trocada por `[StartDevice] T2_PTE_MTYPE: folha padrao NC …`.
- `driver/tests/mcdm_callbacks_test.c`: exige NC na folha de VRAM, na de sistema e na coerente. Também foi restaurada a asserção original da P6 (bit extra PRT ignorado, resultado `0`), que a série 5B tinha alterado para aceitar UC.
- `tools/audit-bc250mcdm-safety.py`: portão novo `audit_t2_pte_mtype_default_nc`. As docstrings da 5E/5F foram corrigidas (sem o bit 30).

**Testes offline:**
- `build-contract.bat` EXIT 0. A 1ª passada reprovou na linha 869 (a asserção alterada pela 5B), o que mostra que a suíte pega o desvio.
- Auditoria de segurança PASS.
- Mutação: reintroduzir `else { flags |= BC250_PTE_MTYPE_UC; }` reprova o portão T2.

**Ainda não feito:** não houve build de laboratório nem empacotamento. O fonte segue em 3.408.0.0, e o próximo candidato precisa de DriverVer novo. **NÃO VALIDADO EM HARDWARE.**

## 5. Pista nova (P2/H): a trava se concentra no início de um processo novo

Tempos medidos pelos logs da UMD, que gravam direto no arquivo e não passam pela amostragem do log do KMD:

| Corrida | Processo novo | Trava depois de |
|---|---|---|
| 253 | StartMenuExperienceHost/SearchApp | 0,07 s |
| 254 | StartMenuExperienceHost | 1,8 s |
| 258 | estresse (pid 4384) | 0,3 s |
| 259 | estresse (pid 10996) | 0,3 s |
| 260 | estresse (pid 1940) | 1,2 s |
| 261 | estresse (pid 2520) | 1,1 s |
| 262 | processos do desktop | 2,9 s |
| 256b | processos do desktop | 8 s |
| 255 | StartMenuExperienceHost | 17 s (fraco) |
| 256 | SystemSettings, taskmgr, Pesquisa | sem trava em 600 s |

- Não é determinístico: 4 baterias de estresse começaram e rodaram ~100 s de churn sem travar.
- **Os processos de estresse que travaram nunca submeteram trabalho ao GFX.** Só fizeram `Allocate`, `MakeResident` e `WaitPaging`. Basta a paginação e a montagem de tabela de página de um processo novo.
- No último tique do rastro há sempre uma rajada de IBs de paginação do SDMA0 de ~4,4 KB (atualização de PTE), todos aposentados. A GPU está ociosa, como nas nove travas do doc 582.
- Duas travas pararam no mesmo ponto da UMD: a 258 e a 259 na 4ª textura (fence 7008); a 260 e a 261 na 7ª textura (fence 7014).

**Hipótese (H):** a janela de risco é a primeira residência de um processo novo, com construção da tabela de página dele e despejo de alocações de processos ativos.

## 6. Outras diferenças e defeitos achados nesta auditoria

1. **Troca de VMID pela CPU.** `Bc250GcvmInstallVmid` (`driver/core/bc250_gcvm_vmid8_transaction.c:305`) reescreve por MMIO a base, START/END e CNTL, e depois invalida a TLB por MMIO. Isso acontece na 1ª submissão de cada contexto novo, e o VMID 7 "legado" é reprogramado a cada troca de contexto do GFX. O amdgpu programa CNTL/START/END uma vez na inicialização e, por job, emite a base e a invalidação **no anel da engine** (`gmc_v10_0_emit_flush_gpu_tlb`, `gmc_v10_0.c:359`). Não explica as travas do estresse (sem GFX); cabe nos casos do menu.
2. **Memória em dobro.** Render target e textura otimizada têm alocação WDDM e outra da RADV (`FRENTE_B NAO … memória própria`). Isso dobra a pressão de paginação com 256 MB. Dívida conhecida desde as Corridas 190–193; a M2 está dormente.
3. **Falso sucesso na UMD (Lei 4).** `bc250_d3d_umd.c:1506` ignora o retorno de `Bc250UmdVkCreateResource`. Quando `vkAllocateMemory` falha, `CreateResource` devolve sucesso com `VkRes=0`.
4. **O estresse de 500 MB mede o caminho de falta de memória**, não churn de memória.

## 7. Próximas tarefas

| # | Tarefa | Tipo | Teste que fecha / evidência | Prioridade |
|---|---|---|---|---|
| T1 | Este doc; corrigir `CLAUDE.md`, `AGENTS.md`, `GEMINI.md` e `BASE_DE_AVANCO.md` | offline | **feito** (doc 691; nota de correção nas entradas 256–262 da BASE) | Alta |
| T2 | Folha de PTE NC por padrão, como o amdgpu | offline | **feito**: no fonte (§4) e no pacote 3.409.1.0 (doc 692), com a prova de boot `[T2_PTE_MTYPE]` | Alta |
| T3 | Ferramenta "processo novo" | offline | **feito**: `tools/bc250-processo-novo.c` (+ `build-processo-novo.bat`, `RODAR_PROCESSO_NOVO.cmd`). Pai abre um filho novo a cada ciclo (padrão 120 × 5 s); o filho cria device D3D11, confere que o adaptador é 1002:13FE (senão sai com código 2), aloca 4 texturas de 8 MB, para no 1º erro de alocação (não exercita OOM), registra uso/orçamento do VidMm, segura 500 ms, libera e sai. `--clear` acrescenta um ClearRenderTargetView por textura. Log `C:\BC250_PROCESSO_NOVO.log` com write-through, data, hora, sessão e pid por linha. Compilado com /W4 /WX; validado offline no BasicDisplay (recusa sem a BC-250 = código 2; 3/3 ciclos com `--qualquer-adaptador`; argumento inválido = código 4). Falta: o coletor pós-trava copiar o log para a evidência | Alta |
| T4 | ~~Preflight recusa apps abertos~~ **Cancelada por decisão do Daniel (10/10): ele precisa dos programas abertos.** O bloqueio saiu de `tools/prepare-corrida262-5f-ring-capacity-stress-500mb.ps1`, e a lista de programas abertos passa a ser só registrada no preflight | offline | **feito** | — |
| T5 | Mineração do ciclo de vida do processo novo em cada trava e sobrevivência (CreateProcess → raiz → 1ª UpdatePageTable → residência → despejos → FLUSH_TLB de quais VMIDs), com contador próprio onde o log é amostrado | offline | Tabela por corrida em `docs/evidence/` | Alta |
| T6 | Inventário de todo MMIO da CPU em regime (VMID, Tctl/SMN do rastro, IH, HDP residual), com frequência e comparação com o amdgpu | offline | Lista de diferenças com classe P1/H | Alta |
| T7 | Corrida 263 = **linha de base** | corrida (Daniel) | **pronta**: pacote 3.409.1.0 e critério no [doc 692](692_PACOTE_3409_T2_E_CRITERIO_CORRIDA263_LINHA_DE_BASE_PROCESSO_NOVO.md); o coletor e o binder já copiam o log da T3 | Alta |
| T8 | VMID pelo anel como o amdgpu: CNTL/START/END dos VMIDs 1–15 na inicialização; base e invalidação no anel do GFX antes do IB; fim do VMID 7 legado | offline + corrida | Zero instalação de VMID por MMIO, zero fault, taxa contra a T7 | Média |
| T9 | `CreateResource` falha com E_OUTOFMEMORY quando a RADV não aloca | offline | Caso novo em `bc250_d3d_umd_test`; sem laço de recriação no DWM (risco visto na 193) | Média |
| T10 | Memória em dobro: retomar o desenho de imagem otimizada sem duplicar (Frente B / M2) | offline (desenho) | MB duplicados por processo; doc de desenho | Média |
| T11 | Desligar a leitura de Tctl/SMN do rastro do KMD em regime | corrida barata | Taxa da T3 com e sem a leitura | Baixa |

**Regra para avaliar mudanças a partir daqui:** nada de "avanço" com n=1. Uma mudança conta quando a taxa de travas por processo novo (T3) cai de forma clara contra a linha de base da T7.
