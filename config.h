#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FW_VERSION "3.0.0"

#define MAX_SOLENOID 24

#define PCF8574_ADDRESS 0x21

#define BUTTON_DEBOUNCE 40
#define WIFI_ENABLE_MS 2000
#define WIFI_DISABLE_MS 5000

#define WIFI_SSID "ESP32"
#define WIFI_PASSWORD "admin123"

#define MAX_FILENAME 128

#define MAX_FILES 100

#endif