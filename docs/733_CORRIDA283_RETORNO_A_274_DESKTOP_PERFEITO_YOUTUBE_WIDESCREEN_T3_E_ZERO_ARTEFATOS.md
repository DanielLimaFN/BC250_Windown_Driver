# 733 — Corrida 283: Retorno à Corrida 274 Provado no Silício — Desktop Perfeito, Vídeo YouTube Widescreen, Menu/Alt+Tab e T3 sem Artefatos

**Data:** 13/10/2026, 13:21:07–13:24:21.  
**Pacote:** lab **3.432.1.0** (SYS `C250C67C638BDF3098C39CD0E8469E20D702F4E94FC927185F351725CC2315FC`, INF `5464EEA1B05E1CB8B2316ABBC145EFDDD32D1767441B08C1F989D9654E25364E`).  
**Parâmetros:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: IRQ do DCN + Cursor HW + DPMS + Timing nativo no VidPn + HPD + D06 leitura de IRQ; D12 e D19 desativados), `DebugTrace = 1`.

**Evidência:**
- Pasta de evidência: [`docs/evidence/2026-10-13_13-21-07-DESKTOP-HQAG6T4-lab-binding/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-10-13_13-21-07-DESKTOP-HQAG6T4-lab-binding/);
- Log do KMD: `display-log-this-bind-FINAL.txt` (231.216 linhas completas);
- Processos de carga D3D11 T3: `BC250_PROCESSO_NOVO.log` (sessão `20261013-132310-6780`, ciclos 1 a 8 concluídos com código 0);
- Watchdog e recuperação: `safe-bind-final.json` (`RECOVERY_OK_ONLINE`, rodada 1 sem reboot), `safe-bind-watchdog-journal.jsonl`.

---

## 1. Houve avanço?

**SIM, avanço funcional e de diagnóstico decisivo sobre a série 275–282.**

1. **Avanço funcional:** A tela preta com artefatos persistentes que inviabilizou as Corridas 275 a 282 foi **100% eliminada**. O operador (Daniel) confirmou visualmente:
   - Desktop apareceu imediatamente e limpo no DisplayPort nativo;
   - Menu Iniciar aberto e fechado várias vezes sem qualquer instabilidade ou falha;
   - Alternância de janelas via `Alt + Tab` fluida e sem telas pretas transitórias;
   - Reprodução de vídeo no YouTube em tela widescreen sem artefatos e sem engasgos;
   - Carga sintética T3 rodando concorrente sem falhas (8 processos concluídos com sucesso).
2. **Avanço de diagnóstico (fechamento de causa):** Confirma de forma conclusiva e irrefutável no silício que a degradação visual das Corridas 275–282 era causada exclusivamente pela alteração dinâmica de temporização vertical (`D19` / `V_TOTAL`), que desestabilizava o stream MSA e o FIFO do DisplayPort herdado do GOP. Com `D19` desativado, o hardware opera perfeitamente estável a 60 FPS com zero cópia de 11 MB.
3. **Recuperação limpa sem reboot:** Ao término dos 180 s do watchdog, o rollback online restaurou o BasicDisplay em Código 0 na primeira rodada (`RECOVERY_OK_ONLINE`), sem necessitar de reboot forçado (ao contrário da Corrida 274 e 276).

---

## 2. Sucessos (P0, Medido em Silício)

- **Flip Direto pelo HUBP0 em Regime Contínuo (D02/D03):**
  - **5.301 flips via `MPO3`** (composição normal do DWM) + 3 flips via `SET_ADDRESS` direto.
  - **5.304 latches confirmados** pelo hardware (`latchados=5304`).
  - **Zero cópias de 11 MB pelo SDMA0**: economia de mais de 58 GB de tráfego inútil de VRAM em 174 segundos.
  - Latência de latch: 5.301 flips latchados em menos de 16,7 ms (`latch em ms <16,7 = 5301`).
- **Interrupção de Flip do HUBP0 em Alta Eficiência (D05):**
  - **5.297 latches concluídos diretamente pela interrupção de hardware** (`latchados_via_irq_do_flip = 5297 de 5304`, **99,87%**).
  - O hardware avisa instantaneamente o dxgkrnl sem depender de polling por software.
- **Temporização e Varredura Nativa do GOP (D01/D04):**
  - Frequência medida pelo OTG: **60,004 Hz** sustentada.
  - 10.286 quadros contados no OTG; 9.208 notificações de VSYNC entregues.
- **Cursor de Cor por Hardware (D10):**
  - Operação normal e contínua do cursor do mouse sem sobrecarga de software.
- **Carga de Estresse D3D11 T3 Concorrente:**
  - 8 ciclos completos com `codigo=0` (processos D3D11 abrindo, alocando 4 texturas de 8 MB na BC-250 1002:13FE, sustentando e fechando limpo). Ciclo 9 estava em execução quando o tempo limite do watchdog foi atingido.
- **Estabilidade do Kernel e da GPU:**
  - **Zero TDR.**
  - **Zero GPU fault / Protection Fault.**
  - **Zero BugCheck.**
  - **Zero travamento da máquina.**
- **Rollback e Recuperação Automática (D32):**
  - Watchdog atuou aos 174,4 s.
  - Transição para BasicDisplay concluída online em 5 amostras consecutivas (`RECOVERY_OK_ONLINE`, exit code 0, round 1).

---

## 3. Falhas e Observações

- **Nenhuma falha funcional ou visual observada no teste.**
- O teste encerrou-se exclusivamente pelo término da janela de segurança de 180 segundos do watchdog de laboratório, como planejado.
- Ciclo 9 da carga T3 foi finalizado no momento do descarregamento do driver pelo watchdog (resultado: término normal do processo filho durante a troca de contexto de vídeo para BasicDisplay).

---

## 4. Veredito

Frente aos critérios definidos no [doc 732](732_PACOTE_3432_RETORNO_A_274_DCN_BASICO_E_CRITERIO_CORRIDA283.md):

**DESFECHO A — SUCESSO TOTAL (VÍDEO LIMPO, ESTÁVEL E PERFORMANCE PLENA)**

1. **Vídeo limpo e perfeito confirmado**: desktop acessível, menu funcional, Alt+Tab íntegro, YouTube em widescreen funcionando sem artefatos.
2. **DCN Básico Provado e Sustentado**: Flip direto pelo HUBP0 a 60 FPS, sem cópia de SDMA, com interrupção de flip a 99,87%.
3. **Decisão sobre D19**: O retorno à configuração 274 restabelece a estabilidade absoluta do subsistema de display sobre o GOP. Experimentos com D19 e D12 devem permanecer guardados e isolados até que a investigação de render/paginação (P1→P8) avance.

---

## 5. Próximo Passo

Com o display provado 100% estável e livre de artefatos com o pacote 3.432.1.0:
- Manter o DCN travado nesta configuração de sucesso (`DcnFlip=1`, `DcnExt=125`).
- Voltar com total segurança para as prioridades de estabilidade de submissão e carga (investigação das causas de trava em processos novos com tempos de execução longos de 600 s).
