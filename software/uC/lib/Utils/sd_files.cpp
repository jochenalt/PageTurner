#include "sd_files.h"


static size_t labelCounters[8] = {0};
bool sd_card_present = false;
size_t no_of_labels = 0;

void save_wav_file(const char* label, uint16_t label_no, const int16_t buf[], size_t samples) {
  if (!sd_card_present) {    
    return;
  }

  // build filename
  String fname = "/" + String(label) + "." + String(labelCounters[label_no]) + ".wav";
  File wf = SD.open(fname.c_str(), FILE_WRITE);
  if (!wf) {
    println(" cannot open %s", fname.c_str());
    return;
  }

  // WAV header
  uint32_t dataBytes = samples * 2;
  uint32_t fileSize  = 36 + dataBytes;
  // RIFF
  wf.write("RIFF", 4);
  wf.write((uint8_t*)&fileSize, 4);
  wf.write("WAVEfmt ", 8);
  uint32_t fmtLen = 16, audioFormat = 1, numChannels = 1,
           sampleRate = OUTPUT_SAMPLE_RATE, byteRate = OUTPUT_SAMPLE_RATE*2,
           blockAlign = 2, bitsPerSample = 16;
  wf.write((uint8_t*)&fmtLen, 4);
  wf.write((uint8_t*)&audioFormat,   2);
  wf.write((uint8_t*)&numChannels,   2);
  wf.write((uint8_t*)&sampleRate,    4);
  wf.write((uint8_t*)&byteRate,      4);
  wf.write((uint8_t*)&blockAlign,    2);
  wf.write((uint8_t*)&bitsPerSample, 2);
  wf.write("data", 4);
  wf.write((uint8_t*)&dataBytes, 4);
  // write PCM data
  wf.write((uint8_t*)buf, dataBytes);
  wf.close();
  println("save %s", fname.c_str()); 
}


// scan SD card and find the next number file
int identifyNextFileID(const char* label) {
  // find next index for this label
  size_t idx = 0;
  // scan existing files once
  File root = SD.open("/");
  while (auto f = root.openNextFile()) {
    String name = f.name();
    if (name.startsWith(String(label) + ".") && name.endsWith(".wav")) {
      // extract number between “.” and “.wav”
      int dot = name.indexOf('.');
      int dot2 = name.lastIndexOf('.');
      size_t n = name.substring(dot+1, dot2).toInt();
      idx = max(idx, n+1);
    }
    f.close();
  }
  root.close();

  // return next free number
  return idx + 1;
}

void init_sd_files(size_t nol, const char* categories[]) {

    no_of_labels = nol;
    if (no_of_labels > sizeof(labelCounters)) {
        println("category array too small");
        return;
    }

    // initialise SD card we use to store wrong inference results 
    if (!SD.begin(SDCARD_CS_PIN)) {
        println("No SD card present, ""bad AI"" not functional");
        sd_card_present  = false;
    } else {
        bool no_file = true;
        for (size_t i = 0;i<no_of_labels;i++) {
            int no_of_files = identifyNextFileID(categories[i]);
            labelCounters[i] = no_of_files;
            if (no_of_files > 0) {
            
            if (no_file)
                println("SD card:");

            no_file = false;

            println("%i bad AI files of label %s",no_of_files, categories[labelCounters[i]]);
            }
        }
    }
}
