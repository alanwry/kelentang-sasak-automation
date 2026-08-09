#include "wifi_manager.h"
#include "config.h"
#include "display.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "webserver.h"

extern void triggerBuzzer(uint16_t duration);

WiFiManager wifiManager;
WiFiManager::WiFiManager() : ssid(""), password(""), enableSTA(false), isConnecting(false), connectionStart(0) {}

void WiFiManager::begin() { 
  loadFromPrefs(); 
}

void WiFiManager::update() {
  if (isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnecting = false;
      webServer.begin();
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
    Serial.printf("[SYSTEM]: WiFi load setting SSID: '%s', STA Enabled: %s\n", ssid.c_str(), enableSTA ? "ON" : "OFF");
  } else {
    // If failed, it might not exist. Try opening in write mode to initialize it.
    if (prefs.begin("gamelan_wifi", false)) {
        ssid = "";
        password = "";
        enableSTA = false;
        prefs.end();
    }
  }
}

void WiFiManager::saveSettings(String newSsid, String newPassword, bool newEnableSTA) {
  ssid = newSsid;
  password = newPassword;
  enableSTA = newEnableSTA;

  Serial.printf("[WIFI]: Saving to Preferences - SSID: '%s', Enable: %s\n", ssid.c_str(), enableSTA ? "ON" : "OFF");
  
  if (prefs.begin("gamelan_wifi", false)) {
    size_t sLen = prefs.putString("ssid", ssid);
    size_t pLen = prefs.putString("password", password);
    size_t eLen = prefs.putBool("enableSTA", enableSTA);
    prefs.end();
    triggerBuzzer(400);
    Serial.printf("[WIFI]: Save complete (Wrote SSID: %d, Pass: %d, Enable: %d)\n", sLen, pLen, eLen);
  } else {
    Serial.println("[WIFI]: Error: Failed to open Preferences for writing");
  }
}

void WiFiManager::getSettings(String &outSsid, String &outPassword, bool &outEnableSTA) {
  outSsid = ssid;
  outPassword = password;
  outEnableSTA = enableSTA;
}

bool WiFiManager::isSTAEnabled() { 
  return enableSTA; 
}
void WiFiManager::stopAll() {
  Serial.println("[WIFI]: Cleaning up WiFi stack...");
  
  // Secure webserver access
  if (webServer.isActive()) {
      webServer.stop();
  }
  
  // Gentler disconnect
  WiFi.disconnect(true); 
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  isConnecting = false; 

  delay(200); 
  Serial.println("[WIFI]: WiFi stack cleaned.");
}

void WiFiManager::startAP() {
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  
  WiFi.mode(WIFI_AP);
  
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
      webServer.begin();
  }
}

void WiFiManager::startSTA() {
  if (ssid.length() == 0) return;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("mydashboard");
  WiFi.begin(ssid.c_str(), password.c_str());

  isConnecting = true;
  connectionStart = millis();
}


