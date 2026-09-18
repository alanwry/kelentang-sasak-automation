#ifndef SDCARD_H
#define SDCARD_H

#include "config.h"
#include <Arduino.h>
#include <SD.h>
#include <freertos/semphr.h>

class SDCardManager {
public:
    bool begin();
    void update();
    void scan();

    bool isDetected() const { return detected; }
    uint16_t getCount() const { return totalFiles; }
    const char *getCurrentFile();
    
    File openCurrent();
    File openFile(const char *path, const char *mode);
    bool deleteFile(const char *path);
    
    bool next();
    bool prev();

private:
    char filenames[MAX_FILES][MAX_FILENAME];
    uint16_t totalFiles = 0;
    int16_t currentIndex = 0;
    SemaphoreHandle_t mutex = nullptr;
    bool detected = false;
    bool sdInserted = true;
};

extern SDCardManager sdcard;

#endif
