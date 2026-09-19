# BC-250 Hardware Watchdog & Web Dashboard (ESP32)

Este módulo implementa uma bancada de **Gerenciamento Fora de Banda (Out-of-Band Mini-BMC)** usando um microcontrolador ESP32 conectado via Wi-Fi e USB ao sistema de testes da AMD BC-250 (`1002:13FE`).

---

## 🎯 Por que usar o ESP32?

1. **Fim do "Ponto Cego" na Tela Preta:**
   Quando o driver WDDM desliga o `BasicDisplay` e a tela apaga, você não precisa ficar no escuro sem saber se o sistema travou ou se o watchdog ainda está contando os segundos.
2. **Detecção Real de Kernel Hang / Travamento:**
   O Windows envia um "ping" a cada **2 segundos**. Se o Windows travar em tela azul (BSOD 0x119, 0x133, 0x0A) ou deadlock em spinlock (DPC), os pings param. Se o ESP32 ficar mais de **4,5 segundos** sem receber sinal, a Dashboard avisa em vermelho:
   > **⚠️ PC TRAVADO / KERNEL HANG DETECTADO (Sem ping há X segundos) — PODE REINICIAR O COMPUTADOR.**
3. **Botão de Pânico / Forçar Rollback Imediato:**
   Se você perceber que o teste falhou ou quiser restaurar o vídeo imediatamente, não precisa esperar os 3 minutos (180s) do watchdog do Windows: basta tocar no botão **"FORÇAR ROLLBACK IMEDIATO"** no celular, e o Windows restaura o `BasicDisplay` em segundos.
4. **Controle Físico (Opcional):**
   O pino `GPIO 4` pode ser conectado ao conector `RESET_SW` da placa-mãe (via transistor ou optoacoplador) para enviar um pulso de reset de hardware real caso o computador congele completamente.

---

## 📶 Configuração de Rede Wi-Fi

O firmware já vem pré-configurado com a sua rede:
* **SSID:** `daniel`
* **Senha:** `daniel3777428260`
* **Porta Web:** 80 (HTTP padrão)
* **Nome mDNS:** `http://bc250.local`

Ao ligar, o ESP32 se conecta na rede e imprime o IP local na Serial (115200 baud). Você pode acessar a dashboard em qualquer celular ou notebook na mesma rede acessando:
`http://bc250.local` ou `http://<IP-DO-ESP32>`

---

## 🚀 Como Gravar no ESP32

### Pelo Arduino IDE:
1. Abra o arquivo `tools/esp32/bc250_watchdog_dashboard.ino`.
2. Em **Ferramentas -> Placa -> ESP32 Arduino**, selecione **ESP32 Dev Module** (ou o modelo exato da sua placa).
3. Selecione a porta serial do ESP32 (ver nota sobre driver abaixo).
4. Clique em **Upload**.

### Driver USB CP2102 no Windows:
Se o dispositivo estiver aparecendo com exclamação amarela no Gerenciador de Dispositivos (`USB\VID_10C4&PID_EA60`), instale o **Silicon Labs CP210x VCP Windows Driver** para habilitar a porta `COMx`.
*Mesmo sem a porta COM instalada no Windows, o sistema funciona 100% via Wi-Fi!*

---

## 💻 Como Iniciar a Ponte no Windows

Para iniciar a transmissão do Heartbeat a cada 2 segundos e permitir comandos do dashboard:

```powershell
# Modo Wi-Fi (usa mDNS ou IP direto):
powershell -ExecutionPolicy Bypass -File tools\esp32-companion.ps1 -Esp32Host "bc250.local"

# Ou se preferir especificar o IP diretamente:
powershell -ExecutionPolicy Bypass -File tools\esp32-companion.ps1 -Esp32Host "192.168.1.X"

# Se o driver CP2102 estiver instalado e você quiser redundância via cabo USB:
powershell -ExecutionPolicy Bypass -File tools\esp32-companion.ps1 -Esp32Host "bc250.local" -ComPort "COM3"
```
