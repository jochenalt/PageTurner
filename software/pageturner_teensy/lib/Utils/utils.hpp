#pragma once

#include <Arduino.h>
#include <Watchdog.h>

// like sprintf but with a String as output
String strPrintf (const char* format, ...);

enum LogLevel 		 { INFO=0, WARN=1, ERROR=2, FATAL=3, DEBUG=4 };
void printLogHeader(const char* name);
void printLogHeader(LogLevel lvl,const char* name);

void printlnBase(const char* format, ...);

// Watchdog (AVR watchdog does not work on Teensy)
extern WDT_T4<WDT1> wdt;

void watchdogWarning();
// set the watchdog timer to 100ms
void fastWatchdog();
void slowWatchdog();
void setupWatchdog();

// returns the difference of an angle going from b to a 
// while assuming that both angles can only go from -PI to +PI
float angleDifference(float a, float b);

// geometric calculations
void quaternion2RPY(double x, double y, double z, double w , double rpy[]);
void RPY2Quaternion(double RPY[], double &x, double &y, double &z, double &w);
void rotate_by_quat(double value[3], double rotation[4], double result[3]);

float roundTo(float x, int digits);
#define DEG2RAD(x) ((x)*TWO_PI/360.)
#define RAD2DEG(x) ((x)*360./TWO_PI)

float sgn(float a);

class Measurement {
  const uint8_t filter = 16;
  public:

    void reset() {
        start_time = micros();
        end_time = start_time;
        duration_us = 0;
        duration_avr = 0;
        deviation_avr = 0;
        within_same_us = 0;
        peak_avr = 0;
        cycle_avr_us = 0; // start with 1 to avoid div by zero
    }
    Measurement() {
      reset();
      name = "";
    };

    Measurement(String n) {
      reset();
      name = n;
    };
    virtual ~Measurement() {};

    void start() { 
        start(micros());
    }

    void start(uint32_t now_us) { 
        uint32_t cycle_duration_us = now_us - start_time;
        if (cycle_duration_us == 0)
          start_within_same_us++;
        else {
          cycle_avr_us = (cycle_avr_us*(filter-1) + ((double)cycle_duration_us)/(double)(start_within_same_us+1.))/filter;
          start_within_same_us = 0;
        }
        start_time = now_us;
    }

    void stop()  {
       end_time = micros(); 
       duration_us = end_time - start_time;

        if (duration_us == 0) {
          within_same_us++;
        } else {
          double act_duration = ((double)duration_us) /(double) (within_same_us+1.);
          double deviation = act_duration > duration_avr?act_duration-duration_avr:duration_avr-act_duration;
          duration_avr = (duration_avr*(float)(filter-1) + duration_us)/(float)filter;
          deviation_avr = (deviation_avr*(float)(filter-1) +deviation)/(float)filter;
          peak_avr = (peak_avr*98.)/100.;
          if (duration_avr > peak_avr)
            peak_avr = duration_avr;
        }
    }
    float getLastStartTime() { return ((float)start_time)/1000000.0;};
    float getTime() { return ((float)duration_us)/1000000.0; };
    float getAvrTime() { return ((float)duration_avr)/1000000.0; };
    float getPeakTime() { return peak_avr/1000000.0; }
    float getAvrFreq() { 
      if (cycle_avr_us > 0)
        return 1000000.0/cycle_avr_us;
      else
        return -1;
     }
    float getAvrDeviation() { 
      if (duration_avr > 0)
        return peak_avr/duration_avr;
      else
        return 0;
     }

    void tick() {
      end_time = micros();
      duration_us = end_time - start_time;
      cycle_avr_us = duration_us; 
 
      if (duration_us == 0) {
        within_same_us++;
      } else {
        double act_duration = ((double)duration_us) /(double) (within_same_us+1.);
        double deviation = act_duration > duration_avr?act_duration-duration_avr:duration_avr-act_duration;
        deviation_avr = (deviation_avr*(filter-1) + deviation)/filter;
        duration_avr = (duration_avr*(filter-1) + act_duration)/filter;
        start_time = end_time;
        within_same_us = 0;
      }
    } 

    void print() {
      if (getAvrFreq() > 10000)
        printlnBase("Measurement %s f=%.1fkHz t=%0.fus", name.c_str(), (float)getAvrFreq()/1000.0, duration_avr);
      else
        printlnBase("Measurement %s f=%.1fHz t=%0.fus", name.c_str(), getAvrFreq(),duration_avr);
    }

  private:
    uint32_t start_time;
    uint32_t end_time;
    uint32_t duration_us;
    float cycle_avr_us;
    double duration_avr;
    double deviation_avr;
    uint32_t within_same_us;
    uint32_t start_within_same_us;
    double peak_avr;
    String name;
};

uint32_t milliseconds();
uint32_t microseconds();


extern uint32_t loop_now_us;
extern uint32_t loop_now_ms;
extern bool idleLoop;

#define POWER_MANAGER_ADDRESS 0x40