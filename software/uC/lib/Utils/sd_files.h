#pragma once

#include <Arduino.h>
#include <SD.h>                                             // used to access the Audio Shield's SD card

#include "constants.h"

void save_wav_file(const char* label, uint16_t label_no, const int16_t buf[], size_t samples);
void init_sd_files(size_t no_of_labels, const  char* categories[]);
