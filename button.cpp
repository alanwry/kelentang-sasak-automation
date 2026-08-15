#include "button.h"
#include "pins.h"
#include "config.h"
#include <Adafruit_PCF8574.h>

ButtonManager button;
Adafruit_PCF8574 pcf;

static const uint8_t buttonPin[4] = { PIN_PREV, PIN_PLAY_PAUSE, PIN_NEXT, PIN_MODE };

void ButtonManager::begin() {
  // CRITICAL: Ensure PCF8574 I2C communication is established during startup
  if (!pcf.begin(PCF8574_ADDRESS, &Wire)) {
    Serial.println("[SYSTEM]: Error: PCF8574 initialization failed!");
    initialized = false;
    return;
  }
  
  Serial.println("[SYSTEM]: PCF8574 initialized.");
  initialized = true;

  for (int i = 0; i < 4; i++) {
    pcf.pinMode(buttonPin[i], INPUT);
    lastState[i] = pcf.digitalRead(buttonPin[i]);
    pressedState[i] = false;
    lastTime[i] = 0;
  }
}

bool ButtonManager::isInitialized() {
  return initialized;
}

void triggerBuzzer(uint16_t duration) {
  pcf.digitalWrite(PIN_BUZZER, HIGH);
  vTaskDelay(duration / portTICK_PERIOD_MS);
  pcf.digitalWrite(PIN_BUZZER, LOW);
}

void ButtonManager::update() {
  if (!initialized) return;

  event = BTN_NONE;
  uint32_t now = millis();

  for (int i = 0; i < 4; i++) {
    bool state = pcf.digitalRead(buttonPin[i]);

    if (state != lastState[i]) {
      if ((now - lastTime[i]) > BUTTON_DEBOUNCE) {
        lastState[i] = state;
        lastTime[i] = now;

        if (state == LOW) {
          pressedState[i] = true;
          triggerBuzzer(50);

          switch (i) {
            case 0:
              event = BTN_PREV;
              Serial.println("[BUTTON]: PREVIOUS pressed");
              break;
            case 1:
              event = BTN_START;
              Serial.println("[BUTTON]: PLAY/PAUSE pressed");
              break;
            case 2:
              event = BTN_NEXT;
              Serial.println("[BUTTON]: NEXT pressed");
              break;
            case 3:
              event = BTN_MODE;
              Serial.println("[BUTTON]: MODE pressed");
              break;
          }
        } else {
          pressedState[i] = false;
        }
      }
    }
  }
}

ButtonID ButtonManager::getEvent() {
  ButtonID e = event;
  event = BTN_NONE;
  return e;
}
