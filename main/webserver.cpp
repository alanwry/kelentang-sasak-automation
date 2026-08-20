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

extern void triggerBuzzer(uint16_t duration);
String sanitizeFilename(String filename); // Forward declaration

WebServerManager webServer;
namespace {
httpd_handle_t server = nullptr;
bool active = false;
bool needsScan = false;
}

const char htmlPageAP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0" />
  <title>Setup AP</title>
  <style>
  :root { --bg-color: #0f172a; --card-bg: #1e293b; --text-main: #f1f5f9; --text-muted: #94a3b8; --accent: #38bdf8; --border: #334155; }
  body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg-color); color: var(--text-main); margin: 0; padding: 20px; }
  .card { background: var(--card-bg); padding: 20px; border-radius: 16px; max-width: 400px; margin: 0 auto; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2); border: 1px solid var(--border); }
  h2 { margin-top: 0; color: var(--accent); }
  .row { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 15px; }
  input { padding: 10px 14px; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; color: white; flex-grow: 1; box-sizing: border-box; }
  button { padding: 10px; border: none; border-radius: 8px; cursor: pointer; font-weight: 600; width: 100%; background: var(--accent); color: #0f172a; }
  </style>
</head>
<body>
<div class="card">
<h2>WiFi Manager</h2>
<div style="display: flex; flex-direction: column; gap: 10px;">
<input type="text" id="wifiSsid" placeholder="SSID" />
<input type="text" id="wifiPass" placeholder="Password" />
<div class="row" style="justify-content: space-between; margin-top: 5px;">
    <label style="font-size: 0.95rem; color: var(--text-muted);">Enable WiFi STA</label>
    <label class="switch">
        <input type="checkbox" id="wifiEnable">
        <span class="slider"></span>
    </label>
</div>
<div class="row">
    <button onclick="saveWifi()" class="primary upload-btn" style="flex: 1;">Save and Apply</button>
</div>
</div>
</div>
<footer style="text-align: center; color: var(--text-muted); font-size: 0.85rem; margin-top: 20px;">
&copy; 2026 AN ELECTRONIC | Mataram, Nusa Tenggara Barat
</footer>
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
  <title>ESP Dashboard</title>
  <style>
  :root { --bg-color: #0f172a; --card-bg: #1e293b; --text-main: #f1f5f9; --text-muted: #94a3b8; --accent: #38bdf8; --danger: #ef4444; --border: #334155; }
  body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg-color); color: var(--text-main); margin: 0; padding: 20px; line-height: 1.5; }
  .card { background: var(--card-bg); padding: 20px; border-radius: 16px; max-width: 600px; margin: 0 auto 20px; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2); border: 1px solid var(--border); }
  h2 { margin-top: 0; color: var(--accent); font-size: 1.25rem; letter-spacing: -0.025em; }
  .row { display: flex; align-items: center; gap: 12px; margin-bottom: 15px; }
  input { padding: 10px 14px; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; color: white; font-size: 0.95rem; flex-grow: 1; transition: border-color 0.2s; box-sizing: border-box; }
  input:focus { outline: none; border-color: var(--accent); }
  button { padding: 6px 10px; border: none; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.75rem; transition: opacity 0.2s; }
  button:hover { opacity: 0.9; }
  .primary { background: var(--accent); color: #0f172a; }
  .danger { background: var(--danger); color: white; }
  .scroll-container { max-height: 200px; overflow-y: auto; border: 1px solid var(--border); border-radius: 8px; -ms-overflow-style: none; scrollbar-width: none; }
  .scroll-container::-webkit-scrollbar { display: none; }
  table { width: 100%; border-collapse: collapse; table-layout: fixed; }
  thead th { position: sticky; top: 0; background: var(--card-bg); z-index: 1; padding: 10px 4px; border-bottom: 1px solid var(--border); }
  th { color: var(--text-muted); font-size: 0.7rem; text-transform: uppercase; padding: 8px 4px; vertical-align: middle; }
  .col-pin { width: 50px; }
  .col-note { width: 60px; }
  .col-midi { width: 60px; }
  .col-s-action { width: 120px; }
  .left { text-align: left; }
  .center { text-align: center; }
  td { padding: 8px 4px; background: rgba(0,0,0,0.1); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 0.85rem; vertical-align: middle; }
  td:first-child { border-radius: 8px 0 0 8px; }
  td:last-child { border-radius: 0 8px 8px 0; }
  .danger { background: var(--danger); color: white; padding: 6px 10px; font-size: 0.75rem; vertical-align: middle; }
  .upload-btn { padding: 0 14px; font-size: 0.95rem; box-sizing: border-box; height: 42px; display: inline-flex; align-items: center; cursor: pointer; }
  .file-label { padding: 0 14px; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; color: var(--text-muted); cursor: pointer; flex-grow: 1; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; height: 42px; box-sizing: border-box; font-size: 0.95rem; display: inline-flex; align-items: center; justify-content: center; }
  .progress-bg { background: #334155; border-radius: 8px; height: 16px; overflow: hidden; margin: 10px 0; }
  .progress-bar { background: var(--accent); height: 100%; width: 0%; transition: width 0.5s linear; }
  </style>
</head>
<body>
<div class="card">
  <h2>Player Control</h2>
  <div id="playerStatus" style="font-size: 0.9rem; color: var(--text-muted); margin-bottom: 5px;">Not playing</div>
  <div class="progress-bg"><div id="playerBar" class="progress-bar"></div></div>
  <div class="row" style="justify-content: space-between; font-size: 0.85rem; color: var(--text-muted);">
    <span id="timeElapsed">0:00</span>
    <span id="modeDisplay">Manual</span>
    <span id="timeRemaining">0:00</span>
  </div>
  <div class="row" style="justify-content: center; gap: 10px;">
    <button onclick="sendCommand('prev')" class="primary upload-btn">Prev</button>
    <button onclick="sendCommand('start')" class="primary upload-btn" style="min-width: 80px;" id="btnStart">Play</button>
    <button onclick="sendCommand('next')" class="primary upload-btn">Next</button>
    <button onclick="sendCommand('mode')" class="primary upload-btn">Mode</button>
  </div>
</div>
<div class="card">
  <h2>MIDI File Manager</h2>
  <div class="row">
    <label for="fileInput" style="padding: 0 14px; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; color: var(--text-muted); cursor: pointer; flex-grow: 1; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; height: 42px; box-sizing: border-box; font-size: 0.95rem; display: inline-flex; align-items: center; justify-content: center;" onclick="document.getElementById('fileInput').click()">Select MIDI File</label>
    <input type="file" id="fileInput" accept=".mid,.midi" style="display:none;" onchange="document.querySelector('label[for=\'fileInput\']').innerText = this.files[0].name" />
    <button onclick="uploadFile()" class="primary upload-btn">Upload</button>
  </div>
  <div class="scroll-container">
  <table>
    <thead><tr><th class="col-name left">Name</th><th class="col-size center">Size</th><th class="col-action center">Action</th></tr></thead>
    <tbody id="fileBody"></tbody>
  </table>
  </div>
  <div id="storageInfo" style="margin-top: 10px; font-size: 0.9rem; color: var(--text-muted); text-align: center;"></div>
</div>
<div class="card">
  <h2>Actuator Active Duration</h2>
  <div class="row" style="justify-content: space-between;">
    <span>Current Duration : <strong id="currentTime">...</strong> ms</span>
  </div>
  <div class="row">
    <input type="number" id="sTime" placeholder="Enter New Duration" />
    <button onclick="saveTime()" class="primary upload-btn">Save</button>
  </div>
</div>
<div class="card">
<h2>Actuator Manager</h2>
<div style="display: flex; flex-direction: column; gap: 10px; margin-bottom: 15px;">
<input type="number" id="sPin" placeholder="GPIO" />
<input type="text" id="sNote" placeholder="Note" />
<input type="number" id="sMidi" placeholder="MIDI Note Number" />
<div class="row">
  <button onclick="backupConfig()" class="primary upload-btn" style="flex: 1;">Download</button>
  <input type="file" id="restoreInput" style="display:none;" onchange="restoreConfig()" />
  <button onclick="document.getElementById('restoreInput').click()" class="danger upload-btn" style="flex: 1;">Upload</button>
  <button onclick="addSolenoid()" class="primary upload-btn" style="flex: 1;">Save</button>
</div>
</div>
<div class="scroll-container">
<table>
  <thead><tr><th class="col-pin center">GPIO</th><th class="col-note center">Note</th><th class="col-midi center">MIDI</th><th class="col-s-action center">Action</th></tr></thead>
  <tbody id="solenoidBody"></tbody>
</table>
</div>
</div>
<div class="card">
<h2>WiFi Manager</h2>
<div style="display: flex; flex-direction: column; gap: 10px;">
<input type="text" id="wifiSsid" placeholder="SSID" />
<input type="text" id="wifiPass" placeholder="Password" />
<div class="row" style="justify-content: space-between; margin-top: 5px;">
    <label style="font-size: 0.95rem; color: var(--text-muted);">Enable WiFi STA</label>
    <label class="switch">
        <input type="checkbox" id="wifiEnable">
        <span class="slider"></span>
    </label>
</div>
<div class="row">
    <button onclick="saveWifi()" class="primary upload-btn" style="flex: 1;">Save and Apply</button>
</div>
</div>
</div>
<div class="card">
<h2>Update Firmware</h2>
<div class="row" style="flex-direction: column; align-items: stretch; gap: 10px;">
  <div>Version: <strong>{{FW_VERSION}}</strong></div>
  <div class="row">
      <label for="otaBinInput" style="padding: 10px 14px; border: 1px solid var(--border); border-radius: 8px; background: #0f172a; color: var(--text-muted); cursor: pointer; flex-grow: 1; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;" onclick="document.getElementById('otaBinInput').click()">Select .bin File</label>
      <input type="file" id="otaBinInput" accept=".bin" style="display:none;" onchange="document.querySelector('label[for=\'otaBinInput\']').innerText = this.files[0].name" />
      <button onclick="uploadOta()" class="primary upload-btn">Upload</button>
      </div>
  <div style="font-size: 0.8rem; color: var(--text-muted);">
      <span>Last Update: {{LAST_UPDATE}}</span>
  </div>
  <div style="background: #334155; border-radius: 8px; height: 16px; overflow: hidden; margin-top: 5px;">
      <div id="otaBar" style="background: var(--accent); height: 100%; width: 0%; transition: width 0.3s;"></div>
  </div>
</div>
</div>
<footer style="text-align: center; color: var(--text-muted); font-size: 0.85rem; margin-top: 30px; margin-bottom: 20px;">
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
      
      // Fetch Player
      try {
          const resP = await fetch('/api/player?t=' + t); 
          const player = await resP.json();
          document.getElementById('playerStatus').innerText = player.playing ? "Playing: " + player.file : (player.paused ? "Paused: " + player.file : "Stopped");
          document.getElementById('btnStart').innerText = player.playing ? "Pause" : "Play";
          document.getElementById('modeDisplay').innerText = player.auto ? "Auto Loop" : "Manual";
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
          solenoids.forEach(s => { sBody.innerHTML += `<tr><td class="col-pin center">${s.pin}</td><td class="col-note center">${s.note}</td><td class="col-midi center">${s.midi}</td><td class="col-s-action center" style="display: flex; justify-content: center; align-items: center; gap: 5px; padding: 8px 4px;"><button class="primary" onclick="testSolenoid(${s.pin})">Play</button><button class="danger" onclick="removeSolenoid(${s.pin})">Delete</button></td></tr>`; });
      } catch (e) { console.error("Solenoids load error", e); }

      // Fetch Files
      try {
          const resF = await fetch('/api/files?t=' + t); 
          const filesRes = await resF.json();
          const fBody = document.getElementById('fileBody'); fBody.innerHTML = '';
          filesRes.files.forEach(f => { fBody.innerHTML += `<tr><td class="col-name left">${f.name}</td><td class="col-size center">${formatSize(f.size)}</td><td class="col-action" style="display: flex; justify-content: center; align-items: center; padding: 8px 4px;"><button class="danger" onclick="deleteFile('${f.name}')">Delete</button></td></tr>`; });
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
  function render(solenoids, files, storage) {
    const sBody = document.getElementById('solenoidBody'); sBody.innerHTML = '';
    solenoids.forEach(s => { sBody.innerHTML += `<tr><td class="col-pin center">${s.pin}</td><td class="col-note center">${s.note}</td><td class="col-midi center">${s.midi}</td><td class="col-s-action center" style="display: flex; justify-content: center; align-items: center; gap: 5px; padding: 8px 4px;"><button class="primary" onclick="testSolenoid(${s.pin})">Play</button><button class="danger" onclick="removeSolenoid(${s.pin})">Delete</button></td></tr>`; });
    const fBody = document.getElementById('fileBody'); fBody.innerHTML = '';
    files.forEach(f => { fBody.innerHTML += `<tr><td class="col-name left">${f.name}</td><td class="col-size center">${formatSize(f.size)}</td><td class="col-action" style="display: flex; justify-content: center; align-items: center; padding: 8px 4px;"><button class="danger" onclick="deleteFile('${f.name}')">Delete</button></td></tr>`; });
    const sInfo = document.getElementById('storageInfo');
    if (storage) {
        const used = storage.total - storage.free;
        sInfo.innerText = `Total: ${formatSize(storage.total)} | Used: ${formatSize(used)} | Free: ${formatSize(storage.free)}`;
    } else sInfo.innerText = 'SD Card not detected';
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
    const pin = parseInt(document.getElementById('sPin').value); let note = document.getElementById('sNote').value; const midi = parseInt(document.getElementById('sMidi').value);
    
    if(!pin || !midi) { alert('GPIO and MIDI Note Number are required!'); return; }
    if(!allowedPins.includes(pin)) { alert('GPIO not valid'); return; }
    
    if(!note) note = '-';
    const resS = await fetch('/api/solenoids'); let solenoids = await resS.json();
    if (solenoids.some(s => s.pin === pin || s.midi === midi)) { alert('GPIO or MIDI Note Number is already used!'); return; }
    solenoids.push({pin: pin, note: note, midi: midi});
    await fetch('/api/solenoids', { method: 'POST', body: JSON.stringify(solenoids) });
    document.getElementById('sPin').value = ''; document.getElementById('sNote').value = ''; document.getElementById('sMidi').value = '';
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
</script>
</body>
</html>
)rawliteral";


esp_err_t root_handler(httpd_req_t *req) {
  const char* page = (WiFi.getMode() == WIFI_AP) ? htmlPageAP : htmlPage;
  String output = String(page);
  output.replace("{{FW_VERSION}}", FW_VERSION);
  Preferences prefs; prefs.begin("ota", true);
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
  char buf[1024]; size_t recv_len; String filename = ""; bool headersParsed = false; size_t header_offset = 0; File file;
  if (req->content_len > 0) {
    while ((recv_len = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
      if (!headersParsed) {
        String chunk(buf, recv_len); int namePos = chunk.indexOf("filename=\"");
        if (namePos >= 0) { int start = namePos + 10; int end = chunk.indexOf("\"", start); if (end > start) filename = sanitizeFilename(chunk.substring(start, end)); }
        int headerEnd = chunk.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
          headersParsed = true; header_offset = headerEnd + 4;
          if (filename.length() > 0 && SD.exists(filename.c_str())) { return httpd_resp_send(req, "SKIP", 4); }
          if (filename.length() > 0 && (filename.endsWith(".mid") || filename.endsWith(".midi"))) {
            file = sdcard.openFile(filename.c_str(), FILE_WRITE); if (!file) return ESP_FAIL;
            if (recv_len > header_offset) file.write((uint8_t *)(buf + header_offset), recv_len - header_offset);
          } else return ESP_FAIL;
        }
      } else if (file) file.write((uint8_t *)buf, recv_len);
    }
  }
  if (file) { file.close(); needsScan = true; return httpd_resp_send(req, "OK", 2); }
  return ESP_FAIL;
}

esp_err_t api_solenoids_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    if (digitalRead(PIN_SD_DET) == HIGH) return httpd_resp_send(req, "[]", 2);
    String json = "[";
    Solenoid *items = solenoid.getItems();
    for (uint8_t i = 0; i < solenoid.getCount(); i++) {
      json += "{\"pin\":" + String(items[i].getPin()) + ",\"note\":\"" + items[i].getNote() + "\",\"midi\":" + String(items[i].getMidiNote()) + "}";
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
        int pStart = obj.indexOf(":") + 1; int pComma = obj.indexOf(",", pStart); int pin = obj.substring(pStart, pComma).toInt();
        int nStart = obj.indexOf(":", pComma) + 2; int nEnd = obj.indexOf("\"", nStart); String note = obj.substring(nStart, nEnd);
        int mStart = obj.indexOf(":", nEnd + 1) + 1; int mEnd = obj.indexOf("}", mStart); int midi = obj.substring(mStart, mEnd).toInt();
        solenoid.addSolenoid(pin, note, midi);
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
        String line = file.readStringUntil('\n'); line.trim();
        if (line.length() == 0) continue;
        int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
        json += "{\"pin\":" + line.substring(0, c1) + ",\"note\":\"" + line.substring(c1+1, c2) + "\",\"midi\":" + line.substring(c2+1) + "},";
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
    char buf[1024]; int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      buf[ret] = 0; String data(buf);
      int dStart = data.indexOf("\"duration\":") + 11;
      int dEnd = data.indexOf(",", dStart); if(dEnd == -1) dEnd = data.indexOf("}", dStart);
      player.setSolenoidTime(data.substring(dStart, dEnd).toInt());
      File file = SD.open("/solenoids.txt", FILE_WRITE);
      int start = data.indexOf("{\"pin\":");
      while (start >= 0) {
        int end = data.indexOf("}", start); String obj = data.substring(start, end + 1);
        int p1 = obj.indexOf(":")+1, p2 = obj.indexOf(",", p1), p3 = obj.indexOf(":", p2)+2, p4 = obj.indexOf("\"", p3), p5 = obj.indexOf(":", p4)+1, p6 = obj.indexOf("}", p5);
        file.println(obj.substring(p1, p2) + "," + obj.substring(p3, p4) + "," + obj.substring(p5, p6));
        start = data.indexOf("{\"pin\":", end);
      }
      file.close(); solenoid.loadConfig(); return httpd_resp_send(req, "OK", 2);
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
    char buf[16]; memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) { player.setSolenoidTime(String(buf).toInt()); httpd_resp_send(req, "OK", 2); }
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
        String decodedName = String(name); decodedName.replace("%20", " ");
        if (sdcard.deleteFile(("/" + decodedName).c_str())) { needsScan = true; return httpd_resp_send(req, "OK", 2); }
      }
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete Failed");
  }
  return ESP_FAIL;
}

esp_err_t api_wifi_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    String ssid, pass; bool enable; wifiManager.getSettings(ssid, pass, enable);
    String json = "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"enable\":" + (enable ? "true" : "false") + "}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
  } else if (req->method == HTTP_POST) {
    char buf[512]; memset(buf, 0, sizeof(buf));
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
      String data(buf);
      String ssid = "", pass = ""; bool enable = false;
      int sIdx = data.indexOf("\"ssid\":\""); if (sIdx != -1) { int start = sIdx + 8; int end = data.indexOf("\"", start); if (end != -1) ssid = data.substring(start, end); }
      int pIdx = data.indexOf("\"pass\":\""); if (pIdx != -1) { int start = pIdx + 8; int end = data.indexOf("\"", start); if (end != -1) pass = data.substring(start, end); }
      int eIdx = data.indexOf("\"enable\":"); if (eIdx != -1) { int colonIdx = data.indexOf(":", eIdx); if (colonIdx != -1) { String val = data.substring(colonIdx + 1); val.trim(); if (val.startsWith("true")) enable = true; else if (val.startsWith("false")) enable = false; } }
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

void WebServerManager::beginAPMinimal() {
  if (active) return;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  if (httpd_start(&server, &config) != ESP_OK) return;

  httpd_uri_t root_uri = {"/", HTTP_GET, root_handler, nullptr};
  httpd_uri_t wifi_get_uri = {"/api/wifi", HTTP_GET, api_wifi_handler, nullptr};
  httpd_uri_t wifi_post_uri = {"/api/wifi", HTTP_POST, api_wifi_handler, nullptr};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &wifi_get_uri);
  httpd_register_uri_handler(server, &wifi_post_uri);

  active = true;
}

void WebServerManager::beginSTAFull() {
  if (active) return;
  
  // Tunggu sejenak untuk memastikan WiFi benar-benar terhubung
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  if (MDNS.begin("mydashboard")) { 
    MDNS.addService("http", "tcp", 80); 
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80; config.max_uri_handlers = 20;
  config.max_open_sockets = 7;
  if (httpd_start(&server, &config) != ESP_OK) return;

  httpd_uri_t root_uri = {"/", HTTP_GET, root_handler, nullptr};
  httpd_uri_t upload_uri = {"/upload", HTTP_POST, upload_handler, nullptr};
  httpd_uri_t solenoids_get_uri = {"/api/solenoids", HTTP_GET, api_solenoids_handler, nullptr};
  httpd_uri_t solenoids_post_uri = {"/api/solenoids", HTTP_POST, api_solenoids_handler, nullptr};
  httpd_uri_t solenoid_test_uri = {"/api/solenoid/test", HTTP_POST, api_solenoid_test_handler, nullptr};
  httpd_uri_t backup_uri = {"/api/backup", HTTP_GET, api_backup_handler, nullptr};
  httpd_uri_t restore_uri = {"/api/restore", HTTP_POST, api_restore_handler, nullptr};
  httpd_uri_t time_get_uri = {"/api/time", HTTP_GET, api_time_handler, nullptr};
  httpd_uri_t time_post_uri = {"/api/time", HTTP_POST, api_time_handler, nullptr};
  httpd_uri_t files_get_uri = {"/api/files", HTTP_GET, api_files_handler, nullptr};
  httpd_uri_t files_delete_uri = {"/api/files", HTTP_DELETE, api_files_handler, nullptr};
  httpd_uri_t wifi_get_uri = {"/api/wifi", HTTP_GET, api_wifi_handler, nullptr};
  httpd_uri_t wifi_post_uri = {"/api/wifi", HTTP_POST, api_wifi_handler, nullptr};

  httpd_uri_t player_get_uri = {"/api/player", HTTP_GET, [](httpd_req_t *req) {
      String file = String(sdcard.getCurrentFile());
      if (file.length() == 0) file = "No file";
      
      String json = "{\"playing\":" + String(player.isPlaying() ? "true" : "false") + 
                    ",\"paused\":" + String(player.isPaused() ? "true" : "false") + 
                    ",\"auto\":" + String(player.isAutoMode() ? "true" : "false") + 
                    ",\"file\":\"" + file + "\"" +
                    ",\"duration\":" + String(player.getDurationUS() / 1000) + 
                    ",\"elapsed\":" + String(player.getElapsedUS() / 1000) + "}";
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, json.c_str(), json.length());
  }, nullptr};

  httpd_uri_t player_cmd_uri = {"/api/player/cmd", HTTP_POST, [](httpd_req_t *req) {
      char buf[64]; size_t len = httpd_req_get_url_query_len(req);
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
  }, nullptr};

  httpd_uri_t ota_uri = {"/update", HTTP_POST, [](httpd_req_t *req) {

      size_t content_len = req->content_len;
      if (content_len == 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
      if (!Update.begin(content_len)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Begin Failed");
      char *buf = (char *)malloc(1024); int ret;
      while ((ret = httpd_req_recv(req, buf, 1024)) > 0) {
          if (Update.write((uint8_t*)buf, ret) != ret) { free(buf); Update.end(); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA Write Failed"); }
      }
      free(buf);
      if (Update.end()) { 
          vTaskDelay(pdMS_TO_TICKS(1000)); 
          Preferences prefs; prefs.begin("ota", false);
          // Menggunakan waktu sistem jika sudah sinkron, atau format sederhana
          prefs.putString("last", "Updated"); // TODO: Ganti dengan tanggal jika NTP jalan
          prefs.end();
          ESP.restart(); 
          return httpd_resp_send(req, "OK", 2); 
      }
      else return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA End Failed");
  }, nullptr};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &upload_uri);
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

  active = true;
}

void WebServerManager::update() {
  if (!active) return;
  if (needsScan) { sdcard.scan(); needsScan = false; }
}

void WebServerManager::stop() {
  if (!active) return;
  if (server) { httpd_stop(server); server = nullptr; }
  MDNS.end();
  active = false;
}

bool WebServerManager::isActive() const { return active; }