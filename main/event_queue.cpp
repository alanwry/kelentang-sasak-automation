#include "event_queue.h"
#include "webserver.h"

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
}

void EventQueue::clear() {
  head = 0;
  tail = 0;
  count = 0;
}

bool EventQueue::empty() const {
  return count == 0;
}

void EventQueue::sort() {
  if (count <= 1) return;

  MidiEvent *temp = new MidiEvent[count];
  uint16_t current = head;
  for (uint16_t i = 0; i < count; i++) {
    temp[i] = buffer[current];
    current = (current + 1) % MAX_EVENTS;
  }

  for (uint16_t i = 1; i < count; i++) {
    MidiEvent key = temp[i];
    int16_t j = i - 1;
    while (j >= 0 && temp[j].timeUS > key.timeUS) {
      temp[j + 1] = temp[j];
      j--;
    }
    temp[j + 1] = key;
  }

  for (uint16_t i = 0; i < count; i++) {
    buffer[i] = temp[i];
  }

  delete[] temp;

  head = 0;
  tail = count;
  if (tail >= MAX_EVENTS) tail = 0;
}