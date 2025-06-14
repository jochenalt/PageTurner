#include <Arduino.h>
#include <cstdarg>

#include <utils.hpp>
#include <constants.hpp>


bool idleLoop = true;

uint32_t loop_now_us = 0;
uint32_t loop_now_ms = 0;


String strPrintf (const char* format, ...)
{
	char s[256];
	__gnuc_va_list  args;
		  
	va_start (args, format);
	vsprintf (s, format, args);
	va_end (args);		
  return s;
}

void printBase(const char* format, va_list args) {
	char s[256];
		  
	vsprintf (s, format, args);
  LOGSerial.print(s);
}


void printlnBase(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
	  sprintf (s, "uC");
    printLogHeader(s);

    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    LOGSerial.println(s);
};


void printLogHeader(const char* name) {
  char s[256];
  float t = fmod(((float)milliseconds())/1000.0, 60.0);		
  String leadingZero = "";
  if (t<10.0)
    leadingZero = "0";
  // annoying: Teensys sprintf does not implement leading zeros in the %f format
	sprintf (s, "%s%02.3f [%s] ", leadingZero.c_str(),t, name);
  LOGSerial.print(s);
}

float roundTo(float x, int digits) {
  // be fast, dont use pow()
  float p[6] = {1, 10,100, 1000,10000,100000};
  if (digits > 6)
    digits = 6;
  return std::ceil(x* p[digits]) /p[digits];
}

float sgn(float a) {
  return a>0?a:-a;
}

uint32_t microseconds() {
    return micros();
}

uint32_t milliseconds() {
  return micros()/1000;
}


