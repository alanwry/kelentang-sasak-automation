#ifndef SOLENOID_H
#define SOLENOID_H

#include "config.h"
#include <Arduino.h>

class Solenoid {
public:
    void begin(uint8_t gpio, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled = true);
    void hit(uint16_t duration = 20);
    void update();
    void off();

    uint8_t getPin() const { return pin; }
    String getNote() const { return note; }
    uint8_t getMidiNote() const { return midiNote; }
    uint8_t getMidiChannel() const { return midiChannel; }
    bool isEnabled() const { return enabled; }
    void setEnabled(bool e) { enabled = e; }

private:
    uint8_t pin;
    String note;
    uint8_t midiNote;
    uint8_t midiChannel;
    bool enabled;
    bool active;
    uint64_t offTime;
};

class SolenoidManager {
public:
    void begin();
    void update();
    void hit(uint8_t id, uint16_t duration = 20);
    void test(uint8_t pin);
    void allOff();

    bool loadConfig();
    bool saveConfig();
    void addSolenoid(uint8_t pin, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled = true);
    void removeSolenoid(uint8_t pin);
    
    uint8_t getCount() const { return count; }
    Solenoid *getItems() { return item; }
    bool hasConfigError() const { return configError; }

private:
    Solenoid item[MAX_SOLENOID];
    uint8_t count = 0;
    uint32_t lastTestTime = 0;
    bool configError = false;
};

extern SolenoidManager solenoid;

#endif
