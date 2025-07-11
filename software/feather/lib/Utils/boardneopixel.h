#pragma once


enum PixelModeType { PIX_PRODUCTION_MODE, PIX_NO_MODE };

void loopNeoPixel();
void setNeoPixelMode(PixelModeType mode);
void initNeoPixel();