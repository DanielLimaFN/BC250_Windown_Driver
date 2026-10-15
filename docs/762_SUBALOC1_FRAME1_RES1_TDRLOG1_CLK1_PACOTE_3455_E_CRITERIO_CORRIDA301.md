# Doc 762 — SUBALOC.1, FRAME1, RES.1, TDRLOG.1 e CLK.1: pacote 3455 e critério da Corrida 301

> **Data:** 14/10/2026 (offline). **Decisão do Daniel:** a 301 leva os itens 9, 6, 1, 2 e 7 do checklist (doc 761 e resposta seguinte).
> **Base:** Corrida 300 (14/10 22:18, lab 3.454.1.0; evidência `docs/evidence/2026-10-14_22-18-01-DESKTOP-HQAG6T4-lab-binding/`, logs em `umd-logs-pos-travamento/`): GravityMark ~8,6 quadros/s com a GPU 5–48% ocupada e o clock fixo em 1.500 MHz; 2 faults no back buffer da WSI; TDR do GFX e trava dura entre o `ResetFromTimeout` e o restart.
> **Pacote:** lab **3.455.1.0** (SYS `510A1D86`, UMD `30D56611`, RADV `C6C9E7F3` igual à 300, no pacote `296F3F8F`). Auditoria do pacote PASS; `audit-bc250mcdm-safety.py` PASS com o portão novo `audit_301_tdrlog_clk_subaloc` (mutação "faixa até 3000 MHz" reprova). **NÃO VALIDADO EM HARDWARE.**

## 0. Correção de registro

No doc 761 e na análise da 300 eu li `SlotSeg=0` do `REJECT source contract` como "alocação não residente". **Isso não se sustenta:** os Presents do GravityMark que **funcionaram** também trazem `Info1: Seg=0` para a mesma alocação. A recusa vem da tradução do VA da origem (`Bc250DisplayResolvePresentSource`), que falhou naquele Present. O mecanismo provável continua sendo a memória por trás de `0x200016000` sair do lugar (o fault de escrita é no mesmo endereço e no mesmo segundo, com despejos subindo de 1404 para 1478), mas a prova não é o `SlotSeg`.

## 1. Mineração da 300 que define o item 6 (P0, `BC250_HANG` do dwm.exe)

| DWM | 299 | 300 |
|---|---|---|
| Quadros/s | 26,1 | 10,4 |
| Intervalo mediano entre quadros | 17,9 ms | 29,7 ms |
| Criar recurso | 33,8 ms cada, 5,1% do tempo | 33,6 ms cada, 10,1% |
| Destruir recurso | 27,7 ms cada, 4,0% | 20,2 ms cada, 5,6% |
| Esperar paginação | 16,5 ms cada, 2,5% | 15,0 ms cada, 4,6% |
| `PresentCb` | 0,5 ms | 0,6 ms |

~80% do tempo não tem marca. Achado de código no caminho: **todo flush da UMD espera a GPU na CPU** (`vkWaitForFences` logo depois do `vkQueueSubmit`, `bc250_umd_vulkan_bridge.c`).

## 2. As cinco mudanças

| Item | Nome | O que faz | Onde |
|---|---|---|---|
| 9 | **SUBALOC.1** | Buffers e texturas de até 1 MiB, sem render target/depth e não compartilhados, saem de blocos de 4 MiB (fatias de 4 KiB a 1 MiB, mapeamento único para memória visível à CPU) em vez de um `vkAllocateMemory` cada. A fatia volta ao bloco na destruição; a rotação de swapchain gira os campos novos. Desliga com `HKLM\SOFTWARE\BC250\Umd\SubAlloc=0`. A alocação WDDM de cada recurso (a outra metade do custo, doc 755) **continua**. | `bc250_umd_vulkan_bridge.c/.h`, `bc250_umd_resource_vk.c`, `bc250_d3d_umd.c/.h` |
| 6 | **FRAME1** | Em todo processo, uma linha `[FRAME1]` a cada ~2 s de Presents: criar, destruir, paginação, Present, envio (fim do CB + `vkQueueSubmit`), espera da CPU pela GPU no flush, desenho, Map, o que foi dentro de criar/destruir, a soma e o resto (app ou ocioso). Sempre ligado, independe do `HangTrace`. | `bc250_wddm_winsys.c/.h`, `bc250_d3d_umd.c`, bridge |
| 1 | **RES.1** | O back buffer capturado pela WSI recebe prioridade **máxima** de residência (`pfnSetPriorityCb`, `D3DDDI_ALLOCATIONPRIORITY_MAXIMUM`); linha `[RES1]`. **Mitigação, não garantia:** a garantia exige a alocação compartilhada e aberta pelo device do app (o runtime cria o back buffer; a UMD sozinha não o compartilha). | `bc250_umd_wsi.c`, winsys |
| 2 | **TDRLOG.1** | Cada passo do TDR vai a `C:\BC250_TDR_PASSOS.txt` com `FILE_WRITE_THROUGH` e flush **antes** de rodar: entrada e saída do Reset e do Restart, SDMA0, GFXRST1, cada etapa da GFXRST.2 (escalonamento, pós-reset, rearme, soltar o ME, prova), MEC e o fim. As DDIs chamadas com a janela do TDR aberta (`BuildPagingBuffer`, `SubmitCommandVirtual`, `QueryEngineStatus`, `PreemptCommand`) deixam uma linha **sem disco** (a paginação não pode fazer I/O) num anel em RAM que o dump preserva (`g_Bc250TdrSteps`). O roteiro renomeia o arquivo anterior. | `bc250_display_ddi.c/.h`, `bc250_mcdm_callbacks.c`, `bc250_active_miniport.c` |
| 7 | **CLK.1** | No StartDevice, com o GFX provado: `RequestGfxclk` (0xE) com a chave `GfxClkMhz` e `UnforceGfxVid` (0x3C), como o `cyan_skillfish_ppt.c` (P1, mensagens conferidas no `smu_v11_8_ppsmc.h`), faixa 1000–2000 MHz. Lê clock, alvo e VID antes e depois (`[CLK1]`); devolve o alvo original no StopDevice. INF padrão 0; o pacote da 301 materializa **1800** (`-GfxClkMhz 1800`), passo conservador sobre os 1.500 medidos. | `bc250_active_miniport.c/.h`, INF, `package-bc250mcdm-lab.ps1` |

## 3. Critério da Corrida 301 (escrito antes)

**Roteiro:** `tools\EXECUTAR_CORRIDA301_SUBALOC_FRAME1_RES1_TDRLOG_CLK1_600S.cmd`, elevado; antes: fusível (`inspect-boot-fuse.ps1 -Reset`), desligar e ligar. Menu Iniciar 3×, vkcube, TFM, GravityMark (mesmas opções da 300), **sem memtest**, desktop até o fim. Se travar: anotar a hora e, depois do reboot, mandar `C:\BC250_TDR_PASSOS.txt`.

| Item | Sucesso (assinatura) | Falha / implicação |
|---|---|---|
| **SUBALOC.1** | `[SUBALOC1] ... LIGADA`, blocos criados, `[FRAME1]` do dwm com criar bem abaixo de ~34 ms por chamada; menu mais rápido (operador) | criar igual → o custo está na alocação WDDM, próxima é ela |
| **FRAME1** | linhas `[FRAME1]` no dwm e nos apps 3D; o "resto" nomeia a parte não medida | sem linhas → defeito de instrumentação |
| **RES.1** | `[RES1] ... hr=0x00000000` e zero `GFX_FAULT_SENTINELA` em endereço de back buffer de WSI | fault de novo → prioridade não basta; próximo é o compartilhamento |
| **TDRLOG.1** | se houver TDR, o arquivo termina no passo que não voltou (ou em `RestartFromTimeout SAIDA`) | arquivo vazio com TDR → a janela morre antes do 1º passo, ou fora de PASSIVE (o irql está em cada linha) |
| **CLK.1** | `[CLK1] APLICADO: pedido=1800 ... RequestGfxclk=OK`, `[SMU1] GFXCLK` perto de 1800 e `[CLK1] DEVOLVIDO` no fim | `RECUSADO` → a SMU não aceita o pedido assim; travas novas logo após o bind → suspeita do clock, voltar a 0 |
| **E** | fora do mapa: descrever antes de julgar | — |

**Riscos declarados:**
- CLK.1 muda a frequência do GFX com a tensão escolhida pelo firmware. Instabilidade nova (TDR ou trava logo depois do bind) entra como suspeita direta do CLK.1.
- A SUBALOC.1 muda onde a memória dos recursos pequenos mora; textura errada ou lixo em ícone aponta para offset de fatia.

## 4. Tabela viva do DCN

Sem mudança. Prontos: D01–D05, D08, D10, D15, D32–D33. Abertos: D06/D07, D11, D12–D14, D18–D23, D25–D29, D31.
