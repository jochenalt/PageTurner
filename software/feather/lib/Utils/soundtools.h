#pragma

#include <Arduino.h>

// Q15‐scaled biquad structure
struct BiquadQ15 {
  int16_t b0, b1, b2;  // feed-forward (numerator) Q15 coeffs
  int16_t a1, a2;      // feedback (denominator) Q15 coeffs
  int32_t x1, x2;      // previous inputs
  int32_t y1, y2;      // previous outputs

  // Initialize as Butterworth low-pass or high-pass:
  // mode: 0 = low-pass, 1 = high-pass
  // fc: cutoff frequency in Hz
  // fs: sample rate in Hz
  void init(uint8_t mode, float fc, float fs) {
    // pre-warp and compute analog prototype
    float w0 = 2.0f * M_PI * fc / fs;
    float K  = tanf(w0 / 2.0f);
    float norm;
    float b0f, b1f, b2f, a1f, a2f;

    if (mode == 0) {
      // 2nd-order Butterworth low-pass
      norm = 1.0f / (1.0f + sqrtf(2.0f)*K + K*K);
      b0f =  K*K * norm;
      b1f =  2.0f * b0f;
      b2f =  b0f;
      a1f =  2.0f * (K*K - 1.0f) * norm;
      a2f =  (1.0f - sqrtf(2.0f)*K + K*K) * norm;
    } else {
      // 2nd-order Butterworth high-pass
      norm = 1.0f / (1.0f + sqrtf(2.0f)*K + K*K);
      b0f =  1.0f * norm;
      b1f = -2.0f * b0f;
      b2f =  b0f;
      a1f =  2.0f * (K*K - 1.0f) * norm;
      a2f =  (1.0f - sqrtf(2.0f)*K + K*K) * norm;
    }

    // Scale to Q15 (±32767)
    b0 = int16_t(roundf(b0f * 32767.0f));
    b1 = int16_t(roundf(b1f * 32767.0f));
    b2 = int16_t(roundf(b2f * 32767.0f));
    a1 = int16_t(roundf(a1f * 32767.0f));
    a2 = int16_t(roundf(a2f * 32767.0f));

    // zero states
    x1 = x2 = y1 = y2 = 0;
  }

  // Process one Q15 sample
  inline int16_t process(int16_t x0) {
    // 32-bit accumulator: b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2
    int32_t acc = (int32_t)b0 * x0
                + (int32_t)b1 * x1
                + (int32_t)b2 * x2
                - (int32_t)a1 * y1
                - (int32_t)a2 * y2;
    // shift back to Q15
    acc = acc >> 15;
    // simple saturation
    if (acc >  32767) acc =  32767;
    if (acc < -32768) acc = -32768;
    int16_t y0 = (int16_t)acc;

    // shift delay line
    x2 = x1;  x1 = x0;
    y2 = y1;  y1 = y0;

    return y0;
  }
};

extern uint32_t last_time_audio_receiver;


void initAudio();
bool isAudioAvailable();
void drainAudioData(int16_t audio_in_buffer[], size_t audioInBufferSize, size_t &added_samples);
void filterAudio(int16_t audioBuffer[], size_t audioBufferSize);
void resetAudioWatchdog();
void generateSineWave(int16_t* buffer, size_t samples, float freq = 440.0, float amplitude = 0.8);
