#include <Audio.h>
#include "AudioTools.h"
#include "constants.h" 
#include "EEPROMStorage.h"

// Audio signal routing
AudioInputI2S        i2s_input;            // Audio from MAX9814 via Audio Shield LINE IN
AudioOutputI2S       audioOutput;          // To headphone output
AudioRecordQueue     recorder;             // Record 2s snippets

// Use simple (low latency) Biquad filter 2nd order to create a bandpass for speech (300–3400 Hz) at 12dB
AudioFilterBiquad    lowPass1;
AudioFilterBiquad    lowPass2;

AudioFilterBiquad    highPass1;
AudioFilterBiquad    highPass2;

AudioConnection      patchMicToRecord1Cord1(i2s_input, 0, lowPass1, 0);
AudioConnection      patchMicToRecord1Cord2(lowPass1, 0, lowPass2, 0);
AudioConnection      patchMicToRecord1Cord3(lowPass2, 0, highPass1, 0);
AudioConnection      patchMicToRecord1Cord4(highPass1, 0, highPass2, 0);

AudioConnection      patchFilteredMicToRecorder(highPass2, 0, recorder, 0);

AudioControlSGTL5000 audioShield;

AudioPlayMemory      clickPlayer;
AudioConnection      patchMetronomToHeadphoneL(clickPlayer, 0, audioOutput, 0);
AudioConnection      patchMetronomToHeadphoneR(clickPlayer, 0, audioOutput, 1);


void processAudioBuffer(const int16_t* inputBuf, size_t inLen, int16_t outBuf[], size_t& outLen) {
 // Fixed-point inkrement: ratio = 44100/16000, skaliert auf Q16
    const uint32_t inc = (uint32_t)(((uint64_t)44100 << 16) / 16000);
    uint32_t pos = 0;
    outLen = 0;

    // Solange wir noch Platz für inBuf[idx+1] haben
    while ((pos >> 16) + 1 < (uint32_t)inLen) {
        uint32_t idx  = pos >> 16;        // ganzzahliger Teil
        uint32_t frac = pos & 0xFFFF;     // Bruchteils-Teil (Q16)

        int32_t x0 = inputBuf[idx];
        int32_t x1 = inputBuf[idx + 1];
        // lineare Interpolation in Q16
        int32_t v = x0 + ((int32_t)(x1 - x0) * (int32_t)frac >> 16);

        // Clip auf int16-Bereich (optional, für Sicherheit)
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;

        outBuf[outLen++] = (int16_t)v;
        pos += inc;
    }

}

void initAudioTools() {
    AudioMemory(280);   // Allocate audio processing memory. Without Metronome, only 80 is required

    // Initialise speech bandpass filter (300–3400 Hz)
    // Butterworth value Q defines “peakedness” or damping of filter's transition band
    lowPass1.setLowpass(0, 3400, 0.707);  // Channel, frequency (Hz), Q
    lowPass2.setLowpass(0, 3400, 0.707);  // Channel, frequency (Hz), Q

    highPass1.setHighpass(0, 300, 0.707);
    highPass2.setHighpass(0, 300, 0.707);

    audioShield.enable();
    audioShield.unmuteHeadphone();
    audioShield.adcHighPassFilterDisable();
    audioShield.inputSelect(AUDIO_INPUT_LINEIN);           // Use line-in (MAX9814)
    audioShield.volume(0.8);                               // Headphone volume (0.0–1.0)
    audioShield.lineInLevel(config.model.gainLevel);       // Line-in gain (0–15)
    audioShield.dacVolume(0.8);
    
    recorder.clear();
    recorder.begin();
    while (recorder.available()) {
        recorder.readBuffer();
        recorder.freeBuffer();
    }

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
