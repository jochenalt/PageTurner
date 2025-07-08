#include <Arduino.h>
#include <HardwareSerial.h> 
#include "constants.h"

const char* serverUrl = "http://172.20.101.7:8000/upload";
//const char* serverUrl = "http://tiny-turner.com:8000/upload";

void println(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    Serial.println(s);
};

void print(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    Serial.print(s);
};

