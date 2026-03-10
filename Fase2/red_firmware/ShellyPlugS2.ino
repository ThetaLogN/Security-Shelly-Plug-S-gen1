/**
 * ============================================================
 *  SHELLY PLUG S GEN1 — Firmware Custom per Arduino IDE
 * ============================================================
 *  Board    : ESP8266 (Generic ESP8266 Module)
 *  Chip     : ESP8266EX
 *  Misure   : ADE7953 via I2C (SDA=4, SCL=5)
 *
 *  LIBRERIE RICHIESTE (Library Manager):
 *    - PubSubClient       (Nick O'Leary)       >= 2.8
 *    - ArduinoJson        (Benoit Blanchon)     >= 6.x
 *    - ESP8266HTTPUpdate  (inclusa nell'ESP8266 core)
 *    - Wire               (inclusa nell'ESP8266 core)
 *
 *  BOARD MANAGER:
 *    URL: http://arduino.esp8266.com/stable/package_esp8266com_index.json
 *    Board: "Generic ESP8266 Module"
 *    Flash: 2MB (FS: 512KB, OTA: ~768KB)
 *
 *  CONFIGURAZIONE:
 *    Modifica la sezione "USER CONFIG" qui sotto.
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Ticker.h>

// ============================================================
//  USER CONFIG — modifica questi valori
// ============================================================
// WiFi
const char* WIFI_SSID     = "TUO_SSID";
const char* WIFI_PASSWORD = "TUA_PASSWORD";

// MQTT
const char* MQTT_SERVER   = "192.168.122.1";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "utente";
const char* MQTT_PASSWORD = "password";
const char* MQTT_BASE     = "shellies/plug-s";   // topic base

// OTA
const char* OTA_URL          = "";                // URL del file .bin (lascia vuoto per disabilitare)
const int   OTA_INTERVAL_SEC = 3600;              // controllo ogni ora
const char* FW_VERSION       = "2.0.0";

// Intervallo lettura energia (secondi)
const int   ENERGY_INTERVAL_SEC = 60;

// ============================================================
//  PIN MAP — Shelly Plug S Gen1
// ============================================================
#define PIN_RELAY      15   // Relay (HIGH = ON)
#define PIN_BTN        13   // Pulsante (LOW = premuto)
#define PIN_LED_GREEN   0   // LED verde  (active LOW)
#define PIN_LED_RED     2   // LED rosso  (active LOW)
#define I2C_SDA         4
#define I2C_SCL         5

// ============================================================
//  ADE7953 — Registri e calibrazione
// ============================================================
#define ADE7953_ADDR   0x38
#define REG_VRMS       0x21C
#define REG_IRMS_A     0x21A
#define REG_WATT_A     0x212
#define REG_UNLOCK     0x102   // registro sblocco
#define REG_CONFIG     0x102

// Fattori di scala calibrati per Shelly Plug S Gen1
const float V_SCALE = 0.0000382f;
const float I_SCALE = 0.00000949f;
const float W_SCALE = 0.00000218f;

// ============================================================
//  TOPIC MQTT
// ============================================================
char TOPIC_RELAY_STATE[64];
char TOPIC_RELAY_CMD[64];
char TOPIC_STATUS[64];
char TOPIC_ENERGY[64];
char TOPIC_VOLTAGE[64];
char TOPIC_CURRENT[64];
char TOPIC_POWER[64];
char TOPIC_OTA_STATUS[64];
char TOPIC_OTA_PROGRESS[64];
char TOPIC_OTA_CMD[64];
char TOPIC_VERSION[64];
char TOPIC_ONLINE[64];
char MQTT_CLIENT_ID[32];

// ============================================================
//  STATO GLOBALE
// ============================================================
bool relayState   = false;
bool mqttReady    = false;
bool otaRunning   = false;

unsigned long lastEnergyMs   = 0;
unsigned long lastOtaCheckMs = 0;
unsigned long lastReconnMs   = 0;
unsigned long lastMqttMs     = 0;

// Debounce pulsante
volatile bool btnPressed     = false;
unsigned long btnLastMs      = 0;
const unsigned long DEBOUNCE = 200;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
Ticker       ledBlinker;

// ============================================================
//  UTILITY LED
// ============================================================
void setLed(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);  // active LOW
}

void updateLeds() {
  if (!mqttReady) return;
  setLed(PIN_LED_GREEN, relayState);
  setLed(PIN_LED_RED,  !relayState);
}

void startBlinkRed() {
  ledBlinker.attach_ms(500, []() {
    static bool s = false;
    setLed(PIN_LED_RED, s);
    s = !s;
  });
}

void startBlinkGreen() {
  ledBlinker.attach_ms(300, []() {
    static bool s = false;
    setLed(PIN_LED_GREEN, s);
    s = !s;
  });
}

void stopBlink() {
  ledBlinker.detach();
  updateLeds();
}

// ============================================================
//  RELAY
// ============================================================
void setRelay(bool on) {
  relayState = on;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  updateLeds();

  Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");

  if (mqttReady) {
    mqtt.publish(TOPIC_RELAY_STATE, on ? "1" : "0", true);

    StaticJsonDocument<128> doc;
    doc["relay"]  = on;
    doc["uptime"] = millis() / 1000;
    char buf[128];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_STATUS, buf);
  }
}

void toggleRelay() {
  setRelay(!relayState);
}

// ============================================================
//  PULSANTE — interrupt
// ============================================================
IRAM_ATTR void btnISR() {
  btnPressed = true;
}

void handleButton() {
  if (!btnPressed) return;
  btnPressed = false;
  unsigned long now = millis();
  if (now - btnLastMs < DEBOUNCE) return;
  btnLastMs = now;
  if (digitalRead(PIN_BTN) == LOW) {
    Serial.println("[BTN] Premuto");
    toggleRelay();
  }
}

// ============================================================
//  ADE7953 — lettura I2C
// ============================================================
void ade7953Init() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(100);

  // Sblocco chip (write 0xAD a registro 0xFE)
  Wire.beginTransmission(ADE7953_ADDR);
  Wire.write(0x00);
  Wire.write(0xFE);
  Wire.write(0xAD);
  Wire.endTransmission();
  delay(10);

  Serial.println("[ADE7953] Inizializzato");
}

int32_t ade7953ReadReg(uint16_t reg) {
  Wire.beginTransmission(ADE7953_ADDR);
  Wire.write((reg >> 8) & 0xFF);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) {
    Serial.printf("[ADE7953] Errore write reg 0x%03X\n", reg);
    return 0;
  }

  Wire.requestFrom((uint8_t)ADE7953_ADDR, (uint8_t)3);
  if (Wire.available() < 3) {
    Serial.println("[ADE7953] Errore read: dati insufficienti");
    return 0;
  }

  uint32_t val = ((uint32_t)Wire.read() << 16) |
                 ((uint32_t)Wire.read() << 8)  |
                  (uint32_t)Wire.read();

  // Converti in signed 24-bit
  if (val & 0x800000) val |= 0xFF000000;
  return (int32_t)val;
}

void readAndPublishEnergy() {
  int32_t rawV = ade7953ReadReg(REG_VRMS);
  int32_t rawI = ade7953ReadReg(REG_IRMS_A);
  int32_t rawW = ade7953ReadReg(REG_WATT_A);

  float voltage  = abs(rawV) * V_SCALE;
  float current  = abs(rawI) * I_SCALE;
  float power    = abs(rawW) * W_SCALE;
  float apparent = voltage * current;
  float pf       = (apparent > 0.01f) ? (power / apparent) : 0.0f;
  if (pf > 1.0f) pf = 1.0f;

  Serial.printf("[ENERGY] V:%.2fV  I:%.3fA  P:%.2fW  PF:%.3f\n",
                voltage, current, power, pf);

  if (!mqttReady) return;

  // Topic separati
  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f", voltage);
  mqtt.publish(TOPIC_VOLTAGE, buf);

  snprintf(buf, sizeof(buf), "%.3f", current);
  mqtt.publish(TOPIC_CURRENT, buf);

  snprintf(buf, sizeof(buf), "%.2f", power);
  mqtt.publish(TOPIC_POWER, buf);

  // Topic JSON completo
  StaticJsonDocument<256> doc;
  doc["voltage"]  = round(voltage  * 100) / 100.0;
  doc["current"]  = round(current  * 1000) / 1000.0;
  doc["power"]    = round(power    * 100) / 100.0;
  doc["apparent"] = round(apparent * 100) / 100.0;
  doc["pf"]       = round(pf       * 1000) / 1000.0;
  doc["uptime"]   = millis() / 1000;

  char jsonBuf[256];
  serializeJson(doc, jsonBuf);
  mqtt.publish(TOPIC_ENERGY, jsonBuf);
}

// ============================================================
//  WIFI
// ============================================================
void wifiConnect() {
  Serial.printf("\n[WiFi] Connessione a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  startBlinkRed();

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connesso! IP: %s\n", WiFi.localIP().toString().c_str());
    stopBlink();
  } else {
    Serial.println("\n[WiFi] Connessione fallita, riavvio...");
    delay(3000);
    ESP.restart();
  }
}

// ============================================================
//  MQTT — callback ricezione messaggi
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  char msg[64] = {0};
  memcpy(msg, payload, min((unsigned int)63, len));
  Serial.printf("[MQTT] RX [%s] = %s\n", topic, msg);

  // Comando relay
  if (strcmp(topic, TOPIC_RELAY_CMD) == 0) {
    if (strcmp(msg, "on")  == 0 || strcmp(msg, "1") == 0) setRelay(true);
    if (strcmp(msg, "off") == 0 || strcmp(msg, "0") == 0) setRelay(false);
    if (strcmp(msg, "toggle") == 0)                        toggleRelay();
    return;
  }

  // Comando OTA
  if (strcmp(topic, TOPIC_OTA_CMD) == 0) {
    if (strcmp(msg, "update") == 0) {
      Serial.println("[OTA] Aggiornamento manuale richiesto via MQTT");
      lastOtaCheckMs = 0;  // forza check immediato nel loop
    }
    if (strcmp(msg, "status") == 0) {
      StaticJsonDocument<128> doc;
      doc["state"]   = otaRunning ? "running" : "idle";
      doc["version"] = FW_VERSION;
      doc["uptime"]  = millis() / 1000;
      char buf[128];
      serializeJson(doc, buf);
      mqtt.publish(TOPIC_OTA_STATUS, buf);
    }
    return;
  }

  // Query stato
  if (strcmp(topic, (String(MQTT_BASE) + "/status/query").c_str()) == 0) {
    StaticJsonDocument<128> doc;
    doc["relay"]    = relayState;
    doc["uptime"]   = millis() / 1000;
    doc["free_ram"] = ESP.getFreeHeap();
    char buf[128];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_STATUS, buf);
  }
}

// ============================================================
//  MQTT — connessione e subscribe
// ============================================================
void mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.printf("[MQTT] Connessione a %s:%d...\n", MQTT_SERVER, MQTT_PORT);
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);
  mqtt.setBufferSize(512);

  // LWT (Last Will Testament) — messaggio offline automatico
  char lwtTopic[64];
  snprintf(lwtTopic, sizeof(lwtTopic), "%s/online", MQTT_BASE);

  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                   lwtTopic, 0, true, "0")) {
    mqttReady = true;
    Serial.println("[MQTT] Connesso!");
    stopBlink();
    updateLeds();

    // Subscribe
    mqtt.subscribe(TOPIC_RELAY_CMD);
    mqtt.subscribe(TOPIC_OTA_CMD);
    mqtt.subscribe((String(MQTT_BASE) + "/status/query").c_str());

    // Annunci
    mqtt.publish(TOPIC_ONLINE,  "1", true);
    mqtt.publish(TOPIC_VERSION, FW_VERSION, true);

    // Stato relay corrente
    mqtt.publish(TOPIC_RELAY_STATE, relayState ? "1" : "0", true);

    Serial.println("[MQTT] Subscribe completati");
  } else {
    mqttReady = false;
    Serial.printf("[MQTT] Fallito, rc=%d\n", mqtt.state());
    startBlinkRed();
  }
}

// ============================================================
//  OTA — aggiornamento HTTP
// ============================================================
void publishOtaStatus(const char* state, const char* extra = "") {
  if (!mqttReady) return;
  StaticJsonDocument<128> doc;
  doc["state"]   = state;
  doc["version"] = FW_VERSION;
  doc["extra"]   = extra;
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_OTA_STATUS, buf);
  mqtt.loop();  // flush immediato
}

void checkForUpdate() {
  if (strlen(OTA_URL) == 0) {
    Serial.println("[OTA] URL non configurato, skip");
    return;
  }
  if (otaRunning) {
    Serial.println("[OTA] Già in corso, skip");
    return;
  }

  otaRunning = true;
  Serial.printf("[OTA] Controllo aggiornamento: %s\n", OTA_URL);
  publishOtaStatus("checking", OTA_URL);
  startBlinkGreen();

  // Callback progresso
  ESPhttpUpdate.onProgress([](int cur, int total) {
    int pct = (total > 0) ? (cur * 100 / total) : 0;
    Serial.printf("[OTA] Progresso: %d%%\n", pct);
    if (mqttReady) {
      char buf[32];
      snprintf(buf, sizeof(buf), "{\"percent\":%d}", pct);
      mqtt.publish(TOPIC_OTA_PROGRESS, buf);
      mqtt.loop();
    }
  });

  // Disabilita relay durante OTA per sicurezza
  bool prevRelay = relayState;
  setRelay(false);

  WiFiClient otaClient;
  t_httpUpdate_return ret = ESPhttpUpdate.update(otaClient, OTA_URL);

  // Se arriviamo qui, l'aggiornamento è fallito
  // (in caso di successo il device si riavvia automaticamente)
  otaRunning = false;
  stopBlink();

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] Errore: (%d) %s\n",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      publishOtaStatus("error", ESPhttpUpdate.getLastErrorString().c_str());
      setRelay(prevRelay);  // ripristina stato relay
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] Nessun aggiornamento disponibile");
      publishOtaStatus("no_update", "Firmware gia' aggiornato");
      setRelay(prevRelay);
      break;

    case HTTP_UPDATE_OK:
      // Non raggiunto: il device si riavvia prima
      publishOtaStatus("rebooting", "Aggiornamento completato");
      break;
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== Shelly Plug S Gen1 - Firmware Custom v" + String(FW_VERSION) + " ===");

  // Build topic strings
  snprintf(TOPIC_RELAY_STATE,  sizeof(TOPIC_RELAY_STATE),  "%s/relay/0",          MQTT_BASE);
  snprintf(TOPIC_RELAY_CMD,    sizeof(TOPIC_RELAY_CMD),    "%s/relay/0/command",   MQTT_BASE);
  snprintf(TOPIC_STATUS,       sizeof(TOPIC_STATUS),       "%s/status",            MQTT_BASE);
  snprintf(TOPIC_ENERGY,       sizeof(TOPIC_ENERGY),       "%s/energy",            MQTT_BASE);
  snprintf(TOPIC_VOLTAGE,      sizeof(TOPIC_VOLTAGE),      "%s/voltage",           MQTT_BASE);
  snprintf(TOPIC_CURRENT,      sizeof(TOPIC_CURRENT),      "%s/current",           MQTT_BASE);
  snprintf(TOPIC_POWER,        sizeof(TOPIC_POWER),        "%s/power",             MQTT_BASE);
  snprintf(TOPIC_OTA_STATUS,   sizeof(TOPIC_OTA_STATUS),   "%s/ota/status",        MQTT_BASE);
  snprintf(TOPIC_OTA_PROGRESS, sizeof(TOPIC_OTA_PROGRESS), "%s/ota/progress",      MQTT_BASE);
  snprintf(TOPIC_OTA_CMD,      sizeof(TOPIC_OTA_CMD),      "%s/ota/command",       MQTT_BASE);
  snprintf(TOPIC_VERSION,      sizeof(TOPIC_VERSION),      "%s/version",           MQTT_BASE);
  snprintf(TOPIC_ONLINE,       sizeof(TOPIC_ONLINE),       "%s/online",            MQTT_BASE);
  snprintf(MQTT_CLIENT_ID,     sizeof(MQTT_CLIENT_ID),     "shelly-plug-s-%06X",   ESP.getChipId());

  // GPIO
  pinMode(PIN_RELAY,     OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED,   OUTPUT);
  pinMode(PIN_BTN,       INPUT_PULLUP);

  digitalWrite(PIN_RELAY, LOW);
  setLed(PIN_LED_GREEN, false);
  setLed(PIN_LED_RED,   true);

  // Interrupt pulsante
  attachInterrupt(digitalPinToInterrupt(PIN_BTN), btnISR, CHANGE);

  // ADE7953
  ade7953Init();

  // WiFi
  wifiConnect();

  // MQTT
  mqttConnect();

  Serial.println("[SETUP] Completato");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // ── Pulsante ────────────────────────────────────────────────
  handleButton();

  // ── Reconnect WiFi ──────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastReconnMs > 10000) {
      lastReconnMs = now;
      mqttReady = false;
      Serial.println("[WiFi] Disconnesso, riconnessione...");
      startBlinkRed();
      WiFi.reconnect();
    }
    return;
  }

  // ── Reconnect MQTT ──────────────────────────────────────────
  if (!mqtt.connected()) {
    mqttReady = false;
    if (now - lastMqttMs > 5000) {
      lastMqttMs = now;
      Serial.println("[MQTT] Disconnesso, riconnessione...");
      startBlinkRed();
      mqttConnect();
    }
    return;
  }

  mqtt.loop();

  // ── Lettura energia ogni ENERGY_INTERVAL_SEC ────────────────
  if (now - lastEnergyMs >= (unsigned long)ENERGY_INTERVAL_SEC * 1000UL) {
    lastEnergyMs = now;
    readAndPublishEnergy();
  }

  // ── Check OTA ogni OTA_INTERVAL_SEC ─────────────────────────
  if (strlen(OTA_URL) > 0) {
    bool firstCheck = (lastOtaCheckMs == 0) && (now > 60000UL);
    bool periodic   = (lastOtaCheckMs > 0) &&
                      (now - lastOtaCheckMs >= (unsigned long)OTA_INTERVAL_SEC * 1000UL);

    if (firstCheck || periodic) {
      lastOtaCheckMs = now;
      checkForUpdate();
    }
  }

  // ── Heartbeat MQTT ogni 30s ──────────────────────────────────
  static unsigned long lastHeartMs = 0;
  if (now - lastHeartMs >= 30000UL) {
    lastHeartMs = now;
    StaticJsonDocument<128> doc;
    doc["relay"]    = relayState;
    doc["uptime"]   = now / 1000;
    doc["free_ram"] = ESP.getFreeHeap();
    doc["version"]  = FW_VERSION;
    char buf[128];
    serializeJson(doc, buf);
    mqtt.publish(TOPIC_STATUS, buf);
  }
}
