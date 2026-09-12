#include "webserver.h"
#include "config.h"
#include "pins.h"
#include "player.h"
#include "sdcard.h"
#include "solenoid.h"
#include "wifi_manager.h"
#include <ESPmDNS.h>
#include <FS.h>
#include <NetBIOS.h>
#include <Preferences.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_http_server.h>
#include <stdarg.h>
#include <time.h>
#include <vector>


#include <driver/temperature_sensor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>


static std::vector<int> ws_clients;
static SemaphoreHandle_t ws_mutex = NULL;
static QueueHandle_t log_queue = NULL;
httpd_handle_t server = nullptr;

static void ws_sender_task(void *pvParameters) {
  char msg[256];
  while (true) {
    if (xQueueReceive(log_queue, msg, portMAX_DELAY)) {
      if (ws_mutex != NULL)
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
      if (!ws_clients.empty()) {
        for (auto it = ws_clients.begin(); it != ws_clients.end();) {
          httpd_ws_frame_t ws_pkt;
          memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
          ws_pkt.payload = (uint8_t *)msg;
          ws_pkt.len = strlen(msg);
          ws_pkt.type = HTTPD_WS_TYPE_TEXT;

          if (httpd_ws_send_frame_async(server, *it, &ws_pkt) != ESP_OK) {
            it = ws_clients.erase(it);
          } else {
            ++it;
          }
        }
      }
      if (ws_mutex != NULL)
        xSemaphoreGive(ws_mutex);
    }
  }
}

static temperature_sensor_handle_t tempHandle = NULL;
static bool tempInit = false;

float getChipTemperature() {
  if (!tempInit) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&cfg, &tempHandle) != ESP_OK)
      return NAN;
    if (temperature_sensor_enable(tempHandle) != ESP_OK)
      return NAN;
    tempInit = true;
  }
  float temp;
  if (temperature_sensor_get_celsius(tempHandle, &temp) == ESP_OK)
    return temp;
  return NAN;
}

void sendLogToClients(const char *message) {
  if (log_queue != NULL) {
    xQueueSend(log_queue, message, 0);
  }
}

void LOG(const char *format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  Serial.print(buf);

  String msg(buf);
  if (!msg.endsWith("\n"))
    msg += "\n";
  sendLogToClients(msg.c_str());
}

extern void triggerBuzzer(uint16_t duration);
String sanitizeFilename(String filename);

WebServerManager webServer;
namespace {
bool active = false;
bool needsScan = false;
} // namespace

const char htmlPageAP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0" />
  <title>Setup AP - ESP32-S3 WROOM-1U</title>
  <style>
    :root { 
      --bg-color: #0b1120; 
      --card-bg: rgba(30, 41, 59, 0.7); 
      --text-main: #f8fafc; 
      --text-muted: #94a3b8; 
      --accent: #00f0ff; 
      --border: rgba(51, 65, 85, 0.6); 
      --input-bg: rgba(15, 23, 42, 0.8);
      --glass-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
    }
    body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: radial-gradient(circle at top, #1e293b, #0f172a); color: var(--text-main); margin: 0; padding: 20px; min-height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; box-sizing: border-box; }
    .card { background: var(--card-bg); padding: 24px; border-radius: 16px; width: 100%; max-width: 380px; box-shadow: var(--glass-shadow); backdrop-filter: blur(10px); border: 1px solid rgba(255, 255, 255, 0.1); transition: transform 0.3s ease, box-shadow 0.3s ease; }
    .card:hover { transform: translateY(-3px); box-shadow: 0 12px 40px rgba(0, 240, 255, 0.1); }
    h2 { margin-top: 0; color: var(--accent); font-size: 1.4rem; margin-bottom: 20px; text-align: center; text-shadow: 0 0 10px rgba(0, 240, 255, 0.4); border-bottom: 1px solid rgba(255,255,255,0.05); padding-bottom: 10px; }
    .input-group { display: flex; flex-direction: column; gap: 14px; }
    input[type="text"] { padding: 12px 14px; border: 1px solid var(--border); border-radius: 10px; background: var(--input-bg); color: white; font-size: 0.95rem; box-sizing: border-box; outline: none; transition: all 0.3s; }
    input[type="text"]:focus { border-color: var(--accent); box-shadow: 0 0 8px rgba(0, 240, 255, 0.3); }
    
    .row { display: flex; align-items: center; justify-content: space-between; background: rgba(0,0,0,0.2); padding: 10px 14px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.02); }
    .switch { position: relative; display: inline-block; width: 46px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: rgba(51, 65, 85, 0.8); transition: .3s; border-radius: 24px; border: 1px solid var(--border); }
    .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 2px; bottom: 2px; background-color: white; transition: .3s; border-radius: 50%; box-shadow: 0 2px 5px rgba(0,0,0,0.3); }
    input:checked + .slider { background-color: var(--accent); border-color: var(--accent); box-shadow: 0 0 8px rgba(0, 240, 255, 0.4); }
    input:checked + .slider:before { transform: translateX(22px); background-color: #000; }
    
    button { padding: 12px; border: none; border-radius: 10px; cursor: pointer; font-weight: 600; width: 100%; background: linear-gradient(135deg, var(--accent) 0%, #0088ff 100%); color: #000; font-size: 0.95rem; margin-top: 5px; transition: all 0.3s; box-shadow: 0 0 10px rgba(0, 240, 255, 0.2); display: flex; justify-content: center; align-items: center; }
    button:hover:not(:disabled) { box-shadow: 0 0 15px rgba(0, 240, 255, 0.5); transform: translateY(-2px); }
    button:active:not(:disabled) { transform: translateY(0); }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    footer { text-align: center; color: var(--text-muted); font-size: 0.85rem; margin-top: 25px; text-shadow: 0 2px 4px rgba(0,0,0,0.5); }
  </style>
</head>
<body>
<div class="card">
  <h2>WiFi Configuration</h2>
  <div class="input-group">
    <input type="text" id="wifiSsid" placeholder="WiFi Name (SSID)" />
    <input type="text" id="wifiPass" placeholder="Password" />
    <div class="row">
      <label style="font-size: 0.95rem; color: var(--text-main); font-weight: 500;">Enable WiFi Station</label>
      <label class="switch">
        <input type="checkbox" id="wifiEnable">
        <span class="slider"></span>
      </label>
    </div>
    <button id="btnSaveAP" onclick="saveWifi()">Save and Reset</button>
  </div>
</div>
<footer>&copy; 2026 AN ELECTRONIC | Mataram, Nusa Tenggara Barat</footer>
<script>
async function loadWifi() {
    try {
        const res = await fetch('/api/wifi');
        const config = await res.json();
        document.getElementById('wifiSsid').value = config.ssid || "";
        document.getElementById('wifiPass').value = config.pass || "";
        document.getElementById('wifiEnable').checked = config.enable || false;
    } catch(e) {}
}
async function saveWifi() {
    const ssid = document.getElementById('wifiSsid').value;
    const enable = document.getElementById('wifiEnable').checked;
    if (enable && !ssid) { alert('SSID is required if STA is enabled!'); return; }
    
    const btn = document.getElementById('btnSaveAP');
    btn.innerText = "Applying...";
    btn.disabled = true;

    try {
        const res = await fetch('/api/wifi', { 
            method: 'POST', 
            body: JSON.stringify({ssid: ssid, pass: document.getElementById('wifiPass').value, enable: enable, restart: true}) 
        });
        if (!res.ok) throw new Error("Failed");
        alert('Settings saved! ESP is rebooting to apply changes...');
        await fetch('/api/reboot', { method: 'POST' });
    } catch (e) {
        alert('Failed to save settings');
        btn.innerText = "Save and Reset";
        btn.disabled = false;
    }
}
loadWifi();
</script>
</body>
</html>
)rawliteral";

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>ESP32-S3 WROOM-1U Dashboard</title>
  <style>
    :root { 
      --bg-color: #0b1120; 
      --card-bg: rgba(30, 41, 59, 0.7); 
      --text-main: #f8fafc; 
      --text-muted: #94a3b8; 
      --accent: #00f0ff; 
      --accent-hover: #00c3cf;
      --danger: #ff0055; 
      --border: rgba(51, 65, 85, 0.6); 
      --input-bg: rgba(15, 23, 42, 0.8);
      --glass-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
    }
    * { box-sizing: border-box; }
    body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: radial-gradient(circle at top, #1e293b, #0f172a); background-attachment: fixed; color: var(--text-main); margin: 0; padding: 20px; line-height: 1.5; min-height: 100vh; display: flex; flex-direction: column; align-items: center; }
    
    header { width: 100%; max-width: 1280px; margin: 0 auto 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 15px; }
    header h1 { margin: 0; font-size: 1.6rem; color: var(--accent); letter-spacing: -0.025em; text-shadow: 0 0 10px rgba(0, 240, 255, 0.4); }
    .header-info { display: flex; gap: 15px; font-size: 0.9rem; color: var(--text-muted); background: var(--card-bg); padding: 5px 15px; border-radius: 20px; border: 1px solid var(--border); backdrop-filter: blur(4px); }
    
    .dashboard-grid { column-count: 3; column-gap: 20px; width: 100%; max-width: 1280px; margin: 0 auto; }
    @media (max-width: 1024px) { .dashboard-grid { column-count: 2; } }
    @media (max-width: 768px) { .dashboard-grid { column-count: 1; } .header-info { flex-direction: column; gap: 5px; align-items: flex-end;} }
    
    .card { break-inside: avoid; margin-bottom: 20px; background: var(--card-bg); padding: 20px; border-radius: 16px; box-shadow: var(--glass-shadow); backdrop-filter: blur(10px); border: 1px solid rgba(255, 255, 255, 0.1); display: flex; flex-direction: column; width: 100%; transition: transform 0.3s ease, box-shadow 0.3s ease; }
    .card:hover { transform: translateY(-3px); box-shadow: 0 12px 40px rgba(0, 240, 255, 0.1); }
    
    h2 { margin-top: 0; margin-bottom: 14px; color: var(--accent); font-size: 1.15rem; border-bottom: 1px solid rgba(255,255,255,0.05); padding-bottom: 8px; text-shadow: 0 0 8px rgba(0, 240, 255, 0.2); }
    .row { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
    .row-wrap { flex-wrap: wrap; }
    
    input[type="text"], input[type="number"] { padding: 9px 12px; border: 1px solid var(--border); border-radius: 8px; background: var(--input-bg); color: white; font-size: 0.85rem; flex-grow: 1; outline: none; transition: all 0.3s; width: 100%; }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; margin: 0; }
    input[type=number] { -moz-appearance: textfield; }
    input:focus { border-color: var(--accent); box-shadow: 0 0 8px rgba(0, 240, 255, 0.3); }
    input:disabled { opacity: 0.6; cursor: not-allowed; background: rgba(0,0,0,0.2); }
    
    button, .btn { padding: 9px 12px; border: none; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 0.85rem; transition: all 0.2s; display: inline-flex; align-items: center; justify-content: center; gap: 6px; }
    button:hover:not(:disabled) { transform: translateY(-2px); }
    button:active:not(:disabled) { transform: translateY(0); }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    
    .primary { background: linear-gradient(135deg, var(--accent) 0%, #0088ff 100%); color: #000; box-shadow: 0 0 10px rgba(0, 240, 255, 0.2); }
    .primary:hover:not(:disabled) { box-shadow: 0 0 15px rgba(0, 240, 255, 0.5); }
    .danger { background: linear-gradient(135deg, var(--danger) 0%, #cc0044 100%); color: white; box-shadow: 0 0 10px rgba(255, 0, 85, 0.2); }
    .danger:hover:not(:disabled) { box-shadow: 0 0 15px rgba(255, 0, 85, 0.5); }
    
    .switch { position: relative; display: inline-block; width: 44px; height: 22px; flex-shrink: 0; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: rgba(51, 65, 85, 0.8); transition: .3s; border-radius: 22px; border: 1px solid var(--border); }
    .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 2px; bottom: 2px; background-color: white; transition: .3s; border-radius: 50%; box-shadow: 0 2px 5px rgba(0,0,0,0.3); }
    input:checked + .slider { background-color: var(--accent); border-color: var(--accent); box-shadow: 0 0 8px rgba(0, 240, 255, 0.4); }
    input:checked + .slider:before { transform: translateX(22px); background-color: #000; }

    .file-label { padding: 9px 12px; border: 1px dashed var(--border); border-radius: 8px; background: var(--input-bg); color: var(--text-muted); cursor: pointer; flex-grow: 1; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 0.85rem; display: inline-flex; align-items: center; justify-content: center; transition: all 0.3s; }
    .file-label:hover { border-color: var(--accent); color: var(--accent); background: rgba(0, 240, 255, 0.05); }
    
    .scroll-body::-webkit-scrollbar, #logContainer::-webkit-scrollbar { width: 8px; height: 8px; }
    .scroll-body::-webkit-scrollbar-track, #logContainer::-webkit-scrollbar-track { background: rgba(0,0,0,0.2); border-radius: 4px; }
    .scroll-body::-webkit-scrollbar-thumb, #logContainer::-webkit-scrollbar-thumb { background: var(--border); border-radius: 4px; }
    .scroll-body::-webkit-scrollbar-thumb:hover, #logContainer::-webkit-scrollbar-thumb:hover { background: var(--accent); }

    .scroll-container { border: 1px solid var(--border); border-radius: 8px; background: rgba(15, 23, 42, 0.6); overflow: hidden; backdrop-filter: blur(4px); }
    .scroll-body { max-height: 250px; overflow-y: auto; overflow-x: hidden; padding: 4px; }
    
    table { width: 100%; border-collapse: separate; border-spacing: 0 6px; table-layout: fixed; }
    th { background: transparent; font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; padding: 4px 4px 8px 4px; text-align: center; font-weight: 600; border: none; letter-spacing: 0.5px; }
    td { padding: 0; background: rgba(0,0,0,0.2); border: none; }
    td:first-child { border-top-left-radius: 8px; border-bottom-left-radius: 8px; }
    td:last-child { border-top-right-radius: 8px; border-bottom-right-radius: 8px; }
    tbody tr { transition: all 0.2s; }
    tbody tr:hover td { background: rgba(0, 240, 255, 0.06); }

    .cell-content { display: flex; align-items: center; justify-content: center; min-height: 42px; width: 100%; font-size: 0.85rem; }
    .cell-content input[type="text"] { height: 28px; width: 90%; padding: 0 4px; text-align: center; font-size: 0.8rem; margin: 0; box-sizing: border-box; background: rgba(0,0,0,0.3); border: 1px solid var(--border); border-radius: 4px; color: white; }
    .cell-content input[type="text"]:focus { border-color: var(--accent); }
    .cell-content input[type="checkbox"] { margin: 0; width: 16px; height: 16px; cursor: pointer; accent-color: var(--accent); }
    .cell-content .action-btn { height: 28px; padding: 0 8px; font-size: 0.75rem; margin: 0; display: inline-flex; align-items: center; justify-content: center; border-radius: 4px; font-weight: 600; line-height: 1; }
    
    .truncate-text { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; display: block; width: 100%; line-height: 42px; }

    .col-name { width: 45%; text-align: left !important; padding-left: 10px !important; }
    .col-f-size { width: 25%; }
    .col-f-action { width: 30%; }
    
    .col-pin { width: 12%; }
    .col-note { width: 15%; }
    .col-midi { width: 18%; }
    .col-ch { width: 12%; }
    .col-en { width: 13%; }
    .col-s-action { width: 30%; }

    #logContainer { background: rgba(0,0,0,0.4); padding: 10px; font-family: 'Consolas', monospace; font-size: 0.75rem; color: #00ffaa; height: 200px; max-height: 200px; overflow-y: auto !important; white-space: pre-wrap; word-break: break-all; border-radius: 0 0 8px 8px; text-shadow: 0 0 2px rgba(0,255,170,0.3); }
    
    .progress-bg { background: rgba(0,0,0,0.3); border: 1px solid var(--border); border-radius: 8px; height: 12px; overflow: hidden; margin: 8px 0; box-shadow: inset 0 2px 4px rgba(0,0,0,0.5); }
    .progress-bar { background: linear-gradient(90deg, #0088ff, var(--accent)); height: 100%; width: 0%; transition: width 0.3s ease; box-shadow: 0 0 10px rgba(0, 240, 255, 0.5); }
    
    footer { text-align: center; color: var(--text-muted); font-size: 0.85rem; margin-top: 30px; margin-bottom: 15px; width: 100%; max-width: 1280px; text-shadow: 0 2px 4px rgba(0,0,0,0.5); }
  </style>
</head>
<body>

<header>
  <h1>KELENTANG ROBOT</h1>
  <div class="header-info">
    <span>IP : <strong style="color: var(--accent);">{{IP_ADDRESS}}</strong></span>
    <!-- Sinyal Wrapper selalu tampil, default abu-abu -->
    <span id="rssiWrapper" style="display:inline-flex; align-items:center; gap:6px;">Signal : <span id="rssiBarContainer" style="display:inline-flex; align-items:flex-end; gap:2px; height:14px; width:22px;"><span id="sig1" style="width:3px; height:25%; background:#555; border-radius:1px;"></span><span id="sig2" style="width:3px; height:50%; background:#555; border-radius:1px;"></span><span id="sig3" style="width:3px; height:75%; background:#555; border-radius:1px;"></span><span id="sig4" style="width:3px; height:100%; background:#555; border-radius:1px;"></span></span> <strong id="rssiVal" style="color: var(--accent);">-- dBm</strong></span>
    <span>ESP : <strong id="tempDisplay" style="color: var(--accent);">--.-°C</strong></span>
  </div>
</header>

<div class="dashboard-grid">

  <div class="card">
    <h2>Player Control</h2>
    <div id="playerStatus" style="font-size: 0.9rem; font-weight: 600; color: var(--text-main); margin-bottom: 5px;">Memeriksa status...</div>
    <div class="progress-bg"><div id="playerBar" class="progress-bar"></div></div>
    <div class="row" style="justify-content: space-between; font-size: 0.75rem; color: var(--text-muted); margin-bottom: 12px;">
      <span id="timeElapsed">0:00</span>
      <span id="modeDisplay" style="background: rgba(51, 65, 85, 0.6); padding: 3px 8px; border-radius: 6px; color: var(--accent); border: 1px solid var(--border);">--</span>
      <span id="timeRemaining">0:00</span>
    </div>
    <div class="row" style="justify-content: center; gap: 6px; margin-bottom: 0;">
      <button onclick="sendCommand('prev')" class="primary" style="flex:1; padding: 8px 4px;">Prev</button>
      <button onclick="sendCommand('start')" class="primary" style="flex:1.2; padding: 8px 4px;" id="btnStart">Play</button>
      <button onclick="sendCommand('next')" class="primary" style="flex:1; padding: 8px 4px;">Next</button>
      <button onclick="sendCommand('mode')" class="primary" style="flex:1; padding: 8px 4px;">Mode</button>
    </div>
  </div>

  <div class="card">
    <h2>File Manager</h2>
    <div class="row">
      <label for="fileInput" class="file-label" onclick="document.getElementById('fileInput').click()">Select MIDI File</label>
      <input type="file" id="fileInput" accept=".mid,.midi" style="display:none;" onchange="document.querySelector('label[for=\'fileInput\']').innerText = this.files[0].name" />
      <button onclick="uploadFile()" class="primary">Upload</button>
    </div>
    <div class="scroll-container">
      <table>
        <thead><tr><th class="col-name">Name</th><th class="col-f-size">Size</th><th class="col-f-action">Action</th></tr></thead>
      </table>
      <div class="scroll-body">
        <!-- Placeholder loading -->
        <table><tbody id="fileBody"><tr><td colspan="3" style="padding:15px; color:var(--text-muted);">Membaca SD Card...</td></tr></tbody></table>
      </div>
    </div>
    <div id="storageInfo" style="margin-top: 10px; font-size: 0.75rem; color: var(--text-muted); text-align: center; background: rgba(0,0,0,0.2); padding: 4px; border-radius: 4px;">Menghitung kapasitas...</div>
  </div>

  <div class="card">
    <h2>Actuator Duration</h2>
    <div style="font-size: 0.85rem; margin-bottom: 10px; color: var(--text-muted);">
      Current Duration : <strong id="currentTime" style="color: var(--accent); font-size: 1.1em;">...</strong> ms
    </div>
    <div class="row" style="margin-bottom: 0;">
      <input type="number" id="sTime" placeholder="Enter New Duration (ms)" />
      <button onclick="saveTime()" class="primary">Save</button>
    </div>
  </div>

  <div class="card">
    <h2>Actuator Manager</h2>
    <div class="row row-wrap" style="gap: 6px; margin-bottom: 8px;">
      <input type="number" id="sPin" placeholder="GPIO" style="flex: 1; min-width: 40px;" />
      <input type="text" id="sNote" placeholder="Note" style="flex: 1; min-width: 40px;" />
      <input type="number" id="sMidi" placeholder="MIDI" style="flex: 1; min-width: 40px;" />
      <input type="number" id="sChannel" placeholder="Channel" style="flex: 1; min-width: 40px;" />
    </div>
    <div class="row" style="gap: 6px; margin-bottom: 10px;">
      <button onclick="backupConfig()" class="primary" style="flex: 1; padding: 6px;">Backup</button>
      <input type="file" id="restoreInput" style="display:none;" onchange="restoreConfig()" />
      <button onclick="document.getElementById('restoreInput').click()" class="danger" style="flex: 1; padding: 6px;">Restore</button>
      <button onclick="addSolenoid()" class="primary" style="flex: 1; padding: 6px;">Add</button>
      <button id="globalEditBtn" onclick="toggleEditMode()" class="primary" style="flex: 1; padding: 6px;">Edit</button>
    </div>
    <div class="scroll-container">
      <table>
        <thead>
          <tr>
            <th class="col-pin">GPIO</th>
            <th class="col-note">Note</th>
            <th class="col-midi">MIDI</th>
            <th class="col-ch">Ch</th>
            <th class="col-en">En</th>
            <th class="col-s-action">Action</th>
          </tr>
        </thead>
      </table>
      <div class="scroll-body">
        <!-- Placeholder loading -->
        <table><tbody id="solenoidBody"><tr><td colspan="6" style="padding:15px; color:var(--text-muted);">Membaca konfigurasi alat...</td></tr></tbody></table>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>WiFi Configuration</h2>
    <div style="display: flex; flex-direction: column; gap: 10px;">
      <input type="text" id="wifiSsid" placeholder="SSID" />
      <input type="text" id="wifiPass" placeholder="Password" />
      <div class="row" style="justify-content: space-between; background: rgba(0,0,0,0.2); padding: 8px 12px; border-radius: 8px;">
        <label style="font-size: 0.9rem; color: var(--text-main);">Enable WiFi Station</label>
        <label class="switch">
          <input type="checkbox" id="wifiEnable">
          <span class="slider"></span>
        </label>
      </div>
      <button id="btnSaveWifiSTA" onclick="saveWifi()" class="primary" style="width: 100%; margin-top: 5px;">Save and Apply</button>
    </div>
  </div>

  <div class="card">
    <h2>Update Firmware</h2>
    <div style="display: flex; flex-direction: column; gap: 8px;">
      <div style="font-size: 0.85rem;">Version : <strong style="color: var(--accent); font-size: 1.1em;">{{FW_VERSION}}</strong></div>
      <div class="row" style="margin-bottom: 0; margin-top: 5px;">
        <label for="otaBinInput" class="file-label" onclick="document.getElementById('otaBinInput').click()">Select BIN File</label>
        <input type="file" id="otaBinInput" accept=".bin" style="display:none;" onchange="document.querySelector('label[for=\'otaBinInput\']').innerText = this.files[0].name" />
        <button onclick="uploadOta()" class="danger">Update</button>
      </div>
      <div style="font-size: 0.75rem; color: var(--text-muted); margin-top: 5px;">
        <span>Last Update : {{LAST_UPDATE}}</span>
      </div>
      <div class="progress-bg"><div id="otaBar" class="progress-bar" style="background: linear-gradient(90deg, #ff0055, #ff6600); box-shadow: 0 0 10px rgba(255,0,85,0.5);"></div></div>
    </div>
  </div>

  <div class="card">
    <h2>System Log</h2>
    <div class="scroll-container">
      <div id="logContainer"></div>
    </div>
    <button onclick="document.getElementById('logContainer').innerText = ''" class="danger" style="margin-top: 10px; align-self: flex-start;">Clear Log</button>
  </div>

</div>

<footer>
  &copy; 2026 AN ELECTRONIC | Mataram, Nusa Tenggara Barat
</footer>

<script>
document.addEventListener("wheel", function(e){
    if(document.activeElement && document.activeElement.type === "number") e.preventDefault();
}, { passive: false });
document.addEventListener("keydown", function(e){
    if(document.activeElement && document.activeElement.type === "number"){
        if(e.key === "ArrowUp" || e.key === "ArrowDown") e.preventDefault();
    }
});

const noteMap = {{NOTE_MAP}};
const allowedPins = {{ALLOWED_PINS}};

let isEditMode = false;
let dataTimer = null;
let isFetching = false;
let isSavingWifi = false;

function toggleEditMode() {
    isEditMode = !isEditMode;
    const btn = document.getElementById('globalEditBtn');
    btn.innerText = isEditMode ? "Save" : "Edit";
    btn.className = isEditMode ? "danger" : "primary";
    
    if (!isEditMode) {
        saveAll();
    } else {
        const sBody = document.getElementById('solenoidBody');
        if (sBody) {
            const inputs = sBody.querySelectorAll('input');
            inputs.forEach(input => input.disabled = false);
            const btns = sBody.querySelectorAll('.action-btn');
            btns.forEach(b => b.disabled = true);
        }
    }
}

async function saveAll() {
    const sBody = document.getElementById('solenoidBody');
    const rows = sBody.querySelectorAll('tr');
    let solenoids = [];
    
    rows.forEach(row => {
        const pinText = row.querySelector('.col-pin .cell-content');
        if(!pinText) return;
        const pin = parseInt(pinText.innerText);
        const note = row.querySelector('.col-note .cell-content').innerText;
        const midiInput = row.querySelector(`input[id^="editMidi-"]`);
        const chInput = row.querySelector(`input[id^="editCh-"]`);
        const enInput = row.querySelector(`input[id^="editEn-"]`);
        
        if (midiInput && chInput && enInput) {
            const midi = parseInt(midiInput.value);
            const ch = parseInt(chInput.value);
            const enabled = enInput.checked;
            solenoids.push({pin, note, midi, ch, en: enabled ? 1 : 0});
        }
    });
    
    sBody.querySelectorAll('input').forEach(input => input.disabled = true);
    
    await fetch('/api/solenoids', { 
        method: 'POST', 
        body: JSON.stringify(solenoids), 
        headers: {'Content-Type': 'application/json'} 
    });
    
    await loadStatic();
}

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('sNote').addEventListener('input', (e) => {
        const noteInput = e.target.value.toLowerCase().trim();
        const midiInput = document.getElementById('sMidi');
        if (noteMap[noteInput]) midiInput.value = noteMap[noteInput];
        else midiInput.value = "";
    });
});

async function uploadOta() {
    const fileInput = document.getElementById('otaBinInput');
    if (!fileInput.files[0]) { alert('Select .bin file first!'); return; }
    
    const bar = document.getElementById('otaBar');
    bar.style.width = '0%';
    
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/update", true);
    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) bar.style.width = (e.loaded / e.total) * 100 + '%';
    };
    xhr.onload = () => {
        if (xhr.status === 200) { alert('Update Success! Restarting...'); location.reload(); }
        else { alert('Update Failed'); bar.style.width = '0%'; }
    };
    xhr.send(fileInput.files[0]);
}

async function sendCommand(cmd) {
    await fetch('/api/player/cmd?action='+cmd, { method: 'POST' });
    if(dataTimer) clearTimeout(dataTimer);
    loadDynamic();
}

// ==== OPTIMASI FETCH DATA ====
// Memisahkan dynamic data (yg butuh update realtime) dan static (yg butuh fetch sekali/saat diubah)

async function loadDynamic() {
    if (isFetching || isSavingWifi) return; 
    isFetching = true;
    
    try {
        const res = await fetch('/api/status?t=' + Date.now());
        if (res.ok) {
            const data = await res.json();
            
            // Update RSSI
            const rssiVal = document.getElementById('rssiVal');
            const rssi = data.rssi;
            const sig1 = document.getElementById('sig1'); const sig2 = document.getElementById('sig2');
            const sig3 = document.getElementById('sig3'); const sig4 = document.getElementById('sig4');
            
            sig1.style.background = '#555'; sig2.style.background = '#555';
            sig3.style.background = '#555'; sig4.style.background = '#555';
            
            if (rssi !== 0 && !isNaN(rssi)) {
                rssiVal.innerText = rssi + ' dBm';
                if (rssi >= -90) sig1.style.background = '#ff0055'; 
                if (rssi >= -80) { sig1.style.background = '#ffcc00'; sig2.style.background = '#ffcc00'; }
                if (rssi >= -70) { sig1.style.background = '#00f0ff'; sig2.style.background = '#00f0ff'; sig3.style.background = '#00f0ff'; }
                if (rssi >= -60) { sig1.style.background = '#00ffaa'; sig2.style.background = '#00ffaa'; sig3.style.background = '#00ffaa'; sig4.style.background = '#00ffaa'; }
            } else { 
                rssiVal.innerText = '-- dBm';
            }

            // Update Temperature
            document.getElementById('tempDisplay').innerText = data.temp !== "--" ? Number(data.temp).toFixed(1) + '°C' : '--.-°C';
            
            // Update Player Status
            const player = data.player;
            const cleanName = player.file.replace(/\//g, '').replace(/\.(mid|midi)$/i, '');
            document.getElementById('playerStatus').innerText = player.playing ? "Playing : " + cleanName : (player.paused ? "Paused : " + cleanName : "Stopped : " + cleanName);
            document.getElementById('btnStart').innerText = player.playing ? "Pause" : "Play";
            document.getElementById('modeDisplay').innerText = player.auto ? "Continuous" : "PlayOnce";
            
            // Fix bug Progress Bar
            const dur = Number(player.duration) || 0;
            const el = Number(player.elapsed) || 0;
            let barWidth = dur > 0 ? (el / dur * 100) : 0;
            barWidth = Math.min(100, Math.max(0, barWidth));
            document.getElementById('playerBar').style.width = barWidth + '%';
            
            const remaining = Math.max(0, dur - el);
            document.getElementById('timeElapsed').innerText = formatTime(el);
            document.getElementById('timeRemaining').innerText = formatTime(remaining);
        }
    } catch (e) { console.error("Error loadDynamic:", e); }
    
    isFetching = false;
    if (!isSavingWifi) dataTimer = setTimeout(loadDynamic, 1000);
}

async function loadStatic() {
    const t = Date.now();
    // 1. Fetch Solenoids
    try {
        const resS = await fetch('/api/solenoids?t=' + t);
        if (resS.ok && !isEditMode) {
            const solenoids = await resS.json();
            const sBody = document.getElementById('solenoidBody');
            
            if (solenoids.length === 0) {
                sBody.innerHTML = `<tr><td colspan="6" style="padding:15px; color:var(--text-muted);">Belum ada aktuator disetel</td></tr>`;
            } else {
                sBody.innerHTML = '';
                solenoids.forEach(s => { 
                    sBody.innerHTML += `<tr>
                    <td class="col-pin"><div class="cell-content">${s.pin}</div></td>
                    <td class="col-note" title="${s.note}"><div class="cell-content truncate-text">${s.note}</div></td>
                    <td class="col-midi"><div class="cell-content"><input type="text" id="editMidi-${s.pin}" value="${s.midi}" disabled oninput="this.value = this.value.replace(/[^0-9]/g, '')"></div></td>
                    <td class="col-ch"><div class="cell-content"><input type="text" id="editCh-${s.pin}" value="${s.ch}" disabled oninput="this.value = this.value.replace(/[^0-9]/g, '')"></div></td>
                    <td class="col-en"><div class="cell-content"><input type="checkbox" id="editEn-${s.pin}" ${s.en ? 'checked' : ''} disabled></div></td>
                    <td class="col-s-action">
                        <div class="cell-content" style="gap:4px;">
                            <button class="primary action-btn" style="flex:1;" onclick="testSolenoid(${s.pin})">Test</button>
                            <button class="danger action-btn" style="flex:1;" onclick="removeSolenoid(${s.pin})">Del</button>
                        </div>
                    </td>
                </tr>`; });
            }
        }
    } catch (e) { }

    // 2. Fetch File
    try {
        const resF = await fetch('/api/files?t=' + t); 
        if (resF.ok) {
            const filesRes = await resF.json();
            const fBody = document.getElementById('fileBody'); 
            
            if (filesRes.files.length === 0) {
                fBody.innerHTML = `<tr><td colspan="3" style="padding:15px; color:var(--text-muted);">Tidak ada file MIDI di SD Card</td></tr>`;
            } else {
                fBody.innerHTML = '';
                filesRes.files.forEach(f => { 
                  fBody.innerHTML += `<tr>
                    <td class="col-name" title="${f.name}"><div class="cell-content truncate-text" style="padding-left: 10px; text-align: left;">${f.name}</div></td>
                    <td class="col-f-size"><div class="cell-content">${formatSize(f.size)}</div></td>
                    <td class="col-f-action"><div class="cell-content"><button class="danger action-btn" style="width: 90%;" onclick="deleteFile('${f.name}')">Delete</button></div></td>
                  </tr>`; 
                });
            }
            
            const sInfo = document.getElementById('storageInfo');
            if (filesRes.storage) {
                const used = filesRes.storage.total - filesRes.storage.free;
                sInfo.innerText = `Total : ${formatSize(filesRes.storage.total)} | Used : ${formatSize(used)} | Free : ${formatSize(filesRes.storage.free)}`;
            } else {
                sInfo.innerText = 'SD Card not detected';
            }
        }
    } catch (e) { }

    // 3. Fetch Time
    try {
        const resT = await fetch('/api/time?t=' + t); 
        if (resT.ok) document.getElementById('currentTime').innerText = await resT.text();
    } catch (e) { }
}

function formatTime(ms) {
  const totalSeconds = Math.floor(ms / 1000);
  const mins = Math.floor(totalSeconds / 60);
  const secs = totalSeconds % 60;
  return mins + ":" + (secs < 10 ? "0" : "") + secs;
}

async function loadWifi() {
    try {
        const res = await fetch('/api/wifi?t=' + Date.now());
        const config = await res.json();
        document.getElementById('wifiSsid').value = config.ssid || "";
        document.getElementById('wifiPass').value = config.pass || "";
        document.getElementById('wifiEnable').checked = config.enable || false;
    } catch (e) { }
}

function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
}

async function testSolenoid(pin) { await fetch('/api/solenoid/test?pin='+pin, { method: 'POST' }); }

async function backupConfig() {
  const res = await fetch('/api/backup');
  const blob = await res.blob();
  const url = window.URL.createObjectURL(blob);
  const a = document.createElement('a'); a.href = url; a.download = 'backup.json'; a.click();
}

async function restoreConfig() {
  const input = document.getElementById('restoreInput'); if(!input.files[0]) return;
  const text = await input.files[0].text();
  await fetch('/api/restore', { method: 'POST', body: text });
  input.value = ''; 
  await loadStatic();
}

async function saveTime() {
  const timeInput = document.getElementById('sTime'); const currentTimeText = document.getElementById('currentTime').innerText;
  const newTime = timeInput.value.trim();
  if (!newTime || isNaN(parseInt(newTime))) { alert('Masukkan durasi angka yang valid!'); return; }
  if (newTime === currentTimeText) { alert('Duration is the same, not saved'); return; }
  await fetch('/api/time', { method: 'POST', body: newTime });
  timeInput.value = ''; 
  await loadStatic();
}

async function uploadFile() {
  const fileInput = document.getElementById('fileInput'); if (!fileInput.files[0]) { alert('Select MIDI file first!'); return; }
  const formData = new FormData(); formData.append("file", fileInput.files[0]);
  const response = await fetch('/upload', { method: 'POST', body: formData });
  const text = await response.text();
  if (text === "SKIP") alert('File already exists on SD Card!');
  else if (text === "OK") { 
      fileInput.value = ''; document.querySelector('label[for=\'fileInput\']').innerText = 'Select MIDI File'; 
      await loadStatic(); 
  }
  else alert('Failed to upload file');
}

async function addSolenoid() {
  const pinRaw = document.getElementById('sPin').value.trim();
  const noteRaw = document.getElementById('sNote').value.trim();
  const midiRaw = document.getElementById('sMidi').value.trim();
  const chRaw = document.getElementById('sChannel').value.trim();
  
  if (!pinRaw || !midiRaw) { alert('Kolom GPIO Pin dan MIDI Note wajib diisi!'); return; }
  
  const pin = parseInt(pinRaw);
  const midi = parseInt(midiRaw);
  const ch = chRaw === "" ? 0 : parseInt(chRaw);
  
  if (isNaN(pin) || isNaN(midi) || isNaN(ch)) { alert('GPIO, MIDI, dan Channel harus berupa angka!'); return; }
  if (ch < 0 || ch > 16) { alert('MIDI Channel harus antara 0 dan 16!'); return; }
  if (!allowedPins.includes(pin)) { alert('GPIO Pin tidak valid!'); return; }
  
  const resS = await fetch('/api/solenoids');
  let solenoids = await resS.json();
  
  if (solenoids.some(s => s.pin === pin)) { alert('GPIO Pin sudah digunakan!'); return; }
  
  const note = noteRaw || '-';
  solenoids.push({pin: pin, note: note, midi: midi, ch: ch});
  await fetch('/api/solenoids', { method: 'POST', body: JSON.stringify(solenoids) });
  
  document.getElementById('sPin').value = '';
  document.getElementById('sNote').value = '';
  document.getElementById('sMidi').value = '';
  document.getElementById('sChannel').value = '';
  
  if (document.activeElement) document.activeElement.blur();
  await loadStatic();
}

async function removeSolenoid(pin) {
  if (document.activeElement) document.activeElement.blur();
  const resS = await fetch('/api/solenoids');
  let solenoids = await resS.json();
  solenoids = solenoids.filter(s => s.pin !== pin);
  await fetch('/api/solenoids', { method: 'POST', body: JSON.stringify(solenoids) });
  await loadStatic();
}

async function saveWifi() {
  const ssid = document.getElementById('wifiSsid').value; 
  const pass = document.getElementById('wifiPass').value; 
  const enable = document.getElementById('wifiEnable').checked;
  if (!ssid && enable) { alert('SSID is required if STA is enabled!'); return; }
  
  isSavingWifi = true;
  if (dataTimer) clearTimeout(dataTimer);

  const btn = document.getElementById('btnSaveWifiSTA');
  btn.innerText = "Applying...";
  btn.disabled = true;
  
  try {
      const res = await fetch('/api/wifi', { 
          method: 'POST', 
          body: JSON.stringify({ssid: ssid, pass: pass, enable: enable, restart: false}) 
      });
      if (res.ok) alert('WiFi settings applied! Trying to connect in background...');
      else alert('Failed to save settings');
  } catch(e) {
      console.log("Connection interrupted during WiFi save (expected behavior)");
  }

  btn.innerText = "Save and Apply";
  btn.disabled = false;
  
  isSavingWifi = false;
  dataTimer = setTimeout(loadDynamic, 2000); 
  loadWifi();
}

async function deleteFile(name) { 
    await fetch('/api/files?name='+name, { method: 'DELETE' }); 
    await loadStatic(); 
}

// Mulai Fetch Pertama Kali
loadStatic().then(() => {
    loadDynamic();
}); 
loadWifi();

let ws = null;
let wsPingInterval = null;

function initWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
    }
    ws = new WebSocket('ws://' + window.location.hostname + '/ws');
    ws.onopen = () => {
        ws.send("ping");
        if (wsPingInterval) clearInterval(wsPingInterval);
        wsPingInterval = setInterval(() => {
            if (ws && ws.readyState === WebSocket.OPEN) {
                try { ws.send("ping"); } catch (e) { ws.close(); }
            } else { if (ws) ws.close(); }
        }, 3000);
    };
    ws.onmessage = (event) => {
        const logContainer = document.getElementById('logContainer');
        if (logContainer) {
            logContainer.innerText += event.data;
            if (logContainer.innerText.length > 5000) logContainer.innerText = logContainer.innerText.substring(logContainer.innerText.length - 5000);
            logContainer.scrollTop = logContainer.scrollHeight;
        }
    };
    ws.onerror = (err) => { if (ws) ws.close(); };
    ws.onclose = () => {
        if (wsPingInterval) clearInterval(wsPingInterval);
        setTimeout(initWebSocket, 2000);
    };
}
initWebSocket();
</script>
</body>
</html>
)rawliteral";

esp_err_t root_handler(httpd_req_t *req) {
  const char *page = (WiFi.getMode() == WIFI_AP) ? htmlPageAP : htmlPage;
  String output = String(page);
  output.replace("{{FW_VERSION}}", FW_VERSION);
  Preferences prefs;
  prefs.begin("ota", true);
  output.replace("{{LAST_UPDATE}}", prefs.getString("last", "-"));
  prefs.end();
  output.replace("{{NOTE_MAP}}", NOTE_MAP_JS);

  String pinsJs = "[";
  for (size_t i = 0; i < sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0]); i++) {
    pinsJs += String(ALLOWED_PINS[i]);
    if (i < (sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0])) - 1)
      pinsJs += ", ";
  }
  pinsJs += "]";
  output.replace("{{ALLOWED_PINS}}", pinsJs);

  String ipAddr = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString()
                                              : WiFi.localIP().toString();
  output.replace("{{IP_ADDRESS}}", ipAddr);

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, output.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t upload_handler(httpd_req_t *req) {
  char buf[1024];
  size_t recv_len;
  String filename = "";
  bool headersParsed = false;
  size_t header_offset = 0;
  File file;
  if (req->content_len > 0) {
    while ((recv_len = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
      if (!headersParsed) {
        String chunk(buf, recv_len);
        int namePos = chunk.indexOf("filename=\"");
        if (namePos >= 0) {
          int start = namePos + 10;
          int end = chunk.indexOf("\"", start);
          if (end > start)
            filename = sanitizeFilename(chunk.substring(start, end));
        }
        int headerEnd = chunk.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
          headersParsed = true;
          header_offset = headerEnd + 4;
          if (filename.length() > 0 && SD.exists(filename.c_str())) {
            return httpd_resp_send(req, "SKIP", 4);
          }
          if (filename.length() > 0 &&
              (filename.endsWith(".mid") || filename.endsWith(".midi"))) {
            file = sdcard.openFile(filename.c_str(), FILE_WRITE);
            if (!file)
              return ESP_FAIL;
            if (recv_len > header_offset)
              file.write((uint8_t *)(buf + header_offset),
                         recv_len - header_offset);
          } else
            return ESP_FAIL;
        }
      } else if (file)
        file.write((uint8_t *)buf, recv_len);
    }
  }
  if (file) {
    file.close();
    needsScan = true;
    return httpd_resp_send(req, "OK", 2);
  }
  return ESP_FAIL;
}

esp_err_t api_solenoids_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    if (digitalRead(PIN_SD_DET) == HIGH) {
      while (solenoid.getCount() > 0)
        solenoid.removeSolenoid(solenoid.getItems()[0].getPin());
      return httpd_resp_send(req, "[]", 2);
    }

    if (solenoid.getCount() == 0)
      solenoid.loadConfig();

    String json = "[";
    Solenoid *items = solenoid.getItems();
    for (uint8_t i = 0; i < solenoid.getCount(); i++) {
      json += "{\"pin\":" + String(items[i].getPin()) + ",\"note\":\"" +
              items[i].getNote() +
              "\",\"midi\":" + String(items[i].getMidiNote()) +
              ",\"ch\":" + String(items[i].getMidiChannel()) +
              ",\"en\":" + String(items[i].isEnabled() ? 1 : 0) + "}";
      if (i < solenoid.getCount() - 1)
        json += ",";
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_POST) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret > 0) {
      std::vector<std::pair<int, bool>> old_states;
      for (uint8_t i = 0; i < solenoid.getCount(); i++) {
        old_states.push_back({solenoid.getItems()[i].getPin(),
                              solenoid.getItems()[i].isEnabled()});
      }

      while (solenoid.getCount() > 0)
        solenoid.removeSolenoid(solenoid.getItems()[0].getPin());
      String data(buf);
      int start = 0;
      while ((start = data.indexOf("{\"pin\":", start)) >= 0) {
        int end = data.indexOf("}", start);
        String obj = data.substring(start, end + 1);
        int pStart = obj.indexOf(":") + 1;
        int pComma = obj.indexOf(",", pStart);
        int pin = obj.substring(pStart, pComma).toInt();
        int nStart = obj.indexOf(":", pComma) + 2;
        int nEnd = obj.indexOf("\"", nStart);
        String note = obj.substring(nStart, nEnd);
        int mStart = obj.indexOf(":", nEnd + 1) + 1;
        int mComma = obj.indexOf(",", mStart);
        int midi = obj.substring(mStart, mComma).toInt();
        int cStart = obj.indexOf(":", mComma) + 1;
        int cComma = obj.indexOf(",", cStart);
        int channel = obj.substring(cStart, cComma).toInt();
        int eStart = obj.indexOf(":", cComma) + 1;
        int eEnd = obj.indexOf("}", eStart);
        bool enabled = (obj.substring(eStart, eEnd).toInt() == 1);
        solenoid.addSolenoid(pin, note, midi, channel, enabled);
        start = end;
      }

      for (uint8_t i = 0; i < solenoid.getCount(); i++) {
        int pin = solenoid.getItems()[i].getPin();
        bool new_enabled = solenoid.getItems()[i].isEnabled();
        for (auto &old : old_states) {
          if (old.first == pin && old.second != new_enabled) {
            LOG("[SOLENOID]: Solenoid %d changed to %s\n", pin,
                new_enabled ? "ENABLED" : "DISABLED");
          }
        }
      }

      solenoid.saveConfig();
      httpd_resp_send(req, "OK", 2);
    }
    return ESP_OK;
  }
  return ESP_FAIL;
}

esp_err_t api_solenoid_test_handler(httpd_req_t *req) {
  if (req->method == HTTP_POST) {
    char buf[128];
    size_t len = httpd_req_get_url_query_len(req);
    if (len < sizeof(buf)) {
      httpd_req_get_url_query_str(req, buf, len + 1);
      char pinVal[8];
      if (httpd_query_key_value(buf, "pin", pinVal, sizeof(pinVal)) == ESP_OK) {
        solenoid.test(String(pinVal).toInt());
        return httpd_resp_send(req, "OK", 2);
      }
    }
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Pin");
  }
  return HTTPD_404_NOT_FOUND;
}

esp_err_t api_backup_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    File file = SD.open("/solenoids.txt", FILE_READ);
    if (!file)
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "Config not found");
    String json = "{\"solenoids\":[";
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
        continue;
      int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
      json += "{\"pin\":" + line.substring(0, c1) + ",\"note\":\"" +
              line.substring(c1 + 1, c2) +
              ",\"midi\":" + line.substring(c2 + 1) + "},";
    }
    file.close();
    if (json.endsWith(","))
      json.remove(json.length() - 1);
    json += "],\"duration\":" + String(player.getSolenoidTime()) + "}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"backup.json\"");
    return httpd_resp_send(req, json.c_str(), json.length());
  }
  return HTTPD_404_NOT_FOUND;
}

esp_err_t api_restore_handler(httpd_req_t *req) {
  if (req->method == HTTP_POST) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      buf[ret] = 0;
      String data(buf);
      int dStart = data.indexOf("\"duration\":") + 11;
      int dEnd = data.indexOf(",", dStart);
      if (dEnd == -1)
        dEnd = data.indexOf("}", dStart);
      player.setSolenoidTime(data.substring(dStart, dEnd).toInt());
      File file = SD.open("/solenoids.txt", FILE_WRITE);
      int start = data.indexOf("{\"pin\":");
      while (start >= 0) {
        int end = data.indexOf("}", start);
        String obj = data.substring(start, end + 1);
        int p1 = obj.indexOf(":") + 1, p2 = obj.indexOf(",", p1),
            p3 = obj.indexOf(":", p2) + 2, p4 = obj.indexOf("\"", p3),
            p5 = obj.indexOf(":", p4) + 1, p6 = obj.indexOf("}", p5);
        file.println(obj.substring(p1, p2) + "," + obj.substring(p3, p4) + "," +
                     obj.substring(p5, p6));
        start = data.indexOf("{\"pin\":", end);
      }
      file.close();
      solenoid.loadConfig();
      return httpd_resp_send(req, "OK", 2);
    }
  }
  return ESP_FAIL;
}

esp_err_t api_time_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    String json = String(player.getSolenoidTime());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_POST) {
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      player.setSolenoidTime(String(buf).toInt());
      httpd_resp_send(req, "OK", 2);
    }
    return ESP_OK;
  }
  return ESP_FAIL;
}

esp_err_t api_files_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    String json = "{\"files\":[";
    if (digitalRead(PIN_SD_DET) == LOW) {
      File root = SD.open("/");
      File file = root.openNextFile();
      bool first = true;
      while (file) {
        String name = file.name();
        if (!file.isDirectory() &&
            (name.endsWith(".mid") || name.endsWith(".midi"))) {
          if (!first)
            json += ",";
          json += "{\"name\":\"" + name + "\",\"size\":" + String(file.size()) +
                  "}";
          first = false;
        }
        file = root.openNextFile();
      }
      json += "], \"storage\":{\"total\":" + String(SD.totalBytes()) +
              ", \"free\":" + String(SD.totalBytes() - SD.usedBytes()) + "}}";
      root.close();
    } else
      json += "], \"storage\":null}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_DELETE) {
    char buf[256];
    size_t len = httpd_req_get_url_query_len(req);
    if (len < sizeof(buf)) {
      httpd_req_get_url_query_str(req, buf, len + 1);
      char name[128];
      if (httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
        String decodedName = String(name);
        decodedName.replace("%20", " ");
        if (sdcard.deleteFile(("/" + decodedName).c_str())) {
          needsScan = true;
          return httpd_resp_send(req, "OK", 2);
        }
      }
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Delete Failed");
  }
  return ESP_FAIL;
}

esp_err_t api_wifi_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    String ssid, pass;
    bool enable;
    wifiManager.getSettings(ssid, pass, enable);
    String json = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass +
                  "\",\"enable\":" + (enable ? "true" : "false") + "}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_POST) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      String data(buf);
      String ssid = "", pass = "";
      bool enable = false;
      int sIdx = data.indexOf("\"ssid\":\"");
      if (sIdx != -1) {
        int start = sIdx + 8;
        int end = data.indexOf("\"", start);
        if (end != -1)
          ssid = data.substring(start, end);
      }
      int pIdx = data.indexOf("\"pass\":\"");
      if (pIdx != -1) {
        int start = pIdx + 8;
        int end = data.indexOf("\"", start);
        if (end != -1)
          pass = data.substring(start, end);
      }
      int eIdx = data.indexOf("\"enable\":");
      if (eIdx != -1) {
        int colonIdx = data.indexOf(":", eIdx);
        if (colonIdx != -1) {
          String val = data.substring(colonIdx + 1);
          val.trim();
          if (val.startsWith("true"))
            enable = true;
          else if (val.startsWith("false"))
            enable = false;
        }
      }
      int rIdx = data.indexOf("\"restart\":");
      bool restart = false;
      if (rIdx != -1) {
        int colonIdx = data.indexOf(":", rIdx);
        if (colonIdx != -1) {
          String val = data.substring(colonIdx + 1);
          val.trim();
          if (val.startsWith("true"))
            restart = true;
        }
      }
      wifiManager.saveSettings(ssid, pass, enable, restart);
      httpd_resp_send(req, "OK", 2);
    }
    return ESP_OK;
  }
  return ESP_FAIL;
}

String sanitizeFilename(String filename) {
  String clean = "/";
  filename.toLowerCase();
  int lastSlash = filename.lastIndexOf('/');
  if (lastSlash >= 0)
    filename = filename.substring(lastSlash + 1);
  int lastBackslash = filename.lastIndexOf('\\');
  if (lastBackslash >= 0)
    filename = filename.substring(lastBackslash + 1);
  for (size_t i = 0; i < filename.length(); i++) {
    char c = filename[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
        c == '_' || c == '-')
      clean += c;
    else
      clean += '_';
  }
  return clean;
}

esp_err_t ws_handler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);

  if (ws_mutex != NULL)
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
  if (std::find(ws_clients.begin(), ws_clients.end(), fd) == ws_clients.end()) {
    ws_clients.push_back(fd);
    Serial.printf("[WS] Client connected: %d\n", fd);
  }
  if (ws_mutex != NULL)
    xSemaphoreGive(ws_mutex);

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK)
    return ret;

  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
    if (ws_mutex != NULL)
      xSemaphoreTake(ws_mutex, portMAX_DELAY);
    auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
    if (it != ws_clients.end()) {
      ws_clients.erase(it);
      Serial.printf("[WS] Client disconnected: %d\n", fd);
    }
    if (ws_mutex != NULL)
      xSemaphoreGive(ws_mutex);
    return ESP_OK;
  }

  if (ws_pkt.len > 0) {
    uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
    if (buf) {
      ws_pkt.payload = buf;
      ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
      if (ret == ESP_OK) {
        buf[ws_pkt.len] = 0;
      }
      free(buf);
    }
  }
  return ESP_OK;
}

auto reboot_handler = [](httpd_req_t *req) {
  httpd_resp_send(req, "OK", 2);
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP.restart();
  return ESP_OK;
};

void WebServerManager::beginAPMinimal() {
  if (active)
    return;
  if (ws_mutex == NULL)
    ws_mutex = xSemaphoreCreateMutex();
  if (log_queue == NULL) {
    log_queue = xQueueCreate(20, 256);
    xTaskCreate(ws_sender_task, "ws_sender", 4096, NULL, 1, NULL);
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 16384;
  if (httpd_start(&server, &config) != ESP_OK)
    return;

  httpd_uri_t root_uri = {"/", HTTP_GET, root_handler, nullptr};
  httpd_uri_t wifi_get_uri = {"/api/wifi", HTTP_GET, api_wifi_handler, nullptr};
  httpd_uri_t wifi_post_uri = {"/api/wifi", HTTP_POST, api_wifi_handler,
                               nullptr};
  httpd_uri_t reboot_post_uri = {"/api/reboot", HTTP_POST, reboot_handler,
                                 nullptr};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &wifi_get_uri);
  httpd_register_uri_handler(server, &wifi_post_uri);
  httpd_register_uri_handler(server, &reboot_post_uri);

  active = true;
}

void WebServerManager::beginSTAFull() {
  if (active)
    return;

  if (ws_mutex == NULL)
    ws_mutex = xSemaphoreCreateMutex();
  if (log_queue == NULL) {
    log_queue = xQueueCreate(20, 256);
    xTaskCreate(ws_sender_task, "ws_sender", 4096, NULL, 1, NULL);
  }

  vTaskDelay(pdMS_TO_TICKS(2000));

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  if (MDNS.begin("mydashboard")) {
    MDNS.addService("http", "tcp", 80);
  }
  NBNS.begin("mydashboard");

  temperature_sensor_config_t ts_cfg =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  temperature_sensor_install(&ts_cfg, &tempHandle);
  temperature_sensor_enable(tempHandle);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 20;
  config.max_open_sockets = 7;
  config.stack_size = 16384;
  if (httpd_start(&server, &config) != ESP_OK)
    return;

  // ==== API GABUNGAN STATUS ====
  httpd_uri_t status_uri = {
      "/api/status", HTTP_GET,
      [](httpd_req_t *req) {
        int rssi = (WiFi.getMode() == WIFI_STA) ? WiFi.RSSI() : 0;
        float temp = 0.0;
        if (tempHandle != NULL)
          temperature_sensor_get_celsius(tempHandle, &temp);

        String file = String(sdcard.getCurrentFile());
        if (file.length() == 0)
          file = "No file";

        char json[512];
        snprintf(
            json, sizeof(json),
            "{\"rssi\":%d,\"temp\":%.2f,\"player\":{\"playing\":%s,\"paused\":%"
            "s,\"auto\":%s,\"file\":\"%s\",\"duration\":%lu,\"elapsed\":%lu}}",
            rssi, temp, player.isPlaying() ? "true" : "false",
            player.isPaused() ? "true" : "false",
            player.isAutoMode() ? "true" : "false", file.c_str(),
            (unsigned long)(player.getDurationUS() / 1000),
            (unsigned long)(player.getElapsedUS() / 1000));

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
      },
      nullptr};

  httpd_uri_t root_uri = {"/", HTTP_GET, root_handler, nullptr};
  httpd_uri_t upload_uri = {"/upload", HTTP_POST, upload_handler, nullptr};
  httpd_uri_t solenoids_get_uri = {"/api/solenoids", HTTP_GET,
                                   api_solenoids_handler, nullptr};
  httpd_uri_t solenoids_post_uri = {"/api/solenoids", HTTP_POST,
                                    api_solenoids_handler, nullptr};
  httpd_uri_t solenoid_test_uri = {"/api/solenoid/test", HTTP_POST,
                                   api_solenoid_test_handler, nullptr};
  httpd_uri_t backup_uri = {"/api/backup", HTTP_GET, api_backup_handler,
                            nullptr};
  httpd_uri_t restore_uri = {"/api/restore", HTTP_POST, api_restore_handler,
                             nullptr};
  httpd_uri_t time_get_uri = {"/api/time", HTTP_GET, api_time_handler, nullptr};
  httpd_uri_t time_post_uri = {"/api/time", HTTP_POST, api_time_handler,
                               nullptr};
  httpd_uri_t files_get_uri = {"/api/files", HTTP_GET, api_files_handler,
                               nullptr};
  httpd_uri_t files_delete_uri = {"/api/files", HTTP_DELETE, api_files_handler,
                                  nullptr};
  httpd_uri_t wifi_get_uri = {"/api/wifi", HTTP_GET, api_wifi_handler, nullptr};
  httpd_uri_t wifi_post_uri = {"/api/wifi", HTTP_POST, api_wifi_handler,
                               nullptr};
  httpd_uri_t reboot_post_uri = {"/api/reboot", HTTP_POST, reboot_handler,
                                 nullptr};

  httpd_uri_t player_cmd_uri = {
      "/api/player/cmd", HTTP_POST,
      [](httpd_req_t *req) {
        char buf[64];
        size_t len = httpd_req_get_url_query_len(req);
        if (len < sizeof(buf)) {
          httpd_req_get_url_query_str(req, buf, len + 1);
          char action[16];
          if (httpd_query_key_value(buf, "action", action, sizeof(action)) ==
              ESP_OK) {
            String cmd(action);
            if (cmd == "start") {
              if (player.isPlaying())
                player.pause();
              else
                player.play();
            } else if (cmd == "next")
              player.nextFile();
            else if (cmd == "prev")
              player.prevFile();
            else if (cmd == "mode")
              player.toggleMode();
            return httpd_resp_send(req, "OK", 2);
          }
        }
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Invalid Command");
      },
      nullptr};

  httpd_uri_t ota_uri = {
      "/update", HTTP_POST,
      [](httpd_req_t *req) {
        size_t content_len = req->content_len;
        if (content_len == 0)
          return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        if (!Update.begin(content_len))
          return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                     "OTA Begin Failed");
        char *buf = (char *)malloc(1024);
        int ret;
        while ((ret = httpd_req_recv(req, buf, 1024)) > 0) {
          if (Update.write((uint8_t *)buf, ret) != ret) {
            free(buf);
            Update.end();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "OTA Write Failed");
          }
        }
        free(buf);
        if (Update.end()) {
          vTaskDelay(pdMS_TO_TICKS(1000));

          struct tm timeinfo;
          char timeStr[32];
          if (getLocalTime(&timeinfo)) {
            strftime(timeStr, sizeof(timeStr), "%d-%m-%Y %H:%M:%S", &timeinfo);
          } else {
            strcpy(timeStr, "Unknown");
          }

          Preferences prefs;
          prefs.begin("ota", false);
          prefs.clear();
          prefs.putString("last", timeStr);
          prefs.end();
          ESP.restart();
          return httpd_resp_send(req, "OK", 2);
        } else
          return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                     "OTA End Failed");
      },
      nullptr};

  httpd_uri_t ws_uri = {.uri = "/ws",
                        .method = HTTP_GET,
                        .handler = ws_handler,
                        .user_ctx = NULL,
                        .is_websocket = true,
                        .handle_ws_control_frames = false,
                        .supported_subprotocol = NULL};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &upload_uri);
  httpd_register_uri_handler(server, &status_uri);
  httpd_register_uri_handler(server, &solenoids_get_uri);
  httpd_register_uri_handler(server, &solenoids_post_uri);
  httpd_register_uri_handler(server, &solenoid_test_uri);
  httpd_register_uri_handler(server, &backup_uri);
  httpd_register_uri_handler(server, &restore_uri);
  httpd_register_uri_handler(server, &time_get_uri);
  httpd_register_uri_handler(server, &time_post_uri);
  httpd_register_uri_handler(server, &files_get_uri);
  httpd_register_uri_handler(server, &files_delete_uri);
  httpd_register_uri_handler(server, &wifi_get_uri);
  httpd_register_uri_handler(server, &wifi_post_uri);
  httpd_register_uri_handler(server, &reboot_post_uri);
  httpd_register_uri_handler(server, &player_cmd_uri);
  httpd_register_uri_handler(server, &ota_uri);
  httpd_register_uri_handler(server, &ws_uri);

  active = true;
}

void WebServerManager::update() {
  if (!active)
    return;
  if (needsScan) {
    sdcard.scan();
    needsScan = false;
  }
}

void WebServerManager::stop() {
  if (!active)
    return;
  if (server) {
    httpd_stop(server);
    server = nullptr;
  }
  MDNS.end();
  active = false;
}

bool WebServerManager::isActive() const { return active; }