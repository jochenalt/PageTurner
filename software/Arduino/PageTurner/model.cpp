
#include "model.h"
#include "constants.h"

void ModelConfigDataType::println(const char* format, ...) {
  char s[256];
  __gnuc_va_list args;
  va_start(args, format);
  vsprintf(s, format, args);
  va_end(args);
  LOGSerial.println(s);
};

void ModelConfigDataType::print() {

  if (modelIsPresent) {
    println("model %i is present", modelVersion);
  } else {
    println("no model present");
  }
  println("gain level %i", gainLevel);
}

void ModelConfigDataType::setup() {
  modelIsPresent = false;
  modelVersion = 0;
  gainLevel = 5;
}
