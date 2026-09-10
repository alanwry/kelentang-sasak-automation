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
#include <U8g2lib.h>

// Inisialisasi U8g2 HW I2C (Mode Full Buffer)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

extern void triggerBuzzer(uint16_t duration);

QueueHandle_t buttonQueue;
SemaphoreHandle_t wifiSemaphore;

void updateOLED() {
  static bool oledInitialized = true;  // Anggap berhasil dulu
  // Jika display tidak diinisialisasi, tambahkan flag atau cek status
  // Namun untuk saat ini, kita tambahkan pengecekan sederhana:

  u8g2.clearBuffer();
  // Font 7x14 pas untuk IP Address 15 karakter (15 * 7 = 105px dari total 128px)
  u8g2.setFont(u8g2_font_7x13_tf);

  if (WiFi.getMode() == WIFI_AP) {
    String ssidStr = String("SSID: ") + WIFI_SSID;
    String passStr = String("PASS: ") + WIFI_PASSWORD;
    u8g2.drawStr(0, 36, ssidStr.c_str());
    u8g2.drawStr(0, 49, passStr.c_str());
    u8g2.drawStr(0, 62, "IP  : 192.11.11.21");
  } else if (WiFi.getMode() == WIFI_STA) {
    u8g2.drawStr(0, 36, "Open The Dashboard");
    u8g2.drawStr(0, 49, "http://mydashboard");
    String ipStr = "IP " + WiFi.localIP().toString();
    u8g2.drawStr(0, 62, ipStr.c_str());
  } else {
    u8g2.drawStr(0, 49, "WiFi Offline");
  }
  u8g2.sendBuffer();
}

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
  // Inisialisasi OLED U8g2
  u8g2.setI2CAddress(0x3C * 2);  // Set alamat I2C OLED (0x3C)
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.sendBuffer();

  button.begin();
  wifiManager.begin();

  // Mode STA atau AP secara otomatis berdasarkan setting
  if (wifiManager.isSTAEnabled()) {
    wifiManager.startSTAOnly();
  } else {
    wifiManager.startAPMinimal();
  }

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

void systemTask(void *pvParameters) {
  for (;;) {
    lastSystemTask = millis();
    button.update();
    sdcard.update();
    updateOLED();

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