#pragma once

#include <Arduino.h>

// Changes the baudrate of the Bluefruit board to 115200 baud.
// Must be called first, before initBluetooth.
// A delay of 100ms is required afterwards to allow the Bluetooth board to adjust.
void initBluetoothBaudrate();

// Initialise the Adafruit Bluefruit module as a HID keyboard.
// Assumes initBluetoothBaudrate was called at least 100ms earlier.
void initBluetooth();

// Send a HID key via Bluetooth.
void sendBluetoothKey(uint16_t key);

// To be called regularly in loop(); releases a pressed key after 100ms.
void updateBluetoothRelease();
