#pragma once

#include <Arduino.h>

#define VERSION 1
const uint16_t version = VERSION;

// Occupied PINS by Teensy Audio Board are 18,19,20,21,23,6,7,8,10,11,12,13
// free pins are 0,1,2,3,4,5,9,14,15,16,17,22
// LEDs 
#define LED_RECORDING_PIN 2 // red
#define LED_LISTENING_PIN 3 // green
#define LED_COMMS_PIN     4 // yellow

#define LOGSerial Serial1
#define LOGSerial_RX_PIN 0
#define LOGSerial_TX_PIN 1

// Working Mode
#define LISTENING_MODE 0
#define RECORDING_MODE 1
#define TRANSFER_MODE  2

