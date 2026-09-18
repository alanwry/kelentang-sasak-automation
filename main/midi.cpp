#include "midi.h"
#include "event_queue.h"
#include "solenoid.h"
#include "player.h"
#include "webserver.h"

MidiFile midi;

static bool seenEvents[128][17]; // Track unique Note-Channel combinations

namespace {

// Helper functions for safe file reading
bool readBe16Safe(File &file, uint16_t &result, uint32_t &bytesRead) {
    if (file.available() < 2) return false;
    uint8_t b1 = file.read();
    uint8_t b2 = file.read();
    bytesRead += 2;
    result = (uint16_t)((b1 << 8) | b2);
    return true;
}

bool readBe32Safe(File &file, uint32_t &result, uint32_t &bytesRead) {
    if (file.available() < 4) return false;
    uint8_t b1 = file.read();
    uint8_t b2 = file.read();
    uint8_t b3 = file.read();
    uint8_t b4 = file.read();
    bytesRead += 4;
    result = ((uint32_t)b1 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | b4;
    return true;
}

bool readVarLengthSafe(File &file, uint32_t &value, uint32_t &bytesRead) {
    value = 0;
    uint8_t b;
    uint8_t count = 0;
    do {
        if (!file.available()) return false;
        b = file.read();
        bytesRead++;
        count++;
        value = (value << 7) | (b & 0x7F);
        if (count > 4) return false;
    } while (b & 0x80);
    return true;
}

bool readExactSafe(File &file, uint8_t *buffer, uint16_t len, uint32_t &bytesRead) {
    if (file.available() < len) return false;
    for (uint16_t i = 0; i < len; i++) buffer[i] = file.read();
    bytesRead += len;
    return true;
}

bool skipSafe(File &file, uint32_t len, uint32_t &bytesRead) {
    if (file.available() < len) return false;
    for (uint32_t i = 0; i < len; i++) file.read();
    bytesRead += len;
    return true;
}
} // namespace

bool MidiFile::open(File file) {
    midiFile = file;
    eventCount = 0;
    lastError = ERR_NONE;
    if (!midiFile) LOG("[MIDI] Open failed: invalid file handle\n");
    return (bool)midiFile;
}

void MidiFile::close() {
    if (midiFile) midiFile.close();
}

bool MidiFile::eof() {
    return !midiFile || midiFile.available() == 0;
}

bool MidiFile::parse() {
    memset(seenEvents, 0, sizeof(seenEvents));

    if (!midiFile) {
        LOG("[MIDI] parse failed: invalid file\n");
        lastError = ERR_FILE_TRUNCATED;
        return false;
    }

    eventQueue.clear();
    eventCount = 0;

    char id[4];
    uint32_t bytesRead = 0;

    // Header validation
    if (!readExactSafe(midiFile, (uint8_t *)id, 4, bytesRead) || strncmp(id, "MThd", 4) != 0) {
        LOG("[MIDI] parse failed: invalid header\n");
        lastError = ERR_INVALID_HEADER;
        return false;
    }

    uint32_t headerLength = 0;
    if (!readBe32Safe(midiFile, headerLength, bytesRead) || headerLength < 6) {
        LOG("[MIDI] parse failed: invalid header length\n");
        lastError = ERR_INVALID_FORMAT;
        return false;
    }

    uint16_t format, trackCount, division;
    if (!readBe16Safe(midiFile, format, bytesRead) || 
        !readBe16Safe(midiFile, trackCount, bytesRead) || 
        !readBe16Safe(midiFile, division, bytesRead)) {
        lastError = ERR_FILE_TRUNCATED;
        return false;
    }

    if (division & 0x8000) division = 480;
    else if (division == 0) division = 480;

    if (headerLength > 6 && !skipSafe(midiFile, headerLength - 6, bytesRead)) {
        lastError = ERR_FILE_TRUNCATED;
        return false;
    }

    uint32_t tempoUsPerQuarter = 500000;
    uint64_t maxAbsoluteTicks = 0;

    // Track parsing
    for (uint16_t trackIndex = 0; trackIndex < trackCount; trackIndex++) {
        char trackId[4];
        if (!readExactSafe(midiFile, (uint8_t *)trackId, 4, bytesRead) || strncmp(trackId, "MTrk", 4) != 0) {
            lastError = ERR_INVALID_TRACK;
            break;
        }

        uint32_t trackLength = 0;
        if (!readBe32Safe(midiFile, trackLength, bytesRead)) {
            lastError = ERR_FILE_TRUNCATED;
            break;
        }

        uint32_t trackEnd = midiFile.position() + trackLength;
        uint32_t trackBytesRead = 0;
        uint8_t runningStatus = 0;
        uint64_t absoluteTicks = 0;

        while (midiFile.position() < trackEnd && trackBytesRead < trackLength) {
            uint32_t delta = 0;
            if (!readVarLengthSafe(midiFile, delta, trackBytesRead)) break;
            absoluteTicks += delta;

            uint8_t statusByte = midiFile.peek();
            if (statusByte & 0x80) {
                statusByte = midiFile.read();
                trackBytesRead++;
                runningStatus = statusByte;
            } else {
                statusByte = runningStatus;
            }

            uint8_t statusNibble = statusByte >> 4;
            uint8_t d1, d2;

            switch (statusNibble) {
                case 0x8: case 0x9: case 0xA: case 0xB: case 0xE:
                    d1 = midiFile.read(); d2 = midiFile.read(); trackBytesRead += 2;
                    
                    if (statusNibble == 0x9 && d2 > 0) { // Note On
                        uint8_t channel = (statusByte & 0x0F) + 1;
                        Solenoid *items = solenoid.getItems();
                        for (uint8_t i = 0; i < solenoid.getCount(); i++) {
                            if (items[i].getMidiNote() == d1 && (items[i].getMidiChannel() == 0 || items[i].getMidiChannel() == channel)) {
                                MidiEvent evt;
                                evt.timeUS = (absoluteTicks * tempoUsPerQuarter) / division;
                                evt.type = EVENT_NOTE_ON;
                                evt.note = d1;
                                evt.solenoidId = i;
                                eventQueue.push(evt);
                                eventCount++;
                            }
                        }
                    }
                    break;
                case 0xC: case 0xD:
                    d1 = midiFile.read(); trackBytesRead++;
                    break;
                case 0xF:
                    if (statusByte == 0xFF) { // Meta Event
                        uint8_t metaType = midiFile.read(); trackBytesRead++;
                        uint32_t metaLen;
                        readVarLengthSafe(midiFile, metaLen, trackBytesRead);
                        if (metaType == 0x51 && metaLen >= 3) {
                            uint32_t t = ((uint32_t)midiFile.read() << 16) | ((uint32_t)midiFile.read() << 8) | midiFile.read();
                            trackBytesRead += 3;
                            tempoUsPerQuarter = constrain(t, 200000, 3000000);
                        } else if (metaLen > 0) {
                            skipSafe(midiFile, metaLen, trackBytesRead);
                        }
                    } else if (statusByte == 0xF0 || statusByte == 0xF7) { // SysEx
                        uint32_t sysexLen;
                        readVarLengthSafe(midiFile, sysexLen, trackBytesRead);
                        skipSafe(midiFile, sysexLen, trackBytesRead);
                    }
                    break;
            }
        }
        midiFile.seek(trackEnd);
        if (absoluteTicks > maxAbsoluteTicks) maxAbsoluteTicks = absoluteTicks;
    }

    eventQueue.sort();
    player.setTotalDurationUS((maxAbsoluteTicks * tempoUsPerQuarter) / division);
    return eventCount > 0;
}
