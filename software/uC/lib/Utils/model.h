#pragma once

#include <Arduino.h>
#include "constants.h"

struct ModelConfigDataType {
  void print();
  void setup();

  void println(const char* format, ...);

  bool modelIsPresent;
  int modelVersion;
  int gainLevel;
}; 