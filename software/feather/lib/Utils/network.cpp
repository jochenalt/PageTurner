#include "network.h"
#include "EEPROMStorage.h"
#include "WifiManager.h"
#include <HTTPClient.h>

WiFiManager wm;

bool tryKnownNetworks() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (strlen(config.model.storedNetworks[i].ssid) == 0) continue;
    
    println("Trying: %s", config.model.storedNetworks[i].ssid);
    WiFi.begin(config.model.storedNetworks[i].ssid, config.model.storedNetworks[i].pass);
    
    if (WiFi.waitForConnectResult(10000) == WL_CONNECTED) {
      persConfig.writeConfig();
      println("Connected to: %s", WiFi.SSID().c_str());
      return true;
    }
  }
  return false;  // No networks connected
}

void startCaptivePortal() {

  char serial_field[WIFI_CREDENTIAL_LEN] = "";

  WiFiManagerParameter custom_serial("serial", "SERIAL",serial_field, WIFI_CREDENTIAL_LEN);
  if (config.model.serialNo[0] == 0) {
    WiFiManagerParameter custom_text("<p>Enter the serial number of your Tiny Turner</p>");
    wm.addParameter(&custom_text);

    wm.addParameter(&custom_serial);
  } else {
    String s = String("<p>Your serial number is ") + String(config.model.serialNo) + String("</p>");
    WiFiManagerParameter custom_text(s.c_str());
  }
  // Custom SSID input field
  // char ssid_field[WIFI_CREDENTIAL_LEN] = "";
  // WiFiManagerParameter custom_ssid("ssid", "SSID", ssid_field, WIFI_CREDENTIAL_LEN);
  // wm.addParameter(&custom_ssid);

  // Custom password field
  // char pass_field[WIFI_CREDENTIAL_LEN] = "";
  // WiFiManagerParameter custom_pass("pass", "Password", pass_field, WIFI_CREDENTIAL_LEN);
  // wm.addParameter(&custom_pass);

  // Start portal with 3min timeout
  bool connectSuccess = wm.startConfigPortal("TinyTurner Setup");
  if (!connectSuccess) {
    println("Failed to connect, restarting.");
    ESP.restart();
  }

  // Save new credentials
  config.model.addNetwork(WiFi.SSID().c_str(), WiFi.psk().c_str());
  println("adding new network is %s pw=%s",WiFi.SSID().c_str(), WiFi.psk().c_str());
  if (config.model.serialNo[0] == 0) {
      strncpy(config.model.serialNo, custom_serial.getValue(), WIFI_CREDENTIAL_LEN);
  }
  persConfig.writeConfig();
}

void setupNetwork() {
  // Try to connect to known networks
  bool connected = tryKnownNetworks();
  
  // Fallback to captive portal if no connection
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
