#include "wifi_manager.h"
#include "config.h"
#include "esp_wifi.h"
#include "webserver.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern void triggerBuzzer(uint16_t duration);

WiFiManager wifiManager;

WiFiManager::WiFiManager()
  : ssid(""), password(""), enableSTA(false), isConnecting(false), connectionStart(0) {}

void WiFiManager::begin() {
  loadFromPrefs();
}

void WiFiManager::update() {
  if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED && !isConnecting) {
    isConnecting = true;
    connectionStart = millis();
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
  }

  if (isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnecting = false;
      LOG("[WIFI] STA Connected successfully\n");
    } else if (millis() - connectionStart > 15000) {
      isConnecting = false;
      LOG("[WIFI] STA Connection failed (timeout). Switching to AP.\n");
      startAPMinimal();
    }
  }
}

void WiFiManager::loadFromPrefs() {
  if (prefs.begin("gamelan_wifi", true)) {
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    enableSTA = prefs.getBool("enableSTA", false);
    prefs.end();
    LOG("[WIFI] Loaded settings: SSID='%s', Password='%s', Enabled=%s\n",
        ssid.c_str(), password.c_str(), enableSTA ? "true" : "false");
  } else {
    if (prefs.begin("gamelan_wifi", false)) {
      ssid = "";
      password = "";
      enableSTA = false;
      prefs.end();
    }
  }
}

void WiFiManager::saveSettings(String newSsid, String newPassword, bool newEnableSTA, bool forceRestart) {
  ssid = newSsid;
  password = newPassword;
  enableSTA = newEnableSTA;

  if (prefs.begin("gamelan_wifi", false)) {
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.putBool("enableSTA", enableSTA);
    prefs.end();
    LOG("[WIFI] Settings saved: SSID='%s', Enabled=%s\n", ssid.c_str(), enableSTA ? "true" : "false");
  }

  triggerBuzzer(50);
  vTaskDelay(pdMS_TO_TICKS(100)); // Delay agar buzzer sempat bunyi
  ESP.restart();
}

void WiFiManager::stopAll() {
  if (webServer.isActive()) {
    webServer.stop();
  }
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  isConnecting = false;
}

void WiFiManager::startAPMinimal() {
  stopAll();
  WiFi.mode(WIFI_AP);
  IPAddress local_IP(192, 11, 11, 21);
  IPAddress gateway(192, 11, 11, 21);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, 0, 2)) {
    LOG("[WIFI] AP Started: %s\n", WIFI_SSID);
    webServer.beginAPMinimal();
  } else {
    LOG("[WIFI] AP Start Failed\n");
  }
}

void WiFiManager::startSTAOnly() {
  if (ssid.length() == 0) {
    startAPMinimal();
    return;
  }
  stopAll();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("mydashboard");
  WiFi.begin(ssid.c_str(), password.c_str());

  xTaskCreatePinnedToCore(
    [](void *parameter) {
      uint32_t startAttempt = millis();
      bool connected = false;

      while (millis() - startAttempt < 15000) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if (connected) {
        LOG("[WIFI] STA Connected\n");
        if (MDNS.begin("mydashboard")) {
          MDNS.addService("http", "tcp", 80);
        }
        webServer.beginSTAFull();
      } else {
        LOG("[WIFI] STA Connection FAILED, falling back to AP\n");
        wifiManager.startAPMinimal();
      }
      vTaskDelete(NULL);
    },
    "STANormal", 4096, NULL, 1, NULL, 1);
}

void WiFiManager::getSettings(String &outSsid, String &outPassword, bool &outEnableSTA) {
  outSsid = ssid;
  outPassword = password;
  outEnableSTA = enableSTA;
}

bool WiFiManager::isSTAEnabled() {
  return enableSTA;
}
