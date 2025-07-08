#include <Arduino.h>
#include "bleturn.h"


#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

NimBLEServer *pServer;
NimBLEHIDDevice *hid;
NimBLECharacteristic *input;

void initBLE() {
  NimBLEDevice::init("Tiny Turner");

  pServer = NimBLEDevice::createServer();
  hid = new NimBLEHIDDevice(pServer);

  // Required HID setup
  hid->setManufacturer("ESP32");
  hid->setPnp(0x02, 0xe502, 0xa111, 0x0210); // Vendor ID, Product ID, etc.
  hid->setHidInfo(0x00, 0x02); // Country code, flags

  // Set HID Report Map (Keyboard)
  static const uint8_t reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0
  };
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));

  // Start HID services
  hid->startServices();

  // Start advertising
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->getHidService()->getUUID());
  pAdvertising->start();

  Serial.println("BLE HID Keyboard Ready!");
}

void sendKey(uint8_t key) {
  uint8_t msg[] = {0x00, 0x00, key, 0x00, 0x00, 0x00, 0x00, 0x00};
  input = hid->getInputReport(1); // Report ID 1 for Keyboard
  input->setValue(msg, sizeof(msg));
  input->notify();

  // Release key
  uint8_t release[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  input->setValue(release, sizeof(release));
  input->notify();
}

void sendPageUp() {
  sendKey(0x4B); // HID code for Page Up
  Serial.println("Sent: PAGE UP");
}

void sendPageDown() {
  sendKey(0x4E); // HID code for Page Down
  Serial.println("Sent: PAGE DOWN");
}
