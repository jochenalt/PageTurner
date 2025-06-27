

#include "PageTurner_inferencing.h"

#include <Arduino.h>

#include <HardwareSerial.h>

#include <Audio.h>                                          // Audio Library for Audio Shield
#include <Wire.h>                                           // für Verbindung mit Audio Shield 
#include "SerialProtocol.h"
#include "constants.h"
#include "EEPROMStorage.h"
#include "constants.h"
#include "AudioFilter.h"
#include "Metronome.h"  
#include "Watchdog_t4.h"

#ifndef PLATFORMIO
#define INC_KEYBOARD
#endif
#ifdef INC_KEYBOARD
#include <Keyboard.h>
#endif

bool commandPending = false;                                // true if command processor saw a command coming and is waiting for more input
String command;                                             // current command coming in
uint32_t commandLastChar_us = 0;                            // time when the last character came in (to reset command if no further input comes in) 

// Audio signal routing
AudioInputI2S        i2s_input;                             // Audio from MAX9814 via Audio Shield LINE IN
AudioOutputI2S       audioOutput;                           // To headphone output
AudioRecordQueue     recorder;                              // record 2s snippets

// use simple (low latency) Biquad filter 2nd order to create a bandpass for speech (300-3400 Hz) at 12db
AudioFilterBiquad       lowPass;     
AudioFilterBiquad       highPass;      

AudioConnection         patchMicToRecord1Cord1(i2s_input, 0, lowPass, 0);
AudioConnection         patchMicToRecord2(lowPass, 0, highPass, 0);
AudioConnection         patchMicToRecord3(highPass, 0, recorder, 0);   // record the left line in channel

AudioControlSGTL5000 audioShield;

AudioPlayMemory        clickPlayer;
AudioConnection        patchMetronomToHeadphoneL(clickPlayer, 0, audioOutput, 0);
AudioConnection        patchMetronomToHeadphoneR(clickPlayer, 0, audioOutput, 1);

Metronome metronome;
WDT_T4<WDT1> wdt;


static int16_t audioBuffer[OUT_SAMPLES];  // full 16 kHz output
bool   recording_mode = false;                  // true, if recording is happening
bool streaming_mode = false;

bool production_mode = false;



// callback function required for calling inference
static int16_t* get_data_buffer_ptr = NULL;
static int get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&get_data_buffer_ptr[offset], out_ptr, length);
    return 0;
}

float  compute_rms(const int16_t* samples, size_t len) {
    uint64_t acc = 0;
    for (size_t i = 0; i < len; ++i) {
        float y = samples[i] / 32768.0f;   // Normierung auf [–1..1]
        acc += uint64_t(y * y * 1e9f);     // skalieren, damit int-acc passt
    }
    float mean = float(acc) / float(len) / 1e9f;
    return mean;
}

bool is_silence(const int16_t* samples, size_t len, float thresh = 0.0002) {
    return compute_rms(samples, len) < thresh;
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
            recorder.clear();
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
static uint16_t key_pressed = 0;
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
    if ((key_pressed != 0) && (millis() - key_pressed_ms < 100)) {
    Keyboard.release(key_pressed);
    key_pressed_ms = 0;
  }
#endif

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

void drainAudioInputBuffer(int16_t audio_in_buffer[], size_t &added_samples) {
  added_samples = 0;
  while (recorder.available()) {
        int16_t* block = recorder.readBuffer();

        // push new audio information to queue
        uint16_t samples = AUDIO_BLOCK_SAMPLES;
        memmove(audio_in_buffer, audio_in_buffer + samples, (RAW_SAMPLES - samples) * sizeof(audio_in_buffer[0]));
        memcpy(audio_in_buffer + (RAW_SAMPLES - samples),block, samples * sizeof(audio_in_buffer[0]));

        recorder.freeBuffer();
        added_samples += samples;
  }
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
    for (int i = 0;i<EI_CLASSIFIER_LABEL_COUNT;i++) {
        if (String(ei_classifier_inferencing_categories[i]) == String("silence"))
            silence_label_no = i;
        if (String(ei_classifier_inferencing_categories[i]) == String("weiter"))
            weiter_label_no = i;
        if (String(ei_classifier_inferencing_categories[i]) == String("next"))
            next_label_no = i;
        if (String(ei_classifier_inferencing_categories[i]) == String("zurück"))
            zurueck_label_no = i;
        if (String(ei_classifier_inferencing_categories[i]) == String("back"))
            back_label_no = i;
    }

    if ((silence_label_no == EI_CLASSIFIER_LABEL_COUNT) ||
        (weiter_label_no == EI_CLASSIFIER_LABEL_COUNT) ||
        (next_label_no == EI_CLASSIFIER_LABEL_COUNT) ||
        (zurueck_label_no == EI_CLASSIFIER_LABEL_COUNT) ||
        (back_label_no == EI_CLASSIFIER_LABEL_COUNT)) {
         print("Special label not found!");
    }
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
  AudioMemory(280);                                      // Allocate audio processing memory. Without Metronome, only 80 is required
  
  // initialise speech bandpass filter (300Hz - 3400 Hz)
  // Butterworth value Q:  “peakedness” or damping of the filter’s transition band.
  // A higher Q gives you a sharper roll-off around the cutoff, but at the cost of a resonance peak right at that frequency.
  // A lower Q gives a more gentle, overdamped response with no pronounced peak, but a slower transition.
  // Configure both Biquad filters for low-pass, for a steeper roll-off
  lowPass.setLowpass(0, 3400, 0.707);  // Channel, frequency (Hz), Q
  highPass.setHighpass(0, 300, 0.707); 
    
  audioShield.enable();
  audioShield.unmuteHeadphone();
  audioShield.adcHighPassFilterEnable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);          // Use line-in (for MAX9814)
  audioShield.volume(0.8);                              // Headphone volume 0.0 - 1.0
  audioShield.lineInLevel(config.model.gainLevel);      // Line-in gain (0-15)
  audioShield.dacVolume(0.8);

  // enable filters for real time audio processing pipeline
  initFilters();

  // initialise audio recorder
  recorder.clear();
  recorder.begin();

  // initialise metronome
  metronome.init();
  metronome.setTempo(120); 
  metronome.turn(false);

  // watchdog
   WDT_timings_t config;
  config.trigger = 2; /* in seconds, 0->128 */
  config.timeout = 1; /* in seconds, 0->128 */
  config.callback = watchdogCallback;
  wdt.begin(config);

  // find the labels we treat specially 
  init_special_labels();

  println("Son of Jochen V%i - h for help", version);
};

static int16_t audio_in_buffer[RAW_SAMPLES] = {0};

void loop() {
  // feed the watch dog
  wdt.feed();
  
  // give the metronome a change to click
  metronome.update();  
  
  // release a pressed key       
  update_keyboard_release();
  
  // check if the recording button has been pushed
  static bool buttonState=false;                             // state after the action, true = pushed
  bool buttonChange = checkButton(buttonState); // true if turned off or turned on

  // start audio if evaluation mode is on or we pushed the button
  if ((!recording_mode && buttonChange && buttonState)) { 
      digitalWrite(LED_RECORDING_PIN, HIGH);
      uint32_t now = millis();

      while ((millis() - now < 1000) && buttonState) {
        delay(10);
        buttonChange = checkButton(buttonState); // true if turned off or turned on
      }
      if (buttonState) {
        // if button is still pressed, set streaming mode
        streaming_mode = true;
        recording_mode = true;
        recorder.clear();
        LOGSerial.println("start streaming");
      } else {
        // start recording mode of 1s
        digitalWrite(LED_RECORDING_PIN, HIGH);
        recording_mode = true;
        streaming_mode = false;
        recorder.clear();
        LOGSerial.println("start recording");
      }
  }

  if (production_mode) {
    
    // read all available samples
    const int inference_freq = 50;                  // [Hz], every 20ms we run inference (one inference call is approximately 8ms)
    static const int same_pred_duration  = 200;     // 4 predictions in a row give a result 
    static const int same_pred_count_req = same_pred_duration/(1000/inference_freq);

    if (recorder.available() > 0) {
      size_t added;
      drainAudioInputBuffer(audio_in_buffer, added);
      size_t filteredbytes;
      processAudioBuffer(audio_in_buffer, RAW_SAMPLES, audioBuffer, filteredbytes);

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

        if (is_silence(audioBuffer, OUT_SAMPLES, 0.0002)) {
            pred_no = silence_label_no;
            pred_label = "silence";
            pred_certainty = 1.0;
        } else {
            run_inference(audioBuffer, OUT_SAMPLES, result,pred_no);
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
            // debounce announcement, at least 1000ms between two different actions
            if (now - last_anouncement > debounce_anouncement_ms) {
              println("Send %s: %.5f", result.classification[pred_no].label, pred_certainty);
              if (next_page)
                  send_keyboard_key(KEY_PAGE_DOWN);
              if (next_page)
                  send_keyboard_key(KEY_PAGE_UP);
              last_anouncement = now;
            }
          }
        }

        digitalWrite(LED_RECORDING_PIN, LOW);
      } // if we run inference
    }
  }


//  While recording, fill rawBuffer FULL 88 200 samples:
if (recording_mode && recorder.available()) {

      static size_t filter_samples_in_buffer = 0;
      size_t added_filtered_samples;
      drainAudioInputBuffer(audio_in_buffer, added_filtered_samples);
      filter_samples_in_buffer += added_filtered_samples;
      size_t filteredbytes;

      if (filter_samples_in_buffer >= RAW_SAMPLES) {
            // 1) produce and send a 1 s snippet
            processAudioBuffer(audio_in_buffer, RAW_SAMPLES, audioBuffer, filteredbytes);
            
            size_t totalBytes = filteredbytes * BYTES_PER_SAMPLE;
            println("recording of %u samples (%u bytes) sent", filteredbytes, totalBytes);

            ei_impulse_result_t result = { 0 };
            int pred_no;
            run_inference(audioBuffer, filteredbytes, result, pred_no);

            send_audio_packet(audioBuffer, filteredbytes, result);
            Serial.flush();

            // 2) reset counter for the *next* second
            filter_samples_in_buffer = 0;

            // 3) check button state: if *still* held, keep recording,
            //    otherwise stop and turn LED off.
            if (streaming_mode && !digitalRead(REC_BUTTON_PIN)) {
              // button is LOW → still pressed
              // drop any leftover in the hardware queue and start fresh
              recorder.clear();
              println("continuing recording next second…");
            } else {
              // button released → clean up
              recording_mode = false;
              digitalWrite(LED_RECORDING_PIN, LOW);
              println("recording finished (button released).");
            }
    }
}

// react on manual input from Serial 
executeManualCommand();

}