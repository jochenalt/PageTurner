#include "network.h"
#include "EEPROMStorage.h"
#include "WifiManager.h"
#include <HTTPClient.h>

WiFiManager wm;
String serverUrl;

const char* hostname = "www.tiny-turner.com";  // Your domain
const char* fallbackIP = "192.168.178.80";       // Local server IP

bool tryKnownNetworks() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (strlen(config.model.storedNetworks[i].ssid) == 0) continue;
    
    println("Trying: %s", config.model.storedNetworks[i].ssid);
    WiFi.begin(config.model.storedNetworks[i].ssid, config.model.storedNetworks[i].pass);
    
    if (WiFi.waitForConnectResult(10000) == WL_CONNECTED) {
      persConfig.writeConfig();
      println("Connected to: %s", WiFi.SSID().c_str());
      serverUrl = config.model.storedNetworks[i].backend;

      IPAddress ip;
      if (WiFi.hostByName(hostname, ip)) {
        serverUrl="http://" + ip.toString() + ":8000";
      }
      else {
        println("using fallback ip %s", fallbackIP);
        serverUrl= "http://" + String(fallbackIP) + ":8000";
      }
      
      return true;
    }
  }
  return false;  // No networks connected
}

void startCaptivePortal() {

  char serial_field[WIFI_CREDENTIAL_LEN] = "";
  char backend_field[WIFI_CREDENTIAL_LEN] = "";

  WiFiManagerParameter custom_serial("serial", "SERIAL",serial_field, WIFI_CREDENTIAL_LEN);
  WiFiManagerParameter custom_backend("backend", "BACKEND",backend_field, WIFI_CREDENTIAL_LEN);

  if (config.model.owner[0] == 0) {
    WiFiManagerParameter custom_text("<p>Enter the serial number of your Tiny Turner</p>");
    wm.addParameter(&custom_text);
    wm.addParameter(&custom_serial);
    wm.addParameter(&custom_backend);

  } else {
    String s = String("<p>Your serial number is ") + String(config.model.owner) + String("</p>");
    WiFiManagerParameter custom_text(s.c_str());
    wm.addParameter(&custom_text); 
  }

  // Start portal with 3min timeout
  bool connectSuccess = wm.startConfigPortal("TinyTurner Setup");
  if (!connectSuccess) {
    println("Failed to connect, restarting.");
    ESP.restart();
  }

  // Save new credentials
  config.model.addNetwork(WiFi.SSID().c_str(), WiFi.psk().c_str());
  println("adding new network is %s pw=%s",WiFi.SSID().c_str(), WiFi.psk().c_str());
  if (config.model.owner[0] == 0) {
      strncpy(config.model.owner, custom_serial.getValue(), WIFI_CREDENTIAL_LEN);

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


bool sendToServer(const char* path, uint8_t command, uint8_t* buffer, size_t bufferSize) {
  if (WiFi.status() != WL_CONNECTED) {
    println("WiFi disconnected");
    return false;
  }

  if (serverUrl == "") {
    println("backend server is unknown");
    return false;
  }

  HTTPClient http;
  String fullpath = serverUrl + path;
  http.begin(fullpath.c_str());
  http.addHeader("Content-Type", "application/octet-stream");
  
  // Create a new buffer with command prefix
  size_t totalSize = bufferSize + 1; // +1 for command byte
  uint8_t* payload = new uint8_t[totalSize];
  
  // First byte is the command
  payload[0] = command;
  
  // Copy the original buffer data
  memcpy(payload + 1, buffer, bufferSize);
  
  // Send the payload
  int code = http.POST(payload, totalSize);
  
  if (code > 0) {
    println("POST '%s' → %d\n", fullpath.c_str(), code);
  } else {
    println("POST failed:%s %s\n", fullpath.c_str(),http.errorToString(code).c_str());
  }
  
  // Clean up
  delete[] payload;
  http.end();

  return (code > 0);
}

bool sendToServer(String path, uint8_t command, String s) {
  return sendToServer(path.c_str(), command, (uint8_t*)s.c_str(), (size_t)s.length());
} 

bool sendDevice() {
  String device = String("") +
    " board:\"" + String(ARDUINO_BOARD) + "\"" +
    " flash:\"" + String(ESP.getFlashChipSize()) + "\"" +
    " psram:\"" + String(ESP.getPsramSize()) + "\"" +
    " freeheap:\"" + String(ESP.getFreeHeap()) + "\"" +
    " version:\"" + String(VERSION) + "\"" +
    " cpu:\"" + String(ESP.getCpuFreqMHz()) + "\"" +
    " cores:\"" + String(ESP.getChipCores()) + "\"" +
    " chip:\"" + String(ESP.getChipModel()) + "\"" +
    " owner:\"" + String(config.model.owner) + "\"" +
    " chipid:\"" + String(ESP.getEfuseMac(), HEX) + "\"";

  Serial.println("Sending device info: " + device); // Debug output
  return sendToServer("/api/device-info", CMD_DEVICE_INFORMATION, device);
}