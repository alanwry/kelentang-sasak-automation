#include "motor.h"
#include <SD.h>
#include "config.h"
#include "webserver.h"

// Konfigurasi PWM
#define PWM_FREQ 5000
#define PWM_RES 8

static uint8_t nextPwmChannel = 0;

void Motor::begin(uint8_t gpio, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled) {
  pin = gpio;
  this->note = note;
  this->midiNote = midiNote;
  this->midiChannel = midiChannel;
  this->enabled = enabled;
  this->pwmChannel = nextPwmChannel++;
  
  ledcAttachChannel(pin, PWM_FREQ, PWM_RES, pwmChannel);
  ledcWrite(pin, 0);
}

void Motor::setSpeed(uint8_t velocity) {
  if (!enabled) return;
  // Velocity MIDI 0-127, PWM 0-255
  uint32_t duty = map(velocity, 0, 127, 0, 255);
  ledcWrite(pin, duty);
}

void Motor::update() {
  // Motor DC tidak butuh update periodik kecuali ada logika timeout
}

void Motor::stop() {
  ledcWrite(pin, 0);
}

uint8_t Motor::getPin() { return pin; }
String Motor::getNote() { return note; }
uint8_t Motor::getMidiNote() { return midiNote; }
uint8_t Motor::getMidiChannel() { return midiChannel; }

MotorManager motor;

void MotorManager::begin() {
  count = 0;
  loadConfig();
}

void MotorManager::test(uint8_t pin) {
  uint32_t now = millis();
  if (now - lastTestTime < 1000) return;
  for (uint8_t i = 0; i < count; i++) {
    if (item[i].getPin() == pin) {
      item[i].setSpeed(127); // Full speed test
      vTaskDelay(500 / portTICK_PERIOD_MS);
      item[i].stop();
      lastTestTime = now;
      break;
    }
  }
}

bool MotorManager::loadConfig() {
  count = 0;
  File file = SD.open("/solenoids.txt", FILE_READ);
  if (!file) return false;

  while (file.available() && count < MAX_SOLENOID) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    int comma3 = line.indexOf(',', comma2 + 1);
    int comma4 = line.indexOf(',', comma3 + 1);

    if (comma1 > 0 && comma2 > comma1) {
      uint8_t p = line.substring(0, comma1).toInt();
      String n = line.substring(comma1 + 1, comma2);
      uint8_t m = line.substring(comma2 + 1, comma3 > 0 ? comma3 : line.length()).toInt();
      uint8_t ch = (comma3 > 0) ? (comma4 > 0 ? line.substring(comma3 + 1, comma4) : line.substring(comma3 + 1)).toInt() : 0;
      bool en = (comma4 > 0) ? (line.substring(comma4 + 1).toInt() == 1) : true;
      addMotor(p, n, m, ch, en);
    }
  }
  file.close();
  return true;
}

bool MotorManager::saveConfig() {
  File file = SD.open("/solenoids.txt", FILE_WRITE);
  if (!file) return false;

  for (uint8_t i = 0; i < count; i++) {
    file.print(item[i].getPin());
    file.print(",");
    file.print(item[i].getNote());
    file.print(",");
    file.print(item[i].getMidiNote());
    file.print(",");
    file.print(item[i].getMidiChannel());
    file.print(",");
    file.println(item[i].isEnabled() ? 1 : 0);
  }
  file.close();
  return true;
}

void MotorManager::addMotor(uint8_t pin, String note, uint8_t midiNote, uint8_t midiChannel, bool enabled) {
  if (count < MAX_SOLENOID) {
    item[count++].begin(pin, note, midiNote, midiChannel, enabled);
  }
}

void MotorManager::removeMotor(uint8_t pin) {
  // Implementasi penghapusan motor
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

void MotorManager::update() {}

void MotorManager::setSpeed(uint8_t id, uint8_t velocity) {
  if (id < count && item[id].isEnabled()) {
    item[id].setSpeed(velocity);
  }
}

void MotorManager::allStop() {
  for (uint8_t i = 0; i < count; i++) {
    item[i].stop();
  }
}

uint8_t MotorManager::getCount() const { return count; }
Motor* MotorManager::getItems() { return (Motor*)item; }