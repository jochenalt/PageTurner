#include <Audio.h>
#include "AudioTools.h"
#include "constants.h" 
#include "EEPROMStorage.h"

// Audio signal routing
AudioInputI2S        i2s_input;            // Audio from MAX9814 via Audio Shield LINE IN
AudioOutputI2S       audioOutput;          // To headphone output
AudioRecordQueue     recorder;             // Record 2s snippets

// Use simple (low latency) Biquad filter 2nd order to create a bandpass for speech (300–3400 Hz) at 12dB
AudioFilterBiquad    lowPass;
AudioFilterBiquad    highPass;

AudioConnection      patchMicToRecord1Cord1(i2s_input, 0, lowPass, 0);
AudioConnection      patchMicToRecord2(lowPass, 0, highPass, 0);
AudioConnection      patchMicToRecord3(highPass, 0, recorder, 0);   // Record the left line-in channel

AudioControlSGTL5000 audioShield;

AudioPlayMemory      clickPlayer;
AudioConnection      patchMetronomToHeadphoneL(clickPlayer, 0, audioOutput, 0);
AudioConnection      patchMetronomToHeadphoneR(clickPlayer, 0, audioOutput, 1);

// Biquad filter class
class BiQuad {
public:
    BiQuad();
    void initLowpass(float fs, float f0, float Q = 0.707f);
    void initBandpass(float fs, float f0, float Q = 0.707f);
    void initHighpass(float fs, float f0, float Q);
    float process(float x0);

private:
    float b0_coef, b1_coef, b2_coef, a1_coef, a2_coef;
    float prev_x1, prev_x2, prev_y1, prev_y2;
};


// Static instances of filters
static BiQuad lpFilterIn;
static BiQuad bpFilter;
static BiQuad highPassFilterOut;
static BiQuad lowPassFilterOut;

BiQuad::BiQuad()
    : b0_coef(0), b1_coef(0), b2_coef(0), a1_coef(0), a2_coef(0),
      prev_x1(0), prev_x2(0), prev_y1(0), prev_y2(0) {}

void BiQuad::initLowpass(float fs, float f0, float Q) {
    float w0 = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 = (1 - cosw0) / 2;
    float b1 = 1 - cosw0;
    float b2 = (1 - cosw0) / 2;
    float a0 = 1 + alpha;
    float a1 = -2 * cosw0;
    float a2 = 1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

void BiQuad::initBandpass(float fs, float f0, float Q) {
    float w0 = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 = alpha;
    float b1 = 0;
    float b2 = -alpha;
    float a0 = 1 + alpha;
    float a1 = -2 * cosw0;
    float a2 = 1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

void BiQuad::initHighpass(float fs, float f0, float Q) {
    float w0 = 2 * PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2 * Q);

    float b0 = (1 + cosw0) / 2;
    float b1 = -(1 + cosw0);
    float b2 = (1 + cosw0) / 2;
    float a0 = 1 + alpha;
    float a1 = -2 * cosw0;
    float a2 = 1 - alpha;

    b0_coef = b0 / a0;
    b1_coef = b1 / a0;
    b2_coef = b2 / a0;
    a1_coef = a1 / a0;
    a2_coef = a2 / a0;

    prev_x1 = prev_x2 = prev_y1 = prev_y2 = 0;
}

float BiQuad::process(float x0) {
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



void processAudioBuffer(const int16_t* inputBuf, size_t inLen, int16_t outBuf[], size_t& outLen) {
    const float ratio = 16000.0f / 44100.0f;
    outLen = size_t(ratio * inLen + 0.5f);
    outLen = min(outLen, (size_t)OUT_SAMPLES);

    // Resample → 16 kHz with 3-tap smoothing
    for (size_t j = 0; j < outLen; j++) {
        float pos = j / ratio;
        int i0 = int(floor(pos));

        // 3-tap average instead of linear interpolation
        float sum = 0, cnt = 0;
        if (i0 > 0)              { sum += inputBuf[i0 - 1] / 32768.0f; cnt += 1; }
        if (i0 < int(inLen))     { sum += inputBuf[i0]     / 32768.0f; cnt += 1; }
        if (i0 + 1 < int(inLen)) { sum += inputBuf[i0 + 1] / 32768.0f; cnt += 1; }

        float downsampled = sum / cnt;

        // Bandpass 300–3400Hz @ 16 kHz and convert back to int16
        float y = lowPassFilterOut.process(highPassFilterOut.process(downsampled));
        float scaled = y * 32767.0f;
        if (scaled >  32767.0f) scaled =  32767.0f;
        if (scaled < -32767.0f) scaled = -32767.0f;
        outBuf[j] = int16_t(scaled);
    }
}

void initAudioTools() {
    AudioMemory(280);   // Allocate audio processing memory. Without Metronome, only 80 is required

    // Initialise speech bandpass filter (300–3400 Hz)
    // Butterworth value Q defines “peakedness” or damping of filter's transition band
    lowPass.setLowpass(0, 3400, 0.707);  // Channel, frequency (Hz), Q
    highPass.setHighpass(0, 300, 0.707);

    audioShield.enable();
    audioShield.unmuteHeadphone();
    audioShield.adcHighPassFilterEnable();
    audioShield.inputSelect(AUDIO_INPUT_LINEIN);           // Use line-in (MAX9814)
    audioShield.volume(0.8);                               // Headphone volume (0.0–1.0)
    audioShield.lineInLevel(config.model.gainLevel);       // Line-in gain (0–15)
    audioShield.dacVolume(0.8);

    // Enable filters for real-time audio processing pipeline
    lpFilterIn.initLowpass(INPUT_SAMPLE_RATE, 8000.0f, 0.707f);
    bpFilter.initBandpass(OUTPUT_SAMPLE_RATE, 1850.0f, 0.707f);
    highPassFilterOut.initHighpass(OUTPUT_SAMPLE_RATE, 300, 0.707f);
    lowPassFilterOut.initLowpass(OUTPUT_SAMPLE_RATE, 3400, 0.707f);

    recorder.clear();
    recorder.begin();
}

void set_gain_level(uint16_t gain) {
    audioShield.lineInLevel(config.model.gainLevel);   // Line-in gain (0–15)
}

void clearAudioBuffer() {
    recorder.clear();
}

int isAudioDataAvailable() {
    return recorder.available();
}

int16_t* recorderReadBuffer() {
    return recorder.readBuffer();
}

void recorderFreeBuffer() {
    recorder.freeBuffer();
}

void drainAudioData(int16_t audio_in_buffer[], size_t &added_samples) {
    added_samples = 0;
    while (recorder.available()) {
        int16_t* block = recorder.readBuffer();

        // Shift audio buffer and add new samples at end
        uint16_t samples = AUDIO_BLOCK_SAMPLES;
        memmove(audio_in_buffer, audio_in_buffer + samples, (RAW_SAMPLES - samples) * sizeof(audio_in_buffer[0]));
        memcpy(audio_in_buffer + (RAW_SAMPLES - samples), block, samples * sizeof(audio_in_buffer[0]));

        recorder.freeBuffer();
        added_samples += samples;
    }
}
