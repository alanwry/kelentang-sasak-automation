#ifndef MIDI_H
#define MIDI_H

#include <Arduino.h>
#include <SD.h>

class MidiFile {
public:
  bool open(File file);  
  void close();          
  bool parse();         
  bool eof();         

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

  enum ErrorCode {
    ERR_NONE = 0,
    ERR_FILE_TRUNCATED = 1, 
    ERR_INVALID_HEADER = 2,  
    ERR_INVALID_FORMAT = 3,  
    ERR_INVALID_TRACK = 4, 
    ERR_PARSE_ERROR = 5     
  };
};

extern MidiFile midi;

#endif