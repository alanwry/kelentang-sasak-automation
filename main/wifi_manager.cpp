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
    : ssid(""), password(""), enableSTA(false), isConnecting(false),
      connectionStart(0) {}

void WiFiManager::begin() { loadFromPrefs(); }

void WiFiManager::update() {
  if (isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnecting = false;
      Serial.println("[WIFI] STA Connected successfully");
      // webServer.begin(); // Removed: handled by startSTAOnly
    } else if (millis() - connectionStart > 15000) { // 15s timeout
      isConnecting = false;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      Serial.println("[WIFI] STA Connection failed (timeout)");
    }
  }
}

void WiFiManager::loadFromPrefs() {
  // Try read-only first
  if (prefs.begin("gamelan_wifi", true)) {
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    enableSTA = prefs.getBool("enableSTA", false);
    prefs.end();
  } else {
    // If failed, it might not exist. Try opening in write mode to initialize
    // it.
    if (prefs.begin("gamelan_wifi", false)) {
      ssid = "";
      password = "";
      enableSTA = false;
      prefs.end();
    }
  }
}

void WiFiManager::saveSettings(String newSsid, String newPassword,
                               bool newEnableSTA) {
  bool oldEnableSTA = enableSTA;
  String oldSsid = ssid;
  String oldPassword = password;
  
  ssid = newSsid;
  password = newPassword;
  enableSTA = newEnableSTA;

  if (prefs.begin("gamelan_wifi", false)) {
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.putBool("enableSTA", enableSTA);
    prefs.end();
    
    // Single longer beep for save confirmation
    triggerBuzzer(400);
    vTaskDelay(pdMS_TO_TICKS(400)); // Give the buzzer time to finish before potentially changing state
    
    // Only take immediate action if we are in STA operational mode
    if (WiFi.getMode() == WIFI_STA) {
        if (!enableSTA) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        } else if (oldEnableSTA != enableSTA || oldSsid != ssid || oldPassword != password) {
            // Restart only if STA was just enabled or credentials changed
            ESP.restart();
        }
    } else if (WiFi.getMode() == WIFI_AP && enableSTA && !oldEnableSTA) {
        // If in AP mode and user enables STA, we might want to restart to apply
        ESP.restart();
    }
  } else {
  }
}

void WiFiManager::getSettings(String &outSsid, String &outPassword,
                              bool &outEnableSTA) {
  outSsid = ssid;
  outPassword = password;
  outEnableSTA = enableSTA;
}

bool WiFiManager::isSTAEnabled() { return enableSTA; }
void WiFiManager::stopAll() {

  if (webServer.isActive()) {
    webServer.stop();
  }

  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  isConnecting = false;

  vTaskDelay(pdMS_TO_TICKS(100));
}

void WiFiManager::startAPMinimal() {
  stopAll();
  xTaskCreatePinnedToCore(
      [](void *parameter) {
        WiFi.mode(WIFI_AP);
        IPAddress local_IP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(local_IP, gateway, subnet);

        if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, 0, 4)) {
          webServer.beginAPMinimal(); // Call the minimal webserver dashboard
        }
        vTaskDelete(NULL);
      },
      "APSetup", 4096, NULL, 1, NULL, 1);
}

void WiFiManager::startSTAOnly() {
  if (ssid.length() == 0) return;
  stopAll();
  Serial.println("[WIFI] Starting STA mode...");
  xTaskCreatePinnedToCore(
      [](void *parameter) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("mydashboard");
        WiFi.begin(wifiManager.ssid.c_str(), wifiManager.password.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("[WIFI] STA Connected");
          webServer.beginSTAFull(); // Call the full operational webserver dashboard
        } else {
          Serial.println("[WIFI] STA Connection FAILED");
        }
        vTaskDelete(NULL);
      },
      "STANormal", 4096, NULL, 1, NULL, 1);
}
