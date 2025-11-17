#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
// ==================== WI-FI AP ======================
const char* ssid = "iPhone";
const char* password = "12345678";
WebServer server(80);
// ==================== MQTT BROKER ===================
// IMPORTANTE: Ajuste o IP abaixo para o IP do seu PC onde o Mosquitto está rodando
// Se o ESP32 está como Access Point, o PC precisa estar conectado na mesma rede Wi-Fi
// Exemplo: se o PC tem IP 192.168.4.2 na rede do ESP32, use "192.168.4.2"
// Para descobrir o IP do PC: no Windows use "ipconfig", no Linux/Mac use "ifconfig"
const char* mqtt_server = "169.254.14.138";  // IP do PC com Mosquitto (AJUSTE AQUI!)
const int mqtt_port = 1883;
const char* mqtt_client_id = "semaforo_inteligente";
const char* mqtt_topic_telemetria = "semaforo/telemetria";
const char* mqtt_topic_comandos = "semaforo/comandos";
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long ultimaPublicacaoMQTT = 0;
const unsigned long intervaloPublicacaoMQTT = 5000;  // Publica a cada 5 segundos
// =============== PINOS DO SEMÁFORO ==================
const int S1_red    = 27;
const int S1_yellow = 14;
const int S1_green  = 12;
const int S2_red    = 33;
const int S2_yellow = 25;
const int S2_green  = 26;
// =============== LDR ================================
const int LDR_PIN = 32;

// =============== DECLARAÇÕES FORWARD =================
void publicarTelemetriaMQTT();  // Declaração forward para uso na classe

// =============== CLASSES ============================
class Semaforo {
public:
  Semaforo(int redPin, int yellowPin, int greenPin)
      : pRed(redPin), pYellow(yellowPin), pGreen(greenPin) {}

  void begin() const {
    pinMode(pRed, OUTPUT);
    pinMode(pYellow, OUTPUT);
    pinMode(pGreen, OUTPUT);
    apagar();
  }

  void verde() const { setEstado(LOW, LOW, HIGH); }
  void amarelo() const { setEstado(LOW, HIGH, LOW); }
  void vermelho() const { setEstado(HIGH, LOW, LOW); }
  void amareloPisca(bool ligado) const { setEstado(LOW, ligado ? HIGH : LOW, LOW); }
  void apagar() const { setEstado(LOW, LOW, LOW); }

private:
  int pRed;
  int pYellow;
  int pGreen;

  void setEstado(uint8_t redState, uint8_t yellowState, uint8_t greenState) const {
    digitalWrite(pRed, redState);
    digitalWrite(pYellow, yellowState);
    digitalWrite(pGreen, greenState);
  }
};

class SemaforoInteligente {
public:
  struct Telemetria {
    int luz = 0;
    bool autoAtivo = true;
    bool noturnoAtivo = false;
    unsigned long timestamp = 0;
  };

  SemaforoInteligente(Semaforo& s1Ref, Semaforo& s2Ref, int ldrPin)
      : semaforo1(s1Ref),
        semaforo2(s2Ref),
        ldrPin(ldrPin) {}

  void begin() {
    Serial.println("[SemaforoInteligente] Inicializando semaforos...");
    semaforo1.begin();
    semaforo2.begin();
    pinMode(ldrPin, INPUT);
    tempoAnterior = millis();
    tempoPisca = millis();
    atualizarTelemetria();
    Serial.println("[SemaforoInteligente] Inicializacao completa");
  }

  void atualizar() {
    lerLuminosidade();
    if (modoAuto) aplicarHisterese();
    if (modoNoturno) cicloNoturno();
    else cicloNormal();
    atualizarTelemetria();
    publicarTelemetriaMQTT();
  }

  void setModoAuto() {
    modoAuto = true;
    Serial.println("[Modo] Alterado para AUTOMATICO");
  }

  void setModoNormal() {
    modoAuto = false;
    modoNoturno = false;
    Serial.println("[Modo] Alterado para NORMAL");
  }

  void setModoNoturno() {
    modoAuto = false;
    modoNoturno = true;
    Serial.println("[Modo] Alterado para NOTURNO");
  }

  bool isModoAuto() const { return modoAuto; }
  bool isModoNoturno() const { return modoNoturno; }
  bool isModoNormal() const { return !modoAuto && !modoNoturno; }
  int getLuminosidade() const { return luminosidade; }
  const Telemetria& getTelemetria() const { return telemetriaAtual; }

private:
  // Ajustados baseado nos valores reais do LDR (Noturno: 0-2000, Diurno: 2000-5000)
  static constexpr int LIMITE_ENTRAR_NOTURNO = 1800;  // Entra no modo noturno quando < 1800
  static constexpr int LIMITE_SAIR_NOTURNO = 2200;    // Sai do modo noturno quando > 2200
  static constexpr unsigned long TEMPO_VERDE = 3000;
  static constexpr unsigned long TEMPO_AMARELO = 1500;
  static constexpr unsigned long TEMPO_PISCA = 500;

  Semaforo& semaforo1;
  Semaforo& semaforo2;
  int ldrPin;

  int luminosidade = 0;
  bool modoAuto = true;
  bool modoNoturno = false;
  unsigned long tempoAnterior = 0;
  int estado = 0;
  unsigned long tempoPisca = 0;
  bool piscaEstado = false;
  Telemetria telemetriaAtual;

  void lerLuminosidade() {
    static unsigned long ultimoPrint = 0;
    luminosidade = analogRead(ldrPin);
    // Print a cada 2 segundos para não poluir o Serial
    if (millis() - ultimoPrint >= 2000) {
      Serial.printf("[LDR] Luminosidade: %d\n", luminosidade);
      ultimoPrint = millis();
    }
  }

  void aplicarHisterese() {
    bool mudou = false;
    if (luminosidade < LIMITE_ENTRAR_NOTURNO && !modoNoturno) {
      modoNoturno = true;
      mudou = true;
      Serial.printf("[Histerese] Entrando em modo NOTURNO (LDR=%d < %d)\n", 
                    luminosidade, LIMITE_ENTRAR_NOTURNO);
    }
    if (luminosidade > LIMITE_SAIR_NOTURNO && modoNoturno) {
      modoNoturno = false;
      mudou = true;
      Serial.printf("[Histerese] Saindo do modo NOTURNO - Modo NORMAL (LDR=%d > %d)\n", 
                    luminosidade, LIMITE_SAIR_NOTURNO);
    }
  }

  void cicloNormal() {
    unsigned long agora = millis();
    switch (estado) {
      case 0:
        semaforo1.verde();
        semaforo2.vermelho();
        if (agora - tempoAnterior >= TEMPO_VERDE) transicaoPara(1, agora);
        break;
      case 1:
        semaforo1.amarelo();
        semaforo2.vermelho();
        if (agora - tempoAnterior >= TEMPO_AMARELO) transicaoPara(2, agora);
        break;
      case 2:
        semaforo1.vermelho();
        semaforo2.verde();
        if (agora - tempoAnterior >= TEMPO_VERDE) transicaoPara(3, agora);
        break;
      case 3:
        semaforo1.vermelho();
        semaforo2.amarelo();
        if (agora - tempoAnterior >= TEMPO_AMARELO) transicaoPara(0, agora);
        break;
      default:
        estado = 0;
        break;
    }
  }

  void transicaoPara(int novoEstado, unsigned long agora) {
    estado = novoEstado;
    tempoAnterior = agora;
    Serial.printf("[Ciclo Normal] Transicao para estado %d\n", novoEstado);
  }

  void cicloNoturno() {
    unsigned long agora = millis();
    if (agora - tempoPisca >= TEMPO_PISCA) {
      tempoPisca = agora;
      piscaEstado = !piscaEstado;
    }
    semaforo1.amareloPisca(piscaEstado);
    semaforo2.amareloPisca(piscaEstado);
  }

  void atualizarTelemetria() {
    telemetriaAtual.luz = luminosidade;
    telemetriaAtual.autoAtivo = modoAuto;
    telemetriaAtual.noturnoAtivo = modoNoturno;
    telemetriaAtual.timestamp = millis();
  }

  void publicarTelemetriaMQTT() {
    // Chama a função global que tem acesso ao mqttClient
    ::publicarTelemetriaMQTT();
  }
};

Semaforo semaforoPrincipal(S1_red, S1_yellow, S1_green);
Semaforo semaforoSecundario(S2_red, S2_yellow, S2_green);
SemaforoInteligente controlador(semaforoPrincipal, semaforoSecundario, LDR_PIN);
// ======================================================
// ================== FUNÇÕES MQTT =====================
// ======================================================
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Mensagem recebida no topico: ");
  Serial.println(topic);
  
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  Serial.print("[MQTT] Conteudo: ");
  Serial.println(mensagem);
  
  // Processar comandos recebidos via MQTT
  if (String(topic) == mqtt_topic_comandos) {
    if (mensagem == "auto" || mensagem == "AUTO") {
      controlador.setModoAuto();
      Serial.println("[MQTT] Comando executado: Modo Automático");
    } else if (mensagem == "normal" || mensagem == "NORMAL") {
      controlador.setModoNormal();
      Serial.println("[MQTT] Comando executado: Modo Normal");
    } else if (mensagem == "noturno" || mensagem == "NOTURNO") {
      controlador.setModoNoturno();
      Serial.println("[MQTT] Comando executado: Modo Noturno");
    }
  }
}

void reconectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Tentando conectar ao broker...");
    if (mqttClient.connect(mqtt_client_id)) {
      Serial.println(" Conectado!");
      // Subscrever ao tópico de comandos
      if (mqttClient.subscribe(mqtt_topic_comandos)) {
        Serial.print("[MQTT] Inscrito no topico: ");
        Serial.println(mqtt_topic_comandos);
      } else {
        Serial.println("[MQTT] ERRO: Falha ao se inscrever no topico");
      }
    } else {
      Serial.print("[MQTT] Falha, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Tentando novamente em 5 segundos...");
      delay(5000);
    }
  }
}

void publicarTelemetriaMQTT() {
  if (!mqttClient.connected()) {
    reconectarMQTT();
  }
  
  mqttClient.loop();  // Manter conexão ativa e processar mensagens
  
  unsigned long agora = millis();
  if (agora - ultimaPublicacaoMQTT >= intervaloPublicacaoMQTT) {
    ultimaPublicacaoMQTT = agora;
    
    const auto& telemetria = controlador.getTelemetria();
    
    // Criar JSON da telemetria
    String json = "{";
    json += "\"luminosidade\":" + String(telemetria.luz) + ",";
    json += "\"modoAuto\":" + String(telemetria.autoAtivo ? "true" : "false") + ",";
    json += "\"modoNoturno\":" + String(telemetria.noturnoAtivo ? "true" : "false") + ",";
    json += "\"timestamp\":" + String(telemetria.timestamp);
    json += "}";
    
    // Publicar no tópico
    if (mqttClient.publish(mqtt_topic_telemetria, json.c_str())) {
      Serial.print("[MQTT] Telemetria publicada: ");
      Serial.println(json);
    } else {
      Serial.println("[MQTT] ERRO: Falha ao publicar telemetria");
    }
  }
}
// ======================================================
// ================== FUNÇÃO HTML =======================
// ======================================================
void handleRoot() {
  Serial.println("[HTTP] Requisicao recebida: /");
  int luxAtual = controlador.getLuminosidade();
  bool autoAtivo = controlador.isModoAuto();
  bool noturnoAtivo = controlador.isModoNoturno();
  String modoAtual = autoAtivo ? "Automático" : (noturnoAtivo ? "Noturno" : "Normal");
  
  String html = R"(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Semáforo Inteligente</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
      color: #333;
    }
    .container {
      max-width: 800px;
      margin: 0 auto;
    }
    .header {
      text-align: center;
      color: white;
      margin-bottom: 30px;
      padding: 20px;
    }
    .header h1 {
      font-size: 2.5em;
      margin-bottom: 10px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
    }
    .header p {
      font-size: 1.1em;
      opacity: 0.9;
    }
    .card {
      background: white;
      border-radius: 20px;
      padding: 30px;
      margin-bottom: 20px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
      transition: transform 0.3s ease, box-shadow 0.3s ease;
    }
    .card:hover {
      transform: translateY(-5px);
      box-shadow: 0 15px 40px rgba(0,0,0,0.3);
    }
    .status-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 20px;
      margin-bottom: 30px;
    }
    .status-item {
      text-align: center;
      padding: 20px;
      background: #f8f9fa;
      border-radius: 15px;
      transition: all 0.3s ease;
    }
    .status-item.active {
      background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%);
      color: white;
      transform: scale(1.05);
      box-shadow: 0 5px 15px rgba(76, 175, 80, 0.4);
    }
    .status-item h3 {
      font-size: 0.9em;
      margin-bottom: 10px;
      opacity: 0.8;
    }
    .status-item .value {
      font-size: 1.8em;
      font-weight: bold;
    }
    .luminosidade-container {
      margin: 20px 0;
    }
    .luminosidade-label {
      display: flex;
      justify-content: space-between;
      margin-bottom: 10px;
      font-weight: 600;
    }
    .progress-bar {
      width: 100%;
      height: 30px;
      background: #e0e0e0;
      border-radius: 15px;
      overflow: hidden;
      position: relative;
      box-shadow: inset 0 2px 5px rgba(0,0,0,0.1);
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #ffd700 0%, #ff8c00 50%, #ff4500 100%);
      border-radius: 15px;
      transition: width 0.5s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
      font-weight: bold;
      font-size: 0.9em;
    }
    .modo-badges {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      justify-content: center;
      margin-top: 20px;
    }
    .badge {
      padding: 10px 20px;
      border-radius: 25px;
      font-weight: 600;
      transition: all 0.3s ease;
      cursor: default;
    }
    .badge.active {
      background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%);
      color: white;
      box-shadow: 0 5px 15px rgba(76, 175, 80, 0.4);
    }
    .badge.inactive {
      background: #e0e0e0;
      color: #666;
    }
    .buttons-container {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 15px;
      margin-top: 20px;
    }
    .btn {
      padding: 15px 30px;
      border: none;
      border-radius: 12px;
      font-size: 1em;
      font-weight: 600;
      cursor: pointer;
      text-decoration: none;
      display: block;
      text-align: center;
      transition: all 0.3s ease;
      color: white;
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
    }
    .btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(0,0,0,0.3);
    }
    .btn:active {
      transform: translateY(0);
    }
    .btn-auto {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    }
    .btn-normal {
      background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%);
    }
    .btn-noturno {
      background: linear-gradient(135deg, #ff9800 0%, #f57c00 100%);
    }
    .info-section {
      background: #f8f9fa;
      padding: 20px;
      border-radius: 15px;
      margin-top: 20px;
    }
    .info-section code {
      background: #e9ecef;
      padding: 5px 10px;
      border-radius: 5px;
      font-family: 'Courier New', monospace;
      color: #d63384;
    }
    .semaforo-visual {
      display: flex;
      justify-content: center;
      gap: 30px;
      margin: 30px 0;
      flex-wrap: wrap;
    }
    .semaforo {
      width: 80px;
      height: 200px;
      background: #2c3e50;
      border-radius: 10px;
      padding: 10px;
      display: flex;
      flex-direction: column;
      gap: 10px;
      box-shadow: 0 5px 15px rgba(0,0,0,0.3);
    }
    .luz {
      flex: 1;
      border-radius: 50%;
      background: #1a1a1a;
      transition: all 0.3s ease;
      box-shadow: inset 0 0 20px rgba(0,0,0,0.5);
    }
    .luz.vermelho.on { background: #e74c3c; box-shadow: 0 0 20px #e74c3c, inset 0 0 20px rgba(231,76,60,0.5); }
    .luz.amarelo.on { background: #f39c12; box-shadow: 0 0 20px #f39c12, inset 0 0 20px rgba(243,156,18,0.5); }
    .luz.verde.on { background: #27ae60; box-shadow: 0 0 20px #27ae60, inset 0 0 20px rgba(39,174,96,0.5); }
    .semaforo-label {
      text-align: center;
      margin-top: 10px;
      font-weight: 600;
      color: white;
    }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    .luz.piscando {
      animation: pulse 1s infinite;
    }
    @media (max-width: 600px) {
      .header h1 { font-size: 2em; }
      .status-grid { grid-template-columns: 1fr; }
      .buttons-container { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🚦 Semáforo Inteligente</h1>
      <p>Sistema de Controle Inteligente de Tráfego</p>
    </div>

    <div class="card">
      <div class="status-grid">
        <div class="status-item" id="statusLuminosidade">
          <h3>💡 Luminosidade</h3>
          <div class="value" id="lux">)" + String(luxAtual) + R"(</div>
          <div style="font-size: 0.8em; margin-top: 5px; opacity: 0.7;">LDR Sensor</div>
        </div>
        <div class="status-item" id="statusModo">
          <h3>⚙️ Modo Atual</h3>
          <div class="value" id="modoAtual">)" + modoAtual + R"(</div>
          <div style="font-size: 0.8em; margin-top: 5px; opacity: 0.7;">Estado do Sistema</div>
        </div>
      </div>

      <div class="luminosidade-container">
        <div class="luminosidade-label">
          <span>Nível de Luminosidade</span>
          <span id="luxPercent">0%</span>
        </div>
        <div class="progress-bar">
          <div class="progress-fill" id="progressFill" style="width: 0%"></div>
        </div>
        <div style="display: flex; justify-content: space-between; margin-top: 5px; font-size: 0.8em; opacity: 0.7;">
          <span>Escuro (0)</span>
          <span>Claro (5000)</span>
        </div>
      </div>

      <div class="modo-badges">
        <span class="badge" id="badgeAuto">🤖 Automático</span>
        <span class="badge" id="badgeNormal">☀️ Normal</span>
        <span class="badge" id="badgeNoturno">🌙 Noturno</span>
      </div>
    </div>

    <div class="card">
      <h2 style="margin-bottom: 20px; text-align: center;">Controle de Modos</h2>
      <div class="buttons-container">
        <a href="/auto" class="btn btn-auto">🤖 Modo Automático</a>
        <a href="/normal" class="btn btn-normal">☀️ Modo Normal</a>
        <a href="/noturno" class="btn btn-noturno">🌙 Modo Noturno</a>
      </div>
    </div>

    <div class="card">
      <div class="semaforo-visual">
        <div>
          <div class="semaforo">
            <div class="luz vermelho" id="s1-red"></div>
            <div class="luz amarelo" id="s1-yellow"></div>
            <div class="luz verde" id="s1-green"></div>
          </div>
          <div class="semaforo-label">Semáforo 1</div>
        </div>
        <div>
          <div class="semaforo">
            <div class="luz vermelho" id="s2-red"></div>
            <div class="luz amarelo" id="s2-yellow"></div>
            <div class="luz verde" id="s2-green"></div>
          </div>
          <div class="semaforo-label">Semáforo 2</div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="info-section">
        <h3 style="margin-bottom: 10px;">📡 API Endpoint</h3>
        <p style="margin-bottom: 10px;">Endpoint JSON para integração:</p>
        <code>/status</code>
        <p style="margin-top: 10px; font-size: 0.9em; opacity: 0.7;">
          Use este endpoint para dashboards Web ou integração futura com MQTT.
        </p>
      </div>
    </div>
  </div>

  <script>
    async function atualizar() {
      try {
        const response = await fetch('/status');
        const data = await response.json();
        
        // Atualizar luminosidade
        document.getElementById('lux').textContent = data.luminosidade;
        const percent = Math.min(100, (data.luminosidade / 5000) * 100);
        document.getElementById('luxPercent').textContent = Math.round(percent) + '%';
        document.getElementById('progressFill').style.width = percent + '%';
        
        // Atualizar modo
        const modoTexto = data.modoAuto ? 'Automático' : (data.modoNoturno ? 'Noturno' : 'Normal');
        document.getElementById('modoAtual').textContent = modoTexto;
        
        // Atualizar badges
        document.getElementById('badgeAuto').className = 'badge ' + (data.modoAuto ? 'active' : 'inactive');
        document.getElementById('badgeNormal').className = 'badge ' + (!data.modoNoturno && !data.modoAuto ? 'active' : 'inactive');
        document.getElementById('badgeNoturno').className = 'badge ' + (data.modoNoturno && !data.modoAuto ? 'active' : 'inactive');
        
        // Atualizar status items
        document.getElementById('statusLuminosidade').classList.toggle('active', true);
        document.getElementById('statusModo').classList.toggle('active', true);
        
        // Simulação visual dos semáforos (baseado no modo)
        atualizarSemaforos(data.modoNoturno);
      } catch (error) {
        console.error('Erro ao atualizar:', error);
      }
    }
    
    function atualizarSemaforos(noturno) {
      // Limpar todos
      document.querySelectorAll('.luz').forEach(l => {
        l.classList.remove('on', 'piscando');
      });
      
      if (noturno) {
        // Modo noturno: amarelo piscando
        document.getElementById('s1-yellow').classList.add('on', 'piscando');
        document.getElementById('s2-yellow').classList.add('on', 'piscando');
      } else {
        // Modo normal: simulação básica (ciclo completo seria mais complexo)
        // Por simplicidade, mostra verde no S1 e vermelho no S2
        document.getElementById('s1-green').classList.add('on');
        document.getElementById('s2-red').classList.add('on');
      }
    }
    
    // Atualizar a cada 2 segundos
    setInterval(atualizar, 2000);
    window.onload = atualizar;
  </script>
</body>
</html>
)";
  
  server.send(200, "text/html", html);
  Serial.println("[HTTP] Resposta enviada: 200 OK");
}

void setAuto()    { Serial.println("[HTTP] Requisicao recebida: /auto"); controlador.setModoAuto();   server.sendHeader("Location", "/"); server.send(303); }
void setNormal()  { Serial.println("[HTTP] Requisicao recebida: /normal"); controlador.setModoNormal(); server.sendHeader("Location", "/"); server.send(303); }
void setNoturno() { Serial.println("[HTTP] Requisicao recebida: /noturno"); controlador.setModoNoturno();server.sendHeader("Location", "/"); server.send(303); }

void handleStatus() {
  Serial.println("[HTTP] Requisicao recebida: /status");
  const auto& telemetria = controlador.getTelemetria();
  String json = "{";
  json += "\"luminosidade\":" + String(telemetria.luz) + ",";
  json += "\"modoAuto\":" + String(telemetria.autoAtivo ? "true" : "false") + ",";
  json += "\"modoNoturno\":" + String(telemetria.noturnoAtivo ? "true" : "false") + ",";
  json += "\"timestamp\":" + String(telemetria.timestamp);
  json += "}";
  server.send(200, "application/json", json);
  Serial.printf("[HTTP] JSON enviado: luz=%d, auto=%s, noturno=%s\n", 
                telemetria.luz, 
                telemetria.autoAtivo ? "true" : "false",
                telemetria.noturnoAtivo ? "true" : "false");
}
// ======================================================
// ======================== SETUP ========================
// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("  SEMAFORO INTELIGENTE - INICIANDO");
  Serial.println("========================================\n");
  
  Serial.println("[Setup] Inicializando controlador...");
  controlador.begin();
  Serial.println("[Setup] Limites LDR configurados:");
  Serial.println("  - Entrar modo NOTURNO: < 1800 (faixa: 0-2000)");
  Serial.println("  - Sair modo NOTURNO:  > 2200 (faixa: 2000-5000)");
  
  Serial.println("[Setup] Configurando Access Point...");
  bool apOk = WiFi.softAP(ssid, password);
  if (apOk) {
    Serial.print("[Setup] AP criado com sucesso! SSID: ");
    Serial.println(ssid);
    Serial.print("[Setup] IP do Access Point: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[Setup] ERRO: Falha ao criar Access Point!");
  }
  
  Serial.println("[Setup] Configurando rotas HTTP...");
  server.on("/", handleRoot);
  server.on("/auto", setAuto);
  server.on("/normal", setNormal);
  server.on("/noturno", setNoturno);
  server.on("/status", handleStatus);
  
  Serial.println("[Setup] Iniciando servidor HTTP na porta 80...");
  server.begin();
  Serial.println("[Setup] Servidor HTTP iniciado com sucesso!");
  
  Serial.println("[Setup] Configurando cliente MQTT...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callbackMQTT);
  Serial.print("[Setup] Broker MQTT: ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(mqtt_port);
  Serial.print("[Setup] Topico telemetria: ");
  Serial.println(mqtt_topic_telemetria);
  Serial.print("[Setup] Topico comandos: ");
  Serial.println(mqtt_topic_comandos);
  
  // Tentar conectar ao broker MQTT (não bloqueia se não conseguir)
  Serial.println("[Setup] Tentando conectar ao broker MQTT...");
  if (mqttClient.connect(mqtt_client_id)) {
    Serial.println("[Setup] Conectado ao broker MQTT com sucesso!");
    if (mqttClient.subscribe(mqtt_topic_comandos)) {
      Serial.print("[Setup] Inscrito no topico de comandos: ");
      Serial.println(mqtt_topic_comandos);
    }
  } else {
    Serial.println("[Setup] AVISO: Nao foi possivel conectar ao broker MQTT.");
    Serial.println("[Setup] O sistema continuara funcionando, mas sem MQTT.");
    Serial.println("[Setup] Verifique se o Mosquitto esta rodando no IP configurado.");
  }
  
  Serial.println("\n========================================");
  Serial.println("  SISTEMA PRONTO!");
  Serial.println("========================================\n");
}
// ======================================================
// ========================= LOOP ========================
// ======================================================
void loop() {
  static unsigned long ultimoHeartbeat = 0;
  
  server.handleClient();
  controlador.atualizar();
  
  // Heartbeat a cada 10 segundos para confirmar que está rodando
  if (millis() - ultimoHeartbeat >= 10000) {
    Serial.println("[Heartbeat] Sistema operacional");
    ultimoHeartbeat = millis();
  }
}