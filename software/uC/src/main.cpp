#include <Arduino.h>
#include <HardwareSerial.h>

#include <SerialProtocol.hpp>
#include <constants.hpp>
#include <EEPROMStorage.hpp>
#include <utils.hpp>
#include <constants.hpp>
#include <AudioFilter.hpp>

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

static int16_t audioBuffer[OUT_SAMPLES];  // full 16 kHz output
static const float ratio   = INPUT_RATE / (float)OUTPUT_RATE; // ≈2.75625
bool   recording = false;                  // true, if recording is happening

// State‐Variablen für den Eval Mode
static bool evalMode = false;
static bool  evalModeStartWaiting = 0;
static bool evalModeEndWaiting = false;
// counters
size_t outCount = 0;
static size_t  rawPosition = 0;           // total raw samples consumed so far

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

// Call this once per incoming block from audio board:
// Takes a mono 16-Bit Audio-Buffer samples at 44.1 kHz and creates a 
// newly allocated int16_t-buffer sampled at 16kHz. In between it does:
//    - Lowpass at  44.1 kHz
//    - Lineares Resampling auf 16 kHz 
//    - Bandpass  IIR-Bandpass (300–3400 Hz) bei 16 kHz and conversion in int16
// und liefert einen
// neu allokierten int16_t-Buffer bei 16 kHz zurück (muss vom Aufrufer gelöscht werden)
void processBlock(const int16_t* block, size_t blockLen) {
  size_t produced;
  static  int16_t filteredBuf[AUDIO_BLOCK_SAMPLES];
  processAudioBuffer(block, blockLen, filteredBuf, produced);
  // Kopiere die produced Samples in Dein globales audioBuffer[]
  size_t copyCnt = min(produced, OUT_SAMPLES - outCount);
  memcpy(audioBuffer + outCount, filteredBuf, copyCnt * sizeof(int16_t));
  outCount += copyCnt;
  rawPosition += blockLen;
}

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
  AudioMemory(80);                                      // Allocate audio processing memory
  audioShield.enable();
  audioShield.adcHighPassFilterDisable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);          // Use line-in (for MAX9814)
  audioShield.volume(0.3);                              // Headphone volume 0.0 - 1.0
  audioShield.lineInLevel(config.model.gainLevel);      // Line-in gain (0-15)
  audioShield.dacVolume(1.0);

  // enable filters for real time audio processing pipeline
  initFilters();

  println("Son of Jochen V%i - h for help", version);
};

void loop() {
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
  static bool buttonState=false;                             // state after the action, true = pushed
  bool buttonChange = checkButton(buttonState); // true if turned off or turned on

  // any push during evaluation mode stops it, but we finish the last snippet
  if (evalMode && buttonChange && buttonState) {
    evalMode = false;
    evalModeStartWaiting = false;
  
    // if we are in the middle of a recording block, end this with the eval command
    if (recording)
      evalModeEndWaiting = true;
  }

  // if we are in recording and release the button in time, we do not wait for an evaluation mode anymore
  if (!evalMode && recording && buttonChange && !buttonState) {
        evalModeStartWaiting = false;
  }

  // start audio if evaluation mode is on or we pushed the button
  if ((evalMode && !recording) || (!recording && buttonChange && buttonState)) { 
      // <1s: normaler 1s-Schnipsel-Aufnahme-Modus
      digitalWrite(LED_RECORDING_PIN, HIGH);
      recording = true;
      evalModeEndWaiting = false;
      outCount   = 0;
      rawPosition = 0;
      recorder.clear();
      recorder.begin();
      if (!evalMode)
        LOGSerial.println("start recording");
      evalModeStartWaiting = millis();
  }

    // 2) While recording, fill rawBuffer FULL 88 200 samples:
if (recording && recorder.available()) {
    while (recorder.available() && rawPosition < RAW_SAMPLES) {
      int16_t* block = (int16_t*)recorder.readBuffer();
      size_t toCopy = min((size_t)AUDIO_BLOCK_SAMPLES, RAW_SAMPLES - rawPosition);
      processBlock(block, toCopy);
      recorder.freeBuffer();
    }

    // once we’ve seen all RAW_SAMPLES, finish up:
    if (rawPosition >= RAW_SAMPLES) {
      recording = false;
      recorder.end();
      digitalWrite(LED_RECORDING_PIN, LOW);

      // send outCount samples at 16 kHz:
      digitalWrite(LED_COMMS_PIN, HIGH);
      size_t totalBytes = outCount * BYTES_PER_SAMPLE;
      int cmd = CMD_AUDIO_RECORDING;
      if (evalModeStartWaiting || (evalMode || evalModeEndWaiting)) {
         cmd = CMD_AUDIO_STREAM;
         println("evaluation audio of %i 16kHz samples %u bytes sent", outCount, totalBytes);
         
         if (evalModeEndWaiting)
           LOGSerial.println("end evaluation stream");
         evalModeEndWaiting = false;
      }
      else
        println("recording of %i 16kHz samples %u bytes sent", outCount, totalBytes);

      send_packet(Serial, cmd, (uint8_t*)audioBuffer, totalBytes);
      send_packet(Serial, CMD_SAMPLE_COUNT, (uint8_t*)&outCount, sizeof(outCount));
      Serial.flush();
      digitalWrite(LED_COMMS_PIN, LOW);

      // if button is still pushed, turn on evaluation mode
      if (evalModeStartWaiting) {
        evalMode = true;
        evalModeStartWaiting = false;
        evalModeEndWaiting = false;
      }
    }
}

  // Kontinuierliches Streaming, sobald evalMode aktiv ist
if (evalMode && recorder.available()) {
  while (recorder.available()) {
    int16_t* block = (int16_t*)recorder.readBuffer();
    // jedes AudioBlock (128 Samples → 256 Bytes) als Packet senden
    digitalWrite(LED_RECORDING_PIN, LOW);
    digitalWrite(LED_COMMS_PIN, HIGH);
    send_packet(Serial, CMD_AUDIO_STREAM,
                (uint8_t*)block,
                AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    recorder.freeBuffer();
    digitalWrite(LED_COMMS_PIN, LOW);
    digitalWrite(LED_RECORDING_PIN, HIGH);
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
