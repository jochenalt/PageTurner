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
  pinMode(LED_BUILTIN, OUTPUT);

  // print content of EEPROM
  config.model.print();
 
  // set up the Wifi
  setupNetwork();
}


void loop() {
  float cellVoltage, cellPercentage;
  readBatMonitor(cellVoltage, cellPercentage);
  println("voltage %.3fV percentage %.1f%", cellVoltage,cellPercentage);
  delay(2000);

  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
}
