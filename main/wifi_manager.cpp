#include "wifi_manager.h"
#include "config.h"
#include "webserver.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if ENABLE_BUZZER_KEYBEEP
extern void triggerBuzzer(uint16_t duration);
#endif

WiFiManager wifiManager;

WiFiManager::WiFiManager()
    : ssid(""), password(""), enableSTA(false), isConnecting(false), connectionStart(0) {}

void WiFiManager::begin() {
    loadFromPrefs();
}

void WiFiManager::loadFromPrefs() {
    if (prefs.begin("gamelan_wifi", true)) {
        ssid = prefs.getString("ssid", "");
        password = prefs.getString("password", "");
        enableSTA = prefs.getBool("enableSTA", false);
        prefs.end();
        LOG("[WIFI] Loaded: SSID='%s', Enabled=%s\n", ssid.c_str(), enableSTA ? "true" : "false");
    }
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
            LOG("[WIFI] STA Connected\n");
        } else if (millis() - connectionStart > 15000) {
            isConnecting = false;
            LOG("[WIFI] STA Timeout. Fallback to AP.\n");
            startAPMinimal();
        }
    }
}

void WiFiManager::saveSettings(String newSsid, String newPassword, bool newEnableSTA) {
    ssid = newSsid;
    password = newPassword;
    enableSTA = newEnableSTA;

    if (prefs.begin("gamelan_wifi", false)) {
        prefs.putString("ssid", ssid);
        prefs.putString("password", password);
        prefs.putBool("enableSTA", enableSTA);
        prefs.end();
        LOG("[WIFI] Settings saved\n");
    }

#if ENABLE_BUZZER_KEYBEEP
    triggerBuzzer(50);
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
    ESP.restart();
}

void WiFiManager::stopAll() {
    if (webServer.isActive()) webServer.stop();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    isConnecting = false;
}

void WiFiManager::startAPMinimal() {
    stopAll();
    WiFi.mode(WIFI_AP);
    
    IPAddress ip(192, 11, 11, 21);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));

    if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, 0, 2)) {
        LOG("[WIFI] AP Started: %s\n", WIFI_SSID);
        webServer.beginAPMinimal();
    }
}

void WiFiManager::startSTAOnly() {
    if (ssid.isEmpty()) {
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
                if (MDNS.begin("mydashboard")) MDNS.addService("http", "tcp", 80);
                webServer.beginSTAFull();
            } else {
                LOG("[WIFI] STA Failed. Fallback to AP\n");
                wifiManager.startAPMinimal();
            }
            vTaskDelete(NULL);
        },
        "STANormal", 4096, NULL, 1, NULL, 1);
}

void WiFiManager::getSettings(String &outSsid, String &outPassword, bool &outEnableSTA) const {
    outSsid = ssid;
    outPassword = password;
    outEnableSTA = enableSTA;
}
