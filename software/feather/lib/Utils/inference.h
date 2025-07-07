#pragma once

#include "PageTurner_inferencing.h"
#include <Arduino.h>

void runInference(int16_t buffer[], size_t samples, ei_impulse_result_t &result, int &pred_no);
