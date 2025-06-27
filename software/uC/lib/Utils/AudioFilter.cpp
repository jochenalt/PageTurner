
// AudioFilter.cpp
#include "AudioFilter.h"
#include "constants.h" 

// Statische Instanzen der Filter
static Biquad lpFilterIn;
static Biquad bpFilter;
static Biquad highPassFilterOut;
static Biquad lowPassFilterOut;

Biquad::Biquad()
    : b0_coef(0), b1_coef(0), b2_coef(0), a1_coef(0), a2_coef(0),
      prev_x1(0), prev_x2(0), prev_y1(0), prev_y2(0) {}

void Biquad::initLowpass(float fs, float f0, float Q) {
    float w0    = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 =  (1 - cosw0) / 2;
    float b1 =    1 - cosw0;
    float b2 =  (1 - cosw0) / 2;
    float a0 =    1 + alpha;
    float a1 =   -2 * cosw0;
    float a2 =    1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

void Biquad::initBandpass(float fs, float f0, float Q) {
    float w0    = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 =  alpha;
    float b1 =      0;
    float b2 = -alpha;
    float a0 =  1 + alpha;
    float a1 = -2 * cosw0;
    float a2 =  1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

void Biquad::initHighpass(float fs, float f0, float Q) {
    float w0    = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 =  (1 + cosw0) / 2;
    float b1 = -(1 + cosw0);
    float b2 =  (1 + cosw0) / 2;
    float a0 =   1 + alpha;
    float a1 =  -2 * cosw0;
    float a2 =   1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

float Biquad::process(float x0) {
    float y0 = b0_coef * x0
             + b1_coef * prev_x1
             + b2_coef * prev_x2
             - a1_coef * prev_y1
             - a2_coef * prev_y2;

    prev_x2 = prev_x1;
    prev_x1 = x0;
    prev_y2 = prev_y1;
    prev_y1 = y0;

    return y0;
}

void initFilters() {
    if (!FILTERS_INITIALIZED) {
        lpFilterIn.initLowpass(INPUT_SAMPLE_RATE, 8000.0f, 0.707f);
        bpFilter.initBandpass(OUTPUT_SAMPLE_RATE, 1850.0f, 0.707f);
        highPassFilterOut.initHighpass(OUTPUT_SAMPLE_RATE, 300, 0.707f);
        lowPassFilterOut.initLowpass(OUTPUT_SAMPLE_RATE, 3400, 0.707f);

    }
}

void processAudioBuffer(const int16_t* inputBuf, size_t inLen, int16_t outBuf[], size_t& outLen) {
    const float ratio = 16000.0f / 44100.0f;
    outLen = size_t(ratio * inLen  + 0.5f);
    outLen = min(outLen, (size_t)OUT_SAMPLES);

    // 2) Resample → 16 kHz with 3-tap smoothing
    for (size_t j = 0; j < outLen; j++) {

        float pos = j / ratio;
        int   i0  = int(floor(pos));

        // 3-tap average instead of linear interp
        float sum = 0, cnt = 0;
        if (i0 > 0)             { sum += inputBuf[i0 - 1]/32768.0f; cnt += 1; }
        if (i0 < int(inLen))    { sum += inputBuf[i0]/32768.0f;     cnt += 1; }
        if (i0 + 1 < int(inLen)){ sum += inputBuf[i0 + 1]/32768.0f; cnt += 1; }
        float downsampled =  sum / cnt;

            // 3) Bandpass 300-3400Hz @ 16 kHz und Rückumwandlung in int16
        float y = lowPassFilterOut.process(highPassFilterOut.process(downsampled));
        float scaled = y * 32767.0f;
        if (scaled >  32767.0f) scaled =  32767.0f;
        if (scaled < -32767.0f) scaled = -32767.0f;
        outBuf[j] = int16_t(scaled);
    }

}
