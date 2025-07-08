#pragma once

#include <Arduino.h>

// Label indices for special commands
extern uint16_t silence_label_no;
extern uint16_t weiter_label_no;
extern uint16_t next_label_no ;
extern uint16_t zurueck_label_no;
extern uint16_t back_label_no ;

// Initialize indices of command labels

float computeRMS(const int16_t* samples, size_t len) ;
void runInference(int16_t buffer[], size_t samples, float confidence[], int &pred_no);
uint8_t get_no_of_labels();
void setupInference();
String getLabelName(uint8_t no);
