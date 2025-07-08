#include <Arduino.h>
#include "inference.h"
#include "PageTurner_inferencing.h"

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

uint8_t get_no_of_labels() {
  return EI_CLASSIFIER_LABEL_COUNT;
}

String getLabelName(uint8_t no) {
  return  ei_classifier_inferencing_categories[no];
}


// Run model inference on audio buffer
void runInference(int16_t buffer[], size_t samples, float confidence[], int &pred_no) {
  // Prepare signal for classifier
  get_data_buffer_ptr = buffer;
  signal_t signal;
  signal.total_length = samples;
  signal.get_data = &get_data;

  // Execute classifier
  ei_impulse_result_t result;
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
      confidence[ix] = score; 
    }
  }

  // Optionally print anomaly score
#if EI_CLASSIFIER_HAS_ANOMALY == 1
  ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif
}


void setupInference() {
  initSpecialLabels();
}