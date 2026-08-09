#include "player.h"

#include "config.h"
#include "display.h"
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

  Serial.printf("[PLAYER]: Init mode: %s\n", autoMode ? "AUTO (LOOP)" : "MANUAL");
  Serial.printf("[PLAYER]: Solenoid Time init: %d ms\n", solenoidTime);

  if (sdcard.getCount() > 0) {
    load();
  } else {
    display.show("NO FILE", "NO MIDI");
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
  Serial.printf("[PLAYER]: Solenoid Time saved: %d ms\n", solenoidTime);
}

bool Player::load() {
  if (sdcard.getCount() == 0) {
    loaded = false;
    eventQueue.clear();
    return false;
  }

  File file = sdcard.openCurrent();
  if (!file) {
    loaded = false;
    display.show("NO FILE", "LOAD FAIL");
    return false;
  }

  if (!midi.open(file)) {
    loaded = false;
    display.show("NO FILE", "LOAD FAIL");
    return false;
  }

  eventQueue.clear();
  bool ok = midi.parse();
  midi.close();
  loaded = ok;

  if (ok) {
    display.show(sdcard.getCurrentFile(), "LOADED");
    Serial.printf("[PLAYER]: File loaded: %s\n", sdcard.getCurrentFile());
  } else {
    display.show("NO FILE", "LOAD FAIL");
  }

  return ok;
}

void Player::play() {
  if (sdcard.getCount() == 0) {
    display.show("NO FILE", "NO MIDI");
    return;
  }
  if (!loaded && !load()) return;

  Serial.printf("[PLAYER]: %s %s\n", paused ? "Resuming" : "Starting", sdcard.getCurrentFile());

  playing = true;
  if (paused) {
    // Resume: adjust startUS to resume from last position
    startUS = esp_timer_get_time() - elapsedUS;
    paused = false;
  } else {
    elapsedUS = 0;
    startUS = esp_timer_get_time();
    paused = false;
  }

  display.show(sdcard.getCurrentFile(), "PLAYING");
}

void Player::stop() {
  if (!playing && !paused) return;

  playing = false;
  paused = false;
  loaded = false;
  elapsedUS = 0;
  solenoid.allOff();
  eventQueue.clear();
  display.show(sdcard.getCurrentFile(), "STOPPED");
  Serial.println("[PLAYER]: Stopped");
}

void Player::pause() {
  if (!playing) return;

  // Save current position
  elapsedUS += (esp_timer_get_time() - startUS);

  playing = false;
  paused = true;
  solenoid.allOff();
  display.show(sdcard.getCurrentFile(), "PAUSED");
}

bool Player::isPlaying() {
  return playing;
}

bool Player::isPaused() {
  return paused;
}

void Player::nextFile() {
  stop();
  sdcard.next();
  load();
}

void Player::prevFile() {
  stop();
  sdcard.prev();
  load();
}

void Player::toggleMode() {
  autoMode = !autoMode;

  prefs.begin("gamelan", false);
  prefs.putBool("autoMode", autoMode);
  prefs.end();

  Serial.printf("[PLAYER]: Mode set to: %s\n", autoMode ? "AUTO (LOOP)" : "MANUAL");
  display.show(sdcard.getCurrentFile(), autoMode ? "AUTO MODE" : "MANUAL MODE");
}

bool Player::isAutoMode() {
  return autoMode;
}

void Player::handleEvent(ButtonID evt) {
  switch (evt) {
    case BTN_START:
      if (playing) pause();
      else if (paused) play();
      else play();
      break;
    case BTN_NEXT:
      nextFile();
      break;
    case BTN_PREV:
      prevFile();
      break;
    case BTN_MODE:
      toggleMode();
      break;
    default: break;
  }
}

void Player::update() {
  if (!playing) return;
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
      Serial.println("[PLAYER]: Loop: 2s delay");
      display.show(sdcard.getCurrentFile(), "NEXT IN 2S");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      nextFile();
      play();
    } else {
      stop();
      display.show(sdcard.getCurrentFile(), "DONE");
    }
  }
}

