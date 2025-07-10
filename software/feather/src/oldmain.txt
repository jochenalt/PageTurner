
#include <Arduino.h>
#include <HardwareSerial.h>

#include "inference.h"

#include "network.h"
#include "EEPROMStorage.h"
#include "soundtools.h"
#include "bleturn.h"

// AudioInputI2S i2s;
// BLEHIDDevice hid;

// Operating Modes
enum ModeType { MODE_NONE, MODE_PRODUCTION, MODE_RECORDING, MODE_STREAMING };
ModeType mode = MODE_PRODUCTION;                                 // current operating mode

static int16_t audioBuffer[SAMPLE_RATE];               // 16kHz

void setup() {
  Serial.begin(115200);
  Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("PSRAM Size: %d MB\n", ESP.getPsramSize() / (1024 * 1024));

  // initialise EPPROM
  persConfig.setup();
 
  // initialise Wifi  
  setupNetwork();

  // initialise inference
  setupInference();

  // initialise Audio
  initAudio();

  // initialie buttons and LED
  pinMode(LED_REC_PIN, OUTPUT);
  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);

  // initialise BLE 
  initBLE();

  println("page turner V%i", version);
}


// Debounced recording button check
bool checkRecButton(bool& state) {
  const unsigned long debounceDelay = 50; // debounce time in ms
  static bool lastButtonReading = HIGH;
  static bool stableButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;

  uint8_t reading = digitalRead(REC_BUTTON_PIN);
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


void loop() {

  // Check button states
  static bool recButtonState = false;
  bool recButtonChange = checkRecButton(recButtonState);

   // Start recording or streaming on button press
  if ((mode != MODE_RECORDING) && (mode != MODE_STREAMING) && recButtonChange && recButtonState) {
    uint32_t now = millis();
    while ((millis() - now < 1000) && recButtonState) {
      delay(100); // delay to avoid recording the click sound
      recButtonChange = checkRecButton(recButtonState);
    }

    if (recButtonState) {
      println("start streaming");
      digitalWrite(LED_REC_PIN, HIGH);  // 
      mode = MODE_STREAMING;
      delay(100);
    } else {
      println("start recording");
      digitalWrite(LED_REC_PIN, HIGH);  // 
      mode = MODE_RECORDING;
    }
  }

  // Recording mode data handling
  if (((mode == MODE_RECORDING) || (mode == MODE_STREAMING)) && isAudioAvailable()) {
    static size_t totalSamplesInBuffer = 0;
    size_t samples;
    drainAudioData(audioBuffer, SAMPLES_IN_SNIPPET, samples);
    totalSamplesInBuffer += samples;

    if (totalSamplesInBuffer >= SAMPLES_IN_SNIPPET) {
      println("recording of %u samples  sent", totalSamplesInBuffer);

      int pred_no;
      static float confidence[MAX_LABELS]; 
      runInference(audioBuffer, totalSamplesInBuffer, confidence,  pred_no);

      // pack result scores in an array to send it to PC
      size_t class_count = get_no_of_labels();

      // send the audio and the predicitons it to the PC
      // sendAudioPacket(audioBuffer, totalSamplesInBuffer, confidence);

      if ((mode == MODE_STREAMING) && !digitalRead(REC_BUTTON_PIN)) {
        digitalWrite(LED_REC_PIN, HIGH);  // 
        println("continuing streaming ");
      } else {
        mode = MODE_PRODUCTION;
        println("recording finished");
        digitalWrite(LED_REC_PIN, LOW);  // 
      }
    }
  }

  // Production (inference) mode processing
  if (mode == MODE_PRODUCTION) {
    uint32_t no_audio_for = millis() - last_time_audio_receiver;
    if (no_audio_for > 200) {
      println("no audio for %ums", no_audio_for);
      resetAudioWatchdog();
    }

    const int inferencePeriod  = 100;           // [ms] time between two inference calls
    static const int samePredCountReq = 3;     // so many equal predictions until it counts 

    if (isAudioAvailable() > 0) {
      size_t added;
      drainAudioData(audioBuffer, SAMPLES_IN_SNIPPET, added);
      resetAudioWatchdog();

      uint32_t now = millis();
      static uint32_t last_inference_time = millis();
      if (now - last_inference_time > inferencePeriod) {
        last_inference_time = now;

        int pred_no;
        String pred_label;
        float pred_certainty;

        // Silence detection
        float rms = computeRMS(audioBuffer, SAMPLES_IN_SNIPPET);
        if (rms < 0.0006) {
          pred_no = silence_label_no;
          pred_label = "silence";
          pred_certainty = 1.0;
        } else {
          int pred_label;
          static float confidence[MAX_LABELS]; 
          runInference(audioBuffer, SAMPLES_IN_SNIPPET, confidence,  pred_no);

          pred_certainty = confidence[pred_label];
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

              println("Send %s: %.3f rms=%.5f", getLabelName(pred_no),  pred_certainty,rms);
              if (next_page) {
                sendPageDown();
              }
              if (prev_page) {
                sendPageUp();
              }
              last_anouncement = now;
            }
          }
        }
      }
    }
  }

}