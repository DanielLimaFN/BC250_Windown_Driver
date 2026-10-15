# 737 — Pacote 3.435.1.0: Carga T3 Animada (60 FPS, 3,0s de Tela, 64 MB VRAM), Telemetria SMU.3 (IF_VER + FEAT Mask) e DCN D23 (Gamma CM0_BLNDGAM) — Critério da Corrida 286

**Data:** 13/10/2026.  
**Pacote:** lab **3.435.1.0** (SYS `DD2D7F10B88DEEB9CC2FC68C8475CA138CB53B2B5145B3B65084768609709160`, INF `36157AFB7246B8FFE66985B470722E6174551554DA0471939344668219CBD261`, CAT `90B3D5ECC2AA4299F536939BD8C40A8B6411A8CC81C57D463D1432CF2BD98138`).  
**Binários associados:** UMD `B73F9BDACEC371842AFF283723485C381066DD470D041AF6E979676FA9ED6F66`, RADV `B4AE2E14`, executável de carga `bc250-processo-novo.exe` (`89DEB11368C25549AAAD60A34EB23CDC57AE54E878C32DC0191DCF8A38255CEB`).  
**Parâmetros de configuração:** `DcnFlip = 1`, `DcnExt = 125` (0x7D: baseline provado das Corridas 274, 283, 284 e 285; D12 e D19 DESATIVADOS), `DebugTrace = 1`.  
**Janela de Watchdog:** **300 s** (5 minutos contínuos).

---

## 1. Contexto e Motivação

Na **Corrida 285** ([doc 736](736_PACOTE_3434_T3_VISUAL_SMU2_E_CRITERIO_CORRIDA285.md)), o sistema operou com estabilidade exemplar por **5 minutos ininterruptos (300 s)** sob carga:
- **Zero congelamento, zero TDR, zero BugCheck:** O watchdog de 300 s concluiu perfeitamente e executou rollback online limpo para BasicDisplay em Código 0 (`RECOVERY_OK_ONLINE`).
- **Telemetria SMU.2 Provada em Silício:** Clock da GPU cravado em 1500 MHz, tensão VDDGFX lida via Mailbox do SMN (100 = 1,00V), e firmware do SMU reportado como `0x00580600` (v58.6.0).
- **Diagnóstico da Janela 3D:** O operador relatou que a janela 3D do teste abria e fechava muito rapidamente ("tentava abrir mas fechava rapidão"). A análise revelou que o parâmetro `--segurar-ms 500` durava apenas 31 frames (~496 ms a 60 FPS), fazendo com que a janela aparecesse apenas como um piscar rápido entre ciclos de criação/destruição de dispositivo, além de sobrecarregar o garbage collector do VidMm com 96 MB de texturas alocadas e liberadas a cada meio segundo.

A pedido do Daniel, a **Corrida 286** implementa três frentes conjuntas:
1. **Correção e Animação do Teste 3D (T3 Animado):**
   - Janela retida por **3,0 segundos completos (~180 frames a 60 FPS)** em cada ciclo;
   - Animação suave e fluida de transição de cores via onda senoidal RGB (`math.h`), permitindo ao operador conferir a renderização ativa na tela;
   - Bombeamento explícito de mensagens do Windows (`PeekMessageA` + `TranslateMessage` + `DispatchMessageA`) para evitar estado "Não Respondendo";
   - Atualização do título da janela a cada 30 quadros com contagem de frames e ciclo atual;
   - Ajuste equilibrado da pressão de VRAM: 4 texturas de 16 MB = **64 MB de VRAM local** alocada por processo, garantindo simultaneamente estresse real de memória e fluidez máxima de swapchain.
2. **Telemetria SMU.3 (Opção 1):**
   - Leitura da versão da interface do driver do SMU (`DriverIfVersion`, Msg `0x03`);
   - Leitura da máscara de features ativas do SMU (`EnabledFeaturesMask`, Msg `0x3D`);
   - Exibição de `IF_VER` e `FEAT` no monitor e gravação de rastro.
3. **DCN D23 Gamma / Color Management (Opção 2):**
   - Leitura não destrutiva do registrador `CM0_BLNDGAM_CONTROL` (offset `0x1088C`) do DPP0/CM0 para aferir o modo do blend gamma herdado do UEFI GOP (Bypass, sRGB, LUT RAM A, LUT RAM B);
   - Implementação e logging das DDIs do WDDM `DxgkDdiSetPalette` e `DxgkDdiSetTargetGamma`, eliminando alertas de dívida técnica de DDI.
4. **Preservação Integral do DCN Básico:**
   - Mantida configuração idêntica às Corridas 274/283/284/285: `DcnFlip=1`, `DcnExt=125`, D12 e D19 desligados.

---

## 2. O que mudou no código

### 2.1 Carga D3D11 T3 Animada (`tools/bc250-processo-novo.c` e `.exe`)
- `LerArgs()` atualizado com novos padrões:
  - `ciclos = 90`;
  - `intervaloMs = 1000` (1 segundo entre processos);
  - `texturas = 4` e `mb = 16` (64 MB de VRAM);
  - `segurarMs = 3000` (3 segundos de apresentação contínua, ~180 frames a 60 FPS).
- Loop de renderização com animação trigonométrica:
  ```c
  float r = (float)(0.5 + 0.5 * sin(angulo));
  float g = (float)(0.5 + 0.5 * sin(angulo + 2.0943951)); // + 120 graus
  float b = (float)(0.5 + 0.5 * sin(angulo + 4.1887902)); // + 240 graus
  ```
- Bombeamento de fila de mensagens a cada quadro:
  ```c
  while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
  }
  ```
- Recompilado com `tools/build-processo-novo.bat` (novo SHA-256: `40EA80C79383A32BDEB7F66082216032D70398AEEE2F82053821EC3B07727EDC`).
- `tools/RODAR_PROCESSO_NOVO.cmd` atualizado com parâmetros explícitos e normalizado em CRLF.

### 2.2 Miniport KMD — Telemetria SMU.3 (`driver/wdk/bc250_active_miniport.c`, `bc250_mcdm_callbacks.c`, `.h`)
- Expandida sonda `Bc250ActiveSmuClockProbe`:
  - Msg `0x37`: `GetGfxFrequency` (MHz);
  - Msg `0x38`: `GetGfxVid` (mV);
  - Msg `0x02`: `GetSmuVersion`;
  - Msg `0x03`: `GetDriverIfVersion`;
  - Msg `0x3D`: `GetEnabledSmuFeatures`.
- `Bc250PagingTraceThread` emite periodicamente:
  ```text
  [SMU1] GFXCLK=%u MHz VDDGFX=%u mV SMU_VER=0x%08X IF_VER=0x%08X FEAT=0x%08X
  ```

### 2.3 Miniport KMD — DCN D23 Gamma (`driver/core/bc250_dcn_ext_contract.h`, `driver/wdk/bc250_display_ddi.c`)
- Definido registrador `BC250_DCN2_REG_CM0_BLNDGAM_CONTROL` (`BC250_DCN_BYTE(0x0D63u)` = `0x1088Cu`).
- Na inicialização DCN Ext, KMD lê e relata:
  ```text
  [DCN2_GAMMA] CM0_BLNDGAM_CONTROL=0x%08X (Mode=%lu: 0=Bypass, 1=sRGB, 2=RAM_A, 3=RAM_B)
  ```
- `Bc250DxgkDdiSetPalette` e `Bc250DxgkDdiSetTargetGamma` implementados com rastreamento completo de parâmetros do SO.

### 2.4 Monitor da Corrida (`tools/monitor-corrida.py`)
- Parseamento de `IF_VER=` e `FEAT=`.
- Exibição em tempo real na tela do console e no cabeçalho de telemetria.

---

## 3. Pré-condições e Checklist

| Item | Descrição | Status Esperado |
| :--- | :--- | :--- |
| **P01** | CMOS configurada em 256 MB aplicados | ✅ Obrigatório |
| **P02** | Boot frio (desligamento elétrico prévio) | ✅ Obrigatório |
| **P03** | `audit-bc250mcdm-safety.py` e `audit-bc250mcdm-lab-package.ps1` aprovados | ✅ Concluído (PASS) |
| **P04** | Driver de laboratório 3.435.1.0 compilado e empacotado | ✅ Concluído |
| **P05** | `tools\EXECUTAR_CORRIDA286_T3_ANIMADO_SMU3_D23_300S.cmd` gerado (300 s, CRLF) | ✅ Concluído |
| **P06** | `tools\prepare-corrida286-t3-smu3-d23.ps1` gerado | ✅ Concluído |

---

## 4. Critério de Aceite e Desfechos

- **DESFECHO A (SUCESSO PLENO):**
  1. Desktop acende no DisplayPort nativo com imagem limpa;
  2. Janela `BC-250 MONITOR` exibe GFXCLK (1500 MHz), tensão VDDGFX em mV, versão do SMU, IF_VER e FEAT;
  3. Carga T3 iniciada: Janela 3D de 480x320 surge na tela e permanece aberta e visível por **3 segundos**, exibindo uma animação contínua e suave de ondas de cores RGB a 60 FPS;
  4. Consumo de VRAM dedicada no monitor reflete a pressão das 4 texturas de 16 MB (~150–200 MB);
  5. Cada processo completa os 180 frames (~3000 ms) e encerra com código 0; o ciclo seguinte inicia normalmente;
  6. Zero TDR, zero BugCheck durante os **300 segundos** do watchdog, com rollback online perfeito para BasicDisplay.

- **DESFECHO B (COMPORTAMENTO ANÔMALO DO SWAPCHAIN / T3):**
  1. A janela 3D abre mas congela ou ocorre erro de apresentação no DXGI.

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
   tools\EXECUTAR_CORRIDA286_T3_ANIMADO_SMU3_D23_300S.cmd
   ```
4. Quando o desktop reiniciar sob o driver BC-250 e o monitor abrir exibindo os clocks, tensões e features, abra outro terminal como **Administrador** e execute:
   ```cmd
   tools\RODAR_PROCESSO_NOVO.cmd
   ```
5. Acompanhe na tela as janelas 3D renderizando as ondas senoidais de cores fluidas por 3 segundos cada uma e o monitor registrando a estabilidade da GPU durante os 300 segundos.
