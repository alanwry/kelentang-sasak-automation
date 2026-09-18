#include "solenoid.h"
#include <SD.h>
#include "config.h"
#include "player.h"
#include "webserver.h"

void Solenoid::begin(uint8_t gpio, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled) {
    this->pin = gpio;
    this->note = note;
    this->midiNote = midiNote;
    this->midiChannel = midiChannel;
    this->enabled = enabled;
    this->active = false;
    this->offTime = 0;
    
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

void Solenoid::hit(uint16_t duration) {
    if (!enabled) return;
#if DEBUG_SOLENOID
    if (!active) LOG("[SOLENOID]: %d ON\n", pin);
#endif
    digitalWrite(pin, HIGH);
    active = true;
    offTime = esp_timer_get_time() + (duration * 1000ULL);
}

void Solenoid::update() {
    if (active && esp_timer_get_time() >= offTime) {
        digitalWrite(pin, LOW);
        active = false;
#if DEBUG_SOLENOID
        LOG("[SOLENOID]: %d OFF\n", pin);
#endif
    }
}

void Solenoid::off() {
    digitalWrite(pin, LOW);
    active = false;
}

SolenoidManager solenoid;

void SolenoidManager::begin() {
    count = 0;
    loadConfig();
}

void SolenoidManager::test(uint8_t pin) {
    uint32_t now = millis();
    if (now - lastTestTime < 1000) return;

    for (uint8_t i = 0; i < count; i++) {
        if (item[i].getPin() == pin) {
            item[i].hit(player.getSolenoidTime());
            lastTestTime = now;
            break;
        }
    }
}

bool SolenoidManager::loadConfig() {
    count = 0;
    File file = SD.open("/solenoids.txt", FILE_READ);
    if (!file) return false;

    while (file.available() && count < MAX_SOLENOID) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1), c4 = line.indexOf(',', c3 + 1);

        if (c1 > 0 && c2 > c1) {
            uint8_t p = line.substring(0, c1).toInt();
            String n = line.substring(c1 + 1, c2);
            uint8_t m = line.substring(c2 + 1, c3 > 0 ? c3 : line.length()).toInt();
            uint8_t ch = (c3 > 0) ? (c4 > 0 ? line.substring(c3 + 1, c4) : line.substring(c3 + 1)).toInt() : 0;
            bool en = (c4 > 0) ? (line.substring(c4 + 1).toInt() == 1) : true;
            addSolenoid(p, n, m, ch, en);
        }
    }
    file.close();
    return true;
}

bool SolenoidManager::saveConfig() {
    File file = SD.open("/solenoids.txt", FILE_WRITE);
    if (!file) return false;

    for (uint8_t i = 0; i < count; i++) {
        file.printf("%d,%s,%d,%d,%d\n", item[i].getPin(), item[i].getNote().c_str(),
                    item[i].getMidiNote(), item[i].getMidiChannel(), item[i].isEnabled() ? 1 : 0);
    }
    file.close();
    LOG("[SOLENOID]: Saved %d configs\n", count);
    return true;
}

void SolenoidManager::addSolenoid(uint8_t pin, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled) {
    if (count < MAX_SOLENOID) item[count++].begin(pin, note, midiNote, midiChannel, enabled);
}

void SolenoidManager::removeSolenoid(uint8_t pin) {
    for (uint8_t i = 0; i < count; i++) {
        if (item[i].getPin() == pin) {
            for (uint8_t j = i; j < count - 1; j++) item[j] = item[j + 1];
            count--;
            break;
        }
    }
}

void SolenoidManager::update() {
    for (uint8_t i = 0; i < count; i++) item[i].update();
}

void SolenoidManager::hit(uint8_t id, uint16_t duration) {
    if (id < count) item[id].hit(duration);
}

void SolenoidManager::allOff() {
    for (uint8_t i = 0; i < count; i++) item[i].off();
}
