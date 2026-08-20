#ifndef PLAYER_H
#define PLAYER_H

#include "button.h"

class Player {
public:
  void begin();
  bool load();
  void play();
  void stop();
  void pause();
  bool isPlaying();
  bool isPaused();
  uint64_t getDurationUS();
  uint64_t getElapsedUS();

  void handleEvent(ButtonID evt);
 // Diganti dari update()
  void update();
  void nextFile();
  void prevFile();

  // Fitur baru
  void toggleMode();
  bool isAutoMode();
  uint16_t getSolenoidTime();
  void setSolenoidTime(uint16_t time);
  void setTotalDurationUS(uint64_t duration);
  bool hasLoadingError() { return loadingError; }

private:
  bool playing = false;
  bool paused = false;
  bool loaded = false;
  bool autoMode = false;  // Mode otomatis (loop)
  bool loadingError = false; // NEW: Flag error
  uint16_t solenoidTime = 20;
  uint64_t startUS = 0;
  uint64_t elapsedUS = 0;
  uint64_t totalDurationUS = 0; // NEW
};

extern Player player;

#endif