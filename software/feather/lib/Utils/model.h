#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "constants.h"

// EEPROM structure (each network = 64 bytes)
typedef struct {
  char ssid[WIFI_CREDENTIAL_LEN];
  char pass[WIFI_CREDENTIAL_LEN];
  char backend[WIFI_CREDENTIAL_LEN];

} WifiCredential;

// contains data stored in EPPROM
struct ModelConfigDataType {
  void print();
  void setup();

  void println(const char* format, ...);
  void addNetwork(const char* ssid, const char* pass, const char* backend = NULL);

  // store 3 Wifi networks
  WifiCredential storedNetworks[MAX_NETWORKS];
  // which are assigned in round robin
  uint8_t nextNewNetwork;
  // the owner 
  char owner[WIFI_CREDENTIAL_LEN];
  
}; 