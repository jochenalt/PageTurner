#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "constants.h"

// EEPROM structure (each network = 64 bytes)
typedef struct {
  char ssid[WIFI_CREDENTIAL_LEN];
  char pass[WIFI_CREDENTIAL_LEN];
} WifiCredential;

// contains data stored in EPPROM
struct ModelConfigDataType {
  void print();
  void setup();

  void println(const char* format, ...);

  void addNetwork(const char* ssid, const char* pass);

  WifiCredential storedNetworks[MAX_NETWORKS];
  uint8_t nextNewNetwork;
  char serialNo[WIFI_CREDENTIAL_LEN];
}; 