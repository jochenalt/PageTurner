

#include "PageTurner_inferencing.h"

#include <Arduino.h>
#include <HardwareSerial.h>

// #include <Audio.h>                                          // Audio Library for Audio Shield
#include <Wire.h>                                           // für Verbindung mit Audio Shield 
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

bool commandPending = false;                                // true if command processor saw a command coming and is waiting for more input
String command;                                             // current command coming in
uint32_t commandLastChar_us = 0;                            // time when the last character came in (to reset command if no further input comes in) 


Metronome metronome;
WDT_T4<WDT1> wdt;


static int16_t audio_raw_buffer[RAW_SAMPLES];   // full 44.1 kHz buffer of the last second
static int16_t audio_out_buffer[OUT_SAMPLES];  // full 16 kHz output
bool recording_mode = false;                 // true, if recording is happening
bool streaming_mode = false;
bool production_mode = true;

// store last audio buffer that ran inference, in case the 
// user pushs "bad AI" and wants to store it on SD  
static int16_t lastAudioBuffer[OUT_SAMPLES];
static uint16_t lastLabelNo = 0;

float compute_rms(const int16_t* samples, size_t len) {
    uint64_t acc = 0;
    for (size_t i = 0; i < len; ++i) {
        float y = samples[i] / 32768.0f;   // Normierung auf [–1..1]
        acc += uint64_t(y * y * 1e9f);     // skalieren, damit int-acc passt
    }
    float mean = float(acc) / float(len) / 1e9f;
    return mean;
}

bool is_silence(const int16_t* samples, size_t len, float thresh) {
    return compute_rms(samples, len) < thresh;
}

// callback function required for calling inference
static int16_t* get_data_buffer_ptr = NULL;
static int get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&get_data_buffer_ptr[offset], out_ptr, length);
    return 0;
}

void run_inference(int16_t buffer[],  size_t samples, ei_impulse_result_t &result, int &pred_no) {
    // set the global buffer pointer that get_data will hand over to the model
    get_data_buffer_ptr = buffer;
    signal_t signal;
    signal.total_length = samples;
    signal.get_data = &get_data;

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    // print the predictions
    // print("Inference (DSP: %d ms, classifier: %d ms) (n=%d samples)", result.timing.dsp, result.timing.classification, result.timing.anomaly, samples);
    float score = 0;
    pred_no = -1;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (score < result.classification[ix].value) {
          pred_no = ix;
          score = result.classification[ix].value;
        }
        // print(" %s:%.3f", result.classification[ix].label, result.classification[ix].value);
    }
    // println(" pred=%s", result.classification[pred_no].label);
#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif
}


// Debounced recording button 
// Returns true if button state changed (press or release)
// Outputs current stable state in 'state' (LOW = pressed)
bool checkRecButton(bool& state) {
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


// Debounced bad AI button 
// Returns true if button state changed (press or release)
// Outputs current stable state in 'state' (LOW = pressed)
bool checkBadAIButton(bool& state) {
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
      state = !stableBadAIButtonState;
      lastBadIAButtonReading = reading;
      return true;  // state has changed
    }
  }

  lastBadIAButtonReading = reading;
  return false;  // no change
}


void gainUp() {
  config.model.gainLevel++;
  if (config.model.gainLevel > 15)
     config.model.gainLevel = 15; 
  println("increase gain to %i",config.model.gainLevel);
     setGainLevel(config.model.gainLevel);                 // Line-in gain (0-15)
}

void gainDown() {
  config.model.gainLevel--;
  if (config.model.gainLevel < 0)
     config.model.gainLevel = 0; 
  if (config.model.gainLevel > 15)
     config.model.gainLevel = 15; 
  println("decrease gain to %i",config.model.gainLevel);

  setGainLevel(config.model.gainLevel);                 // Line-in gain (0-15)
}

// print nice help text and give the status
void printHelp() {
  println("Page Turner V%i", version);
  println("   h       - help");
  println("   s       - self test");
  println("   p       - print configuration");
  println("   r       - reset");
  println("   +       - increase gain Level");
  println("   -       - decrease gain Level");
  println("   <space>   production mode");
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
					delay(5000); // wait for the watchdog to wake up
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
 			case ' ': {
				if (command == "") {
					production_mode = !production_mode;
          if (production_mode) {
            LOGSerial.println("production mode on");
            recorderClear();
          } else 
          {
            LOGSerial.println("production mode off");
          }
        }
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



// send a single Page Up
#ifdef INC_KEYBOARD
static uint32_t key_pressed_ms;
static uint16_t key_pressed = millis();
#endif
static void send_keyboard_key(uint16_t key) {
#ifdef INC_KEYBOARD
  key_pressed = key;
  key_pressed_ms = millis();
  Keyboard.press(key);
#endif
}

// send a single Page Down
static void update_keyboard_release() {
#ifdef INC_KEYBOARD
    if (millis() - key_pressed_ms < 100) {
    Keyboard.release(key_pressed);
    key_pressed_ms = 0;
  }
#endif
}

// send a single Page Down
static uint32_t page_led_on_ms = millis();
void light_page_led(uint16_t key) {
  page_led_on_ms = millis();
  if (key == KEY_PAGE_DOWN)
    digitalWrite(LED_PAGE_DOWN, HIGH);
  if (key == KEY_PAGE_UP)
    digitalWrite(LED_PAGE_UP, HIGH);
}

void update_page_LEDs() {
    if (millis() - page_led_on_ms < 1000) {
      digitalWrite (LED_PAGE_DOWN, LOW);
      digitalWrite (LED_PAGE_UP, LOW);
      page_led_on_ms = 0;
  }
}


void send_audio_packet(int16_t* audioBuf, size_t samples, ei_impulse_result_t &result) {
      send_packet(Serial, CMD_AUDIO_RECORDING, (uint8_t*)audioBuf, samples*BYTES_PER_SAMPLE);

      // pack the CMD_SAMPLE_COUNT packet that consist of the sample count, the class count, and the inference  values
      size_t class_count = EI_CLASSIFIER_LABEL_COUNT;
      size_t buffer_len = sizeof(samples)+sizeof(class_count) + class_count*sizeof(float);
      uint8_t buffer[buffer_len];
      size_t offset = 0;
      memcpy ((uint8_t*)&buffer[offset], (uint8_t*)&samples, sizeof(samples));
      offset += sizeof(samples);
      memcpy ((uint8_t*)&buffer[offset], (uint8_t*)&class_count, sizeof(class_count));
      offset += sizeof(class_count);
      
      for (size_t i = 0;i<class_count;i++) {
        float score = result.classification[i].value;
        memcpy ((uint8_t*)&buffer[offset], (uint8_t*)&score, sizeof(score));
        offset += sizeof(score);
      }
      
      int cmd = streaming_mode ? CMD_AUDIO_STREAM : CMD_SAMPLE_COUNT;
      send_packet(Serial, cmd, (uint8_t*)buffer, buffer_len);
}


void watchdogCallback() {
  println("watchdog woke up");
}

uint16_t silence_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t weiter_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t next_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t zurueck_label_no = EI_CLASSIFIER_LABEL_COUNT;
uint16_t back_label_no = EI_CLASSIFIER_LABEL_COUNT;

void init_special_labels() {
  String targets[] = { "silence", "weiter", "next", "zurück", "back" };
  uint16_t* results[] = { &silence_label_no, &weiter_label_no, &next_label_no, &zurueck_label_no, &back_label_no };

 for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      String label = ei_classifier_inferencing_categories[i];
      for (int j = 0; j < sizeof(targets)/sizeof(targets[0]); j++) {
          if (label == targets[j]) {
              *results[j] = i;
          }
      }
  }
}

void setup() {

  LOGSerial.begin(115200);
  Serial.begin(115200);

  // bluetooth board needs to reset after this, so do all the rest before actually 
  // initialising it 
  init_bluetooth_baudrate();     // change baudrate to 115200


  pinMode(LED_PAGE_DOWN, OUTPUT);  
  pinMode(LED_RECORDING_PIN, OUTPUT);
  pinMode(LED_PAGE_UP, OUTPUT);
  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BAD_AI_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PAGE_DOWN,HIGH);
  digitalWrite(LED_PAGE_UP,HIGH);
  digitalWrite(LED_RECORDING_PIN,HIGH);

  // init audio board
  initFilters();

  // init Teensy Audio board
  init_audio();

  // initialise metronome
  metronome.init();
  metronome.setTempo(120); 
  metronome.turn(false);

  // find the labels we treat specially 
  init_special_labels();

  println("AI-based voice controlled page turner V%i - h for help", version);

  // initialise EEPROM
  persConfig.setup(); 

  // initialise SD card we use to store wrong inference results 
  init_sd_files(EI_CLASSIFIER_LABEL_COUNT, ei_classifier_inferencing_categories);

  // initialise bluettooth module
  init_bluetooth();

   // watchdog
  WDT_timings_t config;
  config.trigger = 2; /* in seconds, 0->128 */
  config.timeout = 1; /* in seconds, 0->128 */
  config.callback = watchdogCallback;
  wdt.begin(config);

  digitalWrite(LED_PAGE_DOWN,LOW);
  digitalWrite(LED_PAGE_UP,LOW);
  digitalWrite(LED_RECORDING_PIN,LOW);

};

void loop() {
  // feed the watch dog
  wdt.feed();
  
  // give the metronome a change to click
  metronome.update();  
  
  // release a pressed key and turn off a page LED      
  update_keyboard_release();
  update_bluetooth_release();
  update_page_LEDs();

  // check if the recording button has been pushed
  static bool recButtonState=false;                      // state after the action, true = pushed
  bool recButtonChange = checkRecButton(recButtonState); // true if turned off or turned on

  static bool badAIButtonState = false;
  bool badAIbuttonChange = checkBadAIButton(badAIButtonState); 
  if (badAIbuttonChange && badAIButtonState && (lastLabelNo != silence_label_no)) {
    println("saving wrong label");
    save_wav_file(ei_classifier_inferencing_categories[lastLabelNo], lastLabelNo, lastAudioBuffer, OUT_SAMPLES);
    // save it only once, so reset the last prediction to silence
    lastLabelNo = silence_label_no;
  }

  // start audio if evaluation mode is on or we pushed the button
  if ((!recording_mode && recButtonChange && recButtonState)) { 
      digitalWrite(LED_RECORDING_PIN, HIGH);
      uint32_t now = millis();

      while ((millis() - now < 1000) && recButtonState) {
        delay(10);
        recButtonChange = checkRecButton(recButtonState); // true if turned off or turned on
      }
      if (recButtonState) {
        // if button is still pressed, set streaming mode
        streaming_mode = true;
        recording_mode = true;
        production_mode = false;
        recorderClear();
        LOGSerial.println("start streaming");
      } else {
        // start recording mode of 1s
        digitalWrite(LED_RECORDING_PIN, HIGH);
        recording_mode = true;
        streaming_mode = false;
        production_mode = false;
        recorderClear();
        LOGSerial.println("start recording");
      }
  }

  // run inference on the 1s window

  if (production_mode) {

    static uint32_t last_time_audio_receiver = millis();
    uint32_t no_audio_for = millis() - last_time_audio_receiver;
    if (no_audio_for > 200) {
      println("no audio for %ums",no_audio_for);
      last_time_audio_receiver = millis();
    } 

    // read all available samples
    const int inference_freq = 50;                   // [Hz], every 20ms we run inference (one inference call is approximately 8ms)
    static const int same_pred_count_req = 3;        // so many predictions need to be the same to count  

    if (recorderAvailable() > 0) {

      last_time_audio_receiver = millis();
      size_t added;
      drainAudioInputBuffer(audio_raw_buffer, added);
      size_t filteredbytes;
      processAudioBuffer(audio_raw_buffer, RAW_SAMPLES, audio_out_buffer, filteredbytes);

      // once we’ve seen all RAW_SAMPLES, finish up:
      uint32_t now = millis();
      static uint32_t last_inference_time = millis();
      if (now - last_inference_time >  1000/inference_freq) {
        last_inference_time = now;

        // run inference on the 1s window
        digitalWrite(LED_RECORDING_PIN, HIGH);
        ei_impulse_result_t result = { 0 };
        int pred_no;
        String pred_label;
        float pred_certainty;

        if (is_silence(audio_out_buffer, OUT_SAMPLES, 0.00011)) {
             // float rms = compute_rms(audio_in_buffer, OUT_SAMPLES);
             // println("rms=%f", rms);
            pred_no = silence_label_no;
            pred_label = "silence";
            pred_certainty = 1.0;
        } else {
            run_inference(audio_out_buffer, OUT_SAMPLES, result,pred_no);
            pred_label = String(result.classification[pred_no].label);
            pred_certainty = result.classification[pred_no].value;
        }

        // println("outCount=%i  pred=%i %s ", outCount, pred_no, result.classification[pred_no].label);

        // debounce the announcement, have at least a pause of 
        static int16_t last_pred_no = -1;
        static int16_t same_pred_count = 0;
        const int16_t debounce_anouncement_ms  = 1500; // [ms] 

        // only accept identical predictions in a row
        if ((pred_no != -1) && (pred_no == last_pred_no)) {
          same_pred_count++;
        } else {
          same_pred_count = 1;
          last_pred_no = pred_no;
        }

        if (same_pred_count == same_pred_count_req) {
          // bool next_page =  (pred_no == weiter_label_no) || (pred_no == next_label_no);
          // bool prev_page =  (pred_no == zurueck_label_no) || (pred_no == back_label_no);
          bool next_page =  (pred_no == weiter_label_no);
          bool prev_page =  (pred_no == zurueck_label_no);

          static int32_t last_anouncement = millis();
          int32_t now = millis();
          if (next_page  || prev_page) {
            // send_audio_packet(audio_out_buffer, filteredbytes, result);

            // debounce announcement, at least 1000ms between two different actions
            if (now - last_anouncement > debounce_anouncement_ms) {
              // store the last audio buffer incase the user pushes "bad AI"
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
      } // if we run inference

    } 

  }


//  While recording, fill rawBuffer FULL 88 200 samples:
if (recording_mode && recorderAvailable()) {

      static size_t filter_samples_in_buffer = 0;
      size_t added_filtered_samples;
      drainAudioInputBuffer(audio_raw_buffer, added_filtered_samples);
      filter_samples_in_buffer += added_filtered_samples;
      size_t filteredbytes;

      if (filter_samples_in_buffer >= RAW_SAMPLES) {
            // 1) produce and send a 1 s snippet
            processAudioBuffer(audio_raw_buffer, RAW_SAMPLES, audio_out_buffer, filteredbytes);
            
            size_t totalBytes = filteredbytes * BYTES_PER_SAMPLE;
            println("recording of %u samples (%u bytes) sent", filteredbytes, totalBytes);

            ei_impulse_result_t result = { 0 };
            int pred_no;
            run_inference(audio_out_buffer, filteredbytes, result, pred_no);

            send_audio_packet(audio_out_buffer, filteredbytes, result);
            Serial.flush();

            // 2) reset counter for the *next* second
            filter_samples_in_buffer = 0;

            // 3) check button state: if *still* held, keep recording,
            //    otherwise stop and turn LED off.
            if (streaming_mode && !digitalRead(REC_BUTTON_PIN)) {
              // button is LOW → still pressed
              // drop any leftover in the hardware queue and start fresh
              recorderClear();
              println("continuing recording next second…");
            } else {
              // button released → clean up
              recording_mode = false;
              digitalWrite(LED_RECORDING_PIN, LOW);
              println("recording finished");
            }
    }
}

// react on manual input from Serial 
executeManualCommand();



}