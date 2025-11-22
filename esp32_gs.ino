#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>

// ==== PROTÓTIPOS DAS FUNÇÕES ====
void connectWiFi();
void handleButton();
void handleBuzzer();
void readSensorsAndUpdate();
void updateLeds(int score);
void sendMetrics(float temp, float hum, int ldr, int noise,
                 int score, bool working, unsigned long workMinutes);
int readNoiseAveraged();

// ====== CONFIG REDE ======
const char* WIFI_SSID     = "BONI_NET";
const char* WIFI_PASSWORD = "20092003";
const char* SERVER_URL    = "http://192.168.0.79:3000/api/metrics";
const char* USER_ID       = "orbita-001";

// ====== PINOS ESP32 ======
#define DHTPIN   4
#define DHTTYPE  DHT11

const int LDR_PIN     = 34;
const int NOISE_PIN   = 35;
const int BUTTON_PIN  = 18;
const int BUZZER_PIN  = 19;
const int LED_R = 25;
const int LED_Y = 26;
const int LED_G = 27;

// ====== OBJETO DHT ======
DHT dht(DHTPIN, DHTTYPE);

// ====== AJUSTES GERAIS (MODO DEMO) ======
const unsigned long LONG_SESSION_MINUTES = 2;   // só para exibir minutos (não entra no score)
const unsigned long readInterval         = 3000; // ms, envia a cada 3s

// ====== CONTROLE DE SESSÃO ======
bool workingSession       = false;
unsigned long workStartMillis = 0;

// ====== CONTROLE DO BOTÃO ======
bool     buttonWasPressed   = false;
unsigned long buttonPressTime = 0;
const unsigned long LONG_PRESS_MS   = 2000; // 2s
const unsigned long MIN_PRESS_MS    = 50;   // ignora toques muito rápidos

// ====== TEMPO DE LEITURA ======
unsigned long lastReadMillis = 0;

// ====== BUZZER ======
bool buzzerOn    = false;
bool buzzerMuted = false;
unsigned long lastBeepMillis = 0;
const unsigned long beepInterval = 800;
const unsigned long beepDuration  = 200;

// ====== SUAVIZAÇÃO (apenas para RUÍDO) ======
float noiseSmooth = -1.0f;
const float NOISE_SMOOTH_ALPHA = 0.5f;  // 0.5 = suavização moderada

// ====== CALIBRAÇÃO DO LDR ======
// Ajuste esses valores olhando o LDRraw no Serial:
// - LDR_DARK_RAW  -> valor com ambiente o mais escuro possível (pra você está ~620, pode pôr 700).
// - LDR_BRIGHT_RAW -> valor com luz forte (lanterna, lampada).
const int LDR_DARK_RAW   = 700;   // "0% luz" no seu ambiente (ajuste se precisar)
const int LDR_BRIGHT_RAW = 3000;  // "100% luz" no seu ambiente (ajuste se precisar)

// ---------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LDR_PIN, INPUT);
  pinMode(NOISE_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_Y, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  updateLeds(100);   // começa verde
  connectWiFi();

  Serial.println("=== ORBITA DESK - ESP32 INICIADO (MODO DEMO – SCORE = LUZ+SOM) ===");
  Serial.println("Botao:");
  Serial.println("  • Clique curto  -> inicia/encerra sessao de trabalho");
  Serial.println("  • Clique longo  -> mute/unmute do buzzer");
}

// ---------------------------------------------------
void loop() {
  handleButton();
  handleBuzzer();

  unsigned long now = millis();
  if (now - lastReadMillis >= readInterval) {
    lastReadMillis = now;
    readSensorsAndUpdate();
  }
}

// ====================== FUNÇÕES =====================

void connectWiFi() {
  Serial.print("Conectando ao WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha ao conectar WiFi");
  }
}

// Botão: clique curto -> inicia/encerra sessão
//        clique longo -> mute/unmute buzzer
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (reading == LOW && !buttonWasPressed) {
    buttonWasPressed  = true;
    buttonPressTime   = now;
  }

  if (reading == HIGH && buttonWasPressed) {
    buttonWasPressed = false;
    unsigned long pressDuration = now - buttonPressTime;

    if (pressDuration < MIN_PRESS_MS) return;

    if (pressDuration >= LONG_PRESS_MS) {
      // LONG PRESS -> mute/unmute buzzer
      buzzerMuted = !buzzerMuted;
      if (buzzerMuted) {
        Serial.println("[BOTAO] LONG PRESS -> Buzzer MUTADO");
      } else {
        Serial.println("[BOTAO] LONG PRESS -> Buzzer DESMUTADO");
      }
    } else {
      // SHORT PRESS -> inicia/encerra sessão
      workingSession = !workingSession;
      if (workingSession) {
        workStartMillis = now;
        Serial.println("[BOTAO] SHORT PRESS -> Sessao de trabalho INICIADA");
      } else {
        Serial.println("[BOTAO] SHORT PRESS -> Sessao de trabalho ENCERRADA");
        buzzerOn = false;
        digitalWrite(BUZZER_PIN, LOW);
        updateLeds(100);  // volta pro verde
      }
    }
  }
}

// Buzzer: bips periódicos enquanto buzzerOn == true e não estiver mutado
void handleBuzzer() {
  static bool beepOn = false;
  unsigned long now = millis();

  bool shouldBeep = buzzerOn && !buzzerMuted;

  if (!shouldBeep) {
    beepOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  if (!beepOn) {
    beepOn = true;
    lastBeepMillis = now;
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    if (now - lastBeepMillis >= beepDuration) {
      digitalWrite(BUZZER_PIN, LOW);
      if (now - lastBeepMillis >= beepInterval) {
        beepOn = false;
        lastBeepMillis = now;
      }
    }
  }
}

// Lê ruído com média
int readNoiseAveraged() {
  const int N = 40;
  long sum = 0;
  for (int i = 0; i < N; i++) {
    sum += analogRead(NOISE_PIN);
    delayMicroseconds(800);
  }
  return (int)(sum / N);
}

// Lê sensores, calcula score (SÓ LUZ E SOM), atualiza LEDs/buzzer e envia HTTP
void readSensorsAndUpdate() {
  if (!workingSession) {
    Serial.println("Sessao NAO iniciada. Aguardando clique curto no botao...");
    return;
  }

  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Erro ao ler DHT11");
    return;
  }

  int ldrRaw   = analogRead(LDR_PIN);
  int noiseRaw = readNoiseAveraged();

  // ===== CALIBRAÇÃO DA LUZ =====
  int ldrCal = ldrRaw;
  if (ldrCal < LDR_DARK_RAW)   ldrCal = LDR_DARK_RAW;
  if (ldrCal > LDR_BRIGHT_RAW) ldrCal = LDR_BRIGHT_RAW;

  float lightPct = ((float)(ldrCal - LDR_DARK_RAW) * 100.0f) /
                   (float)(LDR_BRIGHT_RAW - LDR_DARK_RAW);
  // ===== FIM CALIBRAÇÃO =====

  // RUÍDO: com suavização leve
  if (noiseSmooth < 0) noiseSmooth = noiseRaw;
  else noiseSmooth = (1.0f - NOISE_SMOOTH_ALPHA) * noiseSmooth + NOISE_SMOOTH_ALPHA * noiseRaw;

  float noisePct = (noiseSmooth / 4095.0f) * 100.0f;

  unsigned long workMillis = 0;
  if (workingSession) workMillis = (millis() - workStartMillis);
  unsigned long workMinutesInt = workMillis / 60000UL;
  float workMinutesFloat = workMillis / 60000.0f;

  // =========================
  // SCORE SÓ COM LUZ E SOM
  // =========================
  int score = 100;
  int lightPenalty  = 0;
  int noisePenalty  = 0;

  // Luz: penaliza se muito escuro (agora já calibrado 0–100%)
  if (lightPct < 10) {
    lightPenalty = 25;
  } else if (lightPct < 25) {
    lightPenalty = 10;
  }

  // Ruído: muito alto > 90, moderado > 75
  if (noisePct > 90) {
    noisePenalty = 20;
  } else if (noisePct > 75) {
    noisePenalty = 5;
  }

  // Aplica SOMENTE luz + som
  score -= (lightPenalty + noisePenalty);
  if (score < 0)   score = 0;
  if (score > 100) score = 100;

  updateLeds(score);
  buzzerOn = (score < 45);   // só toca se ficar bem ruim

  // LOG – agora mostra LDR calibrado também
  Serial.print("Temp:");
  Serial.print(temp, 1);
  Serial.print("C Hum:");
  Serial.print(hum, 1);
  Serial.print("% | LDRraw:");
  Serial.print(ldrRaw);
  Serial.print(" LDRcal:");
  Serial.print(ldrCal);
  Serial.print(" (");
  Serial.print(lightPct, 1);
  Serial.print("%) | NoiseRaw:");
  Serial.print(noiseRaw);
  Serial.print(" Nsmooth:");
  Serial.print(noiseSmooth, 1);
  Serial.print(" (");
  Serial.print(noisePct, 1);
  Serial.print("%)");

  Serial.print(" | Score:");
  Serial.print(score);
  Serial.print(" Sessao:");
  Serial.print(workingSession ? "ON" : "OFF");
  Serial.print(" (");
  Serial.print(workMinutesFloat, 2);
  Serial.print(" min)  BuzzerMuted:");
  Serial.print(buzzerMuted ? "SIM" : "NAO");

  Serial.print(" | Penalties(L/N) => L:");
  Serial.print(lightPenalty);
  Serial.print(" N:");
  Serial.println(noisePenalty);

  sendMetrics(temp, hum, ldrRaw, noiseRaw, score, workingSession, workMinutesInt);
}

void updateLeds(int score) {
  // thresholds mais sensíveis: muda de cor mais cedo
  if (score >= 90) {
    digitalWrite(LED_G, HIGH);
    digitalWrite(LED_Y, LOW);
    digitalWrite(LED_R, LOW);
  } else if (score >= 70) {
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_Y, HIGH);
    digitalWrite(LED_R, LOW);
  } else {
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_Y, LOW);
    digitalWrite(LED_R, HIGH);
  }
}

// Envia os dados para o backend ÓRBITA via HTTP POST
void sendMetrics(float temp, float hum, int ldr, int noise,
                 int score, bool working, unsigned long workMinutes) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, tentando reconectar...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Nao foi possivel enviar (sem WiFi)");
      return;
    }
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"userId\":\"" + String(USER_ID) + "\",";
  json += "\"temperature\":" + String(temp, 1) + ",";
  json += "\"humidity\":" + String(hum, 1) + ",";
  json += "\"light\":" + String(ldr) + ",";
  json += "\"noise\":" + String(noise) + ",";
  json += "\"score\":" + String(score) + ",";
  json += "\"working\":" + String(working ? "true" : "false") + ",";
  json += "\"workMinutes\":" + String(workMinutes) + ",";
  json += "\"timestamp\":" + String(millis());
  json += "}";

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    Serial.print("HTTP POST -> codigo: ");
    Serial.println(httpCode);
  } else {
    Serial.print("Erro HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}
