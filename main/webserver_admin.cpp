#include "webserver_admin.h"
#include "config.h"
#include "pins.h"
#include <Preferences.h>
#include <WiFi.h>
#include <base64.h>

const char htmlPageAdmin[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Admin Dashboard - KELENTANG ROBOT</title>
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
    
    /* Layout Mobile: Max Width 400px, tanpa garis bawah */
    header { width: 100%; max-width: 400px; margin: 0 auto 20px; display: flex; flex-direction: column; align-items: center; gap: 12px; padding-bottom: 15px; text-align: center; }
    header h1 { margin: 0; font-size: 1.6rem; color: var(--accent); letter-spacing: -0.025em; text-shadow: 0 0 10px rgba(0, 240, 255, 0.4); }
    
    /* Info disusun Horizontal, dengan flex-wrap agar aman di layar super kecil */
    .header-info { display: flex; flex-direction: row; justify-content: center; flex-wrap: wrap; gap: 12px; font-size: 0.8rem; color: var(--text-muted); background: var(--card-bg); padding: 8px 12px; border-radius: 20px; border: 1px solid var(--border); backdrop-filter: blur(4px); width: 100%; }
    
    /* Kolom ditumpuk ke bawah */
    .dashboard-grid { display: flex; flex-direction: column; gap: 20px; width: 100%; max-width: 400px; margin: 0 auto; }
    
    .card { background: var(--card-bg); padding: 20px; border-radius: 16px; box-shadow: var(--glass-shadow); backdrop-filter: blur(10px); border: 1px solid rgba(255, 255, 255, 0.1); display: flex; flex-direction: column; width: 100%; transition: transform 0.3s ease, box-shadow 0.3s ease; }
    .card:hover { transform: translateY(-3px); box-shadow: 0 12px 40px rgba(0, 240, 255, 0.1); }
    
    h2 { margin-top: 0; margin-bottom: 14px; color: var(--accent); font-size: 1.15rem; border-bottom: 1px solid rgba(255,255,255,0.05); padding-bottom: 8px; text-shadow: 0 0 8px rgba(0, 240, 255, 0.2); text-align: center; }
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
    
    .truncate-text { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; display: block; width: 100%; line-height: 42px; text-align: center; }

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
    
    footer { text-align: center; color: var(--text-muted); font-size: 0.85rem; margin-top: 30px; margin-bottom: 15px; width: 100%; max-width: 400px; text-shadow: 0 2px 4px rgba(0,0,0,0.5); }
  </style>
</head>
<body>

<header>
  <h1>KELENTANG ADMIN</h1>
  <div class="header-info">
    <span>IP : <strong style="color: var(--accent);">{{IP_ADDRESS}}</strong></span>
    <span id="rssiWrapper" style="display:inline-flex; align-items:center; gap:6px;">Signal : <span id="rssiBarContainer" style="display:inline-flex; align-items:flex-end; gap:2px; height:14px; width:22px;"><span id="sig1" style="width:3px; height:25%; background:#555; border-radius:1px;"></span><span id="sig2" style="width:3px; height:50%; background:#555; border-radius:1px;"></span><span id="sig3" style="width:3px; height:75%; background:#555; border-radius:1px;"></span><span id="sig4" style="width:3px; height:100%; background:#555; border-radius:1px;"></span></span> <strong id="rssiVal" style="color: var(--accent);">-- dBm</strong></span>
    <span>Temp : <strong id="tempDisplay" style="color: var(--accent);">--.-°C</strong></span>
  </div>
</header>

<div class="dashboard-grid">

  <div class="card">
    <h2>Player Control</h2>
    <div id="playerStatus" style="font-size: 0.9rem; font-weight: 600; color: var(--text-main); margin-bottom: 5px;">Checking Playback Status...</div>
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
        <table><tbody id="fileBody"><tr><td colspan="3" style="padding:15px; color:var(--text-muted);">Reading SD Card...</td></tr></tbody></table>
      </div>
    </div>
    <div id="storageInfo" style="margin-top: 10px; font-size: 0.75rem; color: var(--text-muted); text-align: center; background: rgba(0,0,0,0.2); padding: 4px; border-radius: 4px;">Reading SD Card Capacity...</div>
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
            <th class="col-note">NOTE</th>
            <th class="col-midi">MIDI</th>
            <th class="col-ch">CH</th>
            <th class="col-en">EN</th>
            <th class="col-s-action">ACTION</th>
          </tr>
        </thead>
      </table>
      <div class="scroll-body">
        <table><tbody id="solenoidBody"><tr><td colspan="6" style="padding:15px; color:var(--text-muted);">Loading Actuator Configuration...</td></tr></tbody></table>
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
    if (cmd === 'start') {
        const btn = document.getElementById('btnStart');
        const status = document.getElementById('playerStatus');
        const isPlaying = btn.innerText === "Play";
        
        btn.innerText = isPlaying ? "Pause" : "Play";
        const currentStatus = status.innerText;
        status.innerText = isPlaying ? "Playing" : "Paused";
    }
    await fetch('/api/player/cmd?action='+cmd, { method: 'POST' });
}

async function loadDynamic() {
    if (isFetching || isSavingWifi) return; 
    isFetching = true;
    
    try {
        const res = await fetch('/api/status?t=' + Date.now());
        if (res.ok) {
            const data = await res.json();
            
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

            document.getElementById('tempDisplay').innerText = data.temp !== "--" ? Number(data.temp).toFixed(1) + '°C' : '--.-°C';
            
            const player = data.player;
            const cleanName = player.file.replace(/\//g, '').replace(/\.(mid|midi)$/i, '');
            document.getElementById('playerStatus').innerText = player.playing ? "Playing : " + cleanName : (player.paused ? "Paused : " + cleanName : "Stopped : " + cleanName);
            document.getElementById('btnStart').innerText = player.playing ? "Pause" : "Play";
            document.getElementById('modeDisplay').innerText = player.auto ? "Continuous" : "PlayOnce";
            
            const dur = Number(player.duration) || 0;
            const el = Number(player.elapsed) || 0;
            let barWidth = dur > 0 ? (el / dur * 100) : 0;
            barWidth = Math.min(100, Math.max(0, barWidth));
            document.getElementById('playerBar').style.width = barWidth + '%';
            
            const remaining = Math.max(0, dur - el);
            document.getElementById('timeElapsed').innerText = formatTime(el);
            document.getElementById('timeRemaining').innerText = formatTime(remaining);
        }
    } catch (e) {}
    
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
                sBody.innerHTML = `<tr><td colspan="6" style="padding:15px; color:var(--text-muted);">No Actuator Configured</td></tr>`;
            } else {
                let sHtml = '';
                solenoids.forEach(s => { 
                    sHtml += `<tr>
                    <td class="col-pin"><div class="cell-content">${s.pin}</div></td>
                    <td class="col-note" title="${s.note}"><div class="cell-content truncate-text">${s.note}</div></td>
                    <td class="col-midi"><div class="cell-content"><input type="text" id="editMidi-${s.pin}" value="${s.midi}" disabled oninput="this.value = this.value.replace(/[^0-9]/g, '')"></div></td>
                    <td class="col-ch"><div class="cell-content"><input type="text" id="editCh-${s.pin}" value="${s.ch}" disabled oninput="this.value = this.value.replace(/[^0-9]/g, '')"></div></td>
                    <td class="col-en"><div class="cell-content"><input type="checkbox" id="editEn-${s.pin}" ${s.en ? 'checked' : ''} disabled></div></td>
                    <td class="col-s-action">
                        <div class="cell-content" style="gap:4px;">
                            <button class="primary action-btn" style="flex:1;" onclick="testSolenoid(${s.pin})">Test</button>
                            <button class="danger action-btn" style="flex:1;" onclick="removeSolenoid(${s.pin})">Delete</button>
                        </div>
                    </td>
                </tr>`; 
                });
                sBody.innerHTML = sHtml;
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
                fBody.innerHTML = `<tr><td colspan="3" style="padding:15px; color:var(--text-muted);">SD Card Is Empty</td></tr>`;
            } else {
                let fHtml = '';
                filesRes.files.forEach(f => { 
                  fHtml += `<tr>
                    <td class="col-name" title="${f.name}"><div class="cell-content truncate-text" style="padding-left: 10px; text-align: left;">${f.name}</div></td>
                    <td class="col-f-size"><div class="cell-content">${formatSize(f.size)}</div></td>
                    <td class="col-f-action"><div class="cell-content"><button class="danger action-btn" style="width: 90%;" onclick="deleteFile('${f.name}')">Delete</button></div></td>
                  </tr>`; 
                });
                fBody.innerHTML = fHtml;
            }
            
            const sInfo = document.getElementById('storageInfo');
            if (filesRes.storage) {
                const used = filesRes.storage.total - filesRes.storage.free;
                sInfo.innerText = `Total : ${formatSize(filesRes.storage.total)} | Used : ${formatSize(used)} | Free : ${formatSize(filesRes.storage.free)}`;
            } else {
                sInfo.innerText = 'SD Card Not Detected';
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
  if (!newTime || isNaN(parseInt(newTime))) { alert('Please enter a valid duration!'); return; }
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
  
  if (!pinRaw || !midiRaw) { alert('GPIO and MIDI fields are required!'); return; }
  
  const pin = parseInt(pinRaw);
  const midi = parseInt(midiRaw);
  const ch = chRaw === "" ? 0 : parseInt(chRaw);
  
  if (isNaN(pin) || isNaN(midi) || isNaN(ch)) { alert('GPIO, MIDI, and CH must be numbers!'); return; }
  if (ch < 0 || ch > 16) { alert('MIDI Channel must be between 0 and 16!'); return; }
  if (!allowedPins.includes(pin)) { alert('Invalid GPIO!'); return; }
  
  const resS = await fetch('/api/solenoids');
  let solenoids = await resS.json();
  
  if (solenoids.some(s => s.pin === pin)) { alert('GPIO is already in use!'); return; }
  
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
    if(confirm('Delete file ' + name + '?')) {
        await fetch('/api/files?name='+name, { method: 'DELETE' }); 
        await loadStatic(); 
    }
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

static esp_err_t admin_root_handler(httpd_req_t *req) {
  // Pengecekan Basic Authentication
  char auth_hdr[128] = { 0 };
  esp_err_t res = httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr,
                                              sizeof(auth_hdr));

  // Format Auth -> Username:Password di encode base64
  String authString =
    String(WEB_ADMIN_USERNAME) + ":" + String(WEB_ADMIN_PASSWORD);
  String expectedBase64 = "Basic " + base64::encode(authString);

  if (res != ESP_OK || String(auth_hdr) != expectedBase64) {
    httpd_resp_set_hdr(req, "WWW-Authenticate",
                       "Basic realm=\"Secure Admin Area\"");
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED,
                               "401 Unauthorized - Need Valid Credentials");
  }

  // Jika otentikasi lolos, render HTML Admin Full
  String output = String(htmlPageAdmin);

  // Lakukan Replace Variable Placeholder
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

void register_urihandlers_admin(httpd_handle_t server) {
  // Daftarkan URL /admin
  httpd_uri_t admin_uri = { "/admin", HTTP_GET, admin_root_handler, nullptr };
  httpd_register_uri_handler(server, &admin_uri);
}