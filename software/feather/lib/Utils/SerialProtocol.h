#pragma once

#include <Arduino.h>

// send an audio snippet to the PC via Serial.
// cmd = CMD_AUDIO_STREAM or CMD_AUDIO_SAMPLE
// audioBuf: 1s of 16kHz audio, mono 16 bit pcm
// samples: no of entries in audioBuf
// scores: outcome of inference values in the order of the labels
// score_count: no of entries in scores
void sendAudioPacket(uint16_t cmd, int16_t* audioBuf, size_t samples, float scores[], size_t score_count);
