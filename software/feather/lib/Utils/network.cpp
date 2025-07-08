#include "network.h"
#include "EEPROMStorage.h"
#include "WifiManager.h"
#include <HTTPClient.h>

WiFiManager wm;

bool tryKnownNetworks() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (strlen(config.model.storedNetworks[i].ssid) == 0) continue;
    
    Serial.printf("Trying: %s\n", config.model.storedNetworks[i].ssid);
    WiFi.begin(config.model.storedNetworks[i].ssid, config.model.storedNetworks[i].pass);
    
    if (WiFi.waitForConnectResult(10000) == WL_CONNECTED) {
      persConfig.writeConfig();
      Serial.printf("Connected to: %s\n", WiFi.SSID().c_str());
      return true;
    }
  }
  return false;  // No networks connected
}

void startCaptivePortal() {
  WiFiManagerParameter custom_text("<p>Enter new WiFi credentials</p>");
  wm.addParameter(&custom_text);

  char serial_field[WIFI_CREDENTIAL_LEN] = "";
  WiFiManagerParameter custom_serial("serial", "SERIAL",serial_field, WIFI_CREDENTIAL_LEN);
  if (config.model.serialNo[0] == 0) {
    wm.addParameter(&custom_serial);
  }
  // Custom SSID input field
  char ssid_field[WIFI_CREDENTIAL_LEN] = "";
  WiFiManagerParameter custom_ssid("ssid", "SSID", ssid_field, WIFI_CREDENTIAL_LEN);
  wm.addParameter(&custom_ssid);

  // Custom password field
  char pass_field[WIFI_CREDENTIAL_LEN] = "";
  WiFiManagerParameter custom_pass("pass", "Password", pass_field, WIFI_CREDENTIAL_LEN);
  wm.addParameter(&custom_pass);

  // Start portal with 3min timeout
  if (!wm.startConfigPortal("TinyTurner Setup")) {
    println("Failed to connect, restarting.");
    ESP.restart();
  }

  // Save new credentials
  config.model.addNetwork(custom_ssid.getValue(), custom_pass.getValue());

  if (config.model.serialNo[0] == 0) {
      strncpy(config.model.serialNo, custom_serial.getValue(), WIFI_CREDENTIAL_LEN);
  }
  persConfig.writeConfig();
}

void setupNetwork() {
  // 1. Try to connect to known networks
  bool connected = tryKnownNetworks();
  
  // 2. Fallback to captive portal if no connection
  if (!connected) {
    startCaptivePortal();
  }

}


void sendAudioToServer(int16_t buffer[], size_t bufferSize) {
   // Send via HTTP POST
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/octet-stream");
    int code = http.POST((uint8_t*)buffer, bufferSize*2);
    if (code > 0) {
      Serial.printf("✔ POST ’%s’ → %d\n", serverUrl, code);
    } else {
      Serial.printf("✖ POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  } else {
    Serial.println("✖ WiFi disconnected");
  }
}
