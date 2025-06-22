/**
 * Manages persistent configuration data that is stored in EEPROM.
 * Attributes are kept in configuration_type.
 * Uses a bank approach that distributes load and does not stress out the EEPROM's write cycles
 */  
#include <Arduino.h>
#include <EEPROM.h>
#include "EEPROMStorage.h"

#define EEPROM_MAX_WRITES 50000UL				// maximum number of writes in EEPROM before switching to next memory block


// persistent configuration in EEPROM
EEPROMStorage persConfig;

// all configuration items contained in configuration_type are stored in EEPROM
// the following block is the EEPROM master block that refers to the data blocks
// purpose is to distribute the changes in EEPROM over the entire memory to leverage 
// the full lifespan. Masterblock only contains a magic number and a pointer to a "bank" 
// that stores the actual data. If the writeCounter of the bank gets too high, the next bank 
// is allocated in the master block and used from now on.

struct eeprom_master_type {
	uint16_t magic_number;			    				// marker to indicate correct initialization
	uint16_t mem_bank_address;					
	void setup() {
		magic_number = EEPROM_MAGIC_NUMBER;				// marker to indicate correct initialization
		mem_bank_address = sizeof(eeprom_master_type);	// location of configuration type block
	}
	void write() {
		eeprom_write_word((uint16_t*)0,magic_number);
		eeprom_write_word((uint16_t*)2,mem_bank_address);

		magic_number = eeprom_read_word((uint16_t*)0);
		mem_bank_address = eeprom_read_word((uint16_t*)2);
	}
	void read() {
		magic_number = eeprom_read_word((uint16_t*)0);
		mem_bank_address = eeprom_read_word((uint16_t*)2);
	}
} persMemory;

configuration_type config;


static void println(const char* format, ...) {
  	char s[256];
 	__gnuc_va_list  args;		
  	va_start(args, format);
  	vsprintf (s, format, args);
  	va_end(args);
  	LOGSerial.println(s);
};


// write the full config block to EEPROM
void configuration_type::write() {
	config.write_counter++;				// mark an additional write cycle

	// check if we need to switch to the next bank
	if (config.write_counter >= EEPROM_MAX_WRITES) {
		// new address, starting at sizeof_eeprom and increased in steps of sizeof(config)
		persMemory.mem_bank_address += sizeof(config);
		if (persMemory.mem_bank_address + sizeof(config) >= E2END)
			persMemory.mem_bank_address = sizeof(persMemory);
		config.write_counter = 0;	
	}

	eeprom_write_block(&config, (char*)(int)persMemory.mem_bank_address, sizeof(config));
}

// read full config block from EEPROM
void configuration_type::read() {
	eeprom_read_block(&config, (char*)(int)persMemory.mem_bank_address, sizeof(config));
}

// write just one byte of config block to EEPROM (used for delayed write)
void configuration_type::writeByte(uint16_t no_of_byte) {
	if (no_of_byte < sizeof(config)) {
		uint8_t write_byte = ((uint8_t*)&config)[no_of_byte];
		EEPROM.write(no_of_byte+persMemory.mem_bank_address, write_byte);
	}
}

void EEPROMStorage::readConfg() {
			persMemory.read();

			if (persMemory.mem_bank_address + sizeof(config) <= E2END)
				config.read();
}

void EEPROMStorage::writeConfig() {
			if (persMemory.mem_bank_address + sizeof(config) <= E2END)
				config.write();

    		persMemory.write();
}

void EEPROMStorage::resetConfig() {
    		persMemory.setup();
			config.setup();
}


void EEPROMStorage::setup() {			
			// read configuration from EEPROM (or initialize if EEPPROM is a virgin)
			readConfg();

			// EPPROM has never been touched, initialized it
			if (persMemory.magic_number != EEPROM_MAGIC_NUMBER) {
				println("new EPPROM version setup from scratch");

				// no one ever touched this EEPROM, initialize it
				persMemory.setup();
				persMemory.write();
				
				config.setup();
				config.write();
				persMemory.write();
			}

			println("magic number %i, %i writes. Setup successful.",EEPROM_MAGIC_NUMBER, config.write_counter);
}	


