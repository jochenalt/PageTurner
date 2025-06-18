#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include <cstdint>
#include <vector>

static const float* g_raw_audio = nullptr;

// Callback: pulls data out of your raw-audio buffer
static int get_signal_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = g_raw_audio[offset + i];
    }
    return EIDSP_OK;
}

extern "C" {

/// How many samples the model expects (e.g. 16000 for 1 s @ 16 kHz)
uint32_t get_input_frame_size() {
    return EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
}

/// How many labels your model outputs
uint32_t get_label_count() {
    return EI_CLASSIFIER_LABEL_COUNT;
}

/// Run inference on a single 1 s raw-audio buffer
///
/// \param raw_audio    pointer to int16_t[ get_input_frame_size() ]
/// \param length       must equal get_input_frame_size()
/// \param out_probs    pointer to float[ get_label_count() ]
/// \param out_labels   pointer to uint32_t that will be set = get_label_count()
/// \returns 0 on success, non-zero on error
int run_pageturner_inference_c(const int16_t *raw_audio,
                               uint32_t      length,
                               float        *out_probs,
                               uint32_t     *out_labels)
{
    if (length != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        return -1;
    }

    // 1) Convert int16 PCM to float in [-1.0, +1.0]
    std::vector<float> float_buf(length);
    for (uint32_t i = 0; i < length; i++) {
        float_buf[i] = (float)raw_audio[i] / 32768.0f;
    }

    // 2) Point the callback at our converted buffer
    g_raw_audio = float_buf.data();

    // 3) Prepare the signal_t
    signal_t signal;
    signal.total_length = length;
    signal.get_data     = get_signal_data;

    // 4) Run the EI classifier
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
    if (r != EI_IMPULSE_OK) {
        return (int)r;
    }

    // 5) Copy out the probabilities
    for (uint32_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        out_probs[i] = result.classification[i].value;
    }
    *out_labels = EI_CLASSIFIER_LABEL_COUNT;
    return 0;
}

/// Returns the number of entries in ei_classifier_inferencing_categories
uint32_t get_category_count() {
    return sizeof(ei_classifier_inferencing_categories)
         / sizeof(ei_classifier_inferencing_categories[0]);
}

/// Returns a pointer to the first element of the categories array
const char** get_categories() {
    return ei_classifier_inferencing_categories;
}

/// (Optional) Convenience: get one category by index (or nullptr if OOB)
const char* get_category(uint32_t idx) {
    uint32_t n = get_category_count();
    return (idx < n) ? ei_classifier_inferencing_categories[idx] : nullptr;
}



} // extern "C"
