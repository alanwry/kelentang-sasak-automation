#include "led.h"
#include "button.h"
#include "pins.h"
#include "player.h"
#include "webserver.h"
#include "sdcard.h"
#include "solenoid.h"
#include <WiFi.h>

extern bool isSystemHang();

LedController led;

void LedController::begin() {
  if (!button.isInitialized()) return;
  
  pcf.pinMode(PIN_LED_NET, OUTPUT);
  pcf.pinMode(PIN_LED_RUN, OUTPUT);
  pcf.pinMode(PIN_LED_ERR, OUTPUT);
  
  pcf.digitalWrite(PIN_LED_NET, LOW);
  pcf.digitalWrite(PIN_LED_RUN, LOW);
  pcf.digitalWrite(PIN_LED_ERR, LOW);
}

void LedController::update() {
  if (!button.isInitialized()) {
    // CRITICAL: PCF8574 not initialized. LED control unavailable.
    return;
  }

  static uint32_t lastBlink = 0;
  static bool blinkState = false;
  uint32_t now = millis();
  
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    if (now - lastBlink >= 200) { lastBlink = now; blinkState = !blinkState; }
    pcf.digitalWrite(PIN_LED_NET, blinkState ? HIGH : LOW);
  } else if (WiFi.getMode() == WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      pcf.digitalWrite(PIN_LED_NET, HIGH);
    } else {
      if (now - lastBlink >= 1000) { lastBlink = now; blinkState = !blinkState; }
      pcf.digitalWrite(PIN_LED_NET, blinkState ? HIGH : LOW);
    }
  } else {
    pcf.digitalWrite(PIN_LED_NET, LOW);
  }

  // RUN LED: ON constant when Paused, blinking when Playing
  if (player.isPlaying()) {
    static uint32_t lastPlayBlink = 0;
    static bool playBlinkState = false;
    uint32_t now = millis();
    if (now - lastPlayBlink >= 500) {
      lastPlayBlink = now;
      playBlinkState = !playBlinkState;
    }
    pcf.digitalWrite(PIN_LED_RUN, playBlinkState ? HIGH : LOW);
  } else if (player.isPaused()) {
    pcf.digitalWrite(PIN_LED_RUN, HIGH);
  } else {
    pcf.digitalWrite(PIN_LED_RUN, LOW);
  }

  // ERR LED: Indicates critical system failures
  bool sdError = !sdcard.isDetected();
  bool playerError = player.hasLoadingError();
  bool solenoidError = solenoid.hasConfigError();
  bool systemHang = isSystemHang();
  
  bool systemError = sdError || playerError || solenoidError || systemHang; 
  
  pcf.digitalWrite(PIN_LED_ERR, systemError ? HIGH : LOW);
}
