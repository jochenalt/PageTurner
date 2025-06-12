#include <Arduino.h>
#include <cstdarg>

#include <Watchdog.h>
#include <utils.hpp>
#include <constants.hpp>


// Watchdog (AVR watchdog does not work on Teensy)
WDT_T4<WDT1> wdt;

static WDT_timings_t config;

bool idleLoop = true;

uint32_t loop_now_us = 0;
uint32_t loop_now_ms = 0;

void watchdogWarning() {
  LOGSerial.println(strPrintf("Watchdog reset timeout=%.3f trigger=%.3f", config.timeout, config.trigger));
}

// set the watchdog timer to 100ms for normal operations
void fastWatchdog() {
  config.trigger = 0.1; /* [s], trigger is how long before the watchdog callback fires */
  config.timeout = 0.2; /* [s] timeout is how long before not feeding will the watchdog reset */
  config.callback = watchdogWarning;
  wdt.begin(config);
}

// set the watchdog timer to 100ms for normal operations
void setupWatchdog() {
  config.trigger = 1.5; /* [s], trigger is how long before the watchdog callback fires */
  config.timeout = 1.5; /* [s] timeout is how long before not feeding will the watchdog reset */
  config.callback = watchdogWarning;
  wdt.begin(config);
}

// set the watchdog timer to 120s (only used for longer things like calibrating things)
void slowWatchdog() {
  config.trigger = 128.0; /* [s], trigger is how long before the watchdog callback fires */
  config.timeout = 128.0; /* [s] timeout is how long before not feeding will the watchdog reset */
  config.callback = watchdogWarning;
  wdt.begin(config);
}

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


void printLogHeader(LogLevel lvl,const char* name) {
  char s[256];
  float t = fmod(((float)milliseconds())/1000.0, 60.0);		
  char lvlChar = String("IWERD")[(int)lvl];
  String leadingZero = "";
  if (t<10.0)
    leadingZero = "0";
  // annoying: Teensys sprintf does not implement leading zeros in the %f format
	sprintf (s, "%s%02.3f %c[%s] ", leadingZero.c_str(),t, lvlChar,name);
  LOGSerial.print(s);
}

void printLogHeader(const char* name) {
  printLogHeader(INFO,name);
}

float roundTo(float x, int digits) {
  // be fast, dont use pow()
  float p[6] = {1, 10,100, 1000,10000,100000};
  if (digits > 6)
    digits = 6;
  return std::ceil(x* p[digits]) /p[digits];
}

// substract two angles that can only go from -PI to +PI in a way that the jump from -PI to PI will be compensated
// E.g. if the angle moves from b= 3.1 to a=-3.0, the difference is a-b = -0.18 (and not -6.1)
//      if the angle moves from b=-3.1 to a= 3.0, the difference is a-b = 0.18 (and not 6.1)
float angleDifference(float a, float b) {
  float result = a-b;

  // look in both directions and return the smaller difference
  if (abs(result-TWO_PI) < abs(result))
    result -= TWO_PI;
  if (abs(result+TWO_PI) < abs(result))
    result += TWO_PI;

  return result;
}

float sgn(float a) {
  return a>0?a:-a;
}


// code stolen from https://stackoverflow.com/questions/11103683/euler-angle-to-quaternion-then-quaternion-to-euler-angle
void quaternion2RPY(double x, double y, double z, double w , double rpy[])
{

    // roll (x-axis rotation)
    double sinr_cosp =     2 * (w * x + y * z);
    double cosr_cosp = 1 - 2 * (x * x + y * y);
    rpy[0] = atan2(sinr_cosp, cosr_cosp);

    // pitch (y-axis rotation)
    double sinp = 2 * (w * y - z * x);
    if (abs(sinp) >= 1)
        rpy[1] = copysign(M_PI / 2, sinp); // use 90 degrees if out of range
    else
        rpy[1] = asin(sinp);

    // yaw (z-axis rotation)
    double siny_cosp = 2 * (w * z + x * y);
    double cosy_cosp = 1 - 2 * (y * y + z * z);
    rpy[2] = atan2(siny_cosp, cosy_cosp);

}

void RPY2Quaternion(double RPY[], double &x, double &y, double &z, double &w) {
  // Abbreviations for the various angular functions
    double cy = cos(RPY[2] * 0.5);
    double sy = sin(RPY[2] * 0.5);
    double cp = cos(RPY[1] * 0.5);
    double sp = sin(RPY[1] * 0.5);
    double cr = cos(RPY[0] * 0.5);
    double sr = sin(RPY[0] * 0.5);

    w = cr * cp * cy + sr * sp * sy;
    x = sr * cp * cy - cr * sp * sy;
    y = cr * sp * cy + sr * cp * sy;
    z = cr * cp * sy - sr * sp * cy;
};


void  cross_product(double v_A[], double  v_B[], double c_P[3]) {
  c_P[0] =   v_A[1] * v_B[2] - v_A[2] * v_B[1];
  c_P[1] = -(v_A[0] * v_B[2] - v_A[2] * v_B[0]);
  c_P[2] =   v_A[0] * v_B[1] - v_A[1] * v_B[0];
}

double dot_product(double vector_a[3], double vector_b[3]) {
   double product = vector_a[0] * vector_b[0] + vector_a[1] * vector_b[1] + vector_a[2] * vector_b[2];
   return product;
}

void rotate_by_quat(double value[3], double rotation[4], double result[3])
{
    float num12 = rotation[1] + rotation[1];
    float num2 = rotation[2] + rotation[2];
    float num = rotation[3] + rotation[3];
    float num11 = rotation[0] * num12;
    float num10 = rotation[0] * num2;
    float num9 = rotation[0] * num;
    float num8 = rotation[1] * num12;
    float num7 = rotation[1] * num2;
    float num6 = rotation[1] * num;
    float num5 = rotation[2] * num2;
    float num4 = rotation[2] * num;
    float num3 = rotation[3] * num;
    float num15 = ((value[0] * ((1. - num5) - num3)) + (value[1] * (num7 - num9))) + (value[2] * (num6 + num10));
    float num14 = ((value[0] * (num7 + num9)) + (value[1] * ((1. - num8) - num3))) + (value[2] * (num4 - num11));
    float num13 = ((value[0] * (num6 - num10)) + (value[1] * (num4 + num11))) + (value[2] * ((1. - num8) - num5));
    result[0] = num15;
    result[1] = num14;
    result[2] = num13;
}

uint32_t microseconds() {
    return micros();
}

uint32_t milliseconds() {
  return micros()/1000;
}


