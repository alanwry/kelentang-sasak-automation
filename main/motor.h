#ifndef MOTOR_H
#define MOTOR_H

#include "config.h"
#include <Arduino.h>

class Motor {
public:
  void begin(uint8_t gpio, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled = true);
  void setSpeed(uint8_t velocity); // Ganti hit dengan setSpeed
  void update();
  void stop();
  uint8_t getPin();
  String getNote();
  uint8_t getMidiNote();
  uint8_t getMidiChannel();
  bool isEnabled() { return enabled; }
  void setEnabled(bool e) { enabled = e; }

private:
  uint8_t pin;
  String note;
  uint8_t midiNote;
  uint8_t midiChannel;
  bool enabled;
  uint8_t pwmChannel; // Channel LEDC
};

class MotorManager {
public:
  void begin();
  void update();
  void setSpeed(uint8_t id, uint8_t velocity); 
  void test(uint8_t pin);
  void allStop();

  bool loadConfig();
  bool saveConfig();
  void addMotor(uint8_t pin, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled = true);
  void removeMotor(uint8_t pin);
  uint8_t getCount() const;
  Motor *getItems();
  bool hasConfigError() { return configError; }

private:
  Motor item[MAX_SOLENOID];
  uint8_t count;
  uint32_t lastTestTime = 0;
  bool configError = false;
};

extern MotorManager motor;

#endif
