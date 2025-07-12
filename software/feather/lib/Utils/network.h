#include <Arduino.h>

void setupNetwork();
void startCaptivePortal();
bool sendDevice();
bool sendAudioSnippet(int16_t audioBuffer[], size_t samples);
