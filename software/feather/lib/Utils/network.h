#include <Arduino.h>


#define CMD_DEVICE_INFORMATION 0xA1

void setupNetwork();
void startCaptivePortal();
bool sendDevice();
