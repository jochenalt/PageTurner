#include <Arduino.h>
#include "terminal.h"
#include "constants.h"
#include "EEPROMStorage.h"
#include "network.h"
#include "soundtools.h"

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
  println("Tiny Turner %s V%i Bat %0.2fV owner:\"%s\"", config.model.owner, VERSION,cellVoltage, config.model.owner );
  println("   n       - start captive WiFi Portal");
  println("   s       - set owner");
  println("   w       - send sine wave audio snippet");
  println("   d       - send device information");
  println("   h       - help");
}

// Set serial number from command input
void setOwner(String newOwner) {
    newOwner.trim();
    
    if (newOwner.length() > 0) {
      strncpy(config.model.owner, newOwner.c_str(), newOwner.length()+1);
      persConfig.writeConfig();
      println("New owner: %s", config.model.owner);
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
      case 'w':
        if (command == "") {
          int16_t* buffer = new int16_t [SAMPLE_RATE] ; 
          generateSineWave(buffer,SAMPLE_RATE);
          sendAudioSnippet(buffer, SAMPLE_RATE); 
          delete buffer;
        }
          else addCmd(inputChar);
        break;
      case 's':
        if (command == "") {
          println("Enter new owner:");
          addCmd(inputChar, false);
        } 
        break;
      case 10:  // LF
      case 13:  // CR
        if (command.startsWith("s")) {
          Serial.println();
          setOwner(command.substring(1));
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



