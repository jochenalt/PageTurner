#include <Arduino.h>
#include <HardwareSerial.h>

#include <utils.hpp>
#include <EEPROMStorage.hpp>

#include <PatternBlinker.hpp>
#include <SerialProtocol.hpp>

bool commandPending = false;                                // true if command processor saw a command coming and is waiting for more input
String command;                                             // current command coming in
uint32_t commandLastChar_us = 0;                            // time when the last character came in (to reset command if no further input comes in) 

// Teensy LED blinker
extern PatternBlinker blinker;	

// yield is called randomly by delay, approx. every ms
// we only allow harmless things happening there (e.g. the blinker)
void yield() {
  blinker.loop(millis());
}

static void println(const char* format, ...) {
    char s[256];
 	  __gnuc_va_list  args;		
    va_start(args, format);
    vsprintf (s, format, args);
    va_end(args);
    LOGSerial.println(s);
};


void setup() {

  // blink a bit to indicate setup. 
  // (if stuck in setup, builtin LED is dark, LED is bright)
  pinMode(LED_LISTENING_PIN, OUTPUT);  
  pinMode(LED_RECORDING_PIN, OUTPUT);
  pinMode(LED_COMMS_PIN, OUTPUT);

  for (int i = 0;i<3;i++) {
    if (i>0)
      delay(100);
    digitalWrite(LED_LISTENING_PIN,HIGH);
    digitalWrite(LED_RECORDING_PIN,HIGH);
    digitalWrite(LED_COMMS_PIN,HIGH);
    delay(50);
    digitalWrite(LED_LISTENING_PIN,LOW);
    digitalWrite(LED_RECORDING_PIN,LOW);
    digitalWrite(LED_COMMS_PIN,LOW);
  }

  // everybody loves a blinking LED
  blinker.setup(LED_BUILTIN, 1000/16 );

 	// read configuration from EEPROM (or initialize if EEPPROM is a virgin)
  persConfig.setup(); 

  println("setup done");

  // activate fast watch dog (yes, there's also a slow one that reacts after 100ms)
  slowWatchdog();
};


// print nice help text and give the status
void printHelp() {
  println("   h       - help");
  println("   s       - self test");
  println("   p       - print configuration");
};


// print nice help text and give the status
void printConfiguration() {

  config.model.print();
};


inline void addCmd(char ch) {
  if ((ch != 10) && (ch != 13))
	  command += ch;
	commandPending = true;
};

inline void emptyCmd() {
	command = "";
	commandPending = false;
};

void executeManualCommand() {
	// if the last key is too old, reset the command after 1s (command-timeout)
	if (commandPending && (micros() - commandLastChar_us) > 1000000) {
		emptyCmd();
	}
	
	// check for any input from Mainboard
	if (LOGSerial.available()) {
		// store time of last character, for timeout of command
		commandLastChar_us = micros();

		char inputChar = LOGSerial.read();
		switch (inputChar) {
			case 'h': {
				if (command == "")
					printHelp();
				else
					addCmd(inputChar);
				break;
      }
      case 10:
			case 13:
				if (command.startsWith("b")) {
					  emptyCmd();
				} 
      default:
				addCmd(inputChar);
			} // switch
		} // if (Serial.available())
}

void loop() {
    // feed the watch dog
  wdt.feed();

  // everybody loves a blinking LED
  blinker.loop(millis());

  // react on manual input from Serial 
  executeManualCommand();
}
