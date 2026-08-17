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
  void handleEvent(ButtonID evt); // Diganti dari update()
  void update();
  void nextFile();
  void prevFile();

  // Fitur baru
  void toggleMode();
  bool isAutoMode();
  uint16_t getSolenoidTime();
  void setSolenoidTime(uint16_t time);
  bool hasLoadingError() { return loadingError; }

private:
  bool playing = false;
  bool paused = false;
  bool loaded = false;
  bool autoMode = false;  // Mode otomatis (loop)
  bool loadingError = false; // NEW: Flag error
  uint16_t solenoidTime = 20;
  uint64_t startUS = 0;
  uint64_t elapsedUS = 0;  // NEW: Menyimpan posisi waktu saat pause
};

extern Player player;

#endif