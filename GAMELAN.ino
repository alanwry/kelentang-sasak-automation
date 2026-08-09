#include "config.h"
#include "pins.h"
#include "button.h"
#include "display.h"
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
#include <Adafruit_PCF8574.h>
#include <WiFi.h>
#include <Wire.h>

extern Adafruit_PCF8574 pcf;
extern void triggerBuzzer(uint16_t duration);

QueueHandle_t buttonQueue;
SemaphoreHandle_t wifiSemaphore;

void midiTask(void *pvParameters) {
  for (;;) {
    ButtonID evt;
    if (xQueueReceive(buttonQueue, &evt, 0) == pdPASS) {
      player.handleEvent(evt);
    }
    player.update();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

void systemTask(void *pvParameters) {
  for (;;) {
    button.update();

    uint32_t holdTime = button.getStopHoldDuration();
    static bool wifiActionTaken = false;
    if (holdTime > 0) {
      if (!wifiActionTaken) {
        if (holdTime >= WIFI_DISABLE_MS) { // 5s hold to switch to STA
          if (WiFi.getMode() == WIFI_AP) {
            if (xSemaphoreTake(wifiSemaphore, pdMS_TO_TICKS(100))) {
              triggerBuzzer(150);
              vTaskDelay(200 / portTICK_PERIOD_MS);
              triggerBuzzer(150);

              wifiManager.stopAll();
              if (wifiManager.isSTAEnabled())
                wifiManager.startSTA();
              xSemaphoreGive(wifiSemaphore);
            }
            wifiActionTaken = true;
          }
        } else if (holdTime >= WIFI_ENABLE_MS) { // 2s hold to switch to AP
          if (WiFi.getMode() != WIFI_AP) {
            if (xSemaphoreTake(wifiSemaphore, pdMS_TO_TICKS(100))) {
              triggerBuzzer(150);
              vTaskDelay(200 / portTICK_PERIOD_MS);
              triggerBuzzer(150);

              wifiManager.stopAll();
              wifiManager.startAP();
              xSemaphoreGive(wifiSemaphore);
            }
            wifiActionTaken = true;
          }
        }
      }
    } else {
      wifiActionTaken = false;
    }

    ButtonID evt = button.getEvent();
    if (evt != BTN_NONE) {
      xQueueSend(buttonQueue, &evt, 0);
    }

    webServer.update();
    wifiManager.update();
    solenoid.update();
    display.update();
    led.update();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("===================================");
  Serial.println("GAMELAN SASAK");
  Serial.println(FW_VERSION);
  Serial.println("===================================");

  Wire.begin(LCD_SDA, LCD_SCL);
  button.begin();

  pcf.pinMode(PIN_BUZZER, OUTPUT);
  pcf.digitalWrite(PIN_BUZZER, LOW);

  display.begin();
  display.splash();
  wifiManager.begin();

  wifiSemaphore = xSemaphoreCreateMutex();
  if (wifiManager.isSTAEnabled())
    wifiManager.startSTA();

  led.begin();
  sdcard.begin();
  solenoid.begin();
  sdcard.scan();
  player.begin();

  triggerBuzzer(400);

  buttonQueue = xQueueCreate(10, sizeof(ButtonID));

  xTaskCreatePinnedToCore(midiTask, "midiTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(systemTask, "systemTask", 8192, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelete(NULL);
}
