#ifndef MAPPING_H
#define MAPPING_H

#include <Arduino.h>

struct MappingEntry {
  uint8_t midi;
  uint8_t solenoid;
};

static constexpr uint8_t MAP_SIZE = 12;

static const MappingEntry noteMap[MAP_SIZE] = {
  { 60, 0 }, { 62, 1 }, { 64, 2 }, { 65, 3 }, { 67, 4 }, { 69, 5 }, { 71, 6 }, { 72, 7 }, { 74, 8 }, { 76, 9 }, { 77, 10 }, { 79, 11 }
};

#endif
