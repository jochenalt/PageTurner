#include <base64.hpp>
#include <utils.hpp>

static const char base64CharacterSet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789()";


void encodeBase64 (uint32_t buffer[], uint16_t bufferSize, char encodedStr[]) {
    int resultIdx = 0;

    for (int i = 0;i<bufferSize;i++) {
        uint32_t x = buffer[i];

        // convert in 6 bits, makes it 6 bytes for a 4 byte float
        for (int j = 0;j<6;j++) {
          uint32_t masked = (x & 0x3F);
          char c = base64CharacterSet[masked];
          encodedStr[resultIdx++] = c;
          x = x >> 6;
        } 
    }
}


bool decodeBase64(const char* encodedStr, uint16_t encoderStrLen, uint32_t result[], uint16_t &resultLen) {
  if (encoderStrLen % 6 != 0)
    return false;

  for (unsigned  i = 0;i< encoderStrLen;i = i + 6) {
    uint32_t x = 0;
    for (int j = 5;j>=0;j--) {
        int idx = -1;
        for (unsigned k = 0;k<sizeof(base64CharacterSet);k++) {
            if (base64CharacterSet[k] == encodedStr[i+j]) {
                idx = k;
                break;
            };
        }
      if (idx >=0) {
        x = x << 6;
        x = x | (idx & 0x3F);
      } else {
        return false;
      }
    }
    result[i/6] = x;
  }
  resultLen = encoderStrLen/6;
  return true;
}


float itof(uint32_t i) {
  union thingtype {
     uint32_t i;
     float f;
  } ;
  thingtype thing;
  thing.i = i;
  return thing.f;
}

uint32_t  ftoi(float f) {
  union thingtype {
     uint32_t i;
     float f;
  } ;
  thingtype thing;
  thing.f = f;
  return thing.i;
}

void encodeBase64 (float f[6], char encodedStr[]) {

  int resultIdx = 0;
  // take 6 floats
  for (int i = 0;i<6;i++) {
    uint32_t x = ftoi(f[i]); 

    // convert in 6 bits, makes it 6 bytes for a 4 byte float
    for (int j = 0;j<6;j++) {
      uint32_t masked = (x & 0x3F);
      char c = base64CharacterSet[masked];
      encodedStr[resultIdx++] = c;
      // println("floatsToStr: j=%i x=%ul mask=%i c=%c",j, x,masked, c);
      x = x >> 6;
    } 
    // println("floatsToStr: f=%f x=%ul ",thing.f, thing.i);

  }
  // println("floatsToStr: s=%s",s.c_str());
}


void decodeBase64(const char* encodedStr, float result[6]) {


  for (unsigned  i = 0;i< encodedStringLength;i = i + 6) {
    uint32_t x = 0;
    for (int j = 5;j>=0;j--) {
        int idx = -1;
        for (unsigned k = 0;k<sizeof(base64CharacterSet);k++) {
            if (base64CharacterSet[k] == encodedStr[i+j]) {
                idx = k;
                break;
            };
        }
      if (idx >=0) {
        x = x << 6;
        x = x | (idx & 0x3F);
      } else {
        printlnBase("error in base64 decoding %s",encodedStr);
      }
      // println("strToFloat: i=%i j=%i idx=%i c=%c x=%ul", i,j, idx, s[i+j], x);
    }
    result[i/6] = itof(x);

  }
}