# 736 — Pacote 3.434.1.0: Carga T3 com Janela 3D Visível, Telemetria SMU.2 (GFXCLK + VDDGFX + FW VER) e Janela de 300 s — Critério da Corrida 285

**Data:** 13/10/2026.  
**Pacote:** lab **3.434.1.0** (SYS `957A4C3AC91D29BF71B6CFE2F05AF1C602E4F597FECB88864055C9626F037E3D`, INF `A80632A18DBEEBFD3A9315EA46FA93157D595AFAC5D48EFF24A90351801F5494`, CAT `FBA1B2FB1DDBDA261F508A90BEE9BA4AD0DE33723C6B1B99212B011551B45D75`).  
**Binários associados:** UMD `B73F9BDA`, RADV `B4AE2E14`, executável de carga `bc250-processo-novo.exe` (`05F3A84C283E6F63A836925EBA47D650C0E45F9B93808B2529AA7502B0AC3629`).  
**Parâmetros de configuração:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: DCN básico provado nas Corridas 274, 283 e 284; D12 e D19 DESATIVADOS), `DebugTrace = 1`.  
**Janela de Watchdog:** **300 s** (5 minutos contínuos).

---

## 1. Contexto e Motivação

Na **Corrida 284** ([doc 735](735_CORRIDA284_SUCESSO_TOTAL_T3_COM_VRAM_PESADA_RENDER_3D_E_CLOCK_SMU_1500MHZ.md)), alcançamos um marco excepcional:
- **Telemetria SMU.1 validada em silício:** O KMD comunicou com sucesso absoluto via Mailbox do SMN com o SMU v11.8 (Msg `0x37`), aferindo a frequência operacional real da GPU cravada em **1500 MHz** (1,5 GHz) durante toda a execução.
- **Carga T3 pesada de VRAM:** 19 processos consecutivos alocaram 96 MB de VRAM dedicada cada, levando o consumo de VRAM no Windows a **228,3 MB** (~90% de todo o carveout CMOS de 256 MB), executando 589 frames de render/clear sem qualquer falha de GCVM, sem OOM e sem falhas de paginação.
- **DCN 274 estável:** Zero TDR, zero BugCheck, rollback online limpo para BasicDisplay em Código 0.

Porém, como notado pelo Daniel, o teste T3 executava seu pipeline de renderização e clears de forma oculta (offscreen/headless), sem exibir visualmente uma janela 3D acelerada no desktop. Além disso, o Daniel solicitou:
1. **Render 3D visível na tela:** O processo deve criar uma janela real no Windows e apresentar os frames na tela em tempo real (`IDXGISwapChain_Present`).
2. **Telemetria SMU.2 ampliada:** Medição da tensão do núcleo gráfico (`VDDGFX` em mV via Msg `0x38`) e leitura da versão do firmware do SMU (Msg `0x02`).
3. **Janela de teste estendida para 300 s (5 minutos):** Para uma sessão de estresse mais prolongada e aprofundada.
4. **Preservação total do DCN:** Manter rigorosamente o DCN básico da 274/283/284 (`DcnFlip=1`, `DcnExt=125`, D12/D19 desligados).

---

## 2. O que mudou no código

### 2.1 Carga D3D11 T3 Visual (`tools/bc250-processo-novo.c` e `.exe`)
- Adicionada criação de janela Win32 nativa (`480x320`, classe `BC250_StressWindow`).
- Adicionada criação de DXGI SwapChain associada ao adaptador BC-250 (`IDXGIFactory1_CreateSwapChain`).
- A cada frame do loop (30 frames por processo):
  - Limpa o backbuffer do swapchain (`ClearRenderTargetView`);
  - Limpa as 6 texturas offscreen de 16 MB (`ClearRenderTargetView`, mantendo a pressão de **96 MB de VRAM**);
  - Descarrega os comandos no contexto D3D11 (`ID3D11DeviceContext_Flush`);
  - Apresenta na tela através do DXGI (`IDXGISwapChain_Present(swapChain, 1, 0)`);
  - Processa as mensagens do Windows (`PeekMessage` / `DispatchMessage`);
- O processo agora renderiza uma janela colorida (alternando tonalidades) visível diretamente pelo operador.
- Recompilado com `tools/build-processo-novo.bat` (novo hash auditado: `05F3A84C283E6F63A836925EBA47D650C0E45F9B93808B2529AA7502B0AC3629`).

### 2.2 Miniport KMD — Telemetria SMU.2 (`driver/wdk/bc250_active_miniport.c` e `bc250_mcdm_callbacks.c`)
- Criado helper genérico e robusto `Bc250ActiveSmuSendMsg(pAdapter, Msg, Param, &Response)` com serialização estrita em `g_Bc250SmnIndexLock` em `PASSIVE_LEVEL`.
- A função de sonda `Bc250ActiveSmuClockProbe` agora consulta:
  1. Msg `0x37` (`BC250_PPSMC_MSG_GetGfxFrequency`): Frequência do clock GFX (MHz);
  2. Msg `0x38` (`BC250_PPSMC_MSG_GetGfxVid`): Tensão `VDDGFX` em mV (conforme especificação Cyan Skillfish / SMU v11.8);
  3. Msg `0x02` (`BC250_PPSMC_MSG_GetSmuVersion`): Versão do firmware do SMU.
- O thread de paginação e rastreamento emite periodicamente no log:
  ```text
  [SMU1] GFXCLK=%u MHz VDDGFX=%u mV SMU_VER=0x%08X
  ```
- 100% aprovado pela auditoria de segurança estrita (`audit-bc250mcdm-safety.py`).

### 2.3 Monitor da Corrida (`tools/monitor-corrida.py`)
- O leitor do log do KMD parseia `GFXCLK=`, `VDDGFX=` e `SMU_VER=`.
- Exibição em tempo real na tela do console:
  ```text
  clock: 1500 MHz | 850 mV | FW 0x00371400
  ```
- Cabeçalho do arquivo `C:\BC250_MONITOR.csv` expandido com a nova coluna `gpu_vid_mv` para análise de estabilidade energética ao longo do estresse.

### 2.4 DCN Intocado (Baseline Provado 274/283/284)
- `DcnFlip = 1`: Flip nativo no HUBP0 sem cópia SDMA.
- `DcnExt = 125` (0x7D): IRQ DCN + Cursor HW + DPMS + Timing nativo no VidPn.
- D12 e D19 continuam rigorosamente desativados.

---

## 3. Pré-condições e Checklist

| Item | Descrição | Status Esperado |
| :--- | :--- | :---: |
| **P01** | CMOS configurada em 256 MB aplicados | ✅ Obrigatório |
| **P02** | Boot frio (desligamento elétrico prévio) | ✅ Obrigatório |
| **P03** | `audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` aprovados | ✅ Concluído (PASS) |
| **P04** | Driver de laboratório 3.434.1.0 compilado e empacotado | ✅ Concluído |
| **P05** | `tools\EXECUTAR_CORRIDA285_T3_VISUAL_SMU2_300S.cmd` gerado (300 s, CRLF) | ✅ Concluído |
| **P06** | `tools\prepare-corrida285-t3-visual-smu2.ps1` gerado | ✅ Concluído |

---

## 4. Critério de Aceite e Desfechos

- **DESFECHO A (SUCESSO PLENO):**
  1. Desktop acende no DisplayPort nativo com imagem limpa;
  2. Janela `BC-250 MONITOR` exibe GFXCLK (~1500 MHz), tensão VDDGFX em mV e versão do SMU;
  3. Carga T3 iniciada: Janela 3D de 480x320 surge na tela do desktop, alternando cores e apresentando quadros acelerados via GPU;
  4. Consumo de VRAM dedicada no monitor reflete a pressão das texturas (~200+ MB);
  5. Processos completam os 30 frames visíveis e fecham com código 0;
  6. Zero TDR, zero BugCheck durante os **300 segundos** do watchdog, com rollback online perfeito.

- **DESFECHO B (COMPORTAMENTO ANÔMALO DO SWAPCHAIN / T3):**
  1. A janela 3D abre mas ocorre erro de apresentação ou dispositivo removido no DXGI.

- **DESFECHO C (TRAVA DE HARDWARE SOB ESTRESSE 300S):**
  1. A máquina congela durante os 300 s de estresse contínuo com janelas 3D ativas.

- **DESFECHO D (INCONCLUSIVO / RECUSA):**
  1. Preflight recusa por falta de boot frio ou falha de pré-requisito.

---

## 5. Como Executar

1. **Desligue completamente o computador da tomada / fonte** (boot frio obrigatório; não reinicie pelo Windows);
2. Ligue a máquina e entre no Windows 10;
3. Abra um terminal como **Administrador** e execute:
   ```cmd
   tools\EXECUTAR_CORRIDA285_T3_VISUAL_SMU2_300S.cmd
   ```
4. Quando o desktop reiniciar sob o driver BC-250 e o monitor abrir exibindo os clocks e tensões, abra outro terminal como **Administrador** e execute:
   ```cmd
   tools\RODAR_PROCESSO_NOVO.cmd
   ```
5. Observe as janelas 3D renderizando ativamente na tela e o monitor registrando os parâmetros da GPU durante os 300 segundos.
