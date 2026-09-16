#include "webserver_ap.h"
#include "config.h"
#include "wifi_manager.h"
#include <WiFi.h>

const char htmlPageAP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0" />
  <title>ESP32-S3 WROOM-1U</title>
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
    <input type="text" id="wifiSsid" placeholder="SSID" />
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

// Handler untuk halaman depan mode AP
static esp_err_t ap_root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, htmlPageAP, HTTPD_RESP_USE_STRLEN);
}

// Handler untuk API WiFi (Bisa GET untuk membaca, POST untuk menyimpan)
static esp_err_t ap_wifi_handler(httpd_req_t *req) {
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

// Handler untuk merestart ESP32
static esp_err_t ap_reboot_handler(httpd_req_t *req) {
  httpd_resp_send(req, "OK", 2);
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP.restart();
  return ESP_OK;
}

// Fungsi utama untuk mendaftarkan semua rute di atas ke web server ESP32
void register_urihandlers_ap(httpd_handle_t server) {
  httpd_uri_t root_uri = {"/", HTTP_GET, ap_root_handler, nullptr};
  httpd_uri_t wifi_get_uri = {"/api/wifi", HTTP_GET, ap_wifi_handler, nullptr};
  httpd_uri_t wifi_post_uri = {"/api/wifi", HTTP_POST, ap_wifi_handler, nullptr};
  httpd_uri_t reboot_post_uri = {"/api/reboot", HTTP_POST, ap_reboot_handler, nullptr};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &wifi_get_uri);
  httpd_register_uri_handler(server, &wifi_post_uri);
  httpd_register_uri_handler(server, &reboot_post_uri);
}