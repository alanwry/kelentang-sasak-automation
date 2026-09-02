#include "webserver.h"
#include "config.h"
#include "pins.h"
#include "wifi_manager.h"
#include "player.h"
#include "sdcard.h"
#include "solenoid.h"
#include <ESPmDNS.h>
#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <Update.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include <stdarg.h>
#include <algorithm>
#include <NetBIOS.h>

#include <driver/temperature_sensor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

static std::vector<int> ws_clients;
static SemaphoreHandle_t ws_mutex = NULL;
static QueueHandle_t log_queue = NULL;
httpd_handle_t server = nullptr;

static void ws_sender_task(void *pvParameters) {
  char msg[256];
  while (true) {
    if (xQueueReceive(log_queue, msg, portMAX_DELAY)) {
      if (ws_mutex != NULL) xSemaphoreTake(ws_mutex, portMAX_DELAY);
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
      if (ws_mutex != NULL) xSemaphoreGive(ws_mutex);
    }
  }
}

static temperature_sensor_handle_t tempHandle = NULL;
static bool tempInit = false;

float getChipTemperature() {
  if (!tempInit) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&cfg, &tempHandle) != ESP_OK) return NAN;
    if (temperature_sensor_enable(tempHandle) != ESP_OK) return NAN;
    tempInit = true;
  }
  float temp;
  if (temperature_sensor_get_celsius(tempHandle, &temp) == ESP_OK) return temp;
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
  if (!msg.endsWith("\n")) msg += "\n";
  sendLogToClients(msg.c_str());
}

extern void triggerBuzzer(uint16_t duration);
String sanitizeFilename(String filename);

WebServerManager webServer;
namespace {
bool active = false;
bool needsScan = false;
}

const char htmlPageAP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0" />
  <title>Setup AP - AN Electronic</title>
  <style>
    :root { --bg-color: #0f172a; --card-bg: #1e293b; --text-main: #f1f5f9; --text-muted: #94a3b8; --accent: #38bdf8; --border: #334155; }
    body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: var(--bg-color); color: var(--text-main); margin: 0; padding: 20px; min-height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; box-sizing: border-box; }
    .card { background: var(--card-bg); padding: 24px; border-radius: 16px; width: 100%; max-width: 380px; box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.4); border: 1px solid var(--border); }
    h2 { margin-top: 0; color: var(--accent); font-size: 1.3rem; margin-bottom: 20px; text-align: center; }
    .input-group { display: flex; flex-direction: column; gap: 12px; }
    input[type="text"] { padding: 12px 14px; border: 1px solid var(--border); border-radius: 10px; background: #0f172a; color: white; font-size: 0.95rem; box-sizing: border-box; outline: none; transition: border-color 0.2s; }
    input[type="text"]:focus { border-color: var(--accent); }
    .row { display: flex; align-items: center; justify-content: space-between; margin-top: 5px; }
    .switch { position: relative; display: inline-block; width: 46px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .3s; border-radius: 24px; }
    .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
    input:checked + .slider { background-color: var(--accent); }
    input:checked + .slider:before { transform: translateX(22px); background-color: #0f172a; }
    button { padding: 12px; border: none; border-radius: 10px; cursor: pointer; font-weight: 600; width: 100%; background: var(--accent); color: #0f172a; font-size: 0.95rem; margin-top: 10px; transition: opacity 0.2s; }
    button:hover { opacity: 0.9; }
    footer { text-align: center; color: var(--text-muted); font-size: 0.8rem; margin-top: 25px; }
  </style>
</head>
<body>
<div class="card">
  <h2>WiFi AP Config</h2>
  <div class="input-group">
    <input type="text" id="wifiSsid" placeholder="SSID Network" />
    <input type="text" id="wifiPass" placeholder="Password" />
    <div class="row">
      <label style="font-size: 0.9rem; color: var(--text-muted);">Enable WiFi STA</label>
      <label class="switch">
        <input type="checkbox" id="wifiEnable">
        <span class="slider"></span>
      </label>
    </div>
    <button onclick="saveWifi()">Save and Apply</button>
  </div>
</div>
<footer>&copy; 2026 AN ELECTRONIC | Mataram, NTB</footer>
<script>
async function loadWifi() {
    const res = await fetch('/api/wifi');
    const config = await res.json();
    document.getElementById('wifiSsid').value = config.ssid || "";
    document.getElementById('wifiPass').value = config.pass || "";
    document.getElementById('wifiEnable').checked = config.enable || false;
}
async function saveWifi() {
    const ssid = document.getElementById('wifiSsid').value;
    const enable = document.getElementById('wifiEnable').checked;
    if (enable && !ssid) { alert('SSID is required if STA is enabled!'); return; }
    const res = await fetch('/api/wifi', { method: 'POST', body: JSON.stringify({ssid: ssid, pass: document.getElementById('wifiPass').value, enable: enable}) });
    if (!res.ok) alert('Failed to save settings');
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
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0" />
  <title>ESP32-S3 WROOM-1U</title>
  <style>
    :root { 
      --bg-color: #0f172a; 
      --card-bg: #1e293b; 
      --text-main: #f1f5f9; 
      --text-muted: #94a3b8; 
      --accent: #38bdf8; 
      --accent-hover: #0284c7;
      --danger: #ef4444; 
      --border: #334155; 
      --input-bg: #0f172a;
    }
    * { box-sizing: border-box; }
    body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: var(--bg-color); color: var(--text-main); margin: 0; padding: 15px; line-height: 1.5; display: flex; flex-direction: column; align-items: center; }
    
    header { width: 100%; max-width: 480px; margin: 0 auto 15px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 12px; }
    header h1 { margin: 0; font-size: 1.3rem; color: var(--accent); letter-spacing: -0.025em; }
    
    .dashboard-grid { 
      display: flex;
      flex-direction: column;
      gap: 16px; 
      width: 100%;
      max-width: 480px; 
      margin: 0 auto; 
    }
    
    .card { background: var(--card-bg); padding: 18px; border-radius: 16px; box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25); border: 1px solid var(--border); display: flex; flex-direction: column; width: 100%; }
    .card-full { width: 100%; }
    
    h2 { margin-top: 0; margin-bottom: 12px; color: var(--accent); font-size: 1.1rem; border-bottom: 1px solid rgba(255,255,255,0.05); padding-bottom: 8px; }
    .row { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
    .row-wrap { flex-wrap: wrap; }
    
    input[type="text"], input[type="number"] { padding: 9px 12px; border: 1px solid var(--border); border-radius: 8px; background: var(--input-bg); color: white; font-size: 0.85rem; flex-grow: 1; outline: none; transition: border-color 0.2s; width: 100%; }
    input:focus { border-color: var(--accent); }
    
    button, .btn { padding: 9px 12px; border: none; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 0.85rem; transition: all 0.2s; display: inline-flex; align-items: center; justify-content: center; gap: 6px; }
    button:hover, .btn:hover { opacity: 0.9; transform: translateY(-1px); }
    button:active, .btn:active { transform: translateY(0); }
    .primary { background: var(--accent); color: #0f172a; }
    .danger { background: var(--danger); color: white; }
    
    /* Toggle Switch */
    .switch { position: relative; display: inline-block; width: 44px; height: 22px; flex-shrink: 0; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .3s; border-radius: 22px; }
    .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
    input:checked + .slider { background-color: var(--accent); }
    input:checked + .slider:before { transform: translateX(22px); background-color: #0f172a; }

    /* Custom File Input */
    .file-label { padding: 9px 12px; border: 1px dashed var(--border); border-radius: 8px; background: var(--input-bg); color: var(--text-muted); cursor: pointer; flex-grow: 1; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 0.85rem; display: inline-flex; align-items: center; justify-content: center; transition: border-color 0.2s; }
    .file-label:hover { border-color: var(--accent); color: var(--text-main); }
    
    /* Tables */
    .scroll-container { max-height: 200px; overflow-y: auto; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; }
    table { width: 100%; border-collapse: collapse; table-layout: fixed; }
    thead th { position: sticky; top: 0; background: #1a2436; z-index: 1; padding: 8px 4px; border-bottom: 1px solid var(--border); font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; }
    td { padding: 6px 4px; border-bottom: 1px solid rgba(255,255,255,0.03); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 0.8rem; text-align: center; }
    tr:last-child td { border-bottom: none; }
    .col-name { text-align: left; padding-left: 10px; }
    .col-pin { width: 45px; }
    .col-note { width: 55px; }
    .col-midi { width: 45px; }
    .col-ch { width: 40px; }
    .col-s-action { width: 110px; }
    
    /* Progress Bars */
    .progress-bg { background: #334155; border-radius: 8px; height: 10px; overflow: hidden; margin: 8px 0; }
    .progress-bar { background: var(--accent); height: 100%; width: 0%; transition: width 0.3s ease; }
    
    footer { text-align: center; color: var(--text-muted); font-size: 0.8rem; margin-top: 30px; margin-bottom: 15px; width: 100%; max-width: 480px; }
  </style>
</head>
<body>

<header>
  <h1>KELENTANG ROBOT</h1>
  <span style="font-size: 0.8rem; color: var(--text-muted);"><span id="tempDisplay">--.-°C</span></span>
</header>

<div class="dashboard-grid">

  <!-- 1. Player Control Card -->
  <div class="card">
    <h2>Player Control</h2>
    <div id="playerStatus" style="font-size: 0.9rem; font-weight: 600; color: var(--text-main); margin-bottom: 5px;">Not playing</div>
    <div class="progress-bg"><div id="playerBar" class="progress-bar"></div></div>
    <div class="row" style="justify-content: space-between; font-size: 0.75rem; color: var(--text-muted); margin-bottom: 12px;">
      <span id="timeElapsed">0:00</span>
      <span id="modeDisplay" style="background: var(--border); padding: 2px 6px; border-radius: 4px; color: var(--accent);">PlayOnce</span>
      <span id="timeRemaining">0:00</span>
    </div>
    <div class="row" style="justify-content: center; gap: 6px; margin-bottom: 0;">
      <button onclick="sendCommand('prev')" class="primary" style="flex:1;">Prev</button>
      <button onclick="sendCommand('start')" class="primary" style="flex:1.5;" id="btnStart">Play</button>
      <button onclick="sendCommand('next')" class="primary" style="flex:1;">Next</button>
      <button onclick="sendCommand('mode')" class="primary" style="flex:1;">Mode</button>
    </div>
  </div>

  <!-- 2. WebSerial Card (System Log) -->
  <div class="card card-full">
    <h2>System Log</h2>
    <div id="logContainer" class="scroll-container" style="background: #0f172a; padding: 10px; font-family: monospace; font-size: 0.75rem; color: #10b981; height: 180px; white-space: pre-wrap;"></div>
    <button onclick="document.getElementById('logContainer').innerText = ''" class="danger" style="margin-top: 10px;">Clear Log</button>
  </div>

  <!-- 3. MIDI File Manager Card -->
  <div class="card">
    <h2>MIDI File Manager</h2>
    <div class="row">
      <label for="fileInput" class="file-label" onclick="document.getElementById('fileInput').click()">Select MIDI File</label>
      <input type="file" id="fileInput" accept=".mid,.midi" style="display:none;" onchange="document.querySelector('label[for=\'fileInput\']').innerText = this.files[0].name" />
      <button onclick="uploadFile()" class="primary">Upload</button>
    </div>
    <div class="scroll-container">
      <table>
        <thead><tr><th class="col-name">Name</th><th style="width:65px;">Size</th><th style="width:65px;">Action</th></tr></thead>
        <tbody id="fileBody"></tbody>
      </table>
    </div>
    <div id="storageInfo" style="margin-top: 8px; font-size: 0.75rem; color: var(--text-muted); text-align: center;"></div>
  </div>

  <!-- 4. Actuator Active Duration Card -->
  <div class="card">
    <h2>Actuator Active Duration</h2>
    <div style="font-size: 0.85rem; margin-bottom: 10px; color: var(--text-muted);">
      Current Duration: <strong id="currentTime" style="color: var(--accent);">...</strong> ms
    </div>
    <div class="row" style="margin-bottom: 0;">
      <input type="number" id="sTime" placeholder="Enter New Duration (ms)" />
      <button onclick="saveTime()" class="primary">Save</button>
    </div>
  </div>

  <!-- 5. Actuator Manager Card -->
  <div class="card card-full">
    <h2>Actuator Manager</h2>
    <div class="row row-wrap" style="gap: 8px; margin-bottom: 12px;">
      <input type="number" id="sPin" placeholder="GPIO Pin" style="flex: 1; min-width: 90px;" />
      <input type="text" id="sNote" placeholder="Note (e.g. C4)" style="flex: 1; min-width: 90px;" />
      <input type="number" id="sMidi" placeholder="MIDI Note" style="flex: 1; min-width: 100px;" />
      <input type="number" id="sChannel" placeholder="MIDI Ch (0=All)" style="flex: 1; min-width: 100px;" />
    </div>
    <div class="row" style="gap: 8px; margin-bottom: 12px;">
      <button onclick="backupConfig()" class="primary" style="flex: 1;">Backup</button>
      <input type="file" id="restoreInput" style="display:none;" onchange="restoreConfig()" />
      <button onclick="document.getElementById('restoreInput').click()" class="danger" style="flex: 1;">Restore</button>
      <button onclick="addSolenoid()" class="primary" style="flex: 1;">Save</button>
    </div>
    <div class="scroll-container">
      <table>
        <thead><tr><th class="col-pin">GPIO</th><th class="col-note">Note</th><th class="col-midi">MIDI</th><th class="col-ch">Ch</th><th class="col-s-action">Action</th></tr></thead>
        <tbody id="solenoidBody"></tbody>
      </table>
    </div>
  </div>

  <!-- 6. WiFi Manager Card -->
  <div class="card">
    <h2>WiFi Manager</h2>
    <div style="display: flex; flex-direction: column; gap: 8px;">
      <input type="text" id="wifiSsid" placeholder="SSID" />
      <input type="text" id="wifiPass" placeholder="Password" />
      <div class="row" style="justify-content: space-between; margin-top: 4px; margin-bottom: 4px;">
        <label style="font-size: 0.85rem; color: var(--text-muted);">Enable WiFi STA</label>
        <label class="switch">
          <input type="checkbox" id="wifiEnable">
          <span class="slider"></span>
        </label>
      </div>
      <button onclick="saveWifi()" class="primary" style="width: 100%;">Save and Apply</button>
    </div>
  </div>

  <!-- 7. Firmware Update Card -->
  <div class="card card-full">
    <h2>Update Firmware</h2>
    <div style="display: flex; flex-direction: column; gap: 8px;">
      <div style="font-size: 0.8rem;">Version: <strong style="color: var(--accent);">{{FW_VERSION}}</strong></div>
      <div class="row" style="margin-bottom: 0;">
        <label for="otaBinInput" class="file-label" onclick="document.getElementById('otaBinInput').click()">Select .bin File</label>
        <input type="file" id="otaBinInput" accept=".bin" style="display:none;" onchange="document.querySelector('label[for=\'otaBinInput\']').innerText = this.files[0].name" />
        <button onclick="uploadOta()" class="primary">Upload</button>
      </div>
      <div style="font-size: 0.75rem; color: var(--text-muted);">
        <span>Last Update: {{LAST_UPDATE}}</span>
      </div>
      <div class="progress-bg"><div id="otaBar" class="progress-bar"></div></div>
    </div>
  </div>

</div>

<footer>
  &copy; 2026 AN ELECTRONIC | Mataram, Nusa Tenggara Barat
</footer>

<script>
const noteMap = {{NOTE_MAP}};
const allowedPins = {{ALLOWED_PINS}};
document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('sNote').addEventListener('input', (e) => {
        const noteInput = e.target.value.toLowerCase().trim();
        const midiInput = document.getElementById('sMidi');
        if (noteMap[noteInput]) {
            midiInput.value = noteMap[noteInput];
        } else {
            midiInput.value = "";
        }
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
        if (xhr.status === 200) {
            alert('Update Success! Restarting...');
            location.reload();
        } else {
            alert('Update Failed');
        }
    };
    xhr.send(fileInput.files[0]);
}
async function sendCommand(cmd) {
    await fetch('/api/player/cmd?action='+cmd, { method: 'POST' });
    loadData();
}
async function loadData() {
    const t = Date.now();
    
    // Fetch Temperature
    try {
        const resTemp = await fetch('/api/temp?t=' + t);
        const temp = await resTemp.text();
        document.getElementById('tempDisplay').innerText = temp;
    } catch (e) { console.error("Temp load error", e); }
    
    // Fetch Player
    try {
        const resP = await fetch('/api/player?t=' + t); 
        const player = await resP.json();
        document.getElementById('playerStatus').innerText = player.playing ? "Playing: " + player.file : (player.paused ? "Paused: " + player.file : "Stopped");
        document.getElementById('btnStart').innerText = player.playing ? "Pause" : "Play";
        document.getElementById('modeDisplay').innerText = player.auto ? "Continuous" : "PlayOnce";
        const barWidth = player.duration > 0 ? (player.elapsed / player.duration * 100) : 0;
        document.getElementById('playerBar').style.width = barWidth + '%';
        const remaining = Math.max(0, player.duration - player.elapsed);
        document.getElementById('timeElapsed').innerText = formatTime(player.elapsed);
        document.getElementById('timeRemaining').innerText = formatTime(remaining);
    } catch (e) { console.error("Player load error", e); }

    // Fetch Solenoids
    try {
    const resS = await fetch('/api/solenoids?t=' + t);
    const solenoids = await resS.json();
    const sBody = document.getElementById('solenoidBody'); sBody.innerHTML = '';
    solenoids.forEach(s => { sBody.innerHTML += `<tr><td class="col-pin">${s.pin}</td><td class="col-note">${s.note}</td><td class="col-midi">${s.midi}</td><td class="col-ch">${s.ch}</td><td class="col-s-action"><button class="primary" style="padding: 3px 6px; font-size: 0.7rem;" onclick="testSolenoid(${s.pin})">Play</button><button class="danger" style="padding: 3px 6px; font-size: 0.7rem;" onclick="removeSolenoid(${s.pin})">Delete</button></td></tr>`; });
    } catch (e) { console.error("Solenoids load error", e); }

    // Fetch Files
    try {
        const resF = await fetch('/api/files?t=' + t); 
        const filesRes = await resF.json();
        const fBody = document.getElementById('fileBody'); fBody.innerHTML = '';
        filesRes.files.forEach(f => { fBody.innerHTML += `<tr><td class="col-name">${f.name}</td><td>${formatSize(f.size)}</td><td><button class="danger" style="padding: 3px 6px; font-size: 0.7rem;" onclick="deleteFile('${f.name}')">Delete</button></td></tr>`; });
        const sInfo = document.getElementById('storageInfo');
        if (filesRes.storage) {
            const used = filesRes.storage.total - filesRes.storage.free;
            sInfo.innerText = `Total: ${formatSize(filesRes.storage.total)} | Used: ${formatSize(used)} | Free: ${formatSize(filesRes.storage.free)}`;
        } else sInfo.innerText = 'SD Card not detected';
    } catch (e) { console.error("Files load error", e); }

    // Fetch Time
    try {
        const resT = await fetch('/api/time?t=' + t); 
        const time = await resT.json();
        document.getElementById('currentTime').innerText = time;
    } catch (e) { console.error("Time load error", e); }
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
    } catch (e) { console.error("Wifi load error", e); }
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
  input.value = ''; loadData();
}
async function saveTime() {
  const timeInput = document.getElementById('sTime'); const currentTimeText = document.getElementById('currentTime').innerText;
  const newTime = timeInput.value;
  if (!newTime) { alert('Enter new duration!'); return; }
  if (newTime === currentTimeText) { alert('Duration is the same, not saved'); return; }
  await fetch('/api/time', { method: 'POST', body: newTime });
  timeInput.value = ''; loadData();
}
async function uploadFile() {
  const fileInput = document.getElementById('fileInput'); if (!fileInput.files[0]) { alert('Select MIDI file first!'); return; }
  const formData = new FormData(); formData.append("file", fileInput.files[0]);
  const response = await fetch('/upload', { method: 'POST', body: formData });
  const text = await response.text();
  if (text === "SKIP") alert('File already exists on SD Card!');
  else if (text === "OK") { fileInput.value = ''; document.querySelector('label[for=\'fileInput\']').innerText = 'Select MIDI File'; loadData(); }
  else alert('Failed to upload file');
}
async function addSolenoid() {
  const pin = parseInt(document.getElementById('sPin').value); let note = document.getElementById('sNote').value; const midi = parseInt(document.getElementById('sMidi').value); const ch = parseInt(document.getElementById('sChannel').value) || 0;
  
  if(!pin || !midi) { alert('GPIO and MIDI Note Number are required!'); return; }
  if(ch < 0 || ch > 16) { alert('MIDI Channel must be between 0 and 16!'); return; }
  if(!allowedPins.includes(pin)) { alert('GPIO not valid'); return; }
  
  if(!note) note = '-';
  const resS = await fetch('/api/solenoids'); let solenoids = await resS.json();
  if (solenoids.some(s => s.pin === pin)) { alert('GPIO is already used!'); return; }
  solenoids.push({pin: pin, note: note, midi: midi, ch: ch});
  await fetch('/api/solenoids', { method: 'POST', body: JSON.stringify(solenoids) });
  document.getElementById('sPin').value = ''; document.getElementById('sNote').value = ''; document.getElementById('sMidi').value = ''; document.getElementById('sChannel').value = '';
  loadData();
}
async function removeSolenoid(pin) {
  const resS = await fetch('/api/solenoids'); let solenoids = await resS.json();
  solenoids = solenoids.filter(s => s.pin !== pin);
  await fetch('/api/solenoids', { method: 'POST', body: JSON.stringify(solenoids) });
  loadData();
}
async function saveWifi() {
  const ssid = document.getElementById('wifiSsid').value; const pass = document.getElementById('wifiPass').value; const enable = document.getElementById('wifiEnable').checked;
  if (!ssid && enable) { alert('SSID is required if STA is enabled!'); return; }
  const res = await fetch('/api/wifi', { method: 'POST', body: JSON.stringify({ssid: ssid, pass: pass, enable: enable}) });
  if (res.ok) setTimeout(loadWifi, 500); else alert('Failed to save settings');
}
async function deleteFile(name) { await fetch('/api/files?name='+name, { method: 'DELETE' }); loadData(); }
setInterval(loadData, 1000); loadData(); loadWifi();

// WebSerial WebSocket Setup dengan Heartbeat Auto-Reconnect
let ws = null;
let wsPingInterval = null;

function initWebSocket() {
    // Jika socket sudah terhubung atau sedang menghubungkan, batalkan
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
    }

    ws = new WebSocket('ws://' + window.location.hostname + '/ws');

    ws.onopen = () => {
        console.log("WebSocket Connected");
        ws.send("ping");

        // Kirim Ping setiap 3 detik untuk memastikan koneksi masih hidup
        if (wsPingInterval) clearInterval(wsPingInterval);
        wsPingInterval = setInterval(() => {
            if (ws && ws.readyState === WebSocket.OPEN) {
                try {
                    ws.send("ping");
                } catch (e) {
                    ws.close(); // Paksa close jika gagal send
                }
            } else {
                if (ws) ws.close();
            }
        }, 3000);
    };

    ws.onmessage = (event) => {
        const logContainer = document.getElementById('logContainer');
        if (logContainer) {
            logContainer.innerText += event.data;
            if (logContainer.innerText.length > 5000) {
                logContainer.innerText = logContainer.innerText.substring(logContainer.innerText.length - 5000);
            }
            logContainer.scrollTop = logContainer.scrollHeight;
        }
    };

    ws.onerror = (err) => {
        console.error("WS Error:", err);
        if (ws) ws.close(); // Trigger onclose saat terjadi error
    };

    ws.onclose = () => {
        if (wsPingInterval) clearInterval(wsPingInterval);
        console.log("WS Disconnected, reconnecting in 2s...");
        setTimeout(initWebSocket, 2000); // Reconnect otomatis
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
    if (i < (sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0])) - 1) pinsJs += ", ";
  }
  pinsJs += "]";
  output.replace("{{ALLOWED_PINS}}", pinsJs);

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
          if (end > start) filename = sanitizeFilename(chunk.substring(start, end));
        }
        int headerEnd = chunk.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
          headersParsed = true;
          header_offset = headerEnd + 4;
          if (filename.length() > 0 && SD.exists(filename.c_str())) { return httpd_resp_send(req, "SKIP", 4); }
          if (filename.length() > 0 && (filename.endsWith(".mid") || filename.endsWith(".midi"))) {
            file = sdcard.openFile(filename.c_str(), FILE_WRITE);
            if (!file) return ESP_FAIL;
            if (recv_len > header_offset) file.write((uint8_t *)(buf + header_offset), recv_len - header_offset);
          } else return ESP_FAIL;
        }
      } else if (file) file.write((uint8_t *)buf, recv_len);
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
      while (solenoid.getCount() > 0) solenoid.removeSolenoid(solenoid.getItems()[0].getPin());
      return httpd_resp_send(req, "[]", 2);
    }

    if (solenoid.getCount() == 0) solenoid.loadConfig();

    String json = "[";
    Solenoid *items = solenoid.getItems();
    for (uint8_t i = 0; i < solenoid.getCount(); i++) {
      json += "{\"pin\":" + String(items[i].getPin()) + ",\"note\":\"" + items[i].getNote() + "\",\"midi\":" + String(items[i].getMidiNote()) + ",\"ch\":" + String(items[i].getMidiChannel()) + "}";
      if (i < solenoid.getCount() - 1) json += ",";
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_POST) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret > 0) {
      while (solenoid.getCount() > 0) solenoid.removeSolenoid(solenoid.getItems()[0].getPin());
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
        int cEnd = obj.indexOf("}", cStart);
        int channel = obj.substring(cStart, cEnd).toInt();
        solenoid.addSolenoid(pin, note, midi, channel);
        start = end;
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
    if (!file) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Config not found");
    String json = "{\"solenoids\":[";
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
      json += "{\"pin\":" + line.substring(0, c1) + ",\"note\":\"" + line.substring(c1 + 1, c2) + "\",\"midi\":" + line.substring(c2 + 1) + "},";
    }
    file.close();
    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "],\"duration\":" + String(player.getSolenoidTime()) + "}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"backup.json\"");
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
      if (dEnd == -1) dEnd = data.indexOf("}", dStart);
      player.setSolenoidTime(data.substring(dStart, dEnd).toInt());
      File file = SD.open("/solenoids.txt", FILE_WRITE);
      int start = data.indexOf("{\"pin\":");
      while (start >= 0) {
        int end = data.indexOf("}", start);
        String obj = data.substring(start, end + 1);
        int p1 = obj.indexOf(":") + 1, p2 = obj.indexOf(",", p1), p3 = obj.indexOf(":", p2) + 2, p4 = obj.indexOf("\"", p3), p5 = obj.indexOf(":", p4) + 1, p6 = obj.indexOf("}", p5);
        file.println(obj.substring(p1, p2) + "," + obj.substring(p3, p4) + "," + obj.substring(p5, p6));
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
        if (!file.isDirectory() && (name.endsWith(".mid") || name.endsWith(".midi"))) {
          if (!first) json += ",";
          json += "{\"name\":\"" + name + "\",\"size\":" + String(file.size()) + "}";
          first = false;
        }
        file = root.openNextFile();
      }
      json += "], \"storage\":{\"total\":" + String(SD.totalBytes()) + ", \"free\":" + String(SD.totalBytes() - SD.usedBytes()) + "}}";
      root.close();
    } else json += "], \"storage\":null}";
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
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete Failed");
  }
  return ESP_FAIL;
}

esp_err_t api_wifi_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    String ssid, pass;
    bool enable;
    wifiManager.getSettings(ssid, pass, enable);
    String json = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"enable\":" + (enable ? "true" : "false") + "}";
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
        if (end != -1) ssid = data.substring(start, end);
      }
      int pIdx = data.indexOf("\"pass\":\"");
      if (pIdx != -1) {
        int start = pIdx + 8;
        int end = data.indexOf("\"", start);
        if (end != -1) pass = data.substring(start, end);
      }
      int eIdx = data.indexOf("\"enable\":");
      if (eIdx != -1) {
        int colonIdx = data.indexOf(":", eIdx);
        if (colonIdx != -1) {
          String val = data.substring(colonIdx + 1);
          val.trim();
          if (val.startsWith("true")) enable = true;
          else if (val.startsWith("false")) enable = false;
        }
      }
      wifiManager.saveSettings(ssid, pass, enable);
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
  if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
  int lastBackslash = filename.lastIndexOf('\\');
  if (lastBackslash >= 0) filename = filename.substring(lastBackslash + 1);
  for (size_t i = 0; i < filename.length(); i++) {
    char c = filename[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') clean += c;
    else clean += '_';
  }
  return clean;
}

esp_err_t ws_handler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);

  if (ws_mutex != NULL) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  if (std::find(ws_clients.begin(), ws_clients.end(), fd) == ws_clients.end()) {
    ws_clients.push_back(fd);
    Serial.printf("[WS] Client connected: %d\n", fd);
  }
  if (ws_mutex != NULL) xSemaphoreGive(ws_mutex);

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) return ret;

  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
    if (ws_mutex != NULL) xSemaphoreTake(ws_mutex, portMAX_DELAY);
    auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
    if (it != ws_clients.end()) {
      ws_clients.erase(it);
      Serial.printf("[WS] Client disconnected: %d\n", fd);
    }
    if (ws_mutex != NULL) xSemaphoreGive(ws_mutex);
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

void WebServerManager::beginAPMinimal() {
  if (active) return;
  if (ws_mutex == NULL) ws_mutex = xSemaphoreCreateMutex();
  if (log_queue == NULL) {
    log_queue = xQueueCreate(20, 256);
    xTaskCreate(ws_sender_task, "ws_sender", 4096, NULL, 1, NULL);
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 16384;
  if (httpd_start(&server, &config) != ESP_OK) return;

  httpd_uri_t root_uri = { "/", HTTP_GET, root_handler, nullptr };
  httpd_uri_t wifi_get_uri = { "/api/wifi", HTTP_GET, api_wifi_handler, nullptr };
  httpd_uri_t wifi_post_uri = { "/api/wifi", HTTP_POST, api_wifi_handler, nullptr };

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &wifi_get_uri);
  httpd_register_uri_handler(server, &wifi_post_uri);

  active = true;
}

void WebServerManager::beginSTAFull() {
  if (active) return;

  if (ws_mutex == NULL) ws_mutex = xSemaphoreCreateMutex();
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

  temperature_sensor_config_t ts_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  temperature_sensor_install(&ts_cfg, &tempHandle);
  temperature_sensor_enable(tempHandle);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 20;
  config.max_open_sockets = 7;
  config.stack_size = 16384;
  if (httpd_start(&server, &config) != ESP_OK) return;

  httpd_uri_t root_uri = { "/", HTTP_GET, root_handler, nullptr };
  httpd_uri_t upload_uri = { "/upload", HTTP_POST, upload_handler, nullptr };
  httpd_uri_t temp_uri = { "/api/temp", HTTP_GET, [](httpd_req_t *req) {
                            float temp = 0;
                            if (tempHandle != NULL) {
                              temperature_sensor_get_celsius(tempHandle, &temp);
                            }
                            char buf[16];
                            snprintf(buf, sizeof(buf), "%.2f°C", temp);
                            httpd_resp_set_type(req, "text/plain");
                            return httpd_resp_send(req, buf, strlen(buf));
                          },
                           nullptr };
  httpd_uri_t solenoids_get_uri = { "/api/solenoids", HTTP_GET, api_solenoids_handler, nullptr };
  httpd_uri_t solenoids_post_uri = { "/api/solenoids", HTTP_POST, api_solenoids_handler, nullptr };
  httpd_uri_t solenoid_test_uri = { "/api/solenoid/test", HTTP_POST, api_solenoid_test_handler, nullptr };
  httpd_uri_t backup_uri = { "/api/backup", HTTP_GET, api_backup_handler, nullptr };
  httpd_uri_t restore_uri = { "/api/restore", HTTP_POST, api_restore_handler, nullptr };
  httpd_uri_t time_get_uri = { "/api/time", HTTP_GET, api_time_handler, nullptr };
  httpd_uri_t time_post_uri = { "/api/time", HTTP_POST, api_time_handler, nullptr };
  httpd_uri_t files_get_uri = { "/api/files", HTTP_GET, api_files_handler, nullptr };
  httpd_uri_t files_delete_uri = { "/api/files", HTTP_DELETE, api_files_handler, nullptr };
  httpd_uri_t wifi_get_uri = { "/api/wifi", HTTP_GET, api_wifi_handler, nullptr };
  httpd_uri_t wifi_post_uri = { "/api/wifi", HTTP_POST, api_wifi_handler, nullptr };

  httpd_uri_t player_get_uri = { "/api/player", HTTP_GET, [](httpd_req_t *req) {
                                  String file = String(sdcard.getCurrentFile());
                                  if (file.length() == 0) file = "No file";

                                  String json = "{\"playing\":" + String(player.isPlaying() ? "true" : "false") + ",\"paused\":" + String(player.isPaused() ? "true" : "false") + ",\"auto\":" + String(player.isAutoMode() ? "true" : "false") + ",\"file\":\"" + file + "\"" + ",\"duration\":" + String(player.getDurationUS() / 1000) + ",\"elapsed\":" + String(player.getElapsedUS() / 1000) + "}";
                                  httpd_resp_set_type(req, "application/json");
                                  return httpd_resp_send(req, json.c_str(), json.length());
                                },
                                 nullptr };

  httpd_uri_t player_cmd_uri = { "/api/player/cmd", HTTP_POST, [](httpd_req_t *req) {
                                  char buf[64];
                                  size_t len = httpd_req_get_url_query_len(req);
                                  if (len < sizeof(buf)) {
                                    httpd_req_get_url_query_str(req, buf, len + 1);
                                    char action[16];
                                    if (httpd_query_key_value(buf, "action", action, sizeof(action)) == ESP_OK) {
                                      String cmd(action);
                                      if (cmd == "start") {
                                        if (player.isPlaying()) player.pause();
                                        else player.play();
                                      } else if (cmd == "next") player.nextFile();
                                      else if (cmd == "prev") player.prevFile();
                                      else if (cmd == "mode") player.toggleMode();
                                      return httpd_resp_send(req, "OK", 2);
                                    }
                                  }
                                  return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Command");
                                },
                                 nullptr };

  httpd_uri_t ota_uri = { "/update", HTTP_POST, [](httpd_req_t *req) {
                           size_t content_len = req->content_len;
                           if (content_len == 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
                           if (!Update.begin(content_len)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin Failed");
                           char *buf = (char *)malloc(1024);
                           int ret;
                           while ((ret = httpd_req_recv(req, buf, 1024)) > 0) {
                             if (Update.write((uint8_t *)buf, ret) != ret) {
                               free(buf);
                               Update.end();
                               return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Write Failed");
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
                           } else return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA End Failed");
                         },
                          nullptr };

  httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &upload_uri);
  httpd_register_uri_handler(server, &temp_uri);
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
  httpd_register_uri_handler(server, &player_get_uri);
  httpd_register_uri_handler(server, &player_cmd_uri);
  httpd_register_uri_handler(server, &ota_uri);
  httpd_register_uri_handler(server, &ws_uri);

  active = true;
}

void WebServerManager::update() {
  if (!active) return;
  if (needsScan) {
    sdcard.scan();
    needsScan = false;
  }
}

void WebServerManager::stop() {
  if (!active) return;
  if (server) {
    httpd_stop(server);
    server = nullptr;
  }
  MDNS.end();
  active = false;
}

bool WebServerManager::isActive() const {
  return active;
}