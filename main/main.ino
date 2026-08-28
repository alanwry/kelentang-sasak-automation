#include "config.h"
#include "pins.h"
#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led.h"
#include "midi.h"
#include "player.h"
#include "sdcard.h"
#include "solenoid.h"
#include "webserver.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <Wire.h>

extern void triggerBuzzer(uint16_t duration);

QueueHandle_t buttonQueue;
SemaphoreHandle_t wifiSemaphore;

volatile uint32_t lastMidiTask = 0;
volatile uint32_t lastSystemTask = 0;

bool isSystemHang() {
  uint32_t now = millis();
  bool midiHang = (now - lastMidiTask > 5000);
  bool systemHang = (now - lastSystemTask > 5000);
  return midiHang || systemHang;
}

void midiTask(void *pvParameters) {
  for (;;) {
    lastMidiTask = millis();
    ButtonID evt;
    if (xQueueReceive(buttonQueue, &evt, 0) == pdPASS) {
      player.handleEvent(evt);
    }
    player.update();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(2000));

  Wire.begin(I2C_SDA, I2C_SCL);

  button.begin();
  wifiManager.begin();

  if (pcf.digitalRead(PIN_MODE) == LOW) {

    vTaskDelay(pdMS_TO_TICKS(500));
    triggerBuzzer(100);
    vTaskDelay(pdMS_TO_TICKS(150));
    triggerBuzzer(100);

    wifiManager.startAPMinimal();

    led.begin();

    uint32_t pressStart = 0;
    while (true) {
      button.update();
      led.update();

      bool modePressed = (pcf.digitalRead(PIN_MODE) == LOW);
      if (modePressed) {
        if (pressStart == 0) pressStart = millis();
        if (millis() - pressStart >= 2000) {

          triggerBuzzer(100);
          vTaskDelay(pdMS_TO_TICKS(150));
          triggerBuzzer(100);
          vTaskDelay(pdMS_TO_TICKS(1000));

          ESP.restart();
        }
      } else {
        pressStart = 0;
      }

      webServer.update();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } else {
    if (wifiManager.isSTAEnabled())
      wifiManager.startSTAOnly();

    led.begin();

    if (sdcard.begin()) {
      LOG("[SYSTEM] SD Card module initialized\n");
    } else {
      LOG("[SYSTEM] SD Card module failed to init\n");
    }

    solenoid.begin();
    sdcard.scan();
    player.begin();
    LOG("[SYSTEM] Play Mode: %s\n", player.isAutoMode() ? "Continuous" : "PlayOnce");
    LOG("[SYSTEM] Actuator Duration: %d ms\n", player.getSolenoidTime());

    triggerBuzzer(400);

    buttonQueue = xQueueCreate(10, sizeof(ButtonID));

    xTaskCreatePinnedToCore(midiTask, "midiTask", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(systemTask, "systemTask", 8192, NULL, 1, NULL, 0);
  }
}

void systemTask(void *pvParameters) {
  for (;;) {
    lastSystemTask = millis();
    button.update();
    sdcard.update();

    ButtonID evt = button.getEvent();
    if (evt != BTN_NONE) {
      xQueueSend(buttonQueue, &evt, 0);
    }

    webServer.update();
    wifiManager.update();
    solenoid.update();
    led.update();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void loop() {
  vTaskDelete(NULL);
}