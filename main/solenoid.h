#ifndef SOLENOID_H
#define SOLENOID_H

#include "config.h"
#include <Arduino.h>

// Solenoid individual - kontrol satu solenoid
class Solenoid {
public:
  void begin(uint8_t gpio, String note, uint8_t midiNote, uint8_t midiChannel);
  void hit(uint16_t duration = 20);
  void update();
  void off();
  uint8_t getPin();
  String getNote();
  uint8_t getMidiNote();
  uint8_t getMidiChannel();

private:
  uint8_t pin;
  String note;
  uint8_t midiNote;
  uint8_t midiChannel; // NEW
  bool active;
  uint64_t offTime;
};

// Solenoid Manager - kontrol solenoid
class SolenoidManager {
public:
  void begin();
  void update();
  void hit(uint8_t id, uint16_t duration = 20);
  void test(uint8_t pin); // Tambahkan ini
  void allOff();

  // Dynamic management
  bool loadConfig();
  bool saveConfig();
  void addSolenoid(uint8_t pin, String note, uint8_t midiNote, uint8_t midiChannel);
  void removeSolenoid(uint8_t pin);
  uint8_t getCount() const;
  Solenoid *getItems();
  bool hasConfigError() { return configError; }

private:
  Solenoid item[MAX_SOLENOID];
  uint8_t count;
  uint32_t lastTestTime = 0; // Tambahkan ini
  bool configError = false; // NEW
};

extern SolenoidManager solenoid;

#endif
