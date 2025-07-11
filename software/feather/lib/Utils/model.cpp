
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
    println("Stored networks:");
    
    for (int i = 0;i<MAX_NETWORKS;i++) {
        println("   %i. wifi SSID %s", i,storedNetworks[i].ssid,storedNetworks[i].pass);
    }
    println("next new network gets in slot  %i",nextNewNetwork);
    println("owner is %s",owner);

}; 
        

void ModelConfigDataType::setup() {
    for (int i = 0;i<MAX_NETWORKS;i++) {
        storedNetworks[i].ssid[0] = 0;
        storedNetworks[i].pass[0] = 0;
    }
    nextNewNetwork = 0;
    owner[0]  = 0;


    // @TODO remove that!!!
    addNetwork("lorem ipsum dolor sit amet","7386801780590940", "172.20.101.7");
}

void ModelConfigDataType::addNetwork(const char* ssid, const char* pass, const char* backendIP) {
  // Store new network
  strncpy(storedNetworks[nextNewNetwork].ssid, ssid, WIFI_CREDENTIAL_LEN);
  strncpy(storedNetworks[nextNewNetwork].pass, pass, WIFI_CREDENTIAL_LEN);
  if (backendIP != NULL) {
      strncpy(storedNetworks[nextNewNetwork].backend, backendIP, WIFI_CREDENTIAL_LEN);
  } else {
      strncpy(storedNetworks[nextNewNetwork].backend, "www.tiny-turner.com", WIFI_CREDENTIAL_LEN);
  }
  
  nextNewNetwork = (nextNewNetwork+1) % MAX_NETWORKS;
  printf("Saved network ""%s""/""%s""/%s (Slot %d)\n", ssid, pass, backendIP, nextNewNetwork);
}
