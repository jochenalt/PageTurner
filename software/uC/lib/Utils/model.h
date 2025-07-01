#pragma once

#include <Arduino.h>

// contains data stored in EPPROM
struct ModelConfigDataType {
  void print();
  void setup();

  void println(const char* format, ...);

  bool modelIsPresent;
  int modelVersion;   
  int gainLevel;      // line-in gain level used by Teensy Audio Board
}; 