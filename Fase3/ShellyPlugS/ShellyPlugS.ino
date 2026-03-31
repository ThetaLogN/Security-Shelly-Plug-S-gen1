#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <HLW8012.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <math.h>
#include <bearssl/bearssl.h>

#define RELAY_PIN 15
#define SEL_PIN 12
#define CF1_PIN 14
#define CF_PIN 5
#define LED_PIN 0
#define BTN_PIN 13
#define ANALOG_PIN A0
#define CURRENT_MODE HIGH

// --- CREDENZIALI PROTEZIONE OTA ---
const char* ota_user = "admin";
const char* ota_pass = "admin"; // Cambiala con una password più sicura!

char mqtt_server[40] = "192.168.1.100";
char mqtt_port[6] = "1883";
char device_id[32] = "shellyplug-s-emulator";
char wifi_ssid[32]  = "";
char wifi_pass[64]  = "";
bool apMode = false;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
HLW8012 hlw8012;
ESP8266WebServer server(80);

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 60000;
char payload[256];

const double R_PULLUP         = 32000.0;   
const double R_NTC_NOMINAL    = 10000.0;   
const double TEMP_NOMINAL     = 25.0;
const double BETA_COEFFICIENT = 3350.0; 

char TOPIC_RELAY_STATE[64];
char TOPIC_RELAY_CMD[64];
char TOPIC_STATUS[64];
char TOPIC_ENERGY[64];
char TOPIC_POWER[64];
char TOPIC_CURRENT[64];
char TOPIC_VOLTAGE[64];
char TOPIC_TEMPERATURE[64];
char TOPIC_ONLINE[64];

const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shelly Plug S - Firmware UI</title>
    <style>
        :root {
            --bg-color: #2c3136; --panel-bg: #212529; --header-bg: #1c1f22;
            --shelly-blue: #00adef; --text-main: #ffffff; --text-muted: #a0a0a0;
            --border-color: #343a40; --input-bg: #16191c; --danger: #dc3545; --success: #28a745;
        }
        body { background-color: var(--bg-color); color: var(--text-main); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; user-select: none; }
        .top-bar { width: 100%; background: var(--header-bg); padding: 10px 20px; box-sizing: border-box; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #111; position: sticky; top: 0; z-index: 100; }
        .logo { color: var(--shelly-blue); font-weight: bold; font-size: 18px; letter-spacing: 1px; }
        .logo span { color: #fff; font-weight: 300; }
        .top-icons { display: flex; gap: 15px; font-size: 14px; }
        .container { width: 100%; max-width: 600px; padding: 15px; box-sizing: border-box; }
        .panel-switch { background: var(--panel-bg); border-radius: 4px; padding: 20px; display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; border: 1px solid var(--border-color); box-shadow: 0 4px 6px rgba(0,0,0,0.2); }
        .power-val { font-size: 26px; font-weight: bold; width: 100px; text-align: right; }
        .power-val span { font-size: 14px; color: var(--text-muted); font-weight: normal; }
        .top-icons span { display: flex; align-items: center; }
        .btn-pwr { width: 44px; height: 44px; border-radius: 50%; border: 2px solid #555; display: flex; align-items: center; justify-content: center; color: #555; cursor: pointer; font-size: 20px; transition: 0.3s; background: transparent; }
        .btn-pwr.on { border-color: var(--shelly-blue); color: var(--shelly-blue); box-shadow: 0 0 10px rgba(0,173,239,0.2); }
        .nav-grid { display: grid; grid-template-columns: repeat(6, 1fr); gap: 1px; background: var(--border-color); border: 1px solid var(--border-color); border-radius: 4px; overflow: hidden; margin-bottom: 15px; }
        .nav-item { background: var(--panel-bg); padding: 12px 2px; text-align: center; display: flex; flex-direction: column; align-items: center; justify-content: center; cursor: pointer; transition: background 0.2s; }
        .nav-item svg { width: 22px; height: 22px; margin-bottom: 8px; color: #ffffff; opacity: 0.6; transition: opacity 0.2s; }
        .nav-item span { font-size: 9px; color: var(--text-muted); text-transform: uppercase; text-align: center; line-height: 1.2; }
        .nav-item:hover { background: #2a2e33; }
        .nav-item.active { border-bottom: 3px solid var(--shelly-blue); background: #2a2e33; }
        .nav-item.active svg, .nav-item:hover svg { opacity: 1; }
        .tab-content { display: none; }
        .tab-content.active { display: block; animation: fadeIn 0.3s; }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(5px); } to { opacity: 1; transform: translateY(0); } }
        .list-header { background: #343a40; color: #fff; padding: 12px; font-size: 12px; text-align: center; text-transform: uppercase; border-radius: 4px 4px 0 0; font-weight: bold; }
        .list-container { background: var(--panel-bg); border: 1px solid var(--border-color); border-radius: 0 0 4px 4px; }
        .menu-row { border-bottom: 1px solid var(--border-color); }
        .menu-row:last-child { border-bottom: none; }
        .list-item { padding: 15px; display: flex; justify-content: space-between; align-items: center; font-size: 12px; cursor: pointer; transition: background 0.2s; text-transform: uppercase; }
        .list-item:hover { background: #2a2e33; }
        .list-item .left { display: flex; align-items: center; gap: 12px; }
        .list-item .icon { color: var(--text-muted); font-size: 16px; width: 20px; text-align: center; }
        .list-item .chevron { color: var(--text-muted); font-size: 10px; transition: transform 0.3s; }
        .menu-row.open .chevron { transform: rotate(180deg); }
        .submenu-content { display: none; background: #1a1d20; padding: 20px; border-top: 1px solid #111; font-size: 13px; color: #ccc; }
        .menu-row.open .submenu-content { display: block; }
        .form-group { margin-bottom: 15px; }
        .form-group label { display: block; margin-bottom: 8px; color: #eee; font-size: 12px; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 10px; background: var(--input-bg); border: 1px solid #444; border-radius: 4px; color: #fff; box-sizing: border-box; font-size: 14px; }
        input[type="checkbox"], input[type="radio"] { transform: scale(1.2); margin-right: 10px; }
        .btn-save { background: var(--shelly-blue); color: white; border: none; padding: 12px 20px; border-radius: 4px; font-weight: bold; cursor: pointer; text-transform: uppercase; font-size: 12px; width: 100%; transition: 0.2s; }
        .btn-save:hover { background: #0096d1; }
        .btn-danger { background: var(--danger); }
        .btn-danger:hover { background: #c82333; }
        .toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #333; color: #fff; padding: 12px 24px; border-radius: 6px; font-size: 13px; z-index: 9999; opacity: 0; transition: opacity 0.3s; pointer-events: none; }
        .toast.show { opacity: 1; }
        .info-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #2a2e33; font-size: 12px; }
        .info-row:last-child { border-bottom: none; }
        .info-row .key { color: var(--text-muted); }
        .info-row .val { color: #fff; font-weight: bold; }
        .footer { margin-top: 30px; text-align: center; font-size: 11px; color: #6c757d; padding-bottom: 20px; }
    </style>
</head>
<body>
    <div id="toast" class="toast"></div>

    <div class="top-bar">
        <div class="logo">Shelly <span>PLUG S</span></div>
        <div class="top-icons">
            <span style="color: var(--danger);" title="Cloud Offline">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"></path></svg>
            </span> 
            <span style="color: #ffffff;" title="WiFi OK">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect><line x1="6" y1="9" x2="6" y2="14"></line><line x1="18" y1="5" x2="18" y2="14"></line><circle cx="12" cy="18" r="1"></circle></svg>
            </span>
            <span style="color: var(--danger);" title="Safety">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect><path d="M7 11V7a5 5 0 0 1 10 0v4"></path></svg>
            </span>
        </div>
        <div style="font-size: 12px; color: var(--text-muted);" id="clock">Time: --:--</div>
    </div>
    
    <div class="container">
        <div class="panel-switch">
            <div style="font-size:14px;font-weight:bold;color:#ccc;">Switch</div>
            <div style="display:flex;align-items:center;gap:10px;">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="#ffffff" stroke="#ffffff" stroke-width="1" style="opacity:0.8;">
                    <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon>
                </svg>
                <div class="power-val" id="power-display">-- <span>W</span></div>
            </div>
            <button class="btn-pwr" id="btn-relay" onclick="toggleRelay()">⏻</button>
        </div>
        <div class="nav-grid">
            <div class="nav-item" onclick="switchTab('timer',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 16 14"></polyline></svg>
                <span>Timer</span>
            </div>
            <div class="nav-item" onclick="switchTab('schedule',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"></rect><line x1="16" y1="2" x2="16" y2="6"></line><line x1="8" y1="2" x2="8" y2="6"></line><line x1="3" y1="10" x2="21" y2="10"></line></svg>
                <span>Weekly<br>schedule</span>
            </div>
            <div class="nav-item active" onclick="switchTab('internet',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                <span>Internet &<br>Security</span>
            </div>
            <div class="nav-item" onclick="switchTab('safety',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
                <span>Safety</span>
            </div>
            <div class="nav-item" onclick="switchTab('actions',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><polyline points="11 8 15 12 11 16"></polyline><line x1="8" y1="12" x2="8.01" y2="12"></line></svg>
                <span>Actions</span>
            </div>
            <div class="nav-item" onclick="switchTab('settings',this)">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg>
                <span>Settings</span>
            </div>
        </div>
        <div id="tab-internet" class="tab-content active">
            <div class="list-header">Internet & Security</div>
            <div class="list-container">
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg></span> WIFI MODE - CLIENT</div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <div class="form-group">
                            <label>WiFi Network (SSID)</label>
                            <input type="text" id="wifi-ssid" placeholder="Nome rete WiFi">
                        </div>
                        <div class="form-group">
                            <label>Password</label>
                            <input type="password" id="wifi-pass" placeholder="Password WiFi">
                        </div>
                        <button class="btn-save" onclick="saveWifi()">Save & Reconnect</button>
                    </div>
                </div>
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg></span>WIFI CLIENT AP ROAMING</div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <div class="form-group">
                            <label>MQTT Server</label>
                            <input type="text" id="mqtt-server" placeholder="Es. 192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label>MQTT Port</label>
                            <input type="text" id="mqtt-port" placeholder="1883">
                        </div>
                        <div class="form-group">
                            <label>Device ID</label>
                            <input type="text" id="mqtt-devid" placeholder="shellyplug-s">
                        </div>
                        <button class="btn-save" onclick="saveMQTT()">Save</button>
                    </div>
                </div>

                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                        </span> WIFI MODE - ACCESS POINT </div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <label><input type="checkbox" checked> WIFI MODE - ACCESS POINT</label><br><br>
                        <button class="btn-save">Save</button>
                    </div>
                </div>

                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                        </span> RESTRICT LOGIN </div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <label><input type="checkbox" checked> RESTRICT LOGIN </label><br><br>
                        <button class="btn-save">Save</button>
                    </div>
                </div>

                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                        </span> SNTP SERVER </div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <label><input type="checkbox" checked> SNTP SERVER </label><br><br>
                        <button class="btn-save">Save</button>
                    </div>
                </div>
                
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
                        </span> CLOUD </div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <label><input type="checkbox" checked> CLOUD </label><br><br>
                        <button class="btn-save">Save</button>
                    </div>
                </div>

            </div>
        </div>
        <div id="tab-settings" class="tab-content">
            <div class="list-header">Settings</div>
            <div class="list-container">
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon">⚡</span> POWER ON DEFAULT MODE</div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <p>Stato del relay al ripristino dell'alimentazione:</p>
                        <label><input type="radio" name="pwr" value="restore" checked> Restore last mode</label><br><br>
                        <label><input type="radio" name="pwr" value="on"> Always ON</label><br><br>
                        <label><input type="radio" name="pwr" value="off"> Always OFF</label><br><br>
                        <button class="btn-save" onclick="showToast('Impostazione salvata (riavvio necessario)')">Save</button>
                    </div>
                </div>
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon">⬆️</span> FIRMWARE UPDATE (OTA)</div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <p>Versione attuale: <strong>v3.0.0</strong></p>
                        <p style="color:var(--text-muted);font-size:11px;">Carica un file .bin </p>
                        <button class="btn-save" onclick="window.location.href='/update'">⬆ Apri OTA Update</button>
                    </div>
                </div>
                <div class="menu-row">
                    <div class="list-item" onclick="toggleAccordion(this)">
                        <div class="left"><span class="icon">🔄</span> REBOOT / RESET WIFI</div>
                        <div class="chevron">▼</div>
                    </div>
                    <div class="submenu-content">
                        <button class="btn-save btn-danger" onclick="rebootDevice()">Reboot Device</button>
                        <br><br>
                        <button class="btn-save btn-danger" style="background:#8b0000;" onclick="resetWifi()">Reset WiFi & Reboot</button>
                    </div>
                </div>

            </div>
        </div>
        <div id="tab-timer" class="tab-content">
            <div class="list-header">Timer</div>
            <div class="list-container" style="padding:20px;text-align:center;color:var(--text-muted);">
                Funzione non ancora implementata.
            </div>
        </div>
        <div id="tab-schedule" class="tab-content">
            <div class="list-header">Weekly Schedule</div>
            <div class="list-container" style="padding:20px;text-align:center;color:var(--text-muted);">
                Funzione non ancora implementata.
            </div>
        </div>
        <div id="tab-safety" class="tab-content">
            <div class="list-header">Safety</div>
            <div class="list-container" style="padding:20px;text-align:center;color:var(--text-muted);">
                Protezione disabilitata dal firmware custom.
            </div>
        </div>
        <div id="tab-actions" class="tab-content">
            <div class="list-header">Actions (Webhooks)</div>
            <div class="list-container" style="padding:20px;text-align:center;color:var(--text-muted);">
                Funzione non ancora implementata.
            </div>
        </div>

        <div class="footer">
            <p>Allterco Robotics Ltd.</p>
            <span style="color:var(--shelly-blue)">support@shelly.cloud</span> 1 <span style="color:var(--shelly-blue)">https://shelly.cloud</span>
        </div>
    </div>

    <script>
        function updateTime() {
            const now = new Date();
            document.getElementById('clock').innerText = 'Time: ' + now.toLocaleTimeString('it-IT', {hour:'2-digit',minute:'2-digit'});
        }
        setInterval(updateTime, 1000); updateTime();
        function showToast(msg, color) {
            const t = document.getElementById('toast');
            t.innerText = msg;
            t.style.background = color || '#333';
            t.classList.add('show');
            setTimeout(() => t.classList.remove('show'), 3000);
        }
        function switchTab(tabId, el) {
            document.querySelectorAll('.nav-item').forEach(e => e.classList.remove('active'));
            el.classList.add('active');
            document.querySelectorAll('.tab-content').forEach(e => e.classList.remove('active'));
            document.getElementById('tab-' + tabId).classList.add('active');
        }
        function toggleAccordion(el) {
            const row = el.parentElement;
            const isOpen = row.classList.contains('open');
            row.parentElement.querySelectorAll('.menu-row').forEach(r => r.classList.remove('open'));
            if (!isOpen) row.classList.add('open');
        }
        function toggleRelay() {
            fetch('/relay/0?turn=toggle')
                .then(r => r.json())
                .then(data => updateRelayUI(data.ison))
                .catch(() => showToast('Errore connessione', '#dc3545'));
        }

        function updateRelayUI(ison) {
            const btn = document.getElementById('btn-relay');
            if (ison) btn.classList.add('on');
            else      btn.classList.remove('on');
        }

        function fetchStatus() {
            fetch('/status')
                .then(r => r.json())
                .then(d => {
                    updateRelayUI(d.ison);
                    document.getElementById('power-display').innerHTML = parseFloat(d.power).toFixed(1) + ' <span>W</span>';
                })
                .catch(() => {});
        }
        setInterval(fetchStatus, 3000); fetchStatus();

        function loadConfig() {
            fetch('/api/config')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('mqtt-server').value = d.mqtt_server || '';
                    document.getElementById('mqtt-port').value   = d.mqtt_port   || '';
                    document.getElementById('mqtt-devid').value  = d.device_id   || '';
                })
                .catch(() => {});
        }
        loadConfig();

        function saveWifi() {
            const ssid = document.getElementById('wifi-ssid').value.trim();
            const pass = document.getElementById('wifi-pass').value;
            if (!ssid) { showToast('Inserisci SSID', '#dc3545'); return; }
            fetch('/api/wifi', {
                method: 'POST',
                headers: {'Content-Type':'application/json'},
                body: JSON.stringify({ssid, pass})
            })
            .then(r => r.json())
            .then(d => showToast(d.msg || 'Salvato! Riconnessione...', '#28a745'))
            .catch(() => showToast('Errore', '#dc3545'));
        }
        
        function saveMQTT() {
            const server = document.getElementById('mqtt-server').value.trim();
            const port   = document.getElementById('mqtt-port').value.trim();
            const devid  = document.getElementById('mqtt-devid').value.trim();
            if (!server || !port || !devid) { showToast('Compila tutti i campi', '#dc3545'); return; }
            fetch('/api/mqtt', {
                method: 'POST',
                headers: {'Content-Type':'application/json'},
                body: JSON.stringify({server, port, devid})
            })
            .then(r => r.json())
            .then(d => showToast(d.msg || 'MQTT aggiornato! Riavvio consigliato.', '#28a745'))
            .catch(() => showToast('Errore', '#dc3545'));
        }
        
        function rebootDevice() {
            if (!confirm('Riavviare il dispositivo?')) return;
            fetch('/reboot').then(() => showToast('Riavvio in corso...', '#f0ad4e'));
        }
        
        function resetWifi() {
            if (!confirm('Reset WiFi e riavvio? Dovrai riconfigurare la rete.')) return;
            fetch('/reset').then(() => showToast('Reset WiFi in corso...', '#dc3545'));
        }
    </script>
</body>
</html>
)rawliteral";

// --- CHIAVE PUBBLICA PER VERIFICA OTA ---
const char public_key_pem[] PROGMEM = R"EOF(
-----BEGIN PUBLIC KEY-----
-----END PUBLIC KEY-----
)EOF";

double getRealTemperature() {
  int rawADC = analogRead(ANALOG_PIN);
  if (rawADC >= 1023) return -273.0;  
  if (rawADC <= 0)    return 999.0;   

  double R_ntc = ((double)rawADC * R_PULLUP) / (1024.0 - (double)rawADC);

  double steinhart = (double)BETA_COEFFICIENT
                   / (BETA_COEFFICIENT / (TEMP_NOMINAL + 273.15)
                   + log(R_ntc / R_NTC_NOMINAL));
  steinhart -= 273.15;

  return steinhart;
}

void ICACHE_RAM_ATTR hlw8012_cf1_interrupt() { hlw8012.cf1_interrupt(); }
void ICACHE_RAM_ATTR hlw8012_cf_interrupt() { hlw8012.cf_interrupt(); }

void loadConfig() {
 if (!LittleFS.begin()) return;
 if (!LittleFS.exists("/config.json")) return;
 File f = LittleFS.open("/config.json", "r");
 if (!f) return;
 size_t size = f.size();
 std::unique_ptr<char[]> buf(new char[size]);
 f.readBytes(buf.get(), size);
 f.close();
 DynamicJsonDocument json(1024);
 if (deserializeJson(json, buf.get())) return;
 if (json.containsKey("mqtt_server")) strcpy(mqtt_server, json["mqtt_server"]);
 if (json.containsKey("mqtt_port"))   strcpy(mqtt_port,   json["mqtt_port"]);
 if (json.containsKey("device_id"))   strcpy(device_id,   json["device_id"]);
 if (json.containsKey("wifi_ssid"))   strcpy(wifi_ssid,   json["wifi_ssid"]);
 if (json.containsKey("wifi_pass"))   strcpy(wifi_pass,   json["wifi_pass"]);
}

void saveConfig() {
 if (!LittleFS.begin()) return;
 DynamicJsonDocument json(1024);
 json["mqtt_server"] = mqtt_server;
 json["mqtt_port"]   = mqtt_port;
 json["device_id"]   = device_id;
 json["wifi_ssid"]   = wifi_ssid;
 json["wifi_pass"]   = wifi_pass;
 File f = LittleFS.open("/config.json", "w");
 if (!f) return;
 serializeJson(json, f);
 f.close();
 Serial.println("[CFG] Configurazione salvata");
}

void wifiConnect() {
 if (strlen(wifi_ssid) == 0) {
   Serial.println("[WiFi] Nessun SSID salvato, avvio AP...");
   apMode = true;
 } else {
   Serial.printf("[WiFi] Connessione a %s...\n", wifi_ssid);
   WiFi.mode(WIFI_STA);
   WiFi.begin(wifi_ssid, wifi_pass);
   unsigned long t = millis();
   while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
     delay(300);
     Serial.print(".");
   }
   Serial.println();
   if (WiFi.status() == WL_CONNECTED) {
     apMode = false;
     Serial.printf("[WiFi] Connesso! IP: %s\n", WiFi.localIP().toString().c_str());
     return;
   }
   Serial.println("[WiFi] Connessione fallita, avvio AP...");
   apMode = true;
 }
 WiFi.mode(WIFI_AP);
 WiFi.softAP("ShellyPlugS-Setup");
 Serial.printf("[AP] SSID: ShellyPlugS-Setup  IP: %s\n",
               WiFi.softAPIP().toString().c_str());
 for (int i = 0; i < 6; i++) {
   digitalWrite(LED_PIN, LOW);  delay(150);
   digitalWrite(LED_PIN, HIGH); delay(150);
 }
}

void publishStatus(){
 if (!mqtt.connected()) return;
 bool on = digitalRead(RELAY_PIN);
 mqtt.publish(TOPIC_RELAY_STATE, on ? "1" : "0", true);
 StaticJsonDocument<128> doc;
 doc["ison"] = on;
 doc["uptime"] = millis() / 1000;
 char buf[128];
 serializeJson(doc, buf);
 mqtt.publish(TOPIC_STATUS, buf);
}

void publishEnergy() {
 if (!mqtt.connected()) return;
 float power = hlw8012.getActivePower();
 float current = hlw8012.getCurrent();
 float voltage = hlw8012.getVoltage();
 float apparent = voltage * current;
 float pf = (apparent > 0.01f) ? (power / apparent) : 0.0f;
 if (pf > 1.0f) pf = 1.0f;
 double temp = getRealTemperature();

 Serial.printf("[ENERGY] V:%.2fV I:%.3fA P:%.2fW PF:%.3f T:%.1f°C\n",
 voltage, current, power, pf, temp);

 char buf[32];
 snprintf(buf, sizeof(buf), "%.2f", voltage); mqtt.publish(TOPIC_VOLTAGE, buf);
 snprintf(buf, sizeof(buf), "%.3f", current); mqtt.publish(TOPIC_CURRENT, buf);
 snprintf(buf, sizeof(buf), "%.2f", power); mqtt.publish(TOPIC_POWER, buf);
 snprintf(buf, sizeof(buf), "%.1f", temp); mqtt.publish(TOPIC_TEMPERATURE, buf);

 StaticJsonDocument<256> doc;
 doc["voltage"] = round(voltage * 100) / 100.0;
 doc["current"] = round(current * 1000) / 1000.0;
 doc["power"] = round(power * 100) / 100.0;
 doc["apparent"] = round(apparent * 100) / 100.0;
 doc["pf"] = round(pf * 1000) / 1000.0;
 doc["temperature"] = round(temp * 10) / 10.0;
 doc["uptime"] = millis() / 1000;
 char jsonBuf[256];
 serializeJson(doc, jsonBuf);
 mqtt.publish(TOPIC_ENERGY, jsonBuf);
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
 char msg[64] = {0};
 memcpy(msg, message, min((unsigned int)63, length));
 Serial.printf("[MQTT] RX [%s] = %s\n", topic, msg);
 if (strcmp(topic, TOPIC_RELAY_CMD) == 0) {
 if (strcmp(msg,"on")==0 || strcmp(msg,"1")==0) { digitalWrite(RELAY_PIN, HIGH); publishStatus(); }
 else if (strcmp(msg,"off")==0 || strcmp(msg,"0")==0) { digitalWrite(RELAY_PIN, LOW); publishStatus(); }
 else if (strcmp(msg,"toggle")==0) { digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN)); publishStatus(); }
 }
 if (strcmp(topic, (String(device_id)+"/energy/query").c_str()) == 0) publishEnergy();
 if (strcmp(topic, (String(device_id)+"/status/query").c_str()) == 0) publishStatus();
}

void mqttReconnect() {
 if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
 Serial.printf("[MQTT] Connessione a %s:%s...\n", mqtt_server, mqtt_port);
 char lwtTopic[64];
 snprintf(lwtTopic, sizeof(lwtTopic), "%s/online", device_id);
 if (mqtt.connect(device_id, nullptr, nullptr, lwtTopic, 0, true, "0")) {
 Serial.println("[MQTT] Connesso!");
 mqtt.subscribe(TOPIC_RELAY_CMD);
 mqtt.subscribe((String(device_id)+"/energy/query").c_str());
 mqtt.subscribe((String(device_id)+"/status/query").c_str());
 mqtt.publish(TOPIC_ONLINE, "1", true);
 publishStatus();
 } else {
 Serial.printf("[MQTT] Fallito rc=%d\n", mqtt.state());
 }
}

void handleButton() {
 static unsigned long lastPress = 0;
 if (digitalRead(BTN_PIN) == LOW && millis() - lastPress > 200) {
 lastPress = millis();
 digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
 publishStatus();
 Serial.println("[BTN] Toggle relay");
 }
}

void setup() {
 Serial.begin(115200);
 delay(100);
 Serial.println("\n=== Shelly Plug S v3.0.0 ===");

 pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW);
 pinMode(BTN_PIN, INPUT_PULLUP);
 pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);

 loadConfig();

 wifiConnect();

 snprintf(TOPIC_RELAY_STATE, sizeof(TOPIC_RELAY_STATE), "%s/relay/0", device_id);
 snprintf(TOPIC_RELAY_CMD, sizeof(TOPIC_RELAY_CMD), "%s/relay/0/command", device_id);
 snprintf(TOPIC_STATUS, sizeof(TOPIC_STATUS), "%s/status", device_id);
 snprintf(TOPIC_ENERGY, sizeof(TOPIC_ENERGY), "%s/energy", device_id);
 snprintf(TOPIC_POWER, sizeof(TOPIC_POWER), "%s/power", device_id);
 snprintf(TOPIC_CURRENT, sizeof(TOPIC_CURRENT), "%s/current", device_id);
 snprintf(TOPIC_VOLTAGE, sizeof(TOPIC_VOLTAGE), "%s/voltage", device_id);
 snprintf(TOPIC_TEMPERATURE, sizeof(TOPIC_TEMPERATURE), "%s/temperature", device_id);
 snprintf(TOPIC_ONLINE, sizeof(TOPIC_ONLINE), "%s/online", device_id);

 hlw8012.begin(CF_PIN, CF1_PIN, SEL_PIN, CURRENT_MODE, true, 1000000);
 hlw8012.setResistors(0.001, 2351000, 1000);

 attachInterrupt(digitalPinToInterrupt(CF1_PIN), hlw8012_cf1_interrupt, CHANGE);
 attachInterrupt(digitalPinToInterrupt(CF_PIN), hlw8012_cf_interrupt, CHANGE);

 mqtt.setServer(mqtt_server, atoi(mqtt_port));
 mqtt.setCallback(mqttCallback);
 mqtt.setBufferSize(512);

 server.on("/", []() {
 server.send_P(200, "text/html", index_html);
 });

 server.on("/status", []() {
 StaticJsonDocument<256> doc;
 doc["ison"] = (bool)digitalRead(RELAY_PIN);
 doc["voltage"] = hlw8012.getVoltage();
 doc["current"] = hlw8012.getCurrent();
 doc["power"] = hlw8012.getActivePower();
 doc["temp"] = getRealTemperature();
 doc["uptime"] = millis() / 1000;
 doc["ip"] = WiFi.localIP().toString();
 char buf[256]; serializeJson(doc, buf);
 server.send(200, "application/json", buf);
 });

 server.on("/relay/0", []() {
 if (server.hasArg("turn")) {
 String a = server.arg("turn");
 if (a == "on") digitalWrite(RELAY_PIN, HIGH);
 else if (a == "off") digitalWrite(RELAY_PIN, LOW);
 else if (a == "toggle") digitalWrite(RELAY_PIN, !digitalRead(RELAY_PIN));
 publishStatus();
 }
 StaticJsonDocument<64> doc;
 doc["ison"] = (bool)digitalRead(RELAY_PIN);
 doc["power"] = hlw8012.getActivePower();
 char buf[64]; serializeJson(doc, buf);
 server.send(200, "application/json", buf);
 });

 server.on("/api/config", []() {
 StaticJsonDocument<256> doc;
 doc["mqtt_server"] = mqtt_server;
 doc["mqtt_port"] = mqtt_port;
 doc["device_id"] = device_id;
 char buf[256]; serializeJson(doc, buf);
 server.send(200, "application/json", buf);
 });

 server.on("/api/mqtt", HTTP_POST, []() {
 if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"msg\":\"No body\"}"); return; }
 DynamicJsonDocument doc(256);
 if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"msg\":\"JSON error\"}"); return; }
 if (doc.containsKey("server")) strlcpy(mqtt_server, doc["server"], sizeof(mqtt_server));
 if (doc.containsKey("port")) strlcpy(mqtt_port, doc["port"], sizeof(mqtt_port));
 if (doc.containsKey("devid")) strlcpy(device_id, doc["devid"], sizeof(device_id));
 saveConfig();
 server.send(200, "application/json", "{\"msg\":\"MQTT salvato! Riavvio per applicare.\"}");
 Serial.println("[MQTT] Config aggiornata, riavvio consigliato");
 });

 server.on("/api/wifi", HTTP_POST, []() {
 if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"msg\":\"No body\"}"); return; }
 DynamicJsonDocument doc(256);
 if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"msg\":\"JSON error\"}"); return; }
 String ssid = doc["ssid"] | "";
 String pass = doc["pass"] | "";
 if (ssid.isEmpty()) { server.send(400, "application/json", "{\"msg\":\"SSID vuoto\"}"); return; }
 strlcpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid));
 strlcpy(wifi_pass, pass.c_str(), sizeof(wifi_pass));
 saveConfig();
 server.send(200, "application/json", "{\"msg\":\"Credenziali salvate, riconnessione...\"}");
 delay(500);
 wifiConnect();
 });

 server.on("/reboot", []() {
 server.send(200, "text/plain", "Rebooting...");
 delay(1000); ESP.restart();
 });

 server.on("/reset", []() {
 server.send(200, "text/plain", "Reset WiFi in corso...");
 wifi_ssid[0] = '\0';
 wifi_pass[0] = '\0';
 saveConfig();
 delay(500);
 wifiConnect();
 });

  server.on("/update", HTTP_GET, []() {
    if (!server.authenticate(ota_user, ota_pass)) return server.requestAuthentication();
    String html = "<html><body style='background:#2c3136;color:white;font-family:sans-serif;text-align:center;padding:50px;'>";
    html += "<h2>Shelly Plug S - OTA Protetto</h2>";
    html += "<p style='color:#a0a0a0'>Carica il file <b>_firmato.bin</b> generato dallo script Python.</p>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin' style='padding:10px;background:#16191c;color:white;'><br><br>";
    html += "<input type='submit' value='Verifica e Aggiorna' style='padding:12px 24px;background:#00adef;border:none;color:white;border-radius:4px;cursor:pointer;font-weight:bold;'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST, []() {
    if (!server.authenticate(ota_user, ota_pass)) return;
    server.sendHeader("Connection", "close");
    if (Update.hasError()) {
      server.send(500, "text/plain", "ERRORE: Aggiornamento Fallito. Firma non valida, file alterato o corrotto.");
    } else {
      server.send(200, "text/plain", "SUCCESSO: Firma verificata. Aggiornamento in corso. La presa si riavviera' a breve...");
      delay(1000);
      ESP.restart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] Inizio ricezione file: %s\n", upload.filename.c_str());
      
      static BearSSL::PublicKey* otaPubKey = new BearSSL::PublicKey(public_key_pem);
      static BearSSL::HashSHA256* otaHash = new BearSSL::HashSHA256();
      Update.installSignature(otaHash, otaPubKey); 
      
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { Update.printError(Serial); }
    } 
    else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } 
    else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { 
        Serial.printf("[OTA] Firma Verificata! Dimensione totale: %u bytes\n", upload.totalSize);
      } else {
        Serial.println("[OTA] ALLARME INTRUSIONE: La firma non corrisponde alla chiave pubblica.");
        Update.printError(Serial);
      }
    }
    yield();
  });

 server.begin();
 Serial.println("[HTTP] Web server avviato");

 digitalWrite(LED_PIN, LOW);
 Serial.printf("[SETUP] Completato — http://%s\n", WiFi.localIP().toString().c_str());
}

void loop() {
 server.handleClient();
 handleButton();

 if (apMode) return; 

 if (!mqtt.connected()) {
   static unsigned long lastReconn = 0;
   if (millis() - lastReconn > 5000) { lastReconn = millis(); mqttReconnect(); }
 }
 mqtt.loop();

 if (millis() - lastSend >= SEND_INTERVAL_MS) {
   lastSend = millis();
   publishEnergy();
 }
}