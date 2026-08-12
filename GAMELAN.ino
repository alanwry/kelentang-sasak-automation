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
    while(true) {
        button.update();
        
        // Exit AP Mode: Hold Mode Button for 2 seconds
        if (button.getModeHoldDuration() >= 2000) {
            Serial.println("[SYSTEM]: Restarting from AP Mode (requested by button hold)...");
            
            // Double beep for exit
            triggerBuzzer(100);
            delay(150);
            triggerBuzzer(100);
            delay(1000);
            
            ESP.restart();
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

// Global buzzer state for non-blocking pattern
enum BuzzerState { BUZZER_IDLE, BUZZER_BEEPING, BUZZER_HOLDING };
BuzzerState bState = BUZZER_IDLE;
uint32_t bLastChange = 0;
bool bOn = false;

void updateBuzzerPattern(uint32_t holdDuration) {
    if (holdDuration < 1000) {
        bState = BUZZER_IDLE;
        pcf.digitalWrite(PIN_BUZZER, LOW);
        return;
    }

    if (holdDuration >= 5000) {
        // Final long beep
        pcf.digitalWrite(PIN_BUZZER, HIGH);
        return;
    }

    // Accelerating pattern between 1s and 5s
    uint32_t elapsed = holdDuration - 1000;
    uint32_t offTime = 1000 - (elapsed * 950 / 4000); // 1000ms down to 50ms
    uint32_t onTime = 100; // Increased to 100ms
    
    uint32_t now = millis();
    if (bOn && (now - bLastChange >= onTime)) {
        bOn = false;
        bLastChange = now;
        pcf.digitalWrite(PIN_BUZZER, LOW);
    } else if (!bOn && (now - bLastChange >= offTime)) {
        bOn = true;
        bLastChange = now;
        pcf.digitalWrite(PIN_BUZZER, HIGH);
    }
}

void systemTask(void *pvParameters) {
  for (;;) {
    button.update();

    // Hold 3s to restart with progressive beep
    uint32_t holdDuration = button.getModeHoldDuration();
    if (holdDuration >= 3000) {
        Serial.println("[SYSTEM]: Restarting from Operational Mode...");
        // Ensure buzzer is on for long beep before restart
        pcf.digitalWrite(PIN_BUZZER, HIGH);
        delay(500); 
        ESP.restart();
    } else if (holdDuration >= 1000) {
        // Progressive acceleration pattern:
        // Fixed ON time: 100ms
        // Variable OFF time: starts at 1000ms (at 1s), decreases to 50ms (at 3s)
        uint32_t elapsed = holdDuration - 1000; // 0 to 2000ms
        // 1000ms to 50ms = 950ms range over 2000ms
        uint32_t offTime = 1000 - (elapsed * 950 / 2000); 
        
        uint32_t onTime = 100;
        uint32_t cycleTime = onTime + offTime;
        
        // Non-blocking timer
        if (millis() % cycleTime < onTime) {
             pcf.digitalWrite(PIN_BUZZER, HIGH);
        } else {
             pcf.digitalWrite(PIN_BUZZER, LOW);
        }
    } else {
        // Only turn off if not held or not in beep range
        pcf.digitalWrite(PIN_BUZZER, LOW);
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

void loop() {
  vTaskDelete(NULL);
}
