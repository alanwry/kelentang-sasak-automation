#ifndef PLAYER_H
#define PLAYER_H

#include "button.h"
#include <Arduino.h>

class Player {
public:
    void begin();
    bool load();
    void play();
    void stop();
    void pause();
    void toggleMode();
    void handleEvent(ButtonID evt);
    void update();
    void nextFile();
    void prevFile();

    bool isPlaying() const { return playing; }
    bool isPaused() const { return paused; }
    bool isAutoMode() const { return autoMode; }
    bool hasLoadingError() const { return loadingError; }

    uint64_t getDurationUS() const { return totalDurationUS; }
    uint64_t getElapsedUS();
    uint16_t getSolenoidTime() const { return solenoidTime; }

    void setSolenoidTime(uint16_t time);
    void setTotalDurationUS(uint64_t duration) { totalDurationUS = duration; }

private:
    bool playing = false;
    bool paused = false;
    bool loaded = false;
    bool autoMode = false;
    bool loadingError = false;

    uint16_t solenoidTime = 20;
    uint64_t startUS = 0;
    uint64_t elapsedUS = 0;
    uint64_t totalDurationUS = 0;
};

extern Player player;

#endif
