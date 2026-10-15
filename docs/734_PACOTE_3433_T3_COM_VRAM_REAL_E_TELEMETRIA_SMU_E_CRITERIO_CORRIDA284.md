# 734 — Pacote 3.433.1.0: Carga T3 com VRAM Real (96 MB), Render Ativo 3D e Telemetria de Clock SMU.1 (GFXCLK) — Critério da Corrida 284

**Data:** 13/10/2026.  
**Pacote:** lab **3.433.1.0** (SYS `C52EF3A06D89330F86CD1611793A3359A9C32EBB58FDDA66361640583F2A53C5`, INF `8218661AA321597F6393C2E49915FF8ABADB5E4D926F6D128867663329D57B7F`, CAT `BAAFAE92A6780FB5CAD5E58E71F2B379D8A969049D65435E488BCEB5917DAB8E`).  
**Binários associados:** UMD `B73F9BDA`, RADV `B4AE2E14`, executável de carga `bc250-processo-novo.exe` (`B14BE83885840D1C16D2812062E6F49F53A48D38874F1985E1F40A9AD5DCB5DA`).  
**Parâmetros de configuração:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: DCN básico provado nas Corridas 274 e 283; D12 e D19 DESATIVADOS), `DebugTrace = 1`.

---

## 1. Contexto e Motivação

Após a validação irrefutável no silício na **Corrida 283** ([doc 733](733_CORRIDA283_RETORNO_A_274_DESKTOP_PERFEITO_YOUTUBE_WIDESCREEN_T3_E_ZERO_ARTEFATOS.md)), onde o subsistema de exibição provou estabilidade plena (desktop limpo, menu funcional, Alt+Tab íntegro, YouTube widescreen a 60 FPS com zero cópia de SDMA e 99,87% de interrupções de hardware), retornamos à investigação central do projeto: **estabilidade sob carga de renderização, paginação de memória e telemetria profunda da GPU**.

A pedido do Daniel, a Corrida 284 introduz dois aprimoramentos cruciais:
1. **Carga sintética T3 real e pesada (`bc250-processo-novo.exe`):** Em vez de alocações residuais de 32 MB que não pressionavam o orçamento de vídeo nem o pipeline gráfico 3D, cada processo agora cria **6 texturas de 16 MB = 96 MB** de alocação de vídeo dedicada (ocupando ~50% de todo o pool de VRAM utilizável no setup CMOS de 256 MB) e executa **30 quadros de render/clear com `ID3D11DeviceContext_Flush` ativo** durante sua vida útil.
2. **Telemetria de Clock do SMU (`SMU.1`):** Acesso em tempo real à frequência operacional do núcleo gráfico (GFXCLK em MHz) consultando diretamente a caixa de correio do SMU v11.8 (Msg 0x37 / `PPSMC_MSG_GetGfxFrequency`).

---

## 2. O que mudou no código

### 2.1 Carga D3D11 T3 (`tools/bc250-processo-novo.c` e `tools/RODAR_PROCESSO_NOVO.cmd`)
- Argumentos padrão atualizados para `--texturas 6 --mb 16 --clear 1 --segurar-ms 500`.
- Em cada ciclo, o processo filho:
  - Cria 6 texturas 2D com formato RGBA8 e flag `D3D11_BIND_RENDER_TARGET`;
  - Cria os respectivos `ID3D11RenderTargetView`;
  - Executa um loop de 30 frames variando cores (vermelho, verde, azul), disparando `ClearRenderTargetView` e submetendo os comandos à GPU com `ID3D11DeviceContext_Flush`;
  - Libera todas as views e texturas antes de sair com `codigo 0`.
- Recompilado com `tools/build-processo-novo.bat` (novo hash `B14BE83885840D1C16D2812062E6F49F53A48D38874F1985E1F40A9AD5DCB5DA`).

### 2.2 Miniport KMD (`driver/wdk/bc250_active_miniport.c` e `bc250_mcdm_callbacks.c`)
- Implementada a função de sonda `Bc250ActiveSmuClockProbe`:
  - Envia a mensagem `0x37` (`BC250_PPSMC_MSG_GetGfxFrequency`) aos registradores de mailbox do SMN (`C2PMSG_66`, `C2PMSG_82`, `C2PMSG_67`);
  - Serializada sob `g_Bc250SmnIndexLock` em `PASSIVE_LEVEL`, com timeout limitado a 2 ms;
  - Conforme comprovado no silício no doc 655 e upstream no driver Linux amdgpu (`cyan_skillfish_ppt.c` / `smu_v11_8_ppsmc.h`).
- Integrada ao thread de telemetria `Bc250PagingTraceThread`: dispara a cada 2,0 segundos (20 tiques de 100 ms) e emite a linha de log `[SMU1] GFXCLK=%u MHz`.
- Totalmente aprovada pela auditoria estrita de segurança (`audit-bc250mcdm-safety.py`).

### 2.3 Monitor da Corrida (`tools/monitor-corrida.py`)
- O leitor do log do KMD agora identifica e parseia as linhas `[SMU1] GFXCLK=`.
- O dashboard exibe:
  - `clock: <MHz> MHz (SMU v11.8)` na linha de telemetria da GPU.
- O arquivo de log contínuo `C:\BC250_MONITOR.csv` ganha uma nova coluna (`gpu_mhz`) registrando a frequência da GPU ao longo de toda a corrida.

### 2.4 DCN Intocado (Baseline Provado 274/283)
- `DcnFlip = 1`: Flip direto pelo HUBP0 sem cópia de SDMA.
- `DcnExt = 125`: (0x7D: IRQ do DCN + Cursor HW + DPMS + Timing nativo no VidPn + HPD + D06 leitura de IRQ).
- D12 e D19 desativados (`DcnExtAuxHwModeArmed = false`, `DcnExtVTotalRefreshTestArmed = false`).

---

## 3. Pré-condições e Checklist

| Item | Descrição | Status Esperado |
| :--- | :--- | :---: |
| **P01** | CMOS configurada em 256 MB aplicados | ✅ Obrigatório |
| **P02** | Boot frio (desligamento elétrico prévio) | ✅ Obrigatório |
| **P03** | `audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` aprovados | ✅ Concluído (PASS) |
| **P04** | Driver de laboratório 3.433.1.0 compilado e empacotado | ✅ Concluído |
| **P05** | `tools\EXECUTAR_CORRIDA284_T3_VRAM_SMU_TELEMETRY_180S.cmd` gerado | ✅ Concluído |
| **P06** | `tools\prepare-corrida284-t3-vram-smu-telemetry.ps1` gerado | ✅ Concluído |

---

## 4. Critério de Aceite e Desfechos

- **DESFECHO A (SUCESSO PLENO):**
  1. Desktop acende perfeito e limpo no DP nativo (baseline 283 mantido);
  2. Janela do monitor `BC-250 MONITOR` mostra a frequência real do clock da GPU (ex: `1000 MHz (SMU v11.8)`);
  3. Carga T3 iniciada: coluna `VRAM dedicada` no monitor sobe refletindo os 96 MB alocados por processo;
  4. Processos executam as 30 operações de clear/flush e encerram com código 0;
  5. Zero TDR, zero fault, zero BugCheck durante os 180 s do watchdog, com rollback limpo ao término.

- **DESFECHO B (TRAVA SOB PRESSÃO DE VRAM):**
  1. O desktop sobe e a telemetria SMU funciona, mas a máquina trava durante os ciclos do T3 ao tentar alocar/paginar os 96 MB.
  2. Diagnóstico: Valida a hipótese de limite de paginação/eviction sob estresse real de VRAM local.

- **DESFECHO C (INCONCLUSIVO / RECUSA):**
  1. O preflight recusa o bind ou o SMU não responde em tempo hábil.

---

## 5. Como Executar

1. Desligue completamente a máquina da tomada / botão de energia (boot frio);
2. Ligue o computador e aguarde entrar no Windows;
3. Execute como **Administrador**:
   ```cmd
   tools\EXECUTAR_CORRIDA284_T3_VRAM_SMU_TELEMETRY_180S.cmd
   ```
4. Assim que o desktop acender e o monitor abrir, abra outra janela de terminal como **Administrador** e execute:
   ```cmd
   tools\RODAR_PROCESSO_NOVO.cmd
   ```
