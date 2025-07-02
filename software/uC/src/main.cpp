#include "PageTurner_inferencing.h"     // needs to come first and colides with Audio.h, both cant be included in ther same file 
#include <Arduino.h>
#include <HardwareSerial.h>

#include "SerialProtocol.h"
#include "constants.h"
#include "EEPROMStorage.h"
#include "constants.h"
#include "AudioTools.h"
#include "Metronome.h"
#include "Watchdog_t4.h"
#include "SDFiles.h"
#include "Bluetooth.h"

// I did not manage to enable thwe USB_HID_SERIAL mode in platformIO, somehow onley works in the Arduino IDE 
#ifndef PLATFORMIO
#define INC_KEYBOARD
#endif
#ifdef INC_KEYBOARD
#include <Keyboard.h>
#endif

// Flags and buffers for command processing
bool commandPending = false;                                // true if a command is in progress
String command;                                             // buffer for incoming command
uint32_t commandLastChar_us = 0;                            // timestamp of last received character

Metronome metronome;                                        // metronome instance
WDT_T4<WDT1> wdt;                                           // watchdog timer

// Audio buffers
static int16_t audioRawBuffer[RAW_SAMPLES];               // 44.1 kHz input buffer
static int16_t audioOutBuffer[OUT_SAMPLES];               // 16 kHz output buffer

// Operating Modes
enum ModeType { MODE_NONE, MODE_PRODUCTION, MODE_RECORDING, MODE_STREAMING };
ModeType mode = MODE_PRODUCTION;                                 // current operating mode

// if set, every label is considered wrong and save (good for getting "background" and "unknown" samples)
bool punishMeConstantly = false;

// Store last inference buffer, in case the "BAD AI" button is pressed and the audio second needs to be saved
static int16_t lastAudioBuffer[OUT_SAMPLES];
static uint16_t lastLabelNo = 0;

static uint32_t last_time_audio_receiver = millis();

void resetAudioWatchdog() {
  last_time_audio_receiver = millis();
}

// Compute RMS of a sample buffer
float computeRMS(const int16_t* samples, size_t len) {
  uint64_t acc = 0;
  for (size_t i = 0; i < len; ++i) {
    float y = samples[i] / 32768.0f;   // normalize to [-1..1]
    acc += uint64_t(y * y * 1e9f);     // scale for integer accumulator
  }
  float mean = float(acc) / float(len) / 1e9f;
  return mean;
}

// Check if buffer is below silence threshold
bool isSilence(const int16_t* samples, size_t len, float thresh) {
  return computeRMS(samples, len) < thresh;
}

// Callback for inference data access
static int16_t* get_data_buffer_ptr = NULL;
static int get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&get_data_buffer_ptr[offset], out_ptr, length);
  return 0;
}

// Run model inference on audio buffer
void runInference(int16_t buffer[], size_t samples, ei_impulse_result_t &result, int &pred_no) {
  // Prepare signal for classifier
  get_data_buffer_ptr = buffer;
  signal_t signal;
  signal.total_length = samples;
  signal.get_data = &get_data;

  // Execute classifier
  EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
  if (r != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\n", r);
    return;
  }

  // Determine highest scoring label
  float score = 0;
  pred_no = -1;
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    if (score < result.classification[ix].value) {
      pred_no = ix;
      score = result.classification[ix].value;
    }
  }

  // Optionally print anomaly score
#if EI_CLASSIFIER_HAS_ANOMALY == 1
  ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif
}

// Debounced recording button check
bool checkRecButton(bool& state) {
  const unsigned long debounceDelay = 50; // debounce time in ms
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
      state = !stableButtonState;          // LOW = pressed
      lastButtonReading = reading;
      return true;  // state changed
    }
  }

  lastButtonReading = reading;
  return false;     // no change
}

// Debounced "bad AI" button check
bool checkBadAIButton(bool& state) {
  const unsigned long debounceDelay = 50; // debounce time in ms
  static bool lastBadIAButtonReading = HIGH;
  static bool stableBadAIButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;

  bool reading = digitalRead(BAD_AI_BUTTON_PIN);
  if (reading != lastBadIAButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableBadAIButtonState) {
      stableBadAIButtonState = reading;
      state = !stableBadAIButtonState;     // LOW = pressed
      lastBadIAButtonReading = reading;
      return true;  // state changed
    }
  }

  lastBadIAButtonReading = reading;
  return false;     // no change
}

// Increase input gain
void gainUp() {
  config.model.gainLevel++;
  if (config.model.gainLevel > 15)
    config.model.gainLevel = 15;
  println("increase gain to %i", config.model.gainLevel);
  set_gain_level(config.model.gainLevel);
}

// Decrease input gain
void gainDown() {
  config.model.gainLevel--;
  if (config.model.gainLevel < 0)
    config.model.gainLevel = 0;
  if (config.model.gainLevel > 15)
    config.model.gainLevel = 15;
  println("decrease gain to %i", config.model.gainLevel);
  set_gain_level(config.model.gainLevel);
}

// Print help menu
void printHelp() {
  println("Page Turner V%i", version);
  println("   h       - help");
  println("   s       - self test");
  println("   r       - reset");
  println("   +       - increase gain Level");
  println("   -       - decrease gain Level");
  println("   p       - punish for all labels");

  println("   <space> - toggle production mode");
}


// Add a character to incoming command
void addCmd(char ch) {
  if ((ch != 10) && (ch != 13))
    command += ch;
  commandPending = true;
}

// Clear current command buffer
void emptyCmd() {
  command = "";
  commandPending = false;
}

// Handle manual serial commands
void executeManualCommand() {
  // Reset if no input for 1s
  if (commandPending && (micros() - commandLastChar_us) > 1000000) {
    emptyCmd();
  }

  if (LOGSerial.available()) {
    commandLastChar_us = micros();
    char inputChar = LOGSerial.read();

    switch (inputChar) {
      case 'h':
        if (command == "") printHelp(); else addCmd(inputChar);
        break;
      case 'p':
        if (command == "") {
          punishMeConstantly = !punishMeConstantly;
          if (punishMeConstantly) {
            println ("punishment mode on");
            mode = MODE_PRODUCTION;
          }
          else
            println ("punishment mode off");
          }
          else addCmd(inputChar);
        break;
      case 'r':
        if (command == "") delay(5000); else addCmd(inputChar);
        break;
      case '+':
        if (command == "") gainUp(); else addCmd(inputChar);
        break;
      case '-':
        if (command == "") gainDown(); else addCmd(inputChar);
        break;
      case ' ':
        if (command == "") {
          if (mode !=  MODE_PRODUCTION) { 
            LOGSerial.println("production mode on");
            mode = MODE_PRODUCTION;
            resetAudioWatchdog();
            clearAudioBuffer();
          } else {
            mode = MODE_NONE;
            LOGSerial.println("production mode off");
          }
        } else addCmd(inputChar);
        break;
      case 10:
      case 13:
        if (command.startsWith("b")) emptyCmd();
        break;
      default:
        addCmd(inputChar);
    }
  }
}

static uint32_t key_pressed_ms;
static uint16_t key_pressed = millis();

// Press and hold a keyboard key
static void sendKeyboardKey(uint16_t key) {
#ifdef INC_KEYBOARD
  key_pressed = key;
  key_pressed_ms = millis();
  Keyboard.press(key);
#endif
}

// Release held keyboard key after short delay
static void updateKeyboardRelease() {
#ifdef INC_KEYBOARD
  if (millis() - key_pressed_ms < 100) {
    Keyboard.release(key_pressed);
    key_pressed_ms = 0;
  }
#endif
}

// Turn on page turn LED
static uint32_t pageLEDOnms = millis();
void turnOnPageLED(uint16_t key) {
  pageLEDOnms = millis();
  if (key == KEY_PAGE_DOWN) digitalWrite(LED_PAGE_DOWN, HIGH);
  if (key == KEY_PAGE_UP)   digitalWrite(LED_PAGE_UP, HIGH);
}

// Turn off page LEDs after timeout
void updatePageLED() {
  if (millis() - pageLEDOnms < 1000) {
    digitalWrite(LED_PAGE_DOWN, LOW);
    digitalWrite(LED_PAGE_UP,   LOW);
    pageLEDOnms = 0;
  }

}

// let the recording light lethargically blink
void updateModeLED() {
  if ((mode == MODE_RECORDING) || (mode == MODE_STREAMING)) {
    digitalWrite(LED_RECORDING_PIN, ANALOG_WRITE_MAX);
  }
  else 
    if (mode == MODE_PRODUCTION) {
      // lethargic blinking, like an "ON AIR" sign
      int val = ((2000 - millis() % 2000) * ANALOG_WRITE_MAX) / 2000;
      analogWrite(LED_RECORDING_PIN, val);
   } else {
      analogWrite(LED_RECORDING_PIN,0 );
   }
}


// Watchdog callback function
void watchdogCallback() {
    digitalWrite(LED_RECORDING_PIN, HIGH);
    digitalWrite(LED_PAGE_DOWN, HIGH);
    digitalWrite(LED_PAGE_UP, HIGH);
    println("watchdog alerted");
}

// Label indices for special commands
uint16_t silence_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t weiter_label_no  = EI_CLASSIFIER_LABEL_COUNT;
uint16_t next_label_no    = EI_CLASSIFIER_LABEL_COUNT;
uint16_t zurueck_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t back_label_no    = EI_CLASSIFIER_LABEL_COUNT;

// Initialize indices of command labels
void initSpecialLabels() {
  String targets[] = { "silence", "weiter", "next", "zurück", "back" };
  uint16_t* results[] = { &silence_label_no, &weiter_label_no, &next_label_no, &zurueck_label_no, &back_label_no };

  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    String label = ei_classifier_inferencing_categories[i];
    for (size_t j = 0; j < sizeof(targets) / sizeof(targets[0]); j++) {
      if (label == targets[j]) {
        *results[j] = i;
      }
    }
  }
}

void setup() {
  LOGSerial.begin(115200);
  Serial.begin(115200);

  // Initialize Bluetooth before other peripherals
  initBluetoothBaudrate();

  // Configure pins
  pinMode(LED_PAGE_DOWN, OUTPUT);
  pinMode(LED_RECORDING_PIN, OUTPUT);
  pinMode(LED_PAGE_UP, OUTPUT);
  pinMode(LED_ON_PIN, OUTPUT);

  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BAD_AI_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SWITCH_ON_OFF_PIN, INPUT_PULLUP);
  
  digitalWrite(LED_PAGE_DOWN, HIGH);
  digitalWrite(LED_PAGE_UP, HIGH);
  digitalWrite(LED_RECORDING_PIN,HIGH);
  digitalWrite(LED_ON_PIN, HIGH);

  // Initialize audio and metronome
  initAudioTools();
  metronome.init();
  metronome.setTempo(120);
  metronome.turn(false);

  // Prepare special labels and peripherals
  initSpecialLabels();
  println("AI-based voice controlled page turner V%i - h for help", version);
  persConfig.setup();
  initSDFiles(EI_CLASSIFIER_LABEL_COUNT, ei_classifier_inferencing_categories);
  initBluetooth();

  // Configure watchdog timer to 3seconds
  WDT_timings_t config;
  config.trigger = 3;
  config.timeout = 2;
  config.callback = watchdogCallback;
  wdt.begin(config);

  // turn off all lights
  digitalWrite(LED_PAGE_DOWN, LOW);
  digitalWrite(LED_PAGE_UP, LOW);
  digitalWrite(LED_RECORDING_PIN, LOW);

  // start measuring the time without audio
  resetAudioWatchdog();
}


void loop() {
  wdt.feed();                    // Reset watchdog

  // if I am turned off, put me to sleep
  if (digitalRead(SWITCH_ON_OFF_PIN) == LOW) {
    digitalWrite(LED_PAGE_DOWN, LOW);
    digitalWrite(LED_PAGE_UP, LOW);
    digitalWrite(LED_RECORDING_PIN, LOW);
    digitalWrite(LED_ON_PIN, LOW);
    delay(1000);
  }

  metronome.update();            // Update metronome click
  updateKeyboardRelease();     // Release key if needed
  updateBluetoothRelease();    // Release Bluetooth key if needed
  updatePageLED();            // Update page LED states

  // in streaming mode, the recording LED is lethargically blinking
  updateModeLED();
  
  // Check button states
  static bool recButtonState = false;
  bool recButtonChange = checkRecButton(recButtonState);
  static bool badAIButtonState = false;
  bool badAIbuttonChange = checkBadAIButton(badAIButtonState);

  // Handle bad AI save
  if (badAIbuttonChange && badAIButtonState && (lastLabelNo != silence_label_no)) {
    println("I am sorry, I am saving the bad label on SD");
    digitalWrite(LED_PAGE_UP, HIGH);
    digitalWrite(LED_PAGE_UP, HIGH);
    digitalWrite(LED_RECORDING_PIN, HIGH);

    saveWavFile(ei_classifier_inferencing_categories[lastLabelNo], lastLabelNo, lastAudioBuffer, OUT_SAMPLES);
    lastLabelNo = silence_label_no; // save label  one only once

    digitalWrite(LED_PAGE_UP, HIGH);
    digitalWrite(LED_PAGE_UP, HIGH);
    digitalWrite(LED_RECORDING_PIN, HIGH);
  }

  // Start recording or streaming on button press
  if ((mode != MODE_RECORDING) && (mode != MODE_STREAMING) && recButtonChange && recButtonState) {
    uint32_t now = millis();
    while ((millis() - now < 1000) && recButtonState) {
      delay(100);
      recButtonChange = checkRecButton(recButtonState);
      wdt.feed();                    // Reset watchdog
    }

    if (recButtonState) {
      println("start streaming");
      mode = MODE_STREAMING;
      clearAudioBuffer();
      delay(100);
    } else {
      println("start recording");
      mode = MODE_RECORDING;
      clearAudioBuffer();
    }
  }

  // Production (inference) mode processing
  if (mode == MODE_PRODUCTION) {
    uint32_t no_audio_for = millis() - last_time_audio_receiver;
    if (no_audio_for > 200) {
      println("no audio for %ums", no_audio_for);
      resetAudioWatchdog();
    }

    const int inferencePeriod  = 25;           // [ms] time between two inference calls
    static const int samePredCountReq = 3;     // so many equal predictions until it counts 

    if (isAudioDataAvailable() > 0) {
      last_time_audio_receiver = millis();
      size_t added;
      drainAudioData(audioRawBuffer, added);
      size_t filteredbytes;
      processAudioBuffer(audioRawBuffer, RAW_SAMPLES, audioOutBuffer, filteredbytes);

      uint32_t now = millis();
      static uint32_t last_inference_time = millis();
      if (now - last_inference_time > inferencePeriod) {
        last_inference_time = now;

        ei_impulse_result_t result = { 0 };
        int pred_no;
        String pred_label;
        float pred_certainty;

        // Silence detection
        if (isSilence(audioOutBuffer, OUT_SAMPLES, 0.00011)) {
          pred_no = silence_label_no;
          pred_label = "silence";
          pred_certainty = 1.0;
        } else {
          runInference(audioOutBuffer, OUT_SAMPLES, result, pred_no);
          pred_label = String(result.classification[pred_no].label);
          pred_certainty = result.classification[pred_no].value;
        }

        // Debounce predictions
        static int16_t last_pred_no = -1;
        static int16_t same_pred_count = 0;
        const int16_t debounce_anouncement_ms = 1500;

        if (pred_no != -1 && pred_no == last_pred_no) {
          same_pred_count++;
        } else {
          same_pred_count = 1;
          last_pred_no = pred_no;
        }

        if (same_pred_count == samePredCountReq) {
          bool next_page = (pred_no == weiter_label_no); // || ((pred_no == next_label_no)
          bool prev_page = (pred_no == zurueck_label_no); // || (pred_no == back_label_no)

          static int32_t last_anouncement = millis();
          int32_t now = millis();
          if (next_page || prev_page) {
            if (now - last_anouncement > debounce_anouncement_ms) {
              memcpy(lastAudioBuffer, audioOutBuffer, OUT_SAMPLES * sizeof(audioOutBuffer[0]));
              lastLabelNo = pred_no;

              println("Send %s: %.3f", result.classification[pred_no].label, pred_certainty);
              if (next_page) {
                turnOnPageLED(KEY_PAGE_DOWN);
                sendKeyboardKey(KEY_PAGE_DOWN);
                sendBluetoothKey(KEY_PAGE_DOWN);
              }
              if (prev_page) {
                turnOnPageLED(KEY_PAGE_UP);
                sendKeyboardKey(KEY_PAGE_UP);
                sendBluetoothKey(KEY_PAGE_UP);
              }
              last_anouncement = now;

              // in punishment mode all labels are considered wrong
              if (punishMeConstantly) {
                saveWavFile(ei_classifier_inferencing_categories[lastLabelNo], lastLabelNo, lastAudioBuffer, OUT_SAMPLES);
                lastLabelNo = silence_label_no;
              }
            }
          }
        }
      }
    }
  }

  // Recording mode data handling
  if (((mode == MODE_RECORDING) || (mode == MODE_STREAMING)) && isAudioDataAvailable()) {
    static size_t filter_samples_in_buffer = 0;
    size_t added_filtered_samples;
    drainAudioData(audioRawBuffer, added_filtered_samples);
    filter_samples_in_buffer += added_filtered_samples;
    size_t filteredbytes;

    if (filter_samples_in_buffer >= RAW_SAMPLES) {
      processAudioBuffer(audioRawBuffer, RAW_SAMPLES, audioOutBuffer, filteredbytes);
      size_t totalBytes = filteredbytes * BYTES_PER_SAMPLE;
      println("recording of %u samples (%u bytes) sent", filteredbytes, totalBytes);

      ei_impulse_result_t result = { 0 };
      int pred_no;
      runInference(audioOutBuffer, filteredbytes, result, pred_no);

      // pack result scores in an array to send it to PC
      size_t class_count = EI_CLASSIFIER_LABEL_COUNT;
      float scored[EI_CLASSIFIER_LABEL_COUNT];
      for (size_t i = 0; i < class_count; i++) {
        scored[i] = result.classification[i].value;
      }

      // send the audio and the predicitons it to the PC
      sendAudioPacket( (mode == MODE_STREAMING) ? CMD_AUDIO_STREAM : CMD_AUDIO_SAMPLE,
                          audioOutBuffer, filteredbytes, scored, EI_CLASSIFIER_LABEL_COUNT);
      Serial.flush();

      filter_samples_in_buffer = 0;

      if ((mode == MODE_STREAMING) && !digitalRead(REC_BUTTON_PIN)) {
        clearAudioBuffer();
        digitalWrite(LED_RECORDING_PIN, LOW);  // 
        println("continuing recording next second…");
      } else {
        mode = MODE_NONE;
        println("recording finished");
      }
    }
  }

  // Process any manual serial commands
  executeManualCommand();
}
