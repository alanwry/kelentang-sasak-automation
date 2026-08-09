#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FW_VERSION "3.0.0"

#define MAX_SOLENOID 24

#define LCD_COL 16
#define LCD_ROW 2

#define LCD_ADDRESS 0x27
#define PCF8574_ADDRESS 0x20

#define BUTTON_DEBOUNCE 40
#define WIFI_ENABLE_MS 2000
#define WIFI_DISABLE_MS 5000

#define WIFI_SSID "ESP32 PORTAL"
#define WIFI_PASSWORD ""

#define MAX_FILENAME 128

#define MAX_FILES 100

#endif