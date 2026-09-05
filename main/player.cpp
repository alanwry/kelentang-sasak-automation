#include "player.h"

#include "config.h"
#include "event_queue.h"
#include "midi.h"
#include "sdcard.h"
#include "solenoid.h"
#include "webserver.h"
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
  LOG("[PLAYER] Solenoid active duration updated: %d ms\n", time);
}

bool Player::load() {
  if (sdcard.getCount() == 0) {
    loaded = false;
    eventQueue.clear();
    loadingError = true;
    LOG("[PLAYER] Load failed: No files on SD\n");
    return false;
  }

  File file = sdcard.openCurrent();
  if (!file) {
    loaded = false;
    loadingError = true;
    LOG("[PLAYER] Load failed: Could not open file\n");
    return false;
  }

  if (!midi.open(file)) {
    loaded = false;
    loadingError = true;
    LOG("[PLAYER] Load failed: MIDI open error\n");
    return false;
  }

  eventQueue.clear();
  bool ok = midi.parse();
  midi.close();
  loaded = ok;
  loadingError = !ok;

  if (ok) {
    LOG("[PLAYER] File loaded: %s\n", sdcard.getCurrentFile());
  } else {
    LOG("[PLAYER] Load failed: MIDI parse error\n");
  }

  return ok;
}

void Player::play() {
  if (sdcard.getCount() == 0) {
    LOG("[PLAYER] Play failed: SD Card empty\n");
    return;
  }
  if (!loaded && !load()) return;

  uint64_t now = esp_timer_get_time();

  if (paused) {
    startUS = now;
    paused = false;
    playing = true;
    LOG("[PLAYER] Resumed: %s\n", sdcard.getCurrentFile());
  } else if (!playing) {
    elapsedUS = 0;
    startUS = now;
    playing = true;
    paused = false;
    LOG("[PLAYER] Playing: %s\n", sdcard.getCurrentFile());
  }
}

void Player::pause() {
  if (!playing) return;

  uint64_t now = esp_timer_get_time();

  elapsedUS += (now - startUS);

  playing = false;
  paused = true;
  solenoid.allOff();
  LOG("[PLAYER] Paused: %s\n", sdcard.getCurrentFile());
}

void Player::nextFile() {
  stop();
  sdcard.next();
  load();
  LOG("[PLAYER] Next file: %s\n", sdcard.getCurrentFile());
}

void Player::prevFile() {
  stop();
  sdcard.prev();
  load();
  LOG("[PLAYER] Prev file: %s\n", sdcard.getCurrentFile());
}

void Player::toggleMode() {
  autoMode = !autoMode;
  LOG("[PLAYER] Mode changed to: %s\n", autoMode ? "Continuous" : "PlayOnce");

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
  LOG("[PLAYER] Stopped\n");
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
      LOG("[BUTTON] START/PAUSE pressed\n");
      if (playing) pause();
      else if (paused) play();
      else play();
      break;
    case BTN_NEXT:
      LOG("[BUTTON] NEXT pressed\n");
      nextFile();
      break;
    case BTN_PREV:
      LOG("[BUTTON] PREV pressed\n");
      prevFile();
      break;
    case BTN_MODE:
      LOG("[BUTTON] MODE pressed\n");
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
      LOG("[PLAYER] File finished, auto-playing next\n");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      nextFile();
      play();
    } else {
      LOG("[PLAYER] File finished, stopped\n");
      stop();
    }
  }
}