#include "player.h"

#include "config.h"
#include "event_queue.h"
#include "midi.h"
#include "sdcard.h"
#include "solenoid.h"
#include "esp_timer.h"
#include <Preferences.h>

Player player;
Preferences prefs;

void Player::begin() {
  prefs.begin("gamelan", false);
  autoMode = prefs.getBool("autoMode", false);
  solenoidTime = prefs.getUShort("solenoidTime", 20);
  prefs.end();

  loaded = false;
  playing = false;
  paused = false;
  startUS = 0;
  elapsedUS = 0;
  eventQueue.clear();

  if (sdcard.getCount() > 0) {
    load();
  }
}

uint16_t Player::getSolenoidTime() {
  return solenoidTime;
}

void Player::setSolenoidTime(uint16_t time) {
  solenoidTime = time;
  prefs.begin("gamelan", false);
  prefs.putUShort("solenoidTime", time);
  prefs.end();
}

bool Player::load() {
  if (sdcard.getCount() == 0) {
    loaded = false;
    eventQueue.clear();
    loadingError = true; // Error
    Serial.println("[PLAYER] Load failed: No files on SD");
    return false;
  }

  File file = sdcard.openCurrent();
  if (!file) {
    loaded = false;
    loadingError = true; // Error
    Serial.println("[PLAYER] Load failed: Could not open file");
    return false;
  }

  if (!midi.open(file)) {
    loaded = false;
    loadingError = true; // Error
    Serial.println("[PLAYER] Load failed: MIDI open error");
    return false;
  }

  eventQueue.clear();
  bool ok = midi.parse();
  midi.close();
  loaded = ok;
  loadingError = !ok; // Set error jika tidak ok
  
  if (ok) {
      Serial.printf("[PLAYER] File loaded: %s\n", sdcard.getCurrentFile());
  } else {
      Serial.println("[PLAYER] Load failed: MIDI parse error");
  }

  return ok;
}
void Player::play() {
  if (sdcard.getCount() == 0) return;
  if (!loaded && !load()) return;

  uint64_t now = esp_timer_get_time();

  if (paused) {
    startUS = now; 
    paused = false;
    playing = true;
  } else if (!playing) {
    elapsedUS = 0;
    startUS = now;
    playing = true;
    paused = false;
  }
}

void Player::pause() {
  if (!playing) return;

  uint64_t now = esp_timer_get_time();

  elapsedUS += (now - startUS);

  playing = false;
  paused = true;
  solenoid.allOff();
}

void Player::nextFile() {
  stop();
  sdcard.next();
  load();
  Serial.printf("[PLAYER] Next file: %s\n", sdcard.getCurrentFile());
}

void Player::prevFile() {
  stop();
  sdcard.prev();
  load();
  Serial.printf("[PLAYER] Prev file: %s\n", sdcard.getCurrentFile());
}

void Player::toggleMode() {
  autoMode = !autoMode;
  Serial.printf("[PLAYER] Mode changed to: %s\n", autoMode ? "AUTO (LOOP)" : "MANUAL");

  prefs.begin("gamelan", false);
  prefs.putBool("autoMode", autoMode);
  prefs.end();
}

void Player::stop() {
  if (!playing && !paused) return;

  playing = false;
  paused = false;
  loaded = false;
  elapsedUS = 0;
  solenoid.allOff();
  eventQueue.clear();
  Serial.println("[PLAYER] Stopped");
}

bool Player::isAutoMode() {
  return autoMode;
}

bool Player::isPlaying() {
  return playing;
}

bool Player::isPaused() {
  return paused;
}

void Player::setTotalDurationUS(uint64_t duration) {
    totalDurationUS = duration;
}

uint64_t Player::getDurationUS() {
    return totalDurationUS;
}

uint64_t Player::getElapsedUS() {
    if (playing) return elapsedUS + (esp_timer_get_time() - startUS);
    return elapsedUS;
}

void Player::handleEvent(ButtonID evt) {
  switch (evt) {
    case BTN_START:
      Serial.println("[BUTTON] START/PAUSE pressed");
      if (playing) pause();
      else if (paused) play();
      else play();
      break;
    case BTN_NEXT:
      Serial.println("[BUTTON] NEXT pressed");
      nextFile();
      break;
    case BTN_PREV:
      Serial.println("[BUTTON] PREV pressed");
      prevFile();
      break;
    case BTN_MODE:
      Serial.println("[BUTTON] MODE pressed");
      toggleMode();
      break;
    default: break;
  }
}

void Player::update() {
  if (!playing) return;
  // Serial.printf("[PLAYER] Update loop: elapsed=%llu\n", elapsedUS + (esp_timer_get_time() - startUS));
  
  if (sdcard.getCount() == 0) {
    stop();
    return;
  }

  // Calculate elapsed time (stored elapsed + running time)
  uint64_t elapsed = elapsedUS + (esp_timer_get_time() - startUS);
  MidiEvent evtData;

  while (eventQueue.peek(evtData)) {
    if (evtData.timeUS > elapsed) break;
    eventQueue.pop(evtData);
    if (evtData.type == EVENT_NOTE_ON) {
      solenoid.hit(evtData.solenoidId, player.getSolenoidTime());
    }
  }

  if (eventQueue.empty()) {
    if (autoMode) {
      Serial.println("[PLAYER] File finished, auto-playing next");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      nextFile();
      play();
    } else {
      Serial.println("[PLAYER] File finished, stopped");
      stop();
    }
  }
}
