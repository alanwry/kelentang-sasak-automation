#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <Arduino.h>

enum EventType {
  EVENT_NOTE_ON = 1
};

struct MidiEvent {
  uint64_t timeUS;
  uint8_t type;
  uint8_t note;
  uint8_t solenoidId;
};

class EventQueue {
public:
  bool peek(MidiEvent &event);
  bool pop(MidiEvent &event);
  void push(const MidiEvent &event);
  void clear();
  bool empty() const;

private:
  static constexpr uint8_t MAX_EVENTS = 128;
  MidiEvent buffer[MAX_EVENTS];
  uint8_t head = 0;
  uint8_t tail = 0;
  uint8_t count = 0;
};

extern EventQueue eventQueue;

#endif
