#include "display.h"
#include "config.h"
#include "pins.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COL, LCD_ROW);
DisplayManager display;

void DisplayManager::begin() {
  Wire.begin(LCD_SDA, LCD_SCL);
  Wire.beginTransmission(LCD_ADDRESS);
  if (Wire.endTransmission() == 0) {
    lcd.init();
    lcd.backlight();
    initialized = true;
  } else {
    initialized = false;
  }
}

bool DisplayManager::isInitialized() {
  return initialized;
}

void DisplayManager::splash() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GAMELAN NUNGGAL");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
}

void DisplayManager::show(const char *song, const char *status) {
  strncpy(songName, song, 16);
  songName[16] = 0;
  strncpy(statusName, status, 16);
  statusName[16] = 0;
}

void DisplayManager::update() {
  lcd.setCursor(0, 0);
  lcd.print(songName);
  lcd.print("                "); // Clear line
  lcd.setCursor(0, 1);
  lcd.print(statusName);
  lcd.print("                "); // Clear line
}