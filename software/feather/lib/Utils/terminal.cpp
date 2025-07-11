#include <Arduino.h>
#include "terminal.h"
#include "constants.h"
#include "EEPROMStorage.h"
#include "network.h"

// Flags and buffers for command processing
bool commandPending = false;                                // true if a command is in progress
String command;                                             // buffer for incoming command
uint32_t commandLastChar_us = 0;                            // timestamp of last received character

// Add a character to incoming command
void addCmd(char ch, bool out = true) {
  if ((ch != 10) && (ch != 13)) {
    if (out) {
      Serial.print(ch);
      Serial.flush();
    }

    command += ch;
  }
  commandPending = true;
}

// Clear current command buffer
void emptyCmd() {
  command = "";
  commandPending = false;
}

// last voltage measured
extern float cellVoltage;

// Print help menu
void printHelp() {
  println("Tiny Turner %s V%i Bat %0.2fV serial:\"%s\"", config.model.serialNo, VERSION,cellVoltage, config.model.serialNo );
  println("   n       - start captive WiFi Portal");
  println("   s       - set serial numbers");
  println("   d       - send device information");
  println("   h       - help");
}


// Set serial number from command input
void setSerialNumber(String newSerial) {
    newSerial.trim();
    
    if (newSerial.length() > 0) {
      strncpy(config.model.serialNo, newSerial.c_str(), newSerial.length()+1);
      persConfig.writeConfig();
      println("Serial number set to: %s", config.model.serialNo);
    }
}


// Handle manual serial commands
void executeManualCommand() {
  // Reset if no input for 1s
  if (commandPending && (micros() - commandLastChar_us) > 1000000) {
    emptyCmd();
  }

  if (Serial.available()) {
    commandLastChar_us = micros();
    char inputChar = Serial.read();

    switch (inputChar) {
      case 'n':
        if (command == "") startCaptivePortal(); else addCmd(inputChar);
        break;
      case 'd':
        if (command == "") sendDevice(); else addCmd(inputChar);
        break;
      case 'h':
        if (command == "") printHelp(); else addCmd(inputChar);
        break;
      case 's':
        if (command == "") {
          println("Enter new serial number:");
          addCmd(inputChar, false);
        } 
        break;
      case 10:  // LF
      case 13:  // CR
        if (command.startsWith("s")) {
          Serial.println();
          setSerialNumber(command.substring(1));
          emptyCmd();
        } else if (command.startsWith("b")) {
          emptyCmd();
        }
        break;
      default:
        addCmd(inputChar);
    }
  }
}



