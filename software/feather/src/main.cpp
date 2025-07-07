
#include <Arduino.h>
#include <HardwareSerial.h>

#include "inference.h"

#include "network.h"
#include "EEPROMStorage.h"

// AudioInputI2S i2s;
// BLEHIDDevice hid;


void setup() {
  Serial.begin(115200);
  Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("PSRAM Size: %d MB\n", ESP.getPsramSize() / (1024 * 1024));

  // initialise EPPROM
  persConfig.setup();
 
  // initialise Wifi  
  setupNetwork();

  // initialise Audio
  // AudioSettings settings(16000, 16, 1); // 16kHz, 16-bit
  // i2s.begin(settings);

  // initialise Bluetooth
  // NimBLEDevice::init("Pageturner");
  // hid.startServices();

  println("page turner V%i", version);
}

void loop() {
  // Your Edge Impulse inference here
}