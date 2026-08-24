# Guia de Monitoramento Térmico e Controle de Fans: AMD BC-250 no Windows

Este guia ensina como monitorar a temperatura do die da GPU, VRM da placa-mãe e configurar o controle de ventoinhas na AMD BC-250 (Cyan Skillfish / Oberon).

---

## 1. Exibição da Temperatura no GPU-Z (ADL Bridge)

O GPU-Z utiliza a biblioteca **AMD Display Library (ADL)** para ler sensores térmicos e elétricos em placas AMD.

### Como Ativar:
1. Localize a DLL compilada em [`BC250-V2/package/atiadlxx.dll`](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/package/atiadlxx.dll).
2. Copie o arquivo `atiadlxx.dll` para a **mesma pasta onde está o executável do GPU-Z (`GPU-Z.exe`)** (ou para `C:\Windows\System32\`).
3. Abra o GPU-Z e clique na aba **Sensors**.
4. Você verá em tempo real:
   - **GPU Temperature**: Temperatura do core gráfico (°C).
   - **GPU Core Voltage**: Tensão VDDGFX em mV (ex: 850 mV).
   - **GPU Clock**: Frequência GFXCLK em MHz (1500 MHz).
   - **Board Power Draw**: Consumo do pacote PPT em Watts.

---

## 2. Monitoramento em Tempo Real via Terminal (CLI)

O projeto inclui o utilitário de diagnóstico [`test_temperature.exe`](file:///c:/Users/Daniel/Desktop/bc250-win/bc250-win/BC250-V2/package/test_temperature.exe).

### Comandos:
* **Leitura Única:**
  ```cmd
  test_temperature.exe
  ```
* **Monitor Contínuo (Watch Mode - atualiza a cada 1 segundo):**
  ```cmd
  test_temperature.exe -w
  ```

---

## 3. Monitoramento de VRM e Controle de Ventoinhas (Nuvoton NCT6686D)

A placa-mãe da BC-250 possui o chip Super I/O **Nuvoton NCT6686D** (conectado no barramento ISA nas portas I/O `0x2E/0x2F` -> espaço lógico `0x0A20`).

### Sensores Físicos da Placa:
* **VRM MOS**: Temperatura dos estágios de potência e reguladores de tensão da APU.
* **Thermistor 14 / 15**: Termistores da placa de circuito impresso (PCB).
* **Conectores de Fan**:
  * **J1**: Conector primário do cooler da GPU/APU.
  * **J4003**: Conector secundário de ventoinha do gabinete.

### Softwares Recomendados no Windows:
1. **FanControl (Rem0o / FanControl):**
   - Software aberto que lê o chip Nuvoton via driver de baixo nível.
   - Permite criar uma curva personalizada (ex: 40°C = 35% PWM, 65°C = 55% PWM, 80°C = 100% PWM).
2. **LibreHardwareMonitor / HWiNFO64:**
   - Detecta o sensor ISA `NCT6686` e exibe todas as linhas de tensão (+12V, +5V, +3.3V, Vcore) e rotações (RPM).

---

## 4. Faixas Térmicas Recomendadas de Operação

| Estado | Temperatura da GPU | Temperatura do VRM | Ventoinha (PWM) |
| :--- | :---: | :---: | :---: |
| **Idle (Desktop / Web)** | 45°C – 55°C | 40°C – 48°C | 30% – 40% |
| **Gaming / Carga Média** | 65°C – 78°C | 55°C – 68°C | 60% – 75% |
| **Compute / LLM / Pico (230W)**| 75°C – 85°C | 65°C – 75°C | 80% – 100% |
| **Limite Crítico (Thermal Throttle)** | > 85°C – 90°C | > 85°C | 100% |
