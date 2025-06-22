
#include "Metronome.h"
#include "constants.h"

extern AudioPlayMemory clickPlayer;

Metronome::Metronome()
  : enabled(false), periodMicros(0), lastBeatMicros(0) {
}

void Metronome::init() {
  pinMode(METRONOME_LED_PIN, OUTPUT);
}

void Metronome::setTempo(uint16_t bpm) {
  if (bpm == 0) return;
  periodMicros = 60000000UL / bpm;
}

uint16_t Metronome::getTempo() {
  return 60000000UL / periodMicros;
}

void Metronome::turn(bool onOff) {
  enabled = onOff;
  lastBeatMicros = micros();
}

bool Metronome::isOn() {
  return enabled;
}

void Metronome::update() {
  if (!enabled || periodMicros == 0) return;
  uint32_t now = micros();
  if (now - lastBeatMicros >= periodMicros) {
    lastBeatMicros += periodMicros;
    playClick();
    digitalWrite(METRONOME_LED_PIN, HIGH);
  }
  // Turn off LED after sample ends
  if (digitalRead(METRONOME_LED_PIN) && (now - lastBeatMicros) >= 100 * 1000) {
    digitalWrite(METRONOME_LED_PIN, LOW);
  }
}

void Metronome::playClick() {
  clickPlayer.play(FranzMetronomeClickSample);
}
