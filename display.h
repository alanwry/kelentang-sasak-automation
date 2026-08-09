#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

class DisplayManager {
public:
  void begin();
  bool isInitialized();
  void splash();
  void show(const char *song, const char *status);
  void update();
private:
  char songName[17];
  char statusName[17];
  bool initialized = false;
};

extern DisplayManager display;

#endif
