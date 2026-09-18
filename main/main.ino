#include "config.h"
#include "pins.h"
#include "button.h"
#include "led.h"
#include "midi.h"
#include "player.h"
#include "sdcard.h"
#include "solenoid.h"
#include "webserver.h"
#include "wifi_manager.h"

#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// =============================================================================
// GLOBAL OBJECTS & HANDLERS
// =============================================================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

extern void triggerBuzzer(uint16_t duration);

QueueHandle_t buttonQueue;
volatile uint32_t lastMidiTask = 0;
volatile uint32_t lastSystemTask = 0;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

void updateOLED() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x13_tf);

    if (WiFi.getMode() == WIFI_AP) {
        u8g2.drawStr(0, 36, ("SSID: " + String(WIFI_SSID)).c_str());
        u8g2.drawStr(0, 49, ("PASS: " + String(WIFI_PASSWORD)).c_str());
        u8g2.drawStr(0, 62, "IP  : 192.11.11.21");
    } else if (WiFi.getMode() == WIFI_STA) {
        u8g2.drawStr(0, 36, "Open The Dashboard");
        u8g2.drawStr(0, 49, "http://mydashboard");
        u8g2.drawStr(0, 62, ("IP  : " + WiFi.localIP().toString()).c_str());
    } else {
        u8g2.drawStr(0, 49, "WiFi Offline");
    }
    u8g2.sendBuffer();
}

bool isSystemHang() {
    uint32_t now = millis();
    return (now - lastMidiTask > 5000) || (now - lastSystemTask > 5000);
}

// =============================================================================
// FREERTOS TASKS
// =============================================================================

void midiTask(void *pvParameters) {
    for (;;) {
        lastMidiTask = millis();
        
        ButtonID evt;
        if (xQueueReceive(buttonQueue, &evt, 0) == pdPASS) {
            player.handleEvent(evt);
        }
        
        player.update();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void systemTask(void *pvParameters) {
    for (;;) {
        lastSystemTask = millis();
        
        // Update System Components
        button.update();
        sdcard.update();
        updateOLED();
        
        // Handle Button Events
        ButtonID evt = button.getEvent();
        if (evt != BTN_NONE) {
            xQueueSend(buttonQueue, &evt, 0);
        }
        
        // Update Management Components
        webServer.update();
        wifiManager.update();
        solenoid.update();
        led.update();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =============================================================================
// MAIN SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(2000));

    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Initialize Display
    u8g2.setI2CAddress(0x3C * 2);
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.sendBuffer();

    // Initialize System Modules
    button.begin();
    wifiManager.begin();

    if (wifiManager.isSTAEnabled()) {
        wifiManager.startSTAOnly();
    } else {
        wifiManager.startAPMinimal();
    }

    led.begin();
    
    if (sdcard.begin()) {
        LOG("[SYSTEM] SD Card initialized\n");
    } else {
        LOG("[SYSTEM] SD Card failed\n");
    }

    solenoid.begin();
    sdcard.scan();
    player.begin();
    
    LOG("[SYSTEM] Play Mode: %s\n", player.isAutoMode() ? "Continuous" : "PlayOnce");
    LOG("[SYSTEM] Actuator Duration: %d ms\n", player.getSolenoidTime());

    // Startup Feedback
    triggerBuzzer(400);

    // Initialize Multitasking
    buttonQueue = xQueueCreate(10, sizeof(ButtonID));
    xTaskCreatePinnedToCore(midiTask, "midiTask", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(systemTask, "systemTask", 8192, NULL, 1, NULL, 0);
}

void loop() {
    // Loop is not used in FreeRTOS implementation
    vTaskDelete(NULL);
}
