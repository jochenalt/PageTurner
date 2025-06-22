/**
 * Manages configuration data within EPPROM
 */ 
#pragma once

#include "constants.h"
#include "model.h"

const long EEPROM_MAGIC_NUMBER = 1565+VERSION;  // magic number to indicate whether the AVR's EEPROM has been initialized already




// This is the configuration memory block that is stored in EEPROM in a "bank". It contains a write_counter for counting the number of 
// write operations determining the right time when to switch the bank.
struct configuration_type {
	uint16_t write_counter;				// counts the write operation to change the EPPROM bank when overflows

    /** block with application configuration data */ 
	uint8_t debugLevel = 0;		

	// configuration data of IMU (mainly magnetometer) 
	ModelConfigDataType model;

	/** end of block with application configuration data */

	// initialize all configuration values to factory settings
	void setup() {
		debugLevel = 0;	
		model.setup();	
	}
	
	void write() ;
	void read() ;
	void writeByte(uint16_t no_of_byte);
};

extern configuration_type config;

class EEPROMStorage {
	public:
		 EEPROMStorage() {};
		 ~EEPROMStorage() {};
		
		void readConfg();
		void writeConfig();
		void resetConfig(); 
		void setup();
};


// persistent configuration in EEPROM
extern EEPROMStorage persConfig;


