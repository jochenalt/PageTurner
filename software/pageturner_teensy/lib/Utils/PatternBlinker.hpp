/*
 * PatternBlinker.hpp
 *
 * Created: 21.11.2014 22:44:41
 *  Author: JochenAlt
 */

#pragma once
#include "TimePassedBy.hpp"

// Class implementing a pretty pattern blinker without use of delay().
// Has to run in a loop.
// use:
//      // define blink pattern, 1=on, 0=off
//		static uint8_t BotIsBalancing[3] = { 0b11001000,0b00001100,0b10000000}; // nice pattern. Each digit takes 50ms
//																				// define blink pattern array as long
//																				// as you like
//		PatternBlinker blinker;													// initiate pattern blinker
//		blinker.setup(LED_PIN_BLUE,50);											// 
//		blinker.set(BotIsBalancing,sizeof(BotIsBalancing));						// assign pattern
// 		
//		loop() {
//			blinker.loop();														// switch on off LED when necessary
//			<do something else>
//		}
//
class PatternBlinker {
	public:

	PatternBlinker() {
		setup(0,0);
	}

	PatternBlinker(uint8_t pPin, uint8_t ms) {
		setup(pPin,ms);
	}

	 void setup(uint8_t pPin, uint8_t ms) {
		mPin = pPin;
		mDuration = ms;
		mPattern = NULL;
		pinMode(pPin, OUTPUT);
		digitalWrite(pPin, LOW);
	}

	// switch blinker off
	void off() {
		mPattern = NULL;
		if (mPin != -1)
		digitalWrite(mPin,HIGH);
	}


	// set the blink pattern on the passed pin
	void set(uint8_t* pPattern, uint8_t pPatternLength) {
		mPattern = pPattern;
		mPatternLen = pPatternLength;
		mSeq = 0;
		oneTime = false;
	}
	
	void setBlocking() {
		static uint8_t pattern[2] = {0b11111111, 0b11111111};
		set(pattern, sizeof(pattern));
		isBlocking = true; 
	}
	void setPowerOff() {
		static uint8_t pattern[2] = {0b1000000, 0b00000000};
		set( pattern, sizeof(pattern));
	}

	void setPowerOn() {
		static uint8_t pattern[3] = { 0b11110001,0b11000000, 0b00000000};
		set(pattern, sizeof(pattern));
	}
	void setMotorOn() {
		static uint8_t pattern[2] =  { 0b11001000, 0b00000000};
		set(pattern, sizeof(pattern));
	}

	void assignPattern() {pendingLoop = true;};

	void setOneTime(uint8_t* pPattern, uint8_t pPatternLength) {
		set(pPattern,pPatternLength);
		oneTime = true;
	}

	void loop(uint32_t now) {
		if (pendingLoop) {
			pendingLoop = false;
		}

		uint32_t passed_ms;
		if ((timer.isDue_ms(mDuration,passed_ms, now))) {
			if (mPattern != NULL) {
				idleLoop = false;
				uint8_t pos,bitpos;
				pos = mSeq / 8;
				bitpos = 7- (mSeq & 0x07);
				if ((mPattern[pos] & _BV(bitpos)) > 0) {
					analogWrite(mPin,255);
				}
				else {
					analogWrite(mPin,64);
				}

				mSeq++;
				if ((mSeq >= (mPatternLen)*8)) {
					mSeq = 0;
					if (oneTime)
						mPattern = NULL;
				}
			} else {
				analogWrite(mPin,255);
			}
		}
	}


	uint8_t mSeq;			// current position within the blink pattern
	int8_t mPin;			// pin to be used
	uint8_t* mPattern;		// blink pattern, passed in set()
	uint8_t  mPatternLen;	// length of the pattern  = sizeof(*mPattern)
	TimePassedBy timer;		// timer for checking passed time
	boolean oneTime;
	uint8_t mDuration;
	bool pendingLoop = false;
	uint8_t state = 255;
	bool isBlocking = false;
};

class ErrorPatternBlinker {
	public:

	ErrorPatternBlinker() {
		setup(0,0);
	}

	ErrorPatternBlinker(uint8_t pPin, uint8_t ms) {
		setup(pPin,ms);
	}

	 void setup(uint8_t pPin, uint8_t ms) {
		mPin = pPin;
		mDuration = ms;
		mPattern = NULL;
		pinMode(pPin, OUTPUT);
		digitalWrite(pPin, LOW);
	}

	// switch blinker off
	void off() {
		mPattern = NULL;
		if (mPin != -1)
		digitalWrite(mPin,HIGH);
	}


	// set the blink pattern on the passed pin
	void set(uint8_t* pPattern, uint8_t pPatternLength) {
		mPattern = pPattern;
		mPatternLen = pPatternLength;
		mSeq = 0;
		oneTime = false;
	}
	
	void setOk() {
		static uint8_t pattern[1] = {0b00000000};
		set(pattern, sizeof(pattern));
	}
	void setError() {
		static uint8_t pattern[2] = {0b11001100, 0b00110011};
		set(pattern, sizeof(pattern));
	}
	void setLowBattError() {
		static uint8_t pattern[3] = {0b1110001, 0b11000000,0b00000000};
		set(pattern, sizeof(pattern));
	}

	void assignPattern() {pendingLoop = true;};

	void setOneTime(uint8_t* pPattern, uint8_t pPatternLength) {
		set(pPattern,pPatternLength);
		oneTime = true;
	}

	void loop(uint32_t now) {
		if (pendingLoop) {
			pendingLoop = false;
		}

		uint32_t passed_ms;
		if ((timer.isDue_ms(mDuration,passed_ms, now))) {
			if (mPattern != NULL) {
				uint8_t pos,bitpos;
				pos = mSeq / 8;
				bitpos = 7- (mSeq & 0x07);
				if ((mPattern[pos] & _BV(bitpos)) > 0) {
					analogWrite(mPin,255);
				}
				else {
					analogWrite(mPin,0);
				}

				mSeq++;
				if ((mSeq >= (mPatternLen)*8)) {
					mSeq = 0;
					if (oneTime)
						mPattern = NULL;
				}
			} else {
				analogWrite(mPin,255);
			}
		}
	}


	uint8_t mSeq;			// current position within the blink pattern
	int8_t mPin;			// pin to be used
	uint8_t* mPattern;		// blink pattern, passed in set()
	uint8_t  mPatternLen;	// length of the pattern  = sizeof(*mPattern)
	TimePassedBy timer;		// timer for checking passed time
	boolean oneTime;
	uint8_t mDuration;
	bool pendingLoop = false;
	uint8_t state = 255;
};

extern PatternBlinker blinker;													                           
extern ErrorPatternBlinker errorBlinker;		