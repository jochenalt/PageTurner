
#include <Arduino.h>
#include "model.h"
#include "constants.h"

void ModelConfigDataType::println(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    Serial.println(s);
};

void ModelConfigDataType::print() {
    for (int i = 0;i<MAX_NETWORKS;i++) {
        println("%i. wifi SSID %s", i,storedNetworks[i].ssid,storedNetworks[i].pass);
    }
}; 
        

void ModelConfigDataType::setup() {
    for (int i = 0;i<MAX_NETWORKS;i++) {
        storedNetworks[i].ssid[0] = 0;
        storedNetworks[i].pass[0] = 0;
    }
    networkPointer = 0;
    serialNo[0]  = 0;
}

void ModelConfigDataType::addNetwork(const char* ssid, const char* pass) {

  // Store new network
  strncpy(storedNetworks[networkPointer].ssid, ssid, WIFI_CREDENTIAL_LEN);
  strncpy(storedNetworks[networkPointer].pass, pass, WIFI_CREDENTIAL_LEN);
  
  printf("Saved: %s (Slot %d)\n", ssid, networkPointer);
  networkPointer = (networkPointer+1) % MAX_NETWORKS;
}

