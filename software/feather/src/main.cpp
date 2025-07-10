// SPDX-FileCopyrightText: 2023 Liz Clark for Adafruit Industries
//
// SPDX-License-Identifier: MIT
//
// Adafruit Battery Monitor Demo
// Checks for MAX17048 or LC709203F

#include <Arduino.h>
#include "battery.h"
#include "constants.h"
#include "network.h"
#include "EEPROMStorage.h"
#include "terminal.h"

// Operating Modes
enum ModeType { MODE_NONE, MODE_PRODUCTION, MODE_RECORDING, MODE_STREAMING };
ModeType mode = MODE_PRODUCTION;                                 // current operating mode


// last voltage measured
float cellVoltage, cellPercentage;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);    // wait until serial monitor opens

  // initialise the on-board battery monitor
  initBatteryMonitor();

  // check memory
  uint32_t flashMem = ESP.getFlashChipSize();
  uint32_t PSRAMMem = ESP.getPsramSize();

  // initialise EPPROM
  persConfig.setup();
  
  // show memory situation
  println("Flash  Size: %d KB", flashMem / (1024));
  println("PSRAM  Size: %d KB", PSRAMMem / (1024));
  println("EEPROM Size: %d B",  EEPROM.length());
 
  // use the blinking LED
  pinMode(LED_RECORDING_PIN, OUTPUT);

  // print content of EEPROM
  config.model.print();
 
  // set up the Wifi
  setupNetwork();
}


// let the recording light lethargically blink
void updateModeLED() {
  if ((mode == MODE_RECORDING) || (mode == MODE_STREAMING)) {
    analogWrite(LED_RECORDING_PIN, ANALOG_WRITE_MAX);
  }
  else 
    if (mode == MODE_PRODUCTION) {
      // lethargic blinking, like an "ON AIR" sign
      int val = ((2000 - millis() % 2000) * ANALOG_WRITE_MAX) / 2000;
      analogWrite(LED_RECORDING_PIN, val);
   } else {
      analogWrite(LED_RECORDING_PIN,0 );
   }
}

void loop() {
  // measure the battery 
  readBatMonitor(cellVoltage, cellPercentage);

  // Process any manual serial commands
  executeManualCommand();

  // let it blink depending on the mode
  updateModeLED();
}
