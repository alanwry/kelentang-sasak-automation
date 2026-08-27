#include "midi.h"
#include "event_queue.h"
#include "solenoid.h"
#include "player.h"

MidiFile midi;

namespace {
// Helper functions for reading MIDI files

// Read 16-bit big-endian with validation
bool readBe16Safe(File &file, uint16_t &result, uint32_t &bytesRead) {
  if (file.available() < 2)
    return false;
  uint8_t b1 = file.read();
  uint8_t b2 = file.read();
  bytesRead += 2;
  result = (uint16_t)((b1 << 8) | b2);
  return true;
}

// Read 32-bit big-endian with validation
bool readBe32Safe(File &file, uint32_t &result, uint32_t &bytesRead) {
  if (file.available() < 4)
    return false;
  uint8_t b1 = file.read();
  uint8_t b2 = file.read();
  uint8_t b3 = file.read();
  uint8_t b4 = file.read();
  bytesRead += 4;
  result = ((uint32_t)b1 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | b4;
  return true;
}

// Variable-length quantity decoder
bool readVarLengthSafe(File &file, uint32_t &value, uint32_t &bytesRead) {
  value = 0;
  uint8_t b;
  uint8_t count = 0;
  do {
    if (!file.available())
      return false;
    b = file.read();
    bytesRead++;
    count++;
    value = (value << 7) | (b & 0x7F);
    if (count > 4)
      return false;
  } while (b & 0x80);
  return true;
}

bool readExactSafe(File &file, uint8_t *buffer, uint16_t len, uint32_t &bytesRead) {
  if (file.available() < len)
    return false;
  for (uint16_t i = 0; i < len; i++)
    buffer[i] = file.read();
  bytesRead += len;
  return true;
}

bool skipSafe(File &file, uint32_t len, uint32_t &bytesRead) {
  if (file.available() < len)
    return false;
  for (uint32_t i = 0; i < len; i++)
    file.read();
  bytesRead += len;
  return true;
}
}

bool MidiFile::open(File file) {
  midiFile = file;
  eventCount = 0;
  lastError = 0;
  return midiFile;
}

void MidiFile::close() {
  if (midiFile) {
    midiFile.close();
  }
}

bool MidiFile::eof() {
  return !midiFile || midiFile.available() == 0;
}

// MAIN MIDI PARSER
bool MidiFile::parse() {
  if (!midiFile) {
    Serial.println("[MIDI] parse failed: file not valid");
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  eventQueue.clear();
  eventCount = 0;

  // STEP 1: Validate MIDI header (MThd)
  char id[4];
  uint32_t bytesRead = 0;

  if (!readExactSafe(midiFile, (uint8_t *)id, 4, bytesRead)) {
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  if (strncmp(id, "MThd", 4) != 0) {
    Serial.println("[MIDI] parse failed: invalid header");
    lastError = ERR_INVALID_HEADER;
    return false;
  }

  // STEP 2: Read MIDI header data
  uint32_t headerLength = 0;
  if (!readBe32Safe(midiFile, headerLength, bytesRead)) {
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  if (headerLength < 6) {
    lastError = ERR_INVALID_FORMAT;
    return false;
  }

  uint16_t format = 0;
  if (!readBe16Safe(midiFile, format, bytesRead)) {
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  if (format > 2) {
    lastError = ERR_INVALID_FORMAT;
    return false;
  }

  uint16_t trackCount = 0;
  if (!readBe16Safe(midiFile, trackCount, bytesRead)) {
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  if (trackCount == 0) {
    Serial.println("[MIDI] parse failed: no tracks");
    lastError = ERR_INVALID_FORMAT;
    return false;
  }

  uint16_t division = 0;
  if (!readBe16Safe(midiFile, division, bytesRead)) {
    lastError = ERR_FILE_TRUNCATED;
    return false;
  }

  if ((division & 0x8000) != 0)
    division = 480;
  else if (division == 0)
    division = 480;

  // Skip extra header bytes if present
  if (headerLength > 6) {
    uint32_t skipLen = headerLength - 6;
    if (!skipSafe(midiFile, skipLen, bytesRead)) {
      lastError = ERR_FILE_TRUNCATED;
      return false;
    }
  }

  // STEP 3: Process tracks
  uint32_t tempoUsPerQuarter = 500000;
  uint64_t maxAbsoluteTicks = 0;

  for (uint16_t trackIndex = 0; trackIndex < trackCount; trackIndex++) {
    char trackId[4];
    if (!readExactSafe(midiFile, (uint8_t *)trackId, 4, bytesRead)) {
      lastError = ERR_FILE_TRUNCATED;
      break;
    }

    if (strncmp(trackId, "MTrk", 4) != 0) {
      lastError = ERR_INVALID_TRACK;
      break;
    }

    uint32_t trackLength = 0;
    if (!readBe32Safe(midiFile, trackLength, bytesRead)) {
      lastError = ERR_FILE_TRUNCATED;
      break;
    }

    uint32_t trackStart = midiFile.position();
    uint32_t trackEnd = trackStart + trackLength;
    uint32_t trackBytesRead = 0;
    uint8_t runningStatus = 0;
    uint64_t absoluteTicks = 0; // Reset ticks for each track to support parallel track playback

    // Process all events in track
    while (midiFile.position() < trackEnd && trackBytesRead < trackLength) {
      uint32_t delta = 0;
      if (!readVarLengthSafe(midiFile, delta, trackBytesRead)) {
        lastError = ERR_PARSE_ERROR;
        break;
      }
      absoluteTicks += delta;

      if (!midiFile.available()) {
        lastError = ERR_FILE_TRUNCATED;
        break;
      }

      uint8_t firstByte = midiFile.peek();
      uint8_t statusByte;

      if (firstByte & 0x80) {
        statusByte = midiFile.read();
        trackBytesRead++;
        runningStatus = statusByte;
      } else {
        statusByte = runningStatus;
      }

      if (statusByte == 0)
        continue;

      uint8_t statusNibble = statusByte >> 4;
      uint8_t data1, data2;

      // STEP 4: Parse each MIDI event based on status byte
      switch (statusNibble) {
        case 0x8:  // Note Off
          if (midiFile.available() < 2) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          data2 = midiFile.read();
          trackBytesRead += 2;
          break;

        case 0x9:  // Note On
          if (midiFile.available() < 2) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          data2 = midiFile.read();
          trackBytesRead += 2;

          if (data2 > 0) {
            Solenoid *items = solenoid.getItems();
            uint8_t channel = (statusByte & 0x0F) + 1; // MIDI channel 1-16
            
            for (uint8_t i = 0; i < solenoid.getCount(); i++) {
              if (items[i].getMidiNote() == data1 && 
                 (items[i].getMidiChannel() == 0 || items[i].getMidiChannel() == channel)) {
                MidiEvent evt;
                evt.timeUS = (absoluteTicks * tempoUsPerQuarter) / division;
                evt.type = EVENT_NOTE_ON;
                evt.note = data1;
                evt.solenoidId = i;
                eventQueue.push(evt);
                eventCount++;
                break;
              }
            }
          }
          break;

        case 0xA:
          if (midiFile.available() < 2) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          data2 = midiFile.read();
          trackBytesRead += 2;
          break;

        case 0xB:  // Control Change
          if (midiFile.available() < 2) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          data2 = midiFile.read();
          trackBytesRead += 2;
          break;

        case 0xC:  // Program Change
          if (!midiFile.available()) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          trackBytesRead++;
          break;

        case 0xD:  // Channel Pressure
          if (!midiFile.available()) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          trackBytesRead++;
          break;

        case 0xE:  // Pitch Bend
          if (midiFile.available() < 2) {
            lastError = ERR_FILE_TRUNCATED;
            goto track_done;
          }
          data1 = midiFile.read();
          data2 = midiFile.read();
          trackBytesRead += 2;
          break;

        case 0xF:                    // System/Meta event
          if (statusByte == 0xFF) {  // Meta event
            if (!midiFile.available()) {
              lastError = ERR_FILE_TRUNCATED;
              goto track_done;
            }

            uint8_t metaType = midiFile.read();
            trackBytesRead++;

            if (!midiFile.available()) {
              lastError = ERR_FILE_TRUNCATED;
              goto track_done;
            }

            uint32_t metaLength = 0;
            if (!readVarLengthSafe(midiFile, metaLength, trackBytesRead)) {
              lastError = ERR_FILE_TRUNCATED;
              goto track_done;
            }

            if (metaType == 0x51 && metaLength >= 3) {  // Set Tempo (IMPORTANT)
              if (midiFile.available() < 3) {
                lastError = ERR_FILE_TRUNCATED;
                goto track_done;
              }
              uint8_t t1 = midiFile.read();
              uint8_t t2 = midiFile.read();
              uint8_t t3 = midiFile.read();
              trackBytesRead += 3;
              tempoUsPerQuarter = ((uint32_t)t1 << 16) | ((uint32_t)t2 << 8) | t3;

              if (tempoUsPerQuarter < 200000)
                tempoUsPerQuarter = 200000;
              if (tempoUsPerQuarter > 3000000)
                tempoUsPerQuarter = 3000000;
            } else if (metaType == 0x2F) {  // End of Track
              if (metaLength > 0) {
                if (!skipSafe(midiFile, metaLength, trackBytesRead)) {
                  lastError = ERR_FILE_TRUNCATED;
                  goto track_done;
                }
              }
              break;
            } else {
              if (metaLength > 0) {
                if (!skipSafe(midiFile, metaLength, trackBytesRead)) {
                  lastError = ERR_FILE_TRUNCATED;
                  goto track_done;
                }
              }
            }
          } else if (statusByte == 0xF0 || statusByte == 0xF7) {  // SysEx
            if (!midiFile.available()) {
              lastError = ERR_FILE_TRUNCATED;
              goto track_done;
            }

            uint32_t sysexLen = 0;
            if (!readVarLengthSafe(midiFile, sysexLen, trackBytesRead)) {
              lastError = ERR_FILE_TRUNCATED;
              goto track_done;
            }

            if (sysexLen > 0) {
              if (!skipSafe(midiFile, sysexLen, trackBytesRead)) {
                lastError = ERR_FILE_TRUNCATED;
                goto track_done;
              }
            }
          }
          break;

        default:
          break;
      }
    }

track_done:
    if (absoluteTicks > maxAbsoluteTicks) {
      maxAbsoluteTicks = absoluteTicks;
    }
    uint32_t currentPos = midiFile.position();
    if (currentPos < trackEnd) {
      midiFile.seek(trackEnd);
    }
  }

  // Urutkan event berdasarkan waktu (timeUS) karena track diproses satu per satu
  eventQueue.sort();

  // Durasi total sudah terhitung di maxAbsoluteTicks selama proses track
  player.setTotalDurationUS((maxAbsoluteTicks * tempoUsPerQuarter) / division);
  return !eventQueue.empty();
}
