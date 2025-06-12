#pragma once

#include <Arduino.h>
#include <constants.hpp>

struct ModelConfigDataType {
  void print();
  void setup();

  void println(const char* format, ...);

  bool modelIsPresent;
  int modelVersion;
  uint8_t model[100000]; // 100k should be sufficient for a tensorflow Lite model
}; 