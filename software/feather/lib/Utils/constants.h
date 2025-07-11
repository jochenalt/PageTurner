#pragma once

#include <Arduino.h>

void println(const char* format, ...);
void print(const char* format, ...);


#define VERSION 7
const uint16_t version = VERSION;

#if CONFIG_IDF_TARGET_ESP32S3
  #define BOARD_IS_FEATHER_S3
#elif CONFIG_IDF_TARGET_ESP32
  #define BOARD_IS_FEATHER_V2
#endif

// the Feather V2 has a battery Monitor pin 
// the feather S3 has a fuel gauge (MAX17048)
#ifdef  BOARD_IS_FEATHER_V2
#define VBAT_PIN A13
#endif

// onboard neopixel
#define NUMPIXELS      1     // Only one onboard NeoPixel

#define LED_RECORDING_PIN BUILTIN_LED
#define POWER_BUTTON_PIN 0


// for the analog pins: this is the max value
#define ANALOG_WRITE_MAX 255

// Inference
#define MAX_LABELS 10

// === Configuration ===
#define MAX_NETWORKS 3       // Max stored networks
#define WIFI_CREDENTIAL_LEN  32      // Allocate EEPROM space

// interface
#define REC_BUTTON_PIN 4    // recording/streaming button
#define LED_REC_PIN 5           // LED indicating recording or streaming

// audio
#define SAMPLE_RATE 16000
#define SAMPLES_IN_SNIPPET SAMPLE_RATE
#define BYTES_PER_SAMPLE  2       // bytes per sample

// backend url or IP address
extern String serverUrl;
