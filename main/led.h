#ifndef LED_H
#define LED_H

#include <Arduino.h>

class LedController {
public:
  void begin();
  void update();
};

extern LedController led;
#endif
