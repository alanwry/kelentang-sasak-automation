#include "sdcard.h"
#include "pins.h"
#include "player.h"
#include "webserver.h"

SDCardManager sdcard;

bool SDCardManager::begin() {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    if (!mutex) return false;

    pinMode(PIN_SD_DET, INPUT_PULLUP);
    sdInserted = (digitalRead(PIN_SD_DET) == LOW);

    if (!sdInserted) {
        detected = false;
        return false;
    }

    SD.end();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, PIN_SD_CS);

    bool ok = false;
    for (int i = 0; i < 3; i++) {
        if ((ok = SD.begin(PIN_SD_CS, SPI, 4000000))) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    detected = ok;
    return ok;
}

void SDCardManager::update() {
    bool currentDetected = (digitalRead(PIN_SD_DET) == LOW);
    if (currentDetected != sdInserted) {
        sdInserted = currentDetected;
        LOG("[SDCARD] Status: %s\n", sdInserted ? "INSERTED" : "REMOVED");
        
        if (sdInserted) {
            detected = begin();
            if (detected) LOG("[SDCARD] Re-initialized\n");
            else LOG("[SDCARD] Re-init failed\n");
        } else {
            player.stop();
            detected = false;
        }
    }
}

void SDCardManager::scan() {
    if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return;
    
    totalFiles = 0;
    File root = SD.open("/");
    if (root) {
        while (File entry = root.openNextFile()) {
            if (!entry.isDirectory()) {
                String name = entry.name();
                name.toLowerCase();
                if (name.endsWith(".mid") || name.endsWith(".midi")) {
                    if (totalFiles < MAX_FILES) {
                        snprintf(filenames[totalFiles], MAX_FILENAME, "/%s", entry.name());
                        totalFiles++;
                    }
                }
            }
            entry.close();
        }
        root.close();
    }
    currentIndex = 0;
    xSemaphoreGive(mutex);
}

bool SDCardManager::next() {
    if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return false;
    if (totalFiles > 0) currentIndex = (currentIndex + 1) % totalFiles;
    xSemaphoreGive(mutex);
    return true;
}

bool SDCardManager::prev() {
    if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return false;
    if (totalFiles > 0) currentIndex = (currentIndex - 1 + totalFiles) % totalFiles;
    xSemaphoreGive(mutex);
    return true;
}

const char *SDCardManager::getCurrentFile() {
    static char currentFile[MAX_FILENAME];
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
        if (totalFiles == 0) currentFile[0] = '\0';
        else strncpy(currentFile, filenames[currentIndex], MAX_FILENAME);
        xSemaphoreGive(mutex);
    }
    return currentFile;
}

File SDCardManager::openCurrent() {
    File file;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
        if (totalFiles > 0) file = SD.open(filenames[currentIndex]);
        xSemaphoreGive(mutex);
    }
    return file;
}

File SDCardManager::openFile(const char *path, const char *mode) {
    if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return File();
    File file = SD.open(path, mode);
    xSemaphoreGive(mutex);
    return file;
}

bool SDCardManager::deleteFile(const char *path) {
    if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return false;
    bool ok = SD.remove(path);
    xSemaphoreGive(mutex);
    return ok;
}
