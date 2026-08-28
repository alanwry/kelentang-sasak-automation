#ifndef PINS_H
#define PINS_H

#include "config.h"

#define I2C_SDA 8
#define I2C_SCL 9

#define PIN_SD_CS 14
#define SD_MOSI 13
#define SD_MISO 11
#define SD_SCK 12
#define PIN_SD_DET 10

#define PIN_PREV 0
#define PIN_PLAY_PAUSE 1
#define PIN_NEXT 2
#define PIN_MODE 3

#define PIN_BUZZER 4
#define PIN_LED_NET 5
#define PIN_LED_RUN 6
#define PIN_LED_ERR 7

inline constexpr uint8_t ALLOWED_PINS[] = {1, 2, 3, 4, 5, 6, 7, 15, 16, 17, 18, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 47, 48};

#endif