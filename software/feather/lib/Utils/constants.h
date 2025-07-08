#pragma once

#include <Arduino.h>

void println(const char* format, ...);
void print(const char* format, ...);


#define VERSION 3
const uint16_t version = VERSION;

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
#define SAMPLES_IN_SNIPPET 16000


// network
const char* serverUrl = "http://192.168.1.100:8000/upload";
//const char* serverUrl = "http://tiny-turner.com:8000/upload";