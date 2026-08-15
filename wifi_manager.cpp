#include "wifi_manager.h"
#include "config.h"
#include "esp_wifi.h"
#include "webserver.h"
#include <ESPmDNS.h>
#include <WiFi.h>

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
      // webServer.begin(); // Removed: handled by startSTAOnly
      Serial.println("[SYSTEM]: STA successfully connected");
    } else if (millis() - connectionStart > 15000) { // 15s timeout
      isConnecting = false;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      Serial.println("[SYSTEM]: STA failed to connect");
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
    Serial.printf("[SYSTEM]: WiFi load setting SSID: '%s', STA Enabled: %s\n",
                  ssid.c_str(), enableSTA ? "ON" : "OFF");
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

  Serial.printf("[WIFI]: Saving to Preferences - SSID: '%s', Enable: %s\n",
                ssid.c_str(), enableSTA ? "ON" : "OFF");

  if (prefs.begin("gamelan_wifi", false)) {
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.putBool("enableSTA", enableSTA);
    prefs.end();
    
    // Single longer beep for save confirmation
    triggerBuzzer(400);
    delay(400); // Give the buzzer time to finish before potentially changing state
    
    // Only take immediate action if we are in STA operational mode
    if (WiFi.getMode() == WIFI_STA) {
        if (!enableSTA) {
            Serial.println("[WIFI]: STA disabled, disconnecting...");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        } else if (oldEnableSTA != enableSTA || oldSsid != ssid || oldPassword != password) {
            // Restart only if STA was just enabled or credentials changed
            Serial.println("[WIFI]: STA settings updated, restarting...");
            ESP.restart();
        }
    } else if (WiFi.getMode() == WIFI_AP && enableSTA && !oldEnableSTA) {
        // If in AP mode and user enables STA, we might want to restart to apply
        Serial.println("[WIFI]: STA enabled in AP mode, restarting to apply...");
        ESP.restart();
    }
    Serial.println("[WIFI]: Save complete");
  } else {
    Serial.println("[WIFI]: Error: Failed to open Preferences for writing");
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
  Serial.println("[WIFI]: Cleaning up WiFi stack...");

  if (webServer.isActive()) {
    webServer.stop();
  }

  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  isConnecting = false;

  delay(100);
  Serial.println("[WIFI]: WiFi stack cleaned.");
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
          Serial.println("[WIFI]: AP Minimal Mode Started");
          webServer.beginAPMinimal(); // Call the minimal webserver dashboard
        }
        vTaskDelete(NULL);
      },
      "APSetup", 4096, NULL, 1, NULL, 1);
}

void WiFiManager::startSTAOnly() {
  if (ssid.length() == 0) return;
  stopAll();
  xTaskCreatePinnedToCore(
      [](void *parameter) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("mydashboard");
        WiFi.begin(wifiManager.ssid.c_str(), wifiManager.password.c_str());
        Serial.println("[WIFI]: STA Normal Mode Started");
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (WiFi.status() == WL_CONNECTED) {
          webServer.beginSTAFull(); // Call the full operational webserver dashboard
        }
        vTaskDelete(NULL);
      },
      "STANormal", 4096, NULL, 1, NULL, 1);
}
