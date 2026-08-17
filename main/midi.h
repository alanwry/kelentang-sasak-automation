#ifndef MIDI_H
#define MIDI_H

#include <Arduino.h>
#include <SD.h>

// MIDI File Parser - membaca dan parse file MIDI
class MidiFile {
public:
  bool open(File file);  // Buka file MIDI
  void close();          // Tutup file
  bool parse();          // Parse file MIDI ke event queue
  bool eof();            // Cek end of file

  // Info untuk debugging
  uint32_t getEventCount() const {
    return eventCount;
  }
  uint32_t getLastError() const {
    return lastError;
  }

private:
  File midiFile;
  uint32_t eventCount = 0;
  uint32_t lastError = 0;

  // Error codes untuk tracking masalah parsing
  enum ErrorCode {
    ERR_NONE = 0,
    ERR_FILE_TRUNCATED = 1,  // File tidak lengkap
    ERR_INVALID_HEADER = 2,  // Header MIDI tidak valid
    ERR_INVALID_FORMAT = 3,  // Format tidak support
    ERR_INVALID_TRACK = 4,   // Track tidak valid
    ERR_PARSE_ERROR = 5      // Error saat parsing
  };
};

extern MidiFile midi;

#endif