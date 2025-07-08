#pragma once

#include <Arduino.h>

void runInference(int16_t buffer[], size_t samples, float confidence[], int &pred_no);
uint8_t get_no_of_labels();
void setupInference();
String getLabelName(uint8_t no);
