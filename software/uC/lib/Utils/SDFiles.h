#pragma once

#include <Arduino.h>

//
// Save an audio buffer that was mislabelled, onto the SD card (Teensy Audio Board).
//
// Parameters:
// - label:      The incorrect label name returned by inference
// - label_no:   The label index in the Edge Impulse category list
// - buf:        Audio buffer (16-bit PCM, 16 kHz, mono, 1 second long)
// - samples:    Number of samples in the buffer
//
void saveWavFile(const char* label, uint16_t label_no, const int16_t buf[], size_t samples);

//
// Initialise SD card and scan for existing mislabelled audio files to determine the
// next available filename.
//
// Parameters:
// - no_of_labels: Number of categories as defined by Edge Impulse
// - categories:   Array of category names
//
void initSDFiles(size_t no_of_labels, const char* categories[]);
