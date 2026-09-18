#include "webserver.h"
#include "webserver_ap.h"
#include "webserver_user.h"
#include "webserver_admin.h"
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
}  // namespace

// =======================================================
// KUMPULAN API HANDLER (Digunakan oleh User maupun Admin)
// =======================================================

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
          if (filename.length() > 0 && (filename.endsWith(".mid") || filename.endsWith(".midi"))) {
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
    json.reserve(512);
    Solenoid *items = solenoid.getItems();
    for (uint8_t i = 0; i < solenoid.getCount(); i++) {
      json += "{\"pin\":" + String(items[i].getPin()) + ",\"note\":\"" + items[i].getNote() + "\",\"midi\":" + String(items[i].getMidiNote()) + ",\"ch\":" + String(items[i].getMidiChannel()) + ",\"en\":" + String(items[i].isEnabled() ? 1 : 0) + "}";
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
        old_states.push_back({ solenoid.getItems()[i].getPin(),
                               solenoid.getItems()[i].isEnabled() });
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
      json += "{\"pin\":" + line.substring(0, c1) + ",\"note\":\"" + line.substring(c1 + 1, c2) + "\",\"midi\":" + line.substring(c2 + 1) + "},";
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
    json.reserve(512);
    if (digitalRead(PIN_SD_DET) == LOW) {
      File root = SD.open("/");
      File file = root.openNextFile();
      bool first = true;
      while (file) {
        String name = file.name();
        if (!file.isDirectory() && (name.endsWith(".mid") || name.endsWith(".midi"))) {
          if (!first)
            json += ",";
          json += "{\"name\":\"" + name + "\",\"size\":" + String(file.size()) + "}";
          first = false;
        }
        file = root.openNextFile();
      }
      json += "], \"storage\":{\"total\":" + String(SD.totalBytes()) + ", \"free\":" + String(SD.totalBytes() - SD.usedBytes()) + "}}";
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
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
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

// =======================================================
// SETUP SERVER UNTUK MODE AP DAN STA
// =======================================================

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

  // Panggil Registrasi Routing AP dari File webserver_ap.cpp
  register_urihandlers_ap(server);

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

  // LIMIT DITAMBAHKAN MENJADI 35 KARENA BANYAK API!
  config.max_uri_handlers = 35;
  config.max_open_sockets = 7;
  config.stack_size = 16384;

  if (httpd_start(&server, &config) != ESP_OK)
    return;

  // 1. PANGGIL ROUTING HALAMAN HTML (USER & ADMIN)
  register_urihandlers_user(server);
  register_urihandlers_admin(server);

  // 2. DAFTARKAN SEMUA API ENDPOINTS
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
    nullptr
  };

  httpd_uri_t player_cmd_uri = {
    "/api/player/cmd", HTTP_POST,
    [](httpd_req_t *req) {
      char buf[64];
      size_t len = httpd_req_get_url_query_len(req);
      if (len < sizeof(buf)) {
        httpd_req_get_url_query_str(req, buf, len + 1);
        char action[16];
        if (httpd_query_key_value(buf, "action", action, sizeof(action)) == ESP_OK) {
#if ENABLE_BUZZER_KEYBEEP
          triggerBuzzer(50);
#endif
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
    nullptr
  };

  httpd_uri_t ota_uri = {
    "/update", HTTP_POST,
    [](httpd_req_t *req) {
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
      } else {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA End Failed");
      }
    },
    nullptr
  };

  httpd_uri_t ws_uri = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = NULL, .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = NULL };
  httpd_uri_t upload_uri = { "/upload", HTTP_POST, upload_handler, nullptr };
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
  httpd_uri_t reboot_post_uri = { "/api/reboot", HTTP_POST, reboot_handler, nullptr };

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