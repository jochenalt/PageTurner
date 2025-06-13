#include <Arduino.h>
#include <HardwareSerial.h>

#include <SerialProtocol.hpp>
#include <constants.hpp>
#include <EEPROMStorage.hpp>
#include <utils.hpp>
#include <constants.hpp>

#include <Audio.h>                                          // Audio Library for Audio Shield
#include <Wire.h>                                           // für Verbindung mit Audio Shield 
#include <SPI.h>                                            // 
#include <SD.h>                                             // The SD card contais the model 

bool commandPending = false;                                // true if command processor saw a command coming and is waiting for more input
String command;                                             // current command coming in
uint32_t commandLastChar_us = 0;                            // time when the last character came in (to reset command if no further input comes in) 
bool levelMeterOn = false;                                  // shows a peak meter 

// Audio signal routing
AudioAnalyzePeak     peak;                                  // Peak detector, just for show
AudioInputI2S        i2s_input;                             // Audio from MAX9814 via Audio Shield LINE IN
AudioOutputI2S       i2s_output;                            // To headphone output
AudioRecordQueue     recorder;                              // record 2s snippets

AudioConnection      patchCord1(i2s_input, 0, i2s_output, 0); // Left channel
// AudioConnection      patchCord2(i2s_input, 1, i2s_output, 1); // Right channel
AudioConnection      patchCord3(i2s_input, 0, peak, 0);       // Left → Peak detector
AudioConnection      patchCord4(i2s_input, 0, recorder, 0); // record the left line in channel
AudioControlSGTL5000 audioShield;

// parameter for recording
#define RECORD_SECONDS 2
#define SAMPLE_RATE 16000
#define BYTES_PER_SAMPLE 2
#define TOTAL_SAMPLES (RECORD_SECONDS * SAMPLE_RATE)
#define AUDIO_BLOCK_SAMPLES 128


void println(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    LOGSerial.println(s);
};


// Debounced recording button 
// Returns true if button state changed (press or release)
// Outputs current stable state in 'state' (LOW = pressed)
bool checkButton(bool& state) {
  const unsigned long debounceDelay = 50; // debounce time in [s]

  static bool lastButtonReading = HIGH;
  static bool stableButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;

  bool reading = digitalRead(REC_BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      state = !stableButtonState;
      lastButtonReading = reading;
      return true;  // state has changed
    }
  }

  lastButtonReading = reading;
  return false;  // no change
}


void gainUp() {
  config.model.gainLevel++;
  if (config.model.gainLevel > 15)
     config.model.gainLevel = 15; 
  println("increase gain to %i",config.model.gainLevel);
     audioShield.lineInLevel(config.model.gainLevel);                 // Line-in gain (0-15)
}

void gainDown() {
  config.model.gainLevel--;
  if (config.model.gainLevel < 0)
     config.model.gainLevel = 0; 
  if (config.model.gainLevel > 15)
     config.model.gainLevel = 15; 
  println("decrease gain to %i",config.model.gainLevel);

  audioShield.lineInLevel(config.model.gainLevel);                 // Line-in gain (0-15)
}

// print nice help text and give the status
void printHelp() {
  println("Son of Jochen V%i", version);
  println("   h       - help");
  println("   s       - self test");
  println("   p       - print configuration");
  println("   r       - reset");
  println("   +       - increase gain Level");
  println("   -       - decrease gain Level");
  println("   l       - peak meter on");
  println("   L       - peak meter off");
};


// print nice help text and give the status
void printConfiguration() {

  config.model.print();
};


inline void addCmd(char ch) {
  if ((ch != 10) && (ch != 13))
	  command += ch;
	commandPending = true;
};

inline void emptyCmd() {
	command = "";
	commandPending = false;
};

void executeManualCommand() {
	// if the last key is too old, reset the command after 1s (command-timeout)
	if (commandPending && (micros() - commandLastChar_us) > 1000000) {
		emptyCmd();
	}
	
	// check for any input from Mainboard
	if (LOGSerial.available()) {
		// store time of last character, for timeout of command
		commandLastChar_us = micros();

		char inputChar = LOGSerial.read();
		switch (inputChar) {
			case 'h': {
				if (command == "")
					printHelp();
				else
					addCmd(inputChar);
				break;
      }
			case 'p': {
				if (command == "")
					printConfiguration();
				else
					addCmd(inputChar);
				break;
      }

 			case 'r': {
				if (command == "")
					setup();
				else
					addCmd(inputChar);
				break;
      }
 			case '+': {
				if (command == "")
					gainUp();
				else
					addCmd(inputChar);
				break;
      }
 			case '-': {
				if (command == "")
					gainDown();
				else
					addCmd(inputChar);
				break;
      }
 			case 'l': {
				if (command == "")
					levelMeterOn = true;
				else
					addCmd(inputChar);
				break;
      }
 			case 'L': {
				if (command == "")
					levelMeterOn = false;
				else
					addCmd(inputChar);
				break;
      }

      case 10:
			case 13:
				if (command.startsWith("b")) {
					  emptyCmd();
				} 
      default:
				addCmd(inputChar);
			} // switch
		} // if (Serial.available())
}

extern void setAudioSampleRate(unsigned int);
void setup() {
  pinMode(LED_LISTENING_PIN, OUTPUT);  
  pinMode(LED_RECORDING_PIN, OUTPUT);
  pinMode(LED_COMMS_PIN, OUTPUT);
  // pinMode(LED_BUILTIN, OUTPUT);
  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);

  for (int i = 0;i<4;i++) {
    if (i>0)
      delay(100);
    digitalWrite(LED_LISTENING_PIN,HIGH);
    delay(20);
    digitalWrite(LED_COMMS_PIN,HIGH);
    delay(20);
    digitalWrite(LED_RECORDING_PIN,HIGH);
    delay(20);
    // digitalWrite(LED_BUILTIN,HIGH);    
    delay(50);
    digitalWrite(LED_LISTENING_PIN,LOW);
    delay(20);
    digitalWrite(LED_COMMS_PIN,LOW);
    delay(20);
    digitalWrite(LED_RECORDING_PIN,LOW);
    delay(20);
    // digitalWrite(LED_BUILTIN,LOW);
    delay(20);

  }

  // initialise EEPROM
  persConfig.setup(); 

  LOGSerial.begin(115200);
  Serial.begin(115200);

  // Enable the audio shield+
  AudioMemory(60);                                      // Allocate audio processing memory
  audioShield.enable();
  audioShield.adcHighPassFilterDisable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);          // Use line-in (for MAX9814)
  audioShield.volume(0.3);                              // Headphone volume 0.0 - 1.0
  audioShield.lineInLevel(config.model.gainLevel);      // Line-in gain (0-15)
  audioShield.dacVolume(1.0);

  println("Son of Jochen V%i - h for help", version);
};

void loop() {
  static bool recording = false;
  static int recordingSampleIndex = 0;
  static int16_t audioBuffer[TOTAL_SAMPLES];


  // feed the watch dog

  // everybody loves a blinking LED
  /*
  static uint32_t blink_ms = 0;
  static bool blinkerOn = false;
  if (now_ms > blink_ms + (blinkerOn?950:50)) {
    digitalWrite(LED_BUILTIN,blinkerOn?HIGH:LOW);
    blinkerOn = !blinkerOn;
    blink_ms = now_ms;
  }
    */

  // check if the recording button has been pushed
  bool buttonState;                             // state after the action, true = pushed
  bool buttonChange = checkButton(buttonState); // true if turned off or turned on
  if (buttonChange) {
    if (buttonState) {
      digitalWrite(LED_RECORDING_PIN, HIGH);
      recording = true;
      recorder.clear();
      recorder.begin();
      recordingSampleIndex = 0;
      LOGSerial.println("start recording");
    };
  }

  // during recording, add everything you got to the audio buffer
  if (recording && recorder.available()) {

    while (recorder.available()) {
      if (recordingSampleIndex + AUDIO_BLOCK_SAMPLES > TOTAL_SAMPLES) {
        LOGSerial.println("⚠️ Audio buffer overflow");
        recorder.freeBuffer();
        continue;
      }

      int16_t* block = (int16_t*)recorder.readBuffer();
      size_t toCopy = min((size_t)(TOTAL_SAMPLES - recordingSampleIndex), (size_t)AUDIO_BLOCK_SAMPLES);
      memcpy(audioBuffer + recordingSampleIndex, block, toCopy * sizeof(int16_t));
      recordingSampleIndex += toCopy;
      recorder.freeBuffer();
    }

    if (recordingSampleIndex >= TOTAL_SAMPLES) {
      recording = false;
      recorder.end();

      // turn off LED just in case 
      digitalWrite(LED_RECORDING_PIN, LOW);

      // now send to PC
      digitalWrite(LED_COMMS_PIN, HIGH);
      
      // Send audio data to PC
      size_t totalBytes = recordingSampleIndex * BYTES_PER_SAMPLE;
      send_packet(Serial, CMD_AUDIO_SNIPPET, (uint8_t*)audioBuffer, totalBytes);

      // Send sample count
      uint32_t sampleCount = recordingSampleIndex;
      send_packet(Serial, CMD_SAMPLE_COUNT, (uint8_t*)&sampleCount, sizeof(sampleCount));

      Serial.flush();

      digitalWrite(LED_COMMS_PIN, LOW);
      println("recording of %i samples %u bytes sent", recordingSampleIndex, totalBytes);
    }
  }

  // Check every 100ms
  if (levelMeterOn) {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint >= 100) {
      lastPrint = millis();
      if (peak.available()) {
        float peakLevel = peak.read();
        int bar = peakLevel * 50.;  // scale 0.0–1.0 to 0–50
        LOGSerial.print("Peak: ");
        for (int i = 0; i < bar; i++) 
          LOGSerial.print("#");
        LOGSerial.println();
      }
    }
  }

  // react on manual input from Serial 
  executeManualCommand();

}
