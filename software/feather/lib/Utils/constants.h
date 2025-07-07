#pragma once

#include <Arduino.h>

void println(const char* format, ...);
void print(const char* format, ...);


#define VERSION 3
const uint16_t version = VERSION;

// === Configuration ===
#define MAX_NETWORKS 3       // Max stored networks
#define WIFI_CREDENTIAL_LEN  32      // Allocate EEPROM space


// the model is stored on the SD card of the teensy audio board
#define SD_SPI_CS 10
#define SD_SPI_MOSI 11
#define SD_SPI_MISO 12
#define SD_SPI_SCK 13
#define SDCARD_CS_PIN SD_SPI_CS

// --- Packet Framing Constants ---
#define PACKET_MAGIC_HI 0xAB
#define PACKET_MAGIC_LO 0xCD
#define PACKET_MAX_PAYLOAD 512

// commands in the header of communication packets 
#define CMD_AUDIO_RECORDING  0xA1       // 1s of audio recording being sent to pyhton
#define CMD_AUDIO_SAMPLE     0xA2       // total number of audio samples (a 2 bytes) being sent in the CMD_AUDIO_SNIPPET command.
#define CMD_AUDIO_STREAM     0xA3       // audio snippet of a permanend audio stream

// parameter for recording
#define RECORD_SECONDS 1
#define INPUT_SAMPLE_RATE  44100 // [Hz]
#define OUTPUT_SAMPLE_RATE 16000 // [Hz]
#define BYTES_PER_SAMPLE 2
#define RAW_SAMPLES      (RECORD_SECONDS * INPUT_SAMPLE_RATE)   // 44100
#define OUT_SAMPLES      (RECORD_SECONDS * OUTPUT_SAMPLE_RATE)  // 16000


