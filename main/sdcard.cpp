#include <Arduino.h>
#include "sdcard.h"
#include "pins.h"
#include "player.h"
#include "webserver.h"

SDCardManager sdcard;
static bool sdInserted = true;

bool SDCardManager::begin() {
  if (!mutex) {
    mutex = xSemaphoreCreateMutex();
    if (!mutex)
      return false;
  }

  pinMode(PIN_SD_DET, INPUT_PULLUP);
  sdInserted = (digitalRead(PIN_SD_DET) == LOW);

  if (!sdInserted) {
    detected = false;
    return false;
  }

  SD.end();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, PIN_SD_CS);

  bool ok = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    ok = SD.begin(PIN_SD_CS, SPI, 4000000);
    if (ok)
      break;
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (!ok) {
    detected = false;
  } else {
    detected = true;
  }
  return ok;
}

void SDCardManager::update() {
  bool currentDetected = (digitalRead(PIN_SD_DET) == LOW);
  if (currentDetected != sdInserted) {
    sdInserted = currentDetected;
    LOG("[SDCARD] Physical status changed: %s\n", sdInserted ? "INSERTED" : "REMOVED");
    if (sdInserted) {
      if (sdcard.begin()) {
        detected = true;
        LOG("[SDCARD] Successfully re-initialized\n");
      } else {
        detected = false;
        LOG("[SDCARD] Failed to re-initialize\n");
      }
    } else {
      player.stop();
      detected = false;
    }
  }
}

bool SDCardManager::isDetected() {
  return detected;
}

void SDCardManager::scan() {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    totalFiles = 0;
    File root = SD.open("/");
    if (root) {
      while (true) {
        File entry = root.openNextFile();
        if (!entry)
          break;
        if (!entry.isDirectory()) {
          String name = entry.name();
          name.toLowerCase();
          if (name.endsWith(".mid") || name.endsWith(".midi")) {
            String path = "/" + String(entry.name());
            strncpy(filenames[totalFiles], path.c_str(), MAX_FILENAME - 1);
            filenames[totalFiles][MAX_FILENAME - 1] = '\0';
            totalFiles++;
            if (totalFiles >= MAX_FILES)
              break;
          }
        }
        entry.close();
      }
      root.close();
    }
    currentIndex = 0;
    xSemaphoreGive(mutex);
  }
}

bool SDCardManager::next() {
  bool result = false;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (totalFiles > 0) {
      currentIndex++;
      if (currentIndex >= totalFiles)
        currentIndex = 0;
      result = true;
    }
    xSemaphoreGive(mutex);
  }
  return result;
}

bool SDCardManager::prev() {
  bool result = false;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (totalFiles > 0) {
      currentIndex--;
      if (currentIndex < 0)
        currentIndex = totalFiles - 1;
      result = true;
    }
    xSemaphoreGive(mutex);
  }
  return result;
}

uint16_t SDCardManager::getCount() {
  uint16_t count = 0;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    count = totalFiles;
    xSemaphoreGive(mutex);
  }
  return count;
}

const char *SDCardManager::getCurrentFile() {
  static char currentFile[MAX_FILENAME];
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (totalFiles == 0)
      currentFile[0] = '\0';
    else
      strncpy(currentFile, filenames[currentIndex], MAX_FILENAME);
    xSemaphoreGive(mutex);
  }
  return currentFile;
}

File SDCardManager::openCurrent() {
  File file;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (totalFiles > 0) {
      file = SD.open(filenames[currentIndex]);
    }
    xSemaphoreGive(mutex);
  }
  return file;
}

File SDCardManager::openFile(const char *path, const char *mode) {
  File file;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    file = SD.open(path, mode);
    xSemaphoreGive(mutex);
  }
  return file;
}

bool SDCardManager::deleteFile(const char *path) {
  bool ok = false;
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    ok = SD.remove(path);
    xSemaphoreGive(mutex);
  }
  return ok;
}