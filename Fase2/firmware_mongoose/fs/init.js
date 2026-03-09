load('api_config.js');
load('api_gpio.js');
load('api_mqtt.js');
load('api_timer.js');
load('api_sys.js');
load('api_log.js');
load('api_i2c.js');

// ── PIN CONFIGURATION ──────────────────────────────────────────
let RELAY_PIN     = Cfg.get('app.relay_pin');      // GPIO 15
let BTN_PIN       = Cfg.get('app.btn_pin');         // GPIO 13
let LED_GREEN_PIN = Cfg.get('app.led_green_pin');   // GPIO 0  (active LOW)
let LED_RED_PIN   = Cfg.get('app.led_red_pin');     // GPIO 2  (active LOW)
let TOPIC_BASE    = Cfg.get('app.mqtt_topic_base'); // es. "shellies/plug-s"

// ── STATO ──────────────────────────────────────────────────────
let relayState = false;   // false = OFF, true = ON
let mqttReady  = false;

// ── HELPERS LED ────────────────────────────────────────────────
// LED active-low: write(0) = acceso, write(1) = spento
function setLed(pin, on) {
  GPIO.write(pin, on ? 0 : 1);
}

function updateLeds() {
  if (!mqttReady) {
    // Lampeggio rosso = non connesso
    setLed(LED_RED_PIN, true);
    setLed(LED_GREEN_PIN, false);
    return;
  }
  setLed(LED_GREEN_PIN, relayState);   // verde = relay ON
  setLed(LED_RED_PIN, !relayState);    // rosso = relay OFF
}

// ── RELAY ──────────────────────────────────────────────────────
function setRelay(on) {
  relayState = on;
  GPIO.write(RELAY_PIN, relayState ? 1 : 0);
  updateLeds();
  Log.print(Log.INFO, 'Relay: ' + (relayState ? 'ON' : 'OFF'));

  // Pubblica lo stato su MQTT
  if (mqttReady) {
    MQTT.pub(TOPIC_BASE + '/relay/0', relayState ? '1' : '0', 0, true);
    MQTT.pub(TOPIC_BASE + '/status', JSON.stringify({
      relay: relayState,
      uptime: Sys.uptime(),
      free_ram: Sys.free_ram()
    }), 0, false);
  }
}

function toggleRelay() {
  setRelay(!relayState);
}

// ── INIZIALIZZAZIONE GPIO ──────────────────────────────────────
// Relay
GPIO.set_mode(RELAY_PIN, GPIO.MODE_OUTPUT);
GPIO.write(RELAY_PIN, 0);  // default OFF

// LED
GPIO.set_mode(LED_GREEN_PIN, GPIO.MODE_OUTPUT);
GPIO.set_mode(LED_RED_PIN,   GPIO.MODE_OUTPUT);
setLed(LED_GREEN_PIN, false);
setLed(LED_RED_PIN, true);  // rosso acceso = avvio

// Pulsante (pull-up interno, active LOW)
GPIO.set_mode(BTN_PIN, GPIO.MODE_INPUT);
GPIO.set_pull(BTN_PIN, GPIO.PULL_UP);
GPIO.set_int_handler(BTN_PIN, GPIO.INT_EDGE_NEG, function(pin) {
  Log.print(Log.INFO, 'Button pressed');
  toggleRelay();
}, null);
GPIO.enable_int(BTN_PIN);

// ── MQTT ───────────────────────────────────────────────────────
// Sottoscrizioni
MQTT.sub(TOPIC_BASE + '/relay/0/command', function(conn, topic, msg) {
  Log.print(Log.INFO, 'MQTT cmd: ' + msg);
  if (msg === 'on'  || msg === '1') setRelay(true);
  if (msg === 'off' || msg === '0') setRelay(false);
  if (msg === 'toggle')             toggleRelay();
}, null);

MQTT.sub(TOPIC_BASE + '/status/query', function(conn, topic, msg) {
  MQTT.pub(TOPIC_BASE + '/status', JSON.stringify({
    relay: relayState,
    uptime: Sys.uptime(),
    free_ram: Sys.free_ram()
  }), 0, false);
}, null);

// Evento connessione MQTT
MQTT.setEventHandler(function(conn, ev, edata) {
  if (ev === MQTT.EV_CONNACK) {
    Log.print(Log.INFO, 'MQTT connesso');
    mqttReady = true;
    updateLeds();
    // Annuncio online
    MQTT.pub(TOPIC_BASE + '/online', '1', 0, true);
    setRelay(false); // stato iniziale sicuro
  }
  if (ev === MQTT.EV_CLOSE) {
    Log.print(Log.INFO, 'MQTT disconnesso');
    mqttReady = false;
    updateLeds();
  }
}, null);

// ── HEARTBEAT ogni 30s ─────────────────────────────────────────
Timer.set(30000, Timer.REPEAT, function() {
  if (mqttReady) {
    MQTT.pub(TOPIC_BASE + '/status', JSON.stringify({
      relay: relayState,
      uptime: Sys.uptime(),
      free_ram: Sys.free_ram()
    }), 0, false);
  }
}, null);

// ── LAMPEGGIO LED durante boot (no MQTT) ───────────────────────
let blinkTimer = Timer.set(500, Timer.REPEAT, function() {
  if (mqttReady) {
    Timer.del(blinkTimer);
    updateLeds();
    return;
  }
  // Lampeggio rosso
  let cur = GPIO.read(LED_RED_PIN);
  setLed(LED_RED_PIN, cur === 0);  // toggle (active-low)
}, null);

Log.print(Log.INFO, '=== Shelly Plug S - Firmware Custom avviato ===');


// ── ADE7953 - MISURATORE ENERGETICO ───────────────────────────
// Shelly Plug S Gen1: ADE7953 su I2C
// SDA = GPIO 4, SCL = GPIO 5, ADDR = 0x38
let I2C_ADDR   = 0x38;
let I2C_SDA    = 4;
let I2C_SCL    = 5;

// Registri ADE7953 (24-bit, big-endian)
let REG_VRMS   = 0x21C;  // Tensione RMS
let REG_IRMS_A = 0x21A;  // Corrente RMS canale A
let REG_WATT_A = 0x212;  // Potenza attiva canale A

// Fattori di scala (calibrazione Shelly Plug S Gen1)
let V_SCALE    = 0.0000382;   // V per LSB  → ~230V nominale
let I_SCALE    = 0.00000949;  // A per LSB
let W_SCALE    = 0.00000218;  // W per LSB

let i2c = I2C.get();

// Legge un registro a 24-bit dal ADE7953
function ade7953Read(reg) {
  // Comando: 2 byte indirizzo registro + 1 byte read
  let addrHi = (reg >> 8) & 0xFF;
  let addrLo = reg & 0xFF;

  // Write indirizzo registro
  I2C.start(i2c, I2C_ADDR, I2C.WRITE);
  I2C.send(i2c, addrHi);
  I2C.send(i2c, addrLo);
  I2C.stop(i2c);

  // Read 3 byte (24-bit big-endian)
  I2C.start(i2c, I2C_ADDR, I2C.READ);
  let b0 = I2C.recv(i2c, I2C.ACK);
  let b1 = I2C.recv(i2c, I2C.ACK);
  let b2 = I2C.recv(i2c, I2C.NACK);
  I2C.stop(i2c);

  // Ricostruisce valore a 24-bit (signed)
  let val = (b0 << 16) | (b1 << 8) | b2;
  // Converti in signed 24-bit
  if (val > 0x7FFFFF) val = val - 0x1000000;
  return val;
}

// Legge e pubblica tensione, corrente e potenza
function readAndPublishEnergy() {
  let rawV = ade7953Read(REG_VRMS);
  let rawI = ade7953Read(REG_IRMS_A);
  let rawW = ade7953Read(REG_WATT_A);

  let voltage = (rawV * V_SCALE).toFixed(2);   // Volt
  let current = (rawI * I_SCALE).toFixed(3);   // Ampere
  let power   = (rawW * W_SCALE).toFixed(2);   // Watt

  // Potenza apparente e fattore di potenza
  let apparent = (voltage * current).toFixed(2);
  let pf = (apparent > 0) ? (power / apparent).toFixed(3) : '0.000';

  let payload = JSON.stringify({
    voltage:  parseFloat(voltage),
    current:  parseFloat(current),
    power:    parseFloat(power),
    apparent: parseFloat(apparent),
    pf:       parseFloat(pf),
    ts:       Sys.uptime()
  });

  Log.print(Log.INFO,
    'Energy → V:' + voltage + 'V  I:' + current + 'A  P:' + power + 'W'
  );

  if (mqttReady) {
    MQTT.pub(TOPIC_BASE + '/energy', payload, 0, false);
    // Topic separati (utile per Home Assistant)
    MQTT.pub(TOPIC_BASE + '/voltage', voltage,  0, false);
    MQTT.pub(TOPIC_BASE + '/current', current,  0, false);
    MQTT.pub(TOPIC_BASE + '/power',   power,    0, false);
  }
}

// ── TIMER LETTURA ENERGIA ogni 60s ────────────────────────────
Timer.set(60000, Timer.REPEAT, function() {
  readAndPublishEnergy();
}, null);

// Prima lettura immediata dopo 5s (attendi MQTT)
Timer.set(5000, 0, function() {
  readAndPublishEnergy();
}, null);

Log.print(Log.INFO, 'ADE7953 energy monitor inizializzato');