# 629 — Corrida 239 (só o menu): três TDRs do GFX recuperados e trava dura 22 s depois do primeiro

**Corrida:** 07/10/2026, bind às 00:06:36. Mesmo lab **3.384.1.0** da 236–238, CMOS 256 MB, boot frio aceito pelo preflight novo (sem bit 19), Edge/WebView2/M365Copilot fechados. Critério: [doc 628](628_CRITERIO_CORRIDA239_SO_O_MENU_INICIAR.md).
**Evidência:** `docs/evidence/2026-10-07_00-06-36-DESKTOP-HQAG6T4-lab-binding/` (coleta automática: `display-log-this-bind-RECUPERADO.txt`, `paging-trace-*`, `umd-logs-pos-travamento/`, `ramtest-pos-trava.txt` sem erros).
**Relato do Daniel:** "quase terminando eu abri o menu, ficou uma tela preta por uns 10 s, depois voltou a mostrar o desktop porém travado, aí reiniciei".

## 1. Linha do tempo (P0; QPC convertida pelo boot de 00:02:01 local)

| Hora | Evento |
|---|---|
| 00:06:45 | StartDevice `0x0`; IH, EOP, SDMA1, MMHUB, D2, P7, P2, P1b armados |
| 00:07–00:15 | menu aberto em todos os minutos; o `StartMenuExperienceHost` cria um device novo 11 vezes (00:07:12 … 00:15:26) |
| **00:15:26.006** | o menu cria o device do contexto `1B8A` (VMID 11) |
| ~00:15:28 | **TDR 1 do GFX**: a 1ª submissão desse device (Fence 1997, IB de 184 dw) não termina. Anel com 10 dw pendentes, RPTR logo depois de dois `INDIRECT_BUFFER`; ME executando o IB (`SET_CONTEXT_REG`), PFP fazendo parse, `GUI_ACTIVE=1 CP_BUSY=1 GfxContextBusy=1`, **zero ondas** (estado ou pacote, não shader), sem fault de VM latchado, SDMA0 ocioso |
| | Recuperação **completa e provada**: SDMA0 rearmado (NOP + fence sobre poison), `TDR_FENCE_RECONCILE PASS`, `TDR_GFX_RESTART_PROOF PASS` (NOPx4 + RELEASE_MEM), GFX volta a concluir trabalho. O DWM reinicia (dois `dwm.exe` novos) e todos os apps recriam devices |
| ~00:15:34 | **TDR 2** (Fence 2003, VMID 1 = DWM novo), recuperado |
| ~00:15:44 | **TDR 3** (Fence 2021, VMID 11, outro device novo; 62 dw pendentes), recuperado |
| **00:15:53** | **trava dura**: último registro do KMD e último tique do rastro (4.895 tiques, nenhum buraco > 0,2 s antes). No último tique: SDMA0 com tudo concluído, GFX com a Fence 2049 (última submetida) concluída 0,2 s antes. **GPU ociosa**, como em todas as travas |
| 00:17:43 | reboot pelo botão (eventos 41/6008 às 00:17:46) |

Totais até a trava: 20.880 IBs, 2.170 Presents, 4.917 cópias da primária, 134 `ActiveOpenAlloc RECUSADO` (superfície compartilhada), zero `GFXHUB_FAULT`/`IB_TRIPOD TIMEOUT`/`Blt Timeout`/`INSIDE_IB`.

## 2. Veredito frente ao critério (doc 628 §3)

**Desfecho B:** trava dura com a assinatura de sempre (GPU ociosa no último tique, sem BugCheck), só com o menu em uso. Nenhum outro app carregou a UMD por iniciativa própria: Pesquisa (`SearchApp`) e TextInputHost são parte do shell e já estavam na 237.

**Fora do mapa (E), descrito:** desta vez a trava veio **depois de três TDRs do GFX em ~16 s**, todos recuperados, o primeiro disparado pela **primeira submissão de um device recém-criado do menu Iniciar**. Mineração das corridas desde 01/10: a 231 teve o mesmo padrão (TDR do GFX recuperado aos ~315 s, trava dura aos ~325 s); a 201, a 208 e a 216 tiveram TDR do GFX **sem** trava em seguida; as outras travas não tiveram TDR antes. Então o TDR não é necessário para a trava (P0), e a correlação "TDR → trava em segundos" aparece em 2 de 4 TDRs (H).

## 3. O que isso muda (H, para decisão)

1. **Há um defeito reproduzível de GFX independente da trava:** device novo do menu (ou DWM recém-reiniciado) cuja primeira submissão trava o anel gráfico com zero ondas. Os TDRs de 201/208/216/231 são da mesma família (VMID 10/11). Isso dá para minerar offline: o dump do anel do TDR já está no log (`TDR_GFX_RING_TAIL`), e falta o conteúdo do IB (o `gfx-ib-dump/` não foi gerado porque a corrida não chegou ao fim).
2. **A trava dura pode ter uma via "GFX desarranjado → reset → tempestade de recriação de devices e paginação → trava".** Na 239 os 22 s entre o TDR 1 e a trava são exatamente a recriação de tudo pelo DWM novo.

**Próximo passo pelo critério (B):** mineração antes de nova corrida: (a) os pacotes antes do RPTR nos três TDRs da 239 e nos da 201/208/216/231 (o que há em comum: primeiro IB de contexto novo? `CONTEXT_CONTROL`? troca de VMID com `WAIT_REG_MEM` no ACK de invalidação `0x28B5`?); (b) o que o menu faz nas 10 primeiras criações que não fez na 11ª.

**NÃO VALIDADO EM HARDWARE:** a causa da trava; a causa do TDR do GFX; que um leve à outra.
