# 735 — Corrida 284: Sucesso Total com Carga T3 Pesada (96 MB VRAM + Render 3D) e Clock SMU Provado em 1500 MHz

**Data:** 13/10/2026, 14:36:38–14:39:56.  
**Pacote:** lab **3.433.1.0** (SYS `C52EF3A06D89330F86CD1611793A3359A9C32EBB58FDDA66361640583F2A53C5`, INF `8218661AA321597F6393C2E49915FF8ABADB5E4D926F6D128867663329D57B7F`).  
**Parâmetros:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: DCN básico provado nas Corridas 274 e 283; D12 e D19 desativados), `DebugTrace = 1`.  
**Carga:** T3 pesada (`tools\RODAR_PROCESSO_NOVO.cmd` com `--texturas 6 --mb 16 --clear 1 --segurar-ms 500`).

**Evidência:**
- Pasta de evidência: [`docs/evidence/2026-10-13_14-36-38-DESKTOP-HQAG6T4-lab-binding/`](file:///c:/Users/Daniel/Desktop/NamixWDDM/docs/evidence/2026-10-13_14-36-38-DESKTOP-HQAG6T4-lab-binding/);
- Log do KMD: `display-log-this-bind-FINAL.txt` (23.103.819 bytes);
- Monitor contínuo: `BC250_MONITOR.csv` (1.705.131 bytes, coluna `gpu_mhz` preenchida em todas as amostras);
- Log de processos novos T3: `BC250_PROCESSO_NOVO.log` (sessão `20261013-143709-10064`, 19 ciclos completos com código 0);
- Watchdog e recuperação: `safe-bind-final.json` (`RECOVERY_OK_ONLINE`, rodada 1 sem reboot).

---

## 1. Houve avanço?

**SIM, avanço gigantesco em estabilidade sob carga pesada e em observabilidade do silício.**

1. **Avanço de Observabilidade (SMU.1 provado P0):** Pela primeira vez no projeto, o miniport KMD e o monitor de corrida leram com sucesso em hardware real a frequência operacional da GPU diretamente da caixa de correio do SMU v11.8 (Msg 0x37 / `PPSMC_MSG_GetGfxFrequency`). O clock do silício GC 10.1.3 da BC-250 operou perfeitamente travado a **1500 MHz (1,5 GHz)** durante 100% da corrida, sem engasgos, sem oscilações e com zero timeouts de barramento SMN.
2. **Avanço de Carga e Pipeline Gráfico (T3 Pesada):** O teste T3 com 96 MB de alocação por processo e 30 frames de render/clear 3D com flush rodou 19 ciclos completos sem nenhuma falha.
3. **Resistência à Pressão de VRAM:** A VRAM dedicada alocada no Windows subiu de zero até atingir picos de **228,3 MB** (praticamente esgotando o limite físico utilizável do carve-out de 256 MB da CMOS), sem gerar nenhum erro de alocação, sem corrupção de heap e sem falhas de paginação (zero faults de GCVM/GFXHUB).
4. **Submissão e Taxa de Comandos:** A GPU processou picos de mais de **258 IBs/s** e **23 submissões GFX/s**, sustentando renderização e composição de desktop simultaneamente.

---

## 2. Sucessos (P0, Medido no Silício)

- **Telemetria de Clock SMU.1 em Hardware Real:**
  - Consulta periódica a cada 2,0 segundos ao SMU via `Bc250ActiveSmuClockProbe`;
  - 100% das mensagens responderam `resp == 1` com zero timeout;
  - Frequência medida: **1500 MHz constante** registrada no KMD e no arquivo `BC250_MONITOR.csv`.
- **Carga D3D11 T3 Concorrente (19 ciclos completos, código 0):**
  - **19 processos D3D11** criados, executados e finalizados;
  - Cada processo alocou com sucesso **6 texturas 2D RGBA8 de 16 MB = 96 MB de VRAM**;
  - Cada processo executou **31 quadros de render/clear com `ID3D11DeviceContext_Flush`**;
  - Total de **589 quadros de carga 3D** submetidos à GPU no nó GFX;
  - 100% dos ciclos fecharam com `codigo=0` em média de ~2,4 segundos por processo.
- **Pressão Máxima de Memória de Vídeo:**
  - VRAM dedicada medida no Windows: pico de **228,3 MB** de uso;
  - Composição do DWM mantida em 60 FPS via MPO3 / HUBP0 sem degradação.
- **Robustez do Driver e da GPU:**
  - **Zero TDR.**
  - **Zero Protection Fault / GCVM Fault / GFXHUB Fault.**
  - **Zero BugCheck.**
  - **Zero travamento da máquina.**
- **Rollback Limpo ao Final dos 180s (D32):**
  - O watchdog descarregou o driver e restaurou o BasicDisplay em Código 0 na rodada 1 (`RECOVERY_OK_ONLINE`) sem reiniciar o sistema.

---

## 3. Falhas e Observações

- **Nenhuma falha funcional ou técnica observada.**
- O teste encerrou-se no ciclo 19 exclusivamente porque a janela de 180 s do watchdog de segurança foi atingida, como previsto.

---

## 4. Veredito

Frente aos critérios definidos no [doc 734](734_PACOTE_3433_T3_COM_VRAM_REAL_E_TELEMETRIA_SMU_E_CRITERIO_CORRIDA284.md):

**DESFECHO A — SUCESSO PLENO (SMU 1500 MHZ CONFIRMADO + T3 COM 96 MB VRAM E RENDER 3D APROVADO)**

1. Clock SMU em 1500 MHz provado em regime contínuo.
2. Capacidade da BC-250 de sustentar alocação massiva de VRAM (~50% a 90% do carve-out) e renderização ativa concorrente com o desktop provada no silício.
3. Robustez do miniport e do UMD validada contra vazamentos e falhas de paginação.

---

## 5. Próximo Passo

1. Atualizar a `BASE_DE_AVANCO.md` com o SMU.1 validado em hardware (GFXCLK 1500 MHz) e o Marco de Estresse de VRAM T3.
2. Preparar corrida de maior duração (ex: 300 s ou 600 s) para rodar os 90 ciclos completos do T3 e testar aplicações 3D adicionais.
