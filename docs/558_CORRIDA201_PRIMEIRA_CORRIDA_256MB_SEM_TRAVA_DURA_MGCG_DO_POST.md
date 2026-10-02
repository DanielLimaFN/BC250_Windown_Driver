# 558 — Corrida 201: primeira corrida com 256 MB sem trava dura (MGCG do SDMA0 como o POST); um TDR gráfico recuperado

**Corrida:** 02/10/2026, bind às 08:04:48, boot às 08:03:05. O driver ficou vivo **591,8 s**, e o rollback programado do watchdog veio às 08:15 (`RECOVERY_OK_ONLINE`, BasicDisplay em Código 0 em 5 amostras, **sem reboot**).
**Candidato:** lab **3.348.1.0** (CG.1, [doc 557](557_CORRIDA200_SDMA0_OCIOSO_NA_PARADA_E_CG1_MGCG_DO_POST_CRITERIO_CORRIDA201.md)): o da 200 mais os overrides do `SDMA0_CLK_CTRL` devolvidos ao POST depois do anel provado. CMOS em 256 MB.
**Evidência:** `docs/evidence/2026-10-02_08-04-48-DESKTOP-HQAG6T4-lab-binding/`
- `display-log-this-bind-FINAL.txt` (31,1 MB);
- `paging-trace-this-bind.bin` e `rastro-corrida-resumo.txt`;
- 12 logs `-FINAL` da UMD e 12 de AppContainer (incluindo 7 da Pesquisa);
- `reset-reason-pre-bind.txt` e `SHA256-corrida201.txt`.

---

## 1. Relato obrigatório

**1. Houve avanço?** **Sim, funcional.**
- **Primeira corrida com 256 MB a chegar ao fim sem trava dura.** A 199 travou em ~140 s e a 200 em ~59 s.
- Nesta, o uso foi mais pesado que na 200: Pesquisa aberta várias vezes, Gerenciador de Tarefas, Configurações, Edge e Claude. A falta de memória (`res=-2`) também aconteceu.

**2. Sucessos (P0):**
- **A variável foi aplicada e se manteve:**
  - `[SDMA_CG] INIT: … entrada(POST)=0x000C0100 antes=0xFF0C0100 … lido=0x000C0100 -> OVERRIDES DO POST RESTAURADOS`, e o mesmo no `TDR_RESTART`;
  - as **108** amostras BCG1 da corrida mostram `sdma0_clk_ctrl=0x000C0100`, igual ao SDMA1.
- **Volume:** 2.883 Presents, 5 `BYTE_PROOF PASS`, 7.618 `IB EXECUTADO`, 4.799 FILLs e 2.823 TRANSFERs de paginação.
- **Sem erro de traçado:** zero `IB_TRIPOD TIMEOUT`, `GFXHUB_FAULT`, `DEVICE_REMOVED`, `StopDevice` e `Lock2`.
- **Rastro contínuo de 595 s** (5.374 ticks), com Tctl de 75,8 a 87,8 °C (último 80,3 °C).
- **Recuperação de TDR funcionou no meio da carga:** `TDR_QUIESCE`, depois `TDR_RESTART PROVADO`, e a corrida seguiu por mais ~480 s.

**3. Falhas:**
- **Um TDR**, ~111 s depois do início, quando o menu "demorou, deu tela preta e voltou" (relato do Daniel).
  - A submissão gráfica `dxg_fence=430`/`ring_fence=431`, contexto `0x61D`, **VMID 10**, 1.064 DW, foi aceita e **nunca retirou**: 32 `PREEMPT_WAIT … no ainda nao drenou`.
  - O SDMA0 estava ocioso e drenado no TDR (`RPTR=WPTR=125444`, `STATUS1=0x3FF`). É um **hang gráfico real**, recuperado.
  - O `dwm.exe` reiniciou: há dois PIDs, 1288 e 8176.
- **Artefatos visuais** no Gerenciador de Tarefas (relato do Daniel, P2).
- **Falta de memória:** `res=-2` no `SystemSettings` (2) e no segundo DWM (1).
- 135 `Journal NO_SPACE`, contra 6 na 200 e 10 na 199.

**4. Veredito frente ao doc 557 §4: A.** Os três sinais de A estão presentes:
1. restauração confirmada (linha `[SDMA_CG]` e BCG1 = SDMA1);
2. sem trava dura até o rollback programado (591,8 s de log do KMD; janela de 600 s);
3. com a falta de memória acontecendo.

**Com um componente E:** o TDR do Node 1 não estava no mapa. Não é o desfecho C, porque o SDMA0 estava drenado e não houve `IB_TRIPOD TIMEOUT`.

**n=1.** O próprio critério manda repetir antes de fechar a causa.

**5. Recuperação e pendências:**
- **Estado ao final:** BasicDisplay em Código 0, rollback online limpo, sem reboot.
- **Boot frio:** obrigatório antes da próxima.
- **NÃO VALIDADO EM HARDWARE:**
  - a reprodutibilidade (n=1);
  - a causa do hang gráfico em VMID 10;
  - os artefatos de UI;
  - a memória (M1b).
- **Pendência de protocolo:** confirmar se o boot das 08:03 foi frio. A leitura pré-bind trouxe o bit 19, ou seja, houve um reset (botão ou reinício) depois da coleta de 22:17.

## 2. Leitura

- **Atribuição (H forte, não P0):** a única variável mudou e a trava dura sumiu, numa carga igual ou maior. Isso sustenta o MGCG forçado desligado do SDMA0 como causa ou gatilho da trava dura com 256 MB. Bate com a referência: o Linux, que nunca toca nesse registrador, não trava. **Uma corrida não basta:** as travas desta semana vieram entre 1 e 3 minutos, então 10 minutos sem trava pesa, mas não fecha.
- **O mecanismo continua desconhecido (H).** Clock forçado ligado no SDMA0 deveria ser o estado mais seguro. Que ele trave a plataforma inteira com a GPU ociosa pede uma interação com o gerenciamento autônomo de energia da SMU (todas as features ligadas), que não medimos.
- **O TDR é uma classe à parte e é recuperável.** O hang gráfico em VMID 10 tem nome (fence, contexto, VA `0x2407FC000`, IB de 1.064 DW) e é o próximo problema de estabilidade do desktop. Ele não derruba a máquina: o par de TDR (item 25) funcionou de ponta a ponta com display vivo, a perna que faltava desde 07/09. **O item 25 merece revisão para fechar com esta prova.**

## 3. Próximo passo

1. **Repetir a mesma corrida** (o mesmo 3.348.1.0, `tools\EXECUTAR_CORRIDA201_SDMA0_MGCG_600S.cmd`), com boot frio de verdade e o mesmo uso.
   - Sem trava dura de novo: a CG.1 vira correção (n=2) e entra no produto.
   - Com trava dura: a atribuição cai.
2. **Offline, em paralelo:** o hang gráfico em VMID 10. Casar o IB `0x2407FC000` (1.064 DW) com o log da UMD do processo dono do contexto `0x61D`, para saber qual app e qual draw.

**NÃO VALIDADO EM HARDWARE** além do que está marcado como P0 acima.
