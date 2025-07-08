#include <Arduino.h>
#include <i2s.h>

#include "soundtools.h"

BiquadQ15 hp, lp;

void initAudio() {
  // Initialize I2S in Philips mode, 16kHz, 16-bit
  if (!I2S.begin(I2S_PHILIPS_MODE, 16000, 16)) {
    Serial.println("Failed to initialize I2S!");
    while(1); // Halt on failure
  }
  Serial.println("I2S initialized successfully");

  // initialise filters
  hp.init(1, 300.0f, 16000.0f);
  lp.init(0, 3400.0f, 16000.0f);
}

/**
 * Checks if audio data is available
 * @return true if at least one full buffer is available
 */
bool isAudioAvailable() {
  return I2S.available() > 0;
}

void drainAudioData(int16_t audio_in_buffer[], size_t audioInBufferSize, size_t &added_samples) {
    added_samples = 0;
    while (I2S.available() > 0) {
        static const size_t smallBufferSize  = 128; 
        static uint16_t smallBuffer[smallBufferSize];
        uint16_t samples  = I2S.read(smallBuffer, smallBufferSize);

        // Shift audio buffer and add new samples at end
        memmove(audio_in_buffer, audio_in_buffer + samples, (audioInBufferSize - samples) * sizeof(audio_in_buffer[0]));
        memcpy(audio_in_buffer + (smallBufferSize - samples), smallBuffer, samples * sizeof(audio_in_buffer[0]));
        added_samples += samples;
    }
}

void filterAudio(int16_t audioBuffer[], size_t audioBufferSize) {
  for (int i = 0;i<audioBufferSize;i++) {
     int16_t sample = audioBuffer[i];
     audioBuffer[i] = hp.process(lp.process(sample));
  }
} 


uint32_t last_time_audio_receiver = millis();

void resetAudioWatchdog() {
  last_time_audio_receiver = millis();
}
