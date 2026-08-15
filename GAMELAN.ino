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

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("===================================");
  Serial.println("GAMELAN SASAK");
  Serial.println(FW_VERSION);
  Serial.println("===================================");

  Wire.begin(I2C_SDA, I2C_SCL);
  button.begin();
  wifiManager.begin();

  // Mode Selection Logic
  if (pcf.digitalRead(PIN_MODE) == LOW) {
    Serial.println("[SYSTEM]: Entering AP Setup Mode - Operational features disabled");
    
    // Double beep for AP Mode before init
    delay(500);// clear buzeer
    triggerBuzzer(100);
    delay(150);
    triggerBuzzer(100);

    wifiManager.startAPMinimal();
    
    // In Setup Mode, we stay in setup() and just loop the webserver
    uint32_t pressStart = 0;
    while(true) {
        button.update();
        
        bool modePressed = (pcf.digitalRead(PIN_MODE) == LOW);
        if (modePressed) {
            if (pressStart == 0) pressStart = millis();
            if (millis() - pressStart >= 2000) {
                Serial.println("[SYSTEM]: Restarting from AP Mode (requested by button hold)...");
                
                // Double beep for exit
                triggerBuzzer(100);
                delay(150);
                triggerBuzzer(100);
                delay(1000);
                
                ESP.restart();
            }
        } else {
            pressStart = 0;
        }

        webServer.update();
        delay(10);
    }
  } else {
    Serial.println("[SYSTEM]: Entering Operational Mode");
    if (wifiManager.isSTAEnabled())
      wifiManager.startSTAOnly();

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
}


void systemTask(void *pvParameters) {
  for (;;) {
    button.update();

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
