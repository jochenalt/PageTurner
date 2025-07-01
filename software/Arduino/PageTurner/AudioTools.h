#pragma once

#include <Arduino.h>



// initialise everything necessary to get audio signals from teensy Audio board
void initAudioTools();

// process a 44.1 kHz audio buffer and generate a 16-bit 16kHz audio buffer that is
// downsampled and filtered
void processAudioBuffer(const int16_t* inputBuf, size_t inLen, int16_t outBuf[],size_t& outLen);

// set gain levele of line-in socket on teensy audio board
void set_gain_level(uint16_t);

// clear input buffer
void clearAudioBuffer();

// returns true, if drainAudioData can be called
int isAudioDataAvailable();

// fetch all available audio data from teensy audio board
void drainAudioData(int16_t audio_in_buffer[], size_t &added_samples);
