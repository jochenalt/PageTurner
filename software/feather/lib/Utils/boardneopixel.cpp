#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "boardneopixel.h"
#include "constants.h"

Adafruit_NeoPixel pixel(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
PixelModeType pixelMode  = PIX_NO_MODE;


void initNeoPixel() {
    pixel.begin();
    pixel.setPixelColor(0, 0, 0, 255); // Blue
    pixel.setBrightness(255);
    pixel.show(); 
}

void setPixelColor(uint16_t brightness,uint16_t r, uint16_t g, uint16_t b) {
    static uint16_t lastBrightness, lastR,lastG,lastB;
    if ((brightness != lastBrightness) || 
        (r != lastR) || 
        (g != lastG) || 
        (b != lastB)) { 
        println("neopixel  %i %i %i",(r*brightness)/256,(g*brightness)/256,(b*brightness)/256);
        pixel.setPixelColor(0, (r*brightness)/256,(g*brightness)/256,(b*brightness)/256);
        pixel.show();
        lastBrightness = brightness;
        lastR = r;
        lastB = b;
        lastG = g;
    }
}

void setNeoPixelMode(PixelModeType mode) {
    pixelMode = mode;
}

void loopNeoPixel() {
    uint32_t now = millis();
    static uint32_t lastUpdateTime = millis();
    static uint32_t cycleStart = lastUpdateTime;
    if (now - lastUpdateTime < 50)
        return;
    lastUpdateTime = now;

    switch(pixelMode) {
        case PIX_PRODUCTION_MODE: {
            const static int32_t cycleLength = 5000;                        // [ms]
            uint32_t inCyclePercent =(now - cycleStart)/(cycleLength/100);  // [0..100]

            if (inCyclePercent <= 2) 
                setPixelColor(3, 255,0,0);
            else 
                setPixelColor(0, 255,0,0);

            pixel.show();


            if (inCyclePercent >= 100)
                cycleStart = now;
            
            break;
        }
        case PIX_NO_MODE: {
            setPixelColor(0,0,0,0);
            pixel.show();

            break;
        }
        default:
            break;
    }
}