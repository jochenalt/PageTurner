#include "PageTurner_inferencing.h"
#include <Arduino.h>
#include <HardwareSerial.h>
// #include <Audio.h> // Audio Library for Audio Shield
#include <Wire.h>      // für Verbindung mit Audio Shield
// #include <Audio.h>

#include "SerialProtocol.h"
#include "constants.h"
#include "EEPROMStorage.h"
#include "constants.h"
#include "AudioFilter.h"
#include "Metronome.h"
#include "Watchdog_t4.h"
#include "sd_files.h"
#include "bluetooth.h"

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
static int16_t audio_raw_buffer[RAW_SAMPLES];               // 44.1 kHz input buffer
static int16_t audio_out_buffer[OUT_SAMPLES];               // 16 kHz output buffer

// Modes
enum ModeType { MODE_NONE, MODE_PRODUCTION, MODE_RECORDING, MODE_STREAMING };
ModeType mode = MODE_PRODUCTION;                                 // current operating mode

// Store last inference buffer for "bad AI" save
static int16_t lastAudioBuffer[OUT_SAMPLES];
static uint16_t lastLabelNo = 0;

// Compute RMS of a sample buffer
float compute_rms(const int16_t* samples, size_t len) {
  uint64_t acc = 0;
  for (size_t i = 0; i < len; ++i) {
    float y = samples[i] / 32768.0f;   // normalize to [-1..1]
    acc += uint64_t(y * y * 1e9f);     // scale for integer accumulator
  }
  float mean = float(acc) / float(len) / 1e9f;
  return mean;
}

// Check if buffer is below silence threshold
bool is_silence(const int16_t* samples, size_t len, float thresh) {
  return compute_rms(samples, len) < thresh;
}

// Callback for inference data access
static int16_t* get_data_buffer_ptr = NULL;
static int get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&get_data_buffer_ptr[offset], out_ptr, length);
  return 0;
}

// Run model inference on audio buffer
void run_inference(int16_t buffer[], size_t samples, ei_impulse_result_t &result, int &pred_no) {
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
  setGainLevel(config.model.gainLevel);
}

// Decrease input gain
void gainDown() {
  config.model.gainLevel--;
  if (config.model.gainLevel < 0)
    config.model.gainLevel = 0;
  if (config.model.gainLevel > 15)
    config.model.gainLevel = 15;
  println("decrease gain to %i", config.model.gainLevel);
  setGainLevel(config.model.gainLevel);
}

// Print help menu
void printHelp() {
  println("Page Turner V%i", version);
  println("   h       - help");
  println("   s       - self test");
  println("   p       - print configuration");
  println("   r       - reset");
  println("   +       - increase gain Level");
  println("   -       - decrease gain Level");
  println("   <space> - toggle production mode");
}

// Print current configuration
void printConfiguration() {
  config.model.print();
}

// Add a character to incoming command
inline void addCmd(char ch) {
  if ((ch != 10) && (ch != 13))
    command += ch;
  commandPending = true;
}

// Clear current command buffer
inline void emptyCmd() {
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
        if (command == "") printConfiguration(); else addCmd(inputChar);
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
            recorderClear();
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

#ifdef INC_KEYBOARD
static uint32_t key_pressed_ms;
static uint16_t key_pressed = millis();
#endif

// Press and hold a keyboard key
static void send_keyboard_key(uint16_t key) {
#ifdef INC_KEYBOARD
  key_pressed = key;
  key_pressed_ms = millis();
  Keyboard.press(key);
#endif
}

// Release held keyboard key after short delay
static void update_keyboard_release() {
#ifdef INC_KEYBOARD
  if (millis() - key_pressed_ms < 100) {
    Keyboard.release(key_pressed);
    key_pressed_ms = 0;
  }
#endif
}

// Turn on page turn LED
static uint32_t page_led_on_ms = millis();
void light_page_led(uint16_t key) {
  page_led_on_ms = millis();
  if (key == KEY_PAGE_DOWN) digitalWrite(LED_PAGE_DOWN, HIGH);
  if (key == KEY_PAGE_UP)   digitalWrite(LED_PAGE_UP, HIGH);
}

// Turn off page LEDs after timeout
void update_page_LEDs() {
  if (millis() - page_led_on_ms < 1000) {
    digitalWrite(LED_PAGE_DOWN, LOW);
    digitalWrite(LED_PAGE_UP,   LOW);
    page_led_on_ms = 0;
  }
}

// Send audio and inference data packet
void send_audio_packet(int16_t* audioBuf, size_t samples, ei_impulse_result_t &result) {
  send_packet(Serial, CMD_AUDIO_RECORDING, (uint8_t*)audioBuf, samples * BYTES_PER_SAMPLE);

  // Prepare sample count and classification scores
  size_t class_count = EI_CLASSIFIER_LABEL_COUNT;
  size_t buffer_len = sizeof(samples) + sizeof(class_count) + class_count * sizeof(float);
  uint8_t buffer[buffer_len];
  size_t offset = 0;

  memcpy(&buffer[offset], &samples, sizeof(samples));
  offset += sizeof(samples);
  memcpy(&buffer[offset], &class_count, sizeof(class_count));
  offset += sizeof(class_count);

  for (size_t i = 0; i < class_count; i++) {
    float score = result.classification[i].value;
    memcpy(&buffer[offset], &score, sizeof(score));
    offset += sizeof(score);
  }

  int cmd = (mode == MODE_STREAMING) ? CMD_AUDIO_STREAM : CMD_SAMPLE_COUNT;
  send_packet(Serial, cmd, (uint8_t*)buffer, buffer_len);
}

// Watchdog callback function
void watchdogCallback() {
  println("watchdog woke up");
}

// Label indices for special commands
uint16_t silence_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t weiter_label_no  = EI_CLASSIFIER_LABEL_COUNT;
uint16_t next_label_no    = EI_CLASSIFIER_LABEL_COUNT;
uint16_t zurueck_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t back_label_no    = EI_CLASSIFIER_LABEL_COUNT;

// Initialize indices of command labels
void init_special_labels() {
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

// Arduino setup function
void setup() {
  LOGSerial.begin(115200);
  Serial.begin(115200);

  // Initialize Bluetooth before other peripherals
  init_bluetooth_baudrate();

  // Configure pins
  pinMode(LED_PAGE_DOWN, OUTPUT);
  pinMode(LED_RECORDING_PIN, OUTPUT);
  pinMode(LED_PAGE_UP, OUTPUT);
  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BAD_AI_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PAGE_DOWN, HIGH);
  digitalWrite(LED_PAGE_UP, HIGH);
  digitalWrite(LED_RECORDING_PIN, HIGH);

  // Initialize audio and metronome
  initFilters();
  init_audio();
  metronome.init();
  metronome.setTempo(120);
  metronome.turn(false);

  // Prepare special labels and peripherals
  init_special_labels();
  println("AI-based voice controlled page turner V%i - h for help", version);
  persConfig.setup();
  init_sd_files(EI_CLASSIFIER_LABEL_COUNT, ei_classifier_inferencing_categories);
  init_bluetooth();

  // Configure watchdog timer
  WDT_timings_t config;
  config.trigger = 2;
  config.timeout = 1;
  config.callback = watchdogCallback;
  wdt.begin(config);

  digitalWrite(LED_PAGE_DOWN, LOW);
  digitalWrite(LED_PAGE_UP, LOW);
  digitalWrite(LED_RECORDING_PIN, LOW);
}

// Arduino main loop
void loop() {
  wdt.feed();                    // Reset watchdog
  metronome.update();            // Update metronome click
  update_keyboard_release();     // Release key if needed
  update_bluetooth_release();    // Release Bluetooth key if needed
  update_page_LEDs();            // Update page LED states

  // Check button states
  static bool recButtonState = false;
  bool recButtonChange = checkRecButton(recButtonState);
  static bool badAIButtonState = false;
  bool badAIbuttonChange = checkBadAIButton(badAIButtonState);

  // Handle bad AI save
  if (badAIbuttonChange && badAIButtonState && (lastLabelNo != silence_label_no)) {
    println("saving wrong label");
    save_wav_file(ei_classifier_inferencing_categories[lastLabelNo], lastLabelNo, lastAudioBuffer, OUT_SAMPLES);
    lastLabelNo = silence_label_no;
  }

  // Start recording or streaming on button press
  if ((mode != MODE_RECORDING) && recButtonChange && recButtonState) {
    digitalWrite(LED_RECORDING_PIN, HIGH);
    uint32_t now = millis();
    while ((millis() - now < 1000) && recButtonState) {
      delay(10);
      recButtonChange = checkRecButton(recButtonState);
    }
    if (recButtonState) {
      mode = MODE_STREAMING;
      recorderClear();
      LOGSerial.println("start streaming");
    } else {
      mode = MODE_RECORDING;
      recorderClear();
      LOGSerial.println("start recording");
    }
  }

  // Production (inference) mode processing
  if (mode == MODE_PRODUCTION) {
    static uint32_t last_time_audio_receiver = millis();
    uint32_t no_audio_for = millis() - last_time_audio_receiver;
    if (no_audio_for > 200) {
      println("no audio for %ums", no_audio_for);
      last_time_audio_receiver = millis();
    }

    const int inference_freq = 50;                // inference frequency [Hz]
    static const int same_pred_count_req = 3;     // so many equal predictions until it counts 

    if (recorderAvailable() > 0) {
      last_time_audio_receiver = millis();
      size_t added;
      drainAudioInputBuffer(audio_raw_buffer, added);
      size_t filteredbytes;
      processAudioBuffer(audio_raw_buffer, RAW_SAMPLES, audio_out_buffer, filteredbytes);

      uint32_t now = millis();
      static uint32_t last_inference_time = millis();
      if (now - last_inference_time > 1000 / inference_freq) {
        last_inference_time = now;
        digitalWrite(LED_RECORDING_PIN, HIGH);

        ei_impulse_result_t result = { 0 };
        int pred_no;
        String pred_label;
        float pred_certainty;

        // Silence detection
        if (is_silence(audio_out_buffer, OUT_SAMPLES, 0.00011)) {
          pred_no = silence_label_no;
          pred_label = "silence";
          pred_certainty = 1.0;
        } else {
          run_inference(audio_out_buffer, OUT_SAMPLES, result, pred_no);
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

        if (same_pred_count == same_pred_count_req) {
          bool next_page = (pred_no == weiter_label_no);
          bool prev_page = (pred_no == zurueck_label_no);

          static int32_t last_anouncement = millis();
          int32_t now = millis();
          if (next_page || prev_page) {
            if (now - last_anouncement > debounce_anouncement_ms) {
              memcpy(lastAudioBuffer, audio_out_buffer, OUT_SAMPLES * sizeof(audio_out_buffer[0]));
              lastLabelNo = pred_no;

              println("Send %s: %.3f", result.classification[pred_no].label, pred_certainty);
              if (next_page) {
                light_page_led(KEY_PAGE_DOWN);
                send_keyboard_key(KEY_PAGE_DOWN);
                send_bluetooth_command(KEY_PAGE_DOWN);
              }
              if (prev_page) {
                light_page_led(KEY_PAGE_UP);
                send_keyboard_key(KEY_PAGE_UP);
                send_bluetooth_command(KEY_PAGE_UP);
              }
              last_anouncement = now;
            }
          }
        }

        digitalWrite(LED_RECORDING_PIN, LOW);
      }
    }
  }

  // Recording mode data handling
  if ((mode == MODE_RECORDING) && recorderAvailable()) {
    static size_t filter_samples_in_buffer = 0;
    size_t added_filtered_samples;
    drainAudioInputBuffer(audio_raw_buffer, added_filtered_samples);
    filter_samples_in_buffer += added_filtered_samples;
    size_t filteredbytes;

    if (filter_samples_in_buffer >= RAW_SAMPLES) {
      processAudioBuffer(audio_raw_buffer, RAW_SAMPLES, audio_out_buffer, filteredbytes);
      size_t totalBytes = filteredbytes * BYTES_PER_SAMPLE;
      println("recording of %u samples (%u bytes) sent", filteredbytes, totalBytes);

      ei_impulse_result_t result = { 0 };
      int pred_no;
      run_inference(audio_out_buffer, filteredbytes, result, pred_no);
      send_audio_packet(audio_out_buffer, filteredbytes, result);
      Serial.flush();

      filter_samples_in_buffer = 0;

      if ((mode == MODE_STREAMING) && !digitalRead(REC_BUTTON_PIN)) {
        recorderClear();
        println("continuing recording next second…");
      } else {
        mode = MODE_NONE;
        digitalWrite(LED_RECORDING_PIN, LOW);
        println("recording finished");
      }
    }
  }

  // Process any manual serial commands
  executeManualCommand();
}
