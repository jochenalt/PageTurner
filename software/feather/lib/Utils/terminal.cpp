#include <Arduino.h>
#include "terminal.h"
#include "constants.h"

// Flags and buffers for command processing
bool commandPending = false;                                // true if a command is in progress
String command;                                             // buffer for incoming command
uint32_t commandLastChar_us = 0;                            // timestamp of last received character

// Add a character to incoming command
void addCmd(char ch) {
  if ((ch != 10) && (ch != 13))
    command += ch;
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
  println("Tiny Turner V%i %f.1V", VERSION,cellVoltage );
  println("   h       - help");
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
      case 'h':
        if (command == "") printHelp(); else addCmd(inputChar);
        break;
      case 10:
      case 13:
        if (command.startsWith("b")) emptyCmd();
        break;
      default:
        addCmd(inputChar);
    }
  }
}




