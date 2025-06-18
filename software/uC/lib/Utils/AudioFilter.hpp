// AudioFilter.h
#ifndef AUDIOFILTER_H
#define AUDIOFILTER_H

#include <Arduino.h>

class Biquad {
public:
    Biquad();
    void initLowpass(float fs, float f0, float Q = 0.707f);
    void initBandpass(float fs, float f0, float Q = 0.707f);
    float process(float x0);

private:
    float b0_coef, b1_coef, b2_coef, a1_coef, a2_coef;
    float prev_x1, prev_x2, prev_y1, prev_y2;
};

// Initialisiert die globalen Filter für 44.1 kHz Lowpass und 16 kHz Bandpass
enum : bool { FILTERS_INITIALIZED = false };
void initFilters();

// Verarbeitet ein mono 16-Bit Audio-Buffer bei 44.1 kHz und liefert einen
// neu allokierten int16_t-Buffer bei 16 kHz zurück 
void processAudioBuffer(const int16_t* inputBuf, size_t inLen, int16_t outBuf[],size_t& outLen);

#endif // AUDIOFILTER_H