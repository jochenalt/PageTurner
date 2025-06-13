#pragma once

#include <Arduino.h>

#define VERSION 2
const uint16_t version = VERSION;

// Occupied PINS by Teensy Audio Board are 18,19,20,21,23,6,7,8,10,11,12,13
// free pins are 0,1,2,3,4,5,9,14,15,16,17,22
// LEDs 
#define LED_LISTENING_PIN 2 // green
#define LED_COMMS_PIN     3 // yellow
#define LED_RECORDING_PIN 4 // red

// Button starting the recording
#define REC_BUTTON_PIN     5 

#define LOGSerial Serial1
#define LOGSerial_RX_PIN 0
#define LOGSerial_TX_PIN 1

// Working Mode
#define LISTENING_MODE 0
#define RECORDING_MODE 1
#define TRANSFER_MODE  2

// the model is stored on the SD card of the teensy audio board
#define SD_SPI_CS 10
#define SD_SPI_MOSI 11
#define SD_SPI_MISO 12
#define SD_SPI_SCK 13

// commands in the header of communication packets 
#define CMD_AUDIO_SNIPPET    0xA1
#define CMD_SAMPLE_COUNT     0xA2