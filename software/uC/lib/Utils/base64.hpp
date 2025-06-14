#include <Arduino.h>

static const int encodedStringLength = 6*6;
// returns a string of 36 bytes
extern void encodeBase64 (float f[6], char encodedStr[]);
// reverse encoding
extern void decodeBase64(const char s[], float result[6]);