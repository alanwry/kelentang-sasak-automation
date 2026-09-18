#include "led.h"
#include "button.h"
#include "pins.h"
#include "player.h"
#include "sdcard.h"
#include "solenoid.h"
#include <WiFi.h>

extern bool isSystemHang();

LedController led;

void LedController::begin() {
    if (!button.isInitialized()) return;
    
    // Initialize LED pins on PCF8574
    pcf.pinMode(PIN_LED_NET, OUTPUT);
    pcf.pinMode(PIN_LED_RUN, OUTPUT);
    pcf.pinMode(PIN_LED_ERR, OUTPUT);
    
    pcf.digitalWrite(PIN_LED_NET, LOW);
    pcf.digitalWrite(PIN_LED_RUN, LOW);
    pcf.digitalWrite(PIN_LED_ERR, LOW);
}

void LedController::update() {
    if (!button.isInitialized()) return;

    // NET LED: WiFi status feedback
    static uint32_t lastNetBlink = 0;
    static bool netBlinkState = false;
    uint32_t now = millis();
    
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        if (now - lastNetBlink >= 200) {
            lastNetBlink = now;
            netBlinkState = !netBlinkState;
        }
        pcf.digitalWrite(PIN_LED_NET, netBlinkState ? HIGH : LOW);
    } else if (WiFi.getMode() == WIFI_STA) {
        if (WiFi.status() == WL_CONNECTED) {
            pcf.digitalWrite(PIN_LED_NET, HIGH);
        } else {
            if (now - lastNetBlink >= 1000) {
                lastNetBlink = now;
                netBlinkState = !netBlinkState;
            }
            pcf.digitalWrite(PIN_LED_NET, netBlinkState ? HIGH : LOW);
        }
    } else {
        pcf.digitalWrite(PIN_LED_NET, LOW);
    }

    // RUN LED: Playback status feedback
    if (player.isPlaying()) {
        static uint32_t lastRunBlink = 0;
        static bool runBlinkState = false;
        if (now - lastRunBlink >= 500) {
            lastRunBlink = now;
            runBlinkState = !runBlinkState;
        }
        pcf.digitalWrite(PIN_LED_RUN, runBlinkState ? HIGH : LOW);
    } else if (player.isPaused()) {
        pcf.digitalWrite(PIN_LED_RUN, HIGH);
    } else {
        pcf.digitalWrite(PIN_LED_RUN, LOW);
    }

    // ERR LED: System health check
    bool systemError = !sdcard.isDetected() || 
                       player.hasLoadingError() || 
                       solenoid.hasConfigError() || 
                       isSystemHang();
    
    pcf.digitalWrite(PIN_LED_ERR, systemError ? HIGH : LOW);
}
