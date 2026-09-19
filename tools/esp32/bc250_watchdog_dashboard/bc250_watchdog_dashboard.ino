/*
 * ============================================================================
 * BC-250 LAB HARDWARE WATCHDOG & WEB DASHBOARD (ESP32)
 * Target: ESP32 Dev Module (WROOM-32 / ESP32-S3 / etc.)
 * Project: NamixWDDM - AMD BC-250 (1002:13FE)
 * Author: Antigravity / Daniel
 * ============================================================================
 * 
 * Funcionalidades:
 * 1. Conecta ao Wi-Fi (SSID: daniel / Pass: daniel3777428260).
 * 2. Hospeda Web Dashboard responsiva (Dark Mode, compatível com smartphone).
 * 3. Monitora Heartbeat do Windows de 2 em 2 segundos (via Wi-Fi HTTP ou USB Serial).
 * 4. Detecção de Kernel Hang: se passar mais de 4 segundos sem ping (>2 pings perdidos),
 *    o dashboard alerta em VERMELHO que o PC travou e pode ser reiniciado.
 * 5. Exibe contagem regressiva do Watchdog (segundos restantes).
 * 6. Exibe status do Driver (bc250mcdm vs BasicDisplay) e do Fusível de Boot.
 * 7. Botão de Ação: "FORÇAR ROLLBACK" (envia comando imediato ao Windows).
 * 8. Suporte opcional a pulso de Hardware Reset no pino GPIO 4 (se conectado à placa-mãe).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Configurações de Wi-Fi
const char* WIFI_SSID = "daniel";
const char* WIFI_PASS = "daniel3777428260";

// Pino opcional para Reset Físico (via optoacoplador / transistor no conector RESET_SW da placa-mãe)
const int PIN_HW_RESET = 4;
const int PIN_LED = 2; // LED onboard (pisca no heartbeat)

WebServer server(80);

// Estado de Telemetria do Host
volatile unsigned long lastPingMs = 0;
volatile bool hostAlive = false;
volatile int missedPings = 0;

String watchdogState = "IDLE";
int remainingSeconds = 0;
String activeDriver = "BasicDisplay";
String bootFuse = "CLEAN (0)";
String lastKmdLog = "Aguardando primeiro heartbeat do Windows...";
String hostIp = "Desconhecido";
String pendingCommand = "";

// Intervalo esperado: 2000 ms. Timeout de travamento: 4500 ms (> 2 pings)
const unsigned long PING_INTERVAL_MS = 2000;
const unsigned long HANG_TIMEOUT_MS = 4500;

// ============================================================================
// HTML / CSS / JS DA DASHBOARD
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>BC-250 Watchdog & Telemetria</title>
  <style>
    :root {
      --bg: #0d1117;
      --card-bg: #161b22;
      --border: #30363d;
      --text: #c9d1d9;
      --text-bright: #f0f6fc;
      --green: #2ea043;
      --green-bg: #1b4728;
      --red: #f85149;
      --red-bg: #5c1e1e;
      --yellow: #d29922;
      --yellow-bg: #4d3800;
      --blue: #58a6ff;
      --blue-bg: #1f3554;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; }
    body { background-color: var(--bg); color: var(--text); padding: 15px; }
    .container { max-width: 650px; margin: 0 auto; }
    header { text-align: center; margin-bottom: 20px; padding-bottom: 10px; border-bottom: 1px solid var(--border); }
    h1 { color: var(--text-bright); font-size: 1.4rem; display: flex; align-items: center; justify-content: center; gap: 8px; }
    .subtitle { color: #8b949e; font-size: 0.85rem; margin-top: 4px; }
    
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 16px; margin-bottom: 15px; box-shadow: 0 4px 12px rgba(0,0,0,0.3); }
    .card-title { font-size: 0.85rem; text-transform: uppercase; color: #8b949e; letter-spacing: 0.5px; margin-bottom: 8px; font-weight: 600; }
    
    /* Alerta de Status do Host */
    .status-box { padding: 14px; border-radius: 6px; font-weight: 600; display: flex; flex-direction: column; gap: 6px; text-align: center; }
    .status-alive { background-color: var(--green-bg); color: #7ee787; border: 1px solid var(--green); }
    .status-hang { background-color: var(--red-bg); color: #ff7b72; border: 2px solid var(--red); animation: pulse 1.5s infinite; }
    @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.7; } 100% { opacity: 1; } }
    .status-title { font-size: 1.2rem; }
    .status-desc { font-size: 0.85rem; font-weight: 400; }

    /* Relógio do Watchdog */
    .timer-container { text-align: center; padding: 10px 0; }
    .timer-val { font-size: 3rem; font-weight: 700; color: var(--text-bright); font-variant-numeric: tabular-nums; }
    .timer-label { font-size: 0.85rem; color: #8b949e; }
    .badge { display: inline-block; padding: 3px 10px; border-radius: 12px; font-size: 0.75rem; font-weight: 600; margin-top: 6px; }
    .badge-armed { background: var(--yellow-bg); color: #f2cc60; border: 1px solid var(--yellow); }
    .badge-idle { background: #21262d; color: #8b949e; border: 1px solid var(--border); }
    .badge-rollback { background: var(--blue-bg); color: #79c0ff; border: 1px solid var(--blue); }
    .badge-disarmed { background: var(--green-bg); color: #7ee787; border: 1px solid var(--green); }

    /* Grid de Métricas */
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .metric { background: #0d1117; padding: 10px; border-radius: 6px; border: 1px solid var(--border); }
    .metric-label { font-size: 0.75rem; color: #8b949e; }
    .metric-value { font-size: 1rem; color: var(--text-bright); font-weight: 600; margin-top: 4px; word-break: break-all; }

    /* Terminal / Log */
    .terminal { background: #05070a; border: 1px solid #21262d; border-radius: 6px; padding: 10px; font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-size: 0.8rem; color: #58a6ff; min-height: 48px; max-height: 90px; overflow-y: auto; word-break: break-all; }

    /* Botões */
    .btn-group { display: flex; flex-direction: column; gap: 10px; margin-top: 5px; }
    button { padding: 14px 18px; border: none; border-radius: 6px; font-weight: 600; font-size: 1rem; cursor: pointer; transition: background 0.2s, transform 0.1s; }
    button:active { transform: scale(0.98); }
    .btn-danger { background-color: #da3633; color: white; border: 1px solid #f85149; }
    .btn-danger:hover { background-color: #b62324; }
    .btn-secondary { background-color: #21262d; color: var(--text); border: 1px solid var(--border); }
    .btn-secondary:hover { background-color: #30363d; }
    
    .toast { display: none; text-align: center; padding: 10px; border-radius: 6px; margin-top: 10px; font-size: 0.85rem; font-weight: 600; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>BC-250 Hardware Watchdog</h1>
      <div class="subtitle">AMD Cyan Skillfish 2 (1002:13FE) &middot; Out-of-Band Mini-BMC</div>
    </header>

    <!-- Card 1: Alerta de Status do Host -->
    <div class="card">
      <div id="host-status-box" class="status-box status-alive">
        <div id="host-status-title" class="status-title">CONECTANDO AO HOST...</div>
        <div id="host-status-desc" class="status-desc">Aguardando telemetria...</div>
      </div>
    </div>

    <!-- Card 2: Temporizador do Watchdog -->
    <div class="card">
      <div class="card-title">Temporizador de Protecao (Watchdog)</div>
      <div class="timer-container">
        <div id="timer-val" class="timer-val">--</div>
        <div class="timer-label">segundos restantes para rollback automatico</div>
        <div><span id="badge-state" class="badge badge-idle">IDLE</span></div>
      </div>
    </div>

    <!-- Card 3: Métricas do Sistema -->
    <div class="card">
      <div class="card-title">Estado do Hardware & Driver</div>
      <div class="grid">
        <div class="metric">
          <div class="metric-label">Driver em Execucao</div>
          <div id="val-driver" class="metric-value">--</div>
        </div>
        <div class="metric">
          <div class="metric-label">Fusivel de Boot (Anti-Loop)</div>
          <div id="val-fuse" class="metric-value">--</div>
        </div>
        <div class="metric">
          <div class="metric-label">IP do PC (Host)</div>
          <div id="val-ip" class="metric-value">--</div>
        </div>
        <div class="metric">
          <div class="metric-label">Ultimo Ping do Host</div>
          <div id="val-lastping" class="metric-value">--</div>
        </div>
      </div>
    </div>

    <!-- Card 4: Último Log KMD -->
    <div class="card">
      <div class="card-title">Ultimo Registro do Driver KMD</div>
      <div id="val-kmdlog" class="terminal">Aguardando...</div>
    </div>

    <!-- Card 5: Controles de Emergência -->
    <div class="card">
      <div class="card-title">Acoes Manuais de Bancada</div>
      <div class="btn-group">
        <button id="btn-rollback" class="btn-danger" onclick="triggerAction('rollback')">
          FORCAR ROLLBACK IMEDIATO (BASICDISPLAY)
        </button>
        <button id="btn-reset-fuse" class="btn-secondary" onclick="triggerAction('reset_fuse')">
          Resetar Fusivel de Boot (Limpar Estado 2)
        </button>
        <button id="btn-hw-reset" class="btn-secondary" style="color: #ff7b72;" onclick="triggerAction('hw_reset')">
          ⚡ Pulso de Hardware Reset (Pino GPIO4)
        </button>
      </div>
      <div id="toast" class="toast"></div>
    </div>
  </div>

  <script>
    function updateUI() {
      fetch('/api/status')
        .then(res => res.json())
        .then(data => {
          const statusBox = document.getElementById('host-status-box');
          const statusTitle = document.getElementById('host-status-title');
          const statusDesc = document.getElementById('host-status-desc');

          if (data.host_alive) {
            statusBox.className = 'status-box status-alive';
            statusTitle.innerHTML = '&#9679; HOST ONLINE &middot; SISTEMA RESPONDENDO';
            statusDesc.innerHTML = 'Ping a cada 2s &middot; Ultima resposta ha ' + data.seconds_since_ping + 's';
          } else {
            statusBox.className = 'status-box status-hang';
            statusTitle.innerHTML = '&#9888; PC TRAVADO / KERNEL HANG DETECTADO';
            statusDesc.innerHTML = 'O Windows parou de responder ha ' + data.seconds_since_ping + 's (>2 pings perdidos). O kernel travou ou deu BugCheck. PODE REINICIAR O COMPUTADOR.';
          }

          // Timer
          document.getElementById('timer-val').innerText = data.remaining_sec;
          const badge = document.getElementById('badge-state');
          badge.innerText = data.watchdog_state;
          badge.className = 'badge';
          if (data.watchdog_state === 'ARMED') badge.classList.add('badge-armed');
          else if (data.watchdog_state === 'DISARMED') badge.classList.add('badge-disarmed');
          else if (data.watchdog_state.includes('ROLLBACK')) badge.classList.add('badge-rollback');
          else badge.classList.add('badge-idle');

          // Métricas
          document.getElementById('val-driver').innerText = data.active_driver;
          document.getElementById('val-fuse').innerText = data.boot_fuse;
          document.getElementById('val-ip').innerText = data.host_ip;
          document.getElementById('val-lastping').innerText = data.seconds_since_ping + 's atras';
          document.getElementById('val-kmdlog').innerText = data.last_log;
        })
        .catch(err => {
          console.error("Erro ao obter status:", err);
        });
    }

    function triggerAction(action) {
      if (action === 'rollback') {
        if (!confirm("Confirmar reversao forçada imediata para o Microsoft BasicDisplay?")) return;
      }
      if (action === 'hw_reset') {
        if (!confirm("AVISO: Isso enviara um pulso fisico no conector de RESET da placa-mae. Deseja reiniciar a maquina agora?")) return;
      }

      fetch('/api/cmd?action=' + action, { method: 'POST' })
        .then(res => res.text())
        .then(msg => {
          const toast = document.getElementById('toast');
          toast.style.display = 'block';
          toast.style.backgroundColor = 'var(--blue-bg)';
          toast.style.color = '#79c0ff';
          toast.innerText = msg;
          setTimeout(() => { toast.style.display = 'none'; }, 4000);
        });
    }

    setInterval(updateUI, 1000);
    updateUI();
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// HANDLERS DO SERVIDOR WEB
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  unsigned long now = millis();
  unsigned long diffMs = (now >= lastPingMs) ? (now - lastPingMs) : 0;
  int secondsSincePing = diffMs / 1000;

  // Lógica de Detecção de Travamento: se passar mais de 4500ms (2 pings de 2s), host está travado
  if (lastPingMs == 0) {
    hostAlive = false;
  } else if (diffMs > HANG_TIMEOUT_MS) {
    hostAlive = false;
  } else {
    hostAlive = true;
  }

  String json = "{";
  json += "\"host_alive\":" + String(hostAlive ? "true" : "false") + ",";
  json += "\"seconds_since_ping\":" + String(secondsSincePing) + ",";
  json += "\"watchdog_state\":\"" + watchdogState + "\",";
  json += "\"remaining_sec\":" + String(remainingSeconds) + ",";
  json += "\"active_driver\":\"" + activeDriver + "\",";
  json += "\"boot_fuse\":\"" + bootFuse + "\",";
  json += "\"host_ip\":\"" + hostIp + "\",";
  // Escapa aspas no log
  String escapedLog = lastKmdLog;
  escapedLog.replace("\"", "\\\"");
  json += "\"last_log\":\"" + escapedLog + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// Endpoint para receber ping/heartbeat do Windows via HTTP POST/GET
void handleHeartbeat() {
  lastPingMs = millis();
  hostAlive = true;

  if (server.hasArg("state")) watchdogState = server.arg("state");
  if (server.hasArg("rem")) remainingSeconds = server.arg("rem").toInt();
  if (server.hasArg("drv")) activeDriver = server.arg("drv");
  if (server.hasArg("fuse")) bootFuse = server.arg("fuse");
  if (server.hasArg("log")) lastKmdLog = server.arg("log");
  hostIp = server.client().remoteIP().toString();

  // Pisca o LED onboard rapidamente a cada ping
  digitalWrite(PIN_LED, HIGH);
  delay(15);
  digitalWrite(PIN_LED, LOW);

  // Retorna comandos pendentes (ex: se o usuário clicou em Rollback no Dashboard)
  String response = "OK";
  if (pendingCommand.length() > 0) {
    response = "CMD:" + pendingCommand;
    pendingCommand = ""; // limpa comando entregue
  }
  server.send(200, "text/plain", response);
}

// Endpoint para ações do usuário (clique nos botões da dashboard)
void handleCommand() {
  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "Acao ausente");
    return;
  }
  String act = server.arg("action");

  if (act == "rollback") {
    pendingCommand = "FORCE_ROLLBACK";
    Serial.println("CMD:FORCE_ROLLBACK");
    server.send(200, "text/plain", "Comando FORCE_ROLLBACK registrado e enviado ao Windows!");
  } else if (act == "reset_fuse") {
    pendingCommand = "RESET_FUSE";
    Serial.println("CMD:RESET_FUSE");
    server.send(200, "text/plain", "Comando RESET_FUSE registrado e enviado ao Windows!");
  } else if (act == "hw_reset") {
    // Pulso físico no pino de reset da placa-mãe
    pinMode(PIN_HW_RESET, OUTPUT);
    digitalWrite(PIN_HW_RESET, HIGH); // aciona optoacoplador / transistor
    delay(500);
    digitalWrite(PIN_HW_RESET, LOW);
    pinMode(PIN_HW_RESET, INPUT);
    Serial.println("INFO:PULSO_HW_RESET_DISPARADO");
    server.send(200, "text/plain", "⚡ Pulso fisico de Hardware Reset disparado no pino GPIO4!");
  } else {
    server.send(400, "text/plain", "Acao desconhecida");
  }
}

// ============================================================================
// PROCESSAMENTO DE ENTRADA SERIAL (USB-UART)
// Permite que o script do Windows também envie status ou leia comandos via USB
// ============================================================================
void processSerialInput() {
  while (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Formato: PING|state=ARMED|rem=140|drv=bc250mcdm|fuse=CLEAN|log=Present #14
    if (line.startsWith("PING|")) {
      lastPingMs = millis();
      hostAlive = true;

      // Parsing de campos chave-valor
      int start = 5;
      while (start < line.length()) {
        int nextPipe = line.indexOf('|', start);
        if (nextPipe == -1) nextPipe = line.length();
        String token = line.substring(start, nextPipe);
        int eq = token.indexOf('=');
        if (eq != -1) {
          String k = token.substring(0, eq);
          String v = token.substring(eq + 1);
          if (k == "state") watchdogState = v;
          else if (k == "rem") remainingSeconds = v.toInt();
          else if (k == "drv") activeDriver = v;
          else if (k == "fuse") bootFuse = v;
          else if (k == "log") lastKmdLog = v;
          else if (k == "ip") hostIp = v;
        }
        start = nextPipe + 1;
      }

      digitalWrite(PIN_LED, HIGH);
      delay(15);
      digitalWrite(PIN_LED, LOW);
    }
  }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BC-250 Watchdog] Inicializando ESP32...");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_HW_RESET, INPUT); // alta impedância por padrão

  // Conexão Wi-Fi
  Serial.printf("[BC-250 Watchdog] Conectando ao Wi-Fi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[BC-250 Watchdog] Wi-Fi Conectado com Sucesso!");
    Serial.printf("[BC-250 Watchdog] IP Local: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("bc250")) {
      Serial.println("[BC-250 Watchdog] mDNS iniciado: http://bc250.local");
    }
  } else {
    Serial.println("\n[BC-250 Watchdog] AVISO: Falha ao conectar no Wi-Fi. Verifique sinal e credenciais.");
  }

  // Rotas do Servidor
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/heartbeat", HTTP_ANY, handleHeartbeat);
  server.on("/api/cmd", HTTP_ANY, handleCommand);

  server.begin();
  Serial.println("[BC-250 Watchdog] Servidor Web ativo na porta 80.");
}

void loop() {
  server.handleClient();
  processSerialInput();

  // Se o host travou, pisca LED de alerta lentamente
  unsigned long now = millis();
  if (lastPingMs > 0 && (now - lastPingMs > HANG_TIMEOUT_MS)) {
    hostAlive = false;
    static unsigned long lastBlink = 0;
    if (now - lastBlink > 300) {
      lastBlink = now;
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
  }
}
