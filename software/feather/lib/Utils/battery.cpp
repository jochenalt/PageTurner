#include "constants.h"
#include <esp_sleep.h>

#if BOARD_IS_FEATHER_S3
#include <Wire.h>
#include "Adafruit_MAX1704X.h"
#include "Adafruit_LC709203F.h"
Adafruit_MAX17048 maxlipo;
Adafruit_LC709203F lc;
#endif

bool batMonitorInitialised = false;

// MAX17048 i2c address
bool addr0x36 = true;

uint32_t lastReadTime = millis();
float lastCcellVoltage;
float lastCellPercentage;

void initBatteryMonitor() {
#ifdef BOARD_IS_FEATHER_V2
  float  measuredvbat = analogReadMilliVolts(VBAT_PIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat /= 1000; // convert to volts!
  lastCcellVoltage = measuredvbat;
  batMonitorInitialised = true;
#endif
#if BOARD_IS_FEATHER_S3
  float measuredvbat = analogReadMilliVolts(VBAT_PIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat /= 1000; // convert to volts!
  Serial.print("VBat: " ); Serial.println(measuredvbat);

  if (!maxlipo.begin()) {
    // if no lc709203f..
    if (!lc.begin()) {
      println("Couldnt find MAX17048 nor LC709203F.");
      return;
    }
    // found lc709203f!
    else {
      addr0x36 = false;
      println("Found LC709203F");
      print("Version: 0x%02X",lc.getICversion());
      lc.setThermistorB(3950);
      print("Thermistor B = %i", lc.getThermistorB());
      lc.setPackSize(LC709203F_APA_500MAH);
      lc.setAlarmVoltage(3.8);
      batMonitorInitialised = true;
    }
  // found max17048!
  }
  else {
    addr0x36 = true;
    println("Found MAX17048 with Chip ID: 0x%02X", maxlipo.getChipID(), HEX);
    batMonitorInitialised = true;
  }
#endif

  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  /*
  // Check wakeup reason, and configure the boot button as wakup source
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    println("Tiny Turner is awake!");
  }
  */
  
}

// after 3 seconds of the pushing the button tiny turner turns off
void loopPowerButton() {
  static uint32_t pushedSince = 0; 
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    if (pushedSince == 0) {
      pushedSince = millis();
    } else {
      if (millis() - pushedSince > 3000) {
          println("going to sleep");
          delay(500);
          esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_BUTTON_PIN, LOW); 
          esp_deep_sleep_start();
      }
    }
  } else {
    pushedSince = 0;
  }
}

void readBatMonitor(float &cellVoltage, float &cellPercentage) {
#ifdef BOARD_IS_FEATHER_S3
    if (millis() - lastReadTime > 5000) {
       if (addr0x36 == true) {
                lastCcellVoltage = maxlipo.cellVoltage();
                lastCellPercentage = maxlipo.cellPercent();
        }
        else {
                lastCcellVoltage = lc.cellVoltage();
                lastCellPercentage = lc.cellPercent();
        }
        lastReadTime = millis();
    }
    cellVoltage = lastCcellVoltage;
    cellPercentage = lastCellPercentage;
#endif
#ifdef BOARD_IS_FEATHER_V2
  if (millis() - lastReadTime > 5000) {
    float  measuredvbat = analogReadMilliVolts(VBAT_PIN);
    measuredvbat *= 2;    // we divided by 2, so multiply back
    measuredvbat /= 1000; // convert to volts!
    lastReadTime = millis();
    lastCcellVoltage = measuredvbat;
  }
  cellVoltage = lastCcellVoltage;
  cellPercentage = lastCcellVoltage/4.2*100.0;
#endif
}
