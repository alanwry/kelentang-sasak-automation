#include "solenoid.h"
#include <SD.h>
#include "config.h"
#include "esp_timer.h"
#include "player.h"

void Solenoid::begin(uint8_t gpio, String note, uint8_t midiNote) {
  pin = gpio;
  this->note = note;
  this->midiNote = midiNote;
  active = false;
  offTime = 0;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void Solenoid::hit(uint16_t duration) {
  digitalWrite(pin, HIGH);
  active = true;
  offTime = esp_timer_get_time() + (duration * 1000ULL);
}

void Solenoid::update() {
  if (active) {
    uint64_t now = esp_timer_get_time();
    if (now >= offTime) {
      digitalWrite(pin, LOW);
      active = false;
    }
  }
}

void Solenoid::off() {
  digitalWrite(pin, LOW);
  active = false;
}

uint8_t Solenoid::getPin() {
  return pin;
}
String Solenoid::getNote() {
  return note;
}
uint8_t Solenoid::getMidiNote() {
  return midiNote;
}

SolenoidManager solenoid;

void SolenoidManager::begin() {
  count = 0;
  if (!loadConfig()) {
    Serial.println("[SOLENOID] No config file, using defaults");
  }
}

void SolenoidManager::test(uint8_t pin) {
  uint32_t now = millis();
  if (now - lastTestTime < 1000) return; // Debounce 1 second
  
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
    if (line.length() == 0) continue;

    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);

    if (comma1 > 0 && comma2 > comma1) {
      uint8_t p = line.substring(0, comma1).toInt();
      String n = line.substring(comma1 + 1, comma2);
      uint8_t m = line.substring(comma2 + 1).toInt();
      addSolenoid(p, n, m);
    }
  }
  file.close();
  return true;
}

bool SolenoidManager::saveConfig() {
  File file = SD.open("/solenoids.txt", FILE_WRITE);
  if (!file) {
    Serial.println("[SOLENOID]: Error: Failed to save config");
    return false;
  }

  for (uint8_t i = 0; i < count; i++) {
    file.print(item[i].getPin());
    file.print(",");
    file.print(item[i].getNote());
    file.print(",");
    file.println(item[i].getMidiNote());
    Serial.printf("[SOLENOID]: Saved - Pin: %d, Note: %s, MIDI: %d\n", item[i].getPin(), item[i].getNote().c_str(), item[i].getMidiNote());
  }

  file.flush();
  file.close();
  return true;
}

void SolenoidManager::addSolenoid(uint8_t pin, String note, uint8_t midiNote) {
  if (count < MAX_SOLENOID) {
    item[count++].begin(pin, note, midiNote);
  }
}

void SolenoidManager::removeSolenoid(uint8_t pin) {
  for (uint8_t i = 0; i < count; i++) {
    if (item[i].getPin() == pin) {
      for (uint8_t j = i; j < count - 1; j++) {
        item[j] = item[j + 1];
      }
      count--;
      break;
    }
  }
}

void SolenoidManager::update() {
  for (uint8_t i = 0; i < count; i++) {
    item[i].update();
  }
}

void SolenoidManager::hit(uint8_t id, uint16_t duration) {
  if (id < count) {
    item[id].hit(duration);
  }
}

void SolenoidManager::allOff() {
  for (uint8_t i = 0; i < count; i++) {
    item[i].off();
  }
}

uint8_t SolenoidManager::getCount() const {
  return count;
}
Solenoid* SolenoidManager::getItems() {
  return (Solenoid*)item;
}

