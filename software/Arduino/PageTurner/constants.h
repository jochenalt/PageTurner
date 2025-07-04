#pragma once

#include <Arduino.h>

void println(const char* format, ...);
void print(const char* format, ...);


#define VERSION 2
const uint16_t version = VERSION;

// Occupied pins by Teensy Audio Board are 18,19,20,21,23,6,7,8,10,11,12,13
// free pins are 0,1,2,3,4,5,9,14,15,16,17,22

// Pins used by audio board     according teensy 4.0 pin
// I2S MCLK	                    23
// I2S BCLK	                    21
// I2S LRCLK	                  20
// I2S TX (to SGTL5000)	         7
// I2S RX (from SGTL5000)	       8
// I2C SDA	                    18
// I2C SCL	                    19
// SD Card CS (optional)	      10
// SD Card MOSI	                11
// SD Card MISO	                12
// SD Card SCK	                13

// try a compile-time check that we do not use a pin the audio board already occupies
#define IS_AUDIO_BOARD_PIN(pin) \
  ((pin) == 7 || (pin) == 8 || (pin) == 10 || (pin) == 11 || (pin) == 12 || \
   (pin) == 13 || (pin) == 18 || (pin) == 19 || (pin) == 20 || (pin) == 21 || (pin) == 23)

// Check if a pin is safe to use
#define SAFE_PIN_USE(pin) static_assert(!IS_AUDIO_BOARD_PIN(pin), "Pin is reserved by Teensy 4.0 Audio Board!")

// declare a pin , and check that it is not being used by the Teensy audio board
#define DECLARE_SAFE_PIN(name, number) \
  constexpr int name = number; \
  static_assert(!IS_AUDIO_BOARD_PIN(number), "Pin " #number " is reserved by Teensy 4.0 Audio Board!")


  // Log output 
#define LOGSerial Serial1
DECLARE_SAFE_PIN(LOGSerial_RX_PIN,0);
DECLARE_SAFE_PIN(LOGSerial_TX_PIN,1);

// LEDs 
DECLARE_SAFE_PIN(LED_PAGE_UP,3);
DECLARE_SAFE_PIN(LED_PAGE_DOWN,2);
DECLARE_SAFE_PIN(LED_RECORDING_PIN,4);
DECLARE_SAFE_PIN(LED_ON_PIN,22);


// Switch on/off
DECLARE_SAFE_PIN(SWITCH_ON_OFF_PIN,15); // if grounded turn me off

// Button starting the recording
DECLARE_SAFE_PIN(REC_BUTTON_PIN,5);

// Bad-AI Button
DECLARE_SAFE_PIN(BAD_AI_BUTTON_PIN,14); 

// buttons are debounced by 50ms
const unsigned long debounceDelay = 50; 

// Button starting the metronome's LED
DECLARE_SAFE_PIN(METRONOME_LED_PIN,9); 

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

// bluetooth LE for sending HID commands
#define FACTORYRESET_ENABLE         false
#define MINIMUM_FIRMWARE_VERSION    "0.6.6" 
#define BUFSIZE                        128   // Size of the read buffer for incoming data
#define VERBOSE_MODE                   true  // If set to 'true' enables debug output
#define BLUEFRUIT_HWSERIAL_NAME      Serial4 // connection to bluetooth module via hardware Serial4
DECLARE_SAFE_PIN(BLUEFRUIT_RX,16);
DECLARE_SAFE_PIN(BLUEFRUIT_TX,17);
#define BLUETOOTH_BAUDRATE 115200


#define ANALOG_WRITE_MAX 255