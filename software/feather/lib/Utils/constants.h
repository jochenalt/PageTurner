#pragma once

#include <Arduino.h>

void println(const char* format, ...);
void print(const char* format, ...);


#define VERSION 4
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

// network
extern const char* serverUrl;
