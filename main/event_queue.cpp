#include "event_queue.h"

EventQueue eventQueue;

bool EventQueue::peek(MidiEvent &event) {
  if (count == 0) {
    return false;
  }

  event = buffer[head];
  return true;
}

bool EventQueue::pop(MidiEvent &event) {
  if (count == 0) {
    return false;
  }

  event = buffer[head];
  head = (head + 1) % MAX_EVENTS;
  count--;
  return true;
}

void EventQueue::push(const MidiEvent &event) {
  if (count >= MAX_EVENTS) {
    return;
  }

  buffer[tail] = event;
  tail = (tail + 1) % MAX_EVENTS;
  count++;
  static bool logged = false;
  if (!logged) {
    Serial.println("[QUEUE]: MIDI event queued");
    logged = true;
  }
}

void EventQueue::clear() {
  head = 0;
  tail = 0;
  count = 0;
  static bool logged = false;
  if (logged) {
    logged = false;
  }
}

bool EventQueue::empty() const {
  return count == 0;
}
