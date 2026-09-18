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

    uint32_t getEventCount() const { return eventCount; }
    uint32_t getLastError() const { return lastError; }

private:
    File midiFile;
    uint32_t eventCount = 0;
    uint32_t lastError = 0;

    enum ErrorCode {
        ERR_NONE = 0,
        ERR_FILE_TRUNCATED,
        ERR_INVALID_HEADER,
        ERR_INVALID_FORMAT,
        ERR_INVALID_TRACK,
        ERR_PARSE_ERROR
    };
};

extern MidiFile midi;

#endif
