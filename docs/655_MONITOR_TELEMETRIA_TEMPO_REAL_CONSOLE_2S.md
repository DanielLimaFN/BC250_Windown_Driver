# 655 — Monitor de Telemetria em Tempo Real no Console (Cadência 2s)

> **Data:** 2026-10-08  
> **Componente:** `tools/monitor/bc250_monitor.c` $\to$ `tools/monitor/bc250_monitor.exe`, `tools/MONITOR_TELEMETRIA_2S.cmd` e `tools/bc250-telemetry-console.ps1`  
> **Alvo:** AMD BC-250 (`1002:13FE`, GC 10.1.3 / Cyan Skillfish 2) no Windows 10 22H2  
> **Finalidade:** Exibição contínua no console a cada 2 segundos de todas as métricas críticas de hardware durante a execução do driver WDDM.

---

## 1. Métricas Monitoradas a Cada 2 Segundos

O monitor coleta e exibe em tempo real:

1. **Clock da CPU (Média de todos os núcleos):**
   - **Mecanismo:** Chamada Win32 `CallNtPowerInformation(ProcessorInformation, ...)` via `powrprof.dll`.
   - **Cálculo:** Obtém a frequência instantânea `CurrentMhz` de cada thread/núcleo lógico (16 threads no Zen 2 APU) e calcula a média aritmética exata:
     $$\text{CPU\_Avg\_MHz} = \frac{1}{N} \sum_{i=0}^{N-1} \text{CurrentMhz}_i$$
   - Também exibe a frequência mínima, máxima e clock base.

2. **Uso de Memória RAM:**
   - **Mecanismo:** Chamada Win32 `GlobalMemoryStatusEx(...)`.
   - **Métricas:** RAM usada (GB), RAM total (GB) e percentual de ocupação (%).

3. **Uso de Memória de Vídeo (VRAM - GDDR6):**
   - **Mecanismo:** Interface COM `IDXGIAdapter3::QueryVideoMemoryInfo(..., DXGI_MEMORY_SEGMENT_GROUP_LOCAL, ...)`.
   - **Métricas:** VRAM alocada em tempo real pelo DWM/aplicações em MB, orçamento total do segmento em MB e percentual (%).

4. **GPU GFX Clock (Frequência do Core):**
   - **Mecanismo:** Consulta via mailbox do SMU (Queue 0, Msg `0x37`: `BC250_PPSMC_MSG_GetGfxFrequency`).
   - **Unidade:** MHz em tempo real (ex.: repouso 75 MHz $\to$ carga 1000 MHz $\to$ boost 2000 MHz).

5. **Tensão VDDGFX (Core Voltage):**
   - **Mecanismo:** Consulta ao SMU (Queue 0, Msg `0x38`: `BC250_PPSMC_MSG_GetGfxVid` convertida para mV via $\text{mV} = (-VID \times 0.00625 + 1.55) \times 1000$ e/ou Queue 3, Msg `0x37`).
   - **Unidade:** Milivolts (mV) reais do silício (faixa nominal: 700 mV a 1129 mV).

6. **Temperatura da GPU:**
   - **Mecanismo:** Leitura direta de SMN via abertura BAR5 (Offset `0x38`/`0x3C`):
     - `BC250_SMN_TEMP_JUNCTION` (`0x03B10020`): Temperatura de junção do silício.
     - `BC250_SMN_TEMP_EDGE` (`0x03B10000`): Sensor de borda.
   - **Unidade:** Graus Celsius (°C).

7. **Temperatura das Memórias (GDDR6 / UMC):**
   - **Mecanismo:** Leitura SMN `BC250_SMN_TEMP_MEM` (`0x03B10028`), decompondo os bytes térmicos dos bancos de memória GDDR6 e do controlador UMC.

8. **Temperatura da CPU (Diodo Térmico Zen 2 TSI):**
   - **Mecanismo:** Diodo térmico integrado Zen 2 TSI em SMN `0x00059800` (fórmula: $((raw \times 125.0) - 49000.0) / 1000.0$ °C).

9. **Carga e Atividade de Renderização da GPU:**
   - **Mecanismo:** Amostragem por janela deslizante no registrador MMIO `GRBM_STATUS` (`0x08010`) durante o ciclo de 2 segundos (100 amostras a cada 20 ms), detectando engajamento das engines `IA_BUSY` (Input Assembler) e `WD_BUSY` (Wavefront Distributor).

---

## 2. Formas de Execução

### Opção 1: Clique Duplo ou Terminal CMD (Recomendado)
```cmd
tools\MONITOR_TELEMETRIA_2S.cmd
```
Para modo de log contínuo (uma linha por ciclo de 2s):
```cmd
tools\MONITOR_TELEMETRIA_2S.cmd --line
```

### Opção 2: PowerShell
```powershell
.\tools\bc250-telemetry-console.ps1
```
Para modo de log contínuo:
```powershell
.\tools\bc250-telemetry-console.ps1 -LineMode
```

---

## 3. Exemplo de Saída no Console

### Modo Dashboard Formatado (Padrão):
```text
================================================================================
       AMD BC-250 (GC 10.1.3 / CYAN SKILLFISH) - TELEMETRIA (2s) [12:56:20]    
================================================================================
 >> RESUMO: CPU: 3194 MHz | RAM: 6.1 GB (39%) | GPU: 1000 MHz | VDDGFX: 918 mV | VRAM: 320 MB | T_GPU: 48 C | T_MEM: 52 C
--------------------------------------------------------------------------------
[PROCESSADOR CPU]
  Clock Medio   : 3194.0 MHz  (Min: 3194 MHz | Max: 3194 MHz | 16 Threads)
  Temperatura   : 54.2 C  (Diodo Termico Zen 2 TSI SMN 0x59800)
  Cores Ativos  : 8 Cores Fisicos (Mascara SMN: 0xFF)

[MEMORIA RAM DO SISTEMA]
  Uso de RAM    : [##########          ] 6.15 GB / 15.48 GB (39.0%)

[PLACA GRAFICA BC-250 (GPU)]
  GPU GFX Clock : 1000 MHz  (DPM SMU Mailbox Msg 0x37)
  Tensao VDDGFX : 918 mV  (VID: 101 | Core VDD: 918 mV)
  Carga da GPU  : [##                  ] 4.0%  (GRBM: 0x00000000)
  WGPs Ativos   : 12 (24 Compute Units Fisicas)

[MEMORIA DE VIDEO VRAM (GDDR6)]
  Uso de VRAM   : 320.0 MB / 16384.0 MB (2.0%)
  Adaptador DXGI: AMD BC-250 WDDM Driver

[TEMPERATURAS DO SILICIO]
  GPU Junction  : 48 C  (Low: 48 C | High: 52 C | Raw: 0x00003430)
  GPU Edge      : Ativo  (Raw: 0x00002E00)
  Memoria GDDR6 : 52 C  (Raw: 0x00003434: B0=52 C, B1=52 C, B2=50 C)
  Ventoinha Fan : 2150 RPM  (PWM: 140 / 255)
================================================================================
 Atualizacao: a cada 2.0s | Pressione Ctrl+C para sair | Parametro: --line para log
```

### Modo Linha Contínua (`--line`):
```text
[12:56:20] CPU: 3194 MHz (med) | RAM:  6.1/15.5 GB (39%) | GPU: 1000 MHz ( 4%) | VDDGFX:  918 mV | VRAM:  320 MB ( 2%) | Temp GPU: 48 C | Temp Mem: 52 C
[12:56:22] CPU: 3194 MHz (med) | RAM:  6.1/15.5 GB (39%) | GPU: 1000 MHz ( 2%) | VDDGFX:  918 mV | VRAM:  320 MB ( 2%) | Temp GPU: 48 C | Temp Mem: 52 C
[12:56:24] CPU: 3194 MHz (med) | RAM:  6.1/15.5 GB (39%) | GPU: 1000 MHz ( 0%) | VDDGFX:  918 mV | VRAM:  320 MB ( 2%) | Temp GPU: 48 C | Temp Mem: 51 C
```

---

## 4. Tolerância e Reconexão Automática

Se o serviço de kernel `bc250compute.sys` não estiver ativo no momento em que o monitor for aberto:
- O monitor **não aborta nem fecha**.
- Ele continua exibindo normalmente o **Clock Médio da CPU, Uso de RAM e Uso de VRAM** (APIs nativas do Windows).
- As métricas de SMN/SMU ficam em modo de espera e se conectam automaticamente no instante em que o driver for inicializado.
