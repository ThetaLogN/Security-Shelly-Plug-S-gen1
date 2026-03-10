/**
 * ============================================================
 *  SHELLY PLUG S GEN1 — Firmware Custom (mod. Giorgio)
 * ============================================================
 *  Modifiche rispetto all'originale:
 *    - Rimossa funzione checkSafety() e soglie di sicurezza
 *    - Aggiunti topic MQTT per corrente, potenza e tensione
 *    - Pubblicazione energia ogni SEND_INTERVAL_MS (60s)
 *    - Aggiunti topic separati per ogni grandezza
 * ============================================================
 *  LIBRERIE RICHIESTE:
 *    - PubSubClient       (Nick O'Leary)
 *    - ArduinoJson        (Benoit Blanchon)
 *    - HLW8012            (Xose Pérez)
 *    - WiFiManager        (tzapu)
 * ============================================================
 */

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <HLW8012.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <math.h>

/* ================== HARDWARE ================== */
#define RELAY_PIN   15
#define SEL_PIN     12
#define CF1_PIN     14
#define CF_PIN       5
#define LED_PIN      0
#define BTN_PIN     13
#define ANALOG_PIN  A0

#define CURRENT_MODE HIGH

/* ================== CONFIGURAZIONE UDP ================== */
IPAddress serverIP(192, 168, 1, 100);
const uint16_t serverPort = 9999;

/* ================== CONFIGURAZIONE DINAMICA (MQTT/WIFI) ================== */
char mqtt_server[40] = "192.168.1.100";
char mqtt_port[6]    = "1883";
char device_id[32]   = "shellyplug-s-emulator";
bool shouldSaveConfig = false;

/* ================== OGGETTI GLOBALI ================== */
WiFiClient       wifiClient;
WiFiUDP          udp;
PubSubClient     mqtt(wifiClient);
HLW8012          hlw8012;
ESP8266WebServer server(80);

/* ================== VARIABILI RUNTIME ================== */
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 60000;   // pubblica energia ogni 60s
char payload[256];

/* ================== PARAMETRI NTC ================== */
const double TEMP_NOMINAL    = 25.0;
const double R_PULLUP         = 10000.0;  
const double R_NTC_NOMINAL    = 10000.0;   
const double BETA_COEFFICIENT = 3350.0;    

/* ================== TOPIC MQTT ================== */
// I topic vengono costruiti dinamicamente con device_id in setup()
char TOPIC_RELAY_STATE[64];
char TOPIC_RELAY_CMD[64];
char TOPIC_STATUS[64];
char TOPIC_ENERGY[64];
char TOPIC_POWER[64];
char TOPIC_CURRENT[64];
char TOPIC_VOLTAGE[64];
char TOPIC_TEMPERATURE[64];
char TOPIC_ONLINE[64];

/* ================== TEMPERATURA NTC ================== */
double getRealTemperature() {
  int rawADC = analogRead(ANALOG_PIN);
  if (rawADC >= 1023) return -273.0;
  if (rawADC <= 0)    return 999.0;

  double R_ntc   = ((double)rawADC * R_PULLUP) / (1024.0 - (double)rawADC);
  double steinhart = R_ntc / R_NTC_NOMINAL;
  steinhart = log(steinhart);
  steinhart /= BETA_COEFFICIENT;
  steinhart += 1.0 / (TEMP_NOMINAL + 273.15);
  steinhart  = 1.0 / steinhart;
  steinhart -= 273.15;
  return steinhart;
}

/* ================== INTERRUPT HLW8012 ================== */
void ICACHE_RAM_ATTR hlw8012_cf1_interrupt() { hlw8012.cf1_interrupt(); }
void ICACHE_RAM_ATTR hlw8012_cf_interrupt()  { hlw8012.cf_interrupt();  }

/* ================== GESTIONE CONFIGURAZIONE ================== */
void saveConfigCallback() {
  Serial.println("Configurazione modificata, necessario salvare");
  shouldSaveConfig = true;
}

void loadConfig() {
  if (LittleFS.begin()) {
    if (LittleFS.exists("/config.json")) {
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());
        if (!error) {
          if (json.containsKey("mqtt_server")) strcpy(mqtt_server, json["mqtt_server"]);
          if (json.containsKey("mqtt_port"))   strcpy(mqtt_port,   json["mqtt_port"]);
          if (json.containsKey("device_id"))   strcpy(device_id,   json["device_id"]);
        }
      }
    }
  }
}

void saveConfig() {
  Serial.println("Salvataggio configurazione...");
  DynamicJsonDocument json(1024);
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"]   = mqtt_port;
  json["device_id"]   = device_id;
  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) return;
  serializeJson(json, configFile);
  configFile.close();
}

/* ================== PUBBLICA STATO RELAY ================== */
void publishStatus() {
  if (!mqtt.connected()) return;
  bool on = digitalRead(RELAY_PIN);
  mqtt.publish(TOPIC_RELAY_STATE, on ? "1" : "0", true);

  StaticJsonDocument<128> doc;
  doc["ison"]   = on;
  doc["uptime"] = millis() / 1000;
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf);

  Serial.printf("[MQTT] Stato relay pubblicato: %s\n", on ? "ON" : "OFF");
}

/* ================================================================
 *  PUBBLICA ENERGIA — corrente, potenza, tensione ogni 60s
 * ================================================================ */
void publishEnergy() {
  if (!mqtt.connected()) return;

  float power   = hlw8012.getActivePower();     // Watt
  float current = hlw8012.getCurrent();          // Ampere
  float voltage = hlw8012.getVoltage();          // Volt
  float apparent = voltage * current;            // VA
  float pf      = (apparent > 0.01f) ? (power / apparent) : 0.0f;
  if (pf > 1.0f) pf = 1.0f;
  double temp   = getRealTemperature();

  Serial.printf("[ENERGY] V:%.2fV  I:%.3fA  P:%.2fW  PF:%.3f  T:%.1f°C\n",
                voltage, current, power, pf, temp);

  // ── Topic separati ─────────────────────────────────────────
  char buf[32];

  snprintf(buf, sizeof(buf), "%.2f", voltage);
  mqtt.publish(TOPIC_VOLTAGE, buf);

  snprintf(buf, sizeof(buf), "%.3f", current);
  mqtt.publish(TOPIC_CURRENT, buf);

  snprintf(buf, sizeof(buf), "%.2f", power);
  mqtt.publish(TOPIC_POWER, buf);

  snprintf(buf, sizeof(buf), "%.1f", temp);
  mqtt.publish(TOPIC_TEMPERATURE, buf);

  // ── Topic JSON completo ─────────────────────────────────────
  StaticJsonDocument<256> doc;
  doc["voltage"]     = round(voltage  * 100) / 100.0;
  doc["current"]     = round(current  * 1000) / 1000.0;
  doc["power"]       = round(power    * 100) / 100.0;
  doc["apparent"]    = round(apparent * 100) / 100.0;
  doc["pf"]          = round(pf       * 1000) / 1000.0;
  doc["temperature"] = round(temp     * 10) / 10.0;
  doc["uptime"]      = millis() / 1000;

  char jsonBuf[256];
  serializeJson(doc, jsonBuf);
  mqtt.publish(TOPIC_ENERGY, jsonBuf);
}

/* ================== MQTT CALLBACK ================== */
void mqttCallback(char* topic, byte* message, unsigned int length) {
  char msg[64] = {0};
  memcpy(msg, message, min((unsigned int)63, length));
  Serial.printf("[MQTT] RX [%s] = %s\n", topic, msg);

  // Comandi relay
  if (strcmp(topic, TOPIC_RELAY_CMD) == 0) {
    if (strcmp(msg, "on")     == 0 || strcmp(msg, "1") == 0) {
      digitalWrite(RELAY_PIN, HIGH);
      publishStatus();
    }
    else if (strcmp(msg, "off") == 0 || strcmp(msg, "0") == 0) {
      digitalWrite(RELAY_PIN, LOW);
      publishStatus();
    }
    else if (strcmp(msg, "toggle") == 0) {
      digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
      publishStatus();
    }
    return;
  }

  // Richiesta lettura energia immediata
  if (strcmp(topic, (String(device_id) + "/energy/query").c_str()) == 0) {
    publishEnergy();
    return;
  }

  // Richiesta stato
  if (strcmp(topic, (String(device_id) + "/status/query").c_str()) == 0) {
    publishStatus();
    return;
  }
}

/* ================== MQTT RECONNECT ================== */
void mqttReconnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  Serial.printf("[MQTT] Connessione a %s:%s...\n", mqtt_server, mqtt_port);

  char lwtTopic[64];
  snprintf(lwtTopic, sizeof(lwtTopic), "%s/online", device_id);

  if (mqtt.connect(device_id, nullptr, nullptr,
                   lwtTopic, 0, true, "0")) {
    Serial.println("[MQTT] Connesso!");

    // Subscribe
    mqtt.subscribe(TOPIC_RELAY_CMD);
    mqtt.subscribe((String(device_id) + "/energy/query").c_str());
    mqtt.subscribe((String(device_id) + "/status/query").c_str());

    // Annunci
    mqtt.publish(TOPIC_ONLINE, "1", true);
    publishStatus();

    Serial.println("[MQTT] Subscribe completati");
  } else {
    Serial.printf("[MQTT] Fallito, rc=%d\n", mqtt.state());
  }
}

/* ================== GESTIONE PULSANTE ================== */
void handleButton() {
  static unsigned long lastPress = 0;
  if (digitalRead(BTN_PIN) == LOW && millis() - lastPress > 200) {
    lastPress = millis();
    digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
    publishStatus();
    Serial.println("[BTN] Toggle relay");
  }
}

/* ================== SETUP ================== */
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);  digitalWrite(RELAY_PIN, LOW);
  pinMode(BTN_PIN,   INPUT_PULLUP);
  pinMode(LED_PIN,   OUTPUT);  digitalWrite(LED_PIN, HIGH);

  loadConfig();

  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port",     "MQTT Port",   mqtt_port,   6);
  WiFiManagerParameter custom_device_id("devid",    "Device ID",   device_id,   32);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_device_id);

  if (!wifiManager.autoConnect("Shelly-Emulator-AP")) {
    ESP.restart();
  }

  if (shouldSaveConfig) {
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port,   custom_mqtt_port.getValue());
    strcpy(device_id,   custom_device_id.getValue());
    saveConfig();
  }

  Serial.println("\n[WiFi] Connesso!");

  // ── Build topic strings ──────────────────────────────────────
  snprintf(TOPIC_RELAY_STATE,  sizeof(TOPIC_RELAY_STATE),  "%s/relay/0",          device_id);
  snprintf(TOPIC_RELAY_CMD,    sizeof(TOPIC_RELAY_CMD),    "%s/relay/0/command",   device_id);
  snprintf(TOPIC_STATUS,       sizeof(TOPIC_STATUS),       "%s/status",            device_id);
  snprintf(TOPIC_ENERGY,       sizeof(TOPIC_ENERGY),       "%s/energy",            device_id);
  snprintf(TOPIC_POWER,        sizeof(TOPIC_POWER),        "%s/power",             device_id);
  snprintf(TOPIC_CURRENT,      sizeof(TOPIC_CURRENT),      "%s/current",           device_id);
  snprintf(TOPIC_VOLTAGE,      sizeof(TOPIC_VOLTAGE),      "%s/voltage",           device_id);
  snprintf(TOPIC_TEMPERATURE,  sizeof(TOPIC_TEMPERATURE),  "%s/temperature",       device_id);
  snprintf(TOPIC_ONLINE,       sizeof(TOPIC_ONLINE),       "%s/online",            device_id);

  // ── HLW8012 ─────────────────────────────────────────────────
  // ── HLW8012 con valori corretti Shelly Plug S Gen1 ──────────
  hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, false, 1000000);

  // Resistori reali Shelly Plug S Gen1:
  // - Shunt corrente: 1mΩ  (0.001 Ω)
  // - Partitore tensione R1: 2.48MΩ (2480000 Ω)
  // - Partitore tensione R2: 1kΩ   (1000 Ω)
  hlw8012.setResistors(0.001, 2480000, 1000);

  // Moltiplicatori di calibrazione empirici per Shelly Plug S
  // (ricavati da misure reali con tester certificato)
  hlw8012.setCurrentMultiplier(13170.0);
  hlw8012.setVoltageMultiplier(477800.0);
  hlw8012.setPowerMultiplier(10420000.0);

  attachInterrupt(digitalPinToInterrupt(CF1_PIN), hlw8012_cf1_interrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CF_PIN),  hlw8012_cf_interrupt,  CHANGE);

  // ── MQTT ────────────────────────────────────────────────────
  mqtt.setServer(mqtt_server, atoi(mqtt_port));
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  // ── Web Server ───────────────────────────────────────────────
  server.on("/", []() {
    String html = "<html><body><h1>Shelly Plug S</h1>";
    html += "<p>Relay: "    + String(digitalRead(RELAY_PIN) ? "ON" : "OFF") + "</p>";
    html += "<p>Potenza: "  + String(hlw8012.getActivePower())  + " W</p>";
    html += "<p>Corrente: " + String(hlw8012.getCurrent())      + " A</p>";
    html += "<p>Tensione: " + String(hlw8012.getVoltage())      + " V</p>";
    html += "<p>Temp: "     + String(getRealTemperature())      + " C</p>";
    html += "<p><a href='/relay/0?turn=on'>ACCENDI</a> | <a href='/relay/0?turn=off'>SPEGNI</a></p>";
    html += "<p><a href='/reboot'>RIAVVIA</a> | <a href='/reset'>RESET WIFI</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/relay/0", []() {
    if (server.hasArg("turn")) {
      String action = server.arg("turn");
      if (action == "on")     digitalWrite(RELAY_PIN, HIGH);
      else if (action == "off")    digitalWrite(RELAY_PIN, LOW);
      else if (action == "toggle") digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
      publishStatus();
    }
    String json = "{\"ison\":" + String(digitalRead(RELAY_PIN) ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });

  server.on("/status", []() {
    String json = "{";
    json += "\"ison\":"    + String(digitalRead(RELAY_PIN) ? "true" : "false") + ",";
    json += "\"temp\":"    + String(getRealTemperature())     + ",";
    json += "\"voltage\":" + String(hlw8012.getVoltage())     + ",";
    json += "\"current\":" + String(hlw8012.getCurrent())     + ",";
    json += "\"power\":"   + String(hlw8012.getActivePower()) + "";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/reboot", []() {
    server.send(200, "text/plain", "Rebooting...");
    delay(1000); ESP.restart();
  });

  server.on("/reset", []() {
    server.send(200, "text/plain", "Resetting WiFi...");
    WiFiManager wm;
    wm.resetSettings();
    delay(1000); ESP.restart();
  });

  server.begin();
  digitalWrite(LED_PIN, LOW);
  Serial.println("[SETUP] Completato");
}

/* ================== LOOP ================== */
void loop() {
  // Reconnect MQTT se necessario
  if (!mqtt.connected()) {
    static unsigned long lastReconn = 0;
    if (millis() - lastReconn > 5000) {
      lastReconn = millis();
      mqttReconnect();
    }
  }
  mqtt.loop();

  server.handleClient();
  handleButton();

  // ── Pubblica energia ogni SEND_INTERVAL_MS ───────────────────
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    publishEnergy();
  }
}
